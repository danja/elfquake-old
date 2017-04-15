/*
 * Various utilities for ffmpeg system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
#include "avformat.h"
#include <ctype.h>
#ifndef CONFIG_WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#else
#define strcasecmp _stricmp
#include <sys/types.h>
#include <sys/timeb.h>
#endif
#include <time.h>
#include <libavcodec/integer.h>

AVInputFormat *first_iformat;
AVOutputFormat *first_oformat;

const uint16_t mpa_bitrate_tab[2][3][15] = {
    { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
      {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } },
    { {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}
    }
};

const uint16_t mpa_freq_tab[3] = { 44100, 48000, 32000 };
 
void av_register_input_format(AVInputFormat *format)
{
    AVInputFormat **p;
    p = &first_iformat;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

void av_register_output_format(AVOutputFormat *format)
{
    AVOutputFormat **p;
    p = &first_oformat;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

int match_ext(const char *filename, const char *extensions)
{
    const char *ext, *p;
    char ext1[32], *q;

    ext = strrchr(filename, '.');
    if (ext) {
        ext++;
        p = extensions;
        for(;;) {
            q = ext1;
            while (*p != '\0' && *p != ',')
                *q++ = *p++;
            *q = '\0';
            if (!strcasecmp(ext1, ext))
                return 1;
            if (*p == '\0')
                break;
            p++;
        }
    }
    return 0;
}

AVOutputFormat *guess_format(const char *short_name, const char *filename,
                             const char *mime_type)
{
    AVOutputFormat *fmt, *fmt_found;
    int score_max, score;

    /* find the proper file type */
    fmt_found = NULL;
    score_max = 0;
    fmt = first_oformat;
    while (fmt != NULL) {
        score = 0;
        if (fmt->name && short_name && !strcmp(fmt->name, short_name))
            score += 100;
        if (fmt->mime_type && mime_type && !strcmp(fmt->mime_type, mime_type))
            score += 10;
        if (filename && fmt->extensions &&
            match_ext(filename, fmt->extensions)) {
            score += 5;
        }
        if (score > score_max) {
            score_max = score;
            fmt_found = fmt;
        }
        fmt = fmt->next;
    }
    return fmt_found;
}

AVOutputFormat *guess_stream_format(const char *short_name, const char *filename,
                             const char *mime_type)
{
    AVOutputFormat *fmt = guess_format(short_name, filename, mime_type);

    if (fmt) {
        AVOutputFormat *stream_fmt;
        char stream_format_name[64];

        snprintf(stream_format_name, sizeof(stream_format_name), "%s_stream", fmt->name);
        stream_fmt = guess_format(stream_format_name, NULL, NULL);

        if (stream_fmt)
            fmt = stream_fmt;
    }

    return fmt;
}

AVInputFormat *guess_input_format(const char *filename)
{
    AVInputFormat *fmt;
    for(fmt = first_iformat; fmt != NULL; fmt = fmt->next) {
        if ( match_ext(filename, fmt->extensions))
            return fmt;
    }
    return NULL;
}

AVInputFormat *av_find_input_format(const char *short_name)
{
    AVInputFormat *fmt;
    for(fmt = first_iformat; fmt != NULL; fmt = fmt->next) {
        if (!strcmp(fmt->name, short_name))
            return fmt;
    }
    return NULL;
}

/* memory handling */

/**
 * Allocate the payload of a packet and intialized its fields to default values.
 *
 * @param pkt packet
 * @param size wanted payload size
 * @return 0 if OK. AVERROR_xxx otherwise.
 */
int av_new_packet(AVPacket *pkt, int size)
{
    int i;
		if( pkt->data && pkt->size< size )
				av_free_packet( pkt );

		if( !pkt->data )
			pkt->data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);

    if (!pkt->data)
        return AVERROR_NOMEM;
    pkt->size = ( pkt->size> size ) ? pkt->size: size;
    /* sane state */
    pkt->pts = AV_NOPTS_VALUE;
    pkt->stream_index = 0;
    pkt->flags = 0;

    for(i=0; i<FF_INPUT_BUFFER_PADDING_SIZE; i++)
        pkt->data[size+i]= 0;

    return 0;
}

