/*
 * MOV decoder.
 * Copyright (c) 2001 Fabrice Bellard.
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
#include "avi.h"

#ifdef CONFIG_ZLIB
#include <zlib.h>
#endif

/*
 * First version by Francois Revol revol@free.fr
 *
 * Features and limitations:
 * - reads most of the QT files I have (at least the structure),
 *   the exceptions are .mov with zlib compressed headers ('cmov' section). It shouldn't be hard to implement.
 *   FIXED, Francois Revol, 07/17/2002
 * - ffmpeg has nearly none of the usual QuickTime codecs,
 *   although I succesfully dumped raw and mp3 audio tracks off .mov files.
 *   Sample QuickTime files with mp3 audio can be found at: http://www.3ivx.com/showcase.html
 * - .mp4 parsing is still hazardous, although the format really is QuickTime with some minor changes
 *   (to make .mov parser crash maybe ?), despite what they say in the MPEG FAQ at
 *   http://mpeg.telecomitalialab.com/faq.htm
 * - the code is quite ugly... maybe I won't do it recursive next time :-)
 *
 * Funny I didn't know about http://sourceforge.net/projects/qt-ffmpeg/
 * when coding this :) (it's a writer anyway)
 *
 * Reference documents:
 * http://www.geocities.com/xhelmboyx/quicktime/formats/qtm-layout.txt
 * Apple:
 *  http://developer.apple.com/techpubs/quicktime/qtdevdocs/QTFF/qtff.html
 *  http://developer.apple.com/techpubs/quicktime/qtdevdocs/PDF/QTFileFormat.pdf
 * QuickTime is a trademark of Apple (AFAIK :))
 */

//#define DEBUG
#ifdef DEBUG
#include <stdio.h>
#include <fcntl.h>
#endif

/* allows chunk splitting - should work now... */
/* in case you can't read a file, try commenting */
#define MOV_SPLIT_CHUNKS

/* Special handling for movies created with Minolta Dimaxe Xi*/
/* this fix should not interfere with other .mov files, but just in case*/
#define MOV_MINOLTA_FIX

/* some streams in QT (and in MP4 mostly) aren't either video nor audio */
/* so we first list them as this, then clean up the list of streams we give back, */
/* getting rid of these */
#define CODEC_TYPE_MOV_OTHER	(enum CodecType) 2
#define MOV_HEADER_PARSED -1000

static const CodecTag mov_video_tags[] = {
/*  { CODEC_ID_, MKTAG('c', 'v', 'i', 'd') }, *//* Cinepak */
/*  { CODEC_ID_H263, MKTAG('r', 'a', 'w', ' ') }, *//* Uncompressed RGB */
/*  { CODEC_ID_H263, MKTAG('Y', 'u', 'v', '2') }, *//* Uncompressed YUV422 */
/*    { CODEC_ID_RAWVIDEO, MKTAG('A', 'V', 'U', 'I') }, *//* YUV with alpha-channel (AVID Uncompressed) */
/* Graphics */
/* Animation */
/* Apple video */
/* Kodak Photo CD */
    { CODEC_ID_MJPEG, MKTAG('j', 'p', 'e', 'g') }, /* PhotoJPEG */
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'e', 'g') }, /* MPEG */
    { CODEC_ID_MJPEG, MKTAG('m', 'j', 'p', 'a') }, /* Motion-JPEG (format A) */
    { CODEC_ID_MJPEG, MKTAG('m', 'j', 'p', 'b') }, /* Motion-JPEG (format B) */
    { CODEC_ID_MJPEG, MKTAG('A', 'V', 'D', 'J') }, /* MJPEG with alpha-channel (AVID JFIF meridien compressed) */
/*    { CODEC_ID_MJPEG, MKTAG('A', 'V', 'R', 'n') }, *//* MJPEG with alpha-channel (AVID ABVB/Truevision NuVista) */
/*    { CODEC_ID_GIF, MKTAG('g', 'i', 'f', ' ') }, *//* embedded gif files as frames (usually one "click to play movie" frame) */
/* Sorenson video */
    { CODEC_ID_SVQ1, MKTAG('S', 'V', 'Q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', 'i') }, /* Sorenson Video v1 (from QT specs)*/
    { CODEC_ID_SVQ3, MKTAG('S', 'V', 'Q', '3') }, /* Sorenson Video v3 */
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') }, /* OpenDiVX *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
/*    { CODEC_ID_, MKTAG('I', 'V', '5', '0') }, *//* Indeo 5.0 */
    { CODEC_ID_H263, MKTAG('h', '2', '6', '3') }, /* H263 */
    { CODEC_ID_H263, MKTAG('s', '2', '6', '3') }, /* H263 ?? works */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', ' ') }, /* DV NTSC */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', 'p') }, /* DV PAL */
/*    { CODEC_ID_DVVIDEO, MKTAG('A', 'V', 'd', 'v') }, *//* AVID dv */
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '1') }, /* On2 VP3 */
    { CODEC_ID_NONE, 0 },
};

