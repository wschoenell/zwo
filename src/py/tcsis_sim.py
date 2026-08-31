#!/usr/bin/env python3
"""
Minimal TCSIS simulator for gcamzwo.

gcam's telio.c speaks a trivial protocol: it sends "<keyword>\\n" and reads
one value terminated by LF/CR. Launching gcamzwo with any '-t' outside
{0,1,2} points telio at localhost:5801, which is what this serves --
see the "TCSIS Simulator" case in zwogcam.c.

Values are plausible but static; the point is to model a *responsive* TCS
so the cost of gcam's per-request round trips can be measured. --delay adds
a per-request service time to model a slower TCS.

    ./tcsis_sim.py                 # responsive
    ./tcsis_sim.py --delay 0.005   # 5 ms per request
"""

import argparse, socket, socketserver, threading, time

VALUES = {
    "ra": "06:45:08.9", "dec": "-16:42:58", "epoch": "2000.0",
    "st": "07:12:33", "ha": "00:27:24", "ut": "23:17:01",
    "zd": "31.4", "airmass": "1.171", "telfocus": "1234.5",
    "rotangle": "45.6", "rotatore": "-12.3", "rotatorn": "3",
    "telaz": "123.4", "telel": "58.6", "telpa": "45.6",
    "guiderx1": "10.250", "guidery1": "-5.125",
    "guiderx2": "0.000",  "guidery2": "0.000",
    "guiderx3": "0.000",  "guidery3": "0.000",
    "telguide": "1 0", "roi": "3", "gdrmountmv": "000", "mountmv": "0000",
    "tambient": "8.4", "tcell": "8.1", "wxtemp": "8.2",
    # multi-value "dump" requests the protocol doc recommends
    "datetime": "2026-08-29 23:17:01 07:12:33",
    "telpos": "06:45:08.9 -16:42:58 2000.0 00:27:24 1.171 45.6",
    "teldata": "3 1 000 0000 123.4 58.6 31.4 45.6 0.0 0",
    "gdr1data": "10.250 -5.125 0.000 0 0",
    "gdr2data": "0.000 0.000 0.000 0 0",
}

COUNT = [0]
LOCK = threading.Lock()


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        while True:
            line = self.rfile.readline()
            if not line:
                break
            if self.server.delay:
                time.sleep(self.server.delay)
            key = line.decode("ascii", "replace").strip().split()[0].lower() \
                if line.strip() else ""
            with LOCK:
                COUNT[0] += 1
            if self.server.verbose:
                print("  req %s" % key, flush=True)
            val = VALUES.get(key)
            # commands (aeg, gpaer, ofra, ...) just acknowledge
            self.wfile.write(((val if val is not None else "0") + "\n").encode())
            self.wfile.flush()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=5801)
    p.add_argument("--delay", type=float, default=0.0,
                   help="per-request service time [s]")
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--report", type=float, default=0.0,
                   help="print request count every N seconds")
    a = p.parse_args()
    srv = Server(("127.0.0.1", a.port), Handler)
    srv.delay = a.delay
    srv.verbose = a.verbose
    print("TCSIS simulator on 127.0.0.1:%d  delay=%.1f ms"
          % (a.port, a.delay * 1e3), flush=True)
    if a.report:
        def rep():
            last = 0
            while True:
                time.sleep(a.report)
                with LOCK:
                    now = COUNT[0]
                print("  TCS requests: %d total (%.1f/s)"
                      % (now, (now - last) / a.report), flush=True)
                last = now
        threading.Thread(target=rep, daemon=True).start()
    srv.serve_forever()


if __name__ == "__main__":
    main()
