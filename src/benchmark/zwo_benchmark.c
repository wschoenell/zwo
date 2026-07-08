/* ----------------------------------------------------------------
 *
 * zwo_benchmark.c
 *
 * ZWO camera server FPS benchmark (video streaming mode).
 * Sweeps {exposure time, binning, pixel bit-depth} and reports
 * measured vs expected FPS (1/exptime) per configuration.
 *
 * Scaffolding step: protocol helpers, CLI parsing, and the outer
 * sweep loop are complete. The per-configuration warmup +
 * measurement window is a stub and will be filled in next.
 *
 * ASI_BANDWIDTHOVERLOAD is set via the server's 'usb' command
 * (--usb N, 40..100); sweep it by running once per value.
 *
 * ---------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>

#include "tcpip.h"
#include "utils.h"
#include "zwo.h"

#define MAX_EXPS   32
#define MAX_BINS    8
#define MAX_BITS    4
#define MAX_ROIS    8
#define CMD_BUF   512
#define LINE_BUF 1024

typedef struct {
  const char *host;
  int         port;
  double      duration_s;
  double      warmup_s;
  double      next_timeout_s;
  int         n_exp;
  double      exptimes[MAX_EXPS];
  int         n_bin;
  int         bins[MAX_BINS];
  int         n_bit;
  int         bitdepths[MAX_BITS];
  int         n_roi;
  double      rois[MAX_ROIS];      /* window size, percent of full frame */
  int         gain, offset, usb, highspeed;
  int         have_gain, have_offset, have_usb, have_highspeed;
  const char *csv_path;
  int         verbose;
} BenchCfg;

typedef struct {
  double exptime;
  int    bin, bits;
  double roi_pct;
  int    x, y;
  int    w, h;
  int    frames;
  double elapsed;
  double fps;
  double expected_fps;
  double efficiency_pct;
  size_t bytes_per_frame;
  double mbps;
  int    drops;
  int    enodata_count;
  char   note[64];
} BenchRow;

static volatile sig_atomic_t g_stop = 0;
static void sigint_handler(int sig) { (void)sig; g_stop = 1; }

/* ---------------- protocol helpers ---------------- */

/* Send a line (we append '\n') and receive a single-line text response
 * (TCPIP_Receive3 strips the trailing LF/CR). Returns 0 on success. */
static int zwo_request(int sock, const char *cmd, char *resp, int resp_len)
{
  char buf[CMD_BUF];
  if ((int)strlen(cmd) + 2 > (int)sizeof(buf)) return -1;
  snprintf(buf, sizeof(buf), "%s\n", cmd);
  if (TCPIP_Send(sock, buf) != 0) return -1;
  if (TCPIP_Receive3(sock, resp, resp_len, 10) != 0) return -1;
  return 0;
}

static int is_error_response(const char *resp)
{
  return (resp[0] == '-' && resp[1] == 'E');
}

static int open_camera(int sock, int *W, int *H, int *cooler, int *color,
                       int *bitDepth, char *model, int model_len)
{
  char buf[LINE_BUF];
  if (zwo_request(sock, "open", buf, sizeof(buf)) != 0) return -1;
  if (is_error_response(buf)) { fprintf(stderr, "open: %s\n", buf); return -1; }
  char m[128] = "";
  int n = sscanf(buf, "%d %d %d %d %d %127s",
                 W, H, cooler, color, bitDepth, m);
  if (n < 6) { fprintf(stderr, "open: bad response '%s'\n", buf); return -1; }
  snprintf(model, model_len, "%s", m);
  return 0;
}

static int close_camera(int sock)
{
  char buf[LINE_BUF];
  return zwo_request(sock, "close", buf, sizeof(buf));
}

static int setup_roi(int sock, int x, int y, int w, int h, int bin, int bits,
                     int *out_x, int *out_y, int *out_w, int *out_h,
                     int *out_bin, int *out_bits)
{
  char cmd[CMD_BUF], buf[LINE_BUF];
  snprintf(cmd, sizeof(cmd), "setup %d %d %d %d %d %d", x, y, w, h, bin, bits);
  if (zwo_request(sock, cmd, buf, sizeof(buf)) != 0) return -1;
  if (is_error_response(buf)) { fprintf(stderr, "setup: %s\n", buf); return -1; }
  int n = sscanf(buf, "%d %d %d %d %d %d",
                 out_x, out_y, out_w, out_h, out_bin, out_bits);
  if (n != 6) { fprintf(stderr, "setup: bad response '%s'\n", buf); return -1; }
  return 0;
}

