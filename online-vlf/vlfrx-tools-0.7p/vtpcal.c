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

// Set by command line options
static double Tbuf = 0.010;                 // Buffer length, seconds
static int EFLAG = 0;                  // Non-zero if -e given, output spectrum
static int Nmax = 0;             // From -N option: number of pulses to capture
static timestamp TstartP = timestamp_ZERO;         // Timestamp of first sample 
static timestamp TstartR = timestamp_ZERO;

static int ptype = 0;
#define PTYPE_STEPPOS  1
#define PTYPE_STEPNEG  2
#define PTYPE_PULSEPOS 3
#define PTYPE_PULSENEG 4

static int chans = 0;
static int sample_rate = 0;

static int buflen = 0;                       // Circular buffer length, samples
static int FFTWID = 0;                                    // FFT width, samples
static int BINS = 0;                                          // Number of bins
static double DF = 0;                                   // Frequency resolution
static double DT = 0;                                        // Time resolution
static double F1 = 0, F2 = 0;                     // Frequency range to analyse
static int bf1, bf2;            // Start and finish bin numbers, from F1 and F2
static timestamp Tin;                        // Timestamp of most recent sample
static int N_out = 0;                      // Number of sferics processed

#define PBUF(C,P) channels[C].pbuf[P]
#define RBUF(C,P) channels[C].rbuf[P]

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtpcal [options] [input]\n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -B            Run in background\n"
       "  -L name       Specify logfile\n"
       "  -F start,end  Frequency range\n"
       "  -e            Output spectrum and waveform data\n"
       "  -N count      Average this many pulses\n"
       "\n"
       "  -s+           Positive going step (default)\n"
       "  -s-           Negative going step\n"
       "  -p+           Positive going pulse\n"
       "  -p-           Negative going pulse\n"
     );
   exit( 1);
}

static struct CHAN
{
   double *pbuf;                                       // Circular input buffer
   double *fft;                                             // FFT input buffer
   complex double *pX;                                            // FFT output
   double *rbuf;
   complex double *rX;                                            // FFT output
   double *upb;                                   // Buffer for unwrapped phase
   double *gd;                                                   // Group delay
   double mslope;                               // Median phase/frequency slope
   double rms;                                        // RMS amplitude of pulse
   fftw_plan fp;
   fftw_plan fr;
   int *tags;
} *channels;

static void init_channel( struct CHAN *cp)
{
   memset( cp, 0, sizeof( struct CHAN));

   cp->pbuf = VT_malloc_zero( sizeof( double) * buflen);
   cp->rbuf = VT_malloc_zero( sizeof( double) * buflen);
   cp->fft = VT_malloc_zero( sizeof( double) * FFTWID);
   cp->pX = VT_malloc( sizeof( complex double) * BINS);
   cp->rX = VT_malloc( sizeof( complex double) * BINS);
   cp->upb = VT_malloc( sizeof( double) * BINS);
   cp->gd = VT_malloc( sizeof( double) * BINS);
   cp->fp = fftw_plan_dft_r2c_1d( FFTWID, cp->fft, cp->pX, FFTW_ESTIMATE);
   cp->fr = fftw_plan_dft_r2c_1d( FFTWID, cp->fft, cp->rX, FFTW_ESTIMATE);
   cp->tags = VT_malloc_zero( sizeof( int) * BINS);
}

static struct BLOCK
{
   double f1, f2;
   int b1, b2;
}
 *blocks = NULL;

static int nblocks = 0;

///////////////////////////////////////////////////////////////////////////////
//  Output Functions                                                         //
///////////////////////////////////////////////////////////////////////////////

// Output a 'H' record - timing offset and RMS of each channel
static void output_standard( FILE *f, double offset, double rms)
{
   fprintf( f, "H %.8f %.3e\n", offset, rms);
}

//
// Extended output records if -e is given.  
//    H is the header record, call output_standard() for that;
//    S records for spectrum data;
//    T records for time domain;
//    E is an end marking record;
//

