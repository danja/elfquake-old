/*
* Copyright (C) 2001, 2002, 2003 Billy Biggs <vektor@dumbterm.net>,
*                                Håkan Hjort <d95hjort@dtek.chalmers.se>,
*                                Björn Englund <d4bjorn@dtek.chalmers.se>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at
* your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
*/

//#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include "dvdcss/dvdcss.h"
#include "dvd_udf.h"
#include "dvd_reader.h"
#include "md5.h"

#define DEFAULT_UDF_CACHE_LEVEL 1

struct dvd_reader_s {
	/* Basic information. */
	int isImageFile;
  
	/* Hack for keeping track of the css status. 
	* 0: no css, 1: perhaps (need init of keys), 2: have done init */
	int css_state;
	int css_title; /* Last title that we have called dvdinpute_title for. */
	
	/* Information required for an image file. */
	dvdcss_t fd;
	
	/* Information required for a directory path drive. */
	char *path_root;
  
	/* Filesystem cache */
	int udfcache_level; /* 0 - turned off, 1 - on */
	void *udfcache;
};

struct dvd_file_s {
	/* Basic information. */
	dvd_reader_t *dvd;
  
	/* Hack for selecting the right css title. */
	int css_title;
	
	/* Information required for an image file. */
	uint32_t lb_start;
	uint32_t seek_pos;
	
	/* Information required for a directory path drive. */
	size_t title_sizes[ 9 ];
	dvdcss_handle title_devs[ 9 ];
	
	/* Calculated at open-time, size in blocks. */
	int filesize;
};

/**
* Set the level of caching on udf
* level = 0 (no caching)
* level = 1 (caching filesystem info)
*/
int DVDUDFCacheLevel(dvd_reader_t *device, int level)
{
  struct dvd_reader_s *dev = (struct dvd_reader_s *)device;
  
  if(level > 0) {
    level = 1;
  } else if(level < 0) {
    return dev->udfcache_level;
  }
	
  dev->udfcache_level = level;
  
  return level;
}

void *GetUDFCacheHandle(dvd_reader_t *device)
{
  struct dvd_reader_s *dev = (struct dvd_reader_s *)device;
  
  return dev->udfcache;
}

void SetUDFCacheHandle(dvd_reader_t *device, void *cache)
{
  struct dvd_reader_s *dev = (struct dvd_reader_s *)device;
	
  dev->udfcache = cache;
}



/**
* Close a DVD block device file.
*/
void DVDClose( dvd_reader_t *dvd )
{
	if( dvd ) {
		if( dvd->fd ) dvdcss_close( dvd->fd );
		if( dvd->path_root ) free( dvd->path_root );
		free( dvd );
	}
}

/**
* Open a DVD block device file.
*/
dvd_reader_t *DVDOpen( const char *location )
{
	dvd_reader_t *dvd;
	dvdcss_t dev;
	
	dev= dvdcss_open((char*)location); 
	if( !dev ) {
		//fprintf( stderr, "libdvdread: Can't open %s for reading\n", location );
		return 0;
	}
	
	dvd = (dvd_reader_t *)malloc( sizeof( dvd_reader_t ) );
	if( !dvd ) 
		return NULL;
	dvd->isImageFile = 1;
	dvd->fd = dev;
	dvd->path_root = 0;
	
	dvd->udfcache_level = DEFAULT_UDF_CACHE_LEVEL;
	dvd->udfcache = NULL;
	
	/* Only if DVDCSS_METHOD = title, a bit if it's disc or if
	* DVDCSS_METHOD = key but region missmatch. Unfortunaly we
	* don't have that information. */
  
	dvd->css_state = 1; /* Need key init. */
	dvd->css_title = 0;
	
	return dvd;
}

