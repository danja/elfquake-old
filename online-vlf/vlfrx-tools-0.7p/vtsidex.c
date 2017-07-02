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

static char *datadir = NULL;

static timestamp TS = timestamp_ZERO,
                 TE = timestamp_ZERO; // Start & end timestamps, from -T option

static double FS = 0,
              FE = 0;              // Start and end frequencies, from -F option

static timestamp Tstart = timestamp_ZERO;     // Reference time when using -otr

struct VTSID_FILE
{
   char *filename;
   FILE *f;
   ino_t inode;

   int dp;     // Offset of start of data
   int count;   // Count of records
   struct VTSID_HDR hdr;
   struct VTSID_FIELD *fields;

   struct VTSID_DATA *d;
   int loaded;
};

// Output format controls
static int OFLAG_TE = 0;   // Set by -ote: Use unix epoch for timestamps
static int OFLAG_TR = 0;   // Set by -otr: Output seconds offset
static int OFLAG_HDR = 0;  // Output headings
static int OFLAG_TBR = 0;  // Blank lines on timing breaks
static int hdr_interval = 0;
static int output_limit = 0;

// Averaging and hysteresis options
static int avglim = 1;    // Set by -a option
static int avgcnt = 0;
static double hyst = 0;   // Hysteresis - degrees, set by -h

// Scanning flags
static int SFLAG_TAIL = 0;
static int SFLAG_STAT = 0;

// Frequeny bin bounds for spectrum output
static int FSn = 0;
static int FEn = 0;

static struct AVGDATA
{
   double v1, v2;
}
  *avgdata = NULL;

static int ngroups = 0;

///////////////////////////////////////////////////////////////////////////////
//  Utilities                                                                //
///////////////////////////////////////////////////////////////////////////////

static void close_file( struct VTSID_FILE *sf)
{
   if( sf->d) free( sf->d);
   if( sf->fields) free( sf->fields);
   if( sf->filename) free( sf->filename);
   if( sf->f) fclose( sf->f);
   free( sf);
}

//
// Size in bytes of a data record in the file 'sf'.
//
static int record_size( struct VTSID_FILE *sf)
{
   if( is_monitor( sf))
      return sizeof( int32_t) + sizeof( float) * sf->hdr.nf;

   if( is_spectrum( sf))
      return sizeof( int32_t) + sizeof( float) * sf->hdr.nf * sf->hdr.spec_size;

   VT_bailout( "invalid type in record_size");
   return 0;
}

//
// Timestamp of the start of file 'sf'.
//
static timestamp base_timestamp( struct VTSID_FILE *sf)
{
   return timestamp_compose( sf->hdr.bt_secs, sf->hdr.bt_nsec/1e9);
}

//
// Timestamp of the current record read from the file 'sf'.
//
static timestamp record_timestamp( struct VTSID_FILE *sf)
{
   return timestamp_add( base_timestamp( sf), sf->d->ts * 100e-6);
}

///////////////////////////////////////////////////////////////////////////////
//  File Operations                                                          //
///////////////////////////////////////////////////////////////////////////////

static int seek_record( struct VTSID_FILE *sf, int n)
{
   return fseek( sf->f, sf->dp + n * record_size( sf), SEEK_SET) < 0 ? 0 : 1;
}

static int read_record( struct VTSID_FILE *sf)
{
   int e = fread( sf->d, record_size( sf), 1, sf->f) == 1;
   if( e) sf->loaded = 1;
   return e;
}

static timestamp last_timestamp( struct VTSID_FILE *sf)
{ 
   if( sf->count)
   {
      if( seek_record( sf, sf->count - 1) &&
          read_record( sf)) return record_timestamp( sf);
      VT_bailout( "error reading %s", sf->filename);
   }

   return base_timestamp( sf);
}

static struct VTSID_FILE *open_file( char *ident, char *filename)
{
   struct VTSID_FILE *sf = VT_malloc_zero( sizeof( struct VTSID_FILE));

   int e;
   if( ident && ident[0])
      e = asprintf( &sf->filename, "%s/mon/%s/%s", datadir, ident, filename);
   else
      e = asprintf( &sf->filename, "%s/spec/%s", datadir, filename);
   if( e < 0) VT_bailout( "out of memory");

