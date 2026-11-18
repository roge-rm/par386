# Par 386

A golf game for Palm OS in the style of early 90s PC golf games. Runs on the Palm IIIx and up. Early work, it only shows a blank form so far. See PLAN.md.

## Build

You need prc-tools-remix, PilRC and the Palm OS SDK 5r3 (at `../palm-os-sdk/sdk-5r3`).

- `make` builds a debug `build/Par386.prc`
- `make release` builds the release one
- `make host-test` builds the game core with the host gcc (ASan and UBSan) and runs its tests, course-test included
- `make courses` compiles the courses in `courses/` into `build/courses/` (needs Python 3 and Pillow), see doc/course-format.md

## Licence

GPLv3, see LICENSE.
