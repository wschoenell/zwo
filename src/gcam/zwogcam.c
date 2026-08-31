/* ----------------------------------------------------------------
 *
 * zwoguider.c / zwogcam.c (since v0332)
 * 
 * Project: ZWO Guider Camera software (OCIW, Pasadena, CA)
 *
 * v0.001  2016-10-24  Christoph C. Birk, birk@carnegiescience.edu
 * v0.018  2016-11-14  Guider Class
 * v0.022  2016-11-21  new GUI layout
 * v0.030  2016-12-07  gwin experiments 
 * v0.040  2017-01-20  binX,Y -> binning
 * v0.042  2017-01-25  "send" command
 * v0.044  2017-01-26  var. number of message lines, Led
 * v0.051  2017-02-10  qltool: fix rev_line() "static"
 * v0.054  2017-09-07  SH-optics tests
 * v0.061  2018-01-23  telio (TCSIS protocol)
 * v0.062  2018-02-03  tests @ Clay (139.229.109.60)
 * v0.064  2018-02-09  tests @ Clay
 * v0.066  2018-02-23  add 'dt', apa=1, send_host, 'lockfile'
 * v0.067  2018-04-02  remove the lock-file on error at startup. 
 *                     mm-2 moves the guider probes and the telescope. 
 * v0.068  2018-04-xx  disable 'Q' in release version
 * v0.069  2018-04-26  add TCP/IP data server
 * FORK IT
 * v0.200  2018-05-09  andorserver
 * v0.203  2018-05-14  USB->TCP
 * v0.219  2018-08-06  MIKE run 
 * v0.220  2018-08-21  auto-start 'server' if 'localhost'
 * v0.221  2018-08-28  use '-i' switch for auto-start 'localhost'
 * v0.222  2020-10-17  horizontal layout, limit 1 guider
 * FORK IT
 * v0.300  2021-03-12  ZWO camera
 * v0.309  2021/Fall   lab testing ok
 * v0.310  2022-07-01  "aeg" command, '-v' switch
 * v0.311  2022-08-08  remove 'lockfile', -angle, -elsign, -rosign
 * v0.312  2022-08-24  get serial number from zwoserver
 * v0.313  2022-09-09  see HTML documentation
 * v0.314  2023-01-10  add permissions to open(O_CREAT), gcc warnings
 * v0.315  2023-01-11  take care of some more gcc warnings
 *                     allow 'elsign==0', change some defaults
 * v0.316  2023-01-19  send FITS files, add MASK,CA,SH keywords
 * v0.317  2023-01-20  bug-fixes, add CAMERA,FRAME,ROTATORN
 * v0.323  2023-04-28  bad pixel mask
 * v0.325  2023-06-20  improved fitting (stable)
 * v0.328  2023-07-18  telescope/guider FITS keywords
 * v0.330  2023-07-20  "fm",F9,F10 
 * v0.331  2023-07-28  rename executable, bug-fixes
 * v0.332  2023-08-01  rename to zwogcam/gcamzwo, TCP/IP interface
 * v0.334  2023-08-17  EDS 
 * v0.343  2023-09-13  layout changes, "send" countdown
 * v0.344  2023-10-31  guider correction and EDS every 'av' frame
 * v0.348  2023-12-13  offset
 * v0.350  2023-12-18  slitview guider (gm=3)
 * v0.400  2024-01-05  single camera only, layout optimizations
 * v0.408  2024-01-29  guide mode 'gm4'
 * v0.415  2024-02-20  configuration files
 * v0.418  2024-03-07  guide mode 'gm5'
 *
 * v0.425  2024-10-18  update compass with flipx and flipy [PP]
 * v0.428              gain fits keyword 
 * v0.429  2025-03-19  add location flag
 * v1.0.1  2025-07-17  added send_host/send_port to config file, merged with Povilas' changes
 * v1.0.2  2025-08-07  sign of mm2 aer commands
 * v1.0.3  2025-08-07  add ca command

 * v1.0.4  2025-08-07  sign of mm2 aer commands
 * v1.0.4  2025-08-07  add ca command
 * v1.0.4  2025-08-16  add more cursor output for IMACS opperations during guiding mm1 command 
 * v1.0.4  2025-12-11  on fone turn of eds guider eds flag 801, and set gdrbox message black.
 * v1.0.4  2026-01-15  added exptime to config file
 * v1.0.4  2026-01-15  added mouse-mode aliases, updated documentation
 * v1.0.5  starting on v1.0.5, updates are documented in the release notes on github
 *
 * http://www.lco.cl/telescopes-information/magellan/
 *   operations-homepage/magellan-control-system/magellan-code/gcam
 *
 * TODO update FITS header (EPOCH wrong in Magellan example)
 *
 * ---------------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG           1              /* debug level */
#endif

#define TIME_TEST       1

/* ------------------------------------------------------- */

#define _REENTRANT

#include <stdlib.h>                    /* atoi(),exit() */
#include <stdio.h>                     /* sprintf() */
#include <string.h>                    /* strcpy(),memset(),memcpy() */
#include <math.h>                      /* sqrt() */
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#if (TIME_TEST > 0)
#include <limits.h> 
#include <sys/times.h>
#include <unistd.h>
#endif

#include <cxt.h>                       /* Xlib stuff */

#include "zwogcam.h"
#include "zwotcp.h"
#include "tcpip.h"                     /* TCPIP stuff */
#include "utils.h"                     /* generic utilities */
#include "ptlib.h"                     /* POSIX threads lib */
#include "guider.h"                    /* qltool.h,graph.h,fits.h */
#include "telio.h"
#include "random.h"
#include "eds.h"

/* DEFINEs -------------------------------------------------------- */

#define PA_BOX

#define P_TITLE         "GcamZwo"

#define MY_HELP         "zwogcam.html"

#define DBE_DATAPATH    "datapath"
#define FMT_RUN         "run%d"
#define DBE_PREFIX      "prefix"    
#define DEF_PREFIX      "gdr"

#define GM_MAX          GM_SV5         /* v0404 */

#define DEF_HELP "https://carnegie-observatories.github.io/zwo/ZWO/"
#if defined(MACOSX)
#define DEF_BROWSER     "open"         /* Safari */
#else
#define DEF_BROWSER     "firefox"
#endif

#define DEF_FONT        "lucidatypewriter"

#define TCPIP_PORT      (50000+(100*PROJECT_ID))
#define IMAGE_PORT      (TCPIP_PORT+100)   /* image server v1.0.6 */
#define IMAGE_TCSAGE    1.0                /* [s] TCS cache lifetime */

#define PREFUN          __func__

/* EXTERNs -------------------------------------------------------- */

#ifdef LINUX
extern void swab(const void *from, void *to, ssize_t n);
#endif

/* X-DEFINEs ------------------------------------------------------ */

#ifdef FONTSIZE
int PXh=FONTSIZE;
#else
int PXh=14;
#endif
#define PXw         (10+(PXh-14)/2)
#define XXh         (PXh+4)
 
enum file_enum {
            FILE_HELP,
            FILE_D1,
            FILE_EXIT,
            FILE_N,
};
 
enum options_enum {
            OPT_DATAPATH,
            OPT_LOGFILE,
            OPT_D1,
            OPT_FLIP_X,
            OPT_FLIP_Y,
            OPT_N
};

/* TYPEDEFs ------------------------------------------------------- */

typedef struct gcam_header_tag {
  int  dimx,dimy,seqn,flag;
  char reserved[24];
} Header;

/* GLOBALs -------------------------------------------------------- */

Application *app;
char logfile[512];
int showMagPix=0;                      /* v0323 NOTE: singleton */

/* function prototype(s) */


/* STATICs -------------------------------------------------------- */

volatile Bool     done=True;

static MainWindow mwin;                /* main window */
static char       fontname[128];
static int        n_msg=0;
static float      throttle=5.0f;

static Menu       fimenu;              /* file dropdown menu */ 
static Menu       opmenu;              /* options dropdown menu */
static EditWindow utbox,rtbox;

static Guider sGuider;                 /* singleton v0400 */

static time_t disk_next=0;
static time_t startup_time;
static int    pa_interval=30;
static float  elev=90,para=0;

static int   baseD=1800,baseB=2,baseI=600,pHIGH=117;
static int   eWIDE,eHIGH;
static int   wINFO;                    /* v0401 */
static int   lSIZE=128;                /* v0402 */
// static float pscale=0.02535f;  /* measured 20230113 Andor:6.5um, ZWO:2.32um */

static const Bool require_control_key=True; // IDEA allow False

static char    setup_rc[512];          /* dot-file in $HOME */

static pthread_mutex_t scanMutex,mesgMutex;

static char   paString[128];

static int   vertical=0, location=1;

/* function prototype(s) ------------------------------------------ */

static void    redraw_text       (void);
static void    redraw_compass    (Guider*);

static void    update_ut         (void);

static void    cb_file           (void*);
static void    cb_options        (void*);

static int     set_exptime       (Guider*,float);
static int     set_ca            (Guider*,double);
static int     set_pa            (Guider*,double,int);
static void    set_sens          (Guider*,double);
static void    set_mm            (Guider*,int);
static void    set_fm            (Guider*,int);
static void    set_av            (Guider*,int);
static void    set_sf            (Guider*,int);
static void    set_gm            (Guider*,int,char);

static int     do_start          (Guider*,int);
static int     do_stop           (Guider*,int);

static void    guider_state      (Guider*,GuiderState*);
static void    update_status     (FITSpars*,Guider*,ZwoFrame*);
static void    update_tcs        (FITSpars*);
static void*   run_image_server  (void*);              /* v1.0.6 */
static int     receive_string    (int,char*,size_t);
static void    push_frame        (Guider*,ZwoFrame*);  /* legacy push */

static int     handle_command    (Guider*,const char*,int);
static int     handle_key        (void*,XEvent*); 
static int     handle_msmode     (void*,XEvent*);

static void*   run_housekeeping  (void*);
static void*   run_init          (void*);
static void*   run_setup         (void*);
static void*   run_cycle         (void*);
static void*   run_tele          (void*);
static void*   run_display       (void*);
static void*   run_write         (void*);
static void*   run_tcpip         (void*);

static void    make_mask         (Guider*,const char*);
static void    load_mask         (Guider*);

static int     check_datapath    (char*,int);
static int     cursor_blocked    (Guider*,int,int);
static void    my_shutdown       (Bool);
static int     read_inifile      (Guider*,const char*);
static int     setup_m_switch    (char);

/* ---------------------------------------------------------------- */
 
/* --- M A I N ---------------------------------------------------- */

