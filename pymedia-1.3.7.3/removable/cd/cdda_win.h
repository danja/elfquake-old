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

#ifndef CDDA_READ_H
#define CDDA_READ_H

#include <windows.h>
#include <winioctl.h>  // From the Win32 SDK \Mstools\Include
#include "ntddcdrm.h"  // From the Windows NT DDK \Ddk\Src\Storage\Inc

#include "cdgeneric.h"  // Generic interface

const int SECTORS_PER_CHUNK= 40;

class CDDARead : public GenericCD
{
private:
  char sDevName[ 255 ];				// Device name
  char sOrigName[ 255 ];				// Device name
	CDROM_TOC stTracks;	// MAXIMUM_NUMBER_TRACKS tracks should be enough for the audio disk.
	DISK_GEOMETRY dgCDROM;
	char sErr[ 1024 ];
public:
	// ---------------------------------------------------
	~CDDARead()
	{
	}

	// ---------------------------------------------------
	CDDARead( char* sDevName ) : GenericCD( sDevName )
	{
		// Open the file
		SetLastError(0);
		sprintf( this->sDevName, "\\\\.\\%s", sDevName );
		if( this->sDevName[ strlen( this->sDevName )- 1 ]== '\\' )
			this->sDevName[ strlen( this->sDevName )- 1 ]= 0;

		strcpy( this->sOrigName, sDevName );
	}
	// ---------------------------------------------------
  bool RefreshTitles()
	{
		if( this->RefreshTOC( true )== -1 )
			return false;

		// See if there is no lead out==
		return ( this->GetTracksCount()> 2 );
	}
	// ---------------------------------------------------
  char* GetType()
	{
		return AUDIO_CD_TYPE;
	}
	// ---------------------------------------------------
	inline char* GetLastError()	{ return this->sErr; }
	// ---------------------------------------------------
	inline unsigned int GetTracksCount()	{ return this->stTracks.LastTrack- this->stTracks.FirstTrack+ 1; }
	// ---------------------------------------------------
	inline float GetTrackLength( int i )
	{
		return (float)(this->GetStartSector( i+ 1 )- this->GetStartSector( i )) / 75;
	}
	// ---------------------------------------------------
	inline unsigned int GetStartSector( int iTrackNum )
	{
		unsigned char* pAddr= &this->stTracks.TrackData[ iTrackNum ].Address[ 0 ];
		// I don't know why MS returns everything 2 secs shifted( just substract 2 secs then )
		return pAddr[1]*60*75+( pAddr[2]- 2) *75+pAddr[3];
	}
	// ---------------------------------------------------
	inline unsigned int GetSectorSize()	{ return 2352;  } //this->dgCDROM.BytesPerSector; }
	// ---------------------------------------------------
	int RefreshTOC( bool bReadToc )
	{
		// Reread the dist to see if something has changed
		DWORD dwNotUsed;
		HANDLE  hCD= CreateFile(
			this->sDevName,
			GENERIC_READ,
			FILE_SHARE_READ,
      NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
      NULL);

		if( hCD )
		{
			if( !DeviceIoControl (hCD, IOCTL_CDROM_GET_DRIVE_GEOMETRY,
                           NULL, 0, &this->dgCDROM, sizeof(this->dgCDROM),
                           &dwNotUsed, NULL))
			{
				CloseHandle( hCD );
				return -1;
			}

			// Reread toc to be sure we have recent info
			if( bReadToc )
			{
				// Read the TOC
				memset(&this->stTracks, 0x00,sizeof( this->stTracks ));
				DWORD dwBytesRead;
				BOOL bRet= DeviceIoControl(
					hCD, IOCTL_CDROM_READ_TOC, NULL, 0, &this->stTracks, sizeof( this->stTracks ), &dwBytesRead, NULL );

				CloseHandle( hCD );
				return bRet ? 0: -1;
			}
			CloseHandle( hCD );
			return 0;
		}
		return -1;
	}
	// ---------------------------------------------------
	int ReadSectors( int iSectorFrom, int iSectorTo, char* sBuf, unsigned int iLen )
	{
		// Validate we have enough buffer to read all the data
		if( iLen< ( iSectorTo- iSectorFrom )* this->GetSectorSize() )
			return -1;

		HANDLE  hCD= CreateFile(
			this->sDevName,
			GENERIC_READ,
			FILE_SHARE_READ,
      NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
      NULL);

		// Read all sectors through the internal buffer in chunks
		for( int i= iSectorFrom; i< iSectorTo && hCD; i+= SECTORS_PER_CHUNK )
		{
			int iTo= ( i+ SECTORS_PER_CHUNK > iSectorTo ) ? iSectorTo: i+ SECTORS_PER_CHUNK;
			DWORD dwBytesRead= 0;

			// Prepare RAW_READ structure
			RAW_READ_INFO stRawInfo;
			memset( &stRawInfo, 0, sizeof( stRawInfo ) );
			stRawInfo.DiskOffset.QuadPart= i * this->dgCDROM.BytesPerSector;
			stRawInfo.TrackMode= CDDA;
  		stRawInfo.SectorCount= iTo- i;

			// Read the data into the internal buffer
		  BOOL bRet= DeviceIoControl(
				hCD, IOCTL_CDROM_RAW_READ, &stRawInfo, sizeof(stRawInfo), sBuf, iLen, &dwBytesRead, NULL );

			// If we read less data than we should, raise an error
			if( !bRet || dwBytesRead!= this->GetSectorSize()* stRawInfo.SectorCount )
			{
				CloseHandle( hCD );
				return -1;
			}

			// Copy data into the buffer provided
			sBuf+= dwBytesRead;
			iLen-= dwBytesRead;
		}

		if( hCD )
			CloseHandle( hCD );

		return 0;
	}

