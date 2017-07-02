//
//  Copyright (c) 2010 Paul Nicholson
//  All rights reserved.
//  
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions
//  are met:
//  1. Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//  
//  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
//  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
//  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
//  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
//  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
//  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
//  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "config.h"
#include "vtport.h"
#include "vtlib.h"

#include <termios.h>

///////////////////////////////////////////////////////////////////////////////
//  Globals                                                                  //
///////////////////////////////////////////////////////////////////////////////

static int dh = -1;   // Device handle
static char *device = "/dev/ttyUSB0"; // Default device, overide with -d option

static double rfgain = 10;                                  // Set by -g option
static int ifgain = 0;                
static double frequency = 0;                                 // Tuner frequency
static int AFLAG = FALSE;                              // Set TRUE by -a option
static char *bname;                               // Output buffer or fifo name
static VTFILE *vtfile;                               // Handle to output stream

static double srcal = 1.0;               // Sample rate calibration coefficient
static unsigned int sample_rate = 196078;                // Nominal sample rate
static int chans = 2;                              // Number of output channels
static timestamp timebase = timestamp_ZERO;            // Base of our timestamp
static uint64_t ntb = 0;             // Number of frames since timebase was set

static uint64_t nout = 0;                            // Number of output frames

static double tadj = 0;     // Timing adjustment to be applied per output block
static timestamp Tfalse = timestamp_ZERO;    // False timestamp, from -T option
static int UFLAG = FALSE;                            // Set TRUE with -u option
static int QFLAG = FALSE;                         // -q option: invert Q signal

///////////////////////////////////////////////////////////////////////////////
//  Timestamping                                                             //
///////////////////////////////////////////////////////////////////////////////

#define OFFSET_LIMIT 20e-3 // Reset if more than this many seconds timing error
#define PRE_RUN_SECS 20                         // Number of seconds of pre-run

static timestamp reftime = timestamp_ZERO;               // Reference timestamp
static uint32_t nft = 0;         // Number of frames since reftime last updated

// State variable for timestamping:-
#define STATE_RESET  0    // Transient state - reset everything and begin again
#define STATE_INIT   1    // Pre-run to settle down and get rough sample rate
#define STATE_SETUP  2    // Polish the sample rate
#define STATE_RUN    3    // Normal running

static int state = STATE_INIT;

