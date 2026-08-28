/* ---------------------------------------------------------------- */
//
// Guider camera library
// Christoph C. Birk (birk@carnegiescience.edu)
// 2016-10-25  Andor
// 2018-05-14  TCP/IP version
// 2021-03-12  ZWO
//
/* ---------------------------------------------------------------- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include "zwogcam.h"
#include "zwotcp.h"
#include "tcpip.h"
#include "ptlib.h"
#include "utils.h"
#include "random.h"

/* ---------------------------------------------------------------- */

#define DEBUG           0
#define TIME_TEST       0

#ifndef PREFUN
#define PREFUN          __func__
#endif

#define SQR(x)         ((x)*(x))

int    sim_star=1,sim_slit=4;          /* v0406 slitWidth=7 */
int    sim_cx,sim_cy;                  /* v0408 */
int    sim_cx2,sim_cy2;                /* v0416 */
double sim_peak=250.0;
double sim_sig2=0.70*18.9*18.9/2.35482;
double sim_north=0.0,sim_angle=225.0,sim_radius=636.396;   /* v0421 */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
 
ZwoStruct* zwo_create(const char* host,int port)
{
  int i;

  ZwoStruct *self = (ZwoStruct*)malloc(sizeof(ZwoStruct));
  if (!self) return NULL;

  strcpy(self->host,host);
  self->port = port;
  self->handle = -1;                   /* connection closed */
  self->err = 0;                       /* used from threads */
  self->serverVersion[0] = '\0';
  self->modelName[0] = '\0';
  self->serialNumber[0] = '\0';
  self->tempSensor = 0.0f;
  self->tempSetpoint = 2.0f;
  self->coolerPercent = 0; 
  self->aoiW = self->aoiH = 0;
  self->expTime = 0.5f;
  self->gain = 0;
  self->fps = 0.0;
  self->rolling = 0;
  self->mask = NULL;

  pthread_mutex_init(&self->ioLock,NULL);
  pthread_mutex_init(&self->frameLock,NULL);

  self->seqNumber = 0;
  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *frame = &self->frames[i];
    frame->ts_ns = 0;
    frame->data = NULL;
  }
  self->tid = 0;
  self->stop_flag = 1;

  return self;
}

/* ---------------------------------------------------------------- */

