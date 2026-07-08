# zwo_benchmark — ZWO server FPS benchmark

Measures client-side frames-per-second of the ZWO camera server
(`src/server/zwoserver.c`, port 52311) in video-streaming mode
(`start` → loop `next` → `stop`), sweeping exposure time, binning, and
pixel bit depth. For each configuration it reports actual FPS vs the
expected FPS (`1/exptime`), frame drops (gaps in the server's `seq`
counter), and network throughput.

Plan / design notes: [docs/plans/zwo-benchmark.md](../../docs/plans/zwo-benchmark.md)

## Build

Links against the server's `tcpip`/`utils`/`ptlib` objects (Linux target):

```
make -C src/server tcpip.o utils.o ptlib.o
make -C src/benchmark
```

## Usage

```
zwo_benchmark [options]
  --host HOST             (default: localhost)
  --port PORT             (default: 52311)
  --exptimes CSV          (default: 0.001,0.005,0.01,0.05,0.1,0.5,1.0)
  --bins CSV              (default: 1,2,4)
  --bits CSV              (default: 8,16)
  --rois CSV              window size as % of full frame, centered
                          (default: 100; e.g. 10,50,80,100)
  --duration SEC          measurement window per config (default: 10.0)
  --warmup SEC            discarded frames before measuring (default: 3.0)
  --next-timeout SEC      timeout passed to 'next' (default: 2.0)
  --gain N                (optional)
  --offset N              (optional)
  --csv PATH              also write results as CSV (optional)
  -v, --verbose           per-frame stderr logging
  -h, --help
```

Example — full default sweep against a remote server, with CSV output:

```
./zwo_benchmark --host 10.8.80.225 --csv results.csv
```

Ctrl-C stops cleanly: the current config's partial row is kept and
labelled `interrupted`.

## Example results

Full default sweep (42 configs, 10 s each) run 2026-07-08 on
`zwo-bootsrv` (Raspberry Pi, gigabit Ethernet) against an
ASI294MM Pro served from 10.8.80.225, with the 2026-07-08 server
fixes deployed (see below):

