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

#define USE_SEM 0

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>

// Stream types
#define VT_TYPE_BUFFER 1                       // Lock-free buffer
#define VT_TYPE_FILE   2                       // File or FIFO
#define VT_TYPE_NET    3                       // Network connection
#define VT_TYPE_NETP   4                       // Persistent network connection

char VT_error[512];

static int in_background = 0;                  // Set to 1 after VT_daemonise()
static char *progname = NULL;
static char *logfile = NULL;                             // Pathname of logfile
static int loglevel = 0;                          //  Incremented by -v options

static void (*bailout_hook)(void) = NULL;
static int psfs_handle;

// Magic numbers for buffer and block headers
#define MAGIC_BUF   26374                            // Lock-free buffer header
#define MAGIC_BLK   27859                                  // Data block header

static VTFILE *alloc_vtfile( char *name)
{
   VTFILE *vtfile = VT_malloc_zero( sizeof( VTFILE));
   vtfile->name = strdup( name);
   return vtfile;
}

static void free_vtfile( VTFILE *vtfile)
{
   if( vtfile->name) free( vtfile->name);
   free( vtfile);
}

#if USE_SEM
static int open_sem( key_t semkey)
{
   int sid;

   if( (sid = semget( semkey, 1, IPC_CREAT | 0666 )) < 0)
   {
      sprintf( VT_error, "semget %d failed, %s", semkey, strerror( errno));
      return -1;
   }

   return sid;
}
#endif

//
//  Point to the data block with offset 'n' in a lock-free buffer.
//
static inline struct VT_BLOCK *vtlib_block( VTFILE *vtfile, int n)
{
   struct VT_BLOCK *bp = (struct VT_BLOCK *)
            ((char *)vtfile->bhead + 
                 sizeof( struct VT_BUFFER) + n * vtfile->bs);
   return bp;
}

//
//  How many bytes does each frame require?
//
static inline int frame_size( int chans, uint32_t flags)
{
   switch( flags & VTFLAG_FMTMASK)
   {
      case VTFLAG_FLOAT8: return chans * 8;
      case VTFLAG_FLOAT4: 
      case VTFLAG_INT4:   return chans * 4;
      case VTFLAG_INT2:   return chans * 2;
      case VTFLAG_INT1:   return chans;
   }

   VT_bailout( "invalid format flags: %08x", flags);
   return 0;  // Not reached
}

//
//  Size in bytes of a stream data block, including header.
//
static inline int block_size( int chans, int bsize, uint32_t flags)
{
   return sizeof( struct VT_BLOCK) + bsize * frame_size( chans, flags);
}

//
//  Choose a suitable stream block size (number of frames per block) to
//  suit the given sample rate.  For sample rates typical of soundcards we
//  use a fixed size of 8192.  For lower or higher rates a power of 2 is
//  selected to give between 3 and 25 blocks per second.
//
static inline int choose_bsize( int sample_rate)
{
   int bsize = 8192;

   if( sample_rate < 16000)
      while( bsize > 1 && sample_rate/bsize < 3) bsize /= 2;

   if( sample_rate > 192000)
      while( bsize < 262144 && sample_rate/bsize > 25) bsize *= 2;

   VT_report( 3, "chose bsize %d", bsize);
   return bsize;
}

///////////////////////////////////////////////////////////////////////////////
// Network Functions                                                         //
///////////////////////////////////////////////////////////////////////////////

static int net_connect( VTFILE *vtfile)
{
   VT_report( 3, "connect to host %s port %d", vtfile->host, vtfile->port);
 
   if( (vtfile->fh = socket( AF_INET, SOCK_STREAM, 0)) < 0)
      VT_bailout( "cannot open socket: %s", strerror( errno));

   struct hostent *dest = gethostbyname( vtfile->host);
   if( !dest) VT_bailout( "cannot reach %s: %s",
                              vtfile->host, strerror( errno));
  
   struct sockaddr_in sa;
   memset( &sa, 0, sizeof( sa));
   sa.sin_family = AF_INET;
   memcpy( &sa.sin_addr.s_addr, dest->h_addr, dest->h_length);
   sa.sin_port = htons( vtfile->port);

   while( 1)
   {
      if( connect( vtfile->fh, (struct sockaddr *) &sa, sizeof( sa)) >= 0)
         break;

      if( vtfile->type == VT_TYPE_NETP) usleep( 5000000);
      else
      {
         VT_report( 0, "unable to connect to %s,%d",
                           vtfile->host, vtfile->port);
         close( vtfile->fh);
         return FALSE;
      }
   }

   return TRUE;
}

static void net_listen_accept( VTFILE *vtfile)
{
   int sockfd;

   if( (sockfd = socket( AF_INET, SOCK_STREAM, 0)) < 0)
      VT_bailout( "cannot open inet socket: %s", strerror( errno));

   struct sockaddr_in sa;
   memset( &sa, 0, sizeof( sa));

   sa.sin_family = AF_INET;
   sa.sin_addr.s_addr = INADDR_ANY;
   sa.sin_port = htons( vtfile->port);

   int val = 1;
   if( setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof( val)) < 0)
      VT_bailout( "cannot set SO_REUSEADDR: %s", strerror( errno));

   if( bind( sockfd, (struct sockaddr *) &sa, sizeof( sa)) < 0)
      VT_bailout( "cannot bind inet socket: %s", strerror( errno));

   if( listen( sockfd, 1) < 0)
      VT_bailout( "listen failed: %s", strerror( errno));

   struct sockaddr_in ca;
   socklen_t ca_len = sizeof( ca);
   vtfile->fh = accept( sockfd, (struct sockaddr *) &ca, &ca_len);
   if( vtfile->fh < 0) VT_bailout( "accept failed: %s", strerror( errno));

   close( sockfd);
}
 
///////////////////////////////////////////////////////////////////////////////
// Output Functions                                                          //
///////////////////////////////////////////////////////////////////////////////

static void cvt_insert_float8( VTFILE *vtfile, double *frame)
{
   double *d = (double *) VT_data_p( vtfile) + 
               vtfile->nfb * vtfile->chans;
   memcpy( d, frame, sizeof( double) * vtfile->chans); 
}
static void cvt_insert_float4( VTFILE *vtfile, double *frame)
{
   float *d = (float *) VT_data_p( vtfile) + 
               vtfile->nfb * vtfile->chans;
   int i;
   for( i=0; i<vtfile->chans; i++) *d++ = frame[i];
}
static void cvt_insert_int4( VTFILE *vtfile, double *frame)
{
   int32_t *d = (int32_t *) VT_data_p( vtfile) + 
               vtfile->nfb * vtfile->chans;
   int i;
   for( i=0; i<vtfile->chans; i++)
   {
      double v = frame[i] * INT32_MAX;
      if( v < INT32_MIN) *d++ = INT32_MIN;
      else
      if( v > INT32_MAX) *d++ = INT32_MAX;
      else *d++ = v;
   }
}
static void cvt_insert_int2( VTFILE *vtfile, double *frame)
{
   int16_t *d = (int16_t *) VT_data_p( vtfile) + 
               vtfile->nfb * vtfile->chans;
   int i;
   for( i=0; i<vtfile->chans; i++)
   {
      int32_t v = lround( frame[i] * INT16_MAX);
      if( v < INT16_MIN) *d++ = INT16_MIN;
      else
      if( v > INT16_MAX) *d++ = INT16_MAX;
      else *d++ = v;
   }
}
static void cvt_insert_int1( VTFILE *vtfile, double *frame)
{
   int8_t *d = (int8_t *) VT_data_p( vtfile) + 
               vtfile->nfb * vtfile->chans;
   int i;
   for( i=0; i<vtfile->chans; i++)
   {
      double v = frame[i] * INT8_MAX;
      if( v < INT8_MIN) *d++ = INT8_MIN;
      else
      if( v > INT8_MAX) *d++ = INT8_MAX;
      else *d++ = v;
   }
}

