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

#include <forms.h>
#include <fftw3.h>

///////////////////////////////////////////////////////////////////////////////
//  Globals and definitions                                                  // 
///////////////////////////////////////////////////////////////////////////////

static int sample_rate;
static int chans;
static timestamp time_base = timestamp_ZERO;
static int nft = 0;
static int avcnt = 0;
static int ires = 0;
static int iavg = 0;
static int got_eof = 0;
static char *hamming = NULL;

#define MODE_LOG 0
#define MODE_LIN 1

#define OB 8             // Outer border
#define DS 8             // Spacer
#define SH 18           // Scrollbar height
#define TW 18           // Thumbwheel width
#define XH 20           // x-axis height
#define GH 40           // General controls area height
#define YW 50          // y-axis width

static int DH;            // Display height, pixels per channel
static int BH = 18;             // Button height
static int DW;            // Display width
static int CW = 60;             // Side controls width
static int binbase = 0;

static int resize_flag = 0;

static GC redGC, bgndGC, blackGC, whiteGC, greenGC, gridGC, grayGC;
static GC rulerGC;

static int fftwid = 0;
static int bins = 0;
static int nbuf = 0;
static double dF = 0;
static int navg = 5;

#define YA_MAX 20
#define YA_MIN -120

#define G1VAL(A) fl_get_thumbwheel_value( (A)->g1)
#define G2VAL(A) fl_get_thumbwheel_value( (A)->g2)

static struct CHANNEL
{
   int unit;
   int src;      
   double sample;

   FL_OBJECT *dp;       // Canvas
   FL_OBJECT *g1;       // Thumbwheel
   FL_OBJECT *g2;       // Thumbwheel
   FL_OBJECT *xa;       // X axis
   FL_OBJECT *ya;       // Y axis
   FL_OBJECT *lf;       // Frame
   FL_OBJECT *dm;       // Display mode lin/log

   double *buf;

   fftw_plan ffplan;
   complex double *X;

   double *spec;
   double *cache;

   int *ytics;
   int *xtics;

   int mode;    // MODE_LIN or MODE_LOG
}
 *channels;

static void bailout_hook( void)
{
}

///////////////////////////////////////////////////////////////////////////////
//  Display Drawing Functions                                                // 
///////////////////////////////////////////////////////////////////////////////

// 
//  Draw Y-axis ruler
//

static void draw_ya_log( Window w, struct CHANNEL *cp)
{
   double gt = G1VAL( cp);
   double gb = G2VAL( cp);

   int lsize = fl_get_object_lsize( cp->ya);
   int lstyle = fl_get_object_lstyle(cp->ya);
   int iy;

   int ascend, descend;
   int ch = fl_get_char_height( lstyle, lsize, &ascend, &descend);
   // int cw = fl_get_char_width( lstyle, lsize);

   int d;

   void minor_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, 0, iy, YW/8, iy);
   }

   void major_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, 0, iy, YW/4, iy);
      char str[50];
      sprintf( str, "%+ddB", d);
      XDrawImageString(  fl_get_display(), w, rulerGC,
                          YW/4+2, iy+ch/2, str, strlen( str));
      if( iy >= 0 && iy < DH) cp->ytics[iy] = 1;
   }

   double range = gt - gb;

   // Decide major tic intervals
   int u;
   if( range >= 50) u = 20;
   else
   if( range >= 20) u = 10;
   else
   if( range >= 10)  u = 5;
   else u = 1;

   if( DH/range/ch * u < 1.5)
   {
      if( u == 1) u = 5;
      else
      if( u == 5) u = 10;
      else
      if( u == 10) u = 20;
   }

   //  Decide minor tic intervals
   int t = 1;
   while( t < u && DH/range/ch * t <= 0.3)
      if( t == 1) t = 5;
      else
      if( t == 5) t = 10;
      else
      if( t == 10) t = 20;

   for( d=20; d > -140; d--)
   {
      iy = DH * (d - gb)/range;
      iy = DH - iy;
      if( iy < 0 || iy >= DH) continue;

      if( d % u == 0) major_tick();
      else
      if( d % t == 0) minor_tick();
   }
}

static void draw_ya_lin( Window w, struct CHANNEL *cp)
{
   int lsize = fl_get_object_lsize( cp->ya);
   int lstyle = fl_get_object_lstyle(cp->ya);
   int iy;

   int ascend, descend;
   int ch = fl_get_char_height( lstyle, lsize, &ascend, &descend);
   // int cw = fl_get_char_width( lstyle, lsize);

   double g = pow( 10, -G1VAL( cp)/10);

   void minor_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, 0, iy, YW/8, iy);
   }
   void major_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, 0, iy, YW/4, iy);
      if( iy >= 0 && iy < DH) cp->ytics[iy] = 1;
   }


   double vmax = 1/g;
   double vmin = 0;
   double range = (vmax - vmin)/10;

   int64_t K = 1;
   int n = 0;   // Number of decimal places after the point

   while( 1)
      if( range*K > 1.1 && n <= 3 || range*K >= 2)
      {
         // Decide major tic intervals.   Don't let them overlap.
         int u = 2;
         if( range*K > 5 || n > 3) u = 10; 
         else
         if( range*K > 3) u = 5;

         if( g/K * DH/ch*u < 1.5)
         {
            if( u == 2) u = 5;
            else
            if( u == 5) u = 10;
            else u = 20;
         }

         // Decide minor tic intervals, keep at least half a character height
         // apart otherwise they look squashed or are irregular.
         int t = 1;
         if( g/K * DH/ch < 0.5)
         {
            if( u == 10) t = 5;
            else t = u;   // No minor tics
         }

         int d;
         for( d=vmin*K; d <= vmax*K; d++)
         {
            double h = d/(double) K * g;
            iy = DH - h * DH;
            if( iy < 0 || iy >= DH) continue;
    
            if( d % u == 0)
            {
               major_tick();
               char str[50];
               if( n < 4)
                  sprintf( str, "%+.*f", n, d/(double) K);
               else
                  sprintf( str, "%+de-%d", (int)(d/10.0), n-1);
 
               XDrawImageString(  fl_get_display(), w, rulerGC,
                                  YW/4+2, iy+ch/2, str, strlen( str));
            } 
            else 
            if( d % t == 0) minor_tick();
         }
         break;
      }
      else
      {
         K *= 10;
         n++;
      }
}

