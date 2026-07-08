/* ----------------------------------------------------------------
 *
 * zwoserver.c  
 * 
 * Project: ZWO Camera software (OCIW, Pasadena, CA)
 *
 * v0.001  2019-01-07  Christoph C. Birk, birk@carnegiescience.edu
 * v0.004  2019-01-11  filter wheel
 * v0.006  2019-01-14  ASI/EFW
 * v0.011  2019-02-28  HiLev commands (open ... close ... filter)
 * v0.015  2019-07-17  cooler, color via network "camera property"
 * v0.020  2019-08-05  gain, offset commands
 * v0.023  2020-08-21  bitDepth
 * v0.024  2021-03-17  requires SDK v1.14.0227+ (ASIGetSerialNumber)
 * v0.025  2021-03-xx  video
 * v0.026  2021-03-23  append _temp,_heater values to video
 * v0.029  2021-10-20  support ASI294-MM
 * v0.031  2022-08-24  serial number
 *
 * NOTE: systemctl stop firewalld
 *       systemctl disable firewalld
 *
 * static linking fails -- libusb and 'udev' missing
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
#include <assert.h>

#include <sys/reboot.h>                /* requires server */
#include <linux/reboot.h>              /* to run as 'root' */
#include <netinet/in.h>                /* IPPROTO_TCP */
#include <netinet/tcp.h>               /* TCP_NODELAY */

#if (TIME_TEST > 0)
#include <limits.h>
#include <sys/times.h>
#include <unistd.h>
#endif

#include "zwo.h"
#include "utils.h"                     /* generic utilities */
#include "random.h"
#include "ptlib.h"                     /* POSIX threads lib */
#include "tcpip.h"                     /* TCPIP stuff */
#include "EFW_filter.h" 
#include "ASICamera2.h"
#include "fits.h"

/* DEFINEs -------------------------------------------------------- */

#define P_TITLE         "ZwoServer"

#define PREFUN          __func__

#define KEY_WIN_X       "window_x"
#define KEY_WIN_Y       "window_y"
#define KEY_WIN_W       "window_w"
#define KEY_WIN_H       "window_h"
#define KEY_BINNING     "binning"
#define KEY_BITS        "bits"
#define KEY_RUN         "run_number"

/* STATICs -------------------------------------------------------- */

static char       logfile[512],rcfile[512];
static int        offtime=0;
static int        tcpDebug=0;
static u_int      cookie=0;
static char       dataPath[512];
static int        runNumber=0;

static time_t     startup_time;
static int        zwo_id=0; // todo used? -- fixed at '0' ?

static const int asi_id=0;
static ASI_EXPOSURE_STATUS asi_exp_status=0;
static u_char *asi_data=NULL;
static u_char *video_data1=NULL,*video_data2=NULL; // v0024
static u_int video_seq=0,video_last=0;
static volatile int video_running=0;   /* run_video thread alive */
/* safety margin on buffers passed to the SDK: ASIGetVideoData was
 * observed to write past w*h*bytes at 16-bit large ROIs (ASI294MM Pro,
 * SDK 1.20.2) corrupting the heap -> SEGV in a later realloc */
#define SDK_BUF_PAD (1L<<20)
static size_t asi_size=0;
static double asi_startTime,asi_expTime;
static int   asi_gain=0,asi_offset=10;
static int   asi_cooler;
static float asi_temperature,asi_target,asi_cooler_power;

static const int efw_id=0;

enum server_errors_enum { E_first=ASI_ERROR_END+1,
                          E_no_camera,
                          E_not_open,
                          E_not_idle,  /* 22 */
                          E_no_data,
			  E_not_video,
                          E_unknown,
                          E_last };

enum server_states_enum  { ZWO_CLOSED,
                           ZWO_IDLE,
                           ZWO_EXPOSING,
                           ZWO_VIDEO,
                           ZWO_LAST };

static int  zwo_state=ZWO_CLOSED;
static int  zwo_x=0,zwo_y=0,zwo_w=0,zwo_h=0,zwo_bin=0,zwo_bits=0;
static int  zwo_width=0,zwo_height=0;
static int  zwo_cooler=0,zwo_color=0;
static int  zwo_bitDepth=12;           /* v0022 */
static char zwo_model[32]="ZWO";       /* v0021 */
static char zwo_IDstr[16]="";          /* v0027 NEW v0031 */

/* function prototype(s) ------------------------------------------ */

static void*   run_tcpip         (void*);
static void*   run_video         (void*);

/* --- M A I N ---------------------------------------------------- */

