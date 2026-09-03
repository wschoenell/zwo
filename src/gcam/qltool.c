/* ------------------------------------------------------------
 *  
 * qltool.c 
 * 
 * Project: Quick-Look Data Display
 *
 * 2011-10-07  Christoph C. Birk, birk@obs.carnegiescience.edu
 * 2012-12-06  bitmap
 *             complicated by 'lc' (number of lines) cont. readout
 * 2013-05-16  64-bit compatible
 * 2015-02-04  qltool_close()
 * 2016-11-01  Andor version
 * 2021-03-12  ZwoGuider version
 *
 * ---------------------------------------------------------------- */

#define IS_QLTOOL_C

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1              /* debug level */
#endif

#define TIME_TEST       0
#define GFIT_TEST       0

#define DAMNED_BIG      0x7f000000
#define MAX_USHORT      0xffff         /* 65535 */

#define PREFUN          __func__

/* X-DEFINEs ------------------------------------------------------ */

extern int PXh;
extern int showMagPix;

#define PXw         (10+(PXh-14)/2)
#define XXh         (PXh+4)

enum options { OPT_LOAD,OPT_D1,
               OPT_SATLEVEL,OPT_SUBRDIM,OPT_D2,
               OPT_CLEAR };

enum lookup_tables {LUT_GREY, LUT_INV, LUT_RAIN, LUT_BBDY};

enum scaling_modes { SCALE_CUTS,SCALE_MED,SCALE_MIMA,SCALE_SPAN };

/* INCLUDEs ------------------------------------------------------- */

#define _REENTRANT                     /* multi-threaded */

#include <stdlib.h>                    /* std-C stuff */
#include <stdio.h>
#include <string.h>                    /* memset(),memcpy() */
#include <math.h>                      /* sqrt(),log(),fabs() */
#include <fcntl.h>                     /* O_RDONLY */
#include <assert.h>

#include <X11/cursorfont.h>

#if (TIME_TEST > 0)                    /* cpu-time measurements */
#include <time.h>                      /* clock(),time() */
#include <limits.h>
#include <unistd.h>                    /* sysconf() */
#include <sys/times.h>
#endif

#include "qltool.h"
#include "utils.h"
#include "random.h"

/* EXTERNs -------------------------------------------------------- */

extern Application *app;

/* function prottype(s) */


/* STATICs -------------------------------------------------------- */

static Visual     *theVisual=NULL;
static int        vdepth,bdepth;
static Colormap   theCmap;
static int        numcols=0,lastcol;
static u_long     black,green,yellow,white,red,lgrey,orange,l_off=0;

/* function prototype(s) */
 
static void    plot_frame         (QlTool*);
static void    do_lupe            (QlTool*);

static void    rev_line24(u_int*,int);

static void    do_lupe24          (QlTool*);
static void    fill_pixels        (QlTool*,int,int);
static void    scale_med          (QlTool*,float,float);
static void    scale_hist         (QlTool*,int,int,int);
static void    scale_minmax       (QlTool*);
static u_short circle             (int,double,double,int);
static u_short linear             (int,double,double,int);
static void    create_image       (QlTool*,int,int,void*);

//     double  get_background     (u_short*,int,int,int,int,int,double*);
static double  get_centroid       (u_short*,int,int,int,int,int,double,
                                           double*,double*);
static void    update_cursor(QlTool*,int);

/* --- M A I N ---------------------------------------------------- */

QlTool* qltool_create(MainWindow* mw,Window parent,const char* fontname,
                      int x1,int y1,int w1,    /* main image geometry */
                      int y2,int w2,           /* lupe position,size */
                      int x3,int y3,           /* cursor block */
                      int x4,int y4,int w4,    /* scaling block */
                      int dim)                 /* data size */
{
  int     i,y,w;
  char    buf[128];
  Display *disp;
  QlTool  *qlt;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)\n",PREFUN,
          mw,fontname,x1,y1,w1,y2,w2,x3,y3,x4,y4,w4,dim);
#endif

  qlt = (QlTool*)malloc(sizeof(QlTool)); if (!qlt) return(NULL);

  qlt->disp = mw->disp;
  qlt->parent = parent;
  qlt->theImage = NULL;
  qlt->iwin = qlt->lwin = 0;
  qlt->flip_x = qlt->flip_y = 0;
  qlt->cmimage = NULL;
  qlt->thelupe = NULL;
  qlt->lupe0 = NULL;
  
  qlt->imgbuf = qlt->lupbuf = NULL;
  qlt->imgsize = qlt->lupsize = 0;

  qlt->guiding = 0;
  qlt->gmode = 0;
  qlt->vrad = 20;                      /* was 15 v0416 */
  qlt->smoothing = 1;
  qlt->nfov = 0;                       /* loadfov */

  qlt->enoise = 1.5;
  qlt->egain = 0.5;

  qlt->data_min = qlt->data_max = 0; qlt->data_dyn = 1;
  qlt->sdata = (u_short*)calloc(sizeof(u_short),dim*dim);
  pthread_mutex_init(&qlt->lock,NULL);

  qlt->dimx = qlt->maxx = dim;         /* image geometry */
  qlt->dimy = qlt->maxy = dim;

  qlt->iWIDE = qlt->iHIGH = w1;

  qltool_reset(qlt,dim,dim,0);

  qlt->cursor_mode = QLT_BOX;          /* "lupe" */
  qlt->cursor_step = 1.0f;
#if 0 // TESTING
  for (i=0; i<QLT_NCURSORS; i++) {  
    switch (i) {
      case QLT_BOX-1: qlt->curx[i] = qlt->cury[i] = dim/2; break;
      case QLT_BOX:   qlt->curx[i] = qlt->cury[i] = dim/4; break;
      default:        qlt->curx[i] = qlt->cury[i] = 0;     break;
    }
  } 
#else
  for (i=0; i<QLT_NCURSORS; i++) {  
     qlt->curx[i] = (i==QLT_BOX) ? dim/2 : 0;
     qlt->cury[i] = (i==QLT_BOX) ? dim/2 : 0; 
  } 
#endif
  qlt->cursor_dark = 0;

  /* allocate colors ---------------------------------------------- */

  CBX_Lock(0);
  disp = app->disp;
  if (!theVisual) {
    theVisual = DefaultVisual(disp,app->screen);
    vdepth = DefaultDepth(disp,app->screen);  /* bits */
    assert(vdepth > 8);                /* true color */
    bdepth  = (size_t)vdepth/8;        /* 8 bits per byte */
    if (bdepth == 3) bdepth = 4;       /* 24 bit color -> 32 bits/pixel */
#if (DEBUG > 1)
    fprintf(stderr,"%s(): visual-depth=%d\n",PREFUN,vdepth);
#endif
    theCmap = DefaultColormap(disp,app->screen);
  }
  CBX_Unlock();

  qlt->picture0 = (void*)malloc(bdepth*(qlt->iWIDE*qlt->iHIGH));

  qlt->lWIDE = w2;                   /* v0402 */
  qlt->lHIGH = w2;
  qlt->lupe0 = (void*)malloc(bdepth*qlt->lWIDE*qlt->lHIGH);

  CBX_Lock(0);
  if (!l_off) {
    black = app->black; lgrey = app->lgrey; green = app->green;
    { XColor xcol,ycol;
      (void)XAllocNamedColor(disp,app->cmap,"DarkGreen",&xcol,&ycol);
      l_off = xcol.pixel;
    }
    yellow = app->yellow; white = app->white; red = app->red;
    orange = app->orange;              /* loadfov overlay */
  }
  CBX_Unlock();

  if (!numcols) {
    numcols = QLT_NCOLORS;
    lastcol = numcols-1;               /* # of 'last' color */
  }
  fill_pixels(qlt,numcols,LUT_GREY);

  qlt->scale = SCALE_MED; qlt->scale_par1 = 3; qlt->scale_par2 = 10;
  qlt->scale = SCALE_SPAN;
  qlt->span = 100;  
  qlt->zero = 0;  
  qlt->pct = 25;                       /* {0..99 } */
  qlt->bkg = 16;                       /* {1..63} */
  qlt->val = 0; 

#ifdef ENG_MODE
  qlt->lmag = 2;
#else
  qlt->lmag = 1;                       /* default magnification v0054 */