/**
* Open an unencrypted file on a DVD image file.
*/
dvd_file_t *DVDOpenFileUDF( dvd_reader_t *dvd, char *filename )
{
	uint32_t start, len;
	dvd_file_t *dvd_file;
	
	start = UDFFindFile( dvd, filename, &len );
	if( !start ) return 0;
	
	dvd_file = (dvd_file_t *) malloc( sizeof( dvd_file_t ) );
	if( !dvd_file ) return 0;
	dvd_file->dvd = dvd;
	dvd_file->lb_start = start;
	dvd_file->seek_pos = 0;
	memset( dvd_file->title_sizes, 0, sizeof( dvd_file->title_sizes ) );
	memset( dvd_file->title_devs, 0, sizeof( dvd_file->title_devs ) );
	dvd_file->filesize = len / DVD_VIDEO_LB_LEN;
	dvd_file->title_sizes[ 0 ]= len / DVD_VIDEO_LB_LEN;
	dvd_file->title_devs[ 0 ]= dvd->fd;
	
	return dvd_file;
}


/* Loop over all titles and call dvdcss_title to crack the keys. */
static int initAllCSSKeys( dvd_reader_t *dvd )
{
	//struct timeval all_s, all_e;
	//struct timeval t_s, t_e;
	char filename[ MAX_UDF_FILE_NAME_LEN ];
	uint32_t start, len;
	int title;
	
	char *nokeys_str = getenv("DVDREAD_NOKEYS");
	if(nokeys_str != NULL)
		return 0;
	
	//fprintf( stderr, "\n" );
	//fprintf( stderr, "libdvdread: Attempting to retrieve all CSS keys\n" );
	//fprintf( stderr, "libdvdread: This can take a _long_ time, please be patient\n\n" );
	
	//gettimeofday(&all_s, NULL);
	
	for( title = 0; title < 100; title++ ) {
		//gettimeofday( &t_s, NULL );
		if( title == 0 ) {
			sprintf( filename, "/VIDEO_TS/VIDEO_TS.VOB" );
		} else {
			sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, 0 );
		}
		start = UDFFindFile( dvd, filename, &len );
		if( start != 0 && len != 0 ) {
			/* Perform CSS key cracking for this title. */
			//fprintf( stderr, "libdvdread: Get key for %s at 0x%08x\n", filename, start );
			if( dvdcss_title( dvd->fd, (int)start ) < 0 ) {
				//fprintf( stderr, "libdvdread: Error cracking CSS key for %s (0x%08x)\n", filename, start);
			}
			//gettimeofday( &t_e, NULL );
			//fprintf( stderr, "libdvdread: Elapsed time %ld\n",  
			//	(long int) t_e.tv_sec - t_s.tv_sec );
		}
		
		if( !title ) continue;
		
		//gettimeofday( &t_s, NULL );
		//sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, 1 );
		start = UDFFindFile( dvd, filename, &len );
		if( start == 0 || len == 0 ) break;
		
		/* Perform CSS key cracking for this title. */
		//fprintf( stderr, "libdvdread: Get key for %s at 0x%08x\n", 
		//	filename, start );
		if( dvdcss_title( dvd->fd, (int)start ) < 0 ) {
			//fprintf( stderr, "libdvdread: Error cracking CSS key for %s (0x%08x)!!\n", filename, start);
		}
		//gettimeofday( &t_e, NULL );
		//fprintf( stderr, "libdvdread: Elapsed time %ld\n",  
		//	(long int) t_e.tv_sec - t_s.tv_sec );
	}
	title--;
	
	//fprintf( stderr, "libdvdread: Found %d VTS's\n", title );
	//gettimeofday(&all_e, NULL);
	//fprintf( stderr, "libdvdread: Elapsed time %ld\n",  
  //	(long int) all_e.tv_sec - all_s.tv_sec );
	
	return 0;
}