	/* ---------------------------------------------------------------------------------*/
	void*
	OpenTitle( char* sPath)
	{
		// Get track number from the path( path is like 'Track 01' or '/dev/cdrom/Track 01' or 'd:\\Track 01' )
		CDDA_TRACK_INFO * stInfo;
		int iTrack= -1;
		char* sTrack= strstr( sPath, "Track " );
		// See if sPath not a relative path, then validate a path
		if( sTrack && sTrack!= sPath )
			if( strncmp( sPath, this->sOrigName, strlen( this->sOrigName )))
				sTrack= NULL;
			else if( sTrack- sPath!= (signed)strlen( this->sOrigName ) )
				sTrack= NULL;

		if( sTrack && strlen( sTrack )!= 8 )
			sTrack= NULL;

		if( !sTrack )
		{
			sprintf( this->sErr, "Path %s is not a valid track name. Name should be like '%s/Track 01'. Cannot open.", sPath, this->sOrigName );
			return NULL;
		}

		// Refresh toc for correct sector numbers
		if( this->RefreshTOC( true )== -1 )
		{
			// Raise an exception when the sampling rate is not supported by the module
			sprintf( this->sErr, "cdda read toc encountered error: Code %d .", GetLastError() );
			return NULL;
		}

		iTrack= ( sTrack[ 6 ]- '0' )* 10+ sTrack[ 7 ]- '0';
		if( iTrack< 1 || iTrack> (int)this->GetTracksCount() )
		{
			sprintf( this->sErr, "Track number( %d ) is out of range( 1, %d ). Cannot open.", iTrack, this->GetTracksCount() );
			return NULL;
		}

		stInfo= (CDDA_TRACK_INFO*)malloc( sizeof( CDDA_TRACK_INFO ) );
		if( !stInfo )
		{
			sprintf( this->sErr, "No memory available. Error code is %d.", GetLastError() );
			return NULL;
		}

		stInfo->iPos= 0;
		stInfo->iStartFrame= this->GetStartSector( iTrack- 1 );
		stInfo->iEndFrame= this->GetStartSector( iTrack );
		stInfo->iTrack= iTrack- 1;
		return stInfo;
	}


};

#endif /*CDDA_READ_H*/
