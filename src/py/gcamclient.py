#!/usr/bin/env python3
"""
Reference client for the gcamzwo image server.

The server listens on 'image_port + gnum' (default 52300+gnum) and speaks
one verb:

    fits [timeout]  ->  "<seq> <ts_ns> <nbytes>\\n" + nbytes of FITS

'timeout' is how many seconds to wait for a frame newer than the last one
served on this connection; 0 (the default) returns the newest frame
available immediately. Nothing new within the timeout gives "-Enodata".
Any other failure gives "-E<message>".

Guider state travels in the FITS header (GDFWHM, GDGUIDE, GDDX, ... and
FRAMETS), so a frame is self-contained -- see docs/plans/gcam-image-server.md.

Examples:
    ./gcamclient.py --host rpi --gnum 3 --out /tmp/frame.fits
    ./gcamclient.py --count 10 --timeout 2 --quiet
"""

import argparse
import socket
import sys


class GcamImageClient:
    def __init__(self, host="127.0.0.1", port=52303, timeout=30.0):
        self.addr = (host, port)
        self.sock = socket.create_connection(self.addr, timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._rx = b""

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _readline(self):
        while b"\n" not in self._rx:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self._rx += chunk
        line, _, self._rx = self._rx.partition(b"\n")
        return line.decode("ascii", "replace").strip()

    def _read_exactly(self, n):
        out = bytearray(self._rx[:n])
        self._rx = self._rx[n:]
        while len(out) < n:
            chunk = self.sock.recv(min(1 << 20, n - len(out)))
            if not chunk:
                raise ConnectionError("short read: got %d of %d bytes" % (len(out), n))
            out += chunk
        return bytes(out)

    def fits(self, timeout=0.0):
        """Return (seq, ts_ns, fits_bytes), or None if no new frame."""
        self.sock.sendall(b"fits %.2f\n" % timeout)
        line = self._readline()
        if line.startswith("-E"):
            if "nodata" in line:
                return None
            raise RuntimeError(line[2:])
        seq, ts_ns, nbytes = (int(f) for f in line.split())
        return seq, ts_ns, self._read_exactly(nbytes)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=None,
                   help="explicit port; overrides --gnum")
    p.add_argument("--gnum", type=int, default=3, help="guider number (port 52300+gnum)")
    p.add_argument("--timeout", type=float, default=0.0,
                   help="seconds to wait for a new frame (default 0)")
    p.add_argument("--count", type=int, default=1, help="frames to fetch")
    p.add_argument("--out", help="write the last frame here")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args()

    port = args.port if args.port else 52300 + args.gnum
    last = None
    with GcamImageClient(args.host, port) as c:
        for i in range(args.count):
            got = c.fits(args.timeout)
            if got is None:
                print("no new frame", file=sys.stderr)
                continue
            seq, ts_ns, blob = got
            last = blob
            if not args.quiet:
                print("seq=%-6d ts_ns=%d bytes=%d" % (seq, ts_ns, len(blob)))

    if args.out and last:
        with open(args.out, "wb") as fp:
            fp.write(last)
        if not args.quiet:
            print("wrote %s (%d bytes)" % (args.out, len(last)))


if __name__ == "__main__":
    main()
