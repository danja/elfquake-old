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
#include <png.h>

#define MAXSAMPLES 2000000        // Max number of input samples we will handle
static double *rdata = NULL;                    // Buffer to hold input samples
static int nrdata = 0;                        // Actual number of input samples

static int bins = 309;                                      // Set by -b option
static int step = 2;
static double gain = 1;
static int sample_rate;
static double mag_scale = 2.0;        // Magnification of the re-gridded output

static double aprune = 0.0;         // -a option: Amplitude pruning factor
static double rprune = 1e9;         // -r option: Range pruning factor
static double pprune = 1e9;         // -p option: MPD pruning factor
static int hcount = 0;

static int NFLAG = 0;    // -n option: produce non-reassigned spectrogram
static int EFLAG = 0;    // -e option: apply vertical EQ
static int DFLAG = 0;    // -d option value:  diagnostic option
static int KFLAG = 0;    // Experimental
static int UFLAG = 0;    // -u option: disable automatic brightness setting

static double *rs = NULL;

static int SH, SW;
static int XH, XW;

static char *hamming = "cosine";
static int fftwid;

static double srcal;
static timestamp Tstart;

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtrsgram [options] [input]\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "\n"
       "  -b bins   Number of frequency bins (integer, default 309)\n"
       "  -s steps  Overlap factor for FT frames (integer, default 2)\n"
       "  -m scale  Image magnification (default 2.0)\n"
       "  -g gain   \n"
       "  -a factor Amplitude pruning threshold\n"
       "  -r factor Range pruning threshold\n"
       "  -p factor Mixed partial derivative pruning\n"
       "  -n        Produce non-reassigned spectrogram\n"
       "  -h count  Use homomorphic vertical EQ\n"
       "  -e        Vertical EQ\n"
       "\n"
       "  -W window Select window function\n"
       "            -W cosine (default)\n"
       "            -W blackman\n"
       "            -W hamming\n"
       "            -W nuttall\n"
       "            -W hann\n"
       "            -W rect\n"
       "\n"
       "Output options:\n"
       "  -opng     png image output (default) \n"
       "  -oa       Reassigned points as ASCII data\n"
       "  -oag      Re-gridded reassigned points as ASCII grid data\n"
       "  -ox       X11 interactive display\n"
       "  -d1       Diagnostic, output window function\n"
     );
   exit( 1);
}

// Output formats
static int outformat = 0;

#define OF_PNG           0    // Default
#define OF_X11           1
#define OF_ASCII_POINTS  2
#define OF_ASCII_GRID    3

static void parse_format_options( char *s)
{
   if( !strcmp( s, "a")) outformat = OF_ASCII_POINTS;
   else
   if( !strcmp( s, "ag")) outformat = OF_ASCII_GRID;
   else
   if( !strcmp( s, "png")) outformat = OF_PNG;
   else
   if( !strcmp( s, "x")) outformat = OF_X11;
   else
      VT_bailout( "unrecognised output format option: [%s]", s);
}

struct RSPOINT
{
   int x, y;              // T,F coordinate in the STFT space
   double rx, ry;         // Reassignment offsets for T and F
   double A;              // Amplitude of the point
   double mpd;            // Mixed partial derivative
   int pv;                // Pixel value
   int mx, my;            // Cell coordinates in magnified image
} *rpoints = NULL;

static int nrp = 0;
static double rsmean = 0;

// A comparison function for qsort()

static int cmp_rpa( const void *p1, const void *p2)
{
   struct RSPOINT *rp1 = (struct RSPOINT *)p1;
   struct RSPOINT *rp2 = (struct RSPOINT *)p2;

   if( rp1->A < rp2->A) return -1;
   if( rp1->A > rp2->A) return 1;
   return 0;
}

///////////////////////////////////////////////////////////////////////////////
//  Window Functions                                                         //
///////////////////////////////////////////////////////////////////////////////

static double *hwindow = NULL;
static double *dwindow = NULL;

