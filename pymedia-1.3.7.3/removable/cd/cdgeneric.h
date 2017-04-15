/*
 *			Class to support direct cdda extraction from any valid device
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

#ifndef CD_GENERIC_H
#define CD_GENERIC_H

#define DATA_CD_TYPE "Data"

// ---------------------------------------------------------------
class GenericCD
{
public:
  GenericCD( char* sName ){};
  virtual ~GenericCD(){};
  virtual bool RefreshTitles()= 0;
  virtual void* OpenTitle( char* s )= 0;
  virtual char* GetType()= 0;
};

// ---------------------------------------------------------------
class DataCDRead : public GenericCD
{
private:
  char sDevName[ 255 ];				// Device name
  char sOrigName[ 255 ];				// Device name
public:
	// ---------------------------------------------------
	~DataCDRead()
	{
	}

	// ---------------------------------------------------
	DataCDRead( char* sDevName ) : GenericCD( sDevName )
	{
		strcpy( this->sOrigName, sDevName );
	}
	// ---------------------------------------------------
  bool RefreshTitles()
	{
		return true;
	}
	// ---------------------------------------------------
  char* GetType()
	{
		return DATA_CD_TYPE;
	}
	/* ---------------------------------------------------------------------------------*/
	void*
	OpenTitle( char* sPath)
	{
		return NULL;
	}

};

// ---------------------------------------------------------------
typedef struct 
{
	PyObject_HEAD
	GenericCD* cObject;
} PyGenericCDObject;

// ---------------------------------------------------------------
typedef struct 
{
	PyObject_HEAD
	GenericCD* cObject;
	void* pData;
} PyTrackObject;


#endif /* CDDA_GENERIC_H */