int main(int argc,char **argv)
{
  int  i;
  char buf[128],buf2[128],buffer[1024];
#if (DEBUG > 0)
  fprintf(stderr,"%s-%s\n",P_TITLE,P_VERSION);
#endif

  char* version = ASIGetSDKVersion(); 
  printf("SDKversion=%s\n",version); //xxx
  // printf("ASI_ERROR_END=%d\n",ASI_ERROR_END);

  /* here comes the code ------------------------------------------ */ 

  startup_time = cor_time(0);

  { extern char *optarg;               /* parse command line */  
    extern int opterr,optopt; opterr=0;
    while ((i=getopt(argc,argv,"di:kw:")) != EOF) {
      switch (i) {
      case 'd':                        /* debug: allow mult. connections */
        tcpDebug = 1;
        break;
      case 'i':                        /* driver 'ID' {0,1,2} */
        zwo_id = atoi(optarg);
        break;
      case 'k':                        /* kill old instance */
        TCPIP_SingleCommand("localhost",SERVER_PORT+zwo_id,"quit\n");
        sleep(1);
        break;
      case 'w':                        /* wait */
        sleep(atoi(optarg));
        break;
      case '?':
        fprintf(stderr,"%s: option '-%c' unknown or parameter missing\n",
                P_TITLE,(char)optopt);
        break;
      }
    } /* endwhile(getopt) */
  }

  InitRandom(0,0,0);                   /* initialize random# */
  cookie = URandom();

  /* -------------------------------------------------------------- */

  sprintf(buffer,"zwoserver%d_",1+zwo_id); 
  log_path(logfile,"ZWOLOG",buffer);
  sprintf(buffer,"zwoserver%d.log",1+zwo_id);
  lnk_logfile(logfile,buffer);
  sprintf(buffer,"%s-v%s: UT= %s LT= %s",P_TITLE,P_VERSION,
          get_ut_timestr(buf,0),get_local_timestr(buf2,0));
  message(NULL,buffer,MSS_FLUSH);

  sprintf(rcfile,"%s/.zwoserver%drc",getenv("HOME"),1+zwo_id);
  printf("%s\n",rcfile);

  /* -------------------------------------------------------------- */

  runNumber = get_long(rcfile,KEY_RUN,1);
  strcpy(dataPath,getenv("HOME"));

  /* -------------------------------------------------------------- */

  run_tcpip(NULL);                     /* blocking this thread */

  /* -------------------------------------------------------------- */

#if (TIME_TEST > 0)                    /* print cpu-time */
  { struct tms tmsbuf;
    (void)times(&tmsbuf);
    sprintf(buffer,"%s(%d): total-cpu: %5.2f seconds",P_TITLE,
      (int)getpid(),
      (float)(tmsbuf.tms_stime)/(float)sysconf(_SC_CLK_TCK) +
      (float)(tmsbuf.tms_utime)/(float)sysconf(_SC_CLK_TCK));
    message(NULL,buffer,MSS_FLUSH); 
  }
#endif
  message(NULL,NULL,MSS_CLOSE);        /* v0017 */
 
  return 0;
}

/* ---------------------------------------------------------------- */

void message(const void* param,const char* text,int flags)
{
  static FILE* fp=NULL;
  char tstr[32];
#if (DEBUG > 1)
  fprintf(stderr,"%s(%s,%d,%p)\n",PREFUN,text,flags,file);
#endif

  get_ut_datestr(tstr,cor_time(offtime));
#ifndef NDEBUG
  fprintf(stderr,"%s: %s\n",tstr,text);
#endif

  if (flags & MSS_FILE) {              /* write to file */
    if (!fp) fp = fopen(logfile,"a");
    if (fp) { char lev[128];
      assert(text);
      if      (flags & MSS_RED)    strcpy(lev,"ERROR");
      else if (flags & MSS_YELLOW) strcpy(lev,"WARNING");
      else                         strcpy(lev,"-");
      fprintf(fp,"%s - %s: %s\n",tstr,lev,text);
      if (flags & MSS_FLUSH) fflush(fp);
    } else {
      fprintf(stderr,"%s:%s: opening %s failed\n",__FILE__,PREFUN,logfile);
    }
  }
  if (flags & MSS_CLOSE) { if (fp) fclose(fp); fp=NULL; }
}

/* ---------------------------------------------------------------- */

static int handle_efw(const char* command,char* answer,int buflen)
{
  int  err=0;
  char cmd[128]="",par1[128]="";
 
  assert(strlen(command) < sizeof(cmd));
  int n = sscanf(command,"%s %s",cmd,par1);
  if (n < 1) return 0;
  assert(!strncmp(cmd,"EFW",3));

  if (!strcmp(cmd,"EFWGetNum")) {
    int ret = EFWGetNum();
    sprintf(answer,"%d",ret);
  } else
  if (!strcmp(cmd,"EFWGetID")) { int i;
    err = EFWGetID(atoi(par1),&i);
    sprintf(answer,"%d %d",err,i);
  } else
  if (!strcmp(cmd,"EFWOpen")) {
    err = EFWOpen(efw_id);
    sprintf(answer,"%d",err);
  } else 
  if (!strcmp(cmd,"EFWGetProperty")) { EFW_INFO info;
    err = EFWGetProperty(atoi(par1),&info);
    if (err) sprintf(answer,"%d",err);
    else sprintf(answer,"%d %d %d %s",err,info.ID,info.slotNum,info.Name);
  } else
  if (!strcmp(cmd,"EFWClose")) {
    err = EFWClose(efw_id);
    sprintf(answer,"%d",err);
  } else 
  if (!strcmp(cmd,"EFWGetDirection")) { bool i;
    err = EFWGetDirection(efw_id,&i);
    sprintf(answer,"%d %d",err,i);
  } else 
  if (!strcmp(cmd,"EFWGetPosition")) { int i;
    err = EFWGetPosition(efw_id,&i);
    sprintf(answer,"%d %d",err,i);
  } else 
  if (!strcmp(cmd,"EFWSetPosition")) {
    int pos = atoi(par1);
    err = EFWSetPosition(efw_id,pos);
    sprintf(answer,"%d",err);
  } else 
  if (!strcmp(cmd,"EFWCalibrate")) {
    err = EFWCalibrate(efw_id);
    sprintf(answer,"%d",err);
  } else {
    err = E_unknown;
  }
  return err;
}

/* ---------------------------------------------------------------- */

