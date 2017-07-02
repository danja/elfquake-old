//
//  Copyright (c) 2010 Paul Nicholson
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions
//  are met:
//  1. Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
//  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
//  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
//  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
//  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
//  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
//  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#define _GNU_SOURCE

#if !LINUX && !SOLARIS
   #define LINUX 1
#endif
#if !ALSA && !OSS
   #define ALSA 1
#endif
#if SOLARIS && ALSA
   #error ALSA not available for Solaris
#endif

#if HAVE_STDINT_H
   #include <stdint.h>
#endif

#if HAVE_STDLIB_H
   #include <stdlib.h>
#endif
#if HAVE_UNISTD_H
   #include <unistd.h>
#endif

#include <stdio.h>
#include <stdarg.h>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <complex.h>
#include <math.h>

#if HAVE_SYS_MMAN_H
   #include <sys/mman.h>
#endif


#if HAVE_NCURSES_H
   #include <ncurses.h>
#endif

#if LINUX
   #ifndef OPEN_MAX
      #define OPEN_MAX sysconf(_SC_OPEN_MAX)
   #endif
#endif
#if SOLARIS
    #define OPEN_MAX 255
#endif

#ifndef FALSE
   #define FALSE 0
#endif

#ifndef TRUE
   #define TRUE (!FALSE)
#endif