   sf->f = fopen( sf->filename, "r");
   if( !sf->f)
   {
      VT_report( 0, "cannot open %s: %s", sf->filename, strerror( errno));
      close_file( sf);
      return NULL;
   }

   if( fread( &sf->hdr, sizeof( struct VTSID_HDR), 1, sf->f) != 1)
   {
      VT_report( 0, "cannot read header of %s: %s", 
                        sf->filename, strerror( errno));
      close_file( sf);
      return NULL;
   }

   if( !is_monitor( sf) && 
       !is_spectrum( sf))
   {
      VT_report( 0, "broken header in %s: bad magic", sf->filename);
      close_file( sf);
      return NULL;
   }
   
   if( !sf->hdr.nf || sf->hdr.nf > 50) // Sanity check on field count 
   {
      VT_report( 0, "broken header in %s: fields %d",
                        sf->filename, sf->hdr.nf);
      close_file( sf);
      return NULL;
   }

   struct stat st;
   if( fstat( fileno( sf->f), &st) < 0)
      VT_bailout( "cannot fstat %s: %s", sf->filename, strerror( errno));
   sf->inode = st.st_ino;
   int size = sizeof( struct VTSID_FIELD) * sf->hdr.nf;
   sf->fields = VT_malloc( size);
   if( fread( sf->fields, size, 1, sf->f) != 1)
   {
      VT_report( 0, "cannot read header of %s: %s", 
                        sf->filename, strerror( errno));
      close_file( sf);
      return NULL;
   }

   sf->dp = ftell( sf->f);
   sf->count = (st.st_size - sf->dp)/record_size( sf);
   sf->d = VT_malloc( record_size( sf));
   VT_report( 2, "opened file %s, rs %d cnt %d dp %d",
                 sf->filename, record_size( sf), sf->count, sf->dp);
   return sf;
}

//
// Open the first file containing records beyond timestamp TS and seek to
// the first such record
//

static struct VTSID_FILE *open_first( char *ident, timestamp TS)
{
   DIR *dh;
   struct dirent *dp;
   timestamp t = timestamp_ZERO;
   char dirname[200];

   //
   //  Scan the data directory to find the files which bracket TS.
   //

   if( ident && ident[0])
      sprintf( dirname, "%s/mon/%s", datadir, ident);
   else
      sprintf( dirname, "%s/spec", datadir);
   if( (dh = opendir( dirname)) == NULL)
   {
      VT_report( 0, "cannot open data directory for %s: %s", 
                       ident, strerror( errno));
      return NULL;
   }

   //  We want the start time of file1 to be less than or equal to TS and the
   // start time of file2 to be greater than TS, with no intervening files.
   char file1[14] = "";
   char file2[14] = "";
   timestamp T1 = timestamp_ZERO;
   timestamp T2 = timestamp_ZERO;
   while( (dp = readdir( dh)) != NULL)
   {
      t = VT_timestamp_from_filename( dp->d_name);
      if( timestamp_is_NONE( t))
         continue;   // Not a valid data filename?  Ignore.

      if( timestamp_LE( t, TS) &&
          (timestamp_is_ZERO( T1) || timestamp_GT( t, T1)))
      {
         strcpy( file1, dp->d_name);
         T1 = t;
      }
      else
      if( timestamp_GT( t, TS) &&
          (timestamp_is_ZERO( T2) || timestamp_LT( t, T2)))
      {
         strcpy( file2, dp->d_name);
         T2 = t;
      }
   }
   closedir( dh);

   if( !file1[0] && !file2[0]) return NULL;   // No data at all

   // If no file with earlier data than TS, start with first record of file2.
   if( !file1[0]) return open_file( ident, file2);

   // See if file1 contains the starting record.
   // Must open it to find its final timestamp.
   struct VTSID_FILE *sf = open_file( ident, file1);
   t = last_timestamp( sf);

   if( timestamp_LT( t, TS))   
   {
      // Data starts beyond file1, so begin with first record of file2
      if( SFLAG_TAIL && !file2[0])
      {
         if( sf->count) seek_record( sf, sf->count - 1);
         return sf;
      }
      close_file( sf);
      if( file2[0]) return open_file( ident, file2);
      return NULL;
   }