int filename_number_test(const char *filename)
{
    char buf[1024];
    return get_frame_filename(buf, sizeof(buf), filename, 1);
}

/* guess file format */
AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened)
{
    AVInputFormat *fmt1, *fmt;
    int score, score_max;

    fmt = NULL;
    score_max = 0;
    for(fmt1 = first_iformat; fmt1 != NULL; fmt1 = fmt1->next) {
        if (!is_opened && !(fmt1->flags & AVFMT_NOFILE))
            continue;
        score = 0;
        if (fmt1->read_probe) {
            score = fmt1->read_probe(pd);
        } else if (fmt1->extensions) {
            if (match_ext(pd->filename, fmt1->extensions)) {
                score = 50;
            }
        }
        if (score > score_max) {
            score_max = score;
            fmt = fmt1;
        }
    }
    return fmt;
}

/************************************************************/
/* input media file */

#define PROBE_BUF_SIZE 2048

/**
 * Read a packet from a media file
 * @param s media file handle
 * @param pkt is filled
 * @return 0 if OK. AVERROR_xxx if error.
 */
int av_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVPacketList *pktl;

    pktl = s->packet_buffer;
    if (pktl) {
        /* read packet from packet buffer, if there is data */
        *pkt = pktl->pkt;
        s->packet_buffer = pktl->next;
        av_free(pktl);
        return 0;
    } else {
        return s->iformat->read_packet(s, pkt);
    }
}

/* state for codec information */
#define CSTATE_NOTFOUND    0
#define CSTATE_DECODING    1
#define CSTATE_FOUND       2

static int has_codec_parameters(AVCodecContext *enc)
{
    int val;
    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        val = enc->sample_rate;
        break;
    case CODEC_TYPE_VIDEO:
        val = enc->width;
        break;
    default:
        val = 1;
        break;
    }
    return (val != 0);
}


/**
 * Close a media file (but not its codecs)
 *
 * @param s media file handle
 */
void av_close_input_file(AVFormatContext *s)
{
    if (s->packet_buffer) {
        AVPacketList *p, *p1;
        p = s->packet_buffer;
        while (p != NULL) {
            p1 = p->next;
            av_free_packet(&p->pkt);
            av_free(p);
            p = p1;
        }
        s->packet_buffer = NULL;
    }
    if (s->iformat->read_close)
        s->iformat->read_close(s);

    av_freep(&s->priv_data);
    //av_free(s);
}

/**
 * Add a new stream to a media file. Can only be called in the
 * read_header function. If the flag AVFMT_NOHEADER is in the format
 * description, then new streams can be added in read_packet too.
 *
 *
 * @param s media file handle
 * @param id file format dependent stream id
 */
AVStream *av_new_stream(AVFormatContext *s, int id)
{
    AVStream *st;

    if (s->nb_streams >= MAX_STREAMS)
        return NULL;

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return NULL;
    avcodec_get_context_defaults(&st->codec);
    if (s->iformat) {
        /* no default bitrate if decoding */
        st->codec.bit_rate = 0;
    }
    st->index = s->nb_streams;
    st->id = id;
    st->start_time = AV_NOPTS_VALUE;
    st->duration = AV_NOPTS_VALUE;
    st->cur_dts = AV_NOPTS_VALUE;

    /* default pts settings is MPEG like */
    av_set_pts_info(st, 33, 1, 90000);
    st->last_IP_pts = AV_NOPTS_VALUE;

    s->streams[s->nb_streams++] = st;
    return st; 
}

/* Return in 'buf' the path with '%d' replaced by number. Also handles
   the '%0nd' format where 'n' is the total number of digits and
   '%%'. Return 0 if OK, and -1 if format error */
