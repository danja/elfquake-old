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

///////////////////////////////////////////////////////////////////////////////
//  Definitions                                                              //
///////////////////////////////////////////////////////////////////////////////

//
// Header for lock-free buffers
//

struct VT_BUFFER {
   int32_t magic;
   uint32_t flags;              // See VT_FLAG_* below
   uint32_t nblocks;            // Number of blocks in the buffer
   uint32_t bsize;              // Number of frames per block
   uint32_t chans;              // Number of channels per frame;
   int32_t semkey;
   volatile uint32_t load;      // Index of most recent block filled
   uint32_t sample_rate;
} __attribute__((packed));

//
// Data block header
//

struct VT_BLOCK {
   int magic;
   uint32_t flags;              // See VT_FLAG_* below
   uint32_t bsize;              // Number of frames per block
   uint32_t chans;              // Number of channels per frame;
   uint32_t sample_rate;        // Nominal sample rate
   volatile uint32_t secs;      // Timestamp, seconds part
   volatile uint32_t nsec;      // Timestamp, nanoseconds part
   volatile int32_t valid;      // Set to one if block is valid
   int32_t frames;              // Number of frames actually contained
   int32_t spare;
   volatile double srcal;       // Actual rate is sample_rate * srcal
} __attribute__((packed));

// Bit definitions for VT_BLOCK and VT_BUFFER flags field
#define VTFLAG_RELT    (1<<0)   // Timestamps are relative, not absolute
#define VTFLAG_FLOAT4  (1<<1)   // 4 byte floats  (8 byte default)
#define VTFLAG_FLOAT8  0
#define VTFLAG_INT1    (2<<1)   // 1 byte signed integers
#define VTFLAG_INT2    (3<<1)   // 2 byte signed integers
#define VTFLAG_INT4    (4<<1)   // 4 byte signed integers
#define VTFLAG_FMTMASK  (VTFLAG_FLOAT4 | VTFLAG_INT1 |\
                         VTFLAG_INT2 | VTFLAG_INT4)   // Mask for format bits


#if SIZEOF_LONG_DOUBLE < 12
   #define USE_COMPOUND_TIMESTAMP
#endif

#ifdef USE_COMPOUND_TIMESTAMP

   typedef struct timestamp {
      int32_t secs;
      double frac;
   } timestamp;

   #define timestamp_NONE ((timestamp){-1,0})
   #define timestamp_ZERO ((timestamp){0,0})
#else

   typedef long double timestamp;

   #define timestamp_NONE -1
   #define timestamp_ZERO 0
#endif

typedef struct VTFILE
{
   char *name;
   int type;                    // Stream type, see VT_TYPE_* in vtlib.c
   int fh;                      // File handle
   uint32_t flags;              // See VT_FLAG_* above
   uint32_t bsize;              // Number of frames per block
   uint32_t chans;              // Number of channels per frame;
   uint32_t sample_rate;
   struct VT_BUFFER *bhead;
   int bs;          // Block size, bytes, including VT_BLOCK header
   int semid;
   int secs;
   int readp;
   int rbreak;             // Break in input channel detected
   int ulp;
   int nfb;
   int bcnt;
   uint64_t nft;                // Number of frames since timebase
   int port;
   char *host;
   double srcal;
   timestamp timebase;
   struct VT_BLOCK *blk;
   void (*cvt_insert)(struct VTFILE *, double *);
   void (*cvt_extract)(struct VTFILE *);
   double *frame;
} VTFILE;

struct VT_CHANSPEC
{
   int *map;
   int n;
};

///////////////////////////////////////////////////////////////////////////////
//  Timestamp functions                                                      //
///////////////////////////////////////////////////////////////////////////////

