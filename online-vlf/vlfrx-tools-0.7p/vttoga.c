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

// Important times, all in seconds
static double Tbuf = 0.06;                 // Buffer length
static double Tpulse = 0.0025;              // Pulse length analysed
static double Tlead = 0.0002;
// static double Tpretrig =  0.0002;          // Pre-trigger period
static double Tpretrig =  0.010;          // Pre-trigger period
// static double Tpretrig =  0.000;          // Pre-trigger period
static double Thold = 0.002;               // Re-trigger hold-off period

// Set by command line options
static int EFLAG = 0;                  // Non-zero if -e given, output spectrum
static int RFLAG = 0;                   // Non-zero if -r given, auto threshold
static int CFLAG = 0;                 // Non-zero if -c given, calibration mode
static int WFLAG = 0;                   // Non-zero if -w given, measure tweeks
static int Nmax = 0;             // From -N option: number of pulses to capture
static double throttle = 0;      // From -r option: target rate triggers/second
static double Etime = 0;            // From -E option, stop after Etime seconds
static int gran = 3600;              // From -G option: output file granularity
static char *outdir = NULL;                 // From -d option: output directory
static timestamp Ttrig;             // From -T option: single shot trigger time
static double qfactor = 0;                 // From -q option: quality threshold

// Variables for auto threshold
static time_t last_T = 0;               // Start time for trigger rate counting
static int last_N = 0;                       // Number of triggers since last_T
#define AT_INT 10                   // Seconds between updates of trigger level

static double trigger_level = 0;                    // Trigger level, set by -h 
static timestamp Tstart = timestamp_ZERO;          // Timestamp of first sample 

static struct CHAN
{
   double *buf;                                        // Circular input buffer
   double *fft;                                             // FFT input buffer
   complex double *X;                                             // FFT output
   double *upb;                                   // Buffer for unwrapped phase
   double *dsb;
   double mslope;                               // Median phase/frequency slope
   double rms;                                        // RMS amplitude of pulse
   double range;                             // Range estimated from dispersion
   double residual;
   timestamp toga;                                     // Time of group arrival
   fftw_plan fp;
   double mode1_f;
} *channels;

struct EVENT {
   double range;
   double residual;
   timestamp toga;
   double mode1_f;
   double rms;
   double bearing;
};

static int chans = 0;
static int sample_rate = 0;

static int buflen = 0;                       // Circular buffer length, samples
static int btrigp = 0;                           // Pre-trigger period, samples
static int b_offset = 0;
static int bp = 0;                         // Base pointer into circular buffer
static int FFTWID = 0;                                    // FFT width, samples
static int BINS = 0;                                          // Number of bins
static double DF = 0;                                   // Frequency resolution
static double DT = 0;                                        // Time resolution
static double F1 = 0, F2 = 0;                     // Frequency range to analyse
static int bf1, bf2;            // Start and finish bin numbers, from F1 and F2
static timestamp Tin;                        // Timestamp of most recent sample
static int thold = 0;                       // Trigger hold-off period, samples
static int one_shot = 0;                                // Non-zero if -T given
static int N_out = 0;                      // Number of sferics processed

//  Variables for polar operation
static int polar_mode = 0;
static double polar1_align = 0;        // Azimuth of 1st H-loop channel
static double polar2_align = 0;        // Azimuth of 2nd H-loop channel

static int ch_EFIELD = -1;
static int ch_HFIELD1 = -1;
static int ch_HFIELD2 = -1;

#define BUF(C,P) channels[C].buf[((P) + bp + buflen) % buflen]

#define DEFAULT_THRESHOLD 0.1   // Trigger threshold, rate of change per sample
#define EIC_CUTOFF 1700
#define EARTH_RAD 6371.0
#define CVLF (300e3*0.9922)

static void usage( void)
{
   fprintf( stderr,
       "usage:  vttoga [options] [input]\n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -B            Run in background\n"
       "  -L name       Specify logfile\n"
       "  -F start,end  Frequency range (default 4000,17000)\n"
       "  -E seconds    Stop after so many seconds\n"
       "  -h thresh     Trigger threshold (default %.1f)\n"
       "  -r rate       Auto threshold to this rate/second\n"
       "  -e            Spectrum and waveform data for each sferic\n"
       "  -d outdir     Output directory (defaults to stdout)\n"
       "  -G seconds    Output file granularity (default 3600)\n"
       "  -T timestamp  One-shot trigger time\n"
       "  -c            Calibration mode\n"
       "  -p polarspec  Specify orientation of input channels\n"
       "  -N count      Examine this many sferics then exit\n",
      DEFAULT_THRESHOLD);
   exit( 1);
}