#endif
  qlt->lut = LUT_GREY; qlt->satcol = app->red;

  qlt->satlev = 0xffff;

  CBX_Lock(0); 
  XSetWindowColormap(qlt->disp,qlt->parent,theCmap); 
  CBX_Unlock();

  qlt->gfs = CBX_CreateGfC_Ext(disp,fontname,"bold",PXh);

  /* create control/magnifier-window elements --------------------- */

  CBX_Lock(0);
  qlt->lwin = XCreateSimpleWindow(qlt->disp,qlt->parent,2 ,y2,
                                     qlt->lWIDE,qlt->lHIGH,
                                     1,app->black,app->lgrey);
  XMapRaised(qlt->disp,qlt->lwin);
  CBX_Unlock();
  CBX_SelectInput_Ext(qlt->disp,qlt->lwin,ExposureMask);

  w = (29*PXw)/2;
  for (i=0; i<QLT_NCURSORS; i++) {
    CBX_CreateAutoOutput_Ext(mw,&qlt->cubox[i],parent,x3,y3,w,XXh," 0 0 0");
    y3 += XXh+PXh/3;                    /* below "cursorStep" */
  }

  qlt->ix = x1;
  qlt->iy = y1;

  CBX_Lock(0);
  qlt->iwin = XCreateSimpleWindow(qlt->disp,qlt->parent,
                                  qlt->ix,qlt->iy,
                                  qlt->iWIDE,qlt->iHIGH,
                                  1,app->black,app->lgrey);
  XMapRaised(qlt->disp,qlt->iwin);
  CBX_Unlock();
  CBX_SelectInput_Ext(qlt->disp,qlt->iwin,ExposureMask | ButtonPressMask );
  CBX_SetCursor_Ext(qlt->disp,qlt->iwin,XC_tcross);  /* v0422 */

  y = y4;
  sprintf(buf,"pct  %5d",qlt->pct);
  CBX_CreateAutoOutput_Ext(mw,&qlt->pctbox,parent,x4,y,w4,XXh,buf);
  y += XXh+PXh/3;
  sprintf(buf,"bkg  %5d",qlt->bkg);
  CBX_CreateAutoOutput_Ext(mw,&qlt->bkgbox,parent,x4,y,w4,XXh,buf);
  y += XXh+PXh/3;
  sprintf(buf,"val  %5d",qlt->val);
  CBX_CreateAutoOutput_Ext(mw,&qlt->valbox,parent,x4,y,w4,XXh,buf);
  y += XXh+PXh/3;
  sprintf(buf,"span %5d",qlt->span);
  CBX_CreateAutoOutput_Ext(mw,&qlt->spnbox,parent,x4,y,w4,XXh,buf);

  for (i=0; i<QLT_NCURSORS; i++) update_cursor(qlt,i);

  return qlt;
}

/* ---------------------------------------------------------------- */

int qltool_event(QlTool* qlt,XEvent *event)
{
  int r=0;
#if (DEBUG > 2)
  fprintf(stderr,"%s(%s)\n",PREFUN,qlt->name);
#endif
  assert(qlt);

  switch (event->type) {
  case Expose:                         /* exposure event */
    if (event->xexpose.window == qlt->iwin) {
      if (event->xexpose.count == 0) plot_frame(qlt);
      r = 1;
    } else 
    if (event->xexpose.window == qlt->lwin) {
      if (event->xexpose.count == 0) do_lupe(qlt);
      r = 1;
    }
    break;
  }
  // if (r) fprintf(stderr,"%s(%s)\n",PREFUN,qlt->name);

  return r;
}

/* ---------------------------------------------------------------- */

static void plot_frame(QlTool* qlt)
{
  Display *disp=qlt->disp;
  GC      gc=qlt->gfs.gc;
  int     iHIGH= qlt->iHIGH;
  int     iWIDE= qlt->iWIDE;
  int     ic,ofy=0;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,qlt->theImage);
#endif

  ic = (int)(qlt->dimy/qlt->MulY);  
  assert(ic <= qlt->iHIGH);

  CBX_Lock(0);

  if (qlt->theImage) XFree((void*)qlt->theImage);
  qlt->theImage= XCreateImage(disp,theVisual,vdepth,ZPixmap,0,
                                qlt->picture0,qlt->iWIDE,ic,
                                (vdepth==24) ? 32 : vdepth,0);
  if (qlt->flip_y) ofy = iHIGH - ic;
  XPutImage(disp,qlt->iwin,gc,qlt->theImage,0,0,0,ofy,iWIDE,ic);

#if 0 // use overlay?
  XSetForeground(disp,gc,black);
  XSetLineAttributes(disp,gc,2,LineSolid,CapRound,JoinRound);
  for (i=0; i<QLT_NCURSORS-1; i++) {
    if (i==0) continue;
    x = (int)(qlt->curx[i]/MulX);
    y = (int)(qlt->cury[i]/MulY);
    if (qlt->flip_x) x = qlt->iWIDE-x-1;
    if (qlt->flip_y) y = qlt->iHIGH-y-1;
    XDrawLine(disp,qlt->iwin,gc,x-10,y,x+10,y);
    XDrawLine(disp,qlt->iwin,gc,x,y-10,x,y+10);
  }
#endif
  
#if 0 // use overlay?
  XSetLineAttributes(disp,gc,1,LineSolid,CapRound,JoinRound);
  if (qlt->guiding) XSetForeground(disp,gc,yellow);
  else if (qlt->cursor_mode == QLT_BOX) XSetForeground(disp,gc,green);
  else                                  XSetForeground(disp,gc,black);
  w  = (int)my_round((1+2*qlt->vrad)/MulX,0);
  x  = (int)my_round(qlt->curx[QLT_BOX]/MulX,0);
  h  = (int)my_round((1+2*qlt->vrad)/MulY,0);
  y  = (int)my_round(qlt->cury[QLT_BOX]/MulY,0);
  if (qlt->flip_x) x = qlt->iWIDE-x-1;
  if (qlt->flip_y) y = qlt->iHIGH-y-1;
  x -= w/2;
  y -= h/2;
  XDrawRectangle(disp,qlt->iwin,gc,x,y,w,h);
#endif

  // XSetForeground(disp,gc,black);

  CBX_Unlock();
}

/* ---------------------------------------------------------------- */

static void do_lupe(QlTool* qlt)
{
  Display *disp=qlt->disp;
  GC  gc=qlt->gfs.gc;
  int lWIDE= qlt->lWIDE;
  int lHIGH= qlt->lHIGH;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p): %d,%d,%d\n",PREFUN,qlt,lWIDE,lHIGH,qlt->lmag);
#endif
  assert(vdepth == 24);

  do_lupe24(qlt);

  if (qlt->flip_x) { int y;            /* magnified lupe data comes */
    for (y=0; y<lHIGH; y++) {          /* from the already flipped image */
      rev_line24(((u_int*)qlt->lupe0)+y*lWIDE,lWIDE);
    }
  }
  if (qlt->flip_y) { int y; char *d,*s;
    if (bdepth*(lWIDE*lHIGH) != qlt->lupsize) {
      qlt->lupsize = bdepth*(lWIDE*lHIGH);
      qlt->lupbuf = (char*)realloc(qlt->lupbuf,qlt->lupsize);
    }
    for (y=0; y<lHIGH; y++) {               /* loop over lines */
      d = qlt->lupbuf + y*bdepth*lWIDE;     /* reverse order */
      s = ((char*)qlt->lupe0) + (lHIGH-y-1)*bdepth*lWIDE;
      memcpy((void*)d,(void*)s,bdepth*lWIDE);
    }
    memcpy(qlt->lupe0,(void*)qlt->lupbuf,qlt->lupsize); /* copy back */
  }

  CBX_Lock(0);

  if (qlt->thelupe) XFree((void*)qlt->thelupe);  /* free old image */
  qlt->thelupe = XCreateImage(disp,theVisual,vdepth,
                              ZPixmap,0,(char*)qlt->lupe0,lWIDE,lHIGH,
                              (vdepth == 24) ? 32 : vdepth,0);
  XPutImage(disp,qlt->lwin,gc,qlt->thelupe,0,0,0,0,lWIDE,lHIGH); 

  assert((lWIDE % qlt->lmag) == 0);

  if (showMagPix) { 
    if (qlt->lut == LUT_RAIN) XSetForeground(disp,gc,black);
    else                      XSetForeground(disp,gc,green);
    { int x,y;                           /* highlight center pixel */
      int pw = qlt->lmag; // (qlt->MulX > 0) ? (qlt->MulX*qlt->lmag) : qlt->lmag;
      int ph = qlt->lmag; //(qlt->MulY > 0) ? (qlt->MulY*qlt->lmag) : qlt->lmag;
      if ((lWIDE % pw) % 2) x = (lWIDE-pw)/2;   /* odd lupe-width */
      else                  x = (lWIDE)/2;
      if ((lHIGH % ph) % 2) y = (lHIGH-ph)/2;   /* odd lupe-height */
      else                  y = (lHIGH)/2;
      if (qlt->flip_x) x -= pw;                /* v2 */
      if (qlt->flip_y) y -= ph;
      XDrawRectangle(disp,qlt->lwin,gc,x-1,y-1,pw+1,ph+1);
    }
    XSetForeground(disp,gc,black);
  }

  CBX_Unlock();
}

