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
static double dF = 0;                        // -r option, frequency resolution

static uint64_t NS; // Number of samples to analyse per window, transform width

static double fcenter = 0;                                    // From -f option
static double fwidth = 0;                                     // From -w option

static timestamp refdate = timestamp_ZERO; // Start of the current transform
static timestamp specdate = timestamp_ZERO;
static uint64_t nif = 0;        // Number of sample frames in current transform

static VTFILE *vtfile = NULL;
static int NF = 0;                                // Number of Goertzel filters
static int AFLAG = 0;                      // -a option, non-coherent averaging
static int CFLAG = 0;                          // -c option, coherent averaging
static int PFLAG = 0;                // -p option, pad timing breaks with zeros
static int KFLAG = 0;                                 // -k option, repeat mode
static int XFLAG = 0;                                            // Experiments

static int nfinal = 0;        // Number of FT frames finalised into the average
static int nmax = 0;               // -N option, number of FT frames to average
static int chans = 0;

static char *hamming = "rect";

///////////////////////////////////////////////////////////////////////////////
//  Goertzels                                                                //
///////////////////////////////////////////////////////////////////////////////

typedef double GTYPE;

static struct GOERTZEL
{
   double f;
   double realW;
   double imagW;

   GTYPE d1, d2;

   complex double pa;
   double pwr;
   int N;
}
 **filters = NULL;

#define filter_cell(chan, bin) (&filters[chan][bin])

static void init_goertzels( void)
{
   int i, c;

   // Decide the number of filters, according to the requested width and
   // resolution.  Make it an odd number so that we always have a center
   // bin at frequency fp->f.

   NF = fwidth/2/dF + 0.5;
   NF = NF * 2 + 1;
   VT_report( 1, "NF %d", NF);

   filters = VT_malloc( sizeof(struct GOERTZEL *) * chans);
   for( c = 0; c<chans; c++)
      filters[c] = VT_malloc( sizeof( struct GOERTZEL) * NF);

   for( c = 0; c<chans; c++)
   { 
      for( i=0; i<NF; i++)
      {
         struct GOERTZEL *fp = filter_cell( c, i);
         fp->f = fcenter + (i - NF/2) * dF;
         fp->d1 = fp->d2 = 0;
         fp->pa = 0;
         fp->pwr = 0;
      }
   }
}

static inline void run_goertzel( double v, int c)
{
   int i;

   for( i=0; i<NF; i++)
   {
      struct GOERTZEL *fp = filter_cell( c, i);
      GTYPE y;
      y = v + fp->realW * fp->d1 - fp->d2;
      fp->d2 = fp->d1;
      fp->d1 = y;
   }
}

static void reset_goertzels( void)
{
   int c, i;
   for( c = 0; c<chans; c++)
      for( i=0; i<NF; i++)
      {
         struct GOERTZEL *fp = filter_cell( c, i);
         fp->d1 = fp->d2 = 0;
         fp->pa = 0;
         fp->pwr = 0;
      }
}

static void set_coeffs( double offset)
{
   int i, c;

   for( c = 0; c<chans; c++)
      for( i=0; i<NF; i++)
      {
         struct GOERTZEL *fp = filter_cell( c, i);
    
         double p = 2 * M_PI * (fp->f + offset)/sample_rate;
         fp->realW = 2 * cos( p);
         fp->imagW = sin( p);
      }
}

///////////////////////////////////////////////////////////////////////////////
//  Windowing                                                                //
///////////////////////////////////////////////////////////////////////////////

static double *hwindow = NULL;                       // Window data, if cached
static double (*hfunction)(uint64_t) = NULL;  // Window function, if on the fly

static double wp;   // Set to M_PI / (NS - 1)

static double window_cosine( uint64_t i)
{
   return sin( i * wp);
}

static double window_hann( uint64_t i)
{
   double a = sin( i * wp);
   return a * a;
}

static double window_blackman( uint64_t i)
{
   double a0 = (1 - 0.16)/2;
   double a1 = 0.5;
   double a2 = 0.16/2;

   return a0 - a1 * cos( i * 2 * wp) + a2 * cos( i * 4 * wp);
}

static double window_hamming( uint64_t i)
{
   return 0.54 - 0.46 * cos( i * 2 * wp);
}

