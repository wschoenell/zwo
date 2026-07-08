/* ----------------------------------------------------------------
 *
 * utils.c
 * 
 * utility functions
 *
 * 1998-06-01  Christoph C. Birk, birk@obs.carnegiescience.edu
 * 1999-07-01  [CCB]
 * 2001-08-01  get_string(),lockf()  [CCB]
 * 2002-04-17  get_double(),put_long(),put_double()  [CCB]
 *        -31  round()
 *      05-28  set_path()  [CCB]
 * 2004-06-24  alpha_str2(), delta_str2()  [CCB]
 * 2006-05-31  mail_file()  [CCB]
 * 2006-08-23  improved put_string()  [CCB]
 *     -12-01  fixed cut_spaces()  [CCB]
 * 2011-08-22  espeak & say  [CCB]
 * 2011-11-10  generic version (BNC, CAPS, NewCCD)
 * 2012-09-06  LDSS3
 * 2013-05-20  MagE
 * 2013-05-22  MIKE,PFS
 * 2014-06-24  'festival'
 * 2015-09-17  fix get_sidereal() by -3.23 seconds
 * 2015-10-28  modify get_epoch()
 * 2016-04-05  __isleap macro
 * 2016-05-05  faster implementation of get_epoch(),float_time()
 *
 * ---------------------------------------------------------------- */

#define IS_UTILS_C

/* DEFINEs -------------------------------------------------------- */

#ifndef DEBUG
#define DEBUG      1                   /* debug level */
#endif

#define PREFUN     __func__

/* INCLUDES ------------------------------------------------------- */

#define _REENTRANT                     /* gmtime_r() */

#include <stdlib.h>                    /* getenv() */
#include <stdio.h>                     /* fprintf() */
#include <string.h>                    /* strcpy() */
#include <math.h>                      /* fabs(),floor() */
#include <ctype.h>                     /* isspace() */
#include <unistd.h>                    /* fork(),_exit(),open() */
#include <fcntl.h>                     /* O_RDWR */
#include <time.h>                      /* nanosleep(),ctime_r() */
#include <assert.h>

#if (DEBUG > 0)
#include <errno.h>
#endif

#ifdef MACOSX
#include <sys/param.h>
#include <sys/mount.h>
#else
#include <sys/statvfs.h>               /* statvfs() */
#endif
#include <sys/stat.h>                  /* fchmod() */

#include "utils.h"

/* GLOBALs -------------------------------------------------------- */

/* none */


/* EXTERNs -------------------------------------------------------- */

/* none */


/* STATICs -------------------------------------------------------- */

/* function prototype(s) */

static char* dtime (char*);

/* ---------------------------------------------------------------- */

char* extend(char *s, char c, int l)
{
  int i;
 
  i = strlen(s);
  while (i < l) {
    s[i++] = c;
  }
  s[i] = '\0';                                          /* s[l] = '\0';   */

  return(s);
}
 
/* ---------------------------------------------------------------------- */

char* uppercase(char *string)
{
  char c,*s;

  s = string;
  while (*s != '\0') { 
    c = (char)toupper((int)*s);
    *s++ = c;
  }
  return(string);
}

/* ---------------------------------------------------------------------- */

char* lowercase(char *string)
{
  char c,*s;
 
  s = string;
  while (*s != '\0') {
    c = (char)tolower((int)*s);
    *s++ = c;
  }
  return(string);
}

/* ---------------------------------------------------------------- */

char* cut_spaces(char* line)
{
  char *p1,*p2;
 
  p1 = line;
  p2 = line + strlen(line) -1;
 
  if (p2 < p1) return(line);             /* no chars */

  if (p1 == p2) {                        /* 1 char */
    if (isspace(*p1)) *p1 = '\0'; 
    return(line);
  }

  while (isspace(*p1) && (p1 <  p2))               p1++;
  while (isspace(*p2) && (p2 >= p1)) { *p2 = '\0'; p2--; }
 
  if (p1 > line) { char *p0;
    p0 = line;
    while (*p1) {
      *p0 = *p1; p0++; p1++;
    }
    *p0 = '\0';
  }
    
  return(line);
}

/* ---------------------------------------------------------------- */

char* extract_filename(char* filename,const char* path)
{
  char *h;
 
  h = strrchr(path,'/');               /* WARNING: U**X specific path */
  if (h == NULL) strcpy(filename,path);
  else           strcpy(filename,h+1);
 
  return filename;
}

/* ---------------------------------------------------------------- */

char* extract_rootname(char* rootname,const char* path)
{
  char *h;

  (void)extract_filename(rootname,path);
  h = strrchr(rootname,'.'); if (h) *h = '\0';

  return rootname;
}

/* ---------------------------------------------------------------- */

char* extract_pathname(char* pathname,const char* path)
{
  char *h;
 
  strcpy(pathname,path);
  h = strrchr(pathname,'/');           /* WARNING: U**X specific path */
  if (h) *h = '\0';
  else   strcpy(pathname,"");
 
  return pathname;
}
 
/* ---------------------------------------------------------------- */
 
