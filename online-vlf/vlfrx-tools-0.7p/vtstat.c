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

static char *bname = NULL;
static VTFILE *vtfile;

static int HFLAG = 0;            // Run histogram
static int AFLAG = 0;            // Run amplitude
static int IFLAG = 0;            // Run info only

static uint64_t nft = 0;

static double *sum = NULL;
static double *sumsq = NULL;
static double *peak = NULL;

static int navg = 0;
static int breaks = 0;
static int nib = 0;
static int sample_rate = 0;
static char *data_format = "";
static char *stamptype = "";
static int chans = 0;
static double Etime = 0;
static timestamp Tstart = timestamp_ZERO, Tend = timestamp_ZERO;
static uint64_t clips = 0;          // Count of samples exceeding +/- 1.0

///////////////////////////////////////////////////////////////////////////////
//  Amplitude                                                                //
///////////////////////////////////////////////////////////////////////////////

static double ampres = 1.0;
static double *ampsum1 = NULL;
static double *ampsum2 = NULL;
static double *ampsum3 = NULL;
static timestamp ampT = timestamp_ZERO;
static int ampN = 0;
static void parse_amp_options( char *s)
{
   while( s && *s)
   {  
      char *p = strchr( s, ',');
      if( p) p++;
      
      if( !strncmp( s, "r=", 2)) ampres = atof( s+2);
      else
         VT_bailout( "unrecognised amplitude option: %s", s);
      
      s = p;
   }
}

static void amp_init( void)
{
   int size = chans * sizeof( double);
   ampsum1 = VT_malloc( size);
   ampsum2 = VT_malloc( size);
   ampsum3 = VT_malloc( size);
}

static void amp_reset( void)
{
   int i;
   for( i=0; i<chans; i++) ampsum1[i] = ampsum2[i] = ampsum3[i] = 0;
   ampT = VT_get_timestamp( vtfile);
   ampN = 0;
}

static void amp_output( void)
{
   char temp[30];   timestamp_string6( ampT, temp);
   printf( "%s %.6f", temp, ampN/(double) sample_rate);

   int ch;
   for( ch = 0; ch < chans; ch++)
      printf( " %.3e %.3e %.3e",
         ampsum1[ch]/ampN, ampsum2[ch]/ampN, ampsum3[ch]/ampN);

   printf( "\n");
}

