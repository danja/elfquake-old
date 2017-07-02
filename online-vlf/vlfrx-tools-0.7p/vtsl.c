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

static VTFILE *vtfile;
static char *vtname = NULL;
static int sample_rate = 0;
static int chans = 0;

static int KFLAG = 0;     // -k option: retry dropped network connections

#define MODE_SEND 1
#define MODE_RECV 2
static int mode = 0;

static int net_port = 0;
static char *net_host = NULL;

#define RETRY_DELAY 20   // 20 seconds between re-connect attempts

static void usage( void)
{
   fprintf( stderr,
       "usage:  vtsl -s|-r -n net_addr [options] [stream]\n"
       "\n"
       "options:\n"
       "  -v            Increase verbosity\n"
       "  -B            Run in background\n"
       "  -L name       Specify logfile\n"
       "\n"
       "  -s            Send to network\n"
       "  -r            Receive from network\n"
       "  -n host,port  Make client connection to server at host,port\n"
       "  -n port       Listen for and serve connections on port\n"
       "  -k            Retry dropped network connections\n"
     );
   exit( 1);
}

//
//  Header used by Spectrum Lab to determine the type of stream.
//

struct STREAM_HDR_1 {
   uint32_t dwPattern8000; // Pattern 0x80008000 (0x00 0x80 0x00 0x80),
                           // Never appears in the 16-bit signed integer
                           // samples because THEIR range is limited to
                           // -32767 ... +32767 .
   uint32_t nBytes;        // Total size of any header, required for stream
                           // reader to skip unknown headers up to the next
                           // raw sample .
   // HERE: nBytes = 4*4 + sizeof(VT_BLOCK) = 16 + 10*4+8 = 64 bytes .
   uint32_t dwReserve;     // 'reserved for future use'
                           // (and for 8-byte alignment)
   uint32_t dwStructID;    // 0=dummy,
                           // 1=the rest is a VT_BLOCK
}; 

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
}

///////////////////////////////////////////////////////////////////////////////
//  Network Listen/Connect                                                   //
///////////////////////////////////////////////////////////////////////////////

static jmp_buf link_failed;
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

      if( KFLAG) usleep( RETRY_DELAY * 1000000);
      else
         VT_bailout( "unable to connect to %s,%d", net_host, net_port);
   }

   VT_report( 0, "connected to %s,%d", net_host, net_port);
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

static void net_close( void)
{
   if( net_fh < 0) return;
   shutdown( net_fh, SHUT_RDWR);  close( net_fh);  net_fh = -1;
}

///////////////////////////////////////////////////////////////////////////////
//  Sending                                                                  //
///////////////////////////////////////////////////////////////////////////////

static void write_network( void *buffer, int nbytes)
{
   if( net_fh < 0) net_setup();

   if( write( net_fh, buffer, nbytes) != nbytes)
   {
      if( !KFLAG) VT_bailout( "network send failed: %s", strerror( errno));

      VT_report( 1, "network send failed");
      net_close();
      longjmp( link_failed, 1);
   }
}

static void run_send( void)
{
   struct VT_CHANSPEC *chspec = VT_parse_chanspec( vtname);

   vtfile = VT_open_input( vtname);
   if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

   VT_init_chanspec( chspec, vtfile);
   chans = chspec->n;
   sample_rate = VT_get_sample_rate( vtfile);
   VT_report( 1, "send channels: %d, sample_rate: %d", chans, sample_rate);

   int16_t *tbuf = NULL;
   int tbuflen = 0;  // Allocated size of tbuf, frames

   if( setjmp( link_failed))
   {
      if( !KFLAG) VT_bailout( "network peer stopped receiving");
  
      usleep( 1000000);

      // If part way through a block, discard the rest and prepare next block
      if( !VT_is_blk( vtfile)) VT_read_next( vtfile);
   }

   while( 1)
   {
      if( !vtfile->nfb && !VT_read_next( vtfile))
      {
         VT_report( 1, "end of stream to send");
         break;
      }

      struct STREAM_HDR_1 slhdr;
      slhdr.dwPattern8000 = 0x80008000;
      slhdr.nBytes = sizeof( struct STREAM_HDR_1) + sizeof( struct VT_BLOCK);
      slhdr.dwReserve = 0;
      slhdr.dwStructID = 1;  // Indicates following data is a VT_BLOCK

      struct VT_BLOCK vh;
      memcpy( &vh, vtfile->blk, sizeof( struct VT_BLOCK));
      vh.flags = VTFLAG_INT2;

      write_network( &slhdr, sizeof( struct STREAM_HDR_1));
      write_network( &vh, sizeof( struct VT_BLOCK));

      if( tbuflen != vtfile->bsize)
      {
         tbuflen = vtfile->bsize;
         tbuf = VT_realloc( tbuf, tbuflen * 2 * chans);
      }

      int nf = 0;   // Frame counter
      while( vtfile->nfb)
      {
         double *frame = VT_get_frame( vtfile);
         int j;
         for( j = 0; j < chans; j++)
         {
            int32_t v = lround( frame[j] * 32767);
            if( v > 32767) v = 32767;
            else
            if( v < -32767) v = -32767;

            tbuf[nf * chans + j] = v & 0xffff;
         }

         nf++;
      }

      write_network( tbuf, nf * 2 * chans);
   }
}

