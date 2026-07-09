# Verifying frame-timestamp synchronization between two ZWO guiders

**Date:** 2026-07-09
**Purpose:** Validate that two ASI294MM Pro guiders (separate Raspberry
Pi hosts) have frame timestamps aligned to ≲1 ms, so their small-ROI
193 Hz streams can be cross-correlated for fast tip-tilt / ground-layer
seeing. Companion to
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

- **GPS receiver with 1 PPS output** (e.g. u-blox NEO-M8/M9 breakout,
  ~$20; or a GPS PCIe/HAT if precise absolute UTC is wanted later).
- **LED + driver** switched by the PPS edge. A single logic gate /
  MOSFET is enough; keep LED rise time « target (a plain LED is
  <1 µs, far below our 1 ms goal). Optionally stretch the pulse to a
  few ms so it lands cleanly inside one 5 ms frame.
- LED positioned in both cameras' fields (diffuser / shared fiber /
  simply both looking at the same lab wall spot). No optical precision
  needed — only that both see the same on/off transition.

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
3. Does PTP (vs NTP) between the two Pis actually deliver sub-ms host
   alignment in the field? The shared PPS-LED flash is the independent
   validator. (Note: Pi 4 does software PTP timestamping only; CM4/Pi5
   have hardware PTP [Geerling 2022].)
4. Is relative alignment sufficient for the science, or is absolute
   UTC also needed? The shared LED gives relative directly; absolute
   needs the GPS UTC reference.

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