/* ---------------------------------------------------------------- */

// NOTE: coordinates {0..dim-1}

static void update_cursor(QlTool* qlt,int i) 
{
  char buf[128];

  assert((i >= 0) && (i < QLT_NCURSORS));
  
  qlt->curx[i] = (float)fmax(0,fmin(qlt->dimx-1,qlt->curx[i]));
  qlt->cury[i] = (float)fmax(0,fmin(qlt->dimy-1,qlt->cury[i]));
 
  if (i == QLT_BOX) { 
    float x = (float)my_round(qlt->curx[i],1);
    float y = (float)my_round(qlt->cury[i],1);
    if (showMagPix) {  
      pthread_mutex_lock(&qlt->lock);
      u_short v = qlt->sdata[(int)x+(int)y*qlt->dimx];
      pthread_mutex_unlock(&qlt->lock);
      sprintf(buf,"%6.1f %6.1f %5u",x,y,v); printf("%s\n",buf);
    }
    sprintf(buf,"%6.1f %6.1f",x,y);
  } else {
    int x = (int)floor(qlt->curx[i]);
    int y = (int)floor(qlt->cury[i]);
    pthread_mutex_lock(&qlt->lock);
    u_short v = qlt->sdata[x+y*qlt->dimx];
    pthread_mutex_unlock(&qlt->lock);
    x = (int)my_round(qlt->curx[i],0);
    y = (int)my_round(qlt->cury[i],0);
    sprintf(buf,"%4d %4d %5u",x,y,v);
  }
  strcpy(qlt->cubox[i].text,buf);
  CBX_UpdateEditWindow(&qlt->cubox[i]);
}

/* ---------------------------------------------------------------- */

void qltool_cursor_off(QlTool* qlt,int c,int x,int y,double r,
                       double* dx,double* dy)
{
  double cx = (qlt->flip_x) ? qlt->dimx-(x*qlt->MulX)-1 : x*qlt->MulX;
  double cy = (qlt->flip_y) ? qlt->dimy-(y*qlt->MulY)-1 : y*qlt->MulY;
  *dx = r * (cx - qlt->curx[c]);
  *dy = r * (cy - qlt->cury[c]);
}

/* --- */

void qltool_cursor_set(QlTool* qlt,int c,int x,int y,double r)
{
  double dx,dy;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d,%d)\n",PREFUN,qlt,x,y);
#endif

  qltool_cursor_off(qlt,c,x,y,r,&dx,&dy);
  qlt->curx[c] += dx;
  qlt->cury[c] += dy;
  update_cursor(qlt,c);
}

/* ---------------------------------------------------------------- */

int qltool_handle_key(QlTool* qlt,XKeyEvent* event,int block)
{
  int   dx=0,dy=0;
  int   cmode = qlt->cursor_mode;
  float step  = qlt->cursor_step;
  char  *keys;

  CBX_Lock(0);
#ifdef __x86_64__
  keys = XKeysymToString(XLookupKeysym(event,0));
#else
  keys = XKeysymToString(XKeycodeToKeysym(qlt->disp,event->keycode,0));
#endif
  CBX_Unlock();

#if (DEBUG > 2)
  fprintf(stderr,"%s(): keys='%s'\n",PREFUN,(keys) ? keys : "NIL");
#endif

  if      (!strcmp(keys,"Up"))       {        dy=-1; }
  else if (!strcmp(keys,"KP_Up"))    {        dy=-1; }
  else if (!strcmp(keys,"Down"))     {        dy= 1; }
  else if (!strcmp(keys,"KP_Down"))  {        dy= 1; }
  else if (!strcmp(keys,"Right"))    { dx= 1; }
  else if (!strcmp(keys,"KP_Right")) { dx= 1; }
  else if (!strcmp(keys,"Left"))     { dx=-1; }
  else if (!strcmp(keys,"KP_Left"))  { dx=-1; }
  else if (!strcmp(keys,"KP_Home"))  { dx=-1; dy=-1; }
  else if (!strcmp(keys,"KP_Prior")) { dx= 1; dy=-1; }
  else if (!strcmp(keys,"KP_End"))   { dx=-1; dy= 1; }
  else if (!strcmp(keys,"KP_Next"))  { dx= 1; dy= 1; }
  if ((dx == 0) && (dy == 0)) {   
#if (DEBUG > 1)
    fprintf(stderr,"%s(): key=%s\n",PREFUN,keys);
#endif
    return 0;                          /* not handled */
  }
  if (block) return 1;
  
  if (qlt->flip_x) dx = -dx;
  if (qlt->flip_y) dy = -dy;

  double fdx = ((double)dx*step);
  double fdy = ((double)dy*step);
  qlt->curx[cmode] = fmax(0,fmin(qlt->dimx-1,qlt->curx[cmode]+fdx));
  qlt->cury[cmode] = fmax(0,fmin(qlt->dimy-1,qlt->cury[cmode]+fdy));
  update_cursor(qlt,cmode);
#if 0 // use overlay
  if (cmode == QLT_BOX) do_lupe(qlt);  /* redraw magnifier */
  plot_frame(qlt);                     /* redraw frame with magn. */
#else
  qltool_redraw(qlt,False); 
#endif

  return 1;
}

/* ---------------------------------------------------------------- */

void qltool_scale(QlTool* qlt,const char* sc,const char* p1,const char* p2)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%s,%s)\n",PREFUN,sc,p1,p2);
#endif

  if (!strncasecmp(sc,"cut",3) || !strncasecmp(sc,"man",2)) { 
    qlt->scale = SCALE_CUTS; 
    qlt->scale_par1 = imax(0,atoi(p1));
    qlt->scale_par2 = imax(1,atoi(p2));
  } else 
  if (!strncasecmp(sc,"med",3)) {
    qlt->scale = SCALE_MED;   
    qlt->scale_par1 = imax(1,atoi(p1));
    qlt->scale_par2 = imax(1,atoi(p2));
  } else
  if (!strncasecmp(sc,"mim",3)) {
    qlt->scale = SCALE_MIMA;
  } else 
  if (!strncasecmp(sc,"span",3)) { 
    qlt->scale = SCALE_SPAN;
    qlt->span = imax(1,atoi(p1));
    sprintf(qlt->spnbox.text,"span %5d",qlt->span);
    CBX_UpdateEditWindow(&qlt->spnbox);
  } else 
  if (!strncasecmp(sc,"zero",3)) {
    qlt->zero = imax(0,atoi(p1));
  } else 
  if (!strncasecmp(sc,"pct",3)) {
    qlt->pct = imax(0,imin(99,atoi(p1)));
    sprintf(qlt->pctbox.text,"pct  %5d",qlt->pct);
    CBX_UpdateEditWindow(&qlt->pctbox);
  } else 
  if (!strncasecmp(sc,"bkg",3)) { 
    qlt->bkg = imax(0,imin(63,atoi(p1)));
    sprintf(qlt->bkgbox.text,"bkg  %5d",qlt->bkg);
    CBX_UpdateEditWindow(&qlt->bkgbox);
  }

  if (qlt->scale != SCALE_CUTS) qltool_redraw(qlt,True);
}

/* ---------------------------------------------------------------- */

void qltool_lut(QlTool *qlt,const char* param)
{
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,param);
#endif

  if      (!strcasecmp(param,"Grey")) qlt->lut = LUT_GREY;
  else if (!strcasecmp(param,"InvG")) qlt->lut = LUT_INV;
  else if (!strcasecmp(param,"Rain")) qlt->lut = LUT_RAIN;
  else if (!strcasecmp(param,"BBdy")) qlt->lut = LUT_BBDY;

  fill_pixels(qlt,numcols,qlt->lut);
  if (qlt->lut == LUT_RAIN) qlt->satcol = white;
  else                      qlt->satcol = red;
  qltool_redraw(qlt,False);
}

/* ---------------------------------------------------------------- */

void qltool_lmag(QlTool* qlt,int value)
{
  int lmag=1; 
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,value);
#endif

  while (value > 1) { 
    if (qlt->lWIDE % (lmag*2)) break;
    lmag *= 2; value /= 2; 
  }
  qlt->lmag = lmag;
  do_lupe(qlt); 
  plot_frame(qlt);
}