char* alpha_str3(char *string,double alpha,int dp,char sep)
{
  double hr,mi,se;

  hr = floor(alpha);
  mi = floor((alpha-hr)*60.0);
  se = (alpha - hr - mi/60.0)*3600.0;
  if (se >= 59.95) { se -= 59.95; mi += 1.0; }
  if (mi >= 60.00) { mi -= 60.00; hr += 1.0; }
  if (hr >= 24.00) { hr -= 24.00; }
  sprintf(string," %02.0f%c%02.0f%c%0*.*f",
          hr,sep,mi,sep,(dp) ? dp+3 : dp+2,dp,se);

  return string;
}

char* alpha_str2(char *string,double alpha,int dp)
{
 
  return(alpha_str3(string,alpha,dp,':'));
}
 
char* alpha_str(char *string,double alpha)
{
  return(alpha_str2(string,alpha,1));
}

/* ---------------------------------------------------------------- */
 
char* delta_str3(char *string,double delta,int dp,char sep)
{
  double dg,pd,mi,se;
  char   sign='+';

  pd = fabs(delta);
  dg = floor(pd);                      /* delta in degrees -> string */
  mi = floor((pd - dg)*60.0);
  se = (pd - dg - mi/60.0)*3600.0;
  if (delta < 0.0) sign = '-';
  if (se >= 59.95) { se -= 59.95; mi += 1.0; }
  if (mi >= 60.00) { mi -= 60.00; dg += 1.0; }
  sprintf(string,"%c%02.0f%c%02.0f%c%0*.*f",
          sign,dg,sep,mi,sep,(dp) ? dp+3 : dp+2,dp,se);

  return string;
}

char* delta_str2(char *string,double delta,int dp)
{
  return(delta_str3(string,delta,dp,':'));
}

char* delta_str(char *string,double delta)
{
  return(delta_str2(string,delta,1));
}

/* ---------------------------------------------------------------- */
 
char* time_str(char *string,double st)
{
  int hr,mi,se;
 
  hr  = (int)floor(st/3600.0);
  st -= hr*3600.0;
  mi  = (int)floor(st/60.0);
  st -= mi*60.0;
  se  = (int)floor(st);
  (void)sprintf(string,"%02d:%02d:%02d",hr,mi,se);

  return string;
}

/* ---------------------------------------------------------------- */

double d_alpha(const char* text)
{
  double alh=0.0,alm=0.0,als=0.0,a;
 
  if (!strchr(text,(int)':'))
    sscanf(text,"%lf %lf %lf",&alh,&alm,&als);
  else
    sscanf(text,"%lf:%lf:%lf",&alh,&alm,&als);
 
  a = alh + alm/60.0 + als/3600.0;
  while (a >= 24.0) a -= 24.0;
  while (a < 0.0)  a += 24.0;
 
  return (a);
}

/* ---------------------------------------------------------------- */

double d_delta(const char* text)
{
  double deg=0.0,dem=0.0,des=0.0,d;
 
  if (!strchr(text,(int)':'))
    sscanf(text,"%lf %lf %lf",&deg,&dem,&des);
  else
    sscanf(text,"%lf:%lf:%lf",&deg,&dem,&des);
 
  d = fabs(deg)+fabs(dem)/60.0+fabs(des)/3600.0;
  while (d > 90.0) d -= 90.0;
 
  if ((deg<0.0) || (dem<0.0) || (des<0.0) || strchr(text,(int)'-'))
    d = -d;
 
  return d;
}

/* ---------------------------------------------------------------- */

static char *dtime(char* buf)
{                                      /* create data/time-string */ 
  time_t t1;

  (void)time(&t1); 
 
  return(ctime_r(&t1,buf));
} 

/* ---------------------------------------------------------------- */
 
void adebug(const char *who,const char *text)
{
  char dfile[256];
  FILE *fp;
 
#if (DEBUG > 0)
  fprintf(stderr,"\t(%s) %s\n",who,text);
#endif

  (void)sprintf(dfile,"%s/debug.log",genv2("HOME","."));

  if ((fp=fopen(dfile,"a")) != NULL) {
    fprintf(fp,"\t(%s) %s\n",who,text);
    (void)fclose(fp);
  }
}
 
/* ---------------------------------------------------------------- */

void tdebug(const char *who,const char *text)
{
  char dfile[256],buf[128];
  FILE *fp;

  (void)dtime(buf);
#if (DEBUG > 0)
  fprintf(stderr,"%s\t(%s) %s\n",buf,who,text);
#endif
  (void)sprintf(dfile,"%s/debug.log",genv2("HOME","."));

  if ((fp=fopen(dfile,"a")) != NULL) {
    fprintf(fp,"%s\t(%s) %s\n",buf,who,text);
    (void)fclose(fp);
  }
}

/* ---------------------------------------------------------------- */

time_t cor_time(int timeoff)
{
  return(time((time_t*)NULL) - (time_t)timeoff);
}

/* ---------------------------------------------------------------- */

double walltime(int timeoff)
{
  struct timeval tp;

  gettimeofday(&tp,NULL);

  return((double)(tp.tv_sec) + (double)(tp.tv_usec)/1.0e6 - timeoff);
}

