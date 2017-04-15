/*
 *			Class to support direct audio playing through OSS
 *
 *		Code is selectively taken form the libao2.oss / MPlayer
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

#ifndef SOUND_UNIX_H
#define SOUND_UNIX_H

#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <fcntl.h>

#include "afmt.h"

#define MAX_DEVICES 4
#define READ_BUFFER_SIZE 4096

// -------------------------------------------------------------------
// Vars and constants
struct BUFFER_CHUNK
{
	unsigned char* sBuf;
	int iLen;
	int iRead;
};

// -------------------------------------------------------------------
struct CHANNEL_INFO
{
	int bMuted;
	int iVolume;
};

// -------------------------------------------------------------------
static char *dsp=PATH_DEV_DSP;
char *mixer = PATH_DEV_MIXER;
const int MAX_INT_BUFFERS= 100;
const int OPEN_FLAGS= (O_WRONLY|O_NONBLOCK);
const int OPEN_FLAGS_READ= (O_RDONLY);

// -------------------------------------------------------------------
int GetDevicesCount()
{
	int audio_fd, i= 0, iRes= 0;
	char audiopath[1024];

	/* Figure out what our audio device is */
	audio_fd = open( dsp, OPEN_FLAGS, 0);
	if( audio_fd> 0 )
	{
		i++;
		close( audio_fd );
		iRes++;
	}

	/* If the first open fails, look for other devices */
	while( i< MAX_DEVICES )
	{
		struct stat sb;

		/* Don't use errno ENOENT - it may not be thread-safe */
		sprintf(audiopath, "%s%d", dsp, i);
		if ( stat(audiopath, &sb) == 0 )
			iRes++;
		i++;
	}
	return iRes;
}

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
		this->iErr= errno;
		strcpy( &this->sErr[ 0 ], strerror( errno ));
	}
public:
	DeviceHandler(){ this->iErr= 0; }
	int GetLastError(){ return this->iErr; }
	char* GetErrorString(){ return this->sErr; }
};

// *****************************************************************************************************
// Input device enumerator
// *****************************************************************************************************
class InputDevices
{
private:
	int iDevs;
	char sName[ 512 ];
	char sErr[ 512 ];

	// ----------------------------------------------
	bool RefreshDevice( int i )
	{
		if( i>= (int)this->iDevs )
		{
			sprintf( this->sErr, "Device id ( %d ) is out of range ( 0-%d )", i, this->iDevs- 1 );
			return false;
		}

		// No way to return textual descriptions in the time being
		strcpy( this->sName, "Device unknown" );
		return true;
	}

public:
	// ----------------------------------------------
	InputDevices()
	{
		this->iDevs= GetDevicesCount();
	}
	// ----------------------------------------------
	int Count(){ return this->iDevs; }
	// ----------------------------------------------
	char* GetName( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		return this->sName;
	}
	// ----------------------------------------------
	char* GetMID( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		sprintf( this->sName, "%x", 0 );
		return this->sName;
	}
	// ----------------------------------------------
	char* GetPID( int i )
	{
		if( !this->RefreshDevice( i ) )
			return NULL;

		sprintf( this->sName, "%x", 0 );
		return this->sName;
	}
	// ----------------------------------------------
	int GetChannels( int i )
	{
		if( !this->RefreshDevice( i ) )
			return -1;

		return -1;
	}
	// ----------------------------------------------
	int GetFormats( int i )
	{
		if( !this->RefreshDevice( i ) )
			return -1;

		return 0;
	}
};

// No difference between input and output...
typedef InputDevices OutputDevices;

// *****************************************************************************************************
//	Sound stream main class
// *****************************************************************************************************
class OSoundStream : public DeviceHandler
{
private:
	bool bStopFlag;
	// Buffers to be freed by the stop op
	int dev;
	int iDev;
	int format;
	int channels;
	int rate;
	int bps;
	int bufsize;
	int bPause;
	int bStop;
	long long bytesPlayed;

