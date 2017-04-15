/*
 *			Extra light decompression library for audio files
 *      The list of all possible codecs supported may be obtained
 *			through the 'codecs' call.
 *			The easiest way to add codec is to use one from the libavcodec project...
 *
 *
 *		Copyright (C) 2005  Dmitry Borisov
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
#include <structmember.h>

#include <libavcodec/avcodec.h>
#include "libavcodec/dsputil.h"
#include "version.h"

#ifndef BUILD_NUM
#define BUILD_NUM 1
#endif

#define MODULE_NAME "pymedia"PYMEDIA_VERSION".audio.acodec"

const int PYBUILD= BUILD_NUM;
char* PYDOC=
"Audio decoding module:\n"
"\t- Decode frames of data into the raw PCM format\n"
"\t- Encodes raw PCM data into any available format( see acodec.formats )\n"
"\t- Provides all codecs with the same interface\n";

#define ENCODER_NAME "Encoder"
#define DECODER_NAME "Decoder"
#define ERROR_NAME "ACodecError"

#define SAMPLE_RATE "sample_rate"
#define BITRATE "bitrate"
#define CHANNELS "channels"
#define SAMPLE_LENGTH "sample_length"
#define BLOCK_ALIGN "block_align"
#define EXTRA_DATA "extra_data"
#define TYPE "type"
#define ID "id"
#define OLD_SAMPLE_RATE "old_sample_rate"
#define OLD_CHANNELS "old_channels"
#define DATA "data"
#define EXTENSION "ext"

#define DECODE "decode"
#define ENCODE "encode"
#define GET_PARAMS "getParams"
#define RESET_NAME "reset"
#define GET_CODEC_ID_NAME "getCodecID"


#define ACSTRING_NAME "ACString"

#define FRAME_DOC \
	"frame is an object that stores data about audio frame in PCM format. Possible members are:\n" \
	"\t"SAMPLE_RATE"\n"\
	"\t"BITRATE"\n"\
	"\t"CHANNELS"\n"\
	"\t"DATA"\n"

#define CONVERT_DOC \
	DECODE"( fragment ) -> audio_frame\n \
	Convert audio compressed data into pcm fragment. \n\
	Returns list of audio frames. "FRAME_DOC

#define RESET_DOC \
	RESET_NAME"() -> None\nReset current state of codec\n"

#define DECODER_DOC \
		DECODER_NAME" is a class which can decode compressed audio stream into raw uncomressed format( PCM )\n" \
		"suitable for playing through the sound module.\n" \
		"The following methods available:\n" \
		"\t"CONVERT_DOC \
		"\t"RESET_DOC

#define ENCODER_DOC \
	  ENCODER_NAME"( codecParams ) -> Codec\n "DECODER_NAME"(default) or encoder that will not use any demuxer to work with the stream\n"\
	  "it will assume that the whole string coming for decode() will contain one frame only"\
	  "The following methods available once you create Codec instance:\n" \
	  "\t"ENCODE_DOC

#define GET_PARAM_DOC \
	  GET_PARAMS"() -> params\n\
	  Parameters that represents the current state of codec\n"

#define ENCODE_DOC \
	ENCODE"( samples ) -> ( frames ) list of encoded string\n\
	  Encodes audio frame(s). It accepts audio samples\n\
		as string, buffers them, and returns list of encoded frames. Note, that its\n\
		behaviour is different from vcodec - vcodec returns only one frame."

#define GET_CODEC_ID_DOC \
	GET_CODEC_ID_NAME"(name) - returns internal numerical codec id for its name"

#define ACSTRING_DOC \
		ACSTRING_NAME"- is an object to represent regular C buffer to Python. Stores the decoded (PCM) audio data"

#define RETURN_NONE return (Py_INCREF(Py_None), Py_None);

PyObject *g_cErr;
static PyTypeObject ACStringType;


#define ENCODE_OUTBUF_SIZE  40000
// ---------------------------------------------------------------------------------
typedef struct
{
	PyObject_HEAD
	AVCodecContext *cCodec;
	AVCodec *codec;
	AVFrame frame;
	void *pPaddedBuf;
	int iPaddedSize;
	int iPaddedPos;
} PyACodecObject;

// ---------------------------------------------------------------------------------
typedef struct
{
	PyObject_HEAD
	void *pData;
	int iLen;
} PyACStringObject;

// ---------------------------------------------------------------------------------
typedef struct
{
	PyObject_HEAD

	PyACStringObject* cData;
	int bit_rate;
	int sample_rate;
	int bits_per_sample;
	int channels;
} PyAFrameObject;

// ---------------------------------------------------------------------------------
static PyACStringObject* ACString_New( void* pData, int iLen )
{
	PyACStringObject* cStr= (PyACStringObject*)PyObject_New( PyACStringObject, &ACStringType );
	if( !cStr )
		return NULL;

	cStr->iLen= iLen;
	cStr->pData= pData;

	// Make sure the codec is not deleted before we close the last vcstring
	return cStr;
}

// ----------------------------------------------------------------
static void ACString_Del( PyACStringObject *str )
{
	av_free( str->pData );
	PyObject_Free( (PyObject*)str );
}

// ----------------------------------------------------------------
static int
acstring_length(PyACStringObject *a){ return a->iLen; }

// ----------------------------------------------------------------
static int
acstring_buffer_getbuf(PyACStringObject *self, int index, const void **ptr)
{
	if( index != 0 )
	{
		PyErr_SetString(PyExc_SystemError, "accessing non-existent string segment");
		return -1;
	}
	*ptr = (void *)self->pData;
	return self->iLen;
}

// ----------------------------------------------------------------
static int
acstring_buffer_getsegcount(PyACStringObject *self, int *lenp)
{
	if ( lenp )
		*lenp = self->iLen;
	return 1;
}

// ----------------------------------------------------------------
static PySequenceMethods acstring_as_sequence = {
	(inquiry)acstring_length, /*sq_length*/
	0, /*sq_concat*/
	0, /*sq_repeat*/
	0, /*sq_item*/
	0, /*sq_slice*/
	0,		/*sq_ass_item*/
	0,		/*sq_ass_slice*/
	0 /*sq_contains*/
};

