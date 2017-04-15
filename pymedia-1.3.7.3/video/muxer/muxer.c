/*
 *	Extra light muxer/demuxer library for different file types
 *      such as avi, wmv, asf and so forth
 *      The list of all possible formats is available through 'formats' call
 *	The easiest way to add format is to use one from the libavformat project...
 *
 *
 *	Copyright (C) 2002-2004  Dmitry Borisov, Fedor Druzhinin
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

#include "muxer.h"
#include "libavformat/patch.h"

#ifndef BUILD_NUM
#define BUILD_NUM 1
#endif

#define MUXER_DOC \
"Muxer( ext ) -> Muxer\n \
	Returns muxer object based on extension passed. \nMuxer can multiplex stream from separate streams based on a \
stream type. Once Muxer is created the following methods are available:\n"\
		"\t"START_DOC \
		"\t"END_DOC \
		"\t"ADD_STREAM_DOC \
		"\t"WRITE_FRAME_DOC \
		"\t"GET_STREAM_PTS_DOC

#define SET_INFO_NAME "setInfo"
#define SET_INFO_DOC \
	SET_INFO_NAME"( { 'album': .., 'artist',.. }) -> \n \
	Sets the header for the compressed file. The following supported at the moment:\n \
	album, artist, year, track, title, comment, copyright\n"

#define ADD_STREAM_NAME "addStream" 
#define ADD_STREAM_DOC ADD_STREAM_NAME"(codec_id, [codec_params]) -> stream index\n\
		Adds  stream to muxer. codec_params is an optional \
		codec parameters dictionary.\n"

#define START_NAME "start"
#define START_DOC START_NAME"() -> encoded string or None if no data\n\
		File header generation.\n"

#define END_NAME "end"
#define END_DOC END_NAME"() -> encoded string or None if no data\n\
		Footer generation\n"
		
#define WRITE_FRAME_NAME "write"
#define WRITE_FRAME_DOC WRITE_FRAME_NAME"(stream_index, str) ->encoded string or None if no data\n\
		Write frame into on of the streams in muxer.\n"

#define GET_STREAM_PTS_NAME "getStreamPTS"
#define GET_STREAM_PTS_DOC GET_STREAM_PTS_NAME"(stream_index) -> tuple of pts_val, pts_num, pts_den for stream \n"

// ---------------------------------------------------------------------------------
typedef struct
{
	PyObject_HEAD
	// Current stream context
	AVFormatContext oc;
	int bStarted;
	// Current packet buffer
} PyMuxerObject;


// ---------------------------------------------------------------------------------
int SetStructVal( int* pVal, PyObject* cObj, char* sKey )
{
	PyObject* obj =PyDict_GetItemString(cObj,sKey);
	if (obj && PyInt_Check(obj))
		*pVal= PyInt_AsLong(obj);
	else 
		return 0;

	return 1;
}

// ----------------------------------------------------------------
static int SetStreamParams(PyObject* cParams,AVCodecContext* cCodec) 
{
	char *s= NULL;
	switch (cCodec->codec_type) 
	{
		case CODEC_TYPE_VIDEO:
			if (cParams) 
			{
				if( !SetStructVal( (int*)&cCodec->codec_id, cParams, PM_ID ))
					s= PM_ID;

				if( !SetStructVal( &cCodec->bit_rate, cParams, PM_BITRATE ))
					s= PM_BITRATE;

				if( !SetStructVal( &cCodec->height, cParams, PM_HEIGHT ))
					s= PM_BITRATE;

				if( !SetStructVal( &cCodec->width, cParams, PM_WIDTH ))
					s= PM_WIDTH;

				if( !SetStructVal( &cCodec->frame_rate, cParams, PM_FRAME_RATE ))
					s= PM_FRAME_RATE;

				if( !SetStructVal( &cCodec->frame_rate_base, cParams, PM_FRAME_RATE_B ))
					cCodec->frame_rate_base= 1;

				if( !SetStructVal( &cCodec->gop_size, cParams, PM_GOP_SIZE))
					cCodec->gop_size= 12;

				if( !SetStructVal( &cCodec->max_b_frames, cParams,	PM_MAX_B_FRAMES))
					cCodec->max_b_frames= 0;
			}
			else
				s= PM_ID;

			break;

		case CODEC_TYPE_AUDIO:
			if (cParams) 
			{
				if( !SetStructVal( &cCodec->bit_rate, cParams, PM_BITRATE ))
					s= PM_BITRATE;

				if( !SetStructVal( &cCodec->sample_rate, cParams,PM_SAMPLE_RATE))
					s= PM_SAMPLE_RATE;

				if( !SetStructVal( &cCodec->channels, cParams,PM_CHANNELS))
					s= PM_CHANNELS;
			}

			break;

		default:
			PyErr_Format(g_cErr, "Unknown stream codec type: %d",	cCodec->codec_type );
			return 0;
	}

	// If default cannot be set
	if( s )
	{
		PyErr_Format( g_cErr, "%s codec: required codec parameter '%s' not found. No default can be specified.", 
			( cCodec->codec_type== CODEC_TYPE_AUDIO ) ? "audio": "video", s );
		return 0;
	}
	return 1;
}

// ---------------------------------------------------------------------------------
static PyObject* Muxer_SetInfo( PyMuxerObject* obj, PyObject *args)
{
	PyObject* cInfo = NULL;
	PyObject* cTmp;
	if (!PyArg_ParseTuple(args, "O!:"SET_INFO_NAME, &PyDict_Type, &cInfo ))
		return NULL;

	// Start parsing the info and feeding it to the codec
	cTmp= PyDict_GetItemString( cInfo, AUTHOR );
	if( cTmp )
	{
		int i= PyString_Size( cTmp );
		strncpy( obj->oc.author, PyString_AsString( cTmp ), i );
		obj->oc.author[ ++i ]= 0;
	}

	cTmp= PyDict_GetItemString( cInfo, TITLE );
	if( cTmp )
	{
		int i= PyString_Size( cTmp );
		strncpy( obj->oc.title, PyString_AsString( cTmp ), i );
		obj->oc.title[ ++i ]= 0;
	}

	cTmp= PyDict_GetItemString( cInfo, YEAR );
	if( cTmp )
	{
		int i= PyString_Size( cTmp );
		strncpy( obj->oc.year, PyString_AsString( cTmp ), i );
		obj->oc.year[ ++i ]= 0;
	}

	cTmp= PyDict_GetItemString( cInfo, ALBUM );
	if( cTmp )
	{
		int i= PyString_Size( cTmp );
		strncpy( obj->oc.album, PyString_AsString( cTmp ), i );
		obj->oc.album[ ++i ]= 0;
	}

	cTmp= PyDict_GetItemString( cInfo, TRACK );
	if( cTmp )
	{
		int i= PyString_Size( cTmp );
		strncpy( obj->oc.track, PyString_AsString( cTmp ), i );
		obj->oc.track[ ++i ]= 0;
	}

	cTmp= PyDict_GetItemString( cInfo, COPYRIGHT );
	if( cTmp )
	{
		int i= PyString_Size( cTmp );
		strncpy( obj->oc.copyright, PyString_AsString( cTmp ), i );
		obj->oc.copyright[ ++i ]= 0;
	}

	cTmp= PyDict_GetItemString( cInfo, COMMENT );
	if( cTmp )
	{
		int i= PyString_Size( cTmp );
		strncpy( obj->oc.comment, PyString_AsString( cTmp ), i );
		obj->oc.comment[ ++i ]= 0;
	}

	obj->oc.has_header= 1;
	RETURN_NONE
}

/*
	// ---------------------------------------------------------------------------------
static PyObject* ACodec_Flush( PyACodecObject* obj)
{
	int i;
	if( obj->ic.oformat->write_packet )
		obj->ic.oformat->write_trailer( &obj->ic );

	// Make the resulting string
	i= (int)url_ftell( &obj->ic.pb );
	url_fseek( &obj->ic.pb, 0, SEEK_SET );
	return PyString_FromStringAndSize( obj->ic.pb.buffer, i );
}

	// ---------------------------------------------------------------------------------
static PyObject* ACodec_Mux( PyACodecObject* obj, PyObject *args)
{
	PyObject* cInfo = NULL;
	PyObject *cData;
	int iCount, i;
	if (!PyArg_ParseTuple(args, "O:"MUX_NAME, &cData ))
		return NULL;

	if( !PySequence_Check( cData ))
	{
		PyErr_SetString(g_cErr, MUX_NAME" accepts only sequence type as parameter. " );
		return NULL;
	}

	// Check if file format was chosen through the extension
	if( !obj->ic.oformat )
	{
		PyErr_SetString(g_cErr, "The format in which muxer will work is not chosen when you created Encoder. \n"
														"Parameters should include 'ext' which will define the output format\n"
														"You should create the encoder with correct parameters in order to use '"MUX_NAME"()' function" );
		return NULL;
	}

	if( !obj->iTriedHeader )
	{
		// Process header into the internal buffer
		init_put_byte( &obj->ic.pb );
		if( obj->ic.oformat->write_header )
			obj->ic.oformat->write_header( &obj->ic );
		obj->iTriedHeader= 1;
	}
	// Start muxing frames
	iCount= PySequence_Size( cData );
	for( i= 0; i< iCount; i++ )
	{
		PyObject *cS= PySequence_GetItem( cData, i );
		if( PyString_Check( cS ) )
		{
			if( obj->ic.oformat->write_packet )
				obj->ic.oformat->write_packet( &obj->ic, 0, PyString_AsString( cS ), PyString_Size( cS ), 0 );
		}
		else
		{
			PyErr_Format(g_cErr, "Element at index %d is not a string. Muxing of elementary audio streams can accept strings only.", i );
			return NULL;
		}
	}
	// Make the resulting string
	i= (int)url_ftell( &obj->ic.pb );
	url_fseek( &obj->ic.pb, 0, SEEK_SET );
	return PyString_FromStringAndSize( obj->ic.pb.buffer, i );
}


*/