/* ---------------------------------------------------------------- */

void msleep(int msec)
{
  struct timespec delay,rem;

  if (msec <= 0) return;

  delay.tv_sec  = msec/1000;
  delay.tv_nsec = (msec % 1000) * 1000000;

  while (nanosleep(&delay,&rem) < 0) {
    delay.tv_sec = rem.tv_sec;
    delay.tv_nsec = rem.tv_nsec;
  }
}
 
/* ---------------------------------------------------------------- */
 
void q_sort(int *zahl, int l, int r)
{
  int  x,i,j,h;
 
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
    q_sort(zahl,l,j);
    q_sort(zahl,i,r);
  }
}
 
/* ---------------------------------------------------------------- */
 
void d_sort(double *zahl,int l,int r)  /* quick-sort */
{
  int    i,j;
  double x,h;
 
  i = l;
  j = r;
 
  if (j > i) {
    x = zahl[(i+j)/2];                 /* center element */
    do {
      while (zahl[i] < x) i++;
      while (zahl[j] > x) j--;
      if (i <= j) {                    /* swap elements */
        h         = zahl[i];
        zahl[i++] = zahl[j];
        zahl[j--] = h;
      }
    } while (!(i>j));
    d_sort(zahl,l,j);
    d_sort(zahl,i,r);
  }
}

/* ---------------------------------------------------------------- */

double get_sidereal(time_t ut,double tl)
{
  double st;
  const double STG0=24055.77;          /* 2015-09-17 */

  st = (double)((int)(STG0+366.2422/365.2422*ut)%(int)86400)*24.0/86400.0;
  st = modulo(st+tl/15.0,0.0,24.0); 

  return(st*3600.0);                   /* seconds */
}

/* ---------------------------------------------------------------- */

#ifndef __isleap
#define __isleap(y)  (((y) % 4 == 0) && (((y) % 100 != 0) || ((y) % 400 == 0)))
#endif

double get_epoch(time_t when)          /* faster 2016-05-05 */
{
  int    yr;
  double doy,hr,dpy,ep;
  struct tm res;

  time_t ut = (when) ? when : cor_time(0);
  gmtime_r(&ut,&res);                  /* get tm-structure (UT) */

  yr  = 1900 + (int)res.tm_year;
#ifdef LINUX                           /* get days per year */
  dpy = (double)dysize(yr);
#else
  dpy = (__isleap(yr)) ? 366.0 : 365.0;  /* 2016-04-05 */
#endif
  doy = (double)res.tm_yday;
  hr  = (double)res.tm_hour;

  if (when) {                          /* 2015-10-28 */
    double mi = (double)res.tm_min;
    double sc = (double)res.tm_sec;
    ep = (double)yr + (doy + hr/24.0 + mi/1440.0 + sc/86400.0)/dpy;
  } else {
    ep = (double)yr + (doy + hr/24.0)/dpy;
  }

  return ep;
}

/* ---------------------------------------------------------------- */

int get_uts(time_t ut)
{
  struct tm *gtime,res;
 
  gtime = gmtime_r(&ut,&res);          /* GMT */
 
  return((int)gtime->tm_hour*3600+(int)gtime->tm_min*60+(int)gtime->tm_sec);
}

/* ---------------------------------------------------------------- */

char* get_night(char* night,time_t when)
{
  struct tm *ltime,res;
 
  time_t ut = (when) ? when : cor_time(0);
  ut   -= 12*3600;
  ltime = localtime_r(&ut,&res);       /* get local time */
 
  strftime(night,16,"%d%b%Y",ltime);
 
  return night;
}
 
/* ---------------------------------------------------------------- */

double get_airmass(double lat, double dec, double ha) /* credits to GP */
{
  double cos_zd,x,airmass;
  const double SCALE=750.0;            /* atmospheric scale height  */
 
  cos_zd = sin (lat/RADS) * sin (dec/RADS) +
           cos (lat/RADS) * cos (dec/RADS) * cos (15.0*ha/RADS);
  if ((cos_zd > 1.0) || (cos_zd < 0.0)) return (9.999);

  x  = SCALE * cos_zd;
  airmass = sqrt (x*x + 2.0*SCALE + 1.0) - x;
  if (airmass > 9.999) airmass = 9.999;
 
  return(airmass);
}

/* ---------------------------------------------------------------- */

double get_ha(time_t ut,double tellong,double alpha) /* hour angle */
{
  double ha;
 
  ha = get_sidereal(ut,tellong)/3600.0 - alpha;
 
  return(ha);
}

/* ---------------------------------------------------------------- */