// ----------------------------------------------------------------
static PyBufferProcs acstring_as_buffer = {
	(getreadbufferproc)acstring_buffer_getbuf,
	0,
	(getsegcountproc)acstring_buffer_getsegcount,
	0,
};

// ----------------------------------------------------------------
static PyObject *
acstring_str(PyACStringObject *self)
{
	return PyString_FromStringAndSize(self->pData, self->iLen);
} 

// ----------------------------------------------------------------
static PyTypeObject ACStringType =
{
	PyObject_HEAD_INIT(NULL)
	0,
	ACSTRING_NAME,
	sizeof(PyACStringObject),
	0,
	(destructor)ACString_Del,				//tp_dealloc
	0,			  //tp_print
	0,				//tp_getattr
	0,			  //tp_setattr
	0,			  //tp_compare
	0,			  //tp_repr
	0,			  //tp_as_number
	&acstring_as_sequence,			  //tp_as_sequence
	0,				//tp_as_mapping
	0,					/* tp_hash */
	0,					/* tp_call */
	&acstring_str,					/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	&acstring_as_buffer,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	ACSTRING_DOC,					/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	0,				/* tp_methods */
	0,			/* tp_members */
	0,			/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,			/* tp_init */
	0,			/* tp_alloc */
	0,				/* tp_new */
	PyObject_Del, /* tp_free */
};

// ---------------------------------------------------------------------------------
int SetStructVal( int* pVal, PyObject* cObj, char* sKey )
{
	PyObject* obj =PyDict_GetItemString(cObj,sKey);
	if (!obj || !PyInt_Check(obj))
	  return 0;

	*pVal= PyInt_AsLong(obj);
	return 1;
}


// ---------------------------------------------------------------------------------
int SetExtraData( AVCodecContext *cCodec, PyObject* cObj )
{
	PyObject* obj =PyDict_GetItemString(cObj,EXTRA_DATA);
	if (!obj || !PyString_Check(obj))
	  return 0;

	cCodec->extradata= av_malloc( PyString_Size( obj ));
	if( !cCodec->extradata )
		return -1;
	cCodec->extradata_size= PyString_Size( obj );
	memcpy( cCodec->extradata, PyString_AsString( obj ), cCodec->extradata_size );
	return 1;
}