///////////////////////////////////////////////////////////////////////////////
//  Output Functions                                                         //
///////////////////////////////////////////////////////////////////////////////

//
//  Output a 'H' record - timestamp and total amplitude.
//

static void output_standard( FILE *f, struct EVENT *ev)
{
   char temp[30];

   timestamp_string6( ev->toga, temp);
   fprintf( f, "H %s %.3e %.0f %.1e", temp, ev->rms, ev->range, ev->residual);

   if( polar_mode) fprintf( f, " %.1f", ev->bearing);

   if( WFLAG)
   {
      int n = 0, i;
      double sum = 0;

      for( i=0; i<chans; i++)
      {
         double mode1 = channels[i].mode1_f;
         if( mode1) { sum += mode1; n++; }
      }
      if( n) sum /= n;
      fprintf( f, " %.0f", sum);
   }
   fprintf( f, "\n");
}

//
// Extended output records if -e is given.  
//    H is the header record, call output_standard() for that;
//    S records for spectrum data;
//    T records for time domain;
//    E is an end marking record;
//

static void output_extended( FILE *f, struct EVENT *ev)
{
   int ch, j;

   for( j=bf1; j<=bf2; j++)
   {
      fprintf( f, "S %.3f", DF * j);
      for( ch=0; ch<chans; ch++)
      {
         struct CHAN *cp = channels + ch;
         fprintf( f, " %.3e %.3f %.3f", 
                 cabs(cp->X[j]), cp->upb[j] * 180/M_PI,
                 cp->dsb[j] * 180/M_PI);
      }
      fprintf( f, "\n");
   }

   double T = timestamp_diff( Tin, ev->toga) - (buflen - 1 - b_offset) * DT;
   for( j=0; j<FFTWID; j++)
   {
      fprintf( f, "T %.6e", T + j * DT);
      for( ch=0; ch<chans; ch++) fprintf( f, " %.4e", BUF(ch,j + b_offset));
      fprintf( f, "\n");
   }

   fprintf( f, "E\n");
}

static void output_record( struct EVENT *ev)
{
   //
   // Open an output file if -d given, otherwise use stdout.
   //

   FILE *f;

   if( !outdir) f = stdout;
   else
   {
      char *filename;
      time_t secs = timestamp_secs( timestamp_add( Tin, -buflen * DT));
      secs = (secs / gran) * gran;
      struct tm *tm = gmtime( &secs);

      if( asprintf( &filename, "%s/%02d%02d%02d-%02d%02d%02d",
            outdir,
            tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec) < 0)
         VT_bailout( "out of memory");
      if( (f = fopen( filename, "a")) == NULL)
         VT_bailout( "cannot open %s: %s", filename, strerror( errno));
      free( filename);
   }

   output_standard( f, ev);
   if( EFLAG) output_extended( f, ev);

   if( f != stdout) fclose( f);
}

///////////////////////////////////////////////////////////////////////////////
//  Tweek Analyser                                                           //
///////////////////////////////////////////////////////////////////////////////

static double *tw_inbuf = NULL;
static complex double *tw_specn = NULL;
static complex double *tw_specd = NULL;
static fftw_plan tw_fpn;
static fftw_plan tw_fpd;
static int tw_ymag = 4;
static double *tw_ra = NULL;

struct TWBAND {
   int f1, f2;
   double peak_A;
   double peak_F;
}
 tw_bands[] = {
   {  800, 1380 },
   { 1400, 2100 },   // Mode 1
   { 2120, 3000 },
   { 3020, 4000 },   // Mode 2
   { 4020, 5000 },
   { 5020, 6000 },   // Mode 3
   { 6020, 9900 },
};

#define NBANDS (sizeof( tw_bands)/sizeof( struct TWBAND))