int get_frame_filename(char *buf, int buf_size,
                       const char *path, int number)
{
    const char *p;
    char *q, buf1[20];
    int nd, len, c, percentd_found;

    q = buf;
    p = path;
    percentd_found = 0;
    for(;;) {
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') {
            do {
                nd = 0;
                while (isdigit(*p)) {
                    nd = nd * 10 + *p++ - '0';
                }
                c = *p++;
                if (c == '*' && nd > 0) {
                    // The nd field is actually the modulus
                    number = number % nd;
                    c = *p++;
                    nd = 0;
                }
            } while (isdigit(c));

            switch(c) {
            case '%':
                goto addchar;
            case 'd':
                if (percentd_found)
                    goto fail;
                percentd_found = 1;
                snprintf(buf1, sizeof(buf1), "%0*d", nd, number);
                len = strlen(buf1);
                if ((q - buf + len) > buf_size - 1)
                    goto fail;
                memcpy(q, buf1, len);
                q += len;
                break;
            default:
                goto fail;
            }
        } else {
        addchar:
            if ((q - buf) < buf_size - 1)
                *q++ = c;
        }
    }
    if (!percentd_found)
        goto fail;
    *q = '\0';
    return 0;
 fail:
    *q = '\0';
    return -1;
}

/**
 *
 * Print on stdout a nice hexa dump of a buffer
 * @param buf buffer
 * @param size buffer size
 */
void av_hex_dump(UINT8 *buf, int size)
{
    int len, i, j, c;

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        printf("%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                printf(" %02x", buf[i+j]);
            else
                printf("   ");
        }
        printf(" ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            printf("%c", c);
        }
        printf("\n");
    }
}


/**
 * Set the pts for a given stream
 * @param s stream
 * @param pts_wrap_bits number of bits effectively used by the pts
 *        (used for wrap control, 33 is the value for MPEG)
 * @param pts_num numerator to convert to seconds (MPEG: 1)
 * @param pts_den denominator to convert to seconds (MPEG: 90000)
 */
void av_set_pts_info(AVStream *s, int pts_wrap_bits,
                     int pts_num, int pts_den)
{
    s->pts_wrap_bits = pts_wrap_bits;
    s->time_base.num = pts_num;
    s->time_base.den = pts_den;
}
 
#if 1

char *av_strdup(const char *s)
{
    char *ptr;
    int len;
    len = strlen(s) + 1;
    ptr = av_malloc(len);
    if (!ptr)
        return NULL;
    memcpy(ptr, s, len);
    return ptr;
}
 
#define FF_COLOR_RGB      0 /* RGB color space */
#define FF_COLOR_GRAY     1 /* gray color space */
#define FF_COLOR_YUV      2 /* YUV color space. 16 <= Y <= 235, 16 <= U, V <= 240 */
#define FF_COLOR_YUV_JPEG 3 /* YUV color space. 0 <= Y <= 255, 0 <= U, V <= 255 */

#define FF_PIXEL_PLANAR   0 /* each channel has one component in AVPicture */
#define FF_PIXEL_PACKED   1 /* only one components containing all the channels */
#define FF_PIXEL_PALETTE  2  /* one components containing indexes for a palette */

typedef struct PixFmtInfo {
    const char *name;
    uint8_t nb_channels;     /* number of channels (including alpha) */
    uint8_t color_type;      /* color type (see FF_COLOR_xxx constants) */
    uint8_t pixel_type;      /* pixel storage type (see FF_PIXEL_xxx constants) */
    uint8_t is_alpha : 1;    /* true if alpha can be specified */
    uint8_t x_chroma_shift;  /* X chroma subsampling factor is 2 ^ shift */
    uint8_t y_chroma_shift;  /* Y chroma subsampling factor is 2 ^ shift */
    uint8_t depth;           /* bit depth of the color components */
} PixFmtInfo;