// ---------------------------------------------------------------------------------
int SetAttribute( PyObject* cDict, char* sKey, int iVal )
{
	PyObject* cVal= PyInt_FromLong( iVal );
	if( !cVal )
		return 0;

	PyDict_SetItemString( cDict, sKey, cVal );
	Py_DECREF( cVal );
	return 1;
}

// ---------------------------------------------------------------------------------
int SetCodecParams( PyACodecObject* obj, PyObject* cObj )
{
	if( !SetStructVal( &obj->cCodec->bit_rate, cObj, BITRATE ))
		return -1;
	if( !SetStructVal( &obj->cCodec->channels, cObj, CHANNELS ))
	  return -2;
	if( !SetStructVal( &obj->cCodec->sample_rate, cObj, SAMPLE_RATE ))
	  return -3;
	if( !SetStructVal( (int*)&obj->cCodec->codec_id, cObj, ID ))
	  return -4;
	// Non mandatory parameters
	SetStructVal( &obj->cCodec->block_align, cObj, BLOCK_ALIGN );
	SetExtraData( obj->cCodec, cObj );
	return 1;
}

// ---------------------------------------------------------------------------------
static PyObject * Codec_GetParams( PyACodecObject* obj, PyObject *args)
{
	PyObject* cRes= PyDict_New();
	if( !cRes )
		return NULL;
	SetAttribute( cRes, BITRATE, obj->cCodec->bit_rate );
	SetAttribute( cRes, CHANNELS, obj->cCodec->channels );
	SetAttribute( cRes, SAMPLE_RATE, obj->cCodec->sample_rate );
	SetAttribute( cRes, TYPE,obj->cCodec->codec_type);
	SetAttribute( cRes, ID,obj->cCodec->codec_id);
	return cRes;
}

// ---------------------------------------------------------------------------------
static PyObject * Codec_GetID( PyACodecObject* obj, PyObject *args)
{
	AVCodec *p;
	char *sName=NULL;
	int i= 0;

	if (!PyArg_ParseTuple(args, "s", &sName ))
		return NULL;

	p = avcodec_find_encoder_by_name(sName);
	if( !p )
		p = avcodec_find_decoder_by_name(sName);

	if (p)
		i= p->id;

	return PyInt_FromLong( i );
}
// ----------------------------------------------------------------
static PyObject *
frame_get_data(PyAFrameObject *obj)
{
  Py_INCREF( obj->cData );
	return (PyObject*)obj->cData;
}

// ----------------------------------------------------------------
static PyMemberDef frame_members[] =
{
	{SAMPLE_RATE,	T_INT, offsetof(PyAFrameObject,sample_rate), 0, "frame sample rate."},
	{BITRATE,	T_INT, offsetof(PyAFrameObject,bit_rate), 0, "frame bitrate."},
	{SAMPLE_LENGTH,	T_INT, offsetof(PyAFrameObject,bits_per_sample), 0, "frame bits per sample."},
	{CHANNELS,	T_INT, offsetof(PyAFrameObject,channels), 0, "number of channels."},
	{NULL}	/* Sentinel */
};

// ----------------------------------------------------------------
static PyGetSetDef frame_getsetlist[] =
{
	{DATA, (getter)frame_get_data, NULL, "frame data as a string."},
	{0},
};

// ----------------------------------------------------------------
static void AFrameClose( PyAFrameObject *obj )
{
	// Close avcodec first !!!
	if( obj->cData )
	{
		Py_DECREF( obj->cData );
	}

	PyObject_Free( (PyObject*)obj );
}


// ----------------------------------------------------------------
PyTypeObject FrameType =
{
	PyObject_HEAD_INIT(NULL)
	0,
	MODULE_NAME".Frame",
	sizeof(PyAFrameObject),
	0,
	(destructor)AFrameClose,  //tp_dealloc
	0,			  //tp_print
	0, //tp_getattr
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
	(char*)FRAME_DOC,		/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	0,					/* tp_methods */
	frame_members,					/* tp_members */
	frame_getsetlist,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	0,				/* tp_new */
	PyObject_Del,				/* tp_free */
};

// ---------------------------------------------------------------------------------
static PyObject * Decoder_Reset( PyACodecObject* obj)
{
	if( obj->cCodec->codec->resync )
		obj->cCodec->codec->resync( obj->cCodec );

	RETURN_NONE
}


