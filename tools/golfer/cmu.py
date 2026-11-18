"""Reads CMU mocap ASF/AMC files and runs forward kinematics with numpy.

Rotations are world-space 3x3 matrices in the ASF frame (Y up). In the ASF
rest pose every bone's world rotation is the identity, so a bone's world
rotation at a frame is also its change from rest.
"""
import numpy as np


def euler(rx, ry, rz):
    """Degrees, applied X then Y then Z about fixed axes (R = Rz Ry Rx)."""
    a, b, c = np.radians([rx, ry, rz])
    X = np.array([[1, 0, 0], [0, np.cos(a), -np.sin(a)], [0, np.sin(a), np.cos(a)]])
    Y = np.array([[np.cos(b), 0, np.sin(b)], [0, 1, 0], [-np.sin(b), 0, np.cos(b)]])
    Z = np.array([[np.cos(c), -np.sin(c), 0], [np.sin(c), np.cos(c), 0], [0, 0, 1]])
    return Z @ Y @ X


class Bone:
    def __init__(self, name):
        self.name = name
        self.direction = np.zeros(3)
        self.length = 0.0
        self.axis = np.eye(3)
        self.dof = []
        self.parent = None
        self.children = []


def read_asf(path):
    bones = {'root': Bone('root')}
    root_dof = []
    lines = [l.strip() for l in open(path) if l.strip() and not l.startswith('#')]
    i = 0
    section = None
    cur = None
    while i < len(lines):
        l = lines[i]
        if l.startswith(':'):
            section = l.split()[0][1:]
            if section == 'root':
                pass
            i += 1
            continue
        w = l.split()
        if section == 'root' and w[0] == 'order':
            root_dof = [x.lower() for x in w[1:]]
        elif section == 'bonedata':
            if w[0] == 'begin':
                cur = None
            elif w[0] == 'name':
                cur = Bone(w[1]); bones[w[1]] = cur
            elif w[0] == 'direction':
                cur.direction = np.array([float(x) for x in w[1:4]])
            elif w[0] == 'length':
                cur.length = float(w[1])
            elif w[0] == 'axis':
                cur.axis = euler(*[float(x) for x in w[1:4]])
            elif w[0] == 'dof':
                cur.dof = w[1:]
        elif section == 'hierarchy':
            if w[0] not in ('begin', 'end'):
                p = bones[w[0]]
                for c in w[1:]:
                    bones[c].parent = p; p.children.append(bones[c])
        i += 1
    bones['root'].dof = root_dof
    return bones


def read_amc(path):
    frames = []
    cur = None
    for l in open(path):
        l = l.strip()
        if not l or l.startswith('#') or l.startswith(':'):
            continue
        w = l.split()
        if w[0].isdigit() and len(w) == 1:
            cur = {}; frames.append(cur)
        else:
            cur[w[0]] = [float(x) for x in w[1:]]
    return frames


def fk(bones, frame):
    """Returns {name: (world rotation, joint start, joint end)} in ASF units."""
    out = {}
    root = bones['root']
    vals = dict(zip(root.dof, frame['root']))
    pos = np.array([vals['tx'], vals['ty'], vals['tz']])
    rot = euler(vals['rx'], vals['ry'], vals['rz'])
    out['root'] = (rot, pos, pos)

    def walk(b):
        p_rot, _, p_end = out[b.parent.name]
        v = dict(zip(b.dof, frame.get(b.name, [])))
        m = euler(v.get('rx', 0), v.get('ry', 0), v.get('rz', 0))
        r = p_rot @ b.axis @ m @ b.axis.T
        end = p_end + r @ (b.direction * b.length)
        out[b.name] = (r, p_end, end)
        for c in b.children:
            walk(c)
    for c in root.children:
        walk(c)
    return out