   if( !sf->count) // For some reason file1 is empty
   {
      // Special case when file1 is empty and there is no file2.  Occurs when
      // TS is in the future and we are tailing the current file.
      if( !file2[0]) return sf;

      // There is a file2, skip the empty file1 and start with file2.
      close_file( sf);   
      return open_file( ident, file2);
   }

   // The starting record is somewhere in file1.  Binary search to find it.
   int n1 = 0;
   int n2 = sf->count - 1;

   // Find the first record with timestamp greater than or equal to TS
   while( n1 < n2 - 1)
   {
      int mid = (n1 + n2)/2;
      if( !seek_record( sf, mid) ||
          !read_record( sf)) VT_bailout( "seek error in %s", sf->filename);
      t = record_timestamp( sf);
      if( timestamp_LT( t, TS)) n1 = mid;
      else
         n2 = mid;
   }

   seek_record( sf, n2);
   return sf;
}

static struct VTSID_FILE *open_next( struct VTSID_FILE *sf)
{
   return open_first( sf->hdr.ident,
                      timestamp_add( last_timestamp( sf), 1e-5));
}

static ino_t current_inode( char *ident)
{
   struct stat st;
   char linkname[200];

   if( ident && ident[0])
      sprintf( linkname, "%s/mon/%s/current", datadir, ident);
   else
      sprintf( linkname, "%s/spec/current", datadir);
   if( stat( linkname, &st) < 0) return 0;

   return (int) st.st_ino;
}

///////////////////////////////////////////////////////////////////////////////
//  Output Functions                                                         //
///////////////////////////////////////////////////////////////////////////////

static struct VTSID_FIELD *fieldspec = NULL;
static int nfieldspec = 0;
static int *fieldmap = NULL;
static int hdr_rowcnt = 0;
static int nout = 0;

static void output_headings( struct VTSID_FILE *sf)
{
   int i;

   printf( "ts");

   for( i=0; i<nfieldspec; i++)
      switch( fieldspec[i].type)
      {
         case SF_AMPLITUDE: printf( " a%d", fieldspec[i].cha + 1);
                            break;
         case SF_PHASE_CAR_90:
         case SF_PHASE_CAR_180:
         case SF_PHASE_CAR_360:
                            // chb == 1 when monitor specifies az=
                            if( fieldspec[i].chb == 1)
                               printf( " cpa");
                            else
                               printf( " cp%d", fieldspec[i].cha + 1);
                            break;
         case SF_PHASE_MOD_90:
         case SF_PHASE_MOD_180:
                            printf( " mp%d", fieldspec[i].cha + 1);
                            break;
         case SF_PHASE_REL: printf( " rp%d_%d", fieldspec[i].cha + 1,
                                                fieldspec[i].chb + 1);
                            break;
         case SF_BEARING_180:
                              printf( " b180");
                              break;
         case SF_BEARING_360: printf( " b360");
                              break;
         default: printf( " u");
                  break;
      }

   if( is_spectrum( sf))
   {
      int ns = sf->hdr.spec_size;
      double f1 = sf->hdr.spec_base;

      if( FEn) ns = FEn + 1;    
      if( FSn)
      {
         f1 = sf->hdr.spec_base + FSn * sf->hdr.spec_step;
         ns -= FSn;
      }

      printf( " spectrum %.5f %.5f %d", f1, sf->hdr.spec_step, ns);
   }
   putchar( '\n');
}

static char *format_timestamp( timestamp T)
{
   static char s[50];

   if( OFLAG_TR) sprintf( s, "%.4f", timestamp_diff( T, Tstart));
   else
   if( OFLAG_TE)
   {
      char temp[30]; timestamp_string4( T, temp); sprintf( s, "%s", temp);
   }
   else
   {
      time_t xsec = timestamp_secs( T);
      struct tm *tm = gmtime( &xsec);
      sprintf( s, "%04d-%02d-%02d_%02d:%02d:%02d.%04d",
                tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec,
                (int)(1e4 * timestamp_frac(T)));
   }

   return s;
}

static void output_field( struct VTSID_FIELD *f, float v)
{
   switch( f->type)
   {
      case SF_AMPLITUDE: printf( " %.3e", v); break;

      case SF_PHASE_CAR_90:
      case SF_PHASE_CAR_180:
      case SF_PHASE_CAR_360:
      case SF_PHASE_MOD_90:
      case SF_PHASE_MOD_180:
      case SF_PHASE_REL:   printf( " %6.2f", v); break;

      case SF_BEARING_180:
      case SF_BEARING_360: printf( " %5.1f", v); break;
   }
}