//
//  Prepare the next data block to be written.
//
void VT_next_write( VTFILE *vtfile)
{
   if( vtfile->type == VT_TYPE_BUFFER)
   {
      vtfile->blk = vtlib_block( vtfile, vtfile->bhead->load);
   }
   else
   {
      memset( vtfile->blk, 0, sizeof( struct VT_BLOCK));
   }

   struct VT_BLOCK *bp = vtfile->blk;

   bp->magic = MAGIC_BLK;
   bp->flags = vtfile->flags;
   bp->bsize = vtfile->bsize;
   bp->chans = vtfile->chans;
   bp->sample_rate = vtfile->sample_rate;

   timestamp T = timestamp_add( vtfile->timebase,
            vtfile->nft/(long double)(vtfile->srcal * vtfile->sample_rate));

   bp->secs = timestamp_secs( T);
   bp->nsec = (uint32_t) (timestamp_frac( T)*1e9 + 0.5);
   bp->srcal = vtfile->srcal;
}

//
//  Called when the application has filled an output block and can release it
//  to the output stream.
//
void VT_release( VTFILE *vtfile)
{
   if( !vtfile->nfb) return;      // Don't output a buffer containing no frames

   vtfile->blk->frames = vtfile->nfb; // Load final frame count into data block
   vtfile->blk->spare = 0;
   vtfile->blk->valid = 1;              // Makes the block available to readers
 
   VT_report( 3, "write block: %s bs=%d", vtfile->name, vtfile->bs);

   if( vtfile->type == VT_TYPE_FILE)
   {
      if( write( vtfile->fh, vtfile->blk, vtfile->bs) != vtfile->bs)
         VT_exit( "write failed on %s: %s", vtfile->name, 
                      strerror( errno));
   }
   else
   if( vtfile->type == VT_TYPE_BUFFER)
   {
      struct VT_BUFFER *bhead = vtfile->bhead;
      bhead->load = (bhead->load + 1) % bhead->nblocks;
      vtlib_block( vtfile, bhead->load)->valid = 0;

      #if USE_SEM
      if( semctl( vtfile->semid, 0, SETVAL, 0) < 0)
         VT_bailout( "semctl: %s", strerror( errno));

      struct sembuf sem_lock = { 0, 1, 0 };
      if( semop( vtfile->semid, &sem_lock, 1) < 0)
         VT_bailout( "semop lock: %s", strerror( errno));
      #endif
   }
   else
   if( vtfile->type == VT_TYPE_NET ||
       vtfile->type == VT_TYPE_NETP)
   {
      while( write( vtfile->fh, vtfile->blk, vtfile->bs) != vtfile->bs)
      {
         if( vtfile->type == VT_TYPE_NET)
            VT_exit( "write failed on %s: %s", vtfile->name, 
                         strerror( errno));

         close( vtfile->fh);
         usleep( 5000000);
         if( !net_connect( vtfile))
            VT_exit( "write failed on %s: %s", vtfile->name, 
                         strerror( errno));
      }
   }
   else
      VT_bailout( "internal error: invalid file type");

   vtfile->bcnt++;            // Counts how many blocks outputed on this stream
   vtfile->nfb = 0;                      // Now zero frames buffered for output
}

void VT_set_timebase( VTFILE *vtfile, timestamp t, double srcal)
{
   if( !timestamp_is_ZERO(vtfile->timebase))
   {
      double shift = timestamp_diff( t, vtfile->timebase)
            - vtfile->nft/(double)(vtfile->srcal * vtfile->sample_rate);

      if( fabs( shift) > 1.0/ vtfile->sample_rate)
      {
         if( vtfile->nfb)
         {
            char temp[30];  timestamp_string6( t, temp);
            VT_report( 3, "release short frame %d shift %f t=%s nft=%d",
                             vtfile->nfb, shift, temp, (int) vtfile->nft);
            VT_release( vtfile);
         }
      }
   }

   vtfile->timebase = t;
   vtfile->srcal = srcal;
   vtfile->nft = 0;
}

//
//  Write a single frame into the output stream.  *frame points to the array
//  of samples.
//

void VT_insert_frame( VTFILE *vtfile, double *frame)
{
   if( !vtfile->nfb) VT_next_write( vtfile);

   vtfile->cvt_insert( vtfile, frame);

   vtfile->nfb++;
   vtfile->nft++;

   if( vtfile->nfb == vtfile->bsize) VT_release( vtfile);
}

//
//  Write several frames provided by the caller in raw 16 bit signed format.
//

void VT_insert_frames_i2( VTFILE *vtfile, int16_t *frames, int nframes)
{
   if( (vtfile->flags & VTFLAG_INT2) == 0)
      VT_bailout( "file/stream %s must have i2 format", vtfile->name);

   while( nframes > 0)
   {
      if( !vtfile->nfb) VT_next_write( vtfile);

      int n = vtfile->bsize - vtfile->nfb;
      if( n > nframes) n = nframes;   // Number of frames to fit in this block

      int16_t *d = (int16_t *) VT_data_p( vtfile) + 
                   vtfile->nfb * vtfile->chans;

      memcpy( d, frames, vtfile->chans * n * 2);

      frames += n * vtfile->chans;
      vtfile->nfb += n;
      vtfile->nft += n;
      nframes -= n;

      if( vtfile->nfb == vtfile->bsize) VT_release( vtfile);
   }
}

///////////////////////////////////////////////////////////////////////////////
// Output Stream Creation                                                    //
///////////////////////////////////////////////////////////////////////////////

static int create_net( VTFILE *vtfile)
{
   // vtfile->name is +host or ++host and anything trailing has already been
   // removed.
   if( vtfile->name[1] == '+')
   {
      vtfile->type = VT_TYPE_NETP;
      vtfile->host = strdup( vtfile->name + 2);
   }
   else
   {
      vtfile->type = VT_TYPE_NET;
      vtfile->host = strdup( vtfile->name + 1);
   }

   vtfile->blk = VT_malloc( vtfile->bs);
   return net_connect( vtfile);
}

static int create_file( VTFILE *vtfile)
{
   vtfile->type = VT_TYPE_FILE;

   VT_report( 3, "create_file: %s, bs=%d", vtfile->name, vtfile->bs);
 
   if( !strcmp( vtfile->name, "-")) vtfile->fh = 1;  // stdout
   else
   if( (vtfile->fh = open( vtfile->name, O_CREAT|O_TRUNC|O_WRONLY, 0644)) < 0)
         VT_bailout( "cannot create %s: %s", vtfile->name,
                                                strerror( errno));
   
   VT_report( 3, "create_file: done, fh=%d", vtfile->fh);

   vtfile->blk = VT_malloc( vtfile->bs);

   struct stat st;
   if( fstat( vtfile->fh, &st) < 0)
   {
      sprintf( VT_error, "cannot fstat %s: %s", vtfile->name, 
                                                   strerror( errno));
      return FALSE;
   }

   if( S_ISFIFO( st.st_mode))
   {
      VT_report( 2, "output is FIFO");

      // Open the fifo for input, just to hold the pipe open
      if( open( vtfile->name, O_RDONLY) < 0)
         VT_report( 2, "unable to hold pipe open");
   } 

   return TRUE;
}

