/*
 * RAW encoder and decoder
 * Copyright (c) 2001 Fabrice Bellard, Dmitry Borisov
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

#define TITLE_STR "title"
#define ARTIST_STR "artist"
#define ALBUM_STR "album"
#define TRACK_STR "tracknumber"
#define YEAR_STR "year"
#define GENRE_STR "genre"

#define ID3_GENRE_MAX 125

static const char *GENRE_TAB[ID3_GENRE_MAX + 1] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco",
    "Funk","Grunge","Hip-Hop","Jazz","Metal",
    "New Age","Oldies","Other","Pop","R&B",
    "Rap", "Reggae", "Rock","Techno","Industrial",
    "Alternative","Ska","Death Metal","Pranks","Soundtrack",
    "Euro-Techno","Ambient","Trip-Hop","Vocal","Jazz+Funk",
    "Fusion",    "Trance",    "Classical",    "Instrumental",    "Acid",
    "House","Game","Sound Clip","Gospel","Noise",
    "AlternRock","Bass", "Soul","Punk","Space",
    "Meditative","Instrumental Pop","Instrumental Rock","Ethnic","Gothic",
    "Darkwave","Techno-Industrial","Electronic","Pop-Folk","Eurodance",
    "Dream","Southern Rock","Comedy","Cult","Gangsta",
    "Top 40","Christian Rap","Pop/Funk","Jungle","Native American",
    "Cabaret","New Wave","Psychadelic","Rave","Showtunes",
    "Trailer","Lo-Fi","Tribal","Acid Punk","Acid Jazz",
    "Polka","Retro","Musical","Rock & Roll","Hard Rock",
    "Folk","Folk-Rock","National Folk","Swing","Fast Fusion",
    "Bebob","Latin","Revival","Celtic","Bluegrass",
    "Avantgarde", "Gothic Rock","Progressive Rock","Psychedelic Rock","Symphonic Rock",
    "Slow Rock", "Big Band","Chorus","Easy Listening","Acoustic",
    "Humour","Speech", "Chanson","Opera","Chamber Music",
    "Sonata","Symphony","Booty Bass","Primus","Porn Groove",
    "Satire","Slow Jam",   "Club","Tango","Samba",
    "Folklore","Ballad","Power Ballad","Rhythmic Soul","Freestyle",
    "Duet","Punk Rock","Drum Solo","A capella","Euro-House",
    "Dance Hall",
};
 
// ---------------------------------------------------------------------------------
typedef struct
{
  char szTitle[30];
  char szArtist[30];
  char szAlbum[30];
  char szYear[4];
  char szCDDBCat[30];
  unsigned char genre;
} ID3INFO;

/* write ID3v2 tag */
int idv3v2_size_put( struct AVFormatContext *s, int i )
{
	char ss[ 4 ];
	ss[ 3 ]= i & 0x7f;
	ss[ 2 ]= ( i >> 7 ) & 0x7f;
	ss[ 1 ]= ( i >> 14 ) & 0x7f;
	ss[ 0 ]= ( i >> 21 ) & 0x7f;
	put_buffer( &s->pb, ss, 4 );
	return 0;
}

/* start writing ID3v2 */
int start_mp3_id3v2( struct AVFormatContext *s )
{
	put_byte( &s->pb, 'I' );
	put_byte( &s->pb, 'D' );
	put_byte( &s->pb, '3' );
	// Version
	put_byte( &s->pb, 3 );
	put_byte( &s->pb, 0 );
	// Flags
	put_byte( &s->pb, 0 );
	// Do not know what's the length would be...
	put_be32( &s->pb, 0 );
	return 0;
}

/* finish writing ID3v2 */
int finish_mp3_id3v2( struct AVFormatContext *s )
{
	// Save current position
	int i= url_ftell( &s->pb );
	// seek to the length field
	url_fseek( &s->pb, 6, SEEK_SET );
	idv3v2_size_put( s, i- 10 );
	url_fseek( &s->pb, i, SEEK_SET );
	return 0;
}