// ---------------------------------------------------------------------------------
int Codec_AdjustPadBuffer( PyACodecObject* obj, int iLen )
{
	if( !obj->pPaddedBuf )
	{
		obj->pPaddedBuf= av_malloc(iLen+FF_INPUT_BUFFER_PADDING_SIZE);
		obj->iPaddedSize= iLen+FF_INPUT_BUFFER_PADDING_SIZE;
	}
	else if( obj->iPaddedSize< iLen+ FF_INPUT_BUFFER_PADDING_SIZE )
	{
		av_free( obj->pPaddedBuf );
		obj->pPaddedBuf= av_malloc(iLen+FF_INPUT_BUFFER_PADDING_SIZE);
		obj->iPaddedSize= iLen+FF_INPUT_BUFFER_PADDING_SIZE;
	}
	if( !obj->pPaddedBuf )
		return 0;

	memset((uint8_t*)obj->pPaddedBuf+ iLen,0,FF_INPUT_BUFFER_PADDING_SIZE);
	return 1;
}

// ---------------------------------------------------------------------------------
static PyObject *
Decoder_Decode( PyACodecObject* obj, PyObject *args)
{
	unsigned char* sData;
	void* pBuf;
	PyAFrameObject *cFrame= NULL;
	int iLen, out_size, len, iBufSize, iPos= 0;
	if (!PyArg_ParseTuple(args, "s#:decode", &sData, &iLen ))
		return NULL;

	// Get the header data first
	//need to add padding to buffer for libavcodec
	if( !Codec_AdjustPadBuffer( obj, iLen ) )
	{
		PyErr_NoMemory();
		return NULL;
	}
	memcpy( obj->pPaddedBuf, sData, iLen);
	sData=(uint8_t*)obj->pPaddedBuf;

	// Realloc memory
	iBufSize= AVCODEC_MAX_AUDIO_FRAME_SIZE* 2;
	pBuf= av_malloc( iBufSize );
	if( !pBuf )
	{
		PyErr_NoMemory();
		return NULL;
	}

	while( iLen> 0 )
	{
		if( iBufSize- iPos< AVCODEC_MAX_AUDIO_FRAME_SIZE )
		{
			pBuf= av_realloc( pBuf, iBufSize+ AVCODEC_MAX_AUDIO_FRAME_SIZE* 2 );
			if( !pBuf )
			{
				PyErr_NoMemory();
				return NULL;
			}
			iBufSize+= AVCODEC_MAX_AUDIO_FRAME_SIZE* 2;
		}
		out_size= 0;
		len= obj->cCodec->codec->decode( obj->cCodec, (char*)pBuf+ iPos, &out_size, sData, iLen );
		if( len < 0 )
		{
			// Need to report out the error( it should be in the error list )
			/*while( g_AvilibErr[ i ].iErrCode )
				if( g_AvilibErr[ i ].iErrCode== len )
				{
					PyErr_SetString(g_cErr, g_AvilibErr[ i ].sErrDesc );
					return NULL;
				}
				else
					i++;
			*/
			PyErr_Format(g_cErr, "Unspecified error %d. Cannot find any help text for it.", len );
			av_free( pBuf );
			return NULL;
		}
		else
		{
			iLen-= len;
			sData+= len;
			if( out_size> 0 )
			{
				iPos+= out_size;
				if( cFrame  )
				{
					cFrame->cData->pData= pBuf;
					cFrame->cData->iLen= iPos;
				}
				else
				{
					PyACStringObject* cRes;
					cFrame= (PyAFrameObject*)PyObject_New( PyAFrameObject, &FrameType );
					if( !cFrame )
						return NULL;

					cRes= ACString_New( pBuf, out_size );
					if( !cRes )
						return NULL;

					cFrame->bit_rate= obj->cCodec->bit_rate; 
					cFrame->sample_rate=	obj->cCodec->sample_rate;
					cFrame->bits_per_sample= obj->cCodec->bits_per_sample;
					cFrame->channels= obj->cCodec->channels;
					cFrame->cData= cRes;
				}
			}
		}
	}
#ifdef HAVE_MMX
    emms();
#endif

	if( !cFrame )
		free( pBuf );
	else
		return (PyObject*)cFrame;

	if( out_size )
		// Raise an error if data was found but no frames created
		return NULL;

	RETURN_NONE
}

