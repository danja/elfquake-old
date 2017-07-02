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

static int sample_rate;
static int frame_rate = 10;
static int CFLAG = 0;
static int do_TD = 0;
static int do_FD = 0;
static char *vmode = "tf";
static int FW = 0;

static char *FFMPEG_container = "avi";
static char *FFMPEG_vcodec = "mpeg4";
static char *FFMPEG_acodec = "libmp3lame";
static int FFMPEG_vbr = 100;   // Video bitrate, kbps
static int FFMPEG_abr = 64;    // Audio bitrate, kbps
static char *inname = NULL, *outname = NULL;

static int PSIZE = 224;    // width and height of square display, pixels
static double video_gain = 1.0;    
static double audio_gain = 1.0;    

static double samples_per_frame;
static double *imgt;    // Time domain image buffer
static double *imgf;    // Frequency domain image buffer
static int *bmask;
static double bufsize = 10;   // Size of buffer before ffmpeg, seconds
#define IMGT(A,B) imgt[(B)*PSIZE + (A)]
#define IMGF(A,B) imgf[(B)*PSIZE + (A)]
#define BMASK(A,B) bmask[(B)*PSIZE + (A)]

static FILE *pf_video, *pf_audio;
static char *polarspec = "90,0";
static double sample_cnt;
static int fso;
static fftw_plan plan_pft1;
static fftw_plan plan_pft2;
static fftw_plan plan_pft3;
static int pft_width;
static double *pft_in1;
static double *pft_in2;
static double *pft_in3;
static fftw_complex *pft_out1;
static fftw_complex *pft_out2;
static fftw_complex *pft_out3;
static int pft_cnt = 0;
static double pft_df;
static double pft_min = 0;
static double pft_max = 0;

static double *pft_window = NULL;
static double polar1_align = 0;
static double polar2_align = 0;
static int polar1_ch = -1;
static int polar2_ch = -1;
static int polar3_ch = -1;

#define AM_MONO 0
#define AM_STEREO 1
static int amode = AM_MONO;

struct CHUNK
{
   char *data;
   int len;
   int out;
   struct CHUNK *next;
};

static struct QUEUE
{
   struct CHUNK *head;
   struct CHUNK *tail;
   FILE *fh;
   int h;
   int size;
   int level;
} vidq, audq;

static void queue_init( struct QUEUE *q, FILE *fh, int size)
{
   q->head = q->tail = NULL;
   q->fh = fh;
   q->h = fileno( q->fh);
   if( fcntl( q->h, F_SETFL, O_NONBLOCK) < 0)
      VT_bailout( "cannot set NONBLOCK");
   q->level = 0;
   q->size = size;
}

static void queue_drain( struct QUEUE *q)
{
   struct CHUNK *ch;

   while( (ch = q->head) != NULL)
   {
      int e;

      if((e = write( q->h, ch->data + ch->out, ch->len - ch->out)) > 0)
      {
         ch->out += e;
         q->level -= e;
         if( ch->out == ch->len)
         {
            free( ch->data);
            q->head = ch->next;
            free( ch);
         }
      }
      else
      {
         if( e == 0 || errno == EAGAIN) return;
         if( errno != EINTR)
            VT_bailout( "queue_write failed %s", strerror( errno));
      }
   }
}

static void queue_add( struct QUEUE *q, char *data, int len)
{
   struct CHUNK *ch = VT_malloc( sizeof( struct CHUNK));
   
   ch->data = data;
   ch->len = len;
   ch->out = 0;
   ch->next = NULL;

   if( !q->head)
      q->head = q->tail = ch;
   else
   {
      q->tail->next = ch;
      q->tail = ch;
   }

   q->level += len;

   while( 1)
   { 
      queue_drain( &audq);
      queue_drain( &vidq);

      if( audq.level > audq.size ||
          vidq.level > vidq.size) sched_yield();
      else break;
   }
}

static void queue_write( struct QUEUE *q, char *data, int len)
{
   queue_add( q, data, len);
}

