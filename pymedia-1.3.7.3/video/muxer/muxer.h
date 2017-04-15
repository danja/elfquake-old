#ifndef _PYMUXER_H_
#define _PYMUXER_H_

#include <Python.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

extern PyObject *g_cErr;

extern PyTypeObject MuxerType ;

#define PM_ID  "id"
#define PM_TYPE  "type"
#define PM_BITRATE  "bitrate"
#define PM_WIDTH  "width"
#define PM_HEIGHT  "height"
#define PM_INDEX  "index"
#define PM_FRAME_RATE  "frame_rate"
#define PM_FRAME_RATE_B  "frame_rate_base"
#define PM_SAMPLE_RATE  "sample_rate"
#define PM_CHANNELS  "channels"
#define PM_GOP_SIZE "gop_size"
#define PM_MAX_B_FRAMES "max_b_frames"
#define PM_DURATION "length"
#define PM_EXTRA_DATA "extra_data"
#define PM_BLOCK_ALIGN "block_align"

#define AUTHOR "artist"
#define TITLE "title"
#define YEAR "year"
#define ALBUM "album"
#define TRACK "track"
#define COPYRIGHT "copyright"
#define COMMENT "comment"
#define GENRE "genre"

#define AUTHOR_U "ARTIST"
#define TITLE_U "TITLE"
#define YEAR_U "YEAR"
#define ALBUM_U "ALBUM"
#define TRACK_U "TRACK"
#define COPYRIGHT_U "COPYRIGHT"
#define COMMENT_U "COMMENT"
#define GENRE_U "GENRE"

#define RETURN_NONE return (Py_INCREF(Py_None), Py_None); 

#endif
