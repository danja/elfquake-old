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

#ifndef CD_COMMON_H
#define CD_COMMON_H

#include <windows.h>
#include <winioctl.h>  // From the Win32 SDK \Mstools\Include
#include "ntddcdrm.h"  // From the Windows NT DDK \Ddk\Src\Storage\Inc

// ---------------------------------------------------
int GetDrivesList( char sDrives[ 255 ][ 20 ] )
{
	DWORD iDrivesMask= GetLogicalDrives();
	char chDrive= 'A';
	int iDrives= 0;
	while( iDrivesMask )
	{
		char s[ 20 ];
		sprintf(s, "%c:\\", chDrive );
		if( ( iDrivesMask & 1 ) && GetDriveType( s ) == DRIVE_CDROM )
		{
			strcpy( &sDrives[ iDrives ][ 0 ], s );
			iDrives++;
		}
		iDrivesMask>>= 1;
		chDrive++;
	}
	return iDrives;
}

/***************************************************************************************
* Generic CD wrapper stuff
***************************************************************************************/

class CD
{
private:
  char sDevName[ 255 ];				// Device name
  char sOrigName[ 255 ];				// Device name
  int iErr;

	// ---------------------------------------------------
	HANDLE GetDevice()
	{
		return CreateFile(
			this->sDevName,
			GENERIC_READ,
			FILE_SHARE_READ,
      NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
      NULL);
	}
public:
	// ---------------------------------------------------
	CD( char* sDevName )
	{
		// Open the file
		SetLastError(0);
		sprintf( this->sDevName, "\\\\.\\%s", sDevName );
		if( this->sDevName[ strlen( this->sDevName )- 1 ]== '\\' )
			this->sDevName[ strlen( this->sDevName )- 1 ]= 0;

		strcpy( this->sOrigName, sDevName );
	}
	// ---------------------------------------------------
	inline char* GetName()	{ return &this->sOrigName[ 0 ]; }
	// ---------------------------------------------------
	inline char* GetPathName()	{ return &this->sOrigName[ 0 ]; }
	// ---------------------------------------------------
	void GetLabel( char* sLabel, int iLabelLen )
	{
		DWORD lVolumeSerialNumber,
					lMaximumComponentLength,
					lFileSystemFlags;
		char sBuf[ 255 ];
		if( !GetVolumeInformation(
			this->sOrigName, sLabel, iLabelLen, &lVolumeSerialNumber, &lMaximumComponentLength,
			&lFileSystemFlags, &sBuf[ 0 ], sizeof( sBuf ) ))
		{
			this->iErr= GetLastError();
			strcpy( sLabel, "No volume" );
		}
	}
	// ---------------------------------------------------
	bool IsReady()
	{
		// Try to get some geometry information
		// If success, then drive is ready
		bool bReady= false;
		HANDLE hCD= this->GetDevice();
		if( hCD )
		{
			DISK_GEOMETRY stGeometries[ 20 ];
			DWORD iReturned;
			bReady= ( DeviceIoControl(
				hCD, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, &stGeometries, sizeof( stGeometries ), &iReturned, NULL )!= 0);
			CloseHandle( hCD );
		}

		return bReady;
	}
	// ---------------------------------------------------
	int Eject()
	{
		// Eject if possible
		DWORD dwNotUsed, dwRet= 0;
		HANDLE  hCD= this->GetDevice();
		if( hCD )
		{
			dwRet= DeviceIoControl (hCD, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &dwNotUsed, NULL);
			CloseHandle( hCD );
		}

		return dwRet ? 0: -1;
	}
};

#endif /* CDDA_COMMON_H */