double get_pa(double ha,double dec,double lat) /* credits to KDC */
{
  double st,ct,sp,cp,sd,cd,cz,pa;

  st = sin(ha*15.0/RADS);
  ct = cos(ha*15.0/RADS);
  sd = sin(dec/RADS);
  cd = cos(dec/RADS);
  sp = sin(lat/RADS);
  cp = cos(lat/RADS);
  cz = ct*cp*cd + sp*sd;
  pa = RADS*atan2(st*cp*cd,sp-cz*sd);
#if (DEBUG > 3)
  if (pa != RADS*atan2(st*cp,(sp-cz*sd)/cd)) 
    fprintf(stderr,"%s: get_pa(): %f,%f\n",__FILE__,
            pa,RADS*atan2(st*cp,(sp-cz*sd)/cd));
  if (pa != RADS*atan2(cp*st,(sp*cd - cp*sd*ct)))
    fprintf(stderr,"%s: get_pa(): %f,%f\n",__FILE__,
            pa,RADS*atan2(cp*st,(sp*cd - cp*sd*ct)));
#endif

  return(pa);
}

/* ---------------------------------------------------------------------- */

double get_zd(double ha,double dec,double lat) /* credits to KDC */
{
  double st,ct,sp,cp,sd,cd,x,y,z,zd;

  st = sin(ha*15.0/RADS);
  ct = cos(ha*15.0/RADS);
  sd = sin(dec/RADS);
  cd = cos(dec/RADS);
  sp = sin(lat/RADS);
  cp = cos(lat/RADS);
  x  = cp*st;
  y  = sp*cd - cp*sd*ct;
  z  = sp*sd + cp*cd*ct;
  zd = RADS*atan2(sqrt(x*x+y*y),z);
#if (DEBUG > 3)
  { double cos_zd;
    cos_zd = sin (lat/RADS) * sin (dec/RADS) +
             cos (lat/RADS) * cos (dec/RADS) * cos (15.*ha/RADS);
    if (zd != RADS*acos(cos_zd))
      fprintf(stderr,"%s: %s(): %f,%f\n",__FILE__,PREFUN,zd,RADS*acos(cos_zd));
  }
#endif

  return(zd);
}

/* ---------------------------------------------------------------- */

double get_telaz(time_t ut,double tellong,double al)
{
  double ha;

  ha = get_ha(ut,tellong,al);

  return(ha*15.0);
}

double get_telel(time_t ut,double tellong,double tellat,double al,double de)
{
  double ha,cos_zd;

  ha = get_ha(ut,tellong,al);
  cos_zd = sin(tellat/RADS) * sin(de/RADS) +
           cos(tellat/RADS) * cos(de/RADS) * cos(15.0*ha/RADS);
  if ((cos_zd > 1.0) || (cos_zd < 0.0)) return (0.0);

  return(90.0-RADS*acos(cos_zd));
}

/* ---------------------------------------------------------------- */

/* from: http://www/stjarnhimlen.se/comp/tutorial.html */

void get_altaz(double st,double lat,double ra,double dec,double* alt,double* az)
{
  double h,x,y,z,a,b,c;

  h = 15.0*(st/3600.0-ra);           // hour angle [degrees]
  x = cos(h/RADS) * cos(dec/RADS);   // rect. coords
  y = sin(h/RADS) * cos(dec/RADS);
  z =               sin(dec/RADS);
  a = x*sin(lat/RADS) - z*cos(lat/RADS); // rotate
  b = y;
  c = x*cos(lat/RADS) + z*sin(lat/RADS);

  *az = RADS*atan2(b,a) + 180.0;
  *alt = RADS*asin(c);               // = atan2(c,sqrt(a*a+b*b)) 
}

/* ---------------------------------------------------------------- */

/* from: http://www/stjarnhimlen.se/comp/tutorial.html */

void get_radec(double st,double lat,double alt,double az,double* ra,double* dec)
{
  double s,c,h;

  while (az  <    0.0) az += 360.0;     // CCB: ormalize
  while (az  >= 360.0) az -= 360.0;
  if    (alt <    0.1) alt =   0.1;
  if    (alt >   89.9) alt =  89.9;

  s = sin(alt/RADS) * sin(lat/RADS);   // calculate 'dec'
  c = cos(alt/RADS) * cos(lat/RADS) * cos(az/RADS);
  *dec = RADS*asin(s+c);

  h = RADS*asin(-sin(az/RADS)*cos(alt/RADS)/cos(*dec/RADS));
  *ra = st/3600.0 - h/15.0;            // calculate 'RA'
  while (*ra <   0.0) *ra += 24.0;
  while (*ra >= 24.0) *ra -= 24.0;
}

/* ---------------------------------------------------------------- */

double get_rt(double ha,double dec,double lat)    /* credits to KDC */
{
  double rt;

  rt = -get_pa(ha,dec,lat) - get_zd(ha,dec,lat);  /* west platform */
                                                  /* pa-zd (east) */
  return(rt);
}

/* ---------------------------------------------------------------- */

double get_rtrate(double ha,double dec,double lat) /* credits to KDC */
{
  double rt1,rt2,drt;
  const double DHA=5.0/60.0;

  rt1 = get_rt(ha-DHA,dec,lat);
  rt2 = get_rt(ha+DHA,dec,lat);

  drt = rt2-rt1;
  if (drt < -180.0) drt = -360.0 - drt;
  if (drt >  180.0) drt =  360.0 - drt;

  return(drt/(2.0*DHA));               /* change per hour */
}

/* ---------------------------------------------------------------- */

