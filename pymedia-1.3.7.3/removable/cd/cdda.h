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

#if !defined( _CDDA_H )
#define _CDDA_H

#define AUDIO_CD_TYPE "AudioCD"
#define LENGTH "length"

typedef struct
{
	int iPos;
	int iStartFrame;
	int iEndFrame;
	int iTrack;
} CDDA_TRACK_INFO;


#if defined( WIN32 ) || defined( SYS_CYGWIN )
#include "cdda_win.h"
#else
#include "cdda_unix.h"
#endif

typedef struct
{
	PyObject_HEAD
	CDDARead* cObject;
} PyCDDAObject;

PyObject *AudioCD_GetTOC( CDDARead *cdda );

#endif /* _CDDA_H */
