# Workstreams: 001-render-core

The machine-readable source of truth is
[`specs/001-render-core/workgraph.json`](../../specs/001-render-core/workgraph.json);
this page is its human view. Task detail is in
[`tasks.md`](../../specs/001-render-core/tasks.md).

## Dependency graph

```mermaid
flowchart LR
    G0{{"Gate: constitution ratified ✓<br/>repo created"}}
    G0 --> O01["OVD-01<br/>build · CI · headers · core"]
    O01 --> O02["OVD-02 color"]
    O01 --> O03["OVD-03 unicode"]
    O01 --> O04["OVD-04 alloc · output"]
    O01 --> O05["OVD-05 VT replay (tests)"]
    O01 --> O06["OVD-06 platform · detect"]
    O03 --> O07["OVD-07 buffer · canvas"]
    O04 --> O07
    O07 --> O08["OVD-08 diff"]
    O02 --> O08
    O04 --> O08
    O05 --> O08
    O07 --> O09["OVD-09 boxes"]
    O08 --> O10["OVD-10 headless terminal · frames"]
    O04 --> O10
    O07 --> O10
    O10 --> O11["OVD-11 system backend"]
    O06 --> O11
    O10 --> O13["OVD-13 benchmark · steady state"]
    O11 --> O12["OVD-12 hello_overdrive"]
    O09 --> O12
    O11 --> O14["OVD-14 docs: implemented state"]
    O13 --> O14
    O12 --> H{{"Human: manual checklist<br/>Windows Terminal + POSIX"}}

    classDef crit stroke:#FF9138,stroke-width:3px
    class O01,O03,O07,O08,O10,O11,O12 crit
```

Orange border = critical path.

## Parallel lanes

| Wave | Issues in flight | Max agents | Starts when |
|---|---|---|---|
| 0 | OVD-01 | 1 | gate G0 |
| 1 | OVD-02, 03, 04, 05, 06 | 5 | OVD-01 merged |
| 2 | OVD-07 (+ any wave-1 stragglers) | 1–3 | OVD-03 and OVD-04 merged |
| 3 | OVD-08, OVD-09 | 2 | OVD-07 merged (+ 02, 05 for 08) |
| 4 | OVD-10, then OVD-11 ∥ OVD-13 | 2 | OVD-08 merged |
| 5 | OVD-12, OVD-14 | 2 | OVD-11 (+ 09, 13) merged |

## File ownership

Each issue owns a disjoint set of paths (checked: no overlap in
`workgraph.json`). Two files are **shared append-only**:
`CMakeLists.txt` (source list) and `tests/CMakeLists.txt` (one
`ovd_add_test` line per test). Agents only append their own lines and
resolve conflicts by rebasing on `main`. `include/overdrive/` is frozen
after OVD-01: changing it takes its own contract-change issue, labeled
`assumption` for the owner's after-the-fact review.

## Human gates

Agents merge through the Art. XVI gate
([swarm protocol](../process/swarm.md#merge-authority)). A human is
needed only for:

1. **Manual validation** — OVD-11/OVD-12 in Windows Terminal and one POSIX
   terminal. Agents cannot observe a real terminal; they merge with the
   gap reported as UNVERIFIED and a `needs-human` issue tracks the check.
2. **License and publication** — before any release; not on this graph.

## Consumer integration (not in this graph)

Adopting Overdrive in the task-tracking application is that repository's
decision, made through its own issues and approvals, after Overdrive's
input feature (002) exists. Nothing in this graph touches that
repository.
