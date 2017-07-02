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

static VTFILE *vtinfile, *vtoutfile;
static char *inname = NULL;
static char *outname = NULL;
static int sample_rate = 0;
static double gain = 1.0;

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtam [options] input output\n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -B            Run in background\n"
       "  -L name       Specify logfile\n"
       "\n"
       "  -g gain       Gain (default 1.0)\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtam");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBL:F:g:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'g') gain = atof( optarg);
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

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( inname);
   vtinfile = VT_open_input( inname);
   if( !vtinfile)
      VT_bailout( "cannot open input %s: %s", inname, VT_error);

   sample_rate = VT_get_sample_rate( vtinfile);

   VT_init_chanspec( chspec, vtinfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chspec->n, sample_rate);

   if( chspec->n % 2) VT_bailout( "input must have pairs of I/Q signals");
   
   vtoutfile = VT_open_output( outname, chspec->n/2, 0, sample_rate);
   if( !vtoutfile) VT_bailout( "cannot open %s: %s", outname, VT_error);
   
   double *outframe = VT_malloc( sizeof( double) * chspec->n/2);
   
   while( 1)
   {
      int isblk = VT_is_block(vtinfile);
      if( isblk < 0)
      {
         VT_report( 1, "end of input");
         break;
      }
   
      double *inframe = VT_get_frame( vtinfile);
   
      int i;
      for( i=0; i<chspec->n/2; i++)
      {
         double vi = inframe[chspec->map[i*2+0]];
         double vq = inframe[chspec->map[i*2+1]];
         outframe[i] = sqrt( vi * vi + vq * vq);

         if( isblk) VT_set_timebase( vtoutfile, 
                                     VT_get_timestamp( vtinfile),
                                     VT_get_srcal( vtinfile));
         VT_insert_frame( vtoutfile, outframe);
      }
   }
   VT_release( vtoutfile);
   return 0;
}


