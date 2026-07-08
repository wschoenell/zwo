# ZWO Server: per-frame timestamps with ns precision — Plan

## Context

PSD / fast tip-tilt work ([docs/ASI294MM-P_200Hz_ROI_report.md](../ASI294MM-P_200Hz_ROI_report.md))
needs per-frame timing. Today clients can only timestamp frames on
arrival, which folds network and protocol jitter into the timing data
and mis-times the ~65 ms SDK lost-wakeup stall frames. A server-side
timestamp taken the moment `ASIGetVideoData` returns removes every
client-side jitter source and lets two guiders be cross-correlated on
absolute time.

## Protocol change (backwards compatible)

`next` response gains a 4th header field:

```
old:  "seq temp power\n"        + pixel data
new:  "seq temp power ts_ns\n"  + pixel data
```

`ts_ns` = `CLOCK_REALTIME` in integer nanoseconds at the instant the
server received the frame from the SDK.

**Compatibility audit (all in-repo `next` consumers):**

| client | parsing | verdict |
|---|---|---|
| gcam ([zwotcp.c:409](../../src/gcam/zwotcp.c#L409)) | `sscanf("%u %lf %d")`, checks `n == 3` | prefix parse; extra token ignored, `n` stays 3 — **unaffected, no change needed** |
| ZWOFinder (ZWO.m) | does not use `next` (still-image mode) | unaffected |
| src/py/zwoclient.py | does not use `next` (temp/fan only) | unaffected |
| src/benchmark | ours | updated to parse the new field |
| src/py/zwo_emulator.py | ours | updated to emit the new field |

Bump `P_VERSION` 1.0.4 → 1.0.5 so clients can feature-detect via the
existing `version` command.

## Timestamp semantics (document in code and README)

- **Clock**: `CLOCK_REALTIME`, ns. Chosen over MONOTONIC so the two
  guiders (separate hosts, NTP-synced — `zwo.service` orders after
  `ntpdate`) can be cross-correlated on absolute time. Caveat: not
  guaranteed monotonic under NTP steps.
- **Epoch marked**: end of USB delivery of the frame (when
  `ASIGetVideoData` returns), not exposure start. The offset from
  exposure start (readout + USB transfer) is constant per
  configuration, so PSD shapes are unaffected.
- **Stall caveat**: for the ~0.1% lost-wakeup stall frames the frame
  was exposed on schedule but delivered late — its timestamp is late
  by the stall duration (~65 ms). These frames are identifiable as
  Δts spikes and should be masked, exactly as arrival-time analysis
  already requires.

## Implementation

### Server — src/server/zwoserver.c

1. `static unsigned long long video_ts[2];` next to `video_data1/2`
   (`[0]` ↔ `video_data1`, `[1]` ↔ `video_data2`), plus a small
   `time_ns()` helper using `clock_gettime(CLOCK_REALTIME,...)`.
2. `run_video`: on `ASI_SUCCESS`, store `time_ns()` into the slot
   matching the buffer just written, **before** the existing
   `__sync_synchronize()` that precedes `video_seq++` — the current
   release/acquire pair then covers the timestamp exactly like the
   pixel data (no new synchronization needed).
3. `next` handler: pick the slot with the same parity expression used
   for the data pointer and append `%llu` to the answer.
4. `P_VERSION` → "1.0.5" in zwo.h.

### Emulator — src/py/zwo_emulator.py

5. Append `time.time_ns()` to the `next` response line.

### Benchmark — src/benchmark/zwo_benchmark.c

6. `next_frame()` parses the optional 4th field (`ts_ns = 0` when
   absent → works against old servers).
7. `--verbose` prints both the client-side `dt` and the server-side
   `dts` per frame — separating camera timing from protocol/network
   jitter is precisely the measurement the PSD work needs.

### Not in scope (note as future)

Timestamping the single-exposure path (`ASIGetDataAfterExp`) and a
FITS keyword in `write` — same pattern, add when needed.

## Edge cases

- Answer-line length grows ~20 chars: server composes into
  `buf[256]` in `run_connection`, clients read into ≥128-byte
  buffers; `"4294967295 -12.3 100 1783512345678901234"` ≈ 42 chars —
  fine everywhere.
- `%llu` on aarch64/x86-64: fine; no 32-bit targets remain.
- Old benchmark binaries against the new server: prefix parse, works.
- New benchmark against an old server: `sscanf` yields 3 fields,
  `ts_ns` stays 0, verbose prints `dts=0` — degrade gracefully.

## Verification

1. **Emulator dry-run**: two configs; assert `ts_ns` strictly
   increasing and `Δts ≈ 1/fps`.
2. **Compat proof**: run the *pre-change* benchmark binary (kept on
   zwo-bootsrv) against the new server — must parse and stream with
   zero drops. gcam needs no rebuild (audit above); optionally smoke
   test it.
3. **Real camera**: at the 193 Hz operating point (bin 2, 200×140,
   5 ms), compare server-side `Δts` scatter vs client-side `Δt`
   scatter — expect σ(Δts) < σ(Δt) = 0.35 ms, confirming the
   protocol-jitter removal. Stall frames must show matching ~65 ms
   spikes in both.
4. **Regression**: full default sweep (42 configs) clean, zero
   crashes — no state-machine changes are made, so risk is low.
5. Update [src/benchmark/README.md](../../src/benchmark/README.md)
   and the tip-tilt report with the server-timestamp jitter numbers.

## Critical files

- **Modify**: [src/server/zwoserver.c](../../src/server/zwoserver.c),
  [src/server/zwo.h](../../src/server/zwo.h) (version),
  [src/benchmark/zwo_benchmark.c](../../src/benchmark/zwo_benchmark.c),
  [src/py/zwo_emulator.py](../../src/py/zwo_emulator.py)
- **Audited, unchanged**: [src/gcam/zwotcp.c](../../src/gcam/zwotcp.c),
  src/finder/ZWOFinder/ZWO.m, src/py/zwoclient.py

## Execution order

1. **First**: branch `plan/zwo-frame-timestamp` off
   `plan/zwo-benchmark` (this builds on the barrier/buffer work not
   yet on `main`), commit this plan standalone
   (`plan: add per-frame ns timestamps to zwo server`).
2. `step-01-server`: zwoserver.c + zwo.h + emulator; emulator
   verification.
3. `step-02-benchmark`: client parsing + verbose `dts`; deploy to
   10.8.80.225 and run verification 2–4 (build/restart on the camera
   host per the standing authorization).
4. Docs updates (README + report rev. 4).