static void draw_ya( struct CHANNEL *cp)
{
   Window w = FL_ObjWin( cp->ya);

   XFillRectangle( fl_get_display(), w, whiteGC, 0, 0, YW, DH);
   memset( cp->ytics, 0, DH * sizeof( int));

   switch( cp->mode)
   {
      case MODE_LIN: draw_ya_lin( w, cp); break;
      case MODE_LOG: draw_ya_log( w, cp); break;
   }

   XSync( fl_get_display(), 0);
}

static void draw_xa( struct CHANNEL *cp, int x1, int x2)
{
   double Fmin = bins >= DW ? binbase * dF : 0;
   double Fmax = bins >= DW ? (binbase + DW) * dF : bins * dF;
   double Frange = Fmax - Fmin;

   Window w = FL_ObjWin( cp->xa);

   int lsize = fl_get_object_lsize( cp->xa);
   int lstyle = fl_get_object_lstyle(cp->xa);
   int ascend, descend;
   int ch = fl_get_char_height( lstyle, lsize, &ascend, &descend);
   // int cw = fl_get_char_width( lstyle, lsize);

   XFillRectangle( fl_get_display(), w, whiteGC, x1, 0, x2-x1, XH);
   memset( cp->xtics, 0, DW * sizeof( int));

   int jf;
   int ix;
   int a = XH - ch - ch/2;
   int b = (XH - ch)/2;
   int c = XH/2 - 4;

   void minor_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, ix, a, ix, 0);
   }

   void major_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, ix, c, ix, 0);
      cp->xtics[ix] = 1;
   }

   char str[50];
   int K = 10000;

   while( 1)
      if( Frange > 100000)
      {
         int u = 2;
         if( Frange > 210000) u = 10;
         else
         if( Frange > 800000) u = 20; 
         VT_report( 2, "Frange %.3f K=%d u=%d", Frange, K, u);
         for( jf = Fmin/K; jf<= Fmax/K; jf++)
         {
            ix = (jf*K - Fmin) * DW/(Fmax - Fmin);
            if( ix < 0 || ix >= DW) continue;
  
            if( jf % u == 0)
            {
               major_tick();

               if( K >= 1000)
                  sprintf( str, "%dkHz", (jf * K)/1000);
               else
               if( K >= 100)
                  sprintf( str, "%.1fkHz", (jf * K)/1000.0);
               else
                  sprintf( str, "%dHz", jf*K);
               int sl = strlen( str);
               XDrawImageString(  fl_get_display(), w, rulerGC,
                                       ix, XH-b, str, sl);
                                       // ix - cw*sl/2, XH-b, str, sl);
            }
            else
               minor_tick();
         }

         break;
      }
      else
      {
         Frange *= 10;
         K /= 10;
      }
 
   XSync( fl_get_display(), 0);
}

static void draw_trace( struct CHANNEL *cp, int x1, int x2)
{
   int i, ix;

   double gt = G1VAL( cp);
   double gb = G2VAL( cp);

   int is = 0;

   if( bins < DW)
   {
      for( ix=0; ix<DW; ix++)
      {
         double bp = ix * bins/(double) DW;
         int bin = bp;
         double v1 = cp->cache[bin];
         double v2 = cp->cache[bin+1];
         double v = v1 + (bp - bin) * (v2 - v1);

         int iv;
         if( cp->mode == MODE_LOG)
         {
            iv = DH * (log10(v) * 10 - gb)/(gt - gb);
         }
         else
         {
            iv = DH * v / pow( 10, gt/10);
         }

         if( cp->xtics[ix])
            XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), gridGC,
                             ix, DH-1, ix, 0);
         else
         {
            XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), grayGC,
                             ix, DH-1, ix, 0);
            for( i=0; i<DH; i++)
               if( cp->ytics[i])
                  XDrawPoint( fl_get_display(), FL_ObjWin( cp->dp), gridGC,
                              ix, i);
         }
         if( ix != 0)
            XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), greenGC,
                          ix-1, DH-2-is, ix, DH-2-iv);
         is = iv;
      }
   }
   else
   {
      for( ix = 0; ix<DW; ix++)
      {
         double vsum = cp->cache[binbase + ix];

         int iv;
         double h1;
         if( cp->mode == MODE_LOG)
         {
            h1 = (log10(vsum) * 10 - gb)/(gt - gb);
            iv = DH * h1;
         }
         else
         {
            h1 = vsum / pow( 10, gt/10);
            iv = DH * h1;
         }

         if( cp->xtics[ix])
            XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), gridGC,
                             ix, DH-1, ix, 0);
         else
         {
            XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), grayGC,
                             ix, DH-1, ix, 0);
            for( i=0; i<DH; i++)
               if( cp->ytics[i])
                  XDrawPoint( fl_get_display(), FL_ObjWin( cp->dp), gridGC,
                              ix, i);
         }

         if( ix != 0)
            XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), greenGC,
                          ix-1, DH-2-is, ix, DH-2-iv);
 
         is = iv;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Fourier Transform                                                        // 
