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

///////////////////////////////////////////////////////////////////////////////
//  Globals and definitions                                                  // 
///////////////////////////////////////////////////////////////////////////////

// Capture buffer
static double buft = 0;   
static int cbuflen = 0;
static float *cbufc = NULL,         
             *cbufd = NULL;
static int cbufc_p = 0,
           cbufd_p = 0;
static timestamp cbufd_t = timestamp_ZERO;

// Trace buffer
static int sample_rate;
static int chans;
static timestamp time_base = timestamp_ZERO; // Timestamp of current data block
static double srcal = 1.0;
static int nft = 0;                // Number of frames read since time_base set
static int ires = -9;
static int ibuf = 0;
static int capstate = 1;
static int got_eof = 0;

// Display and control dimensions
#define OB 8             // Outer border
#define DS 8             // Spacer
#define SH 18            // Scrollbar height
#define TW 18            // Thumbwheel width
#define XH 20            // x-axis ruler height
#define GH 40            // General controls area height
#define YW 50            // y-axis ruler width

static int DH;            // Display height, pixels per channel
static int BH = 18;       // Button height
static int DW;            // Display width
static int CW = 60;       // Side controls width

static int binbase = 0;

static int resize_flag = 0;

static GC redGC, bgndGC, blackGC, whiteGC, greenGC, grayGC, blueGC, gridGC;
static GC rulerGC, traceGC;

static int bins = 0;
static int nbuf = 0;
static double ddT = 0;     // Display resolution, seconds
static double dT = 0;      // Sample interval, seconds

static double trigthresh = 0.0;
static double DR;

static int tsrc = 0;  // Trigger source index
static int tsig = 1;  // Trigger polarity, 1 = +ve, -1 = -ve

static int tdel = -4;  // Pre-trigger delay index
static double pretrig = 0;   // Pre-trigger delay, seconds

#define YA_MAX 5
#define YA_MIN -1

#define G1VAL(A) pow( 10, fl_get_thumbwheel_value( (A)->g1))
#define G2VAL(A) fl_get_thumbwheel_value( (A)->g2)

#define G2RANGE 2

static struct CHANNEL
{
   int unit;
   int src;      
   double sample;

   FL_OBJECT *dp;       // Trace canvas
   FL_OBJECT *g1;       // Gain thumbwheel
   FL_OBJECT *g2;       // Offset thumbwheel
   FL_OBJECT *xa;       // X axis ruler
   FL_OBJECT *ya;       // Y axis ruler
   FL_OBJECT *lf;       // Frame

   int *ytics;
   int *xtics;
}
 *channels;

static void bailout_hook( void)
{
}

static void format_time_display( double val, char *s)
{
   if( val >= 1.0)
      sprintf( s, "%.0f S", val);
   else
   if( val >= 1e-3)
      sprintf( s, "%.0f mS", 1e3 * val);
   else
   if( val >= 1e-6)
      sprintf( s, "%.0f uS", val * 1e6);
   else
      sprintf( s, "%.0f nS", val * 1e9);
}

///////////////////////////////////////////////////////////////////////////////
//  Display Drawing Functions                                                // 
///////////////////////////////////////////////////////////////////////////////

//
//  Draw Y-axis ruler
//
static void draw_ya( struct CHANNEL *cp)
{
   int iy;

   XFillRectangle( fl_get_display(), FL_ObjWin( cp->ya), 
                   whiteGC, 0, 0, YW, DH);
   memset( cp->ytics, 0, DH * sizeof( int));

   double g = G1VAL( cp);
   double s = G2VAL( cp);

   Window w = FL_ObjWin( cp->ya);

   int lsize = fl_get_object_lsize( cp->ya);
   int lstyle = fl_get_object_lstyle(cp->ya);
   int ascend, descend;
   int ch = fl_get_char_height( lstyle, lsize, &ascend, &descend);
// int cw = fl_get_char_width( lstyle, lsize);

   void minor_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, 0, iy, YW/8, iy);
   }
   void major_tick()
   {
      XDrawLine( fl_get_display(), w, rulerGC, 0, iy, YW/4, iy);
      if( iy >= 0 && iy < DH) cp->ytics[iy] = 1;
   }

   double vmax = (1-s)/g;
   double vmin = (-1-s)/g;
   double range = (vmax - vmin)/10;

   VT_report( 2, "s=%.1f g=%.1f vmax %.2f vmin %.2f range %.2f",
                     s, g, vmax, vmin, range);

   int K = 1;
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
            double h = d/(double) K * g + s;
            iy = -h * DH/2 + DH/2;
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

   XSync( fl_get_display(), 0);
}