int main(int argc,char **argv)
{
  int        i,j;
  int        sleeptime=0,winpos=CBX_TOPLEFT;
  int        x,y,w,h,d,tmode=0,err;
  char       buffer[1024],buf[128];
  char       xserver[256];
  XEvent     event;
  XSizeHints hint;

  /* here comes the code ------------------------------------------ */ 

  startup_time = cor_time(0);

  strcpy(xserver,genv2("DISPLAY",""));
  strcpy(fontname,genv2("GCAMZWOFONT",DEF_FONT)); /* v0415 */
  mwin.disp = NULL;
#ifdef MACOSX
  winpos = CBX_CalcPosition(45,0);
  // baseI = 512;
#endif

  pthread_mutex_init(&scanMutex,NULL);
  pthread_mutex_init(&mesgMutex,NULL);

  /* Initialize sGuider with struct initializer for clarity */
  sGuider = (Guider){
    .gnum = 1,
    .gmode = 0,
    .gmpar = 't',
    .angle = -120.0,              /* PFS:-128, MIKE:-119 */
    .elsign = -1.0,
    .rosign = 0.0,
    .parity = -1.0,
    .parit2 = 1.0,
    .offx = 0,                    /* PFS:-10+85, MIKE:+45+230 */
    .offy = 0,
    .slitW = 6,                   /* PFS:6, MIKE:13 */
    .px = 0.051,                  /* PFS:53, MIKE:54 */
    .lmag = 0,
    .name = "",
    .bx = 0,
    .pct = 0,
    .bkg = 0,
    .span = 0,
    .send_host = "localhost",
    .send_port = 0,
    .image_port = IMAGE_PORT,          /* v1.0.6 */
    .tcs_age = IMAGE_TCSAGE,
    .host = "localhost",
    .rPort = 0,
    .sens = 0.5f,
    .status.exptime = 1.0f,
#ifdef ENG_MODE
    .gain = "",
#else
    .gain = "hi",
#endif
  };

  { extern char *optarg; double f;     /* parse command line */  
    extern int opterr,optopt; opterr=0;
    while ((i=getopt(argc,argv,"a:e:f:g:h:i:m:n:o:p:r:t:vl:")) != EOF) {
      switch (i) {
      case 'a':                        /* 'angle' v0311 */
        sGuider.angle = atof(optarg);
        break;
      case 'e':                        /* 'elsign' v0311 */
        f = atof(optarg);              /* v0315 */
        sGuider.elsign = (f > 0) ? 1.0 : ((f < 0) ? -1.0 : 0.0);
        break;
      case 'r':                        /* 'rosign' v0311 */
        f = atof(optarg);
        sGuider.rosign = (f > 0) ? 1.0 : ((f < 0) ? -1.0 : 0.0);
        break;
      case 'f':                        /* .ini file v0415 */
        (void)read_inifile(&sGuider,optarg);
        break;
      case 'g':                        /* guiderCamera number {1..3} */
        sGuider.gnum = imin(3,imax(1,atoi(optarg)));
        break;
      case 'h':                        /* camera (rPi) host */
        strcpy(sGuider.host,optarg);
        break; 
      case 'i':                        /* parity v0351 */
        sGuider.parity = (atof(optarg) < 0) ? -1.0 : 1.0;
        break;
      case 'm':                        /* optical mode */
        if (!setup_m_switch(optarg[0])) {
          fprintf(stderr,"unknown '-m' parameter '%s'\n",optarg); 
        }
        break;
      case 'o':                        /* offset v0348 */
        sscanf(optarg,"%d,%d",&sGuider.offx,&sGuider.offy); 
        break;
      case 'p':                        /* rotator port v0313 */
        sGuider.rPort = atoi(optarg);   
        break;
      case 't':                        /* TCSIS */
        tmode = atoi(optarg);
        break;
      case 'v':                        /* vertical layout */
        vertical = 1;
        break;
      case 'l':                        /*location*/
 	location =atoi(optarg);
        break;
      case '?':
        fprintf(stderr,"%s: option '-%c' unknown or parameter missing\n",
                P_TITLE,(char)optopt);
        break;
      }
    } /* endwhile(getopt) */
  }
  if (!sGuider.gmode) sGuider.gmode = sGuider.gnum;

  if (vertical) pHIGH = imin(88,pHIGH);
  wINFO = lSIZE+4+PXw/3+39*PXw;
  if (vertical) wINFO = imax(wINFO,baseI+6);
  eWIDE = wINFO + ((vertical) ? 2 : baseI+6);
  eHIGH = baseI+6 + ((vertical) ? pHIGH+30*PXh+3 : 0); 

  switch (location) {
  case 1:
    break;                              /*same as default */
  case 2:
    winpos = CBX_CalcPosition(eWIDE+1,0);    /*  MACOS case ?*/
    break;
  case 3:
    winpos = CBX_CalcPosition(2*(eWIDE+1),0);
    break;
  default:
    break;                              /*ignore for now*/
  }

  InitRandom(0,0,0);                   /* QlTool uses Random */

  /* -------------------------------------------------------------- */

  switch (tmode) {
  case 0:                              /* offline */
    err = telio_init(TELIO_NONE,0);
    break;
  case 1:                              /* Baade v0315 */
    err = telio_init(TELIO_MAG1,(sGuider.rPort) ? 5810+sGuider.rPort : 5801);
    break;
  case 2:                              /* Clay v0315*/
    err = telio_init(TELIO_MAG2,(sGuider.rPort) ? 5810+sGuider.rPort : 5801);
    break;
  default:                             /* TCSIS Simulator */
    err = telio_init(TELIO_NONE,5801); 
    break;
  }
  if (err) {
    fprintf(stderr,"%s: failed to connect to TCSIS (err=%d)\n",P_TITLE,err);
    exit(3);
  }
  (void)eds_init(telio_host,tmode);

  /* initialize X-stuff ------------------------------------------- */

  Status s = XInitThreads();
  if (!s) fprintf(stderr,"Xthreads failed\n");
  assert(s);

  if ((app=CBX_InitXlib(xserver)) == NULL) {  /* connect to 'xserver' */
    fprintf(stderr,"%s: CBX_InitXlib(%s) failed\n",P_TITLE,xserver);
    exit(4);
  }
 
  /* get setup from files */

  sprintf(buf,"gcamzwo%d_",sGuider.gnum);
  log_path(logfile,"GCAMZWOLOG",buf); /* create logfile name */
  sprintf(buf,"gcamzwo%d.log",sGuider.gnum);
  lnk_logfile(logfile,buf);            /* link to $HOME */
  sprintf(buf,"%s-v%s",P_TITLE,P_VERSION);
  message(NULL,buf,MSS_FILE);

  sprintf(buf,"gcamzwo%drc",sGuider.gnum); /* runNumber & dataPath */
  (void)set_path(setup_rc,buf);        /* $HOME v0311 */
  get_string(setup_rc,DBE_DATAPATH,buffer,genv2("GCAMZWOPATH", "/opt/gcamzwo"));
  i = check_datapath(buffer,0);
  if (!i) strcpy(buffer,genv2("GCAMZWOPATH", "/opt/gcamzwo")); 

  /* Initialize single guider */
  {
    Guider *g = &sGuider;
    g->server = zwo_create(g->host,SERVER_PORT);
    g->server->expTime = g->status.exptime;
    g->gid = 0;                        /* guiding thread ID */
    g->qltool = NULL;
    g->loop_running = g->house_running = g->tcpip_running = False;
    g->image_running = False; g->image_clients = 0;   /* v1.0.6 */
    g->stop_flag = False;
    g->init_flag = g->send_flag = g->write_flag = 0;
    pthread_mutex_init(&g->mutex,NULL);
    g->fps = g->flux = g->ppix = g->back = g->fwhm = g->dx = g->dy = 0;
    g->pa = 0.0;
    g->shmode = (g->gmode == GM_SH) ? 1 : 0;
    g->pamode = 1;                     /* v0066 */
    g->msmode = 1;
    g->sendNumber = 1;                 /* v0313 */
    //    strcpy(g->send_host,telio_host);
    //    g->send_port = 5700+g->gnum-1;     /* v0316 */ /*maco*/
    g->stored_tf1 = 0.5f; g->stored_tf3 = 1.0f;
    g->stored_send = 0; g->stored_av = 0; g->stored_mode = 1;
    strcpy(g->lastCommand,"");
    strcpy(g->command_msg,"");
    /* setup 'status' structure */
    FITSpars *st = &g->status;
    sprintf(buf,FMT_RUN,g->gnum);
    st->run = (int)get_long(setup_rc,buf,1);
    st->seqNumber = 0;
    get_string(setup_rc,DBE_PREFIX,buf,DEF_PREFIX);
    buf[12] = '\0';                    /* avoid compiler warning v0315 */
    /* Use stack buffer instead of malloc/free */
    char prefix_buf[16];
    strncpy(prefix_buf, buf, sizeof(prefix_buf)-1);
    prefix_buf[sizeof(prefix_buf)-1] = '\0';
    sprintf(st->prefix,"%s%d_",prefix_buf,g->gnum);
    strcpy(st->datapath,buffer); 
    strcpy(st->origin,"LCO/OCIW");
    strcpy(st->version,P_VERSION);
    strcpy(st->instrument,"unknown");
    st->dimx = st->dimy = baseD;
    st->binning = baseB;
    st->pixscale = g->px;              /* FITS header */
    st->exptime = 0.5f; 
    st->ca = modulo(g->angle+180.0,0,360);  /* v0316 */
    st->camera = g->gnum;                /* v0317 */
    st->rotn = sGuider.rPort;            /* v0317 */
    st->psize = 4.63f;                   /* [um] v0327 */
    strcpy(st->st_str,""); strcpy(st->ha_str,""); strcpy(st->tg_str,""); 
  } /* end guider initialization */

  /* create main-window ------------------------------------------- */
 
  sprintf(buf,"%s (v%s)",sGuider.status.instrument,P_VERSION);
  CXT_OpenMainWindow(&mwin,winpos,eWIDE,eHIGH,&hint,buf,P_TITLE,True);
  // XSynchronize(mwin.disp,True);
  CBX_SelectInput(&mwin,ExposureMask | KeyPressMask);
  (void)CBX_CreateGfC(&mwin,fontname,"bold",PXh);

  (void)CBX_CreateAutoDrop(&mwin,&fimenu,2,2,5*PXw,XXh,"File",cb_file);
    (void)CBX_AddMenuEntry(&fimenu,"Help",    0);
    (void)CBX_AddMenuEntry(&fimenu,NULL,      CBX_SEPARATOR);
    (void)CBX_AddMenuEntry(&fimenu,"Exit",    0);
 
  (void)CBX_CreateAutoDrop(&mwin,&opmenu,fimenu.x+fimenu.w+PXw/2,fimenu.y,
                           8*PXw,XXh,"Options",cb_options);
    (void)CBX_AddMenuEntry(&opmenu,"DataPath",0); // OPT_DATAPATH
    (void)CBX_AddMenuEntry(&opmenu,"Logfile",0);  // OPT_LOGFILE
    (void)CBX_AddMenuEntry(&opmenu,NULL,CBX_SEPARATOR);
    (void)CBX_AddMenuEntry(&opmenu,"Flip-X",0);   // OPT_FLIP_X
    (void)CBX_AddMenuEntry(&opmenu,"Flip-Y",0);   // OPT_FLIP_Y

  w = 11*PXw;
  x = wINFO - w - PXw/3;
  y = fimenu.y;
  CBX_CreateAutoOutput(&mwin,&utbox,x,y,w,XXh,"UT");
  x -= w + PXw;
  CBX_CreateAutoOutput(&mwin,&rtbox,x,y,w,XXh,"et");

  /* Create guider controls */
  {
    Guider *g = &sGuider;
    g->win = mwin.win;                 /* one guider window v0400 */
    /* 1st column ------------------------------------------------- */
    Window p = g->win;                 /* parent */
    x = lSIZE+6;
    y = 1+XXh+XXh/3-1;
    w  = (17*PXw)/2;                   /* v0400 */
    CBX_CreateAutoOutput_Ext(&mwin,&g->tcbox,p,x,y,w,XXh,"tc 0");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->mxbox,p,x,y,w,XXh,"mx 0");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->bkbox,p,x,y,w,XXh,"bk 0");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->fwbox,p,x,y,w,XXh,"fw 0");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->dxbox,p,x,y,w,XXh,"dx 0");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->dybox,p,x,y,w,XXh,"dy 0");
    /* 2nd column ------------------------------------------------- */
    x += g->tcbox.w + PXw/2;           
    y  = g->tcbox.y;
    w  = (15*PXw)/2;
    CBX_CreateAutoOutput_Ext(&mwin,&g->gdbox,p,x,y,w,XXh,"gd off");
    y += XXh+PXh/3;   
    CBX_CreateAutoOutput_Ext(&mwin,&g->snbox,p,x,y,w,XXh,"sn    0");
    y += XXh+PXh/3; 
    sprintf(buf,"px  %03d",(int)my_round(1000.0*g->px,0));
    CBX_CreateAutoOutput_Ext(&mwin,&g->pxbox,p,x,y,w,XXh,buf);
    y += XXh+PXh/3;
#ifdef PA_BOX 
    d  = 3*PXw;
    CBX_CreateAutoOutput_Ext(&mwin,&g->pabox,p,x+d,y,w-d,XXh,"    0");
#else
    CBX_CreateAutoOutput_Ext(&mwin,&g->pabox,p,x,y,w,XXh,"pa    0");
#endif
    /* 3rd column ------------------------------------------------- */
    x += g->gdbox.w + PXw/2;
    y  = g->gdbox.y;
    w  = (11*PXw)/2;
    CBX_CreateAutoOutput_Ext(&mwin,&g->gmbox,p,x,y,w,XXh,"gm");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->gabox,p,x,y,w,XXh,"ga  1");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->fmbox,p,x,y,w,XXh,"fm  1");
    y += XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->mmbox,p,x,y,w,XXh,"mm  0");
    /* 4th column ------------------------------------------------- */
    w = (17*PXw)/2;
    x = wINFO - w - PXw/3; 
    y = g->gmbox.y;
    CBX_CreateAutoOutput_Ext(&mwin,&g->csbox,p,x,y,w,XXh,"Stp=1.0");
    w = (11*PXw)/2;
    x = g->csbox.x - w - PXw/2;
    CBX_CreateAutoOutput_Ext(&mwin,&g->bxbox,p,x,y,w,XXh,"");
    /* graphs ----------------------------------------------------- */
    x = 2;
    y = g->dybox.y+XXh+3;
    w = (wINFO-x-1*PXw)/2;
    h = pHIGH;
    d = w/2;
    g->g_tc = graph_create(&mwin,g->win,fontname,"tc",x,y,w,h,1,d);
    graph_scale(g->g_tc,0,10000,0);
    x = wINFO -w - PXw/3;
    g->g_fw = graph_create(&mwin,g->win,fontname,"fw",x,y,w,h,1,d);
    graph_scale(g->g_fw,0.0,2.0,0);
    y += h + 3;
    x = 1;
    g->g_az = graph_create(&mwin,g->win,fontname,"AZ",x,y,w,h,1,d);
    graph_scale(g->g_az,-1.0,1.0,0x01);
    x = wINFO -w - PXw/3;
    g->g_el = graph_create(&mwin,g->win,fontname,"EL",x,y,w,h,1,d);
    graph_scale(g->g_el,-1.0,1.0,0x01);
    /* exposure controls ------------------------------------------ */
    y = y + pHIGH + 4;
    if (vertical) y+= baseI+2;
    sprintf(buf,"%.2f",g->status.exptime);
    CBX_CreateAutoOutput_Ext(&mwin,&g->tfbox,g->win,1+2*PXw,y,9*PXw/2,XXh,buf);
    CBX_CreateAutoOutput_Ext(&mwin,&g->avbox,g->win,
                             g->tfbox.x+g->tfbox.w+3*PXw,  y,3*PXw,XXh,"0");
    CBX_CreateAutoOutput_Ext(&mwin,&g->dtbox,g->win,
                             g->avbox.x+g->avbox.w+6*PXw,  y,3*PXw,XXh,"-");
    sprintf(buf,"%d",g->sendNumber-1); 
    CBX_CreateAutoOutput_Ext(&mwin,&g->sqbox,g->win,
                             g->dtbox.x+g->dtbox.w+1*PXw,  y,5*PXw,XXh,buf);
    w = 5*PXw;
    CBX_CreateAutoOutput_Ext(&mwin,&g->fgbox,g->win,
                             wINFO-w-PXw/3              ,y,w,XXh,"0");
    CBX_CreateAutoOutput_Ext(&mwin,&g->fdbox,g->win,
                             g->fgbox.x-g->fgbox.w-1*PXw,y,w,XXh,"0");
    CBX_CreateAutoOutput_Ext(&mwin,&g->fpbox,g->win,
                             g->fdbox.x-g->fdbox.w-1*PXw,y,w,XXh,"0");
    y += XXh+PXh/3;
    w  = g->fpbox.x-2*PXw-1;                          /* v0313 */
    CBX_CreateAutoOutput_Ext(&mwin,&g->cmbox,g->win,1,y,w,XXh,"_");
    CBX_WindowBorder_Ext(mwin.disp,g->cmbox.win,app->green);
    CBX_CreateAutoOutput_Ext(&mwin,&g->tmbox,g->win,
                             g->fdbox.x,y,g->fgbox.w,XXh,"");
    CBX_CreateAutoOutput_Ext(&mwin,&g->cpbox,g->win,  /* v0313 */
                             g->fgbox.x,y,g->fgbox.w,XXh,"");
    x  = 1;
    y += XXh+PXh/3;
    w  = g->fpbox.x+g->fpbox.w-x;
    n_msg = (eHIGH-y-3)/XXh;
    g->msbox = (EditWindow*)malloc(n_msg*sizeof(EditWindow));
    g->led   = (Led*)malloc(n_msg*sizeof(Led));
    d = PXw+6;
    for (j=0; j<n_msg; j++) {
      CBX_CreateAutoOutput_Ext(&mwin,&g->msbox[j],g->win,x+d,y,w-d,XXh,NULL);
      CBX_CreateLed_Ext(&mwin,&g->led[j],g->win,x+2,y+4,PXw,PXw,app->lgrey); 
      y += XXh;
    }
    /* additional output v0322 */
    w = 10*PXw;
    x = wINFO-w-PXw/3;
    y = eHIGH-XXh-4;
    CBX_CreateAutoOutput_Ext(&mwin,&g->gnbox,g->win,x,y,w,XXh,"gain");
    y -= XXh+PXh/3;
    CBX_CreateAutoOutput_Ext(&mwin,&g->smbox,g->win,x,y,w,XXh,"sm       0");
    /* quicklook tool --------------------------------------------- */
    x = (vertical) ? (wINFO-baseI)/2 : wINFO;
    y = (vertical) ? g->tfbox.y-baseI-6 : 2;
    g->qltool = qltool_create(&mwin,g->win,fontname,
                              x,y,baseI,
                              fimenu.y+fimenu.h+4,lSIZE,
                              g->bxbox.x,g->bxbox.y+XXh+PXh/3,
                              g->smbox.x,g->msbox[0].y,g->smbox.w, 
                              g->status.dimx);
    g->qltool->gmode = g->gmode;
    if (g->lmag > 0) g->qltool->lmag = g->lmag;  /* overwrite v0415 */
    if (g->pct  > 0) { sprintf(buf,"%d",g->pct); qltool_scale(g->qltool,"pct",buf,""); }
    if (g->bkg  > 0) { sprintf(buf,"%d",g->bkg); qltool_scale(g->qltool,"bkg",buf,""); }
    if (g->span > 0) { sprintf(buf,"%d",g->span); qltool_scale(g->qltool,"spa",buf,""); }
    if (g->bx   > 0) { sprintf(buf,"bx %d",g->bx); handle_command(g,buf,0); }
    else sprintf(g->bxbox.text,"bx %2d",1+2*g->qltool->vrad);
  } /* end guider controls */
  done = False;

  sprintf(paString,"PA = %.1f %cELEV %cROTE (%c)",  /* v0315 */
         sGuider.angle,
         (sGuider.elsign > 0) ? '+' : (sGuider.elsign < 0) ? '-' : '0',  
         (sGuider.rosign > 0) ? '+' : (sGuider.rosign < 0) ? '-' : '0',
         (sGuider.parity > 0) ? '+' : '-');   /* v0351 */
  message(&sGuider,paString,MSS_INFO);   /* v0311 */

  /* -------------------------------------------------------------- */

  eds_open(2);
  /* setup GUI */
  {
    Guider *g = &sGuider;
    // redraw_gwin(g);
    // CBX_SendExpose_Ext(mwin.disp,g->win);
    set_fm  (g,1);
    set_mm  (g,1);                     /* mouse mode v0313 */
    set_sens(g,g->sens);               /* guider sensitivity */
    set_pa  (g,360.0,0);
    set_gm  (g,g->gmode,0);
    assert(g->server);
    if (g->server) thread_detach(run_init,g);
    for (j=0; j<QLT_NCURSORS; j++) {   /* v0346 */
      float x = g->qltool->curx[j];
      float y = g->qltool->cury[j];
      eds_send82i(g->gnum,1+j,x,y);
    }
  }
  eds_close(); 

  /* this loop drives the program --------------------------------- */

  while (!done) {                      /* main loop */
    while (!CBX_AutoEvent(&mwin,&event,&sleeptime)) { /* no event */
      CBX_Sleep(&sleeptime,5,350);
      update_ut();
      if (done) break;                 /* break main loop */
    }
    if (done) break;                   /* break main loop */
#if (DEBUG > 3)
    fprintf(stderr,"%s: event.type=%d\n",__FILE__,event.type);
#endif
    switch(event.type) {
    case Expose:                       /* main window exposed  */
      if (event.xexpose.count == 0) {  /* if last event in queue */
        if (event.xexpose.window == mwin.win) {
          redraw_text();               /* redraw everything */
          break;
        }
        {
          Guider *g = &sGuider;
          if (event.xexpose.window == g->win) { 
            redraw_compass(g);
            // implicit redraw_gwin(g); 
            break; 
          }
          if (qltool_event(g->qltool,&event)) break;
          if (graph_event(g->g_tc,&event)) break;
          if (graph_event(g->g_fw,&event)) break;
          if (graph_event(g->g_az,&event)) break;
          if (graph_event(g->g_el,&event)) break;
        }
      }
      break;
    case MapNotify:                    /* window mapped */
    case UnmapNotify:                  /* window unmapped */
#if (DEBUG > 0)
      printf("%s: Un/Map Notify\n",__FILE__);
#endif
      break;
    case DestroyNotify:                /* WM closed window */
#if (DEBUG > 0)
      printf("%s: DestroyNotify on window=%d\n",__FILE__,
             (int)event.xdestroywindow.window);
#endif
      break;
    case MotionNotify:                 /* Mouse */
#if (DEBUG > 0)
      printf("%s: Motion Notify\n",__FILE__);
#endif
      /* ignore */
      break;
    case ReparentNotify:               /* MacOS */
#if (DEBUG > 0)
      printf("%s: Reparent Notify\n",__FILE__);
#endif
      /* ignore */
      break;
    case ConfigureNotify:              /* window moved */
#if (DEBUG > 0)
      printf("%s: Configure Notify\n",__FILE__);
#endif
      /* ignore */
      break;
    case KeyPress:                     /* keyboard events */
      {
        Guider *g = &sGuider;
        if (event.xkey.window == g->win) { 
          int b = cursor_blocked(g,g->qltool->cursor_mode,0);
          if (qltool_handle_key(g->qltool,(XKeyEvent*)&event,b)) {
            if (b) {                   /* no cursor4/5 v0417 */
              printf("cursor motion blocked while guiding in gm5\n");
            } else {
              int c = g->qltool->cursor_mode;
              float x = g->qltool->curx[c];
              float y = g->qltool->cury[c];
              eds_send82i(g->gnum,1+c,x,y); /* v0346 */
            }
            break;
          }
          if (handle_key(g,&event)) break;
        } 
      }
      break;
    case ButtonPress:                  /* mouse button pressed */
      if (event.xbutton.button == 1) { /* left button */
        {
          Guider *g = &sGuider;
          if (g->init_flag != 1) break;
          if (event.xbutton.window == g->qltool->iwin) {
            if (require_control_key) {
              if (!(event.xbutton.state & ControlMask)) {
                message(g,"Control key required",MSS_WARN);
                break; 
              }
            }
            handle_msmode(g,&event);
	    int c = g->qltool->cursor_mode;
	    float x = g->qltool->curx[c];
	    float y = g->qltool->cury[c];
	    eds_send82i(g->gnum,1+c,x,y); /* v0346 */
            break;
          }
        }
      } /* endif(left button) */
      break;
#if (DEBUG > 1)
    default:
      fprintf(stderr,"unknown event '%d'\n",event.type);
      break;
#endif
    } /* endswitch(event.type) */
  } /* while(!done) */                 /* end of main-loop */

  /* free up all the resources and exit --------------------------- */

  CBX_CloseMainWindow(&mwin);

  if (sGuider.server) {
    zwo_free(sGuider.server);
    sGuider.server = NULL;
  }