static void setup_hamming( void)
{
   hwindow = VT_malloc( sizeof( double) * fftwid);
   dwindow = VT_malloc( sizeof( double) * fftwid);

   int i;
   double N = fftwid - 1;

   if( !strcasecmp( hamming, "rect"))
   {
      for( i=0; i<fftwid; i++) hwindow[i] = 1;
   }
   else
   if( !strcasecmp( hamming, "cosine"))
   {
      for( i=0; i<fftwid; i++)
         hwindow[i] = sin( i/N * M_PI);
   }
   else
   if( !strcasecmp( hamming, "hann"))
   {
      for( i=0; i<fftwid; i++)
         hwindow[i] = sin( i/N * M_PI) * sin( i/N * M_PI);
   }
   else
   if( !strcasecmp( hamming, "blackman"))
   {
      double a0 = (1 - 0.16)/2;
      double a1 = 0.5;
      double a2 = 0.16/2;
      for( i=0; i<fftwid; i++)
         hwindow[i] = a0 - a1 * cos( i/N * 2 * M_PI)
                         + a2 * cos( i/N * 4 * M_PI);
   }
   else
   if( !strcasecmp( hamming, "hamming"))
   {
      for( i=0; i<fftwid; i++)
         hwindow[i] = 0.54 - 0.46 * cos( i/N * 2 * M_PI);
   }
   else
   if( !strcasecmp( hamming, "nuttall"))
   {
      double a0 = 0.355768, a1 = 0.487396, a2 = 0.144232, a3 = 0.012604;
      for( i=0; i<fftwid; i++)
         hwindow[i] = a0 - a1 * cos( i/N * 2 * M_PI)
                         + a2 * cos( i/N * 4 * M_PI)
                         - a3 * cos( i/N * 6 * M_PI);
   }
   else VT_bailout( "unknown window function: %s", hamming);

   //
   // Differentiate hwindow[] to produce dwindow[0..fftwid-1]
   //

   complex double *X = VT_malloc( sizeof( fftw_complex) * fftwid); 
   fftw_plan fp1 = fftw_plan_dft_r2c_1d( fftwid, hwindow, X, FFTW_ESTIMATE);
   fftw_execute( fp1);

   for( i=0; i<=fftwid/2; i++) X[i] *= I * i/(double) fftwid;
   fftw_plan fp2 = fftw_plan_dft_c2r_1d( fftwid, X, dwindow, FFTW_ESTIMATE);
   fftw_execute( fp2);

   if( DFLAG == 1)   // -d1 option: Output the window function and derivate
   {
      for( i=0; i<fftwid; i++)
         printf( "%.3e %.3e %.3e\n", i/N, hwindow[i], dwindow[i]);
      exit( 0);
   }
}

static inline double window_value( int i)
{
   return hwindow[i];
}

static inline double window_derivative( int i)
{
   return dwindow[i];
}

///////////////////////////////////////////////////////////////////////////////
//  X Display                                                                //
///////////////////////////////////////////////////////////////////////////////

#if USE_X11

#include <forms.h>

static FL_FORM *form;
static GC redGC, bgndGC, blackGC, greenGC;

#define BH 5                // Border height
#define BW 5                // Border width
#define CH 18               // Controls height
#define CW 120              // Controls width

static FL_OBJECT *utca,     // Absolute UT display
                 *utcr,     // Relative time display
                 *freq,     // Frequency display
                 *trms,     // RMS amplitude display
                 *mag_canvas;   // Magnified image

//  Array of GCs, one for each brightness level
#define MAXLEV 256
static GC plotGC[ MAXLEV];

static Display *xdisplay;

//
//  Display the magnified image
//

static void mag_draw( Window win, int cx, int cy)
{
   int bmin = 0;
   int x, y;

   for( y=0; y<CW; y++)
      for( x=0; x<CW; x++)
      {
         int mx = rint( cx + (x - CW/2.0)/3);
         int my = rint( cy + (y - CW/2.0)/3);
      
         double v = 0; 
         if( mx >= 0 && mx < XW &&
             my >= 0 && my <=XH) v = sqrt(rs[my*XW + mx]) * 2 * gain;

         int pval = bmin + v * MAXLEV;
         if( pval >= MAXLEV ) pval = MAXLEV-1;
         if( pval < bmin) pval = bmin;
   
         XDrawPoint( xdisplay, win, plotGC[pval], x, CW - y - 1);
      }

   XDrawLine( xdisplay, win, greenGC, CW/2, 0, CW/2, CW-1);
   XDrawLine( xdisplay, win, greenGC, 0, CW/2, CW-1, CW/2);
   XSync( xdisplay, 0);
}

