## Context

The decoder (`src/decode.h`) takes bytes and the caller's time, returns
at most one event per call, and reports a deadline while bytes are
held. Input adds what the decoder leaves to its caller:
- a queue of bytes not yet decoded;
- the paste chunk buffer;
- a clock;
- the resize report.

The input state is owned by the terminal module (data-model, Input
buffers): `input.c` is one more file of that module, beside
`terminal.c`.

## Decisions

### D1. The queue is linear, compacted on feed

The queue holds `queue[start .. start + length)`. The decoder needs
contiguous bytes, so feed moves the live bytes to the front when the new
ones would not fit after them. That move is a `memmove` of at most 4096
bytes, and it allocates nothing. A ring buffer would split one decode
into two calls at the wrap, and saves nothing that the bounds need.

### D2. Headless time is a virtual clock in the terminal

R109 and input.h say: "The timeout counts as the time that passed".
- The terminal keeps a `uint64_t` virtual clock, starting at 0. Bytes
  are decoded at the clock's current value. Feed does not advance it.
- A wait whose timeout reaches the decoder's deadline advances the clock
  to the deadline and resolves the held bytes there, returning at once
  if that yields an event. Otherwise the clock advances by the whole
  timeout (0 for a poll), and the wait returns `OVD_EVENT_KIND_NONE`.
- A negative timeout reaches any deadline. With nothing queued or held,
  it returns `OVD_EVENT_KIND_NONE` at once, because nothing else can
  arrive.

Time is cumulative: two waits of 30 ms reach a 50 ms deadline. input.h's
"when timeout_ms is at least the escape delay" is therefore a sufficient
condition, not the only one. This is the narrowest reading of "counts as
the time that passed", and the system backend's real clock behaves the
same way.

### D3. The resize report comes first

`ovd_terminal_set_size` on a terminal with input on marks a resize as
unreported. The next wait returns it before decoding any queued byte,
with the size `ovd_terminal_size` reports. Several set_size calls before
a wait give one event with the latest size ("reported once", input.h).
A terminal with input off records nothing.

Bytes and sizes are not ordered against each other in headless use, and
the system backend's resize is likewise out of band. The resize is
reported first so that a full queue of bytes cannot delay it.

### D4. The interim error stays for the system backend

T113 removes T103's interim `OVD_ERROR_UNSUPPORTED` for headless only.
So:
- `tests/test_terminal.c`'s interim assertion is narrowed to the system
  backend;
- the headless half moves to `tests/test_input.c`, where create
  succeeds.

No open issue owns `test_terminal.c`, and the edit is the smallest one
that keeps the suite true (Art. IX). OVD-18 removes the system half.

### D5. Allocation and argument order

Create with input on adds two requests to its one batch:
- the queue (`OVD_INPUT_QUEUE_SIZE` bytes);
- the chunk buffer (3 × the queue plus `OVD_DECODER_TEXT_MIN`, the
  worst-case repair of a full queue plus the held bytes, data-model).

A refusal of either leaves nothing allocated (R44). Wait and feed check
their arguments in this order:
- nullptr arguments → `OVD_ERROR_INVALID_ARGUMENT`;
- for feed, a backend other than headless → `OVD_ERROR_UNSUPPORTED`;
- input off, or for wait a call from inside the sink →
  `OVD_ERROR_STATE`.

That is the order input.h lists them in. Feed is allowed from inside the
sink: input.h names no state error for it, and it touches no frame
state.

## Risks / Trade-offs

- [The chunk buffer is larger than the decoder needs.] Any buffer of
  `OVD_DECODER_TEXT_MIN` bytes or more works. data-model.md's size means
  one queue's worth of paste is always one chunk. The extra 12 KiB is
  fixed and allocated once.
- [Report order between fed bytes and set_size is not chronological.]
  This is D3. A test pins the order, so a change would be visible.
