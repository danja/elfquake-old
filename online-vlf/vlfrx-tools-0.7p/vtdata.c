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

static int sample_rate = 0;

static char *bname;
static VTFILE *vtfile;

static int TFLAG = FALSE;         // Set by -t option: throttle to real time
static int IFLAG = FALSE;         // Set by -i option: timestamps from input

static timestamp TS = timestamp_ZERO;      // From -T option
static int has_TS = FALSE;        // TRUE if -T is given

static double gain = 1.0;
static int chans = 0;

static void usage( void)
{
   fprintf( stderr,
       "usage:    vtdata [options] [buffer_name]\n"
       "\n"
       "options:\n"
       "  -v           Increase verbosity\n"
       "  -B           Run in background\n"
       "  -L name      Specify log file\n"
       "\n"
       "  -c chans     Number of channels\n"
       "  -r rate      Sample rate\n"
       "  -g gain      Signal gain (default 1.0)\n"
       "\n"
       "  -T timespec  Start timestamp\n"
       "  -i           Timestamp in input column 1\n"
       "  -t           Throttle to real time rate\n"
     );
   exit( 1);
}

static void run( void)
{
   timestamp T_rtc_start = VT_rtc_time();
   double dT = 1.0/sample_rate;
   uint64_t nout = 0;    // Number of output frames, used only for throttling

   //
   //  Initialise timebase, unless we're going to take timestamps from input.
   //

   if( has_TS) VT_set_timebase( vtfile, TS, 1.0);
   else
   if( !IFLAG) VT_set_timebase( vtfile, T_rtc_start, 1.0);

   double *frame = VT_malloc( sizeof( double) * chans);

   // Allow input lines of 4096 chars, plus a newline, plus the null terminator
   // appended by fgets().
   
   char *buff = VT_malloc( 4098);

   while( fgets( buff, 4098, stdin))
   {
      char *p = buff;

      while( *p && isspace( *p)) p++;        // Skip any leading white space
      if( *p == ';' || *p == '#') *p = 0;    // Ignore comments
      if( !*p) continue;                     // Ignore blank input lines

      //
      //  If the input file has a timestamp then parse it from the first
      //  column.  If no -T option is given to override it, then use the
      //  input timestamp to set the output timebase.
      //

      if( IFLAG)
      {
         // Parse the timestamp, but not necessarily use it.
         char *q = p;
         while( *p && !isspace( *p)) p++;
         if( *p) *p++ = 0;
         timestamp Tin = VT_parse_timestamp( q);

         if( !has_TS) // Use the input timestamp?
            VT_set_timebase( vtfile, Tin, 1.0);
      }

      //
      //  Parse the signal columns and output a frame.
      //

      int i;
      for( i = 0; i < chans; i++)
      {
         char *q;
         frame[i] = strtod( p, &q) * gain;
         if( q == p) VT_bailout( "not enough columns in input file");
         p = q;
      }

      VT_insert_frame( vtfile, frame);

      //
      //  Do throttling if required.  This means restricting the throughput
      //  to approximately a real time rate.  At the start of each output
      //  block, delay a while.
      //

      if( TFLAG)
      {
         nout++;
         if( VT_is_blk( vtfile))
            while( timestamp_diff( VT_rtc_time(), T_rtc_start) < nout * dT)
               usleep( 5000);
      }
   }
}

int main( int argc, char *argv[])
{
   VT_init( "vtdata");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBitT:c:r:L:g:z:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'c') chans = atoi( optarg);
      else
      if( c == 'r') sample_rate = atoi( optarg);
      else
      if( c == 'g') gain = atof( optarg);
      else
      if( c == 't') TFLAG = TRUE;
      else
      if( c == 'i') IFLAG = TRUE;
      else
      if( c == 'T')
      {
         TS = VT_parse_timestamp( optarg);
         has_TS = TRUE;
      }
      else
      if( c == -1) break;
      else
         usage();
   }

   if( chans <= 0) VT_bailout( "invalid or missing -c argument");
   if( sample_rate <= 0)
      VT_bailout( "invalid or missing sample rate, needs -r");

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDOUT : 0;
      VT_daemonise( flags);
   }

   VT_report( 1, "buffer name: [%s]", bname);
   VT_report( 1, "channels: %d", chans);

   vtfile = VT_open_output( bname, chans, 1, sample_rate);
   if( !vtfile) VT_bailout( "cannot create buffer: %s", VT_error);

   run();
   VT_release( vtfile);
   return 0;
}