#ifdef HAVE_SHM_OPEN
//
//  Create a circular buffer in Posix shared memory.
//
static char *create_buffer_and_map( VTFILE *vtfile, int blocks, int locked)
{
   // Remove leading '@' from buffer name and produce pseudo file name for
   // shared memory.
   char *filename;
   if( asprintf( &filename, "/%s", vtfile->name+1) < 0)
      VT_bailout( "out of memory");

   int bufsize = sizeof( struct VT_BUFFER) + blocks * vtfile->bs;

   int fh = shm_open( filename, O_CREAT | O_RDWR, 0644);
   if( fh < 0)
   {
      sprintf( VT_error, "cannot shm_open %s, %s",
                             filename, strerror( errno));
      free( filename);
      return NULL;
   }

   if( ftruncate( fh, bufsize) < 0) 
   {
      sprintf( VT_error, "cannot ftruncate %s, %s",
                             filename, strerror( errno));
      free( filename);
      return NULL;
   }

   int mapflags = MAP_SHARED;
   #if defined(MAP_LOCKED)
      if( locked) mapflags |= MAP_LOCKED;
   #endif
   char *mem = mmap( NULL, bufsize, PROT_READ | PROT_WRITE, mapflags, fh, 0);

   if( mem == MAP_FAILED)
   {
      sprintf( VT_error, "cannot mmap %s, %s\n", filename, strerror( errno));
      free( filename);
      return 0;
   }

   free( filename);
   close( fh);

   return mem;
}

#else
//
//  Posix shared memory not available, so create the buffer in a real file.
//
static char *create_buffer_and_map( VTFILE *vtfile, int blocks, int locked)
{
   // Remove leading '@' from buffer name and produce file name for
   // the buffer in BUFDIR.
   char *filename;
   if( asprintf( &filename, "%s/%s", BUFDIR, vtfile->name+1) < 0)
      VT_bailout( "out of memory");

   int bufsize = sizeof( struct VT_BUFFER) + blocks * vtfile->bs;

   int fh = open( filename, O_CREAT | O_RDWR, 0644);
   if( fh < 0)
   {
      sprintf( VT_error, "cannot create buffer file %s, %s",
                             filename, strerror( errno));
      free( filename);
      return NULL;
   }

   if( ftruncate( fh, bufsize) < 0) 
   {
      sprintf( VT_error, "cannot ftruncate %s, %s",
                             filename, strerror( errno));
      free( filename);
      close( fh);
      return NULL;
   }

   int mapflags = MAP_SHARED;
   #if defined(MAP_LOCKED)
      if( locked) mapflags |= MAP_LOCKED;
   #endif
   char *mem = mmap( NULL, bufsize, PROT_READ | PROT_WRITE, mapflags, fh, 0);

   if( mem == MAP_FAILED)
   {
      sprintf( VT_error, "cannot mmap %s, %s\n", filename, strerror( errno));
      free( filename);
      close( fh);
      return 0;
   }

   free( filename);
   close( fh);

   return mem;
}

#endif

static int create_buffer( VTFILE *vtfile, int blocks, int locked)
{
   vtfile->type = VT_TYPE_BUFFER;

   VT_report( 3, "create_buffer: %s, bs=%d", vtfile->name, vtfile->bs);
   VT_report( 1, "buffer size: %d blocks, %.3f seconds",
                  blocks, blocks * vtfile->bsize/(double) vtfile->sample_rate);

   char *mem = create_buffer_and_map( vtfile, blocks, locked);
   if( !mem) return FALSE;

   // Initialise the buffer's header block
   struct VT_BUFFER *bhead = (struct VT_BUFFER *) mem;
   vtfile->bhead = bhead;

   bhead->flags = vtfile->flags;
   bhead->bsize = vtfile->bsize;
   bhead->chans = vtfile->chans;
   bhead->sample_rate = vtfile->sample_rate;
   bhead->nblocks = blocks;
   bhead->load %= bhead->nblocks;

   key_t semkey = 0;
   int i;
   for( i=0; vtfile->name[i]; i++) semkey = (semkey << 1) ^ vtfile->name[i];
   bhead->semkey = semkey;

   // Initialise all the data blocks
   for( i=0; i< blocks; i++)
   {
      struct VT_BLOCK *sp = vtlib_block( vtfile, i);
      sp->valid = 0;
      sp->srcal = 1;
      sp->magic = MAGIC_BLK;
   }

   bhead->magic = MAGIC_BUF;
   VT_error[0] = 0;

   #if USE_SEM
   vtfile->semid = open_sem( bhead->semkey);
   if( vtfile->semid < 0) return 0;

   struct sembuf sem_lock = { 0, 1, 0 };
   if( semop( vtfile->semid, &sem_lock, 1) < 0)
   {
      sprintf( VT_error, "semop lock: %s", strerror( errno));
      return FALSE;
   }
   #endif

   vtlib_block( vtfile, bhead->load)->valid = 0;
   return TRUE;
}

//
// Called by the application to create an output stream.
//

VTFILE *VT_open_output( char *name, int chans, int locked, int sample_rate)
{
   VTFILE *vtfile = alloc_vtfile( name);
   uint32_t flags = VTFLAG_FLOAT8;     // Default data format
   int IVAL = -1;    // -1 indicating unspecified buffer length or port

   // Parse any comma-separated options at the end of the output name

   char *p = vtfile->name;
   while( p && (p = strchr( p, ',')) != NULL)
   {
      *p++ = 0;

      if( isdigit( *p)) IVAL = atoi( p); // Buffer length, or port number
      else
      if( !strncmp( p, "f4", 2)) flags = VTFLAG_FLOAT4;
      else
      if( !strncmp( p, "f8", 2)) flags = VTFLAG_FLOAT8;
      else
      if( !strncmp( p, "i4", 2)) flags = VTFLAG_INT4;
      else
      if( !strncmp( p, "i2", 2)) flags = VTFLAG_INT2;
      else
      if( !strncmp( p, "i1", 2)) flags = VTFLAG_INT1;
      else
         VT_bailout( "unrecognised output name syntax: %s", p);
   }

   vtfile->chans = chans;
   vtfile->sample_rate = sample_rate;
   vtfile->bsize = choose_bsize( sample_rate);
   vtfile->flags = flags;
   vtfile->bs = block_size( chans, vtfile->bsize, flags);

   int ok;
   if( vtfile->name[0] == '+')
   {
      // IVAL holds the destination port number
      if( IVAL < 0 || IVAL > 65535)
         VT_bailout( "invalid port number in %s", vtfile->name);

      vtfile->port = IVAL;
      ok = create_net( vtfile);
   }
   else
   if( vtfile->name[0] == '@')
   {
      // IVAL holds the buffer length in seconds
      if( IVAL < 0) IVAL = 10;    // Default buffer length 10 seconds
      else
      if( IVAL >= 0 && IVAL < 2)
         VT_bailout( "buffer too short: %d seconds", IVAL);
      else
      if( IVAL > 120)
         VT_bailout( "buffer too long: %d seconds", IVAL);

      int nblocks = (sample_rate * IVAL)/vtfile->bsize;
      ok = create_buffer( vtfile, nblocks, locked);
   }
   else
   {
      if( IVAL != -1)
         VT_report( 0, "integer option %d ignored in %s", IVAL, vtfile->name);
      ok = create_file( vtfile);
   }

   if( !ok)
   {
      free_vtfile( vtfile);
      return NULL;
   }

   // Hook up the output conversion function
   switch( vtfile->flags & VTFLAG_FMTMASK)
   {
      case VTFLAG_FLOAT8: vtfile->cvt_insert = cvt_insert_float8; break;
      case VTFLAG_FLOAT4: vtfile->cvt_insert = cvt_insert_float4; break;
      case VTFLAG_INT4:   vtfile->cvt_insert = cvt_insert_int4;   break;
      case VTFLAG_INT2:   vtfile->cvt_insert = cvt_insert_int2;   break;
      case VTFLAG_INT1:   vtfile->cvt_insert = cvt_insert_int1;   break;
   }

   return vtfile;
}

