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
static char *outname = NULL;
static int sample_rate = 0;
static double srcal = 1.0;
static int TFLAG = 0;
static int XFLAG = 0;
static double gain = 1.0;
static timestamp T;
static double dT;
static char *infile = NULL;
static int chans = 1;
static timestamp TS = timestamp_NONE, TE = timestamp_NONE;

static struct COMP
{
   double amp;
   double freq;
   double phase;  // Carrier phase
   double mphase;  // Modulation phase
   double duty;
   double bitrate;
   double range;
   double tconst;

   double (*gen)(struct COMP *);
}
  *comps = NULL;                               // One for each signal component

static int ncomps = 0;                           // Number of signal components

static struct COMP *alloc_comp( void)
{
   comps = VT_realloc( comps, (ncomps+1) * sizeof( struct COMP));
   memset( comps+ncomps, 0, sizeof( struct COMP));
   comps[ncomps].duty = 0.5;
   return comps + ncomps++;
}

//
//  Sine generator
//

static double gen_sine( struct COMP *cp)
{
   return cp->amp * cos( VT_phase( T, cp->freq) + cp->phase * M_PI/180);
}

static void parse_sinespec( char *args)
{
   struct COMP *cp = alloc_comp();

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "a=", 2)) cp->amp = atof( args+2);
      else
      if( !strncmp( args, "f=", 2)) cp->freq = atof( args+2);
      else
      if( !strncmp( args, "p=", 2)) cp->phase = atof( args+2);
      else
         VT_bailout( "unrecognised sine option: %s", args);

      args = p;
   }

   cp->gen = gen_sine;
}

//
//  MSK generator.  Random data sequence. Only used for testing vtsid.
//

static int random_bipolar_bit( void)
{
   return rand() < RAND_MAX/2 ? -1 : 1;
}

static double gen_msk( struct COMP *cp)
{
   static int aI = 0, aQ = 0;
   static double p_s = 0, p_c = 0;

   // Modulation phase
   double modph = VT_phase( T, cp->bitrate/2.0) + cp->mphase * M_PI/180;
   double cos_modph = cos( modph);
   double sin_modph = sin( modph);

   if( !aI) // first time through
   {
      aI = random_bipolar_bit();
      aQ = random_bipolar_bit();
   }

   if( (p_c <= 0 && cos_modph > 0) ||
       (p_c > 0 && cos_modph <= 0)) aI = random_bipolar_bit();
   if( (p_s <= 0 && sin_modph > 0) ||
       (p_s > 0 && sin_modph <= 0)) aQ = random_bipolar_bit();

   p_c = cos_modph; 
   p_s = sin_modph; 

   // Carrier phase
   double carph = VT_phase( T, cp->freq) + cp->phase * M_PI/180;

   double s = aI * cos_modph * cos( carph) + aQ * sin_modph * sin( carph);

   return cp->amp * s;
}

static void parse_mskspec( char *args)
{
   struct COMP *cp = alloc_comp();

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "a=", 2)) cp->amp = atof( args+2);
      else
      if( !strncmp( args, "f=", 2)) cp->freq = atof( args+2);
      else
      if( !strncmp( args, "p=", 2)) cp->phase = atof( args+2);
      else
      if( !strncmp( args, "b=", 2)) cp->bitrate = atof( args+2);
      else
      if( !strncmp( args, "m=", 2)) cp->mphase = atof( args+2);
      else
         VT_bailout( "unrecognised msk option: %s", args);

      args = p;
   }

   cp->gen = gen_msk;
}

//
//  Simple sferic simulator
//

static double gen_sferic( struct COMP *cp)
{
   #define EIC_CUTOFF 1700
   static int once = 0;
   static double fa = 12000;
   static double fr = 14000;
   static double f1 = EIC_CUTOFF, f2 = 24000;
   static double fstep = 0;
   static int NC = 0;
   static double *buf;

   #define CVLF (300e3*0.9922)

   int j;

   if( !once)
   {
      once = 1;

      NC = 0.1 * sample_rate * srcal;
      buf = VT_malloc_zero( sizeof(double) * NC);

      if( f2 > sample_rate/2) f2 = sample_rate/2.0;
      fstep = sample_rate * srcal/(double)NC;

      f1 = (int)(f1/fstep) * fstep + fstep;
      double f; 
      for( f = f1; f < f2; f += fstep)
      {
         // Amplitude coefficient 
         double ap = M_PI * (f - fa)/(2*fr);
         double coef = cos(ap) * cos(ap);

         double fn = EIC_CUTOFF/f;  
         double fn2 = fn * fn;
         double delay = cp->range/CVLF * sqrt(1 - fn2);

         for( j=0; j<NC; j++)
         {
            double t = j/(double) sample_rate*srcal - delay;
            if( t >= 0) buf[j] += coef * cos( 2*M_PI*f * t);
         }
      } 
   }

   double cycles = VT_phase( T, cp->freq) / (2 * M_PI);
   j = (cycles - (int)cycles) * sample_rate*srcal/cp->freq;
   if( j >= NC) return 0;

   return cp->amp * buf[j]/100;
}

