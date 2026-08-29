# gcam: multiple FITS receiving servers — Plan

> **Superseded by [gcam-image-server.md](gcam-image-server.md).** Not
> implemented. Inverting to a pull model removes the problems this plan
> works around, so multi-target push is not being built. Two things here
> still stand: the findings on the current push path, and staging step 1
> (decoupling the existing single push off the acquisition thread), which
> fixes a hazard that exists today.

## Context

gcam can push a FITS frame to exactly one receiver, configured as
`send_host` / `send_port` in the `.ini`
([zwogcam.c:2790](../../src/gcam/zwogcam.c#L2790)) and armed with the
`send` command (`send` = single shot, `send N` = every N seconds).
The push itself is the block at
[zwogcam.c:2066-2083](../../src/gcam/zwogcam.c#L2066-L2083): connect,
`fits_send()`, close — one TCP connection per frame.

The reference receiver `src/gcam/getimages.c` accepts a connection and
reads until EOF, so the receive side is stateless per connection.
**Adding more receivers therefore needs no protocol change at all** —
each target is an independent connect/stream/close. The work is
entirely on gcam's side, and it is not the loop over a list.

## What the investigation found

The current single-target path has four properties that make a naive
"loop over N targets" actively dangerous.

### 1. The send holds a ring-buffer read lock

`zwo_frame_release()` is called at
[zwogcam.c:2084](../../src/gcam/zwogcam.c#L2084) — *after* the whole
send block. So for the entire duration of the TCS query, the connect,
and the pixel transfer, gcam holds one of the `ZWO_NBUFS` = 3 frame
buffers reader-locked. `zwo_frame4writing()` panics when no buffer is
free ([zwotcp.c:673](../../src/gcam/zwotcp.c#L673)), which stops
acquisition and therefore guiding.

### 2. It runs on the acquisition thread

The block is inside `run_cycle()`'s frame loop. Anything slow in the
send stalls the loop that pumps frames to the display and drives the
GUI.

### 3. `connect()` is unbounded

`TCPIP_CreateClientSocket()`
([tcpip.c:201](../../src/gcam/tcpip.c#L201)) does a **blocking
`connect()` with no timeout**, retried **3 times** with 300 ms sleeps
([tcpip.c:231](../../src/gcam/tcpip.c#L231),
[tcpip.c:253](../../src/gcam/tcpip.c#L253)). The one attempt to bound it
is commented out — `#if 0 /* LINUX default=5 (18 seconds) --
non-portable */` ([tcpip.c:246](../../src/gcam/tcpip.c#L246)). It also
holds `tcpip_mutex` across `gethostbyname()`, so an unresolvable name
blocks other users of the library.

A single unreachable `send_host` can therefore stall the acquisition
loop for tens of seconds *per attempted send*, while holding a frame
buffer. That is a pre-existing hazard, not one introduced here.

### 4. `fits_send()` writes synchronously, row by row

Header plus one blocking `send()` per image row
([fits.c:360](../../src/gcam/fits.c#L360),
[fits.c:379](../../src/gcam/fits.c#L379)). A receiver that stops reading
applies TCP backpressure straight into the acquisition thread.

### And `update_status()` costs a TCS round-trip

Called once per send ([zwogcam.c:2067](../../src/gcam/zwogcam.c#L2067)),
it opens a TCS connection and issues eight-plus queries before closing
([zwogcam.c:1985-2004](../../src/gcam/zwogcam.c#L1985-L2004)). Done
per-target it would multiply TCS traffic by N for no benefit — the
header is identical for every target.

## Consequence for the design

Looping over a target list inside the existing block multiplies every
one of the above by N, and makes the ring-buffer lock hold time the sum
of all targets' network latency. **The decoupling is the feature; the
list is the easy part.**

## Proposal

### Architecture

```
run_cycle (acquisition thread)
    frame = zwo_frame4reading(...)
    if any target is due:
        update_status(&g->status,g,frame)     // ONCE per frame
        snap = snapshot_new(frame->data, &g->status)   // one refcounted copy
        for each due target: post(target->queue, snap)
    zwo_frame_release(...)                    // released immediately

sender thread, one per target
    snap = pop(queue)                         // blocking
    sock = connect_with_timeout(host,port)
    fits_send(snap->data,&snap->status,sock)
    close(sock)
    snapshot_release(snap)
    update per-target stats
```

Rules that fall out of the findings:

- **Never hold `rlock` across network I/O.** Copy out of the ring,
  release, then send. Same rule as
  [gcam-image-streaming.md](gcam-image-streaming.md) — it is the same
  underlying hazard.
- **One snapshot per frame, refcounted, shared by all targets.** At
  1512×1512×2 B a frame copy is ~4.6 MB; copying per target would be
  ~4.6 MB × N for no reason, since every target receives identical
  bytes.
- **One `update_status()` per frame**, not per target.
- **Bounded queue, latest-wins, depth 1.** If a target's sender is still
  busy when the next frame is due, replace the queued snapshot rather
  than growing a backlog. A slow or dead target degrades its own
  cadence and affects nothing else. Log the drop — a silent drop reads
  as "delivered".
- **Per-target failure isolation and backoff.** Count consecutive
  failures; after k, back off exponentially to a cap (~60 s) so a dead
  host is not retried every frame.

### Bounded connect (prerequisite)

Even off the acquisition thread, an unbounded `connect()` pins a sender
thread for minutes and makes backoff meaningless. Add a variant of
`TCPIP_CreateClientSocket()` using non-blocking connect plus `select()`
with a few-second timeout (portable), or set `SO_SNDTIMEO` /
`TCP_USER_TIMEOUT`. Leave the existing function alone so other callers
are unaffected.

### Configuration

Backwards compatible — the existing keys keep working and become
target 0:

```
send_host  192.168.1.10        # legacy, still honoured
send_port  5000

send_to    192.168.1.10:5000   # new, repeatable
send_to    archive.lco.cl:5001
send_to    quicklook:5002
```

Parser note: `read_inifile()` reads exactly two tokens per line
(`sscanf(buffer,"%s %s",key,val)`,
[zwogcam.c:2790](../../src/gcam/zwogcam.c#L2790)), so `send_to
host:port` fits the existing parser unchanged. Per-target cadence
(`send_to host:port every=10`) would need a third `%s` — a one-line,
backwards-compatible change — and is worth deferring until someone
actually wants different rates per target.

Cap the list at a small N (4 is ample) so the frame-copy and
thread-count costs stay bounded.

### Command surface

- `send [n]` — unchanged; global cadence / single shot.
- `send list` — one line per target: host:port, enabled, frames sent,
  consecutive failures, backoff state.
- `send on <i>` / `send off <i>` — enable/disable a target at runtime
  without editing the `.ini`.

### Reporting

- `sendNumber` becomes per-target (frames successfully delivered), plus
  a global "frames offered" counter.
- The `sq` box keeps showing the global counter; colour it red when any
  target is in backoff, so a failing receiver is visible without opening
  a terminal.
- The `status` reply (PR #19) gains `targets=<n>` and `tfail=<n>`.

## Staging

1. **Decouple the existing single target** onto a sender thread with
   copy-and-release. Behaviour-preserving, and it removes the
   acquisition stall and the ring-buffer panic risk on its own. Worth
   doing even if multi-target never lands.
2. Bounded connect variant in `tcpip.c`.
3. Generalise to a target array plus `send_to` parsing.
4. Per-target backoff, stats, `send list` / `send on|off`.
5. Optional per-target cadence.

## Semantic changes to be explicit about

Making the send asynchronous changes three visible behaviours:

- The `err` in the existing per-send `printf`
  ([zwogcam.c:2075](../../src/gcam/zwogcam.c#L2075)) is currently the
  synchronous result. It becomes an enqueue result; the delivery result
  arrives later and must be logged from the sender thread.
- `sendNumber` currently increments only on success
  ([zwogcam.c:2080](../../src/gcam/zwogcam.c#L2080)). Per-target
  counters preserve this; the global counter counts frames offered.
- Single-shot (`send_flag < 0`) is reset at
  [zwogcam.c:2081](../../src/gcam/zwogcam.c#L2081) after the send. It
  must reset at *enqueue*, or a failing target would re-arm it.

## Open questions

- Should a target be able to request a different cadence, or is one
  global rate with per-target enable/disable sufficient?
- On total failure of *all* targets, is that an operator-visible alarm
  (red `sq` box plus a message-log warning), or silent?
- Do any receivers need to distinguish which guider sent the frame
  beyond what the FITS header already carries?
- Is connection-per-frame still the right model at higher cadences, or
  should a persistent connection per target be offered? `getimages.c`
  reads until EOF, so a persistent connection would need a receiver
  change — out of scope unless a consumer asks.
