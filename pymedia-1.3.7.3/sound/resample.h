#ifndef RESAMPLE_H
#define RESAMPLE_H

#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(WIN32)
#include <stdlib.h>
#include <math.h>
#define floorf(x) floor(x)
#define strcasecmp(x,y) stricmp(x,y)
#endif

struct ReSampleContext;

typedef struct ReSampleContext ReSampleContext;

typedef struct {
    /* fractional resampling */
    int incr; /* fractional increment */
    int frac;
    int last_sample;
    /* integer down sample */
    int iratio;  /* integer divison ratio */
    int icount, isum;
    int inv;
} ReSampleChannelContext;

struct ReSampleContext {
    ReSampleChannelContext channel_ctx[2];
    float ratio;
    /* channel convert */
    int input_channels, output_channels, filter_channels;
};

ReSampleContext *audio_resample_init(int output_channels, int input_channels,
                                     int output_rate, int input_rate);
int audio_resample(ReSampleContext *s, short *output, short *input, int nb_samples);
void audio_resample_close(ReSampleContext *s);


#ifdef __cplusplus
}
#endif

#endif /* RESAMPLE_H */