/* this table gives more information about formats */
static PixFmtInfo pix_fmt_info[PIX_FMT_NB]= {
		{ "yuv420p", 3, FF_COLOR_YUV, FF_PIXEL_PLANAR, 0, 1, 1, 8 },
		{ "yuv422p", 3, FF_COLOR_YUV, FF_PIXEL_PLANAR, 0, 1, 1, 8 },
    { "rgb24", 3, FF_COLOR_RGB, FF_PIXEL_PACKED, 0, 0, 0, 8 },
    { "bgr24", 3, FF_COLOR_RGB, FF_PIXEL_PACKED, 0, 0, 0, 8 },
    { "yuvj422p", 3, FF_COLOR_YUV_JPEG, FF_PIXEL_PLANAR, 0, 1, 0, 8 },
    { "yuvj444p", 3, FF_COLOR_YUV_JPEG, FF_PIXEL_PLANAR, 0, 0, 0, 8 },
    { "rgba32", 4, FF_COLOR_RGB, FF_PIXEL_PACKED, 1,0, 0, 8 },
    { "yuv410p", 3, FF_COLOR_YUV, FF_PIXEL_PLANAR, 0, 2, 2, 8 },
    { "yuv411p", 3, FF_COLOR_YUV, FF_PIXEL_PLANAR, 0, 2, 0, 8 },
		{ "rgb565", 3, FF_COLOR_RGB, FF_PIXEL_PACKED, 0, 0, 0, 5 },
		{ "rgb555", 4, FF_COLOR_RGB, FF_PIXEL_PACKED, 1, 0, 0, 5 },
		{ "gray", 1, FF_COLOR_GRAY,FF_PIXEL_PLANAR, 0, 0, 0, 8 },
		{ "monow", 1, FF_COLOR_GRAY, FF_PIXEL_PLANAR, 0, 0, 0, 1 },
    { "monob", 1, FF_COLOR_GRAY, FF_PIXEL_PLANAR, 0, 0, 0, 1 },
		{ "pal8", 4, FF_COLOR_RGB, FF_PIXEL_PALETTE, 1, 0, 0, 8 },
		{ "yuvj420p", 3, FF_COLOR_YUV_JPEG, FF_PIXEL_PLANAR, 0, 1, 1, 8 },
		{ "yuv422", 1, FF_COLOR_YUV, FF_PIXEL_PACKED, 0, 1, 0, 8 },
		{ NULL, 0, 0, 0, 0, 0, 0, 0 }, 
		{ NULL, 0, 0, 0, 0, 0, 0, 0 } 
};

void avcodec_get_chroma_sub_sample(int pix_fmt, int *h_shift, int *v_shift)
{
    *h_shift = pix_fmt_info[pix_fmt].x_chroma_shift;
    *v_shift = pix_fmt_info[pix_fmt].y_chroma_shift;
}

typedef struct InternalBuffer{
    int last_pic_num;
    uint8_t *base[4];
    uint8_t *data[4];
}InternalBuffer;

#define INTERNAL_BUFFER_SIZE 32

/**
 * allocates a AVPFrame and set it to defaults.
 * this can be deallocated by simply calling free()
 */
AVFrame *avcodec_alloc_frame(void){
    AVFrame *pic= av_mallocz(sizeof(AVFrame));

    return pic;
}
 
int avcodec_default_get_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    const int width = s->width;
    const int height= s->height;
    InternalBuffer *buf;
    
    assert(pic->data[0]==NULL);
    assert(INTERNAL_BUFFER_SIZE > s->internal_buffer_count);

    if(s->internal_buffer==NULL){
        s->internal_buffer= av_mallocz(INTERNAL_BUFFER_SIZE*sizeof(InternalBuffer));
    }
#if 0
    s->internal_buffer= av_fast_realloc(
        s->internal_buffer, 
        &s->internal_buffer_size, 
        sizeof(InternalBuffer)*FFMAX(99,  s->internal_buffer_count+1)
        );
