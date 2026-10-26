> **Adoption record.** OVD-IDENTITY-001 v1.0.0 adopted for this repository
> on 2026-10-26 as the official naming and branding specification. Its
> naming rules are binding through constitution Art. XIV; its evidence
> rules through Art. XV. The text below is the specification as approved,
> unedited.
>
> **Context notes (not amendments):** the repository is hosted on GitHub
> under AEtherForge Zero Labs and is Lab property. References below to
> GitLab, classroom governance, and the PCC Task Tracking Application
> describe that application's own process; Overdrive's process is
> constitution Art. XVI. The specification calls Overdrive "open-source";
> no license has been chosen yet (§10), so the repository is all rights
> reserved until one is.

# OVERDRIVE
## Official Naming, Branding & Technical Identity Specification

**Specification ID:** OVD-IDENTITY-001
**Version:** 1.0.0
**Status:** Approved brand direction; implementation contracts pending specification
**Project type:** Independent open-source C23 terminal UI framework
**Brand name:** Overdrive
**Tagline:** Every frame matters.
**Public C API prefix:** `ovd_`
**Canonical library name:** `liboverdrive`
**Canonical repository name:** `overdrive`

---

## 1. Project Identity

### 1.1 Official Name

**Overdrive**

The name must be capitalized as *Overdrive* in prose, headings, documentation, and official communications.

Use `overdrive` in machine-readable identifiers, package names, filenames, and repository paths.

Use `OVERDRIVE` in promotional graphics and terminal splash screens.

The public brand does not contain a mandatory version number, implementation language, or platform suffix.

### 1.2 Official Tagline

**Every frame matters.**

The tagline communicates Overdrive's commitment to rendering efficiency, responsive interaction, predictable execution, and intentional visual design.

Do not replace the tagline with performance claims that have not been benchmarked.

### 1.3 Formal Description

Overdrive is a high-performance terminal rendering and user interface framework written in ISO C23.

It provides native applications with a structured, composable approach to terminal graphics, including truecolor rendering, Unicode-aware drawing, efficient frame updates, flexible layouts, interactive widgets, and time-based animation.

Overdrive treats the terminal as a dynamically rendered interface rather than a sequential stream of formatted text.

### 1.4 Short Description

A high-performance, animated terminal UI framework for C23.

### 1.5 Project Positioning

Overdrive is a reusable systems library, not an application-specific UI module.

Its design must not depend on the PCC Task Tracking Application, GitLab, academic coursework, or any particular application's domain model.

The Task Tracking Application may act as an initial integration consumer and reference implementation.

---

## 2. Engineering Philosophy

Overdrive follows six principles.

### 2.1 Performance Is Measured

Performance claims require reproducible measurements.

The engine should minimize unnecessary memory allocation, redundant terminal output, rendering work, and CPU consumption.

Frame-rate objectives are engineering targets, not guaranteed characteristics.

### 2.2 Explicit Control

The public API should make resource ownership, lifecycle, errors, and platform limitations understandable.

Avoid hidden application-wide state and unnecessary runtime dependencies.

### 2.3 Every Frame Is Intentional

A frame should represent a coherent application state.

Rendering, animation, and input processing must follow predictable scheduling rules.

Animations must not block user input.

### 2.4 Portability Through Isolation

Platform-specific terminal operations belong behind defined interfaces.

The rendering model must not assume a particular terminal emulator.

Windows Terminal is the initial primary validation target.

### 2.5 Capability-Aware Presentation

Features such as truecolor, mouse input, extended keyboard sequences, and terminal graphics may depend on platform capabilities.

Unsupported features should degrade predictably.

Optional graphics protocols must never be required for basic operation.

### 2.6 Composition Over Excessive Abstraction

Introduce modules and abstractions only when justified by current requirements.

A lightweight, comprehensible implementation is preferable to an elaborate framework architecture with unused extension points.

---

## 3. Public Naming Conventions

### 3.1 Global Symbol Namespace

All exported functions must begin with:

`ovd_`

Examples:

- `ovd_terminal_create`
- `ovd_terminal_destroy`
- `ovd_frame_begin`
- `ovd_frame_end`
- `ovd_canvas_fill`
- `ovd_layout_split`
- `ovd_motion_update`

These examples establish naming style, not finalized function signatures.

### 3.2 Type Names

Public types use `OvdPascalCase`.

Examples:

- `OvdTerminal`
- `OvdFrame`
- `OvdCanvas`
- `OvdColor`
- `OvdRect`
- `OvdEvent`
- `OvdAnimation`
- `OvdResult`

Opaque types should use forward declarations where appropriate:

```c
typedef struct OvdTerminal OvdTerminal;
```

Use transparent value structs only when exposing their representation is intentional and ABI-safe.

### 3.3 Constants and Enumerations

Public constants use `OVD_UPPER_SNAKE_CASE`.

Examples:

```c
OVD_OK
OVD_ERROR_ALLOCATION
OVD_EVENT_KEY
OVD_EVENT_RESIZE
OVD_COLOR_DEFAULT
OVD_KEY_ESCAPE
```

