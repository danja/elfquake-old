/*
 * MPEG2 transport stream (aka DVB) demux
 * Copyright (c) 2002-2003 Fabrice Bellard.
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

#include "mpegts.h"

//#define DEBUG_SI

/* 1.0 second at 24Mbit/s */
#define MAX_SCAN_PACKETS 32000

/* maximum size in which we look for synchronisation if
   synchronisation is lost */
#define MAX_RESYNC_SIZE 4096

static int add_pes_stream(AVFormatContext *s, int pid);

enum MpegTSFilterType {
    MPEGTS_PES,
    MPEGTS_SECTION,
};

typedef void PESCallback(void *opaque, const uint8_t *buf, int len, int is_start);

typedef struct MpegTSPESFilter {
    PESCallback *pes_cb;
    void *opaque;
} MpegTSPESFilter;

typedef void SectionCallback(void *opaque, const uint8_t *buf, int len);

typedef void SetServiceCallback(void *opaque, int ret);

typedef struct MpegTSSectionFilter {
    int section_index;
    int section_h_size;
    uint8_t *section_buf;
    int check_crc:1;
    int end_of_section_reached:1;
    SectionCallback *section_cb;
    void *opaque;
} MpegTSSectionFilter;

typedef struct MpegTSFilter {
    int pid;
    int last_cc; /* last cc code (-1 if first packet) */
    enum MpegTSFilterType type;
    union {
        MpegTSPESFilter pes_filter;
        MpegTSSectionFilter section_filter;
    } u;
} MpegTSFilter;

typedef struct MpegTSService {
    int running:1;
    int sid;
    char *provider_name;
    char *name;
} MpegTSService;

typedef struct MpegTSContext {
    /* user data */
    AVFormatContext *stream;
    int raw_packet_size; /* raw packet size, including FEC if present */
    int auto_guess; /* if true, all pids are analized to find streams */
    int set_service_ret;

    /* data needed to handle file based ts */
    int stop_parse; /* stop parsing loop */
    AVPacket *pkt; /* packet containing av data */

    /******************************************/
    /* private mpegts data */
    /* scan context */
    MpegTSFilter *sdt_filter;
    int nb_services;
    MpegTSService **services;
    
    /* set service context (XXX: allocated it ?) */
    SetServiceCallback *set_service_cb;
    void *set_service_opaque;
    MpegTSFilter *pat_filter;
    MpegTSFilter *pmt_filter;
    int req_sid;

    MpegTSFilter *pids[NB_PID_MAX];
} MpegTSContext;

/* write DVB SI sections */

static uint32_t crc_table[256] = {
	0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
	0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
	0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
	0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
	0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
	0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
	0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
	0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
	0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
	0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
	0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
	0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
	0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
	0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
	0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
	0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
	0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
	0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
	0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
	0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
	0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
	0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
	0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
	0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
	0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
	0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
	0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
	0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
	0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
	0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
	0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
	0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
	0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
	0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
	0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};
 
unsigned int mpegts_crc32(const uint8_t *data, int len)
{
    register int i;
    unsigned int crc = 0xffffffff;
    
    for (i=0; i<len; i++)
        crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *data++) & 0xff];
    
    return crc;
}
 
static void write_section_data(AVFormatContext *s, MpegTSFilter *tss1,
                               const uint8_t *buf, int buf_size, int is_start)
{
    MpegTSSectionFilter *tss = &tss1->u.section_filter;
    int len;
    unsigned int crc;
    
    if (is_start) {
        memcpy(tss->section_buf, buf, buf_size);
        tss->section_index = buf_size;
        tss->section_h_size = -1;
        tss->end_of_section_reached = 0;
    } else {
        if (tss->end_of_section_reached)
            return;
        len = 4096 - tss->section_index;
        if (buf_size < len)
            len = buf_size;
        memcpy(tss->section_buf + tss->section_index, buf, len);
        tss->section_index += len;
    }

    /* compute section length if possible */
    if (tss->section_h_size == -1 && tss->section_index >= 3) {
        len = (((tss->section_buf[1] & 0xf) << 8) | tss->section_buf[2]) + 3;
        if (len > 4096)
            return;
        tss->section_h_size = len;
    }

    if (tss->section_h_size != -1 && tss->section_index >= tss->section_h_size) {
        if (tss->check_crc) {
            crc = mpegts_crc32(tss->section_buf, tss->section_h_size);
            if (crc != 0)
                goto invalid_crc;
        }
        tss->section_cb(tss->opaque, tss->section_buf, tss->section_h_size);
    invalid_crc:
        tss->end_of_section_reached = 1;
    }
}

