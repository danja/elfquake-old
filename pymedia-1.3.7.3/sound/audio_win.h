/*
 *			Class to support direct audio playing
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

#ifndef SOUND_WIN32_H
#define SOUND_WIN32_H

#include <windows.h>
#include <mmsystem.h>
#include "afmt.h"

const int MAX_HEADERS= 40;
const int BUFFER_SIZE= 10000;

static void CALLBACK wave_callback(HWAVE hWave, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);
static void CALLBACK iwave_callback(HWAVE hWave, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);

// *****************************************************************************************************
// generic error handler for all classes below
// *****************************************************************************************************
class DeviceHandler
{
protected:
	char sErr[ 512 ];
	int iErr;

	// ----------------------------------------------
	void FormatError()
	{
		LPVOID lpMsgBuf;
		this->iErr= GetLastError();
		FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				this->iErr,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
				(LPTSTR) &lpMsgBuf,
				0,
				NULL
		);
		strcpy( this->sErr, (char*)lpMsgBuf );
	}
public:
	DeviceHandler(){ this->iErr= 0; }
	int GetLastError(){ return this->iErr; }
	char* GetErrorString(){ return this->sErr; }

};

// *****************************************************************************************************
// Input device enumerator
// *****************************************************************************************************
class InputDevices : DeviceHandler
{
private:
	UINT iDevs;
	char sName[ 512 ];
	WAVEINCAPS pCaps;

	// ----------------------------------------------
	bool RefreshDevice( int i )
	{
		if( i>= (int)this->iDevs )
		{
			sprintf( this->sErr, "Device id ( %d ) is out of range ( 0-%d )", i, this->iDevs- 1 );
			return false;
		}

		if( waveInGetDevCaps( i, &this->pCaps, sizeof( this->pCaps ))!= MMSYSERR_NOERROR )
		{
			this->FormatError();
			return false;
		}
		return true;
	}

public:
	// ----------------------------------------------
	InputDevices() : DeviceHandler()
	{
		this->iDevs= waveInGetNumDevs();
	}
	// ----------------------------------------------
	int Count(){ return this->iDevs; }
	// ----------------------------------------------
	char* GetName( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		strcpy( this->sName, this->pCaps.szPname );
		return this->sName;
	}
	// ----------------------------------------------
	char* GetMID( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		sprintf( this->sName, "%x", this->pCaps.wMid );
		return this->sName;
	}
	// ----------------------------------------------
	char* GetPID( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		sprintf( this->sName, "%x", this->pCaps.wPid );
		return this->sName;
	}
	// ----------------------------------------------
	int GetChannels( int i )
	{
		if( !this->RefreshDevice( i ) )
			return -1;

		return this->pCaps.wChannels;
	}
	// ----------------------------------------------
	int GetFormats( int i )
	{
		if( !this->RefreshDevice( i ) )
			return -1;

		return this->pCaps.dwFormats;
	}
};

// *****************************************************************************************************
// Output device enumerator
// *****************************************************************************************************
class OutputDevices : DeviceHandler
{
private:
	UINT iDevs;
	char sName[ 512 ];
	WAVEOUTCAPS pCaps;

	// ----------------------------------------------
	bool RefreshDevice( int i )
	{
		if( i>= (int)this->iDevs )
		{
			sprintf( this->sErr, "Device id ( %d ) is out of range ( 0-%d )", i, this->iDevs- 1 );
			return false;
		}

		if( waveOutGetDevCaps( i, &this->pCaps, sizeof( this->pCaps ))!= MMSYSERR_NOERROR )
		{
			this->FormatError();
			return false;
		}
		return true;
	}

public:
	// ----------------------------------------------
	OutputDevices() : DeviceHandler()
	{
		this->iDevs= waveOutGetNumDevs();
	}
	// ----------------------------------------------
	int Count(){ return this->iDevs; }
	// ----------------------------------------------
	char* GetName( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		strcpy( this->sName, this->pCaps.szPname );
		return this->sName;
	}
	// ----------------------------------------------
	char* GetMID( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		sprintf( this->sName, "%x", this->pCaps.wMid );
		return this->sName;
	}
	// ----------------------------------------------
	char* GetPID( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		sprintf( this->sName, "%x", this->pCaps.wPid );
		return this->sName;
	}
	// ----------------------------------------------
	int GetChannels( int i )
	{
		if( !this->RefreshDevice( i ) )
			return -1;

		return this->pCaps.wChannels;
	}
	// ----------------------------------------------
	int GetFormats( int i )
	{
		if( !this->RefreshDevice( i ) )
			return -1;

		return this->pCaps.dwFormats;
	}
};

// *****************************************************************************************************
// Mixer devices enumerator/holder
// *****************************************************************************************************
class Mixer : public DeviceHandler
{
private:
	char sName[ 512 ];
	int iDest;
	int iConn;
	int iControl;
	int iLineControlsCount;
	MIXERCAPS pCaps;
	HMIXEROBJ mixer;
	MIXERLINE pDest;
	MIXERLINE pConn;
	int iMixer;
	MIXERCONTROL astConnectionControls[ 20 ];
	MIXERCONTROL astControls[20];

	MIXERLINECONTROLS  mixerLineControls;

/*
	// ----------------------------------------------
	void RefreshDevice( int i )
	{
		HMIXEROBJ mixer;
		if( mixerOpen( (LPHMIXER)&mixer, i, 0, NULL, MIXER_OBJECTF_MIXER )== MMSYSERR_NOERROR )
		{
			MIXERCAPS  mixcaps;
			if( mixerGetDevCaps((UINT)mixer, &mixcaps, sizeof(MIXERCAPS))== MMSYSERR_NOERROR )
			{
				for( int j= 0; j< (int)mixcaps.cDestinations; j++ )
				{

					MIXERLINE line;
					line.cbStruct= sizeof( MIXERLINE );
					line.dwDestination= j;

					if( mixerGetLineInfo( (HMIXEROBJ)mixer, &line, MIXER_GETLINEINFOF_DESTINATION  )== MMSYSERR_NOERROR )
					{
						// create enough structures for line control
						MIXERCONTROL *p= (MIXERCONTROL*)malloc( sizeof( MIXERCONTROL )* line.cControls );
						MIXERLINECONTROLS controls;
						controls.cbStruct= sizeof( MIXERLINECONTROLS );
						controls.dwLineID= line.dwLineID;
						controls.cControls= line.cControls;
						controls.cbmxctrl= sizeof( MIXERCONTROL );
						controls.pamxctrl= p;
						printf("Destination #%lu = %s\n", i, line.szName);
						int numSrc= line.cConnections;
						for( int k= 0; k< numSrc; k++ )
						{
							line.cbStruct= sizeof(MIXERLINE);
							line.dwDestination= j;
							line.dwSource= k;
							if( mixerGetLineInfo( (HMIXEROBJ)mixer, &line, MIXER_GETLINEINFOF_SOURCE )== MMSYSERR_NOERROR )
							{
								printf("\tSource #%lu = %s, has %d controls\n", i, line.szName, line.cControls );
								// Get line controls if possible
									MIXERCONTROL       mixerControlArray[10];
									MIXERLINECONTROLS  mixerLineControls;
									mixerLineControls.cbStruct = sizeof(MIXERLINECONTROLS);
									mixerLineControls.cControls = line.cControls;
									mixerLineControls.dwLineID = line.dwLineID;
									mixerLineControls.pamxctrl = &mixerControlArray[0];
									mixerLineControls.cbmxctrl = sizeof(MIXERCONTROL);
									if( mixerGetLineControls((HMIXEROBJ)mixer, &mixerLineControls, MIXER_GETLINECONTROLSF_ALL)== MMSYSERR_NOERROR )
									{
										for( int l= 0; l< (int)line.cControls; l++ )
										{
											printf( "\t\t%s=", mixerControlArray[l].szName );
											// Get the value for the control
											MIXERCONTROLDETAILS_UNSIGNED value[ 20 ];
											MIXERCONTROLDETAILS          mixerControlDetails;

											mixerControlDetails.cbStruct = sizeof(MIXERCONTROLDETAILS);
											mixerControlDetails.dwControlID = mixerControlArray[l].dwControlID;
											mixerControlDetails.cChannels = line.cChannels;
											mixerControlDetails.cMultipleItems = 0;
											mixerControlDetails.paDetails = &value;
											mixerControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
											if ( mixerGetControlDetails((HMIXEROBJ)mixer, &mixerControlDetails, MIXER_GETCONTROLDETAILSF_VALUE)== MMSYSERR_NOERROR )
												printf( "%d\n", value[ 0 ].dwValue );
											else
												printf( "undefined\n" );

										}
									}
							}
						}
					}
				}
			}
		}
	}
*/
	// ----------------------------------------------
	bool RefreshDestination( int iDest )
	{
		if( iDest== this->iDest )
			return true;
		
		this->pDest.cbStruct= sizeof( MIXERLINE );
		this->pDest.dwDestination= iDest;
		this->iDest= iDest;

		if( mixerGetLineInfo( (HMIXEROBJ)this->mixer, &this->pDest, MIXER_GETLINEINFOF_DESTINATION  )!= MMSYSERR_NOERROR )
		{
			this->FormatError();
			return false;
		}

		return true;
	}

	// ----------------------------------------------
	bool RefreshConnection( int iDest, int iConn )
	{
		this->RefreshDestination( iDest );
		if( iConn== this->iConn )
			return true;

		MIXERLINECONTROLS controls;
		controls.cbStruct= sizeof( MIXERLINECONTROLS );
		controls.dwLineID= this->pDest.dwLineID;
		controls.cControls= ( this->pDest.cControls > sizeof( this->astConnectionControls ) ? sizeof( this->astConnectionControls ): this->pDest.cControls );
		controls.cbmxctrl= sizeof( MIXERCONTROL );
		controls.pamxctrl= &this->astConnectionControls[ 0 ];
		int numSrc= this->pDest.cConnections;
		this->iConn= iConn;

		this->pConn.cbStruct= sizeof(MIXERLINE);
		this->pConn.dwDestination= iDest;
		this->pConn.dwSource= iConn;
		if( mixerGetLineInfo( (HMIXEROBJ)this->mixer, &this->pConn, MIXER_GETLINEINFOF_SOURCE )!= MMSYSERR_NOERROR )
		{
			this->FormatError();
			return false;
		}

		// Get controls info
		MIXERLINECONTROLS  mixerLineControls;
		mixerLineControls.cbStruct = sizeof(MIXERLINECONTROLS);
		this->iLineControlsCount= mixerLineControls.cControls = ( this->pConn.cControls > sizeof( this->astConnectionControls ) ? sizeof( this->astConnectionControls ): this->pConn.cControls );
		mixerLineControls.dwLineID = this->pConn.dwLineID;
		mixerLineControls.pamxctrl = &this->astControls[0];
		mixerLineControls.cbmxctrl = sizeof(MIXERCONTROL);
		if( mixerGetLineControls((HMIXEROBJ)this->mixer, &mixerLineControls, MIXER_GETLINECONTROLSF_ALL)!= MMSYSERR_NOERROR )
		{
			this->FormatError();
			return false;
		}
		return true;
	}