static void analyse_tweek( int chan)
{
   struct CHAN *cp = channels + chan;
   double df = sample_rate/(double) buflen;
   int i, j;

   // Once-only initialisations...

   if( !tw_inbuf)
   {
      tw_inbuf = VT_malloc( sizeof( double) * buflen);
      tw_specn = VT_malloc( sizeof( complex double) * (buflen/2 + 1));
      tw_specd = VT_malloc( sizeof( complex double) * (buflen/2 + 1));
      tw_ra = VT_malloc( sizeof( complex double) * tw_ymag * (buflen/2 + 1));
      tw_fpn = fftw_plan_dft_r2c_1d( buflen, tw_inbuf, tw_specn, FFTW_ESTIMATE);
      tw_fpd = fftw_plan_dft_r2c_1d( buflen, tw_inbuf, tw_specd, FFTW_ESTIMATE);
   }


   // Do normal spectrum

   for( i=0; i<buflen; i++)
   {
      double v = BUF(chan, b_offset+i);
      if( i * DT < 10e-3) v = 0;
      if( i * DT > 30e-3) v = 0;
      tw_inbuf[i] = v * sin(i/(double)buflen * M_PI);
   }

   fftw_execute( tw_fpn);

   double a;
   for( i=0; i<NBANDS; i++)
   {
      struct TWBAND *b = tw_bands + i;

      b->peak_A = 0;
      for( j=b->f1/df; j<=b->f2/df; j++)
         if( (a = cabs( tw_specn[j])) > b->peak_A)
         {
            b->peak_A = a;
            b->peak_F = j * df;
         }
   }

   #define TW_THRESH 4
printf( "W %.6f %.6f %.6f\n", tw_bands[0].peak_A, tw_bands[1].peak_A, tw_bands[2].peak_A);
   if( tw_bands[1].peak_A < TW_THRESH * tw_bands[0].peak_A ||
       tw_bands[1].peak_A < TW_THRESH * tw_bands[2].peak_A ||
       tw_bands[1].peak_A < TW_THRESH * tw_bands[4].peak_A ||
       tw_bands[1].peak_A < TW_THRESH * tw_bands[6].peak_A
       )
   {
      cp->mode1_f = 0;
      return;
   }

   // Do spectrum with derivative window

   for( i=0; i<buflen; i++)
   {
      double v = BUF(chan, b_offset+i);
      if( i * DT < 10e-3) v = 0;
      if( i * DT > 30e-3) v = 0;
      tw_inbuf[i] = v * cos(i/(double)buflen * M_PI) * 2 * M_PI;
   }

   fftw_execute( tw_fpd);

   // Compute frequency reassignments

   memset( tw_ra, 0, sizeof( double) * tw_ymag * (buflen/2 + 1));
   for( i=0; i<buflen/2; i++)
   {
      double rf = (i - cimag( tw_specd[i]/tw_specn[i])) * df;
      j = (rf * tw_ymag)/df;
      tw_ra[j] += cabs( tw_specn[i]);
   }
  
   // cp->mode1_f = tw_bands[1].peak_F;
   double peak_a = 0;
   double peak_f = 0;

   for( i=tw_bands[1].f1*tw_ymag/df; i<=tw_bands[1].f2*tw_ymag/df; i++)
   {
      if( tw_ra[i] > peak_a) { peak_a = tw_ra[i]; peak_f = i * df/tw_ymag; }
   }  

   cp->mode1_f = peak_f;
   
#if 0
      for( i=0; i<buflen/2; i++)
         printf( "X %.2f %.3e %.2f\n", i * df, cabs(tw_specn[i]),
               (i - cimag( tw_specd[i]/tw_specn[i])) * df);
      for( i=0; i<buflen/2 * tw_ymag; i++)
         printf( "Y %.2f %.3e\n", i * df/tw_ymag, cabs(tw_ra[i]));
#endif

//printf( "mmax %.3e mmean %.3e fmax %.0f\n", mmax, mean, fmax);
}

#if 0
static void dump_vt( void)
{
   char fname[200];

   timestamp Tx = Tin - (buflen - 1) * DT;
   sprintf( fname, "/tmp/test/%.6Lf.vt", Tx);

   VTFILE *vtout = VT_open_output( fname, chans, 0, sample_rate);
   if( !vtout) VT_bailout( "cannot open %s: %s", fname, strerror( errno));

   VT_set_timebase( vtout, Tx, 1.0);

   double *frame = VT_malloc( sizeof(double) * chans);
   int i, ch;
   for( i=0; i<buflen; i++)
   {
      for( ch=0; ch<chans; ch++) frame[ch] = BUF(ch,i);
      VT_insert_frame( vtout, frame);
   }

   VT_release( vtout);
   VT_close( vtout);
   free( frame);
}
#endif