MpegTSFilter *mpegts_open_section_filter(MpegTSContext *ts, unsigned int pid, 
                                         SectionCallback *section_cb, void *opaque,
                                         int check_crc)

{
    MpegTSFilter *filter;
    MpegTSSectionFilter *sec;

    if (pid >= NB_PID_MAX || ts->pids[pid])
        return NULL;
    filter = av_mallocz(sizeof(MpegTSFilter));
    if (!filter) 
        return NULL;
    ts->pids[pid] = filter;
    filter->type = MPEGTS_SECTION;
    filter->pid = pid;
    filter->last_cc = -1;
    sec = &filter->u.section_filter;
    sec->section_cb = section_cb;
    sec->opaque = opaque;
    sec->section_buf = av_malloc(MAX_SECTION_SIZE);
    sec->check_crc = check_crc;
    if (!sec->section_buf) {
        av_free(filter);
        return NULL;
    }
    return filter;
}

MpegTSFilter *mpegts_open_pes_filter(MpegTSContext *ts, unsigned int pid, 
                                     PESCallback *pes_cb,
                                     void *opaque)
{
    MpegTSFilter *filter;
    MpegTSPESFilter *pes;

    if (pid >= NB_PID_MAX || ts->pids[pid])
        return NULL;
    filter = av_mallocz(sizeof(MpegTSFilter));
    if (!filter) 
        return NULL;
    ts->pids[pid] = filter;
    filter->type = MPEGTS_PES;
    filter->pid = pid;
    filter->last_cc = -1;
    pes = &filter->u.pes_filter;
    pes->pes_cb = pes_cb;
    pes->opaque = opaque;
    return filter;
}

void mpegts_close_filter(MpegTSContext *ts, MpegTSFilter *filter)
{
    int pid;

    pid = filter->pid;
    if (filter->type == MPEGTS_SECTION)
        av_freep(&filter->u.section_filter.section_buf);
    av_free(filter);
    ts->pids[pid] = NULL;
}

/* autodetect fec presence. Must have at least 1024 bytes  */
static int get_packet_size(const uint8_t *buf, int size)
{
    int i;

    if (size < (TS_FEC_PACKET_SIZE * 5 + 1))
        return -1;
    for(i=0;i<5;i++) {
        if (buf[i * TS_PACKET_SIZE] != 0x47)
            goto try_fec;
    }
    return TS_PACKET_SIZE;
 try_fec:
    for(i=0;i<5;i++) {
        if (buf[i * TS_FEC_PACKET_SIZE] != 0x47)
            return -1;
    }
    return TS_FEC_PACKET_SIZE;
}

typedef struct SectionHeader {
    uint8_t tid;
    uint16_t id;
    uint8_t version;
    uint8_t sec_num;
    uint8_t last_sec_num;
} SectionHeader;

static inline int get8(const uint8_t **pp, const uint8_t *p_end)
{
    const uint8_t *p;
    int c;

    p = *pp;
    if (p >= p_end)
        return -1;
    c = *p++;
    *pp = p;
    return c;
}

static inline int get16(const uint8_t **pp, const uint8_t *p_end)
{
    const uint8_t *p;
    int c;

    p = *pp;
    if ((p + 1) >= p_end)
        return -1;
    c = (p[0] << 8) | p[1];
    p += 2;
    *pp = p;
    return c;
}

/* read and allocate a DVB string preceeded by its length */
static char *getstr8(const uint8_t **pp, const uint8_t *p_end)
{
    int len;
    const uint8_t *p;
    char *str;

    p = *pp;
    len = get8(&p, p_end);
    if (len < 0)
        return NULL;
    if ((p + len) > p_end)
        return NULL;
    str = av_malloc(len + 1);
    if (!str)
        return NULL;
    memcpy(str, p, len);
    str[len] = '\0';
    p += len;
    *pp = p;
    return str;
}

