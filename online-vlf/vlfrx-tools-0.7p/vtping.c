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

static int rqDF = 6;     // Hertz.  Vertical resolution of spectrogram

// Range of region widths for Hotelling transform
#define HWID_MIN 3
#define HWID_MAX 6

static double DTHRESH = 2.0;    // Detection threshold, -D option
static double RTHRESH = 2.0;    // Rejection threshold, -R option

static double MIN_ANGLE = 70.0; // Degrees to horizontal below which oriented
                                // STFT cells are ignored
static double CTHRESH = 0.2;

#define TSPAN 6.0                             // Time span of the STFT, seconds

#define TBASE 2.45

static double tdelay = 0.4; // Set by -t option.
                            // Delay after event detection before saving the
                            // event.  Gives time for event to be improved by
                            // a better match at another time position.

// Exponential moving average coefficients for tracking the noise floor.
#define SFAC1 0.00002
#define SFAC2 0.02

// Input signal variables
static int sample_rate;
static timestamp timebase = timestamp_ZERO;
static double srcal = 1.0;
static uint64_t NS = 0;                  // Number of input samples read so far

// Variables for X display
static int XFLAG = 0;         // Use X display
static int run = 1;           // Continuous running
static int single_step = 0;   // Do one FFT frame, re-evaluate then pause

// STFT dimensions
static int xxW = 0;       // Width of internal STFT

// Fourier transform variables
static int FFTWID = 0;    // Fourier transform input width
static int BINS = 0;      // Number of frequency bins
static double DF = 0.0;   // Frequency resolution
static double DT = 0.0;   // Time resolution
static int DFAC = 2;      // Fourier transform overlap factor is 1/DFAC

// Output thumbnail spectrogram
static double sg_LOW = -1;   // Spectrogram base, Hz, from -s option
static double sg_HIGH = -1;  // Spectrogram top,Hz, from -s option
static int sgH = 0;       // Spectrogram height, number of cells
static int sgW = 0;       // Spectrogram width, number of cells
static int sgB = 0;       // Base bin of spectrogram

#define VTTIME(A) (timestamp_add((A), -TSPAN + TBASE))
#define SET_T  30         // Initial seconds of faster time constant

// Limits for input dynamic range compression
static double gfloor = 1.2;   
static double gtop = 2.5;

static char *outdir = NULL;     // -d option: output directory for events

static int OFLAG_TE = FALSE;    // TRUE if -ote option given

// Table of frequency ranges to examine
static struct FLIST {
   double f1, f2;    // Hertz, low and high
   int b1, b2;       // Bins, low and high, inclusive
}
 *dlist = NULL, *rlist = NULL;

static int ndlist = 0;
static int nrlist = 0;

///////////////////////////////////////////////////////////////////////////////
//  Ring Buffers                                                             //
///////////////////////////////////////////////////////////////////////////////

//
//  A set of circular buffers, all sharing the same base pointer.
//

//    BP      most recently filled column
//    BP + 1  oldest column
//    BP - 1  last column
//    BP - 2  last but one

static int BP = 0;
static int NP = 0;

static double *aring;                              // Normalised input spectrum
static double *dring;   // Column sums of detected cell strengths
static double *rring;   // Column sums of cell strengths in reject bands

static double *ering;       // E-field time domain ring

#define ARING(X,Y) aring[(X)*BINS + (Y)]
#define ERING(X) (ering + (X) * FFTWID)

// Converts from the x coordinate of the STFT (0..xxW-1) to ring buffer index.
#define xtoBP( x) ((BP + 1 + x) % xxW)

static double *ipacc;
static double *ipin;

///////////////////////////////////////////////////////////////////////////////
//  Principal Component Analysis                                             //
///////////////////////////////////////////////////////////////////////////////

// A structure holding the result of a PCA analysis of a square region of the
// STFT.

struct PCA {
   int hwid;                                               // Region half-width
   double td;                                   // Total pixel energy in window

   double vx, vy;              // Direction cosines of the region's orientation
   double ps;
   double aa;           // fabs(angle to horizonal)   degrees
   double val;
   int valid;   // Hotelling returns a useful orientation
};

// A ring buffer: a 2-D array of PCA results aligned with the STFT 
static struct PCA *pring;
#define PRING(X,Y) pring[(X)*BINS + (Y)]

//
//  Use a Hotelling transform to do a principal component analysis of the
//  STFT region centered on cx, cy and save the results if any in the
//  supplied struct PCA.   The region is square with with odd size given by
//  a half width, so the size is (hwid*2+1) by (hwid*2+1).
//  A range of region sizes are tried, starting with hwid = HWID_MIN and
//  working up to hwid = HWID_MAX, stopping as soon as some oriented feature
//  is discerned centered on cx, cy.
//

