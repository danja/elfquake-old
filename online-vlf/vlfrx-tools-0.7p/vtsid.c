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
#include "vtsid.h"

#include <fftw3.h>

///////////////////////////////////////////////////////////////////////////////
//  Globals and definitions                                                  // 
///////////////////////////////////////////////////////////////////////////////

//
//  Variables beginning CF_ are set directly by the configuration file.
//

static int CF_bins = -1;                            // Number of frequency bins
static double CF_resolution = -1;                       // Frequency resolution
static double CF_monitor_interval = 0;       // Output record interval, seconds
static char CF_mailaddr[100] = "";                // Address for alert messages

static double CF_los_thresh = 0;          // Threshold for loss of signal, 0..1
static int CF_los_timeout = 0;        // Seconds before loss of signal declared

static char *CF_datadir = NULL;                   // Directory for output files
static int CF_do_phase = 0;             // Non-zero if phase is to be monitored

static double CF_spectrum_interval = 0;     // Seconds between spectrum records
static int spectrum_enable = 0;        // Non-zero if spectrum records required

//
//  Variables set by command line options
// 

static char *config_file = NULL;              // From -c option
static char *msk_debug = NULL;           // From -d ident, for software testing

static double DF;                            // Frequency resolution of the FFT
static double cDF;                                     // Calibrated resolution

static VTFILE *vtfile;                                   // Input stream handle
static int chans = 0;                               // Number of input channels

static uint64_t nin = 0;                        // Total number of frames input

static timestamp monitor_output_stamp = timestamp_ZERO; 
static timestamp spectrum_output_stamp = timestamp_ZERO; 

#define FFTWID (2 * CF_bins)                  // Number of samples per FT frame

static complex double *spec_bsum;       // Per-bin bearing average for spectrum
static complex double *bsum;

static int sample_rate;                              // Samples per second
static int monitor_int;                       // Output record interval, frames
static int spectrum_int;
static int monitor_block_cnt = 0;           // Frame counter for output records
static int spectrum_block_cnt = 0;          // Frame counter for output records
static double srcal = 1.0;                    // Sample rate calibration factor

static int alert_on = 0;           // Set when the program actually starts work

static timestamp first;        // Timestamp of first sample of current FT frame
static double *window = NULL;
static double window_norm = 0;

#define MSK_GATE 0.002                       // MSK measurement period, seconds
#define MSK_TC 20               // MSK modulation phase smoothing time constant
static double msk_smooth = 0;            // Exponential coefficient from MSK_TC

static int msk_interval = 0;
static double *msk_win;

static fftw_complex *msk_fdata;
static double *msk_tdata;
static fftw_plan msk_plan;

#define polar_mode (polar1 && polar2)

//
//  A structure for each input channel.
//

#define MAXCHANS 3

// Channel types
#define CH_EFIELD 1
#define CH_HFIELD 2

static struct CHAN
{
   // Channel config items
   int chan;
   int type;             // CH_EFIELD or CH_HFIELD

   double azimuth;
   double cal;

   // FFT things
   double *fft_in;            // Input buffer for FFT
   fftw_complex *fft_out;     // Output of FFT
   fftw_plan plan_fwd;

   // Data items
   double *powspec;          // Power spectrum accumulator for spectrum monitor
   double peak;
   double sum_sq;
   int los_state;
   time_t los_time;
   char fname[100];
}
 channels[MAXCHANS];

static int NC = 0;    // Number of channels configured

// Channels assignments
static struct CHAN *polar1,   // 1st H-field
                   *polar2,   // 2nd H-field
                   *polar3;   // E-field

//
// Table of signals to monitor
//

static struct MONITOR
{
   int type;                                     // Type of signal, SIGTYPE_...
   int skip;                                     // If set, ignore this monitor
   char *ident;                                       // Identifier, "GBZ", etc

   double cf;                                           // Center frequency, Hz
   double w;                                                   // Bandwidth, Hz
   double br;                                // Bit rate, where applicable, bps

   int bin_start, bin_end;              // Bin numbers of cf + w/2 and cf - w/2

   complex double *asum;                         // Amplitude/phase accumulator
   complex double bsum;                      // Accumulator for bearing average
   double *power;

   int msk_chan;                  // Channel, 0..(chans-1) to use for MSK phase
   int msk_use_az; // 1 if MSK phase measurement directed on particular azimuth
   double msk_az;      // The azimuth if msk_use_az = 1, from the az= parameter
   double msk_mod;                                   // From the mod= parameter

   complex double msk_sum;                       // Averaging 4 * carrier phase
   complex double msk_dif;                    // Averaging 4 * modulation phase

   double mskAP_realW, mskAP_imagW;           // A set of 4 Goertzel algorithms
   double mskAP_d1, mskAP_d2;
   double mskAN_realW, mskAN_imagW;
   double mskAN_d1, mskAN_d2;

   double mskBP_realW, mskBP_imagW;
   double mskBP_d1, mskBP_d2;
   double mskBN_realW, mskBN_imagW;
   double mskBN_d1, mskBN_d2;

   double *msk_filt;                               // MSK input filter function

   int msk_fast;                                // Non-zero if fast method used
   int msk_cnt;                         // Sample counter for MSK discriminator
   complex double msk_PH1, msk_PH2;       // Averaging for MSK modulation phase
   int msk_wcnt;     // Counter between re-evaluations of input filter response

   FILE *fo;             // Output file handle
   int nf;               // Number of output fields
   timestamp bt;         // Base timestamp
   struct VTSID_DATA *d; // Data record
}
 *monitors = NULL;    // Table of signals to be monitored

static int nmonitors = 0;

//
// Variables for output policy SPECTRUM
//

static double CF_range1 = -1;
static double CF_range2 = -1;
static int spectrum_bin1 = 0;
static int spectrum_bin2 = 0;
static FILE *sf_fo;

///////////////////////////////////////////////////////////////////////////////
//  Various Utility Functions                                                //
///////////////////////////////////////////////////////////////////////////////

//
//  Log an alert message and send an email if required
//

static void alert( char *format, ...)
{
   FILE *f;
   va_list( ap);
   char cmd[100], temp[100];

   va_start( ap, format);
   vsprintf( temp, format, ap);
   va_end( ap);
 
   VT_report( 0, "%s", temp);

   if( !alert_on || !CF_mailaddr[0]) return;

   sprintf( cmd, "mail -s 'sid alert' '%s'", CF_mailaddr);
   if( (f=popen( cmd, "w")) == NULL)
   {
      VT_report( 0, "cannot exec [%s]: %s", cmd, strerror( errno));
      return;
   }

   fprintf( f, "vtsid: %s\n", temp);
   pclose( f);
}

//
//  Update the loss-of-signal state for the given channel
//

static void check_los( struct CHAN *c)
{
   double rms = sqrt( c->sum_sq/(FFTWID * monitor_int));
   if( !c->los_state)
   {
      if( !c->los_time && rms < CF_los_thresh) time( &c->los_time);
      if( c->los_time && rms > CF_los_thresh) c->los_time = 0;
      if( c->los_time && c->los_time + CF_los_timeout < time( NULL))
      {
         c->los_state = 1;
         c->los_time = 0;
         if( chans == 1) alert( "loss of signal");
         else alert( "loss of signal on channel %d", c->chan);
      }
   }
   else
   {
      if( !c->los_time && rms > CF_los_thresh) time( &c->los_time);
      if( c->los_time && rms < CF_los_thresh) c->los_time = 0;
      if( c->los_time && c->los_time + CF_los_timeout < time( NULL))
      {
         c->los_state = 0;
         c->los_time = 0;
         if( chans == 1) alert( "signal restored");
         else alert( "signal restored on channel %d", c->chan);
      }
   } 
}

