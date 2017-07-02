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

static int chmask = 1;   // Bit map of AIN units to read
static int sample_rate = 0;

static char *bname;
static VTFILE *vtfile;

static double offset = 0;
static double gain = 1.0;

static char *ain_path;    // Directory containing AIN device nodes

static void usage( void)
{
   fprintf( stderr,
       "usage:    vtain [options] buffer_name\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify log file\n"
       "\n"
       "  -c mask   Bit map of AIN inputs to read\n"
       "            mask = 0..255\n"
       "  -r rate   Sample rate\n"
       "\n"
       "  -z offset Zero offset 0..4095 (default 0)\n"
       "  -g gain   Signal gain (default 1.0)\n"
     );
   exit( 1);
}

static double read_ain( int unit)
{
   char file[100], buff[5];

   sprintf( file, "%s/ain%d", ain_path, unit+1);

   int f = open( file, O_RDONLY);
   if( f < 0) VT_bailout( "cannot open %s: %s", file, strerror( errno));
   int n = read( f, buff, 4);
   if( n < 0) VT_bailout( "cannot read %s: %s", file, strerror( errno));
   close( f);

   buff[n] = 0;
   return (atof( buff) - offset) * gain/4096.0;
}

static void run( void)
{
   timestamp T = VT_rtc_time();
   double dT = 1.0/sample_rate;

   VT_set_timebase( vtfile, T, 1.0);

   double frame[8];

   while( 1)
   {
      // Read inputs
      int i, c;
      for( i=c=0; i<8; i++) if( chmask & (1 << i)) frame[c++] = read_ain( i);

      VT_insert_frame( vtfile, frame);
      T = timestamp_add( T, dT);

      while( timestamp_LT( VT_rtc_time(), T)) usleep( 2000);
   }
}

int main( int argc, char *argv[])
{
   VT_init( "vtain");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBc:r:L:g:z:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'c') chmask = atoi( optarg);
      else
      if( c == 'r') sample_rate = atoi( optarg);
      else
      if( c == 'z') offset = atof( optarg);
      else
      if( c == 'g') gain = atof( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( chmask < 0 || chmask > 255) VT_bailout( "invalid -c argument");
   if( sample_rate <= 0)
      VT_bailout( "invalid or missing sample rate, needs -r");

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   // Determine which path reaches the AIN nodes on this version

   struct stat tb;
   if( stat( "/sys/devices/platform/omap/tsc", &tb) == 0 &&
       S_ISDIR( tb.st_mode)) ain_path = "/sys/devices/platform/omap/tsc";
   else
   if( stat( "/sys/devices/platform/omap/tsc", &tb) == 0 &&
       S_ISDIR( tb.st_mode)) ain_path = "/sys/devices/platform/omap/tsc";
   else
      VT_bailout( "cannot find AIN devices");

   VT_report( 1, "using %s", ain_path);

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDOUT : 0;
      VT_daemonise( flags);
   }

   int i, chans = 0;
   for( i=0; i<8; i++) if( chmask & (1 << i)) chans++;

   VT_report( 1, "buffer name: [%s]", bname);
   VT_report( 1, "channel mask: %d", chmask);
   VT_report( 1, "channels: %d", chans);

   vtfile = VT_open_output( bname, chans, 1, sample_rate);
   if( !vtfile) VT_bailout( "cannot create buffer: %s", VT_error);

   run();
   return 0;
}

