/*
 *			Extra light muxer/demuxer library for different file types
 *      such as avi, wmv, asf and so forth
 *      The list of all possible formats is available through 'formats' call
 *			The easiest way to add format is to use one from the libavformat project...
 *
 *
 *	Copyright (C) 2002-2003  Dmitry Borisov, Fedor Druzhinin
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Dmitry Borisov
*/

#include <Python.h>

#include "version.h"
#include "muxer.h"

#ifndef BUILD_NUM
#define BUILD_NUM 1
#endif
 
#define MODULE_NAME "pymedia"PYMEDIA_VERSION".muxer"
#define DEMUXER_NAME "Demuxer"
#define MUXER_NAME "Muxer"

const int PYBUILD= BUILD_NUM;
const char* PYDOC=
"Muxer allows to:\n"
"\t- Identify the format of individual streams inside compound streams such as asf, avi, mov etc\n"
"\t- Demux stream into the separate streams\n";

#define STREAMS "streams"
#define PARSE "parse"
#define RESET "reset"
#define HAS_HEADER "hasHeader"

#define PARSE_DOC \
	PARSE"( fragment ) -> streams\n \
	Parses stream and returns sub stream data in order it appear in the fragment"

#define RESET_DOC \
	RESET"() -> \n \
	Reset demultiplexer buffers"

#define STREAMS_DOC \
	STREAMS" -> streams\n \
	Returns list of streams within after the header is read in order they listed in a master stream.\n \
	It may contain the following attributes:\n \
		id	- internal stream identifier compatible with vcodec\n \
		fourcc - fourcc stream identifier if any \n\
		type - stream type{ video= 0 | audio= 1 | text= ?? }\n\
		bitrate - stream bitrate\n\
		width - picture width if any\n\
		height - picture height if any"

#define HAS_HEADER_DOC \
	HAS_HEADER"() -> { 1 | 0 }\n \
	Returns whether header presented or not in a stream. \n\
	You should not rely on 'streams' call if this function returns 0.\n\
	Some values may be used still, such as 'id' and 'type'\n\
	You should call parse() at least once before you can call this method.\n"

#define GET_INFO "getInfo"
#define GET_HEADER_INFO "getHeaderInfo"

#define GET_INFO_DOC \
	  GET_HEADER_INFO"() -> info\n\
Return all information known from the header by the demuxer as a dictionary. Predefined dictionary entries are:\n \
	"TITLE" - title for the song if exists\n \
	"AUTHOR" - author for the song if exists\n \
	"ALBUM" - song album if exists\n \
	"TRACK" - track number if exists\n \
	"GENRE" - genre title\n \
	"YEAR" - year of album\n \
	"COPYRIGHT" - copyright info\n \
	"COMMENT" - comment\n"

#define DEMUXER_DOC \
	"Demuxer( ext ) -> demuxer\n \
	Returns demuxer object based on extension passed. \nIt can demux stream into separate streams based on a \
stream type. Once demuxer is created the following methods are available:\n"\
"\t"HAS_HEADER_DOC \
"\t"PARSE_DOC \
"\t"RESET_DOC

PyObject *g_cErr;

// ---------------------------------------------------------------------------------
typedef struct
{
	PyObject_HEAD
	// Current strean context
	AVFormatContext ic;
	// Current packet buffer
	AVPacket pkt;
	// Whether header was found for the stream
	int bHasHeader;
	// Whether we tried to get header from the stream
	int bTriedHeader;
	PyObject* cBuffer;
	PyObject* cDict;
} PyDemuxerObject;

/* -----------------------------------------------------------------------------------------------*/
typedef struct AVILIBError
{
	int iErrCode;
	const char* sErrDesc;
} AVILIBError;

/* -----------------------------------------------------------------------------------------------*/
AVILIBError g_AvilibErr[]= {
	{ AVILIB_NO_ERROR, "Generic error. No further help available." },
	{ AVILIB_NO_HEADER, "There is no header in a file where it should be" },
	{ AVILIB_BAD_FORMAT, "The format of file is wrong." },
	{ AVILIB_BAD_HEADER, "The header of the file is corrupted." },
	{ AVILIB_ENCRYPTED, "The stream is encrypted and cannot be processed by codec." },
	{ 0, NULL }
};