static int set_exptime(int sock, double t)
{
  char cmd[CMD_BUF], buf[LINE_BUF];
  snprintf(cmd, sizeof(cmd), "exptime %.6f", t);
  if (zwo_request(sock, cmd, buf, sizeof(buf)) != 0) return -1;
  if (is_error_response(buf)) { fprintf(stderr, "exptime: %s\n", buf); return -1; }
  return 0;
}

static int set_int_control(int sock, const char *name, int value)
{
  char cmd[CMD_BUF], buf[LINE_BUF];
  snprintf(cmd, sizeof(cmd), "%s %d", name, value);
  if (zwo_request(sock, cmd, buf, sizeof(buf)) != 0) return -1;
  if (is_error_response(buf)) {
    fprintf(stderr, "%s: %s\n", name, buf); return -1;
  }
  return 0;
}

static int start_stream(int sock)
{
  char buf[LINE_BUF];
  if (zwo_request(sock, "start", buf, sizeof(buf)) != 0) return -1;
  if (is_error_response(buf)) { fprintf(stderr, "start: %s\n", buf); return -1; }
  return 0;
}

static int stop_stream(int sock)
{
  char buf[LINE_BUF];
  (void)zwo_request(sock, "stop", buf, sizeof(buf));
  return 0;
}

/* Receive exactly nbytes of binary data. Returns 0 ok, -1 on error/timeout. */
static int recv_exact(int sock, u_char *buf, size_t nbytes, int timeout_s)
{
  size_t got = 0;
  while (got < nbytes) {
    ssize_t r = TCPIP_Recv(sock, (char*)buf + got, (int)(nbytes - got), timeout_s);
    if (r <= 0) return -1;
    got += r;
  }
  return 0;
}

/* Fetch one video frame.
 *   return  0 : ok (seq/temp/power/buf populated)
 *   return  1 : server replied -Enodata (no frame within its own timeout)
 *   return <0 : error
 */
static int next_frame(int sock, double server_timeout_s,
                      unsigned int *seq, double *temp, double *power,
                      u_char *buf, size_t nbytes, int recv_timeout_s)
{
  char cmd[CMD_BUF], resp[LINE_BUF];
  snprintf(cmd, sizeof(cmd), "next %.2f", server_timeout_s);
  if (zwo_request(sock, cmd, resp, sizeof(resp)) != 0) return -2;
  if (strncmp(resp, "-Enodata", 8) == 0) return 1;
  if (is_error_response(resp)) {
    fprintf(stderr, "next: %s\n", resp);
    return -3;
  }
  int per = 0;
  if (sscanf(resp, "%u %lf %d", seq, temp, &per) != 3) {
    fprintf(stderr, "next: bad header '%s'\n", resp);
    return -4;
  }
  *power = (double)per;
  if (recv_exact(sock, buf, nbytes, recv_timeout_s) != 0) return -5;
  return 0;
}

/* ---------------- arg parsing ---------------- */

static int parse_double_csv(const char *s, double *out, int max)
{
  int n = 0;
  const char *p = s;
  while (*p && n < max) {
    char *end;
    double v = strtod(p, &end);
    if (end == p) return -1;
    out[n++] = v;
    p = end;
    if (*p == ',') p++;
  }
  return n;
}

static int parse_int_csv(const char *s, int *out, int max)
{
  int n = 0;
  const char *p = s;
  while (*p && n < max) {
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return -1;
    out[n++] = (int)v;
    p = end;
    if (*p == ',') p++;
  }
  return n;
}

static void set_defaults(BenchCfg *c)
{
  memset(c, 0, sizeof(*c));
  c->host = "localhost";
  c->port = SERVER_PORT;
  c->duration_s = 10.0;
  c->warmup_s = 3.0;
  c->next_timeout_s = 2.0;
  double de[] = {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0};
  c->n_exp = (int)(sizeof(de) / sizeof(de[0]));
  memcpy(c->exptimes, de, sizeof(de));
  int db[] = {1, 2, 4};
  c->n_bin = (int)(sizeof(db) / sizeof(db[0]));
  memcpy(c->bins, db, sizeof(db));
  int dbt[] = {8, 16};
  c->n_bit = (int)(sizeof(dbt) / sizeof(dbt[0]));
  memcpy(c->bitdepths, dbt, sizeof(dbt));
  c->n_roi = 1;
  c->rois[0] = 100.0;
}