/* write ID3v2 tag */
int set_mp3_id3v2_tag( unsigned char* sData, char* sTag, struct AVFormatContext *s )
{
	int i= strlen( sData );
	// Put tag
	put_byte( &s->pb, sTag[ 0 ] );
	put_byte( &s->pb, sTag[ 1 ] );
	put_byte( &s->pb, sTag[ 2 ] );
	put_byte( &s->pb, sTag[ 3 ] );
	// Tag size
	idv3v2_size_put( s, i+ 1 );
	put_be16( &s->pb, 0 );
	put_byte( &s->pb, 0 );
	// Tag itself
	put_buffer( &s->pb, sData, i );
	return 0;
}

/* mp3 format with IDv2 support */
int mp3_write_header(struct AVFormatContext *s)
{
	int i= 0;
	if( s->has_header )
	{
		char year[ 5 ];
		start_mp3_id3v2( s );
		set_mp3_id3v2_tag( s->title, "TIT2", s );
		set_mp3_id3v2_tag( s->author, "TPE1", s );
		set_mp3_id3v2_tag( s->album, "TALB", s );
		set_mp3_id3v2_tag( s->track, "TRCK", s );
		strncpy( year, s->year, 4 );
		year[ 4 ]= 0;
		set_mp3_id3v2_tag( year, "TYER", s );
		set_mp3_id3v2_tag( s->comment, "COMM", s );
		set_mp3_id3v2_tag( s->copyright, "TCOP", s );
		finish_mp3_id3v2( s );
	}
	return 1;
}

/* simple formats */
int raw_write_header(struct AVFormatContext *s)
{
    return 0;
}

static int raw_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(&s->pb, pkt->data, pkt->size);
    put_flush_packet(&s->pb);
    return 0;
}
 
int raw_write_trailer(struct AVFormatContext *s)
{
    return 0;
}

/* raw input */
static int raw_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;
    int id;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    if (ap) {
        id = s->iformat->value;
        if (id == CODEC_ID_RAWVIDEO) {
            st->codec.codec_type = CODEC_TYPE_VIDEO;
        } else {
            st->codec.codec_type = CODEC_TYPE_AUDIO;
        }
        st->codec.codec_id = id;

        switch(st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            st->codec.sample_rate = ap->sample_rate;
            st->codec.channels = ap->channels;
            break;
        case CODEC_TYPE_VIDEO:
            st->codec.frame_rate = ap->frame_rate;
            st->codec.width = ap->width;
            st->codec.height = ap->height;
            break;
        default:
            return -1;
        }
				// Add some extradata to a header. Some codecs may require that
				if (s->pb.buf_end- s->pb.buf_ptr > 40) {
						st->codec.extradata_size = 40;
						st->codec.extradata = av_mallocz(40);
						memcpy( st->codec.extradata, s->pb.buf_ptr, st->codec.extradata_size);
				} 
    } else {
        return -1;
    }
    return 0;
}

#define RAW_PACKET_SIZE 4096

int raw_read_packet(AVFormatContext *s,
                    AVPacket *pkt)
{
    int ret, size;
    AVStream *st = s->streams[0];

    size= RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return -EIO;

    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, size);
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

int flac_read_packet(AVFormatContext *s,
                    AVPacket *pkt)
{
    int ret, size;
    AVStream *st = s->streams[0];

    size= RAW_PACKET_SIZE* 3;

    if (av_new_packet(pkt, size) < 0)
        return -EIO;

    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, size);
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

#define VORBIS_COMMENT 4

/* --------------------------------------------------------------------------------- */
void get_flac_tag( char* sDest, char* sFrameName, char* sBuf, int iLen )
{
	/* HACK. Locate desired frame in a buffer and get the data from there */
	int i= *(int*)sBuf;
	int iPos= i+ 8;
	sBuf+= iPos;
	while( iPos< iLen )
	{
		int iFrameLen= *(int*)sBuf;
		int iSize= iFrameLen- strlen( sFrameName )- 1;
		if( !strncmp( sBuf+ 4, sFrameName, strlen( sFrameName ) ) && iSize> 0 )
		{
			memcpy( sDest, sBuf+ strlen( sFrameName )+ 5, iSize );
			sDest[ iSize ]= 0;
			return;
		}

		iPos+= iFrameLen+ 4;
		sBuf+= iFrameLen+ 4;
	}
}

