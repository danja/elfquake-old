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
static double Etime = 0;  // -E option: exit after this many seconds out output
static double Stime = 0;  // -S option: skip this many seconds of input
static int PFLAG = 0;
static timestamp TS = timestamp_ZERO, TE = timestamp_ZERO;
static double TO = 0;            // Timebase offset, -a option
static int spoof_sample_rate = 0;
static double spoof_srcal = 1.0;

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtcat [options] input output\n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -B            Run in background\n"
       "  -L name       Specify logfile\n"
       "  -p            Pad breaks\n"
       "  -T start,end  Timestamp range\n"
       "  -S secs       Start after seconds\n"
       "  -E secs       Copy only seconds\n"
       "  -a secs       Adjust timestamps, adding secs offet\n"
     );
   exit( 1);
}

static void parse_spoofing( char *arg)
{
   spoof_sample_rate = atoi( arg);
   arg = strchr( arg, ',');
   if( arg) spoof_srcal = atof( arg+1);

   VT_report( 0, "spoofing sample rate: %d %.6f",
                  spoof_sample_rate, spoof_srcal);
}

int main( int argc, char *argv[])
{
   VT_init( "vtcat");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vpBT:E:S:a:s:L:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'T') VT_parse_timespec( optarg, &TS, &TE);
      else
      if( c == 'E') Etime = atof( optarg);
      else
      if( c == 'S') Stime = atof( optarg);
      else
      if( c == 'p') PFLAG = 1;
      else
      if( c == 'a') TO = atof( optarg);
      else
      if( c == 's') parse_spoofing( optarg);
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
   
   vtoutfile = VT_open_output( outname, chspec->n, 0,
                  spoof_sample_rate ? spoof_sample_rate : sample_rate);
   if( !vtoutfile)
      VT_bailout( "cannot open %s: %s", outname, VT_error);

   double *outframe = VT_malloc( sizeof( double) * chspec->n);
   double *padframe = VT_malloc( sizeof( double) * chspec->n);
   memset( padframe, 0, sizeof( double) * chspec->n);

   timestamp Tstart = VT_get_timestamp( vtinfile);
   timestamp Tlast = timestamp_ZERO;
   int enable = 0;
   int isblk = 0;
   int i;
   double srcal = 1.0;

   if( Stime) TS = timestamp_add( Tstart, Stime);

   int process_frame( double *frame, timestamp T)
   {
      if( !enable)
      {
         if( timestamp_LT( T, TS)) return 1;
   
         enable = 1;
         Tstart = T;
         if( spoof_sample_rate)
            VT_set_timebase( vtoutfile,
                             timestamp_add( T, TO), spoof_srcal);
         else
         {
            srcal = VT_get_srcal( vtinfile);
            VT_set_timebase( vtoutfile, timestamp_add( T, TO), srcal);
         }
      }
      else
      if( enable && isblk)
      {
         isblk = 0;
         if( !spoof_sample_rate)
         {
            srcal = VT_get_srcal( vtinfile);
            VT_set_timebase( vtoutfile, timestamp_add( T, TO), srcal);
         }
      }
      if( !timestamp_is_ZERO( TE) && timestamp_GE( T, TE))
      {
         VT_report( 1, "reach final timestamp");
         return 0;
      }
      if( Etime && timestamp_diff( T, Tstart) > Etime)
      {
         VT_report( 1, "completed %f seconds", Etime);
         return 0;
      }
   
      VT_insert_frame( vtoutfile, frame);
      return 1;
   }
  
   if( PFLAG && !timestamp_is_ZERO( TS) && timestamp_LT( TS, Tstart))
   {
      srcal = VT_get_srcal( vtinfile);
      double rate = sample_rate * srcal;
      int64_t n = round( timestamp_diff( Tstart, TS) * rate - 1);
      int64_t j = 0;
      while( j < n)
      {
         if( !process_frame( padframe, timestamp_add( TS, j/rate))) 
         {
            VT_release( vtoutfile);
            return 0;
         } 
         j++;
      }
      Tlast = timestamp_add( TS, n/rate);
   }
   
   while( 1)
   {
      isblk = VT_is_block(vtinfile);
      if( isblk < 0)
      {
         VT_report( 1, "end of input");
         break;
      }

      timestamp T = VT_get_timestamp( vtinfile);
      int rbreak = VT_rbreak( vtinfile);

      double *inframe = VT_get_frame( vtinfile);
      for( i=0; i<chspec->n; i++) outframe[i] = inframe[chspec->map[i]];

      // Discard any input that steps backward in time
      if( timestamp_LE( T, Tlast)) continue;

      if( PFLAG && rbreak)
      {
         srcal = VT_get_srcal( vtinfile);
         double rate = sample_rate * srcal;
         int64_t n = round( timestamp_diff(T, Tlast) * rate - 1);
         int64_t j = 0;
         while( j++ < n)
            if( !process_frame( padframe, timestamp_add( Tlast, j/rate))) break;
         if( j < n) break;
      }
      if( !process_frame( outframe, T)) break;

      Tlast = T;
   }

   if( PFLAG && timestamp_LT( Tlast, TE))
   {
      double rate = sample_rate * srcal;
      int64_t n = round( timestamp_diff( TE, Tlast) * rate - 1);
      int64_t j = 0;
      while( j < n)
      {
         VT_insert_frame( vtoutfile, padframe);
//         if( !process_frame( padframe, timestamp_add( Tlast, j/rate))) break;
         j++;
      }
   }

   VT_release( vtoutfile);
   return 0;
}

