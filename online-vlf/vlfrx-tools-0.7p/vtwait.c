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

#include "config.h"
#include "vtport.h"
#include "vtlib.h"

static void usage( void)
{
   fprintf( stderr, 
      "usage: vtwait [options] input\n"
      "\n"
      "options:\n"
      "  -v      Increase verbosity\n"
      "  -t      Wait for data to come through\n"
      "          (default is to wait for buffer creation)\n"
    );
   exit( 1);
}

int main( int argc, char *argv[])
{
   VT_init( "vtwait");

   int TFLAG = 0;

   while( 1)
   {
      int c = getopt( argc, argv, "vBt?");
      
      if( c == 'v') VT_up_loglevel();
      else
      if( c == 't') TFLAG = 1;
      else
      if( c == -1) break;
      else
         usage(); 
   }  
   
   if( optind >= argc) usage();
   char *name = strdup( argv[optind]);

   VT_parse_chanspec( name);

   VTFILE *vtfile;
   while( (vtfile = VT_open_input( name)) == NULL) usleep( 500000);

   VT_report( 1, "channels: %d, sample_rate: %d",
                   VT_get_chans( vtfile), VT_get_sample_rate( vtfile));

   if( TFLAG && !VT_get_frame( vtfile)) VT_bailout( "stream failed");
   return 0;
}

