/*
 *			Python wrapper for the direct dvd reader class.
 *			Has ability to retrieve full information about titles and chapters
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

#if !defined( _DVD_H )
#define _DVD_H

#define DVD_CD_TYPE "DVD"
#define DVD_TITLE "Title "
#define DVD_TITLE0 "Title 0"
#define DVD_EXT ".vob"

#define LENGTH "length"
#define SEEKS "seeks"
#define CHAPTERS "chapters"
#define LANGUAGES "languages"
#define ANGLES "angles"
#define FORMAT "format"
#define UNSPECIFIED "unspecified"
#define LANGUAGE "language"
#define LANGUAGE_MODE "language_mode"
#define SAMPLE_RATE "sample_rate"
#define SAMPLE_FREQUENCY "sample_frequency"
#define CHANNELS "channels"
#define AUDIO_TYPE "type"
#define AUDIO_STREAMS "audio_streams"
#define VIDEO_STREAMS "video_streams"

#include "cdgeneric.h"
#include "dvdlibs/dvdcss/dvdcss.h"
#include "dvdlibs/dvdread/ifo_types.h"
#include "dvdlibs/dvdread/ifo_read.h"
#include "dvdlibs/dvdread/nav_types.h"
#include "dvdlibs/dvdread/nav_read.h"

// ------------------------------------------------------------
class DVDRead;

// **********************************************************************************
typedef struct
{
	dvd_file_t *file;
	dvd_reader_t *dvd;
	ifo_handle_t *ifo_file;
	int iVobuNo;
	int iVobuPos;
	unsigned char* sBuf;
	int iLen;
	int iVobuCount;
	int iVobuAllocated;
	int* aiVobu;
	int iTitle;
	int ttnnum;
} DVD_TRACK_INFO;


// **********************************************************************************
typedef struct
{
	int iTime;
	int iSector;
} DVD_SEEK_INFO;

// ------------------------------------------------------------
extern PyTypeObject PyDVDCDTrackType;

// **********************************************************************************
typedef struct 
{
	PyObject_HEAD
	DVDRead* cObject;
} PyDVDObject;

// **********************************************************************************
class DVDRead : public GenericCD
{
private:
  char sOrigName[ 255 ];				// Original name
  char sDevName[ 255 ];				// Device name
	char sErr[ 1024 ];
	ifo_handle_t *ifo_file;
public:
	// ---------------------------------------------------
	~DVDRead()
	{ 
		if( this->ifo_file )
			ifoClose( this->ifo_file );
	}

	// ---------------------------------------------------
	DVDRead( char* sDevName ) : GenericCD( sDevName )
	{
		// Open the file 
		strcpy( this->sOrigName, sDevName );
		strcpy( this->sDevName, sDevName );
		if( this->sDevName[ strlen( this->sDevName )- 1 ]== '\\' )
			this->sDevName[ strlen( this->sDevName )- 1 ]= 0;
		this->ifo_file= NULL;
	}
	// ---------------------------------------------------
  bool RefreshTitles()
	{
		dvd_reader_t *dvd= DVDOpen( this->sDevName );
    if( !dvd ) 
      return false;

		if( this->ifo_file )
			ifoClose( this->ifo_file );

		this->ifo_file= ifoOpen( dvd, 0 );
		DVDClose( dvd );

		return ( this->ifo_file!= NULL );
	}
	// ---------------------------------------------------
  char* GetType()
	{
		return DVD_CD_TYPE;
	}
	// ---------------------------------------------------
  ifo_handle_t* GetIfo()
	{
		return this->ifo_file;
	}
	// ---------------------------------------------------
	inline char* GetLastError()	{ return this->sErr; }
	// ---------------------------------------------------
	inline int GetTitlesCount()	{ return this->ifo_file->tt_srpt->nr_of_srpts; }
	// ---------------------------------------------------
	int GetTitlesLength( float* afLen )	
	{ 
		dvd_reader_t *dvd= DVDOpen( this->sDevName );
		if( !dvd )
			return -1;

		for( int i= 0; i< this->GetTitlesCount(); i++ )
		{
			ifo_handle_t *ifo_file= 
				ifoOpen( dvd, this->ifo_file->tt_srpt->title[ i ].title_set_nr );

			if( ifo_file )
			{
				pgc_t* pgc= ifo_file->vts_pgcit->pgci_srp[ this->ifo_file->tt_srpt->title[ i ].vts_ttn- 1 ].pgc;
				afLen[ i ]= (float)pgc->playback_time.hour* 3600+ 
							 ( pgc->playback_time.minute >> 4 )* 600+ ( pgc->playback_time.minute & 7 )* 60+ 
							 ( pgc->playback_time.second >> 4 )*10+ ( pgc->playback_time.second & 7 );

				ifoClose( ifo_file );
			}
		}
		DVDClose( dvd );
		return 0;
	}
	// ---------------------------------------------------
	inline unsigned int GetBlockSize()	{ return DVDCSS_BLOCK_SIZE;  } //this->dgCDROM.BytesPerSector; }
	// ---------------------------------------------------
	int ReadCell( dvd_file_t *file, int iOffset, int iCount, unsigned char* sBuf, unsigned int iLen )
	{
		// Validate we have enough buffer to read all the data
		int len;
		if( iLen< iCount* this->GetBlockSize() )
			return -1;

		len = DVDReadBlocks( file, iOffset, iCount, sBuf );
		if( len != (int)iLen ) 
		{
			sprintf( this->sErr, "Read failed for %d blocks at %d", iCount, iOffset );
			return -1;
		}
		return iLen;
	}
	/* ---------------------------------------------------------------------------------*/
	void AddVobus( DVD_TRACK_INFO *stTrack, int iSectorFrom, int iSectorTo )
	{
		// Find out how many vobus involved and place them all into the list
		vobu_admap_t *vobu_admap= stTrack->ifo_file->vts_vobu_admap;
		int iVobuCount= (vobu_admap->last_byte + 1 - VOBU_ADMAP_SIZE)/4; 
		for( int i= 0; i< iVobuCount; i++ )
			if( (int)vobu_admap->vobu_start_sectors[ i ]>= iSectorFrom && 
					(int)vobu_admap->vobu_start_sectors[ i ]< iSectorTo )
			{
				if( stTrack->iVobuAllocated< stTrack->iVobuCount+ 1 )
				{
					// Reallocate vobus list
					stTrack->iVobuAllocated+= 20;
					stTrack->aiVobu= (int*)realloc( stTrack->aiVobu, sizeof( int )* stTrack->iVobuAllocated );
				}
				stTrack->aiVobu[ stTrack->iVobuCount ]= i;
				stTrack->iVobuCount++;
			}
	}

	/* ---------------------------------------------------------------------------------*/
	void*
	OpenTitle( char* sPath )
	{
		// Get title number from the path( path is like 'Title 01' or '/dev/cdrom/Title 01' or 'd:\\Title 01' )
		if( !this->ifo_file )
		{
			strcpy( this->sErr, "DVD titles are not available or dvd is not present" );
			return NULL;
		}

		int iTitle= -1;
		char* sTitle= strstr( sPath, DVD_TITLE );
		// See if sPath not a relative path, then validate a path
		if( sTitle && sTitle!= sPath )
			if( strncmp( sPath, this->sOrigName, strlen( this->sOrigName )))
				sTitle= NULL;
			else if( sTitle- sPath!= (signed)strlen( this->sOrigName ) )
				sTitle= NULL;

		if( sTitle && strlen( sTitle )!= strlen( DVD_TITLE )+ 2+ strlen( DVD_EXT ) )
			sTitle= NULL;

		if( !sTitle )
		{
			sprintf( this->sErr, "Path %s is not a valid title name.", sPath );
			return NULL; 
		}

		iTitle= ( sTitle[ strlen( DVD_TITLE ) ]- '0' )* 10+ sTitle[ strlen( DVD_TITLE )+ 1 ]- '0';
		if( iTitle< 1 || iTitle> (int)this->GetTitlesCount() )
		{
			sprintf( this->sErr, "Title number( %d ) is out of range( 1, %d ). Cannot open.", iTitle, this->GetTitlesCount() );
			return NULL; 
		}

		DVD_TRACK_INFO* stInfo= (DVD_TRACK_INFO*)malloc( sizeof( DVD_TRACK_INFO ) );
		if( !stInfo )
		{
			sprintf( this->sErr, "No memory available. Error code is %s.", GetLastError() );
			return NULL; 
		}

    /**
     * Load the VTS information for the title set our title is in.
     */
		stInfo->dvd= DVDOpen( this->sDevName );
    if( !stInfo->dvd ) 
		{
      sprintf( this->sErr, "Error occured during DVD opening" );
			free( stInfo );
      return NULL;
		}

		iTitle--;
    stInfo->ifo_file = ifoOpen( stInfo->dvd, this->ifo_file->tt_srpt->title[ iTitle ].title_set_nr );
    if( !stInfo->ifo_file ) 
		{
      sprintf( this->sErr, "Can't open the title %d info file.\n", iTitle );
			DVDClose( stInfo->dvd );
			free( stInfo );
      return NULL;
    }
 
    int ttnnum = this->ifo_file->tt_srpt->title[ iTitle ].vts_ttn;
    int chapts = this->ifo_file->tt_srpt->title[ iTitle ].nr_of_ptts;

		stInfo->iVobuNo= 0;
		stInfo->iVobuPos= 0;
		stInfo->aiVobu= NULL;
		stInfo->sBuf= NULL;
		stInfo->iVobuCount= 0;
		stInfo->iVobuAllocated= 0;
		stInfo->iTitle= iTitle;
		stInfo->ttnnum= ttnnum- 1;

		stInfo->file = DVDOpenFile( stInfo->dvd, this->ifo_file->tt_srpt->title[ iTitle ].title_set_nr, DVD_READ_TITLE_VOBS ); 

		// Create vobu list
    vts_ptt_srpt_t *vts_ptt_srpt= stInfo->ifo_file->vts_ptt_srpt;
    for( int j = 0; j < chapts; ++j ) 
		{
      int pgcnum = vts_ptt_srpt->title[ ttnnum - 1 ].ptt[ j ].pgcn;
      int pgn = vts_ptt_srpt->title[ ttnnum - 1 ].ptt[ j ].pgn;
      pgc_t *cur_pgc= stInfo->ifo_file->vts_pgcit->pgci_srp[ pgcnum - 1 ].pgc;
      int start_cell = cur_pgc->program_map[ pgn - 1 ] - 1;
			
			int iLastSector= 0;
			if( j== chapts- 1 )
				iLastSector= cur_pgc->cell_playback[ cur_pgc->nr_of_cells- 1 ].last_sector+ 1;
			else if( cur_pgc->program_map )
				iLastSector= cur_pgc->cell_playback[ cur_pgc->program_map[ pgn ] - 1 ].first_sector;

			this->AddVobus( stInfo,
				cur_pgc->cell_playback[ start_cell ].first_sector,
        iLastSector );
			/*
			this->AddVobus( stInfo,
				cur_pgc->cell_playback[ start_cell ].first_sector,
				cur_pgc->cell_playback[ start_cell ].last_sector );
			*/
    }

		return stInfo;
	}	
};






#endif /* _DVD_H */