```
ZWO benchmark   host=10.8.80.225:52311   duration=10.0s   warmup=3.0s
camera: ZWO_ASI294MM_Pro  8288x5644  cooler=1 color=0 bitDepth=12

+--------+-----+------+-------+------+------+--------+---------+---------+---------+-------+------+---------+--------+--------------------+
| exptim | bin | bits | roi%  | W    | H    | frames | elapsed | fps     | expFPS  | eff%  | drop | enodata | MB/s   | note               |
+--------+-----+------+-------+------+------+--------+---------+---------+---------+-------+------+---------+--------+--------------------+
| 0.0010 |   1 |    8 | 100.0 | 8288 | 5644 |     22 |   10.35 |    2.13 | 1000.00 |   0.2 |   24 |       0 |   99.4 |                    |
| 0.0050 |   1 |    8 | 100.0 | 8288 | 5644 |     22 |   10.28 |    2.14 |  200.00 |   1.1 |   24 |       0 |  100.1 |                    |
| 0.0100 |   1 |    8 | 100.0 | 8288 | 5644 |     22 |   10.28 |    2.14 |  100.00 |   2.1 |   24 |       0 |  100.1 |                    |
| 0.0500 |   1 |    8 | 100.0 | 8288 | 5644 |     22 |   10.26 |    2.14 |   20.00 |  10.7 |   24 |       0 |  100.3 |                    |
| 0.1000 |   1 |    8 | 100.0 | 8288 | 5644 |     22 |   10.38 |    2.12 |   10.00 |  21.2 |   24 |       0 |   99.2 |                    |
| 0.5000 |   1 |    8 | 100.0 | 8288 | 5644 |     20 |   10.01 |    2.00 |    2.00 |  99.9 |    0 |       0 |   93.4 |                    |
| 1.0000 |   1 |    8 | 100.0 | 8288 | 5644 |      8 |   10.32 |    0.78 |    1.00 |  77.5 |    0 |       0 |   36.3 |                    |
| 0.0010 |   2 |    8 | 100.0 | 4144 | 2822 |     85 |   10.07 |    8.44 | 1000.00 |   0.8 |   78 |       0 |   98.7 |                    |
| 0.0050 |   2 |    8 | 100.0 | 4144 | 2822 |     85 |   10.04 |    8.47 |  200.00 |   4.2 |   78 |       0 |   99.0 |                    |
| 0.0100 |   2 |    8 | 100.0 | 4144 | 2822 |     83 |   10.03 |    8.28 |  100.00 |   8.3 |   79 |       0 |   96.8 |                    |
| 0.0500 |   2 |    8 | 100.0 | 4144 | 2822 |     85 |   10.11 |    8.40 |   20.00 |  42.0 |   79 |       0 |   98.3 |                    |
| 0.1000 |   2 |    8 | 100.0 | 4144 | 2822 |     93 |   10.06 |    9.24 |   10.00 |  92.4 |    7 |       0 |  108.1 |                    |
| 0.5000 |   2 |    8 | 100.0 | 4144 | 2822 |     20 |   10.00 |    2.00 |    2.00 | 100.0 |    0 |       0 |   23.4 |                    |
| 1.0000 |   2 |    8 | 100.0 | 4144 | 2822 |      9 |   10.25 |    0.88 |    1.00 |  87.8 |    0 |       0 |   10.3 |                    |
| 0.0010 |   4 |    8 | 100.0 | 2072 | 1410 |    150 |   10.04 |   14.95 | 1000.00 |   1.5 |    0 |       0 |   43.7 |                    |
| 0.0050 |   4 |    8 | 100.0 | 2072 | 1410 |    149 |   10.03 |   14.86 |  200.00 |   7.4 |    0 |       0 |   43.4 |                    |
| 0.0100 |   4 |    8 | 100.0 | 2072 | 1410 |    150 |   10.05 |   14.93 |  100.00 |  14.9 |    0 |       0 |   43.6 |                    |
| 0.0500 |   4 |    8 | 100.0 | 2072 | 1410 |    130 |   10.04 |   12.95 |   20.00 |  64.7 |    0 |       0 |   37.8 |                    |
| 0.1000 |   4 |    8 | 100.0 | 2072 | 1410 |    100 |   10.02 |    9.98 |   10.00 |  99.8 |    0 |       0 |   29.2 |                    |
| 0.5000 |   4 |    8 | 100.0 | 2072 | 1410 |     20 |   10.00 |    2.00 |    2.00 | 100.0 |    0 |       0 |    5.8 |                    |
| 1.0000 |   4 |    8 | 100.0 | 2072 | 1410 |      9 |   10.26 |    0.88 |    1.00 |  87.7 |    0 |       0 |    2.6 |                    |
| 0.0010 |   1 |   16 | 100.0 | 8288 | 5644 |      9 |   10.26 |    0.88 | 1000.00 |   0.1 |   23 |       0 |   82.1 |                    |
| 0.0050 |   1 |   16 | 100.0 | 8288 | 5644 |      9 |   10.18 |    0.88 |  200.00 |   0.4 |   22 |       0 |   82.7 |                    |
| 0.0100 |   1 |   16 | 100.0 | 8288 | 5644 |      9 |   10.01 |    0.90 |  100.00 |   0.9 |   22 |       0 |   84.1 |                    |
| 0.0500 |   1 |   16 | 100.0 | 8288 | 5644 |      9 |   10.17 |    0.89 |   20.00 |   4.4 |   22 |       0 |   82.8 |                    |
| 0.1000 |   1 |   16 | 100.0 | 8288 | 5644 |      9 |   10.27 |    0.88 |   10.00 |   8.8 |   23 |       0 |   82.0 |                    |
| 0.5000 |   1 |   16 | 100.0 | 8288 | 5644 |     12 |   10.53 |    1.14 |    2.00 |  57.0 |    9 |       0 |  106.7 |                    |
| 1.0000 |   1 |   16 | 100.0 | 8288 | 5644 |      8 |   11.01 |    0.73 |    1.00 |  72.6 |    0 |       0 |   68.0 |                    |
| 0.0010 |   2 |   16 | 100.0 | 4144 | 2822 |     39 |   10.20 |    3.82 | 1000.00 |   0.4 |   82 |       0 |   89.4 |                    |
| 0.0050 |   2 |   16 | 100.0 | 4144 | 2822 |     36 |   10.18 |    3.54 |  200.00 |   1.8 |   94 |       0 |   82.7 |                    |
| 0.0100 |   2 |   16 | 100.0 | 4144 | 2822 |     41 |   10.17 |    4.03 |  100.00 |   4.0 |   69 |       0 |   94.3 |                    |
| 0.0500 |   2 |   16 | 100.0 | 4144 | 2822 |     41 |   10.20 |    4.02 |   20.00 |  20.1 |   72 |       0 |   94.0 |                    |
| 0.1000 |   2 |   16 | 100.0 | 4144 | 2822 |     43 |   10.21 |    4.21 |   10.00 |  42.1 |   58 |       0 |   98.5 |                    |
| 0.5000 |   2 |   16 | 100.0 | 4144 | 2822 |     21 |   10.50 |    2.00 |    2.00 | 100.0 |    0 |       0 |   46.8 |                    |
| 1.0000 |   2 |   16 | 100.0 | 4144 | 2822 |      9 |   10.35 |    0.87 |    1.00 |  87.0 |    0 |       0 |   20.3 |                    |
| 0.0010 |   4 |   16 | 100.0 | 2072 | 1410 |    115 |   10.09 |   11.40 | 1000.00 |   1.1 |    0 |       0 |   66.6 |                    |
| 0.0050 |   4 |   16 | 100.0 | 2072 | 1410 |     90 |   10.08 |    8.93 |  200.00 |   4.5 |    0 |       0 |   52.2 |                    |
| 0.0100 |   4 |   16 | 100.0 | 2072 | 1410 |    116 |   10.01 |   11.59 |  100.00 |  11.6 |    0 |       0 |   67.7 |                    |
| 0.0500 |   4 |   16 | 100.0 | 2072 | 1410 |    114 |   10.08 |   11.31 |   20.00 |  56.5 |    0 |       0 |   66.1 |                    |
| 0.1000 |   4 |   16 | 100.0 | 2072 | 1410 |    100 |   10.02 |    9.98 |   10.00 |  99.8 |    0 |       0 |   58.3 |                    |
| 0.5000 |   4 |   16 | 100.0 | 2072 | 1410 |     20 |   10.01 |    2.00 |    2.00 |  99.9 |    0 |       0 |   11.7 |                    |
| 1.0000 |   4 |   16 | 100.0 | 2072 | 1410 |      9 |   10.35 |    0.87 |    1.00 |  87.0 |    0 |       0 |    5.1 |                    |
+--------+-----+------+-------+------+------+--------+---------+---------+---------+-------+------+---------+--------+--------------------+
```