// ---------------------------------------------------------------------------------
void free_mem_buffer( ByteIOContext* stBuf )
{
	if( stBuf->buffer )
		av_free( stBuf->buffer );

	stBuf->buffer = NULL;
}
// ---------------------------------------------------------------------------------
int fill_mem_buffer( ByteIOContext* stBuf, unsigned char* s, int iSize )
{
	int i;
	void *bufTmp;
	if( !stBuf->buffer )
	{
		stBuf->buffer= (UINT8 *)av_malloc( iSize );
		if( !stBuf->buffer )
		{
			PyErr_Format(g_cErr, "Cannot allocate %d bytes of memory. Exiting...", iSize );
			return 0;
		}
		stBuf->buf_ptr= stBuf->buffer;
		stBuf->buf_end= stBuf->buffer+ iSize;
		stBuf->pos= 0;
		memcpy( stBuf->buf_ptr, s, iSize );
	}
	else
	{
		i= stBuf->buf_end- stBuf->buf_ptr;
		//stBuf->pos+= stBuf->buf_ptr- stBuf->buffer;
		if( stBuf->buf_ptr- stBuf->buffer < iSize )
		{
			bufTmp= stBuf->buffer;
			stBuf->buffer= (UINT8 *)av_malloc( i+ iSize );
			if( !stBuf->buffer )
			{
				PyErr_Format(g_cErr, "Cannot allocate %d bytes of memory. Exiting...", iSize );
				return 0;
			}
			memcpy( stBuf->buffer, stBuf->buf_ptr, i );
			memcpy( stBuf->buffer+ i, s, iSize );
			stBuf->buf_end= stBuf->buffer+ i+ iSize;
			av_free( bufTmp );
		}
		else
		{
			memmove( stBuf->buffer, stBuf->buf_ptr, i );
			memcpy( stBuf->buffer+ i, s, iSize );
			stBuf->buf_end= stBuf->buffer+ i+ iSize;
		}
		stBuf->pos= 0;
		stBuf->buf_ptr= stBuf->buffer;
	}
	return 1;
}

// ---------------------------------------------------------------------------------
int SetAttributeI( PyObject* cDict, char* sKey, int iVal )
{
	PyObject* cVal= PyInt_FromLong( iVal );
	if( !cVal )
		return 0;
	PyDict_SetItemString( cDict, sKey, cVal );
	Py_DECREF( cVal );
	return 1;
}

// ---------------------------------------------------------------------------------
int SetAttributeS( PyObject* cDict, char* sKey, char* sVal )
{
	PyObject* cVal= PyString_FromString( sVal );
	if( !cVal )
		return 0;
	PyDict_SetItemString( cDict, sKey, cVal );
	Py_DECREF( cVal );
	return 1;
}

// ---------------------------------------------------------------------------------
int SetAttributeSS( PyObject* cDict, char* sKey, char* sVal, int iSize )
{
	PyObject* cVal= PyString_FromStringAndSize( sVal, iSize );
	if( !cVal )
		return 0;
	PyDict_SetItemString( cDict, sKey, cVal );
	Py_DECREF( cVal );
	return 1;
}

// ---------------------------------------------------------------------------------
int SetAttributeO( PyObject* cDict, char* sKey, PyObject* cVal )
{
	PyDict_SetItemString( cDict, sKey, cVal );
	Py_DECREF( cVal );
	return 1;
}

