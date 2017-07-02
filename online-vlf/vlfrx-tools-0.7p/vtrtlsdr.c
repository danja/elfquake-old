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

#include <rtl-sdr.h>

///////////////////////////////////////////////////////////////////////////////
//  Globals                                                                  //
///////////////////////////////////////////////////////////////////////////////

static int devidx = 0;                                      // RTL device index
static char *bname;                               // Output buffer or fifo name
static VTFILE *vtfile;                               // Handle to output stream

static double srcal = 1.0;               // Sample rate calibration coefficient
static unsigned int sample_rate = 0;                     // Nominal sample rate

static double frequency = 0;                                 // Tuner frequency
static int gain = 0;                 // Tuner gain, units of 0.1dB, 0 = use AGC

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
//  RTL SDR                                                                  //
///////////////////////////////////////////////////////////////////////////////

#include "rtl-sdr.h"

#define DEFAULT_BUF_LENGTH              (16 * 16384)
#define MINIMAL_BUF_LENGTH              512
#define MAXIMAL_BUF_LENGTH              (256 * 16384)

static rtlsdr_dev_t *dev = NULL;

static void open_device( void)
{
   int device_count = rtlsdr_get_device_count();
   if (!device_count) VT_bailout( "no RTLSDR devices found");

   if( devidx < 0 || devidx >= device_count)
      VT_bailout( "invalid device number");

   VT_report( 1, "RTL device %d: %s\n",
                devidx, rtlsdr_get_device_name( devidx));

   if( rtlsdr_open(&dev, devidx) < 0) VT_bailout( "RTL device open failed");
}

static void setup_rtlsdr( int nframes)
{
   int rtl_blk_size = nframes * 2;

   if( rtl_blk_size < MINIMAL_BUF_LENGTH ||
       rtl_blk_size > MAXIMAL_BUF_LENGTH)
         VT_bailout( "cannot set buffer size %d", rtl_blk_size);

   open_device();

   if( rtlsdr_set_sample_rate(dev, sample_rate) < 0)
      VT_bailout( "cannot set RTL sample rate");

   int32_t actual_rate = rtlsdr_get_sample_rate( dev);
   if( actual_rate != sample_rate)
      VT_bailout( "cannot set rate, actual rate %d", actual_rate);

   if( rtlsdr_set_center_freq(dev, (uint32_t) frequency) < 0)
      VT_bailout( "cannot set RTL frequency");

   VT_report( 1, "frequency %.0f Hz", frequency);
   uint32_t actual_freq = rtlsdr_get_center_freq( dev);
   if( !actual_freq) VT_bailout( "error setting frequency");
   VT_report( 1, "actual freq %d Hz", actual_freq);

   VT_report( 1, "frequency correction: %d ppm",
                   rtlsdr_get_freq_correction( dev));
   
   if( !gain)   // If -g gain not given, then turn on AGC
   {
      if( rtlsdr_set_tuner_gain_mode( dev, 0) < 0)
         VT_bailout( "cannot set AGC on");
   }
   else  // Manual fixed gain
   {
      if( rtlsdr_set_tuner_gain_mode( dev, 1) < 0)
         VT_bailout( "cannot set manual gain mode");

      if( rtlsdr_set_tuner_gain( dev, gain) < 0)
         VT_bailout( "cannot set gain");

      VT_report( 1, "gain set: %.1f dB", gain/10.0);
   }

   if( rtlsdr_reset_buffer( dev) < 0) VT_bailout( "cannot reset device");
}

static void list_devices( void)
{
   int i, n = rtlsdr_get_device_count();
   
   for( i = 0; i < n; i++)
      printf( "%d %s\n", i, rtlsdr_get_device_name( i));
}

static void list_gains( void)
{
   open_device();
   int i, n = rtlsdr_get_tuner_gains( dev, NULL);
   int *list = malloc( sizeof( int) * n);
   rtlsdr_get_tuner_gains( dev, list);
   for( i=0; i<n; i++) printf( "%.1f dB\n", list[i]/10.0);
}

static int read_data( uint8_t *buff, int nframes)
{
   int rtl_blk_size = 2 * nframes;
   int n;
   if( rtlsdr_read_sync( dev, buff, rtl_blk_size, &n) < 0)
      VT_bailout( "rtl read failed");

   if( n < rtl_blk_size) VT_report( 0, "short read %d/%d", n, rtl_blk_size);
   if( n > rtl_blk_size) VT_bailout( "excess read %d", n);
   if( n % 2) VT_bailout( "odd read %d", n);

   return n/2;   // Number of frames read
}

