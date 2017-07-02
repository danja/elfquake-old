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

#include <fftw3.h>

static VTFILE *vtinfile, *vtoutfile;
static char *inname = NULL;
static char *outname = NULL;
static int sample_rate = 0;
static int chans = 0;
static char *eqmap_file = NULL;   // Filename of eqmap, -e option
static int DFLAG = 0;             // -d diagnostic option

#define DEFAULT_BW 1
#define DEFAULT_THRESH 6.0

static double an_skip = 2.0;

static double gain = 1.0;
static double *gains;

static int BINS = 8192*2;
static int FFTWID = 0;
static double dF = 0;
static int p_load1 = 0, p_load2 = 0;                       // Two load pointers

static int AN_LIMIT = 6000;
#define AN_SMOOTH 0.95
#define AN_WIDTH  10

fftw_complex *X;
static struct CHANNEL
{
   int unit;
   int an_enable;
   int an_bw;
   int an_mode;
   double an_thresh;
   double ms_acc;
   fftw_complex *filterc;                                // Filter coefficients

   fftw_complex *X;                                   // Working buffer for FFT
   fftw_plan ffp_fwd1, ffp_rev1;              
   fftw_plan ffp_fwd2, ffp_rev2;              

   double *buf1, *buf2;                                 // Two circular buffers
   double *acc;                      // Array of smoothed average signal levels
}
 *channels;   // One of these for each output channel

static double *sinsq;                                     // FT window function

static void init_channel( struct CHANNEL *fp, int unit)
{
   int i;

   fp->unit = unit;
   fp->buf1 = VT_malloc( FFTWID * sizeof( double));
   for( i=0; i<FFTWID; i++) fp->buf1[i] = 0;

   fp->buf2 = VT_malloc( FFTWID * sizeof( double));
   for( i=0; i<FFTWID; i++) fp->buf2[i] = 0;

   fp->ffp_fwd1 = fftw_plan_dft_r2c_1d( FFTWID, fp->buf1, X, FFTW_ESTIMATE);
   fp->ffp_rev1 = fftw_plan_dft_c2r_1d( FFTWID, X, fp->buf1, FFTW_ESTIMATE);
   fp->ffp_fwd2 = fftw_plan_dft_r2c_1d( FFTWID, fp->buf2, X, FFTW_ESTIMATE);
   fp->ffp_rev2 = fftw_plan_dft_c2r_1d( FFTWID, X, fp->buf2, FFTW_ESTIMATE);

   fp->acc = VT_malloc( FFTWID * sizeof( double));
   for( i=0; i<FFTWID; i++) fp->acc[i] = 0;

   fp->an_bw = DEFAULT_BW;
   fp->an_thresh = DEFAULT_THRESH;
   fp->filterc = VT_malloc( (BINS+1) * sizeof( fftw_complex));
   for( i=0; i<=BINS; i++) fp->filterc[i] = 1;
   fp->an_enable = 0;
   fp->ms_acc = 0;
}

static void init_filter( void)
{
   int i;

   X = VT_malloc( sizeof( fftw_complex) * (BINS+1));

   sinsq = VT_malloc( FFTWID * sizeof( double));
   for( i=0; i<FFTWID; i++)
   {
      double theta = M_PI * i/(double) FFTWID;
      sinsq[i] = sin( theta) * sin( theta);
   }

   gains = VT_malloc( (BINS+1) * sizeof( double));
}

static void inline smooth_accumulate_double( double *a, double val, 
                                             double factor)
{
   *a = *a * factor + val * (1-factor);
}

