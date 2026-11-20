"""Builds the golfer scene in Blender: MPFB human, cmu_mb rig, clothes, club,
a CMU golf swing retargeted onto the rig, camera and morning sun.

Run with MPFB enabled in the Blender config (source env.sh, see ASSETS.md):
  blender -b --python build_scene.py -- ASSETS OUT.blend [female]
ASSETS holds cmu/ (64.asf, 64_01.amc) and syspack/x/ (MakeHuman system assets).
Each swing frame in SWING below becomes one Blender frame, address is frame 1.
"""
import os, sys, math
import bpy
import numpy as np
from mathutils import Matrix, Vector, Quaternion

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cmu
from bl_ext.user_default.mpfb.services.humanservice import HumanService

argv = sys.argv[sys.argv.index('--') + 1:]
ASSETS, OUT = argv[0], argv[1]
female = argv[2:] == ['female']
if argv[2:] and not female:
    raise SystemExit('Use ASSETS OUT.blend [female]')
PACK = os.path.join(ASSETS, 'syspack', 'x')
TRIAL = os.path.join(ASSETS, 'cmu', '64_01.amc')
suit_name = 'female_casualsuit01' if female else 'male_casualsuit06'
hair_name = 'ponytail01' if female else 'short02'
skin_name = 'female' if female else 'male'
CS = os.path.join(PACK, 'clothes', suit_name, suit_name)

# CMU 64_01 frames (120 fps): address, then 12 swing frames
SWING = [120, 215, 245, 268, 292, 312, 322, 331, 337, 343, 350, 405, 440]

# ---------------------------------------------------------------- human
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

macro = {"gender": 0.0 if female else 1.0, "age": 0.55, "muscle": 0.6, "weight": 0.55, "proportions": 0.7,
         "height": 0.6, "cupsize": 0.5, "firmness": 0.5,
         "race": {"asian": 0.0, "caucasian": 1.0, "african": 0.0}}
body = HumanService.create_human(macro_detail_dict=macro, feet_on_ground=True, scale=0.1)
rig = HumanService.add_builtin_rig(body, "cmu_mb")
for name in [CS + '.mhclo',
             'clothes/shoes02/shoes02.mhclo',
             f'hair/{hair_name}/{hair_name}.mhclo']:
    kind = 'Hair' if name.startswith('hair') else 'Clothes'
    HumanService.add_mhclo_asset(os.path.join(PACK, name), body, asset_type=kind, subdiv_levels=0)
bpy.context.view_layer.update()

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
suit = next(o for o in meshes if 'casualsuit' in o.name.lower())
shoes = next(o for o in meshes if 'shoes' in o.name.lower())
hair = next(o for o in meshes if hair_name in o.name.lower().replace(' ', ''))

