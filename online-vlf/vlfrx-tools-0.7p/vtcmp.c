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

static double *buf[2];

static int sample_rate;
static double Etime = 0;                                      // From -E option
static double Stime = 0;                                      // From -S option
static int NB = 0;                     // From -w, number of samples per buffer
static double fmrange = 0;                                    // From -w option
static int nfinal = 0;                            // Number of buffers averaged
static int RFLAG = 0;
static double Tstride = 0;          // From -s option
static double limit_F1, limit_F2;
static timestamp TS = timestamp_ZERO, TE = timestamp_ZERO;
static int FFLAG = 0;

static double **work;
static timestamp Twork;

static int mode = 0;         
#define MODE_COR 1
#define MODE_PD180 2
#define MODE_PD360 3

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtcmp [options] input output\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify logfile\n"
       "\n"
       "  -m mode   Type of comparison:\n"
       "            cor    Cross-correlation\n"
       "            pd180  Phase difference, mod 180\n"
       "            pd360  Phase difference, mod 360\n"
       "\n"
       "  -F freqspec  Select frequency range\n"
       "  -w secs   Comparison width\n"
       "  -S secs   Skip seconds before starting work\n"
       "  -E secs   Average for this many seconds\n"
       "  -r        Envelope correlation\n"
     );
   exit( 1);
}

static void parse_mode( char *s)
{
   if( !strcmp( s, "cor")) mode = MODE_COR;
   else
   if( !strcmp( s, "pd180")) mode = MODE_PD180;
   else
   if( !strcmp( s, "pd360")) mode = MODE_PD360;
   else
      VT_bailout( "undefined comparison mode [%s]", s);
}

///////////////////////////////////////////////////////////////////////////////
//  Cross-correlation                                                        //
///////////////////////////////////////////////////////////////////////////////

static complex double *B1, *B2, *B3;
static fftw_plan ffp_f1, ffp_f2, ffp_r;
static double *d3;
static double *xbuf;

static void init_xcorrelation( void)
{
   B1 = VT_malloc( NB * sizeof( complex double));
   B2 = VT_malloc( NB * sizeof( complex double));
   B3 = VT_malloc( NB * sizeof( complex double));

   d3 = VT_malloc( NB * sizeof( double));
   
   ffp_f1 = fftw_plan_dft_r2c_1d( NB, work[0], B1, FFTW_ESTIMATE);
   ffp_f2 = fftw_plan_dft_r2c_1d( NB, work[1], B2, FFTW_ESTIMATE);
   ffp_r = fftw_plan_dft_c2r_1d( NB, B3, d3, FFTW_ESTIMATE);

   xbuf = VT_malloc_zero( NB * sizeof( double));
}

static void process_xcorrelation( void)
{
   int i;

   if( !RFLAG)
   {
      // Subtract the mean from each channel
      double mean1 = 0, mean2 = 0;
      for( i=0; i<NB; i++)
      {
         mean1 += work[0][i];
         mean2 += work[1][i];
      }
   
      mean1 /= NB;  mean2 /= NB;
   
      for( i=0; i<NB; i++)
      {
         work[0][i] -= mean1;
         work[1][i] -= mean2;
      }
   }

   // Fourier transform both channels
   fftw_execute( ffp_f1);
   fftw_execute( ffp_f2);

   // Calculate the RMS amplitudes of the Fourier transforms
   double mag1 = 0, mag2 = 0;

   for( i=0; i<NB; i++)
   {
      mag1 += cabs( B1[i]) * cabs( B1[i]);
      mag2 += cabs( B2[i]) * cabs( B2[i]);
   }
   
   mag1 = sqrt( mag1);
   mag2 = sqrt( mag2);
   double scale = mag1 * mag2 * 2;

   if( scale == 0) VT_bailout( "zero input");

   for( i=0; i<NB; i++) B3[i] = conj( B1[i]) * B2[i]/scale;

   // Apply band limiting if requested by -F
   if( FFLAG)
   {
      double dF = sample_rate/(double) NB;
      int n1 = limit_F1/dF; 
      int n2 = limit_F2/dF; 

      if( n1 < 0) n1 = 0;   if( n1 > NB) n1 = NB;
      if( n2 < 0) n2 = 0;   if( n2 > NB) n2 = NB;

      for( i=0; i<n1; i++) B3[i] = 0;
      for( i=n2+1; i<NB; i++) B3[i] = 0;
   }

   fftw_execute( ffp_r);

   for( i = 0; i < NB; i++) xbuf[i] += d3[i];
}