/* --------------------------------------------------------------------------------- */
/* flac head read */
static int flac_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
  AVStream *st;
	unsigned char sTmp[ 1024 ];
  ByteIOContext *pb = &s->pb;

	if( s->has_header )
		return 0;

	/* See if id tags are present */
	if( get_mem_buffer_size( pb )< 10 )
		return AVILIB_NEED_DATA;

	get_str( pb, sTmp, 4 );
	if( !strncmp( sTmp, "fLaC", 4 ))
	{
		int iLast= 0;
		while( !iLast )
		{
			/* Almost sure we have FLAC in here */
			int iLen1, iType;
			get_str( pb, sTmp, 4 );
			iLen1= ( sTmp[ 1 ] << 16 ) | ( sTmp[ 2 ] << 8 ) | sTmp[ 3 ];
			if( iLen1 > get_mem_buffer_size( pb ) )
			{
				/* No enough data to get the correct tag info */
				url_fseek( pb, 0, SEEK_SET );
				return AVILIB_NEED_DATA;
			}

			// See if we got the comment field
			iLast= sTmp[ 0 ] & 0x80;
			// Get the type of the meta field
			iType= sTmp[ 0 ] & 0x7f;
			get_str( pb, &sTmp[ 0 ], iLen1 );

			if( iType== VORBIS_COMMENT )
			{
				get_flac_tag( s->title, TITLE_STR, sTmp, iLen1 );
				get_flac_tag( s->author, ARTIST_STR, sTmp, iLen1 );
				get_flac_tag( s->album, ALBUM_STR, sTmp, iLen1 );
				get_flac_tag( s->track, TRACK_STR, sTmp, iLen1 );
				get_flac_tag( s->year, YEAR_STR, sTmp, iLen1 );
				s->has_header= 1;
				break;
			}
		}
	}

	url_fseek( pb, 0, SEEK_SET );
  st = av_new_stream(s, 0);
  if (!st)
      return AVERROR_NOMEM;

  st->codec.codec_type = CODEC_TYPE_AUDIO;
  st->codec.codec_id = CODEC_ID_FLAC;
  /* the parameters will be extracted from the compressed bitstream */
  return 0;
}

int raw_read_close(AVFormatContext *s)
{
	if( s->album_cover )
		av_free( s->album_cover );
    return 0;
}


extern const uint16_t mpa_bitrate_tab[2][3][15];
extern const uint16_t mpa_freq_tab[3];

/* --------------------------------------------------------------------------------- */
/* return frame size */
static int mp3_frame_size(uint32_t header)
{
    int bitrate_index, layer, sample_rate, frame_size= 0, mpeg25, padding, lsf;
    if (header & (1<<20))
		{
        lsf = (header & (1<<19)) ? 0 : 1;
        mpeg25 = 0;
    }
		else
		{
        lsf = 1;
        mpeg25 = 1;
    }

    layer = 4 - ((header >> 17) & 3);
    /* extract frequency */
    sample_rate = mpa_freq_tab[ (header >> 10) & 3 ] >> (lsf + mpeg25);
    bitrate_index = (header >> 12) & 0xf;
    padding = (header >> 9) & 1;

    if (bitrate_index != 0)
		{
        frame_size = mpa_bitrate_tab[ lsf ][ layer - 1 ][ bitrate_index ];
        switch( layer )
				{
          case 1:
            frame_size = (frame_size * 12000) / sample_rate;
            frame_size = (frame_size + padding) * 4;
            break;
          case 2:
            frame_size = (frame_size * 144000) / sample_rate+ padding;
            break;
          default:
          case 3:
            frame_size = (frame_size * 144000) / (sample_rate << lsf)+ padding;
            break;
        }
    }
    return frame_size;
}

/* --------------------------------------------------------------------------------- */
/* fast header check for resync */
static int check_header(uint32_t header)
{
return header== 0xfffbb204;
    /* header */
    if ((header & 0xffe00000) != 0xffe00000)
	return 0;
    /* layer check */
    if (((header >> 17) & 3) == 0)
	return 0;
    /* bit rate */
    if (((header >> 12) & 0xf) == 0xf)
	return 0;
    /* frequency */
    if (((header >> 10) & 3) == 3)
	return 0;
    return 1;
}