# ---------------------------------------------------------------- materials
def principled(name, colour, rough=0.8, tex=None, normal=None, sheen=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*colour, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Sheen Weight'].default_value = sheen
    if tex:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images.load(tex)
        mul = nt.nodes.new('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
        mul.inputs['Factor'].default_value = 1.0
        mul.inputs[6].default_value = (*colour, 1)
        nt.links.new(t.outputs['Color'], mul.inputs[7])
        nt.links.new(mul.outputs[2], b.inputs['Base Color'])
    if normal:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images.load(normal)
        t.image.colorspace_settings.name = 'Non-Color'
        nm = nt.nodes.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value = 1.5
        nt.links.new(t.outputs['Color'], nm.inputs['Color']); nt.links.new(nm.outputs['Normal'], b.inputs['Normal'])
    return m

def lin(r, g, b): return tuple((c / 255.0) ** 2.2 for c in (r, g, b))

skin_m = principled('skin', lin(255, 236, 222), 0.55,
                    os.path.join(PACK, f'skins/young_caucasian_{skin_name}/young_lightskinned_{skin_name}_diffuse.png'))
glove_m = principled('glove', lin(236, 236, 230), 0.7)
# The suit is a white tee and blue jeans: the shirt takes its folds from the
# suit's AO and normal maps under the shirt colour; trousers are dark grey-navy.
shirt_m = principled('shirt', lin(184, 92, 88), 0.85, CS + '_ao.png', CS + '_normal.png', 0.3)
trousers_m = principled('trousers', lin(54, 56, 66), 0.75, CS + '_ao.png', CS + '_normal.png', 0.2)
shoe_m = principled('shoes', lin(232, 228, 220), 0.5)
hair_m = principled('hair', lin(70, 50, 34), 0.6)
cap_m = principled('cap', lin(244, 244, 240), 0.7)
club_m = principled('club', lin(140, 140, 146), 0.4)
grip_m = principled('grip', lin(30, 30, 30), 0.6)
head_m = principled('clubhead', lin(178, 176, 172), 0.35)
collar_m = principled('collar', lin(240, 236, 226), 0.8)

def set_only(o, m):
    o.data.materials.clear(); o.data.materials.append(m)

set_only(shoes, shoe_m)
set_only(hair, hair_m)

set_only(suit, shirt_m); suit.data.materials.append(trousers_m)
if female:
    # The shirt and trousers are separate islands in this mesh.
    adjacent = [[] for _ in suit.data.vertices]
    for edge in suit.data.edges:
        a, b = edge.vertices
        adjacent[a].append(b)
        adjacent[b].append(a)
    first = max(suit.data.vertices, key=lambda v: (suit.matrix_world @ v.co).z).index
    shirt = {first}
    pending = [first]
    while pending:
        for v in adjacent[pending.pop()]:
            if v not in shirt:
                shirt.add(v)
                pending.append(v)
    for poly in suit.data.polygons:
        poly.material_index = 0 if poly.vertices[0] in shirt else 1
else:
    # The male suit's white tee and jeans can be separated by texture colour.
    img = bpy.data.images.load(CS + '_diffuse.png')
    W, H = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(H, W, 4)
    uv = suit.data.uv_layers.active.data
    for poly in suit.data.polygons:
        u = sum(uv[i].uv.x for i in poly.loop_indices) / poly.loop_total
        v = sum(uv[i].uv.y for i in poly.loop_indices) / poly.loop_total
        r, g, b, _ = px[min(H - 1, int(v * H)), min(W - 1, int(u * W))]
        poly.material_index = 0 if min(r, g, b) > 0.55 and max(r, g, b) - min(r, g, b) < 0.12 else 1

# body: skin, with the left (lead) hand gloved
set_only(body, skin_m); body.data.materials.append(glove_m)
vg = body.vertex_groups.get('LeftHand')
hand = set()
if vg:
    for v in body.data.vertices:
        for g in v.groups:
            if g.group == vg.index and g.weight > 0.35:
                hand.add(v.index)
for poly in body.data.polygons:
    if sum(1 for i in poly.vertices if i in hand) >= 3:
        poly.material_index = 1

# ---------------------------------------------------------------- retarget
MAP = {'root': 'Hips', 'lhipjoint': 'LHipJoint', 'lfemur': 'LeftUpLeg', 'ltibia': 'LeftLeg',
       'lfoot': 'LeftFoot', 'ltoes': 'LeftToeBase', 'rhipjoint': 'RHipJoint', 'rfemur': 'RightUpLeg',
       'rtibia': 'RightLeg', 'rfoot': 'RightFoot', 'rtoes': 'RightToeBase', 'lowerback': 'LowerBack',
       'upperback': 'Spine', 'thorax': 'Spine1', 'lowerneck': 'Neck', 'upperneck': 'Neck1', 'head': 'Head',
       'lhumerus': 'LeftArm', 'lradius': 'LeftForeArm', 'lwrist': 'LeftHand',
       'lhand': 'LeftFingerBase', 'lfingers': 'LeftHandFinger1', 'lthumb': 'LThumb',
       'rhumerus': 'RightArm', 'rradius': 'RightForeArm', 'rwrist': 'RightHand',
       'rhand': 'RightFingerBase', 'rfingers': 'RightHandFinger1', 'rthumb': 'RThumb'}
INV = {v: k for k, v in MAP.items()}
# clavicles are left out: the CMU rest clavicle points up 22 degrees, which
# would lift the target's shoulders; they follow the chest instead
M = np.array([[1, 0, 0], [0, 0, -1], [0, 1, 0]], dtype=np.float64)   # ASF Y-up to Blender Z-up

bones = cmu.read_asf(os.path.join(ASSETS, 'cmu', '64.asf'))
frames = cmu.read_amc(TRIAL)

arm = rig.data
rest = {b.name: b.matrix_local.to_3x3() for b in arm.bones}
C = {}
for b in arm.bones:
    src = INV.get(b.name)
    if src is None or src == 'root':
        C[b.name] = Matrix.Identity(3)
        continue
    d_src = Vector(M @ bones[src].direction).normalized()
    d_tgt = (rest[b.name] @ Vector((0, 1, 0))).normalized()
    C[b.name] = d_tgt.rotation_difference(d_src).to_matrix()

# scale source root motion by leg length; ground is the ankle height at address
p0 = cmu.fk(bones, frames[SWING[0]])
k = ((arm.bones['LeftUpLeg'].length + arm.bones['LeftLeg'].length) /
     (bones['lfemur'].length + bones['ltibia'].length))
hips_head = arm.bones['Hips'].head_local.copy()
ankle_z = arm.bones['LeftFoot'].head_local.z
src_ankle = (M @ ((p0['lfoot'][1] + p0['rfoot'][1]) / 2))[2]
origin = M @ p0['root'][1]

def order(bs):
    out = []
    def walk(b):
        out.append(b)
        for c in b.children:
            walk(c)
    for b in bs:
        if b.parent is None:
            walk(b)
    return out

ordered = order(arm.bones)
for pb in rig.pose.bones:
    pb.rotation_mode = 'QUATERNION'

# Posture: the MakeHuman torso is longer than the CMU subject's, so the
# address looks upright with the hands high. Bend a little more at the waist
# and knees, about the pelvis's left-right axis (degrees, + tips forward).
BEND = {'LowerBack': 9, 'Spine': 4, 'LeftUpLeg': -9, 'RightUpLeg': -9,
        'LeftLeg': 16, 'RightLeg': 16, 'LeftFoot': -7, 'RightFoot': -7}

# From this frame on (after impact) the lead foot is flat as at address and the
# trail foot pivots on its ankle until the ball of the foot is on the ground,
# toes flat: the mocap feet tip onto their toes at the finish.
FOOT_FIX = 8

def plant_feet(lead0, ltoe0, ball_z):
    pb = rig.pose.bones['LeftFoot']
    m = lead0.copy(); m.translation = pb.matrix.translation
    pb.matrix = m
    rig.pose.bones['LeftToeBase'].rotation_quaternion = ltoe0
    bpy.context.view_layer.update()
    pb = rig.pose.bones['RightFoot']; toe = rig.pose.bones['RightToeBase']
    head = pb.head.copy(); v = toe.head - head
    axis = Vector((v.x, v.y, 0)).normalized().cross(Vector((0, 0, 1)))
    angs = np.radians(np.arange(-80, 80.5, 0.5))
    err = [abs((head + Matrix.Rotation(a, 3, axis) @ v).z - ball_z) + 0.0005 * abs(a) for a in angs]
    R = Matrix.Rotation(angs[int(np.argmin(err))], 4, axis)
    pb.matrix = Matrix.Translation(head) @ R @ Matrix.Translation(-head) @ pb.matrix
    bpy.context.view_layer.update()
    d = toe.tail - toe.head
    q = d.rotation_difference(Vector((d.x, d.y, 0))).to_matrix().to_4x4()
    toe.matrix = Matrix.Translation(toe.head) @ q @ Matrix.Translation(-toe.head) @ toe.matrix
    bpy.context.view_layer.update()

# Finish frames: turn the chest to face the target (CHEST_FACE of the full
# turn), lean the upper body back away from the target (reverse C, degrees)
# and drop the arms back behind the head (degrees), all as world rotations
# added at those bones and passed down to their children.
FINISH = {11: dict(lean=10, arms=40), 12: dict(lean=12, arms=45)}
CHEST_FACE = 0.95
_s = p0
T = Vector(M @ (_s['lfoot'][1] - _s['rfoot'][1])); T.z = 0; T.normalize()
_h = Vector(M @ _s['lwrist'][2]) - Vector(M @ (_s['lfoot'][1] + _s['rfoot'][1]) / 2); _h.z = 0
S0 = (_h - T * _h.dot(T)).normalized()                     # golfer's facing at address
UP = Vector((0, 0, 1))

def yaw(a, b):
    return math.atan2(a.cross(b).z, a.dot(b))

def finish_rots(p, f):
    L0 = Vector(M @ (p0['lhumerus'][1] - p0['rhumerus'][1])); L0.z = 0
    L = Vector(M @ (p['lhumerus'][1] - p['rhumerus'][1])); L.z = 0
    turn = yaw(S0, T) * CHEST_FACE - yaw(L0, L)
    back = (UP - T * math.tan(math.radians(f['lean']))).normalized()
    arms = (UP * math.cos(math.radians(f['arms'])) - T * math.sin(math.radians(f['arms'])))
    print('finish turn add', round(math.degrees(turn), 1))
    return {'LowerBack': Matrix.Rotation(turn, 3, UP),
            'Spine': UP.rotation_difference(back).to_matrix(),
            'LeftArm': UP.rotation_difference(arms).to_matrix(),
            'RightArm': UP.rotation_difference(arms).to_matrix()}

hand_rot = []
for fi, sf in enumerate(SWING):
    p = cmu.fk(bones, frames[sf])
    extra = finish_rots(p, FINISH[fi]) if fi in FINISH else {}
    world = {}
    E = {}
    for b in ordered:
        src = INV.get(b.name)
        if src is not None:
            D = Matrix((M @ p[src][0] @ M.T).tolist())
            W0 = D @ C[b.name] @ rest[b.name]
            Ep = E[b.parent.name] if b.parent else Matrix.Identity(3)
            if b.name in BEND:
                lat = world['Hips'] @ rest['Hips'].inverted() @ Vector((1, 0, 0))
                Ep = Matrix.Rotation(math.radians(BEND[b.name]), 3, lat) @ Ep
            if b.name in extra:
                Ep = extra[b.name] @ Ep
            world[b.name] = Ep @ W0
            E[b.name] = Ep
        else:
            world[b.name] = world[b.parent.name] @ rest[b.parent.name].inverted() @ rest[b.name]
            E[b.name] = E[b.parent.name]
    for b in ordered:
        pb = rig.pose.bones[b.name]
        if b.parent is None:
            basis = rest[b.name].inverted() @ world[b.name]
            r = M @ p['root'][1]
            pos = Vector(((r[0] - origin[0]) * k, (r[1] - origin[1]) * k, ankle_z + (r[2] - src_ankle) * k))
            pb.location = rest[b.name].inverted() @ (pos - hips_head)
        else:
            P = b.parent.name
            basis = (rest[P].inverted() @ rest[b.name]).inverted() @ world[P].inverted() @ world[b.name]
        pb.rotation_quaternion = basis.to_quaternion()
    # keep the lower ankle at its rest height (action unhooked so the update
    # doesn't evaluate the keys over this pose)
    act = rig.animation_data.action if rig.animation_data else None
    if act:
        rig.animation_data.action = None
    bpy.context.view_layer.update()
    hips = rig.pose.bones['Hips']
    low = min(rig.pose.bones['LeftFoot'].head.z, rig.pose.bones['RightFoot'].head.z)
    hips.location = hips.location + rest['Hips'].inverted() @ Vector((0, 0, ankle_z - low))
    bpy.context.view_layer.update()
    if fi == 0:
        lead0 = rig.pose.bones['LeftFoot'].matrix.copy()
        ltoe0 = rig.pose.bones['LeftToeBase'].rotation_quaternion.copy()
        ball_z = rig.pose.bones['RightToeBase'].head.z
    elif fi >= FOOT_FIX:
        plant_feet(lead0, ltoe0, ball_z)
    if act:
        rig.animation_data.action = act
    hips.keyframe_insert('location', frame=fi + 1)
    for pb in rig.pose.bones:
        pb.keyframe_insert('rotation_quaternion', frame=fi + 1)
    hand_rot.append([Matrix((M @ p[h][0] @ M.T).tolist()) @ E[MAP[h]] for h in ('lhand', 'rhand')])

# hold each pose (constant interpolation)
for fc in rig.animation_data.action.fcurves:
    for kp in fc.keyframe_points:
        kp.interpolation = 'CONSTANT'

# ---------------------------------------------------------------- club and cap
def make_club():
    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.019, depth=1.12, location=(0, 0, -0.56))
    shaft = bpy.context.object; set_only(shaft, club_m)
    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.016, depth=0.26, location=(0, 0, -0.10))
    grip = bpy.context.object; set_only(grip, grip_m)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=0.055, location=(0.03, 0, -1.13))
    ch = bpy.context.object; ch.scale = (1.3, 0.85, 0.6); set_only(ch, head_m)
    for o in (shaft, grip, ch):
        o.select_set(True)
    bpy.context.view_layer.objects.active = shaft
    bpy.ops.object.join()
    shaft.name = 'club'
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)   # origin at the grip end
    return shaft