double get_parate(double ha,double dec,double lat) /* credits to KDC */
{
  double pa1,pa2,dpa;
  const double DHA=5.0/60.0;

  pa1 = get_pa(ha-DHA,dec,lat);
  pa2 = get_pa(ha+DHA,dec,lat);

  dpa = pa2 - pa1; 
  if (dpa < -180.0) dpa = -360.0 - dpa;
  if (dpa >  180.0) dpa =  360.0 - dpa;

  return(dpa/(2.0*DHA));               /* change per hour */
}

/* ---------------------------------------------------------------- */

void play_sound(const char* sound)     /* play sound or speak */
{
  char cmd[1024]="",path[512];
 
  assert(sound);
#if (DEBUG > 1)
  fprintf(stderr,"%s: %s(%s)\n",__FILE__,PREFUN,sound);
#endif
  if (*sound == '\0') return;          /* no sound */

  if (strchr(sound,'\'') || strchr(sound,'\"')) { /* speak text */
#if   defined(LINUX)
    sprintf(path,"/tmp/festival_%s",__FILE__);
    FILE *fp = fopen(path,"w");
    if (fp) {
      fprintf(fp,"%s",sound);
      fclose(fp);
    }
    sprintf(cmd,"festival --tts %s &",path);  /* 2014-06-24 */
#elif defined(MACOSX)
    sprintf(cmd,"say %s &",sound);
#endif
  } else {                             /* play sound */
    if (*sound == '/') strcpy(path,sound);
    else sprintf(path,"%s/%s",genv3("MYSOUNDS","HOME","."),sound);
#if   defined(LINUX)
    sprintf(cmd,"/usr/bin/aplay %s >& /dev/null &",path);
#elif defined(MACOSX)
    sprintf(cmd,"/usr/bin/afplay %s &",path);
#endif
  }
  system(cmd);
}

/* ---------------------------------------------------------------- */

void precess(double ain,double din,double ein, double *aout, double *dout,
             double eout)              /* credits to GP */
{
  double al,del,tau0,tau;
  double zeta,theta,z,ac,bc,cc,zx,a1,d1;
  const double c1=2.062648e5;
 
  if (ein == eout) {                   /* input = output equinox ? */
    *aout = ain;
    *dout = din;
    return;
  }
  al  = ain*(M_PI/12.0);               /*  coordinates in radians  */
  del = din*(M_PI/180.0);
 
  tau0   = (ein-1900.0)/100.0;         /* Newcomb: 10" vs SLA */
  tau    = (eout-1900.0)/100.0 - tau0;
  zeta   = (2304.25+1.396*tau0)*tau+.302*tau*tau+0.018*tau*tau*tau;
  z      = zeta+0.791*tau*tau+0.001*tau*tau*tau;
  theta  = (2004.682-0.853*tau0)*tau-0.426*tau*tau-0.042*tau*tau*tau;
  zeta  /= c1;
  z     /= c1;
  theta /= c1;
 
  ac = cos(del)*sin(al+zeta);
  bc = cos(theta)*cos(del)*cos(al+zeta)-sin(theta)*sin(del);
  cc = sin(theta)*cos(del)*cos(al+zeta)+cos(theta)*sin(del);
  d1 = asin(cc);
  zx = sqrt(1.0-cc*cc);
  double q = bc/zx;
  if      (q >  1.0) q =  1.0;
  else if (q < -1.0) q = -1.0;
  a1 = acos(q);
  if (ac/zx < 0.0) a1=2.0*M_PI-a1;
  a1 += z;

  *aout = a1*(12.0/M_PI);              /* -> hours */
  if (*aout <   0.0) *aout += 24.0;
  if (*aout >= 24.0) *aout -= 24.0;
  *dout  = d1*(180.0/M_PI);            /* -> degrees */
}

/* ---------------------------------------------------------------- */

char *genv2(const char *first,char *def_pointer)
{
  if (getenv(first) == NULL) return(def_pointer);
  return(getenv(first));
}
 
/* ---------------------------------------------------------------- */

char *genv3(const char *first,const char *second,char *def_pointer)
{
  if (getenv(first) == NULL) {
    if (getenv(second) == NULL) return(def_pointer);
    else                        return(getenv(second));
  }
  return(getenv(first));
}

/* ---------------------------------------------------------------- */

int imin(int a,int b)
{
  if (a < b) return(a);
  return(b);
}

/* ---------------------------------------------------------------- */

int imax(int a,int b)
{
  if (a > b) return(a);
  return(b);
}

/* ---------------------------------------------------------------- */
 
char* get_local_datestr(char* string,time_t ut)
{
  struct tm *now,res;
 
  if (ut <= 0) ut = time(NULL);
  now = localtime_r(&ut,&res);
  strftime(string,32,"%b %d %H:%M:%S",now);

  return string;
}
 
/* ---------------------------------------------------------------- */

char* get_ut_datestr(char* string,time_t ut)
{ 
  struct tm *now,res;
  
  if (ut <= 0) ut = time(NULL);
  now = gmtime_r(&ut,&res);
  strftime(string,32,"%b %d %H:%M:%S",now);
  
  return string;
}

