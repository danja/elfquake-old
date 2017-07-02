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

#include <setjmp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

static int EFLAG = 0;     // -e option: run encoder
static int DFLAG = 0;     // -d option: run decoder
static int KFLAG = 0;     // -k option: retry uplink connection
static int PFLAG = 0;     // -p option: expect or produce ordinary ogg/vorbis,
                          //            no multiplexed timestamp stream
static int TFLAG = 0;     // -t option: throttle output to sample rate
static int IFLAG = 0;     // -i option: independent encoding of channels

static double Etime = 0;  // Set by -E option

static int sample_rate;   // Nominal sample rate
static int chans;         // Number of samples per frame
static char *bname;       // Input or output VT stream name
static double Q = -1;     // -q option: quality factor for VBR encoding
static double kbps = 0;   // -b option: bitrate for CBR encoding

static int NFLAG = 0;     // TRUE if -n used
static int net_port = 0;
static char *net_host = NULL;
static int bogus_rate = 0;  // 

static VTFILE *vtoutfile = NULL;

#define RETRY_DELAY 20   // 20 seconds between re-connect attempts

//
//  Stream header: to be sent once at start of stream
//
struct VT_OGG_HDR
{
   int32_t magic;
   int32_t chans;         // Number of samples per frame
   int32_t rate;          // Nominal sample rate
   int32_t spare;
} __attribute__((packed));

#define HDR_MAGIC 0x1996c3a3

//
//  Data block: sent whenever new timestamp data comes in on the VT stream
//
struct VT_OGG_BLK
{
   int32_t magic;
   int32_t tbreak;     // Timestamp discontinuity detected
   uint64_t posn;      // Frame count to which the rest of this data applies
   int32_t spare1;
   int32_t spare2;
   int32_t secs;       // Timestamp, seconds
   int32_t nsec;       // and microseconds
   double srcal;       // Sample rate calibration factor
   double glat;        // Geographic coordinates
   double glong;
   double gasl;        // Metres above sea level
} __attribute__((packed));

#define BLK_MAGIC 0x4f60f817

///////////////////////////////////////////////////////////////////////////////
//  Shout uplink                                                             //
///////////////////////////////////////////////////////////////////////////////

static char *up_server = NULL;
static int up_port = 0;
static char *up_passwd = NULL;
static char *up_method = NULL;
static char *up_mount = NULL;
static jmp_buf link_failed;

#if USE_SHOUT

#include <shout/shout.h>