static void output_record( struct VTSID_FILE *sf, timestamp T)
{
   int i, j;

   //
   // Output headings if required
   //

   if( OFLAG_HDR)
   {
      if( !hdr_interval)
      {
         output_headings( sf);
         OFLAG_HDR = 0;
      }
      else
      if( !hdr_rowcnt--)
      {
         output_headings( sf);
         hdr_rowcnt = hdr_interval - 1;
      }
   }

   //
   // Generate an output record
   //

   if( timestamp_is_ZERO( Tstart)) Tstart = T;
   printf( "%s", format_timestamp( T));

   for( j=0; j<ngroups; j++)
   {
      // In spectrum mode, apply FS and FE if given
      if( is_spectrum( sf))
      {
         if( FSn && j < FSn) continue;
         if( FEn && j > FEn) break;
      }

      for( i=0; i<nfieldspec; i++)
      {
         if( fieldmap[i] != -1)
            output_field( fieldspec + i, avgdata[j * nfieldspec + i].v1);
         else
            printf( " 0");
      }
   }
   printf( "\n");

   nout++;
}

///////////////////////////////////////////////////////////////////////////////
//  Record Processing                                                        //
///////////////////////////////////////////////////////////////////////////////

static struct HYST
{
   double bank;
   double last;
   double ma;
}
 *hystdata = NULL;

static double *despike;
static double **despike_cache;
static timestamp *despike_T;
static int DW = 0;

static void do_hysteresis( struct VTSID_FILE *sf, timestamp T)
{
   int i, j;

   //
   // Apply hysteresis to angles if required
   //

   int first = 0;
   if( !hystdata)   // First time through?
   {
      hystdata = 
         VT_malloc_zero( sizeof( struct HYST) * nfieldspec * ngroups);
      first = 1;
   }

   for( j=0; j<ngroups; j++)
      for( i=0; i<nfieldspec; i++)
      {
         if( fieldmap[i] == -1) continue;

         int M;
         switch( fieldspec[i].type)
         {
            case SF_PHASE_CAR_90: 
            case SF_PHASE_MOD_90: M = 90; break;
            case SF_PHASE_CAR_180:
            case SF_PHASE_MOD_180:
            case SF_BEARING_180: M = 180; break;
            case SF_PHASE_CAR_360:
            case SF_BEARING_360: M = 360; break;
            default: continue;
         }

         struct HYST *hp = hystdata + j * nfieldspec + i;
         double v = avgdata[j * nfieldspec + i].v1;

         if( first) hp->ma = v;
         double d = v - hp->last;
         hp->last = v;
         if( d < -M/2) hp->bank += M;
         if( d > M/2) hp->bank -= M;

         v += hp->bank;
         d = v - hp->ma;
         if( d > M/2) { hp->bank -= M;  v -= M; }
         if( d < -M/2) { hp->bank += M;  v += M; }

         if( v >= M + hyst) 
            { hp->bank -= M + hyst; hp->ma -= M + hyst; v -= M + hyst; }
         if( v < 0 - hyst)
            { hp->bank += M + hyst; hp->ma += M + hyst; v += M + hyst; }
         avgdata[j * nfieldspec + i].v1 = v;

         hp->ma = hp->ma * 0.99 + v * 0.01;
      }

   output_record( sf, T);
}

