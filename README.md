# Par 386

A golf game for Palm OS in the style of early 90s PC golf games. Runs on the Palm IIIx and up. Early work, it only shows a blank form so far. See PLAN.md.

## Games

Play a round, pick the game, then set each of the four player slots to Human, a computer golfer (CPU Easy, Fair, Good or Pro) or Off, and Tee off.

| Game | Players | How it's scored |
|---|---|---|
| Stroke play | 1 to 4 | Fewest strokes |
| Match play | 2 | Each hole won, lost or halved; the match ends once one side is more holes up than there are left, as "3 & 2" |
| Skins | 2 to 4 | The lowest score alone wins the hole's skin; a tie carries it to the next hole |
| Best ball | 4 | P1 and P2 against P3 and P4, a match on each side's better score |
| Tournament | 1 to 4 | Stroke play against a field of eight computer golfers; the leaderboard shows after each hole and from the card |

The panel's middle box shows how you stand: holes up or down in match play and best ball, the skins carried over in skins, and your score to par otherwise.

Computer golfers aim, pick a club and shot shape and swing through the same physics as you, with more error the lower their level. In Settings, CPU: Quick shows only where their shot finished on the map, and CPU: Full shows the whole shot. A tap or any key hurries them on.

Practice has a driving range (the first par 5, each ball from the tee) and a putting green (a ball somewhere on the first green each time you hole out).

Your best score on each course shows under its picture on the Courses board. Only rounds played through by a person count.

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