static void output_xcorrelation( void)
{
   int i;
   double DT = 1/(double) sample_rate;

   for( i=NB/2; i<NB; i++)
   {
      double v = xbuf[i]/nfinal;  // if( RFLAG && v < 0) v = -v;
      if( Tstride)
      {
         char temp[30]; timestamp_string6( Twork, temp); printf( "%s ", temp);
      }
      printf( "%.6e %.6e\n", (i - NB) * DT, v);
   }

   for( i=0; i<NB/2; i++)
   {
      double v = xbuf[i]/nfinal;  // if( RFLAG && v < 0) v = -v;
      if( Tstride)
      {
         char temp[30]; timestamp_string6( Twork, temp); printf( "%s ", temp);
      }
      printf( "%.6e %.6e\n", i * DT, v);
   }

   memset( xbuf, 0, sizeof( double) * NB);
}

///////////////////////////////////////////////////////////////////////////////
//  Phase Comparison                                                         //
///////////////////////////////////////////////////////////////////////////////

static fftw_plan p1 = 0;
static fftw_plan p2 = 0;
static complex double *X1 = 0, *X2 = 0;
static double *p_asin = NULL;
static double *p_acos = NULL;
static double *p_mag1 = NULL;
static double *p_mag2 = NULL;

static void init_phasecomp( void)
{
   X1 = VT_malloc( NB * sizeof( complex double));
   X2 = VT_malloc( NB * sizeof( complex double));
   p1 = fftw_plan_dft_r2c_1d( NB, buf[0], X1,
                            FFTW_ESTIMATE | FFTW_DESTROY_INPUT);
   p2 = fftw_plan_dft_r2c_1d( NB, buf[1], X2,
                            FFTW_ESTIMATE | FFTW_DESTROY_INPUT);

   p_asin = VT_malloc_zero( NB/2 * sizeof( double));
   p_acos = VT_malloc_zero( NB/2 * sizeof( double));

   p_mag1 = VT_malloc_zero( NB/2 * sizeof( double));
   p_mag2 = VT_malloc_zero( NB/2 * sizeof( double));
}

static void process_phasecomp( void)
{
   fftw_execute( p1);
   fftw_execute( p2);

   int bins = NB/2;
   int i;

   for( i=0; i<bins; i++)
   {
      double m1 = cabs( X1[i]);
      double m2 = cabs( X2[i]);
      double a = atan2( cimag( X1[i]) * creal( X2[i]) -
                        creal( X1[i]) * cimag( X2[i]),
                        creal( X1[i]) * creal( X2[i]) + 
                        cimag( X1[i]) * cimag( X2[i]) );

      if( mode == MODE_PD180) a *= 2;

      p_asin[i] += sin( a);
      p_acos[i] += cos( a);
      p_mag1[i] += m1 * m1;
      p_mag2[i] += m2 * m2;
   }
}

static void output_phasecomp( void)
{
   double dF = sample_rate/(double) NB;

   int bins = NB/2;
   int i;

   for( i=0; i<bins; i++)
   {
      double sa = p_asin[i]/nfinal;
      double ca = p_acos[i]/nfinal;
      double mean_phase = atan2( sa, ca);

      if( mode == MODE_PD180) mean_phase /= 2;

      double m1 = sqrt( p_mag1[i]/nfinal);
      double m2 = sqrt( p_mag2[i]/nfinal);

      if( Tstride)
      {
         char temp[30]; timestamp_string6( Twork, temp); printf( "%s ", temp);
      }
      printf( "%.1f %.4e %.4e %.3f\n", i * dF, m1, m2, mean_phase * 180/M_PI);
   }

   memset( p_asin, 0, NB/2 * sizeof( double));
   memset( p_acos, 0, NB/2 * sizeof( double));
   memset( p_mag1, 0, NB/2 * sizeof( double));
   memset( p_mag2, 0, NB/2 * sizeof( double));
}

///////////////////////////////////////////////////////////////////////////////
// Main                                                                      //
///////////////////////////////////////////////////////////////////////////////