///////////////////////////////////////////////////////////////////////////////
// Input Functions                                                           //
///////////////////////////////////////////////////////////////////////////////

static void cvt_extract_float8( VTFILE *vtfile)
{
   double *data = VT_data_p( vtfile);
   vtfile->frame = data + vtfile->ulp * vtfile->chans;
}

static void cvt_extract_float4( VTFILE *vtfile)
{
   float *data = (float *)VT_data_p( vtfile) + 
                   vtfile->ulp * vtfile->chans;

   int i;
   for( i=0; i<vtfile->chans; i++)
      vtfile->frame[i] = data[i];
}
static void cvt_extract_int4( VTFILE *vtfile)
{
   int32_t *data = (int32_t *)VT_data_p( vtfile) + 
                   vtfile->ulp * vtfile->chans;

   int i;
   for( i=0; i<vtfile->chans; i++)
      vtfile->frame[i] = data[i]/(double) INT32_MAX;
}
static void cvt_extract_int2( VTFILE *vtfile)
{
   int16_t *data = (int16_t *)VT_data_p( vtfile) + 
                   vtfile->ulp * vtfile->chans;

   int i;
   for( i=0; i<vtfile->chans; i++)
      vtfile->frame[i] = data[i]/(double) INT16_MAX;
}
static void cvt_extract_int1( VTFILE *vtfile)
{
   int8_t *data = (int8_t *)VT_data_p( vtfile) + 
                   vtfile->ulp * vtfile->chans;

   int i;
   for( i=0; i<vtfile->chans; i++)
      vtfile->frame[i] = data[i]/(double) INT8_MAX;
}

static int open_file( VTFILE *vtfile)
{
   vtfile->type = VT_TYPE_FILE;

   if( !strcmp( vtfile->name, "-")) vtfile->fh = 0;   // stdin
   else
   {
      if( (vtfile->fh = open( vtfile->name, O_RDONLY)) < 0)
      {
         sprintf( VT_error, "cannot open %s: %s",
                  vtfile->name, strerror( errno));
         return 0;
      }
 
      struct stat st;
      if( fstat( vtfile->fh, &st) < 0)
      {
         sprintf( VT_error, "cannot fstat %s: %s", vtfile->name, 
                                                      strerror( errno));
         return 0;
      }
   
      if( S_ISFIFO( st.st_mode))
      {
         VT_report( 2, "input is FIFO");
   
         // Open the fifo for output, just to hold the pipe open
         if( open( vtfile->name, O_WRONLY) < 0)
            VT_report( 2, "unable to hold pipe open");
      }
   }

   return VT_read_next( vtfile);
}

#ifdef HAVE_SHM_OPEN
static char *open_buffer_and_map( VTFILE *vtfile)
{
   // Remove leading '@' from buffer name and produce pseudo file name for
   // shared memory.
   char *filename;
   if( asprintf( &filename, "/%s", vtfile->name+1) < 0)
      VT_bailout( "out of memory");

   // Open the buffer file in shared memory
   int fh = shm_open( filename, O_RDONLY, 0);
   if( fh < 0)
   {
      sprintf( VT_error, "cannot shm_open %s, %s",
                             filename, strerror( errno));
      free( filename);
      return NULL;
   }

   // To begin with, map just the header
   char *mem = mmap( NULL, sizeof( struct VT_BUFFER), 
                       PROT_READ, MAP_SHARED, fh, 0);
   if( mem == MAP_FAILED)
   {
      sprintf( VT_error, "cannot mmap shm %s, %s\n", 
                             filename, strerror( errno));
      free( filename);
      return NULL;
   }

   struct VT_BUFFER *p = (struct VT_BUFFER *) mem;

   // Work out the shared memory size and block size and remap the whole file
   vtfile->bs = block_size( p->chans, p->bsize, p->flags);
   int bufsize = sizeof( struct VT_BUFFER) + p->nblocks * vtfile->bs;
   munmap( mem, sizeof( struct VT_BUFFER));

   mem = mmap( NULL, bufsize, PROT_READ, MAP_SHARED, fh, 0);
   if( mem == MAP_FAILED)
   {
      sprintf( VT_error, "cannot mmap shm %s, %s\n", filename, strerror( errno));
      free( filename);
      return NULL;
   }

   close( fh); // No longer need the file handle now mapped
   free( filename);

   return mem;
}
#else
static char *open_buffer_and_map( VTFILE *vtfile)
{
   // Remove leading '@' from buffer name and produce file name for
   // the buffer in BUFDIR.
   char *filename;
   if( asprintf( &filename, "%s/%s", BUFDIR, vtfile->name+1) < 0)
      VT_bailout( "out of memory");

   // Open the buffer file
   int fh = open( filename, O_RDONLY, 0);
   if( fh < 0)
   {
      sprintf( VT_error, "cannot open buffer file %s, %s",
                             filename, strerror( errno));
      free( filename);
      return NULL;
   }

   // To begin with, map just the header
   char *mem = mmap( NULL, sizeof( struct VT_BUFFER), 
                       PROT_READ, MAP_SHARED, fh, 0);
   if( mem == MAP_FAILED)
   {
      sprintf( VT_error, "cannot mmap %s, %s\n", 
                             filename, strerror( errno));
      free( filename);
      return NULL;
   }

   struct VT_BUFFER *p = (struct VT_BUFFER *) mem;

   // Work out the shared memory size and block size and remap the whole file
   vtfile->bs = block_size( p->chans, p->bsize, p->flags);
   int bufsize = sizeof( struct VT_BUFFER) + p->nblocks * vtfile->bs;
   munmap( mem, sizeof( struct VT_BUFFER));

   mem = mmap( NULL, bufsize, PROT_READ, MAP_SHARED, fh, 0);
   if( mem == MAP_FAILED)
   {
      sprintf( VT_error, "cannot mmap %s, %s\n", filename, strerror( errno));
      free( filename);
      return NULL;
   }

   close( fh); // No longer need the file handle now mapped
   free( filename);

   return mem;
}
#endif

static int open_buffer( VTFILE *vtfile)
{
   vtfile->type = VT_TYPE_BUFFER;

   char *mem = open_buffer_and_map( vtfile);
   if( !mem) return FALSE;

   vtfile->bhead = (struct VT_BUFFER *) mem;
   vtfile->sample_rate = vtfile->bhead->sample_rate;
   vtfile->bsize = vtfile->bhead->bsize;
   vtfile->chans = vtfile->bhead->chans;
   vtfile->flags = vtfile->bhead->flags;

   VT_error[0] = 0;

   // Get a handle to the buffer's reader semaphore
   #if USE_SEM
   vtfile->semid = 0;
   vtfile->semid = open_sem( vtfile->bhead->semkey);
   if( vtfile->semid < 0) return 0;
   #endif

   vtfile->readp = vtfile->bhead->load;

   return TRUE;
}

static int open_listen( VTFILE *vtfile)
{
   vtfile->type = vtfile->name[1] == '+' ? VT_TYPE_NETP : VT_TYPE_NET;

   // name:   +[+]port:chans  
   vtfile->port = atoi( vtfile->name[1] == '+' ?
                      vtfile->name+2 : vtfile->name+1);
   if( vtfile->port < 0 || vtfile->port > 65535)
      VT_bailout( "bad network port spec");

   net_listen_accept( vtfile);
   return VT_read_next( vtfile); 
} 