static double window_nuttall( uint64_t i)
{
   double a0 = 0.355768, a1 = 0.487396, a2 = 0.144232, a3 = 0.012604;

   return a0 - a1 * cos( i * 2 * wp)
             + a2 * cos( i * 4 * wp)
             - a3 * cos( i * 6 * wp);
}

static void setup_hamming( void)
{
   wp = M_PI/(NS - 1);  // Used by the window functions

   if( !strcasecmp( hamming, "rect")) return;  // No window function

   if( !strcasecmp( hamming, "cosine"))
      hfunction = window_cosine;
   else
   if( !strcasecmp( hamming, "hann"))
      hfunction = window_hann;
   else
   if( !strcasecmp( hamming, "blackman"))
      hfunction = window_blackman;
   else
   if( !strcasecmp( hamming, "hamming"))
      hfunction = window_hamming;
   else
   if( !strcasecmp( hamming, "nuttall"))
      hfunction = window_nuttall;
   else
      VT_bailout( "unknown window function: %s", hamming);

   //
   // Decide whether to pre-calculate the window function, or do it on-the-fly.
   //

   if( !KFLAG && !CFLAG && !AFLAG ||
       nmax == 1)
   {
      // Window only used once, so call window function on-the-fly
      VT_report( 2, "window used once only");
      return;
   }

   size_t n = sizeof( double) * NS;
   if( n > 100 * 1024 * 1024)
   {
      // More than 100Mbyte cache needed, so call on-the-fly
      VT_report( 2, "cache size would have been %d", (int) n);
      return;
   }

   hwindow = malloc( n);
   if( !hwindow)
   {
      // Not enough memory, have to call on-the-fly
      return;
   }

   // Pre-calculate the window coefficients
   VT_report( 2, "window cache size %.1f MByte", n / 1024.0 / 1024.0);

   int i;
   for( i=0; i<NS; i++) hwindow[i] = hfunction( i);
}

//
//  Output the current spectrum and reset things ready for the next (if any).
//

static void output_spectrum( void)
{
   int bin, c;

   double *m = VT_malloc( sizeof( double) * chans),
          *p = VT_malloc( sizeof( double) * chans),
          *msq = VT_malloc( sizeof( double) * chans);
   for( c=0; c<chans; c++) m[c] = p[c] = msq[c] = 0;
 
   VT_report( 1, "avg: %d", filter_cell(0,0)->N);

   for( bin=0; bin<NF; bin++)
   {
      if( KFLAG)
      {
         char temp[30];  timestamp_string3( specdate, temp);
         printf( "%s %.8f", temp, filter_cell( 0, bin)->f);
      }
      else
         printf( "%.8f", filter_cell( 0, bin)->f);
 
      for( c=0; c<chans; c++)
      {
         struct GOERTZEL *fp = filter_cell( c, bin);
   
         fp->pa /= nfinal;
         double rms = sqrt( fp->pwr/nfinal);
   
         if( AFLAG) printf( " %.6e", rms);
         else
            printf( " %.6e %.6e %.6e", creal( fp->pa), cimag( fp->pa), rms);

         m[c] += rms;
         msq[c] += rms * rms;
         if( rms > p[c]) p[c] = rms;
      }

      printf( "\n");
   }

   for( c=0; c<chans; c++)
   {
      m[c] /= NF;
      double var = msq[c] / NF - m[c] * m[c];

      VT_report( 1, "amplitudes %d m/p/r: %.3e %.3e %.3f p-m %.2f sigma",
                     c, m[c], p[c], p[c]/m[c], (p[c]-m[c])/sqrt(var));
   }

   free( m); free( p); free( msq);

   nfinal = 0;
   reset_goertzels();
   fflush( stdout);
}

//
//  Called at end of each transform period - every NS frames.
//