static int handle_asi(const char* command,char* answer,int buflen)
{
  int  err=0;
  char cmd[128]="",par1[128]="",par2[128]="",par3[128]="",par4[128]="";
 
  assert(strlen(command) < sizeof(cmd));
  int n = sscanf(command,"%s %s %s %s %s",cmd,par1,par2,par3,par4);
  if (n < 1) return 0;
  assert(!strncmp(cmd,"ASI",3));

  if (!strcmp(cmd,"ASIGetNum")) {
    int ret = ASIGetNumOfConnectedCameras();
    sprintf(answer,"%d",ret);
  } else 
  if (!strcmp(cmd,"ASIOpenCamera")) {
    err = ASIOpenCamera(asi_id);
    sprintf(answer,"%d",err);
  } else
  if (!strcmp(cmd,"ASIInitCamera")) {
    err = ASIInitCamera(asi_id);
    sprintf(answer,"%d",err);
  } else
  if (!strcmp(cmd,"ASIGetCameraProperty")) { ASI_CAMERA_INFO info;
    ASIGetCameraProperty(&info,asi_id);
    sprintf(answer,"%ld %ld %d %d %d %s",info.MaxWidth,info.MaxHeight,
      info.IsCoolerCam,info.IsColorCam,info.BitDepth,info.Name);  /* v0022 */
    zwo_width = info.MaxWidth;
    zwo_height = info.MaxHeight;
    zwo_cooler = info.IsCoolerCam;
    zwo_color = info.IsColorCam;
    zwo_bitDepth = info.BitDepth;      /* v0022 */
    strcpy(zwo_model,info.Name);       /* v0017 */
    char *p = strchr(zwo_model,'(');   /* cut at '(' v0027 */
    if (p) {
      *p = '\0'; strcpy(zwo_IDstr,p+1); 
      p = strchr(zwo_IDstr,')'); if (p) *p = '\0'; /* remove ')' */
    }
    do { p=strchr(zwo_model,' '); if (p) *p='_'; } while (p); /* v0021 */
    sprintf(answer,"%d %d %d %d %d %s",zwo_width,zwo_height,  /* v0022 */
      zwo_cooler,zwo_color,zwo_bitDepth,zwo_model);           /* v0027 */
#if 1 /* NEW v0033 */
    { ASI_CONTROL_CAPS caps; int num=0,i;
      ASIGetNumOfControls(asi_id,&num);
      for (i=0; i<num; i++) {
        ASIGetControlCaps(asi_id,i,&caps);
        printf("%24s %5ld %11ld %6ld %d %d %s\n",caps.Name,
          caps.MinValue,caps.MaxValue,caps.DefaultValue,
          caps.IsAutoSupported,caps.IsWritable,caps.Description);
      }
    }
#endif
  } else
  if (!strcmp(cmd,"ASIGetSerialNumber")) { ASI_SN sn; int i;
    ASIGetSerialNumber(asi_id,&sn);    /* v0030 */
    *answer = '\0';
    for (i=0; i<8; i++) { 
      sprintf(par4,"%02x",sn.id[i]); strcat(answer,par4);
    }
  } else 
  if (!strcmp(cmd,"ASICloseCamera")) {
    err = ASICloseCamera(asi_id);
    sprintf(answer,"%d",err);
  } else
  if (!strcmp(cmd,"ASISetControlValue")) {
    int control = atoi(par1);
    int value = atoi(par2);
    err = ASISetControlValue(asi_id,control,value,ASI_FALSE);
    sprintf(answer,"%d",err);
    if (!err) { 
      switch (control) { 
        case ASI_EXPOSURE: asi_expTime = (double)value/1.0e6; break;
        case ASI_COOLER_ON: asi_cooler = value; break;
        case ASI_TARGET_TEMP: asi_target = (float)value; break;
      }
    }
  } else
  if (!strcmp(cmd,"ASIGetControlValue")) { long v; ASI_BOOL b;
    int control = atoi(par1);
    err = ASIGetControlValue(asi_id,control,&v,&b);
    sprintf(answer,"%d %ld",err,v);
    if (!err) { 
      switch (control) { 
        case ASI_EXPOSURE: asi_expTime = (double)v/1.0e6; break;
        case ASI_TEMPERATURE: asi_temperature = (float)v/10.0f; break;
        case ASI_COOLER_POWER_PERC: asi_cooler_power = (float)v; break;
      }
    }
  } else 
  if (!strcmp(cmd,"ASISetROIFormat")) {
    err = ASISetROIFormat(asi_id,atoi(par1),atoi(par2),atoi(par3),atoi(par4));
    sprintf(answer,"%d",err);
    if (!err) {
      if (zwo_bitDepth == 16) {        /* v0023 */
        (void)ASIStartExposure(asi_id,ASI_FALSE);
        (void)ASIStopExposure(asi_id);
      }
    }
  } else 
  if (!strcmp(cmd,"ASIGetROIFormat")) { int w,h,b,t;
    err = ASIGetROIFormat(asi_id,&w,&h,&b,&t);
    sprintf(answer,"%d %d %d %d %d",err,w,h,b,t);
    if (!err) {
      zwo_w = w; zwo_h = h; zwo_bin = b; zwo_bits = (t==0) ? 8 : 16;
    }
  } else 
  if (!strcmp(cmd,"ASISetStartPos")) {
    err = ASISetStartPos(asi_id,atoi(par1),atoi(par2));
    sprintf(answer,"%d",err);
  } else 
  if (!strcmp(cmd,"ASIGetStartPos")) { int x,y;
    err = ASIGetStartPos(asi_id,&x,&y);
    sprintf(answer,"%d %d %d",err,x,y);
    if (!err) { 
      zwo_x = x; zwo_y = y;
    }
  } else
  if (!strcmp(cmd,"ASIStartExposure")) {
    err = ASIStartExposure(asi_id,ASI_FALSE);
    asi_startTime = walltime(0);
    sprintf(answer,"%d",err);
    if (!err) zwo_state = ZWO_EXPOSING;
  } else
  if (!strcmp(cmd,"ASIStopExposure")) {
    err = ASIStopExposure(asi_id);
    sprintf(answer,"%d",err);
    if (!err) zwo_state = ZWO_IDLE;
  } else
  if (!strcmp(cmd,"ASIGetExpStatus")) {
    err = ASIGetExpStatus(asi_id,&asi_exp_status);
    sprintf(answer,"%d %d",err,asi_exp_status);
  } else
  if (!strcmp(cmd,"ASIGetDataAfterExp")) {
    asi_size = atoi(par1);
    asi_data = (u_char*)realloc(asi_data,asi_size+SDK_BUF_PAD);
    int ret = ASIGetDataAfterExp(asi_id,asi_data,asi_size);
    // double t2 = walltime(0);
    // double dt = t2 - asi_startTime - asi_expTime;
    // printf("%.1f MB/s %.3f\n",((float)asi_size/1.0e6)/dt,dt);
    if (ret != ASI_SUCCESS) { asi_size = 0; err = -1; } // todo
    sprintf(answer,"%d",ret);
  } else 
  if (!strcmp(cmd,"ASIStartVideoCapture")) { // IDEA video thread
    err = ASIStartVideoCapture(asi_id);      // requires Mutex
    if (!err) msleep(350);
    sprintf(answer,"%d",err);
  } else
  if (!strcmp(cmd,"ASIGetVideoData")) {
    asi_size = atoi(par1);
    asi_data = (u_char*)realloc(asi_data,asi_size+SDK_BUF_PAD);
    int wait_ms = atoi(par2);
    double t1 = walltime(0);
    int ret = ASIGetVideoData(asi_id,asi_data,asi_size,wait_ms);
    double t2 = walltime(0);
    printf("%.1f MB/s (%.3f)\n",((float)asi_size/1.0e6)/(t2-t1),t2-t1); //xxx
    if (ret != ASI_SUCCESS) { asi_size = 0; err = -1; } // todo
    sprintf(answer,"%d",ret);
  } else
  if (!strcmp(cmd,"ASIGetDroppedFrames")) { int dropped;
    err = ASIGetDroppedFrames(asi_id,&dropped);
    sprintf(answer,"%d %d",err,dropped);  
  } else 
  if (!strcmp(cmd,"ASIStopVideoCapture")) {
    err = ASIStopVideoCapture(asi_id);
    sprintf(answer,"%d",err);
  } else {
    err = E_unknown;
  }
  return err;
}