// ---------------------------------------------------------------------------------
PyObject* GetStreams( PyDemuxerObject* obj )
{
	// Freeup previous formats data
	int i;
	PyObject* cFormats= PyTuple_New( obj->ic.nb_streams );
	if( !cFormats )
		return NULL;

	for( i= 0; i< obj->ic.nb_streams; i++ )
	{
		PyObject* cFormat= Py_None;
		AVCodecContext *cCodec= &obj->ic.streams[ i ]->codec;
 //printf( "Duration: %I64d\n",obj->ic.streams[ i ]->duration );

		if( cCodec->codec_id )
		{
			cFormat= PyDict_New();
			if( !cFormat )
				return NULL;

			SetAttributeI( cFormat, PM_ID, cCodec->codec_id );
//			SetAttribute( cFormat, FOURCC, cCodec->fourcc );
			SetAttributeI( cFormat, PM_TYPE, cCodec->codec_type );
			SetAttributeI( cFormat, PM_BITRATE, cCodec->bit_rate );
			SetAttributeI( cFormat, PM_WIDTH, cCodec->width );
			SetAttributeI( cFormat, PM_HEIGHT, cCodec->height );
			SetAttributeI( cFormat, PM_FRAME_RATE, cCodec->frame_rate );
			SetAttributeI( cFormat, PM_FRAME_RATE_B, cCodec->frame_rate_base );
			SetAttributeI( cFormat, PM_SAMPLE_RATE, cCodec->sample_rate );
			SetAttributeI( cFormat, PM_CHANNELS, cCodec->channels );
			SetAttributeI( cFormat, PM_DURATION, (int)( obj->ic.streams[ i ]->duration / AV_TIME_BASE ) );
			SetAttributeI( cFormat, PM_BLOCK_ALIGN, cCodec->block_align );
			SetAttributeI( cFormat, PM_INDEX, i );
			if( cCodec->extradata_size> 0 )
				SetAttributeSS( cFormat, PM_EXTRA_DATA, (char*)cCodec->extradata, cCodec->extradata_size );
		}

		PyTuple_SetItem( cFormats, i, cFormat );
	}
	return cFormats;
}

// ---------------------------------------------------------------------------------
void StartStreams( PyDemuxerObject* obj )
{
	if( obj->cBuffer )
		Py_DECREF( obj->cBuffer );

	obj->cBuffer= PyList_New( 0 );
}


// ---------------------------------------------------------------------------------
int AppendStreamData( PyDemuxerObject* obj, AVPacket* cPkt )
{
  // Just a sanitary check
  PyObject* cRes;
  if( cPkt->stream_index>= obj->ic.nb_streams || cPkt->stream_index< 0 )
    return 1;

  if( !obj->cBuffer )
    obj->cBuffer= PyList_New( 0 );
  /* Add 07/19/2005 by Vadim Grigoriev  to keep DTS*/
  cRes= Py_BuildValue( "[is#iLL]", cPkt->stream_index, (const char*)cPkt->data, cPkt->size, cPkt->size, cPkt->pts ,cPkt->dts );
  PyList_Append( obj->cBuffer, cRes );
  Py_DECREF( cRes );
  return 1;
}

// ---------------------------------------------------------------------------------
static PyObject *
Demuxer_GetHeaderInfo( PyDemuxerObject* obj)
{
	/* Populate dictionary with the data if header already parsed */
	if( !obj->cDict && obj->bTriedHeader )
	{
		PyObject* cStr;
		obj->cDict= PyDict_New();
		if( !obj->cDict )
			return NULL;

		cStr= PyString_FromString( obj->ic.author );
		PyDict_SetItemString( obj->cDict, AUTHOR, cStr );
		Py_DECREF( cStr );
		cStr= PyString_FromString( obj->ic.title );
		PyDict_SetItemString( obj->cDict, TITLE, cStr );
		Py_DECREF( cStr );
		cStr= PyString_FromString( obj->ic.year );
		PyDict_SetItemString( obj->cDict, YEAR, cStr );
		Py_DECREF( cStr );
		cStr= PyString_FromString( obj->ic.album );
		PyDict_SetItemString( obj->cDict, ALBUM, cStr );
		Py_DECREF( cStr );
		cStr= PyString_FromString( obj->ic.track );
		PyDict_SetItemString( obj->cDict, TRACK, cStr );
		Py_DECREF( cStr );
		cStr= PyString_FromString( obj->ic.genre );
		PyDict_SetItemString( obj->cDict, GENRE, cStr );
		Py_DECREF( cStr );
	}

	if( obj->cDict )
	{
		/* Return builtin dictionary */
		Py_INCREF( obj->cDict );
		return obj->cDict;
	}

	PyErr_SetString( g_cErr, "The header has not been read yet. Cannot get stream information." );
	return NULL;
}

// ---------------------------------------------------------------------------------
static PyObject *
Demuxer_GetInfo( PyDemuxerObject* obj)
{
	if (PyErr_Warn(PyExc_DeprecationWarning, GET_INFO"() is deprecated. Please use the "GET_HEADER_INFO"() instead.") < 0)
		return NULL; 
  return Demuxer_GetHeaderInfo( obj );
}