public:
	// ----------------------------------------------
	Mixer( int i ) : DeviceHandler()
	{
		this->iMixer= i;
		this->iDest= this->iConn= this->iControl= -1;
//RefreshDevice( 0 );
		if( mixerOpen( (LPHMIXER)&this->mixer, i, 0, NULL, MIXER_OBJECTF_MIXER )== MMSYSERR_NOERROR )
			if( mixerGetDevCaps((UINT)this->mixer, &this->pCaps, sizeof(MIXERCAPS))== MMSYSERR_NOERROR )
				return;

		this->FormatError();
	}
	// ----------------------------------------------
	~Mixer()
	{
		mixerClose( (HMIXER)this->mixer );
	}
	// ----------------------------------------------
	char* GetName( int i )
	{
		strcpy( this->sName, this->pCaps.szPname );
		return this->sName;
	}
	// ----------------------------------------------
	char* GetMID( int i )
	{
		sprintf( this->sName, "%x", this->pCaps.wMid );
		return this->sName;
	}
	// ----------------------------------------------
	char* GetPID( int i )
	{
		sprintf( this->sName, "%x", this->pCaps.wPid );
		return this->sName;
	}
	// ----------------------------------------------
	int GetDestinationsCount()
	{
		return this->pCaps.cDestinations;
	}
	// ----------------------------------------------
	char* GetDestinationName( int iDest )
	{
		this->RefreshDestination( iDest );
		return this->pDest.szName;
	}
	// ----------------------------------------------
	// Return number of sources under destination iDest
	int GetConnectionsCount( int iDest )
	{
		this->RefreshDestination( iDest );
		return this->pDest.cConnections;
	}
	// ----------------------------------------------
	// Return number of sources under destination iDest
	char* GetConnectionName( int iDest, int iConn )
	{
		this->RefreshConnection( iDest, iConn );
		return this->pConn.szName;
	}
	// ----------------------------------------------
	// Return number of lines attached to the connection
	int GetControlsCount( int iDest, int iConn )
	{
		this->RefreshConnection( iDest, iConn );
		return this->pConn.cControls;
	}
	// ----------------------------------------------
	// Return control value
	int GetControlValue( int iDest, int iConn, int iControl, int iChannel, int *piValues )
	{
		MIXERCONTROLDETAILS_UNSIGNED value[ 20 ];
		MIXERCONTROLDETAILS          mixerControlDetails;
		MMRESULT											res;
		
		this->RefreshConnection( iDest, iConn );
		if( iControl>= this->GetControlsCount( iDest, iConn ) || iControl< 0 )
			return -1;

		if( iChannel< -1 || iChannel>= (int)this->pConn.cChannels )
		{
			sprintf( this->sErr, "Control has %d channels whereas %d was specified", this->pConn.cChannels, iChannel );
			this->iErr= 505;
			return -1;
		}

		mixerControlDetails.cbStruct = sizeof(MIXERCONTROLDETAILS);
		mixerControlDetails.dwControlID = this->astControls[ iControl ].dwControlID;
		mixerControlDetails.cChannels = this->pConn.cChannels;
		mixerControlDetails.cMultipleItems = 0;
		mixerControlDetails.paDetails = &value;
		mixerControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
		res= mixerGetControlDetails((HMIXEROBJ)this->mixer, &mixerControlDetails, MIXER_GETCONTROLDETAILSF_VALUE);
		if ( res!= MMSYSERR_NOERROR )
		{
			this->FormatError();
			return -1;
		}

		int iRet= 0;
		if( iChannel== -1 )
			// loop through all channels
			while( iRet< (int)this->pConn.cChannels )
				piValues[ iRet ]= value[ iRet ].dwValue, iRet++;
		else
			piValues[ 0 ]= value[ iChannel ].dwValue, iRet++;

		return iRet;
	}

		// ----------------------------------------------
	// Return control value
	bool SetControlValue( int iDest, int iConn, int iControl, int iChannel, int iValue )
	{
		MIXERCONTROLDETAILS_UNSIGNED value[ 20 ];
		MIXERCONTROLDETAILS          mixerControlDetails;
		
		this->RefreshConnection( iDest, iConn );
		if( iControl>= this->GetControlsCount( iDest, iConn ) || iControl< 0 )
			return true;

		if( iChannel< -1 || iChannel>= (int)this->pConn.cChannels )
		{
			sprintf( this->sErr, "Control has %d channels whereas %d was specified", this->pConn.cChannels, iChannel );
			this->iErr= 505;
			return false;
		}

		mixerControlDetails.cbStruct = sizeof(MIXERCONTROLDETAILS);
		mixerControlDetails.dwControlID = this->astControls[ iControl ].dwControlID;
		mixerControlDetails.cChannels = this->pConn.cChannels;
		mixerControlDetails.cMultipleItems = 0;
		mixerControlDetails.paDetails = &value;
		mixerControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);

		if( iChannel== -1 || mixerGetControlDetails((HMIXEROBJ)this->mixer, &mixerControlDetails, MIXER_GETCONTROLDETAILSF_VALUE)== MMSYSERR_NOERROR )
		{
			if( iChannel== -1 )
				for( int i= 0; i< (int)this->pConn.cChannels; i++ )
					value[ i ].dwValue= iValue;
			else
				value[ iChannel ].dwValue= iValue;

			if ( mixerSetControlDetails((HMIXEROBJ)this->mixer, &mixerControlDetails, MIXER_SETCONTROLDETAILSF_VALUE)== MMSYSERR_NOERROR )
				return true;
		}
		this->FormatError();
		return false;
	}

	// ----------------------------------------------
	// Return control name
	char* GetControlName( int iDest, int iConn, int iControl )
	{
		this->RefreshConnection( iDest, iConn );
		if( iControl>= this->GetControlsCount( iDest, iConn ) || iControl< 0 )
			return 0;

		return this->astControls[ iControl ].szName;
	}
	// ----------------------------------------------
	// Return control selection
	bool IsActive( int iDest, int iConn, int iControl )
	{
		// How to get this value in Windows ?
		MIXERCONTROLDETAILS          mixerControlDetails;
		MMRESULT											res;
		MIXERCONTROLDETAILS_BOOLEAN  pmxcdSelectValue[ 20 ];
		
		this->RefreshConnection( iDest, iConn );
		if( iControl>= this->GetControlsCount( iDest, iConn ) || iControl< 0 )
			return false;
		
		mixerControlDetails.cbStruct = sizeof(MIXERCONTROLDETAILS);
		mixerControlDetails.dwControlID = this->astControls[ iControl ].dwControlID;
		mixerControlDetails.cChannels = 1;
		mixerControlDetails.cMultipleItems = this->iLineControlsCount;
		mixerControlDetails.paDetails = &pmxcdSelectValue;
		mixerControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_BOOLEAN);
		res= mixerGetControlDetails((HMIXEROBJ)this->mixer, &mixerControlDetails, MIXER_GETCONTROLDETAILSF_VALUE);
		if ( res!= MMSYSERR_NOERROR )
		{
			this->FormatError();
			return false;
		}
		return false;
	}
	// ----------------------------------------------
	// Set control selection
	bool SetActive( int iDest, int iConn, int iControl )
	{
		// How to set this value in Windows ?
		return true;
	}
	// ----------------------------------------------
	// Return min control value
	bool GetControlValues( int iDest, int iConn, int iControl, int *piMin, int* piMax, int *piStep, int *piType, int* piChannels  )
	{
		this->RefreshConnection( iDest, iConn );
		if( iControl>= this->GetControlsCount( iDest, iConn ) || iControl< 0 )
		{
			return false;
		}

		*piMin= this->astControls[ iControl ].Bounds.dwMinimum;
		*piMax= this->astControls[ iControl ].Bounds.dwMaximum;
		*piStep= this->astControls[ iControl ].Metrics.cSteps;
		*piType= this->astControls[ iControl ].dwControlType;
		*piChannels= this->pConn.cChannels;
		return true;
	}
};


