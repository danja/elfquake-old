"""Config on Windows/Unix """

import os, sys, string
from glob import glob
from distutils.core import setup, Extension
from distutils.sysconfig import get_python_inc

huntpaths = ['..', '..\\..', '..\\*', '..\\..\\*']

class Dependency_win:
    def __init__(self, name, checkhead, checkinc, lib, define):
        self.name = name
        self.wildcard = checkhead
        self.paths = []
        self.path = None
        self.inc_dir = None
        self.lib_dir = None
        self.lib = lib
        self.found = 0
        self.cflags = ''
        self.define= define
        self.checkinc=checkinc
                 
    def hunt(self):
        parent = os.path.abspath('..')
        for p in huntpaths:
            found = glob(os.path.join(p, self.wildcard))
            found.sort() or found.reverse()  #reverse sort
            for f in found:
                if f[:5] == '..'+os.sep+'..' and os.path.abspath(f)[:len(parent)] == parent:
                    continue
                if os.path.isdir(f):
                    self.paths.append(f)

    def choosepath(self):
        if not self.paths:
            print 'Path for ', self.name, 'not found.'
        elif len(self.paths) == 1:
            self.path = self.paths[0]
            print 'Path for '+self.name+':', self.path
        else:
            print 'Select path for '+self.name+':'
            for i in range(len(self.paths)):
                print '  ', i+1, '=', self.paths[i]
            print '  ', 0, '= <Nothing>'
            choice = raw_input('Select 0-'+`len(self.paths)`+' (1=default):')
            if not choice: choice = 1
            else: choice = int(choice)
            if(choice):
                self.path = self.paths[choice-1]

    def findhunt(self, base, paths, file):
        for h in paths:
            hh = os.path.join(base, h)
            if os.path.isdir(hh):
            		if os.path.isfile( os.path.join( hh, file )):
                		return hh.replace('\\', '/')
        return base.replace('\\', '/')

    def configure(self, inc_hunt, lib_hunt):
        self.hunt()
        self.choosepath()
        if self.path:
            self.found = 1
            self.inc_dir = self.findhunt(self.path, inc_hunt, self.checkinc)
            self.lib_dir = self.findhunt(self.path, lib_hunt, self.lib+ '.lib')
        
        return self

class Dependency_unix:
    def __init__(self, name, checkhead, checkinc, lib, define):
        self.name = name
        self.inc_dir = None
        self.lib_dir = None
        self.lib = lib
        self.found = 0
        self.checklib = lib
        self.checkinc = checkinc
        self.define= define
    
    def configure(self, incdirs, libdirs):
        incname = self.checkinc
        libname = self.checklib
        
        for dir in incdirs:
            path = os.path.join(dir, incname)
            if os.path.isfile(path):
                self.inc_dir = dir
                break
        for dir in libdirs:
            path1 = os.path.join(dir, libname+ '.a')
	    path2 = os.path.join(dir, libname+ '.so')
            if os.path.isfile(path1):
                self.lib_dir = dir
                self.lib= self.lib[ 3: ]
                break
	    if os.path.isfile(path2):
                self.lib_dir = dir
                self.lib= self.lib[ 3: ]
                break
		
                
        if self.lib_dir and self.inc_dir:
            print self.name + '             '[len(self.name):] + ': found'
            if self.inc_dir in ( '/usr/include', '/usr/local/include' ):
			    self.inc_dir= None
            self.found = 1
        else:
            print self.name + '             '[len(self.name):] + ': not found'
        
        return self
 
 
def extensions( MODULE, FILES, INC_DIRS, LIB_DIRS, DEFINES, LIBS ):
    res= []
    for ext in FILES:
        ext_files= []
        files= FILES[ ext ]
        d= files[ '#dir' ]
        for fSet in files:
            if fSet!= '#dir':
                ext_files+= [ os.path.join( d, fSet, x ) for x in files[ fSet ] ]
        
        res.append( 
            Extension( 
                MODULE+ '.'+ ext,
                ext_files,
                include_dirs= INC_DIRS+ [ d+ '/' ],
                library_dirs= LIB_DIRS,
                libraries= LIBS,
                define_macros= DEFINES
            )
        )

    return res
