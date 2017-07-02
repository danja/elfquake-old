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

#include <FLAC/stream_encoder.h>
#include <FLAC/stream_decoder.h>

static int EFLAG = 0;     // -e option: run encoder
static int DFLAG = 0;     // -d option: run decoder

static int sample_rate;   // Nominal sample rate
static int chans;         // Number of samples per frame
static char *bname;       // Input or output VT stream name
static double Q = -1;     // -q option: quality factor

static VTFILE *vtoutfile = NULL;

//
//  Stream header: to be sent once at start of stream
//
struct VT_FLAC_HDR
{
   int32_t magic;
   int32_t chans;         // Number of samples per frame
   int32_t rate;          // Nominal sample rate
   int32_t spare;
};

#define HDR_MAGIC 0x97337cdb

//
//  Data block: sent whenever new timestamp data comes in on the VT stream
//
struct VT_FLAC_TBLK
{
   int32_t magic;
   int32_t tbreak;     // Timestamp discontinuity detected
   uint64_t posn;      // Frame count to which the rest of this data applies
   int32_t spare1;
   int32_t spare2;
   int32_t secs;       // Timestamp, seconds
   int32_t nsec;       // and microseconds
   double srcal;       // Sample rate calibration factor
};

#define TBLK_MAGIC 0x0d573355

struct VT_FLAC_DBLK
{
   int32_t magic;
   int32_t size;
};

#define DBLK_MAGIC 0xc09f2d1f

///////////////////////////////////////////////////////////////////////////////
//  Encoder                                                                  //
///////////////////////////////////////////////////////////////////////////////

static void write_data( char *data, int len)
{
   if( fwrite( data, 1, len, stdout) != len)
      VT_bailout( "write failed: %s", strerror( errno));
}