static int mag_expose(  FL_OBJECT *ob, Window win, int w, int h,
                         XEvent *ev, void *d)
{
   return 0;
}

//
//  Report about the cell under the cursor, if there is one.
//

static void trace_report( int x, int y)
{
   int i;
   char temp[100];

   double sumsq = 0;

   for( i=0; i<nrp; i++)
   {
      struct RSPOINT *rp = rpoints + i;
      if( rp->mx != x || rp->my != y) continue;

      double T = (rp->x + rp->rx) * fftwid/(double) sample_rate/step;
      double F = (rp->y + rp->ry) * sample_rate/(double) fftwid;

      sprintf( temp, "%.6f", F);
      fl_set_object_label( freq, temp);

      sprintf( temp, "%.6f", T);
      fl_set_object_label( utcr, temp);

      VT_format_timestamp( temp, timestamp_add( Tstart, T));
      fl_set_object_label( utca, temp);

      VT_report( 1, "trace %4d %3d A %10.3e mpd %8.3f T %.6f",
         x, y, rp->A, rp->mpd, T);

      sumsq += rp->A * rp->A;
   }

   double rms = sqrt( sumsq);

   sprintf( temp, "%.3e", rms);
   fl_set_object_label( trms, temp);

   mag_draw(  FL_ObjWin( mag_canvas), x, y);
}

//
//  Display some or all of the main image.
//

static int trace_expose( FL_OBJECT *ob, Window win, int w, int h, 
                         XEvent *ev, void *d)
{
   XExposeEvent *e = (XExposeEvent *) ev;
   VT_report( 2, "expose: %d,%d %d %d", e->x, e->y, e->width, e->height);

   int bmin = 10;
   int x, y;

   for( y=0; y<XH; y++)
      for( x=0; x<XW; x++)
      {
         double v = sqrt(rs[y*XW + x]) * 2 * gain;

         int pval = bmin + v * MAXLEV;
         if( pval >= MAXLEV ) pval = MAXLEV-1;
         if( pval < bmin) pval = bmin;
   
         XDrawPoint( xdisplay, win, plotGC[pval], x, XH - y - 1);
      }

   XSync( xdisplay, 0);
   return 0;
}

//
//  Mouse button pressed, release.
//
static int trace_press( FL_OBJECT *ob, Window win, int w, int h, 
                        XEvent *ev, void *d)
{
   trace_report( ev->xbutton.x, XH - ev->xbutton.y - 1);
   return 0;
}

static int trace_release( FL_OBJECT *ob, Window win, int w, int h, 
                          XEvent *ev, void *d)
{
   return 0;
}

//
//  Mouse movement.
//

static int trace_motion( FL_OBJECT *ob, Window win, int w, int h, 
                         XEvent *ev, void *d)
{
   trace_report( ev->xmotion.x, XH - ev->xmotion.y - 1);
   return 0;
}