// ---------------------------------------------------------------------------------
static PyObject *
Demuxer_HasHeader( PyDemuxerObject* obj)
{
	return PyLong_FromLong( obj->bHasHeader );
}

// ---------------------------------------------------------------------------------
static PyObject *
Demuxer_Reset( PyDemuxerObject* obj)
{
	free_mem_buffer( &obj->ic.pb );
	Py_INCREF( Py_None );
	return Py_None;
}


// ---------------------------------------------------------------------------------
PyObject *
Demuxer_Parse( PyDemuxerObject* obj, PyObject *args)
{
	unsigned char* sData;
	int iLen, iRet= 0, i= 0;
	if (!PyArg_ParseTuple(args, "s#:parse", &sData, &iLen ))
		return NULL;

	// Get the header data first
	// Sync the buffers first
	i= obj->ic.nb_streams;
	fill_mem_buffer( &obj->ic.pb, sData, iLen );
	if( !obj->bTriedHeader )
	{
		AVFormatParameters params;
		memset( &params, 0, sizeof( params ) );
		iRet= obj->ic.iformat->read_header( &obj->ic, &params );
		if( iRet>= 0 )
		{
			// Set the flag that we've tried to read the header
			obj->bTriedHeader= 1;
			//if( !( obj->ic.iformat->flags & AVFMT_NOHEADER ) )
			obj->bHasHeader= obj->ic.has_header;
		}
	}

	StartStreams( obj );
	while( iRet>= 0 )
	{
		obj->pkt.size= 0;
		iRet= av_read_packet(&obj->ic,&obj->pkt);
		if( iRet>= 0 )
		{
			if( obj->pkt.size> 0 )
			  {
				if( !AppendStreamData( obj, &obj->pkt ) )
				{
				  PyErr_Format(g_cErr, "Cannot allocate memory ( %d bytes ) for codec parameters", obj->pkt.size );
					return NULL;
				}


			  }
		}
	}

	// In case of error or something
	if( iRet<= AVILIB_ERROR )
	{
		// Need to report out the error( it should be in a error list
		while( g_AvilibErr[ i ].iErrCode )
			if( g_AvilibErr[ i ].iErrCode== iRet )
			{
				PyErr_SetString( g_cErr, g_AvilibErr[ i ].sErrDesc );
				return NULL;
			}
			else
				i++;

		PyErr_Format(g_cErr, "Unspecified error %d. Cannot find any help text for it.", iRet );
		return NULL;
	}
	//	printf(" Return the result\n");
//PyErr_SetObject(g_cErr, Py_BuildValue( "(OL)", obj->cBuffer, 123456890999L ));
//return NULL;
	Py_INCREF( obj->cBuffer );
	//printf(" Return the result% i \n");
	return obj->cBuffer;
}

// ---------------------------------------------------------------------------------
// List of all methods for the demuxer
static PyMethodDef demuxer_methods[] =
{
	{
		PARSE,
		(PyCFunction)Demuxer_Parse,
		METH_VARARGS,
		PARSE_DOC
	},
	{
		RESET,
		(PyCFunction)Demuxer_Reset,
		METH_NOARGS,
		RESET_DOC
	},
	{
		HAS_HEADER,
		(PyCFunction)Demuxer_HasHeader,
		METH_NOARGS,
		HAS_HEADER_DOC
	},
	{
		GET_INFO,
		(PyCFunction)Demuxer_GetInfo,
		METH_NOARGS,
		GET_INFO_DOC
	},
	{
		GET_HEADER_INFO,
		(PyCFunction)Demuxer_GetHeaderInfo,
		METH_NOARGS,
		GET_INFO_DOC
	},
	{ NULL, NULL },
};

// ----------------------------------------------------------------
static void
DemuxerClose( PyDemuxerObject *obj )
{
	// Close avicodec first !!!
	if( obj->cBuffer )
		Py_DECREF( obj->cBuffer );

	av_close_input_file( &obj->ic );
	free_mem_buffer( &obj->ic.pb );

	if( obj->cDict )
		Py_DECREF( obj->cDict );

	PyObject_Free( (PyObject*)obj );
}