static void parse_sfericspec( char *args)
{
   struct COMP *cp = alloc_comp();

   cp->freq = 1;
   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "a=", 2)) cp->amp = atof( args+2);
      else
      if( !strncmp( args, "r=", 2)) cp->range = atof( args+2);
      else
      if( !strncmp( args, "f=", 2)) cp->freq = atof( args+2);
      else
         VT_bailout( "unrecognised sferic option: %s", args);

      args = p;
   }

   if( cp->freq <= 0) VT_bailout( "invalid frequency");
   cp->gen = gen_sferic;
 
}
 
//
//  Pulse generator
//

static double gen_pulse( struct COMP *cp)
{
   double cycle = 1 + VT_phase( T, cp->freq)/(2*M_PI) + cp->phase/360;
   cycle -= (int) cycle;

   double v = 0;

   if( cycle < cp->duty) v = cp->amp;

   if( cp->tconst)
   {
      if( cycle < cp->duty)
         v = cp->amp * (1 - exp( -cycle/cp->freq/cp->tconst));
      else
      {
         v = cp->amp * (1 - exp( -cp->duty/cp->freq/cp->tconst));
     
         v *= exp( -(cycle - cp->duty)/cp->freq/cp->tconst);
      }
   }
   return v;
}

static void parse_pulsespec( char *args)
{
   struct COMP *cp = alloc_comp();

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "a=", 2)) cp->amp = atof( args+2);
      else
      if( !strncmp( args, "f=", 2)) cp->freq = atof( args+2);
      else
      if( !strncmp( args, "p=", 2)) cp->phase = atof( args+2);
      else
      if( !strncmp( args, "d=", 2)) cp->duty = atof( args+2);
      else
      if( !strncmp( args, "t=", 2)) cp->tconst = atof( args+2);
      else
         VT_bailout( "unrecognised sine option: %s", args);

      args = p;
   }

   VT_report( 1, "duty %.3f", cp->duty);
   cp->gen = gen_pulse;
}

//
//  Gaussian noise generator
//

static double gen_normal( struct COMP *cp)
{
   double v1, v2, s;

   do
   { 
      double u1 = rand()/(double)(RAND_MAX + 1.0);
      double u2 = rand()/(double)(RAND_MAX + 1.0);
      v1 = 2 * u1 - 1;
      v2 = 2 * u2 - 1;
      s = v1*v1 + v2*v2;
   }
    while( s >= 1);
	
   return cp->amp * sqrt( -2 * log(s) / s) * v2;

}

static void parse_normalspec( char *args)
{
   struct COMP *cp = alloc_comp();

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "a=", 2)) cp->amp = atof( args+2);
      else
         VT_bailout( "unrecognised noise option: %s", args);

      args = p;
   }

   cp->gen = gen_normal;
}

//
//  Uniform noise generator
//

static double gen_uniform( struct COMP *cp)
{
   double v;

   v = 2 * (rand()/(double)(RAND_MAX + 1.0)) - 1;
   return cp->amp * v;
}

static void parse_uniformspec( char *args)
{
   struct COMP *cp = alloc_comp();

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "a=", 2)) cp->amp = atof( args+2);
      else
         VT_bailout( "unrecognised noise option: %s", args);

      args = p;
   }

   cp->gen = gen_uniform;
}

//
//  DC component
//

static double gen_dc( struct COMP *cp)
{
   return cp->amp;
}

static void parse_dc( char *args)
{
   struct COMP *cp = alloc_comp();

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "v=", 2)) cp->amp = atof( args+2);
      else
         VT_bailout( "unrecognised noise option: %s", args);

      args = p;
   }

   cp->gen = gen_dc;
}

//
//  Sum all the components to make a sample
//