static void flush_average( struct VTSID_FILE *sf, timestamp T)
{
   int i, j;

   if( !avgcnt) return;

   //
   // Compute the averages
   //
   for( j=0; j<ngroups; j++)
      for( i=0; i<nfieldspec; i++)
      {
         if( fieldmap[i] == -1) continue;

         switch( fieldspec[i].type)
         {
            case SF_PHASE_CAR_90:
            case SF_PHASE_MOD_90:
               avgdata[j * nfieldspec + i].v1  = 45 / M_PI * 
                     atan2( avgdata[j * nfieldspec + i].v1/avgcnt,
                            avgdata[j * nfieldspec + i].v2/avgcnt);
               if( avgdata[j * nfieldspec + i].v1 < 0)
                   avgdata[j * nfieldspec + i].v1 += 90;
               break;
  
            case SF_PHASE_CAR_180:
            case SF_PHASE_MOD_180:
            case SF_PHASE_REL:
            case SF_BEARING_180:
               avgdata[j * nfieldspec + i].v1  = 90 / M_PI * 
                     atan2( avgdata[j * nfieldspec + i].v1/avgcnt,
                            avgdata[j * nfieldspec + i].v2/avgcnt);
               if( avgdata[j * nfieldspec + i].v1 < 0)
                  avgdata[j * nfieldspec + i].v1 += 180;
               break;

            case SF_PHASE_CAR_360:
            case SF_BEARING_360:
               avgdata[j * nfieldspec + i].v1  = 
                  180 / M_PI * atan2( avgdata[j * nfieldspec + i].v1/avgcnt,
                                      avgdata[j * nfieldspec + i].v2/avgcnt);
               if( avgdata[j * nfieldspec + i].v1 < 0)
                  avgdata[j * nfieldspec + i].v1 += 360;
               break;

            default: avgdata[j * nfieldspec + i].v1 /= avgcnt;
         }
      }

   do_hysteresis( sf, T);
}

static void discard_average( struct VTSID_FILE *sf)
{
   avgcnt = 0;
}

static void do_averaging( struct VTSID_FILE *sf, timestamp T)
{
   static timestamp avgT =
                  timestamp_ZERO;  // Timestamp of the current averaging period
   int i, j;

   //
   //  Do averaging.  If we are not averaging, avglim is set to one and nothing
   //  much happens in this function other than copying despike[] to avgdata[].
   // 

   if( !avgcnt)                             // Starting a new averaging period?
   {
      memset( avgdata, 0, sizeof( struct AVGDATA) *  nfieldspec * ngroups);
      avgT = T;                        // Timestamp of the new averaging period
   }

   for( j=0; j<ngroups; j++)
      for( i=0; i<nfieldspec; i++)
      {
         if( fieldmap[i] != -1)
         {
            double v = despike[j * nfieldspec + i];

            switch( fieldspec[i].type)
            {
               case SF_PHASE_CAR_90:
               case SF_PHASE_MOD_90:
                  avgdata[j * nfieldspec + i].v1 += sin( M_PI * 4 * v/180);
                  avgdata[j * nfieldspec + i].v2 += cos( M_PI * 4 * v/180);
                  break;
             
               case SF_PHASE_REL:
               case SF_PHASE_MOD_180:
               case SF_PHASE_CAR_180:
               case SF_BEARING_180:
                  avgdata[j * nfieldspec + i].v1 += sin( M_PI * 2 * v/180);
                  avgdata[j * nfieldspec + i].v2 += cos( M_PI * 2 * v/180);
                  break;

               case SF_PHASE_CAR_360:
               case SF_BEARING_360:
                  avgdata[j * nfieldspec + i].v1 += sin( M_PI * v/180);
                  avgdata[j * nfieldspec + i].v2 += cos( M_PI * v/180);
                  break;

               default: avgdata[j * nfieldspec + i].v1 += v;
            }
         }
      }

   if( ++avgcnt == avglim) // End of averaging period?
   {
      flush_average( sf, avgT);
      avgcnt = 0;
   }
}

static int ndw = 0;
static double *despike_dev;
// static int ndespike_dev = 0;

