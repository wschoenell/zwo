# Verifying frame-timestamp synchronization across ZWO guiders

**Date:** 2026-07-09
**Purpose:** Validate that independently-hosted ASI294MM Pro guiders
(one Raspberry Pi each) have frame timestamps aligned to ≲1 ms, so
their small-ROI 193 Hz streams can be cross-correlated for fast
tip-tilt / ground-layer seeing. Written for the current two-guider
setup; the method scales to N cameras (e.g. a third on Clay's AUX2).
Companion to
[ASI294MM-P_200Hz_ROI_report.md](ASI294MM-P_200Hz_ROI_report.md).

## Why host timestamps alone are not enough

Our server stamps each frame with `CLOCK_REALTIME` at USB delivery
(zwoserver ≥ 1.0.5). That gives excellent *precision* — 59 µs rms
frame-interval jitter, measured — but says nothing about two things
that a cross-camera measurement depends on:

1. **Absolute offset.** The stamp is taken *after* readout + USB
   transfer, an unknown, mode-dependent delay from the true photon
   epoch. The AO-camera literature is explicit that this latency is
   **not a fixed constant** — it varies with readout mode, ROI, and
   trigger [Kulcsár et al. 2018 SPIE 10703]. So the offset can differ
   between two cameras even if both are configured "the same."
2. **Host-clock alignment.** The two Pis' clocks must agree.
   NTP-disciplined PC clocks have been measured wrong by **tens to
   hundreds of ms** in real astronomy software (802 ms, 79 ± 17 ms,
   up to 1 s) [Barry et al. 2015 PASA 32 e014]. On our own camera host
   we already found `NTPSynchronized=no` with a free-running clock.

The established fix in every relevant community — occultation timing
(IOTA), high-speed photometry, adaptive optics — is an **independent
optical check with a GPS-referenced light source**, never trusting the
host clock at the ms level without it [Barry 2015; Dhillon et al. 2021
MNRAS, HiPERCAM; Layden, Burdge et al. 2026 PASP, proto-Lightspeed].

## Precedent (what the literature achieves)

| System | Method | Accuracy | Source |
|---|---|---|---|
| proto-Lightspeed (**Magellan Clay**, our site) | GPS-PPS hardware trigger + TTL end-of-readout time-tag; validated on-sky vs Crab pulsar | **≤30 µs absolute** | Layden/Burdge 2026 PASP (arXiv:2601.16268) |
| HiPERCAM | GPS PPS-driven LED on focal-plane mask, phase-folded on 1 s | **~100 µs** | Dhillon 2021 (arXiv:2107.10124) |
| proto-Lightspeed lab | GPS-PPS-pulsed LED, mid-exposure recovery | **<50 µs** (LED-rise-limited) | arXiv:2601.16268 |
| SEXTA (occultation) | 500-LED sweep, 1 lit/2 ms from UTC second | **2 ms** frames; device <0.2 ms to UTC | Barry 2015 (arXiv:1503.05705) |

Key architectural lesson from multi-camera AO (CANARY on-sky, DRAGON
lab): align **exposure mid-points**, not data-arrival times — two
cameras with different readout times expose at different epochs even
with identical timestamps; both systems switched from aligning
pixel-arrival to a shared trigger for this reason [Basden et al.,
arXiv:1603.07527].

**Caveat on transfer:** those sub-100 µs numbers are what *dedicated
GPS-trigger hardware* achieves. Our server-side-over-USB approach will
be worse; how much worse for the ASI294MM Pro at 193 Hz is
uncharacterized in the literature and is exactly what this test
measures.

## Recommended test — shared GPS-PPS LED, phase-folded

A single GPS-PPS-driven LED placed so **both cameras image it
simultaneously**. This is the cheapest recipe from the literature and
uniquely fits our need: because both cameras see the *same physical
flash*, it measures their **relative** alignment directly — which is
what cross-correlation needs (relative, not absolute UTC) — and it
validates the host-clock discipline (NTP vs PTP) end-to-end.

### Hardware

**Any cheap GPS module with a PPS pin is sufficient.** The PPS edge of
even a bargain u-blox receiver is accurate to tens of ns against UTC —
~10⁵× below our 1 ms goal — so the GPS is never the limiting factor;
LED rise time and the camera are. Do **not** pay for a timing-grade
receiver for *this* test (that class matters only for host NTP/PTP
discipline, a separate purchase — see below). Cost-effective options
with a broken-out PPS pin (approx. prices, verify at retailer):

| module | chipset | PPS | ~price | notes |
|---|---|---|---|---|
| **GT-U7** | u-blox NEO-6M | yes | ~$10 | what the Stamp-of-Approval flasher uses; proven for this exact job |
| **VK2828U7G5LF** | u-blox G7020 | yes | ~$12 | tiny, ceramic antenna, UART+PPS |
| BN-220 / BN-880 | u-blox M8 | yes | ~$15–20 | ubiquitous drone module, PPS on a pad |
| ATGM336H | AT6668 (non-ublox) | yes | ~$6 | multi-GNSS, cheapest; PPS output |

- **LED + driver** switched by the PPS edge. A single logic gate /
  MOSFET is enough; keep LED rise time « target (a plain LED is
  <1 µs, far below our 1 ms goal). Optionally stretch the pulse to a
  few ms so it lands cleanly inside one 5 ms frame.
- LED positioned in both cameras' fields (diffuser / shared fiber /
  simply both looking at the same lab wall spot). No optical precision
  needed — only that both see the same on/off transition.