///////////////////////////////////////////////////////////////////////////////

static void init_channel( struct CHANNEL *cp, int unit, int src)
{
   memset( cp, 0, sizeof( struct CHANNEL));
   cp->unit = unit;
   cp->src = src;
   cp->mode = MODE_LOG;
}

static void setup_channel_fft( struct CHANNEL *cp)
{
   if( cp->ffplan) fftw_destroy_plan( cp->ffplan);
   cp->buf = VT_realloc( cp->buf, fftwid * sizeof( double));
   cp->X = VT_realloc( cp->X, fftwid * sizeof( complex double));
   cp->spec = VT_realloc( cp->spec, bins * sizeof( double));
   cp->cache = VT_realloc( cp->cache, bins * sizeof( double));
   cp->ffplan = fftw_plan_dft_r2c_1d( fftwid, cp->buf, cp->X,
                               FFTW_ESTIMATE | FFTW_DESTROY_INPUT);
}

static double *hwindow = NULL;

static void setup_hamming( void)
{
   hwindow = VT_realloc( hwindow, sizeof( double) * fftwid);

   int i;
   double N = fftwid - 1;

   if( !hamming) hamming = strdup( "rect");

   if( !strcasecmp( hamming, "rect"))
   {
      for( i=0; i<fftwid; i++) hwindow[i] = 1;
   }
   else
   if( !strcasecmp( hamming, "cosine"))
   {
      for( i=0; i<fftwid; i++) hwindow[i] = sin( i/N * M_PI);
   }
   else
   if( !strcasecmp( hamming, "hann"))
   {
      for( i=0; i<fftwid; i++) hwindow[i] = sin( i/N * M_PI) * sin( i/N * M_PI);
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
      for( i=0; i<fftwid; i++) hwindow[i] = 0.54 - 0.46 * cos( i/N * 2 * M_PI);
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
}

static void run_fft( struct CHANNEL *cp)
{
   fftw_execute( cp->ffplan);

   int bin;
   for( bin = 0; bin < bins; bin++)
   {
      double h = cabs( cp->X[bin])/bins;
      cp->spec[bin] += h * h;
   }
}

static int save_trace_visible = 1;

static void save_trace( const char *pathname)
{
   VT_report( 1, "save %s trace to %s", 
             save_trace_visible ? "visible" : "full", pathname);

   struct stat st;

   if( stat( pathname, &st) == 0)
   {
      char *msg;
      if( asprintf( &msg, "%s already exists, overwrite?", pathname) < 0)
         VT_bailout( "out of memory");
      if( !fl_show_question( msg, 1)) { free( msg); return; }
      free( msg);
   }

   FILE *f = fopen( pathname, "w");
   if( !f)
   {
      fl_show_msg( "Cannot write to %s", pathname);
      return;
   }

   int n1, n2;
   if( save_trace_visible)
   {
      n1 = binbase;
      n2 = binbase + DW;
      if( n2 > bins) n2 = bins;
   }
   else
   {
      n1 = 0;
      n2 = bins;
   }

   int i, j;
   for( i=n1; i<n2; i++)
   {
      fprintf( f, "%.5f", i * dF);

      for( j=0; j<chans; j++) fprintf( f, " %.6e", channels[j].cache[i]);
      fputc( '\n', f);
   }
   fclose( f);
}

static void cache_spec( struct CHANNEL *cp)
{
   int bin;
   for( bin = 0; bin < bins; bin++) cp->cache[bin] = cp->spec[bin]/navg;
   memset( cp->spec, 0, sizeof( double) * bins);
}

FL_FORM *form;
FL_OBJECT *formbox;

FL_OBJECT *utcd;         // Stream timestamp
FL_OBJECT *utcd_txt;

FL_OBJECT *ctcd;         // Cursor frequency
FL_OBJECT *ctcd_txt;

FL_OBJECT *clcd;         // Cursor level
FL_OBJECT *clcd_txt;

FL_OBJECT *b_button;

FL_OBJECT *dg_bar;       // Trace scrollbar

FL_OBJECT *tr_group;

FL_OBJECT *br_group;
FL_OBJECT *br_frame;
FL_OBJECT *br_save;
FL_OBJECT *br_plot;

FL_OBJECT *cg_group;
FL_OBJECT *cg_res_display;
FL_OBJECT *cg_res_button_up;
FL_OBJECT *cg_res_button_dn;
FL_OBJECT *cg_res_txt;

FL_OBJECT *cg_avg_display;
FL_OBJECT *cg_avg_button_up;
FL_OBJECT *cg_avg_button_dn;
FL_OBJECT *cg_avg_txt;

FL_OBJECT *cg_win_select;
FL_OBJECT *cg_win_txt;

#define calc_DW(width) (width - OB*2 - CW - YW - DS)
#define calc_DH(height) \
        ((height - OB*2 - GH - XH * chans - SH - BH - BH*2 - DS)/chans)

#define Y_dp(i)  (OB + BH*2 + DS + (i)*(DH+XH))
#define Y_g1(i)  (Y_dp(i) + DH/9)
#define Y_g2(i)  (Y_g1(i) + DH/2)
#define Y_xa(i)  (Y_dp(i) + DH)
#define Y_lf(i)  (Y_dp(i))
#define Y_cg     ((DH+XH) * chans + SH + DS + DS*2 + BH*2 + DS)
#define X_g12    (OB + DW + DS*2 + YW)
#define X_lf     (OB + DW + YW + DS)
#define X_ya     (OB + DW)

static int trace_beginx = 0;
static int trace_beginy = 0;
static int trace_down = 0;

///////////////////////////////////////////////////////////////////////////////
//  Setup                                                                    // 
///////////////////////////////////////////////////////////////////////////////

//
// Set everything up after display setting changes.
//

static void setup( void)
{
   int i;

   while( 1)
   {
      double new_dF = pow( 10, ires/3);
      switch( ires % 3)
      {
         case -1: new_dF /= 2; break;
         case -2: new_dF /= 5; break;
         case 1: new_dF *= 2; break;
         case 2: new_dF *= 5; break;
      }
   
      double new_bins = (sample_rate/new_dF + 0.5)/2;

      if( new_bins > 250000) ires++;
      else
      if( new_bins < 50) ires--;
      else
      {
         binbase *= dF/new_dF;
         dF = new_dF;
         bins = new_bins;
         break;
      } 
   }

   while( 1)
   {
      double avt = pow( 10, iavg/3);
      switch( iavg % 3)
      {
         case -1: avt /= 2; break;
         case -2: avt /= 5; break;
         case 1: avt *= 2; break;
         case 2: avt *= 5; break;
      }

      navg = (int)(avt * dF + 0.5);
      if( navg < 1) iavg++;
      else break;
   }

   fftwid = bins * 2;
   setup_hamming();

   nbuf = 0;

   if( navg < 1) navg = 1;
   if( avcnt >= navg) avcnt = navg - 1;

   VT_report( 1, "ires=%d iavg=%d navg=%d fftwid=%d dF=%.1f bins=%d", 
          ires, iavg, navg, fftwid, dF, fftwid/2);

   for( i=0; i<chans; i++) setup_channel_fft( channels + i);
   int width = form->w, height = form->h;
   char temp[50];

   fl_freeze_form( form);

   VT_report( 2, "setup %d %d", width, height);

   if( dF >= 1.0)
      sprintf( temp, "%.0f Hz", dF);
   else
   if( dF >= 0.1)
      sprintf( temp, "%.1f Hz", dF);
   else
   if( dF >= 0.01)
      sprintf( temp, "%.2f Hz", dF);
   else
      sprintf( temp, "%.3f Hz", dF);
   fl_set_object_label( cg_res_display, temp);

   double t = navg/dF;
   if( t >= 1.0)
      sprintf( temp, "%.0f S", t);
   else
   if( t >= 0.1)
      sprintf( temp, "%.1f S", t);
   else
      sprintf( temp, "%.2f S", t);

   fl_set_object_label( cg_avg_display, temp);

   int new_DW = calc_DW(width);
   int new_DH = calc_DH(height);

   if( new_DW != DW ||
       new_DH != DH)
   {
      DW = new_DW; DH = new_DH;

      for( i=0; i<chans; i++)
      {
         struct CHANNEL *cp = channels + i;
         fl_set_object_geometry( cp->dp, OB, Y_dp(i), DW, DH);
         fl_set_object_geometry( cp->g1, X_g12, Y_g1(i), TW, DH/3);
         fl_set_object_geometry( cp->g2, X_g12, Y_g2(i), TW, DH/3);
         fl_set_object_geometry( cp->xa, OB, Y_xa(i), DW, XH);
         fl_set_object_geometry( cp->ya, X_ya, Y_dp(i), YW, DH);
         fl_set_object_geometry( cp->lf, X_lf, Y_lf(i), CW, DH);
         fl_set_object_geometry( cp->dm, X_ya, Y_dp(i) + DH, YW, BH);

         cp->ytics = VT_realloc( cp->ytics, DH * sizeof( int));
         cp->xtics = VT_realloc( cp->xtics, DW * sizeof( int));
         memset( cp->ytics, 0, DH * sizeof( int));
         memset( cp->xtics, 0, DW * sizeof( int));
      }
  
      fl_set_object_geometry( dg_bar, OB, Y_dp( chans) + DS, DW, SH);
   }

   if( binbase < 0) binbase = 0;
   if( bins > DW)
   {
      if( binbase + DW > bins) binbase = bins - DW;
      fl_set_scrollbar_size( dg_bar, DW/(double) bins);
      fl_set_scrollbar_value( dg_bar, binbase/(double)(bins - DW));
   }
   else
      fl_set_scrollbar_size( dg_bar, 1.0);

   fl_unfreeze_form( form);
   fl_freeze_form( form);
   for( i=0; i<chans; i++)
   {
      draw_xa( channels + i, 0, DW);
      draw_ya( channels + i);
      draw_trace( channels + i, 0, DW);
   }

   fl_unfreeze_form( form);
}

///////////////////////////////////////////////////////////////////////////////
//  Callback Functions                                                       // 
///////////////////////////////////////////////////////////////////////////////

static void cb_res_button( FL_OBJECT *obj, long u)
{
   ires += u;
   setup();
}

static void cb_avg_button( FL_OBJECT *obj, long u)
{
   iavg += u;
   setup();
}

static void cb_g1( FL_OBJECT *obj, long u)
{
   struct CHANNEL *cp = channels + u;
   if( G1VAL(cp) - 2 < G2VAL(cp))
      fl_set_thumbwheel_value( cp->g1, G2VAL(cp) + 2);
   draw_xa( cp, 0, DW);
   draw_ya( cp);
   draw_trace( cp, 0, DW);
}

static void cb_g2( FL_OBJECT *obj, long u)
{
   struct CHANNEL *cp = channels + u;
   if( G2VAL(cp) + 2 > G1VAL(cp))
      fl_set_thumbwheel_value( cp->g2, G1VAL(cp) - 2);
   draw_xa( cp, 0, DW);
   draw_ya( cp);
   draw_trace( cp, 0, DW);
}

static void cb_dm( FL_OBJECT *obj, long u)
{
   struct CHANNEL *cp = channels + u;

   if( !strcmp( fl_get_object_label( cp->dm), "lin"))
   {
      fl_set_object_label( cp->dm, "log");
      fl_activate_object( cp->g2);
      fl_show_object( cp->g2);
      cp->mode = MODE_LOG;
   }
   else
   {
      fl_set_object_label( cp->dm, "lin");
      fl_deactivate_object( cp->g2);
      fl_hide_object( cp->g2);
      cp->mode = MODE_LIN;
   }

   draw_ya( cp);
}

static void cb_bar( FL_OBJECT *obj, long u)
{
   double v = fl_get_scrollbar_value( dg_bar);

   binbase = v * (bins - DW);
   setup();
}

static void cb_win_select( FL_OBJECT *obj, long u)
{
   FL_POPUP_RETURN *p = fl_get_select_item( obj);
   if( hamming) free( hamming);
   hamming = strdup( p->text);
   hamming[0] = tolower( hamming[0]);
   setup();
}

static void cb_fstrace( void *u)
{
   save_trace_visible = !save_trace_visible;
   FD_FSELECTOR *fs = fl_get_fselector_fdstruct();
   if( !save_trace_visible)
      fl_set_object_label( fs->appbutt[0], "Full trace");
   else
      fl_set_object_label( fs->appbutt[0], "Visible trace");
}

static void cb_save( FL_OBJECT *obj, long u)
{
   static const char *fname = NULL;

   if( fl_get_button( br_save))
   {
      fl_use_fselector( 0);
      fl_add_fselector_appbutton(
         save_trace_visible ? "Visible trace" : "Full trace", cb_fstrace, 0);
      const char *selected = fl_show_fselector( "Save trace data to:",
                                                NULL, NULL, fname);
      fl_remove_fselector_appbutton( "Visible trace");
      fl_remove_fselector_appbutton( "Full trace");

      if( selected && selected[0])
      {
         fname = selected;
         save_trace( fname);
      }

      fl_set_button( br_save, 0);
   }
}

FILE *fplot = NULL;

static void cb_plot( FL_OBJECT *obj, long u)
{
   if( !fplot) fplot = popen( "gnuplot > /dev/null 2>&1", "w");
   if( !fplot)
   {
      fl_show_msg( "Cannot run gnuplot: %s", strerror( errno));
      return;
   }

   int maxj = DW;
   if( binbase + maxj > bins) maxj = bins - binbase;

   fprintf( fplot, "set terminal wxt title 'vtspec'\n");
   fprintf( fplot, "set xrange [%.5f:%.5f]\n",
                    binbase * dF, (binbase+maxj)*dF); 
   fprintf( fplot, "set logscale y\n");
   fprintf( fplot, "set style data lines\n");
   fprintf( fplot, "set xlabel 'Frequency, Hz'\n");
   fprintf( fplot, "set ylabel 'Amplitude'\n");

   fprintf( fplot, "plot ");
   int i, j;
   for( i=0; i<chans; i++)
   {
      if( i) fputc( ',', fplot);
      fprintf( fplot, "'-' using 1:2 title 'ch %d'", i+1);
   }
   fputc( '\n', fplot);

   for( i=0; i<chans; i++)
   {
      for( j=0; j<maxj; j++)
      {
         int u = binbase + j;
         fprintf( fplot, "%.5f %.6e\n", u*dF, channels[i].cache[u]);
      }
      fputs( "e\n", fplot);
   }

   fflush( fplot);
   fl_set_button( br_plot, 0);
}

static int trace_expose( FL_OBJECT *ob, Window win, int w, int h, 
                         XEvent *ev, void *d)
{
   struct CHANNEL *cp = (struct CHANNEL *) d;
   VT_report( 2, "trace expose: %d", cp->unit);
   XExposeEvent *e = (XExposeEvent *) ev;
   draw_xa( cp, e->x, e->x + e->width);
   draw_trace( cp, e->x, e->x + e->width);
   return 0;
}

static int xa_expose( FL_OBJECT *ob, Window win, int w, int h, 
                         XEvent *ev, void *d)
{
   struct CHANNEL *cp = (struct CHANNEL *) d;
   VT_report( 2, "xa expose: %d", cp->unit);
   XExposeEvent *e = (XExposeEvent *) ev;
   draw_xa( cp, e->x, e->x + e->width);
   draw_trace( cp, e->x, e->x + e->width);
   return 0;
}

static int ya_expose( FL_OBJECT *ob, Window win, int w, int h, 
                         XEvent *ev, void *d)
{
   struct CHANNEL *cp = (struct CHANNEL *) d;
   VT_report( 2, "ya expose: %d", cp->unit);
   draw_ya( cp);
   return 0;
}

static int trace_press( FL_OBJECT *ob, Window win, int w, int h, 
                        XEvent *ev, void *d)
{
   struct CHANNEL *cp = (struct CHANNEL *) d;

   double gt = G1VAL( cp);
   double gb = G2VAL( cp);

   double y = DH - 1 - ev->xbutton.y;
   y = y * (gt - gb)/DH + gb;

   double F;

   if( bins > DW) F = (ev->xbutton.x + binbase) * dF;
   else F = ev->xbutton.x/(double) DW * bins * dF;
      
   VT_report( 0, "cursor y=%.2fdB F=%.3f", y, F);
   char temp[100];
   sprintf( temp, "%.6f", F); fl_set_object_label( ctcd, temp);
   sprintf( temp, "%.2fdB", y); fl_set_object_label( clcd, temp);

   trace_down = 1;
   trace_beginx = ev->xbutton.x;
   trace_beginy = ev->xbutton.y;

   return 0;
}

static int trace_release( FL_OBJECT *ob, Window win, int w, int h, 
                          XEvent *ev, void *d)
{
   trace_down = 0;
   return 0;
}

static int trace_motion( FL_OBJECT *ob, Window win, int w, int h, 
                         XEvent *ev, void *d)
{
   struct CHANNEL *cp = (struct CHANNEL *) d;

   if( trace_down)
   {
      if( ob == cp->xa || ob == cp->dp)
         binbase -= ev->xmotion.x - trace_beginx;

      int yshift = ev->xmotion.y - trace_beginy;
      if( yshift && (ob == cp->ya || ob == cp->dp))
      {
         double gt = G1VAL( cp);
         double gb = G2VAL( cp);
         double dy = yshift/(double) DH * (gt - gb);
         if( gt + dy < YA_MAX && gb + dy > YA_MIN)
         {
            fl_set_thumbwheel_value( cp->g1, gt+dy);
            fl_set_thumbwheel_value( cp->g2, gb+dy);
         }
      }
      trace_beginx = ev->xmotion.x;
      trace_beginy = ev->xmotion.y;
      setup();
   }

   return 0;
}

static int form_raw_callback( FL_FORM *fm, void *arg)
{
   XEvent *ev = arg;
   if( ev->type == ConfigureNotify)
   {
      VT_report( 1, "form config");
      resize_flag = 1;
   }
   return 0;
}

///////////////////////////////////////////////////////////////////////////////
//  Display Initialisation                                                   // 
///////////////////////////////////////////////////////////////////////////////

//
//  Once-only initialisation of the display.
//

static void init_display( char *name)
{
   redGC = XCreateGC(fl_get_display(), fl_default_window(), 0, 0);
   fl_set_foreground( redGC, FL_RED);

   bgndGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_set_foreground( bgndGC, FL_COL1);

   greenGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_set_foreground( greenGC, FL_GREEN);

   blackGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_set_foreground( blackGC, FL_BLACK);

   whiteGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_set_foreground( whiteGC, FL_WHITE);

   rulerGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_set_foreground( rulerGC, FL_BLUE);
   fl_set_background( rulerGC, FL_WHITE);

   grayGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_mapcolor( FL_FREE_COL1+1, 10, 10, 10);
   fl_set_foreground( grayGC, FL_FREE_COL1+1);

   gridGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_mapcolor( FL_FREE_COL1+2, 0, 0, 180);
   fl_set_foreground( gridGC, FL_FREE_COL1+2);

   int owidth = 680;
   int oheight = SH + GH + BH*2 + DS + 180 * chans;
   DW = calc_DW( owidth);
   DH = calc_DH( oheight);

   form = fl_bgn_form( FL_NO_BOX, owidth, oheight);
   formbox = fl_add_box( FL_UP_BOX, 0, 0, owidth, oheight, "");

   fl_register_raw_callback( form,
                             FL_ALL_EVENT,
                             form_raw_callback);
 
   int i;
   for( i=0; i<chans; i++)
   {
      struct CHANNEL *cp = channels + i;

      cp->dp = fl_add_canvas( FL_NORMAL_CANVAS, OB, Y_dp(i), DW, DH, "");
      fl_add_canvas_handler( cp->dp, Expose, trace_expose, cp);
      fl_add_canvas_handler( cp->dp, ButtonPress, trace_press, cp);
      fl_add_canvas_handler( cp->dp, ButtonRelease, trace_release, cp);
      fl_add_canvas_handler( cp->dp, MotionNotify, trace_motion, cp);

      cp->g1 = fl_add_thumbwheel( FL_VERT_THUMBWHEEL,
                   X_g12, Y_g1(i), TW, DH/3, "" );
      fl_set_object_callback( cp->g1, cb_g1, i);
      fl_set_thumbwheel_bounds( cp->g1, YA_MIN, YA_MAX);
      fl_set_thumbwheel_step( cp->g1, (YA_MIN-YA_MAX)/20.0);
      fl_set_thumbwheel_value( cp->g1, YA_MAX);

      cp->g2 = fl_add_thumbwheel( FL_VERT_THUMBWHEEL,
                   X_g12, Y_g2(i), TW, DH/3, "" );
      fl_set_object_callback( cp->g2, cb_g2, i);
      fl_set_thumbwheel_bounds( cp->g2, YA_MIN, YA_MAX);
      fl_set_thumbwheel_step( cp->g2, (YA_MIN-YA_MAX)/20.0);
      fl_set_thumbwheel_value( cp->g2, YA_MIN);

      cp->xa = fl_add_canvas( FL_NORMAL_CANVAS, OB, Y_xa(i), DW, XH, "");
      fl_add_canvas_handler( cp->xa, Expose, xa_expose, cp);
      fl_add_canvas_handler( cp->xa, ButtonPress, trace_press, cp);
      fl_add_canvas_handler( cp->xa, ButtonRelease, trace_release, cp);
      fl_add_canvas_handler( cp->xa, MotionNotify, trace_motion, cp);

      cp->ya = fl_add_canvas( FL_NORMAL_CANVAS, X_ya, Y_dp(i), YW, DH, "");
      fl_add_canvas_handler( cp->ya, Expose, ya_expose, cp);
      fl_add_canvas_handler( cp->ya, ButtonPress, trace_press, cp);
      fl_add_canvas_handler( cp->ya, ButtonRelease, trace_release, cp);
      fl_add_canvas_handler( cp->ya, MotionNotify, trace_motion, cp);

      cp->dm = fl_add_button( FL_NORMAL_BUTTON,
                              X_ya, Y_dp(i) + DH, YW, BH, "");
      fl_set_object_label( cp->dm, "log");
      fl_set_object_callback( cp->dm, cb_dm, i);

      char temp[50];
      sprintf( temp, "Chan %d", cp->src + 1);
      cp->lf = fl_add_labelframe( FL_ENGRAVED_FRAME, X_lf, Y_lf(i),
                                  CW, DH, temp);
   }

   dg_bar = fl_add_scrollbar( FL_HOR_SCROLLBAR,
                 OB, Y_dp(chans) + DS, DW, SH, "" );
   fl_set_object_callback( dg_bar, cb_bar, 0);
   fl_set_object_boxtype( dg_bar, FL_UP_BOX);

   tr_group = fl_bgn_group();
   int x = OB;

   // Timestamp display
   #define W_utcd 160
   utcd = fl_add_box( FL_DOWN_BOX, x, OB, W_utcd, BH, "");
   fl_set_object_lalign( utcd, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
   fl_set_object_color( utcd, FL_WHITE, FL_BLUE); 
   utcd_txt = fl_add_text( 
              FL_NORMAL_TEXT, x, OB + BH, W_utcd, BH, "Stream Timestamp");
   x += W_utcd + DS;

   // Cursor frequency display
   #define W_ctcd 100
   ctcd = fl_add_box( FL_DOWN_BOX, x, OB, W_ctcd, BH, "");
   fl_set_object_lalign( ctcd, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
   fl_set_object_color( ctcd, FL_WHITE, FL_BLUE); 
   ctcd_txt = fl_add_text( 
              FL_NORMAL_TEXT, x, OB + BH, W_ctcd, BH, "Cursor frequency");
   x += W_ctcd + DS;

   // Cursor level display
   #define W_clcd 70
   clcd = fl_add_box( FL_DOWN_BOX, x, OB, W_clcd, BH, "");
   fl_set_object_lalign( clcd, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
   fl_set_object_color( clcd, FL_WHITE, FL_BLUE); 
   clcd_txt = fl_add_text( 
              FL_NORMAL_TEXT, x, OB + BH, W_clcd, BH, "Cursor level");
   x += W_clcd + DS;

   fl_end_group();
   fl_set_object_resize( tr_group, FL_RESIZE_NONE);
   fl_set_object_gravity( tr_group, FL_NorthWest, FL_NoGravity);

   cg_group = fl_bgn_group();
   x = OB;


   // Resolution control
   #define W_res 60
   cg_res_display = fl_add_box( 
              FL_DOWN_BOX, x, Y_cg, W_res, BH, "");
   fl_set_object_color( cg_res_display, FL_YELLOW, FL_BLUE);
   cg_res_button_up = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_res, Y_cg, BH, BH, "8");
   cg_res_button_dn = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_res + BH, Y_cg, BH, BH, "2");
   fl_set_object_callback( cg_res_button_up, cb_res_button, 1);
   fl_set_object_callback( cg_res_button_dn, cb_res_button, -1);
   cg_res_txt = fl_add_text(
        FL_NORMAL_TEXT, x, Y_cg + BH, W_res, BH, "Resolution" );

   x += W_res + BH * 2 + DS;
 
   // Averaging control
   #define W_avg 60
   cg_avg_display = fl_add_box( 
            FL_DOWN_BOX,  x, Y_cg, W_avg, BH, "");
   fl_set_object_label( cg_avg_display, "Averaging");
   fl_set_object_color( cg_avg_display, FL_YELLOW, FL_BLUE);
   cg_avg_button_up = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_avg, Y_cg, BH, BH, "8");
   cg_avg_button_dn = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_avg + BH, Y_cg, BH, BH, "2");
   fl_set_object_callback( cg_avg_button_up, cb_avg_button, 1);
   fl_set_object_callback( cg_avg_button_dn, cb_avg_button, -1);
   cg_avg_txt = fl_add_text( FL_NORMAL_TEXT, 
           x, Y_cg + BH, W_avg, BH, "Averaging");

   x += W_avg + BH * 2 + DS;

   // Window selector
   #define W_win 100
   cg_win_select = fl_add_select( FL_MENU_SELECT, x, Y_cg, W_win, BH, "");
   fl_add_select_items( cg_win_select,
        "Rect|Hann|Nuttall|Hamming|Blackman|Cosine");
   fl_set_object_callback( cg_win_select, cb_win_select, -1);
   cg_win_txt = fl_add_text( FL_NORMAL_TEXT, 
           x, Y_cg + BH, W_win, BH, "Window");

   x += W_win + BH * 2 + DS;

   fl_end_group();
 
   fl_set_object_resize( cg_group, FL_RESIZE_NONE);
   fl_set_object_gravity( cg_group, FL_SouthWest, FL_NoGravity);

   br_group = fl_bgn_group();

   int brl = X_lf - YW;
   int brt = Y_dp( chans) + DS;
   int brbh = (oheight - OB - brt - DS*2)/3;
   int brbw = (YW + CW)/2 - DS;
   br_frame = fl_add_frame( FL_ENGRAVED_FRAME, brl, brt,
                                  YW + CW, oheight - OB - brt, "");
   brl += DS;
   brt += DS;
   br_save = fl_add_button( FL_PUSH_BUTTON, brl, brt,
                    brbw, brbh, "Save");
   br_plot = fl_add_button( FL_PUSH_BUTTON, brl, brt + brbh,
                    brbw, brbh, "Plot");

   fl_set_object_callback( br_save, cb_save, 0);
   fl_set_object_callback( br_plot, cb_plot, 0);
   fl_end_group();

   fl_set_object_resize( br_group, FL_RESIZE_NONE);
   fl_set_object_gravity( br_group, FL_SouthEast, FL_NoGravity);


   fl_end_form();

   fl_show_form( form, FL_PLACE_FREE, FL_FULLBORDER, name);

   for( i=0; i<chans; i++)
   {
      struct CHANNEL *cp = channels + i;

      XFillRectangle( fl_get_display(), FL_ObjWin( cp->dp),
                      grayGC, 0, 0, DW, DH);
   }
  
   XSync( fl_get_display(), 0);
}