static int parse_section_header(SectionHeader *h, 
                                const uint8_t **pp, const uint8_t *p_end)
{
    int val;

    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->tid = val;
    *pp += 2;
    val = get16(pp, p_end);
    if (val < 0)
        return -1;
    h->id = val;
    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->version = (val >> 1) & 0x1f;
    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->sec_num = val;
    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->last_sec_num = val;
    return 0;
}

static MpegTSService *new_service(MpegTSContext *ts, int sid, 
                                  char *provider_name, char *name)
{
    MpegTSService *service;

#ifdef DEBUG_SI
    printf("new_service: sid=0x%04x provider='%s' name='%s'\n", 
           sid, provider_name, name);
#endif

    service = av_mallocz(sizeof(MpegTSService));
    if (!service)
        return NULL;
    service->sid = sid;
    service->provider_name = provider_name;
    service->name = name;
    dynarray_add(&ts->services, &ts->nb_services, service);
    return service;
}

static void pmt_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int program_info_length, pcr_pid, pid, stream_type, desc_length;
    
#ifdef DEBUG_SI
    printf("PMT:\n");
    av_hex_dump((uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
#ifdef DEBUG_SI
    printf("sid=0x%x sec_num=%d/%d\n", h->id, h->sec_num, h->last_sec_num);
#endif
    if (h->tid != PMT_TID || (ts->req_sid >= 0 && h->id != ts->req_sid) )
        return;

    pcr_pid = get16(&p, p_end) & 0x1fff;
    if (pcr_pid < 0)
        return;
#ifdef DEBUG_SI
    printf("pcr_pid=0x%x\n", pcr_pid);
#endif
    program_info_length = get16(&p, p_end) & 0xfff;
    if (program_info_length < 0)
        return;
    p += program_info_length;
    if (p >= p_end)
        return;
    for(;;) {
        stream_type = get8(&p, p_end);
        if (stream_type < 0)
            break;
        pid = get16(&p, p_end) & 0x1fff;
        if (pid < 0)
            break;
        desc_length = get16(&p, p_end) & 0xfff;
        if (desc_length < 0)
            break;
        p += desc_length;
        if (p > p_end)
            return;

#ifdef DEBUG_SI
        printf("stream_type=%d pid=0x%x\n", stream_type, pid);
#endif

        /* now create ffmpeg stream */
        switch(stream_type) {
        case STREAM_TYPE_AUDIO_MPEG1:
        case STREAM_TYPE_AUDIO_MPEG2:
        case STREAM_TYPE_VIDEO_MPEG1:
        case STREAM_TYPE_VIDEO_MPEG2:
        case STREAM_TYPE_AUDIO_AC3:
            add_pes_stream(ts->stream, pid);
            break;
        default:
            /* we ignore the other streams */
            break;
        }
    }
    /* all parameters are there */
    ts->set_service_cb(ts->set_service_opaque, 0);
    mpegts_close_filter(ts, ts->pmt_filter);
    ts->pmt_filter = NULL;
}

static void pat_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int sid, pmt_pid;

#ifdef DEBUG_SI
    printf("PAT:\n");
    av_hex_dump((uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != PAT_TID)
        return;

    for(;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        pmt_pid = get16(&p, p_end) & 0x1fff;
        if (pmt_pid < 0)
            break;
#ifdef DEBUG_SI
        printf("sid=0x%x pid=0x%x\n", sid, pmt_pid);
#endif
        if (sid == 0x0000) {
            /* NIT info */
        } else {
            if (ts->req_sid == sid) {
                ts->pmt_filter = mpegts_open_section_filter(ts, pmt_pid, 
                                                            pmt_cb, ts, 1);
                goto found;
            }
        }
    }
    /* not found */
    ts->set_service_cb(ts->set_service_opaque, -1);

 found:
    mpegts_close_filter(ts, ts->pat_filter);
    ts->pat_filter = NULL;
}

/* add all services found in the PAT */
static void pat_scan_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int sid, pmt_pid;
    char *provider_name, *name;
    char buf[256];

#ifdef DEBUG_SI
    printf("PAT:\n");
    av_hex_dump((uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != PAT_TID)
        return;

    for(;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        pmt_pid = get16(&p, p_end) & 0x1fff;
        if (pmt_pid < 0)
            break;
#ifdef DEBUG_SI
        printf("sid=0x%x pid=0x%x\n", sid, pmt_pid);
#endif
        if (sid == 0x0000) {
            /* NIT info */
        } else {
            /* add the service with a dummy name */
            snprintf(buf, sizeof(buf), "Service %x\n", sid);
            name = av_strdup(buf);
            provider_name = av_strdup("");
            if (name && provider_name) {
                new_service(ts, sid, provider_name, name);
            } else {
                av_freep(&name);
                av_freep(&provider_name);
            }
        }
    }
    ts->stop_parse = 1;

    /* remove filter */
    mpegts_close_filter(ts, ts->pat_filter);
    ts->pat_filter = NULL;
}

void mpegts_set_service(MpegTSContext *ts, int sid,
                        SetServiceCallback *set_service_cb, void *opaque)
{
    ts->set_service_cb = set_service_cb;
    ts->set_service_opaque = opaque;
    ts->req_sid = sid;
    ts->pat_filter = mpegts_open_section_filter(ts, PAT_PID, 
                                                pat_cb, ts, 1);
}

static void sdt_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end, *desc_list_end, *desc_end;
    int onid, val, sid, desc_list_len, desc_tag, desc_len, service_type;
    char *name, *provider_name;

#ifdef DEBUG_SI
    printf("SDT:\n");
    av_hex_dump((uint8_t *)section, section_len);
#endif

    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != SDT_TID)
        return;
    onid = get16(&p, p_end);
    if (onid < 0)
        return;
    val = get8(&p, p_end);
    if (val < 0)
        return;
    for(;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        val = get8(&p, p_end);
        if (val < 0)
            break;
        desc_list_len = get16(&p, p_end) & 0xfff;
        if (desc_list_len < 0)
            break;
        desc_list_end = p + desc_list_len;
        if (desc_list_end > p_end)
            break;
        for(;;) {
            desc_tag = get8(&p, desc_list_end);
            if (desc_tag < 0)
                break;
            desc_len = get8(&p, desc_list_end);
            desc_end = p + desc_len;
            if (desc_end > desc_list_end)
                break;
#ifdef DEBUG_SI
            printf("tag: 0x%02x len=%d\n", desc_tag, desc_len);
#endif
            switch(desc_tag) {
            case 0x48:
                service_type = get8(&p, p_end);
                if (service_type < 0)
                    break;
                provider_name = getstr8(&p, p_end);
                if (!provider_name)
                    break;
                name = getstr8(&p, p_end);
                if (!name)
                    break;
                new_service(ts, sid, provider_name, name);
                break;
            default:
                break;
            }
            p = desc_end;
        }
        p = desc_list_end;
    }
    ts->stop_parse = 1;

    /* remove filter */
    mpegts_close_filter(ts, ts->sdt_filter);
    ts->sdt_filter = NULL;
}

