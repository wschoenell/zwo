/* ----------------------------------------------------------------
 *  
 * ZWO/ASI camera
 *
 * ---------------------------------------------------------------- */

#define PROJECT_ID      23
#define P_VERSION       "1.0.6"       /* ASI SDK 1.41 */

extern void message(const void*,const char*,int);

#define SERVER_PORT     (50011+(100*PROJECT_ID))

#define MSS_FILE        0x0001         /* MSG_ --> MSS_ v0034 */
#define MSS_FLUSH       0x0003
#define MSS_CLOSE       0x0004

#define MSS_WINDOW      0x0100
#define MSS_YELLOW      0x0200
#define MSS_RED         0x0400
#define MSS_OVERWRITE   0x0800

#define MSS_INFO        (MSS_FLUSH | MSS_WINDOW)  /* NEW v0034 */
#define MSS_WARN        (MSS_WINDOW | MSS_YELLOW)
#define MSS_ERROR       (MSS_FLUSH | MSS_WINDOW | MSS_RED)
#define MSS_OVER        (MSS_WINDOW | MSS_OVERWRITE)

enum zwo_error_codes {  E_ERROR=200,
                        E_NOTINIT,
                        E_RUNNING,
                        E_MISSPAR,
                        E_NOTACQ,
                        E_INCFRAME,
                        E_FILTER,
                        E_NOTIMP };

#define MAX_EXPTIME     30
#define MIN_EXPTIME     0.0001

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

