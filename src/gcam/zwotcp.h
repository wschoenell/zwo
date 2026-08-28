/* ----------------------------------------------------------------
 *
 * zwotcp.h
 *
 * ---------------------------------------------------------------- */

#include <pthread.h>

#ifdef MACOSX
#include <sys/types.h>
#endif

/* ---------------------------------------------------------------- */

#define ASI_ERROR_END  18

enum zwo_errors_enum { E_zwo_error = 1+ASI_ERROR_END,
                       E_zwo_not_connected,
                       E_zwo_setpar,
                       E_zwo_last };

#define ZWO_NBUFS   3

typedef struct zwo_frame_tag {
  u_int seqNumber;
  unsigned long long ts_ns;  /* server-side receive time [ns], 0=unknown */
  u_short *data;
  int w,h;
  volatile int wlock,rlock;
} ZwoFrame;

typedef struct zwo_struct_tag {
  char   host[128];
  int    port,handle;        /* handle=socket */
  volatile int err;
  char   serverVersion[32],modelName[32],serialNumber[32];
  u_int  cookie;
  float  tempSensor,tempSetpoint,coolerPercent;
  int    sensorW,sensorH;
  int    aoiW,aoiH;
  double expTime;  
  int    gain,offset;
  volatile int rolling;
  double fps;
  pthread_mutex_t ioLock,frameLock;
  u_int seqNumber;
  ZwoFrame frames[ZWO_NBUFS];
  pthread_t tid;
  volatile int stop_flag;
  char *mask;                 /* v0320 */
} ZwoStruct;

/* ---------------------------------------------------------------- */

ZwoStruct* zwo_create     (const char*,int);

int  zwo_connect    (ZwoStruct*);
int  zwo_open       (ZwoStruct*);
int  zwo_setup      (ZwoStruct*,int,int,int,int);
int  zwo_disconnect (ZwoStruct*);
int  zwo_close      (ZwoStruct*);
void zwo_free       (ZwoStruct*);

int zwo_temperature (ZwoStruct*,const char*);
int zwo_exptime     (ZwoStruct*,double);
int zwo_gain        (ZwoStruct*,int,int);

int zwo_cycle_start (ZwoStruct*);
int zwo_cycle_stop  (ZwoStruct*);

ZwoFrame* zwo_frame4writing(ZwoStruct*,u_int);
ZwoFrame* zwo_frame4reading(ZwoStruct*,u_int);
void      zwo_frame_release(ZwoStruct*,ZwoFrame*);

int zwo_server(ZwoStruct*,const char*,char*);

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

