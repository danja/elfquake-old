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

///////////////////////////////////////////////////////////////////////////////
//  OSS and ALSA defs                                                        //
///////////////////////////////////////////////////////////////////////////////

//
//  Choose the right header files for the prevailing sound system
//

#if ALSA
   #include <alsa/asoundlib.h>
#endif

#if OSS
   #if HAVE__USR_LIB_OSS_INCLUDE_SYS_SOUNDCARD_H
      #include "/usr/lib/oss/include/sys/soundcard.h"
   #else
      #ifdef HAVE_LINUX_SOUNDCARD_H
         #include <linux/soundcard.h>
      #endif
   #endif

   #ifdef HAVE_SYS_SOUNDCARD_H
      #include <sys/soundcard.h>
   #endif

   #ifdef HAVE_SYS_UIO_H
      #include <sys/uio.h>
   #endif

   #ifdef HAVE_MACHINE_PCAUDIOIO_H
      #include <machine/pcaudioio.h>
   #endif
   
   #ifdef HAVE_SYS_AUDIOIO_H
      #include <sys/audioio.h>
   #endif
   
   #ifdef HAVE_SOUNDCARD_H
      #include <soundcard.h>
   #endif

   #ifdef HAVE_SYS_RESOURCE_H
      #include <sys/resource.h>
   #endif
#endif

//
//  Set a suitable default device
//

#if ALSA
   #define DEVICE "hw:0,0"
#endif

#if OSS
   #ifdef __OpenBSD__
      #define DEVICE "/dev/audio"
   #else
      #define DEVICE "/dev/dsp"
   #endif
#endif

///////////////////////////////////////////////////////////////////////////////
//  Globals                                                                  //
///////////////////////////////////////////////////////////////////////////////

static int chans = 2;              // Number of channels to read from soundcard
static int bytes = 2;                             // Number of bytes per sample
static int nread = 0;                     // Number of frames to read at a time

static char *device = DEVICE;                                   // Input device
static char *bname;                               // Output buffer or fifo name
static VTFILE *vtfile;                               // Handle to output stream

static double srcal = 1.0;               // Sample rate calibration coefficient
static double gain = 1.0;                                        // Output gain
static unsigned int sample_rate = 0;                     // Nominal sample rate

static timestamp timebase = timestamp_ZERO;            // Base of our timestamp
static uint64_t ntb = 0;             // Number of frames since timebase was set

static char *buffer_opts = NULL;                       // Argument to -A option

static int latency_frames = 0;          // Experimental, not used at the moment
static int three_byte_format = 0;

static uint64_t nout = 0;                            // Number of output frames
static double tadj = 0;     // Timing adjustment to be applied per output block
static timestamp Tfalse = timestamp_ZERO;    // False timestamp, from -T option
static int UFLAG = FALSE;                            // Set TRUE with -u option

#define NOMINAL_PERIOD   2.5e-3     // Desirable soundcard period size, seconds

///////////////////////////////////////////////////////////////////////////////
//  Timestamping                                                             //
///////////////////////////////////////////////////////////////////////////////

#define OFFSET_LIMIT 20e-3 // Reset if more than this many seconds timing error
#define PRE_RUN_SECS 20                         // Number of seconds of pre-run

static timestamp reftime = timestamp_ZERO;               // Reference timestamp
static uint32_t nft = 0;         // Number of frames since reftime last updated

// State variable for timestamping:-
#define STATE_RESET  0    // Transient state - reset everything and begin again
#define STATE_INIT   1    // Pre-run to settle down and get rough sample rate
#define STATE_SETUP  2    // Polish the sample rate
#define STATE_RUN    3    // Normal running

static int state = STATE_INIT;