static void hotelling( int cx, int cy, struct PCA *p)
{
   p->valid = 0;                         // Will be set to 1 if we get a result

   int hwid = HWID_MIN;                                    // Region half-width

   // No significant amplitude at the region center, so give up straight away
   if( ARING( xtoBP( cx), cy) == 0) return;

retry: ;

   double Cxx = 0, Cxy = 0, Cyy = 0;              // Variances and co-variances
   double mx = 0, my = 0;                                     // Center of mass
   double td = 0;

   int i, j, j1, j2;
   if( cy - hwid < 0) j1 = -cy; else j1 = -hwid;
   if( cy + hwid >= BINS) j2 = BINS - cy - 1; else j2 = hwid;

   int bx = xtoBP( cx - hwid);
   for( i= -hwid; i <= hwid; i++, bx = (bx+1) % xxW)
   {
      double *acol = aring + bx * BINS + cy;
      for( j = j1; j <= j2; j++)
      {
         double d = acol[j];
         if( d == 0) continue;
         td += d;

         mx += i * d;
         my += j * d;

         Cxx += i * i * d;
         Cxy += i * j * d;
         Cyy += j * j * d;
      }
   }

   if( td == 0) return;                     // Empty window, nothing to do here

   // Determine the mean position of the pixels in the window.  If the mean is
   // offset from the region center by more than one (T,F) cell then abandon it
   // because another call to this function will be better centered.  Most
   // regions fail this test so relatively few need to be considered further.
   mx /= td;
   my /= td;
   if( fabs( mx) > 1.5 || fabs( my) > 1.5) return;

   p->td = td;

   //  Construct the three elements of the symmetric 2x2 covariance matrix
   Cxx = Cxx / td - mx * mx;
   Cxy = Cxy / td - mx * my;
   Cyy = Cyy / td - my * my;

   // Determine the eigenvalues of the covariance matrix
   double b = -(Cxx + Cyy);
   double c = Cxx * Cyy - Cxy * Cxy;
   double e1 = (-b + sqrt( b*b - 4 * c))/2;
   double e2 = (-b - sqrt( b*b - 4 * c))/2;

   // Sometimes limited precision returns a slight negative eigenvalue
   // or a nan.
   if( e1 < 0 || isnan( e1)) e1 = 0;
   if( e2 < 0 || isnan( e2)) e2 = 0;

   // Make e1 the largest eigenvalue - the principal axis
   if( e2 > e1) { double e = e1; e1 = e2;  e2 = e; }

   // fprintf( stderr, "eigenvalues %.2f %.2f ", e1, e2);

   // Form a measure comparing the magnitude of the principle axis to that of
   // the perpendicular axis.  Use this to decide if we have some significant
   // orientation to the contents of this region.

   p->ps = 2 * e1/(e1 + e2) - 1;
   if( isnan( p->ps)) p->ps = 0;

   if( p->ps < CTHRESH)
   {
      // The region has no significant orientation to it.  Try doubling the
      // region size and re-evaluate to look for a larger scale feature.
      if( hwid < HWID_MAX) { hwid *= 2; goto retry; }
      return;
   }

   //
   // We have some significant orientation in this region.
   //
   // Determine the principle eigenvector and normalise it
   //
   double vx, vy;

   if( Cxx == 0) { vx = 0; vy = 1; }
   else
   {
      vx = 1;
      if( fabs( Cxy) > fabs( e1 - Cyy)) vy = vx * (e1 - Cxx) / Cxy;
      else vy = vx * Cxy/(e1 - Cyy);
   }

   // Form the direction cosines of the principal axis.
   double mag = sqrt( vx*vx + vy*vy);
   p->vx = vx/mag;
   p->vy = vy/mag;

   // Absolute angle to the horizontal, degrees
   p->aa = fabs(180/M_PI * atan2( p->vy, p->vx));

   p->hwid = hwid;
   p->valid = 1;
}

///////////////////////////////////////////////////////////////////////////////
//  X Display                                                                //
///////////////////////////////////////////////////////////////////////////////

//  This is only used for development

#if USE_X11
   #include <X11/Xlib.h>
   #include <X11/Xutil.h>

// Parameters that only affect the display
//

#define FTS     4

#define sgwin_L 0
#define sgwin_T 0
#define sgwin_W (FTS * xxW)
#define sgwin_H (FTS * BINS)

#define edwin_L 0
#define edwin_T (sgwin_T + sgwin_H + 5)
#define edwin_W (FTS * xxW)
#define edwin_H (FTS * BINS)

#define dfwin_L 0
#define dfwin_T (edwin_T + edwin_H + 5)
#define dfwin_W (FTS * xxW)
#define dfwin_H (FTS * BINS)

#define ipwin_L 0
#define ipwin_T (dfwin_T + dfwin_H + 5)
#define ipwin_W (FTS * BINS)
#define ipwin_H (FTS * xxW)

Display *xdisplay;
int      screen;
Window mainwin, sgwin, edwin, dfwin, ipwin, rootwin;

Font    ch_font;
GC gc_text;
XColor yellow, magenta, green, red, cyan, blue;
unsigned long black_pixel, white_pixel;

#define MAXLEV 64
GC whiteGC[ MAXLEV];
GC redGC[ MAXLEV];

static void draw_edwin( void)
{
   int x, y;

   for( x=0; x<xxW; x++)
   {
      int cx = xtoBP( x);
      for( y = 0; y < BINS; y++)
      {
         int sy = BINS - y - 1;
         int bmin = 20;
         int pval = bmin + 2 * PRING( cx, y).val * (MAXLEV - bmin);
         if( pval >= MAXLEV) pval = MAXLEV-1;
         if( pval < bmin) pval = bmin;

         if( PRING(cx,y).aa > MIN_ANGLE)
            XFillRectangle( xdisplay, edwin, 
                         whiteGC[pval], x*FTS, sy*FTS, FTS, FTS);
         else
         {
pval = bmin;
            XFillRectangle( xdisplay, edwin, 
                         redGC[pval], x*FTS, sy*FTS, FTS, FTS);
          }
      }
   }
   XSync( xdisplay, 0);
}