static void amp_update( double *frame)
{  
   int ch;
   
   for( ch = 0; ch < chans; ch++)
   {  
      double v = frame[ch];
      if( v < 0) v = -v;
      ampsum1[ch] += v; 
      ampsum2[ch] += v*v; 
      ampsum3[ch] += v*v*v; 
   }

   ampN++;

   timestamp T = VT_get_timestamp( vtfile);
   if( ampres && timestamp_diff( T, ampT) >= ampres)
   {
      amp_output();
      amp_reset();
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Histogram                                                                //
///////////////////////////////////////////////////////////////////////////////

static uint64_t *hdata = NULL;

static int hbins = 100;                             // Number of histogram bins
static double hmax = 1.0;                           // Upper limit of histogram

static void parse_histo_options( char *s)
{
   while( s && *s)
   {
      char *p = strchr( s, ',');
      if( p) p++;

      if( !strncmp( s, "max=", 4)) hmax = atof( s+4);
      else
      if( !strncmp( s, "bins=", 5)) hbins = atoi( s+5);
      else
         VT_bailout( "unrecognised histogram option: %s", s);

      s = p;
   }
}

static void histo_init( void)
{
   int size = 8 * chans * hbins;
   hdata = VT_malloc_zero( size);
}

static void histo_update( double *frame)
{
   int ch;

   for( ch = 0; ch < chans; ch++)
   {
      double v = frame[ch] / hmax;

      if( v < 0) v = -v;

      int bin = v * hbins;
      if( bin < hbins) hdata[bin * chans + ch]++;
   }
}

static void histo_report( void)
{
   int i, ch;
   uint64_t *totals = VT_malloc_zero( 8 * chans);

   for( i=0; i<hbins; i++)
      for( ch = 0; ch < chans; ch++) totals[ch] += hdata[i*chans + ch];

   for( i=0; i<hbins; i++)
   {
      double v = i/(double) hbins * hmax;

      printf( "%.6e", v);
      for( ch = 0; ch < chans; ch++)
         printf( " %.3e", hdata[i*chans + ch]/(double) totals[ch]);
      printf( "\n");
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Curses display                                                           //
///////////////////////////////////////////////////////////////////////////////

static void curses_bailout_hook( void)
{
   endwin();
}

static void xyprintf( int x, int y, char *format, ...)
{
   va_list ap;
   char temp[200];

   va_start( ap, format);
   vsprintf( temp, format, ap);
   va_end( ap);

   int i;
   for( i=0; temp[i]; i++)
   {
      mvaddch( y, x+i, temp[i]);
   }
}

static void curses_init( void)
{
   VT_bailout_hook( curses_bailout_hook);
   initscr();
   scrollok( stdscr, FALSE); clearok( stdscr, FALSE);
   nonl();

   attrset( A_NORMAL);

   xyprintf( 2, 1, "buffer: %s", bname);
   xyprintf( 2, 2, "format: %s", data_format);
}

static void curses_update( double *frame)
{
   int ch;

   // Update screen 5 times/sec
   if( navg < sample_rate/5) return;

   xyprintf( 2, 3, "sample rate: %d, correction %10.8f", sample_rate,
                VT_get_srcal( vtfile));
   xyprintf( 2, 4, "blocks: %5d", VT_get_bcnt( vtfile));
   xyprintf( 2, 5, "frames per block: %d", vtfile->bsize);

   timestamp T = VT_get_timestamp( vtfile);
   char temp[50];

   VT_format_timestamp( temp, T);

   xyprintf( 2, 7, "timestamp: %s %s", temp, stamptype);

   xyprintf( 2, 9, "latency: %.6f", timestamp_diff( VT_rtc_time(), T));
   xyprintf( 2, 10, "breaks: %d", breaks);

   for( ch=0; ch < chans; ch++)
   {
      double rms = sqrt(sumsq[ch]/navg);
      xyprintf( 2, 12+ch, "channel %2d: rms=%.3f peak=%.3f", 
               ch+1, rms, peak[ch]);
      sum[ch] = sumsq[ch] = peak[ch] = 0;
   }

   navg = 0;
   refresh();
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void usage( void)
{
   fprintf( stderr,
       "usage: vtstat [options] buffer\n"
       "       vtstat -V\n"
       "\n"
       "  -h options  Run histogram with options\n"
       "              max=max,bins=bins\n"
       "  -E secs     Analyse for this number of seconds, then exit\n"
       "  -a          Extract amplitude data\n"
       "  -i          Report info only\n"
       "  -V          Report vtlib package version\n"
       "options:\n"
     );

   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtstat");

   while( 1)
   {
      int c = getopt( argc, argv, "Vvih:E:a:?");
  
      if( c == 'V')
      {
         printf( "vtlib version %s\n", PACKAGE_VERSION);
         VT_report( 1, "double %d", (int) sizeof( double));
         VT_report( 1, "long double %d", (int) sizeof( long double));
         #ifdef USE_COMPOUND_TIMESTAMP
            VT_report( 1, "using compound timestamp");
         #endif
         exit( 0);
      }
      else
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'h')
      {
         HFLAG = 1;
         parse_histo_options( optarg);
      }
      else
      if( c == 'a')
      {
         AFLAG = 1;
         parse_amp_options( optarg);
      }
      else
      if( c == 'i') IFLAG = 1;
      else
      if( c == 'E') Etime = atof( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }  

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   if( (vtfile = VT_open_input( bname)) == NULL)
      VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;

   sum = VT_malloc_zero( sizeof( double) * chans);
   sumsq = VT_malloc_zero( sizeof( double) * chans);
   peak = VT_malloc_zero( sizeof( double) * chans);

   sample_rate = VT_get_sample_rate( vtfile);
   nft = 0;

   switch( VT_flags( vtfile) & VTFLAG_FMTMASK)
   {
      case VTFLAG_FLOAT4: data_format = "float4"; break;
      case VTFLAG_FLOAT8: data_format = "float8"; break;
      case VTFLAG_INT1: data_format = "int1"; break;
      case VTFLAG_INT2: data_format = "int2"; break;
      case VTFLAG_INT4: data_format = "int4"; break;
      default: VT_bailout( "invalid data format");
   }

   stamptype = VT_flags( vtfile) & VTFLAG_RELT ?  "relative" : "absolute";
   Tstart = VT_get_timestamp( vtfile);

   if( HFLAG) histo_init();
   else
   if( AFLAG) amp_init();
   else
   if( !IFLAG)
      curses_init();

   if( AFLAG) amp_reset();

   double srcal = 1.0;

   while( 1)
   {
      int e = VT_is_block( vtfile);
      if( e < 0)
      {
         VT_report( 1, "end of stream");
         break;
      }

      if( VT_rbreak( vtfile) > 0)
      {
         breaks++;

         char temp[50];
         VT_format_timestamp( temp, VT_get_timestamp( vtfile));
         VT_report( 1, "break to %s", temp);
      }

      if( e)
      {
         Tend = VT_get_timestamp( vtfile);
         srcal = VT_get_srcal( vtfile);
        
         char temp[50];
         VT_format_timestamp( temp, Tend); 
         VT_report( 2, "block %s %.9f", temp, srcal);
         nib = 0;
      }
 
      double *frame = VT_get_frame( vtfile);


      int ch;
      for( ch = 0; ch < chans; ch++)
      {
         double val = frame[chspec->map[ch]];
         if( val < 0) val = -val;
         sum[ch] += val;
         sumsq[ch] += val * val;

         if( val > 1) clips++;

         if( val > peak[ch]) peak[ch] = val;
         else
         if( val < -peak[ch]) peak[ch] = -val;
      }

      navg++;
      nft++;
      nib++;

      if( Etime && nft/(double) sample_rate > Etime)
      {
         VT_report( 1, "completed %f seconds", Etime);
         break;
      }

      if( HFLAG)
         histo_update( frame);
      else
      if( AFLAG)
         amp_update( frame);
      else
      if( !IFLAG)
         curses_update( frame);
   }

   Tend = timestamp_add( Tend, nib / (double) sample_rate);

   if( HFLAG) histo_report();
   else
   if( AFLAG)
   {
      if( !ampres) amp_output();
   }
   else
   if( IFLAG)
   {  
      printf( "format: %s\n", data_format);
      printf( "channels: %d\n", chans);
      printf( "samples: %lld\n", (long long) nft);
      printf( "clips: %lld\n", (long long) clips);
      printf( "breaks: %d\n", breaks);
      printf( "sample rate: %d, correction %10.8f\n", sample_rate, srcal);
      printf( "duration: %.7f seconds\n",  nft/(double) sample_rate);

      char temp[50];
      VT_format_timestamp( temp, Tstart);
      printf( "start: %s\n", temp);

      VT_format_timestamp( temp, Tend);
      printf( "end: %s, interval %.7f seconds\n",
               temp, timestamp_diff( Tend, Tstart));

      int ch;
      if( nft)
         for( ch = 0; ch < chans; ch++)
            printf( "mean,rms,peak: %d %.3e,%.3e,%.3e\n", ch+1,
                   sum[ch]/nft, sqrt( sumsq[ch]/nft), peak[ch]);
   }
   else endwin();

   return 0;
}