//
//  Uplink data using shoutcast.
//
static void write_shout( uint8_t *data, int nbytes)
{
   static shout_t *sh = NULL;

   if( !sh)  // Not yet connected?
   {
      shout_init();
   
      if( (sh = shout_new()) == NULL ||
           shout_set_host( sh, up_server) != SHOUTERR_SUCCESS ||
           shout_set_protocol( sh, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS ||
           shout_set_port( sh, up_port) != SHOUTERR_SUCCESS ||
           shout_set_password( sh, up_passwd) != SHOUTERR_SUCCESS ||
           shout_set_mount( sh, up_mount) != SHOUTERR_SUCCESS ||
           shout_set_user( sh, "source") != SHOUTERR_SUCCESS ||
           shout_set_format( sh, SHOUT_FORMAT_OGG) != SHOUTERR_SUCCESS)
         VT_bailout( "shoutcast init failed: %s", shout_get_error( sh));

      if( shout_open(sh) != SHOUTERR_SUCCESS)
      {
         if( !KFLAG)
            VT_bailout( "uplink connect failed: %s", shout_get_error( sh));
         VT_report( 0, "uplink connect failed: %s", shout_get_error( sh));
         longjmp( link_failed, 1);
      }
      else
         VT_report( 0,
            "connected to %s:%d %s", up_server, up_port, up_mount);
   }

   if( shout_send( sh, (uint8_t *) data, nbytes) != SHOUTERR_SUCCESS)
   {
      VT_report( 0, "shout send failed: %s", shout_get_error( sh));
      shout_close( sh);
      shout_free( sh);
      shout_shutdown();
      sh = NULL;
      longjmp( link_failed, 1);
   }
   else
   if( TFLAG) shout_sync( sh);
}

#endif

///////////////////////////////////////////////////////////////////////////////
//  Network Listen/Connect                                                   //
///////////////////////////////////////////////////////////////////////////////

static int net_fh = -1;                                        // Socket handle

static void net_connect( void)
{
   VT_report( 1, "connecting to host %s port %d", net_host, net_port);
 
   if( (net_fh = socket( AF_INET, SOCK_STREAM, 0)) < 0)
      VT_bailout( "cannot open socket: %s", strerror( errno));

   struct hostent *dest = gethostbyname( net_host);
   if( !dest) VT_bailout( "cannot reach %s: %s", net_host, strerror( errno));
  
   struct sockaddr_in sa;
   memset( &sa, 0, sizeof( sa));
   sa.sin_family = AF_INET;
   memcpy( &sa.sin_addr.s_addr, dest->h_addr, dest->h_length);
   sa.sin_port = htons( net_port);

   while( 1)
   {
      if( connect( net_fh, (struct sockaddr *) &sa, sizeof( sa)) >= 0 )
         break;

      if( KFLAG) usleep( 5000000);
      else
         VT_bailout( "unable to connect to %s,%d", net_host, net_port);
   }

   VT_report( 0, "connected to %s:%d", net_host, net_port);
}

static void net_listen_accept( void)
{
   int sockfd;

   if( (sockfd = socket( AF_INET, SOCK_STREAM, 0)) < 0)
      VT_bailout( "cannot open inet socket: %s", strerror( errno));

   struct sockaddr_in sa;
   memset( &sa, 0, sizeof( sa));

   sa.sin_family = AF_INET;
   sa.sin_addr.s_addr = INADDR_ANY;
   sa.sin_port = htons( net_port);

   int val = 1;
   if( setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof( val)) < 0)
      VT_bailout( "cannot set SO_REUSEADDR: %s", strerror( errno));

   if( bind( sockfd, (struct sockaddr *) &sa, sizeof( sa)) < 0)
      VT_bailout( "cannot bind inet socket: %s", strerror( errno));

   VT_report( 1, "listening on port %d", net_port);

   if( listen( sockfd, 1) < 0)
      VT_bailout( "listen failed: %s", strerror( errno));

   struct sockaddr_in ca;
   socklen_t ca_len = sizeof( ca);
   net_fh = accept( sockfd, (struct sockaddr *) &ca, &ca_len);
   if( net_fh < 0) VT_bailout( "accept failed: %s", strerror( errno));

   char client_ip[50];
   strcpy( client_ip, inet_ntoa( ca.sin_addr));
   VT_report( 1, "connection accepted from %s", client_ip);

   close( sockfd);

   #if defined(SOL_TCP)
      // Set quite a short keep-alive timeout
      int ka_set = 1, ka_cnt = 3, ka_idle = 5, ka_int = 5;
      if( setsockopt( net_fh, SOL_SOCKET, SO_KEEPALIVE,
                      &ka_set, sizeof(int)) < 0 ||
          setsockopt( net_fh, SOL_TCP, TCP_KEEPCNT,
                      &ka_cnt, sizeof( int)) < 0 ||
          setsockopt( net_fh, SOL_TCP, TCP_KEEPIDLE,
                      &ka_idle, sizeof( int)) < 0 ||
          setsockopt( net_fh, SOL_TCP, TCP_KEEPINTVL,
                      &ka_int, sizeof( int)) < 0)
         VT_bailout( "cannot set keepalive on socket");
   #endif
}

static void net_setup( void)
{
   if( net_host) net_connect();
   else net_listen_accept();
}

///////////////////////////////////////////////////////////////////////////////
//  Encoder                                                                  //
///////////////////////////////////////////////////////////////////////////////

static void write_page( ogg_page *og)
{
   #if USE_SHOUT
      if( up_server)
      {
         write_shout( og->header, og->header_len);
         write_shout( og->body,  og->body_len);
         return;
      }
   #endif

   if( net_port && net_fh < 0) net_setup();

   int fh = net_port ? net_fh : 1;  
   if( write( fh, og->header, og->header_len) != og->header_len ||
       write( fh, og->body,  og->body_len) != og->body_len)
   {
      if( !net_port || !KFLAG)
         VT_bailout( "vorbis write failed: %s", strerror( errno));

      VT_report( 1, "network send failed");
      close( net_fh);  net_fh = -1;
      longjmp( link_failed, 1);
   }
}

//
// Produce an ogg/vorbis stream from the VT stream given by bname. 
//