static void usage( void)
{
   fprintf( stderr,
           "usage:   vtspec [options] input\n"
           "\n"
            "  -W window Select window function\n"
            "            -W cosine\n"
            "            -W blackman\n"
            "            -W hamming\n"
            "            -W nuttall\n"
            "            -W hann\n"
            "            -W rect (default)\n"
          );

   exit( 1);
}

static int process_stream( VTFILE *vtfile, struct VT_CHANSPEC *chspec)
{
   if( !vtfile->nfb && !VT_poll( vtfile, 0)) return 0;
   if( got_eof) return 0;
   int e = VT_is_block( vtfile);
   if( e < 0)
   {
      got_eof = 1;
      return 0;
   }

   if( e)
   {
      time_base = VT_get_timestamp( vtfile);
      nft = 0;

      char utcs[100];
      VT_format_timestamp( utcs, time_base);
      fl_set_object_label( utcd, utcs);
   }

   double *frame = VT_get_frame( vtfile);
   nft++;
   int ch;

   for( ch = 0; ch < chans; ch++)
      channels[ch].buf[nbuf] = frame[chspec->map[ch]] * hwindow[nbuf];

   if( ++nbuf == fftwid)
   {
      for( ch = 0; ch < chans; ch++) run_fft( channels + ch);
      
      if( ++avcnt == navg)
      {
         for( ch = 0; ch < chans; ch++)
         {
            cache_spec( channels + ch);
            draw_trace( channels + ch, 0, DW);
         }
         avcnt = 0;
      }
      nbuf = 0;
   }

   return 1;
}

int main( int argc, char *argv[])
{
   VT_init( "vtspec");

   fl_initialize( &argc, argv, "vtspec", 0, 0);

   while( 1)
   {
      int c = getopt( argc, argv, "vW:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'W') hamming = strdup( optarg);
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

   VT_bailout_hook( bailout_hook);

   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   VT_report( 1, "channels: %d, sample_rate: %d", chans, sample_rate);

   channels = VT_malloc( chans * sizeof( struct CHANNEL));
   int ch;
   for( ch = 0; ch < chans; ch++) 
      init_channel( channels + ch, ch, chspec->map[ch]);

   init_display( strcmp( bname, "-") ? bname : "vtspec");

   int nfu = 0;
   DW = DH = 0;
   setup();

   while( 1)
   {
      if( !process_stream( vtfile, chspec))
      {
         // No data available so wait a while
         usleep( 50000);
         nfu = 0;
      }

      if( resize_flag)
      {
         setup();
         resize_flag = 0;
      }

      if( !nfu--)
      {
         fl_check_forms();
         XSync( fl_get_display(), 0);
         nfu = 0.1 * sample_rate; // Poll form events about 10 times per second
      }
   }
}