club = make_club()

def make_cap():
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.105)
    crown = bpy.context.object
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='DESELECT')
    bpy.ops.object.mode_set(mode='OBJECT')
    for v in crown.data.vertices:
        v.select = v.co.z < -0.01
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.delete(type='VERT'); bpy.ops.object.mode_set(mode='OBJECT')
    crown.scale = (1.0, 1.08, 0.82)
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.085, depth=0.008, location=(0, -0.11, 0.0))
    brim = bpy.context.object; brim.scale = (1.0, 0.85, 1.0); brim.rotation_euler = (math.radians(10), 0, 0)
    for o in (crown, brim):
        o.select_set(True)
    bpy.context.view_layer.objects.active = crown
    bpy.ops.object.join(); crown.name = 'cap'
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    set_only(crown, cap_m)
    return crown

cap = make_cap()
bpy.context.view_layer.update()

def make_collar():
    """Polo collar: an open, slightly flared band round the shirt's neckline,
    made with the rig at rest and parented to the upper chest."""
    arm.pose_position = 'REST'
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = suit.evaluated_get(dg); me = ev.to_mesh()
    nb = arm.bones['Neck']
    axis = (nb.tail_local - nb.head_local).normalized()
    pts = []
    for poly in me.polygons:
        if poly.material_index == 0:
            pts += [suit.matrix_world @ me.vertices[i].co for i in poly.vertices]
    ev.to_mesh_clear()
    # neckline: shirt vertices close to the neck axis near its base
    near = [p for p in pts if (p - nb.head_local).length < 0.14 and (p - nb.head_local).dot(axis) > -0.06]
    top = max((p - nb.head_local).dot(axis) for p in near)
    ring = [p for p in near if (p - nb.head_local).dot(axis) > top - 0.025]
    c = sum(ring, Vector()) / len(ring)
    r = sum(((p - c) - axis * (p - c).dot(axis)).length for p in ring) / len(ring)
    print('collar ring', len(ring), 'radius', round(r, 3))
    bpy.ops.mesh.primitive_cone_add(vertices=24, radius1=r + 0.03, radius2=r + 0.006, depth=0.05,
                                    end_fill_type='NOTHING', location=(0, 0, 0))
    col = bpy.context.object; col.name = 'collar'
    col.matrix_world = Matrix.Translation(c + axis * 0.012) @ Vector((0, 0, 1)).rotation_difference(axis).to_matrix().to_4x4()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    sol = col.modifiers.new('thick', 'SOLIDIFY'); sol.thickness = 0.005
    set_only(col, collar_m)
    bpy.ops.object.shade_smooth()
    mw = col.matrix_world.copy()
    parent_to_bone(col, 'Spine1')
    bpy.context.view_layer.update()
    col.matrix_world = mw
    arm.pose_position = 'POSE'
    bpy.context.view_layer.update()
    return col


