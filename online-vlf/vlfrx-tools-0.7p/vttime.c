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

#include <fftw3.h>

static VTFILE *vtinfile, *vtoutfile;
static char *inname = NULL;
static char *outname = NULL;
static int sample_rate = 0;

static int timing_chan = 1;  // Selected input channel containing timing signal
                             // set by -c option

static int debug = FALSE;       // Set TRUE by -d option to turn on extra stuff

static double centroid_offset = 0;  // Seconds between centroid and second mark
                                    // set by mode option c=

static double pulse_width = 1e-3;        // Centroid search half-width, seconds
static int pulse_width_auto = TRUE;    // TRUE if pulse_width set automatically
static int noppm = 0;             // If TRUE, does not restrict peak/mean ratio
                                  // set by mode option noppm

static int64_t nin = 0;                                  // Input frame counter
static int state = 0;                                 // 0 = setup, 2 = running

static void (*process_timing)( timestamp, double, double) = NULL;


///////////////////////////////////////////////////////////////////////////////
//  Timebases                                                                //
///////////////////////////////////////////////////////////////////////////////

// There are three distinct timestamps handled in this program:
//
// 1/ The timestamp supplied with the input stream.  This is used only to place
//    a capture buffer around the likely position of the 'second'.

// 2/ Our determination (hopefully more accurate) of the input stream's timing.

static timestamp our_timebase = timestamp_ZERO;
static int64_t ninbase = 0;   // Input frame count to which our_timebase refers
static double our_srcal = 1.0;
static double our_dt = 0.0;

// 3/ The timestamp of the output stream, after resampling to the nominal sample
//    rate (srcal = 1.0).

static timestamp outbase = timestamp_ZERO;

static int64_t nin_prev = 0;      // nin_this from previous capture
static double previous = 0;       // The secmark_index of the previous pulse
static void reset_timebase( void)
{
   state = 0;
   nin = 0;
   ninbase = 0;
   our_srcal = 1.0;
   our_dt = 0.0;
   our_timebase = timestamp_ZERO;
   previous = 0;
   nin_prev = 0;
}

//
//  Function revise_timebase() is called after each PPS measurement.  It
//  smooths the intervals and updates our timebase.
//

static void revise_timebase( double secmark_index,
                             int64_t nin_this,
                             timestamp T_start,
                             double pmr)
{
   //
   //  Calculate pulse interval: number of samples between this and previous.
   //

   double interval = (double)(nin_this - nin_prev) + secmark_index - previous;
   previous = secmark_index;
   nin_prev = nin_this;

   // Only use the raw interval if it is reasonable.   
   if( interval > sample_rate * 1.001 ||
       interval < sample_rate * 0.999)
   {
      VT_report( 0, "wild PPS pulse interval %.6f - skipped", interval);
      return;
   }

   //
   //  Calculate corrections of the input stream's timestamp and sample rate.
   //  Both are adjusted by a fraction 1/sdfac of the error determined from
   //  the current raw PPS interval.
   //
   //  The corrections are applied later, after some median filtering.
   //

   int sdfac  = state ? 6 : 2;  // Faster correction in state 0

   // Calculate the sample rate correction
   double current_rate = sample_rate * our_srcal;
   double raw_rate_error = interval - current_rate;
   double rate_correction = raw_rate_error/sdfac;

   // Calculate the timebase correction
   double out_error = 0;   // Raw timebase offset in samples
   double time_correction = 0;

   if( ninbase)
   {
      // Expected sample number of the second mark, based on our timebase
      double expected = sample_rate * our_srcal * 
                        timestamp_diff( timestamp_round(T_start), our_timebase);
      out_error = nin_this + secmark_index - expected - ninbase;
      time_correction =  - out_error/sdfac * our_dt;
   }

   //
   //  Median filter:  Queue up the last three pairs of rate and time
   //  corrections and choose to use the median.  This is effective at
   //  removing bipolar spike pairs caused by a bad interval.
   //

   static int mi = 0;   // Load index to median queue
   static int mn = 0;   // Number of queue entries
   static struct MQ {
      double rate, time;
   } mq[3];

   if( !ninbase) mi = mn = 0;  // Flush filter after a reset

   mq[mi].rate = rate_correction;
   mq[mi].time = time_correction;
   mi = (mi + 1) % 3;
   if( mn < 3) mn++; 
   else
   {
      if( (mq[0].rate - mq[1].rate) * (mq[0].rate - mq[2].rate) <= 0)
      {
         rate_correction = mq[0].rate;
         time_correction = mq[0].time;
      }
      else
      if( (mq[1].rate - mq[0].rate) * (mq[1].rate - mq[2].rate) <= 0)
      {
         rate_correction = mq[1].rate;
         time_correction = mq[1].time;
      }
      else
      {
         rate_correction = mq[2].rate;
         time_correction = mq[2].time;
      }
   }

   // Apply sample rate correction
   double new_rate = current_rate + rate_correction;
   our_srcal = new_rate/sample_rate;
   our_dt = 1/(sample_rate * our_srcal);

   // Apply the timebase correction
   int timebase_valid = 0;
   static timestamp last_tstart = timestamp_ZERO;

   if( !ninbase)
   {
      // First time through: initialise our timebase
      last_tstart = timestamp_round( T_start);
      our_timebase = timestamp_add( last_tstart, -secmark_index * our_dt);
      ninbase = nin_this;
   }
   else
   {
      int secs = round( timestamp_diff( T_start, last_tstart));
      int nadj = round( sample_rate * our_srcal * secs);
      ninbase += nadj;
      our_timebase = timestamp_add( our_timebase, 
                                    nadj * our_dt + time_correction);

      last_tstart = timestamp_round( T_start);
      timebase_valid = 1;
   }

   double in_err = timestamp_diff( T_start, our_timebase)
                    + (ninbase - nin_this)  * our_dt;

   // Fold this raw pulse interval into our exponentially smoothed estimate
   // of the sample rate, and keep a smoothed variance. This stuff is just
   // used for reporting.

   static double smoothed_interval = 0;   // Average samples between PPS pulses
   static double smoothvar = 0;                 // Variance of the PPS interval
   #define SRTC  20        // Time constant (seconds) for sample rate smoothing
   double smfac = exp(-1.0/SRTC);               // Sample rate smoothing factor

   if( !smoothed_interval) smoothed_interval = interval;
   else smoothed_interval = smoothed_interval * smfac + interval * (1-smfac);

   double var = (interval - smoothed_interval) * (interval - smoothed_interval);
   smoothvar = smoothvar * smfac + var * (1-smfac);

   VT_report( 1, "st%d PPSpmr %.1f PPSsig %.3fuS in %.3fmS "
                    "out %7.3fuS "
                    "rate_err %5.2f inrate %.4f int %.4f", 
       state, pmr, 1e6 * sqrt(smoothvar)/sample_rate, 1e3 * in_err,
       1e6 * out_error/sample_rate,
       raw_rate_error,
       new_rate, interval);

   // If the output stream timestamp error reaches half a sample, then reset
   // everything and re-establish timing from scratch.  The rate error is
   // normally a small (< 0.05) fraction of a sample so 0.5 is pretty bad and
   // usually occurs when the soundcard sample rate or the PCs RTC is drifting
   // too fast to correct.

   if( fabs( time_correction/our_dt) > 0.5)
   {
      VT_report( 0, "input rate drifting too fast");
      if( state) reset_timebase();
      return;
   }

   // Set up the timing baseline required for output resampling.   This occurs
   // only the first time through since the output timebase is constant.
   if( !state && timebase_valid)
   {
      // Set the output to begin on the next second
      outbase = timestamp_add( timestamp_round(T_start), 1);
      VT_set_timebase( vtoutfile, outbase, 1.0);

      // Switch to run state.  The main loop will start to output samples
      // when cstamp_D reaches outbase.  outbase has been set to the next
      // second which means the main loop will discard the remainder of the
      // current second.
      state = 2;  
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Baseband PPS                                                             //
///////////////////////////////////////////////////////////////////////////////

//
//  Pulse buffer.
//

static float *cbuf = NULL;      // Capture buffer
static int cbuflen = 0;         // Capture buffer length, samples
static int cbufp = 0;           // Capture buffer load index
static int pulse_sign = 1;      // +1 or -1, polarity expected of the PPS 

static double hw = 0.45;        // Half width of capture buffer
static double vavg = 0;         // Average signal value of PPS channel
static double vmax = 0;         // Peak value of PPS channel
static int vmax_pos;            // Offset into capture buffer of peak location
static double pmr = 0;          // PPS peak to mean ratio

static int64_t nin_this = 0;    // Frame count of first frame in capture buf
static timestamp T_start = timestamp_ZERO;   // Incoming timestamp of first
                                             // frame in capture buffer

static int load_buffer( timestamp T, double in, double srcal)
{
   static int flag = FALSE; // TRUE while we're loading into the capture buffer

   if( !cbuflen)   // First time through?   Do some initialisation.
   {
      cbuflen = hw * 2 * sample_rate;
      cbuf = VT_malloc( sizeof( float) * cbuflen);
      our_srcal = srcal;
   }

   //
   //  As we reach within hw seconds of the second mark, start capturing 
   //

   double t = timestamp_frac( T);  // Fractional part of the input timestamp
 
   if( !flag && t > 1-hw)
   {
      cbufp = 0;         // Capture buffer load index
      vmax = vavg = 0;   // Average and max accumulators
      nin_this = nin;    // Input sample number at start of capture buffer
      T_start = T;       // Input stream timestamp at start of capture buffer
      flag = TRUE;       // Capture buffer is loading
   }
  
   if( !flag) return FALSE;   // Not yet capturing?

   //
   //  Load this sample into the capture buffer, accumulate max and mean,
   //  and the location within the capture buffer of the peak.
   //

   double v = pulse_sign * in;
   if( v > vmax)
   {
      vmax = v;
      vmax_pos = cbufp;
   }

   vavg += fabs( v);
   cbuf[cbufp++] = v;

   if( cbufp < cbuflen) return FALSE;    // Capture buffer not yet full?

   flag = FALSE; // End of capture, buffer is full

   //
   //  Check pulse amplitude is sufficient, peak/mean ratio of buffer contents.
   //

   pmr = vmax / (vavg/cbuflen);  // Peak to mean ratio
   if( !noppm && pmr < 30)
   {
      // Will occur if the PPS fails, or input timestamping is way out
      VT_report( 0, "insufficient PPS peak/mean ratio %.1f - skipped", pmr);
      return FALSE;
   }

   return TRUE;
}

//
//  Simple step timing:
//  Just look for the consecutive sample pair which straddle the leading
//  edge of the pulse.
//

static void timing_ppsedge( timestamp T, double in, double srcal)
{
   if( !load_buffer( T, in, srcal)) return;

   // Backtrack from the peak to find where the samples straddle 50% of
   // the peak amplitude.

   int vstart;
   for( vstart=vmax_pos; vstart >= 0 && cbuf[vstart] > 0.5 * vmax; vstart--) ;;;

   if( vstart < 0)
   {
      // Pulse must have started before the buffer capture
      VT_report( 0, "pulse starts too early - skipped");
      return;
   }

   // Estimate the edge position, relative to the start of the capture
   // buffer.
   double secmark_index = vstart + 1;

   revise_timebase( secmark_index, nin_this, T_start, pmr);
}

//
//  Pulse centroid timing:
//  Locate the pulse centroid and from that, use the centroid_offset to
//  locate the second mark.
//

static double set_w1 = 0, set_w2 = 0;

static void timing_ppsbase( timestamp T, double in, double srcal)
{
   if( !load_buffer( T, in, srcal)) return;

   //
   //  Determine centroid of the pulse whose peak is at index vmax_pos
   //  in the capture buffer.  aw1 and aw2 are buffer offset which bound
   //  the centroid integration.
   //

   int aw1, aw2;
   if( set_w1 && set_w2)  // w1 and w2 given in the mode options?
   {
      aw1 = vmax_pos - set_w1 * sample_rate;
      aw2 = vmax_pos + set_w2 * sample_rate;
   }
   else
   if( !pulse_width_auto)    // Not using w=auto mode option?
   {
      // pulse_width (actually the half-width) is set by w= mode option
      aw1 = vmax_pos - pulse_width * sample_rate;
      aw2 = vmax_pos + pulse_width * sample_rate;
   }
   else  // Using w=auto mode option
   {
      // Set aw1 and aw2 to the 1% locations of the pulse amplitude
      aw1 = aw2 = vmax_pos;
      while( aw1 > 0 && cbuf[aw1] > vmax * 0.01) aw1--;
      aw1 -= 2;     // For luck, make it start 2 samples earlier. Black art.
      while( aw2 < cbuflen-1 && cbuf[aw2] > vmax * 0.01) aw2++;
   }

   if( aw1 <= 0 || aw2 >= cbuflen-1)
   {
      // Input timing is so far out that the PPS doesn't sit completely in
      // the capture buffer - beginning or end of the pulse has been lost.
      VT_report( 0, "PPS at timing limit - skipped");
      return;
   }

   if( pulse_width_auto)
      VT_report( 2, "w1=%.3fmS w2=%.3fmS",
               (vmax_pos - aw1)/(double) sample_rate * 1000,
               (aw2 - vmax_pos)/(double) sample_rate * 1000);

   // Some simple spike removal
   int j;
   for( j = aw1+1;  j < aw2 - 1; j++)
   {
      if( j == vmax_pos) continue;  // Don't clip the pulse peak

      double d1 = cbuf[j] - cbuf[j-1];
      double d2 = cbuf[j+1] - cbuf[j];
      if( d1 * d2 < 0) cbuf[j] = (cbuf[j-1] + cbuf[j+1])/2;
   }

   // Centroid calculation
   double mprod = 0, msum = 0;
   for( j = aw1; j< aw2; j++)
   {
      double v = cbuf[j];
      msum += v;
      mprod += v * j;
   }

   double centroid = mprod/msum;  // Index of centroid in capture buffer

   // Move back from the centroid to the timing mark of the pulse.  We use our
   // own accurate sample rate to convert the centroid offset seconds into a
   // sample count.
   double secmark_index = centroid - centroid_offset * sample_rate * our_srcal;

   revise_timebase( secmark_index, nin_this, T_start, pmr);
}

///////////////////////////////////////////////////////////////////////////////
//  No Timing                                                                //
///////////////////////////////////////////////////////////////////////////////

//
//  With -m none, the program does interpolation to UT synchronous samples at
//  the exact sample rate, relying entirely on the incoming stream's timestamp.
//

static void timing_none( timestamp T, double in, double srcal)
{
   our_srcal = srcal;
   our_dt = 1/(sample_rate * our_srcal);
   our_timebase = T;
   ninbase = 0;
   nin = 0;

   if( !state)
   {
      outbase = timestamp_compose( timestamp_secs( our_timebase) + 2, 0);
      VT_set_timebase( vtoutfile, outbase, 1.0);
      state = 2;
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Interpolation                                                            //
///////////////////////////////////////////////////////////////////////////////

//
//  Conversion to exact sample rate and UT synchronous samples is done by
//  sinc-weighted interpolation between the input samples.
//

static double **Iframe;                // Input signal buffer - array of frames
static timestamp *Istamp;                             // Input timestamp buffer

#define ILOG 8                         // Log base 2 of the input buffer length
#define Ilen  (1 << ILOG)                                // Input buffer length
#define Imask (Ilen - 1)                                  // Index cycling mask
static unsigned char Ip = 0;                              // Input buffer index 

// Macros to load incoming frames and timestamps
#define IFRAME_IN(A) Iframe[(Ip + (A) - (1<<(ILOG-1))) & Imask]
#define ISTAMP_IN(A) Istamp[(Ip + (A) - (1<<(ILOG-1))) & Imask]

// Macros to extract frames and timestamps.  The zero index is positioned
// half way along the buffer so that (A) can range -Ilen/2 to +Ilen/2
#define IFRAME(A) Iframe[(Ip + (A)) & Imask]
#define ISTAMP(A) Istamp[(Ip + (A)) & Imask]

static float *wsinc;                   // Interpolation kernel, a sinc function
static float **wsinck;                 // See setup_wsinc()

#define WNZ  36       // Half-length of sinc function, number of zero crossings
#define WNL  128            // Number of function points between zero crossings

static double *outframe;

//
//  Construct an output frame located in time between IFRAME(0) and IFRAME(1).
//  x (0 <= x < 1) is the fractional distance in time between the output
//  sample and IFRAME(0).   wsinc[] holds half of the sinc function, the other
//  half is just mirrored.   WNL is the number of sinc function entries per
//  output sampling period.
//

//        IFRAME(-1)     IFRAME(0)        IFRAME(1)       IFRAME(2)
// Input  ----X-------------X----------------X----------------X-------  ...
//                          |<  x >|
// Output ---------x---------------X---------------x----------------x--
//                 ^               ^               ^                ^
//             wsinc[-WNL]      wsinc[0]       wsinc[+WNL]      wsinc[+2*WNL]
// wsinc                    |< km >|
//            |             |                |                |
//      wsinc[-km-WNL]   wsinc[-km]       wsinc[-km+WNL]    wsinc[-km+2*WNL]

static inline void interpolate( int chans, double x)
{
   int i, j;

   int km = x * WNL;

   float *wm = wsinck[km];

   // This uses a lot more cpu if you swap the inner and outer loops
   for( i=0; i<chans; i++)
   {
      double v = 0;

      for( j=-(WNZ-1); j<WNZ; j++) v += IFRAME(-j)[i] * wm[j];

      outframe[i] = v;
   }
}

//
//  Note from the above ASCII art, wsinc[] is used in steps of WNL all having
//  an offset of km.  Auxilliary array wsinck[km][i*WNL] reduces CPU cache
//  misses by listing wsinc[km + i*WNL] values these for all possible values
//  of km.
//
//  wsinck[km][0] is positioned in the center of the allocated space so that
//  signed values of i can be used.
//
 
static void setup_wsinc( void)
{
   int i, k, wsinc_len = WNZ * WNL + 1;

   wsinc = VT_malloc( sizeof(float) * wsinc_len);
   wsinc[0] = 1;
   for( i=1; i<wsinc_len; i++)
   {
      double a = i/(double) WNL;
      wsinc[i]  = sin(M_PI * a)/(M_PI * a);
   }

   wsinck = VT_malloc( sizeof( float *) * (WNL+1));
   for( k=0; k<=WNL; k++)
   {
      wsinck[k] = WNZ + (float *) VT_malloc( sizeof( float) * WNZ * 2);

      for( i = -WNZ; i < WNZ; i++)
      {
         int n = k + i * WNL;  if( n < 0) n = -n;
         wsinck[k][i] = wsinc[n];
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

static void parse_method( char *s)
{
   // ppsbase+,w=width,c=offset
   // ppsbase-,w=width,c=offset
   // ppsedge+
   // ppsedge-

   int w_given = 0, c_given = 0;
  
   while( s && *s)
   {
      char *p = strchr( s, ','); if( p) *p++ = 0;

      if( !strcmp( s, "none"))
      {
         process_timing = timing_none;
      }
      else
      if( !strcmp( s, "ppsbase+"))
      {
         process_timing = timing_ppsbase;
         pulse_sign = 1;
      }
      else
      if( !strcmp( s, "ppsbase-"))
      {
         process_timing = timing_ppsbase;
         pulse_sign = -1;
      }
      else
      if( !strcmp( s, "ppsedge+"))
      {
         process_timing = timing_ppsedge;
         pulse_sign = 1;
      }
      else
      if( !strcmp( s, "ppsedge-"))
      {
         process_timing = timing_ppsedge;
         pulse_sign = -1;
      }
      else
      if( !strncmp( s, "c=", 2))
      {
         centroid_offset = atof( s+2);
         c_given = 1;
      }
      else
      if( !strncmp( s, "w=", 2))
      {
         if( !strncmp( s+2, "auto", 4)) pulse_width_auto = TRUE;
         else
         {
            pulse_width = atof( s+2);
            pulse_width_auto = FALSE;
            w_given = 1;
         }
      }
      else
      if( !strncmp( s, "w1=", 3)) set_w1 = atof( s+3);
      else
      if( !strncmp( s, "w2=", 3)) set_w2 = atof( s+3);
      else
      if( !strncmp( s, "noppm", 5)) noppm = 1;
      else
         VT_bailout( "unrecognised method option: %s", s);

      s = p;
   }

   if( !w_given && c_given) pulse_width = 1.1 * centroid_offset;
}

static void usage( void)
{
   fprintf( stderr,
        "usage: vttime [options] input output\n"
        "\n"
        "options:\n"
        "  -v       Increase verbosity\n"
        "  -B       Run in background\n"
        "  -L name  Specify logfile\n"
        "  -c chan  Channel containing timing signal\n"
        "           (default 1)\n"
        "\n"
        "  -m method,options  PPS timing method\n"
        "     -m ppsbase+,c=centroid,w=halfwidth\n"
        "     -m ppsbase-,c=centroid,w=halfwidth\n"
        "     -m none\n" 
     );

   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vttime");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vdBc:m:L:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'm') parse_method( optarg);
      else
      if( c == 'c') timing_chan = atoi( optarg);
      else
      if( c == 'd') debug = TRUE;
      else
      if( c == -1) break;
      else
         usage();
   }

   if( optind + 2 == argc)
   {
      inname = strdup( argv[optind]);
      outname = strdup( argv[optind+1]);
   }
   else
   if( optind + 1 == argc)
   {
      inname = strdup( argv[optind]);
      outname = strdup( "-");
   }
   else
   if( optind == argc)
   {
      inname = strdup( "-");
      outname = strdup( "-");
   }
   else usage();

   if( background)
   {
      int flags = inname[0] == '-' ? KEEP_STDIN : 0;
      if( outname[0] == '-') flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   if( !process_timing) VT_bailout( "no timing method specified: needs -m");

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( inname);
   vtinfile = VT_open_input( inname);
   if( !vtinfile)
      VT_bailout( "cannot open input %s: %s", inname, VT_error);

   sample_rate = VT_get_sample_rate( vtinfile);
   VT_init_chanspec( chspec, vtinfile);

   VT_report( 1, "channels: %d, input rate: %d", chspec->n, sample_rate);
   VT_report( 1, "centroid offset: %.3e", centroid_offset);

   if( timing_chan < 1 || timing_chan > chspec->n)
      VT_bailout( "invalid channel %d given with -c", timing_chan);

   vtoutfile = VT_open_output( outname, chspec->n, 0, sample_rate);
   if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);

   setup_wsinc();

   int i;
   Iframe = VT_malloc_zero( sizeof( double *) * Ilen);
   Istamp = VT_malloc_zero( sizeof( timestamp) * Ilen);

   for( i=0; i<Ilen; i++)
      Iframe[i] = VT_malloc_zero( sizeof( double) * chspec->n);

   outframe = VT_malloc_zero( sizeof( double) * chspec->n);
   our_dt = 1/(double) sample_rate;

   double in_srcal = 1.0;
   long double DT = 1.0/sample_rate;

   while( 1)
   {
      timestamp in_timebase = VT_get_timestamp( vtinfile);
      if( timestamp_is_NONE( in_timebase)) VT_exit( "end of input");
  
      in_srcal = VT_get_srcal( vtinfile); 

      // Read an incoming frame and call the timing processing
     
      double *inframe = VT_get_frame( vtinfile);
      double pps_in = inframe[chspec->map[timing_chan - 1]];
      process_timing( in_timebase, pps_in, in_srcal);

      // Save the frame at the head of the pipeline and assign it our timestamp

      for( i=0; i<chspec->n; i++) IFRAME_IN(0)[i] = inframe[chspec->map[i]];
      ISTAMP_IN(0) = timestamp_add( our_timebase, (nin - ninbase)  * our_dt);

      nin++;   // Input frame counter

      //  Generate output frames

      if( state == 2)               // In running state?
      {
         // Tout is the timestamp of the next output sample. 
         // ISTAMP(0) is the timestamp of the previous input sample,
         // ISTAMP(1) the next. Tout is somewhere in between the two.

         timestamp Tout = timestamp_add( outbase, vtoutfile->nft * DT);

         // Output as many frames as possible: 0, 1, or 2 frames are
         // interpolated between ISTAMP(0) and ISTAMP(1).

         double Tint = timestamp_diff(ISTAMP(1), ISTAMP(0));
         double x = timestamp_diff(Tout, ISTAMP(0))/Tint;
       
         while( x < 1)   // Until ISTAMP(1) is too old
         {
            interpolate( chspec->n, x < 0 ? 0 : x);
                      
            VT_insert_frame( vtoutfile, outframe);

            x += 1/(sample_rate * Tint);
         }
      }

      Ip++;   // Rotate the input buffer
   }
}