// ---------------------------------------------------------------------------------
PyObject *
DemuxerNew( PyTypeObject *type, PyObject *args, PyObject *kwds )
{
	char* s;
	AVInputFormat *fmt= NULL;
	if (!PyArg_ParseTuple(args, "s:", &s ))
		return NULL;

	// Have extension and match the codec first
	for( fmt= first_iformat; fmt != NULL; fmt = fmt->next)
		if ( (fmt->extensions) && (strstr( fmt->extensions, s )))
		{
			// Create new decoder
			PyDemuxerObject* demuxer= (PyDemuxerObject*)type->tp_alloc(type, 0);
			if( !demuxer )
				return NULL;

			// cleanup decoder data
			memset( &demuxer->ic, 0, sizeof(demuxer->ic));
			memset( &demuxer->pkt, 0, sizeof(demuxer->pkt));
			demuxer->cBuffer= NULL;
			demuxer->bHasHeader= demuxer->bTriedHeader= 0;
			demuxer->ic.iformat = fmt;
			demuxer->cDict= NULL;

			// allocate private data
			if( fmt->priv_data_size )
			{
				demuxer->ic.priv_data = av_mallocz(fmt->priv_data_size);
				if (!demuxer->ic.priv_data)
				{
					PyErr_Format(g_cErr, "Cannot allocate memory ( %d bytes ) for codec parameters", fmt->priv_data_size );
					return NULL;
				}
			}
			else
				demuxer->ic.priv_data= NULL;

			return (PyObject*)demuxer;
		}


	PyErr_Format(g_cErr, "No registered demuxer for the '%s' extension", s );
	return NULL;
}

// ----------------------------------------------------------------
static PyGetSetDef demuxer_getsetlist[] =
{
	{STREAMS, (getter)GetStreams, NULL, STREAMS_DOC },
	{0},
};

// ----------------------------------------------------------------
PyTypeObject DemuxerType =
{
	PyObject_HEAD_INIT(NULL)
	0,
	MODULE_NAME"."DEMUXER_NAME,
	sizeof(PyDemuxerObject),
	0,
	(destructor)DemuxerClose,  //tp_dealloc
	0,			  //tp_print
	0,		//tp_getattr
	0,			  //tp_setattr
	0,			  //tp_compare
	0,			  //tp_repr
	0,			  //tp_as_number
	0,			  //tp_as_sequence
	0,				//tp_as_mapping
	0,					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	(char*)DEMUXER_DOC,		/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	demuxer_methods,				/* tp_methods */
	0,					/* tp_members */
	demuxer_getsetlist,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	DemuxerNew,	/* tp_new */
	PyObject_Del,				/* tp_free */
};

// ---------------------------------------------------------------------------------
// List of all methods for the mp3decoder
static PyMethodDef pymuxer_methods[] =
{
	{ NULL, NULL },
};

// ---------------------------------------------------------------------------------
#define INT_C(name) PyModule_AddIntConstant( m, #name, name )