static void usage(const char *prog)
{
  fprintf(stderr,
"usage: %s [options]\n"
"  --host HOST             (default: localhost)\n"
"  --port PORT             (default: %d)\n"
"  --exptimes CSV          (default: 0.001,0.005,0.01,0.05,0.1,0.5,1.0)\n"
"  --bins CSV              (default: 1,2,4)\n"
"  --bits CSV              (default: 8,16)\n"
"  --rois CSV              window size as %% of full frame, centered\n"
"                          (default: 100; e.g. 10,50,80,100)\n"
"  --duration SEC          (default: 10.0)\n"
"  --warmup SEC            (default: 3.0)\n"
"  --next-timeout SEC      (default: 2.0)\n"
"  --gain N                (optional)\n"
"  --offset N              (optional)\n"
"  --usb N                 ASI_BANDWIDTHOVERLOAD 40..100 (optional)\n"
"  --highspeed N           ASI_HIGH_SPEED_MODE 0/1, 10-bit ADC (optional)\n"
"  --csv PATH              (optional)\n"
"  -v, --verbose\n"
"  -h, --help\n", prog, SERVER_PORT);
}

static int parse_args(int argc, char **argv, BenchCfg *c)
{
  set_defaults(c);
  static struct option opts[] = {
    {"host",         required_argument, 0, 'H'},
    {"port",         required_argument, 0, 'P'},
    {"exptimes",     required_argument, 0, 'e'},
    {"bins",         required_argument, 0, 'b'},
    {"bits",         required_argument, 0, 'B'},
    {"rois",         required_argument, 0, 'r'},
    {"duration",     required_argument, 0, 'd'},
    {"warmup",       required_argument, 0, 'w'},
    {"next-timeout", required_argument, 0, 't'},
    {"gain",         required_argument, 0, 'g'},
    {"offset",       required_argument, 0, 'o'},
    {"usb",          required_argument, 0, 'u'},
    {"highspeed",    required_argument, 0, 'S'},
    {"csv",          required_argument, 0, 'c'},
    {"verbose",      no_argument,       0, 'v'},
    {"help",         no_argument,       0, 'h'},
    {0,0,0,0}
  };
  int idx, opt;
  while ((opt = getopt_long(argc, argv, "vh", opts, &idx)) != -1) {
    switch (opt) {
    case 'H': c->host = optarg; break;
    case 'P': c->port = atoi(optarg); break;
    case 'e': {
      int n = parse_double_csv(optarg, c->exptimes, MAX_EXPS);
      if (n <= 0) { fprintf(stderr, "bad --exptimes\n"); return -1; }
      c->n_exp = n; break;
    }
    case 'b': {
      int n = parse_int_csv(optarg, c->bins, MAX_BINS);
      if (n <= 0) { fprintf(stderr, "bad --bins\n"); return -1; }
      c->n_bin = n; break;
    }
    case 'B': {
      int n = parse_int_csv(optarg, c->bitdepths, MAX_BITS);
      if (n <= 0) { fprintf(stderr, "bad --bits\n"); return -1; }
      c->n_bit = n; break;
    }
    case 'r': {
      int n = parse_double_csv(optarg, c->rois, MAX_ROIS);
      if (n <= 0) { fprintf(stderr, "bad --rois\n"); return -1; }
      for (int i = 0; i < n; i++) {
        if (c->rois[i] <= 0.0 || c->rois[i] > 100.0) {
          fprintf(stderr, "bad --rois: %g not in (0,100]\n", c->rois[i]);
          return -1;
        }
      }
      c->n_roi = n; break;
    }
    case 'd': c->duration_s = atof(optarg); break;
    case 'w': c->warmup_s = atof(optarg); break;
    case 't': c->next_timeout_s = atof(optarg); break;
    case 'g': c->gain = atoi(optarg); c->have_gain = 1; break;
    case 'o': c->offset = atoi(optarg); c->have_offset = 1; break;
    case 'u': c->usb = atoi(optarg); c->have_usb = 1; break;
    case 'S': c->highspeed = atoi(optarg); c->have_highspeed = 1; break;
    case 'c': c->csv_path = optarg; break;
    case 'v': c->verbose = 1; break;
    case 'h':
    default:  usage(argv[0]); return -1;
    }
  }
  for (int i = 0; i < c->n_exp; i++) {
    if (c->exptimes[i] < MIN_EXPTIME || c->exptimes[i] > MAX_EXPTIME) {
      fprintf(stderr, "warning: exptime %.6f out of [%.4f, %d] - skipped\n",
              c->exptimes[i], MIN_EXPTIME, MAX_EXPTIME);
      for (int j = i; j < c->n_exp - 1; j++) c->exptimes[j] = c->exptimes[j+1];
      c->n_exp--; i--;
    }
  }
  return 0;
}

