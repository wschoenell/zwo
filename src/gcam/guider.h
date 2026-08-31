/* ---------------------------------------------------------------- */
// 
// guider.h
//
/* ---------------------------------------------------------------- */

#ifndef INCLUDE_GUIDER_H
#define INCLUDE_GUIDER_H

/* ---------------------------------------------------------------- */

#include "qltool.h"
#include "graph.h"
#include "fits.h"

/* ---------------------------------------------------------------- */

typedef struct guider_tag {
  int           gnum,gmode;            /* v0313 */
  char          gmpar;                 /* v0354 */
  int           fmode;                 /* v0329 */
  char          name[32];
  FITSpars      status;
  volatile Bool loop_running,house_running;
  volatile Bool tcpip_running;
  volatile Bool stop_flag;
  volatile int  write_flag,send_flag,init_flag;
  pthread_t     gid;
  int           pamode,msmode;
  float         stored_tf1,stored_tf3; /* v0329 */
  int           stored_send,stored_av,stored_mode;
  char          lastCommand[256];
  char          command_msg[512];      /* v0333, enlarged for "status" */
  int           slitW;                 /* v0408 */
  double        north;                 /* v0419 */
  /* GUI */
  Window      win;
  QlTool      *qltool;
  GraphWindow *g_az,*g_el,*g_tc,*g_fw;
  EditWindow  tfbox;                   /* exposure time */
  EditWindow  dtbox;                   /* exposure->send countdown v0343 */
  EditWindow  fpbox,fdbox,fgbox;       /* frames per second */
  EditWindow  gdbox,tcbox,mxbox,bkbox,fwbox,dxbox,dybox;
  EditWindow  snbox,pxbox,bxbox,pabox,avbox;
  EditWindow  gmbox,gabox,fmbox,mmbox;
  EditWindow  csbox;                   /* cursor step */
  EditWindow  sqbox;                   /* sequence -> sent frame v0343 */
  EditWindow  tmbox,cpbox;             /* temperature,cooler */
  EditWindow  cmbox;                   /* command line */
  EditWindow  *msbox;                  /* message window(s) */
  Led         *led;
  EditWindow  smbox,gnbox;             /* v0322 */
  /* ZWO */
  ZwoStruct   *server;
  int         offx,offy;
  /* Guiding */
  int         shmode,q_flag;
  u_int       sendNumber;
  float       px;                      /* eff. pixscale (incl. binning) */
  double      angle,elsign,rosign,parity;
  double      parit2; // todo remove 'parit2' v0417
  double      pa,azerp,elerp,sens,azg,elg;
  pthread_mutex_t mutex;
  volatile int update_flag;
  volatile double fps,flux,ppix,back,fwhm,dx,dy;
  /* image server v1.0.6 -- independent of the legacy push below */
  int           image_port;
  double        tcs_age;               /* TCS cache lifetime [s] */
  volatile int  image_clients;
  volatile Bool image_running;
  /* setup parameters (.ini file) */
  /* --- legacy push, scheduled for removal (docs/plans/gcam-image-server.md) */
  char        send_host[128];
  int         send_port;
  /* --- end legacy push */
  int         rPort;                   /* rotator port */
  int         lmag,bx;
  int         pct,bkg,span;            /* default scaling */
  char        host[128],gain[32];
} Guider;

/* --- */

extern void* run_guider(void* param);  /* --> guider.c */

extern void  redraw_gwin(Guider*);     /* --> zwogcam.c */

/* ---------------------------------------------------------------- */

#endif /* INCLUDE_GUIDER_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