#endif
     
    buf= &((InternalBuffer*)s->internal_buffer)[s->internal_buffer_count];

    if(buf->base[0]){
        pic->age= pic->coded_picture_number - buf->last_pic_num;
        buf->last_pic_num= pic->coded_picture_number;
    }else{
        int align, h_chroma_shift, v_chroma_shift;
        int w, h, pixel_size;
        
        avcodec_get_chroma_sub_sample(s->pix_fmt, &h_chroma_shift, &v_chroma_shift);
        switch(s->pix_fmt){
        case PIX_FMT_RGB555:
        case PIX_FMT_RGB565:
        case PIX_FMT_YUV422:
            pixel_size=2;
            break;
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
            pixel_size=3;
            break;
        case PIX_FMT_RGBA32:
            pixel_size=4;
            break;
        default:
            pixel_size=1;
        }
        
        if(s->codec_id==CODEC_ID_SVQ1) align=63;
        else                           align=15;
    
        w= (width +align)&~align;
        h= (height+align)&~align;
    
        if(!(s->flags&CODEC_FLAG_EMU_EDGE)){
            w+= EDGE_WIDTH*2;
            h+= EDGE_WIDTH*2;
        }
        
        buf->last_pic_num= -256*256*256*64;

        for(i=0; i<3; i++){
            const int h_shift= i==0 ? 0 : h_chroma_shift;
            const int v_shift= i==0 ? 0 : v_chroma_shift;

            pic->linesize[i]= pixel_size*w>>h_shift;

            buf->base[i]= av_mallocz((pic->linesize[i]*h>>v_shift)+16); //FIXME 16
            if(buf->base[i]==NULL) return -1;
            memset(buf->base[i], 128, pic->linesize[i]*h>>v_shift);
        
            if(s->flags&CODEC_FLAG_EMU_EDGE)
                buf->data[i] = buf->base[i];
            else
                buf->data[i] = buf->base[i] + (pic->linesize[i]*EDGE_WIDTH>>v_shift) + (EDGE_WIDTH>>h_shift);
        }
        pic->age= 256*256*256*64;
        pic->type= FF_BUFFER_TYPE_INTERNAL;
    }

    for(i=0; i<4; i++){
        pic->base[i]= buf->base[i];
        pic->data[i]= buf->data[i];
    }
    s->internal_buffer_count++;

    return 0;
}

void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    InternalBuffer *buf, *last, temp;

    assert(pic->type==FF_BUFFER_TYPE_INTERNAL);
    assert(s->internal_buffer_count);

    for(i=0; i<s->internal_buffer_count; i++){ //just 3-5 checks so is not worth to optimize
        buf= &((InternalBuffer*)s->internal_buffer)[i];
        if(buf->data[0] == pic->data[0])
            break;
    }
    assert(i < s->internal_buffer_count);
    s->internal_buffer_count--;
    last = &((InternalBuffer*)s->internal_buffer)[s->internal_buffer_count];

    temp= *buf;
    *buf= *last;
    *last= temp;

    for(i=0; i<3; i++){
        pic->data[i]=NULL;
    }
}

enum PixelFormat avcodec_default_get_format(struct AVCodecContext *s, enum PixelFormat * fmt)
{
    return fmt[0];
}

void avcodec_get_context_defaults(AVCodecContext *s)
{
    s->bit_rate= 800*1000;
    s->bit_rate_tolerance= s->bit_rate*10;
    s->qmin= 2;
    s->qmax= 31;
    s->mb_qmin= 2;
    s->mb_qmax= 31;
    s->rc_eq= "tex^qComp";
    s->qcompress= 0.5;
    s->max_qdiff= 3;
    s->b_quant_factor=1.25;
    s->b_quant_offset=1.25;
    s->i_quant_factor=-0.8f;
    s->i_quant_offset=0.0;
    s->error_concealment= 3;
    s->error_resilience= 1;
    s->workaround_bugs= FF_BUG_AUTODETECT;
    s->frame_rate_base= 1;
    s->frame_rate = 25;
    s->gop_size= 50;
    s->me_method= ME_EPZS;
    s->get_buffer= avcodec_default_get_buffer;
    s->release_buffer= avcodec_default_release_buffer;
    s->get_format= avcodec_default_get_format;
    s->me_subpel_quality=8;
    
    s->intra_quant_bias= FF_DEFAULT_QUANT_BIAS;
    s->inter_quant_bias= FF_DEFAULT_QUANT_BIAS;
}
 
#endif
/************************************************************/
/* output media file */

int av_set_parameters(AVFormatContext *s, AVFormatParameters *ap)
{
	int ret;

	if (s->oformat->priv_data_size > 0) {
		s->priv_data = av_mallocz(s->oformat->priv_data_size);
		if (!s->priv_data)
			return AVERROR_NOMEM;
	} else
		s->priv_data = NULL;

	if (s->oformat->set_parameters) {
		ret = s->oformat->set_parameters(s, ap);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/**
 * allocate the stream private data and write the stream header to an
 * output media file
 *
 * @param s media file handle
 * @return 0 if OK. AVERROR_xxx if error.  
 */
int av_write_header(AVFormatContext *s)
{
    int ret, i;
    AVStream *st;

    ret = s->oformat->write_header(s);
    if (ret < 0)
        return ret;

    /* init PTS generation */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];

        switch (st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            av_frac_init(&st->pts, 0, 0, 
                         (int64_t)st->time_base.num * st->codec.sample_rate);
            break;
        case CODEC_TYPE_VIDEO:
            av_frac_init(&st->pts, 0, 0, 
                         (int64_t)st->time_base.num * st->codec.time_base.den);
            break;
        default:
            break;
        }
    }
    return 0;
}
 