static void initialise( void)
{
   samples_per_frame = sample_rate/(double) frame_rate;
   sample_cnt = samples_per_frame;

//   pft_width = sample_rate/12;
pft_width = PSIZE;
   pft_in1 = malloc( pft_width * sizeof( double));
   pft_in2 = malloc( pft_width * sizeof( double));
   pft_in3 = malloc( pft_width * sizeof( double));
   pft_out1 = malloc( pft_width * sizeof( fftw_complex));
   pft_out2 = malloc( pft_width * sizeof( fftw_complex));
   pft_out3 = malloc( pft_width * sizeof( fftw_complex));

   plan_pft1 = fftw_plan_dft_r2c_1d( pft_width, pft_in1, pft_out1,
                           FFTW_ESTIMATE | FFTW_DESTROY_INPUT);
   plan_pft2 = fftw_plan_dft_r2c_1d( pft_width, pft_in2, pft_out2,
                           FFTW_ESTIMATE | FFTW_DESTROY_INPUT);
   plan_pft3 = fftw_plan_dft_r2c_1d( pft_width, pft_in3, pft_out3,
                           FFTW_ESTIMATE | FFTW_DESTROY_INPUT);

   pft_df = sample_rate/(double) pft_width;
   if( pft_max == 0 || pft_max > sample_rate/2.0) pft_max = sample_rate/2.0;

   pft_window = VT_malloc( pft_width * sizeof( double));
   int i;
   for( i=0; i<pft_width; i++)
   {
//pft_window[i] = 1;
//      pft_window[i] = sin( i/(double)pft_width * M_PI);  // Rect
     // Hann
//      pft_window[i] = 0.5 * ( 1 - cos( 2 * M_PI * i/(double)(pft_width-1)));
     // Nuttall
        pft_window[i] = 0.355768
                        - 0.487396 * cos(2*M_PI*i/(double)(pft_width-1))
                        + 0.144232 * cos(4*M_PI*i/(double)(pft_width-1))
                        - 0.012604 * cos(6*M_PI*i/(double)(pft_width-1));
   }

   //
   //  Set up a named pipe for the audio stream
   //
   char audio_fifo[100];
   sprintf( audio_fifo, "/tmp/pfpipe%d.pcm", getpid());
   unlink( audio_fifo);
   if( mknod( audio_fifo, S_IFIFO | 0666, 0) < 0)
      VT_bailout( "cannot create fifo %s, %s", audio_fifo, strerror( errno));

   char *f_option = "";
   if( strncmp( outname, "http:", 5)) 
      if( asprintf( &f_option, "-f %s", FFMPEG_container) < 0)
         VT_bailout( "out of memory");

   //
   //  Construct ffmpeg command
   //
   int achans = amode == AM_STEREO ? 2 : 1;  // Number of audio channels
   char ffmpeg_cmd[500];

   sprintf( ffmpeg_cmd, 
             "ffmpeg -y "
             "-f s16le -ar %d -ac %d -i %s -i - "
             "-vcodec %s -b:v %dk -r %d "
             "-ab %dk -ac %d -acodec %s %s %s", 
      sample_rate, achans, audio_fifo,
      FFMPEG_vcodec,
      FFMPEG_vbr,
      frame_rate, FFMPEG_abr, achans, FFMPEG_acodec,
      f_option, outname);

   //
   //  Video stream goes to stdin of ffmpeg
   //
   if( (pf_video = popen( ffmpeg_cmd, "w")) == NULL)
      VT_bailout( "cannot open A/V output stream: %s", strerror( errno));

   if( do_TD) FW += PSIZE;
   if( do_FD) FW += PSIZE;

   fso = 6 + (FW * PSIZE * 3)/2;   // Total size of frame and frame hdr
   queue_init( &vidq, pf_video, fso * (int)(frame_rate * bufsize));

   if( (pf_audio = fopen( audio_fifo, "w")) == NULL)
      VT_bailout( "cannot open audio output pipe");

   queue_init( &audq, pf_audio, sample_rate * 2 * bufsize);

   // Send YUV422 header and initialise a frame buffer

   #define PALHDR "YUV4MPEG2 W%d H%d F%d:1 Ip A0:0\n"

   char *buff = VT_malloc( 100);
   sprintf( buff, PALHDR, FW, PSIZE, frame_rate);
   queue_write( &vidq, buff, strlen( buff));

   video_gain *= PSIZE/1e7;

   int x, y;
   for( y=0; y<PSIZE; y++)
      for( x=0; x<PSIZE; x++)
      {
         int r2 = (x-PSIZE/2) * (x-PSIZE/2) + (y-PSIZE/2)*(y-PSIZE/2);
         BMASK( x, y) = r2 >= PSIZE/2*PSIZE/2 ? 1 : 0;
      }
}