static const CodecTag mov_audio_tags[] = {
/*    { CODEC_ID_PCM_S16BE, MKTAG('N', 'O', 'N', 'E') }, *//* uncompressed */
    { CODEC_ID_PCM_S16BE, MKTAG('t', 'w', 'o', 's') }, /* 16 bits */
    /* { CODEC_ID_PCM_S8, MKTAG('t', 'w', 'o', 's') },*/ /* 8 bits */
    { CODEC_ID_PCM_U8, MKTAG('r', 'a', 'w', ' ') }, /* 8 bits unsigned */
    { CODEC_ID_PCM_S16LE, MKTAG('s', 'o', 'w', 't') }, /*  */
    { CODEC_ID_PCM_MULAW, MKTAG('u', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_PCM_ALAW, MKTAG('a', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_ADPCM_IMA_QT, MKTAG('i', 'm', 'a', '4') }, /* IMA-4 ADPCM */
    { CODEC_ID_MACE3, MKTAG('M', 'A', 'C', '3') }, /* Macintosh Audio Compression and Expansion 3:1 */
    { CODEC_ID_MACE6, MKTAG('M', 'A', 'C', '6') }, /* Macintosh Audio Compression and Expansion 6:1 */

    { CODEC_ID_MP2, MKTAG('.', 'm', 'p', '3') }, /* MPEG layer 3 */ /* sample files at http://www.3ivx.com/showcase.html use this tag */
    { CODEC_ID_MP2, 0x6D730055 }, /* MPEG layer 3 */
    { CODEC_ID_MP2, 0x5500736D }, /* MPEG layer 3 *//* XXX: check endianness */
/*    { CODEC_ID_OGG_VORBIS, MKTAG('O', 'g', 'g', 'S') }, *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
/* MP4 tags */
    { CODEC_ID_AAC, MKTAG('m', 'p', '4', 'a') }, /* MPEG 4 AAC or audio ? */
    /* The standard for mpeg4 audio is still not normalised AFAIK anyway */
    { CODEC_ID_AMR_NB, MKTAG('s', 'a', 'm', 'r') }, /* AMR-NB 3gp */
    { CODEC_ID_NONE, 0 },
};

/* the QuickTime file format is quite convoluted...
 * it has lots of index tables, each indexing something in another one...
 * Here we just use what is needed to read the chunks
 */

typedef struct MOV_sample_to_chunk_tbl {
    long first;
    long count;
    long id;
} MOV_sample_to_chunk_tbl;

typedef struct {
    uint32_t type;
    int64_t offset;
    int64_t size; /* total size (excluding the size and type fields) */
} MOV_atom_t;

typedef struct {
    int seed;
    int flags;
    int size;
    void* clrs;
} MOV_ctab_t;

typedef struct {
    uint8_t  version;
    uint32_t flags; // 24bit

    /* 0x03 ESDescrTag */
    uint16_t es_id;
#define MP4ODescrTag			0x01
#define MP4IODescrTag			0x02
#define MP4ESDescrTag			0x03
#define MP4DecConfigDescrTag		0x04
#define MP4DecSpecificDescrTag		0x05
#define MP4SLConfigDescrTag		0x06
#define MP4ContentIdDescrTag		0x07
#define MP4SupplContentIdDescrTag	0x08
#define MP4IPIPtrDescrTag		0x09
#define MP4IPMPPtrDescrTag		0x0A
#define MP4IPMPDescrTag			0x0B
#define MP4RegistrationDescrTag		0x0D
#define MP4ESIDIncDescrTag		0x0E
#define MP4ESIDRefDescrTag		0x0F
#define MP4FileIODescrTag		0x10
#define MP4FileODescrTag		0x11
#define MP4ExtProfileLevelDescrTag	0x13
#define MP4ExtDescrTagsStart		0x80
#define MP4ExtDescrTagsEnd		0xFE
    uint8_t  stream_priority;

    /* 0x04 DecConfigDescrTag */
    uint8_t  object_type_id;
    uint8_t  stream_type;
    /* XXX: really streamType is
     * only 6bit, followed by:
     * 1bit  upStream
     * 1bit  reserved
     */
    uint32_t buffer_size_db; // 24
    uint32_t max_bitrate;
    uint32_t avg_bitrate;

    /* 0x05 DecSpecificDescrTag */
    uint8_t  decoder_cfg_len;
    uint8_t *decoder_cfg;

    /* 0x06 SLConfigDescrTag */
    uint8_t  sl_config_len;
    uint8_t *sl_config;
} MOV_esds_t;

struct MOVParseTableEntry;

typedef struct MOVStreamContext {
    int ffindex; /* the ffmpeg stream id */
    int is_ff_stream; /* Is this stream presented to ffmpeg ? i.e. is this an audio or video stream ? */
    long next_chunk;
    long chunk_count;
    int64_t *chunk_offsets;
    long sample_to_chunk_sz;
    MOV_sample_to_chunk_tbl *sample_to_chunk;
    long sample_to_chunk_index;
    long sample_size;
    long sample_count;
    long *sample_sizes;
    int time_scale;
    long current_sample;
    long left_in_chunk; /* how many samples before next chunk */
    /* specific MPEG4 header which is added at the beginning of the stream */
    int header_len;
    uint8_t *header_data;
    MOV_esds_t esds;
} MOVStreamContext;

typedef struct MOVContext {
    int mp4; /* set to 1 as soon as we are sure that the file is an .mp4 file (even some header parsing depends on this) */
    AVFormatContext *fc;
    int time_scale;
    int duration; /* duration of the longest track */
    int found_moov; /* when both 'moov' and 'mdat' sections has been found */
    int found_mdat; /* we suppose we have enough data to read the file */
    int64_t mdat_size;
    int64_t mdat_offset;
    int total_streams;
    /* some streams listed here aren't presented to the ffmpeg API, since they aren't either video nor audio
     * but we need the info to be able to skip data from those streams in the 'mdat' section
     */
    MOVStreamContext *streams[MAX_STREAMS];

    int64_t next_chunk_offset;
    MOVStreamContext *partial; /* != 0 : there is still to read in the current chunk */
    int ctab_size;
    MOV_ctab_t **ctab;           /* color tables */
    const struct MOVParseTableEntry *parse_table; /* could be eventually used to change the table */
    /* NOTE: for recursion save to/ restore from local variable! */
} MOVContext;


/* XXX: it's the first time I make a recursive parser I think... sorry if it's ugly :P */

/* those functions parse an atom */
/* return code:
 1: found what I wanted, exit
 0: continue to parse next atom
 -1: error occured, exit
 */
typedef int (*mov_parse_function)(MOVContext *ctx, ByteIOContext *pb, MOV_atom_t atom);

/* links atom IDs to parse functions */
typedef struct MOVParseTableEntry {
    uint32_t type;
    mov_parse_function func;
} MOVParseTableEntry;

#ifdef DEBUG
/*
 * XXX: static sux, even more in a multithreaded environment...
 * Avoid them. This is here just to help debugging.
 */
static int debug_indent = 0;
void print_atom(const char *str, MOV_atom_t atom)
{
    unsigned int tag, i;
    tag = (unsigned int) atom.type;
    i=debug_indent;
    if(tag == 0) tag = MKTAG('N', 'U', 'L', 'L');
    while(i--)
        printf("|");
    printf("parse:");
    printf(" %s: tag=%c%c%c%c offset=0x%x size=0x%x\n",
           str, tag & 0xff,
           (tag >> 8) & 0xff,
           (tag >> 16) & 0xff,
           (tag >> 24) & 0xff,
           (unsigned int)atom.offset,
	   (unsigned int)atom.size);
    //assert((unsigned int)atom.size < 0x7fffffff);// catching errors
}
#else
#define print_atom(a,b)
#endif


static int mov_read_leaf(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    print_atom("leaf", atom);

    if (atom.size>1)
        url_fskip(pb, atom.size);
/*        url_seek(pb, atom_offset+atom.size, SEEK_SET); */
    return 0;
}

static int mov_read_default(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int64_t total_size = 0;
    MOV_atom_t a;
    int i, pos= url_ftell(pb);
    int err = 0;

#ifdef DEBUG
    print_atom("default", atom);
    debug_indent++;
#endif

    a.offset = atom.offset;

    if (atom.size < 0)
			atom.size = int64_t_C( 0x7fffffffffffffff );

		if( get_mem_buffer_size( pb )< 20 )
			return AVILIB_NEED_DATA;

    while(((total_size + 8) < atom.size) && !url_feof(pb) && !err) {
			a.size = atom.size;
			a.type=0L;
      if(atom.size >= 8) {
		    a.size = get_be32(pb);
        a.type = get_le32(pb);
			}

			total_size += 8;
      a.offset += 8;
	//printf("type: %08x  %.4s  sz: %Lx  %Lx   %Lx\n", type, (char*)&type, size, atom.size, total_size);
      if (a.size == 1) { /* 64 bit extended size */
				a.size = get_be64(pb) - 8;
        a.offset += 8;
        total_size += 8;
      }
			if (a.size == 0) {
				a.size = atom.size - total_size;
				if (a.size <= 8)
          break;
			}
			for (i = 0; c->parse_table[i].type != 0L && c->parse_table[i].type != a.type; i++)
		    /* empty */;
				a.size -= 8;

//        printf(" i=%ld\n", i);
			if (c->parse_table[i].type == 0) { /* skip leaf atoms data */
//            url_seek(pb, atom.offset+atom.size, SEEK_SET);
#ifdef DEBUG
            print_atom("unknown", a);
#endif
				if( get_mem_buffer_size( pb )< a.size+ 20 )
				{
					url_fseek( pb, pos, SEEK_SET );
					return AVILIB_NEED_DATA;
				}
        url_fskip(pb, a.size);
			} else {
#ifdef DEBUG
	    //char b[5] = { type & 0xff, (type >> 8) & 0xff, (type >> 16) & 0xff, (type >> 24) & 0xff, 0 };
	    //print_atom(b, type, offset, size);
#endif
				if( get_mem_buffer_size( pb )< a.size+ 20 )
				{
					if( a.type!= MKTAG('m', 'd', 'a', 't') )
					{
						url_fseek( pb, pos, SEEK_SET );
						return AVILIB_NEED_DATA;
					}
					return (c->parse_table[i].func)(c, pb, a);
				}

				err = (c->parse_table[i].func)(c, pb, a);
				if( err< 0 )
					return err;
			}

			a.offset += a.size;
      total_size += a.size;
    }

    if (!err && total_size < atom.size && atom.size < 0x7ffff) {
	//printf("RESET  %Ld  %Ld  err:%d\n", atom.size, total_size, err);
        url_fskip(pb, atom.size - total_size);
    }

#ifdef DEBUG
    debug_indent--;
#endif
    return err;
}

static int mov_read_ctab(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    unsigned int len;
    MOV_ctab_t *t;
    //url_fskip(pb, atom.size); // for now
    c->ctab = av_realloc(c->ctab, ++c->ctab_size);
    t = c->ctab[c->ctab_size];
    t->seed = get_be32(pb);
    t->flags = get_be16(pb);
    t->size = get_be16(pb) + 1;
    len = 2 * t->size * 4;
    if (len > 0) {
	t->clrs = av_malloc(len); // 16bit A R G B
	if (t->clrs)
	    get_buffer(pb, t->clrs, len);
    }

    return 0;
}

static int mov_read_hdlr(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    int len = 0;
    uint32_t type;
    uint32_t ctype;

    print_atom("hdlr", atom);

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    /* component type */
    ctype = get_le32(pb);
    type = get_le32(pb); /* component subtype */

#ifdef DEBUG
    printf("ctype= %c%c%c%c (0x%08lx)\n", *((char *)&ctype), ((char *)&ctype)[1], ((char *)&ctype)[2], ((char *)&ctype)[3], (long) ctype);
    printf("stype= %c%c%c%c\n", *((char *)&type), ((char *)&type)[1], ((char *)&type)[2], ((char *)&type)[3]);
#endif
#ifdef DEBUG
/* XXX: yeah this is ugly... */
    if(ctype == MKTAG('m', 'h', 'l', 'r')) { /* MOV */
        if(type == MKTAG('v', 'i', 'd', 'e'))
            puts("hdlr: vide");
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            puts("hdlr: soun");
    } else if(ctype == 0) { /* MP4 */
        if(type == MKTAG('v', 'i', 'd', 'e'))
            puts("hdlr: vide");
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            puts("hdlr: soun");
        else if(type == MKTAG('o', 'd', 's', 'm'))
            puts("hdlr: odsm");
        else if(type == MKTAG('s', 'd', 's', 'm'))
            puts("hdlr: sdsm");
    } else puts("hdlr: meta");
#endif

    if(ctype == MKTAG('m', 'h', 'l', 'r')) { /* MOV */
        /* helps parsing the string hereafter... */
        c->mp4 = 0;
        if(type == MKTAG('v', 'i', 'd', 'e'))
            st->codec.codec_type = CODEC_TYPE_VIDEO;
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            st->codec.codec_type = CODEC_TYPE_AUDIO;
    } else if(ctype == 0) { /* MP4 */
        /* helps parsing the string hereafter... */
        c->mp4 = 1;
        if(type == MKTAG('v', 'i', 'd', 'e'))
            st->codec.codec_type = CODEC_TYPE_VIDEO;
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            st->codec.codec_type = CODEC_TYPE_AUDIO;
    }
    get_be32(pb); /* component  manufacture */
    get_be32(pb); /* component flags */
    get_be32(pb); /* component flags mask */

    if(atom.size <= 24)
        return 0; /* nothing left to read */
    /* XXX: MP4 uses a C string, not a pascal one */
    /* component name */

    if(c->mp4) {
        /* .mp4: C string */
        while(get_byte(pb) && (++len < (atom.size - 24)));
    } else {
        /* .mov: PASCAL string */
#ifdef DEBUG
        char* buf;
#endif
        len = get_byte(pb);
#ifdef DEBUG
	buf = (uint8_t*) av_malloc(len+1);
	if (buf) {
	    get_buffer(pb, buf, len);
	    buf[len] = '\0';
	    printf("**buf='%s'\n", buf);
	    av_free(buf);
	} else
#endif
	    url_fskip(pb, len);
    }

    return 0;
}

static int mov_mp4_read_descr_len(ByteIOContext *pb)
{
    int len = 0;
    int count = 4;
    while (count--) {
        int c = get_byte(pb);
	len = (len << 7) | (c & 0x7f);
	if (!(c & 0x80))
	    break;
    }
    return len;
}

static int mov_mp4_read_descr(ByteIOContext *pb, int *tag)
{
    int len;
    *tag = get_byte(pb);
    len = mov_mp4_read_descr_len(pb);
#ifdef DEBUG
    printf("MPEG4 description: tag=0x%02x len=%d\n", *tag, len);
#endif
    return len;
}

static inline unsigned int get_be24(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s) << 16;
    val |= get_byte(s) << 8;
    val |= get_byte(s);
    return val;
}

static int mov_read_esds(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int64_t start_pos = url_ftell(pb);
    int tag, len;

    print_atom("esds", atom);

    /* Well, broken but suffisant for some MP4 streams */
    get_be32(pb); /* version + flags */
    len = mov_mp4_read_descr(pb, &tag);
    if (tag == MP4ESDescrTag) {
	get_be16(pb); /* ID */
	get_byte(pb); /* priority */
    } else
	get_be16(pb); /* ID */

    len = mov_mp4_read_descr(pb, &tag);
    if (tag == MP4DecConfigDescrTag) {
	sc->esds.object_type_id = get_byte(pb);
	sc->esds.stream_type = get_byte(pb);
	sc->esds.buffer_size_db = get_be24(pb);
	sc->esds.max_bitrate = get_be32(pb);
	sc->esds.avg_bitrate = get_be32(pb);

	len = mov_mp4_read_descr(pb, &tag);
	//printf("LEN %d  TAG %d  m:%d a:%d\n", len, tag, sc->esds.max_bitrate, sc->esds.avg_bitrate);
	if (tag == MP4DecSpecificDescrTag) {
#ifdef DEBUG
	    printf("Specific MPEG4 header len=%d\n", len);
#endif
	    st->codec.extradata = (uint8_t*) av_mallocz(len);
	    if (st->codec.extradata) {
		get_buffer(pb, st->codec.extradata, len);
		st->codec.extradata_size = len;
	    }
	}
    }
    /* in any case, skip garbage */
    url_fskip(pb, atom.size - ((url_ftell(pb) - start_pos)));
    return 0;
}

/* this atom contains actual media data */
static int mov_read_mdat(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    print_atom("mdat", atom);

    if(atom.size == 0) /* wrong one (MP4) */
        return 0;
    c->found_mdat=1;
    c->mdat_offset = atom.offset;
    c->mdat_size = atom.size;
    if(c->found_moov)
        return 1; /* found both, just go */
    //url_fskip(pb, atom.size);
    return 0; /* now go for moov */
}

/* this atom should contain all header atoms */
static int mov_read_moov(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int err;

    print_atom("moov", atom);

    err = mov_read_default(c, pb, atom);
    /* we parsed the 'moov' atom, we can terminate the parsing as soon as we find the 'mdat' */
    /* so we don't parse the whole file if over a network */
    c->found_moov=1;
    if(c->found_mdat)
        return 1; /* found both, just go */
    return 0; /* now go for mdat */
}


static int mov_read_mdhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    print_atom("mdhd", atom);

    get_byte(pb); /* version */

    get_byte(pb); get_byte(pb);
    get_byte(pb); /* flags */

    get_be32(pb); /* creation time */
    get_be32(pb); /* modification time */

    c->streams[c->total_streams]->time_scale = get_be32(pb);

#ifdef DEBUG
    printf("track[%i].time_scale = %i\n", c->fc->nb_streams-1, c->streams[c->total_streams]->time_scale); /* time scale */
#endif
    get_be32(pb); /* duration */

    get_be16(pb); /* language */
    get_be16(pb); /* quality */

    return 0;
}

