# gcam: serving images on the command port — Plan

> **Superseded by [gcam-image-server.md](gcam-image-server.md).** Not
> implemented. This plan assumed a single client, which made the command
> port the cheap option; it also mirrored zwoserver's image verbs, a
> requirement since dropped. Kept for the constraint analysis
> (`command_msg` sharing, the 150 ms per-command floor, copy-and-release).

## Context

`gcamzwo` is today a terminal consumer of frames. It pulls them from
`zwoserver`, processes them, displays them and guides on them. The only
way an image leaves gcam is the `send` push
([zwogcam.c:2066](../../src/gcam/zwogcam.c#L2066)): on the `send`
command it opens a *client* connection to `send_host:send_port`, writes
one FITS via `fits_send()`, and closes. There is no way for a client to
*ask* gcam for an image.

This plan adds `data` and `next` to the existing TCP/IP command port
(52200+gnum), using the same image-transfer format as `zwoserver`.

### Why serve from gcam rather than from zwoserver

1. **zwoserver is single-connection in production.** Its accept loop
   calls `run_connection()` inline and only threads per connection when
   started with `-d` ([zwoserver.c:1016](../../src/server/zwoserver.c#L1016)).
   gcam already owns that one connection while guiding, so a second
   client cannot get frames from that camera.
2. **The pixels differ.** gcam shifts every frame `>>2` on receipt and,
   when `av` (rolling) is non-zero, replaces it with a running average
   ([zwotcp.c:500-520](../../src/gcam/zwotcp.c#L500-L520)). The buffer
   gcam guides on is not what zwoserver sent.
3. **The metadata lives in gcam.** Telescope pointing, airmass, rotator,
   guider probe positions, CA/SH flags and the guider's own measurements
   are assembled in `update_status()`
   ([zwogcam.c:1974](../../src/gcam/zwogcam.c#L1974)).

So gcam serves "the frame the guider actually used", not a raw camera
frame.

## Scope

**In scope:** the **image transfer** matches the zwoserver wire format,
so a client's existing frame-reading code works unchanged. gcam's
command language stays its own — no attempt to make port 52200 a
drop-in zwoserver.

**Out of scope — multiple concurrent clients.** The command port accepts
one connection at a time ([zwogcam.c:2869-2890](../../src/gcam/zwogcam.c#L2869-L2890)),
and so does zwoserver in production. Neither supports it today; this
plan does not change that. Consequences, accepted deliberately:

- While an image client holds the connection, nobody else can connect.
- That client can interleave commands and image reads on its one
  connection — `status`, `fone`, `next`, `data` all work in the same
  session. For a single-client model this is *better* than a separate
  image port would be.
- Threading the accept loop is not a small follow-up: `handle_command()`
  writes its reply into the single shared `g->command_msg`
  ([zwogcam.c:1274](../../src/gcam/zwogcam.c#L1274)), so concurrent
  callers would corrupt each other's answers. Anyone revisiting this
  must fix that buffer first.

## Wire format

Framing, matching zwoserver: one text line terminated by `\n`,
optionally followed immediately by a raw binary blob on the same socket
([zwoserver.c:932-937](../../src/server/zwoserver.c#L932-L937)).

```
data [#]        -> "<nbytes>\n"                      + raw pixels
next [timeout]  -> "<seq> <temp> <cooler> <ts_ns>\n" + raw pixels
```

- `data` serves the most recent frame. `#` limits the number of binary
  bytes sent; `0` sends the header only (zwoserver's testing escape
  hatch, kept).
- `next` waits up to `timeout` seconds for a frame newer than the last
  one served on this connection; `timeout` defaults to 0 (no wait).
  On timeout it returns `-Enodata`, the same string zwoserver uses.
- Errors use gcam's existing `-E<message>` convention, which already
  agrees with zwoserver's `-E` prefix. Not acquiring yields
  `-EZWO not acquiring` via the existing `E_NOTACQ`.
- Pixels are 16-bit native-endian (little on both arm64 and x86-64),
  `w*h*2` bytes, matching `ZwoFrame.data` (`u_short*`).

**Both verb names are free** in gcam's namespace — neither `data` nor
`next`, nor any prefix match against the existing `strncasecmp` verbs
(`dt`, `dspi`, `write`, `start`, `stop`, `send`, `scale`, `smooth`, …),
collides.

`next` needs one piece of per-connection state — the last sequence
number served. With a single client this is a `u_int served_seq` in
`Guider`, reset when `run_tcpip()` accepts a connection.

## Mechanism: the pending blob

`handle_command()` returns text only, in `g->command_msg`. Binary has to
be emitted by the caller that owns the socket, exactly as zwoserver does
with its `asi_data`/`asi_size` pair. Add to `Guider`:

```c
u_char *pending_data;                  /* blob to send after the answer */
size_t  pending_size;
```

Lifecycle, chosen so a stale blob can never be sent and nothing leaks:

- **Cleared at entry** to `handle_command()`, right where `msgstr` is
  already cleared ([zwogcam.c:1274](../../src/gcam/zwogcam.c#L1274)):
  free any previous blob, set to NULL/0. Every command therefore starts
  clean.
- **Populated** only by the `data`/`next` branches.
- **Sent and freed** only by `run_tcpip()`, after `TCPIP_Send()` of the
  text answer.

Keying this off `showMsg` would be wrong: `showMsg == 0` is not
exclusively the TCP path — [zwogcam.c:673](../../src/gcam/zwogcam.c#L673)
is an internal startup call. Clearing at entry is independent of the
caller and needs no such assumption. A GUI user typing `data` simply
sees the byte count in the message line and the blob is dropped on the
next command.

The 150 ms `msleep()` in `handle_tcpip()`
([zwogcam.c:2822](../../src/gcam/zwogcam.c#L2822)) must be skipped for
these two verbs, or throughput caps at 6.7 frames/s.

## Safety: never hold `rlock` across a socket write

The single most important rule. `zwo_frame4writing()` panics when all
three ring buffers are reader-locked
([zwotcp.c:673](../../src/gcam/zwotcp.c#L673)), which stalls acquisition
and breaks guiding. The serving branch must:

```
frame = zwo_frame4reading(server,served_seq)  ->  memcpy into pending_data
zwo_frame_release(server,frame)               ->  release immediately
                                              ->  run_tcpip() sends it unlocked
```

The ring already refcounts readers (`rlock += 1`,
[zwotcp.c:686-710](../../src/gcam/zwotcp.c#L686-L710)), so the serving
path coexists with the display and guider threads — provided it holds
the lock only for the copy.

One full-frame copy per served frame. At 1512×1512×2 B that is ~4.6 MB;
the memcpy is negligible, the network is not — 10 fps is ~366 Mbit/s.
Document that full-rate streaming needs GbE, and consider an
every-Nth-frame or ROI/subsample parameter if a client asks for it.

## Prerequisite: plumb the frame timestamp

To emit the 4-field `next` header, gcam must stop discarding `ts_ns`:
add a `u_int64_t ts_ns` to `ZwoFrame`
([zwotcp.h:25](../../src/gcam/zwotcp.h#L25)) and widen the `sscanf` at
[zwotcp.c:409](../../src/gcam/zwotcp.c#L409) from 3 to 4 fields, keeping
`n >= 3` so an older server still works. Worth doing regardless — it is
what makes gcam's frames cross-correlatable with the work in
[zwo-frame-timestamp.md](zwo-frame-timestamp.md).

Under rolling average (`av > 0`) a served frame blends several; the
header should carry the newest contributing frame's stamp, and the
documentation should state how many were averaged (the `av` value is
already in the `status` reply).

## Geometry discovery

A client needs `w`, `h` and bit depth to interpret the blob. Rather than
adding zwoserver's `open`/`setup` verbs (both names are free, but they
carry camera-control semantics gcam must not expose — the client must
never retune the readout under the guider), extend the existing `status`
reply with `w=`, `h=`, `bits=`. It is already the state channel, and
`bits` is always 16 for gcam's buffers.

## Semantics to document, not hide

- Pixels are `>>2`-shifted and, with `av > 0`, rolling-averaged. gcam
  cannot serve raw data — it shifts in place on receipt and never keeps
  the original. Clients expecting raw 16-bit camera values will be
  wrong.
- A `fits` verb (full FITS with the guider/telescope header, same
  framing) is the natural extension over plain pixels, but it must not
  call `update_status()` per frame — that blocks on the TCS
  ([zwogcam.c:1985-2004](../../src/gcam/zwogcam.c#L1985-L2004)). Cache
  the TCS block and refresh on the existing `run_tele` cadence (~30 s).

## Staging

1. Timestamp plumb-through in `ZwoFrame` (prerequisite, independently
   useful).
2. `pending_data`/`pending_size` convention: cleared in
   `handle_command()`, sent and freed in `run_tcpip()`.
3. `data`, plus `w`/`h`/`bits` in the `status` reply.
4. `next`, with `served_seq` per connection; verify against the
   frame-reading code in `src/benchmark`.
5. Optional `fits` verb with cached TCS metadata.
6. Emulator coverage and docs in `docs/ZWO/zwogcam.html`.

## Open questions

- Access control: the command port never calls `TCPIP_AddressCheck()`,
  unlike the generic server in [tcpip.c:534](../../src/gcam/tcpip.c#L534).
  Serving image data widens the exposure — reuse the oklist, or leave it
  open as today?
- Does any consumer want the *raw* (unshifted, unaveraged) frame? That
  has to be solved in `zwotcp.c` before it is servable at all.
- Is a rate limit or ROI/subsample parameter needed, or is full-frame at
  guide rate acceptable on the instrument LAN?
