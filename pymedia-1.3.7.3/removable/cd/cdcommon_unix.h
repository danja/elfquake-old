
/*
 *			Class to support direct cdda extraction from any valid device( unix version )
 *
 *						Copyright (C) 2002-2003  Dmitry Borisov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Dmitry Borisov
*/


#ifndef CD_COMMON_H
#define CD_COMMON_H

#include <stdio.h>
#include <fcntl.h>
#include <linux/cdrom.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#if defined(__USLC__)
#include <sys/mntent.h>
#else
#include <mntent.h>
#endif

#define CDROM_PATH "/dev/cdrom"
#define _PATH_MOUNTED	"/etc/mtab"
#define _PATH_MNTTAB	"/etc/fstab"
#define MNTTYPE_SUPER	"supermount"

#ifndef MNTTYPE_CDROM
#define MNTTYPE_CDROM	"iso9660"
#endif

#define MNTTYPE_AUTO	"auto"

#define ERRNO_TRAYEMPTY(errno)	\
	((errno == EIO)    || (errno == ENOENT) || \
	 (errno == EINVAL) || (errno == ENOMEDIUM))

// ---------------------------------------------------
/* Check a drive to see if it is a CD-ROM */
static int CheckDrive(char *drive, char *mnttype, struct stat *stbuf)
{
	int is_cd, cdfd;
	struct cdrom_subchnl info;

	/* If it doesn't exist, return -1 */
	if ( stat(drive, stbuf) < 0 )
		return(-1);

	/* If it does exist, verify that it's an available CD-ROM */
	is_cd = 0;
	if ( S_ISCHR(stbuf->st_mode) || S_ISBLK(stbuf->st_mode) )
	{
		cdfd = open(drive, (O_RDONLY|O_EXCL|O_NONBLOCK), 0);
		if ( cdfd >= 0 )
		{
			info.cdsc_format = CDROM_MSF;
			/* Under Linux, EIO occurs when a disk is not present.
			 */
			if ( (ioctl(cdfd, CDROMSUBCHNL, &info) == 0) || ERRNO_TRAYEMPTY(errno) )
				is_cd = 1;

			close(cdfd);
		}
		/* Even if we can't read it, it might be mounted */
		else if ( mnttype && (strcmp(mnttype, MNTTYPE_CDROM) == 0 || strcmp(mnttype, MNTTYPE_AUTO) == 0) )
			is_cd = 1;
	}
	return(is_cd);
}

// ---------------------------------------------------
static int CheckMounts(char sDrives[ 255 ][ 20 ], int iDrives, const char *mtab)
{
	FILE *mntfp;
	struct mntent *mntent;
	struct stat stbuf;

	mntfp = setmntent(mtab, "r");
	if ( mntfp != NULL )
	{
		char *tmp;
		char mnt_type[ 1024 ];
		char mnt_dev[ 1024 ];
		char mnt_dir[ 1024 ];

		while ( (mntent=getmntent(mntfp)) != NULL )
		{
			strcpy(mnt_type, mntent->mnt_type);
			strcpy(mnt_dev, mntent->mnt_fsname);
			strcpy(mnt_dir, mntent->mnt_dir);

			/* Handle "supermount" filesystem mounts */
			if ( strcmp(mnt_type, MNTTYPE_SUPER) == 0 )
			{
				tmp = strstr(mntent->mnt_opts, "fs=");
				if ( tmp )
				{
					strcpy( mnt_type, tmp + strlen("fs="));
					tmp = strchr(mnt_type, ',');
					if ( tmp )
						*tmp = '\0';
				}
				tmp = strstr(mntent->mnt_opts, "dev=");
				if ( tmp )
				{
					strcpy( mnt_dev, tmp + strlen("dev="));
					tmp = strchr(mnt_dev, ',');
					if ( tmp )
						*tmp = '\0';
				}
			}
			if ( strcmp(mnt_type, MNTTYPE_CDROM) == 0 || strcmp(mnt_type, MNTTYPE_AUTO) == 0 )
			{
				if (CheckDrive(mnt_dev, mnt_type, &stbuf) > 0)
				{
					// Scan all previous found pathes to find a match
					int i= 0;
					while( i< iDrives )
						if( !strcmp( &sDrives[ i ][ 0 ], mnt_dev ))
							break;
						else
							i++;

					if( i== iDrives )
					{
						strcpy( &sDrives[ iDrives ][ 0 ], mnt_dev);
						iDrives++;
					}
				}
			}
		}
		endmntent(mntfp);
	}
	return iDrives;
}