// *****************************************************************************************************
//	Sound stream main class
// *****************************************************************************************************
class OSoundStream
{
private:
	CRITICAL_SECTION cs;
	bool bStopFlag;
	// Buffers to be freed by the stop op
	WAVEHDR headers[ MAX_HEADERS ];
	HWAVEOUT dev;
	char sBuf[ BUFFER_SIZE ];
	int format;
	int iProcessed;
	__int64 iBytesProcessed;
	int iChannels;
	int iRate;
	int iBuffers;
	int iErr;
	float fLastPosition;

	// ---------------------------------------------------------------------------------------------------
	// Function called by callback
	void free_buffer( WAVEHDR *wh )
	{
		waveOutUnprepareHeader(this->dev, wh, sizeof (WAVEHDR));
		//Deallocate the buffer memory
		if( wh->lpData )
			free( wh->lpData );

		wh->lpData= NULL;
	}

public:
	// ---------------------------------------------------------------------------------------------------
	OSoundStream( int rate, int channels, int format, int flags, int ii )
	{
		MMRESULT res;
		WAVEFORMATEX outFormatex;
		int i;

		memset( headers, 0, sizeof( WAVEHDR )* MAX_HEADERS );
		this->dev= NULL;

		this->iErr= 0;
		InitializeCriticalSection(&this->cs);

		// No error is set, just do it manually
		if(rate == -1)
		{
			this->iErr= ::GetLastError();
		  return;
		}

		// Last error should be set already
		if(!waveOutGetNumDevs())
		{
			this->iErr= ::GetLastError();
			return;
		}

		this->iRate= rate;
		this->format= format;
		this->iBuffers= this->iProcessed= 0;
		this->iBytesProcessed= 0;
		this->fLastPosition= 0;
		this->bStopFlag= false;

		outFormatex.wFormatTag =
			( format!= AFMT_MPEG && format != AFMT_AC3 ) ? WAVE_FORMAT_PCM: -1;
		outFormatex.wBitsPerSample  =
			( format== AFMT_U8 || format== AFMT_S8 ) ? 8:
			( format== AFMT_S16_LE || format== AFMT_S16_BE || format== AFMT_U16_LE || format== AFMT_U16_BE  ) ? 16:
			( format== AFMT_AC3 ) ? 6: 0;
		this->iChannels= channels;
		outFormatex.nChannels       = channels;
		outFormatex.nSamplesPerSec  = rate;
		outFormatex.nAvgBytesPerSec = outFormatex.nSamplesPerSec * outFormatex.nChannels * outFormatex.wBitsPerSample/8;
		outFormatex.nBlockAlign     = outFormatex.nChannels * outFormatex.wBitsPerSample/8;
		res = waveOutOpen( &this->dev, ii, &outFormatex, (DWORD)wave_callback, (DWORD)this, CALLBACK_FUNCTION);
		if(res != MMSYSERR_NOERROR)
		{
			this->iErr= res;
		  return;
		}

		for( i= 0; i< MAX_HEADERS; i++ )
		{
			// Initialize buffers
			int res;
			LPWAVEHDR wh = &this->headers[ i ];
			void* p= malloc( BUFFER_SIZE );
			if( !p )
				return;
			wh->dwBufferLength = BUFFER_SIZE;
			wh->lpData = (char*)p;
			wh->dwFlags= 0;
			res = waveOutPrepareHeader( this->dev, wh, sizeof (WAVEHDR) );
			if(res)
			{
				this->iErr= ::GetLastError();
			  return;
		  }
		}
		/*
		switch(res)
		{
			case -1:
				sprintf( s, "Cannot initialize audio device" );
				break;
			case MMSYSERR_ALLOCATED:
				sprintf( s, "Device Is Already Open" );
				break;
			case MMSYSERR_BADDEVICEID:
				sprintf( s, "The Specified Device Is out of range");
				break;
			case MMSYSERR_NODRIVER:
				sprintf( s, "There is no audio driver in this system.");
			break;
			case MMSYSERR_NOMEM:
				sprintf( s, "Unable to allocate sound memory.");
			break;
			case WAVERR_BADFORMAT:
				sprintf( s, "This audio format is not supported.");
			break;
			case WAVERR_SYNC:
				sprintf( s, "The device is synchronous.");
			break;
			default:
				sprintf( s, "Unknown Media Error" );
			break;
		}
		*/

		waveOutReset( this->dev );
	}