Public enumeration types should use the `Ovd` prefix.

Values with distinct semantics must have distinct types wherever practical.

### 3.4 Function Naming

Use lowercase snake_case.

Preferred pattern:

`ovd_<subsystem>_<operation>`

Examples:

```c
ovd_terminal_create()
ovd_terminal_destroy()
ovd_frame_begin()
ovd_frame_end()
ovd_canvas_fill()
ovd_animation_cancel()
```

Prefer consistent operation verbs.

Avoid ambiguous functions such as `ovd_do`, `ovd_process`, or `ovd_handle` without an explicit object or operation.

### 3.5 File Naming

Public headers:

```text
include/overdrive/overdrive.h
include/overdrive/render.h
include/overdrive/layout.h
include/overdrive/motion.h
```

Implementation filenames should use lowercase snake_case.

Platform-specific code should be isolated from platform-independent code.

The public umbrella header must not expose private implementation details.

### 3.6 Symbol Visibility

Only explicitly designated functions, types, and data symbols belong to the supported public ABI.

Internal helpers should have internal linkage or hidden visibility where appropriate.

Do not export generic names that could collide with another C library.

### 3.7 ABI Versioning

Overdrive must distinguish source compatibility from binary compatibility.

A stable ABI is a future release commitment, not an assumption for version 0.x development.

The first public release should document which interfaces are experimental.

---

## 4. Module Identity

The following terminology establishes a consistent conceptual vocabulary.

These names do not require creating all modules immediately.

| Module | Responsibility |
|---|---|
| Core | Engine lifecycle, errors, and shared primitives |
| Render | Frame buffers, cell state, differential rendering |
| Canvas | Drawing primitives, clipping, styled text |
| Layout | Geometry, constraints, and region assignment |
| Motion | Animation timelines, easing, interpolation |
| Widgets | Reusable presentation and interaction components |
| Input | Events, input processing, and focus |
| Platform | Operating-system terminal integration |

### 4.1 Core

Owns fundamental engine contracts and lifecycle management.

### 4.2 Render

Owns the terminal-cell representation, frame-buffer management, and output-diff strategy.

Rendering behavior must be testable without an active terminal.

### 4.3 Canvas

Provides drawing operations against a renderable surface.

Drawing operations must respect clipping boundaries and terminal-cell geometry.

### 4.4 Layout

Computes widget geometry.

Layout computations should be independent of terminal I/O.

### 4.5 Motion

Handles animation timing, interpolation, transitions, and cancellation.

Animations must use monotonic time.

Animation behavior should be testable with injected clock values.

### 4.6 Widgets

Provides reusable UI components built on the lower-level rendering and layout interfaces.

Widgets must not require direct access to application-specific domain models.

### 4.7 Input

Processes keyboard, mouse, resize, and application events.

Input handling must remain responsive while animations are active.

### 4.8 Platform

Owns platform-dependent terminal operations.

Initial platform targets:

- Windows Terminal / Windows console infrastructure
- Linux and POSIX-compatible terminal environments

Platform-specific features must be capability-gated.

---

## 5. Package and Build Identity

### 5.1 Library

Canonical name:

`liboverdrive`

Example library files:

```text
liboverdrive.a
liboverdrive.so
liboverdrive.dylib
overdrive.dll
overdrive.lib
```

Actual filenames depend on platform and linker conventions.

### 5.2 CMake

Canonical target:

`overdrive`

Recommended exported alias:

`overdrive::overdrive`

A consumer should eventually be able to write:

```cmake
target_link_libraries(myapp PRIVATE overdrive::overdrive)
```

### 5.3 Public Includes

Recommended usage:

```c
#include <overdrive/overdrive.h>
```

Specialized includes may be supported:

```c
#include <overdrive/render.h>
#include <overdrive/layout.h>
```

### 5.4 Internal Configuration

Build options should use the `OVERDRIVE_` prefix.

Examples:

```text
OVERDRIVE_BUILD_TESTS
OVERDRIVE_BUILD_EXAMPLES
OVERDRIVE_ENABLE_SIXEL
OVERDRIVE_ENABLE_BENCHMARKS
```

These are naming conventions for options introduced by actual requirements, not mandatory options to create immediately.

### 5.5 Repository Independence

The canonical independent repository is `overdrive`.

Applications consuming Overdrive should integrate it through an appropriate dependency mechanism.

The task-tracking application's own `tasklib` API and the Overdrive API must remain logically separate.

---

## 6. Visual Identity

### 6.1 Primary Palette

| Token | Name | Hex |
|---|---|---|
| `carbon` | Carbon Black | `#0B1018` |
| `graphite` | Graphite | `#1C2736` |
| `ignition` | Ignition Orange | `#FF9138` |
| `electric` | Electric Blue | `#49B9FF` |
| `signal` | Signal Green | `#63E4AE` |
| `foreground` | High-contrast White | `#F5F7FB` |
| `muted` | Muted Steel | `#AAB8CC` |

### 6.2 Brand Accent

Ignition Orange is the principal visual accent.

Use it for brand elements, important highlights, focus indicators, and restrained emphasis.

Do not use saturated orange indiscriminately across every component.

### 6.3 Typography

Prefer modern, legible sans-serif typography in documentation and branding.

Code examples must use monospaced typography.

The framework must not depend on a particular terminal font for basic functionality.

Avoid assuming Nerd Fonts or private-use glyph availability.

### 6.4 Logo Direction

The proposed identity combines:

- A stylized tachometer or performance gauge
- A frame or terminal-cell motif
- Angular, forward-moving geometry
- Ignition Orange against a dark neutral background

The logo should remain recognizable at small sizes and in monochrome.

An actual logo is a separate visual-design deliverable.

### 6.5 Visual Language

Preferred:

- Strong alignment
- Compact, precise geometry
- Restrained color accents
- Crisp panel boundaries
- Fluid but purposeful transitions
- High information density with clear hierarchy

Avoid:

- Excessive neon gradients
- Constantly moving decorative elements
- Unnecessary flashing
- Overly elaborate borders
- Visual effects that interfere with keyboard navigation
- Claims of performance based solely on aesthetics

---

## 7. Product Language

### 7.1 Approved Terminology

Use:

- Terminal rendering engine
- Terminal UI framework
- Native rendering
- Frame-driven animation
- Differential rendering
- Terminal-cell buffer
- Composable layout
- Capability-aware graphics
- High-performance native interface

### 7.2 Claims Requiring Evidence

Do not claim:

- Zero flicker
- Guaranteed 60 FPS
- Universal terminal compatibility
- Zero allocations
- Zero dependencies
- Stable ABI
- GPU acceleration
- Universal image support

unless the specific claim has been established through implementation evidence.

### 7.3 Standard Taglines

Official:

**Every frame matters.**

Supporting phrases:

- Full color. Full motion. Full control.
- Native rendering for modern terminals.
- Built for the terminal. Tuned for performance.

Only the official tagline should be treated as canonical.

---

## 8. Documentation and Repository Standards

### 8.1 Recommended README Opening

```markdown
# Overdrive

**Every frame matters.**

Overdrive is a high-performance terminal UI framework
written in C23.

Build composable, colorful, animated terminal applications
with a native rendering engine designed for explicit
control and predictable performance.
```

### 8.2 Project Documentation

The independent repository should eventually contain:

- Getting started
- Building and installation
- Public API reference
- Architecture overview
- Rendering model
- Platform compatibility
- Performance benchmarks
- Examples
- Contribution guidelines
- Licensing information

Documentation must distinguish planned capabilities from implemented capabilities.

### 8.3 Example Applications

The framework should provide focused demonstration programs when the corresponding functionality exists.

Suggested names:

```text
hello_overdrive
dashboard_demo
animation_demo
layout_demo
input_demo
render_benchmark
```

Examples must remain independently understandable.

---

## 9. Spec Kit and Agent Instructions

### 9.1 Authority

This document defines the approved branding and naming conventions.

It does not replace project constitutions, assignment requirements, issue specifications, or approved implementation plans.

### 9.2 Naming Enforcement

Coding agents must:

1. Use `ovd_` for exported C functions.
2. Use `Ovd` for public C types.
3. Use `OVD_` for public constants and enumerators.
4. Use `OVERDRIVE_` for build configuration options.
5. Refer to the product as Overdrive in user-facing documentation.
6. Preserve the canonical tagline.
7. Keep framework types independent of consuming applications.
8. Avoid adding unused modules solely because they appear in this specification.

### 9.3 Architectural Planning

Before implementation, agents must define the necessary API contracts and their ownership.

The implementation must comply with the applicable repository constitution and approved Spec Kit artifacts.

### 9.4 Acceptance Criteria for Identity Compliance

- Public exported symbols use the approved prefix.
- Public type naming is consistent.
- Package and target naming follow the conventions.
- Documentation uses the official name and tagline.
- Framework APIs contain no task-manager-specific types.
- Platform-specific dependencies are isolated.
- Public feature descriptions accurately reflect implemented capabilities.
- New modules have documented present-day justification.

---

## 10. Scope and Governance

This specification finalizes project identity, terminology, and naming direction.

It does not authorize arbitrary implementation changes.

It does not declare that the independent framework has already been implemented.

It does not supersede existing GitLab team or classroom governance.

Public licensing, trademark clearance, repository publication, binary compatibility policy, and release ownership require separate decisions.

No public name, package namespace, domain, or trademark availability is guaranteed by this document.

---

## 11. Final Identity Declaration

**Name:** Overdrive
**Tagline:** Every frame matters.
**Language:** ISO C23
**Product:** High-performance terminal UI framework
**Public prefix:** `ovd_`
**Library:** `liboverdrive`
**Repository:** `overdrive`
**CMake namespace:** `overdrive::`
**Visual accent:** Ignition Orange `#FF9138`
**Architecture:** Native, composable, frame-driven, cross-platform

Overdrive exists to demonstrate that modern terminal interfaces can be expressive, animated, responsive, and visually sophisticated without abandoning the clarity and control of native systems programming.

**Every frame matters.**