#ifdef USE_COMPOUND_TIMESTAMP

   static inline timestamp timestamp_normalise( timestamp t)
   {
      int n = floor( t.frac);
      return (timestamp) { t.secs + n, t.frac - n};
   }

   static inline void timestamp_string3( timestamp t, char *s)
   {
      int a = t.secs, d = 0.5 + 1e3 * t.frac;
      if( d == 1000) { d = 0; a++; }
      sprintf( s, "%d.%03d", a, d);
   }

   static inline void timestamp_string4( timestamp t, char *s)
   {
      int a = t.secs, d = 0.5 + 1e4 * t.frac;
      if( d == 10000) { d = 0; a++; }
      sprintf( s, "%d.%04d", a, d);
   }

   static inline void timestamp_string6( timestamp t, char *s)
   {
      int a = t.secs, d = 0.5 + 1e6 * t.frac;
      if( d == 1000000) { d = 0; a++; }
      sprintf( s, "%d.%06d", a, d);
   }

   static inline void timestamp_string7( timestamp t, char *s)
   {
      int a = t.secs, d = 0.5 + 1e7 * t.frac;
      if( d == 10000000) { d = 0; a++; }
      sprintf( s, "%d.%07d", a, d);
   }

   static inline timestamp timestamp_add( timestamp t, long double f)
   {
      return timestamp_normalise(
               (timestamp){ t.secs + (int) f, t.frac + f - (int) f});
   }

   static inline timestamp timestamp_compose( int secs, double frac)
   {
      return timestamp_normalise( (timestamp){ secs, frac});
   }
   
   static inline uint32_t timestamp_secs( timestamp T)
   {
      return T.secs;
   }
   
   static inline double timestamp_frac( timestamp T)
   {
      return T.frac;
   }
   
   static inline double timestamp_diff( timestamp T1, timestamp T2)
   {
      return T1.secs - T2.secs + T1.frac - T2.frac;
   }

   static inline int timestamp_is_ZERO( timestamp T)
   {
      return T.secs == 0 && T.frac == 0;
   }

   static inline int timestamp_is_NONE( timestamp T)
   {
      return T.secs == -1 && T.frac == 0;
   }
   
   static inline int timestamp_GT( timestamp a, timestamp b)
   {
      if( a.secs > b.secs) return 1;
      if( a.secs < b.secs) return 0;
      if( a.frac > b.frac) return 1;
      return 0;
   } 
   
   static inline int timestamp_GE( timestamp a, timestamp b)
   {
      if( a.secs > b.secs) return 1;
      if( a.secs < b.secs) return 0;
      if( a.frac >= b.frac) return 1;
      return 0;
   } 
   
   static inline int timestamp_LT( timestamp a, timestamp b)
   {
      if( a.secs < b.secs) return 1;
      if( a.secs > b.secs) return 0;
      if( a.frac < b.frac) return 1;
      return 0;
   }
 
   static inline int timestamp_LE( timestamp a, timestamp b)
   {
      if( a.secs < b.secs) return 1;
      if( a.secs > b.secs) return 0;
      if( a.frac < b.frac) return 1;
      if( a.frac > b.frac) return 0;
      return 1;
   }
 
   static inline timestamp timestamp_round( timestamp t)
   {
      t = timestamp_normalise( t);
      if( t.frac >= 0.5)
         return timestamp_compose( timestamp_secs( t) + 1, 0);
      else
         return timestamp_compose( timestamp_secs( t), 0);
   }

   static inline timestamp timestamp_truncate( timestamp t, double f)
   {
      int64_t i = timestamp_secs( t)/f + timestamp_frac( t)/f;
      timestamp r;
      r.secs = i * f;
      r.frac = i * f - r.secs;
      return r;
   }

#else

   static inline timestamp timestamp_normalise( timestamp t)
   {
      return t;
   }

   static inline void timestamp_string3( timestamp t, char *s)
   {
      sprintf( s, "%.3Lf", t);
   }

   static inline void timestamp_string4( timestamp t, char *s)
   {
      sprintf( s, "%.4Lf", t);
   }

   static inline void timestamp_string6( timestamp t, char *s)
   {
      sprintf( s, "%.6Lf", t);
   }

   static inline void timestamp_string7( timestamp t, char *s)
   {
      sprintf( s, "%.7Lf", t);
   }

   static inline timestamp timestamp_add( timestamp T, long double t)
   {
      return T + t;
   }
   
   static inline timestamp timestamp_compose( int secs, double frac)
   {
      return secs + (timestamp) frac;  
   }
   
   static inline uint32_t timestamp_secs( timestamp T)
   {
      return (uint32_t) T;
   }
   
   static inline double timestamp_frac( timestamp T)
   {
      return (double)(T - (uint32_t) T);
   }
   
   static inline double timestamp_diff( timestamp T1, timestamp T2)
   {
      return (double)(T1 - T2);
   }

   static inline int timestamp_is_ZERO( timestamp T)
   {
      return T == 0;
   }

   static inline int timestamp_is_NONE( timestamp T)
   {
      return T == -1;
   }
  
   static inline int timestamp_GT( timestamp a, timestamp b)
   {
      return a > b;
   }
  
   static inline int timestamp_GE( timestamp a, timestamp b)
   {
      return a >= b;
   }

   static inline int timestamp_LT( timestamp a, timestamp b)
   {
      return a < b;
   }
  
   static inline int timestamp_LE( timestamp a, timestamp b)
   {
      return a <= b;
   }

   static inline timestamp timestamp_round( timestamp t)
   {
      return roundl( t);
   }

   static inline timestamp timestamp_truncate( timestamp t, double f)
   {
      return (int64_t)(t / f) * (long double) f;
   }
#endif

///////////////////////////////////////////////////////////////////////////////
//  Prototypes                                                               //
///////////////////////////////////////////////////////////////////////////////

VTFILE *VT_open_output( char *name, int chans, int locked, int sample_rate);

struct VT_BLOCK *VT_block( struct VT_BUFFER *s, int n);

VTFILE *VT_open_input( char *name);
void VT_close( VTFILE *vtfile);
extern char VT_error[];

void VT_insert_frame( VTFILE *vtfile, double *frame);
void VT_insert_frames_i2( VTFILE *vtfile, int16_t *frames, int nframes);