static int mov_read_mvhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    print_atom("mvhd", atom);

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    get_be32(pb); /* creation time */
    get_be32(pb); /* modification time */
    c->time_scale = get_be32(pb); /* time scale */
#ifdef DEBUG
    printf("time scale = %i\n", c->time_scale);
#endif
    c->duration = get_be32(pb); /* duration */
    get_be32(pb); /* preferred scale */

    get_be16(pb); /* preferred volume */

    url_fskip(pb, 10); /* reserved */

    url_fskip(pb, 36); /* display matrix */

    get_be32(pb); /* preview time */
    get_be32(pb); /* preview duration */
    get_be32(pb); /* poster time */
    get_be32(pb); /* selection time */
    get_be32(pb); /* selection duration */
    get_be32(pb); /* current time */
    get_be32(pb); /* next track ID */

    return 0;
}

static int mov_read_smi(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    // currently SVQ3 decoder expect full STSD header - so let's fake it
    // this should be fixed and just SMI header should be passed
    av_free(st->codec.extradata);
    st->codec.extradata_size = 0x5a + (int)atom.size;
    st->codec.extradata = (uint8_t*) av_mallocz(st->codec.extradata_size);

    if (st->codec.extradata) {
	strcpy(st->codec.extradata, "SVQ3"); // fake
	get_buffer(pb, (unsigned char*)st->codec.extradata + 0x5a, (int)atom.size);
	//printf("Reading SMI %Ld  %s\n", atom.size, (char*)st->codec.extradata + 0x5a);
    } else
	url_fskip(pb, atom.size);

    return 0;
}