#if (TIME_TEST > 0)                    /* print cpu-time */
  { struct tms tmsbuf;
    (void)times(&tmsbuf);
    printf("%s(%d): total-cpu: %5.2f seconds\n",P_TITLE,(int)getpid(),
      (float)(tmsbuf.tms_stime)/(float)sysconf(_SC_CLK_TCK) +
      (float)(tmsbuf.tms_utime)/(float)sysconf(_SC_CLK_TCK));
  }
#endif
 
  CXT_CloseLib(True);
 
  return 0;
}

/* ---------------------------------------------------------------- */

static void draw_compass(Guider* g,int x,int y,int r,double north,double east,
                         u_long color)
{
  Display *disp=mwin.disp;
  Window  win=g->win;
  GC      gc=mwin.gfs.gc;
  int     x2,y2;

  CBX_Lock(0);                         /* simplified/fixed by [PP] NEW v0425 */
  XDrawArc(disp,win,gc,x-r,y-r,2*r,2*r,0,23040);
  XSetLineAttributes(disp,gc,2,LineSolid,CapRound,JoinRound);
  XSetForeground(disp,gc,color);
  x2 = x + ((g->qltool->flip_x) ? -1 : 1)*(int)floor(r*sin(north/RADS)+0.5);
  y2 = y - ((g->qltool->flip_y) ? -1 : 1)*(int)floor(r*cos(north/RADS)+0.5);
  XDrawLine(disp,win,gc,x,y,x2,y2);
  x2 = x + ((g->qltool->flip_x) ? -1 : 1)*(int)floor(0.5*r*sin(east/RADS)+0.5);
  y2 = y - ((g->qltool->flip_y) ? -1 : 1)*(int)floor(0.5*r*cos(east/RADS)+0.5);
  XDrawLine(disp,win,gc,x,y,x2,y2);
  XSetLineAttributes(disp,gc,1,LineSolid,CapRound,JoinRound);
  XSetForeground(disp,gc,app->black);
  CBX_Unlock();
}

/* --- */

static void redraw_compass(Guider* g)
{
  int    x,y,r=XXh;
  double east,az,el;                   /* simplified/fixed by [PP] NEW v0425 */
  extern double sim_north;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p): pa=%.1f, para=%.1f\n",PREFUN,g,g->pa,para);
#endif
  assert(g != NULL);
  assert(g->pa != 0);

  CBX_Lock(0);
  XClearWindow(mwin.disp,g->win);
  CBX_Unlock();

  x = g->gdbox.x+g->gdbox.w/2;
  y = g->dybox.y - PXh/4;
  g->north = sim_north = g->parity*(-fabs(g->pa) + para);  /* N/E */
  east = g->north - g->parity*90.0;
  draw_compass(g,x,y,r,g->north,east,app->green); 

  x = g->gmbox.x+g->gmbox.w/2;
  el = g->parity *(-fabs(g->pa));         /* az,el */
  az = el + g->parity*90.0;
  draw_compass(g,x,y,r,el,az,app->red); 

  redraw_gwin(g);
}

/* ---------------------------------------------------------------- */

       void redraw_gwin(Guider *g)
{
  int     j,x,y;
  char    buf[128];
  Display *disp=mwin.disp;
  Window  win=g->win;
  GC      gc=mwin.gfs.gc;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif

  (void)CBX_Lock(0);
  XDrawString(disp,win,gc,g->tfbox.x-2*PXw,g->tfbox.y+PXh,"tf",2);
  XDrawString(disp,win,gc,g->avbox.x-2*PXw,g->avbox.y+PXh,"av",2);
  XDrawString(disp,win,gc,g->dtbox.x-4*PXw,g->dtbox.y+PXh,"send",4);
  XDrawString(disp,win,gc,g->fpbox.x-3*PXw,g->fpbox.y+PXh,"FpS",3);
  XDrawString(disp,win,gc,g->tmbox.x-4*PXw,g->tmbox.y+PXh,"Temp",4);
  for (j=0; j<QLT_NCURSORS; j++) {
    if (g->qltool->cursor_mode == j) XSetForeground(disp,gc,app->green);
    x = g->qltool->cubox[j].x-PXw;
    y = g->qltool->cubox[j].y+PXh;
    sprintf(buf,"%d",1+j);
    XDrawString(disp,win,gc,x,y,buf,strlen(buf));
    if (g->qltool->cursor_mode == j) XSetForeground(disp,gc,app->black);
  }
#ifdef PA_BOX
  if (g->pamode) XSetForeground(disp,gc,app->green);
  XDrawString(disp,win,gc,g->pabox.x-2*PXw-PXw/2,g->pabox.y+PXh,"pa",2);
  if (g->pamode) XSetForeground(disp,gc,app->black);
#endif
  CBX_Unlock();
  sprintf(g->csbox.text,"Stp=%.1f",g->qltool->cursor_step);
  CBX_UpdateEditWindow(&g->csbox);
}

/* --- */

static void redraw_text(void)
{
#if 0 // not used 
  Display *disp=mwin.disp;
  Window  win=mwin.win;
  GC      gc=mwin.gfs.gc;
#endif
#if (DEBUG > 2)
  fprintf(stderr,"%s: %s()\n",__FILE__,PREFUN);
#endif

  (void)CBX_Lock(0);

  CBX_Unlock();
}

/* ---------------------------------------------------------------- */

static void update_ut(void)
{
  char buf[128];
  static time_t ut_now=0,et_next=0,pa_last=0;
#if (DEBUG > 2)
  fprintf(stderr,"%s()\n",PREFUN);
#endif

  if (cor_time(0) == ut_now) return;   /* current time */
  ut_now = cor_time(0);

  sprintf(utbox.text,"UT %s",get_ut_timestr(buf,ut_now));
  CBX_UpdateEditWindow(&utbox); 

  if (ut_now >= et_next) { char buf[32]; /* update runtime */
    double runtime = (double)(ut_now - startup_time);
    strcpy(buf,"s"); et_next = ut_now+1;
    if (runtime > 120) {               /* 120 seconds */
      runtime /= 60.0; strcpy(buf,"m"); et_next += 5; /* 0.1 mins */
      if (runtime > 100)  {            /* 100 minutes */
        runtime /= 60.0; strcpy(buf,"h"); et_next += 354; /* 0.1 hrs */
        if (runtime > 96)  { runtime /= 24.0; strcpy(buf,"d"); }
      }
    }
    sprintf(rtbox.text,"et %.1f%s",runtime,buf);
    CBX_UpdateEditWindow(&rtbox); 
  }

  if (ut_now >= pa_last+pa_interval) {
    Guider *g = &sGuider;
    int wait = (g->gmode == GM_SV5) ? 5 : 30; /* if not guiding v0420 */
    if ((!g->qltool->guiding) && (ut_now-pa_last < wait)) return;
    if (g->pamode) {
      thread_detach(run_tele,NULL);
      pa_last = ut_now;
    }
  }
  
  if (ut_now >= disk_next) {           /* check disk */
    Guider *g = &sGuider;              /* takes about 0.5 msec */
    float file_system = checkfs(g->status.datapath); 
    if (file_system < 0.05) { char buf[128]; 
      sprintf(buf,"WARNING: disk %.1f%% full",100.0*(1.0-file_system));
      message(g,buf,MSS_WARN);
    }
    disk_next = ut_now+607;            /* every 10 minutes */
  }
}

/* ---------------------------------------------------------------- */
 
static void cb_file(void* param)
{
  switch (fimenu.sel) {  
  case FILE_HELP:
    { char cmd[512]; AMB *box;
      box = CBX_AsyncMessageOpen(P_TITLE ": startup browser",DEF_HELP);
      sprintf(cmd,"%s %s/%s &",genv2("BROWSER",DEF_BROWSER),DEF_HELP,MY_HELP);
      int err = system(cmd);           /* use 'err' v0315 */
      if (err) fprintf(stderr,"system(%s): err=%d\n",cmd,err);
      else sleep(3);
      CBX_AsyncMessageClose(box);
    }
    break;
  case FILE_EXIT:                      /* exit GUI */
    done = True;                       /* kill housekeeping loop */
    if (sGuider.loop_running) {
      if (!CBX_AlertBox("Exposure","Really exit?")) done = False;
    }
    if (done) my_shutdown(True);
    break;
  }
  CBX_ClearAutoQueue(&mwin);
}

/* ---------------------------------------------------------------- */

static void cb_options(void* param)
{
  char buffer[1024];
  Guider *g=&sGuider;

  switch (opmenu.sel) {
  case OPT_LOGFILE:                    /* set logfile name */
    CBX_AsyncMessageBox(P_TITLE,logfile);
    break;
  case OPT_DATAPATH:                   /* set save-path (data) */
    sprintf(buffer,"%s",sGuider.status.datapath);
    if (CBX_ParameterBox("Set Datapath",buffer)) {
      check_datapath(buffer,1);
      put_string(setup_rc,DBE_DATAPATH,buffer);
      strcpy(sGuider.status.datapath,buffer);
    }
    break;
  case OPT_FLIP_X:
    g->qltool->flip_x = 1-g->qltool->flip_x;
    opmenu.entry[OPT_FLIP_X].flag = (g->qltool->flip_x) ? CBX_CHECKED : 0;
    qltool_redraw(g->qltool,False);
    redraw_compass(g);
    break;
  case OPT_FLIP_Y:
    g->qltool->flip_y = 1-g->qltool->flip_y;
    opmenu.entry[OPT_FLIP_Y].flag = (g->qltool->flip_y) ? CBX_CHECKED : 0;
    qltool_redraw(g->qltool,False);
    redraw_compass(g);
    break;
  }
  CBX_ClearAutoQueue(&mwin);
}

/* ---------------------------------------------------------------- */

static int do_start(Guider* g,int wait) 
{
  int weDidIt=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif

  if (g->init_flag != 1) return -E_NOTINIT;

  if (!g->loop_running) { 
    message(g,"starting acquisition",MSS_INFO);
    thread_detach(run_cycle,g);
    while (wait > 0) {
      if (!g->loop_running) msleep(350);
      wait -= 350;
    }
    weDidIt = 1;
  }

  return weDidIt;
}

/* ---------------------------------------------------------------------- */

static int do_stop(Guider* g,int wait) 
{
  int weDidIt=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%s)\n",PREFUN,g->name);
#endif

  if (g->loop_running) {
    if (g->qltool->guiding) {
      pthread_mutex_lock(&g->mutex);
      g->qltool->guiding = 0;
      pthread_mutex_unlock(&g->mutex);
      msleep(350);
    } 
    g->stop_flag = True;
    message(g,"stopping acquisition",MSS_INFO);
    while (wait > 0) {
      if (g->loop_running) msleep(350);
      wait -= 350;
    }
    weDidIt = 1;
  }
  return weDidIt;
}

/* ---------------------------------------------------------------------- */

static int handle_msmode(void* param,XEvent* event)
{
  int  err=0;
  char buf[128]="";
  Guider *g = (Guider*)param;

  int    x = event->xbutton.x;
  int    y = event->xbutton.y;
  double r = (event->xbutton.state & Button3Mask) ? 1.0 : 0.1;

  switch (g->msmode) {       /* mouse mode */
  case 1:                    /* move cursor to mouse position */
    sprintf(buf,"box: to mouse position (%.1f)",r);
    qltool_cursor_set(g->qltool,QLT_BOX,x,y,r);
    break;
  case 2: case-2:            /* move probe(s) */
    if (g->msmode > 0) sprintf(buf,"probe-%d offset (%.1f)",g->gmode,r);
    else               sprintf(buf,"coordinated offset (%.1f)",r);
    err = telio_open(2); 
    if (!err) {
      if (g->pamode) err = set_pa(g,g->pa,1);
      if (!err) { double dx,dy,da,de;
        qltool_cursor_off(g->qltool,QLT_BOX,x,y,r,&dx,&dy);
        rotate(g->px*dx,g->px*dy,g->pa,g->parity,&da,&de);
        if (g->msmode > 0) {
	  err = telio_gpaer(g->gnum,da,de);
	} else {
	  err = telio_gpaer(3,-da,-de);
	  if (!err)  err = telio_aeg(da,de);
	}
      }
      telio_close();
    }
    break;
  case 3:                    /* move telescope */
    sprintf(buf,"tele: star at mouse to box (%.1f)",r); 
    err = telio_open(2); 
    if (!err) {
      if (g->pamode) err = set_pa(g,g->pa,1);
      if (!err) { double dx,dy,da,de;
        qltool_cursor_off(g->qltool,QLT_BOX,x,y,r,&dx,&dy);
        rotate(g->px*dx,g->px*dy,g->pa,g->parity,&da,&de);
        err = telio_aeg(da,de);
      }
      telio_close();
    }
    break;
  default:
    fprintf(stderr,"%s: invalid mouse mode %d\n",P_TITLE,g->msmode);
    break;
  }
  if (*buf) message(g,buf,MSS_WINDOW);
  if (err) {
    sprintf(buf,"errorCode= %d",err); message(g,buf,MSS_ERROR);
  }

  return err;
}