/* ---------------------------------------------------------------- */

void qltool_reset(QlTool* qlt,int dimx,int dimy,int clear) 
{
  int    i;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d,%d)\n",PREFUN,qlt,dimx,dimy);
#endif
  if (!qlt) return;

  if ((dimx > qlt->maxx) || (dimy > qlt->maxy)) {
    fprintf(stderr,"%s(): invalid image geometry (%d,%d)\n",PREFUN,dimx,dimy);
    return;
  }
#if (DEBUG > 1)
  fprintf(stderr,"%s(): %.2f, %.2f\n",PREFUN,qlt->enoise,qlt->egain);
#endif

  pthread_mutex_lock(&qlt->lock);

  double mx = (double)dimx / (double)qlt->iWIDE; /* v3 */
  double my = (double)dimy / (double)qlt->iHIGH;
#if (DEBUG > 1)
  fprintf(stderr,"%s(): MulX=%f, MulY=%f\n",PREFUN,mx,my);
#endif
  for (i=0; i<QLT_NCURSORS; i++) {
    qlt->curx[i] *= (mx/qlt->MulX);
    qlt->cury[i] *= (my/qlt->MulY);
  }
  qlt->MulX  = mx;
  qlt->MulY  = my;
  qlt->dimx  = dimx;
  qlt->dimy  = dimy;

  pthread_mutex_unlock(&qlt->lock);

  if (clear) {
    CBX_Lock(0);
    XClearWindow(qlt->disp,qlt->iwin);
    XClearWindow(qlt->disp,qlt->lwin);
    CBX_Unlock();
    for (i=0; i<QLT_NCURSORS; i++) update_cursor(qlt,i);
  }
}

/* ---------------------------------------------------------------- */

static void convolve(u_short* p1,u_short *p2,int w,int h) /* v0059 */
{
  auto  int i,j,s,x,y,y1;
  const int x2=w-1,y2=h-1;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p,%d,%d)\n",PREFUN,p1,p2,w,h);
#endif

  for (y=1; y<y2; y++) {
    y1 = y*w;
    for (x=1; x<x2; x++) {
      i = y1 + x;
      s = 4*(int)p1[i] +
          2*(int)p1[i-1]   + 2*(int)p1[i+1] +
          2*(int)p1[i-w]   + 2*(int)p1[i+w] +
            (int)p1[i-1-w] +   (int)p1[i-1+w] +
            (int)p1[i+1-w] +   (int)p1[i+1+w];
      p2[i] = (u_short)(0.5+(double)s/16.0);
    }
  }
  i = 0; j = (h-1)*w;
  for (x=0; x<w; x++,i++,j++) {        /* NOTE: just copy border */
    p2[i] = p1[i];
    p2[j] = p1[j];
  }
  i = 0; j = x2;
  for (y=0; y<h; y++,i+=w,j+=w) {
    p2[i] = p1[i];
    p2[j] = p1[j];
  }
}

/* --- */

void qltool_update(QlTool* qlt,u_short* p) 
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p): sm=%d\n",PREFUN,qlt->sdata,p,qlt->smoothing);
#endif
  if (!qlt) return;

  pthread_mutex_lock(&qlt->lock);
#if (TIME_TEST > 1)
  double t1 = walltime(0);
#endif

  switch (qlt->smoothing) {            /* v0059 */
  case 0: 
    memcpy(qlt->sdata,p,qlt->dimy*qlt->dimx*sizeof(u_short));
    break;
  case 1:
    convolve(p,qlt->sdata,qlt->dimx,qlt->dimy);
    break;
  case 2:
    { u_short *data;
      data = (u_short*)malloc(qlt->dimy*qlt->dimx*sizeof(u_short));
      convolve(p,   data,      qlt->dimx,qlt->dimy);
      convolve(data,qlt->sdata,qlt->dimx,qlt->dimy);
      free((void*)data);
    }
    break;
  case 3: default:
    { u_short *data1,*data2; int i;
      data1 = (u_short*)malloc(qlt->dimy*qlt->dimx*sizeof(u_short));
      data2 = (u_short*)malloc(qlt->dimy*qlt->dimx*sizeof(u_short));
      convolve(p,    data1,     qlt->dimx,qlt->dimy);
      for (i=0; i<qlt->smoothing-2; i++) {
        convolve(data1,data2,     qlt->dimx,qlt->dimy);
        i++; if (i == qlt->smoothing-2) break;
        convolve(data2,data1,     qlt->dimx,qlt->dimy);
      } 
      if (qlt->smoothing % 2) convolve(data2,qlt->sdata,qlt->dimx,qlt->dimy);
      else                    convolve(data1,qlt->sdata,qlt->dimx,qlt->dimy);
      free((void*)data2);
      free((void*)data1);
    }
    break;
  }
#if (TIME_TEST > 1)
  double t2 = walltime(0);
  printf("%s: time=%.2f [ms]\n",PREFUN,1000.0*(t2-t1));
#endif

  pthread_mutex_unlock(&qlt->lock);
}

/* ---------------------------------------------------------------- */

void qltool_redraw(QlTool* qlt,Bool rescale)
{
  int i;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,qlt);
#endif
  if (!qlt) return; 

  if (rescale) {
    switch (qlt->scale) {              /* scaling mode */
    case SCALE_MED:                    /* median -/+ sigma */
      scale_med(qlt,qlt->scale_par1,qlt->scale_par2);  
      sprintf(qlt->valbox.text,"val  %5d",qlt->data_min);
      CBX_UpdateEditWindow(&qlt->valbox);
      sprintf(qlt->spnbox.text,"span %5d",qlt->data_max-qlt->data_min);
      CBX_UpdateEditWindow(&qlt->spnbox);
      break;
    case SCALE_CUTS:
      qlt->data_min = qlt->scale_par1;
      qlt->data_max = qlt->scale_par2;
      break;
    case SCALE_SPAN:                   /* legacy scaling */
      if (qlt->pct != 0) {
        scale_hist(qlt,qlt->pct,qlt->bkg,qlt->span);
        sprintf(qlt->valbox.text,"val  %5d",qlt->val);
      } else {
        qlt->data_min = qlt->zero;
        qlt->data_max = qlt->zero + qlt->span;
        sprintf(qlt->valbox.text,"val  %5d",qlt->zero);
      }
      CBX_UpdateEditWindow(&qlt->valbox);
      break;
    case SCALE_MIMA:                   /* min/max scaling */
    default:
      scale_minmax(qlt);
      sprintf(qlt->valbox.text,"val  %5d",qlt->data_min);
      CBX_UpdateEditWindow(&qlt->valbox);
      sprintf(qlt->spnbox.text,"span %5d",qlt->data_max-qlt->data_min);
      CBX_UpdateEditWindow(&qlt->spnbox);
      break;
    }
    qlt->data_dyn = qlt->data_max - qlt->data_min;
    if (qlt->data_dyn == 0) qlt->data_dyn = 1;
  } /* endif(rescale) */

  for (i=0; i<QLT_NCURSORS; i++) update_cursor(qlt,i);

  create_image(qlt,qlt->data_min,qlt->data_dyn,qlt->picture0); 
  plot_frame(qlt);                /* redraw image */
  do_lupe(qlt);
}

/* ----------------------------------------------------------------
 *  
 * qlutils.c 
 * 
 * Utilities for the QL Data Display
 *
 * 2001-05-17  Christoph C. Birk (birk@obs.carnegiescience.edu)
 * 2004-05-24  scale each chip separately [CCB]
 *
 * ---------------------------------------------------------------- */

/* function prototype(s) */

static void    s_sort     (u_short*,int,int);
static double  gfit       (double*,double*,int,double*,double*,double,double);
static double  lfit       (double*,double*,double*,int,double*,double*);
#if (GFIT_TEST > 0)
static double  fn_gauss   (double,double,double);
#endif

/* ---------------------------------------------------------------- */