static inline double power( complex double c)
{
   return creal( c) * creal( c) + cimag( c) * cimag( c);
}

///////////////////////////////////////////////////////////////////////////////
//  Monitor Output Functions                                                 //
///////////////////////////////////////////////////////////////////////////////

//
// Open a new monitor file and write the header
// 
static FILE *open_monitor_file( struct MONITOR *m,
                                timestamp stamp, char *out_prefix)
{
   FILE *f;
   char dirname[200], linkname[200], filename[200], tmpname[200];

   // Create a monitor subdir
   sprintf( dirname, "%s/mon", CF_datadir);
   if( mkdir( dirname, 0755) < 0 &&
       errno != EEXIST) 
      VT_bailout( "cannot mkdir %s, %s", dirname, strerror( errno));

   // Create a directory for this monitor
   sprintf( dirname, "%s/mon/%s", CF_datadir, m->ident);
   if( mkdir( dirname, 0755) < 0 &&
       errno != EEXIST) 
      VT_bailout( "cannot mkdir %s, %s", dirname, strerror( errno));

   sprintf( filename, "%s/mon/%s/%s", CF_datadir, m->ident, out_prefix);
   if( (f=fopen( filename, "a")) == NULL)
         VT_bailout( "cannot open [%s], %s", filename, strerror( errno));

   struct VTSID_HDR hdr;
   memset( &hdr, 0, sizeof( struct VTSID_HDR));

   hdr.magic = VTSID_MAGIC_MONITOR;
   hdr.bt_secs = (uint32_t) timestamp_secs( stamp);
   hdr.bt_nsec = (uint32_t) (timestamp_frac( stamp)*1e9);
   hdr.type = m->type;
   strcpy( hdr.ident, m->ident);
   hdr.interval = monitor_int * FFTWID/(double) sample_rate;
   hdr.frequency = m->cf;
   hdr.width = m->w;
   strcpy( hdr.vtversion, PACKAGE_VERSION);

   //
   //  Work out number of fields and the field definitions.
   //

   struct VTSID_FIELD *fields = NULL;
   int i;

   // Always one amplitude field for each channel
   fields = 
      VT_realloc( fields, (hdr.nf + chans) * sizeof( struct VTSID_FIELD));
   for( i=0; i<chans; i++, hdr.nf++)
   {
      fields[hdr.nf].type = SF_AMPLITUDE;
      fields[hdr.nf].cha = i;
      fields[hdr.nf].chb = 0;
      fields[hdr.nf].pad = 0;
   }
   
   if( CF_do_phase)    // Phase measurement enabled?
   {
      if( m->type == SIGTYPE_CW)
      {
         // One phase for each input channel
         fields = 
            VT_realloc( fields, (hdr.nf + chans) * sizeof( struct VTSID_FIELD));
         for( i=0; i<chans; i++, hdr.nf++)
         {
            fields[hdr.nf].type = SF_PHASE_CAR_360;
            fields[hdr.nf].cha = i;
            fields[hdr.nf].chb = 0;
            fields[hdr.nf].pad = 0;
         }
      }

      if( m->type == SIGTYPE_MSK)
      {
         // One carrier phase, either from channel specified by msk_chan, or
         // along a given azimuth.
         fields = 
            VT_realloc( fields, (hdr.nf + 1) * sizeof( struct VTSID_FIELD));
        
         fields[hdr.nf].type = m->msk_mod == 180 ?
                                  SF_PHASE_CAR_180 : SF_PHASE_CAR_90;
         if( m->msk_use_az)
         {
            fields[hdr.nf].cha = 0;
            fields[hdr.nf].chb = 1;
         }
         else
         {
            fields[hdr.nf].cha = m->msk_chan;
            fields[hdr.nf].chb = 0;
         }
         fields[hdr.nf].pad = 0;
         hdr.nf++;

         if( m->msk_mod == 90 && !m->msk_fast)
         {
            // One modulation phase
            fields = 
               VT_realloc( fields, (hdr.nf + 1) * sizeof( struct VTSID_FIELD));
           
            fields[hdr.nf].type = SF_PHASE_MOD_90;
            if( m->msk_use_az)
            {
               fields[hdr.nf].cha = 0;
               fields[hdr.nf].chb = 1;
            }
            else
            {
               fields[hdr.nf].cha = m->msk_chan;
               fields[hdr.nf].chb = 0;
            }
            fields[hdr.nf].pad = 0;
            hdr.nf++;
         }
      }
   }

   // One bearing field, if polar mode is configured
   if( polar_mode)
   {
      fields = 
         VT_realloc( fields, (hdr.nf + 1) * sizeof( struct VTSID_FIELD));
      fields[hdr.nf].type = polar3 ? SF_BEARING_360 : SF_BEARING_180;
      fields[hdr.nf].cha = 0;
      fields[hdr.nf].chb = 0;
      fields[hdr.nf].pad = 0;
      hdr.nf++;
   }
 
   // 
   //  Write header and fields array.
   //

   if( fwrite( &hdr, sizeof( struct VTSID_HDR), 1, f) != 1 ||
       fwrite( fields, sizeof( struct VTSID_FIELD), hdr.nf, f) != hdr.nf)
      VT_bailout( "cannot write output file [%s]: %s",
                     filename, strerror( errno));

   fflush( f);

   //
   //  Update the symlink which points to the current data file.
   //

   sprintf( linkname, "%s/mon/%s/current", CF_datadir, m->ident);
   sprintf( tmpname, "%s/mon/%s/current.new", CF_datadir, m->ident);

   if( symlink( out_prefix, tmpname) < 0)
      VT_bailout( "cannot symlink to %s, %s", out_prefix, strerror( errno));
   if( rename( tmpname, linkname) < 0)
      VT_bailout( "cannot symlink to %s, %s", filename, strerror( errno));

   //
   //  Set up the monitor variables required for data records
   //
   m->bt = stamp;
   m->nf = hdr.nf;
   m->d = VT_malloc( sizeof( int32_t)  + sizeof( float) * m->nf); 

   return f;
}