/* ---------------------------------------------------------------------- */

static int handle_key(void* param,XEvent* event) 
{
  Guider *g=(Guider*)param;
  int r=1,i;
  static const float steps[]={0,0.1f,1.0f,4.0f,40.0f,0};
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%p)\n",PREFUN,param,event);
#endif

  CBX_Lock(0);
  char *keys = XKeysymToString(XLookupKeysym((XKeyEvent*)event,0));
  CBX_Unlock();

  if (!strcmp(keys,"F1")) {            /* stop guiding */
    handle_command(g,"fone",1);
  } else
  if (!strcmp(keys,"F2")) {            /* calculate here */
    handle_command(g,"ftwo",1);
  } else
  if (!strcmp(keys,"F3")) {            /* guide here */
    handle_command(g,"fthr",1);
  } else 
  if (!strcmp(keys,"F4")) {            /* center -- calculate  */
    handle_command(g,"ffou",1);
  } else
  if (!strcmp(keys,"F5")) {            /* center -- guide */
    handle_command(g,"ffiv",1);
  } else 
  if (!strcmp(keys,"F9")) {
    handle_command(g,"fnin",1);
  } else 
  if (!strcmp(keys,"F10")) {
    handle_command(g,"ften",1);
  } else 
  if (!strcmp(keys,"Insert") || !strcmp(keys,"Delete") ||
      !strcmp(keys,"Prior") || !strcmp(keys,"Next")) { /* cursor step */
    for (i=0; i<(sizeof(steps)/sizeof(float)); i++) {
      if (g->qltool->cursor_step == steps[i]) break;
    }
    if (!strcmp(keys,"Insert") || !strcmp(keys,"Prior")) {
      if (steps[i+1] != 0) g->qltool->cursor_step = steps[i+1];
    } else {
      if (steps[i-1] != 0) g->qltool->cursor_step = steps[i-1];
    }
    redraw_gwin(g);
  } else {
    // printf("%s(): keys=%s\n",PREFUN,keys);
    r = 0;
  }
  if (r == 0) {                        /* not a special key */
    if (event->xkey.state & ControlMask) { /* Control key is pressed */
      if (!strcmp(keys,"1") || !strcmp(keys,"2") || !strcmp(keys,"3") ||
          !strcmp(keys,"4") || !strcmp(keys,"5")) {
        g->qltool->cursor_mode = atoi(keys)-1;
        redraw_gwin(g);
        qltool_redraw(g->qltool,False);
      }
      r = 1;
    }
  }
  if (r == 0) {                        /* redirect to command edit */
    if (g->cmbox.cpos > 0) {           /* remove '_' */
      g->cmbox.cpos--; g->cmbox.text[g->cmbox.cpos] = '\0'; 
    }
    int flag = CBX_HandleEditWindow(&g->cmbox,(XKeyEvent*)event);
    if (flag) {                        /* <Enter> or <Return> */
      if (strlen(g->cmbox.text) > 0) {
        handle_command(g,g->cmbox.text,1);
        strcpy(g->cmbox.text,"");
        CBX_ResetEditWindow(&g->cmbox);
        // IDEA g->cmbox.cpos = 1; strcpy(g->cmbox.text,"_");
      }
    } else {
      g->cmbox.cpos += 1; strcat(g->cmbox.text,"_");
      CBX_UpdateEditWindow(&g->cmbox);
    }
  }

  return r;
}

/* ---------------------------------------------------------------- */

static int handle_command(Guider* g,const char* command,int showMsg)
{
  int  err=0,n=0;
  char cmd[128]="",par1[128]="",par2[128]="",par3[128]="";
  char buf[256],*msgstr;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%s)\n",PREFUN,g->name,command);
