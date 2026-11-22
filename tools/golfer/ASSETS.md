# Golfer sprite assets

Nothing here is committed: the downloads live in `build/golfer-assets/` and can be fetched again.

| What | Source | Licence |
|---|---|---|
| MPFB 2.0.18 (Blender extension, makes the human, rig and fits clothes) | https://extensions.blender.org/add-ons/mpfb/ | GPLv3 (code, used as a tool only) |
| MakeHuman base mesh, targets, `cmu_mb` rig and weights (inside MPFB) | same | CC0 |
| MakeHuman system assets: `male_casualsuit06`, `female_casualsuit01`, `shoes02`, `short02` and `ponytail01` hair, `young_caucasian_male` and `young_caucasian_female` skin | http://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip | CC0 |
| CMU motion capture, subject 64, trial 01 (golf swing), `64.asf` + `64_01.amc` | https://mocap.cs.cmu.edu/subjects/64/ | CMU allows use in products and prohibits direct resale of the data, including converted copies. See the [CMU motion capture database](https://mocap.cs.cmu.edu/). |

The data used in this project was obtained from mocap.cs.cmu.edu.
The database was created with funding from NSF EIA-0196217.

The cap and club are modelled in `build_scene.py`.

## Setup

```
mkdir -p build/golfer-assets/{cmu,mpfb,syspack} build/blender/{config,scripts,ext}
# cmu/: 64.asf and 64_01.amc from the CMU link
# mpfb/mpfb.zip: from the MPFB page
# syspack/sys.zip from the asset pack link, unzipped into syspack/x/
. tools/golfer/env.sh      # keeps Blender's config, MPFB and its logs on the scratch disk
blender -b --command extension install-file -r user_default -e build/golfer-assets/mpfb/mpfb.zip
```

## Run

```
. tools/golfer/env.sh
heavy build blender -b --python tools/golfer/build_scene.py -- build/golfer-assets build/golfer/golfer.blend
heavy build blender -b build/golfer/golfer.blend --python tools/golfer/render.py -- build/golfer/r640 640 64
python3 tools/golfer/sprites.py build/golfer/r640 build/evidence/golfer
```

`sprites.py` needs `tools/mockup/r/scene_bg_960.png` (from `tools/mockup/render.py r 960`).

For the female golfer, use the same camera, lights and swing with the female MakeHuman model. `sprites.py` puts the five 40 and 80 pixel poses in `art/golfer/female/`.

```
mkdir -p build/golfer-female
. tools/golfer/env.sh
heavy build blender -b --python tools/golfer/build_scene.py -- build/golfer-assets "$PWD/build/golfer-female/golfer.blend" female
heavy build blender -b "$PWD/build/golfer-female/golfer.blend" --python tools/golfer/render.py -- "$PWD/build/golfer-female/r640" 640 64 1,2,4,8,13
python3 tools/golfer/sprites.py build/golfer-female/r640 build/evidence/golfer-female --female
```
