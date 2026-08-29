/* ---------------------------------------------------------------
 *
 * fits.c
 * 
 * Project: ZwoGuider (OCIW, Pasadena, CA) 
 *
 * FITS functions
 *
 * 1999-07-01  Christoph C. Birk, birk@obs.carnegiescience.edu
 * 2001-05-22  MT-safe
 * 2002-03-19  subraster
 *     -07-25  temp_ccd[]
 * 2003-05-21  temp_str
 * 2005-01-19  LDSS3 version
 * 2005-11-01  WF4K version
 * 2008-11-07  NewCCD (WF4K+SITe2K)
 * 2011-11-18  generic load file
 * 2014-06-12  telfocus
 * 2016-10-25  Andor
 * 2021-03-12  ZWO
 * 
 * ---------------------------------------------------------------- */

#define IS_FITS_C

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           0              /* debug level */
#endif

#define PREFUN          __func__

#define KEY_LEN         8              /* length if keyword */
#define RIGHT_MAR       30             /* right margin of value */
#define SLASH_POS       39             /* start of comment field */

#define MAX_SHORT       (32767)
#define MIN_SHORT       (-32768)

/* INCLUDES ------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include <sys/socket.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL    0              /* BSD/macOS */
#endif

#ifdef LINUX
extern void swab(const void*,void*,size_t);
#endif

#include "fits.h"
#include "utils.h"                     /* generic utilities */

/* ---------------------------------------------------------------- */

#ifndef LINUX
static void swap2(void* ptr,int nitems)
{
  register int  i;
  register char c,*p=(char*)ptr;

  for (i=0; i<nitems; i++,p+=2) {
    c = p[0]; p[0] = p[1]; p[1] = c;
  }
}
#endif

/* ---------------------------------------------------------------- */
 
static char* fitsg(char **line,int id,const char* number,const char *comment)
{
  strcpy(line[id],fitskeys[id].keyword);
  extend(line[id],' ',KEY_LEN);        /* fill keyword */
  strcat(line[id],"= ");
  extend(line[id],' ',RIGHT_MAR-strlen(number));  /* right margin */
  strcat(line[id],number);
  if (comment) {
    extend(line[id],' ',SLASH_POS);    /* append comment */
    strcat(line[id],"/ ");
    strcat(line[id],comment);
  }
  return(line[id]);
}

/* ---------------------------------------------------------------- */

static char* fitsd(char **line,int id,double value,int dp,const char *comment)
{
  char number[32];
 
  sprintf(number,"%.*f",dp,value);     /* create number string */
  fitsg(line,id,number,comment);

  return(line[id]);
}
    
/* ---------------------------------------------------------------- */

#if 0 // unused
static char* fitse(char **line,int id,double value,int dp,const char *comment)
{
  char number[32];

  sprintf(number,"%.*E",dp,value);
  fitsg(line,id,number,comment);

  return(line[id]);
}
#endif

/* ---------------------------------------------------------------- */
 
static char* fitsf(char **line,int id,float value,int dp,const char *comment)
{
  char number[32];
 
  sprintf(number,"%.*f",dp,value);     /* create number string */
  fitsg(line,id,number,comment);

  return(line[id]);
}

/* ---------------------------------------------------------------- */
 
static char* fitsi(char **line,int id,int value,const char *comment)
{
  char number[32];

  sprintf(number,"%d",value);          /* create number string */
  fitsg(line,id,number,comment);

  return(line[id]);
}

/* ---------------------------------------------------------------- */

static char* fitsll(char **line,int id,unsigned long long value,
                    const char *comment)         /* 64-bit v1.0.6 */
{
  char number[32];

  sprintf(number,"%llu",value);        /* ns timestamps exceed 'int' */
  fitsg(line,id,number,comment);

  return(line[id]);
}

/* ---------------------------------------------------------------- */
 
