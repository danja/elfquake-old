#!/bin/env python

import sys, config, os
try:
  from distutils.core import setup, Extension
except:
  print 'There is a failure to open the Makefile options. Please install python-devel package before installing PyMedia'
  raise

MODULE_NAME= 'pymedia'
VERSION= '1.3.7.3'

def disable_fPIC():
  """Disable -fPIC in the c compiler command line.

  Some unix setups need -fPIC turned off to successfully compile
  video/libavcodec/i386/dsputil_mmx.c (and other files).  Distutils
  gets the flag by parsing python2.3/config/Makefile and storing the
  result in a cache.

  This function is a hack that depends on that internal distutils
  behavior, so it may someday stop working or corrupt distutils in an
  unexpected way."""

  import distutils.sysconfig
  try:
    distutils.sysconfig.get_config_vars() # parse Makefile and fill the _config_vars cache
  except:
    print 'There is a failure to open the Makefile options. Please install python-devel package before installing PyMedia'
    raise
  
  cv = distutils.sysconfig._config_vars
  for var in cv: #"CCSHARED","GCCSHARED","CFLAGS","OPT","CONFIG_ARGS":
    if type(cv[var])== str:
      cv[var] = cv[var].replace('-fPIC','')
  for var in ("CXX",):
    if var in cv:
      if type(cv[var])== str:
        cv[var] = cv[var].replace('gcc','g++')

MMX_FILES= [
			'i386/cputest.c',
			'i386/dsputil_mmx.c',
			'i386/fdct_mmx.c',
			'i386/idct_mmx.c',
			'i386/motion_est_mmx.c',
			'i386/mpegvideo_mmx.c',
			'i386/simple_idct_mmx.c',
]

NONMMX_FILES= [
			'fdctref.c',
			'fft.c',
			'mdct.c',
]


FILES={
	'audio.acodec':
	{
		'#dir': 'audio',
		'':
		(
			'acodec/acodec.c',
			'mem.c'
		),
		'libavcodec':
		(
			'utils.c',
			'wmadec.c',
			'a52dec.c',
			'liba52/bit_allocate.c',
			'liba52/bitstream.c',
			'liba52/downmix.c',
			'liba52/imdct.c',
			'liba52/parse.c',
			'liba52/crc.c',
			'faad.c',
			'oggvorbis.c',
			'common.c',
			'mpegaudiodec.c',
			'mpegaudio.c',
			'mp3lameaudio.c',
			'fft.c',
			'pcm.c',
			'mdct.c',
			'ac3enc.c',
			'flac.c',
			'adpcm.c',
		),
	},
	'muxer':
	{
		'#dir': 'video',
		'':
		(
			'muxer/demuxer.c',
			'muxer/muxer.c',
			'mem.c',
			'common.c'
		),
		'libavformat':
		(
			'integer.c',
			'utils.c',
			'cutils.c',
			'mov.c',
			'avienc.c',
			'avidec.c',
			'asf.c',
			'aviobuf.c',
			'mpegts.c',
			'mpeg.c',
			'raw.c',
			'ogg.c',
			'wav.c',
		),
	},
	'video.vcodec':
	{
		'#dir': 'video',
		'':
		(
			'vcodec/vcodec.c',
			'mem.c',
			'common.c'
		),
		'libavcodec':
		[
			'cabac.c',
			'error_resilience.c',
			'eval.c',
			'golomb.c',
			'h263.c',
			'h263dec.c',
			'h264.c',
			'imgconvert.c',
			'imgresample.c',
			'jfdctfst.c',
			'jfdctint.c',
			'jrevdct.c',
			'mjpeg.c',
			'mpeg12.c',
			'msmpeg4.c',
			'rv10.c',
			'simple_idct.c',
			'svq1.c',
			'utils.c',
			'xvmcvideo.c',
			'mpegvideo.c',
			'dsputil.c',
			'motion_est.c',
			'ratecontrol.c',
			'opts.c',
		],
	},
	'removable.cd':
	{
		'#dir': 'removable',
		'cd':
		(
			'cd.cpp',
			'dvdcd.cpp',
			'audiocd.cpp'
		),
		'cd/dvdlibs/dvdcss':
		(
			'device.c',
			'libdvdcss.c',
			'error.c',
			'css.c',
			'ioctl.c'
		),
		'cd/dvdlibs/dvdread':
		(
			'dvd_reader.c',
			'dvd_udf.c',
			'ifo_read.c',
			'md5.c',
			'nav_read.c'
		)
	},
	'audio.sound':
	{
		'#dir': 'sound',
		'': ( 'sound.cpp', 'resample.c', 'fft.cpp' )
	}
}

try:
  f= open( 'build.dat' )
  buildNum= f.readlines()[ 0 ]
  f.close()
except:
  buildNum= 1