### Reading the numbers

- **Column meanings**: `fps` = frames/elapsed over the measurement
  window; `expFPS` = 1/exptime; `eff%` = fps/expFPS; `drop` = frames
  the server produced but the client never saw (gaps in `seq`);
  `enodata` = `next` calls that timed out; `MB/s` = pixel payload
  received per second.
- **Gigabit Ethernet is the bottleneck for large frames**: every fast
  config plateaus at ~95–108 MB/s (wire speed). That caps bin 1 8-bit
  at ~2.1 fps (46.8 MB/frame), bin 1 16-bit at ~0.9 fps, and bin 2
  8-bit at ~8.4 fps regardless of exposure time. Non-zero `drop`
  counts appear in exactly those network-limited configs: the server's
  two rotating buffers overwrite frames faster than the client drains
  them.
- **Bin 4 is camera-limited**: it tops out at ~15 fps (8-bit) /
  ~11.5 fps (16-bit) while moving only 40–68 MB/s with zero drops, so
  the ceiling there is sensor readout / USB bandwidth on the server
  host, not the network.
- **~0.87 fps at 1.0 s exposure** across all bins (0.5 s reaches 100%
  efficiency) suggests a fixed per-frame overhead that only becomes
  visible at long exposures — unexplained, worth a closer look.

