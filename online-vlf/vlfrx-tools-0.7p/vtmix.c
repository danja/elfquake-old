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

#define MAXCHANS 32

static VTFILE *vtinfile, *vtoutfile;
static char *inname = NULL;
static char *outname = NULL;
static int sample_rate = 0;

static int BINS = 8192*2;
static int FFTWID = 0;

static struct CHANNEL
{
   fftw_plan ffp1, ffp2;
   fftw_complex *X;                                   // Working buffer for FFT
   double *buf1, *buf2;                                 // Two circular buffers
}
 *in_channels, *out_channels;

static int p_load1 = 0, p_load2 = 0;                       // Two load pointers
static double *sinsq;                                     // FT window function

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtmix [options] input output\n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -B            Run in background\n"
       "  -L name       Specify logfile\n"
       "  -c rowspec\n"
     );
   exit( 1);
}

static int rows = 0;
static int cols = 0;

static complex double matrix[MAXCHANS][MAXCHANS];

static int any_complex( void)
{
   int i, j;

   for( i=0; i<rows; i++)
      for( j=0; j<cols; j++) if( cimag( matrix[i][j])) return 1;

   return 0;
}

static void parse_matrix_row( char *s)
{
   if( rows == MAXCHANS)
      VT_bailout( "too many output channels specified (max %d)", MAXCHANS);

   int c = 0;

   while( s && *s)
   {
      if( c == MAXCHANS)
         VT_bailout( "too many input channels specified (max %d)", MAXCHANS);

      if( !VT_parse_complex( s, &matrix[rows][c]))
         VT_bailout( "cannot parse coefficient [%s]", s);

      c++;
      s = strchr( s, ',');
      if( !s) break;
      else s++;
   }

   if( !cols) cols = c;

   rows++;
}

static void init_transform( void)
{
   int i;

   FFTWID = 2 * BINS;

   sinsq = VT_malloc( FFTWID * sizeof( double));
   for( i=0; i<FFTWID; i++)
   {
      double theta = M_PI * i/(double) FFTWID;
      sinsq[i] = sin( theta) * sin( theta);
   }

   p_load1 = 0;
   p_load2 = FFTWID/2;
}

static void init_input_transform( struct CHANNEL *fp)
{
   int i;

   fp->buf1 = VT_malloc( FFTWID * sizeof( double));
   fp->buf2 = VT_malloc( FFTWID * sizeof( double));
   for( i=0; i<FFTWID; i++) fp->buf1[i] = fp->buf2[i] = 0;

   fp->X = VT_malloc( sizeof( fftw_complex) * (BINS+1));
   fp->ffp1 = fftw_plan_dft_r2c_1d( FFTWID, fp->buf1, fp->X, FFTW_ESTIMATE);
   fp->ffp2 = fftw_plan_dft_r2c_1d( FFTWID, fp->buf2, fp->X, FFTW_ESTIMATE);
}

static void init_output_transform( struct CHANNEL *fp)
{
   int i;

   fp->buf1 = VT_malloc( FFTWID * sizeof( double));
   fp->buf2 = VT_malloc( FFTWID * sizeof( double));
   for( i=0; i<FFTWID; i++) fp->buf1[i] = fp->buf2[i] = 0;

   fp->X = VT_malloc( sizeof( fftw_complex) * (BINS+1));
   fp->ffp1 = fftw_plan_dft_c2r_1d( FFTWID, fp->X, fp->buf1, FFTW_ESTIMATE);
   fp->ffp2 = fftw_plan_dft_c2r_1d( FFTWID, fp->X, fp->buf2, FFTW_ESTIMATE);
}

static void transform( double *inframe, int *map, double *outframe)
{
   int row, col, bin;

   for( row=0; row<rows; row++)
      outframe[row] = (out_channels[row].buf1[p_load1] + 
                       out_channels[row].buf2[p_load2]) / FFTWID;

   for( col=0; col<cols; col++)
   {
      double f = inframe[map[col]];
      in_channels[col].buf1[p_load1] = f * sinsq[p_load1];
      in_channels[col].buf2[p_load2] = f * sinsq[p_load2];
   }

   if( ++p_load1 == FFTWID)
   {
      p_load1 = 0;

      for( col=0; col<cols; col++) fftw_execute( in_channels[col].ffp1);

      for( row=0; row<rows; row++)
      {
         for( bin=0; bin<=BINS; bin++) out_channels[row].X[bin] = 0;

         for( col=0; col<cols; col++)
            for( bin=0; bin<=BINS; bin++)
               out_channels[row].X[bin] += 
                  in_channels[col].X[bin] * matrix[row][col];

         fftw_execute( out_channels[row].ffp1);
      }
   }

   if( ++p_load2 == FFTWID)
   {
      p_load2 = 0;

      for( col=0; col<cols; col++) fftw_execute( in_channels[col].ffp2);

      for( row=0; row<rows; row++)
      {
         for( bin=0; bin<=BINS; bin++) out_channels[row].X[bin] = 0;

         for( col=0; col<cols; col++)
            for( bin=0; bin<=BINS; bin++)
               out_channels[row].X[bin] +=
                  in_channels[col].X[bin] * matrix[row][col];

         fftw_execute( out_channels[row].ffp2);
      }
   }
}