	// ---------------------------------------------------------------------------------------------------
	~OSoundStream()
	{
		this->Stop();

		if(this->dev)
		{
			 waveOutReset(this->dev);      //reset the device
			 waveOutClose(this->dev);      //close the device
			 this->dev=NULL;
		}
		for( int i= 0; i< MAX_HEADERS; i++ )
			this->free_buffer( &this->headers[ i ] );

		DeleteCriticalSection(&this->cs);
	}

	// ---------------------------------------------------------------------------------------------------
	int GetLastError(){ return ::GetLastError();	}
	// ---------------------------------------------------------------------------------------------------
	char* GetErrorString(){ return "";	}
	// ---------------------------------------------------------------------------------------------------
	int GetBuffersCount(){ return MAX_HEADERS;	}
	// ---------------------------------------------------------------------------------------------------
	int GetRate(){ return this->iRate;	}
	// ---------------------------------------------------------------------------------------------------
	int GetChannels(){ return this->iChannels;	}
	// ---------------------------------------------------------------------------------------------------
	void CompleteBuffer( WAVEHDR *wh )
	{
		if( !this->bStopFlag )
		{
	 	  EnterCriticalSection( &this->cs );
			this->iBuffers--;
			this->iBytesProcessed+= wh->dwBufferLength;
		  LeaveCriticalSection( &this->cs );
		}
	}	
	// ---------------------------------------------------------------------------------------------------
	int Pause()	{	 return ( waveOutPause( this->dev ) ) ? -1: 0; }

