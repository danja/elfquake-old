
/*
 *			Class to support direct cdda extraction from any valid device( unix version )
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

#include <stdio.h>
#include <fcntl.h>
#include <linux/cdrom.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include "cdgeneric.h"  // Generic interface

const int SECTORS_PER_CHUNK= 40;
const int MAX_TRACKS= 2000;

struct CDROM_TOC
{
  int min;
  int max;
  int starts[ MAX_TRACKS ];
  char types[ MAX_TRACKS ];
};

// ---------------------------------------------------
extern int GetLastError();

// ---------------------------------------------------
class CDDARead : public GenericCD
{
private:
  char sDevName[ 255 ];				// Device name
  char sErr[ 1024 ];				// Last Error
	CDROM_TOC stTracks;	// MAXIMUM_NUMBER_TRACKS tracks should be enough for the audio disk. But who knows ?
	int  hCD;
public:
	// ---------------------------------------------------
	~CDDARead()
	{
		if( this->hCD )
			close( this->hCD );
	}
	// ---------------------------------------------------
	CDDARead( char* sDevName )  : GenericCD( sDevName )
	{
		strcpy( this->sDevName, sDevName );
		// Open the file 
		if (( this->hCD= open( sDevName, O_RDONLY | O_NONBLOCK ))==-1)
			return;

		this->RefreshTOC( false );
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
  char* GetType(){ return AUDIO_CD_TYPE;	}
	// ---------------------------------------------------
	inline char* GetLastError()	{ return this->sErr; }
	// ---------------------------------------------------
	inline unsigned int GetTracksCount()	{ return this->stTracks.max- this->stTracks.min+ 1; }
	// ---------------------------------------------------
	inline float GetTrackLength( int i )	
	{ 
		return (float)(this->GetStartSector( i+ 1 )- this->GetStartSector( i )) / 75;
	}
	// ---------------------------------------------------
	inline int GetStartSector( int iTrackNum )	
	{ 
		return this->stTracks.starts[ iTrackNum ];
	}
	// ---------------------------------------------------
	inline char* GetName()	{ return this->sDevName; }
	// ---------------------------------------------------
	inline int GetSectorSize()	{ return 2352;  } //this->dgCDROM.BytesPerSector; }
	// ---------------------------------------------------
	int GetTocEntry(int trk,struct cdrom_tocentry *Te,int mode)
	{
		Te->cdte_track=trk;
		Te->cdte_format=mode;
		return ioctl( this->hCD, CDROMREADTOCENTRY, Te );
	}

	// ---------------------------------------------------
	int RefreshTOC( bool bReadToc )
	{
		int i;
		struct cdrom_tochdr Th;
		struct cdrom_tocentry Te;

		if(ioctl( this->hCD, CDROMREADTOCHDR, &Th))
			return -1;

		this->stTracks.min= Th.cdth_trk0;
		this->stTracks.max= Th.cdth_trk1;

		for( i= this->stTracks.min; i<= this->stTracks.max; i++ )
		{
			if( this->GetTocEntry(i, &Te, CDROM_LBA ) )
				return -1;

			this->stTracks.starts[ i- this->stTracks.min ]= Te.cdte_addr.lba;
			this->stTracks.types[ i- this->stTracks.min ]= Te.cdte_ctrl & CDROM_DATA_TRACK;
		}
		i=CDROM_LEADOUT;
		if( this->GetTocEntry(i,&Te,CDROM_LBA))
			return -1;

		this->stTracks.starts[ this->GetTracksCount() ]= Te.cdte_addr.lba;
		this->stTracks.types[ this->GetTracksCount() ]= Te.cdte_ctrl & CDROM_DATA_TRACK;
		return 0;
	}

	// ---------------------------------------------------
	int ReadSectors( int iSectorFrom, int iSectorTo, char* sBuf, int iLen )
	{
		struct cdrom_read_audio ra;
		// Read all sectors through the external buffer in chunks
		for( int i= iSectorFrom; i< iSectorTo; i+= SECTORS_PER_CHUNK )
		{
			int iTo= ( i+ SECTORS_PER_CHUNK > iSectorTo ) ? iSectorTo: i+ SECTORS_PER_CHUNK;
			ra.addr.lba= i;
			ra.addr_format= CDROM_LBA;
			ra.nframes= iTo- i;
			ra.buf= (__u8 *)sBuf;
			if(ioctl( this->hCD, CDROMREADAUDIO, &ra ))
				return -1;

			// Promote buffer
			sBuf+= ra.nframes* this->GetSectorSize();
			iLen-= ra.nframes* this->GetSectorSize();
		}
		return 0;
	}
	/* ---------------------------------------------------------------------------------*/
	void*
	OpenTitle( char* sPath )
	{
		// Get track number from the path( path is like 'Track 01' or '/dev/cdrom/Track 01' or 'd:\\Track 01' )
		CDDA_TRACK_INFO * stInfo;
		int iTrack= -1;
		char* sTrack= strstr( sPath, "Track " );
		// See if sPath not a relative path, then validate a path
		if( sTrack && sTrack!= sPath )
			if( strncmp( sPath, this->sDevName, strlen( this->sDevName )))
				sTrack= NULL;
			else if( sTrack- sPath!= (signed)strlen( this->sDevName ) )
				sTrack= NULL;

		if( sTrack && strlen( sTrack )!= 8 )
			sTrack= NULL;

		if( !sTrack )
		{
			sprintf( this->sErr, "Path %s is not a valid track name. Name should be like '%s/Track 01'. Cannot open.", sPath, this->sDevName );
			return NULL; 
		}

		// Refresh toc for correct sector numbers
		if( this->RefreshTOC( true )== -1 )
		{
			// Raise an exception when the sampling rate is not supported by the module
			sprintf( this->sErr, "cdda read toc encountered error: Code %d .", errno );
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
			sprintf( this->sErr, "No memory available. Error code is %d.", errno );
			return NULL; 
		}

		stInfo->iPos= 0;
		stInfo->iStartFrame= this->GetStartSector( iTrack- 1 );
		stInfo->iEndFrame= this->GetStartSector( iTrack );
		stInfo->iTrack= iTrack;
		return stInfo;
	}	
};

#endif

/*
Trivial jitter correction.
Look further to implement at the higher level

int cd_jc1(int *p1,int *p2) //nuova
	// looks for offset in p1 where can find a subset of p2
{
	int *p,n;

	p=p1+opt_ibufsize-IFRAMESIZE-1;n=0;
	while(n<IFRAMESIZE*opt_overlap && *p==*--p)n++;
	if (n>=IFRAMESIZE*opt_overlap)	// jitter correction is useless on silence
	{
		n=(opt_bufstep)*CD_FRAMESIZE_RAW;
	}
	else			// jitter correction
	{
		n=0;p=p1+opt_ibufsize-opt_keylen/sizeof(int)-1;
		while((n<IFRAMESIZE*(1+opt_overlap)) && memcmp(p,p2,opt_keylen))
		  {p--;n++;};
//		  {p-=6;n+=6;}; //should be more accurate, but doesn't work well
		if(n>=IFRAMESIZE*(1+opt_overlap)){		// no match 
			return -1;
		};
		n=sizeof(int)*(p-p1);
	}
	return n;
}

int cd_jc(int *p1,int *p2)
{
	int n,d;
	n=0;
	if (opt_overlap==0) return (opt_bufstep)*CD_FRAMESIZE_RAW;
	do
		d=cd_jc1(p1,p2+n);
	while((d==-1)&&(n++<opt_ofs));n--;
	if (n<0)n=0;
	if (d==-1) return (d);
	else return (d-n*sizeof(int));
}

*/