FLAC__StreamEncoderWriteStatus encode_write( const FLAC__StreamEncoder *se,
                         const FLAC__byte buffer[], size_t bytes,
                         unsigned samples, unsigned current_frame,
                         void *client_data)
{
   VT_report( 2, "encode write bytes %d samples %d", (int) bytes, samples);

   struct VT_FLAC_DBLK b;

   b.magic = DBLK_MAGIC;
   b.size = bytes;
   write_data( (char *) &b, sizeof( struct VT_FLAC_DBLK));
   write_data( (char *) buffer, bytes);

   return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static void run_encode( double Q)
{
   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   VTFILE *vtfile = VT_open_input( bname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   VT_report( 1, "encode channels: %d, sample_rate: %d", chans, sample_rate);

   struct VT_FLAC_HDR h;
   h.magic = HDR_MAGIC;
   h.chans = chans;
   h.rate = sample_rate;
   h.spare = 0;
   write_data( (char *) &h, sizeof(  struct VT_FLAC_HDR));

   //
   // Initialise encoder
   // 

   FLAC__StreamEncoder *se = FLAC__stream_encoder_new();

   FLAC__stream_encoder_set_channels( se, chans);
   FLAC__stream_encoder_set_bits_per_sample( se, 16);
   FLAC__stream_encoder_set_sample_rate( se, sample_rate);
   FLAC__stream_encoder_set_compression_level( se, 8);

   if( FLAC__stream_encoder_init_stream( se, 
                                         encode_write,
                                         NULL, NULL, NULL, NULL)
       != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
      VT_bailout( "cannot init flac encoder");

   //
   //  Main loop
   //

   uint64_t frame_cnt = 0;   // Total frame counter
   FLAC__int32 *buf = VT_malloc( 4 * chans);
   int i;

   while( 1)
   {
      int isblk = VT_is_block(vtfile);
      if( isblk < 0)
      {
         VT_report( 1, "end of input");
         break;
      }

      if( isblk)
      {
         timestamp T = VT_get_timestamp( vtfile);

         // Write timestamp block
         struct VT_FLAC_TBLK b;
         memset( &b, 0, sizeof( struct VT_FLAC_TBLK));
         b.magic = TBLK_MAGIC;
         b.posn = frame_cnt;
         b.secs = timestamp_secs( T);
         b.nsec = timestamp_frac( T) * 1e9;
         b.srcal = VT_get_srcal( vtfile);
         b.tbreak = VT_rbreak( vtfile);
         write_data( (char *) &b, sizeof( struct VT_FLAC_TBLK));
      }

      double *frame = VT_get_frame( vtfile);
      for( i=0; i<chans; i++)
      {
         int32_t v = lround( frame[chspec->map[i]] * INT16_MAX);
         if( v < INT16_MIN) v = INT16_MIN;
         else
         if( v > INT16_MAX) v = INT16_MAX;
         buf[i] = v;
      }

      if( FLAC__stream_encoder_process_interleaved( se, buf, 1) != true)
         VT_bailout( "encoder_process_interleave failed");

      frame_cnt++;
   }

   if( FLAC__stream_encoder_finish( se) != true)
      VT_bailout( "stream_encoder_finish failed");

   VT_exit( "stream ended");   // Normal exit, doesn't return;
}

///////////////////////////////////////////////////////////////////////////////
//  Decoder                                                                  //
///////////////////////////////////////////////////////////////////////////////

static uint64_t nout = 0;   // Number of output frames issued
static double *frame = NULL;

static int read_data( char *data, int len)
{
   return fread( data, 1, len, stdin) == len;
}

//
//  Timing queue.  Save received VT_FLAC_TBLKs until the associated audio
//  data comes through.
// 

#define MAXTQ 10

static struct VT_FLAC_TBLK tq[MAXTQ];

static int tqn = 0;
static void tq_add( struct VT_FLAC_TBLK *blk)
{
   if( tqn == MAXTQ) VT_bailout( "timing queue full");

   struct VT_FLAC_TBLK *tp = tq + tqn++;
   memcpy( tp, blk, sizeof( struct VT_FLAC_TBLK));
}

//
//  Called to produce timestamp and sample rate calibration for the current
//  output frame count 'nout'.  We interpolate to the current position from
//  the oldest queued VT_FLAC_TBLK timestamp record.  
//
static void tq_get( timestamp *T, double *srcal, int *tbreak)
{
   if( !tqn) VT_bailout( "timing queue empty");

   struct VT_FLAC_TBLK *tp = tq;

   // Shuffle the timing queue down while the oldest record is superceded
   // by new ones
   while( tqn > 1 && nout >= (tp+1)->posn)
   {
      memmove( tp, tp+1, (tqn-1) * sizeof( struct VT_FLAC_TBLK));
      tqn--;
   }
 
   // Should never happen because the VT_FLAC_BLK always is sent before the
   // corresponding audio 
   if( nout < tp->posn) VT_bailout( "timing queue underrun");

   *T = timestamp_compose( tp->secs,
                 tp->nsec*1e-9 + (nout - tp->posn)/(tp->srcal * sample_rate));
   *srcal = tp->srcal;
   *tbreak = tp->tbreak;
}

char *inbuf, *inp;
int ninbuf = 0;
#define MAXINBUF 100000

static FLAC__StreamDecoderReadStatus decode_read(
     const FLAC__StreamDecoder *decoder,
     FLAC__byte buffer[], size_t *bytes, void *client_data)
{
   while( !ninbuf)
   {
      int32_t magic;
      if( !read_data( (char *) &magic, 4)) 
      {
  VT_report( 1, "end of stream");
         *bytes = 0;
         return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
      }
      if( magic == DBLK_MAGIC)
      {
         struct VT_FLAC_DBLK b;
         if( !read_data( 4 + (char *) &b, sizeof( struct VT_FLAC_DBLK) - 4))
            VT_bailout( "DBLK read error");
         if( !read_data( inbuf, b.size))
            VT_bailout( "data block read error");
         inp = inbuf;
         ninbuf = b.size;
// VT_report( 1, "got DBLK %d", b.size);
      }
      else
      if( magic == TBLK_MAGIC)
      {
         struct VT_FLAC_TBLK b;
         if( !read_data( 4 + (char *) &b, sizeof( struct VT_FLAC_TBLK) - 4))
            VT_bailout( "TBLK read error");
         tq_add( &b);
// VT_report( 1, "got TBLK");
      }
      else
         VT_bailout( "unrecognised input");
   }

   int n = *bytes;
   if( n > ninbuf) { n = ninbuf; *bytes = n; }
   memcpy( buffer, inp, n);   inp += n;   ninbuf -= n;
   return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderWriteStatus decode_write(
           const FLAC__StreamDecoder *decoder,
           const FLAC__Frame *flac_frame,
           const FLAC__int32 *const buffer[],
           void *client_data)
{
   int i, j;
   for( i=0; i<flac_frame->header.blocksize; i++)
   {
      if( !vtoutfile->nfb)    // Beginning a new VT output block?
      {
         timestamp timestamp;
         double srcal;
         int tbreak;
         tq_get( &timestamp, &srcal, &tbreak);
         VT_set_timebase( vtoutfile, timestamp, srcal); 
      }

      for( j = 0; j < chans; j++) frame[j] = buffer[j][i]/(double) INT16_MAX;
      VT_insert_frame( vtoutfile, frame);

      nout++;
   }
   return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void decode_error( const FLAC__StreamDecoder *decoder,
                          FLAC__StreamDecoderErrorStatus status,
                          void *client_data)
{
   VT_bailout( "flac decode error %d after %lld", status, (long long) nout);
}

static void run_decode( void)
{
   inp = inbuf = VT_malloc( MAXINBUF);
   ninbuf = 0;

   FLAC__StreamDecoder *se = FLAC__stream_decoder_new();

   if( FLAC__stream_decoder_init_stream( se,
                                         decode_read,
                                         NULL, NULL, NULL, NULL, 
                                         decode_write,
                                         NULL,
                                         decode_error, NULL)
       != FLAC__STREAM_DECODER_INIT_STATUS_OK)
      VT_bailout( "cannot initialise decoder");

   struct VT_FLAC_HDR h;
   if( !read_data( (char *) &h, sizeof( struct VT_FLAC_HDR)))
      VT_bailout( "cannot read input file header");
   if( h.magic != HDR_MAGIC)
      VT_bailout( "bad input file header");
   
   sample_rate = h.rate;
   chans = h.chans;

   VT_report( 1, "decode chan: %d sample rate: %d", chans, sample_rate);
   vtoutfile = VT_open_output( bname, chans, 0, sample_rate);
   if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);
   frame = VT_malloc( sizeof( double) * chans);

   FLAC__stream_decoder_process_until_end_of_stream( se);

   VT_report( 1, "%lld frames decoded", (long long) nout);
   VT_release( vtoutfile);
   VT_exit( "stream ended");
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static void usage( void)
{
   fprintf( stderr,
      "usage: vtflac [-e|-d] [name]\n"
      "\n"
      "  -v        Increase verbosity\n"
      "  -e        Encode\n"
      "  -d        Decode\n"
      "  -B        Run in background\n"
      "  -L name   Specify logfile\n"
   );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtflac");

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
      if( c == 'd') DFLAG = 1;
      else
      if( c == 'e') EFLAG = 1;
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

      run_encode( Q);
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
   }

   return 0;
}