static void gen( double *frame, int chans)
{
   struct COMP *cp = comps;

   double f = 0;
   int i;
   for( i=0; i<ncomps; i++, cp++) f += cp->gen( cp);

   f *= gain;
   if( XFLAG) f *= f;
   for( i=0; i<chans; i++) frame[i] = f;
}

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtgen [options] output\n"
       "\n"
       "options:\n"
       "  -v       Increase verbosity\n"
       "  -B       Run in background\n"
       "  -L name  Specify logfile\n"
       "  -z       Use relative timestamp\n"
       "  -t       Throttle output to real time\n"
       "  -r rate  Sample rate\n"
       "  -g gain  Output gain\n"
       "  -a seed  Seed random number\n"
       "  -i input Input stream for timebase\n"
       "  -T start,end  Timestamp range\n"
       "  -s a=amp,f=freq,p=phase  Generate sinewave\n"
       "  -n a=amp   Generate normally distributed white noise\n"
       "  -u a=amp   Generate uniformly distributed white noise\n"
       "  -p a=amp,f=freq,p=phase,d=duty Generate pulses\n"
       "  -m a=amp,f=freq,p=phase,b=bitrate  Generate MSK\n"
       "  -l a=amp,r=range_km  Generate sferics\n"
       "  -d v=val   Generate DC\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtgen");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBtr:g:s:n:c:p:u:m:d:l:a:i:T:L:x?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'r')
      {
         double frate = atof( optarg);
         sample_rate = round( frate);
         srcal = frate / sample_rate;
      }
      else
      if( c == 'T') VT_parse_timespec( optarg, &TS, &TE);
      else
      if( c == 'g') gain = atof( optarg);
      else
      if( c == 't') TFLAG = 1;
      else
      if( c == 'i') infile = strdup( optarg);
      else
      if( c == 'a') srandom( atoi( optarg));
      else
      if( c == 's') parse_sinespec( optarg);
      else
      if( c == 'p') parse_pulsespec( optarg);
      else
      if( c == 'n') parse_normalspec( optarg);
      else
      if( c == 'u') parse_uniformspec( optarg);
      else
      if( c == 'd') parse_dc( optarg);
      else
      if( c == 'm') parse_mskspec( optarg);
      else
      if( c == 'l') parse_sfericspec( optarg);
      else
      if( c == 'c') chans = atoi( optarg);
      else
      if( c == 'x') XFLAG = 1;
      else
      if( c == -1) break;
      else
         usage();
   }

   if( argc > optind + 1) usage();
   outname = strdup( optind < argc ? argv[optind] : "-");

   VTFILE *vtinfile = NULL;
   if( infile)
   {
      if( (vtinfile = VT_open_input( infile)) == NULL)
         VT_bailout( "cannot open input %s: %s", infile, VT_error);
      sample_rate = VT_get_sample_rate( vtinfile);
   }

   if( !sample_rate) VT_bailout( "sample rate not specified");

   if( background) VT_daemonise( outname[0] == '-' ? KEEP_STDOUT : 0);

   vtfile = VT_open_output( outname, chans, 0, sample_rate);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   double *frame = VT_malloc( sizeof( double) * chans);

   dT = 1/(sample_rate * srcal);
   
   if( !vtinfile)
   {
      uint64_t nft = 0;                        // Total number of frames output

      timestamp Treal = VT_rtc_time();
      timestamp Tstart = !timestamp_is_NONE( TS) ? TS : Treal;

      int tflag_interval = sample_rate / 5;     // Re-evaluate throttle about 5
                                                // times per second

      VT_set_timebase( vtfile, Tstart, srcal);
      VT_report( 1, "rate %d srcal %.7f", sample_rate, srcal);
      T = Tstart;

      while( 1)
      {
         if( !timestamp_is_NONE( TE) && timestamp_GE( T, TE)) break;
         if( TFLAG && nft % tflag_interval == 0) 
         {
            int nu = 1e6 * 
               (timestamp_diff( T, Tstart) -
                    timestamp_diff(VT_rtc_time(), Treal));
            if( nu > 0) usleep( nu);
         }
   
         gen( frame, chans);
         VT_insert_frame( vtfile, frame);
         nft++;
         T = timestamp_add( Tstart, dT * nft);
      }
   }
   else   // Use vtinfile for timing
      while( 1)
      {
         int e = VT_is_block( vtinfile);
         if( e < 0)
         {
            VT_report( 1, "end of input stream");
            break;
         }

         T = VT_get_timestamp( vtinfile);

         if( e)
         {
            if( vtfile->nfb) VT_release( vtfile);
            VT_set_timebase( vtfile, T, VT_get_srcal( vtinfile));
         }

         gen( frame, chans);
         VT_insert_frame( vtfile, frame);
         vtinfile->ulp++;
         vtinfile->nfb--;
      }

   VT_release( vtfile);
   return 0;
}