static int mov_read_stco(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int entries, i;

    print_atom("stco", atom);

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);
    sc->chunk_count = entries;
    sc->chunk_offsets = (int64_t*) av_malloc(entries * sizeof(int64_t));
    if (!sc->chunk_offsets)
        return -1;
    if (atom.type == MKTAG('s', 't', 'c', 'o')) {
        for(i=0; i<entries; i++) {
            sc->chunk_offsets[i] = get_be32(pb);
        }
    } else if (atom.type == MKTAG('c', 'o', '6', '4')) {
        for(i=0; i<entries; i++) {
            sc->chunk_offsets[i] = get_be64(pb);
        }
    } else
        return -1;
#ifdef DEBUG
/*
    for(i=0; i<entries; i++) {
        printf("chunk offset=0x%Lx\n", sc->chunk_offsets[i]);
    }
*/
#endif
    return 0;
}

static int mov_read_stsd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    //MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int entries, frames_per_sample;
    uint32_t format;

    print_atom("stsd", atom);

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);

    while(entries--) 
		{ //Parsing Sample description table
        enum CodecID id;
				int size = get_be32(pb); /* size */
        format = get_le32(pb); /* data format */

        get_be32(pb); /* reserved */
        get_be16(pb); /* reserved */
        get_be16(pb); /* index */

        /* for MPEG4: set codec type by looking for it */
        id = codec_get_id(mov_video_tags, format);
        if (id >= 0) 
				{
					int i;
					st->codec.codec_type= CODEC_TYPE_AUDIO;
					for( i= 0; mov_video_tags[ i ].id!= 0; i++ )
						if( mov_video_tags[ i ].id== id )
						{
							st->codec.codec_type= CODEC_TYPE_VIDEO;
							break;
						}
        }
#ifdef DEBUG
        printf("size=%d 4CC= %c%c%c%c codec_type=%d\n",
               size,
               (format >> 0) & 0xff,
               (format >> 8) & 0xff,
               (format >> 16) & 0xff,
               (format >> 24) & 0xff,
               st->codec.codec_type);
#endif
				st->codec.codec_tag = format;
				if(st->codec.codec_type==CODEC_TYPE_VIDEO) 
				{
						MOV_atom_t a = { 0, 0, 0 };
            st->codec.codec_id = id;
            get_be16(pb); /* version */
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */
            get_be32(pb); /* temporal quality */
            get_be32(pb); /* spacial quality */
            st->codec.width = get_be16(pb); /* width */
            st->codec.height = get_be16(pb); /* height */
#if 1
            if (st->codec.codec_id == CODEC_ID_MPEG4) 
						{
                /* in some MPEG4 the width/height are not correct, so
                   we ignore this info */
                st->codec.width = 0;
                st->codec.height = 0;
            }
#endif
            get_be32(pb); /* horiz resolution */
            get_be32(pb); /* vert resolution */
            get_be32(pb); /* data size, always 0 */
            frames_per_sample = get_be16(pb); /* frames per samples */
#ifdef DEBUG
						printf("frames/samples = %d\n", frames_per_sample);
#endif
						get_buffer(pb, (uint8_t *)st->codec.codec_name, 32); /* codec name */

						st->codec.bits_per_sample = get_be16(pb); /* depth */
            st->codec.color_table_id = get_be16(pb); /* colortable id */

/*          These are set in mov_read_stts and might already be set!
            st->codec.frame_rate      = 25;
            st->codec.frame_rate_base = 1;
*/
						size -= (16+8*4+2+32+2*2);
#if 0
	    while (size >= 8) {
		MOV_atom_t a;
                int64_t start_pos;

		a.size = get_be32(pb);
		a.type = get_le32(pb);
		size -= 8;
#ifdef DEBUG
             printf("VIDEO: atom_type=%c%c%c%c atom.size=%Ld size_left=%d\n",
                       (a.type >> 0) & 0xff,
                       (a.type >> 8) & 0xff,
                       (a.type >> 16) & 0xff,
                       (a.type >> 24) & 0xff,
												a.size, size);
#endif
             start_pos = url_ftell(pb);

		switch(a.type) {
                case MKTAG('e', 's', 'd', 's'):
                    {
                        int tag, len;
                        /* Well, broken but suffisant for some MP4 streams */
                        get_be32(pb); /* version + flags */
			len = mov_mp4_read_descr(pb, &tag);
                        if (tag == 0x03) {
                            /* MP4ESDescrTag */
                            get_be16(pb); /* ID */
                            get_byte(pb); /* priority */
			    len = mov_mp4_read_descr(pb, &tag);
                            if (tag != 0x04)
                                goto fail;
                            /* MP4DecConfigDescrTag */
                            get_byte(pb); /* objectTypeId */
                            get_be32(pb); /* streamType + buffer size */
			    get_be32(pb); /* max bit rate */
                            get_be32(pb); /* avg bit rate */
                            len = mov_mp4_read_descr(pb, &tag);
                            if (tag != 0x05)
                                goto fail;
                            /* MP4DecSpecificDescrTag */
#ifdef DEBUG
                            printf("Specific MPEG4 header len=%d\n", len);
#endif
                            sc->header_data = av_mallocz(len);
                            if (sc->header_data) {
                                get_buffer(pb, sc->header_data, len);
				sc->header_len = len;
                            }
                        }
                        /* in any case, skip garbage */
                    }
                    break;
                default:
                    break;
                }
	    fail:
		printf("ATOMENEWSIZE %Ld   %d\n", atom.size, url_ftell(pb) - start_pos);
		if (atom.size > 8) {
		    url_fskip(pb, (atom.size - 8) -
			      ((url_ftell(pb) - start_pos)));
		    size -= atom.size - 8;
		}
	    }
            if (size > 0) {
                /* unknown extension */
                url_fskip(pb, size);
            }
#else
            a.size = size;
	    mov_read_default(c, pb, a);
#endif
	} else {
            st->codec.codec_id = codec_get_id(mov_audio_tags, format);
	    if(st->codec.codec_id==CODEC_ID_AMR_NB) //from TS26.244
	    {
#ifdef DEBUG
               int damr_size, damr_type, damr_vendor;
               printf("AMR-NB audio identified!!\n");
#endif
               get_be32(pb);get_be32(pb); //Reserved_8
               get_be16(pb);//Reserved_2
               get_be16(pb);//Reserved_2
               get_be32(pb);//Reserved_4
               get_be16(pb);//TimeScale
               get_be16(pb);//Reserved_2

                //AMRSpecificBox.(10 bytes)
                
#ifdef DEBUG
               damr_size=
#endif
               get_be32(pb); //size
#ifdef DEBUG
               damr_type=
#endif
               get_be32(pb); //type=='damr'
#ifdef DEBUG
               damr_vendor=
#endif
               get_be32(pb); //vendor
               get_byte(pb); //decoder version
               get_be16(pb); //mode_set
               get_byte(pb); //mode_change_period
               get_byte(pb); //frames_per_sample

#ifdef DEBUG
               printf("Audio: damr_type=%c%c%c%c damr_size=%d damr_vendor=%c%c%c%c\n",
                       (damr_type >> 24) & 0xff,
                       (damr_type >> 16) & 0xff,
                       (damr_type >> 8) & 0xff,
                       (damr_type >> 0) & 0xff,
		       damr_size, 
                       (damr_vendor >> 24) & 0xff,
                       (damr_vendor >> 16) & 0xff,
                       (damr_vendor >> 8) & 0xff,
                       (damr_vendor >> 0) & 0xff
		       );
#endif

               st->duration = AV_NOPTS_VALUE;//Not possible to get from this info, must count number of AMR frames
               st->codec.sample_rate=8000;
               st->codec.channels=1;
               st->codec.bits_per_sample=16;
               st->codec.bit_rate=0; /*It is not possible to tell this before we have 
                                       an audio frame and even then every frame can be different*/
	    }
            else if( st->codec.codec_tag == MKTAG( 'm', 'p', '4', 's' ))
            {
                //This is some stuff for the hint track, lets ignore it!
                //Do some mp4 auto detect.
                c->mp4=1;
                size-=(16);
                url_fskip(pb, size); /* The mp4s atom also contians a esds atom that we can skip*/
            }
	    else if(size>=(16+20))
	    {//16 bytes read, reading atleast 20 more
                uint16_t version = get_be16(pb); /* version */
#ifdef DEBUG
                printf("audio size=0x%X\n",size);
#endif
                get_be16(pb); /* revision level */
                get_be32(pb); /* vendor */

                st->codec.channels = get_be16(pb);		/* channel count */
				        st->codec.bits_per_sample = get_be16(pb);	/* sample size */

                /* handle specific s8 codec */
                get_be16(pb); /* compression id = 0*/
                get_be16(pb); /* packet size = 0 */

                st->codec.sample_rate = ((get_be32(pb) >> 16));
	        //printf("CODECID %d  %d  %.4s\n", st->codec.codec_id, CODEC_ID_PCM_S16BE, (char*)&format);

	        switch (st->codec.codec_id) {
	        case CODEC_ID_PCM_S16BE:
		    if (st->codec.bits_per_sample == 8)
		        st->codec.codec_id = CODEC_ID_PCM_S8;
                    /* fall */
	        case CODEC_ID_PCM_U8:
		    st->codec.bit_rate = st->codec.sample_rate * 8;
		    break;
	        default:
                    ;
	        }

                //Read QT version 1 fields. In version 0 theese dont exist
#ifdef DEBUG
                printf("version =%d mp4=%d\n",version,c->mp4);
                printf("size-(16+20+16)=%d\n",size-(16+20+16));
#endif
                if((version==1) && size>=(16+20+16))
                {
                    get_be32(pb); /* samples per packet */
                    get_be32(pb); /* bytes per packet */
                    get_be32(pb); /* bytes per frame */
                    get_be32(pb); /* bytes per sample */
                    if(size>(16+20+16))
                    {
                        //Optional, additional atom-based fields
                        MOV_atom_t a = { format, url_ftell(pb), size - (16 + 20 + 16 + 8) };
#ifdef DEBUG
                        printf("offest=0x%X, sizeleft=%d=0x%x,format=%c%c%c%c\n",(int)url_ftell(pb),size - (16 + 20 + 16 ),size - (16 + 20 + 16 ),
                            (format >> 0) & 0xff,
                            (format >> 8) & 0xff,
                            (format >> 16) & 0xff,
                            (format >> 24) & 0xff);
#endif
                        mov_read_default(c, pb, a);
                    }
                }
                else
                {
                    //We should be down to 0 bytes here, but lets make sure.
                    size-=(16+20);
#ifdef DEBUG
                    if(size>0)
                        printf("skipping 0x%X bytes\n",size-(16+20));
#endif
                    url_fskip(pb, size);
                }
            }
            else
            {
                size-=16;
                //Unknown size, but lets do our best and skip the rest.
#ifdef DEBUG
                printf("Strange size, skipping 0x%X bytes\n",size);
#endif
                url_fskip(pb, size);
            }
        }
    }

    return 0;
}

