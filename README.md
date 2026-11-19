# Par 386

A golf game for Palm OS in the style of early 90s PC golf games. Runs on the Palm IIIx and up. Early work, it only shows a blank form so far. See PLAN.md.

## Controls

While you play, the four app buttons belong to the game, so they don't open Date Book and the rest. To leave, use the Home (Applications) button.

The shot shape goes back to Shape (a normal shot) after every shot. Draw, fade, high and low work with any club but the putter, punch from the 3 iron down and open from the 9 iron down.

Palm IIIx and other Palms with app buttons:

| Button | What it does |
|---|---|
| Date Book (1st app button) | Swing: start, power, snap. Select in menus |
| Address (2nd) | Map, and back |
| To Do (3rd) | Scorecard, and back |
| Memo (4th) | Next shot shape |
| Scroll up and down | Change club, move in menus |
| Menu | Back to the clubhouse |

5-way (Tungsten, Zire 72, Treo and the like):

| Key | What it does |
|---|---|
| Left and right | Aim |
| Up and down | Change club |
| Centre | Swing: start, power, snap |
| App buttons | As on the IIIx: 2nd map, 3rd scorecard, 4th shot shape |

Clie Jog Dial:

| Control | What it does |
|---|---|
| Turn up and down | Change club, move in menus |
| Press | Swing: start, power, snap |
| Back | Back to the clubhouse |
| App buttons | As on the IIIx |

Pen, on any device:

| Tap | What it does |
|---|---|
| The view | Aim there |
| The panel at the bottom | Swing: start, power, snap |
| Shape on the panel | Next shot shape |
| Map on the panel | Map; tap the map to come back |
| Card on the map's panel | Scorecard |

With a keyboard, C or S opens the scorecard, backspace goes back to the clubhouse and other letters open the map.

## Build

You need prc-tools-remix, PilRC and the Palm OS SDK 5r3 (at `../palm-os-sdk/sdk-5r3`).

- `make` builds a debug `build/Par386.prc`
- `make release` builds the release one
- `make host-test` builds the game core with the host gcc (ASan and UBSan) and runs its tests, course-test included
- `make courses` compiles the courses in `courses/` into `build/courses/` (needs Python 3 and Pillow), see doc/course-format.md

## Licence

GPLv3, see LICENSE.