def parent_to_bone(o, bone):
    o.parent = rig; o.parent_type = 'BONE'; o.parent_bone = bone

# cap sits on the skull: placed with the rig in its rest position, then
# parented to the head bone keeping that placement
hb = arm.bones['Head']
arm.pose_position = 'REST'
bpy.context.view_layer.update()
crown = hb.head_local + (hb.tail_local - hb.head_local) * 0.52 + Vector((0, 0.012, 0))
parent_to_bone(cap, 'Head')
bpy.context.view_layer.update()
cap.matrix_world = rig.matrix_world @ Matrix.Translation(crown)
arm.pose_position = 'POSE'
bpy.context.view_layer.update()
make_collar()

bpy.context.scene.frame_set(1)
_lf = rig.matrix_world @ rig.pose.bones['LeftFoot'].head
_rf = rig.matrix_world @ rig.pose.bones['RightFoot'].head
_t = (_lf - _rf); _t.z = 0; _t.normalize()
_side = Vector((_t.y, -_t.x, 0))
if (rig.matrix_world @ rig.pose.bones['LeftHand'].head - (_lf + _rf) / 2).dot(_side) < 0:
    _side = -_side
_pb = rig.pose.bones['LeftHand']
_grip = rig.matrix_world @ _pb.matrix @ Vector((0, _pb.length * 0.6, 0))
for _s in np.arange(0.3, 1.5, 0.005):
    ball_at = (_lf + _rf) / 2 + _side * _s + _t * 0.08
    ball_at.z = 0.03
    if (ball_at - _grip).length > 1.07:
        break