int VT_get_frames_i2( VTFILE *vtfile, int16_t *frames, int nframes);

void VT_set_timebase( VTFILE *vtfile, timestamp t, double srcal);

void VT_report( int level, char *format, ...)
                  __attribute__ ((format (printf, 2, 3)));

void VT_set_logfile( char *format, ...)
                  __attribute__ ((format (printf, 1, 2)));
void VT_up_loglevel( void);

void VT_bailout( char *format, ...)
                  __attribute__ ((format (printf, 1, 2), noreturn));
void VT_exit( char *format, ...)
                  __attribute__ ((format (printf, 1, 2), noreturn));

void VT_bailout_hook( void (*fcn)(void));

#define KEEP_STDIN 1
#define KEEP_STDOUT 2
#define KEEP_STDERR 3

void VT_daemonise( int flags);
void *VT_malloc( size_t size) __attribute__ ((malloc));
void *VT_malloc_zero( size_t size) __attribute__ ((malloc));
void *VT_realloc( void *ptr, size_t size);

timestamp VT_timestamp_from_filename( char *);
timestamp VT_parse_timestamp( char *);
void VT_parse_timespec( char *s, timestamp *TS, timestamp *TE);
int VT_parse_complex( char *s, complex double *cp);
void VT_format_timestamp( char *s, timestamp T);
void VT_parse_polarspec( int chans, char *polarspec,
                         int *ch_H1, double *align_H1,
                         int *ch_H2, double *align_H2,
                         int *ch_E);

void VT_init( char *);

void VT_init_chanspec( struct VT_CHANSPEC *, VTFILE *);
struct VT_CHANSPEC *VT_parse_chanspec( char *);

int VT_read_next( VTFILE *vtfile);
int VT_poll( VTFILE *vtfile, double timeout);

void VT_next_write( VTFILE *vtfile);
void VT_release( VTFILE *vtfile);
void VT_parse_freqspec( char *s, double *FS, double *FE);

///////////////////////////////////////////////////////////////////////////////
//  Inline Functions                                                         //
///////////////////////////////////////////////////////////////////////////////

static inline uint32_t VT_flags( VTFILE *vtfile)
{
   return vtfile->flags;
}

static inline int VT_get_sample_rate( VTFILE *vtfile)
{
   return vtfile->sample_rate;
}

static inline int VT_get_bcnt( VTFILE *vtfile)
{
   return vtfile->bcnt;
}

static inline int VT_get_chans( VTFILE *vtfile)
{  
   return vtfile->chans;
}

static inline double *VT_get_frame( VTFILE *vtfile)
{
   if( !vtfile->nfb && !VT_read_next( vtfile)) return NULL;

   vtfile->cvt_extract( vtfile);
   vtfile->ulp++;
   vtfile->nfb--;
   vtfile->rbreak = 0;

   return vtfile->frame;
}

static inline timestamp VT_get_stamp( struct VT_BLOCK *bp)
{
   return timestamp_compose( bp->secs, 1e-9 * bp->nsec);
}

static inline timestamp VT_get_timestamp( VTFILE *vtfile)
{
   if( !vtfile->nfb && !VT_read_next( vtfile)) return timestamp_NONE;

   return timestamp_add( VT_get_stamp( vtfile->blk),
               vtfile->ulp/(double)(vtfile->blk->srcal * vtfile->sample_rate));
}

static inline int VT_rbreak( VTFILE *vtfile)
{
   if( !vtfile->nfb && !VT_read_next( vtfile)) return -1;

   int r = vtfile->rbreak;
   vtfile->rbreak = 0;
   return r;
}

//
//  Point to the start of frame data in the current data block for 'vtfile'.
//
static inline void *VT_data_p( VTFILE *vtfile)
{
   return (char *)vtfile->blk + sizeof( struct VT_BLOCK);
}

static inline double VT_get_srcal( VTFILE *vtfile)
{
   if( !vtfile->nfb && !VT_read_next( vtfile)) return 0;

   return vtfile->blk->srcal;
}

static inline int VT_is_blk( VTFILE *vtfile)
{
   if( !vtfile->nfb) VT_read_next( vtfile);

   return vtfile->ulp == 0;
}

static inline int VT_is_block( VTFILE *vtfile)
{
   if( !vtfile->nfb && !VT_read_next( vtfile)) return -1;

   return vtfile->ulp == 0;
}

static inline timestamp VT_rtc_time( void)
{
   struct timeval tv;

   gettimeofday( &tv, NULL);

   return timestamp_compose( tv.tv_sec, tv.tv_usec/1e6);
}

static inline double VT_phase( timestamp T, double F)
{
   #ifdef USE_COMPOUND_TIMESTAMP
      double c = T.secs * (F - (int)F)
               + T.frac * F;
   #else

      double c = (uint32_t)T * (F - (int)F)
                  + (T - (uint32_t)T) * F;
   #endif
   return 2 * M_PI * (c - (unsigned) c);
}

#ifndef HAVE_CARG
   #define carg(A) atan2(cimag(A),creal(A))
#endif