// ---------------------------------------------------
int GetDrivesList( char sDrives[ 255 ][ 20 ] )
{
	/* Check /dev/cdrom first :-) */
	struct stat stbuf;
 	int iDrives= 0;

	if (CheckDrive("/dev/cdrom", NULL, &stbuf) > 0)
	{
		strcpy( &sDrives[ iDrives ][ 0 ], CDROM_PATH );
		iDrives++;
	}

	/* Now check the currently mounted CD drives */
	iDrives= CheckMounts( sDrives, iDrives, _PATH_MOUNTED);

	/* Finally check possible mountable drives in /etc/fstab */
	iDrives= CheckMounts(sDrives, iDrives, _PATH_MNTTAB);

	return iDrives;
}

/***************************************************************************************
* Generic CD wrapper stuff
***************************************************************************************/

class CD
{
private:
  char sDevName[ 255 ];				// Device name
  char sPathName[ 1024 ];				// Path name
public:
	// ---------------------------------------------------
	CD( char* sDevName )
	{
		// Open the file
		strcpy( this->sDevName, sDevName );
		errno= 0;
	}
	// ---------------------------------------------------
	inline char* GetName()	{ return &this->sDevName[ 0 ]; }

	// ---------------------------------------------------
	char* GetPathName()
	{
		FILE *mntfp;
		struct mntent *mntent;

		this->sPathName[ 0 ]= 0;
		mntfp = setmntent(_PATH_MNTTAB, "r");
		if( mntfp )
		{
			while ( (mntent=getmntent(mntfp)) != NULL )
				if( strcmp( this->GetName(), mntent->mnt_fsname )== 0 )
				{
					strcpy( this->sPathName, mntent->mnt_dir );
					break;
				}

			endmntent(mntfp);
		}
		return &this->sPathName[ 0 ];
	}

	// ---------------------------------------------------
	void GetLabel( char* sLabel, int iLabelLen )
	{
		strcpy( sLabel, "No label" );

		int fd = open(this->sDevName, O_RDONLY);
		if (fd != -1)
		{
			int status = lseek(fd, 32808, SEEK_SET);
			if (status != -1)
				read(fd, sLabel, 32);

			// Strip the string
			for( int i= 31; i> 0; i-- )
				if( sLabel[ i ]== ' ' )
					sLabel[ i ]= 0;
				else
					break;

			close( fd );
		}
	}
	// ---------------------------------------------------
	bool IsReady()
	{
		bool bReady= false;
		int fd= open( this->sDevName, (O_RDONLY|O_NONBLOCK), 0);
		if( fd )
		{
			bReady= ( ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT) == CDS_DISC_OK );
			close( fd );
		}
		return bReady;
	}
	// ---------------------------------------------------
	int Eject()
	{
		// open cdrom
		int fd= open( this->sDevName, (O_RDONLY|O_EXCL|O_NONBLOCK), 0);
		if( fd )
		{
			// We dont care about result of execution
			ioctl( fd, CDROMEJECT, 0 );
			close( fd );
		}
		return 1;
	}
};

#endif

/*
Trivial jitter correction.
Look further to implement at the higher level

int cd_jc1(int *p1,int *p2) //nuova
	// looks for offset in p1 where can find a subset of p2
{
	int *p,n;

	p=p1+opt_ibufsize-IFRAMESIZE-1;n=0;
	while(n<IFRAMESIZE*opt_overlap && *p==*--p)n++;
	if (n>=IFRAMESIZE*opt_overlap)	// jitter correction is useless on silence
	{
		n=(opt_bufstep)*CD_FRAMESIZE_RAW;
	}
	else			// jitter correction
	{
		n=0;p=p1+opt_ibufsize-opt_keylen/sizeof(int)-1;
		while((n<IFRAMESIZE*(1+opt_overlap)) && memcmp(p,p2,opt_keylen))
		  {p--;n++;};
//		  {p-=6;n+=6;}; //should be more accurate, but doesn't work well
		if(n>=IFRAMESIZE*(1+opt_overlap)){		// no match
			return -1;
		};
		n=sizeof(int)*(p-p1);
	}
	return n;
}

int cd_jc(int *p1,int *p2)
{
	int n,d;
	n=0;
	if (opt_overlap==0) return (opt_bufstep)*CD_FRAMESIZE_RAW;
	do
		d=cd_jc1(p1,p2+n);
	while((d==-1)&&(n++<opt_ofs));n--;
	if (n<0)n=0;
	if (d==-1) return (d);
	else return (d-n*sizeof(int));
}

*/

