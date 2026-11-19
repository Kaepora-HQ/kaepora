"""How an item's model stands on a shop's shelf, measured from the game's own model and animation files, for
remake/shop_goods.tsv.

Usage: python3 -I -B tests/shop_goods.py <game.nup> <item>...
Prints the table's row for each item. A shop plays the pose `Pose_Shop` from the model's animations, or `Pose_Common`
where they have none. In that pose, in millimetres: the model's lowest and highest points over its origin, its width
along X, and how far it reaches from the upright through its origin. Only the shapes the item's own bone visibility
set shows count.

The model and animation files are bfres 10.4, and the offsets below were read off the game's files. In the bind pose,
with the item's own visibility set, this reader gives the ActorInfo bounds of every model remake/shop_goods.tsv measures
but the Business Scrub's seed, whose bounds the game took in its pose.
"""

import math
import pathlib
import struct
import sys
from compression import zstd

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import patch_test  # noqa: E402

POSES = ("Pose_Shop", "Pose_Common")
SEGMENT_SCALE_COMPENSATE = 1 << 23
EULER = 1 << 12
VISIBLE = 1


class Bfres:
    def __init__(self, data):
        assert data[:4] == b"FRES", data[:4]
        self.data = data

    def u8(self, at):
        return self.data[at]

    def u16(self, at):
        return struct.unpack_from("<H", self.data, at)[0]

    def u32(self, at):
        return struct.unpack_from("<I", self.data, at)[0]

    def u64(self, at):
        return struct.unpack_from("<Q", self.data, at)[0]

    def floats(self, at, count):
        return list(struct.unpack_from(f"<{count}f", self.data, at))

    def string(self, at):
        return self.data[at + 2:at + 2 + self.u16(at)].decode()

    def names(self, dictionary):
        """A dictionary's names in node order. Version 10 leaves its _DIC magic zero."""
        return [self.string(self.u64(dictionary + 8 + 16 * i + 8)) for i in range(1, self.u32(dictionary + 4) + 1)]

    def poses(self):
        """Each skeletal animation's first-frame bone values, by animation and bone name."""
        poses = {}
        if self.u64(0x58) == 0:
            return poses
        for i, name in enumerate(self.names(self.u64(0x60))):
            anim = self.u64(0x58) + 0x50 * i
            bones = {}
            for j in range(self.u16(anim + 0x4C)):
                bone = self.u64(anim + 0x28) + 0x38 * j
                flags, at, values = self.u32(bone + 0x28), self.u64(bone + 0x10), {"flags": self.u32(bone + 0x28)}
                for bit, key, count in ((0x8, "S", 3), (0x10, "R", 4), (0x20, "T", 3)):
                    if flags & bit:
                        values[key] = self.floats(at, count)
                        at += 4 * count
                bones[self.string(self.u64(bone))] = values
            poses[name] = bones
        return poses

    def visibility(self):
        """Each bone visibility animation's first-frame value, by animation and bone name."""
        sets = {}
        if self.u64(0x78) == 0:
            return sets
        for i, name in enumerate(self.names(self.u64(0x80))):
            anim = self.u64(0x78) + 0x60 * i
            count, bits = self.u16(anim + 0x58), self.u64(anim + 0x30)
            shown = int.from_bytes(self.data[bits:bits + (count + 31) // 32 * 4], "little")
            sets[name] = {self.string(self.u64(self.u64(anim + 0x38) + 8 * j)): bool(shown >> j & 1)
                          for j in range(count)}
        return sets


def rotation(r, euler):
    if euler:
        (cx, sx), (cy, sy), (cz, sz) = ((math.cos(a), math.sin(a)) for a in r[:3])
        return [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
                [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
                [-sy, sx * cy, cx * cy]]
    x, y, z, w = r
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def multiply(a, b):
    """3x4 affine matrices."""
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0) for j in range(4)]
            for i in range(3)]


def inverse(m):
    r = [row[:3] for row in m]
    det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
           + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
    inv = [[(r[(j + 1) % 3][(i + 1) % 3] * r[(j + 2) % 3][(i + 2) % 3]
             - r[(j + 1) % 3][(i + 2) % 3] * r[(j + 2) % 3][(i + 1) % 3]) / det for j in range(3)] for i in range(3)]
    return [inv[i] + [-sum(inv[i][k] * m[k][3] for k in range(3))] for i in range(3)]


def world_matrices(bones, pose, euler):
    """Each bone's model matrix. A bone that compensates its parent's scale undoes it before its own rotation and
    scale: child = parent * T * inverse(parent scale) * R * S."""
    out, scales = [], []
    for bone in bones:
        values = pose.get(bone["name"], {})
        s, r, t = (values.get(key, bone[key]) for key in ("S", "R", "T"))
        rs = rotation(r, euler)
        parent = bone["parent"]
        undo = [1 / v for v in scales[parent]] if parent >= 0 and values.get("flags", bone["flags"]) & \
            SEGMENT_SCALE_COMPENSATE else [1, 1, 1]
        local = [[rs[i][j] * s[j] * undo[i] for j in range(3)] + [t[i]] for i in range(3)]
        out.append(local if parent < 0 else multiply(out[parent], local))
        scales.append(s)
    return out


