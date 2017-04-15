/*
 *			Python wrapper for the removable media extraction class.
 *			Provides some basic functionality to identify all
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


#include <Python.h>
 
#include "cdgeneric.h"
#include "cdda.h"
#include "dvd.h"

#if defined( WIN32 ) || defined( SYS_CYGWIN )
#include "cdcommon_win.h"
#else
#include "cdcommon_unix.h"
#endif

#define MODULE_NAME "pymedia.removable.cd"

#define INIT_DOC "init() -> \n\tinitialize cdrom subsystem\n"
#define QUIT_DOC "quit() -> \n\tquit cdrom subsystem\n"
#define GETCOUNT_DOC "getCount() -> ( count ) \n\treturn number of cdroms in a system\n"

#ifndef BUILD_NUM
#define BUILD_NUM 1
#endif

const int PYCDBUILD= BUILD_NUM;
const char* PYCDVERSION= "2";
const char* PYDOC=
"CD direct CD/DVD ROM access.\nProvides the following functionality:\n"
"- Identify all drives in a system\n"
"- Direct access in file based mode to these drives per track\n"
"The following methods available from the module directly:\n"
"\t"INIT_DOC
"\t"QUIT_DOC
"\t"GETCOUNT_DOC
"To access content of each individual drive in the system, use cd.CD( index )\n";

#define TYPE_KEY "type"
#define TITLES_KEY "titles"
#define LENGTH_KEY "length"
#define NAME_KEY "name"
#define LABEL_KEY "label"

#define CD_ISREADY_HDR "isReady() -> ready\n"
#define CD_EJECT_HDR "eject() -> None\n"
#define CD_OPEN_HDR "open( name ) -> File\n"
#define CD_GETNAME_HDR "getName() -> name\n"
#define CD_GETPATHNAME_HDR "getPathName() -> path\n"
#define CD_GETPROPERTIES_HDR "getProperties() -> props\n"

#define CD_ISREADY_DOC CD_ISREADY_HDR"\tReturns whether drive is ready or not\n"
#define CD_EJECT_DOC CD_EJECT_HDR"\tEject tray\n"
#define CD_OPEN_DOC CD_OPEN_HDR"\tReturn file like object mapped to given track.\n"\
										"\tTrack supports following methods:\n\t\tread( bytes ) -> data\n\t\tseek( offset, seek_pos ) -> None\n"\
										"\t\tclose() -> None\n\t\ttell() -> pos\n"
#define CD_GETNAME_DOC CD_GETNAME_HDR"\tReturn the name of the drive\n"
#define CD_GETPATHNAME_DOC CD_GETPATHNAME_HDR"\tReturn the path where the drive was mounted if any\n"
#define CD_GETPROPERTIES_DOC \
	CD_GETPROPERTIES_HDR \
	"\tReturn properties for a selected ROM as a dictionary" \
	"\n\t'"LABEL_KEY"' is always set and represent the media volume label"\
	"\n\t'"TYPE_KEY"' is always set and can be ( { 'DVD' | 'AudioCD' | 'VCD' | 'Data' } )"\
	"\n\tFor DVD it would be:"\
	"\n\t\t'"TITLES_KEY"' list of titles on DVD."\
	"\n\t\tOnce title is opened through the open() call, "\
	"\n\t\tyou may request additional propertirs by calling "CD_GETPROPERTIES_HDR \
	"\n\t\tProperies will be returned as a dictinary with the following members:"\
	"\n\t\t\t'"LENGTH"' length of the title"\
	"\n\t\t\t'"VIDEO_STREAMS"' list of video streams as dictionaries"\
	"\n\t\t\t'"AUDIO_STREAMS"' list of audio streams as dictionaries"\
	"\n\t\t\t'"CHAPTERS"' list of absolute positions for chapters( to be used in seek )"\
	"\n\t\t\t'"SEEKS"' list of pairs( time_sec, seek_pos )"\
	"\n\t\t\t'"LANGUAGES"' list of languages for a title as dictionaries"\
	"\n\tFor AudioCD it would be:"\
	"\n\t\t'toc' TOC of a disk"\
	"\n\t\t'"TITLES_KEY"' list of title names on a disk( matches TOC )"\
	"\n\t\tOnce title is opened through the open() call, "\
	"\n\t\tyou may request additional propertirs by calling "CD_GETPROPERTIES_HDR \
	"\n\t\tProperies will be returned as a dictinary with the following members:"\
	"\n\t\t\t'"LENGTH"' length of the title"\
	"\n\tFor VideoCD it would be:"\
	"\n\t'"TITLES_KEY"' -> list of file names for a movie"

#define CD_GETTOC_DOC "getTOC() -> ( ( frameStart, frameEnd )* )\n" \
											"\tReturn toc for the drive. If the drive has no audio, returns list of sessions( if any )\n"

#define CD_DOC "CD allow you to query information about media type and read data in a raw mode\n"\
								"Once created, the following methods are available:\n"\
								"\t"CD_ISREADY_HDR \
								"\t"CD_EJECT_HDR \
								"\t"CD_OPEN_HDR \
								"\t"CD_GETPATHNAME_HDR \
								"\t"CD_GETNAME_HDR \
								"\t"CD_GETPROPERTIES_HDR

PyObject *g_cErr;
PyObject *g_d;

extern int GetDrivesList( char* sDrives[ 255 ][ 20 ] );
extern PyTypeObject PyAudioCDTrackType;

int g_iMaxCDROMIndex;
char g_sDrives[ 255 ][ 20 ];

// ---------------------------------------------------------------
typedef struct
{
	PyObject_HEAD
	CD* cObject;
	//GenericCD *cMediaHandle;
} PyCDObject;

// ---------------------------------------------------------------------------------
PyTypeObject *
GetPythonType( GenericCD *cd)
{
	if( !strcmp( cd->GetType(), AUDIO_CD_TYPE ))
		return &PyAudioCDTrackType;
/*	if( !strcmp( cd->GetType(), "VideoCD" ))
		return &PyVideoCDTrackType;
*/
	if( !strcmp( cd->GetType(), DVD_CD_TYPE ))
		return &PyDVDCDTrackType;

	return NULL;
}