/* scan services in a transport stream by looking at the SDT */
void mpegts_scan_sdt(MpegTSContext *ts)
{
    ts->sdt_filter = mpegts_open_section_filter(ts, SDT_PID, 
                                                sdt_cb, ts, 1);
}

/* scan services in a transport stream by looking at the PAT (better
   than nothing !) */
void mpegts_scan_pat(MpegTSContext *ts)
{
    ts->pat_filter = mpegts_open_section_filter(ts, PAT_PID, 
                                                pat_scan_cb, ts, 1);
}

/* TS stream handling */

enum MpegTSState {
    MPEGTS_HEADER = 0,
    MPEGTS_PESHEADER_FILL,
    MPEGTS_PAYLOAD,
    MPEGTS_SKIP,
};

/* enough for PES header + length */
#define PES_START_SIZE 9
#define MAX_PES_HEADER_SIZE (9 + 255)

typedef struct PESContext {
    int pid;
    AVFormatContext *stream;
    AVStream *st;
    enum MpegTSState state;
    /* used to get the format */
    int data_index;
    int total_size;
    int pes_header_size;
    int64_t pts, dts;
    uint8_t header[MAX_PES_HEADER_SIZE];
} PESContext;

static int64_t get_pts(const uint8_t *p)
{
    int64_t pts;
    int val;

    pts = (int64_t)((p[0] >> 1) & 0x07) << 30;
    val = (p[1] << 8) | p[2];
    pts |= (int64_t)(val >> 1) << 15;
    val = (p[3] << 8) | p[4];
    pts |= (int64_t)(val >> 1);
    return pts;
}