//
//  Draw X-axis ruler between pixel positions x1 and x2
//
static void draw_xa( struct CHANNEL *cp, int x1, int x2)
{
   int lsize = fl_get_object_lsize( cp->xa);
   int lstyle = fl_get_object_lstyle(cp->xa);

   int ascend, descend;
   int ch = fl_get_char_height( lstyle, lsize, &ascend, &descend);
// int cw = fl_get_char_width( lstyle, lsize);

   XFillRectangle( fl_get_display(), FL_ObjWin( cp->xa), 
                   whiteGC, x1, 0, x2-x1, XH);
   memset( cp->xtics, 0, DW * sizeof( int));

   int ix, jf;
   double Tmin = binbase * ddT;
   double Tmax = (binbase + DW) * ddT;
   double Trange = Tmax - Tmin;
   Window w = FL_ObjWin( cp->xa);
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

   int K = 10;
   int u;
   while( 1)
   {
      double a = Trange * K;
      if( a >= 50) u = 10;
      else
      if( a >= 20) u = 5;
      else
         u = 2;

      if( a > 10) break;
      K *= 10;
   }

   VT_report( 2, "K=%d a=%.1f", K, Trange * K);

   for( jf = Tmin*K; jf<= Tmax*K; jf++)
   {
      ix = (jf/(double) K - Tmin) * DW/(Tmax - Tmin);
      if( ix < 0 || ix >= DW) continue;

      if( jf % u == 0)
      {
         major_tick();

         char str[50];
         if( K >= 1000000)
            sprintf( str, "%d uS", jf * 1);
         else
         if( K == 100000)
            sprintf( str, "%d uS", jf * 10);
         else
         if( K == 10000)
            sprintf( str, "%d uS", jf * 100);
         else
         if( K == 1000)
            sprintf( str, "%d mS", jf);
         else
         if( K == 100)
            sprintf( str, "%d mS", jf*10);
         else
            sprintf( str, "%d S", jf/10);

         int sl = strlen( str);
         XDrawImageString(  fl_get_display(), w, rulerGC,
                                 ix, XH-b, str, sl);
                                 // ix - cw*sl/2, XH-b, str, sl);
      }
      else
         minor_tick();
   }

   XSync( fl_get_display(), 0);
}

static void draw_trace( struct CHANNEL *cp, int x1, int x2)
{
   int i, ix;

   double g = G1VAL( cp);    // Vertical gain
   double s = G2VAL( cp);    // Vertical shift

   if( DR < 1)
   {
      // More than 1 sample per horizontal pixel.  No fancy de-aliasing here,
      // just scan the samples spanned by this pixel and get the max and min
      // values.  Render as a vertical line from min to max.

      for( ix = x1; ix<x2; ix++)  // For each horizontal pixel
      {
         int j1 = (binbase + ix)/DR;         // First sample
         int j2 = (binbase + ix + 1)/DR;     // Last sample
         double vmin = 1e99, vmax = -1e99;
    
         int j;
         for( j=j1; j<=j2; j++)
         { 
            int up = (cbufd_p + j) % cbuflen;
            double v = cbufd[up * chans + cp->unit];
            if( v > vmax) vmax = v;
            if( v < vmin) vmin = v;
         }

         double h1 = vmax * g + s;
         double h2 = vmin * g + s;
      
         int ih1 = -h1 * DH/2 + DH/2;
         int ih2 = -h2 * DH/2 + DH/2;

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

         XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), traceGC,
                          ix, ih1, ix, ih2);
      }
   }
   else
   {
      // Less than 1 sample per horizontal pixel.  Interpolate between the
      // two bounding samples and draw a sloping line from the previous pixel.
      double h1 = 0;
      for( ix = x1; ix < x2; ix++) // For each horizontal pixel
      {
         double f = (binbase + ix)/DR;
         int u1 = f;  f -= u1;
         int u2 = u1 + 1;

         u1 = (u1 + cbufd_p) % cbuflen;
         u2 = (u2 + cbufd_p) % cbuflen;

         double v1 = cbufd[u1 * chans + cp->unit];
         double v2 = cbufd[u2 * chans + cp->unit];

         double v = v1 + (v2 - v1) * f;

         if( ix == x1)
         {
            h1 = v * g + s;
            continue;
         }

         double h = v * g + s;
         int ih = -h * DH/2 + DH/2;
         int ih1 = -h1 * DH/2 + DH/2;
      
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

         XDrawLine( fl_get_display(), FL_ObjWin( cp->dp), traceGC,
                          ix-1, ih1, ix, ih);
         h1 = h;
      }
   }
}