int main( int argc, char *argv[])
{
   VT_init( "vtcmp");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBrw:m:S:E:L:F:s:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'm') parse_mode( optarg);
      else
      if( c == 'w') fmrange = atof( optarg);
      else
      if( c == 'E') Etime = atof( optarg);
      else
      if( c == 'S') Stime = atof( optarg);
      else
      if( c == 's') Tstride = atof( optarg);
      else
      if( c == 'F')
      {
         VT_parse_freqspec( optarg, &limit_F1, &limit_F2);
         FFLAG = 1;
      }
      else
      if( c == 'r') RFLAG = 1;
      else
      if( c == -1) break;
      else
         usage();
   }

   if( argc > optind + 1) usage();

   char *name =  strdup( optind < argc ? argv[optind] : "-");
   struct VT_CHANSPEC *chspec = VT_parse_chanspec( name);

   if( background)
   {
      int flags = name[0] == '-' ? KEEP_STDIN : 0;
      flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   VTFILE *vtfile = VT_open_input( name);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   int chans = chspec->n;
   if( chans != 2) VT_bailout( "must specify two input channels");

   sample_rate = VT_get_sample_rate( vtfile);

   // Setup input buffer for each channel
   NB = sample_rate * fmrange;
   if( NB <= 0) VT_bailout( "missing or invalid -w argument");
   
   VT_report( 1, "buffer size: %d", NB);

   if( FFLAG)
   {
      if( !limit_F2) limit_F2 = sample_rate/2.0;
      VT_report( 1, "applying filter: %.3f %.3f", limit_F1, limit_F2);
   }

   int i;
   work = VT_malloc( sizeof( double *) * chans);
   for( i=0; i<chans; i++)
   {
      buf[i] = VT_malloc( sizeof( double) * NB);
      work[i] = VT_malloc( sizeof( double) * NB);
   }

   int breaks = 0;
   int e;

   timestamp Tstart = VT_get_timestamp( vtfile);
   timestamp Tlast = timestamp_ZERO;

   if( !timestamp_is_ZERO( TS) && Stime)
      VT_bailout( "ambiguous start time, -T and -S both given");

   if( Stime) TS = timestamp_add( Tstart, Stime);
   if( !timestamp_is_ZERO( TE) && Etime)
      VT_bailout( "ambiguous end time, -T and -E both given");

   if( Etime) TE = timestamp_add( Tstart, Etime);

   switch( mode)
   {
      case MODE_COR: init_xcorrelation(); break;

      case MODE_PD180:
      case MODE_PD360: init_phasecomp();  break;
   }

   int nbuf = 0;                             // Number of samples in the buffer

   while( 1)
   {
      if( (e = VT_rbreak( vtfile)) != 0)
      {
         if( e < 0) break;   // End of stream
         breaks++;
      }
   
      timestamp T = VT_get_timestamp( vtfile);
      double *frame = VT_get_frame( vtfile);

      if( timestamp_LE( T, Tlast))
         continue;    // Discard input that steps backward in time
      if( timestamp_LT( T, TS)) continue;         // Implementing the -S option
      if( !timestamp_is_ZERO( TE) && timestamp_GT( T, TE)) break;

      Tlast = T;

      for( i = 0; i < chans; i++)
      {
         double v = frame[chspec->map[i]];
         buf[i][nbuf] = RFLAG ? v * v : v;
      }

      if( ++nbuf < NB) continue;
      nbuf = 0;
      Twork = timestamp_add( T, -(NB - 1)/(double) sample_rate);

      // Full buffer, compare and add to average

      for( i = 0; i < chans; i++) memcpy( work[i], buf[i], NB * sizeof( double));

      switch( mode)
      {
         case MODE_COR: process_xcorrelation(); break;

         case MODE_PD180:
         case MODE_PD360: process_phasecomp();  break;
      }

      nfinal++;

      if( Tstride)
      {
         switch( mode)
         {
            case MODE_COR: output_xcorrelation(); break;
      
            case MODE_PD180:
            case MODE_PD360: output_phasecomp();  break;
         }
      
         int n = Tstride * sample_rate;
            for( i = 0; i < chans; i++)
               memmove( buf[i], buf[i] + n, (NB - n) * sizeof( double));
         nbuf = n;
         nfinal = 0;
      }
   }

   // End of input.   Output the accumulated average.

   VT_report( 1, "nfinal: %d", nfinal);
 
   if( nfinal)
      switch( mode)
      {
         case MODE_COR: output_xcorrelation(); break;
   
         case MODE_PD180:
         case MODE_PD360: output_phasecomp();  break;
      }

   return 0;
}