/* --------------------------------------------------------------------------------- */
int mp3_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size, sz;
    AVStream *st = s->streams[0];
		uint32_t iHeader= 0;
	  ByteIOContext *pb = &s->pb;

		// Try to locate mp3 header tags and get the frame size
		while( get_mem_buffer_size( pb )> 0 && !check_header( iHeader ) )
		{
			int c= get_byte(pb);
			iHeader= ( iHeader << 8 ) | c;
		}

		sz= get_mem_buffer_size( pb );
		if( sz== 0 )
		{
			url_fskip(pb, -4 );
			return AVILIB_NEED_DATA;
		}
		// Here we found the mp3 header
    size= mp3_frame_size( iHeader );
		// It may not be correct, but it was here before...
		if( !size )
		{
			printf( "Size cannot be calculated for a stream\n" );
			size= ( RAW_PACKET_SIZE> sz ? RAW_PACKET_SIZE: sz );
		}

    if (av_new_packet(pkt, size) < 0)
        return -EIO;

    pkt->stream_index = 0;
		url_fskip(pb, -4 );
    ret = get_buffer(&s->pb, pkt->data, size);
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

/* --------------------------------------------------------------------------------- */
void get_mp3_id3v2_tag( char** sDest1, char* sDest, char* sFrameName, char* sBuf, int iLen, int iMaxLen )
{
	/* HACK. Locate desired frame in a buffer and get the data from there */
	int iPos= 0;
	while( iPos< iLen )
	{
		unsigned char* sTmp=	strstr( sBuf, sFrameName );
		if( sTmp )
		{
			/* Possibly found the tag. Just return the value...*/
			int iSize= ( sTmp[ 4 ] << 21 ) | ( sTmp[ 5 ] << 14 ) | ( sTmp[ 6 ] << 7 ) | sTmp[ 7 ];
			if( iSize> 0 )
			{
				if( sDest1 )
				{
					// Allocate memory for the tag
					sDest= av_malloc( iSize ); 
					*sDest1= sDest;
					if( !sDest )
						return;
				}
        memcpy( sDest, sTmp+ 11, ( iMaxLen< iSize ) ? iMaxLen- 1: iSize- 1 );
			}
			return;
		}

		iPos+= strlen( sBuf )+ 1;
		sBuf+= strlen( sBuf )+ 1;
	}
}