static void fill_pixels(QlTool* qlt,int ncols,int lt)
{
  Display      *disp = app->disp;
  XColor       col[QLT_NCOLORS];
  u_long       *pix = qlt->pixels;
  const double  contrast=1.0,level=0.0;
  int          i,virt,offset;
  u_short      s;
#if (DEBUG > 1)
  fprintf(stderr,"%s(): contrast=%f, level=%f\n",PREFUN,contrast,level);
#endif
  assert(vdepth == 24);
  assert(ncols <= QLT_NCOLORS);
  assert(sizeof(u_int) == 4);

  CBX_Lock(0);
  offset = (int)(ncols/2.0*(1.0-level-fabs(contrast)));
  for (i=0; i<ncols; i++) {
    virt = (int)((i-offset)/fabs(contrast)); /* virtual color cell  */
    if (contrast < 0.0f) virt = ncols-virt;
    if (virt < 0)        virt = 0;
    if (virt >= ncols)   virt = ncols-1;
    switch (lt){
    case LUT_BBDY:                     /* black body */
      col[i].red   = linear(virt,0.00,0.50,ncols);
      col[i].green = linear(virt,0.25,0.75,ncols);
      col[i].blue  = linear(virt,0.50,1.00,ncols);
      break;
    case LUT_RAIN:                     /* rainbow */
      col[i].red   = circle(virt,1.0,0.55,ncols);
      col[i].green = circle(virt,0.5,0.45,ncols);
      col[i].blue  = circle(virt,0.0,0.65,ncols);
      break;
    case LUT_GREY:                     /* grayscale */
    case LUT_INV:                      /* inverted grayscale */
    default:
      if (lt == LUT_INV)
        s = (u_short)(((ncols-virt)*MAX_USHORT)/ncols);
      else
        s = (u_short)(MAX_USHORT-((ncols-virt)*MAX_USHORT)/ncols);
      col[i].red   = s;
      col[i].green = s;
      col[i].blue  = s;
      break;
    }
#if (DEBUG > 3)
    fprintf(stderr,"%4x,%4x,%4x -> ",col[i].red,col[i].green,col[i].blue);
#endif
    (void)XAllocColor(disp,theCmap,&col[i]);
#if (DEBUG > 3)
    fprintf(stderr,"%4x,%4x,%4x (%04lx)\n",
            col[i].red,col[i].green,col[i].blue,col[i].pixel);
#endif
    pix[i] = col[i].pixel;
    qlt->colors24[i] = (u_int)pix[i];
#if (DEBUG > 4)
    fprintf(stderr,"RGB=(%4x,%4x,%4x) - %ld\n",
      col[i].red,col[i].green,col[i].blue,(long)col[i].pixel);
#endif
  }

  CBX_Unlock();
}

/* ---------------------------------------------------------------- */

static void draw_arc(u_int *p,u_int c,int iw,int ih, /* v0418 */
                     int xc,int yc,int r,double a1,double a2)
{
  int x,y;
  double a;

  if (r <= 0) return;

  for (a=a1; a<=a2; a+=180.0/r) {
    x = xc + (int)my_round(cos(a*M_PI/180.0)*r,0);
    if ((x < 0) || (x >= iw)) continue;
    y = yc + (int)my_round(sin(a*M_PI/180.0)*r,0);
    if ((y < 0) || (y >= ih)) continue;
    p[x+y*iw] = c; 
  }
}

/* --- */

static void draw_rect(u_int *p,u_int c,int iw,int ih,
                      int x1,int x2,int y1,int y2)
{
  int x,y;
#if (DEBUG > 2)
  fprintf(stderr,"%s(%p,%d,%d,%d,%d,%d,%d)\n",PREFUN,p,iw,ih,x1,x2,y1,y2);
#endif

  int x3 = imin(x2,iw-1);
  if ((y1 >= 0) && (y1 < ih)) {
    for (x=imax(0,x1); x<=x3; x++) p[x+y1*iw] = c;
  }
  if ((y2 >=0) && (y2 < ih)) {
    for (x=imax(0,x1); x<=x3; x++) p[x+y2*iw] = c;
  }
  int y3 = imin(y2,ih-1);
  if ((x1 >= 0) && (x1 < iw)) {
    for (y=imax(0,y1); y<=y3; y++) p[x1+y*iw] = c;
  }
  if ((x2 >=0) && (x2 < iw)) {
    for (y=imax(0,y1); y<=y3; y++) p[x2+y*iw] = c;
  }
}

/* --- */

static void draw_cross(u_int *p,u_int c,int iw,int ih,
                       int xc,int yc,int w,int h)
{
  int x,y;

  for (y=yc-0; y<=yc+0; y++) {      /* thickness 1 pixel v0318 */
    if ((y >= 0) && (y < ih)) {
      int x1 = imax(0,xc-w/2);
      int x2 = imin(iw-1,xc+w/2);
      for (x=x1; x<=x2; x++) p[x+y*iw] = c;
    }
  }
  for (x=xc-0; x<=xc+0; x++) {
    if ((x >= 0) && (x < iw)) {
      int y1 = imax(0,yc-h/2);
      int y2 = imin(ih-1,yc+h/2);
      for (y=y1; y<=y2; y++) p[x+y*iw] = c;
    }
  }
}

/* --- */

static void draw_line(u_int *p,u_int c,int iw,int ih,  /* loadfov */
                      int x1,int y1,int x2,int y2)
{
  int i,x,y,n=imax(1,imax(abs(x2-x1),abs(y2-y1)));

  for (i=0; i<=n; i++) {
    x = x1 + (int)my_round((double)(x2-x1)*i/n,0);
    y = y1 + (int)my_round((double)(y2-y1)*i/n,0);
    if ((x >= 0) && (x < iw) && (y >= 0) && (y < ih)) p[x+y*iw] = c;
  }
}

/* --- */

static void draw_fov(QlTool* qlt,u_int *p)  /* loadfov: ds9 boxes */
{
  int i,j,x[4],y[4];

  for (i=0; i<qlt->nfov; i++) {
    FovBox *b = &qlt->fov[i];
    double s = sin(b->a*M_PI/180.0),c = cos(b->a*M_PI/180.0);
    for (j=0; j<4; j++) {              /* corners in drawing order */
      double dx = ((j==1) || (j==2)) ? b->w/2.0 : -b->w/2.0;
      double dy = (j >= 2) ? b->h/2.0 : -b->h/2.0;
      x[j] = (int)my_round((b->x + dx*c - dy*s)/qlt->MulX,0);
      y[j] = (int)my_round((b->y + dx*s + dy*c)/qlt->MulY,0);
    }
    for (j=0; j<4; j++) {
      draw_line(p,orange,qlt->iWIDE,qlt->iHIGH,x[j],y[j],x[(j+1)%4],y[(j+1)%4]);
    }
  }
}

/* ---------------------------------------------------------------- */

static void do_lupe24(QlTool* qlt)
{
  int     x,y,x1,x2,y1,y2,xh,yh,yh2;
  int     lx,ly,lx2,ly2,colour;
  u_int   *lupe,c;  
  u_short *d,dval;
  int     lW = qlt->lWIDE;
  int     lmag = qlt->lmag;
  int     dimx = qlt->dimx;
  int     dimy = qlt->dimy;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p): Mult=%f,%f\n",PREFUN,qlt,qlt->MulX,qlt->MulY);
#endif
  assert(qlt->data_dyn != 0);

  int lupex = (int)my_round(qlt->curx[QLT_BOX],0);
  int lupey = (int)my_round(qlt->cury[QLT_BOX],0);

  lupe  = (u_int*)qlt->lupe0;
  assert((qlt->lWIDE % qlt->lmag) == 0); 
  lx    = qlt->lWIDE/qlt->lmag;
  assert((qlt->lHIGH % qlt->lmag) == 0); 
  ly    = qlt->lHIGH/qlt->lmag;

  pthread_mutex_lock(&qlt->lock);

  d  = qlt->sdata;                     /* get color from data */
  assert(d);
  lx2 = lx/2; ly2 = ly/2;
  for (x=0; x<lx; x++) {
    x1 = lupex + x - lx2; xh = x*lmag;
    for (y=0; y<ly; y++) {
      y1 = lupey + y - ly2; yh = y*lmag;
      if (x1<0 || x1>=dimx || y1<0 || y1>=dimy) {
        c = (u_int)l_off;
      } else {
        dval = d[x1+y1*dimx];
        if (dval > qlt->satlev) c = qlt->satcol;
        else {
          colour = (numcols*(dval-qlt->data_min))/qlt->data_dyn;
          if (colour >= numcols) c = qlt->colors24[lastcol];
          else if (colour < 0)   c = qlt->colors24[0];
          else                   c = qlt->colors24[colour];
        }
      }
      for (y2=0; y2<lmag; y2++) {      /* fill with color 'c' */
        yh2 = (yh+y2)*lW+xh;
        for (x2=0; x2<lmag; x2++) {
          lupe[yh2+x2] = c;
        }
      }
    } /* endfor(y) */
  } /* endfor(x) */

  pthread_mutex_unlock(&qlt->lock);

  if (qlt->guiding) {                  /* draw white guide box */
    int xc = qlt->lWIDE/2;
    int yc = qlt->lHIGH/2;
    int x1 = xc - qlt->vrad*lmag;
    int x2 = xc + qlt->vrad*lmag;
    int y1 = yc - qlt->vrad*lmag;
    int y2 = yc + qlt->vrad*lmag;
    switch (qlt->gmode) {              /* v0416 */
    case GM_SV3: case GM_SV4:          /* guide-cross */
      draw_cross(lupe,lgrey,qlt->lWIDE,qlt->lHIGH,xc,yc,
                 2*qlt->vrad*lmag,2*qlt->vrad*lmag);
      break;
    default:                           /* guide-box */
      draw_rect(lupe,white,qlt->lWIDE,qlt->lHIGH,x1,x2,y1,y2);
      break;
    }
  }
}