///////////////////////////////////////////////////////////////////////////////
//  Sferic Analyser                                                          //
///////////////////////////////////////////////////////////////////////////////

static int cmp_double( const void *p1, const void *p2)
{
   double v1 = *(double *)p1;
   double v2 = *(double *)p2;

   if( v1 < v2) return -1;
   if( v1 > v2) return 1;
   return 0;
}

// Rough estimate of dispersion, radians per km, for the given frequency bin.
static inline double propagation_phase( int bin)
{
   double f = bin * DF;
   return 2 * M_PI * f * (1 - sqrt(1 - pow(EIC_CUTOFF/f, 2))) / CVLF;
}

// Rough estimate of propagation delay, seconds per km, for the given
// frequency bin.
static inline double propagation_delay( int bin)
{
   double f = bin * DF;
   return (1 - sqrt(1 - pow(EIC_CUTOFF/f, 2))) / CVLF;
}

//
//  A pulse has been triggered and the time domain waveforms are in
//  channels[].buf with a small amount of pre-trigger.
//
//  Determine the TOGA of each channel, if possible.  Produce an amplitude
//  weighted average TOGA from the channels that gave a measurement.
//
//  Estimate the range by examining the dispersion.
//
//  If polar operation is called for, work out the bearing.
//

static void process_trigger( struct EVENT *ev)
{
   int ch, j, k;
   int nb = bf2 - bf1 + 1;                 // Number of frequency bins analysed
   int ntoga = 0;                // Number of channels from which TOGA obtained
   static double *tsbuf = NULL;

   if( !tsbuf) tsbuf = VT_malloc( sizeof( double) * nb * (nb-1)/2);

   for( ch=0; ch<chans; ch++)
   {
      struct CHAN *cp = channels + ch;

      // FFT the pulse and measure the RMS amplitude
      double sumsq = 0;
      for( j=0; j<FFTWID; j++)
      {
         double v = BUF(ch,j + b_offset);
         cp->fft[j] = v;
         sumsq += v * v;
      }
      fftw_execute( cp->fp);
      cp->rms = sqrt(sumsq/FFTWID);

      // Unwrap the bin phases into cp->upb[]
      double b = carg(cp->X[bf1]);
      for( j=bf1; j<=bf2; j++)
      {
         double dp = carg(cp->X[j]) - carg(cp->X[j-1]);
         if( dp > 0) dp -= 2*M_PI;
         // if( dp > M_PI) dp -= 2*M_PI;
         b += dp;
         cp->upb[j] = b;
      }

      // Use Theil-Sen regression to get an average slope
      int n = 0;
      for( j=bf1; j<bf2; j++)
         for( k=j+1; k<=bf2; k++)
            tsbuf[n++] = (cp->upb[k] - cp->upb[j])/(double)(k-j);

      qsort( tsbuf, n, sizeof( double), cmp_double);

      cp->mslope = tsbuf[n/2];  // Median - radians per bin
      cp->mslope = cp->mslope / (DF * 2 * M_PI); // Radians per (radian per sec)

      // Rotate the phase of cp->X, time shifting the spectrum to the
      // toga.

      for( j=bf1; j<=bf2; j++) cp->upb[j] -= cp->mslope * j * DF * 2 * M_PI;

      // Rotate all the phases to set the middle bin at zero phase.
      int jc = (bf1+bf2)/2;
      double pref = cp->upb[jc];
      for( j=bf1; j<=bf2; j++) cp->upb[j] -= pref;

      // Estimate the range from the dispersion. 

      double minerr = 1e99;
      double td;
      int rg;
      for( rg = 0; rg <= 12000; rg += 500)
      {
         double sd = 0;
         double sw = 0;

         td = rg * propagation_delay( jc);
         for( j=bf1; j <= bf2; j++)
         {
            // Expected value of phase in bin j at range rg
            double p = (rg * propagation_delay(j) * j + td * (j-2*jc))
                       * 2 * M_PI * DF;

            // Compare with measured phase, take the minimum modulo 2 pi
            p -= cp->upb[j];
            while( p >= M_PI) p -= 2*M_PI;
            while( p < -M_PI) p += 2*M_PI;

            // Sum the squared residuals, weighted by the bin amplitude
            double w = cabs( cp->X[j]);
            sd += p * p * w;
            sw += w;
         }

         sd /= sw;     // Mean square residual
         if( sd < minerr) { minerr = sd; cp->range = rg; cp->residual = sd; }
      }

      // For diagnostics, save the modeled phase spectrum
      td = cp->range * propagation_phase( jc)/(2 * M_PI * DF * jc);
      for( j=bf1; j <= bf2; j++)
         cp->dsb[j] = cp->range *
                      (propagation_phase(j) - propagation_phase(jc))
                      + td * (j-jc) * 2 * M_PI * DF ;

#if 0
      // Modify the measured spectrum unwrapping so that the residual is
      // mod 2 pi
      for( j=bf1; j <= bf2; j++)
      {
         while( cp->dsb[j] - cp->upb[j] >= M_PI) cp->upb[j] += 2 * M_PI;
         while( cp->dsb[j] - cp->upb[j] < -M_PI) cp->upb[j] -= 2 * M_PI;
      }
#endif

      cp->toga = timestamp_add( Tin,
                                -(buflen - 1 - b_offset) * DT - cp->mslope);

      ntoga++;             // Count number of channels that have given a result
   }

   if( !ntoga) return;            // No valid TOGA measurement from any channel

   // 
   //  Work out the average TOGA weighted by the RMS amplitude.
   //
   double sum_toga = 0;
   int toga_int = 0;
   double sum_range = 0;             // To reduce the dynamic range of toga_sum
   double sum_rms = 0;
   double sum_residual = 0;

   for( ch = 0; ch < chans; ch++)
   {
      struct CHAN *cp = channels + ch;
      if( timestamp_is_ZERO( cp->toga)) continue;

      if( !toga_int) toga_int = timestamp_secs( cp->toga);
      sum_toga += timestamp_diff(cp->toga,
                       timestamp_compose( toga_int, 0)) * cp->rms;
      sum_range += cp->range * cp->rms;
      sum_residual += cp->residual * cp->rms;
      sum_rms += cp->rms;
   }

   ev->toga = timestamp_compose( toga_int, sum_toga / sum_rms);
   ev->range = sum_range / sum_rms;
   ev->residual = sum_residual / sum_rms;

   if( polar_mode)
   {
      // Matrix to correct for the loop alignments
      double cos1 = cos( polar1_align);
      double cos2 = cos( polar2_align);
      double sin1 = sin( polar1_align);
      double sin2 = sin( polar2_align);
      double det = sin1*cos2 - cos1*sin2;

      double bsin = 0;
      double bcos = 0;

      //
      // Calculate the bearing for each frequency bin and do an average
      // weighted by the RMS amplitude.
      //
      for( j=bf1; j<=bf2; j++)
      {
         complex double *H1 = channels[ch_HFIELD1].X;
         complex double *H2 = channels[ch_HFIELD2].X;

         // N/S and E/W signals, correcting for loop azimuths
         complex double ew = (cos2 * H1[j] - cos1 * H2[j]) * det;
         complex double ns = (-sin2 * H1[j] + sin1 * H2[j]) * det;

         double mag_ew = cabs( ew);
         double mag_ns = cabs( ns);
         double pow_ew = mag_ew * mag_ew;
         double pow_ns = mag_ns * mag_ns;

         // Phase angle between N/S and E/W
         double phsin = cimag( ns) * creal( ew) - creal( ns) * cimag( ew);
         double phcos = creal( ns) * creal( ew) + cimag( ns) * cimag( ew);
         double a = atan2( phsin, phcos);

         // Watson-Watt goniometry to produce cos and sine of 2*bearing.
         double bearing2sin = 2 * mag_ew * mag_ns * cos( a);
         double bearing2cos = pow_ns - pow_ew;
         double pwr = pow_ew + pow_ns;

         double weight = pwr;

         if( ch_EFIELD < 0)
         {
            // No E-field available, so average the sin,cos of 2*bearing
            bsin += bearing2sin * weight;
            bcos += bearing2cos * weight;
            continue;
         }

         // E-field available, compare phase of E with H 
         double bearing180 = atan2( bearing2sin, bearing2cos)/2;
         if( bearing180 < 0) bearing180 += M_PI;
         else
         if( bearing180 >= M_PI) bearing180 -= M_PI;

         //  H-field signal in plane of incidence
         complex double or = ew * sin( bearing180) +
                             ns * cos( bearing180);

         complex double vr = channels[ch_EFIELD].X[j];

         // Phase angle between E and H
         double pha =
              atan2( cimag( or) * creal( vr) - creal( or) * cimag( vr),
                     creal( or) * creal( vr) + cimag( or) * cimag( vr));

         // Reflect the mod 180 bearing to the correct quadrant
         double bearing360 = bearing180;
         if( pha < -M_PI/2 || pha > M_PI/2) bearing360 += M_PI;

         // Average the sin,cos of the bearing
         bsin += sin( bearing360) * weight;
         bcos += cos( bearing360) * weight;
      }

      if( ch_EFIELD < 0) // Bearing modulo 180
      {
         ev->bearing = atan2( bsin, bcos) * 90/M_PI;
         if( ev->bearing < 0) ev->bearing += 180;
      }
      else  // Bearing modulo 360
      {
         ev->bearing = atan2( bsin, bcos) * 180/M_PI;
         if( ev->bearing < 0) ev->bearing += 360;
      }
   }

   double power = 0;
   for( ch=0; ch < chans; ch++)
   {
      power += pow( channels[ch].rms, 2);
   }
   ev->rms = sqrt( power);

   if( WFLAG)
   {
      for( ch=0; ch < chans; ch++) analyse_tweek( ch);

      int n = 0, i;
      double sum = 0;

      for( i=0; i<chans; i++)
      {
         double mode1 = channels[i].mode1_f;
         if( mode1) { sum += mode1; n++; }
      }

      if( n) ev->mode1_f = sum = n;
   }

   if( !qfactor || ev->residual < 1/qfactor) output_record( ev);

//   if( WFLAG && channels[0].mode1_f) dump_vt();

   last_N++;
   N_out++;
   if( Nmax && N_out == Nmax) VT_exit( "completed %d", Nmax);

   thold = Thold * sample_rate;                    // Begin the hold-off period
}

