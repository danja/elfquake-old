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
static int CFLAG = 0;               // Clip at threshold, otherwise do blanking
static int IFLAG = 0;                     // #XXX -i option not yet implemented
static double dwell_time = 0.005;
static double hfactor = 0;                                  // Set by -h option
static double afactor = 0;                                  // Set by -a option
static double speriod = 0;                                  // Set by -s option
static double matc = 100.0;     // From -t option: moving average time constant
static double mafac = 0;              // Moving average exponential coefficient
static uint64_t nfp = 0;                    // Total number of frames processed
static struct VT_CHANSPEC *wspec = NULL;   // -w option: which chans to work on

static struct CHAN
{
   double ma;                                                 // Moving average
   int enable;                       // Set if we're to operate on this channel
   double dropsum;                                 // Number of samples blanked
}
 *channels = NULL;                       // One for each selected input channel

static int nchans = 0;                              // Number of input channels
static int dropcnt;                              // Dwell time sample countdown

static struct FBUF
{
   double *frame;
   float *coefs;
   float gcoef;
}
 *fbuf;

static int sfbuf = 0;
static int kfbuf = 0;
static int ifbuf = 0;

static double *cos2;
static int active = 0;
static int decay = 0;

static void eval_blanking( double *frame)
{
   int ch;

   fbuf[ifbuf].gcoef = 1;
   if( dropcnt)
   {
      if( !--dropcnt)
      {
         decay = sfbuf - 1;
      }
      fbuf[ifbuf].gcoef = 0;
   }

   if( decay)
   {
      decay--;
      fbuf[ifbuf].gcoef = cos2[decay];
   }

   int na = 0;
   for( ch = 0; ch < nchans; ch++)
   {
      struct CHAN *cp = channels + ch;

      if( !cp->enable) continue;

      double v = frame[ch];
      double av = fabs( v);

      // Update the moving average (if used) and set the threshold
      double th;
      if( afactor)
      {
          cp->ma = cp->ma * mafac + av * (1-mafac);
          th = afactor * cp->ma;
      }
      else th = hfactor;

      if( av > th) na = 1;            // Input has exceeded blanking threshold?
   }

   if( na)   // Blanking to be activated?
   {
      if( !active && sfbuf > 1)
      {
         int i;
         for( i=0; i<sfbuf-1; i++)
            fbuf[(ifbuf+i+1) % sfbuf].gcoef *= cos2[i];
      }
      fbuf[ifbuf].gcoef = 0;
      dropcnt = 1 + dwell_time * sample_rate;
      active = 1;
      decay = 0;
   }
   else
   if( !dropcnt) active = 0;                      // End of blanking dwell time
}

static void eval_clipping( double *frame)
{
   int ch;

   fbuf[ifbuf].gcoef = 1;
   if( dropcnt)
   {
      dropcnt--;
      fbuf[ifbuf].gcoef = 0;
   }

   for( ch = 0; ch < nchans; ch++)
   {
      struct CHAN *cp = channels + ch;

      if( !cp->enable) continue;

      double v = frame[ch];
      double av = fabs( v);

      // Update the moving average (if used) and set the threshold
      double th;
      if( afactor)
      {
          cp->ma = cp->ma * mafac + av * (1-mafac);
          th = afactor * cp->ma;
      }
      else th = hfactor;

      if( av > th)
      {
         dropcnt = dwell_time * sample_rate;
         if( CFLAG)
         {
            if( v > 0) frame[ch] = th;
            else frame[ch] = -th;
         }
         else 
         {
            if( !active && sfbuf > 1)
            {
               int i;
               for( i=0; i<sfbuf-1; i++)
               {
//                  fbuf[(ifbuf+i+1) % sfbuf].gcoef = cos2[i];
                  fbuf[(ifbuf+i+1) % sfbuf].gcoef = 0.5;
               }
            }
            fbuf[ifbuf].gcoef = 0;
            active = 1;
         }
      }
   }
}

static void apply( double *frame, int n)
{
   int ch;

   for( ch = 0; ch < nchans; ch++)
   {
      struct CHAN *cp = channels + ch;

      if( cp->enable)
      {
         frame[ch] *= fbuf[n].gcoef;
         cp->dropsum += 1 - fbuf[n].gcoef;
      }
   }
}