static int zwo_request(ZwoStruct* self,const char* cmd,char* res,int tout)
{
  char command[128],answer[128];
  assert(strlen(cmd) < sizeof(command)+2);
  assert(self->handle >= 0);
  if (self->handle < 0) return E_NOTINIT;
  assert(pthread_mutex_trylock(&self->ioLock));  /* must fail here */

  sprintf(command,"%s\n",cmd);
  int err = TCPIP_Request3(self->handle,command,answer,sizeof(answer),tout);
  if (!err) { if (res) strcpy(res,answer); }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_connect(ZwoStruct* self)
{
  int  err=0,handle=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s:%d)\n",PREFUN,self->host,self->port);
#endif
  assert(self);
  assert(self->handle < 0);

  pthread_mutex_lock(&self->ioLock);
  if (!err) {
    handle = TCPIP_CreateClientSocket(self->host,self->port,&err);
#if (DEBUG > 0)
    printf("%s(%s:%d): handle=%d\n",PREFUN,self->host,self->port,handle);
#endif
    if (!err) self->handle = handle;
  }
  if (!err) { char buf[128]; time_t serverTime;
    err = zwo_request(self,"version",buf,5);
    if (!err) {
      assert(strlen(buf) < sizeof(self->serverVersion));
      sscanf(buf,"%s %u %ld",self->serverVersion,&self->cookie,&serverTime);
      sprintf(buf,"serverVersion= %s",self->serverVersion);
      message(self,buf,MSS_FILE);
    }
  }
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_open(ZwoStruct* self)
{
  int  err=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s:%d)\n",PREFUN,self->host,self->port);
#endif
  assert(self);

  if (self->handle < 0) err = zwo_connect(self);

  if (!err) { char buf[128];
    pthread_mutex_lock(&self->ioLock);
    err = zwo_request(self,"open",buf,5);
    if (!err) { int w,h,cooler,color,depth; char model[64]="";
      sscanf(buf,"%d %d %d %d %d %s",&w,&h,&cooler,&color,&depth,model);
      assert(strlen(model) < sizeof(self->modelName));
      strcpy(self->modelName,model);
      self->sensorW = w;
      self->sensorH = h;
    } 
    if (!err) {                        /* get serial number v0312 */
      err = zwo_request(self,"ASIGetSerialNumber",buf,2);
    }
    if (!err) {
      assert(strlen(buf) < sizeof(self->serialNumber));
      strcpy(self->serialNumber,buf);
    }
    if (!err) {
      sprintf(buf,"%s (%s)",self->modelName,self->serialNumber);
      message(self,buf,MSS_FILE | MSS_INFO);
    }
    pthread_mutex_unlock(&self->ioLock);
  }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_setup(ZwoStruct* self,int dim,int bin,int offx,int offy)
{ 
  int  err=0;
  char cmd[128],buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s,%d,%d,%d,%d)\n",PREFUN,self->modelName,dim,bin,offx,offy);
#endif
  assert(self);
  assert(self->handle >= 0);
  if (self->handle < 0) return E_NOTINIT;
  assert(self->tid == 0);
  if (self->tid) return E_RUNNING;

  pthread_mutex_lock(&self->ioLock);
  sprintf(cmd,"offtime %ld",cor_time(0));
  if (!err) err = zwo_request(self,cmd,buf,5);
#if (DEBUG > 1)
  printf("x: %d %d (%d)\n",offx+(self->sensorW/bin-dim)/2,  /* left */
                           offx+(self->sensorW/bin-dim)/2+dim, /* right */
                           self->sensorW/bin);
  printf("y: %d %d (%d)\n",offy+(self->sensorH/bin-dim)/2, /* bottom */
                           offy+(self->sensorH/bin-dim)/2+dim,  /* top */
                           self->sensorH/bin);
#endif
  sprintf(cmd,"setup %d %d %d %d %d %d",    /* 'off' v0348 */
          offx+(self->sensorW/bin-dim)/2,offy+(self->sensorH/bin-dim)/2,
          dim,dim,bin,16);
  if (!err) err = zwo_request(self,cmd,buf,5);
  if (!err) self->aoiW = self->aoiH = dim;
  sim_cx = sim_cy = dim/2;
  sim_cx2 = sim_cy2 = dim/4;
  sprintf(cmd,"exptime %f",self->expTime);
  if (!err) err = zwo_request(self,cmd,buf,5);
  if (!err) self->expTime = atof(buf); 
  pthread_mutex_unlock(&self->ioLock);
 
  self->err = err;

  return err;  
}

/* ---------------------------------------------------------------- */

int zwo_disconnect(ZwoStruct* self)
{
  int err=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif
  assert(self);

  if (self->tid) return E_RUNNING;

  pthread_mutex_lock(&self->ioLock);
  if (self->handle >= 0) err = close(self->handle);
  self->handle = -1;
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_close(ZwoStruct* self)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s %s(%p): tid=%ld\n",__FILE__,PREFUN,self,(long)self->tid);
#endif
  assert(self);

  if (self->handle < 0) return 0;
  if (self->tid) return E_RUNNING;

  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,"close",NULL,5);
  pthread_mutex_unlock(&self->ioLock);
  zwo_disconnect(self);
  message(self,"disconnected from zwoserver",MSS_FLUSH);

  return err;
}

/* ---------------------------------------------------------------- */

void zwo_free(ZwoStruct* self)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif
  assert(self);

  pthread_mutex_destroy(&self->ioLock);
  pthread_mutex_destroy(&self->frameLock);

  free((void*)self);
}

/* ---------------------------------------------------------------- */

int zwo_temperature(ZwoStruct *self,const char* value)
{
  char cmd[128],buf[128];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,value);