static void filter_inner( struct CHANNEL *fp, fftw_plan ffp_forward, 
                                              fftw_plan ffp_reverse)
{
   int i;
   double ms_in;

   //
   //  Do the forward FFT and accumulate the moving average power level in
   //  each bin.
   //
   fftw_execute( ffp_forward);

   for( ms_in=i=0; i<=BINS; i++)
   {
      double a = cabs( X[i]);
      ms_in += a * a;  // Total power
   }

   if( ms_in < an_skip * fp->ms_acc)
      for( i=0; i<=BINS; i++)
      {
         double a = cabs( X[i]);
         smooth_accumulate_double( &fp->acc[i], a * a, AN_SMOOTH);
      }
   smooth_accumulate_double( &fp->ms_acc, ms_in, AN_SMOOTH);

   //
   //  Start with all bins at unity gain.
   //
   for( i=0; i<=BINS; i++) gains[i] = 1;

   //
   //  If the notch filter is on, turn down the gain on any bins whose
   //  accumulated power level exceeds the panel threshold.  As well as the
   //  affected bin, neighbouring bins are also turned down a little.
   //

   int an_limit = AN_LIMIT * (FFTWID / (double) sample_rate);

   if( fp->an_enable) 
      for( i=AN_WIDTH; i<FFTWID/2-AN_WIDTH && i < an_limit; i++)
      {
         int j;
         double sum;

         for( sum = 0, j=-AN_WIDTH; j<=AN_WIDTH; j++)
            if( j) sum += fp->acc[i+j];
         sum /= 2*AN_WIDTH;
  
         if( fp->acc[i] > sum * fp->an_thresh)
         {
            int n;
            for( n = -fp->an_bw; n <= fp->an_bw; n++) gains[i+n] = 0;
         }
      }
  
   //
   //  Apply the gain array and filter coefficients to the array of bins.
   //

   if( !fp->an_mode)
      for( i=0; i<=BINS; i++) X[i] *= fp->filterc[i] * gains[i] / FFTWID;
   else
   {
      int j, k;

      for( i=1; i<BINS; i++)
      {
         if( gains[i]) continue;
         
         for( j=i+1; j<BINS; j++) if( gains[j]) break;
         if( j < BINS)
         {
            complex double p1 = X[i-1];
            complex double p2 = X[j];
            int d = j - (i-1);
            for( k=i; k<j; k++)
               X[k] = p1 + (p2 - p1) * (k-(i-1))/(double)d;
         }
         i = j;
      }
      for( i=0; i<=BINS; i++) X[i] *= fp->filterc[i] / FFTWID;
   }


   //
   //  Do the reverse FFT.
   //
   fftw_execute( ffp_reverse);
}

//
//  The FFT filter.  This runs two FFTs, overlapping by 50%.  Each
//  has its own circular buffer for input.

static void filter_outer( double *inframe, int *map, double *outframe)
{
   int c;

   for( c = 0; c<chans; c++)
   {
      struct CHANNEL *fp = channels + c;

      outframe[c] = gain * (fp->buf1[p_load1] +
                            fp->buf2[p_load2]);

      double f = inframe[map[c]]; 
      fp->buf1[p_load1] = f * sinsq[p_load1];
      fp->buf2[p_load2] = f * sinsq[p_load2];
   }

   if( ++p_load1 == FFTWID)
   {
      p_load1 = 0;

      for( c = 0; c<chans; c++)
      {
         struct CHANNEL *fp = channels + c;
         filter_inner( fp, fp->ffp_fwd1, fp->ffp_rev1);
      }
   }

   if( ++p_load2 == FFTWID)
   {
      p_load2 = 0;

      for( c = 0; c<chans; c++)
      {
         struct CHANNEL *fp = channels + c;
         filter_inner( fp, fp->ffp_fwd2, fp->ffp_rev2);
      }
   }
}

static inline complex double cpower( complex double c, int n)
{
   complex double r = 1;
   while( n-- > 0) r *= c;
   return r;
}

static void setup_hpf( struct CHANNEL *fp, int npoles, double corner)
{
   int i;
   if( npoles <= 0) VT_bailout( "filter must specify number of poles");

   for( i=0; i<=BINS; i++)
   {
      double F = i * dF/corner;   // Normalised frequency
      fp->filterc[i] *= cpower( I*F/(1 + I*F), npoles);
   }

   VT_report( 1, "ch %d hpf: poles=%d corner=%.2f",
                     fp->unit, npoles, corner);
}