static void draw_sgwin( void)
{
   int x, y;

   for( x=0; x<xxW; x++)
   {
      int cx = xtoBP( x);

      for( y = 0; y < BINS; y++)
      {
         int sy = BINS - y - 1;
         int bmin = 20;
         int pval = bmin + ARING( cx, y) * (MAXLEV - bmin);
         if( pval >= MAXLEV) pval = MAXLEV-1;
         if( pval < bmin) pval = bmin;

         XFillRectangle( xdisplay, sgwin, 
                         whiteGC[pval], x*FTS, sy*FTS, FTS, FTS);
      }
   }
   XSync( xdisplay, 0);
}

static void draw_dfwin( void)
{
   int x, y;

   for( x=0; x<xxW; x++)
   {
      int cx = xtoBP( x);

      int v = dring[cx]/(6.0 * DTHRESH) * BINS;
      if( v >= BINS) v = BINS - 1;

      for( y=0; y<BINS; y++)
      {
         int sy = BINS - y - 1;

         XFillRectangle( xdisplay, dfwin, 
                            y == v ? whiteGC[MAXLEV-1] :  whiteGC[2],
                           x*FTS, sy*FTS, FTS, FTS);
      }
   }
   XSync( xdisplay, 0);
}

static void report_ipwin( int x)
{
   double F = DF * x;

   printf( "F=%.0f ipin=%.3e ACC=%.3e\n", F, ipin[x], ipacc[x]);
}

static void draw_ipwin( void)
{
   int x, k;
   int ipmin = -8, ipmax = 3;
   int ipH = xxW * FTS;
   double v;

   XFillRectangle( xdisplay, ipwin, 
                         whiteGC[0], 0, 0, BINS * FTS, ipH);
   for( x = 0; x < BINS; x++)
   {
      v = ipin[ x]; if( v == 0) v = 1e-9; v = log10( v);

      if( v >= ipmin && v < ipmax)
      {
         int y = ipH * (v-ipmin)/(ipmax - ipmin);
         y = ipH - y;
         for( k=0; k<FTS; k++)
            XDrawPoint( xdisplay, ipwin, 
                         whiteGC[MAXLEV-1], x*FTS + k, y);
      }

      v = ipacc[ x]; if( v == 0) v = 1e-9; v = log10( v);

      if( v >= ipmin && v < ipmax)
      {
         int y = ipH * (v-ipmin)/(ipmax - ipmin);
         y = ipH - y;
         for( k=0; k<FTS; k++)
            XDrawPoint( xdisplay, ipwin, 
                         redGC[MAXLEV-1], x*FTS + k, y);
      }
   }
}

static int service( void)
{
   int n = 0;
   XEvent ev;

   while( XPending( xdisplay))
   {
      XNextEvent( xdisplay, &ev);

      if( ev.type == ButtonPress &&
          ev.xbutton.button == 1
        )
      {
         if( ev.xany.window == dfwin ||
             ev.xany.window == edwin ||
             ev.xany.window == sgwin)
         {
            int x = ev.xbutton.x/FTS;
            int y = BINS - ev.xbutton.y/FTS - 1;

            double T = x * DT;
            double F = DF * y;

            fprintf( stderr, "T=%.2f F=%.0f ", T, F);

            struct PCA *p = &PRING( xtoBP( x), y);
            fprintf( stderr, "x,y=%d,%d p->val=%.3f ",
                  x, y, p->val);

            fprintf( stderr, "hwid=%d td=%.3f ps=%.2f ", p->hwid, p->td, p->ps);
            fprintf( stderr, "con=%.3f ", p->ps * p->val);
            fprintf( stderr, "a=%.0f ", p->aa);
            fprintf( stderr, "g=%.2f h=%.2f\n",
                              dring[xtoBP( x)], rring[xtoBP( x)]);
         }

         if( ev.xany.window == ipwin)
            report_ipwin( ev.xbutton.x/FTS);
      }
      else
      if( !run &&
          ev.type == ButtonPress &&
          ev.xbutton.button == 2
        ) { single_step = 1;  run = 1; }
      else
      if( ev.type == ButtonPress &&
          ev.xbutton.button == 3
        ) run = !run;
      else
      if( ev.type == Expose && ev.xexpose.count == 0)
      {
         if( ev.xany.window == sgwin) draw_sgwin();
         if( ev.xany.window == edwin) draw_edwin();
         if( ev.xany.window == dfwin) draw_dfwin();
         if( ev.xany.window == ipwin) draw_ipwin();
      }
      n++;
   }

   return n;
}

static void display_pause( void)
{
   run = 0;
   draw_edwin();
   draw_dfwin();
   while( !run && XFLAG) {service(); XSync( xdisplay, 0); usleep( 10000); }

}

