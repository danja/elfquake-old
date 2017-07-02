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

static int FASCII = 0;                              // -oa option: ASCII output
static int FWAV = 0;                                  // -ow option: WAV output
static int RFLAG = 0;                         // -r option: relative timestamps

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtraw [options] [input]\n"
       "\n"
       "options:\n"
       "  -v        Increase verbosity\n"
       "  -B        Run in background\n"
       "  -L name   Specify logfile\n"
       "  -oa       ASCII output\n"
       "  -ob       Binary output, signed 16 bit\n"
       "  -ow       WAV output, signed 16 bit\n"
       "  -r        Relative time in ASCII output\n"
     );
   exit( 1);
}

static void parse_format_options( char *s)
{
   if( !strcmp( s, "a")) { FASCII = 1; FWAV = 0; return; }
   if( !strcmp( s, "b")) { FASCII = 0; FWAV = 0; return; }
   if( !strcmp( s, "w")) { FASCII = 0; FWAV = 1; return; }

   VT_bailout( "unrecognised output format option: [%s]", s);
}

static void output_wav_header( int chans, VTFILE *vtfile)
{
   uint32_t ua[3] = { 0x46464952, 0xffffffff, 0x45564157 };

   uint32_t ub[2] = { 0x20746d66, 16 };

   uint16_t sa[2] = { 1, chans };

   int sample_rate = VT_get_sample_rate( vtfile);
   uint32_t uc[2] = { sample_rate, sample_rate * chans * 2 };
        
   uint32_t ud[2] = { 0x61746164, 0xffffffff };
 
   uint16_t sb[2] = { 2 * chans, 16 }; 
   if( fwrite( ua, 4, 3, stdout) != 3 ||
       fwrite( ub, 4, 2, stdout) != 2 ||
       fwrite( sa, 2, 2, stdout) != 2 ||
       fwrite( uc, 4, 2, stdout) != 2 ||
       fwrite( sb, 2, 2, stdout) != 2 ||
       fwrite( ud, 4, 2, stdout) != 2)
      VT_bailout( "output failed: %s", strerror( errno));
}

static void complete_wav_header( int chans, uint64_t frames)
{
   if( fseek( stdout, 40, SEEK_SET))
   {
      VT_report( 1, "cannot set wave header size, non-seekable");
      return;
   }

   uint32_t u1 = frames * 2 * chans;
   uint32_t u2 = u1 + 44 - 8;

   if( fwrite( &u1, 4, 1, stdout) != 1 ||
       fseek( stdout, 4, SEEK_SET) ||
       fwrite( &u2, 4, 1, stdout) != 1)
      VT_bailout( "output failed: %s", strerror( errno));
}

int main( int argc, char *argv[])
{
   VT_init( "vtraw");

   int background = 0;
   
   while( 1)
   {
      int c = getopt( argc, argv, "vBro:L:?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'o') parse_format_options( optarg);
      else
      if( c == 'r') RFLAG = 1;
      else
      if( c == -1) break;
      else
         usage(); 
   }  
  
   if( argc > optind + 1) usage();
   char *bname = strdup( optind < argc ? argv[optind] : "-");
 
   if( background)
   {
      int flags = bname[0] == '-' ? KEEP_STDIN : 0;
      flags |= KEEP_STDOUT;
      VT_daemonise( flags);
   }

   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   VTFILE *vtfile = VT_open_input( bname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   int chans = chspec->n;
   VT_report( 1, "channels: %d, sample_rate: %d",
                   chans, VT_get_sample_rate( vtfile));

   timestamp Tstart = VT_get_timestamp( vtfile); 
   double *frame;
   int ch;

   if( FASCII)
      while( 1)
      {
         timestamp T = VT_get_timestamp( vtfile); 

         if( (frame = VT_get_frame( vtfile)) == NULL) break;

         if( RFLAG) printf( "%.7f", timestamp_diff( T, Tstart));
         else
         {
            char temp[30];   timestamp_string7( T, temp);
            printf( "%s", temp);
         }

         for( ch = 0; ch < chans; ch++)
         {
            double v = frame[chspec->map[ch]];
            if( printf( " %.5e", v) <= 0)
               VT_bailout( "output failed: %s", strerror( errno));
         }
         if( printf( "\n") <= 0)
            VT_bailout( "output failed: %s", strerror( errno));
      }
   else
   {
      if( FWAV) output_wav_header( chans, vtfile);
      uint64_t nout = 0;

      while( 1)
      {
         if( (frame = VT_get_frame( vtfile)) == NULL) break;

         for( ch = 0; ch < chans; ch++)
         {
            double v = frame[chspec->map[ch]] * 32767;
            short s;
            if( v > INT16_MAX) s = INT16_MAX;
            else
            if( v < INT16_MIN) s = INT16_MIN;
            else s = v;
   
            if( fwrite( &s, 2, 1, stdout) != 1)
               VT_bailout( "output failed: %s", strerror( errno));
         }

         nout++;
      }

      if( FWAV) complete_wav_header( chans, nout);
   }

   return 0;
}