// ---------------------------------------------------------------------------------
// List of all methods for the wmadecoder
static PyMethodDef decoder_methods[] =
{
	{
		RESET_NAME,
		(PyCFunction)Decoder_Reset,
		METH_NOARGS,
		RESET_DOC
	},
	{
		DECODE,
		(PyCFunction)Decoder_Decode,
		METH_VARARGS,
		CONVERT_DOC
	},
	{ NULL, NULL },
};

// ---------------------------------------------------------------------------------
static int GetFrameSize(PyACodecObject* obj )
{

	int frame_size =0;

	/* ugly hack for PCM codecs (will be removed ASAP with new PCM
	 *        support to compute the input frame size in samples */
	if (obj->cCodec->frame_size <= 1)
	{
		frame_size = ENCODE_OUTBUF_SIZE / obj->cCodec->channels;
		switch(obj->cCodec->codec_id)
		{
			case CODEC_ID_PCM_S16LE:
			case CODEC_ID_PCM_S16BE:
			case CODEC_ID_PCM_U16LE:
			case CODEC_ID_PCM_U16BE:
				frame_size >>= 1;
				break;
			default:
				break;
		}
	}
	else
		frame_size = obj->cCodec->frame_size* sizeof( short )* obj->cCodec->channels;

	return frame_size;
}

// ---------------------------------------------------------------------------------
static PyObject* ACodec_Encode( PyACodecObject* obj, PyObject *args)
{
	PyObject* cRes = NULL;
	uint8_t* sData = NULL;
	int iLen = 0, iPos= 0;
	PyObject* cFrame = NULL;
	char sOutbuf[ ENCODE_OUTBUF_SIZE ];

	if (!PyArg_ParseTuple(args, "s#:"ENCODE, &sData,&iLen))
		return NULL;

	if (!(obj->cCodec || obj->cCodec->codec))
	{
		PyErr_SetString(g_cErr, "Encode error: codec not initialized properly" );
		return NULL;
	}

	if( obj->iPaddedPos )
	{
		// Copy only the rest of the frame to the buffer
		iPos= GetFrameSize(obj)- obj->iPaddedPos;
		memcpy( (char*)obj->pPaddedBuf+ obj->iPaddedPos, sData, iPos );
	}

	cRes = PyList_New(0);
	if (!cRes)
		return NULL;

	while ( iLen- iPos>= GetFrameSize(obj) || obj->iPaddedPos )
	{
		int i= avcodec_encode_audio(obj->cCodec,
				sOutbuf,
				ENCODE_OUTBUF_SIZE,
				obj->iPaddedPos ? obj->pPaddedBuf: sData+ iPos );

		if (i >ENCODE_OUTBUF_SIZE)
		{
			PyErr_SetString(g_cErr, "Encode error: internal buffer is too small");
			return NULL;
		}
		if (i > 0)
		{
			cFrame = PyString_FromStringAndSize((const char*)sOutbuf, i );
			PyList_Append(cRes,cFrame);
			Py_DECREF( cFrame );
		}
		if( obj->iPaddedPos )
			obj->iPaddedPos= 0;
		else
			iPos+= GetFrameSize(obj);
	}

	// Copy the rest of the buffer to the temp place in the codec
	if( iLen- iPos )
	{
		obj->iPaddedPos= iLen- iPos;
		memcpy( obj->pPaddedBuf, sData+ iPos, obj->iPaddedPos );
	}

	return cRes;
}

// ---------------------------------------------------------------------------------
// List of all methods for the wmadecoder
static PyMethodDef encoder_methods[] =
{
	{
		ENCODE,
		(PyCFunction)ACodec_Encode,
		METH_VARARGS,
		ENCODE_DOC
	},
	{
		GET_PARAMS,
		(PyCFunction)Codec_GetParams,
		METH_NOARGS,
		GET_PARAM_DOC
	},
	{ NULL, NULL },
};

// ----------------------------------------------------------------
PyTypeObject EncoderType;
PyTypeObject DecoderType;

// ----------------------------------------------------------------
static void ACodecClose( PyACodecObject *acodec )
{
	// Close avcodec first !!!
	if( acodec->cCodec )
		avcodec_close(acodec->cCodec);

	if( acodec->pPaddedBuf )
		av_free( acodec->pPaddedBuf );

	PyObject_Free( (PyObject*)acodec );
}