static void output_extended( FILE *f, double toga, double rms)
{
   int ch, j;

   output_standard( f, toga, rms);

   for( j=bf1; j<=bf2; j++)
   {
if( channels[0].tags[j]) continue;
      
      fprintf( f, "S %.3f", DF * j);
      for( ch=0; ch<chans; ch++)
      {
         struct CHAN *cp = channels + ch;
         fprintf( f, " %.3e %.3f %.3f %.3f", 
                 cabs(cp->pX[j]),
                 carg(cp->pX[j]) * 180/M_PI,
                 cp->upb[j] * 180/M_PI,
                 cabs(cp->rX[j])
               );
      }
      fprintf( f, "\n");
   }

   for( j=bf1+2; j<bf2-1; j++)
   {
      fprintf( f, "G %.3f", DF * j);
      for( ch=0; ch<chans; ch++)
      {
         struct CHAN *cp = channels + ch;
         fprintf( f, " %.3e", cp->gd[j]);
      }
      fprintf( f, "\n");
   }

   double T = timestamp_frac( TstartP) - 1;
   for( j=0; j<FFTWID; j++)
   {
      fprintf( f, "T %.7f", T + j * DT);
      for( ch=0; ch<chans; ch++) fprintf( f, " %.4e", PBUF(ch,j));
      fprintf( f, "\n");
   }

   fprintf( f, "E\n");
}

static void output_record( double toga,
                           double rms)
{
   FILE *f = stdout;

   if( EFLAG) output_extended( f, toga, rms);
   else output_standard( f, toga, rms);
}

///////////////////////////////////////////////////////////////////////////////
//  Timing Pulse Analyser                                                    //
///////////////////////////////////////////////////////////////////////////////

static int cmp_double( const void *p1, const void *p2)
{
   double v1 = *(double *)p1;
   double v2 = *(double *)p2;

   if( v1 < v2) return -1;
   if( v1 > v2) return 1;
   return 0;
}

double *window = NULL;

