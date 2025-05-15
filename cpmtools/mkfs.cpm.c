// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/* #includes */ /*{{{C}}}*//*{{{*/
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "getopt_.h"
#include "cpmfs.h"
/*}}}*/
/* #defines */ /*{{{*/
#ifndef O_BINARY
#define O_BINARY 0
#endif
/*}}}*/

const char cmd[]="mkfs.cpm";

int main(int argc, char *argv[]) /*{{{*/
{
  char *image;
  const char *format;
  int uppercase=0;
  int c,usage=0;
  struct cpmSuperBlock drive;
  struct cpmInode root;
  const char *label="unlabeled";
  int timeStamps=0;
  size_t bootTrackSize,used;
  char *bootTracks;
  const char *boot[4]={(const char*)0,(const char*)0,(const char*)0,(const char*)0};

  if (!(format=getenv("CPMTOOLSFMT"))) format=FORMAT;
  while ((c=getopt(argc,argv,"b:f:L:tuh?"))!=EOF) switch(c)
  {
    case 'b':
    {
      if (boot[0]==(const char*)0) boot[0]=optarg;
      else if (boot[1]==(const char*)0) boot[1]=optarg;
      else if (boot[2]==(const char*)0) boot[2]=optarg;
      else if (boot[3]==(const char*)0) boot[3]=optarg;
      else usage=1;
      break;
    }
    case 'f': format=optarg; break;
    case 'L': label=optarg; break;
    case 't': timeStamps=1; break;
    case 'u': uppercase=1; break;
    case 'h':
    case '?': usage=1; break;
  }

  if (optind!=(argc-1)) usage=1;
  else image=argv[optind++];

  if (usage)
  {
    fprintf(stderr,"Usage: %s [-f format] [-b boot] [-L label] [-t] [-u] image\n",cmd);
    exit(1);
  }
  drive.dev.opened=0;
  cpmReadSuper(&drive,&root,format,uppercase);
  bootTrackSize=drive.boottrk*drive.secLength*drive.sectrk;
  if ((bootTracks=malloc(bootTrackSize))==(void*)0)
  {
    fprintf(stderr,"%s: can not allocate boot track buffer: %s\n",cmd,strerror(errno));
    exit(1);
  }
  memset(bootTracks,0xe5,bootTrackSize);
  used=0; 
  for (c=0; c<4 && boot[c]; ++c)
  {
    int fd;
    size_t size;

    if ((fd=open(boot[c],O_BINARY|O_RDONLY))==-1)
    {
      fprintf(stderr,"%s: can not open %s: %s\n",cmd,boot[c],strerror(errno));
      exit(1);
    }
    size=read(fd,bootTracks+used,bootTrackSize-used);
#if 0
    fprintf(stderr,"%d %04x %s\n",c,used+0x800,boot[c]);
#endif
    if (size%drive.secLength) size=(size|(drive.secLength-1))+1;
    used+=size;
    close(fd);
  }
  if (mkfs(&drive,image,format,label,bootTracks,timeStamps,uppercase)==-1)
  {
    fprintf(stderr,"%s: can not make new file system: %s\n",cmd,boo);
    exit(1);
  }
  else exit(0);
}
/*}}}*/
