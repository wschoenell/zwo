/* ----------------------------------------------------------------
 *
 * QL-Tool
 *
 * Christoph C. Birk (birk@obs.carnegiescience.edu)
 *
 * ---------------------------------------------------------------- */

#define QLT_NCOLORS     200
#define QLT_NCURSORS    5
#define QLT_BOX         (QLT_NCURSORS-1)
#define QLT_NFOV        64             /* loadfov: max. boxes */

typedef struct { float x,y,w,h,a; } FovBox;  /* ds9 box, frame pixels */

enum guider_modes { GM_PR=1,GM_SH,GM_SV3,GM_SV4,GM_SV5 };

/* ---------------------------------------------------------------- */

#include <pthread.h>

#include <cxt.h>

/* ---------------------------------------------------------------- */

typedef struct qltool_tag {
  Window       parent;
  Display      *disp;
  GfC          gfs;
  int          lut;
  int          smoothing;              /* v0059 */
  EditWindow   pctbox,valbox,bkgbox,spnbox;
  int          scale,scale_par1,scale_par2;
  int          pct,bkg,val,span,zero;
  int          ix,iy;                  /* upper left corner */
  Window       iwin,lwin;              /* image/lupe window(s) */
  XImage       *theImage,*thelupe,*cmimage;
  u_long       pixels[QLT_NCOLORS];
  u_long       satcol;
  volatile int vrad;
  int          dimx,dimy,maxx,maxy;
  double       MulX,MulY;
  u_int        lWIDE,lHIGH;            /* lupe geometry */
  u_int        iWIDE,iHIGH;            /* image geometry */
  int          data_max,data_min,data_dyn;
  u_int        colors24[QLT_NCOLORS];  /* 64-bit */
  char         *imgbuf,*lupbuf;
  size_t       imgsize,lupsize;
  pthread_mutex_t lock;
  u_short*     sdata;
  u_short      satlev;
  void*        picture0;               /* pointer to image data */
  void*        lupe0;
  int          flip_x,flip_y;
  int          lmag;                   /* magnification */
  double       fwhm,flux,enoise,egain;
  volatile int guiding;
  int          gmode;
  /* cursors */
  EditWindow     cubox[QLT_NCURSORS];
  volatile float curx[QLT_NCURSORS],cury[QLT_NCURSORS];
  int            cursor_mode,cursor_dark;  /* v0321 */
  float          cursor_step;
  /* arc in gm5 */
  double    arc_radius,arc_angle;
  /* loadfov overlay */
  FovBox    fov[QLT_NFOV];
  int       nfov;
} QlTool;

QlTool* qltool_create   (MainWindow*,Window,const char*,int,int,int,
                         int,int,int,int,int,int,int,int);
void    qltool_reset    (QlTool*,int,int,int);
void    qltool_update   (QlTool*,u_short*);
void    qltool_redraw   (QlTool*,Bool);
void    qltool_cursor_set(QlTool*,int,int,int,double);
void    qltool_cursor_off(QlTool*,int,int,int,double,double*,double*);

int     qltool_event     (QlTool*,XEvent*);
int     qltool_handle_key(QlTool*,XKeyEvent*,int);

void    qltool_centroid(QlTool*,double*,double*);

void    qltool_lut(QlTool*,const char*);
void    qltool_scale(QlTool*,const char*,const char*,const char*);
void    qltool_lmag(QlTool*,int);

double get_background(u_short*,int,int,int,int,int,double*);
double get_fwhm(u_short*,int,int,int,int,int,double,double,
                double*,double*,double*,double*,double*);
double get_quads(u_short*,int,int,int,int,int,double*,double*,double*);
double calc_quad(int,int,double,double,double*);

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

