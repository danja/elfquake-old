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

static int sample_rate = 0;
static double dF = 0;
static int FFTWID;
static int BINS;

static timestamp refdate = timestamp_ZERO;
static int nif = 0;

static VTFILE *vtfile = NULL;

static int AFLAG = 0;        // -a option: non-coherent averaging of FFT frames
static int PFLAG = 0;                // -p option: pad timing breaks with zeros
static int nfinal = 0;       // Number of FFT frames finalised into the average
static int nmax = 0;              // -N option, number of FFT frames to average
static int chans;
static int XFLAG = 0;   // Experimental

static char *hamming = "rect";

static struct CHANNEL
{
   double *in;
   complex double *X;

   complex double *avg;
   fftw_plan plan;
}
 *channels = NULL;

static void init_channels( void)
{
   int i;

   channels = VT_malloc( sizeof( struct CHANNEL) * chans);

   for( i=0; i<chans; i++)
   {
      struct CHANNEL *cp = channels + i;
      memset( cp, 0, sizeof( struct CHANNEL));

      cp->in = VT_malloc( FFTWID * sizeof( double));
      cp->X = VT_malloc( FFTWID * sizeof( fftw_complex));

      cp->avg = VT_malloc_zero( BINS * sizeof( complex double));
      cp->plan = fftw_plan_dft_r2c_1d( FFTWID, cp->in, cp->X,
                           FFTW_ESTIMATE | FFTW_DESTROY_INPUT);
   }
}

static double *hwindow = NULL;

static void setup_hamming( void)
{
   hwindow = VT_malloc( sizeof( double) * FFTWID);

   int i;
   double N = FFTWID - 1;

   if( !strcasecmp( hamming, "rect"))
   {
      for( i=0; i<FFTWID; i++) hwindow[i] = 1;
   }
   else
   if( !strcasecmp( hamming, "cosine"))
   {
      for( i=0; i<FFTWID; i++) hwindow[i] = sin( i/N * M_PI);
   }
   else
   if( !strcasecmp( hamming, "hann"))
   {
      for( i=0; i<FFTWID; i++) hwindow[i] = sin( i/N * M_PI) * sin( i/N * M_PI);
   }
   else
   if( !strcasecmp( hamming, "blackman"))
   {
      double a0 = (1 - 0.16)/2;
      double a1 = 0.5;
      double a2 = 0.16/2;
      for( i=0; i<FFTWID; i++)
         hwindow[i] = a0 - a1 * cos( i/N * 2 * M_PI)
                         + a2 * cos( i/N * 4 * M_PI);
   }
   else
   if( !strcasecmp( hamming, "hamming"))
   {
      for( i=0; i<FFTWID; i++) hwindow[i] = 0.54 - 0.46 * cos( i/N * 2 * M_PI);
   }
   else
   if( !strcasecmp( hamming, "nuttall"))
   {
      double a0 = 0.355768, a1 = 0.487396, a2 = 0.144232, a3 = 0.012604;
      for( i=0; i<FFTWID; i++)
         hwindow[i] = a0 - a1 * cos( i/N * 2 * M_PI)
                         + a2 * cos( i/N * 4 * M_PI)
                         - a3 * cos( i/N * 6 * M_PI);
   }
   else VT_bailout( "unknown window function: %s", hamming);
}

static void output_spectrum( void)
{
   int bin, ch;
   
   for( bin=0; bin<BINS; bin++)
   {
      double f = bin * dF;

      printf( "%.7f", f);
 
      for( ch=0; ch<chans; ch++)
         if( AFLAG)
         {
            // Real part of avg[] holds the sum of squared amplitudes
            // Output the RMS amplitude
            complex double a = channels[ch].avg[bin]/nfinal; 
            double rms = sqrt(creal( a)/2)/BINS;
            printf( " %.6e", rms);
         }
         else
         {
            // avg[] holds the sum of complex amplitudes
            // Output the mean complex amplitude and its RMS value
            complex double a = channels[ch].avg[bin]/BINS/nfinal; 
            double rms = cabs( a) / sqrt(2);
            printf( " %.6e %.6e %.6e", creal( a), cimag( a), rms);
         }

      printf( "\n");
   }
}

static inline void process_sample( double v, int ch)
{
   channels[ch].in[nif] = v * hwindow[nif];
}


static void finalise( void)
{
   int i, ch;

   for( ch=0; ch < chans; ch++) fftw_execute( channels[ch].plan);

   for( i=0; i<BINS; i++)
      if( AFLAG)
         for( ch=0; ch<chans; ch++)
         {
            // Use real part of avg[] to hold the sum of squared amplitudes
            double a = cabs( channels[ch].X[i]);
            channels[ch].avg[i] += a * a;
         }
      else
      {
         // Rotate the complex amplitude, turning it into an absolute phase
         double ph = XFLAG ? 0 : VT_phase( refdate, i * dF);
 
         for( ch=0; ch<chans; ch++)
            channels[ch].avg[i] += channels[ch].X[i] * (cos( ph) - I*sin( ph));
      }

   nfinal++;
   nif = 0;
   refdate = VT_get_timestamp( vtfile);
}

static void usage( void)
{
   fprintf( stderr, "usage: vtwspec [options] input\n"
            "\n"
            "options:\n"
            "  -B        Run in background\n"
            "  -L name   Specify logfile\n"
            "  -r        Resolution, Hz\n"
            "  -a        Non-coherent average\n"
            "  -N count  Number of transform frames to average\n"
            "  -p        Pad timing breaks with zeros\n"
            "\n"
            "  -W window Select window function\n"
            "            -W cosine\n"
            "            -W blackman\n"
            "            -W hamming\n"
            "            -W nuttall\n"
            "            -W hann\n"
            "            -W rect (default)\n"
            "\n"
          );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtwspec");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "pvBaxkr:N:W:L:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'r') dF = atof( optarg);
      else
      if( c == 'a') AFLAG = 1;
      else
      if( c == 'p') PFLAG = 1;
      else
      if( c == 'x') XFLAG = 1;
      else
      if( c == 'N') nmax = atoi( optarg);
      else
      if( c == 'W') hamming = strdup( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( argc > optind + 1) usage();
   char *bname = strdup( optind < argc ? argv[optind] : "-");

   if( !dF) usage();

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDIN : 0;
      flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);
   if( (vtfile = VT_open_input( bname)) == NULL)
      VT_bailout( "cannot open input %s: %s", bname, VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   FFTWID = round(sample_rate/dF);
   BINS = FFTWID / 2;
   VT_report( 1, "resolution: %.3f  bins %d", dF, BINS);

   setup_hamming();
   init_channels();

   refdate = VT_get_timestamp( vtfile);

   int j;
   while( 1)
   {
      double *inframe = VT_get_frame( vtfile);
      if( !inframe)
      {
         // End of input stream
         if( PFLAG)
         {
            VT_report( 1, "padding %d samples", FFTWID - nif);
            while( nif++ < FFTWID)
               for( j=0; j<chans; j++) process_sample( 0, j);
         }
         break;
      }

      for( j=0; j<chans; j++) process_sample( inframe[chspec->map[j]], j);

      if( ++nif == FFTWID)
      {
         finalise();
         if( !AFLAG) break;
         if( nmax && nfinal == nmax) break;
      }
   }

   if( !AFLAG && nif) finalise();
   output_spectrum();
   VT_report( 1, "nfinal %d", nfinal);

   return 0;
}