static void setup_lpf( struct CHANNEL *fp, int npoles, double corner)
{
   int i;
   if( npoles <= 0) VT_bailout( "filter must specify number of poles");

   for( i=0; i<=BINS; i++)
   {
      double F = i * dF/corner;   // Normalised frequency
      fp->filterc[i] *= cpower( 1 / (1 + I*F), npoles);
   }

   VT_report( 1, "ch %d lpf: poles=%d corner=%.2f",
                     fp->unit, npoles, corner);
}

static void setup_bpf( struct CHANNEL *fp, double center, double width)
{
   int i;

   for( i=0; i<=BINS; i++)
   {
      double freq = (i+0.5) * (sample_rate / (double) FFTWID);
      if( freq < center - width/2 ||
          freq > center + width/2)
          {
             fp->filterc[i] = 0;
          }
   }

   VT_report( 1, "ch %d bpf: center=%.2f width=%.2f",
                     fp->unit, center, width);
}

static void setup_bsf( struct CHANNEL *fp, double center, double width)
{
   int i;

   for( i=0; i<=BINS; i++)
   {
      double freq = (i+0.5) * (sample_rate / (double) FFTWID);
      if( freq >= center - width/2 &&
          freq <= center + width/2)
          {
             fp->filterc[i] = 0;
          }
   }

   VT_report( 1, "ch %d bsf: center=%.2f width=%.2f",
                     fp->unit, center, width);
}

static void parse_autonotch_settings( char *args, VTFILE *vtoutfile)
{
   int an_bw = DEFAULT_BW;
   int an_bw_set = 0;
   double an_thresh = DEFAULT_THRESH;
   int an_th_set = 0;
   int an_mode = 0;

   VT_report( 1, "autonotch settings: %s", args);

   struct VT_CHANSPEC *chspec;
   chspec = VT_parse_chanspec( args);

   VT_init_chanspec( chspec, vtoutfile);

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "bw=", 3))
      {
         an_bw = atoi( args+3); 
         an_bw_set = 1;
      }
      else
      if( !strncmp( args, "th=", 3))
      {
         an_thresh = atof( args+3);
         an_th_set = 1;
      }
      else
      if( !strncmp( args, "ul=",3))
      {
	 AN_LIMIT = atoi(args+3);
      }
      else
      if( !strncmp( args, "int", 3)) an_mode = 1;
      else
         VT_bailout( "unrecognised notch setting: %s", args);

      args = p;
   }

   int i;
   for( i=0; i<chans; i++)
   {
      int c = chspec->map[i];
      struct CHANNEL *fp = channels + c;
      if( an_bw_set) fp->an_bw = an_bw;
      if( an_th_set) fp->an_thresh = an_thresh;
      fp->an_enable = 1;
      fp->an_mode = an_mode;
   }
}

static void parse_filter_args( char *args, VTFILE *vtoutfile)
{
   int type = 0;
   double freq = -1;
   double width = 0;
   int poles = 0;

   VT_report( 1, "filter settings: %s", args);

   struct VT_CHANSPEC *chspec;
   chspec = VT_parse_chanspec( args);

   VT_init_chanspec( chspec, vtoutfile);

   while( args && *args)
   {
      char *p = strchr( args, ',');
      if( p) p++;

      if( !strncmp( args, "lp", 2)) type = 1;
      else
      if( !strncmp( args, "hp", 2)) type = 2;
      else
      if( !strncmp( args, "bp", 2)) type = 3;
      else
      if( !strncmp( args, "bs", 2)) type = 4;
      else
      if( !strncmp( args, "f=", 2)) freq = atof( args+2);
      else
      if( !strncmp( args, "w=", 2)) width = atof( args+2);
      else
      if( !strncmp( args, "poles=", 6)) poles = atoi( args+6);
      else
      if( !strncmp( args, "p=", 2)) poles = atoi( args+6);
      else
         VT_bailout( "unrecognised filter option: %s", args);

      args = p;
   }

   if( !type) VT_bailout( "filter must specify hp or lp");
   if( freq < 0 || freq > sample_rate/2)
      VT_bailout( "invalid or missing frequency for filter");

   int i;
   for( i=0; i<chspec->n; i++)
   {
      int c = chspec->map[i];
      struct CHANNEL *fp = channels + c;
      if( type == 1) setup_lpf( fp, poles, freq);
      if( type == 2) setup_hpf( fp, poles, freq);
      if( type == 3) setup_bpf( fp, freq, width);
      if( type == 4) setup_bsf( fp, freq, width);
   }
}