#endif
  assert(self->handle >= 0);

  if (value) {
    if (!strcmp(value, "fan_on")) {
      sprintf(cmd,"fancon on");
      printf("%s %s\n",value,cmd);
    } else
    if (!strcmp(value, "fan_off")) {
      sprintf(cmd,"fancon off");
      printf("%s %s\n",value,cmd);
    } else {
      sprintf(cmd,"tempcon %s",value);
    }
    if (!strcmp(value,"off") || !strcmp(value,"fan_off") || !strcmp(value,"fan_on")) {
      self->coolerPercent = 0;
    } else {
      self->tempSetpoint = (float)atof(value);
    }
  } else {
    strcpy(cmd,"tempcon");
  }
  pthread_mutex_lock(&self->ioLock); 
  int err = zwo_request(self,cmd,buf,5);  /* -40..30 [0] */
  pthread_mutex_unlock(&self->ioLock);
  if (!err) { float t,p;
    if (sscanf(buf,"%f %f",&t,&p) == 2) {
      self->tempSensor = t;
      self->coolerPercent = p;
#if 0 // debug temperature & cooler
      printf("temp=%.1f, setp=%.1f, cooler=%.0f\n",
        self->tempSensor,self->tempSetpoint,self->coolerPercent);
#endif
    }
  }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_exptime(ZwoStruct* self,double exptime)
{
  char cmd[128],buf[128];

  exptime = fmin(MAX_EXPTIME,fmax(0.01,exptime));
  sprintf(cmd,"exptime %.3f",exptime);
  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,buf,5);
  pthread_mutex_unlock(&self->ioLock);
  if (!err) self->expTime = exptime;

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_gain(ZwoStruct* self,int gain,int offset)
{
  int  err=0;
  char cmd[128],buf[128];

  pthread_mutex_lock(&self->ioLock);

  if (gain < 0)  strcpy(cmd,"gain");
  else          sprintf(cmd,"gain %d",gain); /* 0..570 [200] */
  if (!err) err = zwo_request(self,cmd,buf,5);
  if (!err) self->gain = atoi(buf);

  if (offset < 0)  strcpy(cmd,"offset"); /* 0..80 [8] */
  else            sprintf(cmd,"offset %d",offset);
  if (!err) err = zwo_request(self,cmd,buf,5);
  if (!err) self->offset = atoi(buf);

  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */

static void* run_cycle(void* param)
{
  ZwoStruct *self=(ZwoStruct*)param;
  int     i,n=0,err=0,per,last_err=0;
  u_int   seq=0;
  unsigned long long ts=0;             /* frame timestamp [ns] v1.0.6 */
  double  t1,t2,tmp=0;
  char    cmd[128],buf[256];
  u_short *roll_buf=NULL;
#if (DEBUG > 0)
  fprintf(stderr,"%s: %s(%p)\n",__FILE__,PREFUN,param);
#endif
  message(self,PREFUN,MSS_FLUSH);
#if (TIME_TEST > 1)
  double s1=0,s2=0,sn=0,sm=0,dt,last_expt=0;
  time_t last=0;
#endif
  assert(self->handle >= 0);

  t1 = walltime(0);

  int npix = self->aoiW * self->aoiH;
  int nbytes = npix * sizeof(u_short);
  u_char *data = (u_char*)malloc(nbytes);

  self->fps = 0;
  self->err = 0;
  while (!self->stop_flag) {
    msleep(5);
    sprintf(cmd,"next %.2f",fmin(self->expTime+1.0,2.0));
    pthread_mutex_lock(&self->ioLock);
    err = zwo_request(self,cmd,buf,5);
    if (err) {                         /* request failed */
      fprintf(stderr,"%s: err=%d\n",PREFUN,err);
    } else
    if (!strncmp(buf,"-E",2)) {        /* error string */
      if (strstr(buf,"-Enodata")) {
        float dt = walltime(0)-t1;
        if (dt > self->expTime+0.5f) {   
          printf("%s: %s:%d - no data %.1f\n",get_local_timestr(buf,0),
                 self->host,self->port,dt);
        }
      } else {
        // err=24: no camera connected
        printf("%s: other error '%s'\n",PREFUN,buf);
        char *p = strchr(buf,'='); 
        err = (p) ? atoi(p+1) : E_ERROR;
      }
      msleep(350);
    } else {                           /* regular response */
      ts = 0;                          /* servers before v1.0.5 omit it */
      n = sscanf(buf,"%u %lf %d %llu",&seq,&tmp,&per,&ts);
      if (n < 3) fprintf(stderr,"bad line '%s'\n",buf);
      self->tempSensor = (float)tmp;
      self->coolerPercent = (float)per;
      for (i=0,n=0; i<nbytes/42; i++) { /* transfer image data */
        ssize_t r = TCPIP_Recv(self->handle,(char*)data+n,nbytes-n,2);
        if (r <= 0) break;
        n += r; if (n >= nbytes) break;
      }
      t2 = walltime(0);
      self->fps = 0.7*self->fps + 0.3/(t2-t1);
      t1 = t2;
      if (n != nbytes) {               /* data missing */
        err = E_INCFRAME;
      } else {
#if (DEBUG > 1)
        if (seq > self->seqNumber+1) {
          sprintf(buf,"skipped %u frames",seq-self->seqNumber-1);
          message(self,buf,MSS_WINDOW);
        }
#endif
        /* NOTE: data is in the high 14-bits, shifted at rolling average */
#if 0 // TESTING
        if ((sim_north != 0) && (sim_angle == 225)) sim_angle -= sim_north;
        sim_cx2 = sim_cx+sim_radius*cos((sim_angle+sim_north)/RADS);
        sim_cy2 = sim_cy+sim_radius*sin((sim_angle+sim_north)/RADS);
#endif
        if (self->mask) { int x,y,dx,dy;      /* v0320 */
          // double c1 = walltime(0);
          char* mask = self->mask;
          u_short *udata = (u_short*)data;
          int x2=self->aoiW-1,y2=self->aoiH-1,p,q;
          for (y=1; y<y2; y++) {              /* loop over rows */
            p = 1 + y*self->aoiW;
            for (x=1; x<x2; x++,p++) {        /* loop over columns */
              if (mask[p]) { int s=0,w=0,m;   /* masked */
                for (dy=-1; dy<=1; dy++) {    /* 8 pixels around */
                  for (dx=-1; dx<=1; dx++) {
                    if ((dx) || (dy)) {       /* not center */
                      q = p + dx + dy*self->aoiW;
                      if (!mask[q]) {         /* not masked */
                        m = ((dx) && (dy)) ? 1 : 2;  /* weight */
                        s += m * (int)udata[q]; 
                        w += m;
                      }
                    } /* endif(!center) */
                  } /* endfor(dx) */
                } /* endfor(dy) */
                if (w) udata[p] = (u_short)(0.5+(double)s/(double)w);
              } /* endif(masked) */
#if 0 // TESTING -- center cross 5 pixels 
              if ((x==sim_cx)   && (y==sim_cy))   udata[p] = 0x3f00; 
              if ((x==sim_cx-1) && (y==sim_cy))   udata[p] = 0x1f00; 
              if ((x==sim_cx)   && (y==sim_cy-1)) udata[p] = 0x1f00; 
              if ((x==sim_cx+1) && (y==sim_cy))   udata[p] = 0x1f00; 
              if ((x==sim_cx)   && (y==sim_cy+1)) udata[p] = 0x1f00;
#endif
              /* flux = 2*PI*peak*sig*sig */
              /* flux = 1.133*peak*fw*fw */
#if 0 // TESTING -- gauss with slit 
              static const int ww=30;
              if ((x>=sim_cx-ww) && (x<=sim_cx+ww)) { 
                if (abs(x-self->aoiW/2) < sim_slit) continue; /* blank out slit */
                if ((y>=sim_cy-ww) && (y<=sim_cy+ww)) { 
                  double r2 = ((x-sim_cx)*(x-sim_cx)+(y-sim_cy)*(y-sim_cy));
                  int f = PRandom(sim_peak*exp(-r2/sim_sig2));
                  if (f > 0x3b00) f = 0x3b00;  /* 15104 v0411 */
                  udata[p] += (f << 2);
                }
              }
#endif
#if 0 // TESTING -- gauss without slit 
              static const int w2=30;
              if ((x>=sim_cx2-w2) && (x<=sim_cx2+w2)) { 
                if ((y>=sim_cy2-w2) && (y<=sim_cy2+w2)) { 
                  double r2 = ((x-sim_cx2)*(x-sim_cx2)+(y-sim_cy2)*(y-sim_cy2));
                  int f = PRandom(sim_peak*exp(-r2/sim_sig2));
                  if (f > 0x3b00) f = 0x3b00;  /* 15104 */
                  udata[p] += (f << 2);
                }
              }
#endif
            } /* endfor(x) */
          } /* endfor(y) */
          // double dt = (walltime(0)-c1)*1000.0;   /* [ms] */
          // printf("mask: %.2f [ms]\n",dt); /* takes about 5-10 ms */
        } /* endif(mask) */
        self->seqNumber = seq;
        ZwoFrame *frame = zwo_frame4writing(self,seq);
        if (frame) {
          frame->seqNumber = seq;
          frame->ts_ns = ts;           /* newest contributing frame v1.0.6 */
          register u_short *d=(u_short*)data;
          if (self->rolling == 0) {    /* rolling average */
            register u_short *p=frame->data;
            for (i=0; i<npix; i++,p++,d++) *p = *d >> 2; // shift data
            if (roll_buf) { free((void*)roll_buf); roll_buf=NULL; }
          } else { int mul=self->rolling; float div=1+self->rolling;
#if (TIME_TEST > 1)
            double c1 = walltime(0);
#endif
            if (!roll_buf) {
              roll_buf = (u_short*)malloc(npix*sizeof(u_short));
              register u_short *p=roll_buf;
              for (i=0; i<npix; i++,p++,d++) *p = *d >> 2;
            } else {
              register u_short *p=roll_buf;
              for (i=0; i<npix; i++,p++,d++) {
                *p = (u_short)(0.5f+(((int)*p * mul) + (int)(*d >> 2)) / div);
              }
            }
            memcpy(frame->data,roll_buf,nbytes);
#if (TIME_TEST > 1)
            if (self->expTime != last_expt) { 
              s1 = s2 = sn = sm = 0; last_expt = self->expTime;
            }
            dt = (walltime(0)-c1)*1000.0;   /* [ms] */
            s1 += dt; s2 += dt*dt; sn += 1.0; if (dt > sm) sm = dt;
            if (cor_time(0) > last) { 
              double ave = s1/sn;
              double sig = sqrt(s2/sn-ave*ave);
              sprintf(buf,"%s: ave=%.1f +- %.2f (%.1f)",PREFUN,ave,sig,sm);
              message(self,buf,MSS_FILE); printf("%s\n",buf);
              last = cor_time(0);
            }
#endif
          }
          zwo_frame_release(self,frame);
        } // endif(frame)
      } // endif(incomplete frame)
    } // endif(err)
    pthread_mutex_unlock(&self->ioLock);
    if (err) {
      if (err != last_err) {
        switch (err) {
        case E_tcpip_send:
          sprintf(buf,"sending req. to '%s' failed",self->host);
          break;
        case E_tcpip_timeout:
          /* tested failure modes and GUI responses v0419 */
          /* rPi: unplug ethernet: "no response from rPi" */
          /* rPi: unplug USB: "no data" */
          /* ZWO: unplug power: "no data" */
          sprintf(buf,"no response from '%s'",self->host);
          break;
        case E_RUNNING:  
          sprintf(buf,"%s: data-acq not running",PREFUN);
          break;
        case E_INCFRAME: 
          sprintf(buf,"incomplete frame from '%s'",self->host); 
          break;
        default:         
          sprintf(buf,"%s: err=%d",PREFUN,err); 
          break;
        }
        message(self,buf,MSS_ERROR);
        last_err = err; 
      }
      msleep(500);
    }
  } /* endwhile (!stop_flag) */

  if (roll_buf) { free((void*)roll_buf); roll_buf=NULL; }
  free((void*)data);
  self->err = err;
#if (DEBUG > 0)
  fprintf(stderr,"%s %s() done\n",__FILE__,PREFUN);
#endif

  return (void*)(long)err;
}

/* ---------------------------------------------------------------- */

int zwo_cycle_start(ZwoStruct* self)
{
  char buf[128];

  size_t size = self->aoiW * self->aoiH * sizeof(u_short);

  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,"start",buf,5);
  pthread_mutex_unlock(&self->ioLock);

  if (!err) { int i;
    self->seqNumber = 0;
    for (i=0; i<ZWO_NBUFS; i++) {
      ZwoFrame *frame = &self->frames[i];    
      frame->seqNumber = 0;
      frame->ts_ns = 0;
      assert(!frame->data);
      frame->data = (u_short*)malloc(size);
      assert(((u_long)frame->data & 0x07) == 0);
      frame->w = self->aoiW;
      frame->h = self->aoiH;
      frame->wlock = 0;
      frame->rlock = 0;
    }
  }

  if (!err) {
    self->stop_flag = 0;
    thread_create(run_cycle,(void*)self,&self->tid);
    msleep(50);
  } else {
    self->tid = 0;
  }

  return err;
}

