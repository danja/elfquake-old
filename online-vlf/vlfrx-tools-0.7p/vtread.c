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

static char *datadir = NULL;                             // Data file directory
static char *outname = NULL;                              // Output stream name

struct VTREAD_FILE
{
   char *filename;
   int32_t count;
   VTFILE *vtfile;
};

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

static void close_file( struct VTREAD_FILE *sf)
{
   if( sf->filename) free( sf->filename);
   if( sf->vtfile) VT_close( sf->vtfile);
   free( sf);
}

static inline int seek_record( struct VTREAD_FILE *sf, int n)
{
   sf->vtfile->timebase = timestamp_ZERO;
   return lseek( sf->vtfile->fh,
                 sf->vtfile->bs * (off_t) n, SEEK_SET) < 0 ? 0 : 1;
}

static inline int read_record( struct VTREAD_FILE *sf)
{
   return VT_read_next( sf->vtfile);
}

static struct VTREAD_FILE *open_file( char *filename)
{
   struct VTREAD_FILE *sf = VT_malloc_zero( sizeof( struct VTREAD_FILE));

   if( asprintf( &sf->filename, "%s/%s", datadir, filename) < 0)
      VT_bailout( "out of memory");

   if( (sf->vtfile = VT_open_input( sf->filename)) == NULL)
   {
      VT_report( 0, "cannot open %s: %s", sf->filename, strerror( errno));
      close_file( sf);
      return NULL;
   }

   if( !sf->vtfile->bs)
   {
      VT_report( 0, "empty file: %s", sf->filename);
      close_file( sf);
      return NULL;
   }

   struct stat st;
   if( fstat( sf->vtfile->fh, &st) < 0)
      VT_bailout( "cannot fstat %s: %s", sf->filename, strerror( errno));

   sf->count = st.st_size/sf->vtfile->bs;
   return sf;
}

static timestamp first_timestamp( struct VTREAD_FILE *f)
{
   if( !seek_record( f, 0) ||
       !read_record( f)) VT_bailout( "error reading %s", f->filename);
   return VT_get_timestamp( f->vtfile);
}

static timestamp last_timestamp( struct VTREAD_FILE *f)
{
   if( !seek_record( f, f->count - 1) ||
       !read_record( f)) VT_bailout( "error reading %s", f->filename);

   double dT = 1/(VT_get_sample_rate( f->vtfile) * VT_get_srcal( f->vtfile));
   return timestamp_add( VT_get_timestamp( f->vtfile),
                         (f->vtfile->nfb-1) * dT);
}

static struct VTREAD_FILE *open_next( char *current)
{
   DIR *dh;
   struct dirent *dp;
   timestamp t = timestamp_ZERO;

   current += strlen(current) - 13;

   if( (dh = opendir( datadir)) == NULL)
      VT_bailout( "cannot open data directory %s: %s",
                      datadir, strerror( errno));
 
   // Scan for the next data file: earliest file following the current 
   char file[14] = "";

   while( (dp = readdir( dh)) != NULL)
   {
      t = VT_timestamp_from_filename( dp->d_name);
      if( timestamp_is_NONE( t))
         continue;  // Not a valid data filename?  Ignore.

      if( strcmp( dp->d_name, current) <=0) continue;

      if( !file[0] || strcmp( dp->d_name, file) < 0) 
         strcpy( file, dp->d_name);
   }  
   closedir( dh);
   
   VT_report( 2, "open_next: file [%s]", file); 
   if( !file[0]) return NULL;   // No more data files

   struct VTREAD_FILE *f = open_file( file);
   if( !seek_record( f, 0) ||
       !read_record( f))
         VT_bailout( "seek error in %s at start", f->filename);

   return f;
}

static struct VTREAD_FILE *open_previous( char *current)
{
   DIR *dh;
   struct dirent *dp;
   timestamp t = timestamp_ZERO;

   current += strlen(current) - 13;

   if( (dh = opendir( datadir)) == NULL)
      VT_bailout( "cannot open data directory %s: %s",
                      datadir, strerror( errno));
 
   // Scan for the previous data file: oldest file earlier than the current 
   char file[14] = "";

   while( (dp = readdir( dh)) != NULL)
   {
      t = VT_timestamp_from_filename( dp->d_name);
      if( timestamp_is_NONE( t))
         continue;   // Not a valid data filename?  Ignore.

      if( strcmp( dp->d_name, current) >=0) continue;

      if( !file[0] || strcmp( dp->d_name, file) > 0) 
         strcpy( file, dp->d_name);
   }  
   closedir( dh);
   
   VT_report( 2, "open_prev: file [%s]", file); 
   if( !file[0]) return NULL;   // No more data files

   struct VTREAD_FILE *f = open_file( file);
   if( !seek_record( f, 0) ||
       !read_record( f))
         VT_bailout( "seek error in %s at start", f->filename);

   return f;
}

static struct VTREAD_FILE *open_first( timestamp TS)
{
   DIR *dh;
   struct dirent *dp;
   timestamp t = timestamp_ZERO;

   // VT_report( 2, "enter open_first TS=%Lf", TS);

   if( (dh = opendir( datadir)) == NULL)
      VT_bailout( "cannot open data directory %s: %s",
                      datadir, strerror( errno));
  
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
   