// ---------------------------------------------------------------------------------
GenericCD *
GetMediaHandle( PyCDObject *cd)
{
	GenericCD *cd1= NULL;
	bool bReady;
  Py_BEGIN_ALLOW_THREADS
	bReady= cd->cObject->IsReady();
  Py_END_ALLOW_THREADS
	if( !bReady )
	{
		return NULL;
	}

	//if( cd->cMediaHandle )
	//	return cd->cMediaHandle;
	for( int i= 0; i< 3; i++ )
	{
		switch( i )
		{
			case 0:
				cd1= (GenericCD*)( new CDDARead( cd->cObject->GetName() ) );
				break;
			case 1:
				cd1= (GenericCD*)( new DVDRead( cd->cObject->GetName() ));
				break;
			case 2:
				cd1= (GenericCD*)( new DataCDRead( cd->cObject->GetName() ));
				break;
		}
		if( !cd1 )
		{
			PyErr_NoMemory();
			return NULL;
		}
		Py_BEGIN_ALLOW_THREADS
		bReady= cd1->RefreshTitles();
		Py_END_ALLOW_THREADS

		if( bReady )
			return cd1;

		delete cd1;
	}

	return NULL;
}

// ---------------------------------------------------------------------------------
static PyObject *
CD_GetProperties( PyCDObject *cd )
{
	PyObject *cRes= PyDict_New();
	if( !cRes )
		return NULL;

	// We hardcode properties here
	GenericCD* cCD= GetMediaHandle( cd );
	if( !cCD )
	{
		// Disk is not ready or not VCD, DVD, AudioCD
		PyErr_Format( g_cErr, "Device %s is not ready", cd->cObject->GetName() );
		return NULL;
	}

	PyObject* cTmp= PyString_FromString( cCD->GetType() );
	PyDict_SetItemString( cRes, TYPE_KEY, cTmp );
	Py_DECREF( cTmp );
	if( !strcmp( cCD->GetType(), AUDIO_CD_TYPE ))
	{
		// Process AudioCD
		cTmp= AudioCD_GetTOC( (CDDARead*)cCD );
		PyDict_SetItemString( cRes, "TOC", cTmp );
		Py_DECREF( cTmp );

		// Create names for tracks on the device
		PyObject* cTitles= PyTuple_New( ((CDDARead*)cCD)->GetTracksCount() );
		if( !cTitles )
			return NULL;

		for( unsigned int i= 0; i< ((CDDARead*)cCD)->GetTracksCount(); i++ )
		{
			// Get the title name
			cTmp= PyString_FromFormat( ( i>= 9 ) ? "Track %d": "Track 0%d", i+ 1 );
			PyTuple_SetItem( cTitles, i, cTmp );
		}
		PyDict_SetItemString( cRes, TITLES_KEY, cTitles );
		PyDict_SetItemString( cRes, LABEL_KEY, PyString_FromString( "Audio CD" ) );
		Py_DECREF( cTitles );
	}
	else if( !strcmp( cCD->GetType(), DVD_CD_TYPE ))
	{
		// Create names for titles on the device
		char sLabel[ 256 ];
		PyObject* cTitles= PyTuple_New( ((DVDRead*)cCD)->GetTitlesCount() );
		if( !cTitles )
			return NULL;

		for( int i= 0; i< ((DVDRead*)cCD)->GetTitlesCount(); i++ )
		{
			// Get the title name
			cTmp= PyString_FromFormat( ( i>= 9 ) ? DVD_TITLE"%d"DVD_EXT: DVD_TITLE0"%d"DVD_EXT, i+ 1 );
			PyTuple_SetItem( cTitles, i, cTmp );
		}
		PyDict_SetItemString( cRes, TITLES_KEY, cTitles );
	  Py_BEGIN_ALLOW_THREADS
		cd->cObject->GetLabel( &sLabel[ 0 ], sizeof( sLabel )  );
	  Py_END_ALLOW_THREADS
		PyDict_SetItemString( cRes, LABEL_KEY, PyString_FromString( sLabel ) );
		Py_DECREF( cTitles );
	}
	else if( !strcmp( cCD->GetType(), DATA_CD_TYPE ))
	{
		char sLabel[ 256 ];
	  Py_BEGIN_ALLOW_THREADS
		cd->cObject->GetLabel( &sLabel[ 0 ], sizeof( sLabel )  );
	  Py_END_ALLOW_THREADS
		PyDict_SetItemString( cRes, LABEL_KEY, PyString_FromString( sLabel ) );
	}
	delete cCD;
	return cRes;
}