// ---------------------------------------------------------------------------------
static PyACodecObject *
Codec_New( PyObject* cObj, PyTypeObject *type, int bDecoder )
{
	AVCodec *p;
	int iId, iRes;
	PyACodecObject* codec= (PyACodecObject* )type->tp_alloc(type, 0);
	if( !codec )
		return (PyACodecObject *)PyErr_NoMemory();

		// Get id first if possible
	codec->pPaddedBuf= NULL;
	if( PyDict_Check( cObj ) )
		iId= PyInt_AsLong( PyDict_GetItemString( cObj, ID ));
	else
	{
		PyErr_Format(g_cErr, "Codec(): First parameter should be dict (codec id and params)" );
		Py_DECREF( codec );
		return NULL;
	}

	p = ( bDecoder ) ?
		avcodec_find_decoder((enum CodecID)iId):
		avcodec_find_encoder((enum CodecID)iId);

	// cleanup decoder data
	//codec->cCodec= (AVCodecContext*)av_mallocz( sizeof( AVCodecContext ));
	if( !p )
	{
		PyErr_Format(g_cErr, "cannot find codec with id %x. Check the id in params you pass.", iId );
		Py_DECREF( codec );
		return NULL;
	}

  codec->cCodec= avcodec_alloc_context();
	if( !codec->cCodec )
	{
		PyErr_NoMemory();
		Py_DECREF( codec );
		return NULL;
	}

	codec->cCodec->codec= p;
	// Populate some values from the dictionary
	iRes= SetCodecParams( codec, cObj );
	if( iRes< 0 && !bDecoder )
	{
		// There is some poblem initializing codec with valid data
		char *s= "<NOT_FOUND>";
		switch( iRes )
		{
			case -1: 
				s= BITRATE;
				break;
			case -2:
				s= CHANNELS;
				break;
			case -3:
				s= SAMPLE_RATE;
			case -4:
				s= ID;
		};
		PyErr_Format(g_cErr, "'%s' parameter is missing when initializing codec.", s );
		Py_DECREF( codec );
		return NULL;
	}

	PyErr_Clear();
	if(p->capabilities & CODEC_CAP_TRUNCATED)
		codec->cCodec->flags |= CODEC_FLAG_TRUNCATED;

	avcodec_open( codec->cCodec, p );
	memset( &codec->frame, 0, sizeof( codec->frame ) );
	return codec;
}

// ---------------------------------------------------------------------------------
PyObject *
DecoderNew( PyTypeObject *type, PyObject *args, PyObject *kwds )
{
	PyObject* cObj;
	if (!PyArg_ParseTuple(args, "O:", &cObj))
		return NULL;

	// Create new  codec
	return (PyObject*)Codec_New( cObj, type, 1 );
}
// ----------------------------------------------------------------
PyTypeObject DecoderType =
{
	PyObject_HEAD_INIT(NULL)
	0,
	MODULE_NAME"."DECODER_NAME,
	sizeof(PyACodecObject),
	0,
	(destructor)ACodecClose,  //tp_dealloc
	0,			  //tp_print
	0, //tp_getattr
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
	(char*)DECODER_DOC,		/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	decoder_methods,				/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	DecoderNew,	/* tp_new */
	PyObject_Del,				/* tp_free */
};

// ---------------------------------------------------------------------------------
static PyObject *
EncoderNew( PyTypeObject *type, PyObject *args, PyObject *kwds )
{
	PyObject* cObj;
	PyACodecObject *obj;
	if (!PyArg_ParseTuple(args, "O:"ENCODER_NAME, &cObj ))
		return NULL;

	obj= Codec_New( cObj, type, 0 );
	if( !Codec_AdjustPadBuffer( obj, GetFrameSize( obj ) ) )
	{
		Py_DECREF( (PyObject*)obj );
		obj= NULL;
	}
	obj->iPaddedPos= 0;
	return (PyObject*)obj;
}

// ----------------------------------------------------------------
PyTypeObject EncoderType =
{
	PyObject_HEAD_INIT(NULL)
	0,
	MODULE_NAME"."ENCODER_NAME,
	sizeof(PyACodecObject),
	0,
	(destructor)ACodecClose,  //tp_dealloc
	0,			  //tp_print
	0, //tp_getattr
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
	(char*)ENCODER_DOC,		/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	encoder_methods,				/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	EncoderNew,	/* tp_new */
	PyObject_Del,				/* tp_free */
};