static int mov_read_stsc(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int entries, i;

    print_atom("stsc", atom);

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);
#ifdef DEBUG
printf("track[%i].stsc.entries = %i\n", c->fc->nb_streams-1, entries);
#endif
    sc->sample_to_chunk_sz = entries;
    sc->sample_to_chunk = (MOV_sample_to_chunk_tbl*) av_malloc(entries * sizeof(MOV_sample_to_chunk_tbl));
    if (!sc->sample_to_chunk)
        return -1;
    for(i=0; i<entries; i++) {
        sc->sample_to_chunk[i].first = get_be32(pb);
        sc->sample_to_chunk[i].count = get_be32(pb);
        sc->sample_to_chunk[i].id = get_be32(pb);
#ifdef DEBUG
/*        printf("sample_to_chunk first=%ld count=%ld, id=%ld\n", sc->sample_to_chunk[i].first, sc->sample_to_chunk[i].count, sc->sample_to_chunk[i].id); */
#endif
    }
    return 0;
}

static int mov_read_stsz(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int entries, i;

    print_atom("stsz", atom);

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    sc->sample_size = get_be32(pb);
    entries = get_be32(pb);
    sc->sample_count = entries;
#ifdef DEBUG
    printf("sample_size = %ld sample_count = %ld\n", sc->sample_size, sc->sample_count);
#endif
    if(sc->sample_size)
        return 0; /* there isn't any table following */
    sc->sample_sizes = (long*) av_malloc(entries * sizeof(long));
    if (!sc->sample_sizes)
        return -1;
    for(i=0; i<entries; i++) {
        sc->sample_sizes[i] = get_be32(pb);
#ifdef DEBUG
/*        printf("sample_sizes[]=%ld\n", sc->sample_sizes[i]); */
#endif
    }
    return 0;
}

static int mov_read_stts(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    //MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int entries, i;
    int duration=0;
    int total_sample_count=0;

    print_atom("stts", atom);

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */
    entries = get_be32(pb);


#ifdef DEBUG
printf("track[%i].stts.entries = %i\n", c->fc->nb_streams-1, entries);
#endif
    for(i=0; i<entries; i++) {
        int sample_duration;
        int sample_count;

        sample_count=get_be32(pb);
        sample_duration = get_be32(pb);
#ifdef DEBUG
        printf("sample_count=%d, sample_duration=%d\n",sample_count,sample_duration);
#endif
        duration+=sample_duration*sample_count;
        total_sample_count+=sample_count;

#if 0 //We calculate an average instead, needed by .mp4-files created with nec e606 3g phone

        if (!i && st->codec.codec_type==CODEC_TYPE_VIDEO) {
            st->codec.frame_rate_base = sample_duration ? sample_duration : 1;
            st->codec.frame_rate = c->streams[c->total_streams]->time_scale;
#ifdef DEBUG
            printf("VIDEO FRAME RATE= %i (sd= %i)\n", st->codec.frame_rate, sample_duration);
#endif
        }
#endif
    }

#define MAX(a,b) a>b?a:b
#define MIN(a,b) a>b?b:a
    /*The stsd atom which contain codec type sometimes comes after the stts so we cannot check for codec_type*/
    if(duration>0)
    {
        //Find greatest common divisor to avoid overflow using the Euclidean Algorithm...
        uint32_t max=MAX(duration,total_sample_count);
        uint32_t min=MIN(duration,total_sample_count);
        uint32_t spare=max%min;

        while(spare!=0)
        {
            max=min;
            min=spare;
            spare=max%min;
        }
        
        duration/=min;
        total_sample_count/=min;

        //Only support constant frame rate. But lets calculate the average. Needed by .mp4-files created with nec e606 3g phone.
        //To get better precision, we use the duration as frame_rate_base

        st->codec.frame_rate_base=duration;
        st->codec.frame_rate = c->streams[c->total_streams]->time_scale * total_sample_count;

#ifdef DEBUG
        printf("FRAME RATE average (video or audio)= %f (tot sample count= %i ,tot dur= %i timescale=%d)\n", (float)st->codec.frame_rate/st->codec.frame_rate_base,total_sample_count,duration,c->streams[c->total_streams]->time_scale);
#endif
    }
    else
    {
        st->codec.frame_rate_base = 1;
        st->codec.frame_rate = c->streams[c->total_streams]->time_scale;
    }
#undef MAX
#undef MIN
    return 0;
}

