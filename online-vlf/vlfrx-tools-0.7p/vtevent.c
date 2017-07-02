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

static int rqDF = 120;     // Hertz.  Vertical resolution of spectrogram

// Range of region widths for Hotelling transform
#define HWID_MIN 3
#define HWID_MAX 12

// Range of slope angles considered by the Hough transform
#define MAX_ANGLE 60
#define MIN_ANGLE 10

static double CTHRESH = 0.4;
static double ATHRESH = 0.9995; // Cosine of Hough acceptance angle range
static double PTHRESH = 2.0;     // Hough transform peak threshold

static int MINFREQ = 300;    // Hertz.  Lower limit of STFT
static int MAXFREQ = 12000;  // Hertz.  Upper limit of STFT

static int FMAX = 8000;   // Hertz. Max frequency for PS analysis
static int FMIN = 2000;   // Hertz. Min frequency for PS analysis

#define TSPAN 6.0                             // Time span of the STFT, seconds

#define TBASE 2.45

#define TDELAY 0.4     // Delay after event detection before saving the event.
                       // Gives time for event to be improved by a better match
                       // at another time position.

#define RRANGE1 0.2
#define RRANGE2 1.0
static double RTHRESH1 = 10.0;
static double RTHRESH2 = 30.0;

static int RMAX = 8000;   // Hertz. Max frequency for riser analysis
static int RMIN = 3000;   // Hertz. Min frequency for riser analysis

// Exponential moving average coefficients for tracking the noise floor.
#define SFAC1 0.000002
#define SFAC2 0.002

static int sample_rate;
static timestamp timebase = timestamp_ZERO;
static double srcal = 1.0;

// Variables for X display
static int XFLAG = 0;         // Use X display
static int run = 1;           // Continuous running
static int single_step = 0;   // Do one FFT frame, re-evaluate then pause

static int sgH = 0;       // Height of internal STFT
static int sgW = 0;       // Width of internal STFT
static int sgB = 0;       // Base bin of used part of STFT, set from MINFREQ
static int FFTWID = 0;    // Fourier transform input width
static int BINS = 0;      // Number of frequency bins
static double DF = 0.0;   // Frequency resolution
static double DT = 0.0;   // Time resolution

//  Variables for polar operation

static int polar_mode = 0;
static double polar1_align = 0;        // Azimuth of 1st H-loop channel
static double polar2_align = 0;        // Azimuth of 2nd H-loop channel

static int ch_EFIELD = -1;
static int ch_HFIELD1 = -1;
static int ch_HFIELD2 = -1;

static uint64_t NS = 0;                  // Number of input samples read so far
static int frames = 0;                        // Number of FFT frames processed

#define VTTIME(A) (timestamp_add((A), -TSPAN + TBASE))

// Limits for input dynamic range compression
static double gfloor = 1.2;   
static double gtop = 2.5;

static char *outdir = NULL;     // -d option: output directory for events


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
static double *gring;       // Column sums of positive gradient pixels

static double *ering;       // E-field time domain ring
static double *hring1, *hring2;   // Two H-field time domain rings


#define ARING(X,Y) aring[(X)*sgH + (Y)]
#define ERING(X) (ering + (X) * FFTWID)
#define HRING1(X) (hring1 + (X) * FFTWID)
#define HRING2(X) (hring2 + (X) * FFTWID)

// Converts from the x coordinate of the STFT (0..sgW-1) to ring buffer index.
#define xtoBP( x) ((BP + 1 + x) % sgW)

static struct TLIST {
   short ja, jk;
   short jt, jf;
   float qx, qy;
} *tlist = NULL;

static int ntlist; 
static int maxtlist;

static double *ipacc;
static double *ipin;

///////////////////////////////////////////////////////////////////////////////
//  Dispersion Range                                                         //
///////////////////////////////////////////////////////////////////////////////

//  The variable K used in this program is the square of the dispersion D.
//  Maybe the command line should allow these to be set?

static double PS_KMIN = 150;    // Min value of K
static double PS_KMAX = 13000;  // Max value of K

#define PS_KSTEPS 30                   // Number of Hough space cells in K axis

static double Hspace[3][PS_KSTEPS];             // Hough transform output space

static inline double K_from_jk( int jk)
{
   return PS_KMIN * pow( PS_KMAX/PS_KMIN, (jk+0.5)/(double)(PS_KSTEPS-1));
}

static inline int jk_from_K( double K)
{ 
   return log10(K/PS_KMIN)/log10(PS_KMAX/PS_KMIN) * (PS_KSTEPS -1 );
}

static double Qcol[PS_KSTEPS];

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
   double val;
   int valid;   // Hotelling returns a useful orientation
};

// A ring buffer: a 2-D array of PCA results aligned with the STFT 
static struct PCA *pring;
#define PRING(X,Y) pring[(X)*sgH + (Y)]

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
   if( cy + hwid >= sgH) j2 = sgH - cy - 1; else j2 = hwid;

   int bx = xtoBP( cx - hwid);
   for( i= -hwid; i <= hwid; i++, bx = (bx+1) % sgW)
   {
      double *acol = aring + bx * sgH + cy;
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

   if( p->ps < CTHRESH)
   {
      // The region has no significant orientation to it.  Try doubling the
      // region size and re-evaluate to look for a large scale feature.
      if( hwid < HWID_MAX) { hwid *= 2; goto retry; }
      return;
   }

   //
   // We have some significant orientation in this region.  We'll keep it and
   // supply it to the Hough transform.
   //

   // Determine the principle eigenvector and normalise it
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

#define PS_FTS  4
#define FTS     2
#define IP_FTS  4

#define sgwin_L 0
#define sgwin_T 0
#define sgwin_W (FTS * sgW)
#define sgwin_H (FTS * sgH)

#define edwin_L 0
#define edwin_T (sgwin_T + sgwin_H + 5)
#define edwin_W (FTS * sgW)
#define edwin_H (FTS * sgH)

#define dfwin_L 0
#define dfwin_T (edwin_T + edwin_H + 5)
#define dfwin_W (FTS * sgW)
#define dfwin_H (FTS * sgH)

#define pswin_L 0
#define pswin_T (dfwin_T + dfwin_H + 5)
#define pswin_W (PS_FTS * 3)
#define pswin_H (PS_FTS * PS_KSTEPS)

#define tmwin_L (pswin_W + 5)
#define tmwin_T (dfwin_T + dfwin_H + 5)
#define tmwin_W (PS_FTS * 3)
#define tmwin_H (PS_FTS * PS_KSTEPS)

#define ipwin_L (tmwin_L + tmwin_W + 5)
#define ipwin_T (dfwin_T + dfwin_H + 5)
#define ipwin_W (IP_FTS * sgH)
#define ipwin_H (PS_FTS * PS_KSTEPS)

Display *xdisplay;
int      screen;
Window mainwin, sgwin, pswin, tmwin, edwin, dfwin, ipwin, rootwin;

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

   for( x=0; x<sgW; x++)
   {
      int cx = xtoBP( x);
      for( y=0; y<sgH; y++)
      {
         int sy = sgH - y - 1;
         int bmin = 20;
         int pval = bmin + 2 * PRING( cx, y).val * (MAXLEV - bmin);
         if( pval >= MAXLEV) pval = MAXLEV-1;
         if( pval < bmin) pval = bmin;

         XFillRectangle( xdisplay, edwin, 
                         redGC[pval], x*FTS, sy*FTS, FTS, FTS);
      }
   }
   XSync( xdisplay, 0);
}