/* ---------------------------------------------------------------- */

int zwo_cycle_stop(ZwoStruct *self)
{
  int  err=0,i;
  char buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,self);
#endif

  self->stop_flag = 1;
  pthread_join(self->tid,NULL);  
  self->tid = 0;

  pthread_mutex_lock(&self->ioLock);
  zwo_request(self,"stop",buf,5);
  pthread_mutex_unlock(&self->ioLock);

  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *f = &self->frames[i]; 
    while (f->wlock != 0) { 
      msleep(50); fprintf(stderr,"%s: f=%d, wlock=%d\n",PREFUN,i,f->wlock); 
    }
    while (f->rlock != 0) { 
      msleep(50); fprintf(stderr,"%s: f=%d, rlock=%d\n",PREFUN,i,f->rlock); 
    } 
    if (f->data) { free((void*)f->data); f->data = NULL; }
  }

  return err;
}

/* ---------------------------------------------------------------- */

ZwoFrame* zwo_frame4writing(ZwoStruct* self,u_int s)
{
  int i;
  ZwoFrame *frame=NULL;
#if (DEBUG > 2)
  fprintf(stderr,"%s(%p,%u)\n",PREFUN,self,s);
#endif

  pthread_mutex_lock(&self->frameLock);
  
  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *f = &self->frames[i];    
    assert(f->data);
    assert(f->wlock == 0);             // no frame locked for writing
    if (f->rlock == 0) {               // not locked for reading
      if (f->seqNumber < s) {          // oldest frame
        frame = f;
        s = frame->seqNumber;
      }
    }
  }
  if (!frame) { char buf[128];
    sprintf(buf,"%s: PANIC: no frame available for writing",__FILE__);
    message(self,buf,MSS_WARN | MSS_FILE);
  } else {
    frame->wlock = 1;
  }
  pthread_mutex_unlock(&self->frameLock);
        
  return frame;
}