// ---------------------------------------------------------------------------------
// List of all methods for the mp3decoder
static PyMethodDef pympg_methods[] =
{
	{
		GET_CODEC_ID_NAME,
		(PyCFunction)Codec_GetID,
		METH_VARARGS,
		GET_CODEC_ID_DOC
	},
	{ NULL, NULL },
};

// ---------------------------------------------------------------------------------
DL_EXPORT(void)
initacodec(void)
{
	PyObject *m;

	Py_Initialize();
	m= Py_InitModule(MODULE_NAME, pympg_methods);

  /* register all the codecs (you can also register only the codec you wish to have smaller code */
  register_avcodec(&wmav1_decoder);
  register_avcodec(&wmav2_decoder);
  register_avcodec(&mp2_decoder);
  register_avcodec(&mp3_decoder);
  register_avcodec(&ac3_decoder);
  register_avcodec(&flac_decoder);

  register_avcodec(&ac3_encoder);
  register_avcodec(&mp2_encoder);

#ifdef CONFIG_VORBIS
  register_avcodec(&oggvorbis_decoder);
  register_avcodec(&oggvorbis_encoder);
#endif
#ifdef CONFIG_FAAD
	register_avcodec(&mpeg4aac_decoder);
	register_avcodec(&aac_decoder);
#endif
#if CONFIG_MP3LAME
	register_avcodec(&mp3lame_encoder);
#endif

#define PCM_CODEC(id, name) \
    register_avcodec(& name ## _decoder);

	PCM_CODEC(CODEC_ID_PCM_S16LE, pcm_s16le);
	PCM_CODEC(CODEC_ID_PCM_S16BE, pcm_s16be);
	PCM_CODEC(CODEC_ID_PCM_U16LE, pcm_u16le);
	PCM_CODEC(CODEC_ID_PCM_U16BE, pcm_u16be);
	PCM_CODEC(CODEC_ID_PCM_S8, pcm_s8);
	PCM_CODEC(CODEC_ID_PCM_U8, pcm_u8);
	PCM_CODEC(CODEC_ID_PCM_ALAW, pcm_alaw);
	PCM_CODEC(CODEC_ID_PCM_MULAW, pcm_mulaw);

  PCM_CODEC(CODEC_ID_ADPCM_IMA_QT, adpcm_ima_qt);
  PCM_CODEC(CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav);
  PCM_CODEC(CODEC_ID_ADPCM_MS, adpcm_ms);
  PCM_CODEC(CODEC_ID_ADPCM_4XM, adpcm_4xm);
  
	PyModule_AddStringConstant(m, "__doc__", PYDOC );
	PyModule_AddStringConstant(m, "version", PYMEDIA_VERSION_FULL );
	PyModule_AddIntConstant(m, "build", PYBUILD );

	g_cErr = PyErr_NewException(MODULE_NAME"."ERROR_NAME, NULL, NULL);
	if( g_cErr != NULL)
	  PyModule_AddObject(m, ERROR_NAME, g_cErr );

	DecoderType.ob_type = &PyType_Type;
	Py_INCREF((PyObject *)&DecoderType);
	PyModule_AddObject(m, DECODER_NAME, (PyObject *)&DecoderType);
	EncoderType.ob_type = &PyType_Type;
	Py_INCREF((PyObject *)&EncoderType);
	PyModule_AddObject(m, ENCODER_NAME, (PyObject *)&EncoderType);
}

/*
import pymedia
def test( name ):
	import acodec, sound, time
	dm= pymedia.muxer.Demuxer( str.split( name, '.' )[ -1 ] )
	f= open( name, 'rb' )
	snd= None
	while 1:
		s= f.read( 20000 )
		if len( s ):
			r= dm.parse( s )
			if r and len( r ):
				
			if snd== None:
				print 'Opening sound with %d channels' % r[ 3 ]
				snd= sound.output( r[ 1 ], r[ 3 ], 0x10 )
			while 1:
				try:
					snd.play( r[ 4 ] )
					break
				except:
					time.sleep( .1 )
		else:
			break


# test codec with frame decoder
import pymedia.audio.acodec as acodec
parms= {'index': 1, 'type': 1, 'frame_rate_base': 1, 'height': 0, 'channels': 2, 'width': 0, 'sample_rate': 48000, 'frame_rate': 25, 'bitrate': 192000, 'id': 9}
dec= acodec.Decoder( parms )

# test codec with format decoder
import pymedia.audio.acodec as acodec
dec= acodec.Decoder( 'mp3' )
f= open( 'c:\\music\\Roots.mp3', 'rb' )
s= f.read( 16384 )
r= dec.decode( s )
print dec.hasHeader()
print dec.getInfo()
print r.sample_rate

# test codec with encoder
def encode( codec ):
	import time
	import pymedia.audio.acodec as acodec
	import pymedia.muxer as muxer
	t= time.time()
	parms= {'channels': 2, 'sample_rate': 44100, 'bitrate': 128000, 'id': acodec.getCodecID(codec), 'ext': codec}
	mux= muxer.Muxer( codec )
	id= mux.addStream( muxer.CODEC_TYPE_AUDIO, parms )
	enc= acodec.Encoder( parms )
	f= open( 'c:\\music\\Roots.pcm', 'rb' )
	f1= open( 'c:\\music\\test_enc.'+ codec, 'wb' )
	s= f.read( 300000 )
	ss= mux.start()
	f1.write( ss )
	#enc.setInfo( { 'album': 'Test album', 'artist': 'Test artist', 'title': 'Test title', 'track': '23', 'year': '1985', 'comment': 'No words, just cool', 'copyright': 'Free for everyone' } )
	while len( s ):
		frames= enc.encode( s )
		for fr in frames:
			#print len( fr )
			ss= mux.write( id, fr )
			if ss: 
				f1.write( ss )
		
		s= f.read( 30000 )
	
	print 'Done in %.02f seconds' % ( time.time()- t )
	ss= mux.end() 
	if ss: 
		f1.write( )
	f.close()
	f1.close()

encode( 'ogg' )

def aplayer( name ):
	import pymedia.muxer as muxer, pymedia.audio.acodec as acodec, pymedia.audio.sound as sound
	import time
	dm= muxer.Demuxer( str.split( name, '.' )[ -1 ].lower() )
	f= open( name, 'rb' )
	snd= dec= None
	s= f.read( 32000 )
	while len( s ):
		frames= dm.parse( s )
		print 'demux produced %d frames' % len( frames )
		if frames:
			for fr in frames:
				# Assume for now only audio streams
				if dec== None:
					print dm.getInfo(), dm.streams[ fr[ 0 ] ]
					dec= acodec.Decoder( dm.streams[ fr[ 0 ] ] )
        
				print len( fr[ 1 ] )
				r= dec.decode( fr[ 1 ] )
				if r and r.data:
					if snd== None:
						print 'Opening sound with %d channels' % r.channels
						snd= sound.Output( r.sample_rate, r.channels, sound.AFMT_S16_LE )
					snd.play( r.data )
					
		s= f.read( 512 )
	
	while snd and snd.isPlaying():
	  time.sleep( .05 )

#aplayer( "c:\\music\ATB\\No Silence (2004)\\02 - Ecstasy.flac" )
#aplayer( "c:\\bors\\media\\test.wma" )
aplayer( "c:\\bors\\media\\test.aac" )

def dectest( name ):
	import pymedia.muxer as muxer, pymedia.audio.acodec as acodec
	dm= muxer.Demuxer( str.split( name, '.' )[ -1 ].lower() )
	f= open( name, 'rb' )
	snd= dec= None
	s= f.read( 32000 )
	frames= dm.parse( s )
	sTmp= ''
	if frames:
		for fr in frames:
			# Assume for now only audio streams
			if dec== None:
				#print dm.getInfo(), dm.streams[ fr[ 0 ] ]
				dec= acodec.Decoder( dm.streams[ fr[ 0 ] ] )
      
			r= dec.decode( fr[ 1 ] )
			if r and r.data:
				#print type( r.data )
				sTmp+= str( r.data )
				print len( sTmp )

#dectest( "c:\\music\ATB\\No Silence (2004)\\02 - Ecstasy.flac" )
aplayer( "c:\\bors\\media\\test.wma" )
#aplayer( "c:\\bors\\media\\test.aac" )
dectest( "c:\\bors\\medias\\test.mp3" )

*/