static void remove_spikes( struct VTSID_FILE *sf)
{
   int i, j, k;

   if( !DW)
   {
      // No spike removal, just pass the current record through transparently
      for( j=0; j<ngroups; j++)
         for( i=0; i<nfieldspec; i++)
            if( fieldmap[i] != -1)
            {
               double v = sf->d->data[j * sf->hdr.nf + fieldmap[i]];
               despike[j * nfieldspec + i]  = v;
            }
   
      do_averaging( sf, record_timestamp( sf));
      return;
   }

   if( ndw == DW)
   {
      despike = despike_cache[0];
      do_averaging( sf, despike_T[0]);
   }

   double *t = despike_cache[0]; 
   memmove( despike_cache, despike_cache+1, sizeof( double *) * (DW-1));
   despike_cache[DW-1] = t;
   memmove( despike_T, despike_T+1, sizeof( timestamp) * (DW-1));

   for( j=0; j<ngroups; j++)
      for( i=0; i<nfieldspec; i++)
         if( fieldmap[i] != -1)
         {
            double v = sf->d->data[j * sf->hdr.nf + fieldmap[i]];
            despike_cache[DW-1][j * nfieldspec + i]  = v;
         }
 
   despike_T[DW-1] = record_timestamp( sf);
 
   if( ndw < DW) { ndw++; return; }

   for( j=0; j<ngroups; j++)
      for( i=0; i<nfieldspec; i++)
      {
         if( fieldmap[i] == -1) continue;

         double dv = despike_cache[DW-1][j * nfieldspec + i]
                     - despike_cache[0][j * nfieldspec + i];
         dv *= dv;

         despike_dev[j * nfieldspec + i] = 
            despike_dev[j * nfieldspec + i] * 0.99 + dv * 0.01;

         dv = despike_dev[j * nfieldspec + i];
         for( k=1; k<DW-1; k++)
         {
            double y = despike_cache[k][j * nfieldspec + i]  
                     - despike_cache[0][j * nfieldspec + i];
            y = y * y;
            if( y > 4 * dv)
               despike_cache[k][j * nfieldspec + i] = 
                  despike_cache[0][j * nfieldspec + i];
         }
      }
 
}

static void discard_despike( struct VTSID_FILE *sf)
{
   ndw = 0;
}

static void process_record( struct VTSID_FILE *sf)
{
   static timestamp T_last = timestamp_ZERO;
   static struct VTSID_FILE *so = NULL;
   int i;

   // If no fields specified on the command line, initialise fieldspec
   // from the first data file.  This fixes the output columns for all
   // further records:-
   //   - If new fields appear in later records they will be ignored.
   //   - If existing fields vanish from later data, a zero will be output.
   // This is handled by a structure array called fieldmap[].

   if( !fieldspec)
   {
      nfieldspec = sf->hdr.nf;
      i = sizeof( struct VTSID_FIELD) * nfieldspec;
      fieldspec = VT_malloc( i);
      memcpy( fieldspec, sf->fields, i);
      fieldmap = VT_malloc( nfieldspec * sizeof( int));
      for( i=0; i<nfieldspec; i++) fieldmap[i] = -1;
   }

   // First time through, initialise ngroups, de-spike and averaging arrays.
   if( !ngroups)
   {
      ngroups = is_spectrum( sf) ? sf->hdr.spec_size : 1;
      avgdata = VT_malloc( sizeof( struct AVGDATA) * nfieldspec * ngroups);
      if( DW)
      {
         despike_dev = VT_malloc_zero( sizeof( double) * nfieldspec * ngroups);
         despike_T = VT_malloc( sizeof( timestamp) * DW);
         despike_cache = VT_malloc( sizeof( double *) * DW);
         for( i=0; i<DW; i++)
            despike_cache[i] =
               VT_malloc( sizeof( double) * nfieldspec * ngroups);
      }
      else
         despike = VT_malloc( sizeof( double) * nfieldspec * ngroups);

      // Set spectrum output bounds
      if( is_spectrum( sf))
         for( i=0; i<ngroups; i++)
         {
            double f = sf->hdr.spec_base + i * sf->hdr.spec_step;
            if( FS && !FSn && f >= FS) FSn = i;
            if( FE && f <= FE) FEn = i;
         }
   }

   // First time through, or if the data file has changed, remap the
   // output fields
   if( so != sf)
   {
      so = sf;
      for( i=0; i<nfieldspec; i++)
      {
         fieldmap[i] = -1;
         int j;
         for( j=0; j<sf->hdr.nf; j++)
            if( !memcmp( fieldspec + i, sf->fields + j,
                         sizeof( struct VTSID_FIELD)))
            {
               fieldmap[i] = j;
               break;
            }
      }
   }

   //
   // Check for timing breaks
   //

   timestamp T = record_timestamp( sf);

   if( !timestamp_is_ZERO( T_last) &&
       fabs( timestamp_diff(T, T_last)/sf->hdr.interval - 1) > 0.01)
   {
      discard_average( sf);
      discard_despike( sf);

      VT_report( 1, "timing break %s", format_timestamp( T_last));
      VT_report( 1, "          to %s", format_timestamp( T));

      if( OFLAG_TBR) printf( "\n");
   }
   T_last = T;

   //
   //  Do spike removal.
   //

   remove_spikes( sf);
}