/* ---------------------------------------------------------------- */
 
char* get_ut_timestr(char* string,time_t ut)
{
  struct tm *now,res;
 
  if (ut <= 0) ut = time(NULL);
  now = gmtime_r(&ut,&res);
  strftime(string,16,"%H:%M:%S",now);

  return string;
}

/* ---------------------------------------------------------------- */
 
char* get_local_timestr(char* string,time_t lt)
{
  struct tm *now,res;

  if (lt <= 0) lt = time(NULL);
  now = localtime_r(&lt,&res);
  strftime(string,16,"%H:%M:%S",now);

  return string;
}
 
/* ---------------------------------------------------------------- */

double mag2flux(double mag,double cfzm)
{
  return(cfzm/pow(10.0,0.4*mag));
}

/* ---------------------------------------------------------------- */

double flux2mag(double flux,double cfzm)
{
 return(2.5*log10(cfzm/flux));
}

/* ---------------------------------------------------------------- */

double flux2peak(double flux,double fwhm,double pixscale)
{
  return(flux*(pixscale*pixscale)/(fwhm*fwhm*1.133));
}

/* ---------------------------------------------------------------- */

double peak2flux(double peak,double fwhm)
{
  return(1.133*peak*fwhm*fwhm);
}

/* ---------------------------------------------------------------- */
 
float checkfs(const char* path)
{
#ifdef MACOSX
  struct statfs buf;
#else
  struct statvfs buf;
#endif

#ifdef MACOSX
  if (statfs(path,&buf) != 0) return(0.0f);
#else
  if (statvfs(path,&buf) != 0) return(0.0f);
#endif
#if (DEBUG > 2)
  fprintf(stderr,"f_blocks=%ld (f_frsize=%ld,f_bsize=%ld)\n",
          (long)buf.f_blocks,(long)buf.f_frsize,(long)buf.f_bsize);
  fprintf(stderr,"f_bfree=%ld, f_bavail=%ld\n",
          (long)buf.f_bfree,(long)buf.f_bavail);
#endif 

  return((float)buf.f_bavail/(float)buf.f_blocks); /* ratio of free blocks */
}

/* ---------------------------------------------------------------- */
 
float diff_tp(struct timeval* tp1,struct timeval* tp2)
{
  float dt;
 
#if (DEBUG > 2)
  fprintf(stderr,"diff_tp(%ld,%ld - %ld,%ld)",
          (long)tp2->tv_sec,(long)tp2->tv_usec,
          (long)tp1->tv_sec,(long)tp1->tv_usec);
#endif
  dt = (float)(tp2->tv_sec - tp1->tv_sec) +
       (float)(tp2->tv_usec - tp1->tv_usec)/1000000.0f;
#if (DEBUG > 2)
  fprintf(stderr," = %.3f\n",dt);
#endif
  return(dt);
}

/* ---------------------------------------------------------------- */
/* .ini-file operations */
/* ---------------------------------------------------------------- */

char* get_string(const char* file,const char* key,char* value,char* defstring)
{
  char  buffer[1024],val[128],*h,*r=NULL;
  FILE  *fp;
 
  if (defstring) { strcpy(value,defstring); r=value; }
  if (file == NULL) return(r);

  fp = fopen(file,"r"); 
  if (fp != NULL) {
    while (fgets(buffer,sizeof(buffer)-1,fp)) {  /* search for 'key' */
      (void)cut_spaces(buffer);        /* remove <CR> */
      h = strchr(buffer,';');          /* comment */
      if (h) *h ='\0';
      h = strchr(buffer,'=');          /* delimiter */
      if (!h) continue;
      strcpy(val,h+1);
      *h = '\0';
      cut_spaces(buffer);              /* leading and trailing */
      if (!strcasecmp(buffer,key)) {   /* key found */
        cut_spaces(val);
        strcpy(value,val); r=value;
        break;
      }
    }
    fclose(fp);
  } 
  return(r);
}
 
/* ---------------------------------------------------------------- */
 
#define MAX_LINES     512

