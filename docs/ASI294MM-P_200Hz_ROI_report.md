# ZWO ASI294MM Pro at 200 Hz — small-ROI streaming test for fast tip-tilt

**Date:** 2026-07-08 (rev. 4: per-frame ns timestamps; true camera jitter = 0.059 ms rms)
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
for an 80×56 window (~+25% with high-speed mode, still well short of
200 Hz). The limit is camera/USB readout, not the network — and both
camera-side levers (USB bandwidth, high-speed mode) have been tested
and do not move the bin 2 ceiling.

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

Measured at bin 2, 200×140, 8-bit, 5 ms exposure, over 30 s
(~5700 frames).

The server (protocol v1.0.5) now stamps every frame with a
nanosecond-precision `CLOCK_REALTIME` timestamp taken the instant the
SDK delivers it, returned in the `next` header. This separates the
camera's true timing from protocol/network jitter (backwards
compatible — existing clients ignore the extra field):

| statistic (30 s @ 193 Hz) | client arrival (Δt) | server stamp (Δts) |
|---|---|---|
| mean interval | 5.19 ms | 5.20 ms |
| σ (core) | 0.52 ms | **0.059 ms** |
| stalls | ~66 ms | ~65.4 ms (identical frames) |

**The camera's true delivery jitter is 59 µs rms** — an order of
magnitude tighter than what client-side arrival times show. PSD work
should use the per-frame timestamps directly; the 0.5 ms client-side
scatter is protocol/network and disappears. Stalls (~65 ms, one per
3–10 s; was ~366 ms before the timeout fix below) appear identically
in both series and are precisely stamped, so masking them is exact.

For cross-camera correlation the two guider hosts should share a
clock discipline: NTP gives ~ms alignment; **PTP (`ptp4l` +
`phc2sys`) brings it to tens of µs** on Pi-class hardware (software
timestamping) and disciplines the same `CLOCK_REALTIME` the stamps
read — no code change needed; switch the server's `TS_CLOCK` define
to `CLOCK_TAI` on PTP hosts to be immune to leap-second steps. The
stamp marks end-of-USB-delivery, a per-configuration constant offset
from exposure start.

The core distribution is tight — 0.35 ms rms is small against a 5 ms
frame period. Frame delivery does stall a few times per minute
(~0.1–0.3% of frames; no frames are lost, delivery just pauses).

**Stall root cause found and mitigated.** The stall duration always
equalled the server's `ASIGetVideoData` timeout (350 ms + exposure):
the SDK's internal circular buffer occasionally misses a wakeup and
sleeps the *full* timeout even though the frame is already available
(a lost-wakeup bug in `CirBuf::ReadBuff`, consistent with the gdb
thread traces). Shrinking the server's timeout floor from 350 ms to
50 ms cut the stalls from **~366 ms to ~65 ms** with no change in
rate, jitter, or drops anywhere in the sweep. At 193 Hz a stall now
costs ~12 frames instead of ~70. The stall *frequency* (one per
3–10 s) is SDK-internal and unchanged — PSDs still need these short
gaps masked, and the timeout can be tuned lower if 65 ms is still too
long.

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

## USB bandwidth (`ASI_BANDWIDTHOVERLOAD`) — tested, verdict negative for small ROIs

The setting is now exposed as the server's `usb` command
(`zwo_benchmark --usb N`, 40–100). Sweep at 40 / 60 / 80 / 100 across
bins 1–2, 8/16-bit, 2–10% windows:

- **No effect on any small-ROI ceiling.** 86.4 / 55.6 / 35.0 Hz (bin 1)
  and 194 / 193 / 135 Hz (bin 2) are identical to 0.1% at every
  bandwidth value. The small-window ceilings — including the 5.15 ms
  bin-2 floor — are **sensor readout timing, not USB transfer**, so
  bandwidth cannot buy 200 Hz at bin 1.
- **Large USB-limited frames do speed up**: bin 4 full-frame 16-bit
  went from 6.8 fps (39 MB/s) at usb=40 to ~10.8 fps (63 MB/s) at
  usb=100. Useful for full-detector acquisition, irrelevant to the
  tip-tilt window.
- Stall frequency and core jitter at the 193 Hz operating point are
  unchanged by the setting.

## High-speed mode (`ASI_HIGH_SPEED_MODE`, 10-bit ADC) — tested

Exposed as the server's `highspeed` command
(`zwo_benchmark --highspeed 0/1`). On/off comparison over the same
matrix:

| config | HSM off | HSM on | change |
|---|---|---|---|
| bin 1, 160×112  | 86.4 Hz | 108.2 Hz | **+25%** |
| bin 1, 408×282  | 55.6 Hz | 69.7 Hz  | **+25%** |
| bin 1, 824×564  | 35.0 Hz | 43.9 Hz  | **+25%** |
| bin 2, 80×56    | 194 Hz  | 194 Hz   | none |
| bin 2, 200×140  | 193 Hz  | 193 Hz   | none |
| bin 2, 408×282  | 135 Hz  | 135 Hz   | none |
| bin 4, full     | 13.4 Hz | 13.1 Hz  | none |

- **Bin 1 readout gets uniformly 25% faster** (row time 38 → 30 µs) —
  but even the smallest window only reaches ~108 Hz, still far from
  200 Hz, and the ADC drops to 10 bits.
- **Bin 2 — the tip-tilt operating point — is completely unaffected**,
  as are bins 4 and both bit depths. The 5.15 ms bin-2 floor stands.

## Remaining headroom

- Both camera-side levers are now exhausted: neither USB bandwidth nor
  high-speed mode moves the bin-2 floor. **~193 Hz at bin 2 with a
  ≤200×140 window is the ceiling for this camera + SDK**, and it meets
  the 200 Hz goal to within 3.5%.
- The per-frame Δt series (benchmark `--verbose`) gives the timing
  data needed for PSD work directly.
- The stall-recovery timeout (now 50 ms) can be tuned lower if needed.

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
4. **`usb` and `highspeed` commands added** (`ASI_BANDWIDTHOVERLOAD`
   40–100, `ASI_HIGH_SPEED_MODE` 0/1).
5. **`ASIGetVideoData` timeout floor 350 ms → 50 ms**: cuts the
   SDK lost-wakeup stalls from ~366 ms to ~65 ms.
6. **Per-frame ns timestamps** (v1.0.5): `next` returns
   `"seq temp power ts_ns"`; backwards compatible (audited against
   all existing clients).
7. `Restart=on-failure` added to the systemd unit.

## Reproducing

```
make -C src/server tcpip.o utils.o ptlib.o && make -C src/benchmark
./zwo_benchmark --host <server> --bins 2 --bits 8 --rois 2,5,10 \
    --exptimes 0.001,0.005 --duration 10 --csv out.csv
```

Full methodology, protocol details, and the complete sweep tables:
[src/benchmark/README.md](../src/benchmark/README.md). Raw CSVs from
all runs are on `zwo-bootsrv:~/zwo-benchmark/`.