//
//  Called immediately after a buffer of 'q' frames is received from the
//  device.
//
static void timestamping( int q)
{
   static int n = 0;

   #define AJF_RUN 0.05   // Rate error adjustment factor
   #define AJF_SETUP 0.15
   #define MAX_DEFER 3

   nft += q;

   if( state == STATE_RESET)  
   {
      // When reading from stdin, or if -u option is given, don't bother with
      // timing, go straight to the 'run' state.
      if( UFLAG)
      {
         state = STATE_RUN;
         timebase = reftime =
                          !timestamp_is_ZERO( Tfalse) ? Tfalse : VT_rtc_time();
         nft = 0;
         ntb = nout = 0;
         return;
      }

      reftime = VT_rtc_time();
      nft = 0;
      state = STATE_INIT;
   }

   if( state == STATE_INIT) // Initial pre-run to let things settle down
   {
      if( nft < PRE_RUN_SECS * sample_rate) return;

      // End of pre-run: just get a rough estimate of sample rate
      state = STATE_SETUP;
      n = 0;

      srcal =
         (nft / timestamp_diff(VT_rtc_time(), reftime)) / (double) sample_rate;
      timebase = reftime = VT_rtc_time();
      ntb = nout = 0;
      nft = 0;

      VT_report( 1, "pre-run complete %.2f", srcal * sample_rate);
      return;
   }

   // No sample rate or timebase calibration when using -u
   if( UFLAG) return;

   // Only revise the timing roughly every 10 seconds
   if( nft < sample_rate * 10) return;

   // Expected time to read nft samples, based on the current srcal
   double expected_interval = nft/(double)(srcal * sample_rate);

   // Compare with RTC to calculate a raw sample rate error
   timestamp now = VT_rtc_time();
   double actual_interval = timestamp_diff( now, reftime);

   double err = (expected_interval - actual_interval)/actual_interval;
   double r = fabs( err  * vtfile->bsize/ 2.5); 

   double raw_rate = nft / actual_interval;

   static int defer = 0;

   if( state == STATE_SETUP) // Initial stabilising of sample rate loop
   {
      // Update smoothed estimate of sample rate calibration factor
      srcal *= 1 + err * AJF_SETUP;

      reftime = now; nft = 0;

      VT_report( 1, "setup %+.3e sr %.2f rr %.1f n=%d r=%.2f",
                    err, sample_rate * srcal, raw_rate, n, r);
      if( r > 20)
      {
         VT_report( 0, "rate error too large, resetting");
         state = STATE_RESET;
      }
      else
      if( r < 0.1 && n >= 0) n += 3;
      else
      if( r < 0.5 && n >= 0) n += 2;
      else
      if( r < 1 && n >= 0) n++;
      else
      if( r < 1) n = 0;
      else 
         n--;

      if( n < -10)
      {
         VT_report( 0, "persistent drift, resetting");
         state = STATE_RESET;
      }
      if( n >= 10)
      {
         // Success.  Timebase has settled enough to start running data.
         state = STATE_RUN;
         timebase = reftime;
         ntb = nout = 0;
      }
   }
   else
   if( state == STATE_RUN)   // Normal operation
   {
      // Allow a couple of bad readings to be ignored - they usually come
      // good again.  Occurs when we didn't get scheduled promptly enough.
      if( r > 0.5 && defer++ <= MAX_DEFER) return;

      // Update smoothed estimate of sample rate calibration factor
      srcal *= 1 + err * AJF_RUN;

      timebase = timestamp_add( timebase, expected_interval); ntb += nft;
      reftime = now; nft = 0;
      defer = 0;
      double offset =
                  timestamp_diff( timebase, reftime); // Timing offset, seconds

      // Timing offset is slewed towards zero by adjusting the timebase
      // by tadj seconds per output data block.
      tadj = offset / (10 * sample_rate/vtfile->bsize);

      // Limit the timebase adjustment to 0.25 samples per block
      if( tadj > 0.25/sample_rate) tadj = 0.25/sample_rate;
      if( tadj < -0.25/sample_rate) tadj = -0.25/sample_rate;

      VT_report( 1, "run %+.3e sr %.3f tadj %+.3e rr %.1f offs %+.3fmS r=%.2f",
                 err, sample_rate * srcal, tadj, raw_rate, 1000 * offset, r);
 
      if( fabs( offset) > OFFSET_LIMIT)
      {
         // Either the R2832U sample rate or the system clock has stepped
         // or is drifting too fast for the control loops to correct.
         // Reset everything and start again.
         VT_report( 0, "timebase error %.3f mS, resetting", 1000 * offset);
         state = STATE_RESET;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
//  SDR-IQ Interface                                                         //
///////////////////////////////////////////////////////////////////////////////

// Wait for len bytes from the SDR-IQ
static void readn( uint8_t *buff, int len)
{
   int nd = 0, nr;
   while( nd < len)
   {
      if( (nr=read( dh, buff + nd, len - nd)) <= 0)
         VT_bailout( "SDR-IQ read failed: %s", strerror( errno));

      nd += nr;
   }
}

static void writen( uint8_t *buff, int len)
{
   if( write( dh, buff, len) != len)
      VT_bailout( "SDR-IQ write failed: %s", strerror( errno));
}

static uint8_t rxdata[8192];
static int rxlen;
static int rxtype;

static void read_msg( void)
{
   uint8_t hdr[2]; readn( hdr, 2);   // Read 2 byte header

   rxtype = (hdr[1] & 0xe0) >> 5;
   rxlen = ((hdr[1] & 0x1f) << 8) + hdr[0];
   if( rxlen == 0) rxlen = 8192;    // Special case for data items
   else rxlen -= 2;

   readn( rxdata, rxlen);   // Read remainder
}


// Device control transaction
static void transact( uint8_t *cmd, int cmdlen)
{
   writen( cmd, cmdlen);

   while( 1)
   {
      read_msg();

      if( !rxtype) break;
      VT_report( 1, "drop cntl response type %02X", rxtype);
   }

   uint16_t rxcmd = (rxdata[1] << 8) | rxdata[0];
   VT_report( 2, "cntl type %02X cmd %04X response %d", rxtype, rxcmd, rxlen);
}

static void setup_device( void)
{
   if( (dh = open( device, O_RDWR)) < 0)
      VT_bailout( "cannot open %s: %s", device, strerror( errno));

   //
   // Check comms: read target name and serial
   //

   uint8_t msg1[] = { 0x04, 0x20, 0x01, 0x00};  
   transact( msg1, 4);
   rxdata[rxlen] = 0;
   VT_report( 1, "device name [%s]", rxdata+2);

   uint8_t msg2[] = { 0x04, 0x20, 0x02, 0x00};
   transact( msg2, 4);
   rxdata[rxlen] = 0;
   VT_report( 1, "device serial [%s]", rxdata + 2);

   //
   // Interface version
   //

   uint8_t msg9[] = { 0x04, 0x20, 0x03, 0x00 };
   transact( msg9, 4);
   if( rxlen != 4) VT_report( 1, "interface version request failed %d", rxlen);
   else VT_report( 1, "interface version %.2f",
               (*(uint16_t *)(rxdata + 2))/100.0);

   //
   // Firmware version
   //

   uint8_t msg7[] = { 0x05, 0x20, 0x04, 0x00, 0x01 };
   transact( msg7, 5);
   if( rxlen != 5) VT_report( 1, "version request failed %d", rxlen);
   else VT_report( 1, "firmware version %.1f",
           (*(uint16_t *)(rxdata + 3))/100.0);

   //
   // Set sample rate
   //

   uint8_t msg8[] = { 0x09, 0x00, 0xb8, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00 };
   *(uint32_t *)(msg8 + 5) = sample_rate;
   transact( msg8, 9);
   if( rxlen != 7) VT_bailout( "cannot set sample rate");

   //
   // Check device status
   //

   uint8_t msg3[] = { 0x04, 0x20, 0x05, 0x00};
   transact( msg3, 4);
   int i;
   for( i=2; i<rxlen; i++)
      switch( rxdata[i])
      {
         case 0x0b: VT_report( 1, "SDR-IQ: idle"); break;
         case 0x0c: VT_report( 1, "SDR-IQ: capturing"); break;
         case 0x20: VT_report( 1, "SDR-IQ: A/D overload"); break;
         default: VT_report( 1, "SDR-IQ: status %02X", rxdata[i]); break;
      }

   //
   // Set receiver frequency
   //

   uint8_t msg4[] = { 0x0a, 0x00, 0x20, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x01 };

   *(uint32_t *) (msg4+5) = round( frequency);
   transact( msg4, 10);
   if( rxlen != 8) VT_bailout( "cannot set frequency");
   VT_report( 1, "frequency %.0f Hz", frequency);

   //
   // Set manual gain mode
   //

   int gv =  round( pow( 10, rfgain/20)/0.394637);
   if( gv < 1) gv = 1;    // Dont set zero gain
   if( gv > 127) gv = 127;  // Max gain

   VT_report( 1, "RF gain setting %d, actual gain %.1f dB",
                  gv, 20 * log10( 0.394637 * gv));
   uint8_t msg5[] = {0x06, 0x00, 0x38, 0x00, 0x01, 0x00 };
   msg5[5] = gv;
   if( AFLAG)   // Attenuator requested?
   {
      msg5[5] |= 0x80;
      VT_report( 1, "Attenuator -10dB: ON");
   }
   transact( msg5, 6);
   if( rxlen != 4) VT_bailout( "cannot set RF gain");

   //
   // Set IF gain
   //

   uint8_t msg10[] = { 0x06, 0x00, 0x40, 0x00, 0x00, 0x00 };
   msg10[5] = ifgain;
   transact( msg10, 6);
   if( rxlen != 4) VT_bailout( "cannot set IF gain");
   VT_report( 1, "IF gain: %d dB", ifgain);

   //
   // Set run state
   //

   uint8_t msg6[] = { 0x08, 0x00, 0x18, 0x00, 0x81, 0x02, 0x00, 0x01 };
   transact( msg6, 8);
   if( rxlen != 6) VT_bailout( "cannot set running state");

   VT_report( 1, "SDR-IQ: running");
}

static void read_data( void)
{
   while( 1)
   {
      read_msg();

      if( rxlen == 8192) break;

      uint16_t item = (rxdata[1] << 8) | rxdata[0];
      if( item == 0x0005 && rxdata[2] == 0x20)
         VT_report( 1, "A/D converter overload");
      else
         VT_report( 1, "unsolicited type %02X item %04X len %d",
                        rxtype, item, rxlen);
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Main Loop                                                                //
///////////////////////////////////////////////////////////////////////////////

static void output_block( int16_t *buff, int nframes)
{
   if( state != STATE_RUN) return; // Not yet in running state - discard output

   while( nframes > 0)
   {
      // Prepare the next output block, if necessary
      if( !vtfile->nfb)
      {
         timebase  = timestamp_add( timebase, -tadj);
         VT_set_timebase( vtfile,
             timestamp_add( timebase, (nout - ntb)/(srcal * sample_rate)), srcal);
         VT_next_write( vtfile);
      }

      // Number of frames to fit in this block
      int n = vtfile->bsize - vtfile->nfb;
      if( n > nframes) n = nframes;

      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_INT1)
      {
         char *d = (char *) VT_data_p( vtfile) + vtfile->nfb * chans;
         int i;
         if( chans == 1)
            for( i=0; i<n; i++) d[i] = buff[i*2]/256;
         else
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = buff[i]/256;
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = buff[i]/256;
            for( i=1; i<n*2; i += 2) d[i] = -buff[i]/256;
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_INT2)
      {
         int16_t *d = (int16_t *) VT_data_p( vtfile) + vtfile->nfb * chans;
         int i;
         if( chans == 1)
            for( i=0; i<n; i++) d[i] = buff[i*2];
         else
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = buff[i];
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = buff[i];
            for( i=1; i<n*2; i += 2) d[i] = -buff[i];
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_INT4)
      {
         int32_t *d = (int32_t *) VT_data_p( vtfile) + vtfile->nfb * chans;
         int i;
         if( chans == 1)
            for( i=0; i<n; i++) d[i] = 256 * 256 * (int) buff[i*2];
         else
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = 256 * 256 * (int) buff[i];
         else
         {
            for( i=0; i<n*2; i += 2)
               d[i] = 256 * 256 * (int)buff[i];
            for( i=1; i<n*2; i += 2)
               d[i] = -256 * 256 * (int)buff[i];
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_FLOAT4)
      {
         float *d = (float *) VT_data_p( vtfile) + vtfile->nfb * chans;
         int i;
         if( chans == 1)
            for( i=0; i<n; i++) d[i] = buff[i*2]/(float)INT16_MAX;
         else
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = buff[i]/(float)INT16_MAX;
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = buff[i]/(float)INT16_MAX;
            for( i=1; i<n*2; i += 2) d[i] = -buff[i]/(float)INT16_MAX;
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_FLOAT8)
      {
         double *d = (double *) VT_data_p( vtfile) + vtfile->nfb * chans;
         int i;
         if( chans == 1)
            for( i=0; i<n; i++) d[i] = buff[i*2]/(float)INT16_MAX;
         else
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = buff[i]/(float)INT16_MAX;
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = buff[i]/(float)INT16_MAX;
            for( i=1; i<n*2; i += 2) d[i] = -buff[i]/(float)INT16_MAX;
         }
      }
      else VT_bailout( "unhandled mode %08X", vtfile->flags);


      buff += n * 2;
      vtfile->nfb += n;
      vtfile->nft += n;
      nframes -= n;
      nout += n;
      // Release the output block if full
      if( vtfile->nfb == vtfile->bsize) VT_release( vtfile);
   }
}

static void run( void)
{
   while( 1)
   {
      read_data();   // Read 2048 I/Q pairs = 8192 bytes

      timestamping( 2048);
      output_block( (int16_t *) rxdata, 2048);
   }

   VT_release( vtfile);
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

// Set scheduling priority to the minimum SCHED_FIFO value.
static void set_scheduling( void)
{
   #ifndef HAVE_SCHED_SETSCHEDULER
      int pri = -15;
      if( setpriority( PRIO_PROCESS, getpid(), pri))
      {
         VT_report( 0, "cannot set scheduling priority: %s",
                           strerror( errno));
         return;
      }
   #else
      int pri = sched_get_priority_max( SCHED_FIFO);

      struct sched_param pa;
      pa.sched_priority = pri;
      if( sched_setscheduler( 0, SCHED_FIFO, &pa))
      {
         VT_report( 0, "cannot set scheduling priority: %s",
                          strerror( errno));
         return;
      }
   #endif

   VT_report( 1, "using SCHED_FIFO priority %d", pri);

   if( mlockall( MCL_CURRENT | MCL_FUTURE) < 0)
      VT_report( 0, "unable to lock memory: %s", strerror( errno));
}

static void usage( void)
{
   fprintf( stderr,
       "usage:    vtsdriq [options] buffer_name\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify log file\n"
       "  -d device Device (default /dev/ttyUSB0)\n"
       "  -r rate   Sample rate (default 196078)\n"
       "            Rate must be one of\n"
       "             8138, 16276, 37793, 55556\n"
       "             111111, 158730, 196078\n"
       "\n"
       "  -F hertz  Frequency, Hertz\n"
       "  -g gain   RF gain in dB (default 10)\n"
       "            Valid range -8 to +34\n"
       "  -a        Insert -10dB attenuator\n"
       "  -i        IF gain in dB (default 0)\n"
       "            Allowed values 0, 6, 12, 18, 24\n"
       "\n"
       "  -q        Invert the Q signal\n"
       "  -u        No sample rate tracking\n"
       "  -T stamp  Start time when using -u\n"
       "            (default is to use system clock)\n"
       "\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtsdriq");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBd:r:g:F:T:L:uqai:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'd')
      {
         device = strdup( optarg);
      }
      else
      if( c == 'r')
      {
         sample_rate = atoi( optarg);
         srcal = atof( optarg) / sample_rate;
      }
      else
      if( c == 'g') rfgain = atof( optarg);
      else
      if( c == 'i') ifgain = atoi( optarg);
      else
      if( c == 'a') AFLAG++;
      else
      if( c == 'T') Tfalse = VT_parse_timestamp( optarg);
      else
      if( c == 'u') UFLAG = TRUE;
      else
      if( c == 'F') frequency = atof( optarg);
      else
      if( c == 'q') QFLAG = TRUE;
      else
      if( c == -1) break;
      else
         usage();
   }

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");
 
   if( sample_rate != 8138 && sample_rate != 16276 &&
       sample_rate != 37793 && sample_rate != 55556 &&
       sample_rate != 111111 && sample_rate != 158730 &&
       sample_rate != 196078) VT_bailout( "invalid sample rate");

   if( ifgain != 0 && ifgain != 6 && ifgain != 12 &&
       ifgain !=  18 && ifgain != 24)
      VT_bailout( "invalid setting for IF gain");

   if( frequency < 0 || frequency > 33.333333e6)
      VT_bailout( "invalid or missing frequency");

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDOUT : 0;
      VT_daemonise( flags);
   }

   VT_report( 1, "buffer name: [%s]", bname);

   if( frequency == 0)
   {
      chans = 1;
      VT_report( 1, "using single output channel");
   }

   vtfile = VT_open_output( bname, chans, 1, sample_rate);
   if( !vtfile) VT_bailout( "cannot create buffer: %s", VT_error);

   setup_device();
   set_scheduling();

   state = STATE_RESET;
   run();
   return 0;
}