struct EQMAP
{
   double freq;
   complex double *coeffs;
}
 *eqmap = NULL;

complex double *eqmap_data = NULL;
int eqmap_alloc = 0;
int eqmap_n = 0;
double eqmap_freq = 0;

static void load_eqmap( int chans)
{
   #define ALLOC_BLK 100

   FILE *fh = fopen( eqmap_file, "r");
   if( !fh) VT_bailout( "cannot open eqmap [%s], %s",
                           eqmap_file, strerror( errno));

   int read_value( double *v)
   {
      int e = fscanf( fh, " %lf", v);
      if( e == EOF) return 0;
      if( e != 1) VT_bailout( "error in eqmap [%s]", eqmap_file);
      return 1;
   }

   int read_string( char *s)
   {
      int e = fscanf( fh, " %s", s);
      if( e == EOF) return 0;
      if( e != 1) VT_bailout( "error in eqmap [%s]", eqmap_file);
      return 1;
      
   }
   complex double *allocate( double freq)
   {
      if( freq < eqmap_freq)
         VT_bailout( "non-monotonic frequency in eqmap: %.3e", freq);

      if( eqmap_alloc <= eqmap_n)
      {
         eqmap_alloc += ALLOC_BLK;
         eqmap = VT_realloc( eqmap, sizeof( struct EQMAP) * eqmap_alloc);
         eqmap_data =
            VT_realloc( eqmap_data,
                           sizeof( complex double) * chans * eqmap_alloc);
      }

      eqmap_freq = eqmap[eqmap_n].freq = freq;
      return eqmap_data + eqmap_n++ * chans;
   }

   int i, nr = 0;
   double freq;
   complex double *coeffs;

   while( 1)
   {
      // XXX needs to read lines and split into fields

      if( !read_value( &freq)) break;   // Read first field - frequency Hz
      
      if( !eqmap_n && freq > 0)
      {
         // Jam in a DC entry if not supplied from the file
         coeffs = allocate( 0);
         for( i = 0; i < chans; i++) coeffs[i] = 1;
      }

      coeffs = allocate( freq);

      for( i = 0; i < chans; i++)
      {
         char temp[50];

         if( !read_string( temp)) VT_bailout( "incomplete data in eqmap");
         if( !VT_parse_complex( temp, coeffs + i))
            VT_bailout( "bad coefficient [%s] in eqmap", temp);
      }
      nr++;
   }

   fclose( fh);

   if( !nr) VT_bailout( "no records parsed in eqmap");

   VT_report( 1, "loaded %d eqmap records", nr);

   // Append a record for the Nyquist frequency if one wasn't supplied
   if( eqmap_freq < sample_rate / 2.0)
   {
      coeffs = allocate( sample_rate / 2.0);
      for( i = 0; i < chans; i++) coeffs[i] = 1;
   }

   for( i=0; i<eqmap_n; i++) eqmap[i].coeffs = eqmap_data + i * chans;
}