   VT_report( 2, "file1 [%s] file2 [%s]", file1, file2); 
   if( !file1[0] && !file2[0]) return NULL;   // No data at all

   // If no file with earlier data than TS, start with first block of file2.
   if( !file1[0]) return open_file( file2);

   // See if file1 contains the starting block. Open file1 and find the final
   // timestamp.

   struct VTREAD_FILE *f = open_file( file1);
   t = last_timestamp( f);
   if( timestamp_LT( t, TS))
   {
      // Data starts beyond file1, so begin with first record of file2, if
      // there is one.
      close_file( f);
      return file2[0] ? open_file( file2) : NULL;
   }

   t = first_timestamp( f);
   if( timestamp_GT( t, TS))
   {
      VT_report( 2, "first timestamp in previous file");
      struct VTREAD_FILE *fn = open_previous( f->filename);
      if( !fn) return f;   // No previous file so return current

      t = last_timestamp( fn);
      if( timestamp_LT( t, TS))
      {
         close_file( fn);
         return f;
      }

      close_file( f);
      f = fn;
   }

   // Binary search within within the file to find the starting record.

   int n1 = 0;
   int n2 = f->count - 1;

   // Find the first record with timestamp greater than or equal to TS

   while( 1)
   {
      int mid = (n1 + n2)/2;

      if( !seek_record( f, mid) ||
          !read_record( f))
         VT_bailout( "seek error in %s at %d", f->filename, mid);

      t = VT_get_timestamp( f->vtfile);
      if( timestamp_LT( TS, t)) n2 = mid - 1;
      else
      if( timestamp_GE( TS,
          timestamp_add( t,
           f->vtfile->nfb/(VT_get_sample_rate( f->vtfile) *
                           VT_get_srcal( f->vtfile)))))
         n1 = mid + 1;

      else break;
   }

   // VT_report( 2, "open_first t=%Lf", t);
   return f;
}

static void scan_data( timestamp TS, timestamp TE)
{
   struct VTREAD_FILE *f = open_first( TS);

   if( !f) return;

   int sample_rate = VT_get_sample_rate( f->vtfile);
   int chans = VT_get_chans( f->vtfile);

   if( !sample_rate) return;

   VTFILE * vtoutfile = VT_open_output( outname, chans, 0, sample_rate);
   if( !vtoutfile) VT_bailout( "cannot open %s: %s", outname, VT_error);

   int enable = 0, isblk;
   VT_rbreak( f->vtfile);

   VT_report( 1, "reading from: %s", f->filename);
   timestamp T = timestamp_ZERO;

   while( 1)
   {
      if( (isblk = VT_is_block( f->vtfile)) != 0)
      {
         VT_release( vtoutfile);

         if( isblk < 0)
         {
            VT_report( 1, "reached end of %s", f->filename);
   
            struct VTREAD_FILE *fn = open_next( f->filename);
            close_file( f);
            if( !fn) break;   // No further data files available
            
            f = fn;
            VT_report( 1, "switched to: %s", f->filename);
            continue;
         }

         T = VT_get_timestamp( f->vtfile);
         VT_set_timebase( vtoutfile, T, VT_get_srcal( f->vtfile));

         if( enable)
         {
            double dT = 1/(VT_get_sample_rate( f->vtfile) * 
                           VT_get_srcal( f->vtfile));
            if( !timestamp_is_ZERO( TE) &&
                timestamp_LT( timestamp_add( T, (f->vtfile->nfb-1) * dT), TE))
            {
               // Copy whole block
               while( f->vtfile->nfb)
                  VT_insert_frame( vtoutfile, VT_get_frame( f->vtfile));
               continue;
            }
         }
      }

      T = VT_get_timestamp( f->vtfile);
      if( !timestamp_is_ZERO( TE) && timestamp_GE( T, TE))
      {
         VT_release( vtoutfile);
         break; 
      }

      if( !enable)
      {    
         if( timestamp_LT( T, TS))
         {
            VT_get_frame( f->vtfile);
            continue;
         }
         enable = 1;
         VT_set_timebase( vtoutfile, T, VT_get_srcal( f->vtfile));
      }

      VT_insert_frame( vtoutfile, VT_get_frame( f->vtfile));
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void usage( void)
{
   fprintf( stderr, "usage: vtread datadir outname\n"
       "\n"
       "options:\n"
       " -v          Increase verbosity\n"
       " -B          Run in background\n"
       " -L name     Specify logfile\n"
       " -T timespec Specify time range\n"
          );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtread");

   int background = 0;
   timestamp TS = timestamp_ZERO,
             TE = timestamp_ZERO;

   while( 1)
   {
      int c = getopt( argc, argv, "vBT:L:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'T') VT_parse_timespec( optarg, &TS, &TE);
      else
      if( c == -1) break;
      else
         usage();

   }

   if( optind + 2 == argc)
   {
      datadir = strdup( argv[optind]);
      outname = strdup( argv[optind+1]);
   }
   else
   if( optind + 1 == argc)
   {
      datadir = strdup( argv[optind]);
      outname = strdup( "-");
   }
   else usage();

   if( background)
   {
      int flags = outname[0] == '-' ? KEEP_STDOUT : 0;
      VT_daemonise( flags);
   }

   scan_data( TS, TE);
   return 0;
}