#endif

  msgstr = g->command_msg; *msgstr = '\0';  /* clear return v0333 */

  if (!strncmp(command,"!",1)) {       /* v0323 IDEA command history */
    handle_command(g,g->lastCommand,showMsg);
    return 0;
  }

  if (strlen(command) < 32) {          /* v0065 */
    char *p1=(char*)command,*p2=buf;   /* separate parameter from command */
    do {                               /* eg. tf1 --> tf 1 */
      if (isupper(*p1) != islower(*p1)) {
        *p2 = *p1; p1++; p2++;
      } else {
        if (*p1 != ' ') { *p2 = ' '; p2++; }
        strcpy(p2,p1); 
        break;
      }
    } while (*p1);
    *p2 = *p1;
    if (showMsg) message(g,buf,MSS_WINDOW);
    n = sscanf(buf,"%s %s %s %s",cmd,par1,par2,par3);
  }

  if (!strcasecmp(cmd,"apa")) {        /* toggle camera position angle */
    if (*par1) {
      g->pamode = 1; pa_interval = imax(2,imin(120,atoi(par1))); /* v0420 */
    } else {
      g->pamode = (g->pamode) ? 0 : 1;
    }
    if (g->pamode) set_pa(g,g->pa,1);  /* get posAngle from TCS */
#ifdef PA_BOX
    redraw_gwin(g); 
#else
    g->pabox.fg = (g->pamode) ? app->green : app->black;
    CBX_UpdateEditWindow(&g->pabox);
#endif
  } else
  if (!strncasecmp(cmd,"av",2)) {      /* AVF,AVG */
    if (n >= 2) set_av(g,atoi(par1));
  } else 
  if (!strcasecmp(cmd,"bf") || !strcasecmp(cmd,"bg")) {
    err = E_NOTIMP;
  } else 
  if (!strcasecmp(cmd,"bx")) {         // guider box size
    if (n >= 2) g->qltool->vrad = imax(7,(atoi(par1)-1)/2);
    sprintf(msgstr,"%d",1+2*g->qltool->vrad); 
    sprintf(g->bxbox.text,"bx %2d",1+2*g->qltool->vrad); // bug-fix v0408
    CBX_UpdateEditWindow(&g->bxbox);
  } else
  if (!strcasecmp(cmd,"close")) {      /* to recover using "reset" */
    if (g->loop_running) do_stop(g,5000);
    zwo_close(g->server);
    g->init_flag = 0;
  } else
  if (!strcasecmp(cmd,"exit")) {       /* exit program */
    my_shutdown(True);
  } else
  if (!strcasecmp(cmd,"fone")) {       /* F1 */
    /* F2 F4 & F5 are not blocked by fmode 2 and so should be turned off with F1
       F4 and F5 move the box which could be a problem for SH? block? */
    //    if (g->fmode == 1) {               /* fm1 */
    pthread_mutex_lock(&g->mutex);
    g->qltool->guiding = 0;
    pthread_mutex_unlock(&g->mutex);
    if (g->gid) { pthread_join(g->gid,NULL); g->gid = 0; }
    sprintf(g->gdbox.text,"gd  off"); 
    g->gdbox.fg = app->black;
    CBX_UpdateEditWindow(&g->gdbox);
    eds_send801(g->gnum,0,g->qltool->guiding,0,0,0);
     //    } else
    if (g->fmode == 2) {               /* fm2 v0329 */
      if (g->stored_mode == 3) {       /* v0330 */
        g->stored_tf3 = g->server->expTime; /* store 'F3' values */
        g->stored_av = g->server->rolling;
        g->stored_send = g->send_flag;
        set_sf(g,0);                   /* set 'F1' values */
        set_exptime(g,g->stored_tf1);
        set_av(g,0);
      }
      g->stored_mode = 1;
    }
  } else
  if (!strcasecmp(cmd,"ftwo")) {       /* F2 */
    if (g->loop_running) {             /* implies (init_flag==1) */
      pthread_mutex_lock(&g->mutex);
      g->qltool->guiding = -2;
      pthread_mutex_unlock(&g->mutex);
      if (!g->gid) thread_create(run_guider,g,&g->gid);
      sprintf(g->gdbox.text,"gd calc"); CBX_UpdateEditWindow(&g->gdbox);
    } else err = E_NOTACQ;
  } else 
  if (!strcasecmp(cmd,"fthr")) {       /* F3 */
    if (g->fmode == 1) {               /* fm1 */
      if (g->loop_running) {           /* implies (init_flag==1) */
        pthread_mutex_lock(&g->mutex);
        g->qltool->guiding = -3;
        pthread_mutex_unlock(&g->mutex);
        if (!g->gid) thread_create(run_guider,g,&g->gid); 
        sprintf(g->gdbox.text,"gd move"); CBX_UpdateEditWindow(&g->gdbox);
      } else err = E_NOTACQ;
    } else 
    if (g->fmode == 2) {               /* fm2 v0329 */
      if (g->stored_mode == 1) {       /* v0330 */
        g->stored_tf1 = g->server->expTime; /* store 'F1' values */
        set_exptime(g,g->stored_tf3);       /* set 'F3' values */
        set_av(g,g->stored_av);
        set_sf(g,g->stored_send);
      }
      g->stored_mode = 3;
    }
  } else
  if (!strcasecmp(cmd,"ffou")) {       /* F4 */
    if (g->loop_running) {             /* implies (init_flag==1) */
      pthread_mutex_lock(&g->mutex);
      g->qltool->guiding = -4;
      pthread_mutex_unlock(&g->mutex);
      if (!g->gid) thread_create(run_guider,g,&g->gid);
      sprintf(g->gdbox.text,"gd calc"); CBX_UpdateEditWindow(&g->gdbox);
    } else err = E_NOTACQ;
  } else
  if (!strcasecmp(cmd,"ffiv")) {       /* F5 */
    if (g->loop_running) {             /* implies (init_flag==1) */
      pthread_mutex_lock(&g->mutex);
      g->qltool->guiding = -5;
      pthread_mutex_unlock(&g->mutex);
      if (!g->gid) thread_create(run_guider,g,&g->gid);
      sprintf(g->gdbox.text,"gd move"); CBX_UpdateEditWindow(&g->gdbox);
    } else err = E_NOTACQ;
  } else
  if (!strcasecmp(cmd,"fnin")) {       /* F9 remove SH v0329/v0331 */
    sprintf(buf,"gpfld%d",g->gnum);    /* bug-fix v0418 */
    err = telio_command(buf);
    if (err) err = E_TELIO;
  } else
  if (!strcasecmp(cmd,"ften")) {       /* F10 insert SH v0329/v0331 */
    sprintf(buf,"gpsha%d",g->gnum);    /* bug-fix v0418 */
    err = telio_command(buf);
    if (err) err = E_TELIO;
  } else
  if (!strcasecmp(cmd,"gm")) {         /* v0313 */
    if (*par1) {                       /* set 'gm' v0326 */
      int m = atoi(par1);
      if ((m < 1) || (m > GM_MAX)) {
        message(g,"invalid guider mode",MSS_WARN);
      } else
      if (g->qltool->guiding) {        /* not while guiding v0349 */
        message(g,"cannot change 'gm' while guiding",MSS_WARN);
      } else {
        set_gm(g,m,*par2);
      }
    }
  } else
  if (!strcasecmp(cmd,"fm")) {         /* function mode v0329 */
    if (n >= 2){
      if (isdigit(par1[0])) {
        set_fm(g,atoi(par1));
      } else {
        switch (par1[0]) {
        case 'g': set_fm(g,1); break;  /* guider */
        case 's': set_fm(g,2); break;  /* SH */
        default: message(g,"invalid function mode",MSS_WARN);
        }
      }
    }
  } else
  if (!strcasecmp(cmd,"mm")) {         /* mouse mode */
    if (isdigit(par1[0]) || (par1[0] == '-')) {
      set_mm(g,atoi(par1));
    } else {
      switch (par1[0]) {
      case 'b': set_mm(g,1); break;  /* box */
      case 'p': set_mm(g,2); break;  /* probe */
      case 'c': set_mm(g,-2); break; /* coordinated probe and telescope */
      case 't': set_mm(g,3); break;  /* telescope */
      default: message(g,"invalid mouse mode",MSS_WARN);
      }
    }
  } else
  if (!strncasecmp(cmd,"parity",3)) {  /* todo remove v0417 */
    if (*par1) {
      if (g->qltool->guiding) {
        message(g,"cannot change 'parity' while guiding",MSS_WARN);
      } else {
        g->parit2 = (atof(par1) < 0) ? -1.0 : +1.0;
      }
    }
    sprintf(msgstr,"%+.0f",g->parit2);
  } else
  if (!strcasecmp(cmd,"ca")) {         /* camera angle */
    set_ca(g,atof(par1));
  } else
  if (!strcasecmp(cmd,"pa")) {         /* position angle */
    set_pa(g,atof(par1),0);
  } else
  if (!strcasecmp(cmd,"px")) {         /* pixel scale */
    if (n > 1) g->px = fmax(0.001,atof(par1)/1000.0); /* v0415 */
    int px = (int)my_round(1000.0*g->px,0);
    sprintf(g->pxbox.text,(px >= 1000) ? "px %d" : "px  %03d",px);
    CBX_UpdateEditWindow(&g->pxbox);
  } else 
  if (!strcasecmp(cmd,"send")) {       /* send frame via TCP/IP */
    if (g->init_flag == 1) {
      g->send_flag = (n > 1) ? atoi(par1) : -1;  /* -1: single send */
      set_sf(g,g->send_flag);
    } else { 
      err = E_NOTINIT;
    }
  } else
  if (!strcasecmp(cmd,"sh")) {         /* Shack-Hartman mode */
    if (n > 1) g->shmode = atoi(par1); /* just a flag in the FITS header */
     else      err = E_MISSPAR;        /* missing parameter */
  } else
  if (!strcasecmp(cmd,"sn")) {         /* guider sensitivity */
    set_sens(g,atof(par1));
  } else
  if (!strcasecmp(cmd,"span") || !strcasecmp(cmd,"zero") ||
      !strcasecmp(cmd,"pct")  || !strcasecmp(cmd,"bkg")) {
    qltool_scale(g->qltool,cmd,par1,par2);
  } else 
  if (!strcasecmp(cmd,"tec")) {        /* cooler setting */
    if (*par1) {
      err = zwo_temperature(g->server,par1); 
      if (!strcmp(par1,"off") || !strcmp(par1,"fan_off") || !strcmp(par1,"fan_on")) {
        g->house_running = False;
        strcpy(g->cpbox.text,"");      /* v0313 */
        CBX_UpdateEditWindow(&g->cpbox);
      } else {
        thread_detach(run_housekeeping,(void*)g);
      }
    } else {                           /* no parameter */
      err = zwo_temperature(g->server,NULL); 
      sprintf(buf,"temp=%.1f setp=%.0f cooler=%.0f",g->server->tempSensor,
        g->server->tempSetpoint,g->server->coolerPercent); 
      message(g,buf,MSS_WINDOW);
    }
  } else
  if (!strcasecmp(cmd,"tf") || !strcasecmp(cmd,"tg")) {
    if (n >= 2) set_exptime(g,atof(par1));
  } else
  if (!strcasecmp(cmd,"xy")) {         /* activate cursor 1-5 */
    int j = atoi(par1)-1;
    if ((j >= 0) && (j < QLT_NCURSORS)) { 
      g->qltool->cursor_mode = j;      /* make active */
    }
    redraw_gwin(g);
  } else 
  if (!strcasecmp(cmd,"xys")) {        /* set cursor gm5 */
    if (n > 3) {
      int j = imax(0,imin(QLT_NCURSORS-1,atoi(par1)-1));
      if (!cursor_blocked(g,j,1)) {    /* v0421 */
        g->qltool->curx[j] = (float)atof(par2);
        g->qltool->cury[j] = (float)atof(par3);
        g->qltool->cursor_mode = j;    /* make active */
      }
    } else 
    if (n > 2) {
      if (!cursor_blocked(g,g->qltool->cursor_mode,1)) {
        g->qltool->curx[g->qltool->cursor_mode] = (float)atof(par1);
        g->qltool->cury[g->qltool->cursor_mode] = (float)atof(par2);
      }
    } else err = E_MISSPAR;
    if (!err) {
      redraw_gwin(g);
      qltool_redraw(g->qltool,False);
      float x = g->qltool->curx[g->qltool->cursor_mode];
      float y = g->qltool->cury[g->qltool->cursor_mode];
      eds_send82i(g->gnum,1+g->qltool->cursor_mode,x,y); /* v0347 */
    }
  } else
  if (!strcasecmp(cmd,"xyr")) {        /* move cursor 1-5 */
    if (n > 3) {
      int j = imax(0,imin(QLT_NCURSORS-1,atoi(par1)-1));
      if (!cursor_blocked(g,j,1)) {    /* v0421 */
        g->qltool->curx[j] += (float)atof(par2);
        g->qltool->cury[j] += (float)atof(par3);
        g->qltool->cursor_mode = j;    /* make active */
      }
    } else 
    if (n > 2) { 
      if (!cursor_blocked(g,g->qltool->cursor_mode,1)) {
        g->qltool->curx[g->qltool->cursor_mode] += (float)atof(par1);
        g->qltool->cury[g->qltool->cursor_mode] += (float)atof(par2);
      }
    } else err = E_MISSPAR;
    if (!err) {
      redraw_gwin(g);
      qltool_redraw(g->qltool,False);
      float x = g->qltool->curx[g->qltool->cursor_mode];
      float y = g->qltool->cury[g->qltool->cursor_mode];
      eds_send82i(g->gnum,1+g->qltool->cursor_mode,x,y); /* v0347 */
    }
  } else
  /* --- extended commmand set ------------------------------------ */
  if (!strncasecmp(cmd,"lmag",3)) {    /* lupe magnification */
    if (*par1) qltool_lmag(g->qltool,atoi(par1));
    else       sprintf(msgstr,"%d",g->qltool->lmag);
  } else
  if (!strncasecmp(cmd,"lut",3)) {     /* color lookup table */
    if (n >= 2) qltool_lut(g->qltool,par1);
    else        err = E_MISSPAR;
  } else
  if (!strncasecmp(cmd,"curcol",4)) {  /* cursor color v0321 */
    if (!strcmp(par1,"dark")) g->qltool->cursor_dark = 1;
    else                      g->qltool->cursor_dark = 0;
  } else
  if (!strncasecmp(cmd,"magpix",4)) {  /* show pixel values in mag.center */
    showMagPix = 1-showMagPix;         /* toggle */
  } else 
  if (!strncasecmp(cmd,"scale",3)) {   /* scaling mode */
    qltool_scale(g->qltool,par1,par2,par3);
  } else
  if (!strncasecmp(cmd,"dt",2)) {      /* display throttle */
    if (*par1) throttle = fmax(0,atof(par1));
    else               sprintf(msgstr,"%.0f",throttle); 
  } else
  if (!strncasecmp(cmd,"smooth",2)) {  /* smooth just displayed image */
    g->qltool->smoothing = abs(atoi(par1));
    sprintf(g->smbox.text,"sm %7d",g->qltool->smoothing);
    CBX_UpdateEditWindow(&g->smbox);
  } else
  if (!strncasecmp(cmd,"sw",2)) {      /* slit width for gm4 fit */
#ifdef ENG_MODE
    if (*par1) g->slitW = imax(0,atoi(par1));
#else
    if (*par1) g->slitW = imax(1,atoi(par1));
#endif
    else       sprintf(msgstr,"%d",g->slitW);
  } else
  if (!strncasecmp(cmd,"offgo",5)) {   /* Shec special command NEW v0424 */
#if 1  // todo 
    message(g,"not yet implemented",MSS_INFO);
#else
    if (n < 3) { 
      err = E_MISSPAR;
    } else {
      float dx = atof(par1);
      float dy = atof(par2);
      handle_command(g,"fone",1);  /* todo show command ? */
      // todo coordinated offset
      // todo turn off/on SH guiding? -- needs to go through TCSIS
      // todo telio_open / send commands directly ?
      // err = telio_open(2);
      // telio_move(dx,dy);
      // telio_guider_probe(-dx,-dy,0,1); // todo PR ? "gprdr2 %.2f %.2f"
      // telio_mstat() todo wait for probe/telescope todo timeout ?
      // telio_close
      handle_command(g,"mm 1",1);
      handle_command(g,"gm 5",1);
      message(g,"select offset guide star ... ",MSS_INFO);
      message(g,"... then press <F5>",MSS_INFO);
    }
#endif
  } else 
  if (!strcasecmp(cmd,"status")) {     /* report guider state */
    GuiderState s;
    guider_state(g,&s);                /* shared with the FITS header */
    snprintf(msgstr,sizeof(g->command_msg),
      "init=%d loop=%d guiding=%d gm=%d fm=%d mm=%d "
      "exptime=%.3f av=%d gain=%d offset=%d send=%d "
      "temp=%.1f setp=%.0f cooler=%.0f cfps=%.2f gfps=%.2f "
      "fwhm=%.2f flux=%.0f peak=%.0f back=%.0f dx=%+.2f dy=%+.2f "
      "az=%+.3f el=%+.3f x=%.1f y=%.1f bx=%d px=%.0f pa=%.1f sn=%.1f "
      "iport=%d iclients=%d",
      s.init,s.loop,s.guiding,s.mode,s.fmode,s.mmode,
      g->status.exptime,s.av,g->server->gain,s.offset,s.send,
      g->server->tempSensor,s.setp,s.cooler,s.camfps,s.fps,
      s.fwhm,s.flux,s.peak,s.back,s.dx,s.dy,
      s.az,s.el,s.boxx,s.boxy,s.boxsz,
      1000.0*g->px,s.pa,s.sens,
      g->image_port,g->image_clients);
  } else
  if (!strncasecmp(cmd,"start",4)) {   /* start exposure loop */
    int r = do_start(g,0);
    if (r < 0) err = -r;
  } else
  if (!strncasecmp(cmd,"stop",3)) {    /* stop exposure loop */
    do_stop(g,0);
  } else
  if (!strncasecmp(cmd,"write",3)) {   /* write files */
    g->write_flag = (n > 1) ? atoi(par1) : 1;
    thread_detach(run_write,g);
  } else
  if (!strncasecmp(cmd,"mask",3)) {    /* create bad pixel mask v0320 */
    make_mask(g,par1);
  } else                               /* reset server */
  if (!strcasecmp(cmd,"dspi") || !strcasecmp(cmd,"reset")) {
    if (n > 1) {                       /* load config file if specified */
      char inifile[256];
      sprintf(inifile,"%s.ini",par1);
      strncpy(g->name, "", sizeof(g->name));               /* reset name to avoid confusion */
      if (read_inifile(g,inifile) < 0) {
        sprintf(msgstr,"config file '%s' not found",inifile);
        err = -1;
      } else {                         /* update server host from config */
        strcpy(g->server->host,g->host);
      }
    }
    if (!err) {
      if (g->server->mask) {           /* force mask reload */
        free(g->server->mask);
        g->server->mask = NULL;
      }
      if (g->init_flag >= 0) thread_detach(run_init,g);
    }
  } else                               /* shutdown server */
  if (!strncasecmp(cmd,"shutdown",4) || !strncasecmp(cmd,"poweroff",5)) {
    if (g->loop_running) do_stop(g,3000);
    err = zwo_server(g->server,cmd,buf); /* shutdown requires 'root' at rPi */
  } else                               /* shutdown server */
  if (!strncasecmp(cmd,"gain",4))  {   /* todo default offsets ?Povilas */
    int gain=-1,offs=-1;
    if (n > 1) {
      if      (!strncasecmp(par1,"low" ,2)) { gain = 0;   offs = 10; }
      else if (!strncasecmp(par1,"hdr" ,2)) { gain = 0;   offs = 10; }
      else if (!strncasecmp(par1,"med" ,2)) { gain = 150; offs = 21; }
      else if (!strncasecmp(par1,"high",2)) { gain = 300; offs = 50; }
      else if (!strncasecmp(par1,"lrn" ,2)) { gain = 300; offs = 50; }
      else                                    gain = atoi(par1);
    }
    if (n > 2) offs = atoi(par2);
    err = zwo_gain(g->server,gain,offs);
    sprintf(msgstr,"%d %d",g->server->gain,g->server->offset);
    sprintf(g->gnbox.text,"gain %5d",g->server->gain);
    CBX_UpdateEditWindow(&g->gnbox);
  } else
  if (!strcmp(cmd,"sim")) {
    extern int sim_star,sim_slit,sim_cx,sim_cy; 
    extern double sim_peak,sim_sig2;
    if (!strncasecmp(par1,"star",2)) sim_star = atoi(par2);
    else
    if (!strncasecmp(par1,"slit",2)) sim_slit = atoi(par2); /* NOT width */
    else
    if (!strncasecmp(par1,"peak",1)) sim_peak = atof(par2);
    else
    if (!strncasecmp(par1,"flux",2)) sim_peak = atof(par2)/(sim_sig2*M_PI);
    else
    if (!strncasecmp(par1,"fwhm",2)) {
      double s2 = fmax(0.01,2.0*pow(atof(par2)/(g->px*2.35482),2.0));
      sim_peak *= sim_sig2/s2;
      sim_sig2 = s2;
    } else 
    if (!strcasecmp(par1,"x")) {
      if      (par2[0] == '+') sim_cx += 1;
      else if (par2[0] == '-') sim_cx -= 1;
      else                     sim_cx  = atoi(par2);
    } else 
    if (!strcasecmp(par1,"y")) {
      if      (par2[0] == '+') sim_cy += 1;
      else if (par2[0] == '-') sim_cy -= 1;
      else                     sim_cy  = atoi(par2);
    }
  } else {
    err = E_ERROR; sprintf(msgstr,"unknown command: %s",cmd);
  }
  if (err == 0) {                      /* no error */
    if (showMsg) {             /* show messages on display v0333 */
      if (*msgstr) { char line[96];   /* EditWindow text is limited */
        snprintf(line,sizeof(line),"%s",msgstr);
        message(g,line,MSS_WINDOW);
      }
      strcpy(g->lastCommand,command);
    }
  } else {                             /* error occured */
    switch (err) {
    case E_ERROR:   
      /* msgstr is already filled */
      assert(*msgstr);
      break;
    case E_NOTINIT:
      strcpy(msgstr,"ZWO not initialized");
      break; 
    case E_RUNNING:
      strcpy(msgstr,"data acquisition running");
      break; 
    case E_MISSPAR: 
      strcpy(msgstr,"missing parameter");
      break;
    case E_NOTACQ:  
      strcpy(msgstr,"ZWO not acquiring");
      break;
    case E_TELIO: 
      strcpy(msgstr,"connection to TCS failed");
      break;
    case E_NOTIMP:  
      strcpy(msgstr,"command not implemented");
      break;
    default: 
      fprintf(stderr,"unknown error code %d\n",err);
      break;
    } 
    if (showMsg) message(g,msgstr,MSS_WARN);
  } /* endif(err) */

  return err;
}

/* ---------------------------------------------------------------- */

static void* run_setup(void* param)
{
  Guider *g = (Guider*)param;
  char   buf[128];
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p): %s\n",PREFUN,param,g->name);
#endif
  assert(!done);                       /* GUI up and running */
  assert(g->server->handle >= 0);       /* connected */
  assert(g->server->tid == 0);

  g->init_flag = -1;                   /* init running */
  message(g,PREFUN,MSS_INFO);

  sprintf(buf,"%s %s (%d) - v%s",g->name,g->host,g->gnum,P_VERSION);
  CBX_SetMainWindowName(&mwin,buf);

  long err = zwo_setup(g->server,baseD,baseB,g->offx,g->offy);
  if (!err) {
    strcpy(g->status.instrument,g->server->modelName);
    strcpy(g->status.serial,g->server->serialNumber);
    assert(g->server->aoiW == baseD);
    assert(g->server->aoiH == baseD);
    g->status.binning = baseB;
    g->status.exptime = g->server->expTime;
  }
  if (!err) {
#ifdef ENG_MODE
    err = zwo_gain(g->server,-1,-1);   /* read back current gain */
#else
    if (*g->gain) {
      sprintf(buf,"gain %s",g->gain);
      handle_command(g,buf,1);         /* set default gain v0323 */
    } else {
      err = zwo_gain(g->server,-1,-1);
    }
#endif
    sprintf(g->gnbox.text,"gain %5d",g->server->gain);
    CBX_UpdateEditWindow(&g->gnbox);
    if (!g->server->mask) { 
      int npix = g->server->aoiW * g->server->aoiH;
      g->server->mask = (char*)calloc(npix,sizeof(char)); 
      load_mask(g);
    }
  }
  if (err) {                           /* IDEA just close GUI */
    g->init_flag = 0;                  /* there's no redemption :-( */
    sprintf(buf,"err= %ld",err);
    message(g,buf,MSS_ERROR);
    assert(g->server->err == err);
  } else {
    g->init_flag = 1;
    set_exptime(g,0);                  /* update GUI */
    do_start(g,0);
    // TEC=off v0313 thread_detach(run_housekeeping,(void*)g);
  }

  return (void*)err;
}

/* ---------------------------------------------------------------- */

static void* run_init(void* param)
{
  long err=0;
  char buf[512];
  Guider *g = (Guider*)param;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p): %s\n",PREFUN,param,g->name);
#endif
  assert(!done);                       /* GUI up and running */
  assert(g->server);
 
  g->init_flag = -1;                   /* init running */
  message(g,PREFUN,MSS_INFO);

  if (g->loop_running) {               /* wait for loop to finish */
    do_stop(g,5000); msleep(1000);
  }
  if (g->server->handle > 0) {
    zwo_close(g->server); msleep(500);
  }

  if (!err) {
    err = zwo_open(g->server);
    if (err) {
      sprintf(buf,"no connection to %s:%d",g->server->host,g->server->port);
      message(g,buf,MSS_ERROR);
    } else {
      if (!strncmp(g->server->modelName,"-E",2)) {
        sprintf(buf,"'%s' not connected to ZWO",g->server->host);
        message(g,buf,MSS_ERROR);
        err = E_zwo_not_connected;
      }
    }
  }
  if (!err) {
    err = (long)run_setup(g);
    if (err) { char buf[128];
      sprintf(buf,"setup failed, err=%ld",err);
      message(g,buf,MSS_ERROR);
    }
  }
  if (!err) {
    if (!g->tcpip_running) {           /* NEW v0424 */
      thread_detach(run_tcpip,(void*)g);
    }
    if ((g->image_port > 0) && !g->image_running) {  /* v1.0.6 */
      thread_detach(run_image_server,(void*)g);
    }
  }
  g->init_flag = (err) ? 0 : 1;

  return (void*)err;
}

/* ---------------------------------------------------------------- */

/* update telescope orientation */

static void* run_tele(void* param)
{
  int err=0;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p)\n",PREFUN,param);