static void inline rgb2yuv( double r, double g, double b,
                            double *Y, double *Cr, double *Cb)
{
   *Y  = 0.257 * r + 0.504 * g + 0.098 * b + 16;
   *Cr = 0.439 * r - 0.368 * g - 0.071 * b + 128;
   *Cb = -0.148 * r - 0.291 * g + 0.439 * b + 128;
}

static void draw_line( int x0, int y0, int x1, int y1)
{
   int ax = x1 - x0; if( ax < 0) ax = -ax;
   int ay = y1 - y0; if( ay < 0) ay = -ay;
   int steep = ay > ax ? 1 : 0;

   int t;
   if( steep)
   {
      t = x0; x0 = y0; y0 = t;
      t = x1; x1 = y1; y1 = t;
   }
   if( x0 > x1)
   {
      t = x0; x0 = x1; x1 = t;
      t = y0; y0 = y1; y1 = t;
   }

   int deltax = x1 - x0;
   int deltay = y1 - y0; if( deltay < 0) deltay = -deltay;
   int error = deltax / 2;
   int y = y0;
   int ystep = y0 < y1 ? 1 : -1;
   int x;
   for( x = x0 ; x <= x1; x++)
   {
      if( steep) IMGT( y, x) = 1;
      else IMGT( x, y) = 1;
      error = error - deltay;
      if( error < 0)
      {
          y = y + ystep;
          error = error + deltax;
      }
   }
}

static inline double power( complex double c)
{
   return creal( c) * creal( c) + cimag( c) * cimag( c);
}

//
// Insert a sample pair into the FT buffer and run the FT if necessary.
//
static void add_to_pft( double v1, double v2, double ve)
{
   pft_in1[pft_cnt] = v1 * 32760 * pft_window[pft_cnt];
   pft_in2[pft_cnt] = v2 * 32760 * pft_window[pft_cnt];
   if( polar3_ch >= 0) pft_in3[pft_cnt] = ve * 32760 * pft_window[pft_cnt];

   if( ++pft_cnt == pft_width)
   {
      pft_cnt = 0;
      fftw_execute( plan_pft1);
      fftw_execute( plan_pft2);
      if( polar3_ch >= 0) fftw_execute( plan_pft3);

      int start_bin = pft_min / pft_df;
      if( start_bin < 1) start_bin = 1;
      int end_bin = pft_max / pft_df;
      if( end_bin >= pft_width/2) end_bin = pft_width/2 - 1;

      int i;
      for( i=start_bin; i<end_bin; i++)
      {
         complex double ew = pft_out1[i];
         complex double ns = pft_out2[i];
         complex double vr = pft_out3[i];

         double mag_ew = cabs( ew);
         double mag_ns = cabs( ns);
         double pow_ew = mag_ew * mag_ew; // Power, E/W signal
         double pow_ns = mag_ns * mag_ns; // Power, N/S signal

         double a = atan2( cimag( ns) * creal( ew) - creal( ns) * cimag( ew),
                           creal( ns) * creal( ew) + cimag( ns) * cimag( ew));

         // Calculate bearing, taking proper account of phase angle
         double bsin = 2 * mag_ew * mag_ns * cos( a);
         double bcos = pow_ns - pow_ew; 
         double bearing180 = atan2( bsin, bcos)/2;

         double r = (i - start_bin)/(double)(end_bin - start_bin + 1);

         double g = (pow_ew + pow_ns) * pow( r, 2.5) * 2;
         if( polar3_ch < 0)   // Render mod 180
         {
            // Add total power to diagonally opposite points
            int yd = PSIZE/2 * cos( bearing180) * r;
            int xd = PSIZE/2 * sin( bearing180) * r;
            IMGF( PSIZE/2-xd, PSIZE/2-yd) += g;
            IMGF( PSIZE/2+xd, PSIZE/2+yd) += g;
         }
         else                 // Render mod 360
         {
            // Synthesise loop aligned on signal
            complex double or = ew * sin( bearing180) +
                                ns * cos( bearing180);
          
            // Phase angle between E and H 
            double pha = 
                 atan2( cimag( or) * creal( vr) - creal( or) * cimag( vr),
                        creal( or) * creal( vr) + cimag( or) * cimag( vr));
   
            double bearing360 = bearing180;
            if( pha < -M_PI/2 || pha > M_PI/2)  bearing360 += M_PI;
            int yd = PSIZE/2 * cos( bearing360) * r;
            int xd = PSIZE/2 * sin( bearing360) * r;

            IMGF( PSIZE/2+xd, PSIZE/2+yd) += g;
         }
      }
   }
}