//
//  Called immediately after a buffer of 'q' frames is received from the
//  soundcard driver.
//
static void timestamping( int q)
{
   static int n = 0;

   #define AJF_RUN 0.05   // Rate error adjustment factor
   #define AJF_SETUP 0.15
   #define MAX_DEFER 3

   nft += q;

   if( state == STATE_RESET)  
   {
      // When reading from stdin, or if -u option is given, don't bother with
      // timing, go straight to the 'run' state.
      if( UFLAG || !strcmp( device, "-"))
      {
         state = STATE_RUN;
         timebase = reftime =
                          !timestamp_is_ZERO( Tfalse) ? Tfalse : VT_rtc_time();
         nft = 0;
         ntb = nout = 0;
         return;
      }

      reftime = VT_rtc_time();
      nft = 0;
      state = STATE_INIT;
   }

   if( state == STATE_INIT) // Initial pre-run to let things settle down
   {
      if( nft < PRE_RUN_SECS * sample_rate) return;

      // End of pre-run: just get a rough estimate of sample rate
      state = STATE_SETUP;
      n = 0;

      srcal =
         (nft / timestamp_diff(VT_rtc_time(), reftime)) / (double) sample_rate;
      timebase = reftime = VT_rtc_time();
      ntb = nout = 0;
      nft = 0;

      VT_report( 1, "pre-run complete %.2f", srcal * sample_rate);
      return;
   }

   // No sample rate or timebase calibration when reading stdin or with -u
   if( UFLAG || !strcmp( device, "-")) return;

   // Only revise the timing roughly every 10 seconds
   if( nft < sample_rate * 10) return;

   // Expected time to read nft samples, based on the current srcal
   double expected_interval = nft/(double)(srcal * sample_rate);

   // Compare with RTC to calculate a raw sample rate error
   timestamp now = VT_rtc_time();
   double actual_interval = timestamp_diff( now, reftime);

   double err = (expected_interval - actual_interval)/actual_interval;
   double r = fabs( err  * vtfile->bsize/ 2.5); 

   double raw_rate = nft / actual_interval;

   static int defer = 0;

   if( state == STATE_SETUP) // Initial stabilising of sample rate loop
   {
      // Update smoothed estimate of sample rate calibration factor
      srcal *= 1 + err * AJF_SETUP;

      reftime = now; nft = 0;

      VT_report( 1, "setup %+.3e sr %.2f rr %.1f n=%d r=%.2f",
                    err, sample_rate * srcal, raw_rate, n, r);
      if( r > 20)
      {
         VT_report( 0, "rate error too large, resetting");
         state = STATE_RESET;
      }
      else
      if( r < 0.1 && n >= 0) n += 3;
      else
      if( r < 0.5 && n >= 0) n += 2;
      else
      if( r < 1 && n >= 0) n++;
      else
      if( r < 1) n = 0;
      else 
         n--;

      if( n < -10)
      {
         VT_report( 0, "persistent drift, resetting");
         state = STATE_RESET;
      }
      if( n >= 10)
      {
         // Success.  Timebase has settled enough to start running data.
         state = STATE_RUN;
         timebase = reftime;
         ntb = nout = 0;
      }
   }
   else
   if( state == STATE_RUN)   // Normal operation
   {
      // Allow a couple of bad readings to be ignored - they usually come
      // good again.  Occurs when we didn't get scheduled promptly enough.
      if( r > 0.5 && defer++ <= MAX_DEFER) return;

      // Update smoothed estimate of sample rate calibration factor
      srcal *= 1 + err * AJF_RUN;

      timebase = timestamp_add( timebase, expected_interval); ntb += nft;
      reftime = now; nft = 0;
      defer = 0;
      double offset =
                  timestamp_diff( timebase, reftime); // Timing offset, seconds

      // Timing offset is slewed towards zero by adjusting the timebase
      // by tadj seconds per output data block.
      tadj = offset / (10 * sample_rate/vtfile->bsize);

      // Limit the timebase adjustment to 0.25 samples per block
      if( tadj > 0.25/sample_rate) tadj = 0.25/sample_rate;
      if( tadj < -0.25/sample_rate) tadj = -0.25/sample_rate;

      VT_report( 1, "run %+.3e sr %.3f tadj %+.3e rr %.1f offs %+.3fmS r=%.2f",
                 err, sample_rate * srcal, tadj, raw_rate, 1000 * offset, r);
 
      if( fabs( offset) > OFFSET_LIMIT)
      {
         // Either the soundcard sample rate or the system clock has stepped
         // or is drifting too fast for the control loops to correct.
         // Reset everything and start again.
         VT_report( 0, "timebase error %.3f mS, resetting", 1000 * offset);
         state = STATE_RESET;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Bus Devices                                                              //
///////////////////////////////////////////////////////////////////////////////

//
//  Device listing.
//

static void list_devices( void)
{
   DIR *dir = opendir( "/sys/class/sound");
   if( !dir) VT_bailout( "cannot open sysfs /sys/class/sound");

   struct dirent *de;
   while( (de = readdir( dir)) != NULL)
   {
      if( strncmp( de->d_name, "card", 4)) continue;

      char path[100];
      sprintf( path, "/sys/class/sound/%s", de->d_name);

      struct stat st;
      if( lstat( path, &st) < 0 ||
          !S_ISLNK( st.st_mode)) continue;

      char link[500];
      int n;
      if( (n = readlink( path, link, 499)) < 0) continue; 
      link[n] = 0;

      char *t = strstr( link, "/sound/card");
      if( !t) continue;
      *t = 0;

      if( (t = strrchr( link, '/')) == NULL) continue;
      t++;

      if( strchr( t, '-'))   // USB bus ?
      {
         char *s = strchr( t, ':');  if( s) *s = 0;
         printf( "%s usb:%s\n", de->d_name, t);
      }
      else // PCI bus
      {
         char *s = strchr( t, '.');  if( s) *s = 0;
         printf( "%s pci:%s\n", de->d_name, t);
      }
   }

   closedir( dir);
}

static int lookup_device( char *bus_name, char *alsa_name)
{
   DIR *dir = opendir( "/sys/class/sound");
   if( !dir) VT_bailout( "cannot open sysfs /sys/class/sound");

   int found = FALSE;

   struct dirent *de;
   while( (de = readdir( dir)) != NULL)
   {
      if( strncmp( de->d_name, "card", 4)) continue;

      char path[100];
      sprintf( path, "/sys/class/sound/%s", de->d_name);

      struct stat st;
      if( lstat( path, &st) < 0 ||
          !S_ISLNK( st.st_mode)) continue;

      char link[500];
      int n;
      if( (n = readlink( path, link, 499)) < 0) continue;
      link[n] = 0;

      char *t = strstr( link, "/sound/card");
      if( !t) continue;
      *t = 0;

      if( (t = strrchr( link, '/')) == NULL) continue;
      t++;

      if( !strcmp( bus_name+4, t))
      {
         sprintf( alsa_name, "hw:%d,0", atoi( de->d_name + 4));
         found = TRUE;
         break;
      }

      if( !strncmp( bus_name, "usb:", 4))
      {
         char *s = strchr( t, ':');  if( s) *s = 0;
         if( !strcmp( bus_name+4, t))
         {
            sprintf( alsa_name, "hw:%d,0", atoi( de->d_name + 4));
            found = TRUE;
            break;
         }
      }
      else
      if( !strncmp( bus_name, "pci:", 4))
      {
         char *s = strchr( t, '.');  if( s) *s = 0;
         if( !strcmp( bus_name+4, t))
         {
            sprintf( alsa_name, "hw:%d,0", atoi( de->d_name + 4));
            found = TRUE;
            break;
         }
      }
      else VT_bailout( "invalid device name [%s]", bus_name);
   }

   closedir( dir);

   return found;
}

///////////////////////////////////////////////////////////////////////////////
//  OSS Soundcard Interface                                                  //
///////////////////////////////////////////////////////////////////////////////

#if OSS

#define soundsystem "OSS"
static int capture_handle;

static void setup_input_stream( void)
{
   int xchans, flags;
   struct stat st;

   VT_report( 1, "using OSS device %s", device);

   flags = O_RDONLY;
//   if( CF_open_excl) flags |= O_EXCL;

   if( (capture_handle = open( device, flags)) < 0)
      VT_bailout( "cannot open %s: %s", device, strerror( errno));

   if( fstat( capture_handle, &st) < 0)
      VT_bailout( "cannot stat %s: %s", device, strerror( errno));

   if( !S_ISCHR( st.st_mode)) 
      VT_bailout( "%s is not a character device", device);

   unsigned int format;
   unsigned int req_format = AFMT_S16_LE;
   switch( bytes)
   {
      case 1: req_format = AFMT_U8;       break;
      case 2: req_format = AFMT_S16_LE;   break;
      #ifdef AFMT_S24_LE
         case 3: req_format = AFMT_S24_LE;   break;
      #endif
      #ifdef AFMT_S32_LE
         case 4: req_format = AFMT_S32_LE;   break;
      #endif
   }

   int frag_size = 0;
   int buff_size = 0;

   if( buffer_opts) // -A option given on command line
   {
      char *p;
      p = strstr( buffer_opts, "b="); if( p) buff_size = atoi( p+2);
      p = strstr( buffer_opts, "p="); if( p) frag_size = atoi( p+2);
      p = strstr( buffer_opts, "n="); if( p) nread = atoi( p+2);
   }

   // If -A hasn't given a fragment size, set to somewhere near the nominal
   // period.
   if( !frag_size) frag_size = sample_rate * NOMINAL_PERIOD;

   // Chose a fragment selector (power of 2) to give at least this size
   // http://manuals.opensound.com/developer/SNDCTL_DSP_SETFRAGMENT.html
   int frag_selector = 4;
   while( (1 << frag_selector) < frag_size) frag_selector++;
   VT_report( 2, "fragment size %d requested, frag selector %d",
                    frag_size, frag_selector);

   frag_size = 1 << frag_selector; 

   // If -A doesn't give buffer size, set it to 32 fragments.
   int buff_selector = 32;
   if( buff_size) buff_selector = buff_size/frag_size;
   if( buff_selector < 2) buff_selector = 2;  // OSS minimum
   VT_report( 2, "buffer size %d bytes, fragment size %d bytes",
               buff_selector * frag_size, frag_size);

   int frag = (buff_selector << 16) | frag_selector;
   if (ioctl( capture_handle, SNDCTL_DSP_SETFRAGMENT, &frag))
      VT_report( 0, "cannot set buffer/fragment size");

//   if( ioctl( capture_handle, SNDCTL_DSP_RESET, NULL) < 0)
//      VT_bailout( "cannot reset input device");

   xchans = chans;
   if( ioctl( capture_handle, SNDCTL_DSP_CHANNELS, &xchans) < 0 ||
       xchans != chans) VT_bailout( "cannot set channels on input device");

   if( ioctl( capture_handle, SNDCTL_DSP_GETFMTS, &format) < 0)
      VT_bailout( "cannot get formats from input device");

   VT_report( 2, "formats available: %08X", format);
   if( (format & req_format) == 0)
   {
      VT_report( 0, "available dsp modes: %08X", format);
      VT_bailout( "unable to set %d bit dsp mode", 8 * bytes);
   }
   format = req_format;
   if( ioctl( capture_handle, SNDCTL_DSP_SETFMT, &format) < 0)
      VT_bailout( "cannot set dsp format on %s", device);

//   if( ioctl( capture_handle, SNDCTL_DSP_GETBLKSIZE, &blksize) < 0)
//      VT_bailout( "cannot get block size from input device");
//   VT_report( 2, "dsp block size: %d", blksize);

   VT_report( 1, "requesting rate: %d", sample_rate);
   if( ioctl( capture_handle, SNDCTL_DSP_SPEED, &sample_rate) < 0)
      VT_bailout( "cannot set sample rate of input device");

   VT_report( 1, "actual rate set: %d samples/sec", sample_rate);
   VT_report( 1, "soundcard channels: %d  bits: %d", chans, 8 * bytes);

   // Set the read size to one fragment, if not given by -A n= option
   if( !nread) nread = frag_size;
   VT_report( 2, "nread: %d", nread);
}

static int read_soundcard( char *buf)
{
   int ne;
   int nr = nread * chans * bytes;  // Number of bytes to read
   
   while( (ne = read( capture_handle, buf, nr)) < 0)
   {
      if( !ne || errno == -ENOENT || errno == -EAGAIN || errno == 0) 
      {  
         sched_yield();
         continue;
      }

      state = STATE_RESET;
      VT_report( -1, "soundcard read failed: %s", strerror( errno));
      usleep( 1000000); 
   }

   return ne / (chans * bytes);   // Return count of frames read
}

#endif // OSS

///////////////////////////////////////////////////////////////////////////////
//  ALSA Soundcard Interface                                                 //
///////////////////////////////////////////////////////////////////////////////

#if ALSA

#define soundsystem "ALSA"
static snd_pcm_t *capture_handle;

static void setup_input_stream( void)
{
   int err;
   snd_pcm_hw_params_t *hw_params;

   char alsa_device[100];

   if( !strncmp( device, "usb:", 4) ||
       !strncmp( device, "pci:", 4))
   {
      VT_report( 1, "waiting for bus device: %s", device);
      while( !lookup_device( device, alsa_device)) usleep( 1000000);
   }
   else strcpy( alsa_device, device);

   VT_report( 1, "using ALSA device %s", alsa_device);

   // Open the card, allocate and initialise hw_params structure
   if( (err = snd_pcm_open( &capture_handle, alsa_device, 
                            SND_PCM_STREAM_CAPTURE, 0)) < 0) 
      VT_bailout( "cannot open audio device %s (%s)",
         alsa_device, snd_strerror( err));

   if( (err = snd_pcm_hw_params_malloc( &hw_params)) < 0 ||
       (err = snd_pcm_hw_params_any( capture_handle, hw_params)) < 0)
      VT_bailout( "cannot init hardware params struct (%s)",
         snd_strerror( err));

   // Report some info about the card/driver capability
   unsigned int rate_min, rate_max;
   snd_pcm_hw_params_get_rate_min( hw_params, &rate_min, 0);
   snd_pcm_hw_params_get_rate_max( hw_params, &rate_max, 0);
   VT_report( 1, "rate min %d max %d", rate_min, rate_max);

   // Setup the access mode and sample format
   if( (err = snd_pcm_hw_params_set_access( capture_handle,
              hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
      VT_bailout("cannot set access type (%s)", snd_strerror(err));

   snd_pcm_format_t req_format = 0;
   switch( bytes)
   {
      case 1: req_format = SND_PCM_FORMAT_U8;       break;
      case 2: req_format = SND_PCM_FORMAT_S16_LE;   break;
      case 3: req_format = SND_PCM_FORMAT_S24_LE;   break;
      case 4: req_format = SND_PCM_FORMAT_S32_LE;   break;
   }

   if ((err = snd_pcm_hw_params_set_format(
        capture_handle, hw_params, req_format)) < 0)
   {
      // Some 24 bit soundcards use 3 bytes instead of 4
      if( req_format != SND_PCM_FORMAT_S24_LE)
         VT_bailout( "cannot set sample format (%s)\n", snd_strerror(err));
      
      req_format = SND_PCM_FORMAT_S24_3LE;
      if ((err = snd_pcm_hw_params_set_format(
        capture_handle, hw_params, req_format)) < 0)
         VT_bailout( "cannot set sample format (%s)\n", snd_strerror(err));
      three_byte_format = 1;
      VT_report( 1, "using 3 bytes per sample");
   }

   // Setup the sample rate and channel count
   if ((err = snd_pcm_hw_params_set_rate_near(
             capture_handle, hw_params, &sample_rate, 0)) < 0)
      VT_bailout( "cannot set sample rate (%s)", snd_strerror(err));

   VT_report( 1, "sample rate set: %d", sample_rate);

   if( (err = snd_pcm_hw_params_set_channels(
              capture_handle, hw_params, chans)) < 0)
      VT_bailout( "cannot set channel count (%s)", snd_strerror(err));

   snd_pcm_uframes_t period_size = 0;
   snd_pcm_uframes_t buffer_size = 0;

   if( buffer_opts) // -A option given on command line
   {
      char *p;
      p = strstr( buffer_opts, "b="); if( p) buffer_size = atoi( p+2);
      p = strstr( buffer_opts, "p="); if( p) period_size = atoi( p+2);
      p = strstr( buffer_opts, "n="); if( p) nread = atoi( p+2);
   }

   // If -A hasn't given a period size, then set to about the nominal period,
   // or close power of 2.
   if( !period_size)
   {
      int aim = sample_rate * NOMINAL_PERIOD;
      for( period_size = 32; aim/(double)period_size > 1.5; period_size *= 2);;
   }

   // If buffer or period sizes have been given, then override the ALSA
   // defaults
   if( period_size)
   {
      if( (err = snd_pcm_hw_params_set_period_size_near(
                  capture_handle, hw_params, &period_size, 0)) < 0)
         VT_bailout( "cannot set period size (%s)", snd_strerror( err));
   }

   if( buffer_size)
   {
      if( (err = snd_pcm_hw_params_set_buffer_size_near(
                   capture_handle, hw_params, &buffer_size)) < 0)
         VT_bailout( "cannot set buffer size (%s)", snd_strerror( err));
   
   }

   if( (err = snd_pcm_hw_params( capture_handle, hw_params)) < 0)
       VT_bailout( "cannot set parameters (%s)\n", snd_strerror( err));

   // Report more info, now that the parameters are set
   unsigned int count;
   snd_pcm_uframes_t frames;
   snd_pcm_hw_params_get_period_time( hw_params, &count, 0);
   snd_pcm_hw_params_get_period_size( hw_params, &frames, 0);
   VT_report( 1, "period size: %d frames, %d uS", (int) frames, count);

   snd_pcm_hw_params_get_buffer_time( hw_params, &count, 0);
   snd_pcm_hw_params_get_buffer_size( hw_params, &frames);
   VT_report( 1, "buffer size: %d frames, %d uS", (int) frames, count);

   snd_pcm_hw_params_get_periods( hw_params, &count, 0);
   VT_report( 1, "periods per buffer: %d", count);

   // Set the read size to one period, if not given by -A n= option
   if( !nread)
   {
      snd_pcm_hw_params_get_period_size( hw_params, &frames, 0);
      nread = frames;
   }
   VT_report( 2, "nread: %d", nread);

   snd_pcm_hw_params_free( hw_params);
   if ((err = snd_pcm_prepare( capture_handle)) < 0)
      VT_bailout( "cannot prepare soundcard (%s)", snd_strerror( err));

   snd_pcm_sw_params_t *sw_params;
   snd_pcm_sw_params_alloca( &sw_params);
   if( (err = snd_pcm_sw_params_current( capture_handle, sw_params)) < 0)
      VT_bailout( "cannot get swparams (%s)\n", snd_strerror( err));

   snd_pcm_sw_params_get_avail_min( sw_params, &frames);
   VT_report( 2, "avail min: %d", (int) frames);
}

static int read_soundcard( char *buf)
{
   int ne;

   // #XXX tried this but it doesn't seem to help with stabilising rate
   // measurement.
   //   snd_pcm_sframes_t delf;
   //   if( snd_pcm_delay( capture_handle, &delf) < 0) delf = 0;
   //   latency_frames = delf;
   latency_frames = 0;

   while( (ne = snd_pcm_readi( capture_handle, buf, nread)) <= 0)
   {
      if( ne == -EAGAIN) continue;

      state = STATE_RESET;
      VT_report( -1, "soundcard read failed: %s", snd_strerror( ne));
      usleep( 1000000); 
      if( snd_pcm_prepare( capture_handle) < 0)  // Try to restart capture
      {
         // Unable to restart.  Typically happens with USB devices.
         // Can be recovered by re-opening the device from scratch.
         // Avoid flooding logs if a hard fault by waiting 10 secs.

         VT_report( -1, "re-opening capture device");
         snd_pcm_close( capture_handle);
         snd_config_update_free_global();
         usleep( 5 * 1000000);
         setup_input_stream();
      }
   }

   return ne;   // Number of frames read
}

#endif // ALSA

///////////////////////////////////////////////////////////////////////////////
//  Main Loop                                                                //
///////////////////////////////////////////////////////////////////////////////

//
//  Used when -d- option is given.  Replaces the soundcard read.
// 
static int read_stdin( char *buf)
{
   int ne;
   int nr = nread * chans * bytes;  // Number of bytes to read

   latency_frames = 0;

   if( (ne = read( 0, buf, nr)) <= 0)
   {
      VT_report( 1, "stdin read failed: %s", strerror( errno));
      return 0;
   }

   if( ne % (bytes * chans)) VT_bailout( "stdin odd read");
   ne = ne / (bytes * chans);
   return ne;   // Number of frames read
}

static inline void commit_frame( double *frame)
{
   if( state != STATE_RUN) return; // Not yet in running state - discard output

   if( !vtfile->nfb)  // At start of an output block?
   {
      timebase  = timestamp_add( timebase, -tadj);
      VT_set_timebase( vtfile,
          timestamp_add( timebase, (nout - ntb)/(srcal * sample_rate)), srcal);
   }

   VT_insert_frame( vtfile, frame);
   nout++;
}

static void run( void)
{
   char *buff = VT_malloc( nread * chans * bytes);
   double *frame = VT_malloc( sizeof( double) * chans);

   while( 1) 
   {
      int q, i, chan;

      if( !strcmp( device, "-")) q = read_stdin( buff);
      else q = read_soundcard( buff);

      if( !q) break;    // stdin has ended
      timestamping( q);

      //
      //  Unpack the input buffer and scale to -1..+1
      //
      if( bytes == 1)
      {
         unsigned char *dp = (unsigned char *) buff;
         double f, scale = gain / 128;

         for( i=0; i<q; i++)
         {
            for( chan=0; chan < chans; chan++)
            {
               f = *dp++;
               frame[chan] = scale * (f - 127);
            }
            commit_frame( frame);
         }
      }
      else
      if( bytes == 2)
      {
         short *dp = (short *) buff;
         double f, scale = gain/INT16_MAX;

         for( i=0; i<q; i++)
         {
            for( chan=0; chan < chans; chan++)
            {
               f = *dp++;
               frame[chan] = scale * f;
            }
            commit_frame( frame);
         }
      }
      else
      if( bytes == 3 && three_byte_format)
      {
         unsigned char *dp = (unsigned char *) buff;
         double f, scale = gain/INT32_MAX;

         for( i=0; i<q; i++)
         {
            for( chan=0; chan < chans; chan++)
            {
               // Put the 3 bytes into the upper 3 of a 32 bit word
               uint32_t t = (dp[0] << 8) |
                            (dp[1] << 16) |
                            (dp[2] << 24);
               f = * (int32_t *) &t;
               dp += 3;
               frame[chan] = scale * f;
            }
            commit_frame( frame);
         }
      }
      else
      if( bytes == 3)   // 4 byte format
      {
         // Uses 32-bit word, the three low bytes 
         int *dp = (int *) buff;
         double f, scale = gain / (INT32_MAX >> 8);

         for( i=0; i<q; i++)
         {
            for( chan=0; chan < chans; chan++)
            {
               f = *dp++;
               frame[chan] = scale * f;
            }
            commit_frame( frame);
         }
      }
      else
      if( bytes == 4)
      {
         int *dp = (int *) buff;
         double f, scale = gain/INT32_MAX;

         for( i=0; i<q; i++)
         {
            for( chan=0; chan < chans; chan++)
            {
               f = *dp++;
               frame[chan] = scale * f;
            }
            commit_frame( frame);
         }
      }
   }

   VT_release( vtfile);
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

// Set scheduling priority to the minimum SCHED_FIFO value.
static void set_scheduling( void)
{
   #ifndef HAVE_SCHED_SETSCHEDULER
      int pri = -15;
      if( setpriority( PRIO_PROCESS, getpid(), pri))
      {
         VT_report( 0, "cannot set scheduling priority: %s",
                           strerror( errno));
         return;
      }
   #else
      int pri = sched_get_priority_max( SCHED_FIFO);

      struct sched_param pa;
      pa.sched_priority = pri;
      if( sched_setscheduler( 0, SCHED_FIFO, &pa))
      {
         VT_report( 0, "cannot set scheduling priority: %s",
                          strerror( errno));
         return;
      }
   #endif

   VT_report( 1, "using SCHED_FIFO priority %d", pri);

   if( mlockall( MCL_CURRENT | MCL_FUTURE) < 0)
      VT_report( 0, "unable to lock memory: %s", strerror( errno));
}

static void usage( void)
{
   fprintf( stderr,
       "usage:    vtcard [options] buffer_name\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify log file\n"
       "  -c chans  Channels\n"
       "  -d device Device\n"
       "            use -d- to read stdin\n"
       "            use -dq to list device bus locations\n"
       "  -b bits   Bits per sample\n"
       "  -r rate   Sample rate\n"
       "  -g gain   Gain\n"
       "  -u        No sample rate tracking\n"
       "  -T stamp  Start time when using -d- or -u\n"
       "            (default is to use system clock)\n"
       "  -A opts   Specify buffering options\n"
       "            -A p=psize,b=bsize\n"
       "            psize = period (ALSA) or fragment (OSS) size, bytes\n"
       "            bsize = buffer size (bytes)\n"
       "\n"
     );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtcard");

   int background = 0;
   int bits = 16;

   while( 1)
   {
      int c = getopt( argc, argv, "vBc:d:b:r:g:T:A:L:u?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'c') chans = atoi( optarg);
      else 
      if( c == 'd') device = strdup( optarg);
      else
      if( c == 'b') bits = atoi( optarg);
      else
      if( c == 'r')
      {
         sample_rate = atoi( optarg);
         srcal = atof( optarg) / sample_rate;
      }
      else
      if( c == 'g') gain = atof( optarg);
      else
      if( c == 'T') Tfalse = VT_parse_timestamp( optarg);
      else
      if( c == 'A') buffer_opts = strdup( optarg);
      else
      if( c == 'u') UFLAG = TRUE;
      else
      if( c == -1) break;
      else 
         usage();
   }

   if( !strcmp( device, "q"))
   {
      list_devices();
      exit( 0);
   }

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   if( chans < 0) VT_bailout( "invalid number of channels: %d", chans);
   if( sample_rate <= 0) VT_bailout( "invalid or missing sample rate, needs -r");

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDOUT : 0;
      VT_daemonise( flags);
   }

   bytes = bits/8;
   if( bits % 8 != 0 || bytes < 1 || bytes > 4)
      VT_bailout( "invalid data bits: %d", bits); 

   VT_report( 1, "buffer name: [%s]", bname);

   if( strcmp( device, "-")) setup_input_stream();
   else nread = 1024;

   VT_report( 1, "channels: %d  bytes: %d", chans, bytes);

   vtfile = VT_open_output( bname, chans, 1, sample_rate);
   if( !vtfile) VT_bailout( "cannot create buffer: %s", VT_error);

   if( strcmp( device, "-")) set_scheduling();

   state = STATE_RESET;
   run();
   return 0;
}