static int mov_read_trak(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st;
    MOVStreamContext *sc;

    print_atom("trak", atom);

    st = av_new_stream(c->fc, c->fc->nb_streams);
    if (!st) return -2;
    sc = (MOVStreamContext*) av_mallocz(sizeof(MOVStreamContext));
    if (!sc) {
	av_free(st);
        return -1;
    }

    sc->sample_to_chunk_index = -1;
    st->priv_data = sc;
    st->codec.codec_type = CODEC_TYPE_MOV_OTHER;
    st->start_time = 0; /* XXX: check */
    st->duration = (c->duration * (int64_t)AV_TIME_BASE) / c->time_scale;
    c->streams[c->fc->nb_streams-1] = sc;

    return mov_read_default(c, pb, atom);
}

static int mov_read_tkhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st;

    print_atom("tkhd", atom);

    st = c->fc->streams[c->fc->nb_streams-1];

    get_byte(pb); /* version */

    get_byte(pb); get_byte(pb);
    get_byte(pb); /* flags */
    /*
    MOV_TRACK_ENABLED 0x0001
    MOV_TRACK_IN_MOVIE 0x0002
    MOV_TRACK_IN_PREVIEW 0x0004
    MOV_TRACK_IN_POSTER 0x0008
    */

    get_be32(pb); /* creation time */
    get_be32(pb); /* modification time */
    st->id = (int)get_be32(pb); /* track id (NOT 0 !)*/
    get_be32(pb); /* reserved */
    st->start_time = 0; /* check */
    st->duration = (get_be32(pb) * (int64_t)AV_TIME_BASE) / c->time_scale; /* duration */
    get_be32(pb); /* reserved */
    get_be32(pb); /* reserved */

    get_be16(pb); /* layer */
    get_be16(pb); /* alternate group */
    get_be16(pb); /* volume */
    get_be16(pb); /* reserved */

    url_fskip(pb, 36); /* display matrix */

    /* those are fixed-point */
    st->codec.width = get_be32(pb) >> 16; /* track width */
    st->codec.height = get_be32(pb) >> 16; /* track height */

    return 0;
}

/* this atom should be null (from specs), but some buggy files put the 'moov' atom inside it... */
/* like the files created with Adobe Premiere 5.0, for samples see */
/* http://graphics.tudelft.nl/~wouter/publications/soundtests/ */
static int mov_read_wide(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int err;

#ifdef DEBUG
    print_atom("wide", atom);
    debug_indent++;
#endif
    if (atom.size < 8)
        return 0; /* continue */
    if (get_be32(pb) != 0) { /* 0 sized mdat atom... use the 'wide' atom size */
        url_fskip(pb, atom.size - 4);
        return 0;
    }
    atom.type = get_le32(pb);
    atom.offset += 8;
    atom.size -= 8;
    if (atom.type != MKTAG('m', 'd', 'a', 't')) {
        url_fskip(pb, atom.size);
        return 0;
    }
    err = mov_read_mdat(c, pb, atom);
#ifdef DEBUG
    debug_indent--;
#endif
    return err;
}


#ifdef CONFIG_ZLIB
static int null_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    return -1;
}

static int mov_read_cmov(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    ByteIOContext ctx;
    uint8_t *cmov_data;
    uint8_t *moov_data; /* uncompressed data */
    long cmov_len, moov_len;
    int ret;

    print_atom("cmov", atom);

    get_be32(pb); /* dcom atom */
    if (get_le32(pb) != MKTAG( 'd', 'c', 'o', 'm' ))
        return -1;
    if (get_le32(pb) != MKTAG( 'z', 'l', 'i', 'b' )) {
        dprintf("unknown compression for cmov atom !");
        return -1;
    }
    get_be32(pb); /* cmvd atom */
    if (get_le32(pb) != MKTAG( 'c', 'm', 'v', 'd' ))
        return -1;
    moov_len = get_be32(pb); /* uncompressed size */
    cmov_len = atom.size - 6 * 4;

    cmov_data = (uint8_t *) av_malloc(cmov_len);
    if (!cmov_data)
        return -1;
    moov_data = (uint8_t *) av_malloc(moov_len);
    if (!moov_data) {
        av_free(cmov_data);
        return -1;
    }
    get_buffer(pb, cmov_data, cmov_len);
    if(uncompress (moov_data, (uLongf *) &moov_len, (const Bytef *)cmov_data, cmov_len) != Z_OK)
        return -1;
    if(init_put_byte(&ctx, moov_data, moov_len, 0, NULL, null_read_packet, NULL, NULL) != 0)
        return -1;
    ctx.buf_end = ctx.buffer + moov_len;
    atom.type = MKTAG( 'm', 'o', 'o', 'v' );
    atom.offset = 0;
    atom.size = moov_len;
#ifdef DEBUG
    { int fd = open("/tmp/uncompheader.mov", O_WRONLY | O_CREAT); write(fd, moov_data, moov_len); close(fd); }
#endif
    ret = mov_read_default(c, &ctx, atom);
    av_free(moov_data);
    av_free(cmov_data);

    return ret;
}
#endif