#endif

  err = telio_open(2);
  if (!err) {
    Guider *g = &sGuider;
    if (g->pamode) err = set_pa(g,g->pa,1);
    telio_close();
  }

  return (void*)(long)err;
}

/* ---------------------------------------------------------------- */

static void* run_housekeeping(void* param)
{
  Guider *g=(Guider*)param;
  int  interval=2,err,stabilized=0;
  char buf[128];
  time_t next=0;
#if (DEBUG > 0)
  fprintf(stderr,"%s(%p)\n",PREFUN,param);
#endif
  assert(g->init_flag == 1);

  if (g->house_running) return (void*)0;
  g->house_running = True;
  sprintf(buf,"%.1f",g->server->tempSetpoint);
  zwo_temperature(g->server,buf);      /* define setpoint */

  do {
    sleep(1);
    if (g->init_flag != 1) break;      /* will be restarted at _setup */
    if (!g->house_running) break;      /* stopped */
    assert(g->server);
    if (g->server->handle < 0) break;  /* not connected */
    if (cor_time(0) < next) continue;
    next = cor_time(0)+interval;
    err = zwo_temperature(g->server,NULL);
    if (!err) {
      sprintf(g->tmbox.text,"%.1f",g->server->tempSensor); 
      CBX_UpdateEditWindow(&g->tmbox);
      sprintf(g->cpbox.text,"%.0f%%",g->server->coolerPercent);
      CBX_UpdateEditWindow(&g->cpbox); /* v0313 */
      sprintf(buf,"temp=%.1f cooler=%.0f",g->server->tempSensor,
                                          g->server->coolerPercent); 
      message(g,buf,MSS_WINDOW);
      interval++; if (interval > 30) interval = 30;
      if (fabs(g->server->tempSensor-g->server->tempSetpoint) > 0.2) {
        stabilized=0;
      } else {
        stabilized++;
      }
    } /* endif(!err) */ 
    if (stabilized > 1) break;
  } while (g->house_running);          /* v0313 */
  if (g->house_running) sprintf(buf,"temperature stabilized");
  else                  sprintf(buf,"temp-control off");
#if (DEBUG > 0)
  fprintf(stderr,"%s\n",buf);
#endif
  message(g,buf,MSS_INFO);             /* write to logfile */
  g->house_running = False;

  return (void*)0;
}

/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */

/* --- legacy push, scheduled for removal ---------------------------- *
 *                                                                     *
 * Pushes one FITS to send_host:send_port, one connection per frame.   *
 * Superseded by the image server; kept until the last consumer moves. *
 * Removal checklist: docs/plans/gcam-image-server.md                  *
 *                                                                     *
 * NOTE: runs on the acquisition thread and is called while the caller *
 * still holds the frame's read-lock, so an unreachable send_host can  *
 * stall acquisition. Not worth fixing in code that is being deleted;  *
 * see the plan for the reasoning.                                     */

static void push_frame(Guider* g,ZwoFrame* frame)
{
  int  err=0;
  char buf[128];

  update_status(&g->status,g,frame);
  if (g->send_port > 0) {
    int sock = TCPIP_CreateClientSocket(g->send_host,g->send_port,&err);
    if (!err) {
      err = fits_send(frame->data,&g->status,sock); /* network */
      (void)close(sock);
    }
  }
  printf("%s: sending frame %u to %s:%d, err=%d\n",
         get_ut_timestr(buf,cor_time(0)),g->sendNumber,
         g->send_host,g->send_port,err);
  sprintf(g->sqbox.text,"%u",g->sendNumber); /* v0343 */
  CBX_UpdateEditWindow(&g->sqbox);
  if (!err) g->sendNumber += 1;        // TODO ?Polivas overflow
  if (g->send_flag < 0) set_sf(g,0);   /* single send */
}

/* --- end legacy push ----------------------------------------------- */

/* ---------------------------------------------------------------- */

/* Latch the guider state. Single source of truth for the 'status'    */
/* command and the FITS header, so the two cannot drift apart. v1.0.6 */
/* Fills a caller-owned struct -- never shared state -- so concurrent */
/* readers (command port, image server) do not stomp each other.      */

static void guider_state(Guider* g,GuiderState* s)
{
  QlTool *q = g->qltool;

  pthread_mutex_lock(&g->mutex);       /* latch -- do not clear update_flag */
  s->fps     = (float)g->fps;
  s->flux    = (float)g->flux;
  s->peak    = (float)g->ppix;
  s->back    = (float)g->back;
  s->fwhm    = (float)g->fwhm;
  s->dx      = (float)g->dx;           /* relative to the guide box */
  s->dy      = (float)g->dy;
  s->az      = (float)g->azg;
  s->el      = (float)g->elg;
  s->guiding = q->guiding;             /* also set under 'mutex' */
  s->boxx    = q->curx[QLT_BOX];       /* box follows the star in gm4/gm5 */
  s->boxy    = q->cury[QLT_BOX];
  pthread_mutex_unlock(&g->mutex);

  s->init   = g->init_flag;            /* not written under 'mutex' */
  s->loop   = (int)g->loop_running;
  s->mode   = g->gmode;
  s->fmode  = g->fmode;
  s->mmode  = g->msmode;
  s->av     = g->server->rolling;
  s->offset = g->server->offset;
  s->send   = g->send_flag;
  s->boxsz  = 1+2*q->vrad;
  s->setp   = g->server->tempSetpoint;
  s->cooler = g->server->coolerPercent;
  s->camfps = (float)g->server->fps;
  s->pa     = (float)g->pa;
  s->sens   = (float)g->sens;
}

/* ---------------------------------------------------------------- */

static void update_status(FITSpars* st,Guider* g,ZwoFrame* f) /* v0317 */
{
  guider_state(g,&st->gd);             /* guider block v1.0.6 */
  st->ts_ns = f->ts_ns;                /* server frame time v1.0.6 */
  st->seqNumber = f->seqNumber;
  st->stop_int  = walltime(0);
  st->start_int = st->stop_int - st->exptime;
  st->temp_ccd = g->server->tempSensor;   /* v0313 */
  st->gain = g->server->gain;
  st->shmode = g->shmode;                 /* v0316 */
  st->frame = g->sendNumber;              /* v0317 */
  st->ca = modulo(g->angle+180.0,0,360);  /* v0430 */

  update_tcs(st);
}

/* ---------------------------------------------------------------- */

/* The TCS block. Blocking network I/O -- one 'telio' connection and  */
/* eight-plus queries -- so it must never run per client per frame;   */
/* the image server refreshes it on a timer instead. v1.0.6           */

static void update_tcs(FITSpars* st)
{
  int err = telio_open(2);   /* update status from TCS v0328 */
  if (!err) { char  buf[128];
    if (!err) err = telio_pos(&st->alpha,&st->delta,&st->equinox);
    if (!err) err = telio_geo(&st->airmass,&st->rotangle,&st->rotatore,
                              NULL,NULL,NULL);
    if (!err) err = telio_char("st",st->st_str);
    if (!err) err = telio_char("ha",st->ha_str);
    if (!err) err = telio_char("zd",buf);
    if (!err) st->zd = (float)atof(buf);
    if (!err) err = telio_char("guiderx1",buf); 
    if (!err) st->guiderx1 = (float)atof(buf);
    if (!err) err = telio_char("guidery1",buf); 
    if (!err) st->guidery1 = (float)atof(buf);
    if (!err) err = telio_char("guiderx2",buf); 
    if (!err) st->guiderx2 = (float)atof(buf);
    if (!err) err = telio_char("guidery2",buf); 
    if (!err) st->guidery2 = (float)atof(buf);
    if (!err) err = telio_char("telguide",st->tg_str);
    if (!err) err = telio_getfocus(&st->telfocus);
    telio_close();
  }
}

/* --- */

static void update_fps(EditWindow* box,double fps) /* v0318 */
{
  if (fps < 0.7) sprintf(box->text,"%.2f",my_round(fps,2));
  else           sprintf(box->text,"%.1f",my_round(fps,1));
  CBX_UpdateEditWindow(box);
}

/* ---------------------------------------------------------------- */

static void* run_cycle(void* param)
{
  int    err=0,dt=0,q_flag=0;
  double tFrame,tSend,tNow,tTemp,nodata=2+MAX_EXPTIME,cnt=1;
  char buf[128];
  u_int seqNumber=0;
  pthread_t disp_tid=0;                 
  Guider *g = (Guider*)param;
#if (DEBUG > 0)
  fprintf(stderr,"%s: %s(%p)\n",__FILE__,PREFUN,param);
#endif

  ZwoStruct *server = g->server;
  assert(server);
  assert(g->init_flag == 1);

  g->loop_running = True;
  g->stop_flag = False;

  err = zwo_cycle_start(server);

  tFrame = tSend = tNow = tTemp = walltime(0);
  if (!err) {
    thread_create(run_display,g,&disp_tid);
  }

  if (!err) { ZwoFrame *frame=NULL;
    while (!g->stop_flag) {
      msleep(350);                     /* see 0.175s correction below */
      tNow = walltime(0);              /* now */
      if (tNow >= tTemp) {             /* v0313 */
        sprintf(g->tmbox.text,"%.1f",g->server->tempSensor);
        CBX_UpdateEditWindow(&g->tmbox);
        sprintf(g->cpbox.text,"%.0f%%",g->server->coolerPercent);
        CBX_UpdateEditWindow(&g->cpbox); /* v0313 */
        tTemp += 2.0;                  /* every 2 seconds */
      }
      if (g->send_flag) {
        cnt = g->send_flag - (tNow-tSend);
        sprintf(g->dtbox.text,"%d",(int)my_round(cnt,0));
        CBX_UpdateEditWindow(&g->dtbox);
      } else {
        tSend = tNow;
      }
      frame = zwo_frame4reading(server,seqNumber);
      if (frame) {                     /* a new frame has arrived */
        seqNumber = frame->seqNumber;  /* most recent frame */
        /* --- legacy push, scheduled for removal v1.0.6 --------------- */
        if ((g->send_flag) && (cnt <= 0.0)) {
          push_frame(g,frame);
          tSend = tNow-0.175;          /* time of last send */
        }
        /* --- end legacy push ---------------------------------------- */
        zwo_frame_release(server,frame);
        update_fps(&g->fpbox,g->server->fps);
        tFrame = tNow-0.175;           /* time of last frame */
        nodata = 2+MAX_EXPTIME;
      } else {                         /* no new frame */
        dt = (int)floor(tNow-tFrame);  /* time since last frame */
        if (dt > nodata) {
          /* tested failure modes and GUI responses v0419 */
          /* rPi: unplug ethernet: "no response from rPi" */
          /* rPi: unplug USB: "no data" */
          /* ZWO: unplug power: "no data" */
          message(g,"no data",MSS_WARN);
          nodata *= 1.5;               /* slow down warnings */
        }
      }
      if (g->update_flag) {            /* update GUI from guider */
        pthread_mutex_lock(&g->mutex); /* lock and latch */
        double fps = g->fps;
        double flux = g->flux;
        double ppix = g->ppix;
        double back = g->back;
        double fwhm = g->fwhm;
        double dx = g->dx;
        double dy = g->dy;
        g->update_flag = False;
        pthread_mutex_unlock(&g->mutex);
        update_fps(&g->fgbox,fps);
        if (fwhm > 0) {                /* we have a valid measurement */
          if (g->gmode != GM_SV3) {    /* {1,4,5} */
            if (flux>99999) sprintf(g->tcbox.text,"tc %4.0fk",flux/1000.0); // v0402
            else            sprintf(g->tcbox.text,"tc %5.0f",flux);
            CBX_UpdateEditWindow(&g->tcbox);
            sprintf(g->mxbox.text,"mx %5.0f",ppix);
            g->mxbox.fg = (ppix > 15000) ? app->red : app->black; /* v0411 */
            CBX_UpdateEditWindow(&g->mxbox);
            sprintf(g->bkbox.text,"bk %5.0f",back); 
            CBX_UpdateEditWindow(&g->bkbox);
            sprintf(g->fwbox.text,"fw %5.2f",fwhm);
            CBX_UpdateEditWindow(&g->fwbox);
          } 
          if (g->gmode == GM_SV3) sprintf(g->dxbox.text,"rx %5.2f",dx); // v0414
          else                    sprintf(g->dxbox.text,"dx %5.1f",dx);
          CBX_UpdateEditWindow(&g->dxbox);
          if (g->gmode == GM_SV3) sprintf(g->dybox.text,"ry %5.2f",dy);
          else                    sprintf(g->dybox.text,"dy %5.1f",dy);
          CBX_UpdateEditWindow(&g->dybox);
          graph_redraw(g->g_tc);
          graph_redraw(g->g_fw);
          graph_redraw(g->g_az);
          graph_redraw(g->g_el);
        } /* endif(fwhm) */
        if (q_flag != g->q_flag) {     /* quality flag */
          q_flag = g->q_flag;
          switch (q_flag) {
            case 0: g->gdbox.fg = app->black; break;
            case 1: g->gdbox.fg = app->yellow; break;
            case 2: g->gdbox.fg = app->red; break;
          }
          CBX_UpdateEditWindow(&g->gdbox);
        }
      } /* endif(update) */
    } /* endwhile(!stop_flag) */
    zwo_cycle_stop(server);
  } /* endif(!err) */
  g->loop_running = False;
  sprintf(g->fpbox.text,"%d",0); CBX_UpdateEditWindow(&g->fpbox);
  sprintf(g->dtbox.text,"-"); CBX_UpdateEditWindow(&g->dtbox);

  if (disp_tid) pthread_join(disp_tid,NULL);

  if (err) {
    sprintf(buf,"%s: err=%d",PREFUN,err);
    message(g,buf,MSS_ERROR);
  }
  fprintf(stderr,"%s() done\n",PREFUN);

  return (void*)(long)err;
}
 
/* ---------------------------------------------------------------- */