static char* fitss(char **line,int id,char *text,const char *comment)
{
  char buffer[1024];
 
  strcpy(line[id],fitskeys[id].keyword);
  extend(line[id],' ',KEY_LEN);
  strcat(line[id],"= ");
  if (*text != '\0') {
    sprintf(buffer,"'%s'",text);
    strcat(line[id],buffer);
  }
  if (comment) {
    extend(line[id],' ',SLASH_POS);
    strcat(line[id],"/ ");
    strcat(line[id],comment);
  }
  return(line[id]);
}
 
/* ---------------------------------------------------------------- */ 

char** fits_setup(char** fits,FITSpars* st)
{
  int       i;
  char      buf[128],com[128];
  struct tm *ltime,*gtime,res;
#if (DEBUG > 0)  
  static int fitskeys_tested=0;
#endif
#if (DEBUG > 1)
  fprintf(stderr,"%s(), fitskeys_tested=%d\n",PREFUN,fitskeys_tested);
#endif

  /* fitskeys[] structure ok ? ------------------------------------ */
#if (DEBUG > 0) 
  if (!fitskeys_tested) {              /* not yet tested */
    for (i=0; i<=F_FITS; i++) {        /* test consitency */
      if (i != fitskeys[i].id) {
        fprintf(stderr,"fitskeys[] broken at line=%d, keyword=%s,"\
                       " id=%d\nplease consult your local software guru\n",
          i,fitskeys[i].keyword,fitskeys[i].id);
        return(NULL);
      }
    } /* endfor(i) */
    fitskeys_tested = 1;
  }
#endif
 
  /* allocate fits[] strings -------------------------------------- */
 
  fits[0] = (char*)malloc(FITSLINES*(FITSRECORD+1)*sizeof(char));
  if (fits[0] == NULL) return(NULL);
  for (i=1; i<FITSLINES; i++) {        /* assign FITS header address */
    fits[i] = fits[i-1]+(FITSRECORD+1);
  }
 
  /* FITS header template ----------------------------------------- */
 
  /*                       123456789012345678901234567890           */
  strcpy(fits[F_SIMPLE  ],"SIMPLE  =                    T");
 
  fitsi(fits,F_BITPIX,16,NULL);
  fitsi(fits,F_NAXIS,2,NULL);
  fitsi(fits,F_NAXIS1,st->dimx,NULL);
  fitsi(fits,F_NAXIS2,st->dimy,NULL);
  fitsd(fits,F_BSCALE,1.0,8,"real=bzero+bscale*value");
  fitsd(fits,F_BZERO,(double)st->bzero,8,NULL);
  fitss(fits,F_BUNIT,"DU/PIXEL",NULL);

  fitss(fits,F_ORIGIN,st->origin,NULL);

  fitss(fits,F_INSTRUMENT,st->instrument,st->serial);
  fitsd(fits,F_SCALE,st->pixscale,4,"arcsec/pixel");
  fitsd(fits,F_PSIZE,st->psize,2,"microns");

  time_t ut = (time_t)my_round(st->start_int,0);
  gtime = gmtime_r(&ut,&res);        /* GMT */
  strftime(buf,sizeof(buf),"%Y-%m-%d",gtime);      /* UT date */
  fitss(fits,F_UT_DATE,buf,"UT date (start)");
  fitss(fits,F_DATE_OBS,buf,"UT date (start)");

  strftime(buf,sizeof(buf),"%H:%M:%S",gtime);      /* UT time */
  sprintf(com,"UT time (start) %d seconds",get_uts(ut));
  fitss(fits,F_UT_TIME,buf,com);

  ltime = localtime_r(&ut,&res);                   /* local time */
  strftime(buf,sizeof(buf),"%H:%M:%S",ltime);
  sprintf(com,"local time (start) %d seconds",ltime->tm_hour*3600 +
          ltime->tm_min*60 + ltime->tm_sec);
  fitss(fits,F_LC_TIME,buf,com);
 
  ut = (time_t)my_round(st->stop_int,0);
  gtime = gmtime_r(&ut,&res);         /* GMT */
  strftime(buf,sizeof(buf),"%H:%M:%S",gtime);      /* UT end */
  sprintf(com,"UT time (end) %d seconds",get_uts(ut));
  fitss(fits,F_UT_END,buf,com);

  double ep = get_epoch(ut);                       /* epoch */
  fitsd(fits,F_EPOCH,ep,5,"epoch (start)");

  fitss(fits,F_ALPHA,alpha_str(buf,st->alpha),NULL); 
  fitss(fits,F_DELTA,delta_str(buf,st->delta),NULL); 
  fitsd(fits,F_EQUINOX,st->equinox,5,NULL);
  fitss(fits,F_ST,st->st_str,NULL);
  fitss(fits,F_HA,st->ha_str,NULL);
  fitsd(fits,F_ZD,st->zd,4,NULL);
  fitsd(fits,F_AIRMASS,st->airmass,3,NULL);
  fitsd(fits,F_TELFOCUS,st->telfocus,1,NULL);
  fitsd(fits,F_ROTANGLE,st->rotangle,1,NULL);
  fitsd(fits,F_ROTATORE,st->rotatore,1,NULL);
  fitsd(fits,F_GUIDERX1,st->guiderx1,3,NULL);
  fitsd(fits,F_GUIDERY1,st->guidery1,3,NULL);
  fitsd(fits,F_GUIDERX2,st->guiderx2,3,NULL);
  fitsd(fits,F_GUIDERY2,st->guidery2,3,NULL);
  fitss(fits,F_TELGUIDE,st->tg_str,NULL);

  fitsi(fits,F_SEQN,st->seqNumber,"frame sequence number");
 
  fitss(fits,F_FILENAME,st->filename,NULL);

  sprintf(buf,"%dx%d",st->binning,st->binning);
  fitss(fits,F_BINNING,buf,"binning");

  fitsi(fits,F_MASK,1,"fixed");        /* v0316 */
  fitsi(fits,F_SH,st->shmode,"$(sh #)");
  fitsf(fits,F_CA,st->ca,1,"$(-a #) + 180");
  fitsi(fits,F_SH,st->shmode,"$(sh #)");
  fitsi(fits,F_CAMERA,st->camera,"");  /* v0317 */
  fitsi(fits,F_FRAME,st->frame,"send frame number");
  fitsi(fits,F_ROTN,st->rotn,"rotator port");
  fitsi(fits,F_GAIN,st->gain,"gain");

  /* guider state -- mirrors the 'status' command v1.0.6 */
  fitsi(fits,F_GDINIT,st->gd.init,"camera initialized");
  fitsi(fits,F_GDLOOP,st->gd.loop,"acquisition loop running");
  fitsi(fits,F_GDGUIDE,st->gd.guiding,"0=off, 2..5=mode, <0=first pass");
  fitsi(fits,F_GDMODE,st->gd.mode,"$(gm #) guide mode");
  fitsi(fits,F_GDFMODE,st->gd.fmode,"$(fm #) function mode");
  fitsi(fits,F_GDMMODE,st->gd.mmode,"$(mm #) mouse mode");
  fitsi(fits,F_GDAVG,st->gd.av,"$(av #) frames in rolling average");
  fitsi(fits,F_CCDOFFS,st->gd.offset,"CCD offset");
  fitsi(fits,F_GDSEND,st->gd.send,"$(send #) send flag");
  fitsf(fits,F_TEMPSET,st->gd.setp,1,"cooler setpoint [C]");
  fitsf(fits,F_COOLER,st->gd.cooler,0,"cooler power [%]");
  fitsf(fits,F_CAMFPS,st->gd.camfps,2,"camera frame rate");
  fitsf(fits,F_GDFPS,st->gd.fps,2,"guider loop rate");
  fitsf(fits,F_GDFWHM,st->gd.fwhm,2,"FWHM [pixels]");
  fitsf(fits,F_GDFLUX,st->gd.flux,0,"total counts");
  fitsf(fits,F_GDPEAK,st->gd.peak,0,"peak pixel");
  fitsf(fits,F_GDBACK,st->gd.back,0,"background");
  fitsf(fits,F_GDDX,st->gd.dx,2,"centroid offset x [pixels]");
  fitsf(fits,F_GDDY,st->gd.dy,2,"centroid offset y [pixels]");
  fitsf(fits,F_GDAZ,st->gd.az,3,"last az correction [arcsec]");
  fitsf(fits,F_GDEL,st->gd.el,3,"last el correction [arcsec]");
  fitsf(fits,F_GDBOXX,st->gd.boxx,1,"guide box center x [pixels]");
  fitsf(fits,F_GDBOXY,st->gd.boxy,1,"guide box center y [pixels]");
  fitsi(fits,F_GDBOXSZ,st->gd.boxsz,"$(bx #) guide box size [pixels]");
  fitsf(fits,F_GDPA,st->gd.pa,1,"position angle [deg]");
  fitsf(fits,F_GDSENS,st->gd.sens,1,"$(sn #) guider sensitivity");
  fitsll(fits,F_FRAMETS,st->ts_ns,"server frame time [ns], 0=unknown");

  fitss(fits,F_COMMENT,"","no comment");

  fitsf(fits,F_EXPTIME,st->exptime,3,"exposure time");

  fitsf(fits,F_TEMP_CCD,st->temp_ccd,1,"CCD temperature [C]");
 
  sprintf(fits[F_SOFTWARE],"SOFTWARE= 'Version %s (%s, %s)'",
    st->version,__DATE__,__TIME__);
 
  fitss(fits,F_FITS,FITS_VERSION,"FITS header version");
 
  assert(F_END < FITSLINES);
  strcpy(fits[F_END],"END");
  for (i=F_END+1; i<FITSLINES; i++) { 
    strcpy(fits[i]," "); extend(fits[i],' ',FITSRECORD);
  }
 
#if (DEBUG > 1)
  fprintf(stderr,"%s(): ... done\n",PREFUN);
#endif
  return(fits);
}
 