static void mix_time_domain( struct VT_CHANSPEC *chspec)
{
   VT_report( 1, "mixing in time domain");

   double *inframe;
   double *outframe = VT_malloc( sizeof( double) * rows);
   int i, j;

   while( 1)
   {
      int e = VT_is_block( vtinfile);
      if( e < 0)
      {
         VT_report( 1, "end of input");
         break;
      }

      if( e) VT_set_timebase( vtoutfile, 
                              VT_get_timestamp( vtinfile), 
                              VT_get_srcal( vtinfile));

      inframe = VT_get_frame( vtinfile);

      for( i=0; i<rows; i++) 
      {
         outframe[i]  = 0;
         for( j=0; j<chspec->n; j++) 
         {
            outframe[i] += matrix[i][j] * inframe[chspec->map[j]];
         }
      }
      VT_insert_frame( vtoutfile, outframe);
   }
}

static void mix_freq_domain( struct VT_CHANSPEC *chspec)
{
   int i;

   VT_report( 1, "mixing in frequency domain");

   double *inframe;
   double *outframe = VT_malloc( sizeof( double) * rows);
   init_transform();

   in_channels = VT_malloc( cols * sizeof( struct CHANNEL));
   for( i=0; i<cols; i++) init_input_transform( in_channels+i);

   out_channels = VT_malloc( rows * sizeof( struct CHANNEL));
   for( i=0; i<rows; i++) init_output_transform( out_channels+i);

   double srcal = 1.0;
   int n = 0, e;

   while( 1)
   {
      if( (e = VT_is_block( vtinfile)) < 0)
      {
         VT_report( 1, "end of input");
         break;
      }

      if( e)  // Starting a new input block?
      {
         srcal = VT_get_srcal( vtinfile);
      }

      if( !vtoutfile->nfb)  // Starting a new output block?
      {
         timestamp T = 
           timestamp_add( VT_get_timestamp( vtinfile),
                     -FFTWID/(VT_get_srcal( vtinfile) * sample_rate)); 
         VT_set_timebase( vtoutfile, T, srcal);
      }

      inframe = VT_get_frame( vtinfile);

      transform( inframe, chspec->map, outframe);

      // Discard the first FFTWID frames as these are just leading zeros.
      if( n < FFTWID) n++;
      else
         VT_insert_frame( vtoutfile, outframe);
   }

   // Drain the FFT buffer.
   for( n=0; n<FFTWID; n++)
   {
      inframe = VT_malloc_zero( VT_get_chans( vtinfile) * sizeof( double));
      transform( inframe, chspec->map, outframe);
      VT_insert_frame( vtoutfile, outframe);
   }

   VT_release( vtoutfile);
}

int main( int argc, char *argv[])
{
   VT_init( "vtmix");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBc:L:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'c') parse_matrix_row( optarg);
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

   if( !rows) VT_bailout( "no output channels specified");

   if( background)
   {
      int flags = inname[0] == '-' ? KEEP_STDIN : 0;
      if( outname[0] == '-') flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( inname);
   vtinfile = VT_open_input( inname);
   if( !vtinfile) 
      VT_bailout( "cannot open input %s: %s", inname, VT_error);

   sample_rate = VT_get_sample_rate( vtinfile);

   VT_init_chanspec( chspec, vtinfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chspec->n, sample_rate);
   if( cols != chspec->n) VT_bailout( "incorrect number of matrix columns");
 
   vtoutfile = VT_open_output( outname, rows, 0, sample_rate);
   if( !vtoutfile)
      VT_bailout( "cannot open %s: %s", outname, VT_error);

   int i, j;

   for( i=0; i<rows; i++)
      for( j=0; j<chspec->n; j++)
      VT_report( 2, "matrix: %d %d = (%.3f%+.3fj)", i, j,
              creal( matrix[i][j]), cimag( matrix[i][j]));

   if( any_complex()) mix_freq_domain( chspec);
   else mix_time_domain( chspec);

   VT_release( vtoutfile);
   return 0;
}

