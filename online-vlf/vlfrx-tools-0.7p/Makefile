DESTDIR =
prefix = /usr/local
exec_prefix = ${prefix}
topdir = .
srcdir = .

libdir = /usr/local/lib
bindir = /usr/local/bin

CC = gcc
CFLAGS = -Wall -Wno-parentheses -O3 -I/usr/local/include -I/usr/X11R6/include -I/usr/local/include/libpng
LDFLAGS =  -L/usr/lib -L/usr/local/lib -L/usr/X11R6/lib
DEFS = -DHAVE_CONFIG_H
LIBS = -lpng -lfftw3 -lrt -lm 
LIBS_SOUND = -lasound 
USE_X11 = @USE_X11@
LIBS_X11 = -lforms -lXpm -lX11 
LIBS_VORBIS = -lvorbisenc -lvorbis -logg 
LIBS_SHOUT = -lshout 
LIBS_FLAC = -lFLAC 
LIBS_CURSES = -lncurses 
LIBS_RESAMPLE = -lsamplerate 
LIBS_RTLSDR = 

TARGETS = $(topdir)/vtjoin $(topdir)/vtraw $(topdir)/vtcat $(topdir)/vtwrite $(topdir)/vtread $(topdir)/vtfilter $(topdir)/vtmult $(topdir)/vtcmp $(topdir)/vtgen $(topdir)/vtsid $(topdir)/vtwait $(topdir)/vtsidex $(topdir)/vtnspec $(topdir)/vtwspec $(topdir)/vtblank $(topdir)/vtmix $(topdir)/vtpolar $(topdir)/vtvr2 $(topdir)/vttime $(topdir)/vtdate $(topdir)/vttoga $(topdir)/vtpcal $(topdir)/vtspot $(topdir)/vtrsgram $(topdir)/vtrect $(topdir)/vtain $(topdir)/vtfm $(topdir)/vtam $(topdir)/vtevent $(topdir)/vtping $(topdir)/vtmatch $(topdir)/vtsl $(topdir)/vtdata $(topdir)/vtcard $(topdir)/vtscope $(topdir)/vtspec $(topdir)/vtstat $(topdir)/vtvorbis $(topdir)/vtflac $(topdir)/vtresample
INSTALL_LIST = install_base install_vtcard install_x11 install_curses install_vorbis install_flac install_resample

HDRS = $(srcdir)/vtport.h $(srcdir)/vtlib.h

all: $(TARGETS)

$(topdir)/vtcard: $(srcdir)/vtcard.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtcard.c $(LIBS_SOUND) $(LIBS)

$(topdir)/vtjoin: $(srcdir)/vtjoin.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtjoin.c $(LIBS)

$(topdir)/vtcat: $(srcdir)/vtcat.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtcat.c $(LIBS)

$(topdir)/vtgen: $(srcdir)/vtgen.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtgen.c $(LIBS)

$(topdir)/vtcmp: $(srcdir)/vtcmp.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtcmp.c $(LIBS)

$(topdir)/vtmatch: $(srcdir)/vtmatch.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtmatch.c $(LIBS)

$(topdir)/vtwait: $(srcdir)/vtwait.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtwait.c $(LIBS)

$(topdir)/vtblank: $(srcdir)/vtblank.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtblank.c $(LIBS)

$(topdir)/vtsid: $(srcdir)/vtsid.c $(HDRS) $(srcdir)/vtlib.o $(srcdir)/vtsid.h
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtsid.c $(LIBS)

$(topdir)/vtsidex: $(srcdir)/vtsidex.c $(HDRS) $(srcdir)/vtlib.o $(srcdir)/vtsid.h
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtsidex.c $(LIBS)

$(topdir)/vtstat: $(srcdir)/vtstat.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtstat.c \
            $(LIBS_CURSES) $(LIBS)

$(topdir)/vtraw: $(srcdir)/vtraw.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtraw.c $(LIBS)

$(topdir)/vtmult: $(srcdir)/vtmult.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtmult.c $(LIBS)

$(topdir)/vtwrite: $(srcdir)/vtwrite.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtwrite.c $(LIBS)

$(topdir)/vtread: $(srcdir)/vtread.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtread.c $(LIBS)

$(topdir)/vttoga: $(srcdir)/vttoga.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vttoga.c $(LIBS)

$(topdir)/vtpcal: $(srcdir)/vtpcal.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtpcal.c $(LIBS)

$(topdir)/vtfilter: $(srcdir)/vtfilter.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtfilter.c $(LIBS)

$(topdir)/vtnspec: $(srcdir)/vtnspec.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtnspec.c $(LIBS)

$(topdir)/vtwspec: $(srcdir)/vtwspec.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtwspec.c $(LIBS)

$(topdir)/vtresample: $(srcdir)/vtresample.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ \
              $(srcdir)/vtresample.c $(LIBS_RESAMPLE) $(LIBS)

$(topdir)/vttime: $(srcdir)/vttime.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vttime.c $(LIBS)

$(topdir)/vtvorbis: $(srcdir)/vtvorbis.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtvorbis.c \
              $(LIBS_SHOUT) $(LIBS_VORBIS) $(LIBS)

