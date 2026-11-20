# Par 386

A golf game for Palm OS in the style of early 90s PC golf games. Runs on the Palm IIIx and up. See PLAN.md.

## Installing

Install `Par386.prc` and the course files (`Par386-*.pdb`) you want. Courses can go in RAM or on an SD card, in `/PALM/Programs/Par386/`, `/PALM/Launcher/` or the root of the card. A card course is copied into RAM while you play it, so you need about 70 KB free.

## Player

Open Player in the clubhouse to set your name, golfer, skin colour and handedness. Your name can have up to eight characters. The choices are saved and used for the first human player, including when you continue a round. The left-handed golfer swings on the other side of the ball. Skin shades use the same silhouette on screens with one or two bits per pixel.

## Games

Play a round, pick the game, then set each of the four player slots to Human, a computer golfer (CPU Easy, Fair, Good or Pro) or Off, and Tee off.

| Game | Players | How it's scored |
|---|---|---|
| Stroke play | 1 to 4 | Fewest strokes |
| Match play | 2 | Each hole won, lost or halved; the match ends once one side is more holes up than there are left, as "3 & 2" |
| Skins | 2 to 4 | The lowest score alone wins the hole's skin; a tie carries it to the next hole |
| Best ball | 4 | P1 and P2 against P3 and P4, a match on each side's better score |
| Tournament | 1 to 4 | Two rounds of stroke play against eight computer golfers. The leaderboard shows the running total after each hole and from the card. |

The panel's middle box shows how you stand: holes up or down in match play and best ball, the skins carried over in skins, and your score to par otherwise.

Computer golfers aim, pick a club and shot shape and swing through the same physics as you, with more error the lower their level. In Settings, CPU: Quick shows only where their shot finished on the map, and CPU: Full shows the whole shot. A tap or any key hurries them on.

In Settings, Gimmies: 2 ft gives short putts and counts the stroke. Choose Hole out to play them. The setting applies when you start a new round.

The cup mark on the putting meter allows for green speed and the rise or fall to the cup.

Practice has a driving range (the first par 5, each ball from the tee) and a putting green (a ball somewhere on the first green each time you hole out). On the practice green, tick Repeat to putt from the same spot each time. Tap it again to return to new spots. The Scorecard button also toggles Repeat.

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

On the map, left and right move the target in 5° steps. The wind arrow points the way the wind blows across the map. Centre or the Map button goes back to the scene.

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
| Swing or Putt | Start the meter. Tap anywhere for power and, on a swing, snap |
| The club on the panel | Open the club picker. On the green, only the putter is available |
| Shape on the panel | Next shot shape |
| Map on the panel | Tap a spot to aim there and see its yardage. Back returns to the scene with the target marked |
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
