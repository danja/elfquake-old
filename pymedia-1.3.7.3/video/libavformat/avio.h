#ifndef AVIO_H
#define AVIO_H

#include "patch.h"
 
/* output byte stream handling */

typedef INT64 offset_t;

/* unbuffered I/O */

typedef struct {
    unsigned char *buffer;
    int buffer_size;
    unsigned char *buf_ptr, *buf_end;
    void *opaque;
    int (*read_packet)(void *opaque, UINT8 *buf, int buf_size);
    void (*write_packet)(void *opaque, UINT8 *buf, int buf_size);
    int (*seek)(void *opaque, offset_t offset, int whence);
    offset_t pos; /* position in the file of the current buffer */
    int must_flush; /* true if the next seek should flush */
    int eof_reached; /* true if eof reached */
    int write_flag;  /* true if open for writing */
    int is_streamed;
    int max_packet_size;
    OutputBuf out_buf; 
} ByteIOContext;

int init_put_byte(ByteIOContext *s);
void put_byte(ByteIOContext *s, int b);
void put_buffer(ByteIOContext *s, const unsigned char *buf, int size);
void put_le64(ByteIOContext *s, UINT64 val);
void put_be64(ByteIOContext *s, UINT64 val);
void put_le32(ByteIOContext *s, unsigned int val);
void put_be32(ByteIOContext *s, unsigned int val);
void put_le16(ByteIOContext *s, unsigned int val);
void put_be16(ByteIOContext *s, unsigned int val);
void put_tag(ByteIOContext *s, const char *tag);

void put_be64_double(ByteIOContext *s, double val);
void put_strz(ByteIOContext *s, const char *buf);

offset_t url_fseek(ByteIOContext *s, offset_t offset, int whence);
offset_t url_fskip(ByteIOContext *s, offset_t offset);
offset_t url_ftell(ByteIOContext *s);
int url_feof(ByteIOContext *s);

void put_flush_packet(ByteIOContext *s);

int get_buffer(ByteIOContext *s, unsigned char *buf, int size);
int get_byte(ByteIOContext *s);
unsigned int get_le32(ByteIOContext *s);
UINT64 get_le64(ByteIOContext *s);
unsigned int get_le16(ByteIOContext *s);

double get_be64_double(ByteIOContext *s);
char *get_strz(ByteIOContext *s, char *buf, int maxlen);
unsigned int get_be16(ByteIOContext *s);
unsigned int get_be32(ByteIOContext *s);
UINT64 get_be64(ByteIOContext *s);
int get_mem_buffer_size( ByteIOContext* stBuf );
char *get_str(ByteIOContext *s, char *buf, int maxlen);
static inline int url_is_streamed(ByteIOContext *s)
{
    return s->is_streamed;
}

#endif