static void display( void)
{
   int argc = 1; char *argv[] = { "vtsgram" };
   fl_initialize( &argc, argv, "vtrsgram", 0, 0);

   xdisplay = fl_get_display();
   Window defwin = fl_default_window();

   redGC = XCreateGC( xdisplay, defwin, 0, 0);
   fl_set_foreground( redGC, FL_RED);

   bgndGC = XCreateGC( xdisplay, defwin, 0, 0);
   fl_set_foreground( bgndGC, FL_COL1);

   greenGC = XCreateGC( xdisplay, defwin, 0, 0);
   fl_set_foreground( greenGC, FL_GREEN);

   blackGC = XCreateGC( xdisplay, defwin, 0, 0);
   fl_set_foreground( blackGC, FL_BLACK);

   int screen = DefaultScreen( xdisplay);
   Colormap cmap = DefaultColormap( xdisplay, screen);
   int i;
   for( i=0; i<MAXLEV; i++)
   {
      plotGC[ i] = XCreateGC( xdisplay, defwin, 0, 0);

      XColor t;
      t.red   = 255 *i/(double) MAXLEV;    t.red <<= 8;
      t.green = 255 *i/(double) MAXLEV;    t.green <<= 8;
      t.blue  = 255 *i/(double) MAXLEV;    t.blue <<= 8;
      if( !XAllocColor( xdisplay, cmap, &t))
         VT_bailout( "cannot alloc col %d", i);
      XSetForeground( xdisplay, plotGC[ i], t.pixel);
   }

   //  Set up overall width and height
   int owidth = BW + CW + BW + XW + BW;
   int oheight = XH + 2*BH;
   if( oheight < 14*CH + CW + BH) oheight = 14*CH + CW + BH;

   //
   // Create the display form.
   //

   form = fl_bgn_form( FL_NO_BOX, owidth, oheight);
   fl_add_box( FL_UP_BOX, 0, 0, owidth, oheight, "");

   FL_OBJECT *dp = fl_add_canvas( FL_NORMAL_CANVAS, CW + 2*BW, BH, XW, XH, "");
   fl_add_canvas_handler( dp, Expose, trace_expose, 0);
   fl_add_canvas_handler( dp, ButtonPress, trace_press, 0);
   fl_add_canvas_handler( dp, ButtonRelease, trace_release, 0);
   fl_add_canvas_handler( dp, MotionNotify, trace_motion, 0);

   FL_OBJECT *cgroup = fl_bgn_group();

   utca = fl_add_box( FL_DOWN_BOX, BW, 1 * CH + BH, CW, CH, "");
   fl_set_object_lalign( utca,FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
   fl_set_object_color( utca, FL_WHITE, FL_BLUE); 
   fl_add_text( FL_NORMAL_TEXT, BW, 2 * CH + BH, CW, CH, "Abs Timestamp");

   utcr = fl_add_box( FL_DOWN_BOX, BW, 4*CH + BH, CW, CH, "");
   fl_set_object_lalign( utcr,FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
   fl_set_object_color( utcr, FL_WHITE, FL_BLUE); 
   fl_add_text( FL_NORMAL_TEXT, BW, 5 * CH + BH, CW, CH, "Rel Timestamp");

   freq = fl_add_box( FL_DOWN_BOX, BW, 7*CH + BH, CW, CH, "");
   fl_set_object_lalign( freq,FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
   fl_set_object_color( freq, FL_WHITE, FL_BLUE); 
   fl_add_text( FL_NORMAL_TEXT, BW, 8 * CH + BH, CW, CH, "Frequency");

   mag_canvas = fl_add_canvas( FL_NORMAL_CANVAS, BW, 10*CH+BH, CW, CW, "");
   fl_add_canvas_handler( mag_canvas, Expose, mag_expose, 0);

   trms = fl_add_box( FL_DOWN_BOX, BW, 11*CH + CW + BH, CW, CH, "");
   fl_set_object_lalign( trms,FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
   fl_set_object_color( trms, FL_WHITE, FL_BLUE); 
   fl_add_text( FL_NORMAL_TEXT, BW, 12*CH + CW + BH, CW, CH, "RMS amplitude");

   fl_end_group();

   fl_set_object_resize( cgroup, FL_RESIZE_NONE);
   fl_set_object_gravity( cgroup, FL_NorthWest, FL_NoGravity);

   fl_end_form();

   //
   //  Display the form and loop to handle events.
   //

   fl_show_form( form, FL_PLACE_FREE, FL_FULLBORDER, "vtrsgram");

   XSync( xdisplay, 0);

   while( 1)
   {
      fl_check_forms();
      usleep( 10000);
   }
}

#else 
static void display( void)
{
   VT_bailout( "-ox not available: vlfrx-tools needs configuring to use X11");
}
#endif // USE_X11

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static void do_spectrogram( void)
{
   int stride = fftwid/step;

   int ns = (nrdata / fftwid) * fftwid;
   SH = bins;         // Height of STFT spectrogram
   SW = ns/stride;    // Width of STFT spectrogram

   VT_report( 1, "SW %d SH %d", SW, SH);

   // We have to do four FFTs on each input block of fftwid samples. These
   // are the four transform output arrays:
   complex double *X = VT_malloc( sizeof( fftw_complex) * (bins+1)),
                 *Xt = VT_malloc( sizeof( fftw_complex) * (bins+1)),
                 *Xd = VT_malloc( sizeof( fftw_complex) * (bins+1)),
                *Xtd = VT_malloc( sizeof( fftw_complex) * (bins+1));

   // The four FFT plans can all use the same input buffer: fftI
   double *fftI = VT_malloc( sizeof( double) * fftwid);

   fftw_plan planX = fftw_plan_dft_r2c_1d( fftwid, fftI, X, FFTW_ESTIMATE);
   fftw_plan planXt = fftw_plan_dft_r2c_1d( fftwid, fftI, Xt, FFTW_ESTIMATE);
   fftw_plan planXd = fftw_plan_dft_r2c_1d( fftwid, fftI, Xd, FFTW_ESTIMATE);
   fftw_plan planXtd = fftw_plan_dft_r2c_1d( fftwid, fftI, Xtd, FFTW_ESTIMATE);

   double *rowsum = VT_malloc_zero( sizeof( double) * SH);

   // An array of reassigned spectrogram points.  Not a 2-D grid, this is
   // just a straightforward list of points.
   rpoints = VT_malloc_zero( sizeof( struct RSPOINT) * SW * bins);
   nrp = 0;   // Number of entries used in rpoints

   int i, x, y;

   //
   //  Do all the transforms and the reassignment.
   //
   for( x=0; x<SW; x++)
   {
      // FT with time derivative weighted window
      for( i=0; i<fftwid; i++)
      {
         double v = rdata[x * stride + i];

         fftI[i] = v * window_derivative( i);
      }

      fftw_execute( planXd);

      // FT with time weighted window
      for( i=0; i<fftwid; i++)
      {
         double v = rdata[x * stride + i];

         fftI[i] = v * 1.0/(fftwid) * i * window_value( i);
      }

      fftw_execute( planXt);

      // FT with time weighted time derivative weighted window
      for( i=0; i<fftwid; i++)
      {
         double v = rdata[x * stride + i];

         fftI[i] = v * 1.0/(fftwid) * i * window_derivative( i);
      }

      fftw_execute( planXtd);

      // FT with normal window
      for( i=0; i<fftwid; i++)
      {
         double v = rdata[x * stride + i];

         fftI[i] = v * window_value( i);
      }

      fftw_execute( planX);

      // Calculate the reassignment and add to list of reassigned points
      for( y=1; y<bins; y++)
      {
         double A = 2 * cabs( X[y])/bins/sqrt(2);
         if( !A) continue;

         rowsum[y] += A;

         struct RSPOINT *rp = rpoints + nrp++;
         rp->y = y;
         rp->x = x;
         if( !NFLAG)
         {
            rp->rx = +creal( Xt[y] / X[y]) * step;
            rp->ry = -cimag( Xd[y] / X[y]);
         }
         rp->A = A;
         rp->mpd = 2 * M_PI *
                       (creal(Xt[y]*Xd[y]/X[y]/X[y]) - creal(Xtd[y]/X[y]));
      }
   }

   fftw_destroy_plan( planX); fftw_destroy_plan( planXt);
   fftw_destroy_plan( planXd); fftw_destroy_plan( planXtd);
   free( X); free( Xt);  free( Xd); free( Xtd);

   //
   // Vertical homomorphic equalisation
   //
   
   if( hcount)   // -h count
   {
      double al = 1e-10;
      double fl = log(al);

      // Put the log STFT amplitudes into a grid
      double *sg = VT_malloc_zero( sizeof( double) * SW * bins);
      for( x=0; x<SW; x++) for( y=0; y<bins;y++) sg[x + SW*y] = fl;

      for( i=0; i<nrp; i++)
      {
         double v = rpoints[i].A;  
         sg[rpoints[i].x + SW*rpoints[i].y] = v < al ? fl : log( v);
      }

      // 2D forward FFT to spatial frequency domain of the log STFT image
      complex double *H = VT_malloc( sizeof( complex double) * SW * bins);

      fftw_plan f1 = fftw_plan_dft_r2c_2d( SW, bins, sg, H, FFTW_ESTIMATE);
      fftw_execute( f1);

      // Zero the hcount lowest vertical frequency rows
      for( y = 0; y < hcount && y < bins/2+1; y++)
            for(x=0; x<SW; x++) H[x+SW*y] = 0.0;
        
      // 2D reverse FFT back to log STFT image 
      fftw_plan f2 = fftw_plan_dft_c2r_2d( SW, bins, H, sg, FFTW_ESTIMATE);
      fftw_execute( f2);

      // Return the modified amplitudes to the reassigned points list
      for( i=0; i<nrp; i++)
      {
         double v = exp( (sg[rpoints[i].x + SW*rpoints[i].y])/SW/bins);
         if( v < fl * 10) v = 0;
         rpoints[i].A = v;
      }

      free( H); free( sg);
   }

   //
   // Vertical equalisation using smoothed row averages
   //
   
   if( EFLAG)    // -e option
   {
      #define BFS (bins/20)
      double sigma = 2 * sqrt(BFS);
      double *rowsum_smoothed = VT_malloc_zero( sizeof( double) * SH);
   
      for( y=0; y<bins; y++)
      {
         int y1 = y - BFS;
         int y2 = y + BFS;
         if( y1 < 0) y1 = 0;
         if( y2 >= bins) y2 = bins-1;
   
         rowsum_smoothed[y] = 0;
         double st = 0;
         for( i=y1; i<=y2; i++)
         {
            double s =  1/(sigma * sqrt( 2*M_PI)) *
                          exp( -(i-y) * (i-y)/(2.0 * sigma * sigma));
            st += s;
            rowsum_smoothed[y] += rowsum[i] * s;
         }
   
         rowsum_smoothed[y] /= st * bins;
      }

      for( i=0; i<nrp; i++)
      {
         rpoints[i].A /= rowsum_smoothed[rpoints[i].y];
         if( isinf( rpoints[i].A) || isnan( rpoints[i].A)) rpoints[i].A = 0;
      }

      free( rowsum_smoothed);
   }

   //
   // Prune points according to mixed partial derivative, -p option
   //

   for( i=0; i<nrp; i++)
      if( fabs( rpoints[i].mpd - 0.5) > pprune) rpoints[i].A = 0;

   //
   // Prune points reassigned a long distance, -r option
   //

   for( i=0; i<nrp; i++)
   {
      if( fabs( rpoints[i].rx) > rprune ||
          fabs( rpoints[i].ry) > rprune) rpoints[i].A = 0;
   }

   //
   //  Prune weak points, -a option
   //

   qsort( rpoints, nrp, sizeof( struct RSPOINT), cmp_rpa);

   int np = aprune * nrp;  
   if( np > nrp) np = nrp;
   if( np < 0) np = 0;
   for( i=0; i<np; i++) rpoints[i].A = 0;

   //
   // Output ASCII with -oa option
   //

   if( outformat == OF_ASCII_POINTS)
   {
      for( i = 0; i < nrp; i++)
         if( rpoints[i].A)
         {
            struct RSPOINT *p = rpoints + i;

            double T = (p->x + p->rx) * stride/(double) sample_rate;
            double F = (p->y + p->ry) * sample_rate/(double) fftwid;
            printf( "%.6f %10.4f %9.3e %+9.3f\n", T, F, p->A * gain, p->mpd);
         }
      return;
   }

   //
   // Map the reassigned spectrogram data to a new large grid, size GWxGH
   //

   int GW = SW * mag_scale;
   int GH = SH * mag_scale;

   VT_report( 1, "GW %d GH %d", GW, GH);

   rs = VT_malloc_zero( sizeof( double) * GW * GH);

   double rsum = 0;
   int rcnt = 0;

   for( i = 0; i < nrp; i++)
      if( rpoints[i].A)
      {
         int mx = rint( (rpoints[i].x + rpoints[i].rx) * mag_scale);
         int my = rint( (rpoints[i].y + rpoints[i].ry) * mag_scale);
   
         if( mx < 0 ||
             mx >= GW ||
             my < 0 ||
             my >= GH)
         {
            rpoints[i].A = 0;
            continue;
         }
  
         rpoints[i].mx = mx;
         rpoints[i].my = my;
 
         double v = rpoints[i].A;
   
         v *= v;

         rsum += v;
         rcnt++;
         rs[mx + my*GW] += v;
      }

   // Normalise the spectrogram brightness, proportional to the mean squared
   // amplitude of the whole rs[] image.
   rsmean = rsum / rcnt;
   rsmean *= 100;
   if( !UFLAG) for( i=0; i < GW * GH; i++) rs[i] /= rsmean;
   else 
      for( i=0; i < GW * GH; i++) rs[i] /= 3e-5;
   VT_report( 1, "rcnt %d %.3e mean %.3e", rcnt, rsum, rsmean);

   if( KFLAG == 1)
   {
      double *rq = VT_malloc_zero( sizeof( double) * GW * GH);
      double th = 1.0/(4 * gain * gain);
      int ns = 0;
      int w = 1;
      double kmap[3][3] = {
         { 0.2, 0.5, 0.2 },
         { 0.5, 1.0, 0.5 },
         { 0.2, 0.5, 0.2 }
      };

      int ix, iy;
      double ms = 0;
      for( ix=0; ix < w+2; ix++)
         for( iy=0; iy < w+2; iy++) ms += kmap[ix][iy];
   
      int j;
      for( i=w; i < GW-w; i++)
         for( j=w; j < GH-w; j++)
         {
            double v = rs[i + j*GW];
            if( v < th)
            {
               rq[i + j*GW] = v;
               continue;
            }

            double a = v - th;
            ns++;

            rq[i + j*GW] = th;
            a /= ms;
            for( ix=0; ix < w+2; ix++)
               for( iy=0; iy < w+2; iy++)
                  rq[i+ix-w + (j+iy-w)*GW] += kmap[ix][iy] * a;
         }

      memcpy( rs, rq, sizeof( double) * GW * GH); 
      VT_report( 1, "ns %d", ns);
   }

   if( KFLAG == 3)
   {
      double *rq = VT_malloc_zero( sizeof( double) * GW * GH);
      double th = 1.0/(4 * gain * gain);
      int ns = 0;
      int w = 3;
      double kmap[5][5] = {
         { 0.1, 0.2, 0.3, 0.2, 0.1 },
         { 0.2, 0.4, 0.7, 0.4, 0.2 },
         { 0.3, 0.7, 1.0, 0.7, 0.3 },
         { 0.2, 0.4, 0.7, 0.4, 0.2 },
         { 0.1, 0.2, 0.3, 0.2, 0.1 }
      };

      int ix, iy;
      double ms = 0;
      for( ix=0; ix < w+2; ix++)
         for( iy=0; iy < w+2; iy++) ms += kmap[ix][iy];
   
      int j;
      for( i=w; i < GW-w; i++)
         for( j=w; j < GH-w; j++)
         {
            double v = rs[i + j*GW];
            if( v < th)
            {
               rq[i + j*GW] = v;
               continue;
            }

            double a = v - th;
            ns++;

            rq[i + j*GW] = th;
            a /= ms;
            for( ix=0; ix < w+2; ix++)
               for( iy=0; iy < w+2; iy++)
                  rq[i+ix-w + (j+iy-w)*GW] += kmap[ix][iy] * a;
         }

      memcpy( rs, rq, sizeof( double) * GW * GH); 
      VT_report( 1, "ns %d", ns);
   }
   //
   // Display rs[] on X11 with -ox option
   //

   if( outformat == OF_X11)
   {
      XW = GW;
      XH = GH;
      display();
      return;
   }

   if( outformat == OF_ASCII_GRID)
   {
      int j;
      for( i=0; i<GW; i++)
         for( j=0; j<GH; j++)
         {
            double v = rs[i + j*GW];
            double T = i * stride/(double) sample_rate/mag_scale;
            double F = j * sample_rate/(double) fftwid/mag_scale;
            printf( "%.6f %10.4f %9.3e\n", T, F, v);
         }

      return;
   }

   //
   //  Generate a png from rs[].
   //

   png_structp png_ptr = png_create_write_struct
       (PNG_LIBPNG_VER_STRING, (png_voidp) NULL, NULL, NULL);
   if( !png_ptr) VT_bailout( "cannot alloc png_ptr");

   png_infop info_ptr = png_create_info_struct( png_ptr);
   if( !info_ptr) VT_bailout( "cannot create png info_struct");

   if( setjmp( png_jmpbuf( png_ptr))) VT_bailout( "png jmp error");

   png_init_io( png_ptr, stdout);

   png_set_IHDR( png_ptr, info_ptr, GW, GH,
       8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
       PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

   png_write_info( png_ptr, info_ptr);

   unsigned char *rbuf = VT_malloc( GW);

   int bmin = 30;
   for( y=GH-1; y >= 0; y--)
   {
      unsigned char *pr = rbuf;

      for( x=0; x<GW; x++)
      {
         double v = sqrt(rs[y*GW + x]) * 2 * gain;
         int pval = bmin + v * 255;
         if( pval > 255) pval = 255;
         if( pval < bmin) pval = bmin;
         *pr++ = pval;
      }

      png_write_row( png_ptr, rbuf);
   }

   png_write_end( png_ptr, NULL);
   png_destroy_write_struct( &png_ptr, &info_ptr);

   free( rbuf);
   free( rowsum);

   free( rs); free( rpoints);
}

int main( int argc, char *argv[])
{
   VT_init( "vtrsgram");

   while( 1)
   {
      int c = getopt( argc, argv, "vBneub:g:s:o:m:a:r:p:d:w:h:k:W:L:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'o') parse_format_options( optarg);
      else
      if( c == 'b') bins = atoi( optarg);
      else
      if( c == 's') step = atoi( optarg);
      else
      if( c == 'd') DFLAG = atoi( optarg);
      else
      if( c == 'g') gain = atof( optarg);
      else
      if( c == 'a') aprune = atof( optarg);
      else
      if( c == 'r') rprune = atof( optarg);
      else
      if( c == 'p') pprune = atof( optarg);
      else
      if( c == 'h') hcount = atoi( optarg);
      else
      if( c == 'm') mag_scale = atof( optarg);
      else
      if( c == 'n') NFLAG = 1;
      else
      if( c == 'e') EFLAG = 1;
      else
      if( c == 'u') UFLAG = 1;
      else
      if( c == 'k') KFLAG = atoi( optarg);
      else
      if( c == 'w' || c == 'W') hamming = strdup( optarg);
      else
      if( c == -1) break;
      else
         usage(); 
   }  
  
   if( argc > optind + 1) usage();
   char *bname = strdup( optind < argc ? argv[optind] : "-");
 
   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   VTFILE *vtfile = VT_open_input( bname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   if( chspec->n != 1) VT_bailout( "must select one channel from input");

   sample_rate = VT_get_sample_rate( vtfile);
   srcal  = VT_get_srcal( vtfile);
   Tstart = VT_get_timestamp( vtfile);

   VT_report( 1, "sample_rate: %d", sample_rate);

   rdata = VT_malloc( sizeof( double) * MAXSAMPLES);

   fftwid = 2 * bins;
   setup_hamming();

   while( nrdata < MAXSAMPLES)
   {
      double *frame;
      if( (frame = VT_get_frame( vtfile)) == NULL) break;

      double v = frame[chspec->map[0]];
      rdata[nrdata++] = v;
   }

   VT_report( 1, "%d samples read", nrdata);

   do_spectrogram();
   return 0;
}