/* ---------------- per-configuration run ---------------- */

/* Warmup + measurement window for one (exptime, bin, bits) configuration.
 * Caller owns a reusable frame buffer (`buf`, `buf_cap`) that is grown
 * on demand so we don't pay malloc cost per config. */
static int run_one(int sock, const BenchCfg *cfg,
                   int W, int H, double exptime, int bin, int bits,
                   double roi_pct,
                   u_char **buf, size_t *buf_cap, BenchRow *row)
{
  memset(row, 0, sizeof(*row));
  row->exptime = exptime; row->bin = bin; row->bits = bits;
  row->roi_pct = roi_pct;
  row->expected_fps = 1.0 / exptime;

  /* Window of roi_pct % of the (binned) full frame, centered on the
   * sensor.  Same 8/2 rounding as the server; offsets aligned too. */
  int full_w = (W / bin) & ~7;
  int full_h = (H / bin) & ~1;
  int want_w = (int)(full_w * roi_pct / 100.0) & ~7;
  int want_h = (int)(full_h * roi_pct / 100.0) & ~1;
  if (want_w < 8) want_w = 8;
  if (want_h < 2) want_h = 2;
  int want_x = ((full_w - want_w) / 2) & ~7;
  int want_y = ((full_h - want_h) / 2) & ~1;

  int ox, oy, ow, oh, obin, obits;
  if (setup_roi(sock, want_x, want_y, want_w, want_h, bin, bits,
                &ox, &oy, &ow, &oh, &obin, &obits) != 0) {
    snprintf(row->note, sizeof(row->note), "setup fail");
    return -1;
  }
  row->x = ox; row->y = oy; row->w = ow; row->h = oh; row->bits = obits;
  size_t nbytes = (size_t)ow * (size_t)oh * (size_t)(obits / 8);
  row->bytes_per_frame = nbytes;

  if (nbytes > *buf_cap) {
    u_char *nb = realloc(*buf, nbytes);
    if (!nb) { snprintf(row->note, sizeof(row->note), "oom"); return -1; }
    *buf = nb; *buf_cap = nbytes;
  }

  if (set_exptime(sock, exptime) != 0) {
    snprintf(row->note, sizeof(row->note), "exptime fail");
    return -1;
  }
  if (start_stream(sock) != 0) {
    snprintf(row->note, sizeof(row->note), "start fail");
    return -1;
  }

  int recv_timeout_s = (int)ceil(cfg->next_timeout_s + exptime + 1.0);
  unsigned int seq = 0, last_seq = 0;
  double temp = 0, power = 0;

  /* Warmup: discard frames for max(warmup_s, 5*exptime) AND >= 5 frames.
   * Covers TCP slow-start and any initial camera ramp. */
  double warm_s = cfg->warmup_s;
  if (5.0 * exptime > warm_s) warm_s = 5.0 * exptime;
  double t_warm_end = walltime(0) + warm_s;
  int warm = 0;
  while (!g_stop && (walltime(0) < t_warm_end || warm < 5)) {
    int r = next_frame(sock, cfg->next_timeout_s, &seq, &temp, &power,
                       *buf, nbytes, recv_timeout_s);
    if (r == 0) warm++;
    else if (r == 1) msleep(5);
    else {
      snprintf(row->note, sizeof(row->note), "warmup fail (%d)", r);
      stop_stream(sock);
      return -1;
    }
  }

  /* Measurement window. */
  int first = 1;
  int frames = 0, drops = 0, enodata = 0;
  double t0 = walltime(0);
  double t_end = t0 + cfg->duration_s;
  double t_last = t0;
  while (!g_stop && walltime(0) < t_end) {
    int r = next_frame(sock, cfg->next_timeout_s, &seq, &temp, &power,
                       *buf, nbytes, recv_timeout_s);
    if (r == 1) { enodata++; msleep(5); continue; }
    if (r < 0) {
      snprintf(row->note, sizeof(row->note), "recv fail (%d)", r);
      break;
    }
    if (!first && seq > last_seq + 1) drops += (int)(seq - last_seq - 1);
    last_seq = seq; first = 0; frames++;
    if (cfg->verbose) {
      double t_now = walltime(0);
      fprintf(stderr, "  seq=%u dt=%.4f temp=%.1f power=%.0f\n",
              seq, t_now - t_last, temp, power);
      t_last = t_now;
    }
  }
  double elapsed = walltime(0) - t0;
  stop_stream(sock);

  row->frames = frames;
  row->elapsed = elapsed;
  row->fps = (elapsed > 0) ? frames / elapsed : 0.0;
  row->efficiency_pct = (row->expected_fps > 0)
                        ? 100.0 * row->fps / row->expected_fps : 0.0;
  row->mbps = (elapsed > 0)
              ? (double)frames * (double)nbytes / elapsed / 1.0e6 : 0.0;
  row->drops = drops;
  row->enodata_count = enodata;
  if (g_stop && row->note[0] == '\0')
    snprintf(row->note, sizeof(row->note), "interrupted");
  return 0;
}

