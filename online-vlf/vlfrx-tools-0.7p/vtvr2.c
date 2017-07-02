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

///////////////////////////////////////////////////////////////////////////////
//  VR2 Format                                                               //
///////////////////////////////////////////////////////////////////////////////

// Fixed size, 4103 LSB words, = 7 word header + 4096 word payload;
// 
// VR2 Header format, word offsets.
// 0:  sync, 0xa116;
// 1:  low byte = yy, high byte = month;
// 2:  low byte = day, high byte = hour;
// 3:  low byte = minute, high byte = second;
// 4:  hardware timer, low 16 bits;
// 5:  hardware timer, low byte = high 8 bits, high byte = 0;
// 6:  status word;
//     bit 15:  GPS nagivation, 1 = navigate, 0 = no;
//     bit 14..10: not used;
//     bit 9:   CH2 sampling, 1 = on, 0 = off;
//     bit 8:   CH1 sampling, 1 = on, 0 = off;
//     bit 7..0:  divider value;

// Divider settings:  
//   0 = 200k,    1 = 100k,  2 = 66.6..k, 3 = 50k,  4 = 40k, 5 = 33.3..k, 
//   6 = 2.5714k, 7 = 25k,   8 = 20k,     9 = 10k, 10 = 8k,
//  11 = 4k,     12 = 2k,   13 = 14 = 15 = 20k

static int EFLAG = 0;    // -e option: encode to VR2 format
static int DFLAG = 0;    // -d option: decode from VR2 format

static int sample_rate;   // Nominal sample rate
static int chans;         // Number of samples per frame
static double srcal;      // Sample rate correction

static char *bname;       // Input or output VT stream name
static VTFILE *vtoutfile = NULL;

static struct DIVTAB {
   int rate;
   double srcal;
} divtab[16] = {  
   { 200000, 1.0 },  { 100000, 1.0},    { 66666, 1.00001 },  { 50000, 1.0},
   { 40000, 1.0},    { 33333, 1.00001}, { 28571, 1.000014 }, { 25000, 1.0},
   { 20000, 1.0},    { 10000, 1.0},     { 8000, 1.0},        { 4000,  1.0},
   { 2000, 1.0},     { 20000, 1.0},     { 20000, 1.0},       { 20000, 1.0} 
};

#define VR2SIZE 4103       
#define VR2MAGIC 0xa116

static uint16_t vr2_packet[VR2SIZE];
static int16_t *vr2_data = (int16_t *) vr2_packet + 7;
static uint32_t vfcnt = 0;    // Count of valid VR2 input frames processed

///////////////////////////////////////////////////////////////////////////////
//  Encoder                                                                  //
///////////////////////////////////////////////////////////////////////////////

static void init_vr2_packet( timestamp T)
{
   int i;

   memset( vr2_packet, 0, 14);

   vr2_packet[0] = VR2MAGIC;

   time_t ti = timestamp_secs( T);
   struct tm *tm = gmtime( &ti);
   vr2_packet[1] = ((tm->tm_year - 100) & 0xff) | ((tm->tm_mon + 1) << 8);
   vr2_packet[2] = tm->tm_mday | (tm->tm_hour << 8);
   vr2_packet[3] = tm->tm_min | (tm->tm_sec << 8);

   uint32_t hc = timestamp_frac( T)/78.125e-9;   // Hardware counter
   vr2_packet[4] = hc & 0xffff;
   vr2_packet[5] = hc >> 16;

   uint16_t status = 0x8000;   // Navigate on

   for( i=0; i<16; i++)
      if( divtab[i].rate == sample_rate &&
          divtab[i].srcal == srcal) break;
   if( i == 16) VT_bailout( "cannot encode sample rate in VR2");
   status |= i;

   status |= chans == 2 ? 0x300 : 0x100;

   vr2_packet[6] = status;
}

static void write_vr2_packet( void)
{
   if( fwrite( vr2_packet, 2, VR2SIZE, stdout) != VR2SIZE)
      VT_bailout( "write vr2 failed, %s", strerror( errno));
   vfcnt++;
}

static inline int16_t limiting( double f)
{
  double v = f * INT16_MAX;
  if( v > INT16_MAX) return INT16_MAX;
  else
  if( v < INT16_MIN) return INT16_MIN;
  else return v;
}