static void init_channel( struct CHANNEL *cp, int unit, int src)
{
   memset( cp, 0, sizeof( struct CHANNEL));
   cp->unit = unit;
   cp->src = src;
}

static void setup_channel_fft( struct CHANNEL *cp)
{
//   cp->buf = VT_realloc( cp->buf, fftwid * sizeof( double));
//   cp->spec = VT_realloc( cp->spec, bins * sizeof( double));
//   cp->cache = VT_realloc( cp->cache, bins * sizeof( double));
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
      n1 = binbase/DR;
      n2 = (binbase + DW)/DR;
   }
   else
   {
      n1 = 0;
      n2 = cbuflen - 1;
   }

   int i, j;
   for( i=n1; i<n2; i++)
   {
      int u = (i + cbufd_p) % cbuflen;
      timestamp ta = timestamp_add( cbufd_t, i * dT);
      char temp[30];  timestamp_string7( ta, temp);

      fprintf( f, "%s %.8f", temp, i * dT);

      for( j=0; j<chans; j++) fprintf( f, " %.6e", cbufd[u * chans + j]);
      fputc( '\n', f);
   }
   fclose( f);
}

///////////////////////////////////////////////////////////////////////////////
//  Setup                                                                    // 
///////////////////////////////////////////////////////////////////////////////

FL_FORM *form;
FL_OBJECT *formbox;

FL_OBJECT *dg_bar;

FL_OBJECT *br_group;
FL_OBJECT *br_frame;
FL_OBJECT *br_run;
FL_OBJECT *br_arm;
FL_OBJECT *br_save;
FL_OBJECT *br_plot;

FL_OBJECT *tr_group;
FL_OBJECT *utcd;
FL_OBJECT *utcd_txt;
FL_OBJECT *ctcd;
FL_OBJECT *ctcd_txt;

FL_OBJECT *cg_group;

FL_OBJECT *cg_res_display;   // Time resolution display
FL_OBJECT *cg_res_button_up;
FL_OBJECT *cg_res_button_dn;

FL_OBJECT *cg_buf_display;   // Buffer length display
FL_OBJECT *cg_buf_button_up;
FL_OBJECT *cg_buf_button_dn;

FL_OBJECT *cg_trig;         // Trigger level thumbwheel
FL_OBJECT *cg_tsig_button;  // Trigger polarity button
FL_OBJECT *cg_trig_display; // Trigger level display

FL_OBJECT *cg_tsrc_display;   // Trigger source 
FL_OBJECT *cg_tsrc_button_up;
FL_OBJECT *cg_tsrc_button_dn;

FL_OBJECT *cg_tdel_display;   // Pre-trigger
FL_OBJECT *cg_tdel_button_up;
FL_OBJECT *cg_tdel_button_dn;

#define calc_DW(width) (width - OB*2 - CW - YW - DS)
#define calc_DH(height) ((height - OB*2 - GH - XH * chans - SH - BH - BH*2 - DS)/chans)

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

static void revise_pretrigger( void)
{
   while( 1)
   {
      double new_pretrig = pow( 10, tdel/3);
      switch( tdel % 3)
      {
         case -1: new_pretrig /= 2; break;
         case -2: new_pretrig /= 5; break;
         case  1: new_pretrig *= 2; break;
         case  2: new_pretrig *= 5; break;
      }

      if( new_pretrig > 0.9 * buft) tdel--;
      else
      {
         pretrig = new_pretrig;
         break;
      }
   }
 
   char temp[50];

   format_time_display( pretrig, temp);
   fl_set_object_label( cg_tdel_display, temp);
}

