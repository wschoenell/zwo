/* ----------------------------------------------------------------
 *
 * eds.c
 * 
 * Project: GcamZWO  (OCIW, Pasadena, CA)
 *
 * Magellan Engineering Data Stream (EDS)   
 *
 * 2023-08-17  Christoph C. Birk, birk@obs.carnegiescience.edu
 *
 * ---------------------------------------------------------------- */

#define IS_EDS_C

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1              /* debug level */
#endif

#define EDS_PORT        5603
#define EDS_TIMEOUT     2              /* open/lock [seconds] */

#define PREFUN          __func__

/* INCLUDES ------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>                    /* atoi() */
#include <string.h>                    /* strcpy() */
#include <stdio.h>                     /* fprintf() */
#include <unistd.h>                    /* sleep() */
#include <pthread.h>                   /* mutex and thread creation */
#include <assert.h>

#include "eds.h"
#include "utils.h"
#include "tcpip.h"                     /* TCP/IP lib */

/* GLOBALs -------------------------------------------------------- */


/* EXTERNs -------------------------------------------------------- */

/* none */

/* STATICs -------------------------------------------------------- */

static pthread_mutex_t eds_mutex;
static int             eds_sock=-1;
static volatile int    eds_count=0;
static u_short         eds_port=0;
static char            eds_host[128];

/* function prototype(s) */


/* ---------------------------------------------------------------- */
/* LOCAL */
/* ---------------------------------------------------------------- */

static int eds_lock(int timeout)       /* seconds */
{
#if (DEBUG > 2)
  fprintf(stderr,"%s(%d)\n",PREFUN,timeout);
#endif

  timeout *= 1000;                     /* milli-seconds */

  while (pthread_mutex_trylock(&eds_mutex)) {
    if (timeout <= 0) return E_eds_lock;
    msleep(250); timeout -= 250;
  }
  return 0;
}

/* --- */

static void eds_unlock(void)
{
#if (DEBUG > 2)
  fprintf(stderr,"%s()\n",PREFUN);
#endif
  
  pthread_mutex_unlock(&eds_mutex);
}

/* ---------------------------------------------------------------- */
 
static int eds_send(const char* line)
{
  int  err;
  char cmd[128];
#if (DEBUG > 1) && !defined(ENG_MODE)
  fprintf(stderr,"%s(%s)\n",PREFUN,line);
#endif
  assert(strlen(line) < sizeof(cmd)-2);
 
  err = eds_open(EDS_TIMEOUT); if (err) return err;
  err = eds_lock(EDS_TIMEOUT); 
  if (err) { eds_close(); return err; }
  if (eds_port) {
    sprintf(cmd,"%s\n",line);          /* append [LF] v0342 */
    int nbytes = 1+(int)strlen(cmd);   /* send trailing '\0' v0339 */
    TCPIP_Send2(eds_sock,cmd,nbytes);  /* no response from EDS */
  } else msleep(20);

  eds_unlock();
  eds_close();
 
  return err;
}
 
/* ---------------------------------------------------------------- */

static u_char xor_string(const char* buf,int p1,int p2)
{
  int p;
  u_char s=0;

  for (p=p1; p<=p2; p++) {
    s ^= (u_char)buf[p]; // printf("\t%c %02X\n",buf[p],s); 
  }
  return s;
}

/* --- */

static char* eds_wrap(char* buf,int gn,const char* msg)
{
  char c=' ';
  char rec[128];
  struct timeval tp;

  assert(strlen(msg) < 78);
  u_int len = 11+strlen(msg);
  switch (gn) {
    case 1: c = 'N'; break;
    case 2: c = 'O'; break;
    case 3: c = 'P'; break;
  }

  gettimeofday(&tp,NULL);
  int sec =  tp.tv_sec % 60;
  int min = (tp.tv_sec/60) % 60;
  int hr  = (tp.tv_sec/3600) % 24;
  int hs  = (int)((float)tp.tv_usec/10000.0f);
  sprintf(rec,"~%c200%02u%02d%02d%02d%02d%s",c,len,hr,min,sec,hs,msg);
  u_char cs = xor_string(rec,1,strlen(rec)-1);
  sprintf(buf,"%s%02X",rec,cs);

  return buf;
}

/* ---------------------------------------------------------------- */
/* GLOBAL */
/* ---------------------------------------------------------------- */

int eds_init(const char* host,int online)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s,%d)\n",PREFUN,host,online);
#endif

  pthread_mutex_init(&eds_mutex,NULL);

  eds_port = (online) ? EDS_PORT : 0;
  switch (online) {
  case 0: case 3:
    strcpy(eds_host,"localhost");
    break;
  default: 
    strcpy(eds_host,host);
    break;
  }

  return 0;
}

/* ---------------------------------------------------------------- */

int eds_open(int timeout)              /* seconds */
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d): count=%d\n",PREFUN,timeout,eds_count);
#endif

  int err = eds_lock(timeout); if (err) return err;

  if (eds_count == 0) {
    assert(eds_sock < 0);
    if (eds_port) { 
      eds_sock = TCPIP_CreateClientSocket(eds_host,eds_port,&err);
      if (eds_sock < 0) err = E_eds_open;
      if (!err) TCPIP_KeepAlive(eds_sock);  /* v0340 */
    } else msleep(50);
  }
  if (!err) eds_count++;

  eds_unlock();

  return err;
}

/* ---------------------------------------------------------------- */

void eds_close(void)
{
  int  err;

  err = eds_lock(2*EDS_TIMEOUT);

  if (eds_count > 0) eds_count--;
  if (eds_count == 0) {
    if (eds_port) {
      if (eds_sock > 0) { (void)close(eds_sock); eds_sock = -1; }
    }
  }
#if (DEBUG > 1) 
  fprintf(stderr,"%s(): count=%d\n",PREFUN,eds_count);
#endif
  if (!err) eds_unlock();
}

/* ---------------------------------------------------------------- */

static char* eds_801(char* buf,int gn,float fw,int gm,float dx,float dy,int cn)
{
  int  gf=1;
  char msg[128];

  switch (gm) {                        /* determine guider-flag */
    case 2: case 4: gf = 2; break;
    case 3: case 5: gf = 3; break;
  }
  /* v0427 update format to be compliant except for cn normally %5d */ 
  sprintf(msg,"801;%5.2f%d%5.1f%5.1f%6d ",fw,gf,dx,dy,cn); /* v0341 */

  return eds_wrap(buf,gn,msg);
}

/* --- */

int eds_send801(int gn,float fw,int gm,float dx,float dy,float cn)
{
  char buf[128];

  int err = eds_send(eds_801(buf,gn,fw,gm,dx,dy,(int)cn));
  return err;
}

/* ---------------------------------------------------------------- */

static char* eds_82i(char* buf,int gn,int cur,float x,float y)
{
  char msg[128];

  /* tenths of a pixel, 5+5 digits; the DOS cameras write 4+4, and readers
   * tell them apart by width (up to v1.1.1: "%04d%04d", whole pixels) */
  int xi = (int)my_round(10.0*x,0),yi = (int)my_round(10.0*y,0);
  xi = (xi < 0) ? 0 : (xi > 99999) ? 99999 : xi;
  yi = (yi < 0) ? 0 : (yi > 99999) ? 99999 : yi;
  sprintf(msg,"82%d;%05d%05d",cur,xi,yi);

  return eds_wrap(buf,gn,msg);
}

/* --- */

int eds_send82i(int gn,int cur,float x,float y)
{
  char buf[128];

  int err = eds_send(eds_82i(buf,gn,cur,x,y));
  return err;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