//
//  Add a sample pair to the time domain display.  Each pair is rendered as
//  a line through the center.
//
static void add_to_video( double v1, double v2, double ve)
{
   if( polar3_ch >= 0 && ve < 0) return;

   // Clip gracefully - preserving bearing
   double av1 = v1 > 0 ? v1 : -v1;
   double av2 = v2 > 0 ? v2 : -v2;
   if( av1 > 1) { v1 /= av1 ; v2 /= av1; }
   if( av2 > 1) { v1 /= av2 ; v2 /= av2; }

   int e1 = (int)(PSIZE * (1 + v1)/2);
   int e2 = (int)(PSIZE * (1 + v2)/2);

   if( e1 < 0) e1 = 0 ; if( e1 >= PSIZE) e1 = PSIZE - 1;
   if( e2 < 0) e2 = 0 ; if( e2 >= PSIZE) e2 = PSIZE - 1;
//   draw_line( PSIZE - 1 - e1, PSIZE - 1 - e2, e1, e2);
   draw_line( PSIZE/2, PSIZE/2, e1, e2);
}

static void add_to_audio( double v1, double v2, double ve)
{
   static int16_t *pabuf;
   static int pabufn = 0;

   if( !pabufn) pabuf = VT_malloc( 2000 * 2);

   void audio_sample( double val)
   {
      pabuf[pabufn++] = val * 16380 * audio_gain;

      if( pabufn == 2000)
      {
         queue_write( &audq, (char *) pabuf, 2 * pabufn);
         pabufn = 0;
      }
   }

   if( amode == AM_MONO)
   {
      if( polar3_ch < 0) audio_sample( v1 + v2);
      else audio_sample( ve);
   }
   else
   {
      audio_sample( v1);
      audio_sample( v2);
   }
}

//
//  Apply a phosphor decay.
//

static void frame_decay()
{
   int x, y;

   double at = pow( 0.006, 1.0/frame_rate);
   double af = pow( 0.001, 1.0/frame_rate);

   for( x=0; x<PSIZE; x++)
      for( y=0; y<PSIZE; y++)
   {
      IMGT( x, y) *= at;
      IMGF( x, y) *= af;
   }
}