static void setup( void)
{
   int i;
//   int bins_changed = 0;
   char temp[50];

   while( 1)
   {
      double new_buft = pow( 10, ibuf/3);
      switch( ibuf % 3)
      {
         case -1: new_buft /= 2; break;
         case -2: new_buft /= 5; break;
         case 1: new_buft *= 2; break;
         case 2: new_buft *= 5; break;
      }

      if( new_buft > 10) { ibuf--; continue; }

      int new_cbuflen = sample_rate * new_buft + 0.5;
      if( new_cbuflen < DW) { ibuf++; continue; }

      if( cbuflen != new_cbuflen)
      {
         buft = new_buft;
         cbuflen = new_cbuflen;

         cbufd = VT_realloc( cbufd, cbuflen * chans * sizeof( float));
         cbufc = VT_realloc( cbufc, cbuflen * chans * sizeof( float));
         cbufc_p = 0;
         cbufd_p = 0;

         format_time_display( buft, temp);
         fl_set_object_label( cg_buf_display, temp);

         revise_pretrigger();
      }
      break;
   }

   while( 1)
   {
      double new_ddT = pow( 10, ires/3);
      switch( ires % 3)
      {
         case -1: new_ddT /= 2; break;
         case -2: new_ddT /= 5; break;
         case 1: new_ddT *= 2; break;
         case 2: new_ddT *= 5; break;
      }
   
      double new_bins = buft/new_ddT + 0.5;
      double new_DR = new_bins/(double) cbuflen;

      if( new_bins > 500000 || new_DR >= 11) ires++;
      else
      if( new_bins < DW) ires--;
      else
      {
         binbase *= ddT/new_ddT;
         ddT = new_ddT;
         bins = new_bins;
         DR = new_DR;
         break;
      } 
   }

   VT_report( 1, "bins=%d ddT=%.5f DR=%.3f", bins, ddT, DR);

   nbuf = 0;

   VT_report( 1, "ires=%d ibuf=%d ", ires, ibuf);

   for( i=0; i<chans; i++) setup_channel_fft( channels + i);
   int width = form->w, height = form->h;

   fl_freeze_form( form);

   VT_report( 2, "setup %d %d", width, height);

   format_time_display( ddT, temp);
   fl_set_object_label( cg_res_display, temp);

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

         cp->ytics = VT_realloc( cp->ytics, DH * sizeof( int));
         cp->xtics = VT_realloc( cp->xtics, DW * sizeof( int));
         memset( cp->ytics, 0, DH * sizeof( int));
         memset( cp->xtics, 0, DW * sizeof( int));
      }
  
      fl_set_object_geometry( dg_bar, OB, Y_dp(chans) + DS, DW, SH);
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

   fl_call_object_callback( cg_trig);
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

static void cb_buf_button( FL_OBJECT *obj, long u)
{
   ibuf += u;
   setup();
}

static void cb_tsig_button( FL_OBJECT *obj, long u)
{
   tsig *= -1;
   fl_call_object_callback( cg_trig);
}

static void cb_tsrc_button( FL_OBJECT *obj, long u)
{
   tsrc += u;

   if( tsrc < 0) tsrc = 0;
   if( tsrc > chans) tsrc = chans;

   char temp[50];

   if( tsrc == chans) sprintf( temp, "UT");
   else sprintf( temp, "CH%d", tsrc+1);

   fl_set_object_label( cg_tsrc_display, temp);
}

static void cb_tdel_button( FL_OBJECT *obj, long u)
{
   tdel += u;
   revise_pretrigger();
}

static void cb_run( FL_OBJECT *obj, long u)
{
   VT_report( 1, "cb_run");
   if( fl_get_button( br_run))
   {
      fl_set_button( br_arm, 0);
      fl_call_object_callback( br_arm);
   }
}

static void cb_arm( FL_OBJECT *obj, long u)
{
   if( fl_get_button( br_arm))
   {
      fl_set_button( br_run, 0);
      fl_call_object_callback( br_run);
   }
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

   int n1 = binbase/DR;
   int n2 = (binbase + DW)/DR;

   fprintf( fplot, "set terminal wxt title 'vtscope'\n");
   fprintf( fplot, "set style data lines\n");
   fprintf( fplot, "set xrange [%.6e:%.6e]\n", n1 * dT, n2 * dT); 
   fprintf( fplot, "set xlabel 'Time, seconds'\n");
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
      for( j=n1; j<n2; j++)
      {
         int u = (j + cbufd_p) % cbuflen;
         fprintf( fplot, "%.8f %.6e\n", j * dT, cbufd[u * chans + i]);
      }
      fputs( "e\n", fplot);
   }

   fflush( fplot);
   fl_set_button( br_plot, 0);
}