//
// Called by the application to open a stream for input.  Blocks until the
// first data block is available.
//
VTFILE *VT_open_input( char *name)
{
   VTFILE *vtfile = alloc_vtfile( name);

   // Open the input stream, of whatever type.  These functions block until
   // they have read the first block of stream data.  When they have returned
   // vtfile is filled in with the stream parameters.
   int ok;

   if( name[0] == '+') ok = open_listen( vtfile);
   else
   if( name[0] == '@') ok = open_buffer( vtfile);
   else
      ok = open_file( vtfile);

   if( !ok)
   {
      free_vtfile( vtfile);
      return NULL;
   }

   // Hook up the appropriate frame extractor function for the data type
   switch( vtfile->flags & VTFLAG_FMTMASK)
   {
      case VTFLAG_FLOAT8:  vtfile->cvt_extract = cvt_extract_float8;
                           break;
      case VTFLAG_FLOAT4:  vtfile->cvt_extract = cvt_extract_float4;
                           break;
      case VTFLAG_INT4:    vtfile->cvt_extract = cvt_extract_int4;
                           break;
      case VTFLAG_INT2:    vtfile->cvt_extract = cvt_extract_int2;
                           break;
      case VTFLAG_INT1:    vtfile->cvt_extract = cvt_extract_int1;
                           break;
   }

   // If the buffer contains float8, the 'current frame' pointer can just
   // point into the mapped buffer.  Otherwise we allocate a single frame as
   // a current frame cache.

   if( (vtfile->flags & VTFLAG_FMTMASK) != VTFLAG_FLOAT8)
      vtfile->frame = VT_malloc( vtfile->chans * sizeof( double));

   return vtfile;
}

void VT_close( VTFILE *vtfile)
{
   if( vtfile->type == VT_TYPE_FILE)
   {
      if( vtfile->fh) close( vtfile->fh);
      if( vtfile->blk) free( vtfile->blk);
   }
   if( (vtfile->flags & VTFLAG_FMTMASK) != VTFLAG_FLOAT8 &&
       vtfile->frame) free( vtfile->frame);
   free( vtfile);
}

//
//  Low-level read from an input stream.
//
static int readn( VTFILE *vtfile, char *p, int nb)
{
   int nd = 0, nr;
   while( nd < nb)
   {
      if( (nr=read( vtfile->fh, p + nd, nb - nd)) <= 0)
      {
         sprintf( VT_error, "readn failed: %s", strerror( errno));
         return 0;
      }
      nd += nr;
   }

   return 1;
}

static int resync( VTFILE *vtfile)
{
   VT_report( 1, "Resync on %s...", vtfile->name);

   // Read byte-at-a-time until a block header is found
   uint32_t m = MAGIC_BLK;
   char *p = (char *)&m;

   char c;
   int n = 0;
   while( n < 4)
   {
      if( readn( vtfile, &c, 1) <= 0) return 0;
      if( c == p[n]) n++;
      else n = 0; 
   }

   // Read of the rest of the header
   struct VT_BLOCK blk;
   if( readn( vtfile, 4 + (char *) &blk, 
              sizeof( struct VT_BLOCK) - 4) <= 0) return 0;

   // Check that the stream parameters haven't changed
   if( vtfile->bs)
      if( vtfile->sample_rate != blk.sample_rate ||
          vtfile->chans != blk.chans ||
          vtfile->flags != blk.flags ||
          vtfile->bsize != blk.bsize)
         VT_bailout( "parameters changed on %s", vtfile->name);

   // Dummy read of the block's data
   int bs = block_size( blk.chans, vtfile->bsize, blk.flags)
                   - sizeof( struct VT_BLOCK);
   p = VT_malloc( vtfile->bs);
   int e = readn( vtfile, p, bs);
   free( p);
   return e;
}

//
// Called by the application to bring in the next data block from the stream.
//
int VT_read_next( VTFILE *vtfile)
{
   if( vtfile->type == VT_TYPE_FILE)
   {
      if( !vtfile->bs)  // First read?
      {
         // Don't know the stream parameters yet, so just read the header,
         // resyncing if necessary
         vtfile->blk = VT_realloc( vtfile->blk, sizeof( struct VT_BLOCK));
         while( 1)
         {
            if( !readn( vtfile, (char *)vtfile->blk, 
                        sizeof( struct VT_BLOCK))) return FALSE;
            if( vtfile->blk->magic == MAGIC_BLK) break;
            if( !resync( vtfile)) return FALSE;
         }
     
         vtfile->chans = vtfile->blk->chans;
         vtfile->flags = vtfile->blk->flags;
         vtfile->bsize = vtfile->blk->bsize;
         vtfile->sample_rate = vtfile->blk->sample_rate;

         // Now we can work out the block size and read the rest of the block
         vtfile->bs = block_size( vtfile->chans, vtfile->bsize, vtfile->flags);
         vtfile->blk = VT_realloc( vtfile->blk, vtfile->bs);
         if( !readn( vtfile, VT_data_p( vtfile),
                     vtfile->bs - sizeof( struct VT_BLOCK))) return FALSE;
         VT_report( 3, "initial blk ok bs=%d chans=%d",
                     vtfile->bs, vtfile->chans);
      }
      else
      {
         while( 1)
         {
            if( !readn( vtfile, (char *)vtfile->blk, vtfile->bs)) return FALSE;
            if( vtfile->blk->magic == MAGIC_BLK) break;
            if( !resync( vtfile)) return FALSE;
         }
         if( vtfile->blk->sample_rate != vtfile->sample_rate ||
             vtfile->blk->flags != vtfile->flags)
            VT_bailout( "parameters changed on %s", vtfile->name);
      }
   }
   else
   if( vtfile->type == VT_TYPE_BUFFER)
   {
      // Point into the shared memory of the buffer, to the next block to read
      vtfile->blk = vtlib_block( vtfile, vtfile->readp);

      // If its timestamp is old, or the producer has invalidated it, then
      // block on a semaphore.  A single semaphore for the whole buffer. 
      // The producer releases all processes waiting on it whenever it writes
      // any block.
      while( vtfile->blk->secs < vtfile->secs ||
             !vtfile->blk->valid)
      {
         #if USE_SEM
         struct sembuf sem_wait = { 0, 0, 0 };
         while( (semop( vtfile->semid, &sem_wait, 1)) < 0)
         {
            if( errno != EINTR)
               VT_bailout( "VT_read_next semop failed, %s", strerror( errno));
            usleep( 50);
         }
         #else
            // Suspend for a while, long enough for a block of data to be
            // generated (assuming real time rate) at this sample rate.

            usleep( vtfile->bsize * 1e6 / vtfile->sample_rate);
         #endif
      }

      // This happens if the producer process is stopped and restarted with
      // different format or number of channels 
      if( vtfile->blk->magic != MAGIC_BLK) 
          VT_bailout( "corrupt buffer - parameters changed?");

      vtfile->readp = (vtfile->readp + 1) % vtfile->bhead->nblocks;
      if( !vtfile->rbreak && vtfile->secs)
      {
         if( vtfile->blk->secs > vtfile->secs + 2) vtfile->rbreak = 2;
      }
   }
   else
   if( vtfile->type == VT_TYPE_NET ||
       vtfile->type == VT_TYPE_NETP)
   {
      restart_net:

      if( !vtfile->bs)  // First read?
      {
         // Read the header.  Set up the stream parameters from the header of
         // the first data block.
         vtfile->blk = VT_realloc( vtfile->blk, sizeof( struct VT_BLOCK));

         if( !readn( vtfile, (char *)vtfile->blk, 
                     sizeof( struct VT_BLOCK))) return FALSE;
         if( vtfile->blk->magic != MAGIC_BLK) return FALSE;
     
         vtfile->chans = vtfile->blk->chans;
         vtfile->flags = vtfile->blk->flags;
         vtfile->bsize = vtfile->blk->bsize;
         vtfile->sample_rate = vtfile->blk->sample_rate;

         // Now we can work out the block size and read the rest of the block
         vtfile->bs = block_size( vtfile->chans, vtfile->bsize, vtfile->flags);
         vtfile->blk = VT_realloc( vtfile->blk, vtfile->bs);
         if( !readn( vtfile, VT_data_p( vtfile),
                     vtfile->bs - sizeof( struct VT_BLOCK))) return FALSE;
         VT_report( 3, "initial blk ok bs=%d chans=%d",
                     vtfile->bs, vtfile->chans);
      }
      else
      {
         if( !readn( vtfile, (char *)vtfile->blk, vtfile->bs) ||
             vtfile->blk->magic != MAGIC_BLK)
         {
            if( vtfile->type == VT_TYPE_NETP)
            {
               VT_report( 2, "calling net_listen_accept");
               close( vtfile->fh);
               usleep( 1000000);
               net_listen_accept( vtfile);
               goto restart_net;
            }
            return FALSE;
         }

         if( vtfile->blk->sample_rate != vtfile->sample_rate ||
             vtfile->blk->flags != vtfile->flags)
            VT_bailout( "parameters changed on %s", vtfile->name);
      }
   }
   else
      VT_bailout( "invalid file type: %d", vtfile->type);

   //
   // Check to see if there's a timing break.  If timestamp of the new block
   // is more than 1 sample away from where expected, based on the timestamp
   // and sample rate of the previous block, then declare a timing break.
   //
   timestamp t = timestamp_compose( vtfile->blk->secs, vtfile->blk->nsec/1e9);
   if( !timestamp_is_ZERO( vtfile->timebase) && !vtfile->rbreak)
   {
      double shift = timestamp_diff( t, vtfile->timebase) - 
            vtfile->ulp/(double)(vtfile->srcal * vtfile->sample_rate);

      if( fabs( shift) > 1.0/ vtfile->sample_rate)
      {
         VT_report( 1, "break detected on %s: %.9f", vtfile->name, shift);
         vtfile->rbreak = 1;
      }
   }

   vtfile->timebase = t;
   vtfile->secs = vtfile->blk->secs;
   vtfile->nfb = vtfile->blk->frames;         // Number of frames in this block
   vtfile->srcal = vtfile->blk->srcal;
   vtfile->ulp = 0;           // Frame index into the data - next frame to read
   vtfile->bcnt++;                       // Count of blocks read on this stream

   return TRUE;
}