static void write_frame( void)
{
   int x, y;
   // Overall frame width, luminance
   int XTD = 0;
   int XFD = 0;
   if( do_TD) XTD = 0;
   if( do_FD && do_TD) XFD += PSIZE;

   int FH = PSIZE;       // Overall frame height, luminance
   int CW = FW/2;       // chroma width
   int CH = PSIZE/2;     // chroma height

   uint8_t *frame_buf = VT_malloc( fso);
   uint8_t *frame_lum = frame_buf + 6;   // Frame start, luminance
   uint8_t *frame_chr = frame_buf + 6 + FW*FH;   // Frame start, chroma

   memcpy( frame_buf, "FRAME\n", 6);
   memset( frame_lum, CFLAG ? 50 : 0, FW*FH);
   memset( frame_chr, 127, CW*CH);
   memset( frame_chr + CW*CH, 127, CW*CH);

   if( do_TD)
      for( y=0; y<PSIZE; y++)
         for( x=0; x<PSIZE; x++)
            if( !BMASK( x, y))
               frame_lum[y*FW + XTD + x] = 
                 (uint8_t) (IMGT( x, PSIZE-1-y) * 255);

   if( do_FD)
      for( y=0; y<PSIZE; y++)
         for( x=0; x<PSIZE; x++)
         {
            if( BMASK( x, y)) continue;
   
            double g = video_gain * IMGF( x, PSIZE-1-y);
            if( g > 255) g = 255;
   
            frame_lum[y*FW + XFD + x] = (uint8_t) g;
         }

   queue_write( &vidq, (char *) frame_buf, fso);
}

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtpolar [options] input output\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify logfile\n"
       "  -c        Paint background\n"
       "  -b        Buffer size\n"
       "  -r        Frame rate (default 10)\n"
       "  -s size   Image diameter, pixels\n"
       "\n"
       "  -m mode   Display mode,\n"
       "            -mf   spectrogram\n"
       "            -mt   time domain (vectorscope)\n"
       "            -mtf  both\n"
       "\n"
       "  -am       Mono audio (default)\n"
       "  -as       Stereo audio\n"
       "\n"
       "  -k from,to    Specify frequency range\n"
       "  -p polarspec  Specify input channel assignments\n"
       "\n"
       "  -ga=gain      Specify audio gain\n"
       "  -gv=gain      Specify video gain\n"      
       "  -f options    Specify ffmpeg options\n"
       "                options is a comma separated list of parameters:\n"
       "                  format=container_format, (default avi)\n"
       "                  vcodec=video_codec, (default mpeg4)\n"
       "                  acodec=audio_codec, (default libmp3lame)\n"
       "                  vbr=video_bitrate, (default 100)\n"
       "                  abr=audio_bitrate, (default 64)\n"
     );
   exit( 1);

}

static void parse_ffmpeg_options( char *s)
{
   while( s && *s)
   {
      char *p = strchr( s, ',');
      if( p) *p++ = 0;

      if( !strncmp( s, "format=", 7))
         FFMPEG_container = strdup( s+7);
      else
      if( !strncmp( s, "vcodec=", 7)) FFMPEG_vcodec = strdup( s+7);
      else
      if( !strncmp( s, "acodec=", 7)) FFMPEG_acodec = strdup( s+7);
      else
      if( !strncmp( s, "vbr=", 4)) FFMPEG_vbr = atoi( s+4);
      else
      if( !strncmp( s, "abr=", 4)) FFMPEG_abr = atoi( s+4);
      else
         VT_bailout( "unrecognised ffmpeg option: %s", s);

      s = p;
   }

   VT_report( 1, "ffmpeg: %s %s %s",
      FFMPEG_container, FFMPEG_vcodec, FFMPEG_acodec);
}

static void parse_pft_range( char *s)
{
   if( sscanf( s, "%lf,%lf", &pft_min, &pft_max) != 2)
      VT_bailout( "cannot parse -k argument [%s]", s);
}

static void parse_gain_options( char *s)
{
   while( s && *s)
   {
      char *p = strchr( s, ',');
      if( p) *p++ = 0;

      if( !strncmp( s, "v=", 2)) video_gain = atof( s+2);
      else
      if( !strncmp( s, "a=", 2)) audio_gain = atof( s+2);
      else
         VT_bailout( "unrecognised gain option: %s", s);

      s = p;
   }
}

