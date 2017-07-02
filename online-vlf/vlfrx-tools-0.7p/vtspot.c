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

#define EARTH_RAD 6371.0

#define MING (0.001 * M_PI/180)    // Convergence resolution

static int BFLAG = 0;                           // -b option: calculate bearing
static int DFLAG = 0;                 // -d option: calculate destination point

static double CVLF = 300e3 * 0.9922;            // Propagation velocity, km/sec

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtspot [options] meas1 [meas2 ...] \n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -L name       Specify logfile\n"
       "\n"
       "  -c factor     Velocity factor (default 0.9922)\n"
       "\n"
       "Measurements:\n"
       "\n"
       "    T/location/timestamp[/sigma]        (sigma in seconds)\n"
       "                                (defaults to 100e-6 sigma)\n"
       "    B/location/bearing[/sigma]          (sigma in degrees)\n"
       "                            (defaults to 10 degrees sigma)\n"
       "\n"
       "Utility functions:\n"
       " -b location1 location2         Range and bearing of 2 from 1\n"
       " -d location bearing range_km   Destination from location\n"
     );
   exit( 1);
}

static double constrain( double a, double low, double high)
{
   double r = high - low;
   while( a < low) a += r;
   while( a >= high) a -= r;
   return a;
}

///////////////////////////////////////////////////////////////////////////////
// n-vector Operations                                                       //
///////////////////////////////////////////////////////////////////////////////

typedef double V3[3];
typedef double A3[3][3]; 

