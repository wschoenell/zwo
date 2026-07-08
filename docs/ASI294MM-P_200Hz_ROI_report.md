# ZWO ASI294MM Pro at 200 Hz — small-ROI streaming test for fast tip-tilt

**Date:** 2026-07-08
**Camera:** ZWO ASI294MM Pro (mono, 8288×5644, 12-bit ADC), SDK 1.20.2
**Server:** `zwoserver` on Raspberry Pi 4 (USB3 to camera), gigabit Ethernet
**Client:** Raspberry Pi (`zwo-bootsrv`), same gigabit LAN, RTT 0.32 ms
**Tool:** `src/benchmark/zwo_benchmark` (C client, video mode: `start` → `next` loop → `stop`)

## Question

Can we acquire with the full detector, then stream a small window around
the guide star **as close to 200 Hz as possible**, for tip-tilt / ground
layer PSD studies? Is the limit the 1 Gbps link, or is there per-frame
overhead when cutting an ROI?

All rates below are **end-to-end at the client** (what a tip-tilt loop
would actually see), not camera-internal rates. Every frame is verified
by the server's sequence counter, so drops are counted exactly.

## Answer in one line

**Yes at bin 2: a window up to ~200×140 (binned pixels) streams at
192–194 Hz with zero dropped frames.** Bin 1 tops out at ~105 Hz even
for an 80×56 window. The limit is camera/USB readout, not the network.

## Measured frame-rate ceilings vs window size

Exposure short enough not to matter (0.5–5 ms); 8-bit; each config
measured for 5–20 s; `drops` = frames the camera produced but the
client never received (seq gaps).

| window (binned px) | sensor FOV (px) | bin 1 | bin 2 | drops |
|---|---|---|---|---|
| 40×28    | 80×56    | —        | **194 Hz** | 0 |
| 80×56    | 160×112  | 105 Hz   | **194 Hz** | 0 |
| 200×140  | 400×280  | —        | **193 Hz** | 0 |
| 160×112  | 320×224  | 86 Hz    | —          | 0 |
| 408×282  | 816×564  | 56 Hz    | 135 Hz     | 0 |
| 824×564  | 1648×1128| 35 Hz    | —          | 0 |
| 2072×1410| 4144×2820| 8.8 Hz¹  | 32 Hz      | 0 |

¹ network-limited (103 MB/s); all other rows are camera-limited.

At the 200 Hz target (5 ms exposure), bin 2 with a 200×140 window
delivers **193 fps = 96.5% efficiency, zero drops**, sustained.

## Frame-interval jitter at the operating point

Per-frame arrival intervals (Δt) at bin 2, 200×140, 8-bit, 5 ms
exposure, measured client-side over 20 s (~3900 frames):

| statistic | value |
|---|---|
| mean Δt (core) | 5.19 ms → **192.6 Hz** |
| σ (core) | **0.35 ms** |
| p95 / p99 / max (core) | 5.5 / 5.6 / 6.3 ms |
| stalls | **~366 ms, 2–5 per 20 s** (~0.1% of frames) |

The core distribution is tight — 0.35 ms rms is small against a 5 ms
frame period. However, the camera/USB/SDK side occasionally stalls for
almost exactly 366 ms (a few times per minute; reproduced with all
instrumentation local to the client, so it is not a network artifact;
no frames are lost — delivery just pauses). A tip-tilt loop must
tolerate these ~0.4 s gaps, and they will appear in PSDs as gaps or
must be masked. Origin not yet identified (SDK-internal; possibly its
exposure-control thread) — a follow-up item.

## What sets the limits

- **Not the network.** A bin 1 824×564 window moves only 16 MB/s at
  its 35 Hz ceiling, on a link that sustains ~107 MB/s. The wire only
  becomes the bottleneck above ~50% of the full frame.
- **Readout scales with window height.** Bin 1 fits
  `frame period ≈ 7.4 ms + 38 µs × rows`. Fewer rows → faster, down to
  a floor.
- **A ~5.15 ms/frame floor at bin 2** (the 194 Hz ceiling), identical
  at 8-bit and 16-bit and independent of window size below ~200×140.
  This is camera/USB-side (SDK default USB bandwidth = 40%).
- **Bit depth is free** below the wire limit: 8-bit and 16-bit reach
  identical frame rates (the USB link appears to always carry 16-bit
  pixels). 16-bit is fine for centroiding if wanted.

## Headroom / next steps

- **`ASI_BANDWIDTHOVERLOAD` (USB bandwidth %, SDK default 40) is the
  untested lever.** Raising it should shrink the per-row readout cost
  and the 5.15 ms floor — likely the path to 200+ Hz at bin 1, and
  >200 Hz at bin 2. Requires a small server addition (not yet exposed);
  the benchmark is ready to sweep it.
- `ASI_HIGH_SPEED_MODE` (10-bit ADC) is a second untested lever, at
  some cost in bit depth.
- The per-frame Δt series (benchmark `--verbose`) gives the timing
  data needed for PSD work directly.

## Server fixes made during this test (already deployed)

Running at these rates exposed and fixed several issues in `zwoserver`
on the Raspberry Pi — relevant to anyone reproducing this:

1. **TCP_NODELAY**: sub-few-KB frames collapsed to ~23 Hz with 40 ms
   Nagle/delayed-ACK stalls; now 194 Hz.
2. **`next` poll quantum 5 ms → 1 ms**: removes 0–5 ms of per-frame
   protocol jitter.
3. **aarch64 memory-ordering race fixed**: the video thread published
   buffer pointers without barriers; under fast command turnaround the
   server segfaulted (confirmed by gdb). Benign on the old x86 host,
   fatal on the Pi. Fixed with proper synchronization and validated
   with 4 consecutive full sweeps (396 configs) with zero crashes.
4. `Restart=on-failure` added to the systemd unit.

## Reproducing

```
make -C src/server tcpip.o utils.o ptlib.o && make -C src/benchmark
./zwo_benchmark --host <server> --bins 2 --bits 8 --rois 2,5,10 \
    --exptimes 0.001,0.005 --duration 10 --csv out.csv
```

Full methodology, protocol details, and the complete sweep tables:
[src/benchmark/README.md](../src/benchmark/README.md). Raw CSVs from
all runs are on `zwo-bootsrv:~/zwo-benchmark/`.
