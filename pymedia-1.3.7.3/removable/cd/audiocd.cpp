
/*
 *			Python wrapper for the direct cdda extraction class.
 *			Also provides with some basic functionality to identify all
 *			CD/DVD ROM devices in a system plus some helpful information
 *			about them.
 *
 *						Copyright (C) 2002-2003  Dmitry Borisov
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


#include "Python.h"
#include "cdda.h"

#if !defined( WIN32 )
int GetLastError()
{
	return errno;
}
#endif

extern PyObject *g_cErr;

// ---------------------------------------------------------------------------------
PyObject *
AudioCD_GetTOC( CDDARead *cdda )
{
	// Refresh drive information
	int i;
  Py_BEGIN_ALLOW_THREADS
	i= cdda->RefreshTOC( true );
  Py_END_ALLOW_THREADS
	if( i == -1 )
	{
		// Raise an exception when something wrong with TOC
		PyErr_Format(g_cErr, "cdda read toc encountered error: Code %d .", GetLastError() );
		return NULL; 
	}

	// Create tuple with certain number of entries
	PyObject* cRes= PyTuple_New( cdda->GetTracksCount() );
	for( i= 0; i< (int)cdda->GetTracksCount(); i++ )
	{
		// Create tuple for the 2 items( from and to )
		PyObject * cSubRes= PyTuple_New( 2 );
		int iStart= cdda->GetStartSector( i );
		PyTuple_SetItem( cSubRes, 0, PyLong_FromLong( iStart ) );
		PyTuple_SetItem( cSubRes, 1, PyLong_FromLong( cdda->GetStartSector( i+ 1 )- iStart ) );
		PyTuple_SetItem( cRes, i, cSubRes );
	}
	return cRes;
}


/* ---------------------------------------------------------------------------------*/
static PyObject *
AudioCD_Track_Read( PyTrackObject *track, PyObject *args)
{
	PyObject *cRes;
	int iSize, iFrameFrom, iFrameTo, iStrLen, iOffs, i;
	CDDA_TRACK_INFO* stTrack;
	CDDARead* cTrack;
	void *pBuf= NULL;
	if (!PyArg_ParseTuple(args, "i:read", &iSize ))
		return NULL;

	stTrack= (CDDA_TRACK_INFO*)track->pData;
	cTrack= (CDDARead*)track->cObject;

	iFrameFrom= stTrack->iStartFrame+ ( stTrack->iPos/ cTrack->GetSectorSize() );
	iFrameTo= stTrack->iStartFrame+ ( ( stTrack->iPos+ iSize )/ cTrack->GetSectorSize() )+ 1;
	if( iFrameTo> stTrack->iEndFrame )
		iFrameTo= stTrack->iEndFrame;

	/* Calculate the real length being read */
	iStrLen= ( iFrameTo- iFrameFrom )* cTrack->GetSectorSize();
	iOffs= stTrack->iPos- ( iFrameFrom- stTrack->iStartFrame )* cTrack->GetSectorSize();
	if( iOffs+ iSize> iStrLen )
		iSize= iStrLen- iOffs;
	if( !iSize )
		return PyString_FromStringAndSize( NULL, 0 );

	/* Create buffer to store the data of expected size */
	pBuf= malloc( iStrLen );
	if( !pBuf )
	{
		char s[ 255 ];
		sprintf( s, "Cannot allocate %d bytes of memory for cdda frames. Code %d .", iStrLen, GetLastError() );
		PyErr_SetString( g_cErr, s );
		return NULL; 
	}

	/* Read needed frames into the string */
  Py_BEGIN_ALLOW_THREADS
	i= cTrack->ReadSectors( iFrameFrom, iFrameTo, (char*)pBuf, iStrLen );
  Py_END_ALLOW_THREADS
	if( i== -1 )
	{
		/* Raise an exception when the sampling rate is not supported by the module */
		char s[ 255 ];
		sprintf( s, "cdda read encountered error: Code %d .", GetLastError() );
		PyErr_SetString(g_cErr, s );
		free( pBuf );
		return NULL; 
	}

	cRes= PyString_FromStringAndSize( NULL, iSize );
	if( !cRes )
		return NULL;

	memcpy( PyString_AsString( cRes ), (char*)pBuf+ iOffs, iSize );
	stTrack->iPos+= iSize;
	free( pBuf );
	return cRes;
}

/* ---------------------------------------------------------------------------------*/
static PyObject *
AudioCD_Track_Close( PyTrackObject *track )
{
	CDDA_TRACK_INFO* stTrack= (CDDA_TRACK_INFO*)track->pData;
	stTrack->iPos= -1;
	Py_INCREF( Py_None );
	return Py_None;
}

/* ---------------------------------------------------------------------------------*/
static PyObject *
AudioCD_Track_Seek( PyTrackObject *track, PyObject *args)
{
	int iOffset, iOrigin, iSize, iTmp;
	if (!PyArg_ParseTuple(args, "ii:seek", &iOffset, &iOrigin ))
		return NULL;

	CDDA_TRACK_INFO* stTrack= (CDDA_TRACK_INFO*)track->pData;
	CDDARead* cTrack= (CDDARead*)track->cObject;

	if( stTrack->iPos< 0 )
	{
		PyErr_SetString(g_cErr, "Cannot seek through the closed file." );
		return NULL; 
	}

	iTmp= stTrack->iPos;
	iSize= cTrack->GetSectorSize()* ( stTrack->iEndFrame- stTrack->iStartFrame );
	switch( iOrigin )
	{
		case SEEK_CUR: 
			iTmp+= iOffset;
			break;
		case SEEK_END:
			iTmp= iSize+ iOffset;
			break;
		case SEEK_SET:
			iTmp= iOffset;
			break;
	};

	if( iTmp< 0 || stTrack->iPos> iSize )
	{
		PyErr_SetString(g_cErr, "Cannot seek beyond the file bounds" );
		return NULL; 
	}

	stTrack->iPos= iTmp;
	Py_INCREF( Py_None );
	return Py_None;
}

/* ---------------------------------------------------------------------------------*/
static PyObject *
AudioCD_Track_Tell( PyTrackObject *track )
{
	CDDA_TRACK_INFO* stTrack= (CDDA_TRACK_INFO*)track->pData;
	if( stTrack->iPos< 0 )
	{
		PyErr_SetString(g_cErr, "Cannot tell the position on a closed file." );
		return NULL; 
	}
	return PyLong_FromLong( stTrack->iPos );
}

/* ---------------------------------------------------------------------------------*/
static PyObject *
AudioCD_Track_Properties( PyTrackObject *track )
{
	CDDA_TRACK_INFO* stTrack= (CDDA_TRACK_INFO*)track->pData;
	CDDARead* cTrack= (CDDARead*)track->cObject;
	PyObject *cRes= PyDict_New();
	if( !cRes )
		return PyErr_NoMemory();

	// Set some data in the dictionary
	PyDict_SetItemString( cRes, LENGTH, PyFloat_FromDouble( cTrack->GetTrackLength( stTrack->iTrack )) );

	// Some data hardcoded for now...
	PyDict_SetItemString( cRes, "sample_rate", PyLong_FromLong( 16 ) );
	PyDict_SetItemString( cRes, "sample_freq", PyLong_FromLong( 44100 ) );
	PyDict_SetItemString( cRes, "channels", PyLong_FromLong( 2 ) );
	PyDict_SetItemString( cRes, "bitrate", PyLong_FromLong( 999 ) );
	return cRes;
}

/* ---------------------------------------------------------------------------------*/
// File like object to support contiques read from a track
static PyMethodDef audiocd_track_methods[] = 
{
	{ 
		"read", 
		(PyCFunction)AudioCD_Track_Read, 
		METH_VARARGS,
	  "read( size ) -> data\n"
		"reads data from the track in a file like fashion"
	},
	{ 
		"close", 
		(PyCFunction)AudioCD_Track_Close, 
		METH_NOARGS,
	  "close() -> None\n"
		"Close currently open file. If already closed, does nothing"
	},
	{ 
		"seek", 
		(PyCFunction)AudioCD_Track_Seek, 
		METH_VARARGS,
	  "seek( pos, origin ) -> None\n"
		"Seek track into the certain position"
	},
	{ 
		"tell", 
		(PyCFunction)AudioCD_Track_Tell, 
		METH_NOARGS,
	  "tell() -> pos\n"
		"Returns current position in a file"
	},
	{ 
		"getProperties", 
		(PyCFunction)AudioCD_Track_Properties, 
		METH_NOARGS,
	  "getProperties() -> { properties }\n"
		"Returns properties for the title as dictionary.\n"
		"The following available for AudioCD:\n"
		"\t'"LENGTH"' length of the track in secs\n"
		"\t'sample_rate' sample rate of the track in bits( 16, 24 )\n"
		"\t'sample_freq' sample frequency rate in Hz ( ex 44100, 48000, 96000 )\n"
		"\t'channels' number of channels on a track( ex 1, 2 )\n"
	},
	{ NULL, NULL },
};


/* ----------------------------------------------------------------*/
static void
AudioCD_TrackFree( PyTrackObject *track )
{
	if( track->pData )
		free( track->pData );
	PyObject_Free( track ); 
}

/* ---------------------------------------------------------------- */
static PyObject *
AudioCD_TrackGetAttr( PyTrackObject *track, char *name)
{
	return Py_FindMethod( audiocd_track_methods, (PyObject *)track, name);
}

/* ----------------------------------------------------------------*/
PyTypeObject PyAudioCDTrackType = 
{
	PyObject_HEAD_INIT(NULL)
	0,
	"AudioTrack",
	sizeof(PyTrackObject),
	0,
	(destructor)AudioCD_TrackFree,  /*tp_dealloc*/
	0,			  //tp_print
	(getattrfunc)AudioCD_TrackGetAttr, /*tp_getattr*/
	0,			  //tp_setattr
	0,			  //tp_compare
	0,			  //tp_repr
	0,			  //tp_as_number
	0,			  //tp_as_sequence
	0,				//tp_as_mapping
};