**Strongly consider not building this from scratch:** the occultation
community's **Stamp of Approval** (ChasinSpin, MIT-licensed open
hardware) is *exactly* this device — GT-U7 GPS + constant-current LED
driver, hardware-gated so the GPS→LED delay is ~53 ns, purpose-built
to test camera frame-timestamp accuracy (validated to ≤0.1 ms at
30 fps). Either buy/build it as-is, or lift its LED-driver + PPS-gating
schematic. https://github.com/ChasinSpin/StampOfApproval

*Separate concern — host clock discipline:* for continuously
disciplining the two Pi clocks (NTP/PTP, the ansible task), a
timing-grade receiver like the **u-blox NEO-M8T GNSS Timing HAT**
(~$50) is the right class — it has the single-satellite timing mode
the navigation modules above lack. That is not needed for the flash
test but is the module to standardize on for the sync rollout.

### Capture

Use the existing benchmark's timestamp path — no new capture code:

```
# on each Pi host, simultaneously, small ROI around the LED:
zwo_benchmark --host <cam> --bins 2 --bits 8 --rois 5 \
    --exptimes 0.005 --duration 120 -v 2> cam<N>_dt.log
```

`--verbose` already logs per-frame `ts=<ns>` (the server
CLOCK_REALTIME stamp) plus the ROI pixels are in the stream. For this
test add a tiny capture that records, per frame, `(ts_ns, mean_ROI_flux)`
— the LED shows up as a flux step. *(If preferred I can add a
`--flux-log` option that appends mean-ROI intensity to the verbose
line; ~10 lines.)*

### Analysis

1. In each camera's series, find the frame where LED flux crosses
   its half-max — that frame's `ts_ns` is the camera's measured "LED
   lit" time.
2. **Relative alignment** = difference between the two cameras'
   crossing timestamps, per PPS second. Fold over many seconds (120 s
   → ~120 flashes) → mean gives the fixed inter-camera offset,
   scatter gives the alignment jitter. Target: |mean| and σ both
   ≲ 1 ms.
3. **Absolute check** (optional, needs the GPS UTC): each camera's
   crossing `ts_ns mod 1 s` vs 0 → the host-clock-to-UTC error,
   directly exposing an NTP offset like the ones the papers document.
4. Sub-frame interpolation: with a ~ms LED pulse and 5 ms frames,
   interpolate the flux rise across 1–2 frames to beat the 5 ms
   sampling — this is the phase-folding step HiPERCAM/proto-Lightspeed
   use to reach µs from coarse frames.

### Pass criteria

- Inter-camera offset stable and < 1 ms after whatever clock
  discipline is deployed (NTP now; PTP later — see below).
- Offset **repeatable** across ROI/exposure changes, or if not,
  characterized as a lookup (the mode-dependent-latency pitfall).

## Open questions this test answers (added to the report)

1. Relative timestamp accuracy of the ASI294MM Pro server-timestamped
   at USB delivery vs a hardware trigger — the number no published
   source provides for this chip.
2. Rolling-shutter row-dependent exposure offset across the small ROI
   at 5 ms: fixed per-ROI constant, or a row-by-row effect that
   matters for correlation?
3. Does the chosen clock discipline actually deliver sub-ms host
   alignment in the field? The shared PPS-LED flash is the independent
   validator. Preferred discipline is per-host GPS, not PTP — see the
   clock-architecture section (Pi 4B has no hardware PTP timestamping).
4. Is relative alignment sufficient for the science, or is absolute
   UTC also needed? The shared LED gives relative directly; absolute
   needs the GPS UTC reference.

## Host clock architecture — per-host GPS beats PTP here

Preferred design: **give each guider Pi its own GPS+PPS discipline**
(one u-blox NEO-M8T GNSS Timing HAT per host, ~$50, + gpsd + chrony:
PPS on a GPIO via `dtoverlay=pps-gpio`, NMEA as the coarse anchor).
This locks each Pi's `CLOCK_REALTIME` — the exact clock zwoserver
stamps read — to GPS/UTC at ~1 µs. Two (or three) independently
GPS-locked Stratum-1 clocks are then mutually aligned to ~µs **with no
PTP or NTP between the hosts at all**, which is cleaner than a
grandmaster/client link and scales identically to the AUX2 camera.
The same HAT's PPS also drives the flash-test LED, so one part serves
both roles.

Why not PTP-between-hosts: the **Raspberry Pi 4 Model B NIC
(BCM54213PE) has no hardware timestamping**, so a Pi 4 can only run
`ptp4l` in software-timestamping mode (tens of µs, jittery) — worse
than per-host GPS. Hardware PTP grandmaster capability (a disciplinable
PHC with a PPS-sync pin) exists only on the **CM4** (BCM54210PE) and
**Pi 5** [Geerling 2022; jclark rpi-cm4-ptp-guide]. Confirm each host
with `ethtool -T eth0` (look for `hardware-transmit`/`hardware-receive`)
and `cat /proc/device-tree/model`. Only if the hosts are CM4/Pi 5 is
hardware PTP worth considering over per-host GPS — and even then, with
per-host GPS the cameras are already aligned, so PTP buys little.

## Relation to other work

- Continuous host time sync (chrony/PTP) is a prerequisite and is
  tracked separately for the ansible rollout (see project memory).
- If sub-ms proves impossible over USB, the fallback is the
  proto-Lightspeed architecture (GPS PCIe/HAT with PPS trigger + TTL
  end-of-readout tag) — same telescope, proven ≤30 µs — at the cost of
  added hardware and losing the ZWO's simple USB streaming.

## Primary sources

- Barry et al. 2015, PASA 32 e014 — SEXTA (arXiv:1503.05705)
- Dhillon et al. 2021, MNRAS — HiPERCAM timing (arXiv:2107.10124)
- Layden, Burdge et al. 2026, PASP — proto-Lightspeed, Magellan Clay
  (arXiv:2601.16268)
- Basden et al. 2016 — CANARY/DRAGON WFS sync (arXiv:1603.07527)
- Kulcsár et al. 2018, SPIE 10703 — WFS camera latency measurement
- Geerling 2022 — PTP hardware timestamping on the Pi CM4 (jeffgeerling.com)
- jclark, rpi-cm4-ptp-guide — CM4/CM5 hardware PTP + PPS-disciplined PHC