/**
* Open an vob file on a DVD.
*/
static dvd_file_t *DVDOpenVOBUDF( dvd_reader_t *dvd, int title, int menu )
{
	char filename[ MAX_UDF_FILE_NAME_LEN ];
	uint32_t start, len;
	dvd_file_t *dvd_file;
	
	if( title == 0 ) {
		sprintf( filename, "/VIDEO_TS/VIDEO_TS.VOB" );
	} else {
		sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, menu ? 0 : 1 );
	}
	start = UDFFindFile( dvd, filename, &len );
	if( start == 0 ) return 0;
	
	dvd_file = (dvd_file_t *) malloc( sizeof( dvd_file_t ) );
	if( !dvd_file ) return 0;
	dvd_file->dvd = dvd;
	/*Hack*/ dvd_file->css_title = title << 1 | menu;
	dvd_file->lb_start = start;
	dvd_file->seek_pos = 0;
	memset( dvd_file->title_sizes, 0, sizeof( dvd_file->title_sizes ) );
	memset( dvd_file->title_devs, 0, sizeof( dvd_file->title_devs ) );
	dvd_file->filesize = len / DVD_VIDEO_LB_LEN;
	
	/* Calculate the complete file size for every file in the VOBS */
	if( !menu ) {
		int cur;
		
		for( cur = 2; cur < 10; cur++ ) {
			sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, cur );
			if( !UDFFindFile( dvd, filename, &len ) ) break;
			dvd_file->filesize += len / DVD_VIDEO_LB_LEN;
		}
	}
	
	if( dvd->css_state == 1 /* Need key init */ ) {
		initAllCSSKeys( dvd );
		dvd->css_state = 2;
	}
	/*    
	if( dvdinput_title( dvd_file->dvd->dev, (int)start ) < 0 ) {
	fprintf( stderr, "libdvdread: Error cracking CSS key for %s\n",
	filename );
	}
	*/
	
	return dvd_file;
}

/* Private  */
int UDFReadBlocksRaw( dvd_reader_t *device, uint32_t lb_number,
										 size_t block_count, unsigned char *data, 
										 int encrypted )
{
	int ret;
	
	ret = dvdcss_seek( device->fd, (int) lb_number, DVDCSS_SEEK_KEY );
	if( ret != (int) lb_number ) {
		//fprintf( stderr, "libdvdread: Can't seek to block %u\n", lb_number );
		return 0;
	}
	
	return dvdcss_read( device->fd, (char *) data, (int) block_count, encrypted ); 
}

/* It's required to either fail or deliver all the blocks asked for. */
int DVDReadLBUDF( dvd_reader_t *device, uint32_t lb_number,
								 size_t block_count, unsigned char *data, 
								 int encrypted )
{
  int ret;
  size_t count = block_count;
  
  while(count > 0) {
    
    ret = UDFReadBlocksRaw(device, lb_number, count, data, encrypted);
		
    if(ret <= 0) {
		/* One of the reads failed or nothing more to read, too bad.
			* We won't even bother returning the reads that went ok. */
      return ret;
    }
    
    count -= (size_t)ret;
    lb_number += (uint32_t)ret;
  }
	
  return block_count;
}

/* Reopen file by it's location */
void DVDReopen( const char *location, dvd_file_t *file )
{
	file->dvd= DVDOpen( location );
}

/* Open file by it's domain type */
dvd_file_t *DVDOpenFile( dvd_reader_t *dvd, int titlenum, dvd_read_domain_t domain )
{
	char filename[ MAX_UDF_FILE_NAME_LEN ];
	
	/* Check arguments. */
	if( dvd == NULL || titlenum < 0 )
		return NULL;
	
	switch( domain ) {
	case DVD_READ_INFO_FILE:
		if( titlenum == 0 ) {
			sprintf( filename, "/VIDEO_TS/VIDEO_TS.IFO" );
		} else {
			sprintf( filename, "/VIDEO_TS/VTS_%02i_0.IFO", titlenum );
		}
		break;
	case DVD_READ_INFO_BACKUP_FILE:
		if( titlenum == 0 ) {
			sprintf( filename, "/VIDEO_TS/VIDEO_TS.BUP" );
		} else {
			sprintf( filename, "/VIDEO_TS/VTS_%02i_0.BUP", titlenum );
		}
		break;
	case DVD_READ_MENU_VOBS:
		return DVDOpenVOBUDF( dvd, titlenum, 1 );
	case DVD_READ_TITLE_VOBS:
		if( titlenum == 0 ) return 0;
		return DVDOpenVOBUDF( dvd, titlenum, 0 );
	default:
		//fprintf( stderr, "libdvdread: Invalid domain for file open.\n" );
		return NULL;
	}
	
	return DVDOpenFileUDF( dvd, filename );
}