#if 0   
static void parse_polarspec( int chans)
{
   int ch = 0;
   int n = 0;

   char *spec = strdup( polarspec);

   while( spec && *spec)
   {
      if( ch == chans)
         VT_bailout( "too many polar specifiers for %d channels", chans);

      char *p = strchr( spec, ',');
      if( p) *p++ = 0;

      if( *spec == 'n' || *spec == 'N') ch++;
      else
      if( *spec == 'e' || *spec == 'E')
      {
         if( polar3_ch >= 0)
            VT_bailout( "more than one E-field given to -p");
         polar3_ch = ch++;
      }
      else
      if( isdigit( spec[0]))
      {
         if( n++ == 2)
            VT_bailout( "only two loops allowed with -p");
         double a = atof( spec);
         if( a < 0 || a >= 360)
            VT_bailout( "invalid -p argument [%s]", spec);

         a *= M_PI/180;
         if( n == 1) { polar1_ch = ch; polar1_align = a; }
         if( n == 2) { polar2_ch = ch; polar2_align = a; }
         ch++;
      }
      else
         VT_bailout( "unrecognised polar specifier [%s]", spec);
      spec = p;
   }

   if( ch < chans)
      VT_bailout( "not enough polar specifiers for %d channels", chans);

   if( n < 2)
      VT_bailout( "must have two loops specified for polar");
}
#endif

int main(int argc, char *argv[])
{
   VT_init( "vtpolar");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBf:s:r:cb:m:k:p:g:L:a:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'c') CFLAG = 1;
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'b') bufsize = atof( optarg);
      else
      if( c == 's') PSIZE = atoi( optarg);
      else
      if( c == 'p') polarspec = strdup( optarg);
      else
      if( c == 'r') frame_rate = atoi( optarg);
      else
      if( c == 'k') parse_pft_range( optarg);
      else
      if( c == 'm') vmode = strdup( optarg);
      else
      if( c == 'f') parse_ffmpeg_options( optarg);
      else
      if( c == 'g') parse_gain_options( optarg);
      else
      if( c == 'a')
      {
         if( !strcmp( optarg, "s")) amode = AM_STEREO;
         else
         if( !strcmp( optarg, "m")) amode = AM_MONO;
         else
           VT_bailout( "invalid -a option");
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

   if( strchr( vmode, 'f')) do_FD = 1;
   if( strchr( vmode, 't')) do_TD = 1;

   imgt = VT_malloc( sizeof( double) * PSIZE * PSIZE);
   imgf = VT_malloc( sizeof( double) * PSIZE * PSIZE);
   bmask = VT_malloc( sizeof( int) * PSIZE * PSIZE);

   if( background)
   {
      int flags = inname[0] == '-' ? KEEP_STDIN : 0;
      if( outname[0] == '-') flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( inname);

   VTFILE *vtinfile;
   if( (vtinfile = VT_open_input( inname)) == NULL)
      VT_bailout( "cannot open input %s: %s", inname, VT_error);

   sample_rate = VT_get_sample_rate( vtinfile);

   VT_init_chanspec( chspec, vtinfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chspec->n, sample_rate);

   VT_parse_polarspec( chspec->n, polarspec,
                       &polar1_ch, &polar1_align,
                       &polar2_ch, &polar2_align,
                       &polar3_ch);

   VT_report( 1, "channel assignments:");
   VT_report( 1, "H: chan=%d bearing=%.1f",
                     polar1_ch+1, polar1_align * 180/M_PI);
   VT_report( 1, "H: chan=%d bearing=%.1f",
                     polar2_ch+1, polar2_align * 180/M_PI);
   if( polar3_ch < 0) 
      VT_report( 1, "E: not used");
   else
      VT_report( 1, "E: chan=%d", polar3_ch+1); 

   initialise();

   double cos1 = cos( polar1_align);
   double cos2 = cos( polar2_align);
   double sin1 = sin( polar1_align);
   double sin2 = sin( polar2_align);

   while( 1)
   {
      double *inframe = VT_get_frame( vtinfile);
      if( !inframe) break;
     
      double v1 = inframe[chspec->map[polar1_ch]]; 
      double v2 = inframe[chspec->map[polar2_ch]];
      double ve = polar3_ch >= 0 ? inframe[chspec->map[polar3_ch]] : 0;

      // Produce N/S and E/W signals
      double ns = cos1 * v1 + cos2 * v2;
      double ew = sin1 * v1 + sin2 * v2;
 
      add_to_pft( ew, ns, ve);
      add_to_video( ew, ns, ve);
      add_to_audio( v1, v2, ve);

      if( --sample_cnt < 1)
      {
         write_frame();
         frame_decay();
         sample_cnt += samples_per_frame;
      }
   }

   VT_report( 0, "end of input");
   return 0;
}