/* ---------------------------------------------------------------- */

static int handle_command(const char* command,char* answer,size_t buflen)
{
  int  r=0,n=0;
  long err=0;
  char cmd[512]="",par1[128]="",par2[128]="",par3[128]="",par4[128]="";
  char buf[128]="",par5[128]="",par6[128]="";
#if (DEBUG > 1)
  sprintf(cmd,"%s: %s",PREFUN,command);
  fprintf(stderr,"%s\n",cmd);
  message(NULL,cmd,MSS_FILE);
#endif

  strcpy(answer,"-Einvalid command\n");
  if (strlen(command) >= sizeof(cmd)) return 0;
  n = sscanf(command,"%s %s %s %s %s %s %s",cmd,par1,par2,par3,par4,par5,par6);
  if (n < 1) return 0;

  strcpy(answer,"OK");                 /* default response */
  if (!strncmp(cmd,"ASI",3)) {         /* ASI commands */
    err = handle_asi(command,answer,buflen);
  } else
  if (!strncmp(cmd,"EFW",3)) {         /* EFW commands */
    err = handle_efw(command,answer,buflen);
  } else
  if (!strcasecmp(cmd,"version")) {    /* server version */
    sprintf(answer,"%s %u %ld",P_VERSION,cookie,startup_time);
  } else
  if (!strcasecmp(cmd,"offtime")) {
    if (n > 1) offtime = cor_time(0) - atol(par1);
    sprintf(buf,"offtime= %d",offtime);
    message(NULL,buf,MSS_FILE);
    sprintf(answer,"%d",offtime);
  } else
  if (!strcasecmp(cmd,"open")) { // ASI_SN sn; ASI_ID id;
    if (zwo_state == ZWO_CLOSED) {
      err = handle_asi("ASIGetNum",answer,buflen);
      if (!err && (atoi(answer) == 0)) err = E_no_camera;
      if (!err) err = handle_asi("ASIOpenCamera",answer,buflen);
      if (!err) err = handle_asi("ASIInitCamera",answer,buflen);
      if (!err) err = handle_asi("ASIGetCameraProperty",answer,buflen);
      if (!err) err = handle_asi("ASIGetROIFormat",answer,buflen);
      if (!err) err = handle_asi("ASIGetStartPos",answer,buflen);
      if (!err) zwo_state = ZWO_IDLE;
      if (!err) handle_command("exptime 0.02",answer,buflen);
    }
    if (!err) sprintf(answer,"%d %d %d %d %d %s",zwo_width,zwo_height,
          zwo_cooler,zwo_color,zwo_bitDepth,zwo_model); /* v0027 */
  } else
  if (!strcasecmp(cmd,"setup")) { 
    if (zwo_state != ZWO_IDLE) {
      err = E_not_idle;
    } else {
      if (!strncasecmp(par1,"defaults",3)) { 
        zwo_x =    (int)get_long(rcfile,KEY_WIN_X,0); 
        zwo_y =    (int)get_long(rcfile,KEY_WIN_Y,0);
        zwo_w =    (int)get_long(rcfile,KEY_WIN_W,4656);
        zwo_h =    (int)get_long(rcfile,KEY_WIN_H,3520);
        zwo_bin =  (int)get_long(rcfile,KEY_BINNING,1);
        zwo_bits = (int)get_long(rcfile,KEY_BITS,16);
      } else
      if (!strncasecmp(par1,"image",3)) { 
        zwo_x = 0; zwo_y = 0; 
        zwo_bin = (n>2) ? atoi(par2) : 1;
        zwo_w = zwo_width/zwo_bin; zwo_h = zwo_height/zwo_bin;
        zwo_bits = 16;
      } else
      if (!strncasecmp(par1,"video",3)) { 
        zwo_x = 0; zwo_y = 0; 
        zwo_bin = (n>2) ? atoi(par2) : 1;
        zwo_w = zwo_width/zwo_bin; zwo_h = zwo_height/zwo_bin;
        zwo_bits = 8;
      } else
      if (n > 6) {
        zwo_x = atoi(par1);    put_long(rcfile,KEY_WIN_X,zwo_x);
        zwo_y = atoi(par2);    put_long(rcfile,KEY_WIN_Y,zwo_y);
        zwo_w = atoi(par3);    put_long(rcfile,KEY_WIN_W,zwo_w);
        zwo_h = atoi(par4);    put_long(rcfile,KEY_WIN_H,zwo_h);
        zwo_bin = atoi(par5);  put_long(rcfile,KEY_BINNING,zwo_bin);
        zwo_bits = atoi(par6); put_long(rcfile,KEY_BITS,zwo_bits);
      } 
    }
    if (n > 1) { int t;                /* 't' v0017 */
      while (zwo_w % 8) { zwo_w -= 1; }
      while (zwo_h % 2) { zwo_h -= 1; }
      if (zwo_color) { int bits=zwo_bits;
        t = (bits==8) ? ASI_IMG_Y8 : (bits==16) ? ASI_IMG_RAW16 : ASI_IMG_RGB24;
      } else {
        t = (zwo_bits==8) ? ASI_IMG_RAW8 : ASI_IMG_RAW16;
      }
      sprintf(buf,"ASISetROIFormat %d %d %d %d",zwo_w,zwo_h,zwo_bin,t);
      if (!err) err = handle_asi(buf,answer,buflen);
      sprintf(buf,"ASISetStartPos %d %d",zwo_x,zwo_y);
      if (!err) err = handle_asi(buf,answer,buflen);
    }
    if (!err) sprintf(answer,"%d %d %d %d %d %d\n",
                      zwo_x,zwo_y,zwo_w,zwo_h,zwo_bin,zwo_bits);
  } else
  if (!strcasecmp(cmd,"exptime")) {
    if (zwo_state == ZWO_CLOSED) err = E_not_open;
    if (!err && (n > 1)) {
      int et = (int)floor(1.0e6*atof(par1)+0.5);
      sprintf(buf,"ASISetControlValue %d %d",ASI_EXPOSURE,et);
      err = handle_asi(buf,answer,buflen);
    }
    if (!err) sprintf(answer,"%.6f",asi_expTime);
  } else 
  if (!strcasecmp(cmd,"gain")) {
    if (zwo_state == ZWO_CLOSED) err = E_not_open;
    if (!err && (n > 1)) {
      asi_gain = atoi(par1);
      sprintf(buf,"ASISetControlValue %d %d",ASI_GAIN,asi_gain);
      err = handle_asi(buf,answer,buflen);
    }
    if (!err) sprintf(answer,"%d",asi_gain);
  } else 
  if (!strcasecmp(cmd,"offset")) {
    if (zwo_state == ZWO_CLOSED) err = E_not_open;
    if (!err && (n > 1)) {
      asi_offset = atoi(par1);
      sprintf(buf,"ASISetControlValue %d %d",ASI_OFFSET,asi_offset);
      err = handle_asi(buf,answer,buflen);
    }
    if (!err) sprintf(answer,"%d",asi_offset);
  } else
  if (!strcasecmp(cmd,"status")) {
    if (zwo_state == ZWO_EXPOSING) { int a,b;
      handle_asi("ASIGetExpStatus",answer,buflen);
      sscanf(answer,"%d %d",&a,&b);
      if (b != ASI_EXP_WORKING) zwo_state = ZWO_IDLE;
    }
    switch (zwo_state) { 
      case ZWO_CLOSED:   sprintf(answer,"closed"); break;
      case ZWO_IDLE:     sprintf(answer,"idle"); break;
      case ZWO_EXPOSING: 
        sprintf(answer,"exposing %.1f",walltime(0)-asi_startTime); 
        break;
      case ZWO_VIDEO:    sprintf(answer,"streaming"); break;
      default:           sprintf(answer,"unknown"); break;
    }
  } else
  if (!strcasecmp(cmd,"expose")) {
    if (zwo_state != ZWO_IDLE) {
      handle_command("status",answer,buflen); 
      if (zwo_state != ZWO_IDLE) err = E_not_idle;
    }
    if (!err) {
      err = handle_asi("ASIStartExposure",answer,buflen);
    }
  } else
  if (!strcasecmp(cmd,"data")) {
    if (zwo_state != ZWO_VIDEO) { 
      if (zwo_state != ZWO_IDLE) {
        handle_command("status",answer,buflen); 
        if (zwo_state != ZWO_IDLE) err = E_not_idle;
      }
    }
    if (!err) { int size = zwo_w * zwo_h * zwo_bits/8;
      // printf("w=%d, h=%d, bits=%d\n",zwo_w,zwo_h,zwo_bits);
      if (zwo_state == ZWO_VIDEO) { 
        int wait = 350+(int)(1000.0*asi_expTime); 
        printf("wait=%d, size=%u\n",wait,size); //xxx
        sprintf(buf,"ASIGetVideoData %d %d",size,wait);
      } else {
        sprintf(buf,"ASIGetDataAfterExp %d",size);
      }
      err = handle_asi(buf,answer,buflen);
    }
    if (!err) {
      sprintf(answer,"%u\n",(u_int)asi_size);
      if (n > 1) asi_size = imin(asi_size,atoi(par1));
    }
  } else
  if (!strcasecmp(cmd,"next")) {       /* v0024 */
    if (zwo_state != ZWO_VIDEO) { 
      err = E_not_video;
    } else {  
      double timeout = (n > 1) ? atof(par1) : 0;
      double t1 = walltime(0);
      while (video_seq <= video_last) {     /* b0025 */
	if (walltime(0)-t1 >= timeout) break;
        msleep(1);   /* 5ms quantum capped video at ~194 fps */
      }
      if (video_seq > video_last) {
        __sync_synchronize(); /* frame data written before seq (arm64) */
        video_last = video_seq;
        u_char *data = (video_last % 2) ? video_data2 : video_data1;
        asi_size = zwo_w * zwo_h * zwo_bits/8;
        asi_data = (u_char*)realloc(asi_data,asi_size);
        memcpy(asi_data,data,asi_size); // don't shift here v0028 
        sprintf(answer,"%u %.1f %.0f",video_last,
                asi_temperature,asi_cooler_power);
      } else {
        strcpy(answer,"-Enodata");
      }
    }
  } else
  if (!strcasecmp(cmd,"start")) {
    if (zwo_state != ZWO_IDLE) err = E_not_idle;
    if (!err) { int i;   /* previous thread must be gone before its */
                         /* buffers are realloc'ed for the new one  */
      for (i=0; video_running && (i<300); i++) msleep(10);
      err = handle_asi("ASIStartVideoCapture",answer,buflen);
      if (!err) { size_t vsize = (size_t)zwo_w*zwo_h*zwo_bits/8;
        /* buffers are (re)allocated HERE, on the thread that also   */
        /* serves 'next': aarch64 reorders cross-thread stores, so a */
        /* consumer must never read pointers written by run_video    */
        video_data1 = (u_char*)realloc(video_data1,vsize+SDK_BUF_PAD);
        video_data2 = (u_char*)realloc(video_data2,vsize+SDK_BUF_PAD);
        zwo_state = ZWO_VIDEO;
        video_running = 1;
        __sync_synchronize();
        thread_detach(run_video,NULL);  /* v0024 */
      }
    }
  } else
  if (!strcasecmp(cmd,"stop")) {
    if (zwo_state == ZWO_VIDEO) { int i;
      zwo_state = ZWO_IDLE;
      /* wait for run_video to leave ASIGetVideoData() first: the SDK */
      /* is not thread-safe and StopVideoCapture during GetVideoData  */
      /* corrupts the heap (SEGV in a later realloc)                  */
      for (i=0; video_running && (i<3000); i++) msleep(10);
      err = handle_asi("ASIStopVideoCapture",answer,buflen);
      msleep(350);   /* let SDK worker threads settle, cf. 'start' */
    }
  } else
  if (!strcasecmp(cmd,"write")) { 
    handle_command("data 0",answer,buflen);
    assert(asi_size == 0);
    int size = atoi(answer);
    if (size != zwo_w*zwo_h*zwo_bits/8) {
      err = E_no_data;
    } else {
      handle_command("tempcon",buf,sizeof(buf));
      FITS *f = fits_create(zwo_w,zwo_h,zwo_bits);
      f->bitpix = zwo_bits;
      if (zwo_bits == 8) f->bzero = 0;
      if (n > 1) runNumber = atoi(par1);
      sprintf(answer,"%s/zwo%04d.fits",dataPath,runNumber);
      if (fits_open(f,answer) == 0) { struct tm res;
        time_t ut = (int)asi_startTime - offtime;
        gmtime_r(&ut,&res);
        strftime(buf,32,"%FT%H:%M:%S",&res);
        fits_char (f,"INSTRUME",zwo_model,NULL);
        fits_char (f,"DATE-OBS",buf,NULL);
        fits_float(f,"EPOCH",get_epoch(ut),5,"epoch (start)");
        fits_float(f,"EXPTIME",asi_expTime,3,"exposure time");
        fits_int  (f,"BINNING",zwo_bin,"binning");
        sprintf(buf,"[%d:%d,%d:%d]",1+zwo_x,zwo_x+zwo_w,1+zwo_y,zwo_y+zwo_h);
        fits_char (f,"WINDOW",buf,"window"); 
        fits_char (f,"COMMENT","","no comment");
        fits_float(f,"TEMPCCD",asi_temperature,2,"CCD temperature [C]");
        fits_float(f,"COOLCCD",asi_cooler_power,0,"CCD cooler [%]");
        fits_char (f,"SOFTWARE",P_VERSION,NULL);
        fits_char (f,"FITSVERS","0.019",NULL);
        fits_endHeader(f);
        fits_writeData(f,asi_data);
        fits_close(f);
        fits_free(f);
        runNumber = (1+runNumber) % 10000;
        put_long(rcfile,KEY_RUN,runNumber);
      }
    }
  } else
  if (!strcasecmp(cmd,"close")) {
    if (zwo_state != ZWO_CLOSED) {
      err = handle_asi("ASICloseCamera",answer,buflen);
      zwo_state = ZWO_CLOSED;
    }
  } else
  if (!strcasecmp(cmd,"tempcon")) {
    if (n > 1) { int v=1;
      if (!strcmp(par1,"off")) v = 0;
      sprintf(buf,"ASISetControlValue %d %d",ASI_COOLER_ON,v);
      err = handle_asi(buf,answer,buflen);
      if (!err) {
        if (v) { 
          int t = (int)floor(0.5+atof(par1));
          sprintf(buf,"ASISetControlValue %d %d",ASI_TARGET_TEMP,t);
          handle_asi(buf,answer,buflen); // bug-fix v0026
        } else {
          asi_cooler_power = 0;          /* NEW v0032 */
        }
      }
    } else { int v; float t,p;
      sprintf(buf,"ASIGetControlValue %d",ASI_TEMPERATURE);
      if (!err) err = handle_asi(buf,answer,buflen);
      if (!err) sscanf(answer,"%ld %d",&err,&v);
      if (!err) t = (float)v/10.0f;
      sprintf(buf,"ASIGetControlValue %d",ASI_COOLER_POWER_PERC);
      if (!err) err = handle_asi(buf,answer,buflen);
      if (!err) sscanf(answer,"%ld %d",&err,&v);
      if (!err) p = (float)v;
      if (!err) sprintf(answer,"%.1f %.0f",t,p);
    }
  } else
  if (!strcasecmp(cmd,"fancon")) {
    int v = 0;
    if (n > 1) {
      if (!strcmp(par1,"off")) {
        v = 0;
      } else
      if (!strcmp(par1,"on")) {
        v = 1;
      } else {
        strcpy(answer,"-Einvalid fan state");
        return 0;
      }
      sprintf(buf,"ASISetControlValue %d %d",ASI_FAN_ON,v);
      err = handle_asi(buf,answer,buflen);
    }
    sprintf(buf,"ASIGetControlValue %d",ASI_FAN_ON);
    err = handle_asi(buf,answer,buflen);
    if (!err) sscanf(answer,"%d",&err,&v);
    if (!err) sprintf(answer,"%d",v);
  } else
  if (!strcasecmp(cmd,"filters")) { /* NOTE: must be called before 'filter' */
    err = handle_efw("EFWGetNum",answer,buflen);
    if (err || (atoi(answer)) == 0) {
      sprintf(answer,"%d",0);
    } else { int i,f=0;
      if (!err) err = handle_efw("EFWOpen",answer,buflen);
      if (!err) err = handle_efw("EFWGetProperty",answer,buflen);
      if (!err) sscanf(answer,"%ld %d %d %s",&err,&i,&f,buf);
      if (!err) err = handle_efw("EFWClose",buf,sizeof(buf));
      if (!err) sprintf(answer,"%d",f);
    }
  } else
  if (!strcasecmp(cmd,"filter")) {
    if (!err) err = handle_efw("EFWOpen",answer,buflen);
    if (!err) { 
      if (n > 1) {
        sprintf(buf,"EFWSetPosition %d",atoi(par1));
        err = handle_efw(buf,answer,buflen);
      } else { int i;
        err = handle_efw("EFWGetPosition",answer,buflen);
        if (!err) sscanf(answer,"%ld %d",&err,&i);
        if (!err) sprintf(answer,"%d",i);
      }
      handle_efw("EFWClose",buf,sizeof(buf));
    }
  } else
  if (!strcasecmp(cmd,"quit")) {       /* terminate server */
    message(NULL,cmd,MSS_FLUSH);
    exit(0);
  } else 
  if (!strcasecmp(cmd,"reboot")) {     /* reboot TODO ? */
    message(NULL,cmd,MSS_FLUSH);
    r = 2;                             /* terminate and reboot */
  } else
  if (!strcasecmp(cmd,"restart")) {    /* restart myself */
    message(NULL,cmd,MSS_FLUSH);
    r = 3;                             /* terminate and restart */
  } else
  if (!strcasecmp(cmd,"shutdown") || !strcasecmp(cmd,"poweroff")) {
    message(NULL,cmd,MSS_FLUSH);
    r = 4;
  } else {
    strcpy(answer,"-Eunknown command");
  }
  if (err) { 
    sprintf(answer,"-Eerr=%ld",err);
  } else {                             /* remove <LF> */
    do { 
      size_t len = strlen(answer);
      assert(len > 0);
      if (answer[len-1] != '\n') break;
      answer[len-1] = '\0';
    } while (strlen(answer));
  }
#if (DEBUG > 1)
  sprintf(cmd,"%s: send '%s'",PREFUN,answer);
  message(NULL,cmd,MSS_FILE);
#endif
  strcat(answer,"\n");

  return r;
}