void DVDCloseFile( dvd_file_t *dvd_file )
{
	free( dvd_file );
}

/* This is using a single input and starting from 'dvd_file->lb_start' offset.
*
* Reads 'block_count' blocks from 'dvd_file' at block offset 'offset'
* into the buffer located at 'data' and if 'encrypted' is set
* descramble the data if it's encrypted.  Returning either an
* negative error or the number of blocks read. */
static int DVDReadBlocksUDF( dvd_file_t *dvd_file, uint32_t offset,
														size_t block_count, unsigned char *data,
														int encrypted )
{
	return UDFReadBlocksRaw( dvd_file->dvd, dvd_file->lb_start + offset,
		block_count, data, encrypted );
}

/* This is broken reading more than 2Gb at a time is ssize_t is 32-bit. */
int DVDReadBlocks( dvd_file_t *dvd_file, int offset, 
									size_t block_count, unsigned char *data )
{
	/* Check arguments. */
	if( dvd_file == NULL || offset < 0 || data == NULL )
		return -1;
	
	/* Hack, and it will still fail for multiple opens in a threaded app ! */
	if( dvd_file->dvd->css_title != dvd_file->css_title ) {
		dvd_file->dvd->css_title = dvd_file->css_title;
		/* Here each vobu has it's own dvdcss handle, so no need to update 
		else {
		dvdinput_title( dvd_file->title_devs[ 0 ], (int)dvd_file->lb_start );
	}*/
	}
	
	return DVDReadBlocksUDF( dvd_file, (unsigned int)offset, block_count, data, 1 );
}

int32_t DVDFileSeek( dvd_file_t *dvd_file, int32_t offset )
{
	/* Check arguments. */
	if( dvd_file == NULL || offset < 0 )
		return -1;
	
	dvd_file->seek_pos = (uint32_t) offset;
	return offset;
}

int DVDReadBytes( dvd_file_t *dvd_file, void *data, int byte_size )
{
	unsigned char *secbuf;
	unsigned int numsec, seek_sector, seek_byte;
	int ret;
	
	/* Check arguments. */
	if( dvd_file == NULL || data == NULL )
		return -1;
	
	seek_sector = dvd_file->seek_pos / DVD_VIDEO_LB_LEN;
	seek_byte   = dvd_file->seek_pos % DVD_VIDEO_LB_LEN;
	
	numsec = ( ( seek_byte + byte_size ) / DVD_VIDEO_LB_LEN ) +
		( ( ( seek_byte + byte_size ) % DVD_VIDEO_LB_LEN ) ? 1 : 0 );
	
	secbuf = (unsigned char *) malloc( numsec * DVD_VIDEO_LB_LEN );
	if( !secbuf ) {
		//fprintf( stderr, "libdvdread: Can't allocate memory " 
		//	"for file read!\n" );
		return 0;
	}
	
	ret = DVDReadBlocksUDF( dvd_file, seek_sector, numsec, secbuf, 0 );
	if( ret != (int) numsec ) {
		free( secbuf );
		return ret < 0 ? ret : 0;
	}
	
	memcpy( data, &(secbuf[ seek_byte ]), byte_size );
	free( secbuf );
	
	dvd_file->seek_pos += byte_size;
	return byte_size;
}

int DVDFileSize( dvd_file_t *dvd_file )
{
	/* Check arguments. */
	if( dvd_file == NULL )
		return -1;
	
	return dvd_file->filesize;
}