int put_string(const char* file,const char* key,char* string)
{
  int  i,fd,n=0,found=0;
  char buffer[1024],comment[512],*lines[MAX_LINES],*h;
  FILE *fp;
 
  fd = open(file,O_RDWR | O_CREAT,00644);  /* open file read/write */
  if (fd == -1) {
    fprintf(stderr,"%s: put_string(%s,%s,%s): cannot open file\n",__FILE__,
            file,key,string);
    return(-1);
  }
  lockf(fd,F_LOCK,0);                  /* lock file */
  fchmod(fd,00644);                    /* -rw-r--r-- */
 
  fp = fdopen(fd,"r+");                /* get STREAM pointer */
  if (fp == NULL) {
    lockf(fd,F_ULOCK,0); 
    close(fd);
    fprintf(stderr,"%s: %s(%s,%s,%s): cannot get FILE-pointer (%d)\n",
           __FILE__,PREFUN,file,key,string,fd);
    return(-1);
  }

  while (fgets(buffer,sizeof(buffer)-1,fp)) {
    cut_spaces(buffer); if (*buffer == '\0') continue; /* ignore empty line */
    lines[n] = (char*)malloc((strlen(buffer)+1)*sizeof(char));
    strcpy(lines[n],buffer);           /* copy original line */
    h = strchr(buffer,';');            /* comment */
    if (h) { strcpy(comment,h); *h = '\0'; }  /* save for later */
    else     *comment = '\0';
    h = strchr(buffer,'='); if (!h) continue; /* delimiter */
    *h = '\0';
    cut_spaces(buffer);                /* leading and trailing */
    if (!strcasecmp(buffer,key)) {     /* found key */
      sprintf(buffer,"%s=%s",key,string);   /* replace line */
      if (*comment) { strcat(buffer," \t"); strcat(buffer,comment); }
      free((void*)lines[n]);
      lines[n] = (char*)malloc((strlen(buffer)+1)*sizeof(char));
      strcpy(lines[n],buffer);
      found = 1;
    }
    n++; if (n == MAX_LINES) break;
  }
  if ((n == MAX_LINES) && !found) {    /* too many lines */
    fclose(fp);              
    fprintf(stderr,"%s: %s(%s,%s,%s): OVERFLOW\n",__FILE__,PREFUN,
            file,key,string);
    return(-1);
  }
  if (!found) {                        /* append new line */
    sprintf(buffer,"%s=%s",key,string); 
    lines[n] = (char*)malloc((strlen(buffer)+1)*sizeof(char));
    strcpy(lines[n],buffer);
    n++;
  }
  rewind(fp);                          /* rewind and */
  for (i=0; i<n; i++) {                /* overwrite file */
    (void)fprintf(fp,"%s\n",lines[i]);
    free((void*)lines[i]);
  }
  ftruncate(fd,ftell(fp));             /* truncate to current length */
  fclose(fp);
 
  return(0);
}

#undef MAX_LINES

/* ---------------------------------------------------------------- */
 
long get_long(const char* file,const char* key,long defvalue)
{
  char  buf[128],buf2[256];
  int   l;
  u_int value;
 
  if (get_string(file,key,buf,NULL) == NULL) return(defvalue);
  if (!strncmp(buf,"0x",2)) {          /* hex value */
    (void)sscanf(buf,"%x",&value);
    return (long)value;
  }
  if ((l=strlen(buf)) > 1) {
    if ((buf[l-1] == 'h') || (buf[l-1] == 'H')) {  /* hex value */
      buf[l-1] = '\0';
      sprintf(buf2,"0x%s",buf);
      (void)sscanf(buf2,"%x",&value);
#if (DEBUG > 1)
      fprintf(stderr,"file=%s, value=%u\n",file,value);
#endif
      return (long)value;
    }
  }
  return atol(buf);                    /* decimal value */
}
 
/* ---------------------------------------------------------------- */

int put_long(const char* file,const char* key,long value)
{
  char buf[128];

  sprintf(buf,"%ld",value);
  return(put_string(file,key,buf));
}

/* ---------------------------------------------------------------- */
 
double get_double(const char* file,const char* key,double defvalue)
{
  char buf[128];
 
  if (get_string(file,key,buf,NULL) == NULL) return(defvalue);
  return(atof(buf));                   /* decimal value */
}

/* ---------------------------------------------------------------- */

int put_double(const char* file,const char* key,double value,int dp)
{
  char   buf[128];
  double a=fabs(value);

  if ((a < 0.0001) || (a >= 1.e6)) sprintf(buf,"%.*e",dp,value);
  else                             sprintf(buf,"%.*f",dp,value);
  return(put_string(file,key,buf));
}

/* ---------------------------------------------------------------- */
 
int is_bigendian(void)
{
  union {
    long l;
    char c[sizeof(long)];
  } u;
  u.l = 1;
  return(u.c[sizeof(long)-1] == 1);
}

/* ---------------------------------------------------------------- */

char* ini_path(char* path,const char* env,const char* file)
{
  sprintf(path,"%s/%s",genv3(env,"HOME","."),file);

  return path;
}

/* ---------------------------------------------------------------- */

char* set_path(char* path,const char* file)
{
  sprintf(path,"%s/.%s",genv2("HOME","."),file); /* hidden file */

  return path;
}

/* ---------------------------------------------------------------- */

char* log_path(char* path,const char* env,const char* file)
{
  char buf[32];
  struct tm res;

  if (strstr(file,".log")) {           /* full name supplied ? */
    sprintf(path,"%s/%s",genv2(env,"/tmp"),file);
  } else {                             /* insert week-of-year */
    time_t ut = cor_time(0);
    strftime(buf,sizeof(buf),"%Y_%W",localtime_r(&ut,&res));
    sprintf(path,"%s/%s%s.log",genv2(env,"/tmp"),file,buf);
  }

  return path;
}

/* ---------------------------------------------------------------- */

char* bin_path(char* path,const char* env,const char* file)
{
  char *p;

  p = getenv(env); 
  if (p) {
    sprintf(path,"%s/%s",p,file);
  } else {
    p = getenv("HOME");
    if (p) sprintf(path,"%s/bin/%s",p,file);
    else   strcpy(path,file);
  }
  return(path);
}