//
//  Called after each input frame.  Test to see if anything triggers and if
//  so, call process_trigger().
//

static void evaluate_trigger( void)
{
   int ch;
   struct EVENT ev;

   if( CFLAG)   // Trigger on UT second marks
   {
      timestamp T = timestamp_add( Tin,  -(buflen - 1) * DT);
      
      double t = timestamp_frac( T) + Tpretrig;
      if( t < 1 && t + DT >= 1) 
      {
         memset( &ev, 0, sizeof( struct EVENT));
         process_trigger( &ev);
      }
      return;
   }

   if( one_shot)   // A trigger time has been given with -T
   {
      timestamp T = timestamp_add( Tin, -(buflen - 1) * DT);
      if( timestamp_LT( T, Ttrig) &&
          timestamp_GE( timestamp_add( T, DT), Ttrig))
      {
         memset( &ev, 0, sizeof( struct EVENT));
         process_trigger( &ev);
         VT_exit( "completed trigger");
      }

      return;
   }

   //
   // Normal trigger test.  Hold-off period is restarted whenever the
   // signal rate of change is above the threshold.
   //

   if( thold) thold--;

   for( ch=0; ch<chans; ch++)
   {
      double d = BUF( ch, btrigp+1) - BUF( ch, btrigp);
      if( fabs(d) > trigger_level)
      {
         if( thold) thold = Thold * sample_rate;
         else
         {
            memset( &ev, 0, sizeof( struct EVENT));
            process_trigger( &ev);
         }
         break;
      }
   }
}