static void update_spectrogram( void)
{
   int y;
#define OFS 0

   XCopyArea( xdisplay, sgwin, sgwin, whiteGC[0],
                 FTS, 0, (xxW-OFS)*FTS, BINS * FTS, 0, 0); 

   int x = xtoBP( xxW - 1 - OFS);
   for( y = 0; y < BINS; y++)
   {
      int sy = BINS - y - 1;

      int bmin = 20;
      int pval = bmin + ARING( x, y) * (MAXLEV - bmin);
      if( pval >= MAXLEV) pval = MAXLEV-1;
      if( pval < bmin) pval = bmin;

      XFillRectangle( xdisplay, sgwin, 
                         whiteGC[pval], (xxW-OFS-1)*FTS, sy*FTS, FTS, FTS);
   }

   XCopyArea( xdisplay, edwin, edwin, whiteGC[0],
                 FTS, 0, (xxW-OFS)*FTS, BINS * FTS, 0, 0); 

   for( y = 0; y < BINS; y++)
   {
      int sy = BINS - y - 1;

      int bmin = 20;

      int pval = bmin + PRING( x, y).val * 2 * (MAXLEV - bmin);
      if( pval >= MAXLEV) pval = MAXLEV-1;
      if( pval < bmin) pval = bmin;

      if( PRING(x,y).aa > MIN_ANGLE)
         XFillRectangle( xdisplay, edwin, 
                         whiteGC[pval], (xxW-OFS-1)*FTS, sy*FTS, FTS, FTS);
      else
         XFillRectangle( xdisplay, edwin, 
                         redGC[pval], (xxW-OFS-1)*FTS, sy*FTS, FTS, FTS);
   }

   XCopyArea( xdisplay, dfwin, dfwin, whiteGC[0],
                 FTS, 0, (xxW-OFS)*FTS, BINS * FTS, 0, 0); 

   int v = dring[x]/(6.0 * DTHRESH) * BINS;
   if( v >= BINS) v = BINS - 1;

   for( y=0; y<BINS; y++)
   {
      int sy = BINS - y - 1;

      XFillRectangle( xdisplay, dfwin, 
                         y == v ? whiteGC[MAXLEV-1] :  whiteGC[2],
                        (xxW-OFS-1)*FTS, sy*FTS, FTS, FTS);
   }
}

static void setup_display( int argc, char *argv[])
{
   int i;
   XEvent ev;
   long megamask;

   if( (xdisplay = XOpenDisplay( NULL)) == NULL)
      VT_bailout( "cannot open xdisplay");

   screen = DefaultScreen( xdisplay );
   rootwin = DefaultRootWindow( xdisplay );
   ch_font = XLoadFont( xdisplay, "6x13");

   Colormap cmap = DefaultColormap( xdisplay, screen);
  
   black_pixel = BlackPixel( xdisplay, screen);
   white_pixel = WhitePixel( xdisplay, screen);

   yellow.red = 0xff00;
   yellow.green = 0xff00;
   yellow.blue = 0;
   magenta.red = 0x3c00;
   magenta.green = 0x6000;
   magenta.blue = 0x8000;

   green.red = 0;
   green.green = 0xff00;
   green.blue = 0;

   red.red = 0xff00; 
   red.green = 0;
   red.blue = 0;

   cyan.red = 0xc000;
   cyan.green = 0xc000;
   cyan.blue = 0xff00;

   blue.red = 0;
   blue.green = 0;
   blue.blue = 0xff00;

   if( !XAllocColor( xdisplay, cmap, &yellow) ||
       !XAllocColor( xdisplay, cmap, &magenta) ||
       !XAllocColor( xdisplay, cmap, &red) ||
       !XAllocColor( xdisplay, cmap, &cyan) ||
       !XAllocColor( xdisplay, cmap, &blue) ||
       !XAllocColor( xdisplay, cmap, &green)) VT_bailout( "alloc color");

   mainwin = XCreateSimpleWindow( xdisplay, rootwin, 
              0, 0, FTS * xxW + 4, FTS * BINS * 3 + FTS * xxW + 22, 
              2, white_pixel,  black_pixel);

   XSetStandardProperties( xdisplay, mainwin, 
                           "VLF Signal Analyser", "VSA",
                           None, NULL, 0, NULL);

   sgwin = XCreateSimpleWindow( xdisplay, mainwin, 
                                sgwin_L, sgwin_T, sgwin_W, sgwin_H, 
                                1, white_pixel,  black_pixel);

   edwin = XCreateSimpleWindow( xdisplay, mainwin, 
                                edwin_L, edwin_T, edwin_W, edwin_H, 
                                1, white_pixel,  black_pixel);

   dfwin = XCreateSimpleWindow( xdisplay, mainwin, 
                                dfwin_L, dfwin_T, dfwin_W, dfwin_H, 
                                1, white_pixel,  black_pixel);

   ipwin = XCreateSimpleWindow( xdisplay, mainwin, 
                                ipwin_L, ipwin_T, ipwin_W, ipwin_H,
                                1, white_pixel,  black_pixel);

   gc_text = XCreateGC( xdisplay, mainwin, 0L, 0 );
   XSetForeground( xdisplay, gc_text, white_pixel );
   XSetBackground( xdisplay, gc_text, black_pixel );
   XSetFont( xdisplay, gc_text, ch_font );

   for( i=0; i<MAXLEV; i++)
   {
      whiteGC[ i] = XCreateGC( xdisplay, mainwin, 0, 0);
      redGC[ i] = XCreateGC( xdisplay, mainwin, 0, 0);

      XColor tx;
      tx.red = 255 *i/(double) MAXLEV; tx.red <<= 8;
      tx.green = 255 *i/(double) MAXLEV;  tx.green <<= 8;
      tx.blue = 255 *i/(double) MAXLEV;   tx.blue <<= 8;
      if( !XAllocColor( xdisplay, cmap, &tx))
         VT_bailout( "cannot alloc col %d", i);
      XSetForeground( xdisplay, whiteGC[ i], tx.pixel);

      tx.red = 255 *i/(double) MAXLEV; tx.red <<= 8;
      tx.green = 0;  tx.green <<= 8;
      tx.blue = 0;   tx.blue <<= 8;
      if( !XAllocColor( xdisplay, cmap, &tx))
         VT_bailout( "cannot alloc col %d", i);
      XSetForeground( xdisplay, redGC[ i], tx.pixel);
   }

   XSelectInput( xdisplay, mainwin, ExposureMask );
   XSelectInput( xdisplay, sgwin, ExposureMask );
   XSelectInput( xdisplay, edwin, ExposureMask );
   XSelectInput( xdisplay, dfwin, ExposureMask );
   XSelectInput( xdisplay, ipwin, ExposureMask );
   XMapRaised( xdisplay, mainwin);
   XMapRaised( xdisplay, sgwin);
   XMapRaised( xdisplay, edwin);
   XMapRaised( xdisplay, dfwin);
   XMapRaised( xdisplay, ipwin);

   do 
   {
      XNextEvent( xdisplay, &ev );
   }
    while( ev.type != Expose);

   megamask = KeyPressMask | ExposureMask |
              EnterWindowMask | LeaveWindowMask | 
              ButtonPressMask | ButtonReleaseMask;

   XSelectInput( xdisplay, sgwin, megamask);
   XSelectInput( xdisplay, edwin, megamask);
   XSelectInput( xdisplay, dfwin, megamask);
   XSelectInput( xdisplay, ipwin, megamask);
}