$(topdir)/vtflac: $(srcdir)/vtflac.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtflac.c \
              $(LIBS_FLAC) $(LIBS)

$(topdir)/vtscope: $(srcdir)/vtscope.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtscope.c $(LIBS_X11) $(LIBS)

$(topdir)/vtspec: $(srcdir)/vtspec.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtspec.c $(LIBS_X11) $(LIBS)

$(topdir)/vtevent: $(srcdir)/vtevent.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtevent.c $(LIBS_X11) $(LIBS)

$(topdir)/vtping: $(srcdir)/vtping.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtping.c $(LIBS_X11) $(LIBS)

$(topdir)/vtrsgram: $(srcdir)/vtrsgram.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtrsgram.c $(LIBS_X11) $(LIBS)

$(topdir)/vtlib.o: $(HDRS) $(srcdir)/vtlib.c
	$(CC) -c $(srcdir)/vtlib.c $(CFLAGS) $(DEFS) -o $@

$(topdir)/vtlib.so: $(srcdir)/vtlib.o
	$(CC) -o $(CFLAGS) $(DEFS) -o $@  $(srcdir)/vtlib.o -lvtlib $(LIBS)

$(topdir)/vtmix: $(srcdir)/vtmix.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtmix.c $(LIBS)

$(topdir)/vtvr2: $(srcdir)/vtvr2.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtvr2.c $(LIBS)

$(topdir)/vtain: $(srcdir)/vtain.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtain.c $(LIBS)

$(topdir)/vtfm: $(srcdir)/vtfm.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtfm.c $(LIBS)

$(topdir)/vtam: $(srcdir)/vtam.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtam.c $(LIBS)

$(topdir)/vtpolar: $(srcdir)/vtpolar.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtpolar.c $(LIBS)

$(topdir)/vtdate: $(srcdir)/vtdate.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtdate.c $(LIBS)

$(topdir)/vtspot: $(srcdir)/vtspot.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtspot.c $(LIBS)

$(topdir)/vtrect: $(srcdir)/vtrect.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtrect.c $(LIBS)

$(topdir)/vtrtlsdr: $(srcdir)/vtrtlsdr.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ \
              $(srcdir)/vtrtlsdr.c $(LIBS_RTLSDR) $(LIBS)

$(topdir)/vtsdriq: $(srcdir)/vtsdriq.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ \
              $(srcdir)/vtsdriq.c $(LIBS)

$(topdir)/vtsl: $(srcdir)/vtsl.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtsl.c $(LIBS)

$(topdir)/vtdata: $(srcdir)/vtdata.c $(HDRS) $(srcdir)/vtlib.o
	$(CC) $(CFLAGS) vtlib.o $(LDFLAGS) $(DEFS) -o $@ $(srcdir)/vtdata.c $(LIBS)

install_base:
	install vtjoin $(bindir)
	install vtraw $(bindir)
	install vtwait $(bindir)
	install vtcat $(bindir)
	install vtcmp $(bindir)
	install vtgen $(bindir)
	install vtmult $(bindir)
	install vtwrite $(bindir)
	install vtread $(bindir)
	install vtfilter $(bindir)
	install vtps $(bindir)
	install vttop $(bindir)
	install vtplot $(bindir)
	install vtsgram $(bindir)
	install vtcardplot $(bindir)
	install vtsidplot $(bindir)
	install vttimeplot $(bindir)
	install vtsidgram $(bindir)
	install vtblank $(bindir)
	install vtsid $(bindir)
	install vtsidex $(bindir)
	install vtnspec $(bindir)
	install vtwspec $(bindir)
	install vtmix $(bindir)
	install vtvr2 $(bindir)
	install vtain $(bindir)
	install vtfm $(bindir)
	install vtam $(bindir)
	install vtpolar $(bindir)
	install vtdate $(bindir)
	install vtrsgram $(bindir)
	install vttoga $(bindir)
	install vtpcal $(bindir)
	install vtspot $(bindir)
	install vtrect $(bindir)
	install vtevent $(bindir)
	install vtping $(bindir)
	install vtmatch $(bindir)
	install vtsl $(bindir)
	install vtdata $(bindir)

install_vtcard:
	install vtcard $(bindir)

install_x11:
	install vtscope $(bindir)
	install vtspec $(bindir)

install_vorbis:
	install vtvorbis $(bindir)

install_flac:
	install vtflac $(bindir)

install_curses:
	install vtstat $(bindir)

install_resample:
	install vtresample $(bindir)
	install vttime $(bindir)

install_rtlsdr:
	install vtrtlsdr $(bindir)

install_sdriq:
	install vtsdriq $(bindir)

install: all $(INSTALL_LIST)

uninstall:
	rm -f $(bindir)/vtcard $(libdir)/vtlib.so

clean:
	rm -f $(topdir)/vtcard $(topdir)/vtjoin $(topdir)/vtstat \
              $(topdir)/vtlib.o $(topdir)/vtlib.so

distclean: clean
	rm -f $(topdir)/Makefile \
              $(topdir)/config.status \
              $(topdir)/config.log \
              $(topdir)/config.h \
              $(topdir)/config.cache