static void cb_trig( FL_OBJECT *obj, long u)
{
   trigthresh = fl_get_thumbwheel_value( cg_trig);
   char temp[50];
   sprintf( temp, "%c%.2f", tsig > 0 ? '+' : '-',    trigthresh);
   fl_set_object_label( cg_trig_display, temp);
}

static void cb_g1( FL_OBJECT *obj, long u)
{
   struct CHANNEL *cp = channels + u;
   draw_xa( cp, 0, DW);
   draw_ya( cp);
   draw_trace( cp, 0, DW);
}

static void cb_g2( FL_OBJECT *obj, long u)
{
   struct CHANNEL *cp = channels + u;
   draw_xa( cp, 0, DW);
   draw_ya( cp);
   draw_trace( cp, 0, DW);
}

static void cb_bar( FL_OBJECT *obj, long u)
{
   double v = fl_get_scrollbar_value( dg_bar);

   binbase = v * (bins - DW);
   setup();
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

   double g = G1VAL( cp);
   double s = G2VAL( cp);

   double v = -(2 * (ev->xbutton.y - DH/2)/(double) DH + s)/g;

   timestamp ta = timestamp_add( cbufd_t, (ev->xbutton.x + binbase) * ddT);
   char temp[30];  timestamp_string6( ta, temp);
   VT_report( 0, "cursor v=%.3e Trel=%.3f Tabs=%s", v,
                (ev->xbutton.x + binbase) * ddT, temp);

   char utcs[100];
   VT_format_timestamp( utcs, ta);
   fl_set_object_label( ctcd, utcs);

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
         double g2 = G2VAL( cp);
         double dy = -yshift * 2/(double) DH;
         if( g2 + dy < G2RANGE && g2 + dy > -G2RANGE)
         {
            fl_set_thumbwheel_value( cp->g2, g2+dy);
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

   blueGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_set_foreground( blueGC, FL_BLUE);

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

   traceGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_mapcolor( FL_FREE_COL1+2, 0x0, 0xff, 0x0);
   fl_set_foreground( traceGC, FL_FREE_COL1+2);

   gridGC = XCreateGC(fl_get_display(), fl_default_window(),0,0);
   fl_mapcolor( FL_FREE_COL1+2, 0, 0, 180);
   fl_set_foreground( gridGC, FL_FREE_COL1+2);

   int owidth = 720;
   int oheight = SH + GH + BH*2 + DS + 180 * chans;
   DW = calc_DW( owidth);
   DH = calc_DH( oheight);

   form = fl_bgn_form( FL_NO_BOX, owidth, oheight);
   formbox = fl_add_box( FL_UP_BOX, 0, 0, owidth, oheight, "");

   fl_register_raw_callback( form, FL_ALL_EVENT, form_raw_callback);
 
   int i;
   for( i=0; i<chans; i++)
   {
      struct CHANNEL *cp = channels + i;

      cp->dp = 
        fl_add_canvas( FL_NORMAL_CANVAS, OB, Y_dp(i), DW, DH, "");

      fl_add_canvas_handler( cp->dp, Expose, trace_expose, cp);
      fl_add_canvas_handler( cp->dp, ButtonPress, trace_press, cp);
      fl_add_canvas_handler( cp->dp, ButtonRelease, trace_release, cp);
      fl_add_canvas_handler( cp->dp, MotionNotify, trace_motion, cp);

      cp->g1 = fl_add_thumbwheel( FL_VERT_THUMBWHEEL,
                   X_g12, Y_g1(i), TW, DH/3, "" );
      fl_set_object_callback( cp->g1, cb_g1, i);
      fl_set_thumbwheel_bounds( cp->g1, YA_MIN, YA_MAX);
      fl_set_thumbwheel_step( cp->g1, (YA_MAX-YA_MIN)/40.0);
      fl_set_thumbwheel_value( cp->g1, 0);

      cp->g2 = fl_add_thumbwheel( FL_VERT_THUMBWHEEL,
                   X_g12, Y_g2(i), TW, DH/3, "" );
      fl_set_object_callback( cp->g2, cb_g2, i);
      fl_set_thumbwheel_bounds( cp->g2, -G2RANGE, G2RANGE);
      fl_set_thumbwheel_step( cp->g2, G2RANGE/20.0);
      fl_set_thumbwheel_value( cp->g2, 0.0);

      cp->xa = 
        fl_add_canvas( FL_NORMAL_CANVAS, OB, Y_xa(i), DW, XH, "");
      fl_add_canvas_handler( cp->xa, Expose, xa_expose, cp);
      fl_add_canvas_handler( cp->xa, ButtonPress, trace_press, cp);
      fl_add_canvas_handler( cp->xa, ButtonRelease, trace_release, cp);
      fl_add_canvas_handler( cp->xa, MotionNotify, trace_motion, cp);

      cp->ya = 
        fl_add_canvas( FL_NORMAL_CANVAS, X_ya, Y_dp(i), YW, DH, "");
      fl_add_canvas_handler( cp->ya, Expose, ya_expose, cp);
      fl_add_canvas_handler( cp->ya, ButtonPress, trace_press, cp);
      fl_add_canvas_handler( cp->ya, ButtonRelease, trace_release, cp);
      fl_add_canvas_handler( cp->ya, MotionNotify, trace_motion, cp);

      char temp[50];
      sprintf( temp, "Chan %d", cp->src + 1);
      cp->lf = fl_add_labelframe( FL_ENGRAVED_FRAME, X_lf, Y_lf(i),
                                  CW, DH, temp);
   }

   dg_bar = fl_add_scrollbar( FL_HOR_SCROLLBAR,
                 OB, Y_dp(chans) + DS, DW, SH, "" );
   fl_set_object_callback( dg_bar, cb_bar, 0);
   fl_set_object_boxtype( dg_bar, FL_UP_BOX);

   // Objects above the scope trace display: assigned to tr_group for
   // gravity behaviour

   tr_group = fl_bgn_group();

   int x = OB;

   // Timestamp display
   #define W_utcd 160
   utcd = fl_add_box( FL_DOWN_BOX, x, OB, W_utcd, BH, "");
   fl_set_object_lalign( utcd,FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
   fl_set_object_color( utcd, FL_WHITE, FL_BLUE); 
   utcd_txt = fl_add_text( 
              FL_NORMAL_TEXT, x, OB + BH, W_utcd, BH, "Stream timestamp");

   x += W_utcd + DS;

   ctcd = fl_add_box( FL_DOWN_BOX, x, OB, W_utcd, BH, "");
   fl_set_object_lalign( ctcd,FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
   fl_set_object_color( ctcd, FL_WHITE, FL_BLUE); 
   ctcd_txt = fl_add_text( 
              FL_NORMAL_TEXT, x, OB + BH, W_utcd, BH, "Cursor timestamp");

   x += W_utcd + DS;

   fl_end_group();

   fl_set_object_resize( tr_group, FL_RESIZE_NONE);
   fl_set_object_gravity( tr_group, FL_NorthWest, FL_NoGravity);

   // Objects below the scope trace displays: assigned to cg_group for
   // gravity behaviour

   cg_group = fl_bgn_group();
   x = OB;

   // Resolution control
   #define W_res 60
   cg_res_display = fl_add_box( FL_DOWN_BOX, x, Y_cg, W_res, BH, "");
   fl_set_object_color( cg_res_display, FL_YELLOW, FL_BLUE);
   cg_res_button_up = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_res, Y_cg, BH, BH, "8");
   cg_res_button_dn = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_res + BH, Y_cg, BH, BH, "2");
   fl_set_object_callback( cg_res_button_up, cb_res_button, 1);
   fl_set_object_callback( cg_res_button_dn, cb_res_button, -1);
   fl_add_text( FL_NORMAL_TEXT, x, Y_cg + BH, W_res, BH, "Resolution" );

   x += W_res + BH * 2 + DS;
 
   // Buffer length control
   #define W_len 66
   cg_buf_display = fl_add_box( 
            FL_DOWN_BOX,  x, Y_cg, W_len, BH, "");
   fl_set_object_label( cg_buf_display, "Averaging");
   fl_set_object_color( cg_buf_display, FL_YELLOW, FL_BLUE);
   cg_buf_button_up = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_len, Y_cg, BH, BH, "8");
   cg_buf_button_dn = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + W_len + BH, Y_cg, BH, BH, "2");
   fl_set_object_callback( cg_buf_button_up, cb_buf_button, 1);
   fl_set_object_callback( cg_buf_button_dn, cb_buf_button, -1);
   fl_add_text( FL_NORMAL_TEXT, x, Y_cg + BH, W_len, BH, "Capture Len");

   x += W_len + BH * 2 + DS;

   #define W_trig (TW + DS*2)
   cg_trig = fl_add_thumbwheel( FL_VERT_THUMBWHEEL,
                   x, Y_cg, TW, BH*2, "" );
   fl_set_object_callback( cg_trig, cb_trig, 0);
   fl_set_thumbwheel_bounds( cg_trig, 0, 1);
   fl_set_thumbwheel_step( cg_trig, 1/50.0);
   fl_set_thumbwheel_value( cg_trig, 0);

   x += W_trig;

   cg_tsig_button = fl_add_button( FL_NORMAL_BUTTON, x, Y_cg, 26, BH, "+/-");
   fl_set_object_callback( cg_tsig_button, cb_tsig_button, -1);
   x += 20 + DS;
   cg_trig_display = fl_add_box( FL_DOWN_BOX, x, Y_cg, W_res, BH, "");
   fl_set_object_color( cg_trig_display, FL_YELLOW, FL_BLUE);
   fl_add_text( FL_NORMAL_TEXT, x, Y_cg + BH, W_res, BH, "Trig Level");

   x += W_res + DS;
   cg_tsrc_display = fl_add_box( FL_DOWN_BOX, x, Y_cg, W_res, BH, "");
   fl_set_object_color( cg_tsrc_display, FL_YELLOW, FL_BLUE);
   fl_add_text( FL_NORMAL_TEXT, x, Y_cg + BH, W_res, BH, "Trig Src");

   x += W_res;
   cg_tsrc_button_up = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x, Y_cg, BH, BH, "8");
   cg_tsrc_button_dn = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + BH, Y_cg, BH, BH, "2");
   fl_set_object_callback( cg_tsrc_button_up, cb_tsrc_button, -1);
   fl_set_object_callback( cg_tsrc_button_dn, cb_tsrc_button, 1);
   fl_call_object_callback( cg_tsrc_button_up); 

   x += W_res + DS;
   cg_tdel_display = fl_add_box( FL_DOWN_BOX, x, Y_cg, W_res, BH, "");
   fl_set_object_color( cg_tdel_display, FL_YELLOW, FL_BLUE);
   fl_add_text( FL_NORMAL_TEXT, x, Y_cg + BH, W_res, BH, "Pre-trig");

   x += W_res;
   cg_tdel_button_up = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x, Y_cg, BH, BH, "8");
   cg_tdel_button_dn = fl_add_scrollbutton( FL_TOUCH_BUTTON,
               x + BH, Y_cg, BH, BH, "2");
   fl_set_object_callback( cg_tdel_button_up, cb_tdel_button, 1);
   fl_set_object_callback( cg_tdel_button_dn, cb_tdel_button, -1);

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
   br_run = fl_add_lightbutton( FL_PUSH_BUTTON, brl, brt,
                                brbw, brbh, "Run");
   br_arm = fl_add_lightbutton( FL_PUSH_BUTTON, brl, brt + brbh,
                                brbw, brbh, "Arm");
   brl += brbw;
   br_save = fl_add_button( FL_PUSH_BUTTON, brl, brt, brbw, brbh, "Save");
   br_plot = fl_add_button( FL_PUSH_BUTTON, brl, brt + brbh,
                            brbw, brbh, "Plot");

   fl_set_object_callback( br_run, cb_run, 0);
   fl_set_object_callback( br_arm, cb_arm, 0);
   fl_set_object_callback( br_save, cb_save, 0);
   fl_set_object_callback( br_plot, cb_plot, 0);
   fl_set_button( br_run, 1);
   fl_end_group();

   fl_set_object_resize( br_group, FL_RESIZE_NONE);
   fl_set_object_gravity( br_group, FL_SouthEast, FL_NoGravity);

   fl_end_form();

   fl_show_form( form, FL_PLACE_FREE, FL_FULLBORDER, name);

   for( i=0; i<chans; i++)
   {
      struct CHANNEL *cp = channels + i;

      XFillRectangle( fl_get_display(), FL_ObjWin( cp->dp), grayGC, 0, 0, DW, DH);
   }
  
   XSync( fl_get_display(), 0);
}