static const MOVParseTableEntry mov_default_parse_table[] = {
/* mp4 atoms */
{ MKTAG( 'c', 'o', '6', '4' ), mov_read_stco },
{ MKTAG( 'c', 'p', 'r', 't' ), mov_read_default },
{ MKTAG( 'c', 'r', 'h', 'd' ), mov_read_default },
{ MKTAG( 'c', 't', 't', 's' ), mov_read_leaf }, /* composition time to sample */
{ MKTAG( 'd', 'i', 'n', 'f' ), mov_read_default }, /* data information */
{ MKTAG( 'd', 'p', 'n', 'd' ), mov_read_leaf },
{ MKTAG( 'd', 'r', 'e', 'f' ), mov_read_leaf },
{ MKTAG( 'e', 'd', 't', 's' ), mov_read_default },
{ MKTAG( 'e', 'l', 's', 't' ), mov_read_leaf },
{ MKTAG( 'f', 'r', 'e', 'e' ), mov_read_leaf },
{ MKTAG( 'h', 'd', 'l', 'r' ), mov_read_hdlr },
{ MKTAG( 'h', 'i', 'n', 't' ), mov_read_leaf },
{ MKTAG( 'h', 'm', 'h', 'd' ), mov_read_leaf },
{ MKTAG( 'i', 'o', 'd', 's' ), mov_read_leaf },
{ MKTAG( 'm', 'd', 'a', 't' ), mov_read_mdat },
{ MKTAG( 'm', 'd', 'h', 'd' ), mov_read_mdhd },
{ MKTAG( 'm', 'd', 'i', 'a' ), mov_read_default },
{ MKTAG( 'm', 'i', 'n', 'f' ), mov_read_default },
{ MKTAG( 'm', 'o', 'o', 'v' ), mov_read_moov },
{ MKTAG( 'm', 'p', '4', 'a' ), mov_read_default },
{ MKTAG( 'm', 'p', '4', 's' ), mov_read_default },
{ MKTAG( 'm', 'p', '4', 'v' ), mov_read_default },
{ MKTAG( 'm', 'p', 'o', 'd' ), mov_read_leaf },
{ MKTAG( 'm', 'v', 'h', 'd' ), mov_read_mvhd },
{ MKTAG( 'n', 'm', 'h', 'd' ), mov_read_leaf },
{ MKTAG( 'o', 'd', 'h', 'd' ), mov_read_default },
{ MKTAG( 's', 'd', 'h', 'd' ), mov_read_default },
{ MKTAG( 's', 'k', 'i', 'p' ), mov_read_default },
{ MKTAG( 's', 'm', 'h', 'd' ), mov_read_leaf }, /* sound media info header */
{ MKTAG( 'S', 'M', 'I', ' ' ), mov_read_smi }, /* Sorrenson extension ??? */
{ MKTAG( 's', 't', 'b', 'l' ), mov_read_default },
{ MKTAG( 's', 't', 'c', 'o' ), mov_read_stco },
{ MKTAG( 's', 't', 'd', 'p' ), mov_read_default },
{ MKTAG( 's', 't', 's', 'c' ), mov_read_stsc },
{ MKTAG( 's', 't', 's', 'd' ), mov_read_stsd }, /* sample description */
{ MKTAG( 's', 't', 's', 'h' ), mov_read_default },
{ MKTAG( 's', 't', 's', 's' ), mov_read_leaf }, /* sync sample */
{ MKTAG( 's', 't', 's', 'z' ), mov_read_stsz }, /* sample size */
{ MKTAG( 's', 't', 't', 's' ), mov_read_stts },
{ MKTAG( 't', 'k', 'h', 'd' ), mov_read_tkhd }, /* track header */
{ MKTAG( 't', 'r', 'a', 'k' ), mov_read_trak },
{ MKTAG( 't', 'r', 'e', 'f' ), mov_read_default }, /* not really */
{ MKTAG( 'u', 'd', 't', 'a' ), mov_read_leaf },
{ MKTAG( 'u', 'r', 'l', ' ' ), mov_read_leaf },
{ MKTAG( 'u', 'r', 'n', ' ' ), mov_read_leaf },
{ MKTAG( 'u', 'u', 'i', 'd' ), mov_read_default },
{ MKTAG( 'v', 'm', 'h', 'd' ), mov_read_leaf }, /* video media info header */
{ MKTAG( 'w', 'a', 'v', 'e' ), mov_read_default },
/* extra mp4 */
{ MKTAG( 'M', 'D', 'E', 'S' ), mov_read_leaf },
/* QT atoms */
{ MKTAG( 'c', 'h', 'a', 'p' ), mov_read_leaf },
{ MKTAG( 'c', 'l', 'i', 'p' ), mov_read_default },
{ MKTAG( 'c', 'r', 'g', 'n' ), mov_read_leaf },
{ MKTAG( 'c', 't', 'a', 'b' ), mov_read_ctab },
{ MKTAG( 'e', 's', 'd', 's' ), mov_read_esds },
{ MKTAG( 'k', 'm', 'a', 't' ), mov_read_leaf },
{ MKTAG( 'm', 'a', 't', 't' ), mov_read_default },
{ MKTAG( 'r', 'd', 'r', 'f' ), mov_read_leaf },
{ MKTAG( 'r', 'm', 'd', 'a' ), mov_read_default },
{ MKTAG( 'r', 'm', 'd', 'r' ), mov_read_leaf },
{ MKTAG( 'r', 'm', 'r', 'a' ), mov_read_default },
{ MKTAG( 's', 'c', 'p', 't' ), mov_read_leaf },
{ MKTAG( 's', 's', 'r', 'c' ), mov_read_leaf },
{ MKTAG( 's', 'y', 'n', 'c' ), mov_read_leaf },
{ MKTAG( 't', 'c', 'm', 'd' ), mov_read_leaf },
{ MKTAG( 'w', 'i', 'd', 'e' ), mov_read_wide }, /* place holder */
//{ MKTAG( 'r', 'm', 'q', 'u' ), mov_read_leaf },
#ifdef CONFIG_ZLIB
{ MKTAG( 'c', 'm', 'o', 'v' ), mov_read_cmov },
#else
{ MKTAG( 'c', 'm', 'o', 'v' ), mov_read_leaf },
#endif
{ 0L, mov_read_leaf }
};

static void mov_free_stream_context(MOVStreamContext *sc)
{
    if(sc) {
        av_free(sc->chunk_offsets);
        av_free(sc->sample_to_chunk);
        av_free(sc->sample_sizes);
        av_free(sc->header_data);
        av_free(sc);
    }
}

static inline uint32_t mov_to_tag(uint8_t *buf)
{
    return MKTAG(buf[0], buf[1], buf[2], buf[3]);
}

static inline uint32_t to_be32(uint8_t *buf)
{
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

/* XXX: is it sufficient ? */
static int mov_probe(AVProbeData *p)
{
    unsigned int offset;
    uint32_t tag;

    /* check file header */
    if (p->buf_size <= 12)
        return 0;
    offset = 0;
    for(;;) {
        /* ignore invalid offset */
        if ((offset + 8) > (unsigned int)p->buf_size)
            return 0;
        tag = mov_to_tag(p->buf + offset + 4);
        switch(tag) {
        case MKTAG( 'm', 'o', 'o', 'v' ):
        case MKTAG( 'w', 'i', 'd', 'e' ):
        case MKTAG( 'f', 'r', 'e', 'e' ):
        case MKTAG( 'm', 'd', 'a', 't' ):
        case MKTAG( 'p', 'n', 'o', 't' ): /* detect movs with preview pics like ew.mov and april.mov */
        case MKTAG( 'u', 'd', 't', 'a' ): /* Packet Video PVAuthor adds this and a lot of more junk */
            return AVPROBE_SCORE_MAX;
        case MKTAG( 'f', 't', 'y', 'p' ):
        case MKTAG( 's', 'k', 'i', 'p' ):
            offset = to_be32(p->buf+offset) + offset;
            break;
        default:
            /* unrecognized tag */
            return 0;
        }
    }
    return 0;
}

static int mov_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MOVContext *mov = (MOVContext *) s->priv_data;
    ByteIOContext *pb = &s->pb;
    int i, j, nb, err;
    MOV_atom_t atom = { 0, 0, 0 };

#ifdef DEBUG
		debug_indent= 0;
#endif
    mov->fc = s;
    mov->parse_table = mov_default_parse_table;
#if 0
    /* XXX: I think we should auto detect */
    if(s->iformat->name[1] == 'p')
        mov->mp4 = 1;
#endif
		atom.size = int64_t_C( 0x7fffffffffffffff );

#ifdef DEBUG
    printf("filesz=%Ld\n", atom.size);
#endif

    /* check MOV header */
    err = mov_read_default(mov, pb, atom);
		if( err== AVILIB_NEED_DATA )
			return err;

		// Dirty hack to make demuxer work
    if( err<0  ) 
		{
			fprintf(stderr, "mov: header not found !!! (err:%d, moov:%d, mdat:%d) pos:%lld\n",
				err, mov->found_moov, mov->found_mdat, url_ftell(pb));
			return -1;
    }
#ifdef DEBUG
    printf("on_parse_exit_offset=%d\n", (int) url_ftell(pb));
#endif
    mov->next_chunk_offset = mov->mdat_offset; /* initialise reading */

#ifdef DEBUG
    printf("mdat_reset_offset=%d\n", (int) url_ftell(pb));
#endif

#ifdef DEBUG
    printf("streams= %d\n", s->nb_streams);
#endif
    mov->total_streams = nb = s->nb_streams;

#if 1
    for(i=0; i<s->nb_streams;) {
        if(s->streams[i]->codec.codec_type == CODEC_TYPE_MOV_OTHER) {/* not audio, not video, delete */
            av_free(s->streams[i]);
            for(j=i+1; j<s->nb_streams; j++)
                s->streams[j-1] = s->streams[j];
            s->nb_streams--;
        } else
            i++;
    }
    for(i=0; i<s->nb_streams;i++) {
        MOVStreamContext *sc;
        sc = (MOVStreamContext *)s->streams[i]->priv_data;
        sc->ffindex = i;
        sc->is_ff_stream = 1;
    }
#endif
#ifdef DEBUG
    printf("real streams= %d\n", s->nb_streams);
#endif
    return 0;
}