/* --- */

static int receive_string(int sock,char* string,size_t buflen)
{
  int     i=0;
  ssize_t r;

  do {
    r = recv(sock,&string[i],1,0);
    if (r != 1) break;
    if (string[i] == '\n') break;      /* <LF> */
    if (string[i] == '\r') break;      /* <CR> */
    i++;
  } while (i < buflen-1);
  string[i] = '\0';                    /* overwrite term-char */

  return r;
}

/* --- */

typedef struct {
  char host[128];
  int  port,msgsock;
} Connection;
  
/* --- */

static void* run_connection(void* param)
{
  Connection *c = (Connection*)param;
  int  rval,r;
  long done=0;
  char cmd[128],buf[256];

  do {
    rval = receive_string(c->msgsock,cmd,sizeof(cmd));
    if (rval > 0) {
#if (DEBUG > 1)
      sprintf(buf,"%s(): received '%s'",PREFUN,cmd);
      message(NULL,buf,MSS_FILE);
#endif
      r =  handle_command(cmd,buf,sizeof(buf));
      send(c->msgsock,buf,strlen(buf),MSG_NOSIGNAL);
      if (r == 0) {
        if (asi_size) {
          send(c->msgsock,asi_data,asi_size,MSG_NOSIGNAL);
          asi_size = 0;
        }
      } else
      if (r == 2) {                    /* reboot MinnowBoard */
        sync(); sleep(1);              /* sync disks */
#ifndef SIM_ONLY
        reboot(LINUX_REBOOT_CMD_RESTART); /* Note: requires 'root' */
#endif
        exit(0);
      } else
      if (r == 3) {                    /* restart */
        sprintf(buf,"nohup %s/zwoserver -w 10 -i %d &",getenv("HOME"),zwo_id);
        system(buf);
        exit(0);
      } else 
      if (r == 4) {                    /* power down MinnowBoard */
        sync(); sleep(1);              /* sync disks */
#ifndef SIM_ONLY
        reboot(LINUX_REBOOT_CMD_POWER_OFF); /* Note: requires 'root' */
#endif
        exit(0);
      }
    } else {
      sprintf(buf,"%s(%s): hangup",PREFUN,c->host);
      message(NULL,buf,MSS_FLUSH);
      if (zwo_state != ZWO_CLOSED) ASICloseCamera(asi_id);
      zwo_state = ZWO_CLOSED;
    }
  } while (rval > 0);                  /* while there's something */
  (void)close(c->msgsock);
  free((void*)c);

  return (void*)done;
}