/* ---------------------------------------------------------------- */
 
static char* fits_create_header(char** src0)
{
  int  i,j;
  char *dst0,*dst,*src;
 
  if (src0[0] == NULL) return((char*)NULL);
  dst0 = (char*)malloc((((FITSLEN+8)/8)*8)*sizeof(char));
  if (dst0 == NULL) return (char*)NULL;
 
  for (i=0; i<FITSLINES; i++) {
    dst = dst0 + i*FITSRECORD;
    src = src0[i];
    j   = 0;
    while ((*src != '\0') && (j++ < FITSRECORD)) { /* copy content */
      *dst++ = *src++;
    }
    while (j++ < FITSRECORD) *dst++ = ' ';         /* append spaces */
  }
  *(dst0+FITSLEN) = '\0';
 
  return(dst0);
}

/* ---------------------------------------------------------------- */

int fits_send(u_short *data,FITSpars* st,int sock)
{
  int  err=0,bytes_written=FITSLEN;
  int  x,y;
  char file[1024];
  char *fits[FITSLINES],*fh;
  FILE *fp=NULL;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p,%p,%d)\n",PREFUN,data,st,sock);
#endif
 
  if (sock > 0) {                      /* write to socket */
    strcpy(st->filename,"n/a");
  } else {                             /* write to file */
    assert(MAX_FILENUM == 9999);
    sprintf(st->filename,"%s%04d",st->prefix,st->run);
    sprintf(file,"%s/%s.fits",st->datapath,st->filename);
    printf("%s\n",file);
    fp = fopen(file,"wb");
    if (!fp) return -1;
  }

  int little = (is_bigendian()) ? 0 : 1;
  int line_len = st->dimx;
  short *shortbuf = (short*)malloc(line_len*sizeof(short));
  st->bzero = -MIN_SHORT;              /* convert to (short) */

  fits_setup(fits,st);                 /* setup FITS header */
  fh = fits_create_header(fits);       /* create header block */
  if (fp) fwrite((void*)fh,sizeof(char),FITSLEN,fp);
  else    send(sock,fh,FITSLEN,MSG_NOSIGNAL);
  free((void*)fh);
  free((void*)fits[0]);

  int nlines = st->dimy;
  register u_short *db=data;
  for (y=0; y<nlines; y++) {
    register short   *sb=shortbuf;
    for (x=0; x<line_len; x++,sb++,db++) {
      *sb = (short)((int)*db + MIN_SHORT);
    }
    if (little) {                      /* little endian -> swap bytes */
#ifdef LINUX // safe in-place swap
      swab((void*)shortbuf,(void*)shortbuf,line_len*sizeof(short));
#else        // not safe on BSD
      swap2(shortbuf,line_len);
#endif
    }
    if (fp) fwrite((void*)shortbuf,sizeof(short),line_len,fp);
    else    send(sock,shortbuf,line_len*sizeof(short),MSG_NOSIGNAL);
    bytes_written += line_len*sizeof(short);
  }

  int tail = (FITSBLOCK - (bytes_written % FITSBLOCK)) % FITSBLOCK;
  if (tail) { char tailbuf[FITSBLOCK];
    (void)memset((void*)tailbuf,0,tail);
    if (fp) fwrite((void*)tailbuf,sizeof(char),tail,fp);
    else    send(sock,tailbuf,tail,MSG_NOSIGNAL);
  }

  free((void*)shortbuf);               /* free line buffer */

  if (fp) fclose(fp);
 
  return err;
}