int VT_poll( VTFILE *vtfile, double timeout)
{
   if( vtfile->type == VT_TYPE_BUFFER)
   {
      // Point into the shared memory of the buffer, to the next block to read
      vtfile->blk = vtlib_block( vtfile, vtfile->readp);

      // Test for data available
      return vtfile->blk->secs >= vtfile->secs && vtfile->blk->valid;
   }

   struct timeval tv = { (int)timeout,
                         (int)((timeout - (int)timeout)*1e6) };
   
   while( 1)
   {
      fd_set fds; FD_ZERO( &fds); FD_SET( vtfile->fh, &fds);
    
      int e = select( vtfile->fh+1, &fds, NULL, NULL, &tv); 
      if( e > 0) return 1;
      if( !e) return 0;

      if( errno != EINTR) VT_bailout( "select error on %s: %s",
                                             vtfile->name, strerror( errno));
   }
}

int VT_get_frames_i2( VTFILE *vtfile, int16_t *frames, int nframes)
{
   if( (vtfile->flags & VTFLAG_INT2) == 0)
      VT_bailout( "file/stream %s must have i2 format", vtfile->name);

   int nread = 0;   // Number of frames read into *frames

   while( nread < nframes)
   {
      if( !vtfile->nfb && !VT_read_next( vtfile)) break;

      int n = nframes - nread;
      if( n > vtfile->nfb) n = vtfile->nfb;

      int16_t *s = (int16_t *)VT_data_p( vtfile) + 
                   vtfile->ulp * vtfile->chans;
      memcpy( frames, s, n * 2 * vtfile->chans);

      frames += n * vtfile->chans;
      vtfile->nfb -= n;
      vtfile->ulp += n;

      nread += n;
   }

   return nread;
}


///////////////////////////////////////////////////////////////////////////////
// Utilities                                                                 //
///////////////////////////////////////////////////////////////////////////////

static void handle_signals( int sig)
{
   VT_bailout( "signal %d, %s", sig, strsignal( sig));
}

//
//  Initialise once at the start of a program.
//
void VT_init( char *name)
{
   progname = strdup( name);
   setenv( "TZ", "", 1);

   int i;

   //  Some sanity checks on the compilation
   if( (i = sizeof( float)) != 4)
      VT_bailout( "float incorrect size: %d", i);
   if( (i = sizeof( double)) != 8)
      VT_bailout( "double incorrect size: %d", i);

   if( (i = sizeof( struct VT_BUFFER)) != 32)
      VT_bailout( "VT_HEADER incorrect size: %d", i);
   if( (i = sizeof( struct VT_BLOCK)) != 48)
      VT_bailout( "VT_BLOCK incorrect size: %d", i);

   // Might need this on some systems to prevent the coprocessor from
   // converting our long double arithmetic to double precision.
   #if defined(__ia64__) || defined(__i386__)
      uint16_t fpu_mode = 0x33f;
      asm ("fldcw %0" : : "m" (fpu_mode));
   #endif

   // This file is opened just so that vtps and vttop can find all the
   // VT processes.  We open a FIFO so that programs such as tmpwatch won't
   // remove it during tmp cleanups.
   #ifdef HAVE_MKFIFO
      int r = mkfifo( VTPS_FILE, S_IFIFO | 0666);
   #else
      int r = mknod( VTPS_FILE, S_IFIFO | 0666, 0);
   #endif
   if( r < 0 && errno != EEXIST)
      VT_report( 0, "cannot create %s: %s", VTPS_FILE, strerror( errno));
   else
   if( (psfs_handle = open( VTPS_FILE, O_RDONLY | O_NONBLOCK, 0)) < 0)
      VT_report( 0, "cannot open %s: %s", VTPS_FILE, strerror( errno));

   struct sigaction sa;
   sa.sa_handler = handle_signals;
   sigemptyset( &sa.sa_mask);
   sa.sa_flags = 0;
   sigaction( SIGINT, &sa, NULL);
   sigaction( SIGHUP, &sa, NULL);
   sigaction( SIGQUIT, &sa, NULL);
   sigaction( SIGTERM, &sa, NULL);
   sigaction( SIGFPE, &sa, NULL);
   sigaction( SIGBUS, &sa, NULL);
   sigaction( SIGSEGV, &sa, NULL);
   sigaction( SIGURG, &sa, NULL);
   sigaction( SIGSYS, &sa, NULL);

   #if HAVE_SIGPWR
      sigaction( SIGPWR, &sa, NULL);
   #endif

   sa.sa_handler = SIG_IGN;
   sigemptyset( &sa.sa_mask);
   sa.sa_flags = 0;
   sigaction( SIGPIPE, &sa, NULL);
}

void VT_set_logfile( char *format, ...)
{
   if( logfile) free( logfile);

   va_list ap;
   int n;
   va_start( ap, format); n = vasprintf( &logfile, format, ap); va_end( ap);
   if( n < 0) VT_bailout( "out of memory");
}

void VT_up_loglevel( void)
{
   loglevel++;
}