static void final_report( void)
{
   int i;

   for( i=0; i<nchans; i++)
   {
      VT_report( 1, "dropsum %d %.0f, nfp %lld", i, channels[i].dropsum,
                       (long long) nfp);
      VT_report( 1, "dropfactor %d %.3e", i, channels[i].dropsum/nfp);
   }
}

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtblank [options] input output\n"
       "\n"
       "options:\n"
       "  -v         Increase verbosity\n"
       "  -B         Run in background\n"
       "  -L name    Set logfile\n"
       "  -c         Clip at the threshold\n"
       "             (default is to blank the output)\n"
       " -d secs     Dwell time, seconds\n"
       "             (default is 0.005 seconds)\n"
       " -h thresh   Threshold amplitude\n"
       " -a factor   Automatic threshold factor\n"
       " -t secs     Time constant for moving average\n"
       "             (default 100 seconds)\n"
       " -i          Blank channels independently\n"
       " -s stime    Set smoothing time\n"
       " -w chanspec Operate only on these channels\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtblank");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBcid:h:a:s:t:w:L:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'c') CFLAG = 1;
      else
      if( c == 'i') IFLAG = 1;
      else
      if( c == 'd') dwell_time = atof( optarg);
      else
      if( c == 'h') hfactor = atof( optarg);
      else
      if( c == 'a') afactor = atof( optarg);
      else
      if( c == 't') matc = atof( optarg);
      else
      if( c == 's') speriod = atof( optarg);
      else
      if( c == 'w')
      {
         char *s; 
         if( asprintf( &s, ":%s", optarg) > 0) wspec = VT_parse_chanspec( s);
      }
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

   VT_init_chanspec( chspec, vtinfile);
   nchans = chspec->n;
   sample_rate = VT_get_sample_rate( vtinfile);

   VT_report( 1, "channels: %d, sample_rate: %d", nchans, sample_rate);
   VT_report( 1, "hfactor: %.3e", hfactor);
   VT_report( 1, "afactor: %.3f", afactor);
   if( hfactor && afactor)
      VT_bailout( "options contradict: -a and -h both given");
 
   vtoutfile = VT_open_output( outname, nchans, 0, sample_rate);
   if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);

   channels = VT_malloc( sizeof( struct CHAN) * nchans);
   memset( channels, 0, sizeof( struct CHAN) * nchans);
   int i;
   if( wspec)
   {
         VT_report( 2, "wspec: %d channels", wspec->n );
      for( i=0; i<wspec->n; i++)
      {
         if( wspec->map[i] >= nchans)
            VT_bailout( "invalid channel given to -w");
         channels[wspec->map[i]].enable = 1;
      }
   }
   else
      for( i=0; i<nchans; i++) channels[i].enable = 1;

   for( i=0; i<nchans; i++)
   {
      VT_report( 2, "channel %d: %s", i+1, 
         channels[i].enable ? "enabled": "disabled");
   }
   mafac = exp( -1.0/(matc * sample_rate));

   if( speriod)
   {
      sfbuf = 1 + speriod * sample_rate;
      cos2 = VT_malloc( sizeof( double) * sfbuf);
      for( i=0; i<sfbuf; i++)
      {
         double a = M_PI/2 * i/(double)sfbuf;
         cos2[i] = cos( a) * cos( a);
      }
   }
   else sfbuf = 1;

   VT_report( 2, "buffer size %d frames", sfbuf);

   fbuf = VT_malloc( sizeof( struct FBUF) * sfbuf);
   for( i=0; i<sfbuf; i++)
   {
      fbuf[i].frame = VT_malloc( sizeof( double) * nchans);
      memset( fbuf[i].frame, 0, sizeof( double) * nchans);
      fbuf[i].coefs = VT_malloc( sizeof( float) * nchans);
      memset( fbuf[i].coefs, 0, sizeof( float) * nchans);
   }

   double *inframe;
   VT_bailout_hook( final_report);

   while( 1)
   {
      int e;
      if( (e = VT_is_block( vtinfile)) < 0)
      {
         VT_report( 1, "end of input");
         break;
      }

      if( e)
      {
         timestamp T = VT_get_timestamp( vtinfile);
         double srcal = VT_get_srcal( vtinfile);
         VT_set_timebase( vtoutfile,
                     timestamp_add( T, -kfbuf/(double) sample_rate), srcal);
      }

      inframe = VT_get_frame( vtinfile);
      for( i=0; i<nchans; i++) fbuf[ifbuf].frame[i] = inframe[chspec->map[i]];

      if( CFLAG)
         eval_clipping( fbuf[ifbuf].frame);
      else
         eval_blanking( fbuf[ifbuf].frame);

      int n = (ifbuf + 1) % sfbuf;
      if( kfbuf < sfbuf) kfbuf++;
      else
      {
         apply( fbuf[n].frame, n);
         VT_insert_frame( vtoutfile, fbuf[n].frame);
         nfp++;
      }
 
      ifbuf = n;
   }

   VT_release( vtoutfile);

   final_report();
   return 0;
}