// ----------------------------------------------------------------
static PyObject* Muxer_AddStream( PyMuxerObject* obj, PyObject *args)
{
	AVCodecContext *cCodec;
	AVStream *st;
	int iCodecType;
	PyObject* cParams= NULL;

	if (!PyArg_ParseTuple(args, "iO:", &iCodecType, &cParams ))
		return NULL;
	
	if( !PyDict_Check(cParams) )
	{
		PyErr_Format(g_cErr, ADD_STREAM_NAME"(): second parameter must be dictionary" );
		return NULL; 
	}

	st = av_new_stream(&obj->oc, iCodecType);
	if (!st) 
	{
		PyErr_Format(g_cErr, ADD_STREAM_NAME"(): Could not allocate stream");
		return NULL;
	}
	cCodec = &st->codec;
	cCodec->codec_type = (enum CodecType)iCodecType;

	if( av_set_parameters( &obj->oc, NULL )< 0 )
	{
		PyErr_Format(g_cErr, ADD_STREAM_NAME"(): Could not set format parameters");
		return NULL;
	}

	//add stream parameters
	if (!SetStreamParams(cParams,cCodec))
		return NULL;

	return PyInt_FromLong(st->index);

}

// ----------------------------------------------------------------
static PyObject* Muxer_Start( PyMuxerObject* obj)
{
	PyObject* cRes = NULL;
	// Validate that start() was called. No action if that's the case
	if( obj->bStarted )
		RETURN_NONE

	if (init_put_byte(&obj->oc.pb) < 0)
	{
		PyErr_Format(g_cErr, START_NAME"(): Error initialising init_put_byte");
		return NULL;
	}
	
	if (av_write_header(&obj->oc) < 0)  
	{
		PyErr_Format(g_cErr, ADD_STREAM_NAME"(): Error while writing file header");
		return NULL;
	}
	obj->bStarted= 1;
	return PyString_FromStringAndSize((char*)obj->oc.pb.buffer, obj->oc.pb.buf_ptr- obj->oc.pb.buffer );
}