# club: held at the lead hand; its direction turns with the source hands from
# the address direction (average of both hands, which are noisy on their own)
d0 = (ball_at - _grip).normalized()
# Hand keys for the club direction in (toward target, toward ball, up), for
# the top of the backswing and the finish where the mocap hands wobble.
# None means use the mocap hands; address and impact aim at the ball.
# At the top the shaft points a little left of the target and level, so from
# behind the ball it shows to the left of the head, not end-on as a stub.
CLUB = [None, None, (0.62, -0.45, 0.64), (0.80, -0.55, -0.12), None, (-0.55, 0.15, 0.82), None, None,
        None, (0.92, -0.30, 0.15), (0.35, -0.55, 0.76), (-0.50, 0.55, -0.65), (-0.50, 0.55, -0.65)]
LAG = 80     # degrees between lead forearm and shaft in the downswing, wrists cocked
up = Vector((0, 0, 1))
for fi in range(len(SWING)):
    bpy.context.scene.frame_set(fi + 1)
    pb = rig.pose.bones['LeftHand']
    m = rig.matrix_world @ pb.matrix
    grip = m @ Vector((0, pb.length * 0.6, 0))
    d = Vector((0, 0, 0))
    for h in range(2):
        d += hand_rot[fi][h] @ hand_rot[0][h].inverted() @ d0
    d.normalize()
    print('club', fi, 'mocap', tuple(round(x, 2) for x in (d.dot(_t), d.dot(_side), d.dot(up))))
    if CLUB[fi] == 'lag':
        # shaft cocked up from the lead forearm, in the plane of forearm and up
        fa = rig.pose.bones['LeftForeArm']
        f = (fa.tail - fa.head).normalized()
        u = (up - f * up.dot(f)).normalized()
        d = f * math.cos(math.radians(LAG)) + u * math.sin(math.radians(LAG))
    elif CLUB[fi] is not None:
        a, b, c = CLUB[fi]
        d = (_t * a + _side * b + up * c).normalized()
    if fi in (0, 7):
        # address and impact: the clubhead sits at the ball
        d = (ball_at - grip).normalized()
    q = Vector((0, 0, -1)).rotation_difference(d)
    club.matrix_world = Matrix.Translation(grip + d * -0.05) @ q.to_matrix().to_4x4()
    club.keyframe_insert('location', frame=fi + 1)
    club.rotation_mode = 'QUATERNION'
    club.rotation_quaternion = q
    club.keyframe_insert('rotation_quaternion', frame=fi + 1)