/* get the number of samples of an audio frame. Return (-1) if error */
static int get_audio_frame_size(AVCodecContext *enc, int size)
{
    int frame_size;

    if (enc->frame_size <= 1) {
        /* specific hack for pcm codecs because no frame size is
           provided */
        switch(enc->codec_id) {
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            if (enc->channels == 0)
                return -1;
            frame_size = size / (2 * enc->channels);
            break;
        case CODEC_ID_PCM_S8:
        case CODEC_ID_PCM_U8:
        case CODEC_ID_PCM_MULAW:
        case CODEC_ID_PCM_ALAW:
            if (enc->channels == 0)
                return -1;
            frame_size = size / (enc->channels);
            break;
        default:
            /* used for example by ADPCM codecs */
            if (enc->bit_rate == 0)
                return -1;
            frame_size = (size * 8 * enc->sample_rate) / enc->bit_rate;
            break;
        }
    } else {
        frame_size = enc->frame_size;
    }
    return frame_size;
}
 
/* return the frame duration in seconds, return 0 if not available */
static void compute_frame_duration(int *pnum, int *pden, AVStream *st, 
                                   AVCodecParserContext *pc, AVPacket *pkt)
{
    int frame_size;

    *pnum = 0;
    *pden = 0;
    switch(st->codec.codec_type) {
    case CODEC_TYPE_VIDEO:
        *pnum = st->codec.frame_rate_base;
        *pden = st->codec.frame_rate;
        if (pc && pc->repeat_pict) {
            *pden *= 2;
            *pnum = (*pnum) * (2 + pc->repeat_pict);
        }
        break;
    case CODEC_TYPE_AUDIO:
        frame_size = get_audio_frame_size(&st->codec, pkt->size);
        if (frame_size < 0)
            break;
        *pnum = frame_size;
        *pden = st->codec.sample_rate;
        break;
    default:
        break;
    }
}
 
//FIXME merge with compute_pkt_fields
static void compute_pkt_fields2(AVStream *st, AVPacket *pkt){
    int b_frames = FFMAX(st->codec.has_b_frames, st->codec.max_b_frames);
    int num, den, frame_size;

//    av_log(NULL, AV_LOG_DEBUG, "av_write_frame: pts:%lld dts:%lld cur_dts:%lld b:%d size:%d st:%d\n", pkt->pts, pkt->dts, st->cur_dts, b_frames, pkt->size, pkt->stream_index);
    
/*    if(pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)
        return -1;*/
            
    if(pkt->pts != AV_NOPTS_VALUE)
        pkt->pts = av_rescale(pkt->pts, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);
    if(pkt->dts != AV_NOPTS_VALUE)
        pkt->dts = av_rescale(pkt->dts, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);

    /* duration field */
    pkt->duration = (int)av_rescale(pkt->duration, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);
    if (pkt->duration == 0) {
        compute_frame_duration(&num, &den, st, NULL, pkt);
        if (den && num) {
            pkt->duration = av_rescale(1, num * (int64_t)st->time_base.den, den * (int64_t)st->time_base.num);
        }
    }

    //XXX/FIXME this is a temporary hack until all encoders output pts
    if((pkt->pts == 0 || pkt->pts == AV_NOPTS_VALUE) && pkt->dts == AV_NOPTS_VALUE && !b_frames){
        pkt->dts=
//        pkt->pts= st->cur_dts;
        pkt->pts= st->pts.val;
    }

    //calculate dts from pts    
    if(pkt->pts != AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE){
        if(b_frames){
            if(st->last_IP_pts == AV_NOPTS_VALUE){
                st->last_IP_pts= -pkt->duration;
            }
            if(st->last_IP_pts < pkt->pts){
                pkt->dts= st->last_IP_pts;
                st->last_IP_pts= pkt->pts;
            }else
                pkt->dts= pkt->pts;
        }else
            pkt->dts= pkt->pts;
    }
    
//    av_log(NULL, AV_LOG_DEBUG, "av_write_frame: pts2:%lld dts2:%lld\n", pkt->pts, pkt->dts);
    st->cur_dts= pkt->dts;
    st->pts.val= pkt->dts;

    /* update pts */
    switch (st->codec.codec_type) {
    case CODEC_TYPE_AUDIO:
        frame_size = get_audio_frame_size(&st->codec, pkt->size);

        /* HACK/FIXME, we skip the initial 0-size packets as they are most likely equal to the encoder delay,
           but it would be better if we had the real timestamps from the encoder */
        if (frame_size >= 0 && (pkt->size || st->pts.num!=st->pts.den>>1 || st->pts.val)) {
            av_frac_add(&st->pts, (int64_t)st->time_base.den * frame_size);
        }
        break;
    case CODEC_TYPE_VIDEO:
        av_frac_add(&st->pts, (int64_t)st->time_base.den * st->codec.frame_rate_base);
        break;
    default:
        break;
    }
}