f= open( 'build.dat', 'wt' )
f.write( str( int( buildNum )+ 1 ) )
f.close()

DEFINES= [( 'BUILD_NUM', buildNum ),]

if sys.platform == 'win32':
		print 'Using WINDOWS configuration...\n'
		dep= config.Dependency_win
		inc_hunt = ['include']
		lib_hunt = [
			'win32\\Static_Release',
			'win32\\VorbisEnc_Static_Release',
			'win32\\Vorbis_Static_Release',
			'libmp3lame\\Release',
			'libfaad\\Release',
			'VisualC\\SDL\Release']
		DEFINES+= [ ('WIN32', None ) ]
		LIBS= [ 'winmm' ]
		FILES[ 'video.vcodec' ][ 'libavcodec' ]+= NONMMX_FILES
else:
		print 'Using UNIX configuration...\n'
		disable_fPIC()
		dep= config.Dependency_unix
		inc_hunt = [ 
			'/usr/include', 
                        '/usr/include/lame', 
			'/usr/local/include', 
			'/usr/local/include/lame',]
		lib_hunt = [ '/usr/lib64', '/usr/lib', '/usr/local/lib64', '/usr/local/lib' ]
		LIBS= []
		DEFINES+= [
			('PATH_DEV_DSP', '"/dev/dsp"' ), 
			('PATH_DEV_MIXER','"/dev/mixer"' ), 
			('_FILE_OFFSET_BITS',64),
			('ACCEL_DETECT',1),	
			('HAVE_MMX', '1' ),
		] 
		if sys.platform== 'cygwin':
			DEFINES+= [	
				('SYS_CYGWIN', '1' ),
			]
			LIBS= [ 'winmm' ]
		else:
			DEFINES+= [
				('HAVE_LINUX_DVD_STRUCT', '1' ),
				('DVD_STRUCT_IN_LINUX_CDROM_H', '1' ),
			]

		FILES[ 'video.vcodec' ][ 'libavcodec' ]+= MMX_FILES

DEPS = [
		dep('OGG', 'libogg-[1-9].*', 'ogg/ogg.h', 'libogg', 'CONFIG_VORBIS').configure(inc_hunt,lib_hunt),
		dep('VORBIS', 'libvorbis-[1-9].*', 'vorbis/codec.h', 'libvorbis', 'CONFIG_VORBIS' ).configure(inc_hunt,lib_hunt),
		dep('FAAD', 'libfaad2', 'faad.h', 'libfaad', 'CONFIG_FAAD').configure(inc_hunt,lib_hunt),
		dep('MP3LAME', 'lame-3.95.*', 'lame.h', 'libmp3lame', 'CONFIG_MP3LAME').configure(inc_hunt,lib_hunt),
		dep('VORBISENC', 'libvorbis-[1-9].*','vorbis/vorbisenc.h','libvorbisenc', 'CONFIG_VORBIS').configure(inc_hunt,lib_hunt),
		dep('ALSA', 'libasound','alsa/asoundlib.h','libasound', 'CONFIG_ALSA').configure(inc_hunt,lib_hunt),
]

DEPS= filter( lambda x: x.found, DEPS )
INC_DIRS= [ x.inc_dir for x in DEPS ]+ [ os.getcwd() ]
LIB_DIRS= [ x.lib_dir for x in DEPS ]
DEFINES+= [ ( x.define, None ) for x in DEPS ]+ [ ( 'HAVE_AV_CONFIG_H', None ), ( 'UDF_CACHE', 1 ) ]
LIBS+= [ x.lib for x in DEPS ]

choice = raw_input('Continue building '+MODULE_NAME+' ? [Y,n]:')
if choice== 'n':
		print 'To start installation please run: \n\tsetup.py install and press Enter when prompted\n'
		sys.exit()

METADATA = {
		"name":             "pymedia",
		"version":          VERSION,
		"license":          "LGPL/GPL",
		"url":              "http://pymedia.org/",
		"author":           "Dmitry Borisov",
		"author_email":     "dborisov@pymedia.org",
		"description":      "Pymedia library for multimedia easy experience"
}

MODULE= 'pymedia' #+ VERSION.replace( '.', '' )

PACKAGEDATA = {
				"packages":    [MODULE,MODULE+'.audio',MODULE+'.video', MODULE+'.video.ext_codecs', MODULE+'.removable', 'pymedia'],
				"package_dir": 
					{MODULE: 'inst_lib',MODULE+'.audio': 'inst_lib/audio',MODULE+'.video.ext_codecs': 'inst_lib/video/ext_codecs',MODULE+'.video': 'inst_lib/video',MODULE+'.removable': 'inst_lib/removable'},
				"ext_modules": config.extensions( MODULE, FILES, INC_DIRS, LIB_DIRS, DEFINES, LIBS )
}

PACKAGEDATA.update(METADATA)
apply(setup, [], PACKAGEDATA)
