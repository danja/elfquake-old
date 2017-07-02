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
static double Fc = 0;
static double Km = 1;

#define MODE_MOD   1
#define MODE_DEMOD 2
static double mode = MODE_DEMOD;

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtfm [options] input output\n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -B            Run in background\n"
       "  -L name       Specify logfile\n"
       "\n"
       "  -F hertz      Carrier frequency\n"
       "  -k index      Modulation index, Hz per unit amplitude\n"
       "\n"
       "  -m            Modulate\n"
       "  -d            Demodulate (default)\n"
     );
   exit( 1);
}

static struct CHAN {
   double ph;
   complex double Cd;
}
 *chans;

static double process_mod( int chan, double in)
{
   struct CHAN *cp = chans + chan;
  
   cp->ph += (Fc + Km * in) * 2 * M_PI/sample_rate;
 
   double out = cos( cp->ph);
   return out;
}

//
// Baseband delay demodulator
//
static double process_dem( int chan, double Iin, double Qin)
{
   struct CHAN *cp = chans + chan;
  
   complex double Cin = Iin - I * Qin;

   complex double g = Cin * conj( cp->Cd);
   cp->Cd = Cin;

   return carg( g) * sample_rate/(2 * M_PI * Km);
}

int main( int argc, char *argv[])
{
   VT_init( "vtfm");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBL:F:k:md?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'F') Fc = atof( optarg);
      else
      if( c == 'm') mode = MODE_MOD;
      else
      if( c == 'd') mode = MODE_DEMOD;
      else
      if( c == 'k') Km = atof( optarg);
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

   if( mode == MODE_MOD && !Fc)
      VT_bailout( "must specify carrier freq with -F");

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

   if( mode == MODE_MOD)
   {
      vtoutfile = VT_open_output( outname, chspec->n, 0, sample_rate);
      if( !vtoutfile) VT_bailout( "cannot open %s: %s", outname, VT_error);
   
      double *outframe = VT_malloc( sizeof( double) * chspec->n);
      chans = (struct CHAN *)
                VT_malloc_zero( sizeof( struct CHAN) * chspec->n);
   
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
         for( i=0; i<chspec->n; i++)
            outframe[i] = process_mod( i, inframe[chspec->map[i]]);

         if( isblk) VT_set_timebase( vtoutfile, 
                                     VT_get_timestamp( vtinfile),
                                     VT_get_srcal( vtinfile));
         VT_insert_frame( vtoutfile, outframe);
      }
   }
   else
   if( mode == MODE_DEMOD)
   {
      if( chspec->n % 2)
         VT_bailout( "input must have pairs of I/Q signals");
   
      vtoutfile = VT_open_output( outname, chspec->n/2, 0, sample_rate);
      if( !vtoutfile) VT_bailout( "cannot open %s: %s", outname, VT_error);
   
      double *outframe = VT_malloc( sizeof( double) * chspec->n/2);
      chans = (struct CHAN *)
                  VT_malloc_zero( sizeof( struct CHAN) * chspec->n/2);
   
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
            outframe[i] = process_dem( i, inframe[chspec->map[i*2+0]],
                                          inframe[chspec->map[i*2+1]]);

         if( isblk) VT_set_timebase( vtoutfile, 
                                     VT_get_timestamp( vtinfile),
                                     VT_get_srcal( vtinfile));
         VT_insert_frame( vtoutfile, outframe);
      }
   }
   VT_release( vtoutfile);
   return 0;
}