static void plot_dfcurve( int ik)
{
   draw_edwin();
   XFillRectangle( xdisplay, dfwin, 
                         whiteGC[10], 0, 0, dfwin_W, dfwin_H);

   double K = K_from_jk( ik);

   fprintf( stderr, "plot K=%.0f pimg=%.2e qcol=%.0f", 
                    K, Hspace[2][ik], Qcol[ik]);

   double pm = 0;
   struct TLIST *tp = tlist;
   int i;
   int np = 0, nt = 0;
   for( i=0; i<ntlist; i++, tp++)
      if( tp->jk == ik)
      { 
         nt++;
         struct PCA *p = &PRING( xtoBP( tp->jt), tp->jf);

         int sy = sgH - tp->jf - 1;
         XFillRectangle( xdisplay, dfwin, whiteGC[63], 
                         tp->jt*FTS, sy*FTS, FTS, FTS);

         int bmin = 20;
         int pval = bmin + p->val *  2 * (MAXLEV - bmin);
         if( pval >= MAXLEV) pval = MAXLEV-1;
         if( pval < bmin) pval = bmin;

         XFillRectangle( xdisplay, edwin, 
                         whiteGC[pval], tp->jt*FTS, sy*FTS, FTS, FTS);


         double cosa = (p->vx * tp->qx + p->vy * tp->qy);

         if( p->valid && fabs( cosa) > ATHRESH)
         {
            pm += p->val;
            np++;
         }
      }

   printf( "pm=%.3f np=%d nt=%d\n", pm, np, nt);
}

static void report_edwin( int jt, int jf)
{
   int cy = sgH - jf - 1;
   int cx = xtoBP( jt);

   printf( "cx=%d cy=%d\n", cx, cy);
}

static void plot_tmcurve( int jt, int jf)
{
   XFillRectangle( xdisplay, tmwin, 
                         whiteGC[10], 0, 0, tmwin_W, tmwin_H);

   int i;
   struct TLIST *tp = tlist;
   for( i=0; i<ntlist; i++, tp++)
   {
      if( tp->jt == jt && tp->jf == jf)
      XFillRectangle( xdisplay, tmwin, 
                         whiteGC[63], tp->ja*PS_FTS, 
                         tp->jk*PS_FTS, PS_FTS, PS_FTS);
   }
}

