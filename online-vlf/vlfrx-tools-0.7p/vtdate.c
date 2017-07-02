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

static int NFLAG = 0;                                       // Set by -n option
static int SFLAG = 0;                                       // Set by -s option
static int IFLAG = 0;                                       // Set by -i option
static double trunc_secs = 0;                               // Set by -t option
static double add_secs = 0;                                 // Set by -a option

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtdate [options] [timestamp|timespec]\n"
       "\n"
       "options:\n"
       "  -v       Increase verbosity\n"
       "  -n       Output numeric timestamp\n"
       "  -s       Output string timestamp\n"
       "  -i       Integer output\n"
       "  -t secs  Truncate to multiple of secs\n"
       "  -a secs  Add offset of secs\n"
     );
   exit( 1);
}

static void print_timestamp( timestamp T)
{
   char temp[50];

   if( NFLAG)
   {
      if( IFLAG) printf( "%d", timestamp_secs( T));
      else
      {
         timestamp_string6( T, temp);
         printf( "%s", temp);
      }
   }
   if( NFLAG && SFLAG) printf( " "); 
   if( SFLAG)
   {
      VT_format_timestamp( temp, T);
      IFLAG ? printf( "%.19s", temp) : printf( "%s", temp);
   }
}

int main( int argc, char *argv[])
{
   VT_init( "vtdate");

   while( 1)
   {
      int c = getopt( argc, argv, "vnsit:a:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'n') NFLAG = 1;
      else
      if( c == 's') SFLAG = 1;
      else
      if( c == 'i') IFLAG = 1;
      else
      if( c == 't') trunc_secs = atof( optarg);
      else
      if( c == 'a') add_secs = atof( optarg);
      else
      if( c == -1) break;
      else
         usage();
   }

   if( optind + 1 != argc) usage();
   char *argument = argv[optind];
   int is_timespec = strchr( argument, ',') ? 1 : 0;

   // If neither -n nor -s given, then do both
   if( !NFLAG && !SFLAG) NFLAG = SFLAG = 1;

   if( is_timespec)
   {
      timestamp TS = timestamp_ZERO, TE = timestamp_ZERO;

      VT_parse_timespec( argument, &TS, &TE);

      TS = timestamp_add( TS, add_secs);
      TE = timestamp_add( TE, add_secs);

      if( trunc_secs)
      {
         TS = timestamp_truncate( TS, trunc_secs);
         TE = timestamp_truncate( TE, trunc_secs);
      }

      print_timestamp( TS);
      printf( " ");
      print_timestamp( TE);
   }
   else
   {
      timestamp T;

      T = VT_parse_timestamp( argument);

      T = timestamp_add( T, add_secs);
      if( trunc_secs) T = timestamp_truncate( T, trunc_secs);

      print_timestamp( T);
   }

   printf( "\n");
   return 0;
}