/* ---------------------------------------------------------------- */

static void scale_med(QlTool* qlt,float f1,float f2)
{
  auto     int     n=0,m,i,lim1,lim2;
  auto     int     nsamp,tpix,dmin,dmax;
  auto     u_short *copy0;
  auto     double  sum1=0.0,sum2=0.0,sigma=0.0;
  register u_short *copy,v;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%.1f,%.1f)\n",PREFUN,qlt,f1,f2);
#endif
#if (TIME_TEST > 1)
  double t1 = walltime(0);
#endif

  tpix = qlt->dimx*qlt->dimy;      /* number of pixels */
  if (tpix <= 0) { qlt->data_min = qlt->data_max = 0; return; }
  nsamp = imin(128*(int)pow((double)tpix,0.30),tpix); /* sample size */
  copy = copy0 = (u_short*)malloc(nsamp*sizeof(u_short)); /* sort buffer */
#if (DEBUG > 1)
  fprintf(stderr,"%s(): tpix=%d, nsamp=%d\n",PREFUN,tpix,nsamp);
#endif

  int timeout = tpix;
  while (n < nsamp) {
    if (--timeout < 0) break;
    v = qlt->sdata[(int)DRandom(tpix)]; 
    if ((v == 0) || (v > qlt->satlev)) continue;
    *copy = v; copy++; n++; 
  }
  if (n < 2) { qlt->data_min = qlt->data_max = 0; return; }

  s_sort(copy0,0,n-1);                 /* sort pixels */
#if (DEBUG > 1)
  fprintf(stderr,"%s(%d): median=%u\n",PREFUN,n,copy0[n/2]);
#endif

  if (copy0[0] == copy0[n-1]) {        /* all pixels equal */
    qlt->data_min = qlt->data_max = (int)copy0[0];
    free((void*)copy0);
    return;
  }

  if (f1 < 1.0) {                      /* use fraction of pixels  */
    dmin = (int)copy0[(int)((1.0-f1)*n/2.0)]-1;
    dmax = (int)copy0[(int)((f1+(1.0-f1)/2.0)*n)]+1;
  } else {
    lim1 = copy0[n/2]/2; lim2 = imin(qlt->satlev,2*copy0[n/2]);
#if (DEBUG > 2)
    fprintf(stderr,"%s(): lim1=%d, lim2=%d\n",PREFUN,lim1,lim2);
#endif
    for (i=0,m=0,copy=copy0; i<n; i++,copy++) { /* calc. std-dev. */
      if ((int)*copy > lim1) {
        if ((int)*copy < lim2) {
          sum1 += (double)*copy;
          sum2 += (double)*copy * (double)*copy;
          m++;
        }
      }
    }
    if (m) sigma = sqrt(sum2/m - (sum1/m)*(sum1/m));
    else   sigma = 1.0;
#if (DEBUG > 2)
    fprintf(stderr,"%s(): sigma=%.1f\n",PREFUN,sigma);
#endif
    dmin = (int)copy0[n/2] - (int)(f1*sigma+1.0);
    dmax = (int)copy0[n/2] + (int)(f2*sigma+1.0); 
    dmin = imax(0,imin(MAX_USHORT,dmin));
    dmax = imax(0,imin(MAX_USHORT,dmax));
  }
  qlt->data_min = dmin;
  qlt->data_max = dmax;
 
  free((void*)copy0);

#if (TIME_TEST > 1)
  double t2 = walltime(0);
  printf("%s: time=%.2f [ms] (%d,%d)\n",PREFUN,1000.0*(t2-t1),dmin,dmax);
#endif
}

/* ---------------------------------------------------------------- */

static void scale_hist(QlTool* qlt,int pct,int bkg,int spn)
{
  auto     int     i,m,nsamp,tpix;
  auto     int     hist[MAX_USHORT+1],n=0;
  register u_short v;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%d,%d,%d)\n",PREFUN,qlt,pct,bkg,spn);
#endif
#if (TIME_TEST > 1)
  double t1 = walltime(0);
#endif

  tpix = qlt->dimx * qlt->dimy;
  if (tpix <= 0) { qlt->data_min = qlt->data_max = 0; return; }

  for (i=0; i<=MAX_USHORT; i++) hist[i] = 0;  /* clear histogram */

  m = (qlt->satlev < MAX_USHORT) ? qlt->satlev : MAX_USHORT;
  nsamp = (int)pow((double)tpix,0.60);
#if (DEBUG > 1)
  fprintf(stderr,"%s(): tpix=%d, nsamp=%d\n",PREFUN,tpix,nsamp);
#endif
  int timeout = tpix;
  while (n < nsamp) {
    if (--timeout < 0) break;
    v = qlt->sdata[(int)DRandom(tpix)];
    if (v < m) { hist[v] += 1; n++; }
  }
  if (n < 1) { qlt->data_min = qlt->data_max = 0; return; }

  int np = (n*pct)/100;
  for (i=0; i<MAX_USHORT; i++) {
    if (!hist[i]) continue;
    np -= hist[i]; if (np <= 0) break;
  }
  qlt->val = i;
  qlt->data_min = i - (spn*bkg)/64;
  qlt->data_max = i + (spn*(64-bkg))/64;

#if (TIME_TEST > 1)
  double t2 = walltime(0);
  printf("%s(): time= %.2f [ms] (%d,%d)\n",PREFUN,1000.0*(t2-t1),
         qlt->data_min,qlt->data_max);
#endif
}

/* ---------------------------------------------------------------- */

static void scale_minmax(QlTool* qlt)
{
  int              npix,dmin,dmax;
  register int     i;
  register u_short *data;
#if (TIME_TEST > 1)
  double t1 = walltime(0);
#endif

  dmax = -DAMNED_BIG;                  /* min-max scaling */
  dmin =  DAMNED_BIG;

  data  = qlt->sdata;
  npix  = qlt->dimy * qlt->dimx;
  for (i=0; i<npix; i++, data++) {
    if (*data == 0) continue;
    if (*data > qlt->satlev) continue;
    if ((int)*data > dmax) dmax = (int)*data;    /* new maximum */
    if ((int)*data < dmin) dmin = (int)*data;    /* new minimum */
  }
  qlt->data_min = dmin;
  qlt->data_max = dmax;

#if (TIME_TEST > 1)
  double t2 = walltime(0);
  fprintf(stderr,"%s(): %4.2f [m]] (%d,%d)\n",PREFUN,1000.0*(t2-t1),dmin,dmax);
#endif
}

/* ---------------------------------------------------------------- */

static void CreateImage(QlTool* qlt,int min,int dyn,u_int* p);
static void do_flip_x(QlTool*,void*);
static void do_flip_y(QlTool*,void*);