static void draw_sgwin( void)
{
   int x, y;

   for( x=0; x<sgW; x++)
   {
      int cx = xtoBP( x);

      for( y=0; y<sgH; y++)
      {
         int sy = sgH - y - 1;
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

static void draw_pswin( int autoscale)
{
   int ia, ik;
   int pmax = 0;

   if( !autoscale)
      pmax = 10; //  * sample_rate / (double) 48000;
   else
      for( ia = 0; ia < 3; ia++)
         for( ik = 0; ik < PS_KSTEPS; ik++)
            if( Hspace[ia][ik] > pmax) pmax = Hspace[ia][ik];

   for( ia = 0; ia < 3; ia++)
      for( ik = 0; ik < PS_KSTEPS; ik++)
      {
         int pval = Hspace[ia][ik]/pmax * MAXLEV;
         if( pval < 0) pval = 0;
         if( pval >= MAXLEV) pval = MAXLEV - 1;
        
         XFillRectangle( xdisplay, pswin, 
                      whiteGC[pval], ia*PS_FTS, ik*PS_FTS, PS_FTS, PS_FTS);
      }

  XSync( xdisplay, 0);
}

static void draw_tmwin( void)
{
   int ik;
   int w = 4;
 
   for( ik=1; ik < PS_KSTEPS-1; ik++)
      if( Qcol[ik]) 
      {
         XDrawLine( xdisplay, tmwin, redGC[MAXLEV-1],
              PS_FTS - w + PS_FTS/2, ik * PS_FTS + PS_FTS/2,
              PS_FTS + w + PS_FTS/2, ik * PS_FTS + PS_FTS/2);
         XDrawLine( xdisplay, tmwin, redGC[MAXLEV-1],
              PS_FTS + PS_FTS/2, ik * PS_FTS - w + PS_FTS/2,
              PS_FTS + PS_FTS/2, ik * PS_FTS + w + PS_FTS/2);
      }

  XSync( xdisplay, 0);
}

static void report_ipwin( int x)
{
   double F = DF * (sgB + x);

   printf( "F=%.0f ipin=%.3e ACC=%.3e\n", F, ipin[x], ipacc[x]);
}

static void draw_ipwin( void)
{
   int x, k;
   int ipmin = -8, ipmax = 3;
   int ipH = PS_KSTEPS * PS_FTS;
   double v;

   XFillRectangle( xdisplay, ipwin, 
                         whiteGC[0], 0, 0, sgH * IP_FTS, ipH);
   for( x=0; x<sgH; x++)
   {
      v = ipin[ x]; if( v == 0) v = 1e-9; v = log10( v);

      if( v >= ipmin && v < ipmax)
      {
         int y = ipH * (v-ipmin)/(ipmax - ipmin);
         y = ipH - y;
         for( k=0; k<IP_FTS; k++)
            XDrawPoint( xdisplay, ipwin, 
                         whiteGC[MAXLEV-1], x*IP_FTS + k, y);
      }

      v = ipacc[ x]; if( v == 0) v = 1e-9; v = log10( v);

      if( v >= ipmin && v < ipmax)
      {
         int y = ipH * (v-ipmin)/(ipmax - ipmin);
         y = ipH - y;
         for( k=0; k<IP_FTS; k++)
            XDrawPoint( xdisplay, ipwin, 
                         redGC[MAXLEV-1], x*IP_FTS + k, y);
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
         if( ev.xany.window == pswin ||
             ev.xany.window == tmwin)
            plot_dfcurve( ev.xbutton.y/PS_FTS);

         if( ev.xany.window == dfwin ||
             ev.xany.window == edwin ||
             ev.xany.window == sgwin)
         {
            int x = ev.xbutton.x/FTS;
            int y = sgH - ev.xbutton.y/FTS - 1;

            plot_tmcurve( x, y);

            double T = x * DT;
            double F = DF * (sgB + y);

            fprintf( stderr, "T=%.2f F=%.0f ", T, F);

            struct PCA *p = &PRING( xtoBP( x), y);
            fprintf( stderr, "x,y=%d,%d p->val=%.3f ",
                  x, y, p->val);

            fprintf( stderr, "hwid=%d td=%.3f ps=%.2f ", p->hwid, p->td, p->ps);
            fprintf( stderr, "con=%.3f ", p->ps * p->val);
            fprintf( stderr, "a=%.0f\n", 180/M_PI * atan2( p->vy, p->vx));
         }

         if( ev.xany.window == ipwin)
            report_ipwin( ev.xbutton.x/IP_FTS);

         if( ev.xany.window == edwin)
            report_edwin( ev.xbutton.x/FTS, ev.xbutton.y/FTS);
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
         if( ev.xany.window == pswin) draw_pswin( 1);
         if( ev.xany.window == tmwin) draw_tmwin();
         if( ev.xany.window == ipwin) draw_ipwin();
      }
      n++;
   }

   return n;
}

static void display_pause( void)
{
   run = 0;
   while( !run && XFLAG) {service(); XSync( xdisplay, 0); usleep( 10000); }

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
              0, 0, FTS * sgW + 4, FTS * sgH * 3 + PS_FTS * PS_KSTEPS + 22, 
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

   pswin = XCreateSimpleWindow( xdisplay, mainwin, 
                                pswin_L, pswin_T, pswin_W, pswin_H, 
                                1, white_pixel,  black_pixel);

   tmwin = XCreateSimpleWindow( xdisplay, mainwin, 
                                tmwin_L, tmwin_T, tmwin_W, tmwin_H, 
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
      redGC[ i] = XCreateGC( xdisplay, pswin, 0, 0);

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
   XSelectInput( xdisplay, pswin, ExposureMask );
   XSelectInput( xdisplay, tmwin, ExposureMask );
   XSelectInput( xdisplay, sgwin, ExposureMask );
   XSelectInput( xdisplay, edwin, ExposureMask );
   XSelectInput( xdisplay, dfwin, ExposureMask );
   XSelectInput( xdisplay, ipwin, ExposureMask );
   XMapRaised( xdisplay, mainwin);
   XMapRaised( xdisplay, pswin);
   XMapRaised( xdisplay, tmwin);
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

   XSelectInput( xdisplay, pswin, megamask);
   XSelectInput( xdisplay, tmwin, megamask);
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

// Event types
#define RSET_WHISTLER 1
#define RSET_RISER    2

static struct RSET {
   timestamp T;                                              // Event timestamp
   double srcal;                                     // Sample rate calibration
   int type;                                                   // Type of event

   int KJ;                                          // K-index if RSET_WHISTLER
   double pm;                          // Hough cell amplitude if RSET_WHISTLER
  
   double R1, R2;                                 // Riser counts if RSET_RISER
   int frame;
   double bearing;
   double pa;
   double ecc;
   double er;
   double eh;
   int N;
} rset_main;

static void reset_rset( struct RSET *r)
{
   memset( r, 0, sizeof( struct RSET));
}

//
//  Store a PNG thumbnail spectrogram of the event.
//

#define th_W 194        // Thumbnail width, pixels
#define th_H sgH        // Thumbnail height, pixels
#define th_MARGIN 2.0   // Time offset of left margin in STFT, seconds

static void store_rset_thumb( struct RSET *r, time_t stamp)
{
   char fname[200], tname[200];

   sprintf( fname, "%s/%d.png", outdir, (int) stamp);
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

   int xleft = (th_MARGIN - TDELAY)/DT;
   int xend = xleft + th_W;

   int x, y;
   int bmin = 20;

   for( y=th_H-1; y >= 0; y--)
   {
      unsigned char rbuf[th_W * 3], *pr = rbuf;

      for( x=xleft; x<xend; x++)
      {
         int cx = xtoBP( x);

         int dval = bmin + 2 * PRING( cx, y).val * (255 - bmin);
         if( dval > 255) dval = 255;
         if( dval < bmin) dval = bmin;

         int aval = bmin + ARING( cx, y) * (255 - bmin);
         if( aval > 255) aval = 255;
         if( aval < bmin) aval = bmin;

         if( dval > bmin)
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

static void store_rset_txt( struct RSET *r, time_t stamp)
{
   char fname[200];
   char tname[200];

   sprintf( fname, "%s/%d.txt", outdir, (int) stamp);
   sprintf( tname, "%s/new.txt", outdir);

   FILE *fp = fopen( tname, "w");
   if( !fp) VT_bailout( "cannot open %s, %s", tname, strerror( errno));

   char temp[30];  timestamp_string3( VTTIME( r->T), temp);
   fprintf( fp, "T=%s ", temp);

   if( r->type == RSET_WHISTLER)
   {
      fprintf( fp, "K=%.0f pm=%.3f", 
                 K_from_jk( r->KJ), r->pm);
      if( polar_mode)
         fprintf( fp, " B=%.1f pa=%.1f ecc=%.3f er=%.3f eh=%.3e N=%d",
             r->bearing, r->pa, r->ecc, r->er, r->eh, r->N);
   }
   else
   if( r->type == RSET_RISER) fprintf( fp, "R1=%.3f R2=%.3f", r->R1, r->R2);
   
   fprintf( fp, "\n");
   fclose( fp);

   if( rename( tname, fname) < 0) 
      VT_bailout( "cannot rename output file, %s", strerror( errno));
}

//
//  Store the signal.
//

static void store_rset_vt( struct RSET *r, time_t stamp)
{
   char *fname, *tname, *vname;

   if( asprintf( &fname, "%s/%d.vt", outdir, (int) stamp) < 0 ||
       asprintf( &tname, "%s/new.vt", outdir) < 0 ||
       asprintf( &vname, "%s/new.vt,i2", outdir) < 0)
      VT_bailout( "out of memory");

   int chans = 0;
   if( ch_HFIELD1 >= 0) chans++;
   if( ch_HFIELD2 >= 0) chans++;
   if( ch_EFIELD >= 0) chans++;

   VTFILE *vtout = VT_open_output( vname, chans, 0, sample_rate);
   if( !vtout) VT_bailout( "cannot open %s: %s", tname, strerror( errno));

   double frame[3];

   VT_set_timebase( vtout, timestamp_add( timebase, -sgW*DT + DT), srcal);
   int x, i;
   for( x=0; x<sgW; x++)
   {
      for( i=0; i<FFTWID; i++)
      {
         int xp = xtoBP( x);

         switch( chans)
         {
            case 1: frame[0] = ERING( xp)[i];
                    break;
            case 2: frame[0] = HRING1( xp)[i];  
                    frame[1] = HRING2( xp)[i];
                    break;
            case 3: frame[0] = HRING1( xp)[i];
                    frame[1] = HRING2( xp)[i];
                    frame[2] = ERING( xp)[i];
                    break;
         }
   
         VT_insert_frame( vtout, frame);
      }
   }

   VT_release( vtout);
   VT_close( vtout);

   if( rename( tname, fname) < 0) 
      VT_bailout( "cannot rename output file, %s", strerror( errno));

   free( vname); free( tname); free( fname);
}

//
//  Store the result set in the events directory.  Files are named by the
//  integer part of the event timestamp, and will overwrite an earlier
//  one captured within the same second.
//

static void store_rset( struct RSET *r)
{
   time_t stamp = timestamp_secs( VTTIME( r->T));

   mkdir( outdir, 0777);

   store_rset_vt( r, stamp);
   store_rset_thumb( r, stamp);
   store_rset_txt( r, stamp);

   char temp[30];   timestamp_string3( VTTIME( r->T), temp);

   if( r->type == RSET_WHISTLER)
      VT_report( 0, "RSET: %d T=%s K=%.0f pm=%.3f", 
            (int) stamp, temp, K_from_jk( r->KJ), r->pm);
   else
   if( r->type == RSET_RISER)
      VT_report( 0, "RSET: %d T=%s R1=%.3f R2=%.3f", 
            (int) stamp, temp, r->R1, r->R2);
   else
      VT_report( 0, "RSET: %d T=%s type=%d", (int) stamp, temp, r->type);
}

///////////////////////////////////////////////////////////////////////////////
//  Hough Transform                                                          //
///////////////////////////////////////////////////////////////////////////////

static struct CHAIN {
   int alloc;
   int ntlist;
   int flag;
   struct TLIST *tlist;
} *chains, tlmap[PS_KSTEPS];

//
//  Add an entry to the list of transformation points.
//

static int dups = 0;
static void tlist_insert( int jk, int jt, int jf, double qx, double qy)
{
   int i;
   int n = jf * sgW + jt;
   struct CHAIN *ch = chains + n;

   // Only allow angles between defined limits.  qx,qy are the direction
   // cosines of the whistler curve at this point.
  
   if( -qy >= sin( MAX_ANGLE * M_PI/180.0) ||
        qx >= cos( MIN_ANGLE * M_PI/180.0)) return;

   if( !ch->tlist) 
   {
      ch->tlist = VT_malloc( 100 * sizeof( struct TLIST));
      ch->alloc = 100;
   }

   for( i=0; i<ch->ntlist; i++)
   {
      struct TLIST *p = ch->tlist + i;
      if( p->jk == jk &&
          p->jt == jt &&
          p->jf == jf) { dups++; return; }
   }

   if( ch->ntlist == ch->alloc)
   {
      ch->alloc += 100;
      ch->tlist = realloc( ch->tlist, ch->alloc * sizeof( struct TLIST));
      if( !ch->tlist) VT_bailout( "out of memory for tlist");
   }

   ch->tlist[ch->ntlist].jk = jk;
   ch->tlist[ch->ntlist].jt = jt;
   ch->tlist[ch->ntlist].jf = jf;
   ch->tlist[ch->ntlist].qx = qx;
   ch->tlist[ch->ntlist].qy = qy;
   ch->ntlist++;
   ntlist++;
   tlmap[jk].ntlist++;
}


static void setup_tlist( void)
{
   int n, jk, jt, jf;
   int h2 = FMAX/DF - sgB; 
   int h1 = FMIN/DF - sgB;

   n = sgW * sgH;
   chains = VT_malloc_zero( sizeof( struct CHAIN) * n);
   memset( tlmap, 0, sizeof( tlmap));

   ntlist = maxtlist = 0;

   double A = 2 * DT;

   for( jt = TBASE/DT; jt < sgW; jt++)
   {
      double T = jt * DT - TBASE;
//      if( T < A) continue;

      for( jf = h1; jf < h2; jf++)
      {
         double F = (jf +sgB) * DF;

         double a = 1.0 - F/(double) FMAX;
         double b = -2.0 * F/sqrt(FMAX) * (T-A);
         double c = -F * (T-A) * (T-A);

         double k1 = (-b + sqrt( b*b - 4*a*c))/(2*a);
         double K = k1 * k1;
         jk = jk_from_K( K);
         if( jk < 0 || jk >= PS_KSTEPS) continue;
         double B = sqrt( K/FMAX);

         double dfdt = DT * -2 * K/((T+B-A) * (T+B-A) * (T+B-A))/DF;
         double qr = sqrt( 1 + dfdt * dfdt);
         double qx = 1/qr;
         double qy = dfdt/qr;

         tlist_insert( jk, jt, jf, qx, qy);
      }
   }

   VT_report( 0, "ntlist1: %d", ntlist);

   for( jk = 0; jk < PS_KSTEPS; jk++)
   {
      double K = K_from_jk( jk);
      double B = sqrt( K/FMAX);

      for( jt=TBASE/DT; jt<sgW; jt++)
      {
         double T = jt * DT - TBASE;
 //        if( T < A) continue;
         double F = K/((T+B-A) * (T+B-A));

         if( isnan( F)) VT_bailout( "isnan F in setup_tlist");
         jf = F/DF - sgB;
          
         if( jf < h1 || jf >= h2) continue;

         double dfdt = DT * -2 * K/((T+B-A) * (T+B-A) * (T+B-A))/DF;
         double qr = sqrt( 1 + dfdt * dfdt);
         double qx = 1/qr;
         double qy = dfdt/qr;
         tlist_insert( jk, jt, jf, qx, qy);
      }
   }
   VT_report( 0, "ntlist2: %d dups %d", ntlist, dups);

   for( jt = TBASE/DT; jt < sgW; jt++)
   {
      double T = jt * DT - TBASE;
      for( jf = h1; jf < h2; jf++)
      {
         double F = (jf +sgB) * DF;

         for( jk = 0; jk < PS_KSTEPS; jk++)
         {
            double K = K_from_jk( jk);
            double B = sqrt( K/FMAX);

            double A = T + B - sqrt(K/F);
            if( isnan( A)) VT_bailout( "isnan A in setup_tlist");
          
            if( (int)(A/DT) != 2) continue;
 
            double dfdt = DT * -2 * K/((T+B-A) * (T+B-A) * (T+B-A))/DF;
            double qr = sqrt( 1 + dfdt * dfdt);
            double qx = 1/qr;
            double qy = dfdt/qr;

            tlist_insert( jk, jt, jf, qx, qy);
         }
      }
   }

   VT_report( 0, "ntlist3: %d dups %d", ntlist, dups);
   tlist = VT_malloc( sizeof( struct TLIST) * ntlist);
 
   for( jk=0; jk < PS_KSTEPS; jk++)
   {
      tlmap[jk].tlist = VT_malloc( tlmap[jk].ntlist * sizeof( struct TLIST));
      tlmap[jk].alloc = tlmap[jk].ntlist;
      tlmap[jk].ntlist = 0;
   }

   struct TLIST *q = tlist;
   int i, j;
   for( i=0; i<n; i++)
   {
      struct TLIST *p = chains[i].tlist;
      for( j=0; j<chains[i].ntlist; j++, p++)
      {
         memcpy( q++, p, sizeof( struct TLIST));
         struct CHAIN *c = &tlmap[p->jk];
         memcpy( &c->tlist[c->ntlist++], p, sizeof( struct TLIST));
      }
      if( chains[i].tlist) free( chains[i].tlist);
   } 
}

static double run_tlist( struct TLIST *tp, int n)
{
   int i;
   double s = 0;

   for( i=0; i<n; i++, tp++)
   {
      struct PCA *p = &PRING( xtoBP( tp->jt), tp->jf);
      if( p->valid &&
          fabs( p->vx * tp->qx + p->vy * tp->qy) > ATHRESH) s += p->val;
   }

   return s;
}

#define NHIST 100
#define MAXHIST 5.0
#define HINT 1800

static int HFLAG = 0;
static uint64_t ps_cnt = 0;
static time_t ps_time = 0;
static int ps_hist[PS_KSTEPS][NHIST];

static void inline update_histogram( int ik, double v)
{
   if( v <= 0) return;

   int bin = v/MAXHIST * NHIST;
   if( bin >= NHIST) return;

   ps_cnt++;
   
   ps_hist[ik][bin]++;
}

static void dump_histogram( void)
{
   time_t t = timestamp_secs( timebase);

   if( !ps_time)
   {
      ps_time = t;
      return;
   }

   if( t < ps_time + HINT) return;

   ps_time = t;

   int k;
   for( k=0; k<PS_KSTEPS; k++)
   {
      char fname[100];
      sprintf( fname, "/tmp/hist%d", k);

      FILE *f = fopen( fname, "w");
      if( f)
      {
         int j;
         for( j=0; j<NHIST; j++) 
            fprintf( f, "%.3e %.8e\n", j * MAXHIST/NHIST, 
                                      ps_hist[k][j]/(double) ps_cnt);
         fclose( f);
      }   
      else VT_report( 0, "cannot open histogram file, %s", strerror( errno));
   }
}

static void hough_transform( void)
{
   int ik;

   // Scroll the Hough space one column to the left

   for( ik = 1; ik < PS_KSTEPS; ik++)
   {
      Hspace[0][ik] = Hspace[1][ik];
      Hspace[1][ik] = Hspace[2][ik];
   }

   // Run the Hough transform to fill in a new right-hand column

   for( ik = 1; ik < PS_KSTEPS; ik++) // Loop over a range of dispersions
      Hspace[2][ik] = run_tlist( tlmap[ik].tlist, tlmap[ik].ntlist);
      
   if( HFLAG)
   {
      for( ik = 1; ik <= PS_KSTEPS - 1; ik++)
         update_histogram( ik, Hspace[2][ik]);
      dump_histogram();
   }

   // Scan the middle column of the Hough space, looking for peaks.  A peak
   // is detected at a Hough cell if the cell is greater than all eight
   // neighbours, and is above threshold.   Set Qcol[ik].

   for( ik = 1; ik < PS_KSTEPS - 1; ik++)
   {
      double v = Hspace[1][ik];;

      if( v >= PTHRESH &&
          Hspace[1][ik-1] <= v &&
          Hspace[1][ik+1] <= v &&
          Hspace[0][ik-1] <= v &&
          Hspace[2][ik-1] <= v &&
          Hspace[0][ik] <= v &&
          Hspace[2][ik] <= v &&
          Hspace[0][ik+1] <= v &&
          Hspace[2][ik+1] <= v) Qcol[ ik] = 1;   // Peak detected
      else Qcol[ik] = 0; 
   }
}

static void process_polar( struct RSET *r)
{
   int xo = frames - r->frame + 1;
   int i;
   struct TLIST *tp = tlmap[r->KJ].tlist;
   int n = tlmap[r->KJ].ntlist;
   static double *di = NULL, *h;
   static fftw_plan ffp1, ffp2, ffp3;
   static complex double *H1, *H2, *E;

   if( !di)  // First time through?
   {
      di = VT_malloc( sizeof( double) * FFTWID);
      H1 = VT_malloc( sizeof( complex double) * FFTWID);
      H2 = VT_malloc( sizeof( complex double) * FFTWID);
      E = VT_malloc( sizeof( complex double) * FFTWID);
      ffp1 = fftw_plan_dft_r2c_1d( FFTWID, di, H1, FFTW_ESTIMATE);
      ffp2 = fftw_plan_dft_r2c_1d( FFTWID, di, H2, FFTW_ESTIMATE);
      ffp3 = fftw_plan_dft_r2c_1d( FFTWID, di, E, FFTW_ESTIMATE);
   }

   double cos1 = cos( polar1_align);
   double cos2 = cos( polar2_align);
   double sin1 = sin( polar1_align);
   double sin2 = sin( polar2_align);
   double det =  sin1*cos2 - cos1*sin2;

   double bsin = 0;
   double bcos = 0;
   double ai = 0;
   double ar = 0;
   double ecc = 0;
   double er = 0;
   double eh = 0;
   double weight_sum = 0;

   int N = 0; 
   for( i=0; i<n; i++, tp++)
   {
      struct PCA *p = &PRING( xtoBP( tp->jt - xo), tp->jf);
      if( p->valid &&
          fabs( p->vx * tp->qx + p->vy * tp->qy) > ATHRESH
        )
      {
         h = HRING1( xtoBP( tp->jt - xo));
         memcpy( di, h, FFTWID * sizeof( double));
         fftw_execute( ffp1);
         h = HRING2( xtoBP( tp->jt - xo));
         memcpy( di, h, FFTWID * sizeof( double));
         fftw_execute( ffp2);
         h = ERING( xtoBP( tp->jt - xo));
         memcpy( di, h, FFTWID * sizeof( double));
         fftw_execute( ffp3);

         int bin = tp->jf + sgB;

         // Produce N/S and E/W signals
         complex double ew = (cos2 * H1[bin] - cos1 * H2[bin]) * det;

         complex double ns = (-sin2 * H1[bin] + sin1 * H2[bin]) * det;

         double mag_ew = cabs( ew);
         double mag_ns = cabs( ns);
         double pow_ew = mag_ew * mag_ew; // Power, E/W signal
         double pow_ns = mag_ns * mag_ns; // Power, N/S signal

         // Phase angle between N/S and E/W
         double phsin = cimag( ns) * creal( ew) - creal( ns) * cimag( ew);
         double phcos = creal( ns) * creal( ew) + cimag( ns) * cimag( ew);
         double a = atan2( phsin, phcos);

         double bearing2sin = 2 * mag_ew * mag_ns * cos( a);
         double bearing2cos = pow_ns - pow_ew;
         double pwr = pow_ew + pow_ns;

         // double weight = p->val;
         double weight = pwr;

         double bearing180 = atan2( bearing2sin, bearing2cos)/2;
         if( bearing180 < 0) bearing180 += M_PI;
         else
         if( bearing180 >= M_PI) bearing180 -= M_PI;

         //  Oriented signal
         complex double or = ew * sin( bearing180) +
                             ns * cos( bearing180);

         // Perpendicular signal
         complex double pr = ew * sin( bearing180 + M_PI/2) +
                             ns * cos( bearing180 + M_PI/2);

         complex double vr = E[bin];

         // Phase angle between E and H
         double pha =
              atan2( cimag( or) * creal( vr) - creal( or) * cimag( vr),
                     creal( or) * creal( vr) + cimag( or) * cimag( vr));

         double bearing360 = bearing180;

         if( pha < -M_PI/2 || pha > M_PI/2) bearing360 += M_PI;

         bsin += sin( bearing360) * weight;
         bcos += cos( bearing360) * weight;

         // Phase angle between or and pr
         ai += (cimag( or) * creal( pr) - creal( or) * cimag( pr)) * weight;
         ar += (creal( or) * creal( pr) + cimag( or) * cimag( pr)) * weight;

         // Eccentricity
         double A = cabs( or);
         double B = cabs( pr);
         ecc += sqrt( 1 - B*B/(A*A)) * weight;
         er += B / A * weight;
         eh += cabs( vr) / sqrt( A*A + B*B) * weight;
//VT_report( 1, "ecc A %.3e B %.3e ecc %.3e pwr %.3e",
// A, B, sqrt( 1 - B*B/(A*A)), pwr);
         weight_sum += weight;
         N++;
      }
   }

   r->bearing = atan2( bsin, bcos) * 180/M_PI;
   r->pa = atan2( ai, ar) * 180/M_PI;
   r->ecc = ecc/weight_sum;
   r->er = er/weight_sum;
   r->eh = 4 * M_PI * 1e-7 * eh / weight_sum;
   r->N = N;
   VT_report( 0, "polar bearing=%.1f pa=%.1f ecc=%.2f er=%.2f eh=%.2e N=%d",
          r->bearing, r->pa, r->ecc, r->er, r->eh, r->N);

}

static double rlevel1 = 0;
static double rlevel2 = 0;

static void risers( void)
{
   int j;

   rlevel1 = 0;
   for( j = TBASE/DT; j < (TBASE + RRANGE1)/DT; j++)
   {
      int x = xtoBP( j);
      rlevel1 += gring[x];
   }

   rlevel2 = rlevel1;
   for( ; j < (TBASE + RRANGE2)/DT; j++)
   {
      int x = xtoBP( j);
      rlevel2 += gring[x];
   }
}

static void register_events( void)
{
   //
   // Dont register any events until we've been running for 30 seconds, to
   // allow time for moving averages to settle.
   //

   if( NS/sample_rate < 30) return;

   int i;

   if( !rset_main.type || rset_main.type == RSET_WHISTLER)
      for( i = 1; i < PS_KSTEPS - 1; i++)
         if( Qcol[i])
         {
            if( Hspace[1][i] > rset_main.pm)
            {
               rset_main.type = 1;
               rset_main.T = timebase;
               rset_main.srcal = srcal;
               rset_main.KJ = i;
               rset_main.pm = Hspace[1][i];
               rset_main.frame = frames;
            }
   
            #if USE_X11 
            if( XFLAG) 
            {
               draw_tmwin(); 
               plot_dfcurve( rset_main.KJ);
               display_pause();
            }
            #endif
         }

   if( !rset_main.type || rset_main.type == RSET_RISER)
   {
      if( (rlevel1 > RTHRESH1 && rlevel1 > rset_main.R1) || 
          (rlevel2 > RTHRESH2 && rlevel2 > rset_main.R2))
      {
         rset_main.type = 2;
         rset_main.T = timebase;
         rset_main.srcal = srcal;
         rset_main.R1 = rlevel1;
         rset_main.R2 = rlevel2;
         rset_main.frame = frames;
      }
   }

   //  If we have an event and it's now older than TDELAY seconds without
   //  further improvement, save the event.
   
   if( rset_main.type && 
       timestamp_GT( timebase, timestamp_add( rset_main.T, TDELAY)))
   {
      if( !outdir)
      {
         char temp[30]; timestamp_string3( VTTIME( rset_main.T), temp);

         if( rset_main.type == RSET_WHISTLER)
            printf( "RS T=%s D=%.0f pm=%.3f\n", 
                    temp, sqrt( K_from_jk( rset_main.KJ)), rset_main.pm);
         else
         if( rset_main.type == RSET_RISER)
            printf( "RS T=%s R1=%.3f R2=%.3f\n", 
                    temp, rset_main.R1, rset_main.R2);
         fflush( stdout);
      }

      if( polar_mode && rset_main.type == RSET_WHISTLER)
         process_polar( &rset_main);
 
      if( outdir) store_rset( &rset_main);
      reset_rset( &rset_main);

      #if USE_X11
      if( XFLAG) display_pause();
      #endif
   }
}

static void update_spectrogram( void)
{
#if USE_X11
   int y;
#define OFS 20

   XCopyArea( xdisplay, sgwin, sgwin, whiteGC[0],
                 FTS, 0, (sgW-OFS)*FTS, sgH * FTS, 0, 0); 

   int x = xtoBP( sgW - 1 - OFS);
   for( y=0; y<sgH; y++)
   {
      int sy = sgH - y - 1;

      int bmin = 20;
      int pval = bmin + ARING( x, y) * (MAXLEV - bmin);
      if( pval >= MAXLEV) pval = MAXLEV-1;
      if( pval < bmin) pval = bmin;

      XFillRectangle( xdisplay, sgwin, 
                         whiteGC[pval], (sgW-OFS-1)*FTS, sy*FTS, FTS, FTS);
   }

   XCopyArea( xdisplay, edwin, edwin, whiteGC[0],
                 FTS, 0, (sgW-OFS)*FTS, sgH * FTS, 0, 0); 

   for( y=0; y<sgH; y++)
   {
      int sy = sgH - y - 1;

      int bmin = 20;

      int pval = bmin + PRING( x, y).val * 2 * (MAXLEV - bmin);
      if( pval >= MAXLEV) pval = MAXLEV-1;
      if( pval < bmin) pval = bmin;

      XFillRectangle( xdisplay, edwin, 
                         redGC[pval], (sgW-OFS-1)*FTS, sy*FTS, FTS, FTS);
   }
#endif
}

static inline void update_noise_floor( int j, double v)
{
   // Track the noise floor.  Asymmetric time constants in a moving
   // average.

   if( v > ipacc[j])
      ipacc[j] = ipacc[j] * (1-SFAC1) + SFAC1 * v;
   else
      ipacc[j] = ipacc[j] * (1-SFAC2) + SFAC2 * v;
}

static void process_buffer( double *ebuf, double *hbuf1, double *hbuf2)
{
   static complex double *X = NULL;
   static double *Xin;
   static fftw_plan ffp;

   static timestamp last_T = timestamp_ZERO;
   int i, j;

   //
   // First time through, initialise the Fourier transform
   //

   if( !X)
   {
      X = VT_malloc( sizeof( complex double) * FFTWID);
      Xin = VT_malloc( sizeof( double) * FFTWID);
      ffp = fftw_plan_dft_r2c_1d( FFTWID, Xin, X, FFTW_ESTIMATE);
   }

   //
   //  Line up the ring buffer column to be filled by this transform frame.
   //

   frames++;
   BP = (BP + 1) % sgW;


   // Save the raw E-field and prepare it for FFT
   double *tp = ERING( BP);
   for( i=0; i<FFTWID; i++) *tp++ = Xin[i] = ebuf[i];

   if( polar_mode)
   {
      // In polar mode, save the two H-field channels raw.  We don't do
      // anything else with them unless a whistler is detected.
      tp = HRING1( BP);
      for( i=0; i<FFTWID; i++) tp[i] = hbuf1[i];
      tp = HRING2( BP);
      for( i=0; i<FFTWID; i++) tp[i] = hbuf2[i];
   }

   fftw_execute( ffp);     // FFT the E-field

   //
   // Maintain a moving average noise floor power for each bin
   //
   for( j=0; j<sgH; j++)
   {
      int bin = j + sgB;
      double v = cabs( X[bin]) * cabs( X[bin]);

      ipin[j] = v;   // Bin power

      update_noise_floor( j, v);
   }

   //
   // Limit the signal in each bin to logarithmic range gfloor to gtop and
   // scale to range 0..1.   Store the result in the PCA analyser input
   // buffer ARING.
   //
   for( j=0; j<sgH; j++)
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
   // Run hotelling transform to fill in a column of PRING with PCA data
   //
   int x1 = xtoBP( sgW - HWID_MAX - 1);

   double rlevel = 0;
   double a = sqrt( 0.5);
   int h2 = RMAX/DF - sgB; 
   int h1 = RMIN/DF - sgB;

   for( j=1; j<sgH-1; j++)
   {
      struct PCA *p = &PRING( x1, j);
      hotelling( sgW - HWID_MAX - 1, j, p);
      if( p->valid) 
      {
         p->val = ARING( x1, j) * p->ps;

         if( j >= h1 && j < h2)
         {
            double cosa = p->vx * a + p->vy * a;
            if( fabs( cosa) > 0.94) rlevel += p->val;
         }
      }
      else p->val = 0;
   }
   gring[x1] = rlevel;

   if( NP < sgW) NP++;
   else 
   {
      if( XFLAG) update_spectrogram();

      risers();
      hough_transform();
      register_events();
      
      if( timestamp_GT( timebase, timestamp_add( last_T, 600)))
      {
         last_T = timebase;
         char temp[50];
         VT_format_timestamp( temp, timebase);  temp[19] = 0;
         VT_report( 0, "= %s", temp);
      }
      
      #if USE_X11
      if( run && XFLAG) draw_pswin( 1);
      if( run && XFLAG) draw_tmwin();
      #endif
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void usage( void)
{
   fprintf( stderr, 
      "usage:  vtevent [options] -d outdir input\n"
      "\n"
      "options:\n"
      " -v          Increase verbosity\n"
      " -B          Run in background\n"
      " -L name     Specify logfile\n"
      " -X          Use X display\n"
      " -p spec     Specify polar channel assignments\n"
      " -d outdir   Place event files under outdir\n"
   );

   exit( 1);
}

int main(int argc, char *argv[])
{
   VT_init( "vtevent");

   char *bname;
   VTFILE *vtfile;
   int background = 0;
   char *polarspec = NULL;

   while( 1)
   {  
      int c = getopt( argc, argv, "vBXhd:p:L:?");
   
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'h') HFLAG = 1;
      else
      if( c == 'X') XFLAG = 1;
      else
      if( c == 'd') outdir = strdup( optarg);
      else
      if( c == 'p') polarspec = strdup( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( !outdir)
      VT_report( 1, "No output directory given, events will not be stored");

   #if !USE_X11
      if( XFLAG) VT_report( 0, "-X ignored, not compiled with X11");
   #endif

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDIN : 0;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);
   vtfile = VT_open_input( bname);
   if( !vtfile)
      VT_bailout( "cannot open input %s: %s", bname, VT_error);

   sample_rate = VT_get_sample_rate( vtfile);

   VT_init_chanspec( chspec, vtfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chspec->n, sample_rate);

   if( polarspec)
   {
      VT_parse_polarspec( chspec->n, polarspec,
                          &ch_HFIELD1, &polar1_align,
                          &ch_HFIELD2, &polar2_align,
                          &ch_EFIELD);
      if( ch_EFIELD >= 0)
      {
         polar_mode = 1;
         VT_report( 1, "polar mode");
      }
   }
   else ch_EFIELD = 0;

   //
   //  Setup FFT.
   //  rqDF is the requested frequency resolution, DF is the actual resolution.
   //

   int nyquist = sample_rate / 2;
   BINS = nyquist / rqDF;
   DF = nyquist / (double) BINS;

   FFTWID = BINS * 2;
   DT = FFTWID / (double) sample_rate;

   //
   //  Setup spectrogram.  sgB is the lowest bin used of the spectrogram,
   //  sgH is the highest bin.  sgW is the overall spectrogram width.
   // 
   sgB = MINFREQ / DF;
   sgH = (MAXFREQ - MINFREQ) / DF;
   if( sgH > BINS) sgH = BINS;
   sgW = TSPAN / DT; 

   VT_report( 1, "bins=%d DF=%.1f DT=%.3f sgH=%d sgW=%d", 
                     BINS, DF, DT, sgH, sgW);

   aring = VT_malloc_zero( sizeof( double) * sgW * sgH);
   pring = VT_malloc_zero( sizeof( struct PCA) * sgW * sgH);
   gring = VT_malloc_zero( sizeof( double) * sgW);
   ipin = VT_malloc_zero( sizeof( double) * sgH);
   ering = VT_malloc_zero( sizeof( double) * sgW * FFTWID);

   if( polar_mode)
   {
      hring1 = VT_malloc_zero( sizeof( double) * sgW * FFTWID);
      hring2 = VT_malloc_zero( sizeof( double) * sgW * FFTWID);
   }

   #if USE_X11
   if( XFLAG) setup_display( argc, argv);
   #endif

   ipacc = VT_malloc_zero( sizeof( double) * BINS);

   setup_tlist();

   //
   //  Main loop
   //

   double *ebuf = VT_malloc( sizeof( double) * FFTWID);
   double *hbuf1 = VT_malloc( sizeof( double) * FFTWID);
   double *hbuf2 = VT_malloc( sizeof( double) * FFTWID);
   double *inframe;
   int nb = 0;

   while( 1)
   {
      // Capture the timestamp of the first sample of each FT frame, long with
      // the current sample rate calibration.
      if( !nb)
      {
         timebase = VT_get_timestamp( vtfile);
         srcal = VT_get_srcal( vtfile);
      }

      inframe = VT_get_frame( vtfile);
      if( !inframe) break;

      ebuf[nb] = inframe[chspec->map[ch_EFIELD]];
      if( polar_mode)
      {
         hbuf1[nb] = inframe[chspec->map[ch_HFIELD1]];
         hbuf2[nb] = inframe[chspec->map[ch_HFIELD2]];
      }

      if( ++nb < FFTWID) continue;
      nb = 0;

      //
      //  The FT input buffer is full, process the frame
      //

      if( !XFLAG || run)
      {
         process_buffer( ebuf, hbuf1, hbuf2);   NS += FFTWID;
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

