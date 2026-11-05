## Context

R104 makes the decoder an owning module, because a sequence split
across reads must decode as one event. Its callers are OVD-17's headless
queue and OVD-18's system reads, which do not exist yet. The interface
below is therefore the narrowest one both of them need: bytes in, at
most one event out, and the time passed in by the caller (T108).

## Decisions

### D1. Reusing `unicode.c`'s UTF-8 decoder

T108 says to reuse `src/unicode.c`'s decoder. The only function
`unicode.h` exposes is `ovd_unicode_next_grapheme`, and it does not fit:
- it joins extenders into graphemes, but a key carries one scalar;
- it replaces controls, but paste text keeps them as data (R103);
- it cannot tell a sequence cut short by the end of a read from an
  ill-formed one. The decoder needs that to hold a partial scalar.

So `unicode.{h,c}` gain `ovd_unicode_decode_utf8(bytes, available)`. It
is the existing static `decode`, split into:
- a raw step that returns the code point, size, and one of `SCALAR`,
  `ILL_FORMED` or `TRUNCATED`;
- the grapheme iterator's classification, which maps both error statuses
  to the same maximal-subpart replacement as before.

Copying the table logic into `decode.c` would be the duplication that
Art. IX forbids. Plan.md lists `unicode.c` as "reused, unchanged"; this
reuse needs one function made visible, and `test_unicode` proves the
iterator unchanged. `unicode.{h,c}` are outside #53's owned files, and no
open issue owns them, so no parallel agent conflicts (Art. IX). This is
stated on #53's plan comment.

### D2. Interface

```c
[[nodiscard]] bool ovd_decoder_init(OvdDecoder *, uint32_t escape_delay_ms,
                                    char *text, size_t text_capacity);
[[nodiscard]] bool ovd_decoder_next(OvdDecoder *, const char *bytes, size_t length,
                                    uint64_t now_ms, size_t *consumed, OvdEvent *event);
[[nodiscard]] bool ovd_decoder_deadline(const OvdDecoder *, uint64_t *deadline_ms);
```

- `next` decodes from `bytes` until it has one event or has consumed
  every byte. It stores the count it consumed. It returns whether
  `*event` holds an event. Progress: each call consumes at least one
  byte or returns an event (data-model invariant). An event can leave
  the byte that ended it unconsumed, when that byte starts something new
  (001 R17's "the byte that ends a subpart is not consumed").
- `now_ms` is the caller's monotonic time. The module reads no clock
  (T108). Headless passes virtual time, and the system backend passes
  R109's clock.
- `deadline` reports when held bytes expire, so a system wait knows how
  long to block. It returns false when nothing is held.
- `text` is the paste chunk buffer that the terminal owns (data-model,
  Input buffers). A chunk's `bytes` point into it and stay valid until
  the next call.

`OvdDecoder` is a struct defined in the private header, because
`OvdTerminal` embeds it (data-model). Only `decode.c` writes its fields.
The tests read them to check invariants.

### D3. The escape delay applies to every held sequence

FR-013 says "once no further byte arrives within the escape delay". The
delay is therefore measured from the last byte consumed. At the start
of a call, if bytes are held and `now_ms` is at least the delay past
that byte, the held bytes resolve first:
- a lone ESC becomes Escape;
- `ESC [` alone becomes Alt+[, and `ESC O` alone becomes Alt+O (R105);
- an unfinished CSI or SS3 is discarded (FR-014);
- a held partial UTF-8 scalar becomes U+FFFD (001 R17), with Alt if an
  ESC came before it.

Bytes given in one call arrived together, so they never expire against
each other. Paste mode is not timed: the end-marker prefix it holds
waits for the next byte, whatever the time (R103).

Measuring from the last byte, not the first, means that a long unknown
sequence split across many reads is still consumed to its final byte.
For a lone ESC the two rules agree.

### D4. Shapes the dispatch table accepts

The dispatch table accepts only the shapes R105 and FR-015 list:
- CSI letter keys with no parameters, or `1;m`;
- `CSI n ~` and `CSI n;m~` keys;
- `CSI Z` (Shift+Tab, xterm "rather than CSI Z");
- `CSI I` and `CSI O` (focus);
- `CSI 200~` (paste start);
- `CSI < b;x;y M/m` (mouse);
- `SS3` with A–D, H, F, P–S.

Everything else is discarded: empty parameters, a `:` sub-parameter, a
modifier outside 2–16, intermediates, and any other private marker.
Meta (modifier bit 8) is dropped, because it is outside the spec's
modifier set (R105). Mouse:
- coordinates are 1-based on the wire, and 0 or anything above 2³¹ is
  discarded. The 0-based result therefore fits `int32_t` up to
  `INT32_MAX` (T106);
- buttons 6–11 (codes 66, 67 and 128 and above) are discarded, as are a
  press with no button, a wheel release, and a motion release.

### D5. Paste chunks

In paste state, bytes are repaired to UTF-8 (001 R17) into the chunk
buffer. Two things are held back:
- a prefix of `ESC [ 2 0 1 ~`, at most 5 bytes;
- one partial scalar.

The held marker prefix and a partial scalar cannot coexist. An ESC ends
the scalar first, as U+FFFD.

A chunk is returned:
- when the call runs out of bytes;
- when one more byte might not fit;
- when the end marker completes. The `~` is then left unconsumed, so the
  next call returns PASTE_END.

One byte adds at most 8 bytes of text: a 5-byte marker prefix given
back, plus a U+FFFD. `init` therefore rejects a buffer smaller than
`OVD_DECODER_TEXT_MIN` (8). Any larger buffer only changes where chunks
split, never their content.

## Risks / Trade-offs

- [The ECMA-48 byte ranges in R104 are still ASSUMED.] xterm's
  introduction (Patch #411) confirms that a sequence runs "until the
  sequence is complete, or some byte is found which is not allowed in
  the sequence". It does not list the ranges. → The PR keeps them
  ASSUMED. A wrong range would show up as a table row failing, not as an
  unbounded read.
- [Two ESCs typed within the delay read as Alt+Escape.] This is R105's
  rule, "ESC + key within the delay". → It is documented in the test
  table.
- [`ESC [` followed by a letter within the delay is a CSI.] For example,
  Alt+[ then a is discarded as an unknown CSI. R105 already records this
  ambiguity.