static void apply_eqmap( int chans)
{
   int bin, ne, ch;

   for( ne = bin = 0; bin <= BINS;)
   {
      struct EQMAP *base = eqmap + ne;
      struct EQMAP *next = eqmap + ne + 1;
      double f = bin * dF;
      if( f > next->freq) { ne++; continue; }

      double r = ne == 0 ? 0 : 
                 log( f / base->freq) / log( next->freq / base->freq);

      for( ch = 0; ch < chans; ch++)
      {
         double a1 = cabs(base->coeffs[ch]);
         double a2 = cabs(next->coeffs[ch]);
//         double da = a2 - a1;
         double p1 = carg(base->coeffs[ch]);
         double p2 = carg(next->coeffs[ch]);
         double dp = p2 - p1;
         if( dp >= M_PI) dp -= 2 * M_PI;
         if( dp < -M_PI) dp += 2 * M_PI;

         double ma = exp( log( a1) + (log(a2) - log(a1)) * r);
         // double pa = exp( log( p1) + (log(p2) - log(p1)) * r);
         double pa = p1 + dp * r;
         channels[ch].filterc[bin] *= ma * (cos(pa) + I*sin(pa));
      }
      bin++;
   }
}

//
//  Output filter coefficients.  -d1 option outputs in eqmap format, -d2
//  outputs amplitude and phase in separate columns
// 
static void output_filterc( int chans)
{
   int bin, ch;
   
   for( bin = 0; bin <= BINS; bin++)
   {
      printf( "%.6f", bin * dF);

      for( ch = 0; ch < chans; ch++)
      {
         struct CHANNEL *cp = channels + ch;

         if( DFLAG == 1)
            printf( " %.6e%+.6ej", creal( cp->filterc[bin]),
                                   cimag( cp->filterc[bin]));
         else
         if( DFLAG == 2)
            printf( " %.6e %.6e", cabs( cp->filterc[bin]),
                                  carg( cp->filterc[bin]) * 180/M_PI);
      }

      printf( "\n");
   }
}

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtfilter [options] input output\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify logfile\n"
       "  -g gain   Output gain\n"
       "  -e eqmap  Apply EQ table\n"
       "  -n bins   Frequency bins (default 16384)\n"
       "  -a autonotch_options\n"
       "     th=threshold,bw=notchwidth,ul=upper limit\n"
       "  -h filter_options\n"
       "     Butterworth low pass: lp,f=corner,p=poles\n"
       "     Butterworth high pass: hp,f=corner,p=poles\n"
       "     Brick wall bandpass: bp,f=center,w=width\n"
       "     Brick wall bandstop: bs,f=center,w=width\n"
     );
   exit( 1);
}

int main(int argc, char *argv[])
{
   VT_init( "vtfilter");

   int i;
   int background = 0;

   // Options for -a and -h are just saved for now, scanned
   // later when we know how many channels we're dealing with
   char **auto_notch_args = NULL;
   int auto_notch_n = 0;
   char **filter_args = NULL;
   int filter_n = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBg:a:h:e:d:n:L:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'g') gain = atof( optarg);
      else
      if( c == 'e') eqmap_file = strdup( optarg);
      else
      if( c == 'd') DFLAG = atoi( optarg);
      else
      if( c == 'n') BINS = atoi( optarg);
      else
      if( c == 'a')
      {
         if( !auto_notch_n) auto_notch_args = VT_malloc( sizeof( char *));
         else auto_notch_args = VT_realloc( auto_notch_args,
                                       (auto_notch_n+1) * sizeof( char *));
         auto_notch_args[auto_notch_n++] = strdup( optarg);
      }
      else
      if( c == 'h')
      {
         if( !filter_n) filter_args = VT_malloc( sizeof( char *));
         else filter_args = VT_realloc( filter_args,
                                       (filter_n+1) * sizeof( char *));
         filter_args[filter_n++] = strdup( optarg);
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

   sample_rate = VT_get_sample_rate( vtinfile);
   FFTWID = 2 * BINS;
   dF = sample_rate/(double) FFTWID;

   VT_init_chanspec( chspec, vtinfile);
   chans = chspec->n;
   VT_report( 1, "channels: %d, sample_rate: %d resolution: %.3f",
                     chans, sample_rate, dF);
   VT_report( 2, "fft width %d, bins %d", FFTWID, BINS);

   // Loading the eqmap is deferred until source opened, so we know how many
   // channels to deal with.
  
   if( eqmap_file) load_eqmap( chans);
 
   /* Setup filtering */

   init_filter();

   vtoutfile = VT_open_output( outname, chans, 0, sample_rate);
   if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);

   channels = VT_malloc( chans * sizeof( struct CHANNEL));
   for( i=0; i<chans; i++) init_channel( channels+i, i+1);

   if( eqmap_file) apply_eqmap( chans);

   for( i=0; i<auto_notch_n; i++)
   {
      VT_report( 1, "autonotch args %d: %s", i, auto_notch_args[i]);
      parse_autonotch_settings( auto_notch_args[i], vtoutfile);
   }

   for( i=0; i<filter_n; i++)
   {
      VT_report( 1, "filter args %d: %s", i, filter_args[i]);
      parse_filter_args( filter_args[i], vtoutfile);
   }

   for( i=0; i<chans; i++)
   {
      struct CHANNEL *fp = channels + i;
      VT_report( 1, "channel: %d", i+1);
      VT_report( 1, " an=%s bw=%d thresh=%.2f",
                fp->an_enable ? "on ": "off",
                fp->an_bw, fp->an_thresh);
   }
 
   if( DFLAG == 1 ||
       DFLAG == 2)  // -d1 or -d2 option: Dump the filter transform and exit
   {
      output_filterc( chans); 
      VT_exit( "done -d1");
   }

   double *inframe;
   double *outframe = VT_malloc( sizeof( double) * chans);

   double srcal = 1.0;
   int n = 0, e;
   p_load1 = 0;
   p_load2 = FFTWID/2;

   timestamp T = timestamp_ZERO;

   while( 1)
   {
      e = VT_is_block( vtinfile);
      if( e < 0)
      {
         VT_report( 1, "end of input");
         break;
      }

      if( e)  // Starting a new input block?
         srcal = VT_get_srcal( vtinfile);

      if( !vtoutfile->nfb)  // Starting a new output block?
      {
         T = VT_get_timestamp( vtinfile);
         double offset = FFTWID/(srcal * sample_rate);
         VT_set_timebase( vtoutfile, timestamp_add( T, -offset), srcal);
      }

      inframe = VT_get_frame( vtinfile);

      filter_outer( inframe, chspec->map, outframe);

      // Discard the first FFTWID frames as these are just leading zeros.
      if( n < FFTWID) n++;
      else
         VT_insert_frame( vtoutfile, outframe);
   }

   // An empty frame, a dummy for use in flushing
   inframe = VT_malloc_zero( VT_get_chans( vtinfile) * sizeof( double));

   //
   // If necessary, finish discarding the first FFTWID frames.   This kicks in
   // if the input stream was shorter than FFTWID samples.
   //
   // n is normally equal to FFTWID, but if the I/P stream was shorter, 
   // n will give the length
   //
   int u;
   for( u = n; u < FFTWID; u++)
      filter_outer( inframe, chspec->map, outframe);

   if( n < FFTWID)
   {
      // First timestamp of output stream will have been set incorrectly, on
      // the assumption that more than FFTWID samples will come through.
      // Now set the correct start time because we know the input length
      double offset = (n-1)/(srcal * sample_rate);
      VT_set_timebase( vtoutfile, timestamp_add( T, -offset), srcal);
   }

   // Drain the filter buffer.   Another min(n,FFTWID) frames to output.
   for( u=0; u<n; u++)
   {
      filter_outer( inframe, chspec->map, outframe);
      VT_insert_frame( vtoutfile, outframe);
   }

   VT_release( vtoutfile);

   return 0;
}

