/* ----------------------------------------------------------------
 *
 * fits.h
 *
 * Project: ZwoGuider  (OCIW, Pasadena, CA)
 *
 * Christoph C. Birk (birk@carnegiescience.edu)
 *
 * ---------------------------------------------------------------- */

#ifndef INCLUDE_FITS_H
#define INCLUDE_FITS_H 

/* DEFINEs -------------------------------------------------------- */

#ifdef IS_FITS_C
#define EXT_FITS
#else
#define EXT_FITS extern
#endif

#define FITS_VERSION  "0.328"

#define FITSRECORD    80                      /* FITS record length */
#define FITSBLOCK     (36*FITSRECORD)         /* FITS block length */
#define FITSNBLKS     3                       /* # of FITS blocks v1.0.6 */
#define FITSLINES     (FITSNBLKS*36)          /* # of header-lines */
#define FITSLEN       (FITSLINES*FITSRECORD)  /* length of FITS-header */

#define MAX_FILENUM   9999

/* INCLUDEs ------------------------------------------------------- */

#include <time.h>                      /* time_t */

#ifdef MACOSX
#include <sys/types.h>
#endif

/* TYPEDEFs ------------------------------------------------------- */
 
typedef struct fitskey_tag {
  int   id;
  char* keyword;
} FITSKey;

/* guider state, filled by guider_state() -- single source of truth for  */
/* both the 'status' command and the FITS header v1.0.6                  */

typedef struct guider_state_tag {
  int    init,loop,guiding;            /* latched under Guider.mutex */
  int    mode,fmode,mmode;
  int    av,offset,send,boxsz;
  float  setp,cooler,camfps,fps;
  float  fwhm,flux,peak,back;
  float  dx,dy,az,el;                  /* dx,dy relative to the guide box */
  float  boxx,boxy,pa,sens;
} GuiderState;

typedef struct system_parameters_tag { // IDEA combine with Guider struct
  /* camera */
  char   instrument[64],serial[32];
  int    dimx,dimy;
  float  pixscale,psize;
  float  pa;                           /* instrument vs. North */
  float  ca;                           /* v0316 */
  int    shmode;                       /* v0316 */
  int    camera,frame,rotn;            /* v0317 */
  /* observing */
  double start_int,stop_int;
  int    run;
  u_int  seqNumber;
  float  exptime;
  int    binning;
  char   prefix[16];
  float  temp_ccd;
  char   origin[64];
  int    gain;
  GuiderState gd;                      /* guider state v1.0.6 */
  unsigned long long ts_ns;            /* frame timestamp [ns], 0=unknown */
  /* telescope stuff v0328*/
  double alpha,delta,equinox;
  float  telfocus,zd,airmass;
  char   st_str[32],ha_str[32],tg_str[32];
  float  rotangle,rotatore;
  float  guiderx1,guidery1,guiderx2,guidery2;
  /* FITS */
  int    bzero;
  char   datapath[512];
  char   filename[64];
  char   version[64];
} FITSpars;

/* function prototype(s) ------------------------------------------ */ 

EXT_FITS int      fits_save    (u_short*,FITSpars*);
EXT_FITS int      fits_send    (u_short*,FITSpars*,int);

/* ---------------------------------------------------------------- */

enum fits_headers {
  F_SIMPLE,F_BITPIX,F_NAXIS,F_NAXIS1,F_NAXIS2,   /* mandatory */
 
  F_BSCALE,F_BZERO,F_BUNIT,                      /* data */
 
  F_ORIGIN,
  F_INSTRUMENT,
  F_SCALE,          
  F_PSIZE,
 
  F_DATE_OBS,
  F_UT_DATE,F_UT_TIME,F_UT_END,F_LC_TIME,        /* time */
  F_EPOCH,

  F_ALPHA,F_DELTA,F_EQUINOX,
  F_ST,F_HA,F_ZD,
  F_AIRMASS,F_TELFOCUS,
  F_ROTANGLE,F_ROTATORE,
  F_GUIDERX1,F_GUIDERY1,
  F_GUIDERX2,F_GUIDERY2,
  F_TELGUIDE,
 
  F_FILENAME,
  F_EXPTIME,
  F_SEQN,
  F_BINNING,

  F_MASK,                    /* v0316 */
  F_SH,                      /* v0316 */
  F_CA,                      /* v0316 */
  F_CAMERA,                  /* v0317 */
  F_FRAME,                   /* v0317 */
  F_ROTN,                    /* v0317 */
  F_GAIN,

  /* guider state -- mirrors the 'status' command v1.0.6 */
  F_GDINIT,F_GDLOOP,F_GDGUIDE,
  F_GDMODE,F_GDFMODE,F_GDMMODE,
  F_GDAVG,F_CCDOFFS,F_GDSEND,
  F_TEMPSET,F_COOLER,
  F_CAMFPS,F_GDFPS,
  F_GDFWHM,F_GDFLUX,F_GDPEAK,F_GDBACK,
  F_GDDX,F_GDDY,F_GDAZ,F_GDEL,
  F_GDBOXX,F_GDBOXY,F_GDBOXSZ,
  F_GDPA,F_GDSENS,
  F_FRAMETS,