for fc in club.animation_data.action.fcurves:
    for kp in fc.keyframe_points:
        kp.interpolation = 'CONSTANT'

# ---------------------------------------------------------------- camera, light
sc = bpy.context.scene
sc.frame_start, sc.frame_end = 1, len(SWING)
bpy.context.scene.frame_set(1)
# target line: from the trailing foot toward the lead foot
lf = rig.matrix_world @ rig.pose.bones['LeftFoot'].head
rf = rig.matrix_world @ rig.pose.bones['RightFoot'].head
tgt = (lf - rf); tgt.z = 0; tgt.normalize()
mid = (lf + rf) / 2
side = Vector((tgt.y, -tgt.x, 0))   # toward the ball, golfer's front
hands = rig.matrix_world @ rig.pose.bones['LeftHand'].head
if (hands - mid).dot(side) < 0:
    side = -side
ball = mid + side * 0.62; ball.z = 0
sc['target'] = tuple(tgt); sc['ball'] = tuple(ball_at)
print('check: ball side . camera right (want > 0):', round(side.dot(tgt.cross(Vector((0, 0, 1)))), 3))
for f in range(1, len(SWING) + 1):
    sc.frame_set(f)
    L = rig.pose.bones['LeftArm'].head - rig.pose.bones['RightArm'].head; L.z = 0
    face = L.cross(Vector((0, 0, 1))).normalized()
    print(f'check: frame {f} chest . target {face.dot(tgt):+.2f}  chest . ball side {face.dot(side):+.2f}')
