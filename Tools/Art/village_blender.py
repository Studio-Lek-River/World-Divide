"""Builds the greybox of River Crossing's in-house village set and the bridge span's damage states (#408) into
Art/Shared/village/, the placeholders the Codex art of the building workflow replaces:

  "D:/Program Files/Blender Foundation/Blender 5.1/blender.exe" -b --python Tools/Art/village_blender.py

One modern temperate-European village style at real scale (docs/art/village/VILLAGE_BRIEF.md): BUILDINGS lists each type's
footprint, eaves and ridge. Every type comes in three states, the meshes ARTSCivilBuilding swaps as it loses health:

- SM_Village_<Type>: rendered walls (planks for the barn), a gable roof, windows, sills and doors, the ridge along X and
  the main door in the +X gable, the origin on the ground in the middle of the footprint. The walls reach SINK under the
  ground, so a slope does not show a gap.
- SM_Village_<Type>_Damaged: the same with a hole through the roof (rafters showing), a breach in the -Y wall, blown
  windows and debris at its foot.
- SM_Village_<Type>_Rubble: wall stumps, fallen roof slabs and a heap, the ruin that stays for the match.
- SM_Bridge_Span_Damaged and SM_Bridge_Span_Destroyed: bridge_blender.py's span with a shell crater and a broken parapet,
  and collapsed into the river, its two halves hanging from the piers. The intact span stays SM_Bridge_Span.

The garrison data of each type (#252) comes from the same openings: a firing position just inside every window and door,
at eye height on its floor, looking out, and the entry point outside the main door. Material slots: Wall, Roof, Trim, Glass,
Wood, Char (blown openings, scorch) and Rubble; the spans keep Concrete and Asphalt. UCX_ boxes are the collision the
import keeps. Metres in Blender, centimetres in the FBX, +X forward and Z up, as bridge_blender.py exports. It writes
village.json (each mesh's slots, triangles, collision boxes and bounds, and each type's garrison data, in Unreal
centimetres and degrees), which Tools/Editor/village_art.py reads. The output is committed; rerun only when the set changes.

Superseded (#432): Art/Shared/village/ now holds the #431 art and a village.json merged from Art/Shared/village/final/.
Do not rerun this script there; it would overwrite the accepted set with the greybox.
"""

import json
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_blender as bridge  # noqa: E402

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
VILLAGE_DIR = os.path.join(PROJECT_DIR, "Art", "Shared", "village")

SINK = 0.5
WALL = 0.3
STOREY = 3.0
ROOF_THICKNESS = 0.22
OVERHANG = 0.45
# A window's glass and its sill stand this far proud of the wall.
PROUD = 0.06
# A shooter stands this far inside an opening, at this height over its floor.
INSIDE = 0.5
EYE = 1.5
DOOR = (1.1, 2.2)
WINDOW = (1.2, 1.4)
WINDOW_SILL = 0.9
WINDOW_PITCH = 3.0

# Each type: display name, length along X, width along Y, eaves and ridge height, wall slot, storeys with windows, garrison
# capacity, health, and its openings' overrides. Footprints fit the greybox's village plots (rivercrossing_greybox.py).
BUILDINGS = {
    "HouseA": dict(name="Village House", length=11.0, width=9.0, eaves=5.8, ridge=9.6, wall="Wall", storeys=2,
                   capacity=5, health=1500.0),
    "HouseB": dict(name="Semi-Detached Houses", length=14.0, width=9.0, eaves=6.0, ridge=10.0, wall="Wall", storeys=2,
                   capacity=6, health=1800.0, side_doors=(-3.5, 3.5)),
    "HouseC": dict(name="Corner House", length=9.5, width=9.5, eaves=5.6, ridge=9.0, wall="Wall", storeys=2,
                   capacity=4, health=1300.0),
    "Farmhouse": dict(name="Farmhouse", length=18.0, width=11.0, eaves=4.8, ridge=10.5, wall="Wall", storeys=2,
                      capacity=8, health=2200.0),
    "Barn": dict(name="Barn", length=20.0, width=13.0, eaves=5.0, ridge=11.0, wall="Wood", storeys=1, capacity=10,
                 health=2000.0, front_door=(4.0, 4.5), window_pitch=6.5),
    "Workshop": dict(name="Workshop", length=16.0, width=12.0, eaves=5.5, ridge=6.6, wall="Wall", storeys=1, capacity=6,
                     health=2000.0, front_door=(3.5, 4.0), front_doors=(-3.0, 3.0), window_sill=2.6),
    "Church": dict(name="Church", length=22.0, width=11.0, eaves=8.0, ridge=15.0, wall="Wall", storeys=1, capacity=10,
                   health=4000.0, window=(1.2, 3.6), window_sill=2.6, window_pitch=4.0, tower=(6.5, 26.0, 36.0)),
}