///////////////////////////////////////////////////////////////////////////////
//  File Stat                                                                //
///////////////////////////////////////////////////////////////////////////////

//
//  Describe the given data file.
//

static void file_stat( char *ident, char *filename)
{
   if( !filename) filename = "current";

   struct VTSID_FILE *sf = open_file( ident, filename);

   if( !sf) return;

   if( is_monitor( sf))
   {
      printf( "           type: monitor, %s, %s\n", sf->hdr.ident, 
                                         sigtype_to_txt( sf->hdr.type));
   }

   if( is_spectrum( sf)) printf( "           type: spectrum\n");

   timestamp LT = base_timestamp( sf);
   printf( "first timestamp: %s\n", format_timestamp( LT));
   if( sf->count)
   {
      if( seek_record( sf, sf->count - 1) &&
          read_record( sf)) LT = record_timestamp( sf);
   }
   printf( " last timestamp: %s\n", format_timestamp( LT));
   printf( "        records: %d\n", sf->count);
   printf( "    record size: %d bytes\n", record_size( sf));
   printf( "record interval: %.4f seconds\n", sf->hdr.interval);

   if( is_spectrum( sf))
   {
      printf( " frequency from: %14.6f Hz\n", sf->hdr.spec_base);
      printf( "             to: %14.6f Hz\n", sf->hdr.spec_base + 
                   sf->hdr.spec_size * sf->hdr.spec_step);
      printf( "           step: %.4f Hz\n", sf->hdr.spec_step);
      printf( "           bins: %d\n", sf->hdr.spec_size);
   }
   if( is_monitor( sf))
   {
      printf( "      frequency: %.6f Hz\n", sf->hdr.frequency);
      printf( "      bandwidth: %.6f Hz\n", sf->hdr.width);
   }

   printf( "         fields: %d\n", sf->hdr.nf);
   int i;
   for( i=0; i<sf->hdr.nf; i++)
   {
      printf( "              %d: ", i+1);
      switch( sf->fields[i].type)
      {
         case SF_AMPLITUDE:   printf( "amplitude ch%d",
                                       sf->fields[i].cha + 1);
                              break;

         case SF_PHASE_CAR_90:
         case SF_PHASE_CAR_180:
         case SF_PHASE_CAR_360:
                              printf( "absolute phase ch%d",
                                          sf->fields[i].cha + 1);
                              break;
         case SF_PHASE_MOD_90:
         case SF_PHASE_MOD_180:
                              printf( "modulation phase ch%d",
                                       sf->fields[i].cha + 1);
                              break;
         case SF_PHASE_REL:   printf( "relative phase ch%d to ch%d",
                                       sf->fields[i].cha + 1,
                                       sf->fields[i].chb + 1);
         case SF_BEARING_180: printf( "bearing, mod 180");
                              break;
         case SF_BEARING_360: printf( "bearing, mod 360");
                              break;
      }
      printf( "\n");
   }

   close_file( sf);
}

///////////////////////////////////////////////////////////////////////////////
//  Scanning                                                                 //
///////////////////////////////////////////////////////////////////////////////

//
//  Extract data from a specific file if filename given, otherwise scan the
//  timestamp range TS,TE.   Passed each record down to process_record().
//