static void create_image(QlTool* qlt,int min,int dyn,void* p0)
{
  int i,xc,yc;
  u_long color;
#if (DEBUG > 1) 
  fprintf(stderr,"%s(%p,%d,%d,%p)\n",PREFUN,qlt,min,dyn,p0);
#endif
  assert(dyn != 0);

  pthread_mutex_lock(&qlt->lock);

  CreateImage(qlt,min,dyn,p0);

  for (i=0; i<QLT_NCURSORS; i++) {     /* overlay cursors */
    color = (i==qlt->cursor_mode) ? green : (qlt->cursor_dark) ? black : lgrey;
    xc = (int)my_round(qlt->curx[i]/qlt->MulX,0);
    yc = (int)my_round(qlt->cury[i]/qlt->MulY,0);
    if (i == QLT_BOX) {
      if (qlt->guiding) color = yellow;
      int x1 = xc - qlt->lWIDE/(2*qlt->lmag*(int)qlt->MulX); 
      int x2 = xc + qlt->lWIDE/(2*qlt->lmag*(int)qlt->MulX);
      int y1 = yc - qlt->lHIGH/(2*qlt->lmag*(int)qlt->MulY);
      int y2 = yc + qlt->lHIGH/(2*qlt->lmag*(int)qlt->MulY);
      draw_rect((u_int*)p0,color,qlt->iWIDE,qlt->iHIGH,x1,x2,y1,y2);
    } else {  
      draw_cross((u_int*)p0,color,qlt->iWIDE,qlt->iHIGH,xc,yc,20,20);
    }
  }
  if ((qlt->guiding > 0) && (qlt->gmode == GM_SV5) && (qlt->arc_radius)) {
    xc = (int)my_round(qlt->curx[3]/qlt->MulX,0); /* cursor4 is */
    yc = (int)my_round(qlt->cury[3]/qlt->MulY,0); /* center of arc */
    assert(qlt->MulX == qlt->MulY);
    double r  = qlt->arc_radius/qlt->MulX;
    double a1 = qlt->arc_angle - 30.0; 
    double a2 = qlt->arc_angle + 30.0;
    draw_arc(p0,yellow,qlt->iWIDE,qlt->iHIGH,xc,yc,r,a1,a2); /* v0418 */
  }
  draw_fov(qlt,(u_int*)p0);            /* loadfov overlay */

  if (qlt->flip_x) do_flip_x(qlt,p0);
  if (qlt->flip_y) do_flip_y(qlt,p0);  

  pthread_mutex_unlock(&qlt->lock);
}

/* ---------------------------------------------------------------- */

static void do_flip_x(QlTool* qlt,void* p0)
{
  int  y,ic;

  assert(vdepth == 24);

  ic = (int)(qlt->dimy/qlt->MulY);
  assert(ic <= qlt->iHIGH);

  for (y=0; y<ic; y++) {               /* loop over lines */
    rev_line24(((u_int*)p0) +y*qlt->iWIDE,qlt->iWIDE);
  }
}

/* ---------------------------------------------------------------- */

static void rev_line24(u_int* p0,int n)
{
  register u_int *p1 = p0;
  register u_int *p2 = p0+n-1;
  register u_int pp;
  
  while (p1 < p2) {
    pp = *p1; *p1 = *p2; *p2 = pp; p1++; p2--;
  }
}

/* ---------------------------------------------------------------- */

static void do_flip_y(QlTool* qlt,void* p0)
{
  int  y,ic;
  char *s,*d;
  int  iW=qlt->iWIDE,iH=qlt->iHIGH;

  if (bdepth*(iW*iH) != qlt->imgsize) {
    qlt->imgsize = bdepth*(iW*iH);
    qlt->imgbuf = (char*)realloc(qlt->imgbuf,qlt->imgsize);
  }
  ic = (int)(qlt->dimy/qlt->MulY);
  assert(ic <= qlt->iHIGH);

  for (y=0; y<ic; y++) {               /* loop over lines */
    d = qlt->imgbuf + y*bdepth*iW;     /* reverse order */
    s = ((char*)p0) + (ic-y-1)*bdepth*iW;
    memcpy((void*)d,(void*)s,bdepth*iW);
  }
  memcpy(p0,(void*)qlt->imgbuf,qlt->imgsize); /* copy back */
}

/* ---------------------------------------------------------------- */

static void CreateImage(QlTool* qlt,int min,int dyn,u_int* p)
{
  auto     int     x,y,colour,satflag,lc,nx,ny,n2,xi,yi;
  auto     long    s;
  auto     double  MulX= qlt->MulX;
  auto     double  MulY= qlt->MulY;
  auto     int     dimx= qlt->dimx;
#ifndef NDEBUG
  auto     int     dimy= qlt->dimy;
  auto     int     iH  = qlt->iHIGH;
#endif
  auto     int     iW  = qlt->iWIDE;
  register int     xx,yy;
  register u_short *data;
#if (DEBUG > 2)
  fprintf(stderr,"%s() MulX=%f, MulY=%f\n",PREFUN,MulX,MulY);
#endif
  assert(MulX > 0);
  assert(MulY > 0);
  assert(vdepth == 24);

  nx = (int)ceil(MulX); assert(nx >= 1);
  ny = (int)ceil(MulY); assert(ny >= 1);
  n2 = nx * ny;
  lc = (int)(qlt->dimy/MulY);
#if (DEBUG > 2)
  fprintf(stderr,"nx=%d, ny=%d, n2=%d, lc=%d\n",nx,ny,n2,lc);
#endif
  assert(lc <= iH);

  for (yi=0; yi<lc; yi++) {            /* loop over image coords. */
    y = (int)(yi*MulY); assert(y < dimy);
    for (xi=0; xi<iW; xi++) {
      x = (int)(xi*MulX); assert(x < dimx); /* data coord. */
      s = 0; satflag=0;
      for (yy=0; yy<ny; yy++) {
        data = qlt->sdata + (y+yy)*dimx + x;
        for (xx=0; xx<nx; xx++,data++) {
          if (*data > qlt->satlev) { satflag=1; break; }
          s += *data;
        }
        if (satflag) break;
      }
      if (satflag) *p++ = qlt->satcol;
      else {
        colour = (numcols*(s/n2-min))/dyn;
        if (colour >= numcols) *p++ = qlt->colors24[lastcol];
        else if (colour < 0)   *p++ = qlt->colors24[0];
        else                   *p++ = qlt->colors24[colour];
      }
    }
  }
}

/* ---------------------------------------------------------------- */

static void s_sort(u_short *zahl, int l, int r)
{
  int     i,j;
  u_short x,h;
 
  i = l;
  j = r;
 
  if (j>i) {
    x = zahl[(i+j)/2];
    do {
      while (zahl[i] < x) i++;
      while (zahl[j] > x) j--;
      if (i<=j) {
        h         = zahl[i];
        zahl[i++] = zahl[j];
        zahl[j--] = h;
      }
    } while (!(i>j));
    s_sort(zahl,l,j);
    s_sort(zahl,i,r);
  }
}

/* ---------------------------------------------------------------- */

static u_short linear(int value,double x1,double x2,int ncolors)
{
  double  x;
 
  x = (double)value/(double)ncolors;
  x = (x-x1)/(x2-x1);
  if (x < 0.0) x = 0.0;
  if (x > 1.0) x = 1.0;
  x *= (double)MAX_USHORT;
 
  return((u_short)x);
}

/* ---------------------------------------------------------------- */
 
static u_short circle(int value,double m,double radius,int virtcols)
{
  double x;
  u_short r;
 
  x = (double)value/(double)(virtcols-1);
 
  if ((x-m)*(x-m) < radius*radius)
    r = (u_short)(MAX_USHORT*sqrt(radius*radius - (x-m)*(x-m))/radius);
  else
    r = (u_short)0;
 
  return(r);
}

/* ---------------------------------------------------------------- */

void qltool_centroid(QlTool *qlt,double *cx,double* cy)
{
  double nbck,back;

  pthread_mutex_lock(&qlt->lock);

  int lupex = my_round(qlt->curx[QLT_BOX],0);
  int lupey = my_round(qlt->cury[QLT_BOX],0);
  back = get_background(qlt->sdata,qlt->dimx,qlt->dimy,lupex,lupey,
                        qlt->vrad,&nbck);
  get_centroid(qlt->sdata,qlt->dimx,qlt->dimy,lupex,lupey,
               qlt->vrad,back,cx,cy);

  pthread_mutex_unlock(&qlt->lock);
}

/* ---------------------------------------------------------------- */

double get_fwhm(u_short *data,int dimx,int dimy,int x0,int y0,int r,
                double enoise,double egain,
                double* back,double* cx,double *cy,double *peak,double* flx)
{
  int     x,y,r2,n=0;
  double  *flux,*dist,nbck,fwhm,a,b,d;
#if (DEBUG > 1)
  fprintf(stderr,"%s()\n",PREFUN);
#endif
#if (TIME_TEST > 1)
  double t1 = walltime(0);
#endif
 
  r2   = r*r;
  *back = get_background(data,dimx,dimy,x0,y0,r,&nbck);
  (void)get_centroid(data,dimx,dimy,x0,y0,r,*back,cx,cy);
#if (DEBUG > 1)
  fprintf(stderr,"%s(): noise=%.1f\n",PREFUN,nbck);
#endif

  flux = (double*)malloc(4*r2*sizeof(double));
  dist = (double*)malloc(4*r2*sizeof(double));

  for (x=x0-r; x<=x0+r; x++) {         /* get flux */
    for (y=y0-r; y<=y0+r; y++) {       /* circular aperture */
      if ((x<0) || (x>=dimx) || (y<0) || (y>=dimy)) continue;
      d = ((double)x-(*cx))*((double)x-(*cx)) + 
          ((double)y-(*cy))*((double)y-(*cy));
      if (d > r2) continue;            /* use inside */
      flux[n] = (double)data[x+y*dimx] - *back;
      if (flux[n] < 3.0*nbck) continue;   /* flux too low */
      dist[n] = sqrt(d);
      n++;
    }
  }

  if (n > 2) { 
    (void)gfit(dist,flux,n,&a,&b,egain,enoise);
    fwhm = 2.35482*b;               /* 2*sqrt(-2*ln(0.5)) */
    *flx = peak2flux(a,fwhm);       /* WARNING: valid only if */
    *peak = a;
  } else {
    fwhm = *flx = *peak = 0;
  }

  free((void*)flux); free((void*)dist);

#if (TIME_TEST > 1)
  double t2 = walltime(0);
  fprintf(stderr,"%s(): %.4f (ms)\n",PREFUN,1000.0*(t2-t1));
#endif

  return fwhm;
}