# The span's damage (bridge_blender.py's span, 30 m): a crater in the carriageway and a gap in the +Y parapet; destroyed,
# the middle is gone and each half hangs from its pier, tilted SPAN_DROP degrees, its broken end in the river.
CRATER = ((-3.0, 2.0), (-2.0, 3.0))
PARAPET_GAP = (4.0, 10.0)
SPAN_PIECE = 11.0
SPAN_DROP = 24.0
SPAN_HEALTH = 6000.0

SLOTS = ("Wall", "Roof", "Trim", "Glass", "Wood", "Char", "Rubble", "Concrete", "Asphalt")


def material(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


class Builder:
    """One mesh of boxes, wedges and pyramids, each face on a slot, under an optional placing matrix, with its UCX_ boxes.
    Only the slots it uses become material slots, in first-use order."""

    def __init__(self, name):
        self.name = name
        self.mesh = bmesh.new()
        self.mesh.loops.layers.uv.new("UVMap")
        self.collision = []
        self.slots = []
        self.matrix = Matrix.Identity(4)

    def _tag(self, verts, slot):
        if slot not in self.slots:
            self.slots.append(slot)
        for face in {face for vert in verts for face in vert.link_faces}:
            face.material_index = self.slots.index(slot)

    def box(self, low, high, slot="Concrete", collides=False):
        size = [h - l for l, h in zip(low, high)]
        center = [(l + h) / 2.0 for l, h in zip(low, high)]
        matrix = self.matrix @ Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0))
        made = bmesh.ops.create_cube(self.mesh, size=1.0, matrix=matrix, calc_uvs=True)
        self._tag(made["verts"], slot)
        if collides:
            self.collision.append((low, high))

    def turned(self, center, size, degrees, slot):
        """A box of size about center, turned by the XYZ Euler angles in degrees."""
        rotation = Euler([math.radians(angle) for angle in degrees], "XYZ").to_matrix().to_4x4()
        matrix = self.matrix @ Matrix.Translation(center) @ rotation @ Matrix.Diagonal((*size, 1.0))
        made = bmesh.ops.create_cube(self.mesh, size=1.0, matrix=matrix, calc_uvs=True)
        self._tag(made["verts"], slot)

    def gable(self, x0, x1, half_width, bottom, top, slot):
        """The solid triangle under a gable roof, its ridge along X at the middle."""
        ends = []
        for x in (x0, x1):
            ends.append([self.mesh.verts.new(self.matrix @ Matrix.Translation((x, y, z)).to_translation())
                         for y, z in ((-half_width, bottom), (half_width, bottom), (0.0, top))])
        faces = [self.mesh.faces.new(ends[0][::-1]), self.mesh.faces.new(ends[1])]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            faces.append(self.mesh.faces.new((ends[0][a], ends[0][b], ends[1][b], ends[1][a])))
        bmesh.ops.recalc_face_normals(self.mesh, faces=faces)
        self._tag([vert for end in ends for vert in end], slot)

    def pyramid(self, center, half, bottom, top, slot):
        matrix = self.matrix @ Matrix.Translation((center[0], center[1], (bottom + top) / 2.0)) @ Matrix.Rotation(
            math.radians(45.0), 4, "Z")
        made = bmesh.ops.create_cone(self.mesh, cap_ends=True, cap_tris=True, segments=4, radius1=half * math.sqrt(2.0),
                                     radius2=0.0, depth=top - bottom, matrix=matrix, calc_uvs=True)
        self._tag(made["verts"], slot)

    def finish(self):
        mesh = bpy.data.meshes.new(self.name)
        self.mesh.to_mesh(mesh)
        self.mesh.free()
        for slot in self.slots:
            mesh.materials.append(material(slot))
        mesh.update()
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.collection.objects.link(obj)
        hulls = []
        for index, (low, high) in enumerate(self.collision):
            built = bmesh.new()
            size = [h - l for l, h in zip(low, high)]
            center = [(l + h) / 2.0 for l, h in zip(low, high)]
            bmesh.ops.create_cube(built, size=1.0, matrix=Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0)))
            hull_mesh = bpy.data.meshes.new(f"UCX_{self.name}_{index:02d}")
            built.to_mesh(hull_mesh)
            built.free()
            hull = bpy.data.objects.new(hull_mesh.name, hull_mesh)
            bpy.context.collection.objects.link(hull)
            hulls.append(hull)
        return obj, hulls