	// ---------------------------------------------------------------------------------------------------
	int GetVolume()
	{
		 DWORD dwVolume;
		 return ( waveOutGetVolume( this->dev, &dwVolume ) ) ? (-1): (int)dwVolume;
	}

	// ---------------------------------------------------------------------------------------------------
	int SetVolume(int iVolume ) { return (waveOutSetVolume( this->dev, iVolume )) ? (-1): 0; }

	// ---------------------------------------------------------------------------------------------------
	int Stop()
	{
		EnterCriticalSection( &this->cs );
		this->bStopFlag= true;
		this->iBytesProcessed= this->iProcessed= this->iBuffers= 0;
		this->fLastPosition= 0;
		LeaveCriticalSection( &this->cs );

		if( this->dev )
			waveOutReset( this->dev );

		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	int Unpause() { return (waveOutRestart( this->dev )) ? (-1): 0; }

	// ---------------------------------------------------------------------------------------------------
	int IsPlaying() { return this->iBuffers!= 0; }

	// ---------------------------------------------------------------------------------------------------
	float Play(unsigned char *buf,int len)
	{
		// Try to fit chunk in remaining buffers
		this->bStopFlag= false;
		while( len> 0 )
		{
			// See if we need to wait until buffers are available
			if( this->iBuffers== MAX_HEADERS )
			{
				Sleep( 1 );
				continue;
			}

			int i= this->iProcessed % MAX_HEADERS;
			int l= ( len> BUFFER_SIZE ) ? BUFFER_SIZE: len;

			EnterCriticalSection( &this->cs );
			memcpy( this->headers[ i ].lpData, buf, l );
			this->headers[ i ].dwBufferLength = l;
			this->iProcessed++;
			this->iBuffers++;
			LeaveCriticalSection( &this->cs );

			buf+= l;
			len-= l;
			if( this->bStopFlag )
				break;

		  if( waveOutWrite( this->dev, &this->headers[ i ], sizeof (WAVEHDR) ) )
		  {
				this->iErr= ::GetLastError();
				return -1;
			}
		}
		return (float)0;
	}

	// ---------------------------------------------------------------------------------------------------
	float GetPosition()
	{
		// Adjust position using the correction factor 
		MMTIME stTime;
		stTime.wType= TIME_MS;

		waveOutGetPosition( this->dev, &stTime, sizeof( stTime ) );
		float fPosRes= ((float)stTime.u.ms)/ this->iRate;
		if( this->fLastPosition> fPosRes+ 0x7FFFFF )
			fPosRes+= ((float)0x7FFFFFF)/ this->iRate;

		EnterCriticalSection( &this->cs );
		this->fLastPosition= fPosRes;
		LeaveCriticalSection( &this->cs );
		return fPosRes;
	}
	// ---------------------------------------------------------------------------------------------------
	double GetLeft()
	{
		// Get physical position for the end of the buffer
		double fPos= ((double)this->iBytesProcessed)/ ((double)( 2* this->iChannels* this->iRate ));
		double fPos1= this->GetPosition();
		for( int i= 0; i< this->iBuffers; i++ )
			fPos+= ((double)this->headers[ ( this->iProcessed+ i ) % MAX_HEADERS ].dwBufferLength )/ ((double)( 2* this->iChannels* this->iRate ));
    return ( fPos- fPos1 ) < 0 ? 0: (this->iBuffers ? ( fPos- fPos1 ): 0 );
	}
	// ---------------------------------------------------------------------------------------------------
	int GetSpace()
	{
		// We have fixed number of buffers, just get the size of it
		return ( MAX_HEADERS- this->iBuffers )* BUFFER_SIZE;
	}
};

// *****************************************************************************************************
//	Input sound stream main class
// *****************************************************************************************************
class ISoundStream
{
private:
	CRITICAL_SECTION cs;
	bool bStopFlag;
	// Buffers to be freed by the stop op
	WAVEHDR headers[ MAX_HEADERS ];
	HWAVEIN dev;
	HANDLE hSem;
	int format;
	int iChannels;
	int iRate;
	int iErr;
  char databuf [ MAX_HEADERS * BUFFER_SIZE ];
  int databuf_length;