/* --------------------------------------------------------------------------------- */
/* mp3 head read */
static int mp3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
  AVStream *st;
	char sTmp[ 1024 ];
  ByteIOContext *pb = &s->pb;

	if( s->has_header )
		return 0;

	/* See if id tags are present */
	if( get_mem_buffer_size( pb )< 10 )
		return AVILIB_NEED_DATA;

	get_str( pb, sTmp, 3 );
	if( !strncmp( sTmp, "ID3", 3 ))
	{
		/* Almost sure we have id3v2 tag in there, lets get the length and see if the whole tag fits into the buffer */
		int iLen1;
		char* sTmp1;
		get_str( pb, sTmp, 7 );
		iLen1= ( sTmp[ 3 ] << 21 ) | ( sTmp[ 4 ] << 14 ) | ( sTmp[ 5 ] << 7 ) | sTmp[ 6 ];
		if( iLen1 > get_mem_buffer_size( pb ) )
		{
			/* No enough data to get the correct tag info */
			url_fseek( pb, -10, SEEK_CUR );
			return AVILIB_NEED_DATA;
		}

		/* Copy only up to the buffer length or all if any */
		sTmp1= av_malloc( iLen1 );
		if( !sTmp1 )
			return AVERROR_NOMEM;

		get_str( pb, sTmp1, iLen1 );
		get_mp3_id3v2_tag( NULL, s->title, "TIT2", sTmp1, iLen1, sizeof( s->title ) );
		get_mp3_id3v2_tag( NULL, s->author, "TPE1", sTmp1, iLen1, sizeof( s->author ) );
		get_mp3_id3v2_tag( NULL, s->album, "TALB", sTmp1, iLen1, sizeof( s->album ) );
		get_mp3_id3v2_tag( NULL, s->track, "TRCK", sTmp1, iLen1, sizeof( s->track ) );
		get_mp3_id3v2_tag( NULL, s->year, "TYER", sTmp1, iLen1, sizeof( s->year ) );
		get_mp3_id3v2_tag( NULL, s->genre, "TCON", sTmp1, iLen1, sizeof( s->genre ) );
    // Check reference in ID3v2 tag
    if( s->genre[ 0 ]== '(' )
    {
      int iGenre= 0;
      s->genre[ strchr( s->genre, ')' )- s->genre ]= 0;
      iGenre= atoi( s->genre+ 1 );
      if( iGenre> ID3_GENRE_MAX )
        strcpy( s->genre, "unknown" );
      else
        strcpy( s->genre, GENRE_TAB[ iGenre ] );
    }
		//get_mp3_id3v2_tag( &s->album_cover, NULL, "APIC", sTmp1, iLen1 );

		av_free( sTmp1 );
		s->has_header= 1;
	}
	else if( !strncmp( sTmp, "TAG", 3 ))
	{
		/* Almost sure we have id3 tag in here */
		ID3INFO* id3_info= (ID3INFO*)sTmp;
    int i;
		if( get_mem_buffer_size( pb )< 125 )
			/* No enough data to get the correct tag info( hopefully it won't happen ever... ) */
			return 0;

		get_str( pb, sTmp, 125 );

		// Populate title/author/album/year/track etc
		strncpy( s->title, id3_info->szTitle, 30 );
    i= 29;
    while( s->title[ i ]== ' ' )
      s->title[ i-- ]= 0;
		strncpy( s->author, id3_info->szArtist, 30 );
    i= 29;
    while( s->author[ i ]== ' ' )
      s->author[ i-- ]= 0;
		strncpy( s->album, id3_info->szAlbum, 30 );
    i= 29;
    while( s->album[ i ]== ' ' )
      s->album[ i-- ]= 0;
		strncpy( s->year, id3_info->szYear, 4 );
		// Process genre from the extrnal tab
    if( id3_info->genre> ID3_GENRE_MAX )
  		strcpy( s->genre, "unknown" );
    else
		  strcpy( s->genre, GENRE_TAB[ id3_info->genre ] );
		s->has_header= 1;
	}
	else
		url_fseek( pb, -3, SEEK_CUR );

  st = av_new_stream(s, 0);
  if (!st)
      return AVERROR_NOMEM;

  st->codec.codec_type = CODEC_TYPE_AUDIO;
  st->codec.codec_id = CODEC_ID_MP2;
  /* the parameters will be extracted from the compressed bitstream */
  return 0;
}


#define SEQ_START_CODE		0x000001b3
#define GOP_START_CODE		0x000001b8
#define PICTURE_START_CODE	0x00000100

AVInputFormat mp3_iformat = {
    "mp3",
    "MPEG audio",
    0,
    NULL,
    mp3_read_header,
    raw_read_packet,
    raw_read_close,
		NULL,
		0,
    "mp2,mp3", /* XXX: use probe */
};


AVInputFormat ac3_iformat = {
    "ac3",
    "raw ac3",
    0,
    NULL,
    raw_read_header,
    raw_read_packet,
    raw_read_close,
		NULL,
		0,
    "ac3",
    CODEC_ID_AC3
};

AVInputFormat aac_iformat = {
    "aac",
    "raw/mpeg2 aac",
    0,
    NULL,
    raw_read_header,
    raw_read_packet,
    raw_read_close,
		NULL,
		0,
    "aac",
    CODEC_ID_AAC
}; 

AVInputFormat flac_iformat = {
    "flac",
    "FLAC loseless format",
    0,
    NULL,
    flac_read_header,
    flac_read_packet,
    raw_read_close,
		NULL,
		0,
    "flac",
    CODEC_ID_FLAC
}; 

AVOutputFormat mp3_oformat = {
    "mp3",
    "MPEG audio layer 3",
    "audio/x-mpeg",
    "mp3,mp2",
    0,
    CODEC_ID_MP3,
    0,
    mp3_write_header,
    raw_write_packet,
    raw_write_trailer,
};
 
#ifdef WORDS_BIGENDIAN
#define BE_DEF(s) s
#define LE_DEF(s) NULL
#else
#define BE_DEF(s) NULL
#define LE_DEF(s) s
#endif


int raw_init(void)
{
    av_register_input_format(&mp3_iformat);
    av_register_input_format(&ac3_iformat);
    av_register_input_format(&aac_iformat);
    av_register_input_format(&flac_iformat);
    av_register_output_format(&mp3_oformat);
    return 0;
}