static void process_channel( struct CHAN *cp, double atp)
{
   int j, k;
   int nb = bf2 - bf1 + 1;                 // Number of frequency bins analysed

   double *tsbuf = VT_malloc( sizeof( double) * nb * (nb-1)/2);

   for( j=0; j<nblocks; j++)
   {
      struct BLOCK *bp = blocks + j;

      for( k=bp->b1;  k <= bp->b2; k++) cp->tags[k] = 1;
   }

   // FFT the pulse and measure the RMS amplitude
   double sumsq = 0;
   double tdpeak = 0;
   for( j=0; j<FFTWID; j++)
   {
      double v = cp->pbuf[j];
      cp->fft[j] = v * window[j];
      sumsq += v * v;
      if( fabs(v) > tdpeak) tdpeak = fabs(v);
   }

   fftw_execute( cp->fp);
   cp->rms = sqrt(sumsq/FFTWID);

   for( j=0; j<FFTWID; j++) cp->fft[j] = cp->rbuf[j] * window[j];
   fftw_execute( cp->fr);

   //
   //  Normalise the frequency domain amplitudes, allowing for the
   //  expected spectrum of the test signal.
   //

   if( ptype == PTYPE_STEPPOS || ptype == PTYPE_STEPNEG)
   {
      for( j=bf1; j<=bf2; j++) cp->pX[j] *= j;
      for( j=bf1; j<=bf2; j++) cp->rX[j] *= j;
   }

   double fdpeak = 0;
   for( j=bf1; j<=bf2; j++)
   {
      double v = cabs(cp->pX[j]);
      if( v > fdpeak) fdpeak = v;
   }

   for( j=bf1; j<=bf2; j++) cp->pX[j] /= fdpeak;
   for( j=bf1; j<=bf2; j++) cp->rX[j] /= fdpeak;

//   for( j=bf1; j<=bf2; j++)
//      if( cabs( cp->rX[j]) > 0.001) cp->tags[j] = 1;

   for( j=0; j<FFTWID; j++) cp->pbuf[j] /= tdpeak;

   //
   //
   //

   // Refer the phase to the second mark
   for( j=bf1; j<=bf2; j++)
   {
      double ph = atp * j * DF * 2 * M_PI;
      cp->pX[j] *= cos(ph) - I*sin(ph);
   }

   //
   //  Adjust the phase according to the type of step/pulse used.  Steps
   //  are a sum of sines so they are rotate +/-90 deg to bring the phase
   //  to zero at the phase center of the step.  Impulses are a sum of
   //  cosines, so they just need the polarity setting.
   //
   
   complex double p = 0;

   switch( ptype)
   {
      case PTYPE_STEPPOS:  p = I;   break;
      case PTYPE_STEPNEG:  p = -I;  break;
      case PTYPE_PULSEPOS: p = 1;   break;
      case PTYPE_PULSENEG: p = -1;  break;
   }

   for( j=bf1; j<=bf2; j++)
   {
      cp->pX[j] *= p;
      cp->rX[j] *= p;
   }

   //
   //  Unwrap the bin phases into cp->upb[].  Hopefully the pulse delay is
   //  not so much that the phase changes by more than pi radians between
   //  bins.
   //

   double b;
   j = bf1;
   while( cp->tags[j]) j++;
   b = carg(cp->pX[j]);
   cp->upb[j] = b;
   k = j;
   j++;
   for( ; j<=bf2; j++)
   {
      if( cp->tags[j]) continue;

      double dp = carg(cp->pX[j]) - carg(cp->pX[k]);
      if( dp > M_PI ) dp -= 2*M_PI;
      if( dp < -M_PI) dp += 2*M_PI;
      b += dp;
      cp->upb[j] = b;
      k = j;
   }

   //
   //  Record the group delay.   This is the differential of phase with
   //  respect to frequency, evaluated with a 5-point finite difference.
   //  Two bins at each end of the spectrum are not calculated.
   //

   for( j=bf1+2; j<bf2-1; j++)
   {
      if( cp->tags[j-2] || cp->tags[j-1] || cp->tags[j] ||
          cp->tags[j+1] || cp->tags[j+2]) continue;

      cp->gd[j] = -1/(2 * M_PI * DF) * (
                  2*(cp->upb[j+1] - cp->upb[j-1])/3
                - 1*(cp->upb[j+2] - cp->upb[j-2])/12);
   }

   // Use Theil-Sen regression to get an average slope
   int n = 0;

   for( j=bf1; j<bf2; j++)
      for( k=j+1; k<=bf2; k++)
         if( !cp->tags[j] && !cp->tags[k])
            tsbuf[n++] = (cp->upb[k] - cp->upb[j])/(double)(k-j);

   qsort( tsbuf, n, sizeof( double), cmp_double);

   cp->mslope = -tsbuf[n/2];  // Median - radians per bin
   cp->mslope = cp->mslope / (DF * 2 * M_PI); // Radians per (radian per sec)

   free( tsbuf);
}

//
//
//

static void process_buffer( void)
{
   int ch;

   double atp = 1 - timestamp_frac( TstartP);

   for( ch=0; ch<chans; ch++)
   {
      struct CHAN *cp = channels + ch;
      process_channel( cp, atp);
   }

   // 
   //  Work out the average timing offset weighted by the RMS amplitude.
   //
   double mean_dt = 0;
   double sumrms = 0;
   
   for( ch = 0; ch < chans; ch++)
   {
      struct CHAN *cp = channels + ch;

      mean_dt += cp->mslope * cp->rms;
      sumrms += cp->rms;
   }

   mean_dt /= sumrms;

   double power = 0;
   for( ch=0; ch < chans; ch++) power += pow( channels[ch].rms, 2);
   output_record( mean_dt, sqrt(power));

   N_out++;
   if( Nmax && N_out == Nmax) VT_exit( "completed %d", Nmax);
}

//
//  Called with input frame.
//

static void process_frame( double *frame, struct VT_CHANSPEC *chspec)
{
   static int capture_state = 0;
   static int capture_index = 0;
   static int capture_count = 0;
   int ch;
 
   double t = timestamp_frac( Tin);

   if( !capture_state) // Waiting for start time
   {
      if( t < 1 - Tbuf/2 - Tbuf) return;

      capture_index = 0;
      capture_state = 1;
      TstartR = Tin;
   }

   if( capture_state == 1)
   {
      if( capture_index < buflen)
      {
         for( ch = 0; ch < chans; ch++)
            RBUF(ch, capture_index) += frame[chspec->map[ch]];
   
         capture_index++;
      }
   
      if( capture_index == buflen)
      {
         capture_state = 2;
         capture_index = 0;
         TstartP = Tin;
      }

      return;
   }

   if( capture_state == 2)
   {
      if( capture_index < buflen)
      {
         for( ch = 0; ch < chans; ch++)
            PBUF(ch, capture_index) += frame[chspec->map[ch]];
   
         capture_index++;
      }
   
      if( capture_index == buflen)
      {
         capture_state = 0;
         capture_index = 0;
   
         if( ++capture_count == Nmax)
         {
         capture_index = 0;
            process_buffer();
            VT_exit( "completed %d pulses", Nmax);
         }
      }
   }
}