// ---------------------------------------------------------------------------------

static PyObject* Muxer_End( PyMuxerObject* obj, PyObject *args) 
{
	PyObject* cRes = NULL;

	// Validate that start() was called 
	if( !obj->bStarted )
	{
		PyErr_Format(g_cErr,  
			"The format was not initialized. start() needs te be called first before calling any other muxer method");
		return NULL;
	}

	if (av_write_trailer(&obj->oc)) 
	{
		PyErr_Format(g_cErr,  "Error while writing file trailer");
		if (obj->oc.pb.out_buf.buffer)
			free(obj->oc.pb.out_buf.buffer);

		obj->oc.pb.out_buf.buffer= NULL;
		return NULL;
	}

	//printf("got %d bytes from muxer when writing trailer\n",
	//		obj->oc.pb.out_buf->buf_size);
	if (obj->oc.pb.out_buf.buffer) 
	{
		cRes = PyString_FromStringAndSize((char*)obj->oc.pb.out_buf.buffer, obj->oc.pb.out_buf.buf_size);

		free(obj->oc.pb.out_buf.buffer);
		obj->oc.pb.out_buf.buffer= NULL;
	} 
	else 
	{
		Py_INCREF( Py_None );
		cRes = Py_None;	
	}


	if (obj->oc.pb.buffer)
		av_free(obj->oc.pb.buffer);

	obj->oc.pb.out_buf.buffer= NULL;
	obj->oc.pb.out_buf.buf_size= 0;
	memset(&obj->oc.pb,0,sizeof(ByteIOContext));
	return cRes;
}
// ---------------------------------------------------------------------------------