// ---------------------------------------------------------------------------------
static PyObject *
CD_Eject( PyCDObject *cd )
{
	if( cd->cObject->Eject()== -1 )
	{
		// Raise an exception when eject was not successfull
		char s[ 255 ];
		sprintf( s, "cd eject encountered error: Code %d .", GetLastError() );
		PyErr_SetString(g_cErr, s );
		return NULL;
	}
	Py_INCREF( Py_None );
	return Py_None;
}

// ---------------------------------------------------------------------------------
static PyObject *
CD_IsReady( PyCDObject *cd )
{
	bool bReady;
  Py_BEGIN_ALLOW_THREADS
  bReady= cd->cObject->IsReady();
  Py_END_ALLOW_THREADS
	return PyLong_FromLong( bReady );
}

// ---------------------------------------------------------------------------------
static PyObject *
CD_GetName( PyCDObject *cd )
{
	return PyString_FromString( cd->cObject->GetName() );
}

// ---------------------------------------------------------------------------------
static PyObject *
CD_GetPathName( PyCDObject *cd )
{
	return PyString_FromString( cd->cObject->GetPathName() );
}

// ---------------------------------------------------------------------------------
static PyObject *
CD_Open( PyCDObject *cd, PyObject *args)
{
	/* Some validations first */
	char *sPath;
	void* pTrackData;
	if (!PyArg_ParseTuple(args, "s:open", &sPath ))
		return NULL;

	// Get media handle first
	GenericCD* cCD= GetMediaHandle( cd );
	if( !cCD )
	{
		PyErr_Format( g_cErr, "Media in %s is not DVD, AudioCD or VCD. Cannot open %s in a raw mode", cd->cObject->GetName(), sPath );
		return NULL;
	}
	//cd->cMediaHandle= cCD;

	// Try to open file by its name
	Py_BEGIN_ALLOW_THREADS
	pTrackData= cCD->OpenTitle( sPath );
	Py_END_ALLOW_THREADS
	if( !pTrackData )
	{
		PyErr_Format( g_cErr, "Title ( %s ) cannot be found on %s drive %s", sPath, cCD->GetType(), cd->cObject->GetName() );
		delete cCD;
		return NULL;
	}

	/* Create new file-like object based on a currently existing media type */
	PyTypeObject *cType= GetPythonType( cCD );
	PyTrackObject* track= PyObject_New( PyTrackObject, cType );
	if( !track )
		return NULL;

	track->cObject= cCD;
	track->pData= pTrackData;
	return (PyObject*)track;
}

// ---------------------------------------------------------------------------------
// List of all methods for the mp3decoder
static PyMethodDef cd_methods[] =
{
	{	"getProperties", (PyCFunction)CD_GetProperties, METH_NOARGS, CD_GETPROPERTIES_DOC },
	{	"isReady", (PyCFunction)CD_IsReady,	METH_NOARGS, CD_ISREADY_DOC	},
	{	"eject", (PyCFunction)CD_Eject,	METH_NOARGS, CD_EJECT_DOC },
	{	"open",	(PyCFunction)CD_Open,	METH_VARARGS, CD_OPEN_DOC },
	{	"getName",(PyCFunction)CD_GetName, METH_NOARGS, CD_GETNAME_DOC },
	{	"getPathName",(PyCFunction)CD_GetPathName, METH_NOARGS, CD_GETPATHNAME_DOC },
	{ NULL, NULL },
};

// ----------------------------------------------------------------
static void
CDClose( PyCDObject *cd )
{
	delete cd->cObject;
	PyObject_Free( cd );
}

// ---------------------------------------------------------------------------------
static PyObject *
CDNew( PyTypeObject *type, PyObject *args, PyObject *kwds )
{
	int iIndex;
	if (!PyArg_ParseTuple(args, "i:", &iIndex ))
		return NULL;

	// Whether init was run ?
	if( g_iMaxCDROMIndex== -1 )
	{
		PyErr_SetString(g_cErr, "cd module was not initialized, Please initialize it first." );
		return NULL;
	}

	if( iIndex< 0 || iIndex>= g_iMaxCDROMIndex )
	{
		// Raise an exception when the sampling rate is not supported by the module
		char s[ 255 ];
		sprintf( s, "No cdrom with index: %d in a system.", iIndex );
		PyErr_SetString(g_cErr, s );
		return NULL;
	}

	char* sDriveName= g_sDrives[ iIndex ];
	PyCDObject* cd= (PyCDObject*)type->tp_alloc(type, 0);
	if( !cd )
		return NULL;

	cd->cObject= new CD( sDriveName );
	if( !cd->cObject )
		return NULL;

	if( GetLastError()!= 0 )
	{
		// Raise an exception when cdrom cannot be opened
		char s[ 255 ];
		sprintf( s, "Cannot open CD drive. Error code is: %d", GetLastError() );
		PyErr_SetString(g_cErr, s );
		CDClose( cd );
		return NULL;
	}

	//cd->cMediaHandle= NULL;

	return (PyObject*)cd;
}

// ----------------------------------------------------------------
static PyTypeObject PyCDType =
{
	PyObject_HEAD_INIT(NULL)
	0,
	MODULE_NAME".CD",
	sizeof(PyCDObject),
	0,
	(destructor)CDClose,  //tp_dealloc
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
	(char*)CD_DOC,		/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	cd_methods,				/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	CDNew,	/* tp_new */
	PyObject_Del,				/* tp_free */
};

// ---------------------------------------------------------------------------------
static PyObject *
CDROMCount( PyObject* obj)
{
	// Whether init was run ?
	if( g_iMaxCDROMIndex== -1 )
	{
		PyErr_SetString(g_cErr, "cd module was not initialized, please initialize it first." );
		return NULL;
	}

	return PyInt_FromLong( g_iMaxCDROMIndex );
}

// ---------------------------------------------------------------------------------
static PyObject *
Quit( PyObject* obj)
{
	g_iMaxCDROMIndex= -1;

	Py_INCREF( Py_None );
	return Py_None;
}

// ---------------------------------------------------------------------------------
// Fake for now. Need to improve.
static PyObject *
Init( PyObject* obj)
{
	/* Scan the system for CD-ROM drives */
	g_iMaxCDROMIndex= GetDrivesList( g_sDrives );

	Py_INCREF( Py_None );
	return Py_None;
}

// ---------------------------------------------------------------------------------
// List of all methods for the mp3decoder
static PyMethodDef pycd_methods[] =
{
	{ "init",	(PyCFunction)Init, 	METH_NOARGS, INIT_DOC },
	{ "quit", (PyCFunction)Quit,  METH_NOARGS, QUIT_DOC },
	{ "getCount", (PyCFunction)CDROMCount, METH_NOARGS, GETCOUNT_DOC	},
	{ NULL, NULL },
};

extern "C"
{
// ---------------------------------------------------------------------------------
#define INT_CONSTANT(name) PyModule_AddIntConstant( m, #name, name )

// ---------------------------------------------------------------------------------
DL_EXPORT(void)
initcd(void)
{
	Py_Initialize();
	g_iMaxCDROMIndex= -1;
	PyObject *m = Py_InitModule("cd", pycd_methods);
	PyModule_AddStringConstant( m, "__doc__", (char*)PYDOC );
	PyModule_AddStringConstant( m, "version", (char*)PYCDVERSION );
	PyModule_AddIntConstant( m, "build", PYCDBUILD );

	INT_CONSTANT( SEEK_SET );
	INT_CONSTANT( SEEK_END );
	INT_CONSTANT( SEEK_CUR );
	g_cErr = PyErr_NewException(MODULE_NAME".CDError", NULL, NULL);
	if( g_cErr != NULL)
	  PyModule_AddObject(m, "CDError", g_cErr );

	PyCDType.ob_type = &PyType_Type;
	Py_INCREF((PyObject *)&PyCDType);
	PyModule_AddObject(m, "CD", (PyObject *)&PyCDType);
}

};

/*
import pymedia.removable.cd as cd
cd.init()
c= cd.CD(0)
c.isReady()
c.getProperties()
f= c.open( 'Title 01' )
p= f.getProperties()
s= ' '
f1= open( 'test.vob', 'wb' )
while len( s )> 0:
  s= f.read( 2000000 )
  f1.write( s )

f1.close()

toc= c.getTOC()
tr0= c.open( "d:\\Track 01" )
s= tr0.read( 400000 )

*/