///////////////////////////////////////////////////////////////////////////////
//  Main Loop                                                                //
///////////////////////////////////////////////////////////////////////////////

static void output_block( uint8_t *buff, int nframes)
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
         char *d = (char *) VT_data_p( vtfile) + vtfile->nfb * 2;
         int i;
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = (int)buff[i] - 128;
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = (int)buff[i] - 128;
            for( i=1; i<n*2; i += 2) d[i] = -((int)buff[i] - 128);
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_INT2)
      {
         int16_t *d = (int16_t *) VT_data_p( vtfile) + vtfile->nfb * 2;
         int i;
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = 128 * ((int)buff[i] - 128);
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = 128 * ((int)buff[i] - 128);
            for( i=1; i<n*2; i += 2) d[i] = -128 * ((int)buff[i] - 128);
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_INT4)
      {
         int32_t *d = (int32_t *) VT_data_p( vtfile) + vtfile->nfb * 2;
         int i;
         if( !QFLAG)
            for( i=0; i<n*2; i++)
               d[i] = 256 * 256 * 256 * ((int)buff[i] - 128);
         else
         {
            for( i=0; i<n*2; i += 2)
               d[i] = 256 * 256 * 256 * ((int)buff[i] - 128);
            for( i=1; i<n*2; i += 2)
               d[i] = -256 * 256 * 256 * ((int)buff[i] - 128);
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_FLOAT4)
      {
         float *d = (float *) VT_data_p( vtfile) + vtfile->nfb * 2;
         int i;
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = ((int)buff[i] - 128)/128.0;
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = ((int)buff[i] - 128)/128.0;
            for( i=1; i<n*2; i += 2) d[i] = -((int)buff[i] - 128)/128.0;
         }
      }
      else
      if( (vtfile->flags & VTFLAG_FMTMASK) == VTFLAG_FLOAT8)
      {
         double *d = (double *) VT_data_p( vtfile) + vtfile->nfb * 2;
         int i;
         if( !QFLAG)
            for( i=0; i<n*2; i++) d[i] = ((int)buff[i] - 128)/128.0;
         else
         {
            for( i=0; i<n*2; i += 2) d[i] = ((int)buff[i] - 128)/128.0;
            for( i=1; i<n*2; i += 2) d[i] = -((int)buff[i] - 128)/128.0;
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
   uint8_t *buff = VT_malloc( vtfile->bsize * 2);

   while( 1) 
   {
      int q = read_data( buff, vtfile->bsize);
      if( q <= 0) break;

      timestamping( q);
      output_block( buff, q);
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
       "usage:    vtrtlsdr [options] buffer_name\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify log file\n"
       "  -d device Device number (default 0)\n"
       "  -d ?      List available devices\n"
       "  -r rate   Sample rate 1000000 to 3200000\n"
       "\n"
       "  -F hertz  Tuner frequency, Hertz\n"
       "  -g gain   Tuner gain (dB, default 0 = auto)\n"
       "  -g ?      List available gain settings\n"
       "  -q        Invert the Q signal\n"
       "\n"
       "  -u        No sample rate tracking\n"
       "  -T stamp  Start time when using -u\n"
       "            (default is to use system clock)\n"
       "\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtrtlsdr");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBd:r:g:F:T:L:uq?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'd')
      {
         if( !strcmp( optarg, "?"))
         {
            list_devices();
            exit( 0);
         }

         devidx = atoi( optarg);
      }
      else
      if( c == 'r')
      {
         sample_rate = atoi( optarg);
         srcal = atof( optarg) / sample_rate;
      }
      else
      if( c == 'g')
      {
         if( !strcmp( optarg, "?"))
         {
            list_gains();
            exit( 0);
         }

         gain = round( atof( optarg) * 10);
      }
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

   if( sample_rate <= 0)
      VT_bailout( "invalid or missing sample rate, needs -r");
   if( sample_rate > 3200000) VT_bailout( "max sample rate 3200000");
   if( sample_rate < 1000000) VT_bailout( "min sample rate 1000000");

   if( frequency <= 0)
      VT_bailout( "invalid or missing frequency, needs -F");

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDOUT : 0;
      VT_daemonise( flags);
   }

   VT_report( 1, "buffer name: [%s]", bname);

   vtfile = VT_open_output( bname, 2, 1, sample_rate);
   if( !vtfile) VT_bailout( "cannot create buffer: %s", VT_error);
   if( vtfile->flags & VTFLAG_INT1 == 0)
      VT_bailout( "must use i1 mode for output stream");

   setup_rtlsdr( vtfile->bsize);
   set_scheduling();

   state = STATE_RESET;
   run();
   return 0;
}