#endif

///////////////////////////////////////////////////////////////////////////////
//  Result set                                                               //
///////////////////////////////////////////////////////////////////////////////

//
//  A structure to hold detected events.  
//

static struct RSET {
   timestamp T;                                              // Event timestamp
   double srcal;                                     // Sample rate calibration

   double D;      // Detection level                           
   double R;      // Rejection level
} rset;

static void reset_rset( struct RSET *r)
{
   memset( r, 0, sizeof( struct RSET));
}

//
//  Store a PNG thumbnail spectrogram of the event.
//

#define th_MARGIN 0.0   // Time offset of left margin in STFT, seconds
#define th_scale 2      // Scale factor, STFT cells to pixels   
#define th_W (sgW * th_scale)        // Thumbnail width, pixels
#define th_H (sgH * th_scale)        // Thumbnail height, pixels

static void store_rset_thumb( struct RSET *r, char *prefix)
{
   char fname[200], tname[200];

   sprintf( fname, "%s/%s.png", outdir, prefix);
   sprintf( tname, "%s/new.png", outdir);

   FILE *fp = fopen( tname, "w");
   if( !fp) VT_bailout( "cannot open %s, %s", tname, strerror( errno));

   png_structp png_ptr = png_create_write_struct
       (PNG_LIBPNG_VER_STRING, (png_voidp) NULL, NULL, NULL);
   if( !png_ptr) VT_bailout( "cannot alloc png_ptr");

   png_infop info_ptr = png_create_info_struct( png_ptr);
   if( !info_ptr) VT_bailout( "cannot create png info_struct");

   if( setjmp( png_jmpbuf( png_ptr))) VT_bailout( "png jmp error");

   png_init_io( png_ptr, fp); 

   png_set_IHDR( png_ptr, info_ptr, th_W, th_H,
       8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
       PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

   png_write_info( png_ptr, info_ptr);

   int x, y;
   int bmin = 20;

   for( y = th_H-1; y >= 0; y--)
   {
      unsigned char rbuf[th_W * 3], *pr = rbuf;

      for( x = 0; x < th_W; x++)
      {
         int cx = xtoBP( x/th_scale);
         int cy = y/th_scale + sgB;
         int dval = bmin + 2 * PRING( cx, cy).val * (255 - bmin);
         if( dval > 255) dval = 255;
         if( dval < bmin) dval = bmin;

         int aval = bmin + ARING( cx, cy) * (255 - bmin);
         if( aval > 255) aval = 255;
         if( aval < bmin) aval = bmin;

         if( PRING(cx,cy).aa > MIN_ANGLE)
         {
            *pr++ = dval;
            *pr++ = dval;
            *pr++ = dval;
         }
         else
         {
            *pr++ = aval;
            *pr++ = 0;
            *pr++ = 0;
         }
      }

      png_write_row( png_ptr, rbuf);
   }
   
   png_write_end( png_ptr, NULL);
   fclose( fp);
   png_destroy_write_struct( &png_ptr, &info_ptr);

   if( rename( tname, fname) < 0)
      VT_bailout( "cannot rename output file, %s", strerror( errno));
}

//
//  Write a text file giving info about the event
//

static void store_rset_txt( struct RSET *r, char *prefix)
{
   char fname[200];
   char tname[200];

   sprintf( fname, "%s/%s.txt", outdir, prefix);
   sprintf( tname, "%s/new.txt", outdir);

   FILE *fp = fopen( tname, "w");
   if( !fp) VT_bailout( "cannot open %s, %s", tname, strerror( errno));

   char temp[30];  timestamp_string3( VTTIME( r->T), temp);
   fprintf( fp, "T=%s", temp);

   fprintf( fp, " D=%.2f R=%.2f", r->D, r->R);
   if( r->R > RTHRESH) fprintf( fp, " S");
   fprintf( fp, "\n");
   fclose( fp);

   if( rename( tname, fname) < 0) 
      VT_bailout( "cannot rename output file, %s", strerror( errno));
}

//
//  Store the signal.
//

static void store_rset_vt( struct RSET *r, char *prefix)
{
   char *fname, *tname, *vname;

   if( asprintf( &fname, "%s/%s.vt", outdir, prefix) < 0 ||
       asprintf( &tname, "%s/new.vt", outdir) < 0 ||
       asprintf( &vname, "%s/new.vt,i2", outdir) < 0)
      VT_bailout( "out of memory");

   int chans = 1;

   VTFILE *vtout = VT_open_output( vname, chans, 0, sample_rate);
   if( !vtout) VT_bailout( "cannot open %s: %s", tname, strerror( errno));

   double frame[3];

   VT_set_timebase( vtout, timestamp_add( timebase, -xxW*DT + DT), srcal);
   int x, i;
   for( x=0; x<xxW; x++)
      for( i=0; i<FFTWID/DFAC; i++)
      {
         int xp = xtoBP( x);
         frame[0] = ERING( xp)[i];
         VT_insert_frame( vtout, frame);
      }

   VT_release( vtout);
   VT_close( vtout);

   if( rename( tname, fname) < 0) 
      VT_bailout( "cannot rename output file, %s", strerror( errno));

   free( vname); free( tname); free( fname);
}

//
//  Store the result set in the events directory.
//

static void store_rset( struct RSET *r)
{
   mkdir( outdir, 0777);

   char temp[30];   timestamp_string3( VTTIME( r->T), temp);

   char prefix[50];
   sprintf( prefix, "%d.%d",
            timestamp_secs( VTTIME(r->T)),
            (int)(timestamp_frac( VTTIME(r->T)) * 10) );

   VT_report( 0, "RSET: %s T=%s D=%.2f R=%.2f", prefix, temp, r->D, r->R);
   store_rset_vt( r, prefix);
   store_rset_thumb( r, prefix);
   store_rset_txt( r, prefix);
}

///////////////////////////////////////////////////////////////////////////////
//  Ping Register                                                            //
///////////////////////////////////////////////////////////////////////////////

static double sum_dlevel = 0;
static double sum_rlevel = 0;
static double sum_dlevelsq = 0;
static double sum_rlevelsq = 0;
static int nlevel = 0;

static void register_events( void)
{
   //
   //  Don't register any events until we've been running for 30 seconds, to
   //  allow time for moving averages to settle.
   //

   if( NS/sample_rate < 30) return;

   int x = TBASE/DT;
   double dlevel = dring[ xtoBP(x)];
   double rlevel = rring[ xtoBP(x)];

   sum_dlevel += dlevel;
   sum_rlevel += rlevel;
   sum_dlevelsq += dlevel * dlevel;
   sum_rlevelsq += rlevel * rlevel;
   if( ++nlevel > 120 /DT)
   {
      double mean_dlevel = sum_dlevel/nlevel;
      double mean_rlevel = sum_rlevel/nlevel;

      VT_report( 1, "dlevel: mean %.2f sdev %.2f  rlevel: mean %.2f sdev %.2f", 
           mean_dlevel, sqrt(sum_dlevelsq/nlevel - mean_dlevel * mean_dlevel),
           mean_rlevel, sqrt(sum_rlevelsq/nlevel - mean_rlevel * mean_rlevel));

      nlevel = 0;
      sum_dlevel = sum_dlevelsq = sum_rlevel = sum_rlevelsq = 0;
   }

   if( dlevel > DTHRESH && rlevel < RTHRESH && dlevel > rset.D) 
   {
      rset.T = timebase;
      rset.srcal = srcal;
      rset.D = dlevel;
      rset.R = rlevel;
   }

   //  If we have an event and it's now older than tdelay seconds without
   //  further improvement, save the event.
   
   if( rset.D && 
       timestamp_GT( timebase, timestamp_add( rset.T, tdelay)))
   {
      if( !outdir)
      {
         timestamp T = VTTIME( rset.T);
         char temp[30];

         if( OFLAG_TE)
            timestamp_string3( T, temp);
         else
         {
            time_t xsec = timestamp_secs( T);
            struct tm *tm = gmtime( &xsec);
            sprintf( temp, "%04d-%02d-%02d_%02d:%02d:%02d.%03d",
                            tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday,
                            tm->tm_hour, tm->tm_min, tm->tm_sec,
                            (int)(1e3 * timestamp_frac(T)));
         }
         printf( "PING %s %.3f %.3f\n", temp, rset.D, rset.R);
         fflush( stdout);
      }

      if( outdir) store_rset( &rset);
      reset_rset( &rset);

      #if USE_X11
      if( XFLAG) display_pause();
      #endif
   }
}

//
// Track the noise floor.  Asymmetric time constants in a moving
// average.
//
static void update_noise_floor( int j, double v)
{
   static int set = 0;

   // For first SET_T seconds, use faster time constants to settle the
   // background quickly after start-up.
   if( !set)
   {
      if( NS < SET_T * sample_rate)
      {
         if( v > ipacc[j])
            ipacc[j] = ipacc[j] * (1 - SFAC1 * 10) + SFAC1 * 10 * v;
         else
            ipacc[j] = ipacc[j] * (1 - SFAC2 * 10) + SFAC2 * 10 * v;

         return;
      }

      set = 1;   // Move to normal time constants from now on
      VT_report( 1, "settling completed");
   }

   if( v > ipacc[j])
      ipacc[j] = ipacc[j] * (1 - SFAC1) + SFAC1 * v;
   else
      ipacc[j] = ipacc[j] * (1 - SFAC2) + SFAC2 * v;
}

static void process_buffer( double *ebuf)
{
   static complex double *Xout = NULL;
   static double *Xin;
   static fftw_plan ffp;
   static double *save;

   int i, j;

   //
   //  First time through, initialise the Fourier transform
   //

   if( !Xout)
   {
      Xout = VT_malloc( sizeof( complex double) * FFTWID);
      Xin = VT_malloc( sizeof( double) * FFTWID);
      ffp = fftw_plan_dft_r2c_1d( FFTWID, Xin, Xout, FFTW_ESTIMATE);
      save = VT_malloc_zero( sizeof( double) * FFTWID);
   }

   //
   //  Line up the ring buffer column to be filled by this transform frame.
   //

   BP = (BP + 1) % xxW;

   // Save the raw signal and prepare it for FFT
   double *tp = ERING( BP);
   for( i=0; i<FFTWID/DFAC; i++) *tp++ = ebuf[i];

   memmove( save, save + FFTWID/DFAC, sizeof( double) * (FFTWID - FFTWID/DFAC));
   for( i=0; i<FFTWID/DFAC; i++) save[FFTWID - FFTWID/DFAC + i] = ebuf[i];

   // Run the FFT
   for( i=0; i<FFTWID; i++) Xin[i] = save[i];
   fftw_execute( ffp);

   //
   //  Maintain a moving average noise floor power for each bin
   //

   for( j=0; j<BINS; j++)
   {
      int bin = j;
      double v = cabs( Xout[bin]) * cabs( Xout[bin]);

      ipin[j] = v;   // Bin power

      update_noise_floor( j, v);
   }

   //
   //  Limit the signal in each bin to logarithmic range gfloor to gtop and
   //  scale to range 0..1.   Store the result in the analyser input
   //  buffer ARING.
   //

   for( j=0; j<BINS; j++)
   {
      double v = ipin[j] / ipacc[j];   // Power S/N ratio

      // Log power S/N ratio and restrict dynamic range
      v = log10( isnan( v) || v <= 0 ? v = 1e-100 : v);
      v = (v - gfloor)/(gtop - gfloor);
      if( v < 0) v = 0;
      if( v > 1) v = 1;

      ARING( BP, j) = v;    // Insert to the analyser ring buffer
   }

   #if USE_X11
   if( XFLAG) draw_ipwin();
   #endif

   //
   //  Run hotelling transform to fill in a column of PRING with PCA data
   //

   int x1 = xtoBP( xxW - HWID_MAX - 1);
   for( j=1; j<BINS-1; j++)
   {
      struct PCA *p = &PRING( x1, j);
      hotelling( xxW - HWID_MAX - 1, j, p);

      if( p->valid) p->val = ARING( x1, j) * p->ps;
      else p->val = p->aa = 0;
   }

   double dlevel = 0;
   int n;
   for( n = 0; n < ndlist; n++)
   {
      struct FLIST *f = dlist + n;

      for( j = f->b1; j <= f->b2; j++)
      {
         struct PCA *p = &PRING( x1, j);
         if( p->valid &&  p->aa > MIN_ANGLE) dlevel += p->val;
      }
   }

   dring[x1] = dlevel;

   double rlevel = 0;
   for( n = 0; n < nrlist; n++)
   {
      struct FLIST *f = rlist + n;

      for( j = f->b1; j <= f->b2; j++)
      {
         struct PCA *p = &PRING( x1, j);
         if( p->valid &&  p->aa > MIN_ANGLE) rlevel += p->val;
      }
   }

   rring[x1] = rlevel;

   if( NP < xxW) NP++;
   else 
   {
      #if USE_X11
         if( XFLAG) update_spectrogram();
      #endif

      register_events();
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void usage( void)
{
   fprintf( stderr, 
      "usage:  vtping [options] -d outdir input\n"
      "\n"
      "options:\n"
      " -v          Increase verbosity\n"
      " -B          Run in background\n"
      " -L name     Specify logfile\n"
      " -X          Use X display\n"
      "\n"
      " -D thresh   Detection threshold (default 2.0)\n"
      " -R thresh   Rejection threshold (default 2.0)\n"
      "\n"
      " -F detect,low,high  Detection frequency range, Hz\n"
      " -F reject,low,high  Rejection frequency range, Hz\n"
      " -t secs     Trigger holdoff (default 0.4 seconds\n"
      "\n"
      " -d outdir   Place event files under outdir\n"
      " -s low,high  Output spectrogram frequency range\n"
      "\n"
      "-ote         Output unix epoch timestamp\n"
      "-oti         Output ISO timestamps (default)\n"
   );

   exit( 1);
}

static void parse_output_option( char *s)
{
   if( !strcmp( s, "te")) { OFLAG_TE = 1; return; }
   if( !strcmp( s, "ti")) { OFLAG_TE = 0; return; }

   VT_bailout( "unrecognised output option: [%s]", s);
}

int main(int argc, char *argv[])
{
   VT_init( "vtping");

   char *bname;
   VTFILE *vtfile;
   int background = 0;

   while( 1)
   {  
      int c = getopt( argc, argv, "vBXhd:p:o:F:R:D:L:s:t:?");
   
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'X') XFLAG = 1;
      else
      if( c == 'F' && !strncmp( optarg, "detect,", 7))
      {
         dlist = VT_realloc( dlist, (ndlist+1) * sizeof( struct FLIST));
         VT_parse_freqspec( optarg+7, &dlist[ndlist].f1, &dlist[ndlist].f2);
         if( dlist[ndlist].f1 > dlist[ndlist].f2)
            VT_bailout( "invalid -F argument");
         ndlist++;
      }
      else
      if( c == 'F' && !strncmp( optarg, "reject,", 7))
      {
         rlist = VT_realloc( rlist, (nrlist+1) * sizeof( struct FLIST));
         VT_parse_freqspec( optarg+7, &rlist[nrlist].f1, &rlist[nrlist].f2);
         if( rlist[nrlist].f1 > rlist[nrlist].f2)
            VT_bailout( "invalid -F argument");
         nrlist++;
      }
      else
      if( c == 'F') VT_bailout( "bad argument to -F");
      else
      if( c == 's') VT_parse_freqspec( optarg, &sg_LOW, &sg_HIGH);
      else
      if( c == 'd') outdir = strdup( optarg);
      else
      if( c == 'o') parse_output_option( optarg);
      else
      if( c == 'D') DTHRESH = atof( optarg);
      else
      if( c == 'R') RTHRESH = atof( optarg);
      else
      if( c == 't') tdelay = atof( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( !outdir)
      VT_report( 1, "no output directory given, events will not be stored");

   if( !ndlist) VT_bailout( "no frequency range given, needs -F");

   #if !USE_X11
      if( XFLAG) VT_report( 0, "-X ignored, not compiled with X11");
   #endif

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDIN : 0;
      if( !outdir) flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);
   vtfile = VT_open_input( bname);
   if( !vtfile)
      VT_bailout( "cannot open input %s: %s", bname, VT_error);

   sample_rate = VT_get_sample_rate( vtfile);

   VT_init_chanspec( chspec, vtfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chspec->n, sample_rate);

   //
   //  Setup FFT.
   //  rqDF is the requested frequency resolution, DF is the actual resolution.
   //

   int nyquist = sample_rate / 2;
   BINS = nyquist / rqDF;
   DF = nyquist / (double) BINS;

   FFTWID = BINS * 2;
   DT = FFTWID / DFAC / (double) sample_rate;

   //
   //  Setup STFT.
   // 

   xxW = TSPAN / DT; 

   //
   //  Prepare output spectrogram
   //

   if( sg_LOW < 0) sg_LOW = 0;
   if( sg_HIGH < 0) sg_HIGH = sample_rate/2;
   sgB = sg_LOW / DF;
   sgH = sg_HIGH / DF - sgB;
   sgW = xxW;

   VT_report( 1, "bins=%d DF=%.1f DT=%.3f sgH=%d sgW=%d", 
                     BINS, DF, DT, sgH, sgW);

   aring = VT_malloc_zero( sizeof( double) * xxW * BINS);
   pring = VT_malloc_zero( sizeof( struct PCA) * xxW * BINS);
   dring = VT_malloc_zero( sizeof( double) * xxW);
   rring = VT_malloc_zero( sizeof( double) * xxW);
   ipin = VT_malloc_zero( sizeof( double) * BINS);
   ering = VT_malloc_zero( sizeof( double) * xxW * FFTWID);

   // Prepare frequency ranges
   int i;
   for( i=0; i<ndlist; i++)
   {
      dlist[i].b1 = dlist[i].f1 / DF;
      if( dlist[i].b1 <0) dlist[i].b1 = 0;
      dlist[i].b2 = dlist[i].f2 / DF;
      if( dlist[i].b2 >= BINS) dlist[i].b2 = BINS - 1;
      VT_report( 1, "detect %.0f to %.0f bins %d to %d", 
             dlist[i].f1, dlist[i].f2, dlist[i].b1, dlist[i].b2);
   }

   for( i=0; i<nrlist; i++)
   {
      rlist[i].b1 = rlist[i].f1 / DF;
      if( rlist[i].b1 <0) rlist[i].b1 = 0;
      rlist[i].b2 = rlist[i].f2 / DF;
      if( rlist[i].b2 >= BINS) rlist[i].b2 = BINS - 1;
      VT_report( 1, "reject %.0f to %.0f bins %d to %d", 
             rlist[i].f1, rlist[i].f2, rlist[i].b1, rlist[i].b2);
   }

   #if USE_X11
   if( XFLAG) setup_display( argc, argv);
   #endif

   ipacc = VT_malloc_zero( sizeof( double) * BINS);

   //
   //  Main loop
   //

   double *ebuf = VT_malloc( sizeof( double) * FFTWID);
   double *inframe;
   int nb = 0;

   while( 1)
   {
      if( run)
      {
         // Capture the timestamp of the first sample of each FT frame,
         // along with the current sample rate calibration.
         if( !nb)
         {
            timebase = VT_get_timestamp( vtfile);
            srcal = VT_get_srcal( vtfile);
         }
   
         inframe = VT_get_frame( vtfile);
         if( !inframe) break;
   
         ebuf[nb] = inframe[chspec->map[0]];
         if( ++nb < FFTWID/DFAC) continue;
         nb = 0;
   
         //
         //  The FT input buffer is full, process the frame
         //
   
         process_buffer( ebuf); NS += FFTWID / DFAC;
         if( XFLAG && single_step) single_step = run = 0;
      }
      else usleep( 10000);

      #if USE_X11
      if( XFLAG && service()) XSync( xdisplay, 0);
      #endif
   }

   VT_exit( "end of input");
   return 0;
}