def posed_points(model, pose, shown):
    """Every vertex of the file's model that a shown shape holds, posed. A shape shows when its bone does: by the
    visibility set, or else by the bone's own flag. A skinned vertex's indices name entries of the skeleton's matrix
    list; a vertex of a shape with one influence is in its bone's space."""
    fmdl = model.u64(0x28)
    skeleton = model.u64(fmdl + 0x18)
    bones = []
    for i in range(model.u16(skeleton + 0x38)):
        at = model.u64(skeleton + 0x10) + 0x58 * i
        bones.append({"name": model.string(model.u64(at)), "parent": struct.unpack_from("<h", model.data, at + 0x22)[0],
                      "flags": model.u32(at + 0x2C), "S": model.floats(at + 0x30, 3), "R": model.floats(at + 0x3C, 4),
                      "T": model.floats(at + 0x4C, 3)})
    matrices = [model.u16(model.u64(skeleton + 0x18) + 2 * i)
                for i in range(model.u16(skeleton + 0x3A) + model.u16(skeleton + 0x3C))]
    euler = bool(model.u32(skeleton + 4) & EULER)
    bind = world_matrices(bones, {}, euler)
    posed = world_matrices(bones, pose, euler)
    skin = [multiply(p, inverse(b)) for p, b in zip(posed, bind)]
    memory = model.u64(model.u64(0xB0) + 8)
    points = []
    for i in range(model.u16(fmdl + 0x6A)):
        shape = model.u64(fmdl + 0x28) + 0x60 * i
        bone = bones[model.u16(shape + 0x54)]
        if not shown.get(bone["name"], bool(bone["flags"] & VISIBLE)):
            continue
        influences = model.u8(shape + 0x5A)
        vertices = model.u64(fmdl + 0x20) + 0x58 * model.u16(shape + 0x56)
        attributes = attributes_of(model, memory, vertices)
        indices, weights = attributes.get("_i0"), attributes.get("_w0")
        if "_i1" in attributes:
            indices = [first + second for first, second in zip(indices, attributes["_i1"])]
            weights = [first + second for first, second in zip(weights, attributes["_w1"])]
        for n, (x, y, z) in enumerate(attributes["_p0"]):
            if influences == 0:
                parts = [(posed[model.u16(shape + 0x54)], 1)]
            elif influences == 1:
                parts = [(posed[matrices[indices[n][0]]], 1)]
            else:
                parts = [(skin[matrices[k]], w) for k, w in zip(indices[n][:influences], weights[n][:influences])]
            points.append([sum(w * (m[r][0] * x + m[r][1] * y + m[r][2] * z + m[r][3]) for m, w in parts)
                           for r in range(3)])
    return points


def attributes_of(model, memory, vertices):
    """A vertex buffer's positions, bone indices and weights, by attribute name. Each buffer's data follows the one
    before, aligned to 8 bytes."""
    count, buffers = model.u32(vertices + 0x50), model.u8(vertices + 0x4D)
    strides = [model.u32(model.u64(vertices + 0x38) + 0x10 * i) for i in range(buffers)]
    starts, at = [], memory + model.u32(vertices + 0x48)
    for stride in strides:
        starts.append(at)
        at = (at + stride * count + 7) & ~7
    formats = {0x1505: ("<4e", 8), 0x1805: ("<3f", 12), 0x0203: ("<B", 1), 0x0903: ("<2B", 2), 0x0B03: ("<4B", 4),
               0x0201: ("<B", 1), 0x0901: ("<2B", 2), 0x0B01: ("<4B", 4)}
    out = {}
    for i in range(model.u8(vertices + 0x4C)):
        attribute = model.u64(vertices + 0x08) + 0x10 * i
        name = model.string(model.u64(attribute))
        if name not in ("_p0", "_i0", "_i1", "_w0", "_w1"):
            continue
        kind, offset, buffer = model.u32(attribute + 8), model.u16(attribute + 12), model.u16(attribute + 14)
        layout, size = formats[kind]
        values = [struct.unpack_from(layout, model.data, starts[buffer] + strides[buffer] * n + offset)
                  for n in range(count)]
        if name == "_p0":
            values = [v[:3] for v in values]
        elif name.startswith("_w"):
            values = [[b / 255 for b in v] for v in values]
        out[name] = values
    return out


def row(game, actor_info, item):
    """The item's row of remake/shop_goods.tsv but its goods column."""
    info = actor_info[item]
    files = game.pack(game.read(f"Pack/Actor/{item}.pack.zs"))
    components = patch_test.resolved(files, f"Actor/{item}.engine__actor__ActorParam.bgyml")["Components"]
    model_info = patch_test.resolved(files, components["ModelInfoRef"].lstrip("?"))
    poses, sets = {}, {}
    for resource in info["AnimationResources"]:
        path = f"Model/{resource['ModelProjectName']}.anim.bfres.zs"
        if path in game.files:
            animations = Bfres(zstd.decompress(game.read(path), zstd_dict=game.zs_dictionary))
            for name, pose in animations.poses().items():
                poses.setdefault(name, pose)
            for name, shown in animations.visibility().items():
                sets.setdefault(name, shown)
    pose = next(name for name in POSES if name in poses)
    shown = {}
    for anim in model_info.get("ModelVariationAnims", []):
        if anim["Fmab"].endswith(".fvbb"):
            shown |= sets[anim["Fmab"].rsplit("/", 1)[1].removesuffix(".fvbb")]
    model = Bfres(zstd.decompress(game.read(f"Model/{info['ModelProjectName']}.{info['FmdbName']}.bfres.zs"),
                                  zstd_dict=game.zs_dictionary))
    points = posed_points(model, poses[pose], shown)
    ys = [p[1] for p in points]
    xs = [p[0] for p in points]
    reach = max(math.hypot(p[0], p[2]) for p in points)
    return [pose] + [str(round(v * 1000)) for v in (min(ys), max(ys), max(xs) - min(xs), reach)]


def actor_infos(game):
    return {r["__RowId"]: r for r in patch_test.byml(zstd.decompress(game.read(patch_test.ACTOR_INFO)))}


def main():
    game = patch_test.Game(sys.argv[1])
    infos = actor_infos(game)
    for item in sys.argv[2:]:
        print("\t".join([item, ""] + row(game, infos, item)))


if __name__ == "__main__":
    main()