static void run_encode( void)
{
   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   VTFILE *vtfile = VT_open_input( bname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   srcal = VT_get_srcal( vtfile);
   VT_report( 1, "encode channels: %d, sample_rate: %d", chans, sample_rate);

   if( chans > 2) VT_bailout( "max 2 channels in VR2");

   int NF = 4096/chans;         // Number of frames in VR2 packet
   int i;

   while( 1)
   {
      timestamp T = VT_get_timestamp( vtfile);
      if( timestamp_is_NONE( T))
      {
         VT_report( 1, "end of input");
         break;
      }
      init_vr2_packet( T);
      if( chans == 1)
         for( i=0; i<NF; i++)
         {
            double *frame = VT_get_frame( vtfile);
            if( !frame) break;
            vr2_data[i] = limiting( frame[0]);
         }
      else
         for( i=0; i<NF; i++)
         {
            double *frame = VT_get_frame( vtfile);
            if( !frame) break;
            vr2_data[i*2+0] = limiting( frame[0]);
            vr2_data[i*2+1] = limiting( frame[1]);
         }

      if( i != NF)
      {
         VT_report( 1, "end of input");
         break;
      }

      write_vr2_packet();
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Decoder                                                                  //
///////////////////////////////////////////////////////////////////////////////

static double *frame = NULL;

static int read_vr2_packet( timestamp *TP)
{
   static uint16_t status = 0;   // Status word

   while( 1)
   {
      errno = 0;
      if( fread( vr2_packet, 2, VR2SIZE, stdin) != VR2SIZE)
      {
         VT_report( 1, "end of input, %s",
                           errno ? strerror( errno) : "no error");
         return 0;
      }

      if( vr2_packet[0] != VR2MAGIC)
      {
         VT_report( 0, "bad magic in vr2 header, frame skipped");
         continue;
      }

      struct tm tm;
      memset( &tm, 0, sizeof( struct tm));
      tm.tm_year = (vr2_packet[1] & 0xff) + 100;
      tm.tm_mon = ((vr2_packet[1] >> 8) & 0xff) - 1;
      tm.tm_mday = vr2_packet[2] & 0xff;
      tm.tm_hour = (vr2_packet[2] >> 8) & 0xff;
      tm.tm_min = vr2_packet[3] & 0xff;
      tm.tm_sec = (vr2_packet[3] >> 8) & 0xff;

      time_t w = mktime( &tm);
      if( w < 0)
      {
         VT_report( 0, "bad timestamp in vr2 header, frame skipped");
         continue;
      }

      timestamp T = timestamp_compose( w, 0);

      T = timestamp_add( T, 
             78.125e-9 *
                ((uint32_t) vr2_packet[4] | ((uint32_t) vr2_packet[5] << 16)));

      if( !sample_rate)   // First time through?
      {
         status = vr2_packet[6];
         uint8_t div = status & 0xff;
         if( div >= 16)
         {
            VT_report( 0, "bad divisor in vr2 header, frame skipped");
            continue;
         }

         if( ((status >> 8) & 0x3) == 1) chans = 1;
         else
         if( ((status >> 8) & 0x3) == 3) chans = 2;
         else
         {
            VT_report( 0, "invalid channel bits in vr2 header, frame skipped");
            continue;
         }
         sample_rate = divtab[div].rate;
         srcal = divtab[div].srcal;
         VT_report( 1, "navigate: %s", status & 0x8000 ? "on" : "off");
      }
      else
      if( status != vr2_packet[6])
      {
         VT_report( 0, "status word changed in vr2 header, frame skipped");
         continue;
      }

      *TP = T;
      break;
   }

   return 1;  
}

static void run_decode( void)
{
   static int once = 0;
   timestamp T = timestamp_ZERO;

   while( 1)
   {
      if( !read_vr2_packet( &T))
      {
         VT_release( vtoutfile);
         return;
      }

      vfcnt++;  
      
      if( !once)
      { 
         once = 1;

         VT_report( 1, "channels: %d sample rate: %d srcal: %.6f",
                       chans, sample_rate, srcal);
         vtoutfile = VT_open_output( bname, chans, 0, sample_rate);
         if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);
         frame = VT_malloc( sizeof( double) * chans);
      }

      VT_set_timebase( vtoutfile, T, srcal);

      int i;

      if( chans == 1)
         for( i=0; i<4096; i++)
         {
            frame[0] = vr2_data[i]/(double)INT16_MAX;
            VT_insert_frame( vtoutfile, frame); 
         }
      else
         for( i=0; i<2048; i++)
         {
            frame[0] = vr2_data[i*2 + 0]/(double)INT16_MAX;
            frame[1] = vr2_data[i*2 + 1]/(double)INT16_MAX;
            VT_insert_frame( vtoutfile, frame); 
         }
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void usage( void)
{
   fprintf( stderr,
      "usage: vtvr2 [-e|-d] name\n"
      "\n"
      "  -v        Increase verbosity\n"
      "  -e        Encode\n"
      "  -d        Decode\n"
      "  -B        Run in background\n"
      "  -L name   Specify logfile\n"
   );
   exit( 1);
}

int main(int argc, char *argv[])
{
   VT_init( "vtvr2");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBedL:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'e') EFLAG = 1;
      else
      if( c == 'd') DFLAG = 1;
      else
      if( c == -1) break;
      else
         usage();
   }

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   if( !EFLAG && !DFLAG) VT_bailout( "must specify -e or -d");

   if( EFLAG)
   {
      if( background)
      {
         int flags = KEEP_STDOUT;
         if( bname[0] == '-') flags |= KEEP_STDIN;
         VT_daemonise( flags);
      }

      run_encode();
      VT_report( 1, "wrote %d VR2 frames", vfcnt);
   }
   else
   if( DFLAG)
   {
      if( background)
      {
         int flags = KEEP_STDIN;
         if( bname[0] == '-') flags |= KEEP_STDOUT;
         VT_daemonise( flags);
      }

      run_decode();

      VT_report( 1, "received %d VR2 frames", vfcnt);
   }

   return 0;
}