/* --- */

static void* run_tcpip(void* param)
{
  int    port=SERVER_PORT+zwo_id;
  const int done=0;
  int    err,msgsock;
  char   buf[256],host[128];
  struct sockaddr sadr;
  socklen_t size=sizeof(sadr);
#if (DEBUG > 0)
  fprintf(stderr,"%s(%d)\n",PREFUN,port);
#endif

  int sock = TCPIP_CreateServerSocket(port,&err);
  if (err) {
    sprintf(buf,"%s(%d): err=%d",PREFUN,port,err);
    message(NULL,buf,MSS_FLUSH);
    return (void*)(long)err;
  }

  while (!done) {
    // NOTE: if someone tries to connect while we are busy
    //       the requests get queued -- IDEA flush ?
    msgsock = accept(sock,&sadr,&size);   /* accept connection */
#ifdef SO_NOSIGPIPE                    /* 2017-07-05 */
    int on=1;
    (void)setsockopt(msgsock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
    { int nd=1;      /* no Nagle delay on header+data sends of 'next' */
      (void)setsockopt(msgsock,IPPROTO_TCP,TCP_NODELAY,&nd,sizeof(nd));
    }
    sprintf(host,"%u.%u.%u.%u",
            (u_char)sadr.sa_data[2],(u_char)sadr.sa_data[3],
            (u_char)sadr.sa_data[4],(u_char)sadr.sa_data[5]);
    sprintf(buf,"connection accepted from %s",host);
    message(NULL,buf,MSS_FLUSH);
#ifdef SO_NOSIGPIPE                    /* 2017-07-05 */
    (void)setsockopt(msgsock,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
    Connection *c = malloc(sizeof(Connection));
    strcpy(c->host,host);
    c->port = port;
    c->msgsock = msgsock;
    if (tcpDebug) {                    /* allow mult. connections */
      thread_detach(run_connection,(void*)c);
    } else {                           /* single connection */
      run_connection((void*)c);
    }
  } /* while(!done) */

  (void)close(sock);

  sprintf(buf,"%s(%d) done\n",PREFUN,port);
  message(NULL,buf,MSS_FLUSH);

  return (void*)0;
}

/* ---------------------------------------------------------------- */

static void* run_video(void* param)
{
  int    wait,ret,size=zwo_w*zwo_h*zwo_bits/8;
  time_t next=0;
  u_char *data;
  char   buf[128];

  /* video_data1/2 are allocated by 'start' before this thread spawns */

  while (zwo_state == ZWO_VIDEO) {
    if (cor_time(0) < next) {   // v0026
      msleep(5);
    } else {                    // update temp/cooler
      handle_command("tempcon",buf,sizeof(buf));
      next = cor_time(0)+30; // TODO every 30 seconds
    }
    wait = 350+(int)(1000.0*asi_expTime); 
    // printf("wait=%d, size=%u, seq=%u\n",wait,size,video_seq);
    data = (video_seq % 2) ? video_data1 : video_data2;
    // printf("writing to buffer %d\n",(data==video_data1) ? 1 : 2);
    ret = ASIGetVideoData(asi_id,data,size,wait);
    if (ret == ASI_SUCCESS) {
      __sync_synchronize();  /* frame data visible before seq (arm64) */
      video_seq++;
    }
  }
  printf("%s done\n",PREFUN); //xxx
  __sync_synchronize();
  video_running = 0;

  return (void*)0;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