# --- Buildings ----------------------------------------------------------------------------------------------------------

# A face: its axis, its side and the axis along it. Unreal mirrors Blender's Y, so its outward yaw there is atan2(-ny, nx).
FACES = {"+X": (0, 1.0, 1), "-X": (0, -1.0, 1), "+Y": (1, 1.0, 0), "-Y": (1, -1.0, 0)}


def face_yaw(face):
    axis, side, _ = FACES[face]
    normal = (side, 0.0) if axis == 0 else (0.0, side)
    return round(math.degrees(math.atan2(-normal[1], normal[0])), 2) + 0.0


def openings(spec):
    """Every window and door: (face, along, sill, width, height, kind, floor), the main door first."""
    length, width = spec["length"], spec["width"]
    window = spec.get("window", WINDOW)
    sill = spec.get("window_sill", WINDOW_SILL)
    pitch = spec.get("window_pitch", WINDOW_PITCH)
    door = spec.get("front_door", DOOR)
    found = []
    for along in spec.get("front_doors", (0.0,)):
        found.append(("+X", along, 0.0, door[0], door[1], "door", 0))
    for along in spec.get("side_doors", ()):
        found.append(("+Y", along, 0.0, DOOR[0], DOOR[1], "door", 0))
    faces = (("+X", width), ("+Y", length), ("-Y", length)) if "tower" in spec else (
        ("+X", width), ("-X", width), ("+Y", length), ("-Y", length))
    for face, span in faces:
        count = max(1, int((span - 2.0) // pitch))
        for floor in range(spec["storeys"]):
            for index in range(count):
                along = (index - (count - 1) / 2.0) * pitch
                if floor == 0 and any(o[0] == face and abs(o[1] - along) < (o[3] + window[0]) / 2.0 + 0.3 for o in found):
                    continue
                found.append((face, along, sill + floor * STOREY, window[0], window[1], "window", floor))
    if "tower" not in spec:
        # A gable window in the attic when the roof leaves room for one.
        if spec["ridge"] - spec["eaves"] > 2.8:
            for face in ("+X", "-X"):
                found.append((face, 0.0, spec["eaves"] + 0.2, 0.9, 1.1, "window", spec["storeys"]))
    return found


def face_box(face, along, bottom, top, half_along, depth_low, depth_high, half_extent):
    """The box of an opening on a face: along it, between bottom and top, from depth_low to depth_high off the wall plane."""
    axis, side, other = FACES[face]
    low, high = [0.0, 0.0, bottom], [0.0, 0.0, top]
    plane = side * half_extent
    a, b = plane + side * depth_low, plane + side * depth_high
    low[axis], high[axis] = min(a, b), max(a, b)
    low[other], high[other] = along - half_along, along + half_along
    return tuple(low), tuple(high)


def walls(builder, spec, slot, breach=None, top=None):
    """The four walls up to the eaves (or top); breach is (x0, x1, z0, z1), a hole through the -Y wall."""
    hl, hw = spec["length"] / 2.0, spec["width"] / 2.0
    top = spec["eaves"] if top is None else top
    builder.box((hl - WALL, -hw, -SINK), (hl, hw, top), slot)
    builder.box((-hl, -hw, -SINK), (-hl + WALL, hw, top), slot)
    builder.box((-hl + WALL, hw - WALL, -SINK), (hl - WALL, hw, top), slot)
    if breach is None:
        builder.box((-hl + WALL, -hw, -SINK), (hl - WALL, -hw + WALL, top), slot)
        return
    x0, x1, z0, z1 = breach
    builder.box((-hl + WALL, -hw, -SINK), (x0, -hw + WALL, top), slot)
    builder.box((x1, -hw, -SINK), (hl - WALL, -hw + WALL, top), slot)
    builder.box((x0, -hw, -SINK), (x1, -hw + WALL, z0), slot)
    builder.box((x0, -hw, z1), (x1, -hw + WALL, top), slot)


def roof_slab(builder, spec, x0, x1, side, slot="Roof"):
    """One side's roof plane (side +1 or -1 in Y) from x0 to x1, from the ridge out over the eaves by OVERHANG."""
    hw, rise = spec["width"] / 2.0, spec["ridge"] - spec["eaves"]
    slope = math.atan2(rise, hw)
    run = math.hypot(hw, rise) + OVERHANG
    # The slab's top face runs from the ridge down; its middle lies half a run down the slope and half its thickness under.
    down = run / 2.0
    center_y = side * down * math.cos(slope) + side * ROOF_THICKNESS / 2.0 * math.sin(slope)
    center_z = spec["ridge"] - down * math.sin(slope) + ROOF_THICKNESS / 2.0 * math.cos(slope) - 0.05
    builder.turned(((x0 + x1) / 2.0, center_y, center_z), (x1 - x0, run, ROOF_THICKNESS), (-side * math.degrees(slope), 0.0, 0.0),
                   slot)


def roof(builder, spec, hole=None):
    """The gable and both roof planes; hole is (x0, x1), a gap in the -Y plane with three rafters across it."""
    hl, hw = spec["length"] / 2.0, spec["width"] / 2.0
    builder.gable(-hl, hl, hw, spec["eaves"], spec["ridge"] - 0.05, spec["wall"])
    ends = (-hl - OVERHANG, hl + OVERHANG)
    roof_slab(builder, spec, *ends, 1.0)
    if hole is None:
        roof_slab(builder, spec, *ends, -1.0)
        return
    roof_slab(builder, spec, ends[0], hole[0], -1.0)
    roof_slab(builder, spec, hole[1], ends[1], -1.0)
    slope = math.degrees(math.atan2(spec["ridge"] - spec["eaves"], hw))
    for index in range(3):
        x = hole[0] + (hole[1] - hole[0]) * (index + 1) / 4.0
        builder.turned((x, -hw / 2.0, (spec["eaves"] + spec["ridge"]) / 2.0), (0.12, math.hypot(hw, spec["ridge"] - spec["eaves"]), 0.2),
                       (slope, 0.0, 0.0), "Wood")
    # The scorched attic floor seen through the hole.
    builder.box((hole[0], -hw + WALL, spec["eaves"] - 0.3), (hole[1], 0.0, spec["eaves"] - 0.1), "Char")


def tower_box(spec, top):
    """The tower's box: against the -X gable, so the main door in the +X gable stays free."""
    side = spec["tower"][0]
    hl = spec["length"] / 2.0
    return (-hl - side, -side / 2.0, -SINK), (-hl, side / 2.0, top)


def tower(builder, spec, top=None, spire=True):
    side, height, spire_top = spec["tower"]
    top = height if top is None else top
    low, high = tower_box(spec, top)
    builder.box(low, high, spec["wall"], collides=True)
    if spire:
        center = (low[0] + side / 2.0, 0.0)
        builder.pyramid(center, side / 2.0 + 0.3, top, spire_top, "Roof")
        # The belfry's louvres, one in each free face.
        for face_center, size in (((low[0] - PROUD / 2.0, 0.0), (PROUD, 1.6)),
                                  ((center[0], side / 2.0 + PROUD / 2.0), (1.6, PROUD)),
                                  ((center[0], -side / 2.0 - PROUD / 2.0), (1.6, PROUD))):
            builder.box((face_center[0] - size[0] / 2.0, face_center[1] - size[1] / 2.0, top - 6.0),
                        (face_center[0] + size[0] / 2.0, face_center[1] + size[1] / 2.0, top - 3.0), "Wood")


def place_openings(builder, spec, found, blown=()):
    """Glass (or Char where blown), a sill under each window and a door leaf in each door."""
    hl, hw = spec["length"] / 2.0, spec["width"] / 2.0
    for index, (face, along, sill, width, height, kind, _) in enumerate(found):
        extent = hl if FACES[face][0] == 0 else hw
        if kind == "door":
            slot = "Char" if index in blown else ("Trim" if width > 2.0 else "Wood")
            builder.box(*face_box(face, along, 0.0, height, width / 2.0, -0.05, PROUD, extent), slot)
            continue
        builder.box(*face_box(face, along, sill, sill + height, width / 2.0, -0.05, PROUD / 2.0, extent),
                    "Char" if index in blown else "Glass")
        builder.box(*face_box(face, along, sill - 0.08, sill, width / 2.0 + 0.08, 0.0, PROUD * 2.0, extent), "Trim")


def body_collision(builder, spec):
    hl, hw = spec["length"] / 2.0, spec["width"] / 2.0
    builder.collision.append(((-hl, -hw, -SINK), (hl, hw, spec["eaves"])))
    builder.collision.append(((-hl, -hw / 2.0, spec["eaves"]), (hl, hw / 2.0, spec["ridge"])))


def pristine(key, spec):
    builder = Builder(f"SM_Village_{key}")
    walls(builder, spec, spec["wall"])
    roof(builder, spec)
    if "tower" in spec:
        tower(builder, spec)
    place_openings(builder, spec, openings(spec))
    body_collision(builder, spec)
    return builder.finish()


def damaged(key, spec):
    builder = Builder(f"SM_Village_{key}_Damaged")
    hl, hw = spec["length"] / 2.0, spec["width"] / 2.0
    breach = (-hl * 0.45, -hl * 0.45 + min(3.0, spec["length"] * 0.25), 0.6, min(3.4, spec["eaves"] - 0.8))
    walls(builder, spec, spec["wall"], breach)
    roof(builder, spec, (hl * 0.05, hl * 0.05 + min(4.0, spec["length"] * 0.3)))
    if "tower" in spec:
        # The spire is shot away: the tower broken off under the belfry.
        tower(builder, spec, top=spec["tower"][1] - 6.0, spire=False)
    # The ground-floor openings in the breach went with the wall; every other one left is blown out.
    found = [o for o in openings(spec) if not (o[0] == "-Y" and breach[0] - 0.8 < o[1] < breach[1] + 0.8 and o[6] == 0)]
    place_openings(builder, spec, found, {index for index in range(len(found)) if index % 2 == 1})
    rng = random.Random(builder.name)
    for _ in range(7):
        x = rng.uniform(breach[0] - 1.0, breach[1] + 1.0)
        size = (rng.uniform(0.4, 1.2), rng.uniform(0.4, 1.0), rng.uniform(0.25, 0.6))
        builder.turned((x, -hw - rng.uniform(0.4, 2.2), size[2] / 2.0 - 0.1), size,
                       (rng.uniform(-20.0, 20.0), rng.uniform(-20.0, 20.0), rng.uniform(0.0, 90.0)), "Rubble")
    body_collision(builder, spec)
    return builder.finish()


def rubble(key, spec):
    builder = Builder(f"SM_Village_{key}_Rubble")
    rng = random.Random(builder.name)
    hl, hw = spec["length"] / 2.0, spec["width"] / 2.0
    # Wall stumps: the walls in pieces of about two metres, each broken off at its own height.
    for axis_length, fixed, along_x in ((spec["length"], hw - WALL / 2.0, True), (spec["length"], -hw + WALL / 2.0, True),
                                        (spec["width"], hl - WALL / 2.0, False), (spec["width"], -hl + WALL / 2.0, False)):
        pieces = max(2, int(axis_length // 2.0))
        step = axis_length / pieces
        for index in range(pieces):
            if rng.random() < 0.25:
                continue
            a = -axis_length / 2.0 + index * step
            height = rng.uniform(0.5, 2.8)
            if along_x:
                builder.box((a, fixed - WALL / 2.0, -SINK), (a + step, fixed + WALL / 2.0, height), spec["wall"])
            else:
                builder.box((fixed - WALL / 2.0, a, -SINK), (fixed + WALL / 2.0, a + step, height), spec["wall"])
    if "tower" in spec:
        # The tower stands to a third of its height, the landmark's ruin.
        builder.box(*tower_box(spec, spec["tower"][1] / 3.0), spec["wall"], collides=True)
    # Two roof slabs slid down, and the heap of the floors and the roof inside.
    for side in (-1.0, 1.0):
        builder.turned((rng.uniform(-hl * 0.3, hl * 0.3), side * hw * 0.45, 1.0),
                       (spec["length"] * 0.45, spec["width"] * 0.35, ROOF_THICKNESS),
                       (side * rng.uniform(12.0, 25.0), rng.uniform(-6.0, 6.0), rng.uniform(-8.0, 8.0)), "Roof")
    for index in range(int(spec["length"] * spec["width"] / 6.0)):
        size = (rng.uniform(0.8, 2.6), rng.uniform(0.8, 2.2), rng.uniform(0.3, 1.2))
        x, y = rng.uniform(-hl + 1.0, hl - 1.0), rng.uniform(-hw + 1.0, hw - 1.0)
        # Higher in the middle: a heap, not a floor of blocks.
        lift = 1.4 * (1.0 - max(abs(x) / hl, abs(y) / hw)) + rng.uniform(-0.2, 0.3)
        builder.turned((x, y, max(0.1, lift)), size, (rng.uniform(-25.0, 25.0), rng.uniform(-25.0, 25.0), rng.uniform(0.0, 180.0)),
                       "Char" if index % 5 == 0 else ("Wood" if index % 7 == 0 else "Rubble"))
    builder.collision.insert(0, ((-hl, -hw, -SINK), (hl, hw, 1.5)))
    return builder.finish()


def garrison(spec):
    """The type's capacity, entry point and firing positions in the building's frame, Unreal cm and degrees."""
    found = openings(spec)
    hl, hw = spec["length"] / 2.0, spec["width"] / 2.0
    positions = []
    for index, (face, along, sill, width, height, kind, floor) in enumerate(found):
        axis, side, other = FACES[face]
        # A high window has a platform under it.
        point = [0.0, 0.0, max(floor * STOREY + EYE, sill + 0.5) if kind == "window" else EYE]
        point[axis] = side * ((hl if axis == 0 else hw) - INSIDE)
        point[other] = along
        positions.append({"name": f"{kind.capitalize()}{face.replace('+', 'P').replace('-', 'N')}{index:02d}",
                          "location": unreal_cm(point), "yaw": face_yaw(face), "arc": 60.0 if kind == "door" else 90.0})
    if "tower" in spec:
        side, height, _ = spec["tower"]
        low, _ = tower_box(spec, height)
        center_x = low[0] + side / 2.0
        for face, point in (("-X", [low[0] + INSIDE, 0.0, height - 4.5]), ("+Y", [center_x, side / 2.0 - INSIDE, height - 4.5]),
                            ("-Y", [center_x, -side / 2.0 + INSIDE, height - 4.5])):
            positions.append({"name": f"Belfry{face.replace('+', 'P').replace('-', 'N')}", "location": unreal_cm(point),
                              "yaw": face_yaw(face), "arc": 90.0})
    door = found[0]
    entry = [hl + 3.0, door[1], 0.0] if door[0] == "+X" else [door[1], hw + 3.0, 0.0]
    return {"capacity": min(spec["capacity"], len(positions)), "entry": unreal_cm(entry), "firing_positions": positions}


# --- Bridge span ----------------------------------------------------------------------------------------------------------


def deck(builder, x0, x1, crater=None, parapet_gap=None):
    """bridge_blender.py's span deck from x0 to x1: carriageway (with crater a hole in it), slab, fascias, parapets
    (parapet_gap a gap in the +Y one) and girders."""
    half_width = bridge.DECK_WIDTH / 2.0
    carriage = bridge.CARRIAGEWAY / 2.0
    if crater is None:
        builder.box((x0, -carriage, -bridge.ASPHALT), (x1, carriage, 0.0), "Asphalt")
    else:
        (cx0, cx1), (cy0, cy1) = crater
        builder.box((x0, -carriage, -bridge.ASPHALT), (cx0, carriage, 0.0), "Asphalt")
        builder.box((cx1, -carriage, -bridge.ASPHALT), (x1, carriage, 0.0), "Asphalt")
        builder.box((cx0, -carriage, -bridge.ASPHALT), (cx1, cy0, 0.0), "Asphalt")
        builder.box((cx0, cy1, -bridge.ASPHALT), (cx1, carriage, 0.0), "Asphalt")
        builder.box((cx0, cy0, -bridge.ASPHALT - 0.2), (cx1, cy1, -bridge.ASPHALT - 0.05), "Char")
    builder.box((x0, -half_width, -bridge.ASPHALT - bridge.SLAB), (x1, half_width, -bridge.ASPHALT), "Concrete")
    for side in (-1.0, 1.0):
        edge = side * half_width
        builder.box((x0, min(edge, edge - side * 0.35), -bridge.ASPHALT - bridge.SLAB - bridge.FASCIA_DROP),
                    (x1, max(edge, edge - side * 0.35), -bridge.ASPHALT - bridge.SLAB), "Concrete")
        if side > 0 and parapet_gap:
            bridge.parapet(builder, x0, parapet_gap[0], side, collides=False)
            bridge.parapet(builder, parapet_gap[1], x1, side, collides=False)
        else:
            bridge.parapet(builder, x0, x1, side, collides=False)
    pitch = (bridge.DECK_WIDTH - 2.0) / bridge.GIRDERS
    for index in range(bridge.GIRDERS):
        y = -half_width + 1.0 + pitch * (index + 0.5)
        builder.box((x0, y - bridge.GIRDER_WIDTH / 2.0, -bridge.DECK_DEPTH), (x1, y + bridge.GIRDER_WIDTH / 2.0, -bridge.ASPHALT - bridge.SLAB),
                    "Concrete")
        builder.box((x0, y - bridge.GIRDER_WIDTH, -bridge.DECK_DEPTH), (x1, y + bridge.GIRDER_WIDTH, -bridge.DECK_DEPTH + 0.25), "Concrete")


def span_parapet_collision(builder, x0, x1, side):
    inner, outer = side * bridge.CARRIAGEWAY / 2.0, side * bridge.DECK_WIDTH / 2.0
    builder.collision.append(((x0, min(inner, outer), 0.0), (x1, max(inner, outer), bridge.PARAPET_HEIGHT)))


def span_damaged():
    builder = Builder("SM_Bridge_Span_Damaged")
    half = bridge.SPAN_LENGTH / 2.0
    deck(builder, -half, half, CRATER, PARAPET_GAP)
    rng = random.Random(builder.name)
    for _ in range(6):
        size = (rng.uniform(0.3, 0.9), rng.uniform(0.3, 0.8), rng.uniform(0.2, 0.4))
        builder.turned((rng.uniform(*CRATER[0]) + rng.uniform(-2.0, 2.0), rng.uniform(-4.0, 4.0), size[2] / 2.0), size,
                       (rng.uniform(-15.0, 15.0), rng.uniform(-15.0, 15.0), rng.uniform(0.0, 90.0)), "Rubble")
    half_width = bridge.DECK_WIDTH / 2.0
    builder.collision.append(((-half, -half_width, -bridge.DECK_DEPTH), (half, half_width, 0.0)))
    span_parapet_collision(builder, -half, half, -1.0)
    span_parapet_collision(builder, -half, PARAPET_GAP[0], 1.0)
    span_parapet_collision(builder, PARAPET_GAP[1], half, 1.0)
    return builder.finish()


def span_destroyed():
    builder = Builder("SM_Bridge_Span_Destroyed")
    half = bridge.SPAN_LENGTH / 2.0
    half_width = bridge.DECK_WIDTH / 2.0
    for side in (-1.0, 1.0):
        # Hinged at its pier's end, its free end toward the middle, dropped into the river.
        builder.matrix = Matrix.Translation((side * half, 0.0, 0.0)) @ Matrix.Rotation(math.radians(-side * SPAN_DROP), 4, "Y")
        if side < 0:
            deck(builder, 0.0, SPAN_PIECE)
        else:
            deck(builder, -SPAN_PIECE, 0.0)
    builder.matrix = Matrix.Identity(4)
    rng = random.Random(builder.name)
    drop = SPAN_PIECE * math.sin(math.radians(SPAN_DROP))
    for _ in range(10):
        size = (rng.uniform(1.0, 3.0), rng.uniform(1.0, 3.5), rng.uniform(0.4, 1.2))
        builder.turned((rng.uniform(-4.0, 4.0), rng.uniform(-half_width, half_width), -drop - rng.uniform(0.5, 2.5)), size,
                       (rng.uniform(-30.0, 30.0), rng.uniform(-30.0, 30.0), rng.uniform(0.0, 180.0)), "Concrete")
    # Only the stubs over the piers keep collision: nothing crosses the gap.
    for side in (-1.0, 1.0):
        builder.collision.append(((min(side * half, side * (half - 1.5)), -half_width, -bridge.DECK_DEPTH),
                                  (max(side * half, side * (half - 1.5)), half_width, 0.0)))
    return builder.finish()


# --- Export ---------------------------------------------------------------------------------------------------------------


def unreal_cm(point):
    return [round(point[0] * 100.0, 2) + 0.0, round(-point[1] * 100.0, 2) + 0.0, round(point[2] * 100.0, 2) + 0.0]


def record(obj, hulls):
    obj.data.calc_loop_triangles()
    xs, ys, zs = zip(*(vertex.co for vertex in obj.data.vertices))
    return {"fbx": f"{obj.name}.fbx", "slots": [slot.material.name for slot in obj.material_slots],
            "triangles": len(obj.data.loop_triangles), "collision_boxes": len(hulls),
            "bounds": [unreal_cm((min(xs), max(ys), min(zs))), unreal_cm((max(xs), min(ys), max(zs)))]}


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(VILLAGE_DIR, exist_ok=True)
    meshes, buildings = {}, {}

    def keep(made):
        obj, hulls = made
        bridge.export([obj] + hulls, os.path.join(VILLAGE_DIR, f"{obj.name}.fbx"))
        meshes[obj.name] = record(obj, hulls)
        print(f"village_blender: {obj.name} {meshes[obj.name]['triangles']} triangles, {len(hulls)} collision boxes")
        return obj.name

    for key, spec in BUILDINGS.items():
        buildings[key] = {"name": spec["name"], "health": spec["health"],
                          "states": [keep(pristine(key, spec)), keep(damaged(key, spec)), keep(rubble(key, spec))],
                          **garrison(spec)}
    buildings["BridgeSpan"] = {"name": "Road Bridge Span", "health": SPAN_HEALTH,
                               "states": ["SM_Bridge_Span", keep(span_damaged()), keep(span_destroyed())],
                               "capacity": 0, "entry": [0.0, 0.0, 0.0], "firing_positions": []}
    with open(os.path.join(VILLAGE_DIR, "village.json"), "w", encoding="utf-8", newline="\n") as file:
        json.dump({"about": "Generated by Tools/Art/village_blender.py; read by Tools/Editor/village_art.py.",
                   "meshes": meshes, "buildings": buildings}, file, indent=1)
        file.write("\n")


if __name__ == "__main__":
    main()