// ---------------------------------------------------------------------------------
DL_EXPORT(void)
initmuxer(void)
{

	PyObject *m, *cExtensions;
	AVInputFormat *fmt, *lastFmt;

	Py_Initialize();
	m= Py_InitModule(MODULE_NAME, pymuxer_methods);

	// Formats
	avidec_init();
	avienc_init();
	mov_init();
	mpegts_init();
	mpegps_init();

  // Video extensions
	cExtensions = PyList_New(0);
  for(fmt = first_iformat; fmt != NULL; fmt = fmt->next)
	{
		/* Split string by commas */
		PyObject *cStr;

		char *s1= NULL, *s= (char*)fmt->extensions;
		while( s && ( ( s1= strchr( s, ',' ) )!= NULL || s) )
		{
			if( s1 )
			{
				cStr= PyString_FromStringAndSize( s, s1- s );
				s= s1+ 1;
			}
			else
			{
				cStr= PyString_FromString( s );
				s= NULL;
			}

			s1= NULL;
			PyList_Append( cExtensions, cStr );
			Py_DECREF( cStr );
		}
    lastFmt= fmt;
	}
	PyModule_AddObject( m, "video_extensions", cExtensions );


	asf_init();
  wav_init();
	raw_init();
#ifdef CONFIG_VORBIS
	ogg_init();
#endif
  // Audio extensions
	cExtensions = PyList_New(0);
  for( fmt= lastFmt->next; fmt != NULL; fmt = fmt->next)
	{
		/* Split string by commas */
		PyObject *cStr;

		char *s1= NULL, *s= (char*)fmt->extensions;
		while( s && ( ( s1= strchr( s, ',' ) )!= NULL || s) )
		{
			if( s1 )
			{
				cStr= PyString_FromStringAndSize( s, s1- s );
				s= s1+ 1;
			}
			else
			{
				cStr= PyString_FromString( s );
				s= NULL;
			}

			s1= NULL;
			PyList_Append( cExtensions, cStr );
			Py_DECREF( cStr );
		}
	}
	PyModule_AddObject( m, "audio_extensions", cExtensions );

	PyModule_AddStringConstant( m, "__doc__", (char*)PYDOC );
	PyModule_AddStringConstant( m, "version", PYMEDIA_VERSION_FULL );

	PyModule_AddStringConstant( m, TITLE_U"_KEY", TITLE );
	PyModule_AddStringConstant( m, AUTHOR_U"_KEY", AUTHOR );
	PyModule_AddStringConstant( m, ALBUM_U"_KEY", ALBUM );
	PyModule_AddStringConstant( m, TRACK_U"_KEY", TRACK );
	PyModule_AddStringConstant( m, GENRE_U"_KEY", GENRE );
	PyModule_AddStringConstant( m, YEAR_U"_KEY", YEAR );
	PyModule_AddStringConstant( m, COPYRIGHT_U"_KEY", COPYRIGHT );
	PyModule_AddStringConstant( m, COMMENT_U"_KEY", COMMENT );

	PyModule_AddIntConstant( m, "build", PYBUILD );
	INT_C(CODEC_TYPE_AUDIO);
	INT_C(CODEC_TYPE_VIDEO);
	//PyModule_AddObject( m, "extensions", cExtensions );

	g_cErr = PyErr_NewException(MODULE_NAME".MuxerError", NULL, NULL);
	if( g_cErr )
		PyModule_AddObject( m, "MuxerError", g_cErr );

	DemuxerType.ob_type = &PyType_Type;
	Py_INCREF((PyObject *)&DemuxerType);
	PyModule_AddObject(m, DEMUXER_NAME, (PyObject *)&DemuxerType);

	MuxerType.ob_type = &PyType_Type;
	Py_INCREF((PyObject *)&MuxerType);
	PyModule_AddObject(m, "Muxer", (PyObject *)&MuxerType);
}

/*
 
import pymedia.muxer as muxer
dm= muxer.Demuxer( 'avi' )
f= open( 'c:\\movies\\old\\Office Space (1999) Cd2 Dvdrip Xvid Ac3.6Ch Shc.avi', 'rb' )
s= f.read( 300000 )
r= dm.parse( s )
dm.streams

# wav testing
import pymedia
import pymedia.muxer as muxer
dm= muxer.Demuxer( 'wav' )
f= open( "c:\\bors\\hmedia\\Partners\\Scala\\code\\data\\ClipArt\\Music\\Wav\\3jazzy-loop.wav", 'rb' )
s= f.read( 300000 )
r= dm.parse( s )


import pymedia.muxer as muxer
dm= muxer.Demuxer( 'ogg' )
f= open( "c:\\music\\Green Velvet\\Green Velvet\\01-Flash.ogg", 'rb' )
s= f.read( 300000 )
r= dm.parse( s )
print dm.hasHeader(), dm.streams, dm.getHeaderInfo()
dm= muxer.Demuxer( 'mp3' )
f.seek( -128, 2 )
s= f.read( 128 )
r= dm.parse( s )
print dm.hasHeader(), dm.streams, dm.getHeaderInfo()

while len( s ):
  s= f.read( 512 )
  r= dm.parse( s )
  if len( r ):
    print '-----------------------'

import pymedia.muxer as muxer
dm= muxer.Demuxer( 'aac' )
f= open( 'c:\\bors\\media\\test.aac', 'rb' )
s= f.read( 3000 )
try:
  r= dm.parse( s )
except muxer.MuxerError, (a,b):
	print 'Exception', a,b


*/