static void make_output_prefix( char *s, time_t sec)
{
   struct tm *tm = gmtime( &sec);

   sprintf( s, "%02d%02d%02d-%02d%02d%02d", 
             tm->tm_year % 100, tm->tm_mon+1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

//
//  Output each monitor into its own data file
//

static void output_monitor_records( timestamp tstamp)
{
   int i, j;
   struct MONITOR *m;

   // Decide if a new filename prefix is required

   static time_t output_file_sec = 0;
   char out_prefix[200];

   if( timestamp_secs(tstamp)/86400 != output_file_sec/86400)
   {
      output_file_sec = timestamp_secs( tstamp);
      make_output_prefix( out_prefix, output_file_sec);

      VT_report( 1, "monitor output prefix %s", out_prefix);

      for( m = monitors, i = 0; i < nmonitors; i++, m++)
      {
         if( m->skip) continue;

         if( m->fo) fclose( m->fo);
         m->fo = open_monitor_file( m, tstamp, out_prefix);
      }
   }

   for( m = monitors, i = 0; i < nmonitors; i++, m++)
   {
      if( m->skip) continue;

      m->d->ts = timestamp_diff( tstamp, m->bt) / 100e-6; 
      int fn = 0;
      int ch;

      for( ch=0; ch<chans; ch++)
      {
         // SIGTYPE_NOISE is recorded as a noise density, otherwise record
         // the total power in the monitor bandwidth.

         double e = m->power[ch] / monitor_int;
         if( m->type == SIGTYPE_NOISE)
            e /= DF * (m->bin_end - m->bin_start + 1);

         m->d->data[fn++] = sqrt( e/window_norm)/CF_bins/sqrt(2);
      }

      if( CF_do_phase && m->type == SIGTYPE_CW)
         for( ch=0; ch<chans; ch++)
         {
            double pa = carg( m->asum[ch]) * 180/M_PI;
            while( pa < -180) pa += 360;
            while( pa >= 180) pa -= 360;
            m->d->data[fn++] = pa;
         }

      if( CF_do_phase && m->type == SIGTYPE_MSK)
      {
         if( m->msk_mod == 180)
         {
            double cp = -carg( m->msk_sum) * 90/M_PI;
            m->d->data[fn++] = cp;
         }
         else
         if( !m->msk_fast)
         {
            double cp;
            cp = -carg( m->msk_sum) * 45/M_PI;
            m->d->data[fn++] = cp;
            cp = -carg( m->msk_dif) * 45/M_PI;
            m->d->data[fn++] = cp;
         }
         else
         {
            double cp;
            cp = carg( m->msk_sum) * 45/M_PI;
            m->d->data[fn++] = cp;
         }
      }

      if( polar_mode)
      {
         double bearing = carg( m->bsum) * 180/M_PI;
         if( !polar3)
         {
            // 2-axis system, so bearing is only mod 180 and it is bearing*2
            // which has been averaged
            bearing /= 2;
            while( bearing < 0) bearing += 180;
            while( bearing >= 180) bearing -= 180;
         }
         m->d->data[fn++] = bearing;
      }

      for( j=0; j<fn; j++) if( isnan( m->d->data[j])) break;
      if( j == fn)
      {
         if( fwrite( m->d, sizeof( int32_t) + sizeof( float) * m->nf, 
                     1, m->fo) != 1)
           VT_bailout( "cannot write output file: %s", strerror( errno));

         fflush( m->fo);
      }
      for( ch=0; ch<chans; ch++) m->asum[ch] = 0;
      m->msk_sum = m->msk_dif = 0;
   }
}

//
//  Reset the accumulators of the output record ready for the next monitor
//  interval.
//

static void reset_monitors( void)
{
   int i, ch;

   struct MONITOR *b;
   for( b = monitors, i = 0; i < nmonitors; i++, b++)
   {
      for( ch = 0; ch < chans; ch++) b->power[ch] = 0;  
      b->bsum = 0;
   }
   monitor_block_cnt = 0;
   monitor_output_stamp = timestamp_ZERO;
}

///////////////////////////////////////////////////////////////////////////////
//  Spectrum Output Functions                                                //
///////////////////////////////////////////////////////////////////////////////

static struct VTSID_DATA *specdata = NULL;
static timestamp specbt = timestamp_ZERO;

//
// Open a new spectrum file and write the header
// 
static FILE *open_spectrum_file( timestamp stamp, char *out_prefix)
{
   FILE *f;
   char dirname[200], linkname[200], filename[200], tmpname[200];

   // Create a spectrum subdir
   sprintf( dirname, "%s/spec", CF_datadir);
   if( mkdir( dirname, 0755) < 0 &&
       errno != EEXIST) 
      VT_bailout( "cannot mkdir %s, %s", dirname, strerror( errno));

   sprintf( filename, "%s/spec/%s", CF_datadir, out_prefix);
   if( (f=fopen( filename, "a")) == NULL)
         VT_bailout( "cannot open [%s], %s", filename, strerror( errno));

   struct VTSID_HDR hdr;
   memset( &hdr, 0, sizeof( struct VTSID_HDR));

   hdr.magic = VTSID_MAGIC_SPECTRUM;
   hdr.bt_secs = (uint32_t) timestamp_secs( stamp);
   hdr.bt_nsec = (uint32_t) (timestamp_frac( stamp)*1e9);
   hdr.interval = spectrum_int * FFTWID/(double) sample_rate;
   hdr.spec_base = DF * spectrum_bin1;
   hdr.spec_step = DF;
   hdr.spec_size = spectrum_bin2 - spectrum_bin1 + 1;
   strcpy( hdr.vtversion, PACKAGE_VERSION);

   //
   //  Work out number of fields and the field definitions.
   //

   struct VTSID_FIELD *fields = NULL;
   int i;

   // Always one amplitude field for each channel
   fields = 
      VT_realloc( fields,
                     (hdr.nf + chans) * sizeof( struct VTSID_FIELD));
   for( i=0; i<chans; i++, hdr.nf++)
   {
      fields[hdr.nf].type = SF_AMPLITUDE;
      fields[hdr.nf].cha = i;
      fields[hdr.nf].chb = 0;
      fields[hdr.nf].pad = 0;
   }
   
   // One bearing field, if polar mode is configured
   if( polar_mode)
   {
      fields = 
         VT_realloc( fields, (hdr.nf + 1) * sizeof( struct VTSID_FIELD));
      fields[hdr.nf].type = polar3 ? SF_BEARING_360 : SF_BEARING_180;
      fields[hdr.nf].cha = 0;
      fields[hdr.nf].chb = 0;
      fields[hdr.nf].pad = 0;
      hdr.nf++;
   }
 
   // 
   //  Write header and fields array.
   //

   if( fwrite( &hdr, sizeof( struct VTSID_HDR), 1, f) != 1 ||
       fwrite( fields, sizeof( struct VTSID_FIELD), hdr.nf, f) != hdr.nf)
      VT_bailout( "cannot write output file [%s]: %s",
                     filename, strerror( errno));

   fflush( f);

   //
   //  Update the symlink which points to the current data file.
   //

   sprintf( linkname, "%s/spec/current", CF_datadir);
   sprintf( tmpname, "%s/spec/current.new", CF_datadir);

   if( symlink( out_prefix, tmpname) < 0)
      VT_bailout( "cannot symlink to %s, %s", out_prefix, strerror( errno));
   if( rename( tmpname, linkname) < 0)
      VT_bailout( "cannot symlink to %s, %s", filename, strerror( errno));

   specbt = stamp;
   if( !specdata)
      specdata = VT_malloc( sizeof( int32_t) +
                  sizeof( float) * hdr.nf * hdr.spec_size); 
   return f;
}

//
//  Output a record to the current spectrum file
//

static void output_spectrum_record( timestamp tstamp)
{
   // Decide if a new filename prefix is required, set a flag if so

   static time_t output_file_sec = 0;
   char out_prefix[200];

   if( timestamp_secs( tstamp)/86400 != output_file_sec/86400)
   {
      output_file_sec = timestamp_secs( tstamp);
      make_output_prefix( out_prefix, output_file_sec);

      VT_report( 1, "spectrum output prefix %s", out_prefix);

      if( sf_fo) fclose( sf_fo);
      sf_fo = open_spectrum_file( tstamp, out_prefix);
   }

   specdata->ts = timestamp_diff(tstamp, specbt) / 100e-6; 
   int fn = 0;

   int i, ch;
   for( i=spectrum_bin1; i<=spectrum_bin2; i++)
   {
      for( ch=0; ch<chans; ch++)
      {
         struct CHAN *cp = channels + ch;
         double e = cp->powspec[i]/spectrum_int;
         specdata->data[fn++] = sqrt( e/window_norm)/CF_bins/sqrt(2);
      }

      if( polar_mode)
      {
         double bearing = carg( spec_bsum[i]) * 180/M_PI;
 
         if( !polar3)
         {
            // 2-axis system, so bearing is only mod 180 and it is bearing*2
            // which has been averaged
            bearing /= 2;
            while( bearing < 0) bearing += 180;
            while( bearing >= 180) bearing -= 180;
         }
         specdata->data[fn++] = bearing;
      }
   }

   if( fwrite( specdata, sizeof( int32_t) + sizeof( float) * fn, 
                  1, sf_fo) != 1)
         VT_bailout( "cannot write output file: %s", strerror( errno));
   fflush( sf_fo);
}

//
//  Reset the accumulators of the output record ready for the next spectrum
//  interval.
//

static void reset_spectrum( void)
{
   int i, ch;

   for( ch = 0; ch < chans; ch++)
   {
      struct CHAN *cp = channels + ch;
      cp->peak = cp->sum_sq = 0;
      for( i=0; i<CF_bins; i++) cp->powspec[i] = 0;
   }

   for( i=0; i<CF_bins; i++) spec_bsum[i] = 0;

   spectrum_block_cnt = 0;
   spectrum_output_stamp = timestamp_ZERO;
}


///////////////////////////////////////////////////////////////////////////////
//  Signal Processing                                                        //
///////////////////////////////////////////////////////////////////////////////

//
//  Bearing calculations
//

static void do_polar( void)
{
   static int init = 0;
   static double cos1;
   static double cos2;
   static double sin1;
   static double sin2;
   static double det;

   if( !init)    // First time through?   Initialise a few things.
   {
      init = 1;
      // Matrix coefficients which synthesize N/S and E/W signals from the
      // actual loop azimuths. 
      cos1 = cos( polar1->azimuth);
      cos2 = cos( polar2->azimuth);
      sin1 = sin( polar1->azimuth);
      sin2 = sin( polar2->azimuth);
      det =  cos1*sin2 - sin1*cos2;
   }

   int i;
   for( i=0; i<CF_bins; i++)
   {
      // Synthesise N/S and E/W signals
      complex double ew = (-cos2 * polar1->fft_out[i] +
                            cos1 * polar2->fft_out[i]) / det;

      complex double ns = ( sin2 * polar1->fft_out[i] -
                            sin1 * polar2->fft_out[i]) / det;

      // Average phase difference between N/S and E/W
      double phsin = cimag( ns) * creal( ew) - creal( ns) * cimag( ew);
      double phcos = creal( ns) * creal( ew) + cimag( ns) * cimag( ew);
      double a = atan2( phsin, phcos); // Angle between N/S and E/W components

      // Signal amplitudes
      double mag_ew = cabs( ew);
      double mag_ns = cabs( ns);

      // Signal powers
      double pow_ew = mag_ew * mag_ew; // Power, E/W signal
      double pow_ns = mag_ns * mag_ns; // Power, N/S signal
      double pwr = pow_ew + pow_ns;

      // Determine the bearing as the major axis of the Lissajous figure,
      // as per Watson-Watt.  Actually, we calculate bearing * 2, or rather,
      // sin and cos of that.
     
      double bearing2sin = 2 * mag_ew * mag_ns * cos( a);
      double bearing2cos = pow_ns - pow_ew; 

      if( !polar3)    // No vertical E-field available?
      {
         // Average the sin, cos of double the bearing to achieve bearing
         // mod 180. Contributions to the average are weighted by the total
         // signal power.
  
         complex double c = pwr * (bearing2cos + I*bearing2sin); 
         bsum[i] = c;
         spec_bsum[i] += c;
      }
      else
      {
         // E-field is available so do mod 360 bearing.   First do mod 180.
         double bearing180 = atan2( bearing2sin, bearing2cos)/2;
         if( bearing180 < 0) bearing180 += M_PI;
         else
         if( bearing180 >= M_PI) bearing180 -= M_PI;

         // Synthesise a loop in the plane of incidence
         complex double hh = ew * sin( bearing180) +
                             ns * cos( bearing180);

         // Vertical E-field
         complex double ve = polar3->fft_out[i];

         // Phase angle between E and H
         double pha =
              atan2( cimag( hh) * creal( ve) - creal( hh) * cimag( ve),
                     creal( hh) * creal( ve) + cimag( hh) * cimag( ve));

         // If necessary flip to the correct quadrant
         double bearing360 = bearing180;
         if( pha < -M_PI/2 || pha > M_PI/2) bearing360 += M_PI;

         complex double c =  pwr * (cos( bearing360) + I*sin( bearing360));
         bsum[i] = c;
         spec_bsum[i] += c;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
// MSK Carrier Phase                                                         //
///////////////////////////////////////////////////////////////////////////////

//
//  Two methods are implemented:
//    msk_phase_slow:  Averages the phase, mod 90 or mod 180 over periods much
//                     longer than the bit period, by frequency doubling and
//                     averaging the resulting carriers;
//    msk_phase_fast:  Detects the frequency switches at the bitrate using a
//                     discriminator and maintains separate averages for the
//                     upper and lower carrier frequencies;
//

static void msk_phase_slow( struct MONITOR *b, double *inbuf)
{
   //
   // Mix down to baseband I and Q by multiplying by a complex local oscillator
   // at the MSK carrier frequency.   Then square the IQ signal to convert the
   // 4-point constellation to a 2-point.
   //

   double loph = VT_phase( first, b->cf);  // Local oscillator absolute phase
   int i;
   for( i=0; i<FFTWID; i++, loph += b->cf * 2 * M_PI/sample_rate)
   {
      double f = inbuf[i];

      // Mix down and square
      complex double IQ = f * cos( loph) + I * f * sin( loph);
      complex double IQ_square = IQ * IQ;

      // Feed into a pair of complex 1-bin Fourier transforms at the positive
      // and negative bitrate frequencies.  Each is implemented as a pair of
      // real transforms using Goertzel algorithm.

      double y;
      y = creal(IQ_square) + b->mskAP_realW * b->mskAP_d1 - b->mskAP_d2;
      b->mskAP_d2 = b->mskAP_d1;   b->mskAP_d1 = y;
    
      y = cimag(IQ_square) + b->mskBP_realW * b->mskBP_d1 - b->mskBP_d2;
      b->mskBP_d2 = b->mskBP_d1;   b->mskBP_d1 = y;

      y = creal(IQ_square) + b->mskAN_realW * b->mskAN_d1 - b->mskAN_d2;
      b->mskAN_d2 = b->mskAN_d1;   b->mskAN_d1 = y;
    
      y = cimag(IQ_square) + b->mskBN_realW * b->mskBN_d1 - b->mskBN_d2;
      b->mskBN_d2 = b->mskBN_d1;   b->mskBN_d1 = y;
   }

   //
   //  Finalise the Goertzels.
   // 
   complex double A, B;

   A = 0.5 * b->mskAP_realW * b->mskAP_d1 - b->mskAP_d2
                                +  I * b->mskAP_imagW * b->mskAP_d1;
   B = 0.5 * b->mskBP_realW * b->mskBP_d1 - b->mskBP_d2
                                +  I * b->mskBP_imagW * b->mskBP_d1;

   complex double Gp = A + I*B;  // Amplitude at +bitrate

   A = 0.5 * b->mskAN_realW * b->mskAN_d1 - b->mskAN_d2
                       +  I * b->mskAN_imagW * b->mskAN_d1;
   B = 0.5 * b->mskBN_realW * b->mskBN_d1 - b->mskBN_d2
                       +  I * b->mskBN_imagW * b->mskBN_d1;

   complex double Gn = A + I*B;  // Amplitude at -bitrate

   double ph = VT_phase( timestamp_add( first, (FFTWID+1)/(double)sample_rate),
                         b->br);
   Gp *= cos(ph) - I*sin(ph);
   Gn *= cos(ph) + I*sin(ph);

   if( b->msk_mod == 90)
   {
      b->msk_sum += Gp * Gn;
      b->msk_dif += Gn / Gp;
   }
   else
      b->msk_sum += Gp;
     
   b->mskAP_d1 = b->mskAP_d2 = b->mskBP_d1 = b->mskBP_d2 = 0;
   b->mskAN_d1 = b->mskAN_d2 = b->mskBN_d1 = b->mskBN_d2 = 0;

   if( msk_debug && !strcmp( b->ident, msk_debug))
      VT_report( 0, " c1 %7.2f c2 %7.2f",
         carg(Gp)*180/M_PI, carg(Gn)*180/M_PI);
}

static void msk_phase_fast( struct MONITOR *m, double in, int sc)
{
   int i;

   // Update a pair of Goertzel algorithms, one at the upper bit frequency, one
   // at the lower

   double y;
   i = (m->msk_cnt + msk_interval/2) % msk_interval;
   y = in*msk_win[i] + m->mskAP_realW * m->mskAP_d1 - m->mskAP_d2;
   m->mskAP_d2 = m->mskAP_d1;   m->mskAP_d1 = y;
    
   y = in*msk_win[i] + m->mskAN_realW * m->mskAN_d1 - m->mskAN_d2;
   m->mskAN_d2 = m->mskAN_d1;   m->mskAN_d1 = y;
   
   i = m->msk_cnt % msk_interval;
   y = in*msk_win[i] + m->mskBP_realW * m->mskBP_d1 - m->mskBP_d2;
   m->mskBP_d2 = m->mskBP_d1;   m->mskBP_d1 = y;
    
   y = in*msk_win[i] + m->mskBN_realW * m->mskBN_d1 - m->mskBN_d2;
   m->mskBN_d2 = m->mskBN_d1;   m->mskBN_d1 = y;

   m->msk_cnt++;

   complex double C1, C2;   // Measured amplitudes, upper and lower frequencies
   if( m->msk_cnt == msk_interval/2)
   {
      C1 = 0.5 * m->mskAP_realW * m->mskAP_d1 - m->mskAP_d2
                                   +  I * m->mskAP_imagW * m->mskAP_d1;
   
      C2 = 0.5 * m->mskAN_realW * m->mskAN_d1 - m->mskAN_d2
                          +  I * m->mskAN_imagW * m->mskAN_d1;
   
      m->mskAP_d1 = m->mskAP_d2 = m->mskAN_d1 = m->mskAN_d2 = 0;
   }
   else
   if( m->msk_cnt == msk_interval)
   {
      C1 = 0.5 * m->mskBP_realW * m->mskBP_d1 - m->mskBP_d2
                                   +  I * m->mskBP_imagW * m->mskBP_d1;
   
      C2 = 0.5 * m->mskBN_realW * m->mskBN_d1 - m->mskBN_d2
                          +  I * m->mskBN_imagW * m->mskBN_d1;
   
      m->mskBP_d1 = m->mskBP_d2 = m->mskBN_d1 = m->mskBN_d2 = 0;
      m->msk_cnt = 0;
   }
   else return;

   // d is the discriminator output
   double d = (cabs(C1) - cabs(C2))/(cabs(C1) + cabs(C2));
   double pd = carg(m->msk_PH1/m->msk_PH2);

   // Update the average phase of the upper or lower signal, according to
   // whether d is +ve or -ve.   The contribution to the average is weighted
   // by abs(d) to minimise the effect of measurements which straddle a 
   // switch between frequencies.
   if( d > 0)
   {
      double ph =
                VT_phase( timestamp_add( first, (sc+1)/(double)sample_rate),
                          m->cf + m->br/2);
      C1 *= (cos(ph) - I*sin(ph)) * fabs(d);
      complex double P1 = C1 * C1;

      m->msk_PH1 = m->msk_PH1 * msk_smooth + P1 * (1-msk_smooth);
      m->msk_sum += P1 * P1 * (cos(pd) - I*sin(pd));
   }
   else
   {
      double ph =
            VT_phase( timestamp_add( first, (sc+1)/(double)sample_rate),
                      m->cf - m->br/2);
      C2 *= (cos(ph) - I*sin(ph)) * fabs(d);
      complex double P2 = C2 * C2;

      m->msk_PH2 = m->msk_PH2 * msk_smooth + P2 * (1-msk_smooth);
      m->msk_sum += P2 * P2 * (cos(pd) + I*sin(pd));
   }

   if( msk_debug && !strcmp( msk_debug, m->ident))
   {
      double cap = carg(C1*C1*C1*C1)/4 + M_PI/4;
      double can = carg(C2*C2*C2*C2)/4 + M_PI/4;

      char temp[50]; 
      VT_format_timestamp( temp,
                           timestamp_add( first, sc/(double) sample_rate));

      VT_report( 0,
        "%s C %7.2f %7.2f d %7.3f PH %6.2f %6.2f pd %6.2f",
             temp, cap*180/M_PI, can*180/M_PI, d,
             carg(m->msk_PH1)*90/M_PI, carg( m->msk_PH2)*90/M_PI,
             pd*180/M_PI);
   }
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

//
//  process_frame() is called when a block of input samples is ready to analyse.
//  The input time domain data in channels[].fft_in[] has already been
//  windowed.
//

static void process_frame( void)
{
   int ch, i, j;
   struct MONITOR *m;

   //
   // Fourier transform each input channel, accumulate the signal power
   // per bin, and check for loss of signal.
   //
   for( ch=0; ch<chans; ch++)
   {
      struct CHAN *cp = channels + ch;

      fftw_execute( cp->plan_fwd);

      for( i=0; i<CF_bins; i++) cp->powspec[ i] += power( cp->fft_out[i]);
   
      check_los( cp);
   }

   //
   // Sum the signal power per channel for each monitor.
   // 
   for( m = monitors, i = 0; i < nmonitors; i++, m++)
   {
      if( m->skip) continue;

      int n1 = m->bin_start;
      int n2 = m->bin_end;

      for( ch=0; ch<chans; ch++)
      {
         struct CHAN *cp = channels + ch;

         // Sum the signal power across all the bins in the monitor bandwidth
         double e = 0;
         for( j = n1; j <= n2; j++) e += power( cp->fft_out[j]);

         m->power[ch] += e;   // Sum total power to the end of monitor interval
      }
   }

   //
   // Do 2-axis or 3-axis bearing processing.   Bearing measurements are
   // summed by direction cosines stored in a complex number.
   // 
   if( polar_mode)
   {
      do_polar();

      // After do_polar() the power-weighted direction cosines (bsum[]) of
      // the bearing of each bin are available.   Accumulate average of these
      // over the bin range of each monitor.

      for( m = monitors, i = 0; i < nmonitors; i++, m++)
         if( !m->skip)
            for( j = m->bin_start; j <= m->bin_end; j++) m->bsum += bsum[j];
   }

   //
   // Do phase analysis
   //
   if( CF_do_phase)
      for( m = monitors, i = 0; i < nmonitors; i++, m++)
      { 
         if( m->skip) continue;

         //
         // Phase of CW monitors.   Only the center bin of the monitor's band
         // is used, maybe we should use the whole band.
         //
         if( m->type == SIGTYPE_CW)
         {
            int j = m->cf/DF;   // Center bin

            // Rotate to adjust for the start timestamp of this transform
            double ph =
               VT_phase( timestamp_add( first, FFTWID/2/(double)sample_rate),
                         m->cf);
            complex double c = cos(ph) - I*sin(ph); 

            for( ch=0; ch<chans; ch++)
               m->asum[ch] += channels[ch].fft_out[j] * c;
         }

         //
         // Phase of MSK monitors.
         //
         if( m->type == SIGTYPE_MSK)
         {
            if( !m->msk_use_az)
            {
               // Do phase measurement on a particular input channel
               int ch = m->msk_chan;
               for( j=0; j<FFTWID/2+1; j++)
               {
                  complex double v = channels[ch].fft_out[j];
                  msk_fdata[j] = v * m->msk_filt[j];
               }
            }
            else
            {
               // Do phase measurement on a loop synthesized on the requested
               // azimuth.
               double cos1 = cos( polar1->azimuth);
               double cos2 = cos( polar2->azimuth);
               double sin1 = sin( polar1->azimuth);
               double sin2 = sin( polar2->azimuth);
               double det =  cos1*sin2 - sin1*cos2;

               double cos_msk_az = cos( m->msk_az);
               double sin_msk_az = sin( m->msk_az);

               // Synthesise N/S and E/W signals from the actual loop azimuths,
               // then synthesize a signal on the requested azimuth.
               for( j=0; j<FFTWID/2+1; j++) 
               {
                  complex double ew = (-cos2 * polar1->fft_out[j] +
                                        cos1 * polar2->fft_out[j]) / det;

                  complex double ns = ( sin2 * polar1->fft_out[j] -
                                        sin1 * polar2->fft_out[j]) / det;

                  complex double v = ew * sin_msk_az + ns * cos_msk_az;
                  msk_fdata[j] = v * m->msk_filt[j];
               }
            }

            // Reverse FFT msk_fdata into msk_tdata 
            fftw_execute( msk_plan);

            // Analyse the MSK phase
            if( m->msk_fast)
               for( j=0; j<FFTWID; j++) msk_phase_fast( m, msk_tdata[j], j);
            else msk_phase_slow( m, msk_tdata);
         }
      }

   //
   //  Output a data record when the number of FT blocks averaged equals
   //  the output interval.
   //

   if( ++monitor_block_cnt == monitor_int)
   {
      output_monitor_records( monitor_output_stamp);
      reset_monitors();             // Cleardown ready for the next record
   }

   if( ++spectrum_block_cnt == spectrum_int)
   {
      if( spectrum_enable) output_spectrum_record( spectrum_output_stamp);
      reset_spectrum();             // Cleardown ready for the next record
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Configuration File Stuff                                                 //
///////////////////////////////////////////////////////////////////////////////

static void config_monitor( char *ident, char *spec)
{
   //
   // Allocate a new monitor.
   // 
   monitors = VT_realloc( monitors, sizeof( struct MONITOR) * (nmonitors+1));
   struct MONITOR *m = monitors + nmonitors++;

   memset( m, 0, sizeof( struct MONITOR));
   m->ident = strdup( ident);

   //
   // Parse the signal type - always the first word.
   //
   if( !strncasecmp( spec, "signal", 6))
   {
      m->type = SIGTYPE_SIGNAL;  spec += 6;
   }
   else
   if( !strncasecmp( spec, "noise", 5))
   {
      m->type = SIGTYPE_NOISE;  spec += 5;
   }
   else
   if( !strncasecmp( spec, "cw", 2))
   {
      m->type = SIGTYPE_CW;
      spec += 2;
   }
   else
   if( !strncasecmp( spec, "msk", 3))
   {
      m->type = SIGTYPE_MSK;
      spec += 3;
   }
   else
      VT_bailout( "unrecognised signal type [%s] for %s", spec, m->ident);

   //
   // Parse the rest of the comma-separated monitor specification string.
   //
   while( spec && *spec)
   {
      if( *spec == ',') { spec++; continue; }
      
      char *p = strchr( spec, ',');
      if( p) *p++ = 0;

      if( !strncasecmp( spec, "f=", 2)) m->cf = atof( spec + 2);
      else
      if( !strncasecmp( spec, "w=", 2)) m->w = atof( spec + 2);
      else
      if( !strncasecmp( spec, "br=", 3)) m->br = atof( spec + 3);
      else
      if( !strncasecmp( spec, "ch=", 3)) m->msk_chan = atoi( spec + 3) - 1;
      else
      if( !strncasecmp( spec, "az=", 3)) 
      {
         m->msk_az = atof( spec + 3) * M_PI/180;
         m->msk_use_az = 1;
      }
      else
      if( !strncasecmp( spec, "fast", 4)) m->msk_fast = 1;
      else
      if( !strncasecmp( spec, "mod=", 4)) m->msk_mod = atoi( spec+4);
      else
         VT_bailout( "unrecognised monitor spec [%s]", spec);

      spec = p;
   }

   //
   // Check some of the settings.
   //
   if( !m->cf) VT_bailout( "monitor %s: needs frequency f=", m->ident);
   if( m->w < 0) VT_bailout( "monitor %s: invalid bandwidth w=", m->ident);
   if( m->br < 0) VT_bailout( "monitor %s: invalid bitrate br=", m->ident);

   if( m->type == SIGTYPE_CW ||
       m->type == SIGTYPE_NOISE ||
       m->type == SIGTYPE_SIGNAL)
   {
      if( !m->w) VT_bailout( "monitor %s: needs bandwidth w=", m->ident);
      if( m->msk_mod) VT_report( 0, "monitor %s: mod= ignored", m->ident);
   }

   if( m->type == SIGTYPE_MSK)
   {
      if( !m->br) VT_bailout( "monitor %s: needs bit rate br=", m->ident);
      if( !m->w) m->w = 5 * m->br;
      if( m->w < 3 * m->br)
      {
         m->w = 3 * m->br;
         VT_report( 0, "monitor %s: bandwidth too narrow", m->ident);
         VT_report( 0, "monitor %s: setting bandwidth to %.0fHz",
                        m->ident, m->w); 
      }
      if( !m->msk_mod) m->msk_mod = 180;
      if( m->msk_mod != 90 && m->msk_mod != 180)
         VT_bailout( "monitor %s: mod= must be 90 or 180", m->ident);
      if( m->msk_mod == 180 && m->msk_fast)
      {
         m->msk_fast = 0;
         VT_report( 0, "monitor %s: fast option ignored", m->ident);
      }
   }
}

static void config_channel( char *type, char *spec)
{
   if( NC == MAXCHANS) VT_bailout( "limit of %d input channels", MAXCHANS);
   if( NC == chans) VT_bailout( "not enough input channels for this config");

   struct CHAN *ch = channels + NC;

   memset( ch, 0, sizeof( struct CHAN));
   ch->cal = 1.0;

   if( !strcmp( type, "efield")) ch->type = CH_EFIELD;
   else
   if( !strcmp( type, "hfield")) ch->type = CH_HFIELD;
   else
      VT_bailout( "must specify channel efield or hfield");

   while( spec && *spec)
   {
      char *p = strchr( spec, ',');
      if( p) *p++ = 0;

      if( !strncmp( spec, "az=", 3))
      {
         if( ch->type != CH_HFIELD)
            VT_bailout( "azimuth specified for non-hfield");

         ch->azimuth = atof( spec+3) * M_PI/180;
         if( ch->azimuth >= 2*M_PI || ch->azimuth < 0)
            VT_bailout( "azimuth out of range 0..360");
      }
      else
      if( !strncmp( spec, "cal=", 4)) ch->cal = atof( spec+4);
      else
         VT_bailout( "unrecognised channel option [%s]", spec);

      spec = p;
   }

   switch( ch->type)
   {
      case CH_EFIELD:
         VT_report( 1, "channel %d: E-field cal=%.3e", NC, ch->cal);
         if( !polar3) polar3 = ch;
         break;

      case CH_HFIELD:
         VT_report( 1, "channel %d: H-field azimuth %.1f cal=%.3e",
             NC, ch->azimuth * 180/M_PI, ch->cal);

         if( !polar1) polar1 = ch;
         else
         if( !polar2) polar2 = ch;
         else VT_bailout( "too many H-field channels specified");
 
         break;
   }
   NC++;
}

void load_config( void)
{
   int lino = 0, nf;
   FILE *f;
   char buff[100], *p, *fields[20];

   if( (f=fopen( config_file, "r")) == NULL)
      VT_bailout( "cannot open config file %s: %s",
                      config_file, strerror( errno));

   while( fgets( buff, 99, f))               // For each line of config file...
   {
      lino++;                                                   // Line counter

      // Remove line terminators and comments
      if( (p=strchr( buff, '\r')) != NULL) *p = 0;
      if( (p=strchr( buff, '\n')) != NULL) *p = 0;
      if( (p=strchr( buff, ';')) != NULL) *p = 0;

      // Unpack the line into whitespace separated fields
      p = buff;  nf = 0;
      while( 1)
      {
         while( *p && isspace( *p)) p++;
         if( !*p) break;
         fields[nf++] = p;
         while( *p && !isspace( *p)) p++;
         if( *p) *p++ = 0;
      }
      if( !nf) continue;                                        // Blank line?

      if( nf == 2 && !strcasecmp( fields[0], "monitor_interval"))
         CF_monitor_interval = atof( fields[1]);   // Seconds
      else
      if( nf == 2 && !strcasecmp( fields[0], "spectrum_interval"))
      {
         CF_spectrum_interval = atof( fields[1]);   // Seconds
         spectrum_enable = 1;
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "spectrum_lower"))
      {
         CF_range1 = atof( fields[1]);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "spectrum_upper"))
      {
         CF_range2 = atof( fields[1]);
      }
      else
      if( nf == 3 && !strcasecmp( fields[0], "monitor"))
      {
         config_monitor( fields[1], fields[2]);
      }
      else
      if( nf >= 2 && nf <= 3 && !strcasecmp( fields[0], "channel"))
      {
         config_channel( fields[1], nf == 3 ? fields[2] : "");
      }
      else
      if( nf == 3 && !strcasecmp( fields[0], "los"))
      {
         CF_los_thresh = atof( fields[1]);
         CF_los_timeout = atoi( fields[2]);
         VT_report( 1, "los threshold %.3f, timeout %d seconds", 
                    CF_los_thresh, CF_los_timeout);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "bins"))
      {
         CF_bins = atoi( fields[1]);
      }
      else
      if( nf == 1 && !strcasecmp( fields[0], "phase"))
      {
         CF_do_phase = 1;
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "resolution"))
      {
         CF_resolution = atof( fields[1]);
         CF_bins = 0.5 + sample_rate/(2*CF_resolution);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "datadir"))
      {
         struct stat st;
         CF_datadir = strdup( fields[1]);
         if( stat( CF_datadir, &st) < 0 || !S_ISDIR( st.st_mode))
            VT_bailout( "no data directory, %s", CF_datadir);
      }
      else
      if( nf == 2 && !strcasecmp( fields[0], "mail"))
         strcpy( CF_mailaddr, fields[1]);
      else
         VT_bailout( "error in config file, line %d [%s...]", lino, fields[0]);
   }

   fclose( f);
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void initialise_channel( struct CHAN *c, int chan)
{
   c->chan = chan;
   c->fft_in = (double *) VT_malloc( FFTWID * sizeof( double));
   c->fft_out = VT_malloc( sizeof( fftw_complex) * FFTWID);
   c->plan_fwd = fftw_plan_dft_r2c_1d( FFTWID, c->fft_in, c->fft_out,
                           FFTW_ESTIMATE | FFTW_DESTROY_INPUT);

   c->powspec = (double *) VT_malloc_zero( CF_bins * sizeof( double));

   // If a calibration wasn't given, set it to 1.0
   if( !c->cal) c->cal = 1.0;
}

static void usage( void)
{
   fprintf( stderr, "usage: vtsid [options] -c config_file input\n"
                    "\n"
                    "options:\n"
                    " -v        Increase verbosity\n"
                    " -B        Run in background\n"
                    " -L name   Specify logfile\n"
          );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtsid");

   int i;
   int background = 0;

   if( sizeof( struct VTSID_HDR) != 128) 
      VT_bailout( "VTSID_HDR size incorrect: %d, expecting 128",
         (int) sizeof( struct VTSID_HDR));
   if( sizeof( struct VTSID_FIELD) != 4)
      VT_bailout( "VTSID_FIELD size incorrect: %d, expecting 4",
         (int) sizeof( struct VTSID_FIELD));

   while( 1)
   {
      int c = getopt( argc, argv, "vBc:d:L:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'c') config_file = strdup( optarg);
      else 
      if( c == 'd') msk_debug = strdup( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( !config_file)
   {
      fprintf( stderr, "must specify config file, needs -c option");
      usage();
   }

   if( argc > optind + 1) usage();
   char *bname = strdup( optind < argc ? argv[optind] : "-");

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDIN : 0;
      VT_daemonise( flags);
   }

   //
   // Open the input stream, get sample rate and parse channel specification.
   //
   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);
   if( (vtfile = VT_open_input( bname)) == NULL)
      VT_bailout( "cannot open input %s: %s", bname, VT_error);

   sample_rate = VT_get_sample_rate( vtfile);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   VT_report( 1, "channels: %d, sample_rate: %d", chans, sample_rate);

   //
   // Load the configuration file.
   //
   load_config();
   if( !CF_datadir) CF_datadir = ".";
   if( CF_bins < 0) VT_bailout( "neither bins nor resolution given");
      
   DF = sample_rate/(double) FFTWID;
   cDF = DF * srcal;
   VT_report( 1, "resolution: bins=%d fftwid=%d df=%f", CF_bins, FFTWID, DF);

   // Convert CF_monitor_interval seconds to monitor_int frames
   monitor_int = rint( CF_monitor_interval * sample_rate / FFTWID);
   if( monitor_int == 0) monitor_int = 1;
   VT_report( 2, "monitor output interval: %d frames", monitor_int);

   // Convert CF_spectrum_interval seconds to spectrum_int frames
   if( spectrum_enable) // Spectrum file configured?
   {
      spectrum_int = rint( CF_spectrum_interval * sample_rate / FFTWID);
      if( spectrum_int == 0) spectrum_int = 1;
      VT_report( 2, "spectrum output interval: %d frames", spectrum_int);

      // Convert range variables Hertz to bins
      spectrum_bin1 = CF_range1 >= 0 ? CF_range1 / DF : 0;
      spectrum_bin2 = CF_range2 >= 0 ? CF_range2 / DF : CF_bins - 1;
      if( spectrum_bin2 >= CF_bins) spectrum_bin2 = CF_bins - 1;
      VT_report( 2, "spectrum range: %.1f to %.1f",
                    spectrum_bin1 * DF, spectrum_bin2 * DF);
   }   

   msk_smooth = exp( -1.0/(MSK_TC/MSK_GATE));
   msk_interval = MSK_GATE * sample_rate;
   msk_win = VT_malloc( msk_interval * sizeof( double));
   for( i=0; i<msk_interval; i++) msk_win[i] = sin( i * M_PI/msk_interval);

   //
   // Initialise monitors.
   //
   for( i=0; i<nmonitors; i++)
   {
      struct MONITOR *m = monitors + i;
     
      m->bin_start = (m->cf - m->w/2)/DF;
      m->bin_end = (m->cf + m->w/2)/DF;

      m->asum = VT_malloc_zero( sizeof( complex double) * chans);
      m->power = VT_malloc( sizeof( double) * chans); 

      if( m->type == SIGTYPE_MSK && !m->msk_fast)
      {
         if( m->msk_chan < 0 || m->msk_chan >= chans)
            VT_bailout( "invalid channel (%d) given for %s",
                        m->msk_chan+1, m->ident);

         m->mskAP_realW = m->mskBP_realW = 2 * cos( 2*M_PI * m->br/sample_rate);
         m->mskAP_imagW = m->mskBP_imagW = sin( 2*M_PI * m->br/sample_rate);

         m->mskAN_realW = m->mskBN_realW = 2 * cos( 2*M_PI * m->br/sample_rate);
         m->mskAN_imagW = m->mskBN_imagW = sin( 2*M_PI * -m->br/sample_rate);
      }

      if( m->type == SIGTYPE_MSK && m->msk_fast)
      {
         if( m->msk_chan < 0 || m->msk_chan >= chans)
            VT_bailout( "invalid channel (%d) given for %s",
                        m->msk_chan+1, m->ident);

         m->mskAP_realW = m->mskBP_realW =
                          2 * cos( 2 * M_PI * (m->cf + m->br/2)/sample_rate);
         m->mskAP_imagW = m->mskBP_imagW =
                          sin( 2 * M_PI * (m->cf + m->br/2)/sample_rate);

         m->mskAN_realW = m->mskBN_realW =
                          2 * cos( 2 * M_PI * (m->cf - m->br/2)/sample_rate);
         m->mskAN_imagW = m->mskBN_imagW =
                          sin( 2 * M_PI * (m->cf - m->br/2)/sample_rate);

         m->msk_cnt = 0;
      }

      if( m->type == SIGTYPE_MSK)
      {
         m->msk_filt = VT_malloc_zero( sizeof( double) * (FFTWID/2+1));

         int j1 = (m->cf - m->w/2)/DF;
         int j2 = (m->cf + m->w/2)/DF;
         if( j1 < 0) j1 = 0;
         if( j2 > FFTWID/2) j2 = FFTWID/2;

         int j;
         for( j=j1; j<=j2; j++) m->msk_filt[j] = 1;
      }

      VT_report( 1, "monitor %s %.3f %.1f %s", 
            m->ident, m->cf, m->w, sigtype_to_txt( m->type));
      VT_report( 2, "%s %d %d", m->ident, m->bin_start, m->bin_end);

      if( m->bin_start < 0)
      {
         m->bin_start = 0;
         VT_report( 0,
            "warning: monitor %s lower bound overlaps DC - truncated band",
            m->ident);
      }
      if( m->bin_start >= CF_bins)
      {
         m->skip = 1;
         VT_report( 0, "warning: monitor %s beyond Nyquist - dropped",
                       m->ident);
      }
      else
      if( m->bin_end >= CF_bins)
      {
         m->bin_end = CF_bins - 1;
         VT_report( 0,
            "warning: monitor %s overlaps Nyquist - truncated band",
            m->ident);
      }

      if( m->msk_az && !polar_mode)
         VT_bailout( "monitor %s: needs loop inputs to use az=", m->ident);
   }

   //
   // Initialise the input channels.
   // 
   for( i=0; i<chans; i++) initialise_channel( channels+i, i+1);

   //
   // Initialise spectrum data.
   //

   spec_bsum = VT_malloc_zero( CF_bins * sizeof( complex double));
   bsum = VT_malloc( CF_bins * sizeof( complex double));

   //
   // Things needed for MSK phase measurement
   // 
   msk_fdata = VT_malloc( sizeof( fftw_complex) * FFTWID);
   msk_tdata = VT_malloc( FFTWID * sizeof( double));
   msk_plan = fftw_plan_dft_c2r_1d( FFTWID, msk_fdata, msk_tdata,
                                    FFTW_ESTIMATE);

   reset_monitors();
   reset_spectrum();

   //
   //  Construct FFT window.  Really should be using a better window function
   //  to separate adjacent channels.
   //

   window = VT_malloc( sizeof( double) * FFTWID);
   window_norm = 0;
   for( i=0; i<FFTWID; i++)
   {
      window[i] = sin( i * M_PI/FFTWID);
      window_norm += window[i] * window[i];
   }
   window_norm /= FFTWID;

   VT_report( 1, "vtsid version %s: starting work", PACKAGE_VERSION);
   alert_on = 1;

   int grab_cnt = 0;                // Count of samples into the FFT buffer
   while( 1)
   {
      int e;
      if( (e = VT_is_block( vtfile)) < 0) VT_exit( "end of input");
     
      if( e)   // At start of input block?
      {
         srcal = VT_get_srcal( vtfile);

         if( VT_rbreak( vtfile)) // Timing gap between this and previous block?
         {
            // Force a reset of the output record integrations
            VT_report( 0, "timing break on input stream, resetting");
            grab_cnt = 0;
            reset_monitors();
            reset_spectrum();
         }
      }

      if( !grab_cnt)  // First sample of the Fourier transform, save timestamp
      {
         first = VT_get_timestamp( vtfile);

         // If this Fourier transform is the first of an averaging period,
         // save the timestamp to label the output record.
         if( timestamp_is_ZERO( monitor_output_stamp))
            monitor_output_stamp = first;
         if( timestamp_is_ZERO( spectrum_output_stamp))
            spectrum_output_stamp = first;
      }

      //  Take next input frame, keep track of RMS and peak, apply window and
      //  save in Fourier input buffer.

      double *inframe = VT_get_frame( vtfile);
      nin++;

      for( i=0; i<chspec->n; i++)
      {
         struct CHAN *c = channels + i;
         double f = inframe[chspec->map[i]] * c->cal;
        
         c->sum_sq += f * f;
         if( f > c->peak) c->peak = f;
         if( f < -c->peak) c->peak = -f;

         c->fft_in[grab_cnt] = f * window[grab_cnt];
      }

      // Run the FFT when the buffer is full
      if( ++grab_cnt == FFTWID)
      {
         grab_cnt = 0;
         process_frame();
      }
   }

   return 0;
}

