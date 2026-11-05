# Data Model: Input and Abnormal-Exit Restoration

Public shapes are owned by [contracts/](contracts/include/overdrive/)
(Art. I); this file adds the internal state and the invariants each
owner keeps. Decisions are cited by number from [research.md](research.md).

## Public values (input.h)

| Type | Kind | Invariant |
|---|---|---|
| `OvdEvent` | transparent tagged union | only the member `kind` names is non-zero |
| `OvdKeyEvent` | transparent | `codepoint` is a scalar value iff `key == OVD_KEY_CHAR`, else 0 |
| `OvdMouseEvent` | transparent | `button` is a wheel direction iff `action == OVD_MOUSE_ACTION_WHEEL` |
| `OvdPasteText` | transparent view | valid UTF-8, `length > 0`, valid until the next input call or destroy (R103) |
| `OvdEventKind`, `OvdKey`, `OvdModifier`, `OvdMouseAction`, `OvdMouseButton` | fixed-type enums | closed sets (Art. III) |

## Config (terminal.h, `OvdTerminalConfig`)

| Field | Default | Validation (R115) |
|---|---|---|
| `input` | false | system: stdin must be a terminal → else `NOT_A_TERMINAL` |
| `mouse`, `bracketed_paste`, `focus_events` | false | require `input` → else `INVALID_ARGUMENT` |
| `escape_delay_ms` | 50 | 0–1000 → else `INVALID_ARGUMENT` |
| `restore_on_exit` | true | system only; ignored by headless |

## Internal state

### `OvdDecoder` (src/decode.h, owning module, R104)

Embedded in `OvdTerminal`. Fields: parse state (ground, ESC, CSI, SS3,
paste), the bounded parameter area (64 bytes) and its length, an
`overflow` flag (discarding to the final byte), the paste end-marker
match length (0–5), a held partial UTF-8 scalar (0–3 bytes), and the
arrival time of a pending lone ESC.

Invariants:
- the parameter length never exceeds its capacity; past it, `overflow`
  is set and bytes are consumed, not stored;
- in paste state no key, mouse, or focus event is produced;
- the decoder never reads outside the bytes it is given;
- each call consumes at least one byte or produces one event, so it
  cannot loop without progress (SC-002).

State transitions:

```text
ground --ESC--> escape --'['--> csi --final--> ground (dispatch)
                       --'O'--> ss3 --byte--> ground (dispatch)
                       --other--> ground (Alt + key)
                       --delay elapsed--> ground (Escape key)
csi --"200~"--> paste --"ESC[201~"--> ground
```

### Input buffers (owned by `OvdTerminal`, R110, R114)

- input queue: `OVD_INPUT_QUEUE_SIZE` (4096) bytes, a ring of bytes fed
  (headless) or read (system) and not yet decoded;
- paste chunk buffer: sized for the worst-case UTF-8 repair of one
  queue's worth (each ill-formed byte → 3-byte U+FFFD), plus the held
  bytes;
- both allocated at create when `input` is on, freed at destroy.

### `OvdTerminalSystem` additions (src/internal.h)

- the reports turned on (mouse, paste, focus), so the exit sequence
  turns exactly those off (R116);
- whether this terminal owns the process-wide restoration record.

### `OvdPlatformModes` additions (src/platform/platform.h)

- a raw-input step (R106, R107), recorded with its saved value like the
  existing steps; `OVD_PLATFORM_SAVED_SIZE` (256) is checked by each
  platform's `static_assert`;
- POSIX: the self-pipe's two descriptors (R111).

### Restoration record (src/platform, static storage, R112, R113)

One per process. Fields: `state` (lock-free atomic), the exit sequence
bytes and length (fixed buffer, CAN first), the saved input modes
(termios, or the Windows console modes and code page), and the previous
handler of each handled signal (POSIX `struct sigaction`, Windows the
previous unhandled-exception filter).

State transitions (R112):

```text
IDLE --create, record filled--> ARMED
ARMED --handler or destroy wins compare-exchange--> CLAIMED
CLAIMED --restore finished--> DONE
DONE --destroy, handlers put back--> IDLE
```

Invariants:
- the fields other than `state` are written only by the thread that
  creates or destroys the owning terminal, only while `state` is `IDLE`,
  and published by the release store of `ARMED`; handlers read them
  after an acquire load;
- `CLAIMED` is entered once per arming; its holder is the one restore
  (FR-033), and anyone else who sees `CLAIMED` waits for `DONE`;
- the thread holding `CLAIMED` has every handled signal blocked (the
  handler's `sa_mask`, or destroy's `pthread_sigmask`), so a waiter is
  never the holder;
- destroy puts back each previous handler whose slot still holds
  Overdrive's before storing `IDLE`.