/* return non zero if a packet could be constructed */
static void mpegts_push_data(void *opaque,
                             const uint8_t *buf, int buf_size, int is_start)
{
    PESContext *pes = opaque;
    MpegTSContext *ts = pes->stream->priv_data;
    AVStream *st;
    const uint8_t *p;
    int len, code, codec_type, codec_id;
    
    if (is_start) {
        pes->state = MPEGTS_HEADER;
        pes->data_index = 0;
    }
    p = buf;
    while (buf_size > 0) {
        switch(pes->state) {
        case MPEGTS_HEADER:
            len = PES_START_SIZE - pes->data_index;
            if (len > buf_size)
                len = buf_size;
            memcpy(pes->header + pes->data_index, p, len);
            pes->data_index += len;
            p += len;
            buf_size -= len;
            if (pes->data_index == PES_START_SIZE) {
                /* we got all the PES or section header. We can now
                   decide */
#if 0
                av_hex_dump(pes->header, pes->data_index);
#endif
                if (pes->header[0] == 0x00 && pes->header[1] == 0x00 &&
                    pes->header[2] == 0x01) {
                    /* it must be an mpeg2 PES stream */
                    code = pes->header[3] | 0x100;
                    if (!((code >= 0x1c0 && code <= 0x1df) ||
                          (code >= 0x1e0 && code <= 0x1ef) ||
                          (code == 0x1bd)))
                        goto skip;
                    if (!pes->st) {
                        /* allocate stream */
                        if (code >= 0x1c0 && code <= 0x1df) {
                            codec_type = CODEC_TYPE_AUDIO;
                            codec_id = CODEC_ID_MP2;
                        } else if (code == 0x1bd) {
                            codec_type = CODEC_TYPE_AUDIO;
                            codec_id = CODEC_ID_AC3;
                        } else {
                            codec_type = CODEC_TYPE_VIDEO;
                            codec_id = CODEC_ID_MPEG1VIDEO;
                        }
                        st = av_new_stream(pes->stream, pes->pid);
                        if (st) {
                            st->priv_data = pes;
                            st->codec.codec_type = codec_type;
                            st->codec.codec_id = codec_id;
                            pes->st = st;
                        }
                    }
                    pes->state = MPEGTS_PESHEADER_FILL;
                    pes->total_size = (pes->header[4] << 8) | pes->header[5];
                    /* NOTE: a zero total size means the PES size is
                       unbounded */
                    if (pes->total_size)
                        pes->total_size += 6;
                    pes->pes_header_size = pes->header[8] + 9;
                } else {
                    /* otherwise, it should be a table */
                    /* skip packet */
                skip:
                    pes->state = MPEGTS_SKIP;
                    continue;
                }
            }
            break;
            /**********************************************/
            /* PES packing parsing */
        case MPEGTS_PESHEADER_FILL:
            len = pes->pes_header_size - pes->data_index;
            if (len > buf_size)
                len = buf_size;
            memcpy(pes->header + pes->data_index, p, len);
            pes->data_index += len;
            p += len;
            buf_size -= len;
            if (pes->data_index == pes->pes_header_size) {
                const uint8_t *r;
                unsigned int flags;

                flags = pes->header[7];
                r = pes->header + 9;
                pes->pts = AV_NOPTS_VALUE;
                pes->dts = AV_NOPTS_VALUE;
                if ((flags & 0xc0) == 0x80) {
                    pes->pts = get_pts(r);
                    r += 5;
                } else if ((flags & 0xc0) == 0xc0) {
                    pes->pts = get_pts(r);
                    r += 5;
                    pes->dts = get_pts(r);
                    r += 5;
                }
                /* we got the full header. We parse it and get the payload */
                pes->state = MPEGTS_PAYLOAD;
            }
            break;
        case MPEGTS_PAYLOAD:
            if (pes->total_size) {
                len = pes->total_size - pes->data_index;
                if (len > buf_size)
                    len = buf_size;
            } else {
                len = buf_size;
            }
            if (len > 0) {
                AVPacket *pkt = ts->pkt;
                if (pes->st && av_new_packet(pkt, len) == 0) {
                    memcpy(pkt->data, p, len);
                    pkt->stream_index = pes->st->index;
                    pkt->pts = pes->pts;
                    /* reset pts values */
                    pes->pts = AV_NOPTS_VALUE;
                    pes->dts = AV_NOPTS_VALUE;
                    ts->stop_parse = 1;
                    return;
                }
            }
            buf_size = 0;
            break;
        case MPEGTS_SKIP:
            buf_size = 0;
            break;
        }
    }
}