static PyObject* Muxer_Write_Frame( PyMuxerObject* obj, PyObject *args)
{
	int iStreamID;
	uint8_t* sData = NULL;
	int iLen = 0;
	PyObject* cRes = NULL;
	AVPacket cPckt;

	if (!PyArg_ParseTuple(args, "is#:", &iStreamID, &sData, &iLen ))
		return NULL;

	// Validate that start() was called 
	if( !obj->bStarted )
	{
		PyErr_Format(g_cErr,  
			"The format was not initialized. start() needs te be called first before calling any other muxer method");
		return NULL;
	}

	cPckt.stream_index= iStreamID;
	cPckt.data= sData;
	cPckt.size= iLen;
	cPckt.pts= 0;
	if (av_write_frame(&obj->oc, &cPckt ))
	{
		PyErr_Format(g_cErr,  "Error while writing frame");
		if (obj->oc.pb.out_buf.buffer)
			free(obj->oc.pb.out_buf.buffer);

		obj->oc.pb.out_buf.buffer= NULL;
		return NULL;
	}
	
	//printf("got %d bytes from muxer\n",obj->oc.pb.out_buf->buf_size);
	if (obj->oc.pb.buffer) 
	{
		cRes = PyString_FromStringAndSize((char*)obj->oc.pb.buffer,	obj->oc.pb.buf_ptr- obj->oc.pb.buffer );
		obj->oc.pb.buf_ptr= obj->oc.pb.buffer;
	} 
	else 
	{
		Py_INCREF( Py_None );
		cRes = Py_None;	
	}

	return cRes;
}

// ---------------------------------------------------------------------------------
//returns a tuple of (val,num,den)
static PyObject* GetStreamPTS( PyMuxerObject* obj, PyObject *args)
{
	int iStreamID;
	
	if (!PyArg_ParseTuple(args, "i:", &iStreamID) )
		return NULL;
	
	if (!obj->oc.streams)
	{
		PyErr_Format(g_cErr,  "Codec not initialized");
		return NULL;
	}
	
	if ((iStreamID > obj->oc.nb_streams)||(iStreamID < 0)) 
	{
		PyErr_Format(g_cErr,  "Bad stream index");
		return NULL;
	}

	return Py_BuildValue("i,i,i",
			obj->oc.streams[iStreamID]->pts.val,
			obj->oc.streams[iStreamID]->pts.num,
			obj->oc.streams[iStreamID]->pts.den);
}

// ---------------------------------------------------------------------------------
// List of all methods for the demuxer
static PyMethodDef muxer_methods[] =
{
	{
		ADD_STREAM_NAME,
		(PyCFunction)Muxer_AddStream,
		METH_VARARGS,
		ADD_STREAM_DOC
	},
	{
		SET_INFO_NAME,
		(PyCFunction)Muxer_SetInfo,
		METH_VARARGS,
		SET_INFO_DOC
	},
	{
		WRITE_FRAME_NAME,
		(PyCFunction)Muxer_Write_Frame,
		 METH_VARARGS,
		WRITE_FRAME_DOC
	},
	{
		START_NAME,
		(PyCFunction)Muxer_Start,
		METH_NOARGS,
		START_DOC
	},
	{	END_NAME,
		(PyCFunction)Muxer_End,
		METH_NOARGS,
		END_DOC
	},
	{ GET_STREAM_PTS_NAME,
		(PyCFunction)GetStreamPTS,
		METH_VARARGS,
		GET_STREAM_PTS_DOC
	},
	{ NULL, NULL },
};