sc.frame_set(1)

cam_d = bpy.data.cameras.new('cam'); cam = bpy.data.objects.new('cam', cam_d); sc.collection.objects.link(cam)
look = mid + Vector((0, 0, 0.9))
# behind the ball looking straight down the target line, as the game camera
# is, raised about 18 degrees (13 yd up at 40 yd in tools/mockup/render.py)
pos = look - tgt * 9.55 + Vector((0, 0, 3.2))
cam.location = pos
cam.rotation_euler = (look - pos).to_track_quat('-Z', 'Y').to_euler()
cam_d.type = 'ORTHO'; cam_d.ortho_scale = 2.9
sc.camera = cam

sun_d = bpy.data.lights.new('sun', 'SUN'); sun = bpy.data.objects.new('sun', sun_d); sc.collection.objects.link(sun)
# render.py: to-sun (-0.55, 0.62, -0.56) in (right, up, forward) screen space, from this camera
fwd = look - pos; fwd.z = 0; fwd.normalize()
right = fwd.cross(Vector((0, 0, 1)))
to_sun = (right * -0.55 + Vector((0, 0, 0.62)) + fwd * -0.56).normalized()
sun.rotation_euler = to_sun.to_track_quat('Z', 'Y').to_euler()
sun_d.energy = 4.0; sun_d.color = (1.0, 0.93, 0.82); sun_d.angle = math.radians(2)

world = bpy.data.worlds.new('w'); sc.world = world; world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.42, 0.55, 0.85, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.55

# a ground plane that only bounces light back up, not seen by the camera
bpy.ops.mesh.primitive_plane_add(size=30, location=(mid.x, mid.y, 0))
ground = bpy.context.object; ground.name = 'ground'
set_only(ground, principled('grass', lin(90, 150, 60), 0.9))
ground.visible_camera = False
ground.visible_shadow = False

bpy.ops.wm.save_as_mainfile(filepath=OUT)
print('saved', OUT, 'frames', len(SWING))