static int add_pes_stream(AVFormatContext *s, int pid)
{
    MpegTSContext *ts = s->priv_data;
    MpegTSFilter *tss;
    PESContext *pes;

    /* if no pid found, then add a pid context */
    pes = av_mallocz(sizeof(PESContext));
    if (!pes)
        return -1;
    pes->stream = s;
    pes->pid = pid;
    tss = mpegts_open_pes_filter(ts, pid, mpegts_push_data, pes);
    if (!tss) {
        av_free(pes);
        return -1;
    }
    return 0;
}

/* handle one TS packet */
static void handle_packet(AVFormatContext *s, uint8_t *packet)
{
    MpegTSContext *ts = s->priv_data;
    MpegTSFilter *tss;
    int len, pid, cc, cc_ok, afc, is_start;
    const uint8_t *p, *p_end;

    pid = ((packet[1] & 0x1f) << 8) | packet[2];
    is_start = packet[1] & 0x40;
    tss = ts->pids[pid];
    if (ts->auto_guess && tss == NULL && is_start) {
        add_pes_stream(s, pid);
        tss = ts->pids[pid];
    }
    if (!tss)
        return;

    /* continuity check (currently not used) */
    cc = (packet[3] & 0xf);
    cc_ok = (tss->last_cc < 0) || ((((tss->last_cc + 1) & 0x0f) == cc));
    tss->last_cc = cc;
    
    /* skip adaptation field */
    afc = (packet[3] >> 4) & 3;
    p = packet + 4;
    if (afc == 0) /* reserved value */
        return;
    if (afc == 2) /* adaptation field only */
        return;
    if (afc == 3) {
        /* skip adapation field */
        p += p[0] + 1;
    }
    /* if past the end of packet, ignore */
    p_end = packet + TS_PACKET_SIZE;
    if (p >= p_end)
        return;
    
    if (tss->type == MPEGTS_SECTION) {
        if (is_start) {
            /* pointer field present */
            len = *p++;
            if (p + len > p_end)
                return;
            if (len && cc_ok) {
                /* write remaning section bytes */
                write_section_data(s, tss, 
                                   p, len, 0);
            }
            p += len;
            if (p < p_end) {
                write_section_data(s, tss, 
                                   p, p_end - p, 1);
            }
        } else {
            if (cc_ok) {
                write_section_data(s, tss, 
                                   p, p_end - p, 0);
            }
        }
    } else {
        tss->u.pes_filter.pes_cb(tss->u.pes_filter.opaque, 
                                 p, p_end - p, is_start);
    }
}

/* XXX: try to find a better synchro over several packets (use
   get_packet_size() ?) */
static int mpegts_resync(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    int c, i;

    for(i = 0;i < MAX_RESYNC_SIZE; i++) {
        c = get_byte(pb);
        if (c < 0)
            return -1;
        if (c == 0x47) {
            url_fseek(pb, -1, SEEK_CUR);
            return 0;
        }
    }
    /* no sync found */
    return -1;
}