  F_COMMENT,

  F_TEMP_CCD,

  F_SOFTWARE,                /* software version */
  F_FITS,                    /* FITS header version */
  F_END
};

/* ---------------------------------------------------------------- */

#if defined(IS_FITS_C) || defined(IS_QLTOOL_C)

static FITSKey fitskeys[] = {
  { F_SIMPLE,    "SIMPLE  " },         /* mandatory keywords */
  { F_BITPIX,    "BITPIX  " },
  { F_NAXIS,     "NAXIS   " },
  { F_NAXIS1,    "NAXIS1  " },
  { F_NAXIS2,    "NAXIS2  " },
  { F_BSCALE,    "BSCALE  " },         /* data scaling */
  { F_BZERO,     "BZERO   " },
  { F_BUNIT,     "BUNIT   " },
 
  { F_ORIGIN,    "ORIGIN" },           /* origin */
  { F_INSTRUMENT,"INSTRUME" },   
  { F_SCALE,     "SCALE" },
  { F_PSIZE,     "PSIZE" },
 
  { F_DATE_OBS,  "DATE-OBS" },         /* time */
  { F_UT_DATE,   "UT-DATE" },
  { F_UT_TIME,   "UT-TIME" },
  { F_UT_END,    "UT-END" },
  { F_LC_TIME,   "LC-TIME" },
  { F_EPOCH,     "EPOCH" },

  { F_ALPHA,     "RA" },
  { F_DELTA,     "DEC" },
  { F_EQUINOX,   "EQUINOX" },
  { F_ST,        "ST" },
  { F_HA,        "HA" },
  { F_ZD,        "ZD" },
  { F_AIRMASS,   "AIRMASS" },
  { F_TELFOCUS,  "TELFOCUS" },
  { F_ROTANGLE,  "ROTANGLE" },
  { F_ROTATORE,  "ROTATORE" },
  { F_GUIDERX1,  "GUIDERX1" },
  { F_GUIDERY1,  "GUIDERY1" },
  { F_GUIDERX2,  "GUIDERX2" },
  { F_GUIDERY2,  "GUIDERY2" },
  { F_TELGUIDE,  "TELGUIDE" },

  { F_FILENAME,  "FILENAME" },

  { F_EXPTIME,   "EXPTIME" },
  { F_SEQN,      "SEQ-NUM" },
  { F_BINNING,   "BINNING" },

  { F_MASK,      "MASK" },
  { F_SH,        "SH" },
  { F_CA,        "CA" },
  { F_CAMERA,    "CAMERA" },
  { F_FRAME,     "FRAME" },
  { F_ROTN,      "ROTATORN" },
  { F_GAIN,      "GAIN" },

  { F_GDINIT,    "GDINIT" },           /* guider state v1.0.6 */
  { F_GDLOOP,    "GDLOOP" },
  { F_GDGUIDE,   "GDGUIDE" },
  { F_GDMODE,    "GDMODE" },
  { F_GDFMODE,   "GDFMODE" },
  { F_GDMMODE,   "GDMMODE" },
  { F_GDAVG,     "GDAVG" },
  { F_CCDOFFS,   "CCDOFFS" },
  { F_GDSEND,    "GDSEND" },
  { F_TEMPSET,   "TEMPSET" },
  { F_COOLER,    "COOLER" },
  { F_CAMFPS,    "CAMFPS" },
  { F_GDFPS,     "GDFPS" },
  { F_GDFWHM,    "GDFWHM" },
  { F_GDFLUX,    "GDFLUX" },
  { F_GDPEAK,    "GDPEAK" },
  { F_GDBACK,    "GDBACK" },
  { F_GDDX,      "GDDX" },
  { F_GDDY,      "GDDY" },
  { F_GDAZ,      "GDAZ" },
  { F_GDEL,      "GDEL" },
  { F_GDBOXX,    "GDBOXX" },
  { F_GDBOXY,    "GDBOXY" },
  { F_GDBOXSZ,   "GDBOXSZ" },
  { F_GDPA,      "GDPA" },
  { F_GDSENS,    "GDSENS" },
  { F_FRAMETS,   "FRAMETS" },

  { F_COMMENT,   "COMMENT" },

  { F_TEMP_CCD,  "TEMPCCD" },

  { F_SOFTWARE,  "SOFTWARE" },
  { F_FITS,      "FITSVERS" },
 
  { F_END,       "END" },
 
  { -1,"" }
};
 
#endif /* IS_FITS_C */

/* ---------------------------------------------------------------- */

#endif  /* INCLUDE_FITS_H */

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