	// ---------------------------------------------------------------------------------------------------
	// close audio device
	void Uninit()
	{
		if( this->dev == -1 )
			return;

		ioctl( this->dev, SNDCTL_DSP_RESET, NULL);
		close( this->dev );
		this->dev = -1;
	}

	// ---------------------------------------------------------------------------------------------------
	int Init( int rate, int channels, int format, int iDev )
	{
		if( iDev )
		{
			char s[ 20 ];
			sprintf( s, "%s%d", dsp, iDev );
			this->dev= open( s, OPEN_FLAGS );
		}
		else
			this->dev= open( dsp, OPEN_FLAGS );

		if( this->dev< 0 )
		{
			this->iErr= errno;
			sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "OPEN");
			return -1;
		}

    if (fcntl(dev, F_SETFL, 0) == -1)
		{
			this->iErr= errno;
			sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "SET_BLOCK");
			return -1;
		}

		this->format=format;
		if( ioctl( this->dev, SNDCTL_DSP_SETFMT, &this->format)<0 || this->format != format)
			if( format == AFMT_AC3)
				return 0;

		this->channels = channels;
		if( format != AFMT_AC3 )
		{
			// We only use SNDCTL_DSP_CHANNELS for >2 channels, in case some drivers don't have it
			if( this->channels > 2 )
				if( ioctl( this->dev, SNDCTL_DSP_CHANNELS, &this->channels) == -1 || this->channels != channels )
				{
					this->iErr= errno;
					sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "SET_CHANNELS");
					return -1;
				}
				else;
			else
			{
				int c = this->channels-1;
				if (ioctl( this->dev, SNDCTL_DSP_STEREO, &c) == -1)
				{
					this->iErr= errno;
					sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "SET_STEREO");
					return -1;
				}

				this->channels= c+1;
			}
		}

		// set rate
		this->rate= rate;
		this->bytesPlayed= 0;
		ioctl( this->dev, SNDCTL_DSP_SPEED, &this->rate);
		return 0;
	}

public:
	// ---------------------------------------------------------------------------------------------------
	OSoundStream( int rate, int channels, int format, int flags, int iDev ) : DeviceHandler()
	{
		this->iDev= iDev;
		this->bPause= 0;
		this->bStop= 0;
		if( this->Init( rate, channels, format, iDev )== 0 )
			this->bufsize= this->GetSpace();
	}

	// ---------------------------------------------------------------------------------------------------
	~OSoundStream()
	{
		this->Uninit();
	}

	// ---------------------------------------------------------------------------------------------------
	int GetDevice(){ return this->dev;	}
	// ---------------------------------------------------------------------------------------------------
	int GetAudioBufSize(){ return this->bufsize;}
	// ---------------------------------------------------------------------------------------------------
	// return: how many bytes can be played without blocking
	int GetSpace()
	{
		audio_buf_info zz;
		if( this->bStop )
			return this->bufsize;

		if( ioctl( this->GetDevice(), SNDCTL_DSP_GETOSPACE, &zz)== -1 )
		{
			this->iErr= errno;
			sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "SNDCTL_DSP_GETOSPACE");
			return -1;
		}

		return zz.fragments*zz.fragsize;
	}
	// ---------------------------------------------------------------------------------------------------
	int GetRate(){ return this->rate;	}
	// ---------------------------------------------------------------------------------------------------
	int GetChannels(){ return this->channels;	}
	// ---------------------------------------------------------------------------------------------------
	// return: delay in bytes between first and last playing samples in buffer
	int IsPlaying()
	{
		int r=0;
		if( this->bStop )
			return 0;

		if(ioctl( this->GetDevice(), SNDCTL_DSP_GETODELAY, &r)!=-1)
			 return r;

		this->iErr= errno;
		sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "SNDCTL_DSP_GETODELAY");
		return -1;
	}


	// ---------------------------------------------------------------------------------------------------
	int GetVolume()
	{
		int fd, devs, v= 0;
		if( this->iDev )
		{
			char s[ 20 ];
			sprintf( s, "%s%d", mixer, this->iDev );
			fd= open( s, O_RDWR, 0 );
		}
		else
			fd= open( mixer, O_RDWR, 0 );

		if( fd== -1 )
		{
			this->FormatError();
			return -1;
		}
		ioctl( fd, SOUND_MIXER_READ_DEVMASK, &devs );
		if( devs & SOUND_MASK_PCM )
			ioctl(fd, SOUND_MIXER_READ_PCM, &v);
		close( fd );
		// Make sure the volume is adjusted to match 16 bit
		v= ( v & 0xff00 ) | 0xff;
		return v;
	}

	// ---------------------------------------------------------------------------------------------------
	int SetVolume(int iVolume )
	{
		int fd, devs;
		if( this->iDev )
		{
			char s[ 20 ];
			sprintf( s, "%s%d", mixer, this->iDev );
			fd= open( s, O_RDWR, 0 );
		}
		else
			fd= open( mixer, O_RDWR, 0 );

		if( fd== -1 )
		{
			this->FormatError();
			return -1;
		}

		ioctl( fd, SOUND_MIXER_READ_DEVMASK, &devs );
		// Adjust volume to match 16 bit
		iVolume= ( iVolume >> 8 ) | ( iVolume & 0xff00 );
		if( devs & SOUND_MASK_PCM )
			ioctl( fd, SOUND_MIXER_WRITE_PCM, &iVolume);

		close( fd );
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	int Pause()
	{
		// Don't care about return code much...
		ioctl( this->dev, SNDCTL_DSP_POST, NULL);
		this->bPause= 1;
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	int Unpause()
	{
		this->bPause= 0;
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	float GetLeft()
	{
		float f= this->IsPlaying();
		if( f< 0 )
			return f;

		return f/((double)( 2* this->channels* this->rate ));
	}

	// ---------------------------------------------------------------------------------------------------
	int Stop()
	{
		this->bStop= 1;
		this->bPause= 0;
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	float Play( unsigned char *buf, int iLen )
	{
		// See how many bytes playing right now and add them to the pos
		if( this->bStop )
			this->bStop= 0;

		while( iLen )
		{
			int iAvail= this->GetSpace();
			if( iAvail== 0 || this->bPause )
			{
				usleep( 5 );
				continue;
			}
			if( this->bStop )
			{
				ioctl( this->dev, SNDCTL_DSP_POST, NULL);
				break;
			}

			int i= write( this->GetDevice(), buf, ( iAvail> iLen ) ? iLen: iAvail );
			if( i< 0 )
			{
				if( GetLastError()== EAGAIN )
					// Some disconnect between GetOSpace and write...
					continue;

				strcpy( &this->sErr[ 0 ], strerror( errno ));
				return i;
			}
			this->bytesPlayed+= i;
			buf+= i;
			iLen-= i;
		}
		return this->GetLeft();
	}

	// ---------------------------------------------------------------------------------------------------
	// Return position in s
	double GetPosition()
	{
		int r= this->IsPlaying();
		return ((double)( this->bytesPlayed- r ))/((double)( 2* this->channels* this->rate ));
	}

};


// *****************************************************************************************************
//	Input sound stream main class
// *****************************************************************************************************
class ISoundStream : public DeviceHandler
{
private:
	bool bStopFlag;
	// Buffers to be freed by the stop op
	int dev;
	int format;
	int channels;
	int rate;
	int bufscount;
	int bufsize;
	int bufsused;
	long long bytes;

	// ---------------------------------------------------------------------------------------------------
	// close audio device
	void Uninit()
	{
		if( this->dev == -1 )
			return;

		ioctl( this->dev, SNDCTL_DSP_RESET, NULL);
		close( this->dev );
		this->dev = -1;
	}

	// ---------------------------------------------------------------------------------------------------
	int Init( int rate, int channels, int format )
	{
		this->dev= open( dsp, OPEN_FLAGS_READ );
		if( this->dev< 0 )
		{
			this->iErr= errno;
			sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "OPEN");
			return -1;
		}

    /*if (fcntl(this->dev, F_SETFL, O_NONBLOCK) == -1)
		{
			this->iErr= errno;
			sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "SET_BlOCK");
			return -1;
		}*/

		this->format=format;
		if( ioctl( this->dev, SNDCTL_DSP_SETFMT, &this->format)<0 || this->format != format)

		this->channels = channels;
		int c = this->channels-1;
		if (ioctl( this->dev, SNDCTL_DSP_STEREO, &c) == -1)
		{
			this->iErr= errno;
			sprintf( &this->sErr[ 0 ], "%s at %s", strerror( errno ), "SET_CHANNELS");
			return -1;
		}

		this->channels= c+1;

		// set rate
		this->rate= rate;
		this->bytes= this->bufsused= 0;
		ioctl( this->dev, SNDCTL_DSP_SPEED, &this->rate);
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	float GetLeft()
	{
		return (float)( this->bufsize- this->GetSpace() )/ ((double)( 2* this->channels* this->rate ));
	}
	// ---------------------------------------------------------------------------------------------------
	// return: how many bytes can be grabbed without blocking
	int GetSpace()
	{
		audio_buf_info zz;
		if( ioctl( this->dev , SNDCTL_DSP_GETISPACE, &zz)== -1 )
			return -1;

		return zz.fragments*zz.fragsize;
	}
public:
	// ---------------------------------------------------------------------------------------------------
	ISoundStream( int rate, int channels, int format, int flags, int iDev ) : DeviceHandler()
	{
		this->bufsize= 0;
		if( this->Init( rate, channels, format )== 0 )
			this->bufsize= READ_BUFFER_SIZE;
		this->Stop();
	}
	// ---------------------------------------------------------------------------------------------------
	~ISoundStream()
	{
		this->Stop();
	}
	// ---------------------------------------------------------------------------------------------------
	int GetLastError()	{ return( this->iErr );	}
	// ---------------------------------------------------------------------------------------------------
	char* GetErrorString()	{ return( this->sErr );	}
	// ---------------------------------------------------------------------------------------------------
	int GetDevice(){ return this->dev;	}
	// ---------------------------------------------------------------------------------------------------
	int GetAudioBufSize(){ return this->bufsize;	}
	// ---------------------------------------------------------------------------------------------------
	int GetRate(){ return this->rate;	}
	// ---------------------------------------------------------------------------------------------------
	int GetChannels(){ return this->channels;	}

	// ---------------------------------------------------------------------------------------------------
	int Stop()
	{
		this->Uninit();
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	bool Start()
	{
		if( this->GetDevice()== -1 )
			this->Init( this->rate, this->channels, this->format );

		return true;
	}

	// ---------------------------------------------------------------------------------------------------
	// Return position in s
	double GetPosition()
	{
		return ((double)this->bytes )/((double)( 2* this->channels* this->rate ));
	}
	// ---------------------------------------------------------------------------------------------------
	// Get size of the data that already in the buffer( read at least half a buffer )
	int GetSize()
	{
		return READ_BUFFER_SIZE;
	}
	// ---------------------------------------------------------------------------------------------------
	// Return data from the buffer
	int GetData( char* pData, int iSize )
	{
		int i= read( this->GetDevice(), pData, iSize );
		if( i< 0 )
		{
			this->FormatError();
			return i;
		}
		this->bytes+= i;
		return i;
	}

};

// *****************************************************************************************************
// Mixer devices enumerator/holder
// *****************************************************************************************************
class Mixer : public DeviceHandler
{
private:
	char sName[ 512 ];
	CHANNEL_INFO aiChannel[ 32 ];
	int dev;

	int devmask;
	int recmask;
	int stereodevs;
	int recsrc;

	// ----------------------------------------------
	int GetConnection( int iDest, int iConn )
	{
		int i, cnt= 0;
		for( i= 0; i< SOUND_MIXER_NRDEVICES; i++ )
			if( this->devmask & ( 1 << i ) )
			{
				// Get connection name by number
				if( cnt== iConn )
					return i;

				// Device exists so proceed with count
				if( (this->recmask & ( 1 << i )) == ( iDest << i ))
					cnt++;
			}

		return -1;
	}

	// ----------------------------------------------
	bool Refresh()
	{
		int status = ioctl( this->dev, SOUND_MIXER_READ_DEVMASK, &this->devmask);
		if (status == -1)
		{
			this->FormatError();
			return false;
		}
		status = ioctl( this->dev, SOUND_MIXER_READ_RECMASK, &this->recmask);
		if (status == -1)
		{
			this->FormatError();
			return false;
		}
		status = ioctl( this->dev, SOUND_MIXER_READ_STEREODEVS, &this->stereodevs);
		if (status == -1)
		{
			this->FormatError();
			return false;
		}
		status = ioctl( this->dev, SOUND_MIXER_READ_RECSRC, &this->recsrc);
		if (status == -1)
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
		if( i )
		{
			char s[ 20 ];
			sprintf( s, "%s%d", mixer, i );
			this->dev= open( s, O_RDWR|O_NONBLOCK, 0 );
		}
		else
			this->dev= open( mixer, O_RDWR|O_NONBLOCK, 0 );

		memset( this->aiChannel, 0, sizeof( this->aiChannel ) );

		if( this->dev< 0 )
			this->FormatError();
		else
			this->Refresh();
	}
	// ----------------------------------------------
	~Mixer()
	{
		if( this->dev> 0 )
			close( this->dev );
	}
	// ----------------------------------------------
	int GetDestinationsCount()
	{
		// It seems OSS is very limited in terms of destination enumeration...
		// hardcode number of destinations
		return 2;
	}
	// ----------------------------------------------
	char* GetDestinationName( int iDest )
	{
		return (char*)( iDest ? "Recording Control": "Volume Control" );
	}
	// ----------------------------------------------
	// Return number of sources under destination iDest
	int GetConnectionsCount( int iDest )
	{
		int i, cnt= 0;
		for( i= 0; i< SOUND_MIXER_NRDEVICES; i++ )
			if( this->devmask & ( 1 << i ) )
				// Device exists so proceed with count
				if( ( this->recmask & ( 1 << i )) == ( iDest << i ))
					cnt++;

		return cnt;
	}
	// ----------------------------------------------
	// Return number of sources under destination iDest
	char* GetConnectionName( int iDest, int iConn )
	{
	  const char *sound_device_names[] = SOUND_DEVICE_LABELS;
		int i= this->GetConnection( iDest, iConn );
		return (char*)( ( i>= 0 ) ? sound_device_names[ i ]: "?????" );
	}
	// ----------------------------------------------
	// Return number of lines attached to the connection
	int GetControlsCount( int iDest, int iConn )
	{
		// It looks like OSS does not support mutliple lines attached to the connection
		// Just hardcode the number of controls for Volume is 2, for Record is 1
		return ( iDest ) ? 1: 2;
	}
	// ----------------------------------------------
	// Return control value
	int GetControlValue( int iDest, int iConn, int iControl, int iChannel, int *piValues )
	{
		int i= this->GetConnection( iDest, iConn ), iRet= 0;
		// Get values normalized to 0xffff
		if( i!= -1 )
		{
			int iVal, status;
			if( iControl )
			{
				piValues[ 0 ]= this->aiChannel[ i ].bMuted;
				return 1;
			}

			if( this->aiChannel[ i ].bMuted )
				iVal= this->aiChannel[ i ].iVolume;
			else
			{
				status = ioctl(this->dev, MIXER_READ(i), &iVal);
				if (status == -1)
				{
					this->FormatError();
					return -1;
				}
			}

			iRet++;
			if( iChannel== -1 )
			{
				// loop through all channels
				piValues[ 0 ]= (int)( (float)( iVal & 0xff )* 655.35 );
				if( this->stereodevs & ( 1 << i ) )
				{
					piValues[ 1 ]= (int)( (float)( ( iVal & 0xff00 ) >> 8 )* 655.35 );
					iRet++;
				}
			}
			else
				piValues[ 0 ]= iChannel ?
					(int)( (float)( ( iVal & 0xff00 ) >> 8 )* 655.35 ):
					(int)( (float)( iVal & 0xff )* 655.35 );
		}
		return iRet;
	}

	// ----------------------------------------------
	// Set control value
	bool SetControlValue( int iDest, int iConn, int iControl, int iChannel, int iValue )
	{
		int i= this->GetConnection( iDest, iConn );
		// Get values normalized to 0xffff
		if( i!= -1 )
		{
			int iVal, iNewVal, status= 0;
			iNewVal= (int)( (float)iValue/ 655.35 ) & 0xff;

			if( iChannel== -1 && !iControl )
				// loop through all channels
				iVal= iNewVal | ( iNewVal << 8 );
			else
			{
				status = ioctl(this->dev, MIXER_READ(i), &iVal);
				if (status == -1)
				{
					this->FormatError();
					return false;
				}

				// Save previous value in case if there was mute on channel
				// Not thread safe !!!
				if( iControl )
					if( iValue== 1 )
					{
						if( iChannel== -1 && !this->aiChannel[ i ].bMuted )
						{
							this->aiChannel[ i ].bMuted= 1;
							this->aiChannel[ i ].iVolume= iVal;
							iVal= 0;
						}
					}
					else if( iValue== 0 )
					{
						this->aiChannel[ i ].bMuted= 0;
						iVal= this->aiChannel[ i ].iVolume;
					}
				else
					iVal= iChannel ?
						( iVal & 0xff ) | ( iNewVal << 8 ):
						( iVal & 0xff00 ) | iNewVal;

			}
			// If channel is muted but trying to adjust, just adjust the logical value
			if( !iControl && this->aiChannel[ i ].bMuted )
				this->aiChannel[ i ].iVolume= iVal;
			else
				status = ioctl( this->dev, MIXER_WRITE(i), &iVal );

			/* set gain */
			if (status == -1)
			{
				this->FormatError();
				return false;
			}
		}
		return true;
	}

	// ----------------------------------------------
	// Return control name
	const char* GetControlName( int iDest, int iConn, int iControl )
	{
		return iControl ? "Mute": "Volume";
	}
	// ----------------------------------------------
	// Return control selection
	bool IsActive( int iDest, int iConn, int iControl )
	{
		int i= this->GetConnection( iDest, iConn );
		if( i== -1 )
			return false;

		return ( this->recsrc & ( 1 << i ) );
	}

	// ----------------------------------------------
	// Set control selection
	bool SetActive( int iDest, int iConn, int iControl )
	{
		int i= this->GetConnection( iDest, iConn );
		if( i== -1 )
			return false;

		i= ( 1 << i );
		ioctl(this->dev, SOUND_MIXER_WRITE_RECSRC, &i);
		return this->Refresh();
	}

	// ----------------------------------------------
	// Return control values
	bool GetControlValues( int iDest, int iConn, int iControl, int *piMin, int* piMax, int *piStep, int *piType, int* piChannels  )
	{
		int i= this->GetConnection( iDest, iConn );
		if( i== -1 )
			return false;

		*piMin= 0;
		*piMax= 0xffff;
		*piStep= 656;
		*piType= 0;
		*piChannels= ( this->stereodevs & ( 1 << i ) ) ? 2: 1;
		return true;
	}
};


#endif


/*


	*/