static V3 V3north = { 0, 0, 1 };
// static A3 A0 = { { 0, 0, 0}, {0, 0, 0}, {0, 0, 0} };
static A3 AI = { { 1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

static void v3_add( V3 v1, V3 v2, V3 vr)
{
   vr[0] = v1[0] + v2[0];
   vr[1] = v1[1] + v2[1];
   vr[2] = v1[2] + v2[2];
}

#if 0
static void v3_sub( V3 v1, V3 v2, V3 vr)
{
   vr[0] = v1[0] - v2[0];
   vr[1] = v1[1] - v2[1];
   vr[2] = v1[2] - v2[2];
}
#endif

// Dot product
static double v3_dot( V3 v1, V3 v2)
{
   return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

// Cross product, v1 x v2 -> vc
static void v3_cross( V3 v1, V3 v2, V3 vc)
{
   vc[0] = v1[1] * v2[2] - v1[2] * v2[1];
   vc[1] = v1[2] * v2[0] - v1[0] * v2[2];
   vc[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

// Multiply by a scalar
static void v3_mul_scalar( V3 v, double s, V3 r)
{
   int i;
   for( i=0; i<3; i++) r[i] = v[i] * s;
}

// Make into unit vector
static void v3_normalise( V3 v)
{
   double m = sqrt( v3_dot( v, v));
   v3_mul_scalar( v, 1/m, v);
}

// Random normalised vector
static void v3_random( V3 v)
{
   v[0] = rand(); v[1] = rand(); v[2] = rand();
   v3_normalise( v);
}

// Cross product matrix from vector
static void a3_cpmat( V3 v, A3 a)
{
   a[0][0] = 0;     a[0][1] = -v[2];      a[0][2] = v[1];
   a[1][0] = v[2];  a[1][1] = 0;          a[1][2] = -v[0];
   a[2][0] = -v[1]; a[2][1] = v[0];       a[2][2] = 0;
}

static void a3_scalar_mul( A3 a, double s, A3 r)
{
   int i, j;
   for( i=0; i<3; i++) for( j=0; j<3; j++) r[i][j] = a[i][j] * s;
}

#if 0
static void a3_copy( A3 s, A3 d)
{
   memcpy( d, s, sizeof(A3));
}
#endif

static void v3_copy( V3 s, V3 d)
{
   memcpy( d, s, sizeof( V3));
}

// Matrix sum, a + b -> r and r can be the same matrix as a and/or b
static void a3_sum( A3 a, A3 b, A3 r)
{
   int i, j;
   for( i=0; i<3; i++) for( j=0; j<3; j++) r[i][j] = a[i][j] + b[i][j];
}

// Matrix multiplication a x b -> r
static void a3_mul( A3 a, A3 b, A3 r)
{
   int i, j, k;

   for( i=0; i<3; i++)
      for( j=0; j<3; j++)
      {
         r[i][j] = 0;
         for( k=0; k<3; k++) r[i][j] += a[i][k] * b[k][j];
      }
}

#if 0
static double a3_det( A3 X)
{
   return
    X[0][0]*X[1][1]*X[2][2] + X[0][1]*X[1][2]*X[2][0] +
    X[0][2]*X[1][0]*X[2][1] - X[0][2]*X[1][1]*X[2][0] -
    X[0][1]*X[1][0]*X[2][2] - X[0][0]*X[1][2]*X[2][1];
}
#endif

// Compute the unit vector normal to v1 and v2
static void v3_unit_normal_to( V3 v1, V3 v2, V3 vn)
{
   v3_cross( v1, v2, vn);
   v3_normalise( vn);
}

#if 0
static void v3_outer_prod( V3 v1, V3 v2, A3 r)
{
   int i, j;
   for( i=0; i<3; i++) for( j=0; j<3; j++) r[i][j] = v1[i] * v2[j];
}
#endif

static void v3_transform( V3 v, A3 rot, V3 r)
{
   int i, j;

   for( i=0; i<3; i++)
   {
      r[i] = 0;
      for( j=0; j<3; j++) r[i] += rot[i][j] * v[j];
   }
}

// Compute rotation matrix R from unit axis vector v and angle phi
static void a3_rot( V3 v, double phi, A3 r)
{
   A3 k;  a3_cpmat( v, k);
   A3 t;  a3_scalar_mul( k, sin(phi), t);  a3_sum( AI, t, r);
   a3_mul( k, k, t);  a3_scalar_mul( t, 1 - cos(phi), t); a3_sum( r, t, r);
}

///////////////////////////////////////////////////////////////////////////////
// N-vector Calculations                                                     //
///////////////////////////////////////////////////////////////////////////////

static char * v3_string( V3 v, char *s)
{
   static char temp[50];

   if( !s) s = temp;

   double lat = atan2( v[2], sqrt(v[0] * v[0] + v[1] * v[1]));
   double lon = atan2( v[1], v[0]);

   if( lat > M_PI/2)
   {
      lat = M_PI - lat;
      lon += M_PI;
   }
   else
   if( lat < -M_PI/2)
   {
      lat = -M_PI - lat;
      lon += M_PI;
   }

   lon = constrain( lon, -M_PI, M_PI);
   sprintf( s, "%.3f,%.3f", lat*180/M_PI, lon*180/M_PI);
   return s;
}

static void v3_make( double lat, double lon, V3 v)
{
   v[0] = cos( lat) * cos( lon);
   v[1] = cos( lat) * sin( lon);
   v[2] = sin( lat);
}

// Range in km between two points v1 and v2
static double v3_range( V3 v1, V3 v2)
{
   V3 xp; v3_cross( v1, v2, xp);
   return atan2( sqrt( v3_dot( xp, xp)), v3_dot( v1, v2)) * EARTH_RAD;
}

// Bearing of v2 from v1
static double v3_bearing( V3 v1, V3 v2)
{
   V3 x1;  v3_unit_normal_to( V3north, v1, x1);
   V3 x2;  v3_unit_normal_to( v2, v1, x2);

   V3 xp;  v3_cross( x1, x2, xp);
   V3 xpn;  v3_copy( xp, xpn); v3_normalise( xpn);
   return -v3_dot( v1, xpn) * atan2( sqrt( v3_dot( xp, xp)), v3_dot( x1, x2));
}

static void destination_point( V3 vs, double b, double a, V3 vf)
{
   A3 r0;  a3_rot( vs, -b, r0);
   V3 v0;  v3_transform( V3north, r0, v0);
   V3 n0;  v3_unit_normal_to( vs, v0, n0);
   A3 r1;  a3_rot( n0, a, r1);
   v3_transform( vs, r1, vf);
}

static double parse_coord( char *str)
{
   double a;
   char temp[50], *p;

   strcpy( temp, str);
   int sign = 1;

   if( (p = strchr( temp, 'S')) ||
       (p = strchr( temp, 'W')) ||
       (p = strchr( temp, 's')) ||
       (p = strchr( temp, 'w')))
   {
      *p =0;  sign = -sign;
   }

   if( (p = strchr( temp, 'N')) ||
       (p = strchr( temp, 'E')) ||
       (p = strchr( temp, 'n')) ||
       (p = strchr( temp, 'e')))
   {
      *p =0;
   }

   int fd, fm;
   double fs;

   if( sscanf( temp, "%d:%d:%lf", &fd, &fm, &fs) == 3 ||
       sscanf( temp, "%d:%d", &fd, &fm) == 2)
      a = fd + fm/60.0 + fs/3600.0;
   else
   if( sscanf( temp, "%lf", &a) != 1)
      VT_bailout( "bad coordinate [%s]", temp);

   return a * sign * M_PI/180;
}

static struct SPOT {
   char *name;
   V3 v;
}
 *spots = NULL;

static int nspots = 0;

static void alloc_spot( char *name, V3 v)
{
   VT_report( 3, "alloc spot [%s] %s", name, v3_string( v, NULL));

   spots = VT_realloc( spots, (nspots+1) * sizeof( struct SPOT));
   spots[nspots].name = strdup( name);
   v3_copy( v, spots[nspots].v);
   nspots++;
}

static int load_spots_file( char *filename)
{
   FILE *f = fopen( filename, "r");
   if( !f) return 0;

   int lino = 0;

   char temp[500], *p, *q, *s;
   while( fgets( temp, 500, f))
   {
      lino++;

      p = strchr( temp, '\r'); if( p) *p = 0;
      p = strchr( temp, '\n'); if( p) *p = 0;
      p = strchr( temp, ';'); if( p) *p = 0;

      p = temp;
      while( isspace( *p)) p++;
      if( !*p) continue;

      if( isalpha( *p))
      {
         VT_report( 0, "error in %s, line %d", filename, lino);
         continue;
      }

      for( q = p; *q && !isspace( *q); ) q++;
      if( *q) *q++ = 0;
      if( (s = strchr( p, ',')) == NULL)
      {
         VT_report( 0, "error in %s, line %d", filename, lino);
         continue;
      }

      *s++ = 0;
      double lat = parse_coord( p);
      double lon = parse_coord( s);
      V3 v;
      v3_make( lat, lon, v);

      p = q;
      int n = 0;
      while( 1)
      {
         while( isspace( *p)) p++;
         if( !*p) break;
         
         for( q = p; *q && !isspace( *q); ) q++;
         if( *q) *q++ = 0;

         alloc_spot( p, v);
         n++;
         p = q;
      }

      if( !n)
      {
         VT_report( 0, "error in %s, line %d", filename, lino);
         continue;
      }
   }

   fclose( f);
   VT_report( 2, "loaded spots file [%s], %d entries", filename, nspots);
   return 1;
}

static void load_spots( void)
{
   if( nspots) return;

   if( load_spots_file( "./spots")) return;
 
   char *home = getenv( "HOME");
   if( !home) return;

   char *path;
   if( asprintf( &path, "%s/spots", home) < 0) return;
   if( !path) return;
   load_spots_file( path);
   free( path);
}

static int lookup_spot( char *s, V3 v)
{
   int i;

   for( i=0; i<nspots; i++)
      if( !strcasecmp( spots[i].name, s))
      {
         v3_copy( spots[i].v, v);
         return 1;
      }

   return 0;
}

static void parse_latlon( char *s, V3 v)
{
   char temp[150], *p;

   strcpy( temp, s);

   if( isalpha( s[0]))
   {
      if( lookup_spot( s, v)) return;
      VT_bailout( "no definition in spots file for %s", s);
   }

   if( (p = strchr( temp, ',')) == NULL)
       VT_bailout( "bad lat/long [%s]", s);
   *p++ = 0;

   double lat = parse_coord( temp);
   double lon = parse_coord( p);

   v3_make( lat, lon, v);
}

///////////////////////////////////////////////////////////////////////////////
// Input Measurements                                                        //
///////////////////////////////////////////////////////////////////////////////

//
// Table of locations and arrival times, usually TOGAs but could also be
// trigger times.
//

#define MAXAT 20

struct AT {
   V3 v;
   timestamp toga;
   double sigma;
} ats[MAXAT];

static int nats = 0;    // Number of elements of ats[] filled from command line

//
// Table of locations and bearings.
//

#define MAXBR 20
struct BR {
   V3 v;
   double bearing;
   double sigma;
} brs[MAXAT];

static int nbrs = 0;    // Number of elements of brs[] filled from command line

struct ATD {
   V3 v1, v2;
   double atd;
   double sigma;
   double tbase;
   double range;
   V3 vbase;      // Baseline axis
}
 atd[MAXAT*MAXAT];

static int natd = 0;

static char *ident = NULL;
 
static void reset_measurement_set( void)
{
   nats = 0;
   nbrs = 0;
   natd = 0;
   if( ident) { free( ident); ident = NULL; }
}

static void parse_measurement( char *arg)
{
   char *s1 = strdup( arg), *s2, *s3, *s4 = NULL, *s5 = NULL;

   if( s1[1] != '/') VT_bailout( "cannot parse measurement [%s]", arg);
   s2 = s1 + 2;
   s3 = strchr( s2, '/');
   if( s3)
   {
      *s3++ = 0;
      s4 = strchr( s3, '/');
      if( s4)
      {
         *s4++ = 0;
         s5 = strchr( s4, '/');
         if( s5) *s5++ = 0;
      }
   }

   switch( *s1)
   {
      case 'T':  parse_latlon( s2, ats[nats].v);
                 if( !s3) VT_bailout( "missing timestamp in [%s]", arg);
                 ats[nats].toga = VT_parse_timestamp( s3);
                 ats[nats].sigma = s4 ? atof( s4) : 100e-6;
                 nats++;
                 break;

      case 'B':  parse_latlon( s2, brs[nbrs].v);
                 if( !s3) VT_bailout( "missing bearing in [%s]", arg);
                 brs[nbrs].bearing = atof( s3) * M_PI/180;
                 brs[nbrs].sigma = (s4 ? atof( s4) : 5) * M_PI/180;
                 nbrs++;
                 break;

      case 'A':  parse_latlon( s2, atd[natd].v1);
                 if( !s3) VT_bailout( "missing location in [%s]", arg);
                 parse_latlon( s3, atd[natd].v2);
                 if( !s4) VT_bailout( "missing ATD in [%s]", arg);
                 atd[natd].atd = atof( s4);
                 atd[natd].sigma = s5 ? atof( s5) : 100e-6;
                 natd++;
                 break;
                 
      case 'I':  if( ident) free( ident);
                 ident = strdup( s2);
                 break;

      default: VT_bailout( "unknown measurement type [%s]", arg);
   }

   free( s1);
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

//
//  Compute a score for a proposed position 'v' relative to the current
//  measurement set.  The score is measured in units of standard deviations.
//  Each measurement in the set is compared with the value it should have if
//  the source is at location v and the result expressed in standard deviations
//  of that measurement.
//
//  Return value is the worst score of all the measurements in the set.
//

static double score( V3 v)
{
   int i;
   double score_max = 0, s;

   for( i=0; i<natd; i++)
   {
      struct ATD *a = atd + i;
      double t1 = v3_range( v, a->v1)/CVLF;
      double t2 = v3_range( v, a->v2)/CVLF;
      double e = fabs(t1 - t2 - a->atd);
      s = e/a->sigma;
      if( s > score_max) score_max = s;
   }

   for( i=0; i<nbrs; i++)
   {
      struct BR *b = brs + i;
      double e = v3_bearing( b->v, v) - b->bearing;
      if( e > M_PI) e -= 2*M_PI;
      if( e < -M_PI) e += 2*M_PI;
      s = fabs(e) / b->sigma;
      if( s > score_max) score_max = s;
   }

   return score_max;
}

//
//  Output a solution location, v.  'n' indicates which solution (0, 1, ...)
//  if there is more than one solution for the measurement set.  'sc' is the
//  score for the solution.
//

static void output( V3 v, double sc, int n)
{
   timestamp T = timestamp_ZERO;

   // If any arrival times are given in the measurement set, we can calculate
   // the timestamp of the source lightning.   For each arrival time, we
   // use the calculated range to determine the source time, then take the
   // average.   When averaging, we have to take steps to limit the dynamic
   // range of the accumulator and this is done just by removing the integer
   // part of the timestamps.

   if( nats)   // Number of arrival timestamps
   {
      double T_acc = 0;
      timestamp T_int = timestamp_compose( timestamp_secs( ats[0].toga), 0);
      int i;
      for( i=0; i<nats; i++)
         T_acc += timestamp_diff(ats[i].toga, T_int)
                   - v3_range( v, ats[i].v)/CVLF;
      T = timestamp_add( T_int, T_acc / nats);
   }

   // If the measurement set contained I/ident then start the output record
   // with the ident token.

   if( ident) printf( "%s ", ident);

   char temp[30];  timestamp_string6( T, temp);
   printf( "%d %s %s %.3e\n", n, v3_string( v, NULL), temp, sc);
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

//
// Parametric function of an ATD hyperbola.  'param' is the input parameter,
// IS1 and IS2 are the two mirror image points associated with param.
//

static int hyperbola_point( struct ATD *a, double param, V3 IS1, V3 IS2)
{
   double anglediff = a->atd * CVLF/EARTH_RAD;

   double Ra = param + (a->range - a->atd * CVLF)/2/EARTH_RAD;
   double Rb = Ra + anglediff;

   double CA = cos(Ra);
   double CB = cos(Rb);

   V3 Va;  v3_copy( a->v2, Va);
   V3 Vb;  v3_copy( a->v1, Vb);

   double Ax = Va[0], Ay = Va[1], Az = Va[2];
   double Bx = Vb[0], By = Vb[1], Bz = Vb[2];

   V3 VX; v3_cross( Va, Vb, VX);

   double SQRT = sqrt(
             -v3_dot(Va,Va)*CB*CB -v3_dot(Vb,Vb)*CA*CA +2*v3_dot(Va,Vb)*CA*CB
                   + v3_dot( VX,VX));

   // Sometimes, limited numeric precision can give a slight -ve argument to
   // sqrt(), when actually it should be zero or nearly zero.
   if( isnan( SQRT))
   {
      if( param) return 0;
      SQRT = 0;
   }

   A3 Ar = {
              { +Az*Az+Ay*Ay, -Ax*Ay,      -Ax*Az},
              { -Ax*Ay,       Az*Az+Ax*Ax, -Ay*Az} ,
              { -Ax*Az,       -Ay*Az,      Ay*Ay+Ax*Ax}
            
           };

   A3 Br = {
             { Bz*Bz+By*By, -Bx*By,       -Bx*Bz} ,
             { -Bx*By,      +Bz*Bz+Bx*Bx, -By*Bz} ,
             { -Bx*Bz,        -By*Bz,     By*By+Bx*Bx}
           };

   V3 FB;   v3_transform( Vb, Ar, FB);
   V3 FA;   v3_transform( Va, Br, FA);

   v3_mul_scalar( FA, CA, FA);
   v3_mul_scalar( FB, CB, FB);
   V3 F;   v3_add( FA, FB, F);

   v3_mul_scalar( VX, +SQRT, IS1);
   v3_add( IS1, F, IS1);

   v3_mul_scalar( VX, -SQRT, IS2);
   v3_add( IS2, F, IS2);

   v3_normalise( IS1);
   v3_normalise( IS2);
   return 1;
}

//
//  Called with an approximate location 'v' for the source.  converge() uses
//  a simple pattern search algorithm to polish the solution.
//

static double converge( V3 v, double g)
{
   double sc = score( v);
   VT_report( 2, "begin converge with %.3f at %s", sc, v3_string( v, NULL));

   int N = 0;

   //
   // Set up a pair of orthogonal axes, a priori there is no preferred
   // orientation so we just pick something at random.
   //

   V3 vr;   v3_random( vr);
   V3 vx;   v3_unit_normal_to( v, vr, vx);
   V3 vy;   v3_unit_normal_to( v, vx, vy);
 
   while( g >= MING)
   { 
      N++;

      A3 rx1;   a3_rot( vx, +g, rx1);
      A3 ry1;   a3_rot( vy, +g, ry1);
      A3 rx2;   a3_rot( vx, -g, rx2);
      A3 ry2;   a3_rot( vy, -g, ry2);

      //  Evaluate points in orthogonal directions away from the current
      //  point, continue until no further improvement at this scale.

      int n;
      do {
         n = 0;

//       printf( "spot %s 0.01 %d %.5f\n", v3_string( v, NULL),N, sc);
      
         V3 vt;
         double s;

         v3_transform( v, rx1, vt); 
         s = score( vt);
         if( s < sc) { v3_copy( vt, v); sc = s; n++; }
      
         v3_transform( v, rx2, vt); 
         s = score( vt);
         if( s < sc) { v3_copy( vt, v); sc = s; n++; }
      
         v3_transform( v, ry1, vt); 
         s = score( vt);
         if( s < sc) { v3_copy( vt, v); sc = s; n++; }
      
         v3_transform( v, ry2, vt); 
         s = score( vt);
         if( s < sc) { v3_copy( vt, v); sc = s; n++; }
      }
       while( n);

      //  No further improvement so reduce the scale factor
      g *= 0.8;
   }

   VT_report( 2, "converge iterations: %d", N);

   return sc;
}

//
//  Walk along a bearing line is steps of 'ps' radians, looking for the
//  minimum score.  Call converge() to polish the position.  This needs more
//  work because sometimes there will be two possible solutions.
//

static void walk_bearing( double ps)
{
   A3 r0; a3_rot( brs[0].v, -brs[0].bearing, r0);
   V3 v0; v3_transform( V3north, r0, v0);

   V3 n0;  v3_unit_normal_to( brs[0].v, v0, n0);

   double r;
   double sc = 1e99;
   V3 v;
   for( r = 0.01 * M_PI/180; r<2*M_PI - 2*ps; r += ps)
   {
      A3 r1; a3_rot( n0, r, r1);
      V3 vt; v3_transform( brs[0].v, r1, vt);

      double st = score( vt);
      if( st < sc) { v3_copy( vt, v); sc = st; }
   } 

   double s = converge( v, ps);
   output( v, s, 0);
}

//
// Walk along an ATD hyperbola in steps of 'ps' radians.  Find the minimum
// score positions on each side of the ATD baseline and converge each one.
//

static void walk_atd( double ps)
{
   double r;
   double sc1 = 1e99, sc2 = 1e99;
//   double sc1 = 1e99, sc2 = 1e99, st;
//   V3 v1, v2, IS1, IS2;
   int n = 2 * (int)(M_PI/ps);
   int i1 = -1, i2 = -1;
   static V3 *plist = NULL;
   static double *scores = NULL;

   if( !plist)
   {
      plist = VT_malloc( sizeof( V3) * n);
      scores = VT_malloc( sizeof( double) * n);
   }

#if 0
   for( r = 0.01 * M_PI/180; r<M_PI; r += ps)
   {
      hyperbola_point( atd+0, r, IS1, IS2);
      st = score( IS1);
      if( st < sc1) { v3_copy( IS1, v1); sc1 = st; }
      st = score( IS2);
      if( st < sc2) { v3_copy( IS2, v2); sc2 = st; }
   }
#endif

   int i;
   for( i=0; i<n/2; i++)
   {
      r = ps/2 + i * ps;
      if( !hyperbola_point( atd+0, r, plist[n/2 - i], plist[n/2 + i]))
         scores[n/2 - i] = scores[n/2 + i] = 1e99; 
      else
         scores[n/2 - i] = score( plist[n/2 - i]), 
         scores[n/2 + i] = score( plist[n/2 + i]);
   }


   for( i=1; i<n-1; i++)
   {
      if( scores[i] > scores[i-1] ||
          scores[i] > scores[i+1]) continue;

      if( sc1 > sc2 && scores[i] < sc1) { sc1 = scores[i]; i1 = i; }     
      else
      if( sc2 >= sc1 && scores[i] < sc2) { sc2 = scores[i]; i2 = i; }     
   }

//for( i=0; i<n; i++)
//   printf( "i=%3d: %s %.3f\n", i, v3_string( plist[i], NULL), scores[i]);

   if( i1 >= 0)
   {
      double s1 = converge( plist[i1], ps);
      output( plist[i1], s1, 0);
   }
   if( i2 >= 0)
   {
      double s2 = converge( plist[i2], ps);
      output( plist[i2], s2, 1);
   }
}

static void process_measurement_set( void)
{
   int i, j;

   for( i=0; i<nats; i++)
   {
      char temp[30];
      timestamp_string6( ats[i].toga, temp);
      VT_report( 2, "at %s %s (%.6f)",
                      v3_string( ats[i].v, NULL),
                      temp, ats[i].sigma);
   }
   for( i=0; i<nbrs; i++)
      VT_report( 2, "br %s %.1f (%.1f)",
                      v3_string( brs[i].v, NULL),
                      brs[i].bearing * 180/M_PI,
                      brs[i].sigma * 180/M_PI);

   // Take the arrival times ats[] of the measurement set build a list of
   // all the arrival time differences.   If any ATD exceeds the baseline
   // this functions returns without attempting a solution with the other
   // ATDs.  Probably could be a bit more robust than this.

   for( i=0; i< nats - 1; i++)
   {
      for( j=i+1 ; j < nats; j++)
      {
         struct ATD *a = atd + natd;

         v3_copy( ats[i].v, a->v1);
         v3_copy( ats[j].v, a->v2);
         a->atd = timestamp_diff( ats[i].toga, ats[j].toga);
         a->sigma = ats[i].sigma;
         if( ats[j].sigma > a->sigma) a->sigma = ats[j].sigma;
         natd++;
      }
   }

   for( i=0; i<natd; i++)
   {
      struct ATD *a = atd + i;

      a->range = v3_range( a->v1, a->v2);
      a->tbase = a->range/CVLF;

      char temp1[50], temp2[50];
      v3_string( a->v1, temp1);
      v3_string( a->v2, temp2);
      if( fabs(a->atd) > a->tbase)
      {
         VT_report( 1, "ATD out of range %s %s %.6f limit %.6f",
                           temp1, temp2, a->atd, a->tbase);
         return;
      }

      VT_report( 1, "ATD %s %s %.6f", temp1, temp2, a->atd);
      v3_unit_normal_to( a->v1, a->v2, a->vbase);
   }

   if( nats == 0 && nbrs == 2)
   {
      // Special case: intersection of two bearings.  Calculate the exact
      // solutions without iteration.

      // Make 2nd point on each GC by rotating V3north around each point
      A3 r0; a3_rot(  brs[0].v, -brs[0].bearing, r0);
      V3 v0; v3_transform( V3north, r0, v0);
      A3 r1; a3_rot(  brs[1].v, -brs[1].bearing, r1);
      V3 v1; v3_transform( V3north, r1, v1);

      // Define GCs by their normal unit vector
      V3 n0;  v3_unit_normal_to( brs[0].v, v0, n0);
      V3 n1;  v3_unit_normal_to( brs[1].v, v1, n1);

      V3 it1;  v3_unit_normal_to( n0, n1, it1);
      V3 it2;  v3_mul_scalar( it1, -1, it2);

      char temp1[50]; v3_string( it1, temp1);
      char temp2[50]; v3_string( it2, temp2);

      output( it1, 0, 0);
      output( it2, 0, 1);
      return;
   }

   double ps = 1.0 * M_PI/180;

   // If we have any arrival time differences, then pick the first ATD
   // baseline and scan that for approximate solutions.   Otherwise scan
   // the first bearing line of the measurement set.

   if( natd)
      walk_atd( ps);
   else
   if( nbrs)
      walk_bearing( ps);
}

int main( int argc, char *argv[])
{
   VT_init( "vtspot");

   while( 1)
   {
      int c = getopt( argc, argv, "vL:c:bd?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'b') BFLAG = 1;
      else
      if( c == 'c') CVLF = 300e3 * atof( optarg);
      else
      if( c == 'd') DFLAG = 1;
      else
      if( c == -1) break;
      else
         usage();
   }

   load_spots();

   if( BFLAG)
   {
      if( argc - optind != 2) usage();

      V3 v1, v2;
      parse_latlon( argv[optind++], v1);
      parse_latlon( argv[optind++], v2);

      double d = v3_range( v1, v2);
      double b = v3_bearing( v1, v2);
      b = constrain( b, 0, 2*M_PI);
      printf( "%.3f %.1f\n", d, b * 180/M_PI);
      return 0;
   }

   if( DFLAG)
   {
      if( argc - optind != 3) usage();
      V3 vs, vf;
      parse_latlon( argv[optind++], vs);
      double b = atof( argv[optind++]) * M_PI/180;
      double a = atof( argv[optind])/EARTH_RAD;
      destination_point( vs, b, a, vf);
      printf( "%s\n", v3_string( vf, NULL));
      return 0;
   }

   if( optind < argc)
   {
      // Take a measurement set from the command line
      reset_measurement_set();
      while( optind < argc) parse_measurement( argv[optind++]);
      process_measurement_set();
   }
   else
   {
      // Read measurement sets from stdin and process each
      char *inbuf = malloc( 4096), *p, *q;

      while( fgets( inbuf, 4096, stdin))
      {
         if( (p = strchr( inbuf, '\r')) != NULL) *p = 0;
         if( (p = strchr( inbuf, '\n')) != NULL) *p = 0;

         reset_measurement_set();
         p = inbuf;
         while( *p)
         {
            if( isspace( *p)) { p++; continue; }
            for( q = p; *q; q++) if( isspace( *q)) break;
            *q++ = 0; 
            parse_measurement( p);
            p = q;
         }

         process_measurement_set();
      }
   }

   return 0;
}

