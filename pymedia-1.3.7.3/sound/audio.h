/* 
 * Audio 'LIB' defines
 */

#define AUDIO_OUT_HEADPHONES       0x01
#define AUDIO_OUT_INTERNAL_SPEAKER 0x02
#define AUDIO_OUT_LINE_OUT         0x04

enum { DECODE_TEST, DECODE_AUDIO, DECODE_FILE, DECODE_BUFFER, DECODE_WAV,
	DECODE_AU,DECODE_CDR,DECODE_AUDIOFILE };

#define AUDIO_FORMAT_MASK	  0x100
#define AUDIO_FORMAT_16		  0x100
#define AUDIO_FORMAT_8		  0x000

#define AUDIO_FORMAT_SIGNED_16    0x110
#define AUDIO_FORMAT_UNSIGNED_16  0x120
#define AUDIO_FORMAT_UNSIGNED_8   0x1
#define AUDIO_FORMAT_SIGNED_8     0x2
#define AUDIO_FORMAT_ULAW_8       0x4
#define AUDIO_FORMAT_ALAW_8       0x8

/* 3% rate tolerance */
#define AUDIO_RATE_TOLERANCE	  3

#include <stdio.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#endif

#define AUDIO_USES_FD

struct audio_info_struct
{
	int iMute;
#ifdef AUDIO_USES_FD
  int fn; /* filenumber */
#endif
#ifdef SGI
  ALconfig config;
  ALport port;
#endif
  long rate;
  long gain;
  int output;
#ifdef ALSA
  void *handle;
  snd_pcm_format_t alsa_format;
#endif
  char *device;
  int channels;
  int format;
  int private1;
  void *private2;
	void (*onComplete)(struct audio_info_struct *);
#ifdef WIN32
	HWAVEOUT dev;
#endif
};

struct audio_name {
  int  val;
  char *name;
  char *sname;
};

extern int audio_capabilities(struct audio_info_struct *);
extern void audio_fit_capabilities(struct audio_info_struct *ai,int c,int r);

extern int audio_play_samples(struct audio_info_struct *,unsigned char *,int);
extern int audio_open(struct audio_info_struct *);
extern int audio_reset_parameters(struct audio_info_struct *);
extern int audio_rate_best_match(struct audio_info_struct *ai);
extern int audio_set_rate(struct audio_info_struct *);
extern int audio_set_format(struct audio_info_struct *);
extern int audio_get_formats(struct audio_info_struct *);
extern int audio_set_channels(struct audio_info_struct *);
extern int audio_write_sample(struct audio_info_struct *,short *,int);
extern int audio_close(struct audio_info_struct *);
extern void audio_info_struct_init(struct audio_info_struct *);
extern void audio_queueflush(struct audio_info_struct *ai);
extern int audio_pause(struct audio_info_struct *ai );
extern int audio_unpause(struct audio_info_struct *ai );
extern int audio_stop(struct audio_info_struct *ai );
extern int audio_get_position( struct audio_info_struct *ai );
extern int audio_get_volume(struct audio_info_struct *ai );
extern int audio_set_volume(struct audio_info_struct *ai, int iVolume );

extern const long FREQS[9];