## ROI sweep — can a small window reach 200 Hz?

Motivation: acquire with the full detector, then read a small window
around the guide star as close to 200 Hz as possible (fast-guiding /
ground-layer PSD study). Question: is the small-window limit the
1 Gbps link, or is there per-frame overhead?

Sweep run 2026-07-08, same setup as above (`--rois 10,50,80,100`,
exposures 0.5–50 ms, bins 1–2, 8/16 bit; full data in
`results_roi.csv` on zwo-bootsrv), plus a tiny-window run
(`--rois 1,2,5`). FPS ceilings observed (exposure short enough not to
matter, 8-bit; 16-bit gives the *same* fps wherever the wire isn't
saturated):

```
bin 1:  80x56 -> 105 fps   160x112 -> 86 fps   408x282 -> 56 fps
        824x564 -> 35 fps  4144x2822 -> 8.8 fps (wire-limited)
bin 2:  80x56 -> 194 fps   200x140 -> 193 fps  408x282 -> 135 fps
        2072x1410 -> 32 fps  4144x2822 -> 8.1 fps (wire-limited)
```

Findings (after the 2026-07-08 server fixes, see below):

- **Small windows never touch the wire** (a 10% bin-1 window moves
  only 16 MB/s at its 35 fps ceiling); the limit is per-frame
  overhead, answering the original question: ROI cut-off has real
  overhead, the 1 Gbps link is only the limit above ~50% window.
- **The overhead is linear in window height** plus a fixed floor.
  Bin 1 fits `period ~ 7.4 ms + 38 us/row`; readout (camera/USB side),
  not network. Bit depth is free below the wire limit — 8 and 16 bit
  reach identical fps, so the USB side appears to always move 16-bit
  pixels.
- **The ~194 fps ceiling is the camera itself** (~5.15 ms minimum
  frame period at bin 2 small ROI, SDK 1.20.2, default USB bandwidth).
  It was first thought to be the server's 5 ms `next` poll, but
  shrinking that to 1 ms did not move it, and with zero seq gaps the
  client provably receives every frame the camera produces.
- **200 Hz verdict: reachable at bin 2** with windows up to ~200x140
  binned pixels: 192-194 fps (96% efficiency) at 5 ms exposure, zero
  drops. At bin 1 even 80x56 only reaches ~105 fps.
- **Untested lever**: `ASI_BANDWIDTHOVERLOAD` (USB bandwidth %, SDK
  default 40) is not exposed by zwoserver; raising it should shrink
  the per-row readout cost and lift the camera-limited ceilings
  above (including the ~5.15 ms floor).

### Server fixes this benchmark motivated (zwoserver.c, 2026-07-08)

- `TCP_NODELAY` on client sockets: cured a Nagle/delayed-ACK collapse
  for frames under a few KB (40x28 bin 2 went from ~23 fps with 40 ms
  stalls to 194 fps).
- `next` poll quantum 5 ms -> 1 ms (removes 0-5 ms of per-frame jitter).
- **aarch64 memory-ordering bug fixed**: `run_video` shared the video
  buffer pointers and `video_seq` with the connection thread through
  plain globals. On x86 (original deployment) this was benign; on the
  Raspberry Pi the consumer could see `video_seq` tick before the new
  buffer pointers, memcpy from a freed buffer, and crash the server
  (SEGV confirmed by gdb backtraces). Buffers are now allocated in the
  `start` handler on the connection thread and the seq handoff uses
  `__sync_synchronize()` barriers. Faster command turnaround from
  TCP_NODELAY is what exposed this latent bug.
- Video thread lifetime serialized across stop/start, SDK buffers
  padded (`SDK_BUF_PAD`), 350 ms settle after `ASIStopVideoCapture`,
  and `Restart=on-failure` in `zwo.service`.
- Validated: 4 consecutive full 96-config sweeps + tiny-ROI set
  (396 configs, ~100 stop/setup/start transitions) with zero failures
  and zero crashes under gdb.

## TODO

Sweep `ASI_BANDWIDTHOVERLOAD` once `zwoserver.c` exposes a command to
set it (currently only exposure/gain/offset/cooler are wired).
