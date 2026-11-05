# Overdrive documentation

| Document | What it is |
|---|---|
| [identity/OVD-IDENTITY-001.md](identity/OVD-IDENTITY-001.md) | Naming, branding, and identity specification (adopted) |
| [architecture.md](architecture.md) | Layers, frame lifecycle, rendering model, ownership (planned) |
| [plan/workstreams.md](plan/workstreams.md) | Issue dependency graph, parallel lanes, human gates |
| [process/swarm.md](process/swarm.md) | How parallel AI agents claim, build, review, and hand off work |
| [../.specify/memory/constitution.md](../.specify/memory/constitution.md) | The binding rules (v1.4.1, ratified) |
| [../.specify/memory/baseline.md](../.specify/memory/baseline.md) | Toolchain and machine baseline |

## Feature specifications (Spec Kit)

Spec Kit reads these from `specs/`, so they live there rather than here.

| Feature | Spec | Plan | Contracts | Tasks | Work graph |
|---|---|---|---|---|---|
| 001 Rendering core | [spec](../specs/001-render-core/spec.md) | [plan](../specs/001-render-core/plan.md) · [research](../specs/001-render-core/research.md) | [headers](../specs/001-render-core/contracts/include/overdrive/) | [tasks](../specs/001-render-core/tasks.md) | [workgraph.json](../specs/001-render-core/workgraph.json) |
| 002 Input and abnormal-exit restoration (specified, not implemented) | [spec](../specs/002-input-restore/spec.md) | [plan](../specs/002-input-restore/plan.md) · [research](../specs/002-input-restore/research.md) | [headers](../specs/002-input-restore/contracts/include/overdrive/) | [tasks](../specs/002-input-restore/tasks.md) | [workgraph.json](../specs/002-input-restore/workgraph.json) |