static int handle_packets(AVFormatContext *s, int nb_packets)
{
    MpegTSContext *ts = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint8_t packet[TS_FEC_PACKET_SIZE];
    int packet_num, len;
    int64_t pos;

    ts->stop_parse = 0;
    packet_num = 0;
    for(;;) {
        if (ts->stop_parse)
            break;
        packet_num++;
        if (nb_packets != 0 && packet_num >= nb_packets)
            break;
        pos = url_ftell(pb);
        len = get_buffer(pb, packet, ts->raw_packet_size);
        if (len != ts->raw_packet_size)
            return AVERROR_IO;
        /* check paquet sync byte */
        if (packet[0] != 0x47) {
            /* find a new packet start */
            url_fseek(pb, -ts->raw_packet_size, SEEK_CUR);
            if (mpegts_resync(s) < 0)
                return AVERROR_INVALIDDATA;
            else
                continue;
        }
        handle_packet(s, packet);
    }
    return 0;
}

static int mpegts_probe(AVProbeData *p)
{
#if 1
    int size;
    size = get_packet_size(p->buf, p->buf_size);
    if (size < 0)
        return 0;
    return AVPROBE_SCORE_MAX - 1;
#else
    /* only use the extension for safer guess */
    if (match_ext(p->filename, "ts"))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
#endif
}

void set_service_cb(void *opaque, int ret)
{
    MpegTSContext *ts = opaque;
    ts->set_service_ret = ret;
    ts->stop_parse = 1;
}

static int mpegts_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    MpegTSContext *ts = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint8_t buf[1024];
    int len, sid;
    int64_t pos;
    MpegTSService *service;
    
    /* read the first 1024 bytes to get packet size */
    pos = url_ftell(pb);
    len = get_buffer(pb, buf, sizeof(buf));
    if (len != sizeof(buf))
        goto fail;
    ts->raw_packet_size = get_packet_size(buf, sizeof(buf));
    if (ts->raw_packet_size <= 0)
        goto fail;
    ts->auto_guess = 0;
    
    if (!ts->auto_guess) {
        ts->set_service_ret = -1;

        /* first do a scaning to get all the services */
        url_fseek(pb, pos, SEEK_SET);
        mpegts_scan_sdt(ts);

        handle_packets(s, MAX_SCAN_PACKETS);

        if (ts->nb_services <= 0) {
            /* no SDT found, we try to look at the PAT */
            
            url_fseek(pb, pos, SEEK_SET);
            mpegts_scan_pat(ts);
            
            handle_packets(s, MAX_SCAN_PACKETS);
        }
        
        if (ts->nb_services <= 0)
            return -1;
        
        /* tune to first service found */
        service = ts->services[0];
        sid = service->sid;
#ifdef DEBUG_SI
        printf("tuning to '%s'\n", service->name);
#endif

        /* now find the info for the first service if we found any,
           otherwise try to filter all PATs */

        url_fseek(pb, pos, SEEK_SET);
        ts->stream = s;
        mpegts_set_service(ts, sid, set_service_cb, ts);

        handle_packets(s, MAX_SCAN_PACKETS);

        /* if could not find service, exit */
        if (ts->set_service_ret != 0)
            return -1;

#ifdef DEBUG_SI
        printf("tuning done\n");
#endif
    }

    url_fseek(pb, pos, SEEK_SET);
    return 0;
 fail:
    return -1;
}

static int mpegts_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    MpegTSContext *ts = s->priv_data;
    ts->pkt = pkt;
    return handle_packets(s, 0);
}

static int mpegts_read_close(AVFormatContext *s)
{
    MpegTSContext *ts = s->priv_data;
    int i;
    for(i=0;i<NB_PID_MAX;i++)
        av_free(ts->pids[i]);
    return 0;
}

AVInputFormat mpegts_demux = {
    "mpegts",
    "MPEG2 transport stream format",
    sizeof(MpegTSContext),
    mpegts_probe,
    mpegts_read_header,
    mpegts_read_packet,
    mpegts_read_close,
		NULL,
    AVFMT_NOHEADER | AVFMT_SHOW_IDS,
		"mpeg"
};

int mpegts_init(void)
{
    av_register_input_format(&mpegts_demux);
    return 0;
}