/* ---------------------------------------------------------------- */

void lnk_logfile(char* path,const char* file)
{
  char linkname[256];

  set_path(linkname,file);             /* $HOME */
  unlink(linkname);
  symlink(path,linkname);
}

/* ---------------------------------------------------------------- */

#if 0 // unused
void mail_file(const char* file,const char* sub,const char* addr,int zip_uue)
{
#ifdef SIM_ONLY
  fprintf(stderr,"%s: ignore %s(%s,%d)\n",__FILE__,PREFUN,file,zip_uue);
  if (sub) fprintf(stderr,"\tsubject='%s'\n",sub);
#else
  char mailfile[512],subject[128],buffer[1024];
 
  if (sub) strcpy(subject,sub);        /* take provided subject */
  else (void)extract_filename(subject,file);  /* use filename */

  if (zip_uue) { char uudfile[256],tmpfile[128],*h; static int unique=0;
    sprintf(tmpfile,"/tmp/imacs%d%d",getpid(),++unique);
    sprintf(buffer,"cp %s %s",file,tmpfile);  /* create temp.file */
#if (DEBUG > 1)
    fprintf(stderr,"%s(): %s\n",PREFUN,buffer);
#endif
    system(buffer);
    sprintf(buffer,"gzip -f %s",tmpfile);     /* compress */
#if (DEBUG > 1)
    fprintf(stderr,"%s(): %s\n",PREFUN,buffer);
#endif
    system(buffer);
    sprintf(uudfile,"%s.gz",subject);         /* uuencode */
    sprintf(mailfile,"%s.uue",tmpfile);
    sprintf(buffer,"uuencode %s.gz %s > %s",tmpfile,uudfile,mailfile);
#if (DEBUG > 1)
    fprintf(stderr,"%s(): %s\n",PREFUN,buffer);
#endif
    system(buffer);
    sprintf(buffer,"rm -f %s.gz",tmpfile);    /* remove temp.file */
#if (DEBUG > 1)
    fprintf(stderr,"%s(): %s\n",PREFUN,buffer);
#endif
    system(buffer);
    h = strchr(subject,'.');
    if (h) { *h = '\0'; strcat(subject,".uue"); }
  } else {                             /* plain text e-mail */
    strcpy(mailfile,file);
  }

  sprintf(buffer,"mail -s '%s' %s < %s",subject,addr,mailfile);
#if (DEBUG > 1)
  fprintf(stderr,"%s(): %s\n",PREFUN,buffer);
#endif
  system(buffer);

  if (zip_uue) {                       /* remove temp.file */
    sprintf(buffer,"rm -f %s",mailfile);
#if (DEBUG > 1)
    fprintf(stderr,"%s(): %s\n",PREFUN,buffer);
#endif
    system(buffer);
  }
#endif /* SIM_ONLY */
}
#endif

/* ---------------------------------------------------------------- */

double my_round(double x,int d)
{
  int  i;

  for (i=0; i<d; i++) x *= 10.0;
  x = floor(x+0.5);
  for (i=0; i<d; i++) x /= 10.0;

  return(x);
}

/* ---------------------------------------------------------------- */

double modulo(double x,double min,double max)
{
  if (max < min) return x;

  while (x >= max) x -= (max-min);
  while (x <  min) x += (max-min);

  return x;
}

/* ---------------------------------------------------------------- */

double modneg(double x,double max)
{
  while (x >   max/2.0) x -= max;
  while (x <= -max/2.0) x += max;

  return x;
}

/* ---------------------------------------------------------------- */

double modtop(double x,double max)
{
  if (x > max) {
    x = max - modulo(x,0.0,max);
  } else
  if (x < -max) {
    x = -max + modulo(fabs(x),0,max);
  }
  return x;
}

/* ---------------------------------------------------------------- */

int lockfile_open(const char* file)
{
  int  fd;
  char buf[32];

  fd = open(file,O_RDWR | O_CREAT,00644);  /* open lockfile */
  if (fd == -1) return(-1);            /* failed */
  if (lockf(fd,F_TLOCK,0) < 0) {       /* lock failed */
    close(fd);
    return(-2);
  }
  sprintf(buf,"%d\n",(int)getpid());   /* write PID */
  write(fd,(void*)buf,strlen(buf));
  fchmod(fd,00666);                    /* read/write all */

  return fd;
}

/* ---------------------------------------------------------------- */

void lockfile_close(int fd)
{
  if (fd != -1) {
    (void)lockf(fd,F_ULOCK,0);         /* unlock file */
    close(fd);
  }
}

/* ---------------------------------------------------------------- */

double stringVal(const char* buf,int i)
{
  char *s,*s0,*p=NULL;

  s = s0 = (char*)malloc(1+strlen(buf));
  strcpy(s0,buf);
  while (i > 0) {
    p = strsep(&s," "); if (!p) break;
    if (*p) i--;
  }
  double r = (p) ? atof(p) : 0.0;
  free((void*)s0);

  return r;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