///////////////////////////////////////////////////////////////////////////////
//  Receiving                                                                //
///////////////////////////////////////////////////////////////////////////////

static int read_network_low( void *buffer, int nbytes)
{
   if( net_fh < 0) net_setup();

   int n = read( net_fh, buffer, nbytes);
   if( n <= 0)
   {
      VT_report( 1, "network read failed");
      net_close();
      longjmp( link_failed, 1);
   }

   return n;
}

static void read_network( void *buffer, int nbytes)
{
   char *p = (char *) buffer;

   do
   {
      int n = read_network_low( p, nbytes);
      nbytes -= n;
      p += n;
   }
    while( nbytes);
}

static void init_output( int channels, int rate)
{
   static int once = 0;

   if( !once)
   {
      sample_rate = rate;
      chans = channels;

      VT_report( 1, "receive: channels %d sample rate: %d", chans, sample_rate);
      vtfile = VT_open_output( vtname, chans, 0, sample_rate);
      if( !vtfile) VT_bailout( "cannot open: %s", VT_error);

      once = 1;
   }
   else
   if( chans != channels ||
       sample_rate != rate)
      VT_bailout( "source parameters changed to %d chans at %d after reset",
                      channels, (int) rate);
}

static void run_recv( void)
{
   double *frame = NULL;

   int16_t *tbuf = NULL;
   int tbuflen = 0;  // Allocated size of tbuf, frames

   if( setjmp( link_failed))
   {
      if( !KFLAG) VT_bailout( "network peer stopped sending");
   }

   while( 1)
   {
      struct STREAM_HDR_1 slhdr;
      read_network( &slhdr, sizeof( struct STREAM_HDR_1));
      if( slhdr.dwPattern8000 != 0x80008000)
         VT_bailout( "network stream out of sync");
      if(  slhdr.dwStructID != 1)
         VT_bailout( "network stream unknown StructID %d", slhdr.dwStructID);
      if( slhdr.nBytes !=
             sizeof( struct STREAM_HDR_1) + sizeof( struct VT_BLOCK))
         VT_bailout( "network stream incorrect header size");

      struct VT_BLOCK vh;
      read_network( &vh, sizeof( struct VT_BLOCK));
 
      init_output( vh.chans, vh.sample_rate);
      if( !frame) frame = VT_malloc( sizeof( double) * chans);

      if( tbuflen != vh.bsize)
      {
         tbuflen = vh.bsize;
         tbuf = VT_realloc( tbuf, tbuflen * 2 * chans);
      }

      VT_set_timebase( vtfile, VT_get_stamp( &vh), vh.srcal);

      read_network( tbuf, vh.frames * 2 * chans);

      int nf;
      for( nf = 0; nf < vh.frames; nf++)
      {
         int j;
         for( j = 0; j < chans; j++)
         {
            if( (uint16_t) tbuf[nf * chans + j] == 0x8000)
               VT_bailout( "unexpected header 0x8000");
            frame[j] = tbuf[nf * chans + j] / 32767.0;
         }

         VT_insert_frame( vtfile, frame);
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main( int argc, char *argv[])
{
   VT_init( "vtsl");

   int background = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBL:n:srk?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 'B') background = 1;
      else
      if( c == 'L') VT_set_logfile( "%s", optarg);
      else
      if( c == 'k') KFLAG = 1;
      else
      if( c == 's') mode = MODE_SEND;
      else
      if( c == 'r') mode = MODE_RECV;
      else
      if( c == 'n') parse_net( strdup( optarg));
      else
      if( c == -1) break;
      else
         usage();
   }

   if( !net_port) VT_bailout( "needs -n option to specify network address");

   if( optind + 1 == argc)
   {
      vtname = strdup( argv[optind]);
   }
   else
   if( optind == argc)
   {
      vtname = strdup( "-");
   }
   else usage();

   VT_bailout_hook( net_close);

   if( mode == MODE_RECV)
   {
      if( background)
      {
         int flags = vtname[0] == '-' ? KEEP_STDOUT : 0;
         VT_daemonise( flags);
      }

      run_recv();
   }
   else
   if( mode == MODE_SEND)
   {
      if( background)
      {
         int flags = vtname[0] == '-' ? KEEP_STDIN : 0;
         VT_daemonise( flags);
      }

      run_send();
   }
   else VT_bailout( "must specify send or receive with -s or -r");

   net_close();
   return 0;
}