/* ---------------- output ---------------- */

static void print_session_banner(const BenchCfg *cfg, const char *model,
                                 int W, int H, int cooler, int color,
                                 int bitDepth)
{
  printf("ZWO benchmark   host=%s:%d   duration=%.1fs   warmup=%.1fs",
         cfg->host, cfg->port, cfg->duration_s, cfg->warmup_s);
  if (cfg->have_usb) printf("   usb=%d", cfg->usb);
  if (cfg->have_highspeed) printf("   highspeed=%d", cfg->highspeed);
  printf("\n");
  printf("camera: %s  %dx%d  cooler=%d color=%d bitDepth=%d\n\n",
         model, W, H, cooler, color, bitDepth);
  fflush(stdout);
}

/* Short progress line on stderr so stdout stays clean for the final table
 * (allows `zwo_benchmark ... > results.txt` without losing feedback). */
static void print_progress_start(int idx, int total, const BenchRow *r)
{
  fprintf(stderr, "[%2d/%2d] exptime=%.4f bin=%d bits=%d roi=%.0f%% ... ",
          idx, total, r->exptime, r->bin, r->bits, r->roi_pct);
  fflush(stderr);
}

static void print_progress_done(const BenchRow *r)
{
  if (r->note[0]) {
    fprintf(stderr, "%s (fps=%.2f/%.2f)\n", r->note, r->fps, r->expected_fps);
  } else {
    fprintf(stderr, "fps=%.2f/%.2f eff=%.1f%% drops=%d\n",
            r->fps, r->expected_fps, r->efficiency_pct, r->drops);
  }
  fflush(stderr);
}

/* Column layout for the bordered ASCII table. Keep the widths here in sync
 * with the format strings in print_table_*(). */
static void print_table_separator(FILE *fp)
{
  fprintf(fp,
    "+--------+-----+------+-------+------+------+--------+---------+---------"
    "+---------+-------+------+---------+--------+--------------------+\n");
}

static void print_table_header(FILE *fp)
{
  fprintf(fp,
    "| %-6s | %-3s | %-4s | %-5s | %-4s | %-4s | %-6s | %-7s | %-7s | %-7s | "
    "%-5s | %-4s | %-7s | %-6s | %-18s |\n",
    "exptim", "bin", "bits", "roi%", "W", "H", "frames", "elapsed", "fps",
    "expFPS", "eff%", "drop", "enodata", "MB/s", "note");
}

static void print_table_row(FILE *fp, const BenchRow *r)
{
  fprintf(fp,
    "| %6.4f | %3d | %4d | %5.1f | %4d | %4d | %6d | %7.2f | %7.2f | %7.2f | "
    "%5.1f | %4d | %7d | %6.1f | %-18.18s |\n",
    r->exptime, r->bin, r->bits, r->roi_pct, r->w, r->h,
    r->frames, r->elapsed, r->fps, r->expected_fps,
    r->efficiency_pct, r->drops, r->enodata_count, r->mbps,
    r->note[0] ? r->note : "");
}

