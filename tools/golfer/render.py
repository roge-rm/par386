"""Renders every frame of the golfer scene with Cycles on the CPU, transparent.

  blender -b SCENE.blend --python render.py -- OUTDIR [SIZE] [SAMPLES] [FRAMES]
FRAMES is a comma list such as 3,4 to render only those again.
Writes OUTDIR/f01.png ... at SIZE x SIZE (default 480) without the club
(it still casts its shadow), and cNN.png with the club alone, everything else
held out, so sprites.py can draw the shaft as a thin line.
"""
import sys, os, time, json
import bpy
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]
size = int(argv[1]) if len(argv) > 1 else 480
samples = int(argv[2]) if len(argv) > 2 else 64
only = [int(x) for x in argv[3].split(',')] if len(argv) > 3 else None
os.makedirs(out, exist_ok=True)

sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.device = 'CPU'
sc.cycles.samples = samples
sc.cycles.use_denoising = False          # keep the grain, it reads as film
sc.cycles.max_bounces = 4
sc.render.threads_mode = 'FIXED'
sc.render.threads = 10
sc.render.film_transparent = True
sc.render.filter_size = 1.0
sc.render.resolution_x = sc.render.resolution_y = size
sc.render.resolution_percentage = 100
sc.render.image_settings.file_format = 'PNG'
sc.render.image_settings.color_mode = 'RGBA'
sc.view_settings.view_transform = 'Standard'
sc.view_settings.look = 'Medium High Contrast'

# where the ball and the feet land in the image, for placing the sprite
sc.frame_set(1)
rig = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
def px(co):
    v = world_to_camera_view(sc, sc.camera, co)
    return [v.x * size, (1 - v.y) * size]
feet = [rig.matrix_world @ rig.pose.bones[b].head for b in ('LeftFoot', 'RightFoot')]
ground = (feet[0] + feet[1]) / 2; ground.z = 0
# a point 200 yd down the target line: same screen column as the ball when the camera looks down the line
far = Vector(sc['ball']) + Vector(sc['target']) * 182.88
json.dump({'size': size, 'ball': px(Vector(sc['ball'])), 'far': px(far), 'feet': px(ground)},
          open(os.path.join(out, 'meta.json'), 'w'))

others = [o for o in bpy.data.objects if o.type == 'MESH' and o.name != 'club']
club = bpy.data.objects['club']
club.visible_camera = False
def club_pass(f):
    club.visible_camera = True
    for o in others:
        o.is_holdout = True
    sc.cycles.samples = 16
    sc.render.filepath = os.path.join(out, f'c{f:02d}.png')
    bpy.ops.render.render(write_still=True)
    sc.cycles.samples = samples
    for o in others:
        o.is_holdout = False
    club.visible_camera = False

for f in range(sc.frame_start, sc.frame_end + 1):
    if only and f not in only:
        continue
    t = time.time()
    sc.frame_set(f)
    sc.render.filepath = os.path.join(out, f'f{f:02d}.png')
    bpy.ops.render.render(write_still=True)
    club_pass(f)
    print(f'frame {f} {time.time() - t:.1f}s', flush=True)