static void* run_display(void* param) 
{
  Guider *g = (Guider*)param;
  u_int seqNumber=0;
  double fps=1,t1,t2,slpt=0.350,tmax;
  QlTool *qltool = g->qltool;
  ZwoStruct *server = g->server;
  assert(server);
  assert(g->init_flag == 1);

  int gx = g->status.dimx;
  int gy = g->status.dimy;
  qltool_reset(qltool,gx,gy,1);

  t1 = walltime(0);
  while (g->loop_running) {
    ZwoFrame *frame = zwo_frame4reading(server,seqNumber);
    if (frame) {
      seqNumber = frame->seqNumber; 
      qltool_update(qltool,frame->data);
      zwo_frame_release(server,frame);
      qltool_redraw(qltool,True);
      t2 = walltime(0); 
      fps = 0.7*fps + 0.3/(t2-t1);
      update_fps(&g->fdbox,fps);
      t1 = t2;
    } // endif(frame)
    slpt = (throttle) ? 0.6*slpt + 0.4*slpt*(fps/throttle) : 0.02;
    tmax = (throttle) ? 1.0/throttle : 0.35;
    slpt = fmax(0.02,fmin(tmax,fmax(slpt,g->status.exptime/3.0)));
#if (DEBUG > 3)
    fprintf(stderr,"slpt=%.3f\n",slpt);
#endif
    msleep((int)(1000.0*slpt)); 
  } // endwhile(!stop)
  sprintf(g->fdbox.text,"%d",0); CBX_UpdateEditWindow(&g->fdbox);
  fprintf(stderr,"%s() done\n",PREFUN);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

static void* run_write(void* param) 
{
  int  weStartedTheLoop=0;
  char buf[128];
  Guider *g = (Guider*)param;
  double fps=0,t1,t2;
  ZwoStruct *server = g->server;
  FITSpars *st = &g->status;

  if (g->init_flag != 1) return (void*)0;
  if (!g->loop_running) {
    weStartedTheLoop = do_start(g,2000);
  }

  t1 = walltime(0);
  while (g->loop_running) {
    ZwoFrame *frame = zwo_frame4reading(server,st->seqNumber);
    if (!frame) {
      sprintf(buf,"failed to get new frame (>%u)",st->seqNumber);
      message(g,buf,MSS_WINDOW);
    } else { 
      update_status(st,g,frame);
      int e = fits_send(frame->data,st,0);  /* save to disk */
      if (e) { 
        sprintf(buf,"FITS error=%d",e);
        message(g,buf,MSS_WARN);
      } else {
        sprintf(buf,"%s.fits",st->filename);
        message(g,buf,MSS_FLUSH);
        st->run += 1;
        if (st->run > MAX_FILENUM) {
          st->run = 1;
          message(g,"file number overflow",MSS_WARN);
        }
        sprintf(buf,FMT_RUN,g->gnum);
        put_long(setup_rc,buf,st->run);
      } 
      zwo_frame_release(server,frame);
      t2 = walltime(0);
      fps = 0.5*fps + 0.5/(t2-t1);
      printf("%s: got frame-%u, fps=%.1f\n",PREFUN,st->seqNumber,fps);
      t1 = t2;
      g->write_flag -= 1;
    } // endif(frame)
    if (g->write_flag <= 0) break;
    msleep(50);
  } // endwhile(loop-doing)

  if (weStartedTheLoop) do_stop(g,0);
  printf("%s() done\n",PREFUN);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

static char* mask_name(Guider *g,char *name)
{
  ZwoStruct *server = g->server;
  if (g->offx || g->offy) {            /* v0348 */
    sprintf(name,"zwo%s_%d_%d_%d.mask",server->serialNumber,
            server->aoiW,g->offx,g->offy);
  } else {
    sprintf(name,"zwo%s_%d.mask",server->serialNumber,server->aoiW);
  }
  return name;
}

/* --- */

static void load_mask(Guider *g)       /* v0322 */
{
  ZwoStruct *server = g->server;
  char name[128],file[512],buf[512];

  assert(server->mask);
  int npix = server->aoiW * server->aoiH;
  assert(server->aoiW == server->aoiH);
  sprintf(file,"%s/%s",genv2("GCAMZWOPATH", "/opt/gcamzwo"),mask_name(g,name));
  printf("load_mask: loading %s (serial=%s, aoiW=%d, offx=%d, offy=%d)\n",
         file, server->serialNumber, server->aoiW, g->offx, g->offy);

  FILE *fp = fopen(file,"r");
  if (!fp) { 
    if (strlen(file) > 30) sprintf(buf,"failed %s",file);
    else                   sprintf(buf,"failed to load %s",file);
    message(g,buf,MSS_WARN|MSS_FILE);
  } else {
    size_t s = fread(server->mask,sizeof(char),npix,fp);
    fclose(fp);
    if (s != npix) {
      memset(server->mask,0,npix*sizeof(char));
      sprintf(buf,"invalid file %s",file);
      message(g,buf,MSS_WARN|MSS_FILE);
    } else {
      printf("loaded %s\n",file);
    }
  }
}

/* --- */

static void make_mask(Guider *g,const char* par)  /* v0319 */
{
  int i,mpix=0;
  double s1,s2,sn,ave,sig,cut,value=0;
  u_short *d;
  ZwoStruct *server = g->server;
  char buf[512],file[512],name[128];
  static u_int seqNumber=0;

  assert(server->mask);
  assert(server->aoiW == server->aoiH);
  int npix = server->aoiW * server->aoiH;
  sprintf(file,"%s/%s",genv2("GCAMZWOPATH", "/opt/gcamzwo"),mask_name(g,name));

  if (!strcmp(par,"off")) {            /* turn OFF mask */
    memset(server->mask,0,npix*sizeof(char));
    return;
  } else 
  if (!strcmp(par,"on")) {             /* load mask v0322 */
    load_mask(g);
    return;
  } else {
    value = atof(par);
    if (value < 2.0) {
      sprintf(buf,"invalid parameter '%f'",value);
      message(g,buf,MSS_WARN);
      return;
    }
  }

  ZwoFrame *frame = zwo_frame4reading(server,seqNumber);
  if (!frame) {
    sprintf(buf,"failed to get new frame (>%u)",seqNumber);
    message(g,buf,MSS_WARN);
    return;
  }
  seqNumber = frame->seqNumber;

  char *mask = (char*)calloc(npix,sizeof(char));
  do {
    s1=0.0; s2=0.0; sn=0.0;
    for (i=0,d=frame->data; i<npix; i++,d++) {
      if (!mask[i]) {
        s1 += (double)*d; s2 += (double)*d * (double)*d; sn += 1.0;
      }
    }
    assert(sn == npix-mpix);
    ave = s1/sn;
    sig = sqrt(s2/sn-ave*ave);
    cut = sig*value;
    for (i=0,d=frame->data; i<npix; i++,d++) {
      if (!mask[i]) {
        if (fabs(*d-ave) > cut) { mask[i]=1; mpix++; } 
      }
    }
    fprintf(stdout,"mpix=%d, ave=%.1f, sig=%.2f\n",mpix,ave,sig);
  } while (sn+mpix > npix);
  zwo_frame_release(server,frame);

  memcpy(server->mask,mask,npix*sizeof(char));

  FILE *fp = fopen(file,"w");
  if (!fp) {
    sprintf(buf,"failed to write %s",file);
    message(g,buf,MSS_WARN);
  } else {
    fwrite(mask,sizeof(char),npix,fp);
    fclose(fp);
    printf("saved %s\n",file);
  }
  free((void*)mask);
}

/* ---------------------------------------------------------------- */

static int set_ca(Guider* g,double f)
{
  int err=0;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%f)\n",PREFUN,g->name,f);
#endif

  if (f == 0.0) f = 360.0;
  while (f >  360.0) f -= 360.0;
  while (f <    0.0) f += 360.0;
  g->angle = f;
  if (g->pamode) err = set_pa(g,g->pa,1);
  return err;

}

/* ---------------------------------------------------------------- */

static int set_pa(Guider* g,double f,int fromTCS) /* position angle */
{
  int err=0;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%.1f,%d)\n",PREFUN,g,f,fromTCS);
#endif

  if (fromTCS) { float el,pa,re=0.0f;
    if (g->rosign) err = telio_geo(NULL,NULL,&re ,NULL,&el,&pa); 
    else           err = telio_geo(NULL,NULL,NULL,NULL,&el,&pa);
    if (!err) {                 /*  am , ra , re , az , el, pa */
      f = g->angle + g->elsign*el + g->rosign*re;  /* v0311 */
      elev = el;                       /* telescope elevation */
      para = pa;                       /* parallactic angle */
    }
  }
  if (f == 0.0) f = 360.0;
  while (f >  360.0) f -= 360.0;
  while (f <    0.0) f += 360.0;
  g->pa = g->parity*f;
  sprintf(g->pabox.text,"pa %4.0f",my_round(g->pa,0));
#ifndef PA_BOX
  g->pabox.fg = (g->pamode) ? app->green : app->black;
#endif
  CBX_UpdateEditWindow(&g->pabox);
  redraw_compass(g);

  return err;
}

/* ---------------------------------------------------------------- */

static void set_sens(Guider* g,double f)
{
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%f)\n",PREFUN,g->name,f);
#endif

  g->sens = fmin(4.0,fmax(0.1,f));
  sprintf(g->snbox.text,"sn %4.1f",g->sens);
  CBX_UpdateEditWindow(&g->snbox);
}

/* ---------------------------------------------------------------- */

static void set_mm(Guider* g,int m)
{
 switch (g->gmode) {
  case GM_PR: case GM_SH:              /* PR,SH guiders */
    switch (m) {
    case 1: case  2: case 3:           /* allowed mouse modes */
      g->msmode = m;
      break;
    }
    break;
  case GM_SV3: case GM_SV4:            /* SV guider */
    switch (m) {
    case 1: case -2: case 3:           /* allowed mouse modes */
      g->msmode = m;
      break;
    }
    break;
  }
  if (g->msmode != m) {
    message(g,"invalid mouse mode",MSS_WARN);
  }
  // display mouse mode in string form
  char msmode_str[4];
  switch (g->msmode)
  {
  case 1:
    strcpy(msmode_str,"bx");  /* box */
    break;
  case 2:
    strcpy(msmode_str,"pr");  /* probe */
    break;
  case -2:
    strcpy(msmode_str,"cd");  /* coordinated probe and telescope */
    break;
  case 3:
    strcpy(msmode_str,"tl");  /* telescope */
    break;
  default:
    strcpy(msmode_str,"un");  /* unknown */
    break;
  }
  sprintf(g->mmbox.text,"mm %s",msmode_str);
  CBX_UpdateEditWindow(&g->mmbox);
}

/* ---------------------------------------------------------------- */

static void set_fm(Guider* g,int m)    /* v0329 */
{
  switch (m) {
  case 1: case 2: // case 3:
    g->fmode = m;
    break;
  }
  if (g->fmode != m) { 
    message(g,"invalid function mode",MSS_WARN);
  }
  // display function mode in string form
  char fmmode_str[4];
  switch (g->fmode)
  {
  case 1:
    strcpy(fmmode_str,"gd");
    break;
  case 2:
    strcpy(fmmode_str,"sh");
    break;
  default:
    strcpy(fmmode_str,"un");
    break;
  }
  sprintf(g->fmbox.text,"fm %s",fmmode_str);
  CBX_UpdateEditWindow(&g->fmbox);
}

/* ---------------------------------------------------------------- */

static void set_av(Guider* g,int a)
{
  g->server->rolling = imax(0,imin(99,a));
#if 0 // old "av" box format
  sprintf(g->avbox.text,"av %d",g->server->rolling);
#else
  sprintf(g->avbox.text,"%d",g->server->rolling);
#endif
  CBX_UpdateEditWindow(&g->avbox);
}

/* ---------------------------------------------------------------- */

static void set_sf(Guider* g,int s)    /* v0343 */
{
  g->send_flag = s;
  if (s) sprintf(g->dtbox.text,"%d",imax(0,s));
  else    strcpy(g->dtbox.text,"-");
  CBX_UpdateEditWindow(&g->dtbox);
}

/* ---------------------------------------------------------------- */

static void set_gm(Guider* g,int m,char c)  /* v0354 */
{
  g->gmode = imax(1,imin(GM_MAX,m));
  if (c) g->gmpar = (c == 'p') ? 'p' : 't';
  switch (g->gmode) {                  /* v0416 */
  case GM_SV3: case GM_SV4:            /* has 'gmpar' */
    sprintf(g->gmbox.text,"gm %1d%c",g->gmode,g->gmpar);
    break;
  default:                             /* no 'gmpar' */
    sprintf(g->gmbox.text,"gm %2d",g->gmode);
    break;
  }
  CBX_UpdateEditWindow(&g->gmbox);
  g->qltool->gmode = g->gmode;

  switch (g->gmode) {                  /* v0414 */
  case GM_PR: case GM_SV5:             /* SV5 v0416 */
    strcpy(g->g_tc->name,"tc"); graph_scale(g->g_fw,0,10000,0);
    strcpy(g->g_fw->name,"fw"); graph_scale(g->g_fw,0.0,2.0,0);
    strcpy(g->g_az->name,"AZ"); graph_scale(g->g_az,-1.0,1.0,0x01);
    strcpy(g->g_el->name,"EL"); graph_scale(g->g_el,-1.0,1.0,0x01);
    break;
  case GM_SV3:                         /* [ratio] */
    strcpy(g->g_tc->name,"rx"); graph_scale(g->g_tc,-0.5,0.5,0x03);
    strcpy(g->g_fw->name,"ry"); graph_scale(g->g_fw,-0.5,0.5,0x03);
    strcpy(g->g_az->name,"AZ"); graph_scale(g->g_az,-1.0,1.0,0x01);
    strcpy(g->g_el->name,"EL"); graph_scale(g->g_el,-1.0,1.0,0x01);
    break;
  case GM_SV4:  /* [arcsec] before rotation and derivative */
    strcpy(g->g_tc->name,"tc"); graph_scale(g->g_fw,0,10000,0);
    strcpy(g->g_fw->name,"fw"); graph_scale(g->g_fw,0.0,2.0,0);
    strcpy(g->g_az->name,"X");  graph_scale(g->g_az,-1.0,1.0,0x03);
    strcpy(g->g_el->name,"Y");  graph_scale(g->g_el,-1.0,1.0,0x03);
    break;
  }
}

/* ---------------------------------------------------------------- */

static int set_exptime(Guider* g,float t)
{
  int err=0; 

  if (t > 0) {
    double tmin = (g->server->aoiW*g->server->aoiH)/90.0e6;
    tmin = 0.01*ceil(tmin/0.01);
    g->status.exptime = (float)fmin(MAX_EXPTIME,fmax(tmin,t));
  }
  sprintf(g->tfbox.text,"%.*f",(g->status.exptime < 10) ? 2 : 1,
          g->status.exptime);
  CBX_ResetEditWindow(&g->tfbox);

  if (t > 0) {
    err = zwo_exptime(g->server,g->status.exptime);
    if (err) { char buf[128];
      g->status.exptime = (float)g->server->expTime;
      sprintf(buf,"sending 'ExpTime' failed, err=%d",err);
      message(g,buf,MSS_ERROR);
      set_exptime(g,0);                /* just update GUI */
    }
  }
  return err;
}

/* ---------------------------------------------------------------- */

void message(const void *param,const char* text,int flags)
{
  Guider *g=NULL;
  int  i=0;
  char tstr[32];
  static FILE* fp=NULL;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%p,%s,%x)\n",PREFUN,param,text,flags);
#endif
  assert(text);

  if (param) {
    /* Check if param matches sGuider */
    if (param == &sGuider) { g = &sGuider; }
    else if (param == sGuider.server) { g = &sGuider; }
  }

  if (flags & MSS_FILE) { char who[64]="-";  /* write to file */
    pthread_mutex_lock(&mesgMutex); 
    if (!fp) fp = fopen(logfile,"a");
    if (fp) {
      // strcpy(who,(g) ? g->name : P_TITLE);
      if      (flags & MSS_RED)     strcat(who," ERROR:");
      else if (flags & MSS_YELLOW)  strcat(who," WARNING:");
      get_ut_datestr(tstr,cor_time(0));
      fprintf(fp,"%s %s %s\n",tstr,who,text);
      if (flags & MSS_FLUSH) fflush(fp);
    } 
    pthread_mutex_unlock(&mesgMutex); 
  }
  if (flags & (MSS_RED | MSS_YELLOW)) {
    fprintf(stderr,"%s\n",text);
  }

  if (!g) return;
  if (!(flags & MSS_WINDOW)) return;
  assert(mwin.disp);

  get_ut_timestr(tstr,cor_time(0));
  if (flags & MSS_OVERWRITE) {         /* overwrite */
    sprintf(g->msbox[0].text,"%s: %s",tstr,text);
    CBX_UpdateEditWindow(&g->msbox[0]);
    return;
  }
 
  pthread_mutex_lock(&mesgMutex); 
  for (i=n_msg-1; i>=0; i--) {         /* prepend line */
    if (i == 0) {
      sprintf(g->msbox[i].text,"%s: %s",tstr,text);
      if      (flags & MSS_RED)    CBX_ChangeLed(&g->led[i],app->red); 
      else if (flags & MSS_YELLOW) CBX_ChangeLed(&g->led[i],app->yellow); 
      else                         CBX_ChangeLed(&g->led[i],app->green); 
    } else {
      strcpy(g->msbox[i].text,g->msbox[i-1].text);
      CBX_ChangeLed(&g->led[i],g->led[i-1].bg);
    }
    CBX_UpdateEditWindow(&g->msbox[i]);
  }
  pthread_mutex_unlock(&mesgMutex); 
}

/* ---------------------------------------------------------------- */

