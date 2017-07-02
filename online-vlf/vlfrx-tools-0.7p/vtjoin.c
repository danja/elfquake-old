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

static VTFILE *vtoutfile;
static char *bname = NULL;

static uint64_t nout = 0;  // Number of output samples

#define MAXINPUTS 20
static struct INPUT
{
   char *name;
   VTFILE *vtfile;
   struct VT_BLOCK *bp;
   double *frame;
   struct VT_CHANSPEC *chspec;
   int discarded;
   int duplicated;
   int ndisc;
   int ndup;
}
 inputs[MAXINPUTS];

static int ninputs = 0;
static int nchans = 0;
static int sample_rate = 0;

static void add_input( char *name)
{
   struct INPUT *ip = inputs + ninputs++;
   ip->name = strdup( name);
   ip->chspec = VT_parse_chanspec( ip->name);
}

static timestamp time_base = timestamp_ZERO;
static double dT;

static int next_in( struct INPUT *ip)
{
   // Timestamp of next outgoing frame
   timestamp Tnext = timestamp_add( time_base, nout/(double)sample_rate);

   double input_offset;

   while( 1)
   {
      if( VT_rbreak( ip->vtfile)) return 0;
      input_offset = timestamp_diff(  VT_get_timestamp( ip->vtfile), Tnext);

      if( input_offset > -dT/2) break;

      /* Input lagging behind timebase. Discard frames until the
         input catches up */

      if( !VT_get_frame( ip->vtfile)) return 0;
      ip->discarded++;
      if( ++ip->ndisc > 2)
      {
         VT_report( 0, "too many discarded on %s", ip->name);
         return 0;
      }
   }

   /* Input getting ahead of timebase? Duplicate a frame */
   if( input_offset > dT/2)
   {
      ip->duplicated++;
      if( ++ip->ndup > 2)
      {
         VT_report( 0, "too many duplicated on %s", ip->name);
         return 0;
      }
      // Caller re-uses previous ip->frame if there is one
      if( ip->frame) return 1;
   }

   ip->ndisc = ip->ndup = 0;
   /* Otherwise, extract next frame */
   ip->frame = VT_get_frame( ip->vtfile);
   return ip->frame != NULL;
}

static void status( void)
{
   int i;
   struct INPUT *ip = inputs;
   for( i=0; i<ninputs; i++, ip++)
   {
      VT_report( 1, "input %d: disc=%d dup=%d %s", i,
           ip->discarded, ip->duplicated, ip->name);  

      ip->discarded = ip->duplicated = 0;
   }
}

void usage( void)
{
   fprintf( stderr,
       "usage:    vtjoin [options] input1 [input2] ... output_buffer\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify logfile\n"
       "\n"
       "output stream must be given, does not default to stdout\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtjoin");

   int i, j, c, background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBL:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( optind+1 >= argc) usage();

   while( optind + 1 < argc) add_input( argv[optind++]);
   bname = strdup( argv[optind]);

   if( !ninputs) VT_bailout( "no inputs given");
   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDOUT : 0;
      for( i=0; i<ninputs; i++)
         if( inputs[i].name[0] == '-') flags |= KEEP_STDIN;
      VT_daemonise( flags);
   }

   for( i=0; i<ninputs; i++)
   {
      struct INPUT *ip = inputs + i;

      if( (ip->vtfile = VT_open_input( ip->name)) == NULL)
         VT_bailout( "cannot open input %s: %s", ip->name, VT_error);
      VT_init_chanspec( ip->chspec, ip->vtfile);
      nchans += ip->chspec->n;

      if( !sample_rate) sample_rate = VT_get_sample_rate( ip->vtfile);
      else 
      if( sample_rate != VT_get_sample_rate( ip->vtfile))
         VT_bailout( "inputs not all same sample rate");

      ip->discarded = ip->duplicated = 0;
   }

   VT_report( 1, "inputs: %d, channels: %d", ninputs, nchans);
   
   vtoutfile = VT_open_output( bname, nchans, 0, sample_rate);
   if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);

   double *outframe = VT_malloc( sizeof( double) * nchans);

   dT = 1/(double) sample_rate;

   restart:

   if( timestamp_is_ZERO( time_base)) VT_report( 1, "restart");

   // Set out outgoing timestamp to the latest of the input timestamps
   time_base = timestamp_ZERO;
   for( i=0; i<ninputs; i++) 
   {
      timestamp T = VT_get_timestamp( inputs[i].vtfile);
      if( timestamp_is_NONE(T))
      {
         VT_report( 1, "end of input");
         goto finished;
      }
      if( timestamp_GT( T, time_base)) time_base = T;
   }
      
   for( i=0; i<ninputs; i++) 
      while( 1)
      {
         timestamp T = VT_get_timestamp( inputs[i].vtfile);
         if( timestamp_is_NONE( T))
         {
            VT_report( 1, "end of input");
            goto finished;
         }
         if( timestamp_GT( T, timestamp_add( time_base, -dT/2))) break;
         if( VT_rbreak( inputs[i].vtfile)) goto restart;
         if( !VT_get_frame( inputs[i].vtfile)) goto restart;
      }

   VT_report( 1, "inputs aligned");

   VT_set_timebase( vtoutfile, time_base, 1.0);

   int ns = 0;  // Frame counter for reporting interval
   nout = 0;     // Total number of output samples

   while( 1)
   {
      struct INPUT *ip = inputs;
      for( i=c=0; i<ninputs; i++, ip++)
      {
         if( !next_in( ip))
         {
            VT_report( 1, "break on input %s", ip->name);
            usleep( 1000000);
            goto restart;
         }
         for( j=0; j<ip->chspec->n; j++)
            outframe[c++] = ip->frame[ip->chspec->map[j]];
      }

      VT_insert_frame( vtoutfile, outframe);
      nout++;
      if( ++ns == 10 * sample_rate) { status(); ns = 0; }
   }

   finished: 
  
   VT_release( vtoutfile);
   return 0;
}