/* ---------------------------------------------------------------- */

int fits_get_info(const char* file,int* bp,int* n1,int* n2,
                  int* xd,int* yd,double* bs,double* bz)
{
  int     i,fd,rval=0;
  int     bitpix=0,naxis1=0,naxis2=0,bl=0,os=0,endflag=0;
  double  bscale=1.0,bzero=0.0;
  char    header[FITSRECORD+1];

  if ((fd = open(file,O_RDONLY)) == -1) return 0;

  while (endflag == 0) {               /* read header blocks */
    for (i=0; i<36; i++) {             /* header block has 36 records */
      rval = read(fd,header,FITSRECORD);
      if (rval < FITSRECORD) break;
      header[FITSRECORD] = '\0';       /* terminate */
      if        (!strncmp(header,fitskeys[F_BITPIX].keyword,8)) {
        bitpix = atoi(header+10);
      } else if (!strncmp(header,fitskeys[F_NAXIS1].keyword,8)) {
        naxis1 = atoi(header+10);
      } else if (!strncmp(header,fitskeys[F_NAXIS2].keyword,8)) {
        naxis2 = atoi(header+10);
      } else if (!strncmp(header,fitskeys[F_BSCALE].keyword,8)) {
        bscale = atof(header+10);
      } else if (!strncmp(header,fitskeys[F_BZERO].keyword, 8)) {
        bzero = atof(header+10);
      } else if (!strncmp(header,"END ",4)) {    /* end of header */
        endflag = 1;
      }
    }
    if (rval < FITSRECORD) break;
  }
  close(fd);
#if (DEBUG > 0)
  fprintf(stderr,"%s(): bitpix=%d, naxis1=%d, naxis2=%d\n",PREFUN,
          bitpix,naxis1,naxis2);
#endif
  if ((bitpix != 16) && (bitpix != 32)) return 0;
  if ((naxis1==0) || (naxis2==0) || (rval < FITSRECORD)) return 0;

  *bp = bitpix;
  *n1 = naxis1;
  *n2 = naxis2;
  *xd = naxis1 - os;
  *yd = naxis2 - bl;
  *bs = bscale;
  *bz = bzero;

#if (DEBUG > 0)
  fprintf(stderr,"%s(): xdim=%d, ydim=%d\n",PREFUN,*xd,*yd);
#endif

  return 1;
}

/* ---------------------------------------------------------------- */ 
/* ---------------------------------------------------------------- */ 
/* ---------------------------------------------------------------- */ 