static int check_datapath(char* path,int interactive)
{
  char buffer[2048],cmd[2048],*h;
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d)\n",PREFUN,path,interactive);
#endif

  cut_spaces(path);
  if (*path == '\0') return 0;

  if (path[0] != '/') {
    if (interactive) {
      sprintf(buffer,"invalid path (%s), must be absolute ('/...')",path);
      CBX_MessageBox(P_TITLE,buffer);
    }
    *path = '\0';
    return 0;
  }

  if (chdir(path) == -1) {
    if (interactive) {
      if (CBX_AlertBox(path,"does not exist - create new directory ?")) {
        strcpy(buffer,path); path[0]='\0';
        h=strtok(buffer,"/");
        do {
          strcat(path,"/"); strcat(path,h);
          if (chdir(path) == -1) {       /* path does not exist */
            sprintf(cmd,"mkdir %s",path);
            int err = system(cmd);       /* use 'err' v0315 */
            if (err) fprintf(stderr,"system(mkdir): err=%d\n",err);
          }
        } while ((h=strtok(NULL,"/")) != NULL);
      } else {
        strcpy(path,genv2("GCAMZWOPATH", "/opt/gcamzwo"));
        CBX_MessageBox(P_TITLE ": set DataPath to",path);
      }
      CBX_ClearAutoQueue(&mwin);
    } else {
      *path = '\0';
    }
  }
  if (*path) disk_next = 0;

  return (*path) ? 1 : 0;
}

/* ---------------------------------------------------------------- */

static int cursor_blocked(Guider *g,int mode,int prnt) /* v0421 */
{
  int b=0;

  if ((g->gmode == GM_SV5) && (g->qltool->guiding)) {
    /* do not allow to move cursor4 and 5 (box) */
    if (mode == QLT_BOX  ) b=1;
    if (mode == QLT_BOX-1) b=1;
    if (b && prnt) printf("cursor motion blocked while guiding in gm5\n");
  }
  return b;
}

/* ---------------------------------------------------------------- */

static void my_shutdown(Bool force)
{
  if (!force) {
    force = True;
    if (sGuider.loop_running) force = False;
  }
  if (force) { Bool wait=False;
    if (sGuider.loop_running) {
      wait = True;
      do_stop(&sGuider,0);
    }
    if (wait) msleep(1000);
    done = True;
  }
}

/* ---------------------------------------------------------------- */

static int setup_m_switch(char m)       /* v0423 */
{
  int r=1;

  switch (m) {                         /* geometry modes */
    case '1': baseD= 504; baseB=2; baseI=504; pHIGH=87;  break;
    case '2': baseD=1000; baseB=2; baseI=500; pHIGH=82;  break; /* PFS */
    case '3': baseD=1512; baseB=2; baseI=504; pHIGH=87;  break; /* MIKE */
    case '4': baseD=2000; baseB=2; baseI=500; pHIGH=82;  break;
    case '5': baseD=2520; baseB=2; baseI=504; pHIGH=87;  break; /* MagE v0423 */
    case 'h': baseD=2048; baseB=1; baseI=512; pHIGH=88;  break;
    case 'f': baseD=2400; baseB=2; baseI=600; pHIGH=117; break;
    default: r=0; break;
  }
  return r;
} 
 
/* ---------------------------------------------------------------- */

static int read_inifile(Guider *g,const char* name) /* v0415 */
{
  int n=0;
  char file[512],buffer[1024],key[128],val[128];

  if (*name == '/') strcpy(file,name);
  else             sprintf(file,"%s/%s",genv2("GCAMZWOINI", "/opt/gcamzwo"), name);
  FILE *fp = fopen(file,"r");
  if (!fp) {
    fprintf(stderr,"failed to open %s\n",file);
    return -1;
  }
  while (fgets(buffer,sizeof(buffer),fp) != NULL) {
    if (sscanf(buffer,"%s %s",key,val) == 2) {
      n++;
      if      (!strcmp(key,"host")) strcpy(g->host,val);
      else if (!strcmp(key,"port")) g->rPort = atoi(val); 
      else if (!strcmp(key,"send_host")) strcpy(g->send_host,val);
      else if (!strcmp(key,"send_port")) g->send_port = atoi(val);
      else if (!strcmp(key,"image_port")) g->image_port = atoi(val); /* v1.0.6 */
      else if (!strcmp(key,"tcs_age")) g->tcs_age = fmax(0.0,atof(val)); /* v1.0.6 */ 
      else if (!strcmp(key,"gain")) strcpy(g->gain,val);
      else if (!strcmp(key,"mode")) setup_m_switch(val[0]);
      else if (!strcmp(key,"gnum")) g->gnum = atoi(val);
      else if (!strcmp(key,"name")) strcpy(g->name,val);
      else if (!strcmp(key,"gmode")) g->gmode = atoi(val);
      else if (!strcmp(key,"gmpar")) g->gmpar = (int)val[0];
      else if (!strcmp(key,"angle")) g->angle = atof(val);
      else if (!strcmp(key,"elsign")) g->elsign = atof(val);
      else if (!strcmp(key,"rosign")) g->rosign = atof(val);
      else if (!strcmp(key,"parity")) g->parity = (atof(val) < 0) ? -1.0 : 1.0;
      else if (!strcmp(key,"offx")) g->offx = atoi(val);
      else if (!strcmp(key,"offy")) g->offy = atoi(val);
      else if (!strcmp(key,"px")) g->px = atof(val)/1000.0;
      else if (!strcmp(key,"lmag")) g->lmag = atoi(val);
      else if (!strcmp(key,"pct")) g->pct = atoi(val);
      else if (!strcmp(key,"bkg")) g->bkg = atoi(val);
      else if (!strcmp(key,"span")) g->span = atoi(val);
      else if (!strcmp(key,"bx")) g->bx = atoi(val);
      else if (!strcmp(key,"sw")) g->slitW = atoi(val); 
      else if (!strcmp(key,"sn")) g->sens = fmin(4.0,fmax(0.1,atof(val)));
      else if (!strcmp(key,"exptime")) g->status.exptime = atof(val);
      else n--;

    }
  }
  fclose(fp);
  printf("%d items read from %s\n",n,file);
  return n;
}

/* ---------------------------------------------------------------- */

/* 'answer' must hold at least sizeof(g->command_msg)+4 bytes */

static int handle_tcpip(Guider *g,const char* command,char* answer)
{
  char cmd[128]="",par[128]="";

  strcpy(answer,"-Einvalid command\n");
  if (strlen(command) >= sizeof(cmd)) return 0;

  /* handle special commands here, and pass on  */
  /* other commands to the regular command handler */
  (void)sscanf(command,"%s %s",cmd,par);
  strcpy(answer,"OK");                 /* preset 'answer' */
  if (!strncasecmp(cmd,"version",3)) { /* version */
    sprintf(answer,"%s",P_VERSION);
  } else
  if (!strncmp(cmd,"!",1)) {           /* ! */
    strcpy(answer,"-Einvalid command");
  } else {                             /* v0332,v0333 */
    int err = handle_command(g,command,0);  /* pass on */
    if (err) { 
      sprintf(answer,"-E%s",g->command_msg);
      msleep(850);
    } else { 
      if (*g->command_msg) strcpy(answer,g->command_msg);
    }
  }
  strcat(answer,"\n");
  msleep(150);

  return 0;
}

/* --- */

static int receive_string(int sock,char* string,size_t buflen)
{
  int     i=0;
  ssize_t r;

  do {
    r = TCPIP_Recv(sock,&string[i],1,0);
    if (r != 1) break;
    if (string[i] == '\n') break;      /* <LF> */
    if (string[i] == '\r') break;      /* <CR> */
    i++;
  } while (i < buflen-1);
  string[i] = '\0';                    /* overwrite term-char */

  return r;
}

/* --- */

static void* run_tcpip(void* param)
{
  Guider *g = (Guider*)param;
  int    port = TCPIP_PORT + g->gnum;
  int    err,msgsock,rval;
  char   cmd[128],buf[sizeof(g->command_msg)+4];
  IP_Address ip;
#if (DEBUG > 0)
  fprintf(stderr,"%s:%s(%d)\n",__FILE__,PREFUN,port);
#endif

  int sock = TCPIP_CreateServerSocket(port,&err);
  if (err) { 
    sprintf(buf,"TCPIP-Server failed, err=%d",err);
    message(g,buf,MSS_WARN);
    return (void*)(long)err;
  }
  sprintf(buf,"TCPIP-Server listening (%s,%d)",g->name,port);
  message(g,buf,MSS_FLUSH);

  g->tcpip_running = True;
  while (!done) {
    msgsock = TCPIP_ServerAccept(sock,&ip);
    sprintf(buf,"connection accepted from %u.%u.%u.%u",
            ip.byte1,ip.byte2,ip.byte3,ip.byte4);
    message(g,buf,MSS_FLUSH);
#ifdef SO_NOSIGPIPE                    /* 2017-07-05 */
    int on=1; (void)setsockopt(msgsock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
    do {
      rval = receive_string(msgsock,cmd,sizeof(cmd));
      if (rval > 0) {
        fprintf(stdout,"%s(): received '%s'\n",PREFUN,cmd);
        (void)handle_tcpip(g,cmd,buf);
        TCPIP_Send(msgsock,buf);
      } else {
#if (DEBUG > 0)
        fprintf(stdout,"%s(): hangup (rval=%d)\n",PREFUN,rval);
#endif
      }
    } while (rval > 0);                /* loop while there's something */
    (void)close(msgsock);
  } /* while(!done) */
  g->tcpip_running = False;

  (void)close(sock);
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d) done\n",PREFUN,port);
#endif

  return (void*)0;
}

/* ---------------------------------------------------------------- */
/* image server (v1.0.6) -- docs/plans/gcam-image-server.md           */
/* ---------------------------------------------------------------- */

/* One verb:                                                          */
/*   fits [timeout] -> "<seq> <ts_ns> <nbytes>\n" + nbytes of FITS    */
/* 'timeout' waits that many seconds for a frame newer than the last  */
/* served on this connection; 0 (default) returns the newest frame    */
/* available immediately. Nothing new -> "-Enodata".                  */

#define IMAGE_MAXCLIENTS  4

static pthread_mutex_t tcsLock=PTHREAD_MUTEX_INITIALIZER;
static double          tcs_last=0.0;
static pthread_mutex_t imgLock=PTHREAD_MUTEX_INITIALIZER;

/* --- */

static void image_clients(Guider* g,int d)   /* +1 / -1 */
{
  pthread_mutex_lock(&imgLock);
  g->image_clients += d;
  pthread_mutex_unlock(&imgLock);
}

/* --- */

/* Refresh the shared TCS block at most every 'tcs_age' seconds,      */
/* however many clients are connected. Must be called with no frame   */
/* read-lock held -- it does blocking network I/O.                    */

static void tcs_cache(Guider* g)
{
  pthread_mutex_lock(&tcsLock);
  if ((walltime(0)-tcs_last) >= g->tcs_age) {
    update_tcs(&g->status);
    tcs_last = walltime(0);
  }
  pthread_mutex_unlock(&tcsLock);
}

/* --- */

static void* run_image_conn(void* param)
{
  Guider    *g = ((Guider**)param)[0];
  int       sock = (int)(long)((void**)param)[1];
  ZwoStruct *server = g->server;
  u_short   *pix=NULL;
  size_t    pixsize=0;
  u_int     last_seq=0;
  int       rval;
  char      cmd[128],verb[64],par[64],line[128];

  free(param);

  do {
    rval = receive_string(sock,cmd,sizeof(cmd));
    if (rval <= 0) break;
    *verb = *par = '\0';
    (void)sscanf(cmd,"%s %s",verb,par);

    if (strcasecmp(verb,"fits")) {     /* the only verb */
      TCPIP_Send(sock,"-Einvalid command\n");
      continue;
    }
    if (g->init_flag != 1) { TCPIP_Send(sock,"-EZWO not initialized\n"); continue; }
    if (!g->loop_running) { TCPIP_Send(sock,"-EZWO not acquiring\n");   continue; }

    double timeout = (*par) ? atof(par) : 0.0;
    double deadline = walltime(0) + timeout;
    ZwoFrame *frame = NULL;
    do {                               /* wait for a frame we have not served */
      frame = zwo_frame4reading(server,last_seq);
      if (frame || (walltime(0) >= deadline)) break;
      msleep(5);
    } while (!done && !g->stop_flag);
    if (!frame && (timeout <= 0.0)) {  /* newest, even if already served */
      frame = zwo_frame4reading(server,0);
    }
    if (!frame) { TCPIP_Send(sock,"-Enodata\n"); continue; }

    /* Copy out and release BEFORE any network I/O -- holding the     */
    /* read-lock across a socket write can starve zwo_frame4writing() */
    /* and kill acquisition. Nothing below touches 'frame'.           */
    int    fw = frame->w, fh = frame->h;
    u_int  fseq = frame->seqNumber;
    unsigned long long fts = frame->ts_ns;
    size_t nbytes = (size_t)fw * fh * sizeof(u_short);
    if (pixsize != nbytes) {
      pix = (u_short*)realloc(pix,nbytes);
      pixsize = (pix) ? nbytes : 0;
    }
    if (pix) memcpy(pix,frame->data,nbytes);
    last_seq = fseq;
    zwo_frame_release(server,frame);   /* released -- socket work is next */
    frame = NULL;

    if (!pix) { TCPIP_Send(sock,"-Eout of memory\n"); continue; }

    tcs_cache(g);                      /* blocking I/O, no lock held */

    FITSpars st = g->status;           /* config + cached TCS block */
    st.dimx = fw;
    st.dimy = fh;
    st.seqNumber = fseq;
    st.ts_ns = fts;
    guider_state(g,&st.gd);            /* shared with the 'status' command */
    st.gain = g->server->gain;
    st.temp_ccd = g->server->tempSensor;
    st.stop_int = walltime(0);
    st.start_int = st.stop_int - st.exptime;

    int nb = FITSLEN + (int)nbytes;    /* FITS is block-padded */
    nb += (FITSBLOCK - (nb % FITSBLOCK)) % FITSBLOCK;
    sprintf(line,"%u %llu %d\n",st.seqNumber,st.ts_ns,nb);
    if (TCPIP_Send(sock,line)) break;
    if (fits_send(pix,&st,sock)) break;
  } while (!done);

  if (pix) free((void*)pix);
  (void)close(sock);
  image_clients(g,-1);

  return (void*)0;
}

/* --- */

static void* run_image_server(void* param)
{
  Guider *g = (Guider*)param;
  int    port = g->image_port + g->gnum;
  int    err,msgsock;
  char   buf[128];
  IP_Address ip;

  int sock = TCPIP_CreateServerSocket(port,&err);
  if (err) {
    sprintf(buf,"image server failed, err=%d",err);
    message(g,buf,MSS_WARN);
    return (void*)(long)err;
  }
  sprintf(buf,"image server listening (%s,%d)",g->name,port);
  message(g,buf,MSS_FLUSH);

  g->image_running = True;
  while (!done) {
    msgsock = TCPIP_ServerAccept(sock,&ip);
    if (msgsock < 0) continue;
#ifdef SO_NOSIGPIPE                    /* 2017-07-05 */
    int on=1; (void)setsockopt(msgsock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
    if (g->image_clients >= IMAGE_MAXCLIENTS) {
      sprintf(buf,"image server busy (%d clients)",g->image_clients);
      message(g,buf,MSS_WARN);
      (void)TCPIP_Send(msgsock,"-Etoo many clients\n");
      (void)close(msgsock);
      continue;
    }
    sprintf(buf,"image client %u.%u.%u.%u",
            ip.byte1,ip.byte2,ip.byte3,ip.byte4);
    message(g,buf,MSS_FLUSH);
    void **arg = (void**)malloc(2*sizeof(void*));
    arg[0] = (void*)g;
    arg[1] = (void*)(long)msgsock;
    image_clients(g,+1);            /* counted before the thread starts */
    thread_detach(run_image_conn,(void*)arg);
  } /* while(!done) */
  g->image_running = False;

  (void)close(sock);

  return (void*)0;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

