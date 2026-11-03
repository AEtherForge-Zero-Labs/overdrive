## 1. Template

- [x] 1.1 Put `Refs #<number>` in its own paragraph above the trailers in the `AGENTS.md` commit template
- [x] 1.2 Explain, without naming an issue, PR, or date, why the trailers stand alone and how to check a message
- [x] 1.3 State git's mixed-paragraph rule exactly (review nit)

## 2. Checks

- [x] 2.1 `git interpret-trailers --parse` finds no trailers in the old form and both trailers in the new form
- [x] 2.2 `openspec validate --all --strict` passes and the docs form check is clean
