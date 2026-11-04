/* The system backend (T033): the process's own console under the terminal
 * API. FR-010, FR-011, FR-014; research.md R25 to R29, R46.
 *
 * Owning module of OvdTerminalSystem (internal.h), and the only terminal
 * file that calls the operating-system layer (platform/platform.h).
 * terminal.c calls these functions where the backend matters: create,
 * destroy, and each frame's size read and write.
 *
 * The enter and exit sequences are R26's. Mode 1049 (alternate screen,
 * saving and restoring the cursor), mode 25 (cursor visible), mode 7
 * (autowrap), SGR 0, and CUP are from xterm's control-sequence reference
 * (invisible-island.net/xterm/ctlseqs/ctlseqs.html), cited in R26 and
 * R33. They are not observed here in a real terminal (Art. X; the manual
 * checklist, OVD-12). */
#include "internal.h"

#include "detect.h"

#include <string.h>

#define CSI "\x1b["
#define SGR_RESET CSI "0m"
#define AUTOWRAP_ON CSI "?7h"
#define AUTOWRAP_OFF CSI "?7l"
#define CURSOR_SHOW CSI "?25h"
#define CURSOR_HIDE CSI "?25l"
#define ALTERNATE_ENTER CSI "?1049h"
#define ALTERNATE_LEAVE CSI "?1049l"
#define LITERAL_LENGTH(text) (sizeof(text) - 1)

/* Digits in the largest row count, INT32_MAX = 2147483647. */
static constexpr size_t ROW_DIGITS_MAX = 10;
/* CSI <rows> ; 1 H: column 0 of the last row, as the diff moves (R26). */
static constexpr size_t ROW_MOVE_MAX = LITERAL_LENGTH(CSI) + ROW_DIGITS_MAX + LITERAL_LENGTH(";1H");
static constexpr size_t ENTER_MAX
    = LITERAL_LENGTH(ALTERNATE_ENTER) + LITERAL_LENGTH(CURSOR_HIDE) + LITERAL_LENGTH(AUTOWRAP_OFF);
static constexpr size_t EXIT_MAX
    = LITERAL_LENGTH(SGR_RESET) + LITERAL_LENGTH(AUTOWRAP_ON) + LITERAL_LENGTH(CURSOR_SHOW)
      + (LITERAL_LENGTH(ALTERNATE_LEAVE) > ROW_MOVE_MAX ? LITERAL_LENGTH(ALTERNATE_LEAVE)
                                                         : ROW_MOVE_MAX);
static constexpr size_t SEQUENCE_CAPACITY = 32;
static_assert(ENTER_MAX <= SEQUENCE_CAPACITY && EXIT_MAX <= SEQUENCE_CAPACITY,
              "the enter and exit sequences must fit the sequence buffer");

/* One lifecycle sequence, built on the stack and written in one write.
 * overflow is set if an append would not fit; the static_assert above
 * proves no sequence here does, and an overflowed one is never written. */
typedef struct Sequence {
    char bytes[SEQUENCE_CAPACITY];
    size_t length;
    bool overflow;
} Sequence;

static void put(Sequence *sequence, const char *text, size_t length)
{
    if (sequence->overflow || length > sizeof sequence->bytes - sequence->length) {
        sequence->overflow = true;
        return;
    }
    memcpy(sequence->bytes + sequence->length, text, length);
    sequence->length += length;
}

#define PUT_LITERAL(sequence, text) put((sequence), (text), LITERAL_LENGTH(text))

/* Appends the decimal digits of value, which is positive. */
static void put_number(Sequence *sequence, int32_t value)
{
    char digits[ROW_DIGITS_MAX];
    size_t length = 0;
    do {
        digits[sizeof digits - 1 - length] = (char)('0' + value % 10);
        value /= 10;
        ++length;
    } while (value > 0);
    put(sequence, digits + sizeof digits - length, length);
}

static bool write_sequence(const Sequence *sequence)
{
    return !sequence->overflow && ovd_platform_write(sequence->bytes, sequence->length);
}

/* R26's exit sequence, best effort: a failed write cannot be reported
 * (R28), so it is not. rows is positive by OvdTerminal's invariant;
 * anything else moves to row 1. */
static void write_exit(const OvdTerminalSystem *system, int32_t rows)
{
    Sequence sequence = {};
    PUT_LITERAL(&sequence, SGR_RESET);
    PUT_LITERAL(&sequence, AUTOWRAP_ON);
    PUT_LITERAL(&sequence, CURSOR_SHOW);
    if (system->alternate_screen) {
        PUT_LITERAL(&sequence, ALTERNATE_LEAVE);
    } else {
        PUT_LITERAL(&sequence, CSI);
        put_number(&sequence, rows > 0 ? rows : 1);
        PUT_LITERAL(&sequence, ";1H");
    }
    write_sequence(&sequence);
}

OvdResult ovd_terminal_system_open(OvdTerminalSystem *system, bool alternate_screen,
                                   OvdSize *size)
{
    /* Step 1, before anything is written or changed. */
    if (!ovd_platform_output_is_terminal()) {
        return OVD_ERROR_NOT_A_TERMINAL;
    }
    /* Steps 2 to 4. On failure ovd_platform_enter has undone what it
     * completed and records nothing. */
    OvdResult result = ovd_platform_enter(&system->modes);
    if (result != OVD_OK) {
        return result;
    }
    /* Step 5. */
    result = ovd_platform_size(size);
    if (result != OVD_OK) {
        ovd_platform_leave(&system->modes);
        return result;
    }
    system->alternate_screen = alternate_screen;
    return OVD_OK;
}

/* ovd_platform_environment, through a file-local function. Taking the
 * address of the external function makes x86_64 MinGW GCC emit a global
 * `.refptr.ovd_platform_environment` symbol, which the prefix check on the
 * static archive rightly rejects (R12); a static function's address needs
 * none. */
static const char *environment(void *context, const char *name)
{
    return ovd_platform_environment(context, name);
}

OvdColorDepth ovd_terminal_system_detect(const OvdTerminalSystem *system)
{
    return ovd_detect_color_depth(environment, nullptr,
                                  ovd_platform_virtual_terminal(&system->modes));
}

void ovd_terminal_system_undo(OvdTerminalSystem *system)
{
    ovd_platform_leave(&system->modes);
}

OvdResult ovd_terminal_system_enter(OvdTerminalSystem *system, int32_t rows)
{
    Sequence sequence = {};
    if (system->alternate_screen) {
        PUT_LITERAL(&sequence, ALTERNATE_ENTER);
    }
    PUT_LITERAL(&sequence, CURSOR_HIDE);
    PUT_LITERAL(&sequence, AUTOWRAP_OFF);
    if (write_sequence(&sequence)) {
        return OVD_OK;
    }
    /* Part of the enter sequence may have reached the terminal, and the
     * failure does not show whether CSI ? 1049 h took effect, so the exit
     * sequence leaves the alternate screen whenever it was requested
     * (R28). The modes stay recorded: the caller frees step 6's blocks
     * first, then undoes them, in reverse order. */
    write_exit(system, rows);
    return OVD_ERROR_IO;
}

void ovd_terminal_system_close(OvdTerminalSystem *system, int32_t rows)
{
    write_exit(system, rows);
    ovd_platform_leave(&system->modes);
}

OvdResult ovd_terminal_system_size(OvdSize *size)
{
    return ovd_platform_size(size);
}

bool ovd_terminal_system_write(const char *bytes, size_t length)
{
    return ovd_platform_write(bytes, length);
}