static void print_table(const BenchRow *rows, int n)
{
  print_table_separator(stdout);
  print_table_header(stdout);
  print_table_separator(stdout);
  for (int i = 0; i < n; i++) print_table_row(stdout, &rows[i]);
  print_table_separator(stdout);
  fflush(stdout);
}

static int write_csv(const BenchRow *rows, int n, const char *path)
{
  FILE *fp = fopen(path, "w");
  if (!fp) {
    fprintf(stderr, "csv: cannot open '%s': %s\n", path, strerror(errno));
    return -1;
  }
  fprintf(fp, "exptime,bin,bits,roi_pct,x,y,w,h,frames,elapsed,fps,expected_fps,"
              "efficiency_pct,drops,enodata,bytes_per_frame,mbps,note\n");
  for (int i = 0; i < n; i++) {
    const BenchRow *r = &rows[i];
    fprintf(fp, "%.6f,%d,%d,%.2f,%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.2f,%d,%d,%zu,%.3f,\"%s\"\n",
            r->exptime, r->bin, r->bits, r->roi_pct, r->x, r->y, r->w, r->h,
            r->frames, r->elapsed, r->fps, r->expected_fps,
            r->efficiency_pct, r->drops, r->enodata_count,
            r->bytes_per_frame, r->mbps, r->note);
  }
  fclose(fp);
  return 0;
}

/* ---------------- main ---------------- */

int main(int argc, char **argv)
{
  BenchCfg cfg;
  if (parse_args(argc, argv, &cfg) != 0) return 1;

  signal(SIGINT, sigint_handler);

  int err = 0;
  int sock = TCPIP_CreateClientSocket(cfg.host, (u_short)cfg.port, &err);
  if (sock < 0) {
    fprintf(stderr, "connect to %s:%d failed (err=%d)\n",
            cfg.host, cfg.port, err);
    return 2;
  }

  int W = 0, H = 0, cooler = 0, color = 0, bitDepth = 0;
  char model[64] = "";
  if (open_camera(sock, &W, &H, &cooler, &color, &bitDepth,
                  model, sizeof(model)) != 0) {
    close(sock);
    return 3;
  }

  if (cfg.have_gain)   set_int_control(sock, "gain",   cfg.gain);
  if (cfg.have_offset) set_int_control(sock, "offset", cfg.offset);
  if (cfg.have_usb)    set_int_control(sock, "usb",    cfg.usb);
  if (cfg.have_highspeed) set_int_control(sock, "highspeed", cfg.highspeed);

  print_session_banner(&cfg, model, W, H, cooler, color, bitDepth);

  int n_rows = cfg.n_bit * cfg.n_bin * cfg.n_roi * cfg.n_exp;
  BenchRow *rows = calloc((size_t)n_rows, sizeof(BenchRow));
  u_char *frame_buf = NULL;
  size_t  frame_cap = 0;
  int idx = 0, completed = 0;
  for (int bi = 0; bi < cfg.n_bit && !g_stop; bi++) {
    for (int ni = 0; ni < cfg.n_bin && !g_stop; ni++) {
      for (int ri = 0; ri < cfg.n_roi && !g_stop; ri++) {
        for (int ei = 0; ei < cfg.n_exp && !g_stop; ei++) {
          BenchRow *row = &rows[idx];
          row->exptime = cfg.exptimes[ei];
          row->bin     = cfg.bins[ni];
          row->bits    = cfg.bitdepths[bi];
          row->roi_pct = cfg.rois[ri];
          print_progress_start(idx + 1, n_rows, row);
          (void)run_one(sock, &cfg, W, H,
                        row->exptime, row->bin, row->bits, row->roi_pct,
                        &frame_buf, &frame_cap, row);
          print_progress_done(row);
          completed = ++idx;
        }
      }
    }
  }

  fprintf(stderr, "\n");
  print_table(rows, completed);

  if (cfg.csv_path) {
    if (write_csv(rows, completed, cfg.csv_path) == 0) {
      fprintf(stderr, "wrote %d rows to %s\n", completed, cfg.csv_path);
    }
  }

  close_camera(sock);
  close(sock);
  free(frame_buf);
  free(rows);
  return 0;
}

/* ---------------------------------------------------------------- */