	// ---------------------------------------------------------------------------------------------------
	// Function called by callback
	void free_buffer( WAVEHDR *wh )
	{
		waveInUnprepareHeader(this->dev, wh, sizeof (WAVEHDR));
		//Deallocate the buffer memory
		if( wh->lpData )
			free( wh->lpData );

		wh->lpData= NULL;
	}

public:
	// ---------------------------------------------------------------------------------------------------
	ISoundStream( int rate, int channels, int format, int flags, int iId )
	{
		MMRESULT res;
		WAVEFORMATEX inFormatex;
		int i;

		memset( headers, 0, sizeof( WAVEHDR )* MAX_HEADERS );
		this->dev= NULL;

		this->iErr= 0;
		InitializeCriticalSection(&this->cs);

		// No error is set, just do it manually
		if(rate == -1)
		{
			this->iErr= ::GetLastError();
		  return;
		}

		// Last error should be set already
		if((int)waveInGetNumDevs()<= iId)
		{
			this->iErr= ::GetLastError();
			return;
		}

		this->iRate= rate;
		this->format= format;
		this->bStopFlag= true;

		inFormatex.wFormatTag = WAVE_FORMAT_PCM;
		inFormatex.wBitsPerSample = ( format== AFMT_U8 || format== AFMT_S8 ) ? 8: 16;
		this->iChannels= channels;
		inFormatex.nChannels       = channels;
		inFormatex.nSamplesPerSec  = rate;
		inFormatex.nAvgBytesPerSec = inFormatex.nSamplesPerSec * inFormatex.nChannels * inFormatex.wBitsPerSample/8;
		inFormatex.nBlockAlign     = inFormatex.nChannels * inFormatex.wBitsPerSample/8;
		inFormatex.cbSize       	 = 0;
		res = waveInOpen( &this->dev, iId, &inFormatex, (DWORD)iwave_callback, (DWORD)this, CALLBACK_FUNCTION);
		if(res != MMSYSERR_NOERROR)
		{
			this->iErr= res;
		  return;
		}

		for( i= 0; i< MAX_HEADERS; i++ )
		{
			// Initialize buffers
			int res;
			LPWAVEHDR wh = &this->headers[ i ];
			void* p= malloc( BUFFER_SIZE );
			if( !p )
				return;
			memset(p,0,BUFFER_SIZE);
			wh->dwBufferLength = BUFFER_SIZE;
			wh->lpData = (char*)p;
			wh->dwFlags= 0;
			res = waveInPrepareHeader( this->dev, wh, sizeof (WAVEHDR) );
			if(res)
			{
				this->iErr= ::GetLastError();
				return;
		  }
		}
		waveInReset( this->dev );
	}

