# gcam: image server — Plan (pull, multi-client, FITS only)

**Supersedes** [gcam-image-streaming.md](gcam-image-streaming.md) (serving
on the command port, single client) and
[gcam-multi-send-targets.md](gcam-multi-send-targets.md) (pushing FITS to
N receivers). Both remain readable for the analysis that led here; neither
was implemented.

**Status:** for review. Nothing built yet. The one prerequisite,
per-frame timestamps in `ZwoFrame`, is done (PR #21).

## What this is

A dedicated TCP port on gcam that any number of clients can connect to
and pull FITS frames from. gcam serves "the frame the guider actually
used" — `>>2`-shifted, optionally rolling-averaged — with the guider,
telescope and camera state carried in the FITS header.

**One verb. One format.**

## Why pull, and why its own port

**Pull.** Every hard problem in the multi-send plan existed because gcam
would initiate connections to peers it does not control: unbounded
`connect()` ([tcpip.c:246](../../src/gcam/tcpip.c#L246) has the limiter
left `#if 0`), `gethostbyname()` under `tcpip_mutex`, per-target backoff,
a receiver list in the `.ini`, and — worst — a dead receiver stalling the
acquisition thread while holding a ring-buffer `rlock`
([zwogcam.c:2068-2084](../../src/gcam/zwogcam.c#L2068-L2084)). If gcam
never dials out, none of that exists.

**Its own port.** Concurrency on the *command* port is expensive because
`handle_command()` writes every reply into the single shared
`g->command_msg` ([zwogcam.c:1274](../../src/gcam/zwogcam.c#L1274)), so
concurrent callers corrupt each other. A separate port never enters
`handle_command()`, so thread-per-connection is free: `XInitThreads()` is
already called ([zwogcam.c:437](../../src/gcam/zwogcam.c#L437)) and the
frame ring already refcounts readers.

## Protocol

Port `52300+gnum` by default, `.ini` key `image_port` (0 disables).

```
fits [timeout]   ->  "<seq> <ts_ns> <nbytes>\n"  + nbytes of FITS
```

- `timeout` — seconds to wait for a frame **newer than the last one
  served on this connection**. Default 0: return the newest frame
  available immediately, even if already served.
- Nothing new within `timeout` → `-Enodata`.
- Any other failure → `-E<message>`, gcam's existing convention.

A client is: send a line, read a line, parse three integers, read exactly
`nbytes`, hand the buffer to any FITS reader.

The three preamble fields are all repeated inside the header. They stay
because `nbytes` is needed for framing regardless, and having `seq` and
`ts_ns` in the line lets a client detect dropped frames and measure
latency without instantiating a FITS parser.

### Why FITS only

Raw pixels were the second format in the previous draft. Dropping them
removes more than a verb:

- **The out-of-band contract disappears.** Raw meant the client had to be
  told width, height, bit depth, byte order and the `>>2` scaling
  convention separately — exactly the coupling that made `open`/`setup`
  necessary. FITS states all of it in the file.
- **One code path.** `fits_send()` already exists and is already used by
  the `send` push. No second serializer to write or keep in step.
- **Every consumer already reads it** — astropy, ds9, IRAF — so a
  quicklook client is a few lines rather than a bespoke reader.
- **A geometry change mid-session is self-describing.** An operator
  running `dspi` with a new config changes `NAXIS1`/`NAXIS2` in the next
  frame instead of silently corrupting a client that cached dimensions.

Cost: the FITS header per frame (see below) against a ~4.6 MB image —
0.1%. Not a consideration.

## The header carries the full guider state

Everything the command port's `status` reply returns goes into the FITS
header, so a saved frame is self-contained: what the guider was doing,
what it measured, and what it commanded, at the moment of that exposure.

Already present: `exptime` → `EXPTIME`, `gain` → `GAIN`, `temp` →
`TEMP_CCD`, `px` → `SCALE`, plus the telescope block that
`update_status()` already fills.

To add — 26 from `status` plus the frame timestamp:

| status | FITS | | status | FITS |
|---|---|---|---|---|
| `init` | `GDINIT` (T/F) | | `fwhm` | `GDFWHM` |
| `loop` | `GDLOOP` (T/F) | | `flux` | `GDFLUX` |
| `guiding` | `GDGUIDE` | | `peak` | `GDPEAK` |
| `gm` | `GDMODE` | | `back` | `GDBACK` |
| `fm` | `GDFMODE` | | `dx` | `GDDX` |
| `mm` | `GDMMODE` | | `dy` | `GDDY` |
| `av` | `GDAVG` | | `az` | `GDAZ` |
| `offset` | `CCDOFFS` | | `el` | `GDEL` |
| `setp` | `TEMPSET` | | `x` | `GDBOXX` |
| `cooler` | `COOLER` | | `y` | `GDBOXY` |
| `cfps` | `CAMFPS` | | `bx` | `GDBOXSZ` |
| `gfps` | `GDFPS` | | `pa` | `GDPA` |
| `send` | `GDSEND` | | `sn` | `GDSENS` |
| — | `FRAMETS` (ns) | | | |

Names are 8 characters or fewer as FITS requires; the exact spellings are
worth settling in review, since they become a data format.

### Structural consequence: the header needs a third block

`FITSNBLKS` is 2, giving `FITSLINES` = 72 cards
([fits.h:26-28](../../src/gcam/fits.h#L26-L28)). The `fits_headers` enum
uses 49 including `END`, leaving 23 free — and 27 are needed. **`FITSNBLKS`
must go 2 → 3**, giving 108 cards with 32 spare.

The header grows 5760 → 8640 bytes. Both readers in the tree loop over
blocks until they hit `END` — `fits_get_info()`
([fits.c:407](../../src/gcam/fits.c#L407)) and `getimages.c`, which reads
to EOF — so neither assumes the current size.

### Single source of truth

The guider fields must be latched under `g->mutex` exactly as the
`status` command does. Factor that latch into one function used by both
`status` and the header builder, so the text reply and the FITS header
can never drift apart. Without this they are two hand-maintained lists of
the same 26 values.

### Blast radius: `fits_setup()` is shared by three paths

`fits_setup()` serves the new server, the `send` push, **and** the `write`
command's disk output ([zwogcam.c:2230](../../src/gcam/zwogcam.c#L2230)).
Two consequences:

- Files written to disk gain the full guider state too — a straight
  benefit, and the easiest place to validate the new keywords.
- **One client is still consuming the `send` push.** Bumping `FITSNBLKS`
  changes the header *length* of every FITS gcam emits, 5760 → 8640
  bytes. A consumer using a real FITS library is unaffected; one that
  reads a fixed-size header, or seeks to data at a hardcoded offset, will
  break silently.

Because of that live consumer, **do not land the header change in the
push path for testing convenience**. Validate it via `write` to disk and
the new server, and only let the push emit it once that client has been
checked or migrated.

## Implementation

### Listener

One detached thread per connection, hard cap on concurrent clients
(4 is ample). Per-connection state is one `u_int last_seq` — a thread
local, not shared. Nothing in the command path changes.

### The one hard rule: copy, release, then send

`zwo_frame4writing()` panics when all three ring buffers are
reader-locked (`PANIC: no frame available for writing` in
[zwotcp.c](../../src/gcam/zwotcp.c)), which stops acquisition and
therefore guiding. Every serving thread must:

```
frame = zwo_frame4reading(server,last_seq)   ->  memcpy pixels + latch status
zwo_frame_release(server,frame)              ->  release immediately
build FITS, send(...)                        ->  socket write, unlocked
```

The ring already refcounts readers, so N serving threads coexist with the
display and guider threads — provided none holds the lock across network
I/O. Note `fits_send()` currently writes row by row into the socket
([fits.c:379](../../src/gcam/fits.c#L379)); that is fine once it is off
the ring lock, but it must never run while holding one.

### Request/response, not a subscription

The client asks when it is ready, so it self-paces. That is what makes a
slow client harmless with no machinery: no queue to bound, no drop policy,
no backpressure detection. A slow client simply asks less often and gets
a newer frame when it returns.

### Resource budget

One reusable buffer per connection, reallocated only on geometry change:
~4.6 MB each, so ~18 MB at a 4-client cap. One client at 10 fps is
~366 Mbit/s, so full-rate multi-client streaming needs GbE and
realistically an ROI or every-Nth-frame parameter. Add it when a client
asks, and log any cap applied rather than silently truncating.

### TCS metadata

`update_status()` opens a TCS connection and issues eight-plus queries
before closing ([zwogcam.c:1974](../../src/gcam/zwogcam.c#L1974)). It must
not run per client per frame. Cache the TCS block and refresh on the
existing `run_tele` cadence (~30 s), shared by all clients.

### Pixel semantics to document, not hide

Served pixels are `>>2`-shifted and, with `av > 0`, rolling-averaged.
gcam cannot serve raw data — it shifts in place on receipt and never
keeps the original. `FRAMETS` is the newest contributing frame's stamp
and `GDAVG` says how many were blended. See also issue #20 on the shift
convention.

## The existing push: keep it working, design it for deletion

One client still uses `send_host`/`send_port`, so the push stays for now.
Removal is expected in a following PR once that client moves to this
server. Do **not** build multi-target push.

Everything below exists so that removal is a mechanical excision rather
than an archaeology exercise.

### What removal will have to touch

| | Sites |
|---|---|
| `Guider` fields | `send_host`, `send_port` ([guider.h:67-68](../../src/gcam/guider.h#L67-L68)), `send_flag` (`guider.h:27`), `sendNumber` (`guider.h:58`) |
| `.ini` keys | `send_host` / `send_port` ([zwogcam.c:2794-2795](../../src/gcam/zwogcam.c#L2794-L2795)) |
| command | `send [n]` ([zwogcam.c:1476](../../src/gcam/zwogcam.c#L1476)), `set_sf()` |
| UI | `dtbox` countdown, `sqbox` counter |
| the push itself | [zwogcam.c:2066-2083](../../src/gcam/zwogcam.c#L2066-L2083) |
| entanglement | `send_flag` is read by the `fm2` stored-mode logic (`stored_send`, [zwogcam.c:1348](../../src/gcam/zwogcam.c#L1348), [1380](../../src/gcam/zwogcam.c#L1380)) |

The `fm2` coupling is the only non-obvious one: F1/F3 save and restore
`send_flag` alongside exposure time and `av`. Removing the push means
removing that from the stored-mode set too.

### Rules to keep it excisable

1. **The new server must not reuse push-owned state.** No borrowing of
   `send_flag`, `sendNumber`, `dtbox` or `sqbox`. The server gets its own
   per-connection sequence and its own `iclients` counter. Sharing a
   counter is what would turn deletion into a refactor.
2. **Shared pieces stay neutral.** `fits_send()` and `update_status()`
   are already shared with the `write`-to-disk path
   ([zwogcam.c:2229-2230](../../src/gcam/zwogcam.c#L2229-L2230)), so
   deleting the push orphans neither. The **TCS metadata cache must be
   owned by the metadata layer, not by the push** — if it were introduced
   inside the send block it would have to be rescued during removal.
3. **Keep the push contiguous and single-entry.** Extract the run_cycle
   block into one `push_frame(g,frame)` with one call site, delimited by
   an explicit `/* legacy push — scheduled for removal */` marker, so the
   excision is a function plus its call site plus the table above.
4. **No new `.ini` keys that overlap.** The server uses `image_port`;
   nothing named `send_*`.

### Do not decouple the push

The superseded plan proposed moving the push off the acquisition thread
to fix its stall-and-hold-`rlock` hazard. If removal is genuinely the next
PR, that work is wasted — the hazard disappears with the code. Skip it,
unless the remaining client's migration is expected to take long enough
that a live guiding risk is worth paying to remove sooner. That is a
judgement call about timelines, not about code.

Note the same pattern exists independently in `run_write`, which holds a
frame `rlock` across `update_status()`'s TCS round-trip and the disk write
before releasing at [zwogcam.c:2245](../../src/gcam/zwogcam.c#L2245). That
one does **not** go away with the push and should be looked at on its own.

## Staging

1. `FITSNBLKS` 2 → 3, the new keywords, and the shared status latch.
   Validate through `write`-to-disk, **not** through the push — there is
   a live consumer on the other end of that socket.
2. Isolate the push into `push_frame()` behind its removal marker, and
   keep it emitting whatever header its remaining client tolerates.
3. Listener, thread-per-connection, client cap, `fits`.
4. Reference client (extend `src/py/zwoclient.py`), emulator coverage,
   docs in `docs/ZWO/zwogcam.html`, and `iport=` / `iclients=` in the
   command-port `status` reply.
5. Migrate the remaining consumer to the server.
6. **Next PR:** delete the push — checklist below.

## TODO: the removal PR

Do not start until the remaining `send_host`/`send_port` consumer is
confirmed migrated. Everything here is deletion; nothing needs designing.

**Code**

- [ ] Delete `push_frame()` and its single call site in `run_cycle()`
      (both inside the `legacy push` markers).
- [ ] Delete the `send` command branch in `handle_command()` and
      `set_sf()`.
- [ ] Delete `Guider` fields `send_host`, `send_port`, `send_flag`,
      `sendNumber`.
- [ ] Remove `send_flag` from the `fm2` stored-mode set: the
      `stored_send` field, and its save/restore in the F1 and F3
      branches. **This is the one non-mechanical edit** — F1/F3 must
      still save and restore exposure time and `av`.
- [ ] Delete the `dtbox` and `sqbox` widgets: struct members, creation,
      layout, and every `CBX_UpdateEditWindow` on them. Check the layout
      still packs correctly with two boxes gone.

**Config and interface**

- [ ] Drop `send_host` / `send_port` from `read_inifile()`, and from the
      `.ini` files under `etc/ini/` that set them.
- [ ] Drop `send=` from the `status` reply, and say so in the release
      notes — it is a published field.
- [ ] Remove the `send` entry from `docs/ZWO/zwogcam.html`.

**Do not touch**

- `fits_send()` and `update_status()` — shared with the `write`-to-disk
  path and with the image server.
- The TCS metadata cache — owned by the metadata layer, not the push.
- `getimages.c` — it is the reference receiver and still useful for
  testing the FITS output.

**After**

- [ ] Confirm `grep -rn "send_host\|send_port\|send_flag\|sendNumber\|
      set_sf\|dtbox\|sqbox\|stored_send" src/gcam/` returns nothing.
- [ ] Rebuild, run against the emulator, verify guiding and `write` still
      work and the layout is intact.

## Open questions

- **Gating: which direction does the network allow?** Pull needs clients
  to open connections *into* the guider host. If only outbound is
  permitted, this plan is void and multi-target push returns despite its
  costs — including for the client that is being migrated.
- **Does the remaining `send` client read the FITS header by keyword or
  by offset?** It decides whether step 2 can simply adopt the new header
  or has to pin the old one until migration.
- **Keyword names** become a data format — worth agreeing before the
  first frame ships.
- **Access control.** The command port never calls
  `TCPIP_AddressCheck()`, unlike the generic server in
  [tcpip.c:534](../../src/gcam/tcpip.c#L534). An inbound multi-client
  port is a larger surface — use the oklist, or stay open as today?
- Does any consumer need the *raw* (unshifted, unaveraged) frame? That
  must be solved in `zwotcp.c` before it is servable at all.
