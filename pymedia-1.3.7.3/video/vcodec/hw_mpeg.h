/*
 * HW MPEG2 decoder interface
 * Copyright (c) 2004 Dmitry Borisov
 * 
 * Standalone hw MPEG frame parser for CLE266 chipset
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifndef MPEG_HW_DECODER_H
#define MPEG_HW_DECODER_H

#include "common.h"
#include "mem.h"
#include "libavcodec/avcodec.h"

/* Start codes. */
#define SEQ_END_CODE		0x000001b7
#define SEQ_START_CODE		0x000001b3
#define GOP_START_CODE		0x000001b8
#define PICTURE_START_CODE	0x00000100
#define SLICE_MIN_START_CODE	0x00000101
#define SLICE_MAX_START_CODE	0x000001af
#define EXT_START_CODE		0x000001b5
#define USER_START_CODE		0x000001b2

#define END_NOT_FOUND -100 
#define ERROR_NEED_BUFFERS -1

#define FF_INPUT_BUFFER_PADDING_SIZE 8

#define I_TYPE 1  ///< Intra
#define P_TYPE 2  ///< Predicted
#define B_TYPE 3  ///< Bi-dir predicted 

/**
 * Scantable.
 */
typedef struct ScanTable
{
    const uint8_t *scantable;
    uint8_t permutated[64];
    uint8_t raster_end[64];
#ifdef ARCH_POWERPC
		/** Used by dct_quantise_alitvec to find last-non-zero */
    uint8_t __align8 inverse[64];
#endif
} ScanTable;
 

typedef struct ParseContext{
    uint8_t *buffer;
    int index;
    int last_index;
    int buffer_size;
    uint32_t state;             ///< contains the last few bytes in MSB order
    int frame_start_found;
    int overread;               ///< the number of bytes which where irreversibly read from the next frame
    int overread_index;         ///< the index into ParseContext.buffer of the overreaded bytes
} ParseContext;
 

typedef struct MpegContext 
{
  /* the following parameters must be initialized before encoding */
  int width, height;///< picture size. must be a multiple of 16
  int gop_size;
  int intra_only;   ///< if true, only intra pictures are generated
  int bit_rate;     ///< wanted bit rate
  int bit_rate_tolerance; ///< amount of +- bits (>0) 
  float aspect_ratio; 
  int aspect_ratio_info; //FIXME remove 
  int frame_rate; 
  int frame_rate_index; 
  int frame_rate_base; 
  int resync; 
	int pict_type;

  int full_pel[2]; 
  int mpeg_f_code[2][2]; 
  int intra_dc_precision; 

/* picture type */
#define PICT_TOP_FIELD     1
#define PICT_BOTTOM_FIELD  2
#define PICT_FRAME         3
 
  int picture_structure; 
  int frame_pred_frame_dct;
  int top_field_first;
  int concealment_motion_vectors;
  int q_scale_type;
  int intra_vlc_format;
  int alternate_scan;
  int repeat_first_field;
  int chroma_420_type;
  int progressive_frame; 
  int first_field;         ///< is 1 for the first field of a field picture 0 otherwise
  int first_slice;
  int progressive_sequence; 
  int low_delay;                   ///< no reordering needed / has no b-frames 
  int flags;        ///< AVCodecContext.flags (HQ, MV4, ...)
  int max_b_frames; ///< max number of b-frames for encoding 
  int second_field;

  uint8_t *mbskip_table;        /**< used to avoid copy if macroblock skipped (for black regions for example)
                                 and used for b-frame encoding & decoding (contains skip table of next P Frame) */ 
	int mb_width, mb_height;   ///< number of MBs horizontally & vertically
	int mb_stride;             ///< mb_width+1 used for some arrays to allow simple addressng of left & top MBs withoutt sig11 

	uint8_t idct_permutation[64]; 
  /** matrix transmitted in the bitstream */
  uint16_t intra_matrix[64];
  uint16_t chroma_intra_matrix[64];
  uint16_t inter_matrix[64];
  uint16_t chroma_inter_matrix[64]; 

	ParseContext parse_context;
	/* Scan tables */
  ScanTable intra_scantable;
  ScanTable intra_h_scantable;
  ScanTable intra_v_scantable;
  ScanTable inter_scantable; ///< if inter == intra then intra should be used to reduce tha cache usage
 
	GetBitContext gb;

  struct _fb 
	{
      int bwref;              /* Backward-reference framebuffer. (0-3) */
      int fwref;              /* Forward-reference framebuffer. (0-3) */
      int b;                  /* Current B-frame framebuffer. (0-3) */
      int idle;               /* Idle framebuffer. (0-3) */
      int dst;                /* Current decoder destination framebuffer. (0-3) */
      int display;            /* Framebuffer to display. (0-3) */

      /* Framebuffer VRAM memory offsets. */
      uint32_t y[4];
      uint32_t u[4];
      uint32_t v[4];
  } fb;

  uint8_t* mmio;              /* HW regs */
	uint32_t temp;
} MpegContext;
	

/* ----------------------------------------------------------------------------- */
int mpeg_decode_frame(MpegContext *s, void *data, int *data_size, uint8_t *buf, int buf_size);
/* ----------------------------------------------------------------------------- */
void mpeg_reset( MpegContext *s );
/* ----------------------------------------------------------------------------- */
int mpeg_init( MpegContext *s );
/* ----------------------------------------------------------------------------- */
int mpeg_close( MpegContext *s );
/* ----------------------------------------------------------------------------- */
int mpeg_probe( void );
/* ----------------------------------------------------------------------------- */
int mpeg_index( MpegContext *s );
/* ----------------------------------------------------------------------------- */
void mpeg_set_surface( MpegContext *s, int iSurf, int offs, int stride );
/* ----------------------------------------------------------------------------- */
void mpeg_set_fb_stride( MpegContext *s, int stride );
/* ----------------------------------------------------------------------------- */
//void mpeg_blit_surface( MpegContext *s, uint32_t offs, uint32_t iSurf );

#endif /* MPEG_HW_DECODER_H */