static void run_encode( double Q)
{
   struct VT_CHANSPEC *chspec = VT_parse_chanspec( bname);

   VTFILE *vtfile = VT_open_input( bname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   VT_report( 1, "encode channels: %d, sample_rate: %d", chans, sample_rate);

   //
   // Initialise vorbis encoder
   // 
   vorbis_info      vi;   // For static vorbis bitstream settings
   vorbis_comment   vc;   // For user comments

   vorbis_dsp_state vd; // vorbis state variable
   vorbis_block     vb; // local working space for packet->PCM decode 

   ogg_stream_state osv,          // Stream state - vorbis stream
                    ost;          //              - timestamp stream

   if( setjmp( link_failed))
   {
      // Jumps here if write_page() or write_shout() fails.
      // De-initialise all the ogg/vorbis to prepare for a complete restart
      time_t retry_time = time( NULL) + RETRY_DELAY;

      ogg_stream_clear( &osv);
      ogg_stream_clear( &ost);
      vorbis_block_clear( &vb);
      vorbis_dsp_clear( &vd);
      vorbis_info_clear( &vi);

      // Now wait until we reach retry_time, discarding incoming data, making
      // sure we restart on an VT block boundary.  Only do this if we're the
      // one making the connection - just to prevent churning on the network.

      if( net_host || up_server)            // Are we originator of connection?
         while( time( NULL) < retry_time)
            if( !VT_get_frame( vtfile)) VT_exit( "stream ended");

      if( !PFLAG)
         while( !VT_is_block( vtfile))  // Wait for start of next VT packet
            if( !VT_get_frame( vtfile)) VT_exit( "stream ended");

      // Now fall through and re-setup the ogg/vorbis from scratch
   }

   int e, j;
   vorbis_info_init( &vi);

   if( Q == -1) // CBR encoding?
   {
      // Default to 64kbps if not specified by -b
      int bps = kbps ? chans * kbps * 1000 : chans * 64000;
      VT_report( 1, "CBR encoding, bps=%d", bps);

      if( (e=vorbis_encode_setup_managed( &vi, chans, 
                                          sample_rate, -1, bps, -1)) < 0)
      {
         if( e == OV_EIMPL)
            VT_bailout(
             "vorbis rejects combination chans=%d, sample_rate=%d, bps=%d",
                              chans, sample_rate, bps);
         VT_bailout( 
           "cannot encode chans=%d, sample_rate=%d, bps=%d, err=%d",
                              chans, sample_rate, bps, e);
      }
   }
   else // VBR encoding
   {
      VT_report( 1, "VBR encoding, q=%.2f", Q);
      if( (e=vorbis_encode_setup_vbr( &vi, chans, sample_rate, Q)) < 0)
      {
         if( e == OV_EIMPL)
           VT_bailout(                                                                      "vorbis rejects combination chans=%d, sample_rate=%d, VBR",
                              chans, sample_rate);

         VT_bailout( "cannot encode chans=%d, sample_rate=%d, Q=%.2f err=%d",
                 chans, sample_rate, Q, e);
      }
   }

   int coupling_mode;
   if( vorbis_encode_ctl( &vi, OV_ECTL_COUPLING_GET, &coupling_mode) < 0)
      VT_bailout( "OV_ECTL_COUPLING_GET failed");
   VT_report( 1, "coupling mode: %d", coupling_mode);

   if( IFLAG && coupling_mode)
   {
      coupling_mode = 0;
      if( vorbis_encode_ctl( &vi, OV_ECTL_COUPLING_SET, &coupling_mode) < 0)
         VT_bailout( "OV_ECTL_COUPLING_SET failed");
      VT_report( 1, "coupling mode reset");

      if( vorbis_encode_ctl( &vi, OV_ECTL_COUPLING_GET, &coupling_mode) < 0)
         VT_bailout( "OV_ECTL_COUPLING_GET failed");
      if( coupling_mode) VT_bailout( "failed to set independent encoding");
   }

   if( vorbis_encode_setup_init( &vi) < 0)
      VT_bailout( "vorbis_encode_setup_init failed");

   //
   // setup two stream encoders, stream 0 for vorbis, stream 1 for timestamps
   //

   if( ogg_stream_init( &osv, 0) < 0 ||
       ogg_stream_init( &ost, 1) < 0) VT_bailout( "ogg_stream_init failed");

   ogg_page   og;
   ogg_packet op;
 
   //
   //  Construct and send the VT_OGG_HDR on stream 'ost'
   //

   int64_t vt_packetno = 0;
   if( !PFLAG)
   {
      struct VT_OGG_HDR vt_ogg_hdr;
      vt_ogg_hdr.magic = HDR_MAGIC;
      vt_ogg_hdr.chans = chans;
      vt_ogg_hdr.rate = sample_rate;
      vt_ogg_hdr.spare = 0;
   
      op.packet = (unsigned char *) &vt_ogg_hdr;
      op.bytes = sizeof( struct VT_OGG_HDR);
      op.b_o_s = 1;
      op.e_o_s = 0;
      op.granulepos = 0;
      op.packetno = vt_packetno++;
      ogg_stream_packetin( &ost, &op);
   
      ogg_stream_flush( &ost, &og);
      write_page( &og);
   }

   //
   //  Construct and send the vorbis headers on stream 'osv'
   // 
   vorbis_comment_init( &vc);
   vorbis_comment_add( &vc, "encoded by vtvorbis");

   vorbis_analysis_init( &vd, &vi);
   vorbis_block_init( &vd, &vb);

   ogg_packet header;
   ogg_packet header_comm;
   ogg_packet header_code;

   vorbis_analysis_headerout( &vd, &vc, &header, &header_comm, &header_code);
  
   ogg_stream_packetin( &osv, &header);
   ogg_stream_packetin( &osv, &header_comm);
   ogg_stream_packetin( &osv, &header_code);

   while( ogg_stream_pageout( &osv, &og) || 
          ogg_stream_flush( &osv, &og)) write_page( &og);

   //
   //  Main loop
   //
   uint64_t frame_cnt = 0;   // Total frame counter

   #define NREAD 1024   // Number of frames to read and encode per buffer
   int nread;           // Number of frames in the current read, up to NREAD
   int eop = 0;         // Non-zero when end of PCM source has been detected

   while( !eop)
   {
      float **buffer = vorbis_analysis_buffer( &vd, NREAD);
      for( nread = 0; nread < NREAD; nread++)   
      {
         int is_blk = VT_is_block( vtfile);
         if( is_blk < 0)
         {
            VT_report( 2, "end of input");
            eop = 1;
            break;
         }

         if( !PFLAG && is_blk) // Fresh timestamp info ready?
         {
            // Retrieve timestamp, sample rate calibration, etc, and put the
            // info into a VT_OGG_BLK
            timestamp timebase = VT_get_timestamp( vtfile);
            struct VT_OGG_BLK vt_ogg_blk;
            vt_ogg_blk.magic = BLK_MAGIC;
            vt_ogg_blk.secs = timestamp_secs( timebase);
            vt_ogg_blk.nsec = timestamp_frac( timebase)*1e9;
            vt_ogg_blk.srcal = VT_get_srcal( vtfile);
            vt_ogg_blk.tbreak = VT_rbreak( vtfile);
            vt_ogg_blk.posn = frame_cnt;
            vt_ogg_blk.spare1 = 0;
            vt_ogg_blk.spare2 = 0;
            // XXX: hard-coded geo data for now 
            vt_ogg_blk.glat = 53.703;
            vt_ogg_blk.glong = -2.072;
            vt_ogg_blk.gasl = 300;

            // Send the VT_OGG_BLK out on the timestamp stream
            op.packet = (unsigned char *) &vt_ogg_blk;
            op.bytes = sizeof( struct VT_OGG_BLK);
            op.b_o_s = 0;
            op.e_o_s = 0;
            op.granulepos = frame_cnt;
            op.packetno = vt_packetno++;
            ogg_stream_packetin( &ost, &op);

            // Send all available pages, this ensures that the VT_OGG_BLK gets
            // to the other end ahead of the vorbis data it refers to
            while( ogg_stream_pageout( &ost, &og) ||
                   ogg_stream_flush( &ost, &og)) write_page( &og);
         }

         // Get the next frame, deinterleave, add into the vorbis encode buffer
         double *frame = VT_get_frame( vtfile);

         for( j=0; j<chans; j++) buffer[j][nread] = frame[chspec->map[j]];
         frame_cnt++;
      }

      // Submit the PCM buffer for encoding
      vorbis_analysis_wrote( &vd, nread);
      if( eop) vorbis_analysis_wrote( &vd, 0);  // Finalise the vorbis stream

      // Encode what we can
      while( vorbis_analysis_blockout( &vd, &vb) == 1)
      {
         vorbis_analysis( &vb, NULL);
         vorbis_bitrate_addblock( &vb);
         vorbis_bitrate_flushpacket( &vd, &op);
         ogg_stream_packetin( &osv, &op);

         // write out pages (if any)
         while( ogg_stream_pageout( &osv, &og)) write_page( &og);
      }
   }

   VT_exit( "stream ended");   // Normal exit, doesn't return;
}

///////////////////////////////////////////////////////////////////////////////
//  Decoder                                                                  //
///////////////////////////////////////////////////////////////////////////////

static uint64_t nrxed = 0;   // Received frame count, reset on each connect
static timestamp tbase;
static double *frame = NULL;
static int have_ogg_hdr = 0;   // 1 after receiving the OGG_HDR

//
//  Timing queue
//
//  Here we save received VT_OGG_BLK packets while they wait for the
//  associated vorbis audio to be decoded.
// 

#define MAXTQ 1000

static struct VT_OGG_BLK tq[MAXTQ];

static int tqn = 0;
static void tq_add( struct VT_OGG_BLK *blk)
{
   VT_report( 2, "VT_OGG_BLK packet rxed, for frame_cnt %lld",
             (long long) blk->posn);
   if( tqn == MAXTQ) VT_bailout( "timing queue full");
   if( blk->magic != BLK_MAGIC) VT_bailout( "input stream bad blk");

   struct VT_OGG_BLK *tp = tq + tqn++;
   memcpy( tp, blk, sizeof( struct VT_OGG_BLK));
}

//
//  Called to produce timestamp and sample rate calibration for the current
//  output frame count 'nrxed'.  We interpolate to the current position from
//  the oldest queued VT_OGG_BLK timestamp record.  
//
static void tq_get( timestamp *T, double *srcal)
{
   if( !tqn) VT_bailout( "timing queue empty");

   struct VT_OGG_BLK *tp = tq;

   // Shuffle the timing queue down while the oldest record is superceded
   // by new ones
   while( tqn > 1 && nrxed >= (tp+1)->posn)
   {
      memmove( tp, tp+1, (tqn-1) * sizeof( struct VT_OGG_BLK));
      tqn--;
   }
 
   // Should never happen because the VT_OGG_BLK always is sent before the
   // corresponding audio 
   if( nrxed < tp->posn) VT_bailout( "timing queue underrun");

   *T = timestamp_compose( tp->secs,
                 tp->nsec*1e-9 + (nrxed - tp->posn)/(tp->srcal * sample_rate));
   *srcal = tp->srcal;
}

//
//  Low level read from input vorbis stream - stdin or a network port.
//

static int read_low( char *buffer, int nbytes)
{
   int n = 0;

   if( !net_port)
   {
      n = read( 0, buffer, nbytes);
      return n;
   }

   if( KFLAG)
   {
      double timeout = 30.0;
   
      struct timeval tv = { (int)timeout,
                            (int)((timeout - (int)timeout)*1e6) };
   
      while( 1)
      {
         fd_set fds; FD_ZERO( &fds); FD_SET( net_fh, &fds);
      
         int e = select( net_fh+1, &fds, NULL, NULL, &tv); 
         if( e > 0) break;
         if( !e || errno != EINTR) 
         {
            VT_report( 1, "network peer stopped sending");
            longjmp( link_failed, 1);
         }
      }
   }

   n = read( net_fh, buffer, nbytes);

   if( n <= 0)
   {
      VT_report( 1, "network read failed");
      if( KFLAG) longjmp( link_failed, 1);
   }

   return n;
}
 
//
//  Open the output VT stream - called as soon as we get channels and sample
//  rate from the ogg/vorbis stream.
//
static void init_output( int channels, long rate)
{
   static int once = 0;

   if( !once)
   {
      sample_rate = rate;
      chans = channels;

      VT_report( 1, "decode chan: %d sample rate: %d", chans, sample_rate);
      vtoutfile = VT_open_output( bname, chans, 0, sample_rate);
      if( !vtoutfile) VT_bailout( "cannot open: %s", VT_error);
      frame = VT_malloc( sizeof( double) * chans);
   }
   else
   if( chans != channels ||
       sample_rate != rate)
      VT_bailout( "vorbis source changed to %d chans at %d after reset",
                      channels, (int) rate);
}

//
//  Attempt to decode a vorbis packet, returns zero if failed.  Called with
//  NULL as a request to reset everything.
//
static int handle_vorbis_packet( ogg_packet *op)
{
   static int vpcnt = 0;   // Count of vorbis headers received

   static vorbis_info vi;
   static vorbis_comment vc;
   static vorbis_dsp_state vd;
   static vorbis_block vb;
   static timestamp Tlast = timestamp_ZERO;
   static uint32_t ntxed = 0;   // Output frame count in current block
   static int tvalid = 0;
   static int once = 0;

   if( !op)   // Request for a complete reset
   {
      if( once)
      {
         vorbis_info_clear( &vi);
         vorbis_comment_clear( &vc);
         vorbis_dsp_clear( &vd);
         vorbis_block_clear( &vb);
         vpcnt = 0;
         once = 0;
      }
      return 0;
   }

   if( !once)    // First time through?
   {
      vorbis_info_init( &vi);
      vorbis_comment_init( &vc);
      once = 1;
   }

   if( vpcnt < 3)  // Collect 3 vorbis headers
   {
      if( vorbis_synthesis_headerin( &vi, &vc, op) < 0)
      {
         if( !vpcnt) return 0;
         VT_bailout( "vorbis header %d bad", vpcnt + 1);
      }
      VT_report( 2, "got vorbis header %d", vpcnt + 1);

      if( ++vpcnt == 3)  // All headers received?
      {
         // Initialise vorbis decoder
         vorbis_synthesis_init( &vd, &vi);
         vorbis_block_init( &vd, &vb);

         if( vi.bitrate_upper == vi.bitrate_nominal &&
             vi.bitrate_upper == vi.bitrate_lower)
            VT_report( 1, "constant bit rate %d", (int) vi.bitrate_nominal);
         else
         if( vi.bitrate_upper <= 0 && vi.bitrate_lower <= 0 &&
             vi.bitrate_nominal > 0)
            VT_report( 1, "average bit rate: %d", (int) vi.bitrate_nominal);
         else
            VT_report( 1, "variable bit rate: %d to %d",
                          (int) vi.bitrate_lower, (int) vi.bitrate_upper);

         if( PFLAG)
         {
            // Not expecting a timestamp stream, so initialise channels and
            // sample rate from the vorbis header info
            if( bogus_rate)
            {
               VT_report( 0, "declared rate %d overridden to %d",
                              (int) vi.rate, bogus_rate);
               init_output( vi.channels, bogus_rate);
            }
            else
               init_output( vi.channels, vi.rate);
         }
      }
   }
   else   // Deal with vorbis audio data
   {
      if( !PFLAG && !vtoutfile)
      {
         VT_report( 0, "no timestamp stream, maybe -p option is required");
         VT_bailout( "missing timestamp stream");
      }

      if( vorbis_synthesis( &vb, op) == 0)
         vorbis_synthesis_blockin( &vd, &vb);

      float **pcm;
      int frames;

      while( (frames=vorbis_synthesis_pcmout( &vd, &pcm)) > 0)
      {
         VT_report( 2, "vorbis audio packet rxed, %d frames, frame_cnt %lld",
             frames, (long long) nrxed);
         int i, j;
         for( j=0; j<frames; j++)
         {
            if( !vtoutfile->nfb)    // Beginning a new VT output block?
            {
               timestamp T;
               double srcal;
               if( !PFLAG) tq_get( &T, &srcal);
               else
               {   // Plain vorbis stream, make dummy timestamp
                  srcal = 1.0;
                  T = timestamp_add( tbase, nrxed/(double)sample_rate);
               }

               if( tvalid)   // Previous timestamp was valid?
               {
                  double shift = timestamp_diff( T, Tlast) -
                     ntxed/(double)(srcal * sample_rate);
                  if( fabs( shift) > 1.0/sample_rate)
                     VT_report( 0, "break detected: %.9f", shift);

                  // Some logic to discard negative timing breaks. 
                  if( timestamp_LE( T, Tlast))
                     VT_report( 0, "negative timestamp step, discarded data");
               }

               tvalid = timestamp_is_ZERO( Tlast) || timestamp_GT( T, Tlast);
               if( tvalid)
               {
                  VT_set_timebase( vtoutfile, T, srcal); 
                  Tlast = T;
                  ntxed = 0;
               }
            }

            if( tvalid)
            {
               for( i = 0; i < chans; i++) frame[i] = pcm[i][j];
               VT_insert_frame( vtoutfile, frame);
               ntxed++;
            }

            nrxed++;
         }

         vorbis_synthesis_read( &vd, frames);
      }

      if( Etime && nrxed/(double) sample_rate > Etime)
         VT_exit( "completed %f seconds", Etime);
   }

   return 1;
}

//
//  Attempt to decode a timestamp packet, returns zero if failed.
//
static int handle_tstamp_packet( ogg_packet *op)
{
   if( !have_ogg_hdr)                  // Still waiting for initial VT_OGG_HDR?
   {
      if( op->bytes != sizeof( struct VT_OGG_HDR)) return 0;

      struct VT_OGG_HDR vt_ogg_hdr;
      memcpy( &vt_ogg_hdr, op->packet, sizeof( struct VT_OGG_HDR));
      if( vt_ogg_hdr.magic != HDR_MAGIC) return 0;

      VT_report( 2, "VT_OGG_HDR packet rxed, %d chans, %d sample rate",
                   vt_ogg_hdr.chans, vt_ogg_hdr.rate);
      have_ogg_hdr = 1;
      init_output( vt_ogg_hdr.chans, vt_ogg_hdr.rate);
   }
   else  // Must be a VT_OGG_BLK incoming
   {
      if( op->bytes != sizeof( struct VT_OGG_BLK))
         VT_bailout( "VT_OGG_BLK size mismatch");
      tq_add( (struct VT_OGG_BLK *)op->packet);
   }

   return 1;
}

//
//  Dummy handler for ignoring unrecognised logical streams in the ogg multiplex
//
static int handle_dummy_packet( ogg_packet *op)
{
   return 1;
}

//
//  Read ogg stream from stdin and demultiplex
//
static void run_decode( void)
{
   int i, ndemux = 0;
   ogg_sync_state oy;

   #define MAX_DEMUX 10

   struct DEMUX
   {
      int serial;
      ogg_stream_state os;
      int (*handler)(ogg_packet *);
   } demux[MAX_DEMUX];

   if( setjmp( link_failed))
   {
      // Jumps here if read_low() fails
      // De-initialise all the ogg/vorbis to prepare for a complete restart

      for( i=0; i<ndemux; i++)
      {
         ogg_stream_clear( &demux[i].os);
         demux[i].handler = NULL;
         demux[i].serial = 0;
      }

      ogg_sync_clear( &oy);
      handle_vorbis_packet( NULL);

      close( net_fh);
      usleep( 5000000);
   }

   nrxed = 0;
   tqn = 0;
   ndemux = 0;
   have_ogg_hdr = 0;
   int have_vorbis = 0;
   int have_tstamp = 0;

   ogg_sync_init( &oy);

   // Open the network connection to receive vorbis data.  This can be a
   // connect or a listen, according to the -n option.

   if( net_port) net_setup(); 
   tbase = VT_rtc_time();

   while( 1)
   {
      char *buffer = ogg_sync_buffer( &oy, 4096);
      int bytes = read_low( buffer, 4096);
      VT_report( 3, "read bytes %d", bytes);

      if( bytes <= 0)
      { 
         VT_release( vtoutfile);
         VT_exit( "stream ended");
      }
      ogg_sync_wrote( &oy, bytes);

      // Extract ogg pages until no more are ready
      ogg_page og;
      while( ogg_sync_pageout( &oy, &og) == 1)
      {
         int serial = ogg_page_serialno( &og);

         // Find the demux entry for this serial, or allocate if not seen
         // before
         struct DEMUX *dm = demux;
         for( i = 0; i<ndemux && dm->serial != serial; i++, dm++);;;;
         if( i == ndemux)  // First time this serial has been seen?
         {
            if( ndemux == MAX_DEMUX) continue;  // Ignore stream
            ogg_stream_init( &dm->os, serial);
            dm->handler = NULL;
            dm->serial = serial;
            ndemux++;
         }

         if( ogg_stream_pagein( &dm->os, &og) < 0)
              VT_bailout( "Error reading bitstream data");

         // Read all available packets in this page
         ogg_packet op;
         while( ogg_stream_packetout( &dm->os, &op) == 1)
         {
            // Pass the packet to the appropriate handler, if we have one,
            // otherwise try each of our decoders

            if( dm->handler) dm->handler( &op);
            else
            if( !have_vorbis && handle_vorbis_packet( &op))
            {
               have_vorbis = 1;
               dm->handler = handle_vorbis_packet;          
               VT_report( 2, "found vorbis stream, serial %d", serial);
            }
            else
            if( !have_tstamp && handle_tstamp_packet( &op))
            {
               have_tstamp = 1;
               VT_report( 2, "found timestamp stream, serial %d", serial);
               if( PFLAG) 
               {
                  VT_report( 0, "ignoring timestamp stream, -p option");
                  dm->handler = handle_dummy_packet;
               }
               else dm->handler = handle_tstamp_packet;
            }
            else
            {
               // Not a stream we want to handle, dummy handler to ignore it
               dm->handler = handle_dummy_packet;
               VT_report( 2, 
                  "ignoring unrecognised stream, serial %d", serial);
            }
         }
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Main                                                                     //
///////////////////////////////////////////////////////////////////////////////

static int parse_uplink( char *arg)
{
   // method,server,port,mountpoint,password
   // shout,67.207.143.181,80,/test.ogg,passwd

   char *p = arg, *q;

   q = strchr( p, ','); if( !q) return 0;
   *q++ = 0; up_method = p;  p = q;

   q = strchr( p, ','); if( !q) return 0;
   *q++ = 0; up_server = p;  p = q;

   q = strchr( p, ','); if( !q) return 0;
   *q++ = 0; up_port = atoi( p);  p = q;
     
   q = strchr( p, ','); if( !q) return 0;
   *q++ = 0; up_mount = p;  p = q;

   up_passwd = p;

   VT_report( 1, "uplink: %s,%s,%d,%s",
                     up_method, up_server, up_port, up_mount);  
   return 1;
}

static void parse_net( char *arg)
{
   // Parse server,port or just port.

   char *p = strchr( arg, ',');
   if( p)
   {
      net_port = atoi( p+1);
      *p = 0;
      net_host = arg;
   }
   else
      net_port = atoi( arg);

   if( net_port <= 0 ||
       net_port >= 65535) VT_bailout( "invalid/missing port number in -n");
 
   NFLAG = 1;
}

static void usage( void)
{
   fprintf( stderr,
      "usage: vtvorbis [-e|-d] [name]\n"
      "\n"
      "  -v        Increase verbosity\n"
      "  -e        Encode\n"
      "  -d        Decode\n"
      "  -B        Run in background\n"
      "  -L name   Specify logfile\n"
      "  -p        Expect/generate only a pure ogg/vorbis stream\n"
      "            (default is to multiplex a timestamp stream)\n"
      "\n"
      "encoding options:\n"
      "  -q factor   Specify VBR encoding with this quality factor\n"
      "              factor range -0.1 to 1.0\n"
      "  -b kbps     Specify CBR encoding at this kbps per channel\n"
      "              (default is CBR, 64kbps per channel)\n"
      "  -i          Independent encoding of each channel\n"
      "\n"
      "  -u method,server,port,mount,passwd\n"
      "     uplink to icecast server\n"
      "  -k          Retry failed uplink connections\n"
      "              (default is to exit if connection drops)\n"
      "  -t          Throttle output to sample rate\n"
      "              (default is to encode and output as fast as possible)\n"
      "\n"
      "  -n [server,]port  Uplink or downlink vorbis, without protocol\n"
      "\n"
      "decoding options:\n"
      "  -E secs     Decode only specified number of seconds, then exit\n"
   );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtvorbis");

   if( sizeof( struct VT_OGG_HDR) != 16)
      VT_bailout( "VT_OGG_HDR incorrect size: %d, expecting 16",
           (int) sizeof( struct VT_OGG_HDR));
   if( sizeof( struct VT_OGG_BLK) != 64)
      VT_bailout( "VT_OGG_BLK incorrect size: %d, expecting 64",
           (int) sizeof( struct VT_OGG_BLK));

   int background = 0;
   while( 1)
   {
      int c = getopt( argc, argv, "vktBiedpq:u:E:L:b:n:r:?");

      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'k') KFLAG = 1;
      else
      if( c == 't') TFLAG = 1;
      else
      if( c == 'd') DFLAG = 1;
      else
      if( c == 'e') EFLAG = 1;
      else
      if( c == 'p') PFLAG = 1;
      else
      if( c == 'i') IFLAG = 1;
      else
      if( c == 'q') Q = atof( optarg);
      else
      if( c == 'b') kbps = atof( optarg);
      else
      if( c == 'E') Etime = atof( optarg);
      else
      if( c == 'r') bogus_rate = atoi( optarg);
      else
      if( c == 'u')
      {
         if( !parse_uplink( strdup( optarg)))
            VT_bailout( "invalid uplink argument: %s", optarg);
      }
      else
      if( c == 'n') parse_net( strdup( optarg));
      else
      if( c == -1) break;
      else
         usage();
   }

   if( argc > optind + 1) usage();
   bname = strdup( optind < argc ? argv[optind] : "-");

   if( !EFLAG && !DFLAG) VT_bailout( "must specify -e or -d");

   if( Q != -1)
   {
      if( Q < -0.1 || Q > 1.0)
         VT_bailout( "q factor out of range -0.1 to 1.0");
      if( kbps)
         VT_bailout( "cannot have -q (VBR) and -b (CBR) together");
   }

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


