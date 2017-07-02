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

static VTFILE *vtfile;
static char *inname = NULL;
static int sample_rate = 0;

static FILE *fo = NULL;

static char *datadir = NULL;
static int32_t optime = 0;
static char opfilename[500];
static int gran = 86400;

static void close_file( void)
{
   if( fo) fclose( fo);
   fo = NULL;
}

static void maybe_open_file( time_t secs)
{
   int32_t tx = secs / gran;

   if( fo && tx == optime) return;

   close_file();

   optime = tx;

   struct tm *tm = gmtime( &secs);
   sprintf( opfilename, "%s/%02d%02d%02d-%02d%02d%02d",
            datadir,
            tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec); 

   VT_report( 1, "output file %s", opfilename);
   if( (fo = fopen( opfilename, "w")) == NULL)
      VT_bailout( "cannot open %s: %s", opfilename, strerror( errno)); 
}

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtwrite [options] input output_path\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify logfile\n"
       "  -G gran   File granularity, seconds\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtwrite");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBi:G:L:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'G') gran = atoi( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( optind + 2 == argc)
   {
      inname = strdup( argv[optind]);
      datadir = strdup( argv[optind+1]);
   }
   else
   if( optind + 1 == argc)
   {
      inname = strdup( "-");
      datadir = strdup( argv[optind]);
   }
   else usage();

   if( background)
   {
      int flags = inname[0] == '-' ? KEEP_STDIN : 0;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( inname);
   vtfile = VT_open_input( inname);
   if( !vtfile) 
      VT_bailout( "cannot open input %s: %s", inname, VT_error);

   sample_rate = VT_get_sample_rate( vtfile);

   VT_init_chanspec( chspec, vtfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chspec->n, sample_rate);
   VT_report( 1, "file granularity: %d seconds", gran);

   do
   {
      if( VT_rbreak( vtfile))
      {
         VT_report( 0, "break detected");
         close_file();
      }

      int secs = timestamp_secs( VT_get_timestamp( vtfile));
      maybe_open_file( secs);
      
      if( fwrite( vtfile->blk, vtfile->bs, 1, fo) != 1)
         VT_bailout( "cannot write %s: %s", opfilename, strerror( errno));
      vtfile->ulp = vtfile->nfb;
      vtfile->nfb = 0;
   }
    while( VT_read_next( vtfile));

   return 0;
}