void VT_report( int level, char *format, ...)
{   
   va_list ap;
   char temp[ 300];

   if( loglevel < level) return;

   va_start( ap, format); vsprintf( temp, format, ap); va_end( ap);

   if( !in_background) fprintf( stderr, "%s: %s\n", progname, temp);

   if( !logfile || !logfile[0]) return;

   time_t now = time( NULL);
   struct tm *tm = gmtime( &now);
   FILE *flog;
      
   if( (flog = fopen( logfile, "a+")) == NULL)
      VT_bailout( "cannot open logfile [%s]: %s", logfile, strerror( errno));
    
   fprintf( flog, "%04d/%02d/%02d %02d:%02d:%02d %s\n",
                tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec, temp);
   fclose( flog);
}

static void vtlib_exit( int status, char *msg) __attribute__ ((noreturn));
static void vtlib_exit( int status, char *msg)
{
   static int bailout_flag = FALSE;

   if( bailout_flag) exit( 1);
   bailout_flag = TRUE;                         // Prevents any re-entrance

   if( bailout_hook) bailout_hook();

   VT_report( status ? -1 : 1, "terminating: %s", msg);
   exit( status);
}

void VT_bailout( char *format, ...)
{
   va_list ap;
   char temp[ 200];

   va_start( ap, format);
   vsprintf( temp, format, ap);
   va_end( ap);

   vtlib_exit( 1, temp);
}

void VT_exit( char *format, ...)
{
   va_list ap;
   char temp[ 200];

   va_start( ap, format);
   vsprintf( temp, format, ap);
   va_end( ap);

   vtlib_exit( 0, temp);
}

void VT_bailout_hook( void (*fcn)(void))
{
   bailout_hook = fcn;
}

void VT_daemonise( int flags)
{
   int childpid, fd;
   long open_max = OPEN_MAX;

   if( (childpid = fork()) < 0)
      VT_bailout( "cannot fork: %s", strerror( errno));
   else if( childpid > 0) exit( 0);

   #if SETPGRP_VOID
      if( setpgrp() == -1) VT_bailout( "cannot setpgrp");
   #else
      if( setpgrp(0,0) == -1) VT_bailout( "cannot setpgrp");
   #endif

   #ifdef TIOCNOTTY
      if( (fd = open( "/dev/tty", O_RDWR)) >= 0)
      {
         ioctl( fd, TIOCNOTTY, 0);
         close( fd);
      }
   #endif /* TIOCNOTTY */

   if( (childpid = fork()) < 0)
      VT_bailout( "cannot fork: %s", strerror( errno));
   else if( childpid > 0) exit( 0);

   for( fd = 0; fd < open_max; fd++)
   {
      if( fd == 0 && (flags & KEEP_STDIN)) continue;
      if( fd == 1 && (flags & KEEP_STDOUT)) continue;
      if( fd == 2 && (flags & KEEP_STDERR)) continue;
      if( fd == psfs_handle) continue;
      close( fd);
   }

   in_background = 1;
}

void *VT_malloc( size_t size)
{
   void *p = malloc( size);
   if( !p) VT_bailout( "out of memory with alloc of %ld", (long) size);
   return p;
}

void *VT_malloc_zero( size_t size)
{
   void *p = malloc( size);
   if( !p) VT_bailout( "out of memory with alloc of %ld", (long) size);

   memset( p, 0, size); 
   return p;
}

void *VT_realloc( void *ptr, size_t size)
{
   void *p = realloc( ptr, size);
   if( !p) VT_bailout( "out of memory with realloc of %ld", (long) size);
   return p;
}

//
//  Scan an input stream name to parse any comma-separated list of channel
//  numbers.
//
struct VT_CHANSPEC *VT_parse_chanspec( char *bname)
{
   struct VT_CHANSPEC *sp = VT_malloc_zero( sizeof( struct VT_CHANSPEC));

   if( !bname) return sp;

   char *p = strchr( bname, ':');
   if( !p) return sp;

   char *q = p++;

   while( p && *p)
   {
      if( *p == ',') { p++; continue; }

      int c = atoi( p);
      if( c < 1) VT_bailout( "invalid channel spec in %s", bname);

      if( !sp->map) sp->map = VT_malloc( sizeof( int));
      else sp->map = VT_realloc( sp->map, sizeof( int) * (sp->n + 1));

      sp->map[sp->n++] = c - 1;
      p = strchr( p, ',');
   }

   *q = 0;
   return sp;
}

void VT_init_chanspec( struct VT_CHANSPEC *sp, VTFILE *vtfile)
{
   int i;

   if( !sp->map)
   {
      sp->n = vtfile->chans;
      sp->map = VT_malloc( sizeof( int) * sp->n);
      for( i=0; i<sp->n; i++) sp->map[i] = i;
   }

   for( i=0; i<sp->n; i++)
     VT_report( 1, "selected channel: %d = %s:%d", 
                      i+1, vtfile->name, sp->map[i]+1); 
   for( i=0; i<sp->n; i++)
      if( sp->map[i] >= vtfile->chans)
         VT_bailout( "channel %d not present in %s",
             sp->map[i]+1, vtfile->name);
}

static int atoin( char *s, int n)
{
   int val = 0;

   while( n-- && isdigit( *s)) val = val * 10 + (*s++ - '0');
   return val;
}

//
//  Parse a string timestamp in our standard format, turn it into a long
//  double.
//
timestamp VT_parse_timestamp( char *stamp)
{
   char *s = stamp;

   //
   // Deal with some aliases
   //   
   if( !strcasecmp( s, "now")) return VT_rtc_time();

   if( !strcasecmp( s, "today"))
   {
      int day = timestamp_secs( VT_rtc_time())/ 86400;
      return timestamp_compose( day * 86400, 0);
   }

   if( !strcasecmp( s, "yesterday"))
   {
      int day = timestamp_secs( VT_rtc_time())/ 86400L;
      return timestamp_compose( (day-1) * 86400, 0);
   }

   int secs = 0;
   double frac = 0;

   // Test for real number representing unix epoch time
   char *t = stamp;
   while( *t && (isdigit( *t) || *t == '.')) t++;
   if( !*t)
   {
      secs = atol( stamp);
      t = strchr( stamp, '.');
      if( t) frac = atof( t);
      return timestamp_compose( secs, frac);
   }

   struct tm tm;
   memset( &tm, 0, sizeof( struct tm));

   // yyyy-mm-dd
   // 0123456789
   if( isdigit( s[0]) &&
       isdigit( s[1]) &&
       isdigit( s[2]) &&
       isdigit( s[3]) &&
       (s[4] == '-' || s[4] == '/') &&
       isdigit( s[5]) &&
       isdigit( s[6]))
   {
      tm.tm_year = atoin( s, 4) - 1900;
      tm.tm_mon = atoin( s+5, 2) - 1;
      s += 7;
   }
   else
   // yyyymmdd
   // 01234567
   if( isdigit( s[0]) &&
       isdigit( s[1]) &&
       isdigit( s[2]) &&
       isdigit( s[3]) &&
       isdigit( s[4]) &&
       isdigit( s[5]))
   {
      tm.tm_year = atoin( s, 4) - 1900;
      tm.tm_mon = atoin( s+4, 2) - 1;
      s += 6;
   }
   else VT_bailout( "invalid year-month in timestamp: [%s]", stamp);

   if( *s)
   {
      if( (s[0] == '-' || s[0] == '/') &&
          isdigit( s[1]) &&
          isdigit( s[2])) { tm.tm_mday = atoin( s+1, 2); s += 3; }
      else
      if( isdigit( s[0]) &&
          isdigit( s[1])) { tm.tm_mday = atoin( s, 2); s += 2; }
      else
         VT_bailout( "invalid day in timestamp: [%s]", stamp);

   }
   else VT_bailout( "invalid day in timestamp: [%s]", stamp);

   // Separator between date and time.
   // Space or underscore or UT or t or T or nothing

   if(*s == ' ' || *s == '_' || *s == 't' || *s == 'T') s++;
   else
   if( !strncasecmp( s, "UT", 2)) s += 2;
   else
   if( *s) VT_bailout( "invalid timestamp syntax: [%s]", stamp);

   // hh[:mm[:ss[.ssss...]]] 

   if( *s)
   {
      if( !isdigit( s[0]) ||
          !isdigit( s[1]))
         VT_bailout( "invalid hour in timestamp: [%s]", stamp);

      tm.tm_hour = atoin( s, 2); s += 2;
   }

   if( *s)
   {
      if( *s == ':' &&
          isdigit( s[1])  &&
          isdigit( s[2])) { tm.tm_min = atoin( s+1, 2); s += 3; }
      else
      if( isdigit( s[0]) &&
          isdigit( s[1])) { tm.tm_min = atoin( s, 2); s += 2; }
      else
         VT_bailout( "invalid minute in timestamp: [%s]", stamp);
   }

   if( *s)
   {
      if( *s == ':' &&
          isdigit( s[1]) &&
          isdigit( s[2]))
         frac = atof( s+1);
      else
      if( isdigit( s[0]) &&
          isdigit( s[1])) frac = atof( s);
      else
         VT_bailout( "invalid second in timestamp: [%s]", stamp);

   }

   secs = mktime( &tm);
   if( secs < 0) VT_bailout( "bad date in timestamp: [%s]", stamp);

   return timestamp_normalise( timestamp_compose( secs, frac));
}