	// ---------------------------------------------------------------------------------------------------
	~ISoundStream()
	{
		this->Stop();

		if(this->dev)
		{
			 waveInReset(this->dev);      //reset the device
			 waveInClose(this->dev);      //close the device
			 this->dev=NULL;
		}
		for( int i= 0; i< MAX_HEADERS; i++ )
			this->free_buffer( &this->headers[ i ] );

		DeleteCriticalSection(&this->cs);
	}

	// ---------------------------------------------------------------------------------------------------
	int GetLastError(){ return this->iErr;	}
	// ---------------------------------------------------------------------------------------------------
	char* GetErrorString(){ return "";	}
	// ---------------------------------------------------------------------------------------------------
	int GetRate(){ return this->iRate;	}
	// ---------------------------------------------------------------------------------------------------
	int GetChannels(){ return this->iChannels;	}
	// ---------------------------------------------------------------------------------------------------
	void CompleteBuffer( WAVEHDR *wh )
	{
		// Submit buffer after it's completed
		if( !this->bStopFlag )
		{
			if( wh )
			{
 				EnterCriticalSection( &this->cs );
				if (databuf_length<BUFFER_SIZE*MAX_HEADERS)
				{
					memcpy( this->databuf+ this->databuf_length, wh->lpData, BUFFER_SIZE);
					this->databuf_length+=BUFFER_SIZE;
				}
				LeaveCriticalSection( &this->cs );
			}

			memset(wh->lpData,0,BUFFER_SIZE);
			wh->dwFlags = 0;
			wh->dwBufferLength = BUFFER_SIZE;
			waveInPrepareHeader( this->dev, wh, sizeof (WAVEHDR));
			waveInAddBuffer( this->dev, wh, sizeof(WAVEHDR) );
		}
	}
	// ---------------------------------------------------------------------------------------------------
	int Stop()
	{
		this->bStopFlag= true;
		if( this->dev )
			waveInReset( this->dev );

		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	bool Start()
	{
		if( this->bStopFlag )
		{
			this->bStopFlag= false;
			if( waveInStart( this->dev )!= MMSYSERR_NOERROR )
			{
				this->iErr= ::GetLastError();
				return false;
			}

			// Submit buffers for grabbing
			this->databuf_length= 0;
			for (int j=0;j<MAX_HEADERS;j++)
		    waveInAddBuffer( this->dev, &this->headers[ j ], sizeof(WAVEHDR) );
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------------------
	double GetPosition()
	{
		MMTIME stTime;
		stTime.wType= TIME_MS;
		waveInGetPosition( this->dev, &stTime, sizeof( stTime ) );
		return ((double)stTime.u.ms)/ this->iRate;
	}
	// ---------------------------------------------------------------------------------------------------
	// Get size of the data already in the buffers
	int GetSize()
	{
		return this->databuf_length;
	}
	// ---------------------------------------------------------------------------------------------------
	// Return data from the buffers
	int GetData( char* pData, int iSize )
	{
	  memcpy( pData, this->databuf, iSize);
 		EnterCriticalSection( &this->cs );
 		if (databuf_length>iSize)
 		  memcpy(databuf,databuf+iSize,databuf_length-iSize);

		databuf_length -= iSize;
 		LeaveCriticalSection( &this->cs );
 		return iSize;
	}
};

// ---------------------------------------------------------------------------------------------------
// Callback for input
static void CALLBACK iwave_callback(HWAVE hWave, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	ISoundStream *cStream= (ISoundStream *)dwInstance;
	WAVEHDR *wh = (WAVEHDR *)dwParam1;
  if(uMsg == WIM_DATA )
		cStream->CompleteBuffer( wh );
}

// ---------------------------------------------------------------------------------------------------
// Callback itself
static void CALLBACK wave_callback(HWAVE hWave, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	 OSoundStream *cStream= (OSoundStream *)dwInstance;
	 WAVEHDR *wh = (WAVEHDR *)dwParam1;
   if(uMsg == WOM_DONE )
		cStream->CompleteBuffer( wh );
}

#endif