static void scan_data( char *ident, char *filename)
{
   struct VTSID_FILE *sf;
   
   if( filename)
      sf = open_file( ident, filename);
   else
      sf = open_first( ident, TS);            // Opens earliest file contain TS

   if( !sf)
   {
      VT_report( 0, "no data found");
      return;
   }

   time_t r = time( NULL);
   int current = sf->inode == current_inode( sf->hdr.ident);

   while( 1) 
   {
      if( read_record( sf)) 
      {
         timestamp t = record_timestamp( sf);

         if( !timestamp_is_ZERO( TE) &&
             timestamp_GE( t, TE) && !SFLAG_TAIL) break;

         if( timestamp_LT( t, TS) && !SFLAG_TAIL) continue;

         process_record( sf);
         if( output_limit && nout == output_limit) break;
         time( &r);
         continue;
      }

      // No more data available in this file
      if( !current)   // Historical data file?
      {
         // Move to next file, if there is one
         struct VTSID_FILE *sf2 = open_next( sf);
         close_file( sf);
         if( !sf2) break;
   
         sf = sf2;
         current = sf->inode == current_inode( sf->hdr.ident);
      }
      else  // Currently active data file
      {
         if( !SFLAG_TAIL) break;

         // -t options is set: Wait a moment, see if more data arrives.
         // Check the inode every few seconds to see if vtsid has started
         // a new file.
         time_t w = time( NULL);
         if( w - r > 5)
         {
            time( &r);
            if( sf->inode != current_inode( sf->hdr.ident))
            {
               VT_report( 1, "new inode detected");
               current = 0;
            }
         }
         usleep( 100000); continue;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Options Parsing                                                          //
///////////////////////////////////////////////////////////////////////////////

static void parse_format_options( char *s)
{
   if( !strcmp( s, "te")) { OFLAG_TE = 1; return; }
   if( !strcmp( s, "ti")) { OFLAG_TE = 0; return; }
   if( !strcmp( s, "tr")) { OFLAG_TR = 1; return; }
   if( s[0] == 'h')
   {
      OFLAG_HDR = 1;
      hdr_interval = atoi( s+1);
      return;
   }

   if( !strcmp( s, "tb")) { OFLAG_TBR = 1; return; }

   VT_bailout( "unrecognised output format option: [%s]", s);
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void usage( void)
{
   fprintf( stderr,
       "usage: vtsidex options datadir\n"
       "\n"
       "options:\n"
       " -v          Increase verbosity\n"
       " -L name     Specify logfile\n"
       " -m monitor  Specify monitor (default spectrum)\n"
       " -f file     Specify a particular data file\n"
       " -n recs     Limit output to number of records\n"
       " -t          Tail the data\n"
       " -s          Print status\n"
       " -a n        Average every n records\n"
       " -d n        De-spike spanning n records (before averaging)\n"
       " -h degrees  Apply hysteresis to angle outputs\n"
       " -T timespec Specify time range\n"
       " -ohn        Output header every n records\n"
       " -ote        Timestamps in unix epoch\n"
       " -oti        Timestamps in ISO format\n"
       " -otr        Relative timestamp in seconds\n"
          );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtsidex");

   char *filename = NULL;
   char *ident = NULL;

   if( sizeof( struct VTSID_HDR) != 128)
      VT_bailout( "VTSID_HDR size incorrect: %d, expecting 128", 
         (int) sizeof( struct VTSID_HDR));
   if( sizeof( struct VTSID_FIELD) != 4)
      VT_bailout( "VTSID_FIELD size incorrect: %d, expecint 4",
         (int) sizeof( struct VTSID_FIELD));

   while( 1)
   {
      int c = getopt( argc, argv, "vf:o:T:m:n:sta:d:h:F:L:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'o') parse_format_options( optarg);
      else
      if( c == 'f') filename = strdup( optarg);
      else
      if( c == 's') SFLAG_STAT = 1;
      else
      if( c == 'T') VT_parse_timespec( optarg, &TS, &TE);
      else
      if( c == 'F') VT_parse_freqspec( optarg, &FS, &FE);
      else
      if( c == 't') SFLAG_TAIL = 1;
      else
      if( c == 'm') ident = strdup( optarg);
      else
      if( c == 'a') avglim = (int) atof( optarg) + 0.5;
      else
      if( c == 'h') hyst = atof( optarg);
      else
      if( c == 'd') DW = atoi( optarg) + 2;
      else
      if( c == 'n')
      {
         output_limit = atoi( optarg);
         if( output_limit <= 0)
            VT_bailout( "invalid output limit [%s]", optarg);
      }
      else
      if( c == -1) break;
      else
         usage();
   }

   if( optind + 1 != argc) usage();
   datadir = strdup( argv[optind]);

   if( avglim < 1) avglim = 1;

   if( SFLAG_STAT)
   {
      file_stat( ident, filename);
      return 0;
   }

   if( !timestamp_is_ZERO( TS) &&
       !timestamp_is_ZERO( TE) && timestamp_LT( TE, TS))
      VT_bailout( "end timestamp before start timestamp");

   if( !timestamp_is_ZERO( TS)) Tstart = TS;

   if( SFLAG_TAIL && timestamp_is_ZERO( TS))
       TS = timestamp_compose( INT32_MAX, 0);

   scan_data( ident, filename);

   return 0;
}