void VT_parse_timespec( char *s, timestamp *TS, timestamp *TE)
{
   *TS = *TE = timestamp_ZERO;

   char *p, temp[100];
   if( *s != ',')
   {
      strcpy( temp, s);
      if( (p = strchr( temp, ',')) != NULL) *p = 0;
      s += strlen( temp);
      *TS = VT_parse_timestamp( temp);
   }

   if( !*s) return;

   if( *s != ',') VT_bailout( "invalid timespec");
   s++;
   if( *s)
   {
      if( *s == '+')
      {
         double offset = 0;
         char mc;
         if( sscanf( s+1, "%lf%c", &offset, &mc) == 2)
         {
            switch( tolower( mc))
            {
               case 'w': offset *= 7 * 86400;   break;
               case 'd': offset *= 86400;       break;
               case 'h': offset *= 3600;        break;
               case 'm': offset *= 60;          break;
               case 's': break;
               default: VT_bailout( "unrecognised unit in timespec: %s", s+1);
            }
         }
         else offset = atof( s+1);
         *TE = timestamp_add( *TS,  offset);
      }
      else
         *TE = VT_parse_timestamp( s);
   }
}

//
// Timestamp from filename.
// yymmdd-hhmmss
// 0123456789012
//
timestamp VT_timestamp_from_filename( char *filename)
{
   if( !isdigit( filename[0]) || !isdigit( filename[1]) ||
       !isdigit( filename[2]) || !isdigit( filename[3]) ||
       !isdigit( filename[4]) || !isdigit( filename[5]) ||
       filename[6] != '-' ||
       !isdigit( filename[7]) || !isdigit( filename[8]) ||
       !isdigit( filename[9]) || !isdigit( filename[10]) ||
       !isdigit( filename[11]) || !isdigit( filename[12]) ||
       filename[13]) return timestamp_NONE;

   struct tm tm;
   memset( &tm, 0, sizeof( struct tm));
   tm.tm_year = 100 + atoin( filename, 2);
   tm.tm_mon = atoin( filename+2, 2) - 1;
   tm.tm_mday = atoin( filename+4, 2);
   tm.tm_hour = atoin( filename+7, 2);
   tm.tm_min = atoin( filename+9, 2);
   tm.tm_sec = atoin( filename+11, 2);

   return timestamp_compose( mktime( &tm), 0);
}

void VT_format_timestamp( char *s, timestamp T)
{
   time_t sec = timestamp_secs( T);

   int d = 0.5 + 1e6 * timestamp_frac(T);
   if( d == 1000000) { d = 0; sec++; }

   struct tm *tm = gmtime( &sec);

   sprintf( s, "%04d-%02d-%02d_%02d:%02d:%02d.%06d",
       tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday,
       tm->tm_hour, tm->tm_min, tm->tm_sec, d);
}

void VT_parse_freqspec( char *s, double *FS, double *FE)
{
   *FS = *FE = 0;

   char *p, temp[100];
   if( *s != ',') 
   {
      strcpy( temp, s);
      if( (p = strchr( temp, ',')) != NULL) *p = 0;
      s += strlen( temp);
      *FS = atof( temp);
   }  
   
   if( !*s)
   {
      *FE = *FS;
      return;
   }  
   
   if( *s != ',') VT_bailout( "invalid freqspec");
   s++;
   if( *s)
   {
      if( *s == '+') *FE = *FS + atof( s+1);
      else
         *FE = atof( s);
   }
}

void VT_parse_polarspec( int chans, char *polarspec,
                         int *ch_H1, double *align_H1,
                         int *ch_H2, double *align_H2,
                         int *ch_E)
{
   int ch = 0;
   int n = 0;

   *ch_H1 = *ch_H2 = *ch_E = -1;

   char *spec = strdup( polarspec);
   while( spec && *spec)
   {
      if( ch == chans)
         VT_bailout( "too many channels in -p for %d input channels", chans);

      char *p = strchr( spec, ',');  if( p) *p++ = 0;

      if( *spec == 'n' || *spec == 'N') ch++;
      else
      if( *spec == 'e' || *spec == 'E')
      {
         if( *ch_E >= 0) VT_bailout( "more than one E-field given to -p");
         *ch_E = ch++;
      }
      else
      if( isdigit( spec[0]))
      {
         if( n++ == 2) VT_bailout( "only two loops allowed with -p");
         double a = atof( spec);
         if( a < 0 || a >= 360)
            VT_bailout( "invalid -p argument [%s]", spec);

         a *= M_PI/180;
         if( n == 1) { *ch_H1 = ch; *align_H1 = a; }
         if( n == 2) { *ch_H2 = ch; *align_H2 = a; }
         ch++;
      }
      else
         VT_bailout( "unrecognised polar specifier [%s]", spec);
      spec = p;
   }

   if( ch < chans)
      VT_bailout( "not enough -p channels for %d input channels", chans);

   if( *ch_H1 * *ch_H2 < 0)
      VT_bailout( "must have two loops specified for polar");
}

//
//  This is an ugly function.
//
int VT_parse_complex( char *s, complex double *cp)
{
   double real, imag, mag, phase;
   char j;

   // A+Bj
   if( sscanf( s, "%lf%lf%c", &real, &imag, &j) == 3)
   {
      if( j != 'j' && j != 'J') return 0;
      *cp = real + I * imag;
      return 1;
   }

   // M/P
   if( sscanf( s, "%lf/%lf", &mag, &phase) == 2)
   {
      *cp = mag * (cos(phase*M_PI/180) + I*sin(phase*M_PI/180));
      return 1;
   }

   // Bj
   if( sscanf( s, "%lf%c", &imag, &j) == 2)
      if( j == 'j' || j == 'J') 
      {
         *cp = I*imag;
         return 1;
      }

   // A
   if( sscanf( s, "%lf", &real) == 1)
   {
      *cp = real;
      return 1;
   }

   // j
   if( s[0] == 'j' || s[0] == 'J')
   {
      *cp = I;
      return 1;
   }

   // j
   if( s[0] == '-' && (s[1] == 'j' || s[1] == 'J'))
   {
      *cp = -I;
      return 1;
   }

   *cp = 0;
   return 0;
}