int DVDDiscID( dvd_reader_t *dvd, unsigned char *discid )
{
	struct md5_ctx ctx;
	int title;
	
	/* Check arguments. */
	if( dvd == NULL || discid == NULL )
		return 0;
	
    /* Go through the first 10 IFO:s, in order, 
	* and md5sum them, i.e  VIDEO_TS.IFO and VTS_0?_0.IFO */
	md5_init_ctx( &ctx );
	for( title = 0; title < 10; title++ ) {
		dvd_file_t *dvd_file = DVDOpenFile( dvd, title, DVD_READ_INFO_FILE );
		if( dvd_file != NULL ) {
			int bytes_read;
			int file_size = dvd_file->filesize * DVD_VIDEO_LB_LEN;
			char *buffer = malloc( file_size );
			
			if( buffer == NULL ) {
				//fprintf( stderr, "libdvdread: DVDDiscId, failed to "
				//	"allocate memory for file read!\n" );
				return -1;
			}
			bytes_read = DVDReadBytes( dvd_file, buffer, file_size );
			if( bytes_read != file_size ) {
				//fprintf( stderr, "libdvdread: DVDDiscId read returned %d bytes"
				//	", wanted %d\n", bytes_read, file_size );
				DVDCloseFile( dvd_file );
				return -1;
			}
			
			md5_process_bytes( buffer, file_size,  &ctx );
			
			DVDCloseFile( dvd_file );
			free( buffer );
		}
	}
	md5_finish_ctx( &ctx, discid );
	
	return 0;
}


int DVDISOVolumeInfo( dvd_reader_t *dvd,
										 char *volid, unsigned int volid_size,
										 unsigned char *volsetid, unsigned int volsetid_size )
{
  unsigned char *buffer;
  int ret;
	
  /* Check arguments. */
  if( dvd == NULL )
    return 0;
  
  if( dvd->fd == NULL ) {
    /* No block access, so no ISO... */
    return -1;
  }
  
  buffer = malloc( DVD_VIDEO_LB_LEN );
  if( buffer == NULL ) {
    fprintf( stderr, "libdvdread: DVDISOVolumeInfo, failed to "
	     "allocate memory for file read!\n" );
    return -1;
  }
	
  ret = UDFReadBlocksRaw( dvd, 16, 1, buffer, 0 );
  if( ret != 1 ) {
    fprintf( stderr, "libdvdread: DVDISOVolumeInfo, failed to "
	     "read ISO9660 Primary Volume Descriptor!\n" );
    return -1;
  }
  
  if( (volid != NULL) && (volid_size > 0) ) {
    unsigned int n;
    for(n = 0; n < 32; n++) {
      if(buffer[40+n] == 0x20) {
				break;
      }
    }
    
    if(volid_size > n+1) {
      volid_size = n+1;
    }
		
    memcpy(volid, &buffer[40], volid_size-1);
    volid[volid_size-1] = '\0';
  }
  
  if( (volsetid != NULL) && (volsetid_size > 0) ) {
    if(volsetid_size > 128) {
      volsetid_size = 128;
    }
    memcpy(volsetid, &buffer[190], volsetid_size);
  }
  return 0;
}


int DVDUDFVolumeInfo( dvd_reader_t *dvd,
										 char *volid, unsigned int volid_size,
										 unsigned char *volsetid, unsigned int volsetid_size )
{
  int ret;
  /* Check arguments. */
  if( dvd == NULL )
    return -1;
  
  if( dvd->fd == NULL ) {
    /* No block access, so no UDF VolumeSet Identifier */
    return -1;
  }
  
  if( (volid != NULL) && (volid_size > 0) ) {
    ret = UDFGetVolumeIdentifier(dvd, volid, volid_size);
    if(!ret) {
      return -1;
    }
  }
  if( (volsetid != NULL) && (volsetid_size > 0) ) {
    ret =  UDFGetVolumeSetIdentifier(dvd, volsetid, volsetid_size);
    if(!ret) {
      return -1;
    }
  }
	
  return 0;  
}