static void usage( void)
{
   fprintf( stderr, "usage:   vtscope [options] buffer\n"
          );

   exit( 1);
}

///////////////////////////////////////////////////////////////////////////////
//  Processing                                                               //
///////////////////////////////////////////////////////////////////////////////

static void capture_and_draw( void)
{
   fl_set_button( br_arm, 0);
   fl_call_object_callback( br_arm);

   // Exchange capture buffer (cbufc) and display buffer (cbufd)
   // Note the timestamp of the buffer just captured

   float *tmpf = cbufc;  cbufc = cbufd; cbufd = tmpf;
   int tmpi = cbufc_p;  cbufc_p = cbufd_p; cbufd_p = tmpi;
   cbufd_t = timestamp_add( time_base,
                            (nft - nbuf)/(double)(sample_rate * srcal));
   cbufd_p = (cbufd_p + cbuflen - nbuf) % cbuflen;

   nbuf = 0;   // Empty the buffer to begin next capture

   VT_report( 2, "capture complete");

   int ch;
   for( ch = 0; ch < chans; ch++) draw_trace( channels + ch, 0, DW);
}

static void eval_trigger( void)
{
   if( tsrc < chans)   // Trig source set to one of the channels?
   {
      int xp = (cbufc_p + (int)(pretrig*sample_rate)) % cbuflen;

      double val = cbufc[xp*chans + tsrc];
      if( tsig > 0 && val >= trigthresh ||
          tsig < 0 && val <= -trigthresh) capture_and_draw();
   }
   else
   {
      // Trigger as the timestamp at the pretrigger point crosses the
      // second mark.

      double t = timestamp_frac(
                    timestamp_add( time_base,
                        (nft - nbuf)/(double)(sample_rate * srcal) + pretrig));
      if( t < 1 && t + dT * 1.5 >= 1) capture_and_draw();
   }
}

static int process_stream( VTFILE *vtfile, struct VT_CHANSPEC *chspec)
{
   if( !vtfile->nfb && !VT_poll( vtfile, 0)) return 0;
   if( got_eof) return 0;

   int e = VT_is_block( vtfile);
   if( e < 0)
   {
      got_eof = 1;
      if( capstate == 1) eval_trigger();
      return 0;
   }

   if( e)   
   {
      time_base = VT_get_timestamp( vtfile);
      srcal = VT_get_srcal( vtfile);
      nft = 0;
      char utcs[100];
      VT_format_timestamp( utcs, time_base);

      fl_set_object_label( utcd, utcs);
   }
   else nft++;

   double *frame = VT_get_frame( vtfile);

   int ch;

   // Signal runs continuously into the circular capture buffer.

   for( ch = 0; ch < chans; ch++)
         cbufc[cbufc_p * chans + ch] = frame[chspec->map[ch]];
   cbufc_p = (cbufc_p + 1) % cbuflen;

   if( nbuf < cbuflen) nbuf++;  // Capture buffer not yet full?
   else
   if( capstate == 1)   // Trigger enabled?
      eval_trigger();

   return 1;
}

int main( int argc, char *argv[])
{
   VT_init( "vtscope");

   fl_initialize( &argc, argv, "vtscope", 0, 0);

   while( 1)
   {
      int c = getopt( argc, argv, "v?");

      if( c == 'v') VT_up_loglevel();
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
   dT = 1.0/sample_rate;
   VT_report( 1, "channels: %d, sample_rate: %d", chans, sample_rate);

   channels = VT_malloc( chans * sizeof( struct CHANNEL));
   int ch;
   for( ch = 0; ch < chans; ch++) 
      init_channel( channels + ch, ch, chspec->map[ch]);

   init_display( strcmp( bname, "-") ? bname : "vtscope");

   got_eof = 0;     // Set to 1 when input stream ends
   capstate = 1;    // 1 = trigger enabled, 0 = trigger off
   DW = DH = 0;

   setup();

   int nfu = 0;     // Countdown to display service call

   while( 1)
   {
      if( !process_stream( vtfile, chspec))
      {
         // No data available so wait a while
         usleep( 50000);
         nfu = 0;
      }

      capstate = fl_get_button( br_run) || fl_get_button( br_arm) ? 1 : 0;

      if( resize_flag)
      {
         setup();
         resize_flag = 0;
      }

      // Service display about 10 times per second
      if( !nfu--)
      {
         fl_check_forms();
         XSync( fl_get_display(), 0);
         nfu = 0.1 * sample_rate;
      }
   }
}