static void finalise( void)
{
   int i, c;

   timestamp T = timestamp_add( refdate, nif/(double)sample_rate);
   
   VT_report( 2, "finalise %d %lu", nfinal, (long unsigned) nif);

   for( c=0; c<chans; c++)
   {
      for( i=0; i<NF; i++)
      {
         struct GOERTZEL *fp = filter_cell( c, i);
         complex double a = 0.5 * fp->realW * fp->d1 - fp->d2
                            + I * fp->imagW * fp->d1;
   
         double ph = VT_phase( T, fp->f);

         a *= cos( ph) - I*sin( ph);
         if( XFLAG && cabs(a) != 0) a *= a / cabs(a);

         fp->pa += a / nif * 2;
//         fp->pa += a * (cos( ph) - I*sin( ph)) / nif * 2;
         fp->pwr += cabs( a) * cabs( a) / (nif * nif) * 2;
         fp->d1 = fp->d2 = 0;
      }
   }
 
   nfinal++;
   nif = 0;
   specdate = refdate;
   refdate = VT_get_timestamp( vtfile);
}

static void usage( void)
{
   fprintf( stderr,
      "usage: vtnspec [options] [input [output]]\n"
      "\n"
      "options:\n"
      "  -v        Increase verbosity\n"
      "  -B        Run in background\n"
      "  -L name   Specify log file\n"
      "  -f freq   Center frequency, Hz\n"
      "  -r res    Resolution, Hz\n"
      "  -w width  Width, Hz\n"
      "  -N count  Average this many transform frames\n"
      "  -a        Non-coherent averaging\n"
      "  -c        Coherent averaging\n"
      "  -p        Pad timing breaks with zeros\n"
      "\n"
      "  -W window Select window function\n"
      "            -W cosine\n"
      "            -W blackman\n"
      "            -W hamming\n"
      "            -W nuttall\n"
      "            -W hann\n"
      "            -W rect (default)\n"
    );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtnspec");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "pvBcxkar:f:w:N:W:L:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'r') dF = atof( optarg);
      else
      if( c == 'f') fcenter = atof( optarg);
      else
      if( c == 'w') fwidth = atof( optarg);
      else
      if( c == 'c') CFLAG = 1;
      else
      if( c == 'a') AFLAG = 1;
      else
      if( c == 'p') PFLAG = 1;
      else
      if( c == 'k') KFLAG = 1;
      else
      if( c == 'x') XFLAG = 1;
      else
      if( c == 'W') hamming = strdup( optarg);
      else
      if( c == 'N') nmax = atoi( optarg);
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

   VT_report( 1, "resolution: %.3f", dF);
   VT_report( 1, "center: %.6f", fcenter);
   VT_report( 1, "width: %.6f", fwidth);
   
   if( !fcenter) VT_bailout( "needs -f option to specify center frequency");
   if( !dF) VT_bailout( "needs -r option to specify resolution");

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);
   if( (vtfile = VT_open_input( bname)) == NULL)
      VT_bailout( "cannot open input %s: %s", bname, VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   int c;

   sample_rate = VT_get_sample_rate( vtfile);
   
   NS = 0.5 + sample_rate/dF;     // Number of samples to analyse per transform

   setup_hamming();
   init_goertzels();
   set_coeffs( 0);
   
   refdate = VT_get_timestamp( vtfile);

   while( 1)
   {
      double *inframe = VT_get_frame( vtfile);
      if( !inframe)
      {
         VT_report( 1, "end of input");
         if( PFLAG)
         {
            VT_report( 1, "padding %lu samples", (long unsigned) (NS - nif));
            while( nif++ < NS)
               for( c=0; c<chans; c++) run_goertzel( 0, c);
         }
         break;
      }

      double w;
      if( hwindow) // Using pre-calculated window coefficients?
         w = hwindow[nif];
      else
      if( hfunction)  // Calling window function on-the-fly
         w = hfunction( nif);
      else
         w = 1;   // Rectangular window

      for( c=0; c<chans; c++)
      { 
         double v = inframe[chspec->map[c]] * w;
         run_goertzel( v, c);
      }

      if( ++nif == NS)
      {
         finalise();
         if( KFLAG)
         {
            if( !nmax || nmax == nfinal) output_spectrum();
         }
         else
         {
            if( !CFLAG && !AFLAG) break;
            else
            if( nmax && nfinal == nmax) break;
         }
      }
   }

   VT_report( 2, "NS=%lu", (long unsigned) NS);
   VT_report( 2, "end nif=%lu, %.5f", (long unsigned) nif, nif/(double)NS);
   if( !nfinal || nif/(double)NS > 0.9) finalise();

   if( AFLAG || CFLAG) VT_report( 1, "nfinal %d", nfinal);
   output_spectrum();

   return 0;
}