/* ---------------------------------------------------------------- */

ZwoFrame* zwo_frame4reading(ZwoStruct* self,u_int s)
{
  int i;
  ZwoFrame *frame=NULL;

  if (self->stop_flag) return NULL;

  pthread_mutex_lock(&self->frameLock);
  
  for (i=0; i<ZWO_NBUFS; i++) {
    ZwoFrame *f = &self->frames[i];    
    assert(f->data);
    if (f->wlock == 0) {               // not locked for writing
      if (f->seqNumber > s) {          // newest frame
        frame = &self->frames[i];
        s = frame->seqNumber;
      }
    }
  }
  if (frame) {
    frame->rlock += 1;
  }
  pthread_mutex_unlock(&self->frameLock);
        
  return frame;
}


/* ---------------------------------------------------------------- */

void zwo_frame_release(ZwoStruct* self,ZwoFrame *frame)
{
  pthread_mutex_lock(&self->frameLock);

  if (frame->wlock) {                  /* is locked for writing */
    assert(frame->rlock == 0);
    assert(frame->wlock == 1);
    frame->wlock = 0;
  } else {                             /* is locked for reading */
    assert(frame->rlock >  0);
    frame->rlock -= 1;
    assert(frame->rlock >= 0);
  }
  
  pthread_mutex_unlock(&self->frameLock);
}

/* ---------------------------------------------------------------- */

int zwo_server(ZwoStruct* self,const char* cmd,char* res)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s,%s)\n",PREFUN,self,cmd,res);
#endif
  assert(self);
  if (self->handle < 0) return E_NOTINIT;

  pthread_mutex_lock(&self->ioLock);
  int err = zwo_request(self,cmd,res,5);
  pthread_mutex_unlock(&self->ioLock);

  return err;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