//
//  Revise the trigger threshold every AT_INT seconds.  The threshold is
//  changed by +/-10% according to whether the trigger rate over the last
//  AT_INT seconds is above or below the target rate.
//

static void evaluate_threshold( void)
{
   double rate = last_N / (double) AT_INT;

   if( rate < throttle) trigger_level *= 0.9;
   else trigger_level *= 1.1;

   VT_report( 1, "rate %.2f trigger %.3f", rate, trigger_level);
}


int main( int argc, char *argv[])
{
   VT_init( "vttoga");

   int background = 0;
   char *polarspec = NULL;

   while( 1)
   {
      int c = getopt( argc, argv, "vBL:F:h:r:E:d:G:T:p:N:w:q:ec?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'E') Etime = atof( optarg);
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'F') VT_parse_freqspec( optarg, &F1, &F2);
      else
      if( c == 'd') outdir = strdup( optarg);
      else
      if( c == 'G') gran = atoi( optarg);
      else
      if( c == 'q') qfactor = atof( optarg);
      else
      if( c == 'w')
      {
         double wtime = atof( optarg);
         Tbuf += wtime;
         WFLAG = 1;
      }
      else
      if( c == 'p')
      {
         polarspec = strdup( optarg);
      }
      else
      if( c == 'T') Ttrig = VT_parse_timestamp( optarg);
      else
      if( c == 'r') 
      {
         throttle = atof( optarg);
         RFLAG = 1;
         if( throttle < 0) VT_bailout( "invalid -r option");
      }
      else
      if( c == 'e') EFLAG = 1;
      else
      if( c == 'c') CFLAG = 1;
      else
      if( c == 'N') Nmax = atoi( optarg);
      else
      if( c == 'h') trigger_level = atof( optarg);
      else
      if( c == -1) break;
      else
         usage(); 
   }  
  
   if( argc > optind + 1) usage();
   char *bname = strdup( optind < argc ? argv[optind] : "-");
 
   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDIN : 0;
      flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   VTFILE *vtfile = VT_open_input( bname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chans, sample_rate);

   if( !timestamp_is_ZERO( Ttrig)) one_shot = 1;

   if( polarspec)
   {
      VT_parse_polarspec( chans, polarspec,
                          &ch_HFIELD1, &polar1_align,
                          &ch_HFIELD2, &polar2_align,
                          &ch_EFIELD);

      if( ch_HFIELD1 >= 0 && ch_HFIELD2 >= 0) polar_mode = 1;
   }

   //
   //  Set up buffers lengths, etc.  If -T given, an input buffer is created
   //  to hold the given time window, otherwise a default circular buffer is
   //  created.
   // 

   buflen = Tbuf * sample_rate;
   btrigp = Tpretrig * sample_rate;
   b_offset = (Tpretrig - Tlead) * sample_rate;
   FFTWID = Tpulse * sample_rate;
   BINS = FFTWID/2 + 1;
   DF = sample_rate/(double) FFTWID;
   DT = 1/(double)sample_rate;

   VT_report( 2, "buffer length: %d samples trigp %d DF=%.3f",
                  buflen, btrigp, DF);

   if( !F1) F1 = 4000;
   if( !F2) F2 = 17000;
   if( F2 <= F1) VT_bailout( "invalid frequency range");

   bf1 = F1/DF;
   bf2 = F2/DF;
   if( bf2 >= BINS) bf2 = BINS-1;

   channels = VT_malloc_zero( sizeof( struct CHAN) * chans);
   int i;
   for( i=0; i<chans; i++)
   {
      struct CHAN *cp = channels + i;

      cp->buf = VT_malloc( sizeof( double) * buflen);
      cp->fft = VT_malloc( sizeof( double) * FFTWID);
      cp->X = VT_malloc( sizeof( complex double) * BINS);
      cp->upb = VT_malloc( sizeof( double) * BINS);
      cp->dsb = VT_malloc( sizeof( double) * BINS);
      cp->fp = fftw_plan_dft_r2c_1d( FFTWID, cp->fft, cp->X, FFTW_ESTIMATE);
   }

   if( !trigger_level) trigger_level = DEFAULT_THRESHOLD;

   double *frame;
   int ch;
   int nbuf = 0;

   Tstart = VT_get_timestamp( vtfile);
   last_T = timestamp_secs( Tstart);

   while( 1)
   {
      //
      // Read a frame and add to circular input buffers
      //
      Tin = VT_get_timestamp( vtfile); 

      if( (frame = VT_get_frame( vtfile)) == NULL) break;

      for( ch = 0; ch < chans; ch++)
      {
         double v = frame[chspec->map[ch]];
         channels[ch].buf[bp] = v;
      }

      bp = (bp + 1) % buflen;

      //
      // Once the buffer is full, start looking for triggers
      //

      if( nbuf < buflen) nbuf++;  
      else evaluate_trigger();

      //
      //  Reconsider the trigger threshold to aim for the target rate.
      //

      time_t t = timestamp_secs( Tin);
      if( RFLAG && t - last_T > AT_INT)
      {
         evaluate_threshold();
         last_T = t;
         last_N = 0;
      }

      //
      // Finish if reached a given end time.
      //

      if( Etime && timestamp_diff(Tin, Tstart) > Etime)
      {
         VT_report( 1, "completed %f seconds", Etime);
         break;
      }
   }

   return 0;
}

