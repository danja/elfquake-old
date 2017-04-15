#ifndef __CLE266_HWDEC_H__
#define __CLE266_HWDEC_H__

#include "common.h"
#include "vcodec/hw_mpeg.h"

#ifdef WIN32
#define MPEG_IO(reg) ( &s->temp )
#else
#define MPEG_IO(reg) ((uint32_t*)(0xc00+s->mmio+(reg)))
#endif

/* HQV Registers */
#define HQV_CONTROL             0x1D0
#define HQV_SRC_STARTADDR_Y     0x1D4
#define HQV_SRC_STARTADDR_U     0x1D8
#define HQV_SRC_STARTADDR_V     0x1DC
#define HQV_SRC_FETCH_LINE      0x1E0
#define HQV_FILTER_CONTROL      0x1E4
#define HQV_MINIFY_CONTROL      0x1E8
#define HQV_DST_STARTADDR0      0x1EC
#define HQV_DST_STARTADDR1      0x1F0
#define HQV_DST_STARTADDR2      0x1FC
#define HQV_DST_STRIDE          0x1F4
#define HQV_SRC_STRIDE          0x1F8

// First call this func to see if HW decoding is supported
int hwdec_open( MpegContext *s );

uint32_t hwdec_wait(MpegContext *s);

int hwdec_is_busy( MpegContext *s );

uint32_t hwdec_get_status(MpegContext *s);

void hwdec_reset(MpegContext *s);

void hwdec_set_fb(MpegContext *s, int i, int iFb );

void hwdec_set_fb_stride(MpegContext *s, uint32_t y_stride, uint32_t uv_stride);

void hwdec_begin_picture(MpegContext *s );

/* Write one raw (unpadded) slice to the hardware. */
void hwdec_write_slice(MpegContext *s, uint8_t* slice, int nbytes);

void hwdec_set_fb_stride(MpegContext *s, uint32_t y_stride, uint32_t uv_stride);

//void hwdec_blit_surface( MpegContext *s, uint32_t dst, uint32_t src );
#endif /* __CLE266_HWDEC_H__ */