int main( int argc, char *argv[])
{
   VT_init( "vtpcal");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBL:F:h:N:s:p:b:e?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'F') VT_parse_freqspec( optarg, &F1, &F2);
      else
      if( c == 'e') EFLAG = 1;
      else
      if( c == 'N') Nmax = atoi( optarg);
      else
      if( c == 's')
      {
         if( ptype) VT_bailout( "pulse type specified more than once");
         if( !strcmp( optarg, "+")) ptype = PTYPE_STEPPOS;
         else
         if( !strcmp( optarg, "-")) ptype = PTYPE_STEPNEG;
         else VT_bailout( "unrecognised step type: %s", optarg);
      }
      else
      if( c == 'p')
      {
         if( ptype) VT_bailout( "pulse type specified more than once");
         if( !strcmp( optarg, "+")) ptype = PTYPE_PULSEPOS;
         else
         if( !strcmp( optarg, "-")) ptype = PTYPE_PULSENEG;
         else VT_bailout( "unrecognised pulse type: %s", optarg);
      }
      else
      if( c == 'b')
      {
         blocks = VT_realloc( blocks, (nblocks+1) * sizeof( struct BLOCK));
         VT_parse_freqspec( optarg, &blocks[nblocks].f1, &blocks[nblocks].f2);
         nblocks++;
      }
      else
      if( c == -1) break;
      else
         usage(); 
   }  
  
   if( argc > optind + 1) usage();
   char *bname = strdup( optind < argc ? argv[optind] : "-");
 
   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDIN : 0;
      flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   VTFILE *vtfile = VT_open_input( bname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chans, sample_rate);

   if( !Nmax) Nmax = 1;
   if( !ptype) ptype = PTYPE_STEPPOS;

   //
   //  Set up buffers lengths, etc.  If -T given, an input buffer is created
   //  to hold the given time window, otherwise a default circular buffer is
   //  created.
   // 

   buflen = Tbuf * sample_rate;
   FFTWID = buflen;
   BINS = FFTWID/2 + 1;
   DF = sample_rate/(double) FFTWID;
   DT = 1/(double)sample_rate;

   VT_report( 2, "buffer length: %d samples DF=%.3f",
                  buflen, DF);

   if( !F2) F2 = sample_rate/2;
   bf1 = F1/DF;
   bf2 = F2/DF;
   if( bf1 <= 0) bf1 = 1;
   if( bf2 >= BINS) bf2 = BINS-1;

   int i;
   window = VT_malloc( sizeof( double) * buflen);
   for( i=0; i<buflen; i++) window[i] = sin( i * M_PI/buflen);

   channels = VT_malloc_zero( sizeof( struct CHAN) * chans);
   for( i=0; i<chans; i++) init_channel( channels + i);

   for( i=0; i<nblocks; i++)
   {
      struct BLOCK *bp = blocks + i;

      bp->b1 = bp->f1/DF;
      if( bp->b1 < 0) bp->b1 = 0;
      if( bp->b1 >= BINS) bp->b1 = BINS - 1;
      bp->b2 = bp->f2/DF;
      if( bp->b2 < 0) bp->b2 = 0;
      if( bp->b2 >= BINS) bp->b2 = BINS - 1;

      VT_report( 2, "block %.2f %.2f", bp->f1, bp->f2);
   }

   double *frame;

   while( 1)
   {
      //
      // Read a frame and add to circular input buffers
      //

      Tin = VT_get_timestamp( vtfile); 

      if( (frame = VT_get_frame( vtfile)) == NULL) break;

      process_frame( frame, chspec);
   }

   return 0;
}

