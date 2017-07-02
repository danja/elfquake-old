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

#include <samplerate.h>

static VTFILE *vtinfile, *vtoutfile;
static char *inname = NULL;
static char *outname = NULL;
static int in_rate = 0;
static int out_rate = 0;
static int quality = 0;
static double tweak = 0;

#if 0
static double measure_src_lag( SRC_STATE *src, SRC_DATA *srcd, int nr)
{
   int i, out = 0;

   memset( srcd->data_in, 0, sizeof( float) * nr);
   srcd->input_frames = nr;
   srcd->output_frames = nr * srcd->src_ratio + 1;
   if( src_process( src, srcd)) VT_bailout( "src_process error");
   if( srcd->input_frames_used != nr) VT_bailout( "frames not used (1)");

   while( 1)
   {
      for( i=0; i<nr; i++) srcd->data_in[i] = 1.0;
      srcd->input_frames = nr;
      srcd->output_frames = nr * srcd->src_ratio + 1;
      if( src_process( src, srcd)) VT_bailout( "src_process error");
      if( srcd->input_frames_used != nr) VT_bailout( "frames not used (2)");
   
      for( i=0; i<srcd->output_frames_gen; i++, out++)
         if( srcd->data_out[i] > 0.5)
         {
            double lag = out/(double)(in_rate * srcd->src_ratio);
            VT_report( 1, "src delay %.8f", lag);
            src_reset( src);
            return lag;
         }
   }
}
#endif

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtresample [options] input output\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify logfile\n"
       "  -r        Output sample rate\n"
       "  -g gain   Gain applied after resampling\n"
       "  -q qual   Quality\n"
       "              0  fastest\n"
       "              1  medium\n"
       "              2  best\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtresample");

   int background = 0;
   double gain = 1;

   while( 1)
   {
      int c = getopt( argc, argv, "vBr:g:q:c:L:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'r') out_rate = atoi( optarg);
      else
      if( c == 'c') tweak = atof( optarg);
      else
      if( c == 'g') gain = atof( optarg);
      else
      if( c == 'q') quality = atoi( optarg);
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

   if( !out_rate) VT_bailout( "output rate not specified");

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( inname);
   if( (vtinfile = VT_open_input( inname)) == NULL)
      VT_bailout( "cannot open input %s: %s", inname, VT_error);

   in_rate = VT_get_sample_rate( vtinfile);
   VT_init_chanspec( chspec, vtinfile);

   VT_report( 1, "channels: %d, input rate: %d, output rate: %d",
              chspec->n, in_rate, out_rate);
   
   vtoutfile = VT_open_output( outname, chspec->n, 0, out_rate);
   if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);

   int src_err;
   int src_quality = 0;
   switch( quality)
   {
      case 0: src_quality = SRC_SINC_FASTEST; break;
      case 1: src_quality = SRC_SINC_MEDIUM_QUALITY; break;
      case 2: src_quality = SRC_SINC_BEST_QUALITY; break;
      default: VT_bailout( "invalid -q");
   }

   SRC_STATE *src = src_new ( src_quality, chspec->n, &src_err);
   if( !src) VT_bailout( "resampler init failed: %d", src_err);

   SRC_DATA srcd;
   int NREAD = in_rate/8;
   srcd.src_ratio = out_rate/(double) in_rate;
   if( tweak) srcd.src_ratio *= tweak;
   int inbufsize = NREAD;
   int outbufsize = NREAD * srcd.src_ratio + 1;
   srcd.data_in = VT_malloc( sizeof( float) * chspec->n * inbufsize);
   srcd.data_out = VT_malloc( sizeof( float) * chspec->n * outbufsize);
   srcd.end_of_input = 0;

   // measure_src_lag( src, &srcd, NREAD);

   double *inframe;
   int q = 0;
   uint64_t nin = 0, nout = 0;

   timestamp timebase = timestamp_ZERO;
   double srcal = 1.0;
   uint64_t nfbase = 0;
   double *outframe = VT_malloc( sizeof( double) * chspec->n);
   int i, j, e;

   while( 1)
   {
      if( (e = VT_is_block( vtinfile)) < 0)
      {
         VT_report( 1, "end of input");
         break;
      }

      if( e)
      {
         timebase = VT_get_timestamp( vtinfile);
         srcal = VT_get_srcal( vtinfile);
         nfbase = nin;
         // int tbreak = VT_rbreak( vtinfile);
      }

      inframe = VT_get_frame( vtinfile);   nin++;
   
      for( j=0; j<chspec->n; j++) 
         srcd.data_in[q*chspec->n+j] = inframe[chspec->map[j]];

      if( ++q == inbufsize)
      {
         srcd.input_frames = inbufsize;
         srcd.output_frames = outbufsize;
         if( src_process( src, &srcd)) VT_bailout( "src_process error");
         if( srcd.input_frames_used != q)
            VT_bailout( "frames not used, %d remain",
               (int) (q - srcd.input_frames_used));

         q = srcd.output_frames_gen;
         for( i=0; i<q; i++)
         {
            if( !vtoutfile->nfb)
            {
               timestamp T =
                  timestamp_add( timebase,
                       ((nout + i)/srcd.src_ratio - nfbase)/(srcal * in_rate));
               VT_set_timebase( vtoutfile, T, srcal);
            }
   
            for( j=0; j<chspec->n; j++)
               outframe[j] = gain * srcd.data_out[i*chspec->n + j];
            VT_insert_frame( vtoutfile, outframe);
         }
  
         nout += q;
         q = 0;
      }
   }

   return 0;
}