// ----------------------------------------------------------------
static PyObject * MuxerNew( PyTypeObject *type, PyObject *args, PyObject *kwds )
{
	AVOutputFormat *fmt= NULL;
	char* s = NULL;
	if (!PyArg_ParseTuple(args, "s:", &s )) 
		return NULL;

	// Have extension and match the codec first
	for( fmt= first_oformat; fmt != NULL; fmt = fmt->next) 
		if ( (fmt->extensions) && (strstr( fmt->extensions, s )))
		{
			// Create new decoder
			PyMuxerObject* muxer= (PyMuxerObject*)type->tp_alloc(type, 0);
			if( !muxer )
				return NULL;
			
			memset (&muxer->oc,0,sizeof(AVFormatContext));
			muxer->oc.oformat = fmt;

			if (av_set_parameters(&muxer->oc, NULL) < 0) 
			{
				PyErr_Format(g_cErr,  "Invalid output format parameters");
				return NULL;
			}

			muxer->bStarted= 0;
			return (PyObject*)muxer;
		}

	PyErr_Format(g_cErr, "No registered muxer for the '%s' extension", s );
	return NULL;
}

// ----------------------------------------------------------------
static void MuxerClose( PyMuxerObject *obj )
{
	if (obj->oc.pb.buffer)
		av_free(obj->oc.pb.buffer);

	memset(&obj->oc.pb,0,sizeof(ByteIOContext));

	PyObject_Free( (PyObject*)obj );
}

// ----------------------------------------------------------------

static PyObject * MuxerGetAttr( PyMuxerObject *muxer, char *name)
{
	return Py_FindMethod( muxer_methods, (PyObject *)muxer, name);
}

// ----------------------------------------------------------------
PyTypeObject MuxerType =
{
	PyObject_HEAD_INIT(NULL)
	0,
	"pymedia.video.muxer.Muxer",
	sizeof(PyMuxerObject),
	0,
	(destructor)MuxerClose,  //tp_dealloc
	0,			  //tp_print
	(getattrfunc)MuxerGetAttr, //tp_getattr
	0,			  //tp_setattr
	0,			  //tp_compare
	0,			  //tp_repr
	0,			  //tp_as_number
	0,			  //tp_as_sequence
	0,				//tp_as_mapping
	0,					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	0,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	(char*)MUXER_DOC,		/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	muxer_methods,				/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	MuxerNew,	/* tp_new */
	PyObject_Del,				/* tp_free */
};

/*

########################################################################3
# Simple  audio encoder 
def recodeAudio( fName, fOutput, type, bitrate= None ):
  # ------------------------------------
  import pymedia.audio.acodec as acodec
  import pymedia.muxer as muxer
  # Open demuxer
  dm= muxer.Demuxer( fName.split( '.' )[ -1 ].lower() )
  f= open( fName, 'rb' )
  s= f.read( 90000 )
  dec= enc= mx= None
  print 'Recoding %s into %s' % ( fName, fOutput )
  while len( s ):
    frames= dm.parse( s )
    if frames:
      for fr in frames:
        # Assume for now only audio streams
        if dec== None:
          # Open decoder
          dec= acodec.Decoder( dm.streams[ fr[ 0 ] ] )
          print 'Decoder params:', dm.streams[ fr[ 0 ] ]
        
        # Decode audio frame
        r= dec.decode( fr[ 1 ] )
        if r:
          if bitrate== None:
            bitrate= r.bitrate
          
          # Open muxer and encoder
          if enc== None:
            params= { 'id': acodec.getCodecID(type),
                      'bitrate': bitrate,
                      'sample_rate': r.sample_rate,
                      'channels': r.channels }
            print 'Encoder params:', params
            mx= muxer.Muxer( type )
            stId= mx.addStream( muxer.CODEC_TYPE_AUDIO, params )
            enc= acodec.Encoder( params )
            fw= open(fOutput, 'wb')
            mx.setInfo( { 'album': 'Test album', 'artist': 'Test artist', 'title': 'Test title', 'track': '23', 'year': '1985', 'comment': 'No words, just cool', 'copyright': 'Free for everyone' } )
            ss= mx.start()
            fw.write(ss)
        
          enc_frames= enc.encode( r.data )
          if enc_frames:
            for efr in enc_frames:
              ss= mx.write( stId, efr )
              if ss:
                fw.write(ss)
    
    s= f.read( 100000 )
  
  f.close()
  
  if fw:
    if mx:
      ss= mx.end()
      if ss:
        fw.write(ss)
    fw.close()

recodeAudio( 'c:\\bors\\media\\test.ac3', 'test.ogg', 'ogg' )

	*/