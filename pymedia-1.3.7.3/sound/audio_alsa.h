/*
 *                      Class to support direct audio playing through ALSA
 *
 *              
 *
 *                              Copyright (C) 2002-2005  Dmitry Borisov,Alex Galkin
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

#ifndef ALSA_UNIX_H
#define ALSA_UNIX_H

#include <alsa/asoundlib.h>

#include <fcntl.h>

#include "afmt.h"

#define MAX_DEVICES 4
#define READ_BUFFER_SIZE 4096

#define MIXER_CAP_VOLUME            0x0001
#define MIXER_CAP_VOLUME_JOINED     0x0002
#define MIXER_CAP_PVOLUME           0x0004
#define MIXER_CAP_PVOLUME_JOINED    0x0008
#define MIXER_CAP_CVOLUME           0x0010
#define MIXER_CAP_CVOLUME_JOINED    0x0020

#define MIXER_CAP_SWITCH            0x0001
#define MIXER_CAP_SWITCH_JOINED     0x0002
#define MIXER_CAP_PSWITCH           0x0004
#define MIXER_CAP_PSWITCH_JOINED    0x0008
#define MIXER_CAP_CSWITCH           0x0010
#define MIXER_CAP_CSWITCH_JOINED    0x0020
#define MIXER_CAP_CSWITCH_EXCLUSIVE 0x0040

#define MIXER_CHANNEL_ALL -1

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
int oss2alsa_soundconst(int sndconst)
{
    switch( sndconst)
    {

   case 1: //AFMT_MU_LAW
   return SND_PCM_FORMAT_MU_LAW;
   case 2: //AFMT_A_LAW
   return SND_PCM_FORMAT_A_LAW;
   case 4: //AFMT_IMA_ADPCM
   return SND_PCM_FORMAT_IMA_ADPCM;
   case 8: //_EXPORT_INT(m, AFMT_U8);
   return SND_PCM_FORMAT_U8;
   case 0x10: //_EXPORT_INT(m, AFMT_S16_LE);
   return SND_PCM_FORMAT_S16_LE;
   case 0x20: //_EXPORT_INT(m, AFMT_S16_BE);
   return SND_PCM_FORMAT_S16_BE;
   case 0x40: //_EXPORT_INT(m, AFMT_S8);
   return SND_PCM_FORMAT_S8;
   case 0x80: //_EXPORT_INT(m, AFMT_U16_LE);
   return SND_PCM_FORMAT_U16_LE;
   case 0x100: //_EXPORT_INT(m, AFMT_U16_BE);
   return SND_PCM_FORMAT_U16_BE;
   case 0x200: //_EXPORT_INT(m, AFMT_MPEG);
   return SND_PCM_FORMAT_MPEG;
   case 0x400: //_EXPORT_INT(m, AFMT_AC3);
   return -1;
   }
return -1;
}
// -------------------------------------------------------------------
int
alsamixer_gethandle(char *cardname, snd_mixer_t **handle) {
  int err;
  if ((err = snd_mixer_open(handle, 0)) < 0) return err;
  if ((err = snd_mixer_attach(*handle, cardname)) < 0) return err;
  if ((err = snd_mixer_selem_register(*handle, NULL, NULL)) < 0) return err;
  if ((err = snd_mixer_load(*handle)) < 0) return err;

  return 0;
}

static snd_mixer_elem_t *alsamixer_find_elem(snd_mixer_t *handle, char *control, int id) 
{
  snd_mixer_selem_id_t *sid;
  static snd_mixer_elem_t *elem;
  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, id);
  snd_mixer_selem_id_set_name(sid, control);
  elem=snd_mixer_find_selem(handle, sid);
  return elem;
}

int GetDevicesCount()
{
  int card=-1;
  int cardcnt,res;
  res=snd_card_next( &card);
  //printf("res %d card %d\n",res,card);
  for (cardcnt=0;card!=-1 && res==0;cardcnt++)
  {
   res=snd_card_next( &card);
   //printf("res %d card %d\n",res,card);
  } 
  return cardcnt;
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
		strcpy( &this->sErr[ 0 ], snd_strerror(  this->iErr ));
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
	unsigned int channels;
	int formats;    
	// ----------------------------------------------
	bool RefreshDevice( int i )
	{
	char *cname; 
	char devname[20];
	int err;

	snd_pcm_hw_params_t *hwparams;
	//printf("refreshdevice\n");
		if( i>= (int)this->iDevs )
		{
			sprintf( this->sErr, "Device id ( %d ) is out of range ( 0-%d )", i, this->iDevs- 1 );
			return false;
		}
	err=snd_card_get_name(i,&cname);
	
	if (err==0)
	{
	   strcpy(this->sName,cname);
	}
	else
	{
	    strcpy(this->sErr,snd_strerror(err));      
	    return false;
	}
	int val,res;
	unsigned int val2;
	snd_pcm_t *handle;
	//printf("<-opening->\n");

        if (i>0)
	sprintf((char *) &devname[0],"hw:%d",i-1);
	else
	strcpy((char *)&devname[0],"default");
	res = snd_pcm_open( &handle,(char *)&devname[0],SND_PCM_STREAM_PLAYBACK,0);
	if (res < 0) 
	return false;
	//printf("<-opened->\n");		
	snd_pcm_hw_params_alloca(&hwparams);
	snd_pcm_hw_params_current(handle,hwparams);
	snd_pcm_hw_params_get_format(hwparams,(snd_pcm_format_t *) &val); 
	formats=val;	
	snd_pcm_hw_params_get_channels(hwparams,&val2); 
	channels=val2;
	snd_pcm_close(handle);
	//printf(" <%s>\n",cname);
		return true;
	}

public:
	// ----------------------------------------------
	InputDevices()
	{
	//printf("InputDevices");
		this->iDevs= GetDevicesCount();
	//printf("->\n");		
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

		return (int)this->channels;
	}
	// ----------------------------------------------
	int GetFormats( int i )
	{
		if( !this->RefreshDevice( i ) )
			return -1;

		return this->formats;
	}
};

// No difference between input and output...
typedef InputDevices OutputDevices;

// *****************************************************************************************************
//      Sound stream main class
// *****************************************************************************************************
class OSoundStream : public DeviceHandler
{
private:
	bool bStopFlag;
	// Buffers to be freed by the stop op
	int iDev;
	int format;
	int channels;
	int rate;
	int bps;
	int bufsize;
	int bPause;
	int bStop;
	long long bytesPlayed;
	//---------------------------------------------------------------------
    //ALSA data 
    
    snd_pcm_t *handle;
    snd_pcm_uframes_t periodsize;
    int framesize;
    /* Mixer identification for this stream*/
    char *cardname;
    char devname[20];
    char *controlname;
    int controlid;    
    unsigned int periods;
    snd_mixer_t *mixer_handle;
    snd_mixer_selem_id_t *mixer_sid;
    snd_mixer_elem_t *mixer_elem;
	//---------------------------------------------------------------------
	
	// ---------------------------------------------------------------------------------------------------
	// close audio device
	void Uninit()
	{
	      if (this->handle) 
	   {
		   snd_pcm_close(this->handle);
	   this->handle = 0;
	   }
	}

	// ---------------------------------------------------------------------------------------------------
	int Init( int rate, int channels, int format, int iDev )
	{
	char *cardname = "default";
	int res,dir;
	unsigned int val;
	snd_pcm_uframes_t frames;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
	snd_mixer_selem_id_t *sid;
	snd_mixer_elem_t *elem;
	int err;
	this->bytesPlayed=0;
	//printf("Init begin\n");
	/*if (this->handle) 
	{
	   //printf("this->handle\n");
	   snd_pcm_close(this->handle);
	   this->handle = 0;
	}*/
	// Modes : 
	// 0, 
	// SND_PCM_NONBLOCK - Non blocking mode 
	// SND_PCM_ASYNC - Async notification
	//
	if(iDev>0 && iDev<=GetDevicesCount()) 
	{
	char *cname;
	int err;
	//printf("snd_card_get_name\n");
	err=snd_card_get_name(iDev,&cname);
	
	if (err==0)
	{
	   cardname =cname;
	}
	else
	{
	    this->iErr=err;
	    strcpy(this->sErr,snd_strerror(err));      
	    return err;
	}          
	
	}
        if (iDev>0)
	{
	sprintf((char *)&this->devname,"hw:%d",iDev-1);
	}
	else
	{
	strcpy((char *)&this->devname,"default");
	}
	//printf("snd_pcm_open\n");
	res = snd_pcm_open( &(this->handle),(char *)&this->devname,SND_PCM_STREAM_PLAYBACK,0); //SND_PCM_NONBLOCK
	if (res < 0) 
	return res;
/*        res = snd_pcm_nonblock(this->handle, SND_PCM_NONBLOCK);
	if (res < 0) 
	return res;*/

	// Allocate a hwparam structure, and fill it in with configuration space 
	//printf("snd_pcm_hw_params_alloca\n");
	snd_pcm_hw_params_alloca(&hwparams);
	snd_pcm_sw_params_alloca(&swparams);

	res = snd_pcm_hw_params_any(this->handle, hwparams);
	if (res < 0) return res;
	// Fill it in with default values. 
	snd_pcm_hw_params_any(this->handle, hwparams);
	res=snd_pcm_hw_params_set_access(this->handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (res < 0) return res;
	res = snd_pcm_hw_params_test_format(this->handle,hwparams, (snd_pcm_format_t)format);
	if (res < 0) return res;
	res=snd_pcm_hw_params_set_format(this->handle, hwparams, (snd_pcm_format_t)format);
	if (res < 0) return res;
	this->format=format;
	res=snd_pcm_hw_params_set_channels(this->handle, hwparams, channels);
	if (res < 0) return res;
	this->channels =channels;
	dir = 0;

	res=snd_pcm_hw_params_set_rate_near(this->handle, hwparams, (unsigned int *)&rate, &dir);
	if (res < 0) return res;
	this->rate = rate;
	res=snd_pcm_hw_params_set_period_size_near(this->handle, hwparams, &this->periodsize, &dir);
	if (res < 0) return res;
	res=snd_pcm_hw_params_set_periods_near(this->handle,hwparams,&this->periods,&dir);
	if (res < 0) return res;
	this->framesize = this->channels * snd_pcm_hw_params_get_sbits(hwparams)/8;
	this->bufsize=this->framesize * frames;
	// Write it to the device 
	res = snd_pcm_hw_params(this->handle, hwparams);
	if (res) return res;
/*
	res = snd_pcm_sw_params_current(this->handle,swparams);
	if (res < 0) return res;
	res = snd_pcm_sw_params_set_avail_min(this->handle, swparams, 4);
	if (res < 0) return res;
	res=snd_pcm_sw_params(this->handle,swparams);
	if (res < 0) return res;   
*/
	res = snd_pcm_prepare(this->handle);
	if (res < 0) return res;
	// Now open default mixer control for this stream
	snd_mixer_selem_id_alloca(&sid);
	err = alsamixer_gethandle(cardname,&this->mixer_handle);
	if (err < 0) {
	     snd_mixer_close(this->mixer_handle);
	     return err;
		      }
       const char *cntrl_name=NULL;
       for (elem = snd_mixer_first_elem(this->mixer_handle); elem; elem = snd_mixer_elem_next(elem)) 
       {
/*       snd_mixer_selem_get_id(elem, sid);
       cntrl_name=snd_mixer_selem_id_get_name(sid);
       if (strcmp(cntrl_name,"PCM")==0 )         break;
       if (err=snd_mixer_selem_has_playback_volume_joined(elem))        break; */
       }
       if (err < 0) {
		    snd_mixer_close(this->mixer_handle);
		    return err;
		     }		     
       this->mixer_sid=sid;              
       this->controlname=(char *) cntrl_name;
       this->controlid=snd_mixer_selem_id_get_index(sid);        
       this->mixer_elem=elem;
       //printf("cardname %s\n control name %s\n",cardname,this->controlname);
       return res;
	}

public:
	// ---------------------------------------------------------------------------------------------------
	OSoundStream( int rate, int channels, int format, int flags, int iDev ) : DeviceHandler()
	{
   	    this->iDev= iDev;
	    this->bPause= 0;
	    this->bStop= 0;
	    this->rate = 44100;
	    this->format = SND_PCM_FORMAT_S16_LE;
	    this->periodsize = 64;
	    this->periods=128;
	    this->iErr=this->Init( rate, channels, oss2alsa_soundconst( format), iDev );
	    if(this->iErr<0) strcpy( &this->sErr[ 0 ], snd_strerror(  this->iErr ));					
	}

	// ---------------------------------------------------------------------------------------------------
	~OSoundStream()
	{
		this->Uninit();
	}

	// ---------------------------------------------------------------------------------------------------
	snd_pcm_t *GetDevice(){ return this->handle;    }
	// ---------------------------------------------------------------------------------------------------
	int GetAudioBufSize(){ return this->bufsize;}
	// ---------------------------------------------------------------------------------------------------
	// return: how many bytes can be played without blocking
	int GetSpace()
	{

			return this->bufsize;
	}

	// ---------------------------------------------------------------------------------------------------
	int GetRate(){ return this->rate;       }
	// ---------------------------------------------------------------------------------------------------
	int GetChannels(){ return this->channels;}
	// ---------------------------------------------------------------------------------------------------
	// return: delay in bytes between first and last playing samples in buffer
	int IsPlaying()
	{
	
		int err;
		if( this->bStop )
			return 0;
		snd_pcm_status_t *status;
    		snd_pcm_status_alloca(&status);
		err = snd_pcm_status(this->handle, status);
		if (err<0) return err;
    		switch(snd_pcm_status_get_state(status))
    			{
			case SND_PCM_STATE_OPEN:
			case SND_PCM_STATE_PREPARED:
			case SND_PCM_STATE_RUNNING:
			    err = snd_pcm_status_get_delay(status)*this->framesize;
			    break;
			default:
			    err = 0;
			}
		if (err<0)
			{
		this->iErr= err;
		strcpy(&this->sErr[ 0 ], snd_strerror(err));
		return -1;
			}
    		return(err);
    			
	}


	// ---------------------------------------------------------------------------------------------------
	int GetVolume()
	{
	long int vol;
	int err;
	   err=snd_mixer_selem_get_playback_volume(  this->mixer_elem,(snd_mixer_selem_channel_id_t)0,&vol);
	if(err<0) return err;
	return vol;
	}

	// ---------------------------------------------------------------------------------------------------
	int SetVolume(int iVolume )
	{
	   int err;
	   err=snd_mixer_selem_set_playback_volume_all( this->mixer_elem,iVolume);
	   return err;
	}

	// ---------------------------------------------------------------------------------------------------
	int Pause()
	{
        this->bPause= 1;
        snd_pcm_pause(this->handle,1);
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	int Unpause()
	{
		this->bPause= 0;
		snd_pcm_pause(this->handle,0);
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
	snd_pcm_drop(this->handle);
		this->bStop= 1;
		this->bPause= 0;
		return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	float Play( unsigned char *buf, int iLen )
	{
	int res,frames;
		// See how many bytes playing right now and add them to the pos
		if( this->bStop )
			this->bStop= 0;
	if (iLen % this->framesize)
	{
	return -1;
	}
	frames=iLen / this->framesize;
//again:
	while(frames>0)
	{
	res = snd_pcm_writei(this->handle, buf, frames);
	if (res == -EPIPE) 
	     {
	     // EPIPE means underrun 
	     snd_pcm_prepare(this->handle);
	     res=snd_pcm_writei( this->handle,buf, iLen/this->framesize);
	     res=snd_pcm_writei( this->handle,buf, iLen/this->framesize);
	     }
	else if (res == -EAGAIN || res == -EBUSY) {
				 snd_pcm_wait(this->handle, 100);
				 }
				 else if (res < 0) {
				      return res;
					}             
	frames -= res;                      
	}
			this->bytesPlayed+= this->framesize*frames;
		return frames; //this->GetLeft();
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
//      Input sound stream main class
// *****************************************************************************************************
class ISoundStream : public DeviceHandler
{
private:
	bool bStopFlag;
	// Buffers to be freed by the stop op
	int iDev;
	int format;
	int channels;
	int rate;
	int bps;
	int bufsize;
	int bPause;
	int bStop;
	char *buffer;
	long long bytesReaded;	
	long long lastReaded;
	//---------------------------------------------------------------------
    //ALSA data 
    
    snd_pcm_t *handle;
    snd_pcm_uframes_t periodsize;
    int framesize;
    /* Mixer identification for this stream*/
    char *cardname;
    char devname[20];
    const char *controlname;
    int controlid;    
    snd_mixer_t *mixer_handle;
    snd_mixer_selem_id_t *mixer_sid;
    snd_mixer_elem_t *mixer_elem;

 	// ---------------------------------------------------------------------------------------------------
	// close audio device
	void Uninit()
	{
	      if (this->handle) 
	   {
		   snd_pcm_close(this->handle);
	   this->handle = 0;
	   }
	}

	// ---------------------------------------------------------------------------------------------------
	int Init( int rate, int channels, int format )
	{
	char *cardname = "default";
	int res,dir;
	unsigned int val;
	snd_pcm_uframes_t frames;
	snd_pcm_hw_params_t *hwparams;
	snd_mixer_selem_id_t *sid;
	snd_mixer_elem_t *elem;
	
	int err;
	
	// Modes : 
	// 0, 
	// SND_PCM_NONBLOCK - Non blocking mode 
	// SND_PCM_ASYNC - Async notification
	//
	if(iDev>0 && iDev<=GetDevicesCount()) 
	{
	char *cname;
	int err;
	err=snd_card_get_name(iDev,&cname);
	if (err==0)
	 {
	   cardname =cname;
	 }
	 else
	 {
	    this->iErr=err;
	    strcpy(this->sErr,snd_strerror(err));      
	    return err;
	 }          
	}
        if (iDev>0)
	{
	sprintf((char *)&this->devname,"hw:%d",iDev-1);
	}
	else
	{
	strcpy((char *)&this->devname,"default");
	}

	res = snd_pcm_open( &(this->handle),(char *)&this->devname,SND_PCM_STREAM_CAPTURE,0); //SND_PCM_NONBLOCK
	if (res < 0) 
	return res;
	const char *cntrl_name=NULL;
	// Allocate a hwparam structure, and fill it in with configuration space 
	snd_pcm_hw_params_alloca(&hwparams);
	res = snd_pcm_hw_params_any(this->handle, hwparams);
	if (res < 0) return res;
	// Fill it in with default values. 
	snd_pcm_hw_params_any(this->handle, hwparams);
	snd_pcm_hw_params_set_access(this->handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(this->handle, hwparams,(snd_pcm_format_t) format);
	snd_pcm_hw_params_set_channels(this->handle, hwparams, channels);
	dir = 0;
	snd_pcm_hw_params_set_rate(this->handle, hwparams, rate, dir);
	snd_pcm_hw_params_set_period_size(this->handle, hwparams, this->periodsize, dir);
	snd_pcm_hw_params_set_periods(this->handle,hwparams,4,0);

	// Write it to the device 
	res = snd_pcm_hw_params(this->handle, hwparams);
	if (res) return res;
	// Query current settings. These may differ from the requested values,
	// which should therefore be sync'ed with actual values 
	snd_pcm_hw_params_current(this->handle,hwparams);
	snd_pcm_hw_params_get_format(hwparams,(snd_pcm_format_t *)&val); this->format = val;
	snd_pcm_hw_params_get_channels(hwparams,&val); this->channels = val;
	snd_pcm_hw_params_get_rate(hwparams,&val,&dir); this->rate = val;
	snd_pcm_hw_params_get_period_size(hwparams,&frames,&dir); this->periodsize = (int) frames;
	this->framesize = this->channels * snd_pcm_hw_params_get_sbits(hwparams)/8;
	this->bufsize=this->framesize * frames;
	// Now open default mixer control for this stream
	snd_mixer_selem_id_alloca(&sid);
	err = alsamixer_gethandle(cardname,&this->mixer_handle);
	if (err < 0) {
	     snd_mixer_close(this->mixer_handle);
	     return err;
	      }
       for (elem = snd_mixer_first_elem(this->mixer_handle); elem; elem = snd_mixer_elem_next(elem)) 
       {
/*       snd_mixer_selem_get_id(elem, sid);
       cntrl_name= snd_mixer_selem_id_get_name(sid);
       if (strcmp(cntrl_name,"PCM")==0 )         break;
       if (err=snd_mixer_selem_has_capture_volume_joined(elem))        break; */
       }
       if (err < 0) {
		    snd_mixer_close(this->mixer_handle);
		    return err;
		     }
       this->mixer_sid=sid;              
       this->controlname= cntrl_name;
       this->controlid=snd_mixer_selem_id_get_index(sid);        
       this->mixer_elem=elem;
       return res;        

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
		return this->bufsize;
	}
public:
	// ---------------------------------------------------------------------------------------------------
	ISoundStream( int rate, int channels, int format, int flags, int iDev ) : DeviceHandler()
	{
		  
		this->bufsize= 0;
		this->bytesReaded=0;
		this->buffer=NULL;
		if( this->Init( rate, channels, oss2alsa_soundconst( format) )== 0 )
			{
			this->buffer=(char *)malloc(this->bufsize);
			}
		this->Stop();
	}
	// ---------------------------------------------------------------------------------------------------
	~ISoundStream()
	{
	//printf ("ISoundStream()");
	this->Stop();
	free(this->buffer);
	this->buffer=NULL;
	//printf( "->\n");
	}
	// ---------------------------------------------------------------------------------------------------
	int GetLastError()      { return( this->iErr ); }
	// ---------------------------------------------------------------------------------------------------
	char* GetErrorString()  { return( this->sErr ); }
	// ---------------------------------------------------------------------------------------------------
	snd_pcm_t *GetDevice(){ return this->handle;    }
	// ---------------------------------------------------------------------------------------------------
	int GetAudioBufSize(){ return this->bufsize;    }
	// ---------------------------------------------------------------------------------------------------
	int GetRate(){ return this->rate;       }
	// ---------------------------------------------------------------------------------------------------
	int GetChannels(){ return this->channels;       }

	// ---------------------------------------------------------------------------------------------------
	int Stop()
	{
	//printf("Stop!");
	snd_pcm_drop(this->handle);
	if( this->buffer!=NULL) 
		free(this->buffer);
	this->buffer=NULL;
		this->bStop= 1;
	//printf("->\n");
	return 0;
	}

	// ---------------------------------------------------------------------------------------------------
	bool Start()
	{
		if( this->GetDevice() )
			this->Init( this->rate, this->channels, this->format );
		if (this->buffer==NULL) this->buffer=(char *)malloc(this->bufsize);
		return true;
	}

	// ---------------------------------------------------------------------------------------------------
	// Return position in s
	double GetPosition()
	{
		return ((double)this->bytesReaded )/((double)( 2* this->channels* this->rate ));
	}
	// ---------------------------------------------------------------------------------------------------
	// Get size of the data that already in the buffer( read at least half a buffer )
	int GetSize()
	{
		return this->bufsize;
	}
	// ---------------------------------------------------------------------------------------------------
	// Return data from the buffer
	int GetData( char* pData, int iSize )
	{
	
	int err,frcnt,dt;
	dt=iSize;
    	//printf("GetData %d \n",iSize);
	frcnt=this->bufsize/this->framesize;
	  //printf("read frames %d\n",frcnt);
	  err = snd_pcm_readi(this->handle, buffer, frcnt);
	  if (err == -EPIPE) {
	    /* EPIPE means overrun */
	    snd_pcm_prepare(this->handle);
	  }
	  else if (err == -EAGAIN ) //|| err == -EBUSY
          {
	    err = 0;
	  }
	  else if (err < 0) 
          {
    	    this->iErr=err;
	    strcpy(this->sErr,snd_strerror(err));      
	    return false;
  	  } 
			
	    //printf("frames %d data %d \n",err,this->framesize*err);
	    memcpy(pData,this->buffer,iSize);
            this->bytesReaded+=(this->framesize*err);
	    //printf("iSize %d\n",dt);
		return dt;
	}

};

// *****************************************************************************************************
// Mixer devices enumerator/holder
// *****************************************************************************************************
//destination - play/record
//connection - pcm/master/midi
//control  - volume,  tremble/bas /mute/unmute 
class Mixer : public DeviceHandler
{
private:
	char sName[ 512 ];
	CHANNEL_INFO aiChannel[ 32 ];
	int iDev;
    snd_mixer_t *handle;
     /* Mixer identification */
    char *cardname;
  /* Capabilities */
    unsigned int volume_cap;
    unsigned int switch_cap;
    unsigned int pchannels;
    unsigned int cchannels;
    snd_mixer_selem_id_t *sid;
    snd_mixer_elem_t *elem;
  /* min and max values for playback and capture volumes */
    long pmin;
    long pmax;
    long cmin;
    long cmax;
    
	// ----------------------------------------------
snd_mixer_elem_t * GetConnection( int iDest, int iConn )
	{
     snd_mixer_elem_t *elem;
     int i=0;   
//	printf("GetConnection %d %d\n",iDest,iConn);
     	for (elem = snd_mixer_first_elem(this->handle); elem; elem = snd_mixer_elem_next(elem)) 
	{
     	 	if (iDest==0) //PLAY
      		 {
       		   if (snd_mixer_selem_has_playback_channel(elem,(snd_mixer_selem_channel_id_t)0)==1) 
		  {	
//		   printf("playback %d (need %d)\n",i,iConn);
		   if(iConn==i) return elem;
		   i++;
		  }           
       		 }
      		else
      		 {
       		 if (snd_mixer_selem_has_capture_channel(elem,(snd_mixer_selem_channel_id_t)0)==1) 
		  {
//		   printf("capture %d (need %d)\n",i,iConn);
	  	   if(iConn==i) return elem;
		   i++;
		  }
      		 }
	}
      return NULL;
}

	// ----------------------------------------------
	// Check for device caps: play,rec etc.
	bool Refresh()
	{
		
  /* Determine mixer capabilities */
	this->volume_cap = this->switch_cap = 0;
	return true;
	}
bool SetMuteControlValue( int iDest, int iConn, int iChannel, int iValue )
	{
	  snd_mixer_elem_t *elem=NULL;
	  elem= this->GetConnection( iDest, iConn );
	  if (elem==NULL) return 0;   
	      if (snd_mixer_selem_has_common_switch(elem) || iDest==0 )  //has_common_switch
		{
		//printf("\nCommon switch for playback and capture OR Dest==0 (PLAYBACK)\n");
	        if(iChannel==-1)
		 {                     
		 //printf("All ");                      
		 if (snd_mixer_selem_has_playback_switch_joined(elem)) 
		  {
		   //printf("common switch \n");
		   if (snd_mixer_selem_set_playback_switch_all(elem,!iValue)<0) return false;                                                 
		  }
		 else
		  {
		   if (snd_mixer_selem_is_playback_mono(elem)) 
			{	
		     //printf("mono \n");
		     if (snd_mixer_selem_set_playback_switch(elem,(snd_mixer_selem_channel_id_t)0,!iValue)) return false;
			}
		   else 
		       {
  			//printf("all channels one by one %d\n",iValue);
		        for (unsigned char channel=0; channel <= SND_MIXER_SCHN_LAST; channel++) 
		        {
		 	 if (snd_mixer_selem_has_playback_channel(elem, (snd_mixer_selem_channel_id_t)channel)) 
		 		if(snd_mixer_selem_set_playback_switch(elem,(snd_mixer_selem_channel_id_t)channel,!iValue)<0) return false;
		     	else break;
		        }
		      } 
		    }                                   
		}
	         else // mute only iChannell
		{
		 //printf("only %d \n",iChannel);
		 if (snd_mixer_selem_has_playback_channel(elem, (snd_mixer_selem_channel_id_t)iChannel)) 
			if(snd_mixer_selem_set_playback_switch(elem,(snd_mixer_selem_channel_id_t)iChannel,!iValue)<0) return false;
		}  

		}
	      else
		{	
		 // Capture
		//printf("Capture ");
	        if(iChannel==-1)
		 {                     
		 //printf("All ");                      
		 if (snd_mixer_selem_has_capture_switch_joined(elem)) 
		  {
		   //printf("common switch \n");                      			
		   if(snd_mixer_selem_set_capture_switch_all(elem,!iValue)<0) return false;                                                 
		  }
		 else
		  {
		   if (snd_mixer_selem_is_capture_mono(elem)) 
			{	
		     //printf("mono \n");
		     if(snd_mixer_selem_set_capture_switch(elem,(snd_mixer_selem_channel_id_t)0,!iValue)<0) return false;
			}
		   else 
		       {
  			//printf("all channels one by one %d\n",iValue);
		        for (unsigned char channel=0; channel<= SND_MIXER_SCHN_LAST; channel++) 
		        {
		 	 if (snd_mixer_selem_has_capture_channel(elem, (snd_mixer_selem_channel_id_t)channel)) 
		 		if(snd_mixer_selem_set_capture_switch(elem,(snd_mixer_selem_channel_id_t)channel,!iValue)<0) return false;
		     	else break;
		        }
		      } 
		    }                                   
		}
	         else // mute only iChannell
		{
		 //printf("only %d \n",iChannel);
		 if (snd_mixer_selem_has_capture_channel(elem, (snd_mixer_selem_channel_id_t)iChannel)) 
			if(snd_mixer_selem_set_capture_switch(elem,(snd_mixer_selem_channel_id_t)iChannel,!iValue)<0) return false;
		}  

	       }
	  return true;			
	}
public:
	// ----------------------------------------------
	Mixer( int i ) : DeviceHandler()
	{
	char *cardname = "default";
        char devname[20];
	int err;
	// Now open default mixer control for this stream
	if(i>0)
	{
	 err=snd_card_get_name(iDev,&cardname);
	 if(err<0) return ;
	}
        if (iDev>0)
	{
	sprintf((char *)&devname,"hw:%d",iDev-1);
	}
	else
	{
	strcpy((char *)&devname,"default");
	}

	err = alsamixer_gethandle((char *)&devname,&this->handle);
	if (err < 0) {

	     snd_mixer_close(this->handle);
	     return ;
		     }
		err=this->Refresh();             
	return ;              

       
	}
	// ----------------------------------------------
	~Mixer()
	{
		if( this->handle> 0 )
			snd_mixer_close(this->handle);
	}
	// ----------------------------------------------
	int GetDestinationsCount()
	{
//printf("GetDestinationsCount\n");
		return 2;
	}
	// ----------------------------------------------
	char* GetDestinationName( int iDest )
	{
//printf("GetDestinationName\n");
		return (char *)( iDest ? "Recording Control": "Volume Control" );
	}
	// ----------------------------------------------
	// Return number of sources under destination iDest
int GetConnectionsCount( int iDest )
	{
      int cnt=0;  

	snd_mixer_elem_t *elem;
      for (elem = snd_mixer_first_elem(this->handle); elem; elem = snd_mixer_elem_next(elem)) 
      {
       if (iDest==0)
       	 {
         	 if (snd_mixer_selem_has_playback_channel( elem,(snd_mixer_selem_channel_id_t)0)==1) cnt++;   
	     }
       else
         {
	 if (snd_mixer_selem_has_capture_channel( elem,(snd_mixer_selem_channel_id_t)0)==1) cnt++;      
         }
	 
      }
//printf("GetConnectionsCount %d\n",cnt);
		return cnt;
	}
	// ----------------------------------------------
	// Return name of source under destination iDest
     char* GetConnectionName( int iDest, int iConn )
	{
//printf("GetConnectionName ");
     	char *name;     
     	snd_mixer_elem_t *elem;
     	int j=0;     
	    if (iDest==0)
	       {
           elem = snd_mixer_first_elem(this->handle);
           for (int i=0; elem; elem = snd_mixer_elem_next(elem),i++) 
           {          
	       if (snd_mixer_selem_has_playback_channel( elem,(snd_mixer_selem_channel_id_t)0)==1) 
	          {
	          if(j==iConn)
       	      {
              name=(char *)snd_mixer_selem_get_name(elem);            
//printf("%s\n",name);
              return (char *)name;
              }            
	          j++;
	          }   
           }   
	       }
	    else
	       {
           elem = snd_mixer_first_elem(this->handle);
           for (int i=0; elem; elem = snd_mixer_elem_next(elem),i++) 
           {              
	       if (snd_mixer_selem_has_capture_channel( elem,(snd_mixer_selem_channel_id_t)0)==1) 
	          {
              if(j==iConn)
       	      {                                      
	           name=(char *)snd_mixer_selem_get_name(elem);
//printf("%s\n",name);
               return (char *)name;
              } 
              j++;
	          }
           }   
	       }	    
        return NULL;    
	}
	// ----------------------------------------------
	// Return number of lines attached to the connection
	int GetControlsCount( int iDest, int iConn )
	{
//printf("GetControlsCount %d %d ret  %d\n",iDest,iConn,(( iDest ) ? 1: 2));
		// It looks like OSS does not support mutliple lines attached to the connection
		// Just hardcode the number of controls for Volume is 2, for Record is 1
		return ( iDest ) ? 1: 2;
	}
	// ----------------------------------------------
	// Return control value
	int GetControlValue( int iDest, int iConn, int iControl, int iChannel, int *piValues )
	{
//printf("GetControlValue ");
	//Control: Volume = 0 Mute >0
 	int iRet= 0;

	snd_mixer_elem_t *elem;
	elem= this->GetConnection( iDest, iConn );
	if (elem==NULL) return 0;   
	if( iControl ) //Mute
		{
		//printf("Mute ");	
		     if (snd_mixer_selem_has_common_switch(elem) || iDest==0) 
		     {
			if(iChannel==-1)
			    {		     
		                for (int i2 = 0; i2  <= SND_MIXER_SCHN_LAST; i2++) 
		                {
			            if (snd_mixer_selem_has_playback_channel(elem,(snd_mixer_selem_channel_id_t) i2)) 
			             {
			             	int ival;                                                    
			              	if(snd_mixer_selem_get_playback_switch( elem, (snd_mixer_selem_channel_id_t)i2, &ival)<0) return 0;
					iRet++;
//			              	this->aiChannel[ i2 ].bMuted=ival;
                          		piValues[ i2 ]=!ival;
			                //printf(" channel %d: %d\n",i2,ival);
                         	     }
		                 }
			   }
			else
			  {
			            if (snd_mixer_selem_has_playback_channel(elem,(snd_mixer_selem_channel_id_t) iChannel)) 
			             {
			             	int ival;                                                    
			              	if(snd_mixer_selem_get_playback_switch( elem, (snd_mixer_selem_channel_id_t)iChannel, &ival)<0) return 0;
					iRet++;
                          		piValues[ 0 ]=!ival;
			                //printf(" channel %d: %d\n",iChannel,ival);
                         	     }

			  }

		      }
		     else // CAPTURE
		      {  
			if(iChannel==-1)
			 {
		         for (int i2 = 0; i2  <= SND_MIXER_SCHN_LAST; i2++) 
		                {
			            if (snd_mixer_selem_has_capture_channel(elem,(snd_mixer_selem_channel_id_t) i2)) 
			             {
			             	int ival;                                                    
			              	if(snd_mixer_selem_get_capture_switch( elem, (snd_mixer_selem_channel_id_t)i2, &ival)<0) return 0;
					iRet++;
//			              	this->aiChannel[ i2 ].bMuted=ival;
                          		piValues[ i2 ]=!ival;
			                //printf(" channel %d: %d\n",i2,ival);

                         	     }
		                 }
			}
			else
			{
			            if (snd_mixer_selem_has_capture_channel(elem,(snd_mixer_selem_channel_id_t) iChannel)) 
			             {
			             	int ival;                                                    
			              	if(snd_mixer_selem_get_capture_switch( elem, (snd_mixer_selem_channel_id_t)iChannel, &ival)<0) return 0;
					iRet++;
//			              	this->aiChannel[ 0 ].bMuted=ival;
                          		piValues[ 0 ]=!ival;
			                //printf(" channel %d: %d\n",iChannel,ival);

                         	     }

			}
		      }
			  return iRet;
		}
	    //Volume
	if( iChannel== -1 ) // All channels
		{
			//printf("Volume\n");
		// loop through all channels
		      for (int i2=0; i2 <= SND_MIXER_SCHN_LAST; i2++)
		      {
			long ival;                              
			if (snd_mixer_selem_has_playback_channel(elem,(snd_mixer_selem_channel_id_t) i2)) 
			 {
			 if(snd_mixer_selem_get_playback_volume( elem,(snd_mixer_selem_channel_id_t) i2, &ival)<0) return 0;
			iRet++;
			//printf(" channel %d: %d\n",i2,ival);
			 piValues[ i2 ]= (int)ival;
			 }
		      }    
		}
	else // selected channel
		{
		 if (snd_mixer_selem_has_playback_channel(elem,(snd_mixer_selem_channel_id_t) iChannel)) 
		  {
		  long ival;
		  if(snd_mixer_selem_get_playback_volume(elem, (snd_mixer_selem_channel_id_t)iChannel, &ival)<0) return 0;
 		  iRet++;
		  piValues[ 0 ]= (int)ival;
		  //printf(" channel %d: %d\n",iChannel,ival);
		  }
		 }		
	return iRet;
	}
	// ----------------------------------------------
	// Set control value
	bool SetControlValue( int iDest, int iConn, int iControl, int iChannel, int iValue )
	{
	  //printf("SetControlValue %d %d %d %d %d\n",iDest,iConn,iControl,iChannel,iValue);
     	  //Control: Volume = 0 Mute >0
	  snd_mixer_elem_t *elem=NULL;
	  elem= this->GetConnection( iDest, iConn );
	  if (elem==NULL) return 0;   
	  if (iControl) //Mute
	      {
		return SetMuteControlValue( iDest,  iConn, iChannel,  iValue);
		 }
		 else // Volume
		 {
	      //printf("Volume ");
	      if (snd_mixer_selem_has_common_volume(elem) || iDest==0) //has_common_volume
	       {
	      //printf("Playback");
	       if(iChannel==-1)
		{                                           
		if (snd_mixer_selem_has_playback_volume_joined(elem)) 
		 {
			//printf("set playback volume all %d",iValue);
		  if(snd_mixer_selem_set_playback_volume_all(elem,(long)iValue)<0) return 0;                                                 
			//printf("ok\n");
		 }
		else
		 {
		  if (snd_mixer_selem_is_playback_mono(elem))
		   { 
		   if(snd_mixer_selem_set_playback_volume(elem,(snd_mixer_selem_channel_id_t)0,(long)iValue)<0) return 0;
                   }
		  else 
		   {
		    for (unsigned char channel=0; channel <= SND_MIXER_SCHN_LAST; channel++) 
		    {
			 if (snd_mixer_selem_has_playback_channel(elem,(snd_mixer_selem_channel_id_t) channel)) 
			{
			//printf("set playback volume ");
			if(snd_mixer_selem_set_playback_volume(elem,(snd_mixer_selem_channel_id_t)channel,(long)iValue)<0) return 0;
			//printf("ok\n");
			}
		     else break;
		    }
		    } 
		 }                                   
		}
	       else // only iChannell
		{
		 if (snd_mixer_selem_has_playback_channel(elem,(snd_mixer_selem_channel_id_t) iChannel)) 
			if(snd_mixer_selem_set_playback_volume(elem,(snd_mixer_selem_channel_id_t)iChannel,(long)iValue)<0) return 0;
		}  
	       } // different volumes for playback and capture
	       else
	       {   
		//printf(" Capture ");                                         
		  if(iChannel==-1)
		{                                           
		if (snd_mixer_selem_has_capture_volume_joined(elem)) 
		 {
		  if(snd_mixer_selem_set_capture_volume_all(elem,(long)iValue)<0) return 0;                                                 
		 }
		else
		 {
		  if (snd_mixer_selem_is_capture_mono(elem)) 
		   if(snd_mixer_selem_set_capture_volume(elem,(snd_mixer_selem_channel_id_t)0,(long)iValue)<0) return 0;
		  else 
		   {
		    for (unsigned char channel=0; channel <= SND_MIXER_SCHN_LAST; channel++) 
		    {
			 if (snd_mixer_selem_has_capture_channel(elem,(snd_mixer_selem_channel_id_t) channel)) 
			{
			//printf("set capture volume ");
			if(snd_mixer_selem_set_capture_volume(elem,(snd_mixer_selem_channel_id_t)channel,(long)iValue)<0) return 0;
			//printf("ok\n");
			}
		     else break;
		    }
		    } 
		 }                                   
		}
	       else // only iChannell
		{
		 if (snd_mixer_selem_has_capture_channel(elem,(snd_mixer_selem_channel_id_t) iChannel)) 
			if(snd_mixer_selem_set_capture_volume(elem,(snd_mixer_selem_channel_id_t)iChannel,(long)iValue)<0) return 0;
		}	       
		  
	      }    

	 return true;
    }   
	return false;
   }
	// ----------------------------------------------
	// Return control name
	const char* GetControlName( int iDest, int iConn, int iControl )
	{
//	//printf("GetControlName %d %d %d\n",iDest,iConn, iControl);
		return iControl ? "Mute": "Volume";
	}
	// ----------------------------------------------
	// Return control selection
	bool IsActive( int iDest, int iConn, int iControl )
	{
//	printf("IsActive %d %d %d\n",iDest,iConn, iControl);
	snd_mixer_elem_t *elem=NULL;          
		elem= this->GetConnection( iDest, iConn );
		if( elem==NULL )
			return false;

		return ( snd_mixer_selem_is_active(elem)); 
	}

	// ----------------------------------------------
	// Set control selection
	bool SetActive( int iDest, int iConn, int iControl )
	{
	snd_mixer_elem_t *elem=NULL;
//	printf("SetActive %d %d %d\n",iDest,iConn, iControl);
		elem= this->GetConnection( iDest, iConn );
		if( elem== NULL )
			return false;

		return this->Refresh();
	}

	// ----------------------------------------------
	// Return control values
	bool GetControlValues( int iDest, int iConn, int iControl, int *piMin, int* piMax, int *piStep, int *piType, int* piChannels  )
	{
//printf("GetControlValues  iDest %d  iConn %d iControl %d\n",iDest,iConn,iControl);
	snd_mixer_elem_t *elem=NULL; 
		elem= this->GetConnection( iDest, iConn );
	
	 if (elem==NULL) return false;           
	 if (iDest)
	  {
	  long rmin,rmax;         
	  snd_mixer_selem_get_capture_volume_range(elem, &rmin, &rmax);
	  *piMin=rmin;
	  *piMax=rmax;
	  unsigned char channel=0;
	  for (channel=0; channel <= SND_MIXER_SCHN_LAST; channel++) 
			{
			    if (!snd_mixer_selem_has_capture_channel(elem, (snd_mixer_selem_channel_id_t)channel)) 
			    break;
			}
	      *piChannels=channel;              
	  } 
	 else
	  {
	  long vmin,vmax;                                  
	  snd_mixer_selem_get_playback_volume_range(elem, &vmin, &vmax);
	  *piMin=(int) vmin;
	  *piMax=(int) vmax;
	  unsigned char channel;
	  for (channel=0; channel <= SND_MIXER_SCHN_LAST; channel++) 
			{
			    if (!snd_mixer_selem_has_playback_channel(elem, (snd_mixer_selem_channel_id_t)channel)) 
			    break;
			}

	      *piChannels=channel;              
	  }
//	printf(" *piMin %d *piMax %d *pistep %d *piChannels %d\n ",*piMin,*piMax,*piStep,*piChannels);
		*piStep= 1;
		*piType= 0;

		return true;
	}
};
#endif