/* --- */

double get_quads(u_short* data,int dimx,int dimy,int x0,int y0,int r,
              double *rx,double* ry,double *flux)
{
  int    x,y;
  double xp=0,yp=0,xm=0,ym=0,v;

  if ((x0-r<0) || (x0+r>=dimx) || (y0-r<0) || (y0+r>=dimy)) return -1;

  double b = get_background(data,dimx,dimy,x0,y0,r,NULL);
  *flux = 0.0;
  for (x=x0-r; x<=x0+r; x++) { 
    if (x == x0) continue;
    for (y=y0-r; y<=y0+r; y++) {   
      v = (double)data[x+y*dimx]-b; if (v < 0) continue;
      *flux += v;                      /* L+R */
      if (x > x0) xp += v;
      else        xm += v;
      if (y > y0) yp += v;
      if (y < y0) ym += v;
    }
  }
  *rx = (xp-xm)/(xp+xm);               /* ratio */
  *ry = (yp-ym)/(yp+ym);  
  // printf("%.0f %.0f %.0f %.0f --> %+.4f %+.4f\n",xp,xm,yp,ym,*rx,*ry);

  return b;
}

/* --- */

double calc_quad(int vrad,int sw,double sig,double dx,double *sf) /* v0409 */
{
  int xx,yy;
  double xp=0,xm=0,xc=0,sig2=2.0*sig*sig,y2,f; 

  int swL = -sw/2;
  int swR =  sw/2;
  if (!(sw % 2)) swR -= 1; 
  for (yy=-vrad; yy<=vrad; yy++) { 
    y2 = (double)(yy*yy);
    for (xx=-vrad; xx<=vrad; xx++) {
      f = exp(-(((double)xx-dx)*((double)xx-dx)+y2)/sig2);
      if      (xx < swL) xm += f;
      else if (xx > swR) xp += f;
      else               xc += f;
    }
  }
  if (sf) *sf = (xm+xp+xc)/(xm+xp);    /* scale factor v0413 */

  return (xp-xm)/(xp+xm);
}

/* ---------------------------------------------------------------- */

double get_background(u_short* data,int dimx,int dimy,int x0,int y0,
                             int r,double* noise)
{
  int     r2,n=0,x,y;
  u_short *sortval,d;
  double  s1=0.0,s2=0.0,back=0.0;

  r2 = r*r;
  sortval = (u_short*)malloc(4*r2*sizeof(u_short));

  for (x=x0-r; x<=x0+r; x++) {         /* find background */
    for (y=y0-r; y<=y0+r; y++) {       /* circular aperture */
      if ((x<0) || (x>=dimx) || (y<0) || (y>=dimy)) continue;
      if ((x-x0)*(x-x0)+(y-y0)*(y-y0) < r2) continue; /* use outside */
      d = data[x+y*dimx];
      if (d > 0) {
        sortval[n] = d; n++;
        if (noise) { s1 += (double)d; s2 += (double)d * (double)d; }
      }
    }
  }
  if (n > 0) {                         /* at least 1 pixel */
    s_sort(sortval,0,n-1);
    back = (double)sortval[n/2];
    if (noise) *noise = sqrt(s2/n - (s1/n)*(s1/n));
  }
#if (DEBUG > 1)
  fprintf(stderr,"%s(): x0=%d, y0=%d, back=%.0f (n=%d)\n",PREFUN,
          x0+1,y0+1,back,n);
#endif
  free((void*)sortval);

  return back;
}

/* ---------------------------------------------------------------- */

static double get_centroid(u_short* data,int dimx,int dimy,int x0,int y0,
                    int r,double back,double* cx,double* cy)
{
  int    r2,x,y;
  double m=0.0,h;

  if (back < 0.0) back = get_background(data,dimx,dimy,x0,y0,r,NULL);

  r2 = r*r;
  *cx = *cy = 0.0;
  for (x=x0-r; x<=x0+r; x++) {         /* find centeroid */
    for (y=y0-r; y<=y0+r; y++) {
      if ((x<0) || (x>=dimx) || (y<0) || (y>=dimy)) continue;
      if ((x-x0)*(x-x0)+(y-y0)*(y-y0) > r2) continue; /* use inside */
      h = (double)data[x+y*dimx] - back;
      if (h < 0.0) continue;           /* signal too low */
      *cx += h*x;
      *cy += h*y;
      m   += h;
    }
  }
  if (m == 0.0) return(-1.0);
  *cx /= m;                            /* normalize centroid */
  *cy /= m;

#if (DEBUG > 1)
  fprintf(stderr,"%s(): cx=%.1f, cy=%.1f (m=%.1f)\n",PREFUN,
          *cx+1.0,*cy+1.0,m);
#endif

  return back;
}

/* ---------------------------------------------------------------- */

#if (GFIT_TEST > 0)

static double fn_gauss(double x,double a,double b)
{
  return(a * exp(-(x*x/(2.0*b*b))));
}
 
#endif

/* ---------------------------------------------------------------- */

static double gfit(double* x,double* y,int n,double* a,double* b,
                   double egain,double enoise)
{
  int    i;
  double *xval,*yval,*eval,chi2;

  xval = (double*)malloc(n*sizeof(double));
  yval = (double*)malloc(n*sizeof(double));
  eval = (double*)malloc(n*sizeof(double));

  for (i=0; i<n; i++) {                /* 'linearize' data */
    xval[i] = x[i]*x[i];
    yval[i] = log(y[i]);               /* eval ~ 1/weight */
    eval[i] = (sqrt(y[i]*egain+enoise*enoise)/egain)/y[i];
  }
#if (GFIT_TEST > 0)
  {
    FILE *fp;
    fp = fopen("/tmp/linear.dat","w");
    for (i=0; i<n; i++) fprintf(fp,"%f  %f\n",xval[i],yval[i]);
    fclose(fp);
  }
#endif

  chi2 = lfit(xval,yval,eval,n,a,b);   /* linear regression fit */
  *a = exp(*a);                        /* 'de-linearize' */
  *b = sqrt(-0.5/(*b));
#if (DEBUG > 1)
  fprintf(stderr,"%s(): a=%f, b=%f\n",PREFUN,*a,*b);
#endif
  
  free((void*)xval); free((void*)yval); free((void*)eval);

  return(chi2);
}
 
/* ---------------------------------------------------------------- */

static double sqd(double x)
{
  return(x*x);
}

/* ---------------------------------------------------------------- */
/* modified from 'Numerical Recipes in C' */

static double lfit(double* x,double* y,double* e,int n,double* a,double* b)
{
  int    i;
  double delta,h;
  double s=0.0,sx=0.0,sy=0.0,sxx=0.0,sxy=0.0,chi2=0.0;

  for (i=0; i<n; i++) {
    h    = e[i]*e[i];
    s   += 1./h;
    sx  += x[i]/h;
    sy  += y[i]/h;
    sxx += x[i]*x[i]/h;
    sxy += x[i]*y[i]/h;
  }
#if (DEBUG > 2)
  fprintf(stderr,"s=%.1f, sx=%.1f, sy=%.1f, sxx=%.1f, sxy=%.1f\n",
          s,sx,sy,sxx,sxy);
#endif
  delta = s*sxx - sx*sx;
  *a    = (sxx*sy-sx*sxy)/delta;       /* y = a + bx */
  *b    = (s*sxy-sx*sy)/delta;
#if (DEBUG > 2)
  fprintf(stderr,"%s(): a=%f, b=%f\n",PREFUN,*a,*b);
#endif

  for (i=0; i<n; i++) {                /* chi-square */
    chi2 += sqd((y[i] - *a  - (*b)*x[i]) / e[i]);
  }

  return chi2;
} 

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