static void truncate_ts(AVStream *st, AVPacket *pkt){
    int64_t pts_mask = (int64_t_C(2) << (st->pts_wrap_bits-1)) - 1;
    
    if(pkt->dts < 0)
        pkt->dts= 0;  //this happens for low_delay=0 and b frames, FIXME, needs further invstigation about what we should do here
    
    pkt->pts &= pts_mask;
    pkt->dts &= pts_mask;
}

/**
 * Write a packet to an output media file. The packet shall contain
 * one audio or video frame.
 *
 * @param s media file handle
 * @param pkt the packet, which contains the stream_index, buf/buf_size, dts/pts, ...
 * @return < 0 if error, = 0 if OK, 1 if end of stream wanted.
 */
int av_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    compute_pkt_fields2(s->streams[pkt->stream_index], pkt);
    
    truncate_ts(s->streams[pkt->stream_index], pkt);

    return s->oformat->write_packet(s, pkt);
}

/**
 * write the stream trailer to an output media file and and free the
 * file private data.
 *
 * @param s media file handle
 * @return 0 if OK. AVERROR_xxx if error.  */
int av_write_trailer(AVFormatContext *s)
{
    int ret;
    ret = s->oformat->write_trailer(s);
    av_freep(&s->priv_data);
    return ret;
}

/* fraction handling */

/**
 * f = val + (num / den) + 0.5. 'num' is normalized so that it is such
 * as 0 <= num < den.
 *
 * @param f fractional number
 * @param val integer value
 * @param num must be >= 0
 * @param den must be >= 1 
 */
void av_frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den)
{
    num += (den >> 1);
    if (num >= den) {
        val += num / den;
        num = num % den;
    }
    f->val = val;
    f->num = num;
    f->den = den;
}

/* set f to (val + 0.5) */
void av_frac_set(AVFrac *f, int64_t val)
{
    f->val = val;
    f->num = f->den >> 1;
}

/**
 * Fractionnal addition to f: f = f + (incr / f->den)
 *
 * @param f fractional number
 * @param incr increment, can be positive or negative
 */
void av_frac_add(AVFrac *f, int64_t incr)
{
    int64_t num, den;

    num = f->num + incr;
    den = f->den;
    if (num < 0) {
        f->val += num / den;
        num = num % den;
        if (num < 0) {
            num += den;
            f->val--;
        }
    } else if (num >= den) {
        f->val += num / den;
        num = num % den;
    }
    f->num = num;
}

int64_t av_rescale(int64_t a, int64_t b, int64_t c){
    AVInteger ai, ci;
    assert(c > 0);
    assert(b >=0);
    
    if(a<0) return -av_rescale(-a, b, c);
    
    if(b<=MAXINT && c<=MAXINT){
        if(a<=MAXINT)
            return (a * b + c/2)/c;
        else
            return a/c*b + (a%c*b + c/2)/c;
    }
    
    ai= av_mul_i(av_int2i(a), av_int2i(b));
    ci= av_int2i(c);
    ai= av_add_i(ai, av_shr_i(ci,1));
    
    return av_i2int(av_div_i(ai, ci));
}
 
