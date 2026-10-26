/*
 * Overdrive core: results, version, and geometry shared by every module.
 *
 * PROPOSED CONTRACT (specs/001-render-core). Experimental for all 0.x
 * releases: source and binary compatibility are not yet promised.
 */
#ifndef OVERDRIVE_CORE_H
#define OVERDRIVE_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Marks the supported public ABI. Everything without OVD_API is internal
 * and, in a shared build, hidden (-fvisibility=hidden). OVERDRIVE_SHARED
 * is defined by the build for consumers of the shared library;
 * OVERDRIVE_BUILDING only while compiling Overdrive itself. */
#if defined(OVERDRIVE_SHARED)
#  if defined(_WIN32)
#    if defined(OVERDRIVE_BUILDING)
#      define OVD_API __declspec(dllexport)
#    else
#      define OVD_API __declspec(dllimport)
#    endif
#  else
#    define OVD_API __attribute__((visibility("default")))
#  endif
#else
#  define OVD_API
#endif

/* The version of the headers being compiled against. Compare with
 * ovd_version() to detect a header/library mismatch at run time. */
#define OVD_VERSION_MAJOR 0
#define OVD_VERSION_MINOR 1
#define OVD_VERSION_PATCH 0

typedef struct OvdVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} OvdVersion;

/* The version of the library actually linked. */
OVD_API OvdVersion ovd_version(void);

/* Every fallible public function returns an OvdResult. On anything but
 * OVD_OK, out parameters are left unchanged and no state was modified.
 * The one exception is ovd_frame_end returning OVD_ERROR_IO; its comment
 * says what it changes.
 *
 * nullptr: a fallible function given a nullptr object or out pointer
 * returns OVD_ERROR_INVALID_ARGUMENT. A function that cannot fail does
 * nothing when given a nullptr object and returns a zero value (a zero
 * size, OVD_COLOR_DEPTH_NONE, false, 0). */
typedef enum OvdResult : int32_t {
    OVD_OK = 0,
    /* nullptr where an object is required, or a value outside its
     * documented range. */
    OVD_ERROR_INVALID_ARGUMENT,
    /* The allocator returned nullptr. */
    OVD_ERROR_ALLOCATION,
    /* A call out of order: a frame is already open, or none is. */
    OVD_ERROR_STATE,
    /* The operation does not apply to this terminal's backend. */
    OVD_ERROR_UNSUPPORTED,
    /* The system backend was requested but the output is not a terminal. */
    OVD_ERROR_NOT_A_TERMINAL,
    /* An operating-system terminal call failed. */
    OVD_ERROR_PLATFORM,
    /* Writing output failed, or the sink reported failure. */
    OVD_ERROR_IO,
} OvdResult;

/* A static, NUL-terminated name such as "OVD_ERROR_STATE". Never nullptr;
 * an out-of-range value yields "OVD_RESULT_UNKNOWN". */
OVD_API const char *ovd_result_name(OvdResult result);

/* A size in terminal cells. Negative or zero components mean empty. */
typedef struct OvdSize {
    int32_t cols;
    int32_t rows;
} OvdSize;

/* A rectangle in terminal cells. (x, y) is the top-left cell; column 0,
 * row 0 is the top-left of the screen. Coordinates may be negative or
 * extend past the screen: drawing clips to the canvas, never fails. Any
 * int32_t values are valid: Overdrive's rectangle arithmetic does not
 * overflow. */
typedef struct OvdRect {
    int32_t x;
    int32_t y;
    int32_t cols;
    int32_t rows;
} OvdRect;

#endif /* OVERDRIVE_CORE_H */