/* Yes, this is ugly... I didn't write the specs of QT :p */
/* XXX:remove useless commented code sometime */
static int mov_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = (MOVContext *) s->priv_data;
    MOVStreamContext *sc;
    int64_t offset = int64_t_C( 0x0FFFFFFFFFFFFFFF );
    int i;
    int size;
    size = 0x0FFFFFFF;

#ifdef MOV_SPLIT_CHUNKS
    if (mov->partial) {
      int idx;

			sc = mov->partial;
			idx = sc->sample_to_chunk_index;

      if (idx < 0) return 0;
				size = sc->sample_sizes[sc->current_sample];

      sc->current_sample++;
      sc->left_in_chunk--;

      if (sc->left_in_chunk <= 0)
          mov->partial = 0;
      offset = mov->next_chunk_offset;
      /* extract the sample */

      goto readchunk;
    }
#endif

again:
    sc = NULL;
    for(i=0; i<mov->total_streams; i++) {
			MOVStreamContext *msc = mov->streams[i];
	//printf("MOCHUNK %ld  %d   %p  pos:%Ld\n", mov->streams[i]->next_chunk, mov->total_streams, mov->streams[i], url_ftell(&s->pb));
        if ((msc->next_chunk < msc->chunk_count) && msc->next_chunk >= 0
						&& (msc->chunk_offsets[msc->next_chunk] < offset)) {
					sc = msc;
					offset = msc->chunk_offsets[msc->next_chunk];
					//printf("SELETED  %Ld  i:%d\n", offset, i);
        }
    }
    if (!sc || offset==int64_t_C( 0x0FFFFFFFFFFFFFFF ))
			return -1;

    if(mov->next_chunk_offset < offset) 
		{ /* some meta data */
        if( url_fskip(&s->pb, (offset - mov->next_chunk_offset))< 0 )
					return AVILIB_NEED_DATA;
        mov->next_chunk_offset = offset;
    }

//printf("chunk: [%i] %lli -> %lli\n", st_id, mov->next_chunk_offset, offset);
    if(!sc->is_ff_stream) {
        if( url_fskip(&s->pb, (offset - mov->next_chunk_offset))< 0 )
					return AVILIB_NEED_DATA;
        mov->next_chunk_offset = offset;
				offset = int64_t_C( 0x0FFFFFFFFFFFFFFF );
        goto again;
    }

    /* now get the chunk size... */
    sc->next_chunk++;

    for(i=0; i<mov->total_streams; i++) {
			MOVStreamContext *msc = mov->streams[i];
			if ((msc->next_chunk < msc->chunk_count)
					&& ((msc->chunk_offsets[msc->next_chunk] - offset) < size))
					size = (int)( msc->chunk_offsets[msc->next_chunk] - offset );
		}

#ifdef MOV_MINOLTA_FIX
    //Make sure that size is according to sample_size (Needed by .mov files 
    //created on a Minolta Dimage Xi where audio chunks contains waste data in the end)
    //Maybe we should really not only check sc->sample_size, but also sc->sample_sizes
    //but I have no such movies
    if (sc->sample_size > 0) { 
        int foundsize=0;
        for(i=0; i<(sc->sample_to_chunk_sz); i++) {
            if( (sc->sample_to_chunk[i].first)<=(sc->next_chunk) && (sc->sample_size>0) )
            {
                foundsize=sc->sample_to_chunk[i].count*sc->sample_size;
            }
#ifdef DEBUG
            /*printf("sample_to_chunk first=%ld count=%ld, id=%ld\n", sc->sample_to_chunk[i].first, sc->sample_to_chunk[i].count, sc->sample_to_chunk[i].id);*/
#endif
        }
        if( (foundsize>0) && (foundsize<size) )
        {
#ifdef DEBUG
            /*printf("this size should actually be %d\n",foundsize);*/
#endif
            size=foundsize;
        }
    }
#endif //MOV_MINOLTA_FIX

#ifdef MOV_SPLIT_CHUNKS
    /* split chunks into samples */
    if (sc->sample_size == 0) {
        int idx = sc->sample_to_chunk_index;
        if ((idx + 1 < sc->sample_to_chunk_sz)
	    && (sc->next_chunk >= sc->sample_to_chunk[idx + 1].first))
           idx++;
        sc->sample_to_chunk_index = idx;
        if (idx >= 0 && sc->sample_to_chunk[idx].count != 1) {
	    mov->partial = sc;
            /* we'll have to get those samples before next chunk */
            sc->left_in_chunk = sc->sample_to_chunk[idx].count - 1;
            size = sc->sample_sizes[sc->current_sample];
        }

        sc->current_sample++;
    }
#endif

readchunk:
//printf("chunk: [%i] %lli -> %lli (%i)\n", st_id, offset, offset + size, size);
    if(size == 0x0FFFFFFF)
        size = (int)( mov->mdat_size + mov->mdat_offset - offset );
    if(size < 0)
        return -1;
    if(size == 0)
        return -1;
    if( url_fseek(&s->pb, offset, SEEK_SET)< 0 || get_mem_buffer_size( &s->pb )< size )
			return AVILIB_NEED_DATA;

    if (sc->header_len > 0) {
      av_new_packet(pkt, size + sc->header_len);
      memcpy(pkt->data, sc->header_data, sc->header_len);
      get_buffer(&s->pb, pkt->data + sc->header_len, size);
      /* free header */
      av_freep(&sc->header_data);
      sc->header_len = 0;
    } else {
        av_new_packet(pkt, size);
        get_buffer(&s->pb, pkt->data, pkt->size);
    }
    pkt->stream_index = sc->ffindex;

#ifdef DEBUG
/*
    printf("Packet (%d, %d, %ld) ", pkt->stream_index, st_id, pkt->size);
    for(i=0; i<8; i++)
        printf("%02x ", pkt->data[i]);
    for(i=0; i<8; i++)
        printf("%c ", (pkt->data[i]) & 0x7F);
    puts("");
*/
#endif

    mov->next_chunk_offset = offset + size;

    return 0;
}

static int mov_read_close(AVFormatContext *s)
{
    int i;
    MOVContext *mov = (MOVContext *) s->priv_data;
    for(i=0; i<mov->total_streams; i++)
        mov_free_stream_context(mov->streams[i]);
    for(i=0; i<s->nb_streams; i++)
	av_freep(&s->streams[i]);
    /* free color tabs */
    for(i=0; i<mov->ctab_size; i++)
	av_freep(&mov->ctab[i]);
    av_freep(&mov->ctab);
    return 0;
}

static AVInputFormat mov_iformat = {
    "mov",
    "QuickTime/MPEG4 format",
    sizeof(MOVContext),
    mov_probe,
    mov_read_header,
    mov_read_packet,
    mov_read_close,
		NULL,
		0,
		"mov,mp4"
};

int mov_init(void)
{
    av_register_input_format(&mov_iformat);
    return 0;
}


/*

import pymedia.video.muxer as muxer
dm= muxer.Demuxer("mp4")
f= open("c:\\bors\\Download\\movie.mp4", 'rb' )
s= f.read( 20000 )
r= dm.parse( s )

	*/