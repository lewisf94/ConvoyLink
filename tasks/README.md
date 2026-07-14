# Task queue

Each `T*.md` file is one self-contained unit of work sized for a single
focused agent session. The execution protocol lives in `/CLAUDE.md`
(session protocol, verification, invariants) — read it first, every time.

## Anatomy of a task file

| Section | Meaning |
|---|---|
| **Depends** | Tasks that must be `done` first (see `STATUS.md`) |
| **Required reading** | Doc sections that are binding for this task |
| **Deliverables** | The complete list of files you create/modify |
| **Interface contract** | Signatures/layouts you must match exactly |
| **Acceptance — CI** | Commands that must pass (this is your definition of done) |
| **Acceptance — hardware** | Checklist the project owner runs on real parts; you ship it, you don't run it |
| **Out of scope** | Things deliberately excluded — do not do them |

## Rules of engagement

- Statuses in `STATUS.md`: `todo` → `in-progress` → `done` (or `blocked`
  with a note). Update your row in the same commit as the work.
- Never mark a task `done` with failing or skipped CI acceptance.
- Hardware-facing tasks are `done` when code + checklist ship and CI
  passes; the owner then executes the checklist and either closes the
  milestone gate or files findings as notes on the task (fixes become
  follow-up work in the same task file, reopened to `in-progress`).
- Phase ordering matters (`/ROADMAP.md`): M1 tasks are independent of each
  other except T05→T06; M3 needs M1; M4 needs its listed deps. Don't
  cherry-pick ahead of dependencies.
