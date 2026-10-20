"""Builds the USA Headquarters (original Lek River Studio work, after docs/art/refs/USA_HQ_Default_Approved.png and the
spec in docs/art/usa_hq/USA_HQ_SPEC.md) from code, one object per module of the spec's module map (M1 to M16):

  blender -b --python Tools/Art/usa_hq_blender.py -- [--level N] [--verify] [--no-render] [--samples N]

--level picks how far the build goes: 1 is the block-out (#186), the default is the highest level, each one adding real
construction detail on top of the one before without moving a module. --verify builds the model, checks it against the
spec and exits 1 on any failure (footprint, height, garage door and bay, clearance volume, module overlap); it writes
nothing. Without --verify the script builds, verifies, writes the .blend under source/, the FBX under fbx/ and the
renders under renders/ (blockout_*.png for level 1, round<N>_*.png above), all under Art/USA/buildings/usa_hq.

Every dimension is in metres in the block below, in Unreal's frame from the spec (section 2): +X is the garage face, +Y
is to the right when facing +X, Z up, the origin at the footprint centre on the ground. Blender's frame is right-handed,
so the geometry is built in Unreal's frame and mirrored in Y once per object (the FBX import mirrors it back, as for the
M20 Guardian): every number below reads as the spec reads it.

Movable parts (garage door, radar array, wing dish) are separate objects with their origin on the spec's pivot and an
empty marking it. The builder's clearance volume and the exit lane are non-rendering objects in their own collection,
used by --verify only.
"""

import math
import os
import re
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m20_guardian_render as presentation  # noqa: E402

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "USA", "buildings", "usa_hq")
BLEND = os.path.join(OUT_DIR, "source", "USA_HQ_Blockout.blend")
FBX_DIR = os.path.join(OUT_DIR, "fbx")
FBX = os.path.join(FBX_DIR, "SM_USA_HQ_Blockout.fbx")
RENDER_DIR = os.path.join(OUT_DIR, "renders")
CONCEPT = os.path.join(PROJECT_DIR, "docs", "art", "refs", "USA_HQ_Default_Approved.png")
GAME_INI = os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini")

# ---- dimensions (metres, Unreal frame) ----
# Spec section 3: footprint, overall height. The paved slab edge is the footprint edge.
FOOTPRINT = 28.0
HALF = FOOTPRINT / 2.0
APRON_TOP = 0.05
TOTAL_HEIGHT = 24.0  # to the core mast tip, whole mesh
HEIGHT_TOLERANCE = 0.01
FOOTPRINT_TOLERANCE = 0.01

# Positions confirmed by #186 round 6 against the concept's apron margins (about 4 m of open slab behind and beside the
# buildings, an open forecourt towards the (+X, +Y) corner); spec section 4 records them.
# M1 command core: a broad, squat octagon, flats on the axes and the diagonals. Its (+X, -Y) face clears the garage's
# rear corner (3.6, -1.0), which sets the centre.
CORE_CENTRE = (-3.5, 0.5)
CORE_APOTHEM = 6.0  # 12 m across the flats
CORE_ROOF = 14.0
DRUM_APOTHEM = 3.9
DRUM_ROOF = 16.5
CORE_MAST_TIP = TOTAL_HEIGHT

# M2 administration / communications wing along the +Y side, wrapping the core: rear block (dish tower on it), link,
# front block beside the forecourt. Boxes are (x0, x1, y0, y1).
# Round 7: two 3.5 m storeys (link one lower), blocks at least 5 m across, the front block's -Y face pulled back to
# +7.2 so the axis to the core's portal stays open. Round 8 (producer): the wing runs out along -X (screen left in the
# RTS view) as in the concept: the front block by the forecourt, a long low middle section with the equipment terrace,
# and a taller end block carrying the dish at the far end, 2 m off the -X and +Y slab edges and clear of the core.
WING_REAR = (-12.0, -8.0, 7.0, 12.0)  # the end block
WING_LINK = (-8.0, 0.8, 7.5, 12.0)  # the middle section, 8.8 m long
WING_FRONT = (0.8, 6.0, 7.2, 12.0)
WING_END_ROOF = 9.5  # the end block stands a storey over the wing; the dish sits on its flat roof
WING_ROOF = 7.5
WING_LINK_ROOF = 6.0
DISH_TOWER = None  # round 8: the end block itself carries the dish (rounds 1 to 7 had a tower on the rear block)
DISH_TOWER_ROOF = WING_END_ROOF
DISH_PIVOT = ((WING_REAR[0] + WING_REAR[1]) / 2.0, (WING_REAR[2] + WING_REAR[3]) / 2.0, DISH_TOWER_ROOF)
END_ROWS = [(1.0, 2.9), (4.3, 6.2), (7.2, 8.7)]
DISH_RADIUS = 1.25
WING_MAST_TIP = 17.0

# M3 garage (spec section 5): door 7.0 x 4.5 m clear in the plane X = +12.5, bay 8.5 m deep, 5.0 m clear inside.
GARAGE = (3.6, 12.5, -11.0, -1.0)
GARAGE_ROOF = 7.5
DOOR_PLANE = 12.5
DOOR_CENTRE_Y = -6.0  # the lateral exit offset for #192, -600 cm
DOOR_WIDTH = 7.0
DOOR_HEIGHT = 4.5
BAY_DEPTH = 8.5
BAY_HEIGHT = 5.0
# Clear heights count from the paved slab, so the opening's top and the bay ceiling sit APRON_TOP higher.
DOOR_TOP = APRON_TOP + DOOR_HEIGHT
BAY_CEILING = APRON_TOP + BAY_HEIGHT
LINTEL_DEPTH = 0.4
DOOR_PIVOT = (DOOR_PLANE, DOOR_CENTRE_Y, 4.7)
DOOR_LEAF = (DOOR_PLANE - LINTEL_DEPTH + 0.15, DOOR_PLANE - LINTEL_DEPTH + 0.25)  # x of the closed leaf, behind the lintel face
SPAWN = (8.25, DOOR_CENTRE_Y, 0.0)

# The design vehicle (standard section 3) and the margins of its clearance volume along the exit path.
BUILDER = (6.5, 3.0, 3.2)  # length, width, height
CLEAR_SIDE = 1.0  # each side: 5.0 m wide, 1.0 m from each door jamb
CLEAR_TOP = 0.5
CLEAR_REAR = 0.5
CLEAR_FLOOR = 0.05  # above the apron
LANE_WIDTH = 7.0  # spec section 5: nothing stands in it from the bay's back wall to the footprint edge

# M4 forecourt, raised with steps on its front edge, from the core's +X flat and entrance face (x0 follows the core)
# to x1, notched round the wing's front block; M5 flag on its plinth.
COURTYARD_X = (CORE_CENTRE[0] + CORE_APOTHEM, 10.2)  # the steps end at 11.4, clear of the fuel bund
COURTYARD_Y = (1.0, 9.0)
COURTYARD_GAP = 0.2  # to the wing's front block
COURTYARD_TOP = 0.6
STEPS = (3, 0.4)  # risers, tread
STEPS_Y = (1.0, 9.0)  # the forecourt's full width
FLAG_COURTYARD = (7.0, 5.0)
FLAG_COURTYARD_TIP = 9.0
PLINTH = (3.0, 2.2, 0.9)  # x, y, height over the courtyard
PLANTERS = [(7.0, 2.2), (8.2, 7.9)]

# M6 flag on the core's rear annex; M10 connectors.
ANNEX_DEPTH = 1.6  # rear annex off the core's (-X, -Y) face
FLAG_CORE = (CORE_CENTRE[0] - (CORE_APOTHEM + ANNEX_DEPTH / 2.0) * math.sqrt(0.5),
             CORE_CENTRE[1] - (CORE_APOTHEM + ANNEX_DEPTH / 2.0) * math.sqrt(0.5))  # the annex's centre
FLAG_CORE_TIP = 16.0
CONNECTOR_ROOF = 5.2  # under the wing link's 6.0 m, so the links step down from both neighbours
CONNECTOR_WING = (-5.0, -1.1)  # x span, from the core's +Y flat to the wing link (about 4 m wide)
CONNECTOR_GARAGE_Y = (-5.4, -2.0)  # from the core's (+X, -Y) face to the garage's rear wall

# M7 radar tower and its array (a part), M8 satcom lattice and radome, M9 secondary mast.
RADAR_TOWER = (-3.0, 1.0, -10.0, -6.0)
RADAR_TOWER_ROOF = 11.0
RADAR_PIVOT = (-1.0, -8.0, RADAR_TOWER_ROOF)
RADAR_TOP = 13.5
SATCOM_CENTRE = (-6.5, -8.5)
SATCOM_BASE = 3.0
RADOME_RADIUS = 1.5
RADOME_TOP = 18.0
SATCOM_MAST_TIP = 20.0
MAST_CENTRE = (-3.5, -11.0)
MAST_TIP = 17.0

# M12 checkpoints, M13 fuel tanks, M14 supply stacks, M15 loader, M16 light masts; all outside the exit lane.
CHECKPOINTS = [(12.6, 13.8, -2.3, 0.4), (12.6, 13.8, 7.6, 11.2)]
CHECKPOINT_LAMP_TIP = 4.0
FUEL_TANKS = [(1.35, 0.55), (2.6, 0.55)]  # (y centre, radius); axis along X
FUEL_TANK_X = (11.6, 13.8)
FUEL_TANK_AXIS_Z = 0.95
LOADER = (7.0, 9.8, -0.85, 0.85, 2.5)
SUPPLY = [(6.4, 11.2, 9.4, 11.0, 2.4), (4.5, 11.5, -13.6, -11.4, 2.5), (-12.0, -10.0, 3.8, 6.2, 1.9),
          (-11.0, -9.3, -12.4, -10.4, 2.2)]
LIGHT_MASTS = [(-13.2, 13.2, 8.0), (13.3, 13.3, 8.0), (-13.2, -13.2, 8.0), (13.3, -13.3, 8.0)]

# Overlap check: these pairs may touch (shared faces), checked with each mesh shrunk by this much.
TOUCH_TOLERANCE = 0.005
ADJACENT = {("Core", "Courtyard"), ("Core", "Connectors"), ("Wing", "Connectors"), ("Garage", "Connectors"),
            ("Garage", "GarageDoor"), ("Wing", "WingDish"), ("RadarTower", "Radar"), ("Connectors", "FlagCore"),
            ("Courtyard", "FlagCourtyard")}
ON_APRON = "Apron"  # every module stands on the apron

# Construction detail (level 2 and up), sized after real reinforced-concrete command buildings: windows 0.25 m deep in
# a precast-clad wall, 0.9 m parapets under a metal coping, 3.5-4.5 m storeys, 2.4 m window bays, ~3 m wide panels
# with 20 mm joints.
REVEAL = 0.25  # outer face of the cladding to the glazing; the building mass stands back this far
PARAPET = 0.9  # roof deck below the coping top
PARAPET_WALL = 0.25
COPING = 0.08
ROOF_MEMBRANE = 0.03
PANEL = 3.0
PANEL_JOINT = 0.02
BAY = 2.4
WINDOW = 1.5
FRAME = 0.06  # window frame and mullion face width
FRAME_DEPTH = 0.08
GLASS = 0.03
SILL = 0.05
CORE_ROWS = [(1.5, 3.9), (5.9, 8.1), (10.3, 12.3)]  # (sill, head) per storey
DRUM_ROWS = [(14.4, 15.6)]
DRUM_PARAPET = 0.6
WING_ROWS = [(1.0, 2.9), (4.3, 6.2)]
LINK_ROWS = [(1.0, 2.8), (3.6, 4.9)]
TOWER_ROWS = [(1.2, 3.0), (5.0, 6.6)]
ANNEX_ROWS = [(1.2, 3.0)]
ANNEX_PARAPET = 0.6
ENTRANCE = (3.2, 3.9)  # core doors: width, head (threshold at the courtyard)
CANOPY = (3.8, 0.9, 4.0, 0.25)  # width, depth, underside, thickness
CORE_PLATE = (3.8, 5.0, 8.4)  # eagle plaque over the entrance: width, bottom, top
WING_PLATE = (3.0, 2.0, 4.6)  # width, bottom, top
TOWER_PLATE = (2.6, 7.4, 10.0)
DISH_TOWER_PLATE = (2.4, 7.8, 9.2)
PLATE_DEPTH = 0.06
LAMP_STRIP = 0.12
GARAGE_SKIN = 0.2  # the hall's side walls stand back behind their pilasters
PILASTER = (0.45, 2.85)  # width, spacing
CLERESTORY = (5.3, 6.3)
DOOR_POST = (0.3, 0.08)  # hazard-striped steel door frame: width, proud of the door plane
HAZARD_BAND = 0.3
CORRIDOR = (0.8, 0.6, 0.15, 1.5)  # glazed links: plinth height, roof fascia, glazing set-back, mullion spacing
MARKING = (0.4, 0.15, 0.001)  # apron edge line: inset, width, thickness
LANE_LINE = 0.15

# Roof equipment, masts and flags (level 3 and up). HVAC condensers ~1.2 x 1.0 x 0.8 m on steel rails, 0.3 m mushroom
# vents, 1.0 x 1.6 m photovoltaic modules tilted ~15 degrees, a 1.9:1 flag, floodlight masts with two heads, apron
# slabs cut every 4 m with sawn joints.
CORE_UNIT = (0.8, 1.1, 0.75)  # radial depth, width, height of the condensers ringing the drum
FLAG = (1.9, 1.0, 0.25)  # length, height, gap from the top of the pole
FINIAL = 0.07
APRON_JOINT = (4.0, 0.015)  # spacing, width
SOLAR_ROWS = [6.6, 8.25, 9.9]  # x centres on the garage roof
SOLAR = (1.2, 1.6, 0.25, 0.7, 0.04)  # depth (x), module length (y), low and high edge over the deck, thickness
SOLAR_Y = (-9.0, -4.0)
GUYS = [((2.5, -0.6), 12.0), ((-1.9, -1.2), 9.0), ((-0.7, 1.6), 12.0)]  # secondary mast: anchor offset, height on it
FLOOD_ARM = 1.2
CONCRETE_TONE = 0.82  # level 3 darkens the render concrete towards the concept's grey

# Ground props and wall fixtures (level 4 and up), at real sizes: 1.2 x 0.8 x 0.62 m stores crates, a 3 m (10 ft class)
# stores container, 0.81 m jersey barriers in 2 m segments, a compact wheel loader, domed horizontal fuel tanks in a bund,
# steel bollards, dark steel corner plates and vertical blue lamp strips at the corners.
CRATE = (1.2, 0.8, 0.62)
CRATE_GAP = 0.08
CONTAINER = (4.5, 7.5)  # x span in the garage-side stack
JERSEY = [(-0.3, 0.0), (0.3, 0.0), (0.3, 0.08), (0.18, 0.33), (0.075, 0.81), (-0.075, 0.81), (-0.18, 0.33),
          (-0.3, 0.08)]  # profile (across, up)
JERSEY_LENGTH = 2.0
BOLLARDS = [(12.4, 6.8), (12.8, -10.0), (12.8, -10.6)]
BUND = (11.45, 13.95, 0.55, 3.35, 0.3, 0.15)  # x0, x1, y0, y1, height, wall
CORNER_PLATE = (0.45, 0.03)  # leg, thickness

# Round 5 (level 5 and up), after the visual review: blast-resistant glazing in the UFC 4-010-01 manner (single laminated
# panes in deep reveals, heavy frames, no muntins, projecting sills, ribbon bands on the wing), chamfered wing corners,
# a projecting base course, deeper panel joints, a recessed core portal, the garage's chamfered eave and projecting
# door portal, guard blocks at the checkpoints and mixed supply clusters.
BLAST_REVEAL = 0.38
BLAST_FRAME = (0.10, 0.12)  # face width, depth
BLAST_JOINT = 0.04
SILL_PROJECTION = 0.08
PLINTH_COURSE = (0.6, 0.05)  # height, projection
WING_CHAMFER = 0.55
RIBBON_MULLION = 1.5
PORTAL = (3.6, 5.5, 0.8)  # clear width (held to the forecourt in front of the 4.97 m entrance face), height, recess
PORTAL_FRAME = (0.3, 0.3, 0.4)  # jamb width, projection, lintel depth
PORTAL_PLATE = (3.6, 0.6, 3.6)  # plaque width, gap over the lintel, height
CORE_FLUSH = (0, 1, 2, 5, 7)  # core faces with a neighbour against them: no base course, flush sills
EAVE = 1.0  # the garage's 45 degree eave on its +X and +-Y faces
EAVE_WALL = 0.25
GARAGE_PORTAL = (0.5, 0.5, 0.7)  # post width, projection, header depth over the door
GARAGE_ROOF_KIT = {"hvac": (4.9, -3.2), "fans": ((10.6, -3.0), (10.6, -9.0)), "vents": ((7.4, -2.9), (5.0, -9.1)),
                   "hatch": (4.7, -8.4)}
CORE_CABINET = (0.6, 0.6, 1.0)
WHIP_CLUSTER = (0.6, (2.2, 3.0, 3.6))  # base, whip heights
GUARD_POSTS = [((12.65, 13.85, -1.85, 0.45), (12.25, -0.9, 0.45)),  # guard block (x0, x1, y0, y1), barrier run (x, y0, y1)
               ((12.65, 13.85, 8.15, 10.65), (12.25, 7.8, 11.0))]
GUARD_HEIGHT = 1.2
BOLLARDS_R5 = [(12.4, 6.8), (13.3, -10.2), (13.3, -10.9)]
DRUM = (0.58, 0.88)  # diameter, height
PALLET = (1.2, 1.0, 0.15)
SUPPLY_CLUSTERS = [  # inside the level 1 supply extents, gaps between them
    ("pallets", 6.4, 8.0, 9.4, 10.6, 1.5), ("crates", 8.3, 9.9, 9.4, 11.0, 2.4), ("drums", 10.1, 11.2, 9.4, 10.6, 0.0),
    ("container", 4.5, 7.5, -13.6, -11.4, 2.5), ("crates", 8.3, 9.9, -13.6, -11.6, 1.9),
    ("drums", 10.4, 11.5, -13.4, -12.1, 0.0),
    ("crates", -12.0, -10.0, 3.8, 6.2, 1.9), ("drums", -9.6, -8.4, 4.8, 6.2, 0.0), ("pallets", -7.6, -6.2, 6.6, 7.3, 1.3),
    ("pallets", -11.0, -9.3, -12.4, -10.4, 1.4), ("crates", -11.0, -9.6, -9.9, -8.4, 2.2)]
# Round 6: two storeys on the core (a double-height lobby, one office floor), the drum's windows in one band.
CORE_ROWS_R6 = [(1.5, 5.4), (8.0, 11.6)]
# Round 7: the core steps. A lower ring (the 2-storey lobby and one floor, at the core's apothem) to 10 m with the
# equipment terrace on it, a setback tier to the decided ~14 m, then the drum.
CORE_LOWER_ROOF = 10.0
CORE_LOWER_ROWS = [(1.5, 5.4), (6.6, 8.6)]
CORE_UPPER_APOTHEM = 4.5  # leaves a terrace for the condensers inside the lower parapet
CORE_UPPER_ROWS = [(10.4, 12.6)]
TERRACE_UNIT = (0.7, 1.0, 0.7)  # condensers on the terrace: radial depth, width, height
CORE_PLATE_R7 = (3.6, 0.2, 2.6)  # plaque on the lower ring: width, gap over the lintel, height
CONNECTOR_R7 = (1.2, 0.3)  # glazing band from 1.2 m to the fascia (4.5 m), roof projecting 0.3 m on free sides
BAY_NICHE = (5.0, 11.0, 0.5, 3.0)  # recesses in the bay's side walls for racks and cabinets: x0, x1, depth, height
WING_PANEL = (2.0, 1.2, 1.0)  # the M2 panel antenna: width, height, mast
# The scale proof: the M20 Guardian from its own .blend, parked on the exit lane past the slab, facing +X.
M20_BLEND = os.path.join(PROJECT_DIR, "Art", "USA", "vehicles", "m20_guardian", "source", "M20_Guardian_V5.blend")
M20_SPOT = (19.5, DOOR_CENTRE_Y, 0.0)
TRIANGLE_BUDGET = (60000, 10000)  # root, movable parts (spec section 9)

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
MAX_LEVEL = 8
LEVEL = int(ARGS[ARGS.index("--level") + 1]) if "--level" in ARGS else MAX_LEVEL
SAMPLES = int(ARGS[ARGS.index("--samples") + 1]) if "--samples" in ARGS else 64
VERIFY_ONLY = "--verify" in ARGS
RENDER = "--no-render" not in ARGS
if LEVEL >= 5:  # deeper reveals, heavier frames and wider joints from round 5 (the builders read these at call time)
    REVEAL, (FRAME, FRAME_DEPTH), PANEL_JOINT = BLAST_REVEAL, BLAST_FRAME, BLAST_JOINT
if LEVEL >= 6:
    CORE_ROWS = CORE_ROWS_R6

PREFIX = "USA_HQ_"

# Render materials, one per spec section 7 category (#189 replaces them): name, base colour, roughness, metallic,
# emission strength.
MATERIALS = {
    "Concrete": ((0.46, 0.45, 0.42), 0.85, 0.0, 0.0),
    "PaintedMetal": ((0.55, 0.56, 0.57), 0.55, 0.3, 0.0),
    "BareMetal": ((0.45, 0.45, 0.46), 0.4, 1.0, 0.0),
    "ArmouredSteel": ((0.12, 0.13, 0.14), 0.5, 0.8, 0.0),
    "Glass": ((0.05, 0.07, 0.09), 0.08, 0.0, 0.0),
    "Roofing": ((0.18, 0.18, 0.19), 0.9, 0.0, 0.0),
    "Rubber": ((0.04, 0.04, 0.04), 0.9, 0.0, 0.0),
    "BlueAccent": ((0.03, 0.08, 0.25), 0.5, 0.2, 0.0),
    "EaglePlate": ((0.04, 0.10, 0.30), 0.45, 0.2, 0.0),
    "Lamp": ((0.25, 0.5, 1.0), 0.3, 0.0, 6.0),
    "Olive": ((0.17, 0.19, 0.10), 0.7, 0.1, 0.0),
    "Apron": ((0.36, 0.36, 0.35), 0.9, 0.0, 0.0),
    "Hazard": ((0.75, 0.55, 0.05), 0.6, 0.1, 0.0),
    "EmblemWhite": ((0.80, 0.80, 0.78), 0.5, 0.0, 0.0),
    "Marking": ((0.70, 0.70, 0.67), 0.75, 0.0, 0.0),
    "Foliage": ((0.07, 0.11, 0.04), 0.8, 0.0, 0.0),
    "Soil": ((0.11, 0.08, 0.05), 0.95, 0.0, 0.0),
    "Solar": ((0.02, 0.03, 0.07), 0.12, 0.3, 0.0),
    "FlagRed": ((0.45, 0.03, 0.04), 0.7, 0.0, 0.0),
    "FlagBlue": ((0.03, 0.05, 0.20), 0.7, 0.0, 0.0),
    "Wood": ((0.30, 0.21, 0.12), 0.85, 0.0, 0.0),
    "Envelope": ((1.0, 0.45, 0.05), 0.5, 0.0, 0.0),
}
# Weathering for the render materials from level 2: noise scale, colour variation, bump strength, grime height (m).
WEATHERED = {"Concrete": (0.9, 0.18, 0.25, 1.6), "Apron": (0.35, 0.22, 0.2, 0.0), "PaintedMetal": (2.0, 0.08, 0.05, 1.0),
             "Roofing": (1.2, 0.15, 0.2, 0.0), "Olive": (1.5, 0.15, 0.1, 0.6), "BareMetal": (3.0, 0.12, 0.05, 0.0),
             "BlueAccent": (1.5, 0.10, 0.05, 0.0), "EaglePlate": (1.5, 0.08, 0.05, 0.0)}


# ---- geometry (all builders take Unreal-frame coordinates) ----

def loft(bottom, top):
    """A closed solid between two polygons with the same vertex count."""
    def build(bm):
        low = [bm.verts.new(p) for p in bottom]
        high = [bm.verts.new(p) for p in top]
        bm.faces.new(list(reversed(low)))
        bm.faces.new(high)
        count = len(low)
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((low[i], low[j], high[j], high[i]))
    return build


def plan_prism(profile_xy, z0, z1, top_xy=None):
    return loft([(x, y, z0) for x, y in profile_xy], [(x, y, z1) for x, y in (top_xy or profile_xy)])


def rect(x0, x1, y0, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def box(x0, x1, y0, y1, z0, z1):
    return plan_prism(rect(x0, x1, y0, y1), z0, z1)


def centred_box(cx, cy, sx, sy, z0, z1):
    return box(cx - sx / 2.0, cx + sx / 2.0, cy - sy / 2.0, cy + sy / 2.0, z0, z1)


def axis_frame(axis):
    a = Vector(axis).normalized()
    helper = Vector((0.0, 0.0, 1.0)) if abs(a.z) < 0.9 else Vector((1.0, 0.0, 0.0))
    u = a.cross(helper).normalized()
    return a, u, a.cross(u)


def ring(centre, axis, offset, radius, segments, phase=0.0):
    a, u, v = axis_frame(axis)
    c = Vector(centre) + a * offset
    return [c + (u * math.cos(t) + v * math.sin(t)) * radius
            for t in (phase + 2.0 * math.pi * i / segments for i in range(segments))]


def cylinder(centre, axis, radius, length, segments=12, radius_end=None):
    return loft(ring(centre, axis, -length / 2.0, radius, segments),
                ring(centre, axis, length / 2.0, radius if radius_end is None else radius_end, segments))


def post(x, y, z0, z1, radius, segments=8, radius_top=None):
    return cylinder((x, y, (z0 + z1) / 2.0), (0.0, 0.0, 1.0), radius, z1 - z0, segments, radius_top)


def bar(start, end, radius, segments=6):
    a, b = Vector(start), Vector(end)
    return cylinder((a + b) / 2.0, b - a, radius, (b - a).length, segments)


def lathe(centre, axis, profile, segments=16):
    """A capped solid of revolution: profile is a list of (offset along axis, radius)."""
    def build(bm):
        rings = [[bm.verts.new(p) for p in ring(centre, axis, offset, max(radius, 1e-4), segments)]
                 for offset, radius in profile]
        for r0, r1 in zip(rings, rings[1:]):
            for i in range(segments):
                j = (i + 1) % segments
                bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])
    return build


def sphere(centre, radius, segments=16, rings=8):
    return lathe(centre, (0.0, 0.0, 1.0), [(-radius * math.cos(math.pi * k / rings), radius * math.sin(math.pi * k / rings))
                                          for k in range(rings + 1)], segments)


def octagon(cx, cy, apothem):
    """Flats on the axes and diagonals, counter-clockwise from the +X flat's lower corner."""
    r = apothem / math.cos(math.pi / 8.0)
    return [(cx + r * math.cos(math.pi / 8.0 + k * math.pi / 4.0 - math.pi / 4.0),
             cy + r * math.sin(math.pi / 8.0 + k * math.pi / 4.0 - math.pi / 4.0)) for k in range(8)]


def on_core_face(normal_degrees, t, offset=0.0):
    """A point on the core's flat facing normal_degrees (0 = +X, 90 = +Y), t from -1 to 1 along it, offset outwards."""
    a = math.radians(normal_degrees)
    n = Vector((math.cos(a), math.sin(a)))
    along = Vector((-n.y, n.x))
    half = CORE_APOTHEM * math.tan(math.pi / 8.0)
    p = Vector(CORE_CENTRE) + n * (CORE_APOTHEM + offset) + along * half * t
    return (p.x, p.y)


def lattice_tower(x, y, z0, z1, base, top, levels):
    """Four legs tapering from base to top (square half-widths), with a ring of braces at each level."""
    def legs(z):
        h = base + (top - base) * (z - z0) / (z1 - z0)
        return [(x + sx * h, y + sy * h, z) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    pieces = []
    for i in range(4):
        pieces.append(bar(legs(z0)[i], legs(z1)[i], 0.07))
    for k in range(levels + 1):
        z = z0 + (z1 - z0) * k / levels
        ring_points = legs(z)
        for i in range(4):
            pieces.append(bar(ring_points[i], ring_points[(i + 1) % 4], 0.04, 4))
        if k < levels:
            upper = legs(z0 + (z1 - z0) * (k + 1) / levels)
            for i in range(4):
                pieces.append(bar(ring_points[i], upper[(i + 1) % 4], 0.03, 4))
    return pieces


# ---- architecture (level 2 and up) ----

def signed_area(points):
    return sum(points[i][0] * points[(i + 1) % len(points)][1] - points[(i + 1) % len(points)][0] * points[i][1]
               for i in range(len(points))) / 2.0


def ccw(points):
    return list(points) if signed_area(points) > 0.0 else list(reversed(points))


def inset_outline(points, distance):
    """A closed counter-clockwise outline moved inwards by distance (mitred corners)."""
    pts = [Vector(p[:2]) for p in points]
    count = len(pts)
    out = []
    for i in range(count):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % count]
        e0, e1 = (p1 - p0).normalized(), (p2 - p1).normalized()
        n0, n1 = Vector((-e0.y, e0.x)), Vector((-e1.y, e1.x))
        bisector = (n0 + n1).normalized()
        out.append(tuple(p1 + bisector * distance / max(bisector.dot(n0), 0.3)))
    return out


def annulus(outer, inner, z0, z1):
    """A closed ring between two outlines with the same vertex count: parapets, copings, kerbs."""
    def build(bm):
        ob = [bm.verts.new((x, y, z0)) for x, y in outer]
        ot = [bm.verts.new((x, y, z1)) for x, y in outer]
        ib = [bm.verts.new((x, y, z0)) for x, y in inner]
        it = [bm.verts.new((x, y, z1)) for x, y in inner]
        count = len(outer)
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((ob[i], ob[j], ot[j], ot[i]))
            bm.faces.new((ib[j], ib[i], it[i], it[j]))
            bm.faces.new((ot[i], ot[j], it[j], it[i]))
            bm.faces.new((ob[j], ob[i], ib[i], ib[j]))
    return build


class Frame:
    """A facade's plane on one edge of a counter-clockwise outline: s along the edge from p0, d outwards (negative into
    the wall), z up."""

    def __init__(self, p0, p1):
        self.p0 = Vector(p0[:2])
        edge = Vector(p1[:2]) - self.p0
        self.length = edge.length
        self.e = edge / self.length
        self.n = Vector((self.e.y, -self.e.x))

    def point(self, s, d, z):
        p = self.p0 + self.e * s + self.n * d
        return (p.x, p.y, z)

    def box(self, s0, s1, d0, d1, z0, z1):
        corners = ((s0, d0), (s1, d0), (s1, d1), (s0, d1))
        return loft([self.point(s, d, z0) for s, d in corners], [self.point(s, d, z1) for s, d in corners])

    def plate(self, outline_sz, d0, d1):
        """A flat shape drawn on the facade as (s, z) points, d0..d1 thick."""
        return loft([self.point(s, d0, z) for s, z in outline_sz], [self.point(s, d1, z) for s, z in outline_sz])


def panels(m, frame, s0, s1, z0, z1, d0=None, d1=0.0):
    """Precast cladding panels between s0 and s1, with open joints showing the wall behind."""
    d0 = -REVEAL if d0 is None else d0
    if z1 - z0 < 0.01 or s1 - s0 < 0.01:
        return
    count = max(1, round((s1 - s0) / PANEL))
    step = (s1 - s0) / count
    for i in range(count):
        a = s0 + step * i + (PANEL_JOINT / 2.0 if i else 0.0)
        b = s0 + step * (i + 1) - (PANEL_JOINT / 2.0 if i < count - 1 else 0.0)
        m.add("Concrete", frame.box(a, b, d0, d1, z0, z1))


def glazing(m, frame, a, b, sill, head, door=False, ribbon=False, proud=False, back=None):
    """A window (or glazed doors) set back in its reveal: glass, frame, mullions about every metre, a transom on tall
    openings and a metal sill. From level 5 a window is one laminated pane in a heavy frame (ribbons keep structural
    mullions every RIBBON_MULLION), its sill projecting where the wall is free (proud)."""
    back = -REVEAL if back is None else back
    front = back + FRAME_DEPTH
    m.add("Glass", frame.box(a, b, back, back + GLASS, sill, head))
    m.add("PaintedMetal", frame.box(a, a + FRAME, back, front, sill, head), frame.box(b - FRAME, b, back, front, sill, head),
          frame.box(a + FRAME, b - FRAME, back, front, head - FRAME, head))
    if door:
        m.add("PaintedMetal", frame.box(a + FRAME, b - FRAME, back, front, sill, sill + 0.12))
    elif LEVEL >= 5:
        m.add("PaintedMetal", frame.box(a + FRAME, b - FRAME, back, front, sill, sill + FRAME))
        m.add("PaintedMetal", frame.box(a - 0.05, b + 0.05, back, SILL_PROJECTION if proud else -0.01, sill - 0.06, sill))
    else:
        m.add("PaintedMetal", frame.box(a - 0.04, b + 0.04, back, -0.01, sill, sill + SILL))
    if LEVEL < 5 or door or ribbon:
        count = max(1, round((b - a) / (RIBBON_MULLION if ribbon else 1.0)))
        for i in range(1, count):
            s = a + (b - a) * i / count
            m.add("PaintedMetal", frame.box(s - FRAME / 2.0, s + FRAME / 2.0, back, front, sill, head))
    if head - sill > 1.9 and (LEVEL < 5 or door):
        z = sill + (head - sill) * (0.78 if door else 0.72)
        m.add("PaintedMetal", frame.box(a + FRAME, b - FRAME, back, front, z - FRAME / 2.0, z + FRAME / 2.0))


def facade(m, frame, z0, z1, rows=(), trims=(0.0, 0.0), skip=(), spans=None, door=False, ribbon=False, proud=False,
           glaze=True):
    """The cladding of one wall from z0 to z1: panelled bands between the window rows, piers between the windows.
    rows: (sill, head) per storey; skip: (s0, s1) ranges kept blank (plaques); spans: explicit window ranges; ribbon:
    one band of glazing per row between the skips; glaze False leaves the spans open (a portal glazed elsewhere)."""
    s0, s1 = trims[0], frame.length - trims[1]
    if s1 - s0 < 0.05:
        return
    if spans is None and ribbon and rows:
        spans, start = [], s0 + 0.45
        for k0, k1 in sorted(skip):
            if k0 - start > 0.6:
                spans.append((start, k0))
            start = max(start, k1)
        if s1 - 0.45 - start > 0.6:
            spans.append((start, s1 - 0.45))
    if spans is None:
        spans = []
        if rows:
            count = max(1, int((s1 - s0) / BAY + 0.3))
            step = (s1 - s0) / count
            width = min(WINDOW, step - 0.6)
            for i in range(count):
                c = s0 + step * (i + 0.5)
                a, b = c - width / 2.0, c + width / 2.0
                if width > 0.5 and not any(a < k1 and b > k0 for k0, k1 in skip):
                    spans.append((a, b))
    levels = [z0] + [z for row in rows for z in row] + [z1]
    for k in range(0, len(levels), 2):
        panels(m, frame, s0, s1, levels[k], levels[k + 1])
    edges = [s0] + [x for span in spans for x in span] + [s1]
    for sill, head in rows:
        for k in range(0, len(edges), 2):
            if edges[k + 1] - edges[k] > 0.01:
                m.add("Concrete", frame.box(edges[k], edges[k + 1], -REVEAL, 0.0, sill, head))
        for a, b in spans if glaze else ():
            glazing(m, frame, a, b, sill, head, door, ribbon, proud)


def block(m, outline, z0, roof, rows=None, skips=None, spans=None, doors=(), parapet=PARAPET, coping_above=False,
          flush=(), ribbons=(), notch=None, open_edges=()):
    """A flat-roofed building on a counter-clockwise outline: the mass set back by the reveal, the cladding in front of
    it with corner piers, and the roof: a parapet under a metal coping round a membrane deck, or (coping_above) a flat
    deck at the roof height with the coping standing on its edge. rows, skips, spans and doors are per edge index
    (edge k runs from outline[k] to outline[k + 1]). From level 5: flush edges (a neighbour against them) get no base
    course and flush sills, ribbons glaze in bands, notch = (edge, s0, s1, depth, top) recesses the mass for a portal
    and open_edges leave their spans unglazed. Returns the deck height and the deck's outline."""
    rows, skips, spans = rows or {}, skips or {}, spans or {}
    inner = inset_outline(outline, REVEAL)
    deck = roof if coping_above else roof - parapet
    top = roof if coping_above else roof - COPING
    if notch:
        k, a, b, depth, notch_top = notch
        frame = Frame(outline[k], outline[(k + 1) % len(outline)])
        cut = [frame.point(a, -REVEAL, 0.0)[:2], frame.point(a, -depth, 0.0)[:2], frame.point(b, -depth, 0.0)[:2],
               frame.point(b, -REVEAL, 0.0)[:2]]
        m.add("Concrete", plan_prism(inner[:k + 1] + cut + inner[k + 1:], z0, notch_top))
        m.add("Concrete", plan_prism(inner, notch_top, deck))
    else:
        m.add("Concrete", plan_prism(inner, z0, deck))
    count = len(outline)
    for k in range(count):
        p0, p1 = outline[k], outline[(k + 1) % count]
        frame = Frame(p0, p1)
        t0 = (Vector(inner[k]) - Vector(p0)).dot(frame.e)
        t1 = (Vector(p1) - Vector(inner[(k + 1) % count])).dot(frame.e)
        facade(m, frame, z0, top, rows.get(k, ()), (t0, t1), skips.get(k, ()), spans.get(k), k in doors, k in ribbons,
               LEVEL >= 5 and k not in flush, k not in open_edges)
        if LEVEL >= 5 and z0 <= APRON_TOP + 1e-6 and k not in flush:  # base course, proud of the cladding
            height, out = PLINTH_COURSE
            # Wrapped round free corners, stopped short of a corner whose other face has a neighbour against it.
            start = -out if (k - 1) % count not in flush else REVEAL
            end = frame.length + (out if (k + 1) % count not in flush else -REVEAL)
            m.add("Concrete", frame.box(start, end, -REVEAL, out, z0, z0 + height))
        before = Frame(outline[k - 1], p0).e
        corner, back = Vector(p0), Vector(inner[k])
        piece = [corner + before * (back - corner).dot(before), corner, corner + frame.e * (back - corner).dot(frame.e), back]
        m.add("Concrete", plan_prism([tuple(p) for p in piece], z0, top))
    if coping_above:
        m.add(trim(), annulus(outline, inset_outline(outline, REVEAL + 0.3), roof, roof + COPING))
        return deck, inset_outline(outline, REVEAL + 0.3)
    wall = inset_outline(outline, REVEAL + PARAPET_WALL)
    m.add("Concrete", annulus(inner, wall, deck, top))
    m.add("Roofing", plan_prism(wall, deck, deck + ROOF_MEMBRANE))
    m.add(trim(), annulus(outline, inset_outline(outline, REVEAL + PARAPET_WALL + 0.03), top, roof))
    return deck, wall


def emblem(m, frame, cs, cz, size, d0):
    """The generic eagle-and-star emblem (original): a ring round a five-point star, three swept feathers each side."""
    d1 = d0 + 0.025
    r = size * 0.2
    segments = 20
    for i in range(segments):
        a0, a1 = 2.0 * math.pi * i / segments, 2.0 * math.pi * (i + 1) / segments
        m.add("EmblemWhite", frame.plate([(cs + r * math.cos(a0), cz + r * math.sin(a0)),
                                          (cs + r * math.cos(a1), cz + r * math.sin(a1)),
                                          (cs + r * 0.84 * math.cos(a1), cz + r * 0.84 * math.sin(a1)),
                                          (cs + r * 0.84 * math.cos(a0), cz + r * 0.84 * math.sin(a0))], d0, d1))
    star = [(cs + (r * 0.72 if i % 2 == 0 else r * 0.29) * math.cos(math.pi / 2.0 + math.pi * i / 5.0),
             cz + (r * 0.72 if i % 2 == 0 else r * 0.29) * math.sin(math.pi / 2.0 + math.pi * i / 5.0)) for i in range(10)]
    m.add("EmblemWhite", frame.plate(star, d0, d1))
    for side in (-1.0, 1.0):
        for j in range(3):
            inner_s = cs + side * r * 1.1
            outer_s = cs + side * (r * 1.1 + size * (0.30 - 0.06 * j))
            zc = cz + size * (0.07 - 0.085 * j)
            h = size * 0.055
            lift = size * (0.05 - 0.01 * j)
            m.add("EmblemWhite", frame.plate([(inner_s, zc - h), (outer_s, zc + lift - h * 0.35),
                                              (outer_s, zc + lift + h * 0.35), (inner_s, zc + h)], d0, d1))


def plaque(m, frame, s_centre, z0, z1, width, d0=0.0):
    """A team-colour eagle plaque standing proud of a wall: blue plate, metal border, white emblem."""
    m.add("PaintedMetal", frame.box(s_centre - width / 2.0, s_centre + width / 2.0, d0, d0 + PLATE_DEPTH * 0.5, z0, z1))
    m.add("EaglePlate", frame.box(s_centre - width / 2.0 + 0.06, s_centre + width / 2.0 - 0.06, d0, d0 + PLATE_DEPTH,
                                  z0 + 0.06, z1 - 0.06))
    emblem(m, frame, s_centre, (z0 + z1) / 2.0, min(width, (z1 - z0) * 1.4) * 0.9, d0 + PLATE_DEPTH)


def trim():
    return "ArmouredSteel" if LEVEL >= 3 else "PaintedMetal"


def local_point(cx, cy, angle, u, v):
    a = math.radians(angle)
    return (cx + u * math.cos(a) - v * math.sin(a), cy + u * math.sin(a) + v * math.cos(a))


def obox(cx, cy, angle, sx, sy, z0, z1, u=0.0, v=0.0):
    """A box sx (along angle) by sy, centred at local (u, v) of a frame at (cx, cy) turned by angle degrees."""
    return plan_prism([local_point(cx, cy, angle, u + a * sx / 2.0, v + b * sy / 2.0)
                       for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))], z0, z1)


def box3(centre, u, v, w, hu, hv, hw):
    """A box on three orthogonal axes: tilted plates and arms."""
    c = Vector(centre)
    u, v, w = Vector(u), Vector(v), Vector(w)

    def face(sign):
        return [c + u * a * hu + v * b * hv + w * sign * hw for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    return loft(face(-1), face(1))


def hvac(m, cx, cy, angle, sx, sy, z, h):
    """A packaged condenser on two steel rails: casing, a fan disc on top, a grille panel on one side."""
    for v in (-sy / 2.0 + 0.12, sy / 2.0 - 0.12):
        m.add("ArmouredSteel", obox(cx, cy, angle, sx + 0.1, 0.1, z, z + 0.1, v=v))
    m.add("PaintedMetal", obox(cx, cy, angle, sx, sy, z + 0.1, z + h))
    m.add("Rubber", obox(cx, cy, angle, 0.02, sy - 0.2, z + 0.2, z + h - 0.1, u=sx / 2.0 + 0.01))
    fan = local_point(cx, cy, angle, 0.0, 0.0)
    m.add("Rubber", post(fan[0], fan[1], z + h, z + h + 0.05, min(sx, sy) * 0.36, 12))
    m.add("PaintedMetal", post(fan[0], fan[1], z + h, z + h + 0.09, 0.06, 6))


def vent(m, x, y, z, radius=0.15):
    """A mushroom roof vent."""
    m.add("PaintedMetal", post(x, y, z, z + 0.45, radius * 0.6, 8))
    m.add("PaintedMetal", post(x, y, z + 0.45, z + 0.55, radius * 1.3, 8, radius * 0.7))


def hatch(m, x, y, z):
    m.add("PaintedMetal", centred_box(x, y, 0.9, 0.9, z, z + 0.35))
    m.add("ArmouredSteel", centred_box(x, y, 1.0, 1.0, z + 0.35, z + 0.4))


def beacon(m, x, y, z, radius=0.08):
    m.add("Lamp", sphere((x, y, z), radius, 8, 4))


def flag_cloth(m, x, y, top):
    """A flag of thirteen stripes with a blue canton, waving gently off the pole towards +X."""
    length, height, _ = FLAG
    columns = 8
    stripe = height / 13.0

    def wave(t):
        return 0.12 * math.sin(t * math.pi * 1.7) * t

    def strip(material, z0, z1, t_end, half):
        for j in range(columns):
            t0, t1 = j / columns * t_end, (j + 1) / columns * t_end
            xa, xb = x + 0.09 + length * t0, x + 0.09 + length * t1
            ya, yb = y + wave(t0), y + wave(t1)
            corners = ((xa, ya - half), (xb, yb - half), (xb, yb + half), (xa, ya + half))
            m.add(material, loft([(a, b, z0) for a, b in corners], [(a, b, z1) for a, b in corners]))
    for i in range(13):
        strip("FlagRed" if i % 2 == 0 else "EmblemWhite", top - (i + 1) * stripe, top - i * stripe, 1.0, 0.006)
    strip("FlagBlue", top - 7 * stripe, top + 0.001, 0.4, 0.009)


def chamfer(outline, cut):
    """The outline with each corner cut back by cut along both edges: edge k of the input becomes edge 2k + 1."""
    pts = [Vector(p[:2]) for p in outline]
    out = []
    for k in range(len(pts)):
        before = (pts[k] - pts[k - 1]).normalized()
        after = (pts[(k + 1) % len(pts)] - pts[k]).normalized()
        out += [tuple(pts[k] - before * cut), tuple(pts[k] + after * cut)]
    return out


def frustum_ring(bottom, top, inner, z0, z1):
    """A closed ring whose outer face runs from the bottom outline at z0 to the top outline at z1 (a sloped eave), with
    a vertical inner face on inner."""
    def build(bm):
        o = [bm.verts.new((x, y, z0)) for x, y in bottom]
        t = [bm.verts.new((x, y, z1)) for x, y in top]
        i = [bm.verts.new((x, y, z1)) for x, y in inner]
        b = [bm.verts.new((x, y, z0)) for x, y in inner]
        n = len(bottom)
        for k in range(n):
            j = (k + 1) % n
            bm.faces.new((o[k], o[j], t[j], t[k]))
            bm.faces.new((t[k], t[j], i[j], i[k]))
            bm.faces.new((i[k], i[j], b[j], b[k]))
            bm.faces.new((b[k], b[j], o[j], o[k]))
    return build


def whip_cluster(m, x, y, z):
    """A small antenna farm: a steel base with three whips of different heights, a beacon on the tallest."""
    base, heights = WHIP_CLUSTER
    m.add("ArmouredSteel", centred_box(x, y, base, base, z, z + 0.1))
    for (dx, dy), h in zip(((-0.15, -0.15), (0.15, -0.1), (0.0, 0.17)), heights):
        m.add("ArmouredSteel", post(x + dx, y + dy, z + 0.1, z + h, 0.03, 6, 0.015))
    beacon(m, x, y + 0.17, z + max(heights) + 0.05, 0.06)


def ladder(m, x0, x1, y, z0, z1, width=0.5):
    """A fixed ladder against a wall facing +X: two rails and rungs every 0.3 m, from x0 (wall) to x1."""
    xm = (x0 + x1) / 2.0
    for side in (-1.0, 1.0):
        m.add("BareMetal", box(x0, x1, y + side * width / 2.0 - 0.03, y + side * width / 2.0 + 0.03, z0, z1))
    for k in range(int((z1 - z0 - 0.3) / 0.3)):
        z = z0 + 0.3 * (k + 1)
        m.add("BareMetal", bar((xm, y - width / 2.0, z), (xm, y + width / 2.0, z), 0.015, 4))


def drums(m, x0, x1, y0, y1):
    """Upright 200 l drums on a grid."""
    d, h = DRUM
    for i in range(max(1, int((x1 - x0) / (d + 0.05)))):
        for j in range(max(1, int((y1 - y0) / (d + 0.05)))):
            x, y = x0 + d / 2.0 + i * (d + 0.05), y0 + d / 2.0 + j * (d + 0.05)
            m.add("Olive", post(x, y, APRON_TOP, APRON_TOP + h, d / 2.0, 10))
            m.add("ArmouredSteel", post(x, y, APRON_TOP + h, APRON_TOP + h + 0.02, d / 2.0 - 0.03, 10))


def pallets(m, x0, x1, y0, y1, height, seed):
    """Wrapped loads on wooden pallets, one or two high."""
    length, width, deck = PALLET
    along_x = (x1 - x0) >= (y1 - y0)
    lx, ly = (length, width) if along_x else (width, length)
    nx, ny = max(1, int((x1 - x0 + 0.1) / (lx + 0.1))), max(1, int((y1 - y0 + 0.1) / (ly + 0.1)))
    lx, ly = min(lx, x1 - x0), min(ly, y1 - y0)
    for i in range(nx):
        for j in range(ny):
            a, c = x0 + i * (lx + 0.1), y0 + j * (ly + 0.1)
            z = APRON_TOP
            for k in range(1 + hashed(seed, i, j) % 2):
                load = (height - APRON_TOP) / 2.0 - deck - 0.05 * ((i + j) % 2)
                m.add("Wood", box(a, a + lx, c, c + ly, z, z + deck))
                m.add("Marking" if (i + j + k) % 2 else "Olive", box(a + 0.04, a + lx - 0.04, c + 0.04, c + ly - 0.04,
                                                                      z + deck, z + deck + load))
                z += deck + load


def corner_plates(m, outline, z0, z1, lamps=False, corners=None):
    """Dark steel plates wrapping the outline's corners (and a vertical blue lamp strip beside each), proud of the
    cladding."""
    leg, thick = CORNER_PLATE
    count = len(outline)
    for k in corners if corners is not None else range(count):
        before, after = Frame(outline[k - 1], outline[k]), Frame(outline[k], outline[(k + 1) % count])
        m.add("ArmouredSteel", after.box(0.0, leg, 0.0, thick, z0, z1),
              before.box(before.length - leg, before.length, 0.0, thick, z0, z1))
        if lamps:
            m.add("Lamp", after.box(leg + 0.08, leg + 0.08 + LAMP_STRIP, 0.0, 0.04, z0 + 0.6, z1 - 0.6))


def jersey(m, x, y0, y1):
    """A run of concrete jersey barriers along Y, centred on x."""
    count = max(1, round((y1 - y0) / JERSEY_LENGTH))
    step = (y1 - y0) / count
    for i in range(count):
        a, b = y0 + step * i + (0.02 if i else 0.0), y0 + step * (i + 1) - (0.02 if i < count - 1 else 0.0)
        m.add("Concrete", loft([(x + u, a, APRON_TOP + v) for u, v in JERSEY], [(x + u, b, APRON_TOP + v) for u, v in JERSEY]))


def hashed(*values):
    h = 2166136261
    for v in values:
        h = ((h ^ (v & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return h


def crate(m, x0, x1, y0, y1, z0, z1):
    """An olive stores crate: body, skids, a lid rim and a stencil plate."""
    m.add("Olive", box(x0 + 0.03, x1 - 0.03, y0 + 0.03, y1 - 0.03, z0 + 0.06, z1 - 0.07))
    m.add("ArmouredSteel", box(x0, x1, y0, y1, z0, z0 + 0.06), box(x0, x1, y0, y1, z1 - 0.07, z1))
    if x1 - x0 > y1 - y0:
        m.add("Marking", box((x0 + x1) / 2.0 - 0.18, (x0 + x1) / 2.0 + 0.18, y0 + 0.02, y0 + 0.03, z0 + 0.25, z0 + 0.4))
    else:
        m.add("Marking", box(x0 + 0.02, x0 + 0.03, (y0 + y1) / 2.0 - 0.18, (y0 + y1) / 2.0 + 0.18, z0 + 0.25, z0 + 0.4))


def crates(m, x0, x1, y0, y1, height, seed):
    """Crates on a grid over the extent, their long side along its long side, stacked one to three high."""
    along_x = (x1 - x0) >= (y1 - y0)
    length, width, high = CRATE
    lx, ly = (length, width) if along_x else (width, length)
    nx = max(1, int((x1 - x0 + CRATE_GAP) / (lx + CRATE_GAP)))
    ny = max(1, int((y1 - y0 + CRATE_GAP) / (ly + CRATE_GAP)))
    sx = (x1 - x0 - CRATE_GAP * (nx - 1)) / nx
    sy = (y1 - y0 - CRATE_GAP * (ny - 1)) / ny
    levels = max(1, int((height - APRON_TOP) / high + 1e-6))
    for i in range(nx):
        for j in range(ny):
            for k in range(1 + hashed(seed, i, j) % levels):
                a, c = x0 + i * (sx + CRATE_GAP), y0 + j * (sy + CRATE_GAP)
                crate(m, a, a + sx, c, c + sy, APRON_TOP + k * high, APRON_TOP + (k + 1) * high)


def container(m, x0, x1, y0, y1, z1):
    """A stores container: corrugated sides, end doors with locking bars, corner castings."""
    m.add("Olive", box(x0 + 0.04, x1 - 0.04, y0 + 0.04, y1 - 0.04, APRON_TOP + 0.1, z1 - 0.05))
    for x in (x0, x1 - 0.15):
        for y in (y0, y1 - 0.15):
            m.add("ArmouredSteel", box(x, x + 0.15, y, y + 0.15, APRON_TOP, z1))
    ribs = int((x1 - x0 - 0.3) / 0.28)
    for i in range(ribs):
        x = x0 + 0.15 + (x1 - x0 - 0.3) * (i + 0.5) / ribs
        for y in (y0, y1 - 0.04):
            m.add("Olive", box(x - 0.06, x + 0.06, y, y + 0.04, APRON_TOP + 0.15, z1 - 0.1))
    m.add("ArmouredSteel", box(x0, x1, y0, y1, z1 - 0.05, z1), box(x0, x1, y0, y1, APRON_TOP, APRON_TOP + 0.1))
    for y in (y0 + (y1 - y0) * 0.3, y0 + (y1 - y0) * 0.45, y0 + (y1 - y0) * 0.55, y0 + (y1 - y0) * 0.7):
        m.add("BareMetal", box(x1 - 0.04, x1, y - 0.025, y + 0.025, APRON_TOP + 0.2, z1 - 0.15))


def shrub(m, x, y, z, radius):
    m.add("Foliage", sphere((x, y, z + radius * 0.7), radius, 8, 5))


class Module:
    """One object's geometry, built in Unreal's frame; each piece carries its material."""

    def __init__(self, key, collection=None):
        self.key = key
        self.name = PREFIX + key
        self.collection = collection
        self.bm = bmesh.new()

    def add(self, material, *builds):
        index = list(MATERIALS).index(material)
        for build in builds:
            start = len(self.bm.faces)
            build(self.bm)
            self.bm.faces.ensure_lookup_table()
            for i in range(start, len(self.bm.faces)):
                self.bm.faces[i].material_index = index

    def finish(self, pivot=(0.0, 0.0, 0.0)):
        """The object, its origin at pivot (Unreal frame), mirrored into Blender's frame."""
        bm = self.bm
        bmesh.ops.translate(bm, verts=bm.verts, vec=-Vector(pivot))
        bmesh.ops.scale(bm, vec=(1.0, -1.0, 1.0), verts=bm.verts)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        used = sorted({face.material_index for face in bm.faces})
        for face in bm.faces:
            face.material_index = used.index(face.material_index)
        mesh = bpy.data.meshes.new(self.name)
        bm.to_mesh(mesh)
        bm.free()
        for index in used:
            mesh.materials.append(bpy.data.materials[list(MATERIALS)[index]])
        obj = bpy.data.objects.new(self.name, mesh)
        (self.collection or bpy.context.scene.collection).objects.link(obj)
        obj.location = blender(pivot)
        return obj


def blender(p):
    return Vector((p[0], -p[1], p[2]))


def empty(name, location, size=0.6, collection=None):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = "ARROWS"
    obj.empty_display_size = size
    obj.location = blender(location)
    (collection or bpy.context.scene.collection).objects.link(obj)
    return obj


# ---- modules ----

def build_apron():
    m = Module("Apron")
    m.add("Apron", box(-HALF, HALF, -HALF, HALF, 0.0, APRON_TOP))
    if LEVEL >= 2:
        # Painted lines: the slab's edge line, the exit lane's edges and a keep-clear hatch in front of the door.
        inset, width, thick = MARKING
        a, b = HALF - inset - width, HALF - inset
        z0, z1 = APRON_TOP - 0.003, APRON_TOP + thick  # let into the slab, 1 mm proud
        m.add("Marking", box(-b, b, a, b, z0, z1), box(-b, b, -b, -a, z0, z1), box(a, b, -a, a, z0, z1),
              box(-b, -a, -a, a, z0, z1))
        x0 = DOOR_PLANE + DOOR_POST[1] + 0.05
        for y in (DOOR_CENTRE_Y - LANE_WIDTH / 2.0, DOOR_CENTRE_Y + LANE_WIDTH / 2.0):
            m.add("Hazard", box(x0, a - 0.05, y - LANE_LINE / 2.0, y + LANE_LINE / 2.0, z0, z1))
        if LEVEL >= 3:
            # Sawn joints between the 4 m slab bays, 0.5 mm under the paint so the lines stay on top.
            spacing, joint = APRON_JOINT
            reach = a - 0.05
            k = int(reach / spacing)
            for i in range(-k, k + 1):
                c = i * spacing
                m.add("Rubber", box(c - joint / 2.0, c + joint / 2.0, -reach, reach, z0, APRON_TOP + 0.0005),
                      box(-reach, reach, c - joint / 2.0, c + joint / 2.0, z0, APRON_TOP + 0.0005))
        lane0 = DOOR_CENTRE_Y - LANE_WIDTH / 2.0 + 0.2
        for i in range(7):
            y = lane0 + i * 0.9
            m.add("Hazard", plan_prism([(x0 + 0.1, y), (x0 + 0.1, y + 0.3), (a - 0.15, y + 0.95), (a - 0.15, y + 0.65)],
                                       z0, z1))
    return m.finish()


def build_core():
    m = Module("Core")
    cx, cy = CORE_CENTRE
    if LEVEL < 2:
        m.add("Concrete", plan_prism(octagon(cx, cy, CORE_APOTHEM), APRON_TOP, CORE_ROOF))
        m.add("PaintedMetal", plan_prism(octagon(cx, cy, DRUM_APOTHEM), CORE_ROOF, DRUM_ROOF))
        mast_base = DRUM_ROOF
    else:
        outline = octagon(cx, cy, CORE_APOTHEM)
        entrance = Frame(outline[1], outline[2])  # the (+X, +Y) face, over the courtyard
        mid = entrance.length / 2.0
        if LEVEL >= 7:
            deck = core_portal(m, outline, entrance, mid, CORE_LOWER_ROOF, CORE_LOWER_ROWS)
            upper = octagon(cx, cy, CORE_UPPER_APOTHEM)
            deck, _ = block(m, upper, deck, CORE_ROOF, {k: CORE_UPPER_ROWS for k in range(8)}, ribbons=set(range(8)))
            corner_plates(m, upper, deck - 3.0, CORE_ROOF - COPING)
        elif LEVEL >= 5:
            deck = core_portal(m, outline, entrance, mid)
        else:
            deck = core_canopy(m, outline, entrance, mid)
        # The drum: a ribbon of windows, a blue lamp band under its coping.
        drum = octagon(cx, cy, DRUM_APOTHEM)
        mast_base, _ = block(m, drum, deck, DRUM_ROOF, {k: DRUM_ROWS for k in range(8)}, parapet=DRUM_PARAPET)
        for k in range(8):
            frame = Frame(drum[k], drum[(k + 1) % 8])
            m.add("Lamp", frame.box(0.35, frame.length - 0.35, 0.0, 0.03, DRUM_ROOF - 0.42, DRUM_ROOF - 0.32))
        mast_base += ROOF_MEMBRANE
    core_masts(m, mast_base)
    return m.finish()


def core_canopy(m, outline, entrance, mid):
    """Levels 2 to 4: doors under a canopy, the plaque above."""
    width, head = ENTRANCE
    rows = {k: CORE_ROWS for k in range(8)}
    rows[1] = [(COURTYARD_TOP, head)]
    deck, _ = block(m, outline, APRON_TOP, CORE_ROOF, rows, spans={1: [(mid - width / 2.0, mid + width / 2.0)]},
                    doors={1})
    # Entrance: a canopy with a lamp strip under its edge, blue lamp strips either side, the plaque above.
    w, depth, under, thick = CANOPY
    m.add("Concrete", entrance.box(mid - w / 2.0, mid + w / 2.0, 0.0, depth, under, under + thick))
    m.add("Lamp", entrance.box(mid - w / 2.0 + 0.2, mid + w / 2.0 - 0.2, depth - 0.12, depth - 0.04, under - 0.03,
                               under))
    for side in (-1.0, 1.0):
        s = mid + side * (width / 2.0 + 0.3)
        m.add("Lamp", entrance.box(s - LAMP_STRIP / 2.0, s + LAMP_STRIP / 2.0, 0.0, 0.05, COURTYARD_TOP + 0.3,
                                   head - 0.2))
    plate_w, plate_z0, plate_z1 = CORE_PLATE
    plaque(m, entrance, mid, plate_z0, plate_z1, plate_w)
    if LEVEL >= 4:
        corner_plates(m, outline, plate_z1 + 0.4, CORE_ROOF - COPING)
    return deck


def core_portal(m, outline, entrance, mid, roof=CORE_ROOF, rows_all=None):
    """Level 5: a double-height portal recessed into the entrance face, glazed doors with a transom at its back, a
    projecting steel frame with full-height lamp strips, the plaque over the lintel."""
    width, height, depth = PORTAL
    jamb, proj, lintel = PORTAL_FRAME
    a, b = mid - width / 2.0, mid + width / 2.0
    head = COURTYARD_TOP + height
    rows = {k: rows_all or CORE_ROWS for k in range(8)}
    rows[1] = [(COURTYARD_TOP, head)]
    deck, _ = block(m, outline, APRON_TOP, roof, rows, spans={1: [(a, b)]}, doors={1}, flush=CORE_FLUSH,
                    notch=(1, a, b, depth, head), open_edges={1})
    glazing(m, entrance, a, b, COURTYARD_TOP, head, door=True, back=-depth)
    m.add("Concrete", entrance.box(a, b, -depth, -REVEAL, APRON_TOP, COURTYARD_TOP))  # threshold
    bottom = COURTYARD_TOP + 0.06  # on the courtyard kerb
    m.add("ArmouredSteel", entrance.box(a - jamb, a, -REVEAL, proj, bottom, head + lintel),
          entrance.box(b, b + jamb, -REVEAL, proj, bottom, head + lintel),
          entrance.box(a - jamb, b + jamb, -REVEAL, proj, head, head + lintel))
    for s in (a - jamb / 2.0, b + jamb / 2.0):
        m.add("Lamp", entrance.box(s - LAMP_STRIP / 2.0, s + LAMP_STRIP / 2.0, proj, proj + 0.04, bottom + 0.3, head - 0.1))
    m.add("Lamp", entrance.box(a + 0.2, b - 0.2, -depth + 0.1, -REVEAL, head - 0.05, head))  # downlight in the soffit
    plate_w, gap, plate_h = PORTAL_PLATE if roof >= CORE_ROOF else CORE_PLATE_R7
    plaque(m, entrance, mid, head + lintel + gap, head + lintel + gap + plate_h, plate_w)
    if roof >= CORE_ROOF:
        corner_plates(m, outline, head + lintel + gap + plate_h + 0.4, CORE_ROOF - COPING)
    return deck


def core_masts(m, mast_base):
    cx, cy = CORE_CENTRE
    # The antenna cluster: a lattice mast on the drum to the 24 m tip, whips round it.
    mast = "ArmouredSteel" if LEVEL >= 3 else "BareMetal"
    m.add(mast, *lattice_tower(cx, cy, mast_base, CORE_MAST_TIP - 1.5, 0.45, 0.2, 4))
    m.add(mast, post(cx, cy, CORE_MAST_TIP - 1.5, CORE_MAST_TIP, 0.06))
    for dx, dy, tip in ((1.6, 1.6, 20.5), (-1.6, 1.6, 19.5), (1.6, -1.6, 19.0), (-1.6, -1.6, 20.0)):
        m.add(mast, post(cx + dx, cy + dy, mast_base, tip, 0.05))
        if LEVEL >= 3:
            beacon(m, cx + dx, cy + dy, tip - 0.25)
            m.add("PaintedMetal", post(cx + dx, cy + dy, tip - 1.6, tip - 0.6, 0.09, 8))  # whip antenna radome
    # The equipment ring: on the core's roof round the drum, from level 7 on the lower ring's terrace round the upper tier.
    terrace = LEVEL >= 7
    ring_deck = (CORE_LOWER_ROOF if terrace else CORE_ROOF) - PARAPET + ROOF_MEMBRANE
    ring_inner = CORE_UPPER_APOTHEM if terrace else DRUM_APOTHEM
    ring = (ring_inner + CORE_APOTHEM - REVEAL - PARAPET_WALL) / 2.0
    if LEVEL >= 3:
        deck = ring_deck
        depth, width, height = TERRACE_UNIT if terrace else CORE_UNIT
        for k in range(8):
            angle = 45.0 * k
            ux, uy = local_point(cx, cy, angle, ring, 0.0)
            hvac(m, ux, uy, angle, depth, width, deck, height)
        # Sector antennas on the lattice, cabinets round its foot, beacons up the mast.
        for k in range(3):
            angle = 90.0 + 120.0 * k
            m.add("PaintedMetal", obox(cx, cy, angle, 0.12, 0.32, 19.2, 20.5, u=0.45))
        for k in range(4):
            m.add("PaintedMetal", obox(cx, cy, 90.0 * k, 0.5, 0.7, mast_base, mast_base + 1.0, u=2.15))
        for z in (CORE_MAST_TIP - 0.2, 21.0):
            beacon(m, cx + (0.0 if z > 23 else 0.25), cy, z, 0.07)
    if LEVEL >= 5:
        # Cabinets in the corners between the condensers, a guard rail on the drum's coping, a ladder up the drum.
        deck = ring_deck
        sx, sy, h = CORE_CABINET
        corner = ring / math.cos(math.pi / 8.0)
        for k in range(8):
            angle = 22.5 + 45.0 * k
            m.add("PaintedMetal", obox(cx, cy, angle, sx, sy, deck, deck + h, u=corner))
            m.add("ArmouredSteel", obox(cx, cy, angle, sx + 0.06, sy + 0.06, deck + h, deck + h + 0.04, u=corner))
        rail = octagon(cx, cy, DRUM_APOTHEM - 0.15)
        for k in range(8):
            p, q = rail[k], rail[(k + 1) % 8]
            m.add("BareMetal", post(p[0], p[1], DRUM_ROOF, DRUM_ROOF + 1.0, 0.025, 4))
            m.add("BareMetal", bar((p[0], p[1], DRUM_ROOF + 1.0), (q[0], q[1], DRUM_ROOF + 1.0), 0.025, 4))
            m.add("BareMetal", bar((p[0], p[1], DRUM_ROOF + 0.5), (q[0], q[1], DRUM_ROOF + 0.5), 0.02, 4))
        if terrace:
            # Guard rail on the lower ring's coping round the terrace.
            rail = octagon(cx, cy, CORE_APOTHEM - 0.2)
            for k in range(8):
                p, q = rail[k], rail[(k + 1) % 8]
                m.add("BareMetal", post(p[0], p[1], CORE_LOWER_ROOF, CORE_LOWER_ROOF + 1.0, 0.025, 4))
                m.add("BareMetal", bar((p[0], p[1], CORE_LOWER_ROOF + 1.0), (q[0], q[1], CORE_LOWER_ROOF + 1.0), 0.025, 4))
            deck = CORE_ROOF  # the drum's ladder starts on the upper tier's coping
        face = Frame(*[octagon(cx, cy, DRUM_APOTHEM)[i] for i in (5, 6)])  # the drum's (-X, -Y) face
        for side in (-1.0, 1.0):
            m.add("BareMetal", face.box(face.length / 2.0 + side * 0.25 - 0.03, face.length / 2.0 + side * 0.25 + 0.03,
                                        0.0, 0.1, deck, DRUM_ROOF + 1.0))
        for k in range(int((DRUM_ROOF - deck) / 0.3)):
            z = deck + 0.3 * (k + 1)
            m.add("BareMetal", face.box(face.length / 2.0 - 0.25, face.length / 2.0 + 0.25, 0.05, 0.08, z - 0.015, z + 0.015))


def build_wing():
    m = Module("Wing")
    if LEVEL < 2:
        for extent, roof in ((WING_REAR, WING_END_ROOF), (WING_LINK, WING_LINK_ROOF), (WING_FRONT, WING_ROOF)):
            m.add("Concrete", box(*extent, APRON_TOP, roof))
        deck = WING_ROOF
    else:
        # Edges of a rect outline: 0 faces -Y, 1 faces +X, 2 faces +Y, 3 faces -X. Faces against another block stay
        # blank; the +Y faces carry the eagle plaques.
        plate_w, plate_z0, plate_z1 = WING_PLATE
        for extent, roof, rows, plaques in ((WING_REAR, WING_END_ROOF, {0: END_ROWS, 2: END_ROWS, 3: END_ROWS}, (2,)),
                                            (WING_LINK, WING_LINK_ROOF, {0: LINK_ROWS, 2: LINK_ROWS}, ()),
                                            (WING_FRONT, WING_ROOF, {0: WING_ROWS, 1: WING_ROWS, 2: WING_ROWS}, (1, 2))):
            outline = rect(*extent)
            skips = {}
            for k in plaques:
                frame = Frame(outline[k], outline[(k + 1) % 4])
                mid = frame.length / 2.0
                skips[k] = [(mid - plate_w / 2.0 - 0.2, mid + plate_w / 2.0 + 0.2)]
            front = extent is WING_FRONT
            end = extent is WING_REAR  # flat roof with the coping on its edge, the dish's pivot on the deck
            door_mid = Frame(outline[0], outline[1]).length / 2.0
            if front and LEVEL >= 4:
                skips[0] = [(door_mid - 0.7, door_mid + 0.7)]  # the door to the courtyard
            if LEVEL >= 5:
                # Chamfered corners (edge k becomes 2k + 1, s shifts by the cut), ribbon glazing, the link's face against
                # the connector kept flush.
                shape = chamfer(outline, WING_CHAMFER)
                block(m, shape, APRON_TOP, roof, {2 * k + 1: r for k, r in rows.items()},
                      {2 * k + 1: [(a - WING_CHAMFER, b - WING_CHAMFER) for a, b in v] for k, v in skips.items()},
                      flush=(1,) if extent is WING_LINK else (), ribbons={2 * k + 1 for k in rows}, coping_above=end)
                outline_plates = shape
            else:
                block(m, outline, APRON_TOP, roof, rows, skips, coping_above=end)
                outline_plates = outline
            for k in plaques:
                frame = Frame(outline[k], outline[(k + 1) % 4])
                plaque(m, frame, frame.length / 2.0, plate_z0, plate_z1, plate_w)
            if LEVEL >= 4:
                # Not on the link's corners either side of the connector's face (chamfered: corners 1 and 2).
                skip = (1, 2) if extent is WING_LINK and LEVEL >= 5 else ()
                corner_plates(m, outline_plates, APRON_TOP + 0.6, roof - (0.0 if end else COPING), lamps=front or end,
                              corners=[k for k in range(len(outline_plates)) if k not in skip])
                if front:
                    door = Frame(outline[0], outline[1])
                    m.add("ArmouredSteel", door.box(door_mid - 0.5, door_mid + 0.5, 0.0, 0.04, APRON_TOP, 2.3))
                    m.add("ArmouredSteel", door.box(door_mid - 0.6, door_mid + 0.6, 0.0, 0.3, 2.45, 2.55))
                    m.add("Lamp", door.box(door_mid - 0.15, door_mid + 0.15, 0.15, 0.27, 2.38, 2.45))
        deck = WING_ROOF - PARAPET
        if DISH_TOWER:
            tower = rect(*DISH_TOWER)
            block(m, tower, deck, DISH_TOWER_ROOF, {0: [(7.8, 8.9)], 3: [(7.8, 8.9)]}, coping_above=True)
            frame = Frame(tower[2], tower[3])
            w, z0, z1 = DISH_TOWER_PLATE
            plaque(m, frame, frame.length / 2.0, z0, z1, w)
        deck += ROOF_MEMBRANE
    wing_roofs(m, deck)
    return m.finish()


def wing_roofs(m, deck):
    """The wing's roof kit, placed from the blocks' extents: the masts and the panel antenna array on the link (the dish
    tower fills the rear block); from level 3 condensers, a hatch, vents; from level 5 whip clusters and a ladder up
    from the link to the rear block's roof."""
    edge = REVEAL + PARAPET_WALL if LEVEL >= 2 else 0.3
    link_deck = WING_LINK_ROOF - (PARAPET - ROOF_MEMBRANE if LEVEL >= 2 else 0.0)
    lx0, lx1, ly0, ly1 = WING_LINK[0] + edge, WING_LINK[1] - edge, WING_LINK[2] + edge, WING_LINK[3] - edge
    fx0, fx1, fy0, fy1 = WING_FRONT[0] + edge, WING_FRONT[1] - edge, WING_FRONT[2] + edge, WING_FRONT[3] - edge
    mast = "ArmouredSteel" if LEVEL >= 3 else "BareMetal"
    if LEVEL >= 7:
        # The M2 panel antenna: a framed 2 x 1.2 m panel on a short mast, tilted 10 degrees back.
        width, height, mast_h = WING_PANEL
        px, py = lx0 + 1.3, ly0 + 0.6
        m.add("ArmouredSteel", post(px, py, link_deck, link_deck + mast_h + 0.3, 0.08, 8))
        tilt = math.radians(10.0)
        up = Vector((0.0, math.sin(tilt), math.cos(tilt)))
        normal = Vector((0.0, -math.cos(tilt), math.sin(tilt)))
        centre = Vector((px, py, link_deck + mast_h + height / 2.0))
        m.add("PaintedMetal", box3(centre, (1.0, 0.0, 0.0), up, normal, width / 2.0 - 0.06, height / 2.0 - 0.06, 0.05))
        for s_, h_ in ((1.0, 0.0), (-1.0, 0.0)):
            m.add("ArmouredSteel", box3(centre + Vector((s_ * (width / 2.0 - 0.03), 0.0, 0.0)), (1.0, 0.0, 0.0), up, normal,
                                        0.03, height / 2.0, 0.07))
        for s_ in (1.0, -1.0):
            m.add("ArmouredSteel", box3(centre + up * s_ * (height / 2.0 - 0.03), (1.0, 0.0, 0.0), up, normal,
                                        width / 2.0, 0.03, 0.07))
    else:
        m.add("PaintedMetal", box(lx0 + 0.3, lx0 + 2.3, ly0 + 0.05, ly0 + 0.45, link_deck, link_deck + 2.0))
    for x in (lx0 + 0.4, lx0 + 1.4):
        m.add(mast, post(x, ly1 - 0.3, link_deck, WING_MAST_TIP, 0.06))
        if LEVEL >= 3:
            beacon(m, x, ly1 - 0.3, WING_MAST_TIP - 0.3)
    if LEVEL < 3:
        return
    if LEVEL < 7:
        for x in (lx0 + 0.5, lx0 + 2.1):
            m.add("ArmouredSteel", box(x - 0.05, x + 0.05, ly0 + 0.45, ly0 + 1.05, link_deck, link_deck + 1.6))
        m.add("ArmouredSteel", box(lx0 + 0.3, lx0 + 2.3, ly0 + 0.45, ly0 + 1.05, link_deck + 1.55, link_deck + 1.65))
    vent(m, lx1 - 0.5, ly0 + 0.4, link_deck)
    hvac(m, fx0 + 1.2, fy0 + 0.95, 0.0, 1.2, 1.4, deck, 0.8)
    hatch(m, fx1 - 0.55, fy1 - 0.55, deck)
    vent(m, fx0 + 0.4, fy1 - 0.4, deck)
    if LEVEL < 5:
        return
    whip_cluster(m, fx1 - 0.45, fy0 + 0.5, deck)
    whip_cluster(m, lx1 - 1.5, ly1 - 0.4, link_deck)
    whip_cluster(m, lx0 + 3.0, ly0 + 0.5, link_deck)
    ladder(m, WING_REAR[1], WING_REAR[1] + 0.2, (ly0 + ly1) / 2.0, link_deck,
           (WING_END_ROOF if DISH_TOWER is None else WING_ROOF) + 0.9)
    if LEVEL < 8:
        return
    # Round 8: the middle section's terrace packed with plant, a lamp band over its ground-floor glazing.
    hvac(m, lx0 + 3.6, ly1 - 1.1, 0.0, 1.2, 1.0, link_deck, 0.75)
    hvac(m, lx1 - 2.4, ly1 - 1.4, 0.0, 1.2, 1.0, link_deck, 0.75)
    for x, y in ((lx1 - 1.0, ly1 - 1.4), (lx0 + 2.3, ly1 - 0.5)):
        m.add("PaintedMetal", centred_box(x, y, 0.6, 0.6, link_deck, link_deck + 1.0))
        m.add("ArmouredSteel", centred_box(x, y, 0.66, 0.66, link_deck + 1.0, link_deck + 1.04))
    vent(m, lx0 + 4.8, ly0 + 0.4, link_deck)
    cut = WING_CHAMFER if LEVEL >= 5 else 0.0
    x0, x1, y0, y1 = WING_LINK
    m.add("Lamp", box(x0 + cut + 0.2, x1 - cut - 0.2, y1, y1 + 0.03, LINK_ROWS[0][1] + 0.2, LINK_ROWS[0][1] + 0.3))
    m.add("Lamp", box(x0 + cut + 0.2, CONNECTOR_WING[0] - 0.3, y0 - 0.03, y0, LINK_ROWS[0][1] + 0.2,
                      LINK_ROWS[0][1] + 0.3))


def garage_shell(m):
    x0, x1, y0, y1 = GARAGE
    bay_x0 = DOOR_PLANE - BAY_DEPTH
    door_y0, door_y1 = DOOR_CENTRE_Y - DOOR_WIDTH / 2.0, DOOR_CENTRE_Y + DOOR_WIDTH / 2.0
    m.add("Concrete",
          box(x0, bay_x0, y0, y1, APRON_TOP, GARAGE_ROOF),  # back wall
          box(bay_x0, x1, y0, door_y0, APRON_TOP, GARAGE_ROOF),  # -Y side and jamb
          box(bay_x0, x1, door_y1, y1, APRON_TOP, GARAGE_ROOF),  # +Y side and jamb
          box(bay_x0, x1, door_y0, door_y1, BAY_CEILING, GARAGE_ROOF),  # roof over the bay
          box(x1 - LINTEL_DEPTH, x1, door_y0, door_y1, DOOR_TOP, BAY_CEILING))  # lintel


def build_garage():
    m = Module("Garage")
    if LEVEL < 2:
        garage_shell(m)
        m.add("Glass", box(6.0, 10.5, -9.0, -4.0, GARAGE_ROOF, GARAGE_ROOF + 0.4))  # rooftop solar panels
        return m.finish()
    x0, x1, y0, y1 = GARAGE
    bay_x0 = DOOR_PLANE - BAY_DEPTH
    dy0, dy1 = DOOR_CENTRE_Y - DOOR_WIDTH / 2.0, DOOR_CENTRE_Y + DOOR_WIDTH / 2.0
    eave = LEVEL >= 5
    deck = GARAGE_ROOF - (EAVE if eave else PARAPET)
    top = deck if eave else GARAGE_ROOF - COPING
    skin = GARAGE_SKIN
    # The shell round the bay (the bay itself exactly as level 1), its long walls set back behind the pilasters.
    m.add("Concrete",
          box(x0, bay_x0, y0 + skin, y1 - skin, APRON_TOP, deck),
          box(bay_x0, x1, dy0, dy1, BAY_CEILING, deck),
          box(x1 - LINTEL_DEPTH, x1, dy0, dy1, DOOR_TOP, BAY_CEILING))
    if LEVEL >= 7:
        # The side walls carry recesses behind the bay's faces, so racks and cabinets stay out of the 7 m lane.
        nx0, nx1, depth, height = BAY_NICHE
        for wall0, wall1, face, side in ((y0 + skin, dy0, dy0, -1.0), (dy1, y1 - skin, dy1, 1.0)):
            back = face + side * depth
            ya, yb = sorted((face, back))
            outer = (wall0, ya) if side < 0 else (yb, wall1)
            m.add("Concrete", box(bay_x0, x1, *outer, APRON_TOP, deck),
                  box(bay_x0, nx0, ya, yb, APRON_TOP, deck), box(nx1, x1, ya, yb, APRON_TOP, deck),
                  box(nx0, nx1, ya, yb, APRON_TOP + height, deck))
            bay_kit(m, nx0, nx1, back, face, side)
        for y in (dy0 + 1.0, dy1 - 1.0):
            m.add("Lamp", box(bay_x0 + 1.0, x1 - 1.5, y - 0.08, y + 0.08, BAY_CEILING - 0.3, BAY_CEILING - 0.24))
            for x in (bay_x0 + 1.2, x1 - 1.7):
                m.add("BareMetal", box(x - 0.02, x + 0.02, y - 0.02, y + 0.02, BAY_CEILING - 0.24, BAY_CEILING))
    else:
        m.add("Concrete", box(bay_x0, x1, y0 + skin, dy0, APRON_TOP, deck), box(bay_x0, x1, dy1, y1 - skin, APRON_TOP, deck))
    width, spacing = PILASTER
    count = max(1, round((x1 - x0) / spacing))
    columns = [x0 + (x1 - x0) * i / count for i in range(count + 1)]
    for x in columns:
        a, b = max(x0, x - width / 2.0), min(x1, x + width / 2.0)
        m.add("Concrete", box(a, b, y0, y0 + skin, APRON_TOP, top), box(a, b, y1 - skin, y1, APRON_TOP, top))
    for a, b in zip(columns, columns[1:]):
        for wall, out in ((y0 + skin, -1.0), (y1 - skin, 1.0)):
            # Plinth, a clerestory strip with its frame.
            ya, yb = sorted((wall, wall + out * 0.06))
            m.add("Concrete", box(a + width / 2.0, b - width / 2.0, ya, yb, APRON_TOP, 0.6))
            ga, gb = sorted((wall, wall + out * 0.03))
            m.add("Glass", box(a + width / 2.0 + 0.2, b - width / 2.0 - 0.2, ga, gb, CLERESTORY[0], CLERESTORY[1]))
            for z in (CLERESTORY[0] - 0.08, CLERESTORY[1]):
                m.add("PaintedMetal", box(a + width / 2.0 + 0.15, b - width / 2.0 - 0.15, ya, yb, z, z + 0.08))
    outline = rect(x0, x1, y0, y1)
    if eave:
        # A heavy plated eave sloping 45 degrees in over the +X and +-Y faces (straight up over the back wall, where
        # the connector joins), round a membrane deck.
        crest = rect(x0, x1 - EAVE, y0 + EAVE, y1 - EAVE)
        wall = rect(x0 + EAVE_WALL, x1 - EAVE - EAVE_WALL, y0 + EAVE + EAVE_WALL, y1 - EAVE - EAVE_WALL)
        m.add("PaintedMetal", frustum_ring(outline, crest, wall, deck, GARAGE_ROOF))
        m.add("Roofing", plan_prism(wall, deck, deck + ROOF_MEMBRANE))
    else:
        # Roof: parapet round a membrane deck, metal coping.
        shell = rect(x0, x1, y0 + skin, y1 - skin)
        wall = inset_outline(shell, PARAPET_WALL)
        m.add("Concrete", annulus(shell, wall, deck, top))
        m.add("Roofing", plan_prism(wall, deck, deck + ROOF_MEMBRANE))
        m.add(trim(), annulus(outline, inset_outline(outline, skin + PARAPET_WALL + 0.03), top, GARAGE_ROOF))
    roof = deck + ROOF_MEMBRANE
    if LEVEL < 3:
        m.add("Glass", box(6.0, 10.5, -9.0, -4.0, roof, deck + 0.4))  # solar panels (detailed later)
    else:
        # Photovoltaic rows on steel racks, a condenser, two roof exhaust fans and vents.
        depth, length, low, high, thick = SOLAR
        count = max(1, round((SOLAR_Y[1] - SOLAR_Y[0]) / length))
        step = (SOLAR_Y[1] - SOLAR_Y[0]) / count
        for xc in SOLAR_ROWS:
            for yr in (SOLAR_Y[0] + 0.2, SOLAR_Y[1] - 0.2):
                m.add("ArmouredSteel", box(xc - depth / 2.0, xc - depth / 2.0 + 0.08, yr - 0.04, yr + 0.04, roof, roof + high),
                      box(xc + depth / 2.0 - 0.08, xc + depth / 2.0, yr - 0.04, yr + 0.04, roof, roof + low))
            for i in range(count):
                ya, yb = SOLAR_Y[0] + step * i + 0.03, SOLAR_Y[0] + step * (i + 1) - 0.03
                corners = [(xc + depth / 2.0, ya, roof + low), (xc - depth / 2.0, ya, roof + high),
                           (xc - depth / 2.0, yb, roof + high), (xc + depth / 2.0, yb, roof + low)]
                m.add("Solar", loft(corners, [(a, b, c + thick) for a, b, c in corners]))
        kit = GARAGE_ROOF_KIT if eave else {"hvac": (4.9, -2.7), "fans": ((11.3, -2.5), (11.3, -9.5)),
                                            "vents": ((5.0, -9.6), (7.4, -2.4))}
        hvac(m, *kit["hvac"], 0.0, 1.0, 1.3, roof, 0.8)
        for xf, yf in kit["fans"]:
            m.add("PaintedMetal", post(xf, yf, roof, roof + 0.5, 0.42, 12))
            m.add("ArmouredSteel", post(xf, yf, roof + 0.5, roof + 0.62, 0.48, 12, 0.3))
        for xv, yv in kit["vents"]:
            vent(m, xv, yv, roof)
        if eave:
            hatch(m, *kit["hatch"], roof)
    # The door frame: hazard-striped steel posts and a steel header, a blue lamp strip over the opening. From level 5
    # a heavy portal frame projecting from the door plane, still clear of the 7 m lane.
    if eave:
        post_w, proj, header = GARAGE_PORTAL
        front0, front1 = x1 - 0.1, x1 + proj
    else:
        front0, front1 = x1 - 0.1, x1 + DOOR_POST[1]
        post_w, header = DOOR_POST[0], 0.35
    bands = int(math.ceil((DOOR_TOP - APRON_TOP) / HAZARD_BAND))
    for ya, yb in ((dy0 - post_w, dy0), (dy1, dy1 + post_w)):
        for i in range(bands):
            z0 = APRON_TOP + i * HAZARD_BAND
            m.add("Hazard" if i % 2 else "ArmouredSteel", box(front0, front1, ya, yb, z0, min(DOOR_TOP, z0 + HAZARD_BAND)))
    m.add("ArmouredSteel", box(front0, front1, dy0 - post_w, dy1 + post_w, DOOR_TOP, DOOR_TOP + header))
    if eave:
        for ya, yb in ((dy0 - post_w - 0.05, dy0), (dy1, dy1 + post_w + 0.05)):  # feet, kept out of the lane
            m.add("ArmouredSteel", box(front1 - 0.1, front1 + 0.05, ya, yb, APRON_TOP, APRON_TOP + 0.3))
        m.add("Lamp", box(front1, front1 + 0.05, dy0 + 0.2, dy1 - 0.2, DOOR_TOP + header * 0.4, DOOR_TOP + header * 0.55))
    else:
        m.add("Lamp", box(front1, front1 + 0.05, dy0 + 0.2, dy1 - 0.2, DOOR_TOP + 0.45, DOOR_TOP + 0.55))
    # Inside: the roller drum's housing behind the lintel, ceiling light strips, a rubber floor.
    m.add("ArmouredSteel", box(x1 - LINTEL_DEPTH - 0.5, x1 - LINTEL_DEPTH, dy0, dy1, DOOR_TOP, BAY_CEILING))
    for y in (dy0 + 1.0, dy1 - 1.0):
        m.add("Lamp", box(bay_x0 + 1.0, x1 - 1.5, y - 0.1, y + 0.1, BAY_CEILING - 0.02, BAY_CEILING))
    m.add("Rubber", box(bay_x0, x1 - LINTEL_DEPTH, dy0, dy1, APRON_TOP, APRON_TOP + 0.01))
    # The +Y wall: a blue accent panel and a personnel door; the -Y wall: a blue panel.
    m.add("BlueAccent", box(9.6, 11.4, y1 - skin, y1 - skin + 0.05, 1.4, 4.4))
    m.add("ArmouredSteel", box(4.4, 5.4, y1 - skin, y1 - skin + 0.04, APRON_TOP, 2.3))
    m.add("BlueAccent", box(6.6, 9.2, y0 + skin - 0.05, y0 + skin, 1.4, 4.4))
    if LEVEL >= 4:
        # Front corners: steel plates and blue lamp strips; a lamp over the personnel door; a bay sign by the door.
        leg, thick = CORNER_PLATE
        for ya, yb, yl in ((y0, y0 + leg, (y0 + leg + 0.08, y0 + leg + 0.08 + LAMP_STRIP)),
                           (y1 - leg, y1, (y1 - leg - 0.08 - LAMP_STRIP, y1 - leg - 0.08))):
            m.add("ArmouredSteel", box(x1, x1 + thick, ya, yb, APRON_TOP + 0.6, top))
            m.add("Lamp", box(x1, x1 + 0.04, yl[0], yl[1], 1.2, top - 0.6))
        m.add("ArmouredSteel", box(x1 - leg, x1, y0 - thick, y0, APRON_TOP + 0.6, top),
              box(x1 - leg, x1, y1, y1 + thick, APRON_TOP + 0.6, top))
        m.add("ArmouredSteel", box(4.3, 5.5, y1 - skin, y1 - skin + 0.3, 2.45, 2.55))
        m.add("Lamp", box(4.75, 5.05, y1 - skin + 0.15, y1 - skin + 0.27, 2.38, 2.45))
        m.add("Marking", box(10.0, 11.0, y1 - skin + 0.05, y1 - skin + 0.07, 4.7, 5.3))
        m.add("BlueAccent", box(10.0, 11.0, y1 - skin + 0.07, y1 - skin + 0.08, 4.7, 4.85))
    return m.finish()


def bay_kit(m, x0, x1, back, face, side):
    """A recess's fit-out: steel racks with stores on their shelves, a tool cabinet and a hose reel, all behind the bay's
    wall face (face) so the 7 m lane stays clear."""
    inner = face + side * 0.03
    ya, yb = sorted((back, inner))
    for rack in range(2):
        a = x0 + 0.1 + rack * 2.1
        b = a + 1.9
        for x in (a, b - 0.06):
            for y in (ya, yb - 0.06):
                m.add("ArmouredSteel", box(x, x + 0.06, y, y + 0.06, APRON_TOP, 2.6))
        for z in (0.35, 1.15, 1.95):
            m.add("PaintedMetal", box(a, b, ya, yb, z, z + 0.04))
            m.add("Olive", box(a + 0.15, a + 0.85, ya + 0.05, yb - 0.05, z + 0.04, z + 0.42),
                  box(a + 1.0, b - 0.15, ya + 0.05, yb - 0.05, z + 0.04, z + 0.32))
    c = x0 + 4.3
    m.add("Hazard", box(c, c + 0.8, ya, yb, APRON_TOP, 1.8))
    m.add("ArmouredSteel", box(c + 0.05, c + 0.75, *sorted((inner, inner - side * 0.01)), 0.2, 1.7))
    m.add("PaintedMetal", cylinder((x1 - 0.5, (ya + yb) / 2.0, 1.5), (0.0, 1.0, 0.0), 0.35, yb - ya - 0.04, 12))
    m.add("ArmouredSteel", box(x1 - 0.55, x1 - 0.45, ya, yb, APRON_TOP, 1.2))


def build_garage_door():
    m = Module("GarageDoor")
    y0, y1 = DOOR_CENTRE_Y - DOOR_WIDTH / 2.0 + 0.01, DOOR_CENTRE_Y + DOOR_WIDTH / 2.0 - 0.01
    m.add("ArmouredSteel", box(*DOOR_LEAF, y0, y1, APRON_TOP, DOOR_TOP))
    if LEVEL >= 2:
        # Sectional leaf: six insulated panels with ribs at the joints, a row of vision panels, a rubber bottom seal.
        face = DOOR_LEAF[1]
        sections = 6
        height = (DOOR_TOP - APRON_TOP) / sections
        for i in range(1, sections):
            z = APRON_TOP + height * i
            m.add("PaintedMetal", box(face, face + 0.03, y0, y1, z - 0.03, z + 0.03))
        pane = (y1 - y0 - 0.8) / 6.0
        for k in range(6):
            a = y0 + 0.4 + k * pane
            m.add("Glass", box(face, face + 0.01, a + 0.1, a + pane - 0.1, APRON_TOP + height * 3.3,
                               APRON_TOP + height * 3.7))
        m.add("Rubber", box(DOOR_LEAF[0], face + 0.02, y0, y1, APRON_TOP, APRON_TOP + 0.06))
    return m.finish(DOOR_PIVOT)


def build_courtyard():
    m = Module("Courtyard")
    x0, x1 = COURTYARD_X
    y0, y1 = COURTYARD_Y
    # Against the core's +X flat (x0) and its (+X, +Y) face, where the entrance is, notched round the wing's front block.
    corner = on_core_face(45.0, -1.0)
    reach = sum(CORE_CENTRE) + CORE_APOTHEM * math.sqrt(2.0)  # x + y along the entrance face
    nx, ny = WING_FRONT[1] + COURTYARD_GAP, WING_FRONT[2] - COURTYARD_GAP
    top = on_core_face(45.0, 1.0)  # the entrance face's upper end
    if ny < top[1]:
        outline = [(x0, y0), (x1, y0), (x1, y1), (nx, y1), (nx, ny), (reach - ny, ny), corner]
    else:  # the front block stands back past the face: the forecourt runs across the whole entrance face
        outline = [(x0, y0), (x1, y0), (x1, y1), (nx, y1), (nx, ny), (top[0], ny), top, corner]
    m.add("Concrete", plan_prism(outline, APRON_TOP, COURTYARD_TOP))
    risers, tread = STEPS
    rise = (COURTYARD_TOP - APRON_TOP) / risers
    for k in range(risers):
        m.add("Concrete", box(x1, x1 + tread * (risers - k), STEPS_Y[0], STEPS_Y[1], APRON_TOP + rise * k,
                              APRON_TOP + rise * (k + 1)))
    fx, fy = FLAG_COURTYARD
    if LEVEL < 2:
        m.add("EaglePlate", centred_box(fx, fy, PLINTH[0], PLINTH[1], COURTYARD_TOP, COURTYARD_TOP + PLINTH[2]))
        for px, py in PLANTERS:
            m.add("Concrete", centred_box(px, py, 2.2, 1.4, COURTYARD_TOP, COURTYARD_TOP + 0.7))
        return m.finish()
    # Kerb and paving, metal step nosings.
    inner = inset_outline(outline, 0.3)
    m.add("Concrete", annulus(outline, inner, COURTYARD_TOP, COURTYARD_TOP + 0.06))
    m.add("Apron", plan_prism(inner, COURTYARD_TOP, COURTYARD_TOP + 0.02))
    for k in range(risers):
        edge = x1 + tread * (risers - k)
        m.add("ArmouredSteel", box(edge - 0.05, edge, STEPS_Y[0], STEPS_Y[1], APRON_TOP + rise * (k + 1) - 0.03,
                                   APRON_TOP + rise * (k + 1) + 0.005))
    # The flag plinth: concrete with the eagle plaque on its front.
    sx, sy, sh = PLINTH
    m.add("Concrete", centred_box(fx, fy, sx, sy, COURTYARD_TOP, COURTYARD_TOP + sh))
    m.add("Concrete", centred_box(fx, fy, sx + 0.16, sy + 0.16, COURTYARD_TOP, COURTYARD_TOP + 0.15))
    front = Frame((fx + sx / 2.0, fy - sy / 2.0), (fx + sx / 2.0, fy + sy / 2.0))
    plaque(m, front, sy / 2.0, COURTYARD_TOP + 0.2, COURTYARD_TOP + sh - 0.08, sy - 0.2)
    # Two planters with soil and shrubs.
    for px, py in PLANTERS:
        planter = rect(px - 1.1, px + 1.1, py - 0.7, py + 0.7)
        bed = inset_outline(planter, 0.15)
        m.add("Concrete", annulus(planter, bed, COURTYARD_TOP, COURTYARD_TOP + 0.7))
        m.add("Soil", plan_prism(bed, COURTYARD_TOP, COURTYARD_TOP + 0.6))
        for dx, dy, r in ((-0.55, -0.15, 0.38), (0.05, 0.2, 0.45), (0.6, -0.1, 0.35)):
            shrub(m, px + dx, py + dy, COURTYARD_TOP + 0.6, r)
    return m.finish()


def build_flag(key, x, y, z0, tip):
    m = Module(key)
    if LEVEL < 3:
        m.add("BareMetal", post(x, y, z0, tip, 0.07, 8, 0.04))
        m.add("BlueAccent", box(x + 0.08, x + 1.98, y - 0.02, y + 0.02, tip - 1.3, tip - 0.1))
        return m.finish()
    # Tapered pole on a collar, a ball finial at the tip, the flag hoisted to the top.
    m.add("BareMetal", post(x, y, z0, z0 + 0.25, 0.14, 12))
    m.add("BareMetal", post(x, y, z0 + 0.25, tip - 2.0 * FINIAL, 0.07, 10, 0.04))
    m.add("BareMetal", sphere((x, y, tip - FINIAL), FINIAL, 10, 5))
    flag_cloth(m, x, y, tip - FLAG[2])
    return m.finish()


def corridor(m, outline, roof, exposed):
    """A glazed link: concrete plinth, glazing set back between mullions, a roof slab with a blue lamp strip along the
    fascia of its exposed edges (by index)."""
    plinth, fascia, setback, spacing = CORRIDOR
    if LEVEL >= 7:
        plinth, overhang = CONNECTOR_R7
        fascia = roof - 4.5
        for k in exposed:
            frame = Frame(outline[k], outline[(k + 1) % len(outline)])
            m.add("Concrete", frame.box(0.25, frame.length - 0.25, 0.0, overhang, roof - 0.25, roof))
            m.add("Lamp", frame.box(0.35, frame.length - 0.35, overhang - 0.12, overhang - 0.04, roof - 0.28, roof - 0.25))
    m.add("Concrete", plan_prism(outline, APRON_TOP, plinth))
    m.add("Glass", plan_prism(inset_outline(outline, setback), plinth, roof - fascia))
    m.add("Concrete", plan_prism(outline, roof - fascia, roof))
    for k in range(len(outline)):
        frame = Frame(outline[k], outline[(k + 1) % len(outline)])
        count = max(1, round(frame.length / spacing))
        for i in range(count + 1):
            s = min(max(frame.length * i / count, 0.25), frame.length - 0.25)  # clear of acute corners
            m.add("PaintedMetal", frame.box(s - 0.05, s + 0.05, -setback, 0.0, plinth, roof - fascia))
        if k in exposed:
            m.add("Lamp", frame.box(0.2, frame.length - 0.2, 0.0, 0.03, roof - fascia + 0.1, roof - fascia + 0.18))
            if LEVEL >= 6:  # a transom over the glazing band and a lamp strip along the plinth
                m.add("PaintedMetal", frame.box(0.25, frame.length - 0.25, -setback, 0.0, roof - fascia - 1.0,
                                                roof - fascia - 0.92))
                m.add("Lamp", frame.box(0.2, frame.length - 0.2, 0.0, 0.03, plinth - 0.12, plinth - 0.06))


def build_connectors():
    m = Module("Connectors")
    # Core (+Y flat) to the wing link.
    x0, x1 = CONNECTOR_WING
    if LEVEL >= 2:
        corridor(m, rect(x0, x1, on_core_face(90.0, 0.0)[1], WING_LINK[2]), CONNECTOR_ROOF, (1, 3))
    else:
        m.add("Glass", box(x0, x1, on_core_face(90.0, 0.0)[1], WING_LINK[2], APRON_TOP, CONNECTOR_ROOF))
    # Core ((+X, -Y) face) to the garage's rear wall.
    y0, y1 = CONNECTOR_GARAGE_Y
    normal = Vector((math.cos(math.radians(-45.0)), math.sin(math.radians(-45.0))))
    reach = CORE_APOTHEM + Vector(CORE_CENTRE).dot(normal)

    def face_x(y):  # the core's (+X, -Y) face line at this y
        return (reach - normal.y * y) / normal.x
    link = [(face_x(y0), y0), (GARAGE[0], y0), (GARAGE[0], y1), (face_x(y1), y1)]
    # Rear annex off the core's (-X, -Y) face, the second flag on its roof.
    a, b = on_core_face(-135.0, 1.0), on_core_face(-135.0, -1.0)
    a2, b2 = on_core_face(-135.0, 1.0, ANNEX_DEPTH), on_core_face(-135.0, -1.0, ANNEX_DEPTH)
    annex = ccw([a, a2, b2, b])
    if LEVEL >= 2:
        corridor(m, link, CONNECTOR_ROOF, (0, 2))
        against = [k for k in range(4) if {annex[k], annex[(k + 1) % 4]} == {a, b}]  # the edge on the core's face
        block(m, annex, APRON_TOP, CONNECTOR_ROOF, {k: ANNEX_ROWS for k in range(4)}, parapet=ANNEX_PARAPET,
              flush=against)
    else:
        m.add("Glass", plan_prism(link, APRON_TOP, CONNECTOR_ROOF))
        m.add("Concrete", plan_prism(annex, APRON_TOP, CONNECTOR_ROOF))
    return m.finish()


def build_radar_tower():
    m = Module("RadarTower")
    if LEVEL < 2:
        m.add("Concrete", box(*RADAR_TOWER, APRON_TOP, RADAR_TOWER_ROOF))
        return m.finish()
    tower = rect(*RADAR_TOWER)
    block(m, tower, APRON_TOP, RADAR_TOWER_ROOF, {k: TOWER_ROWS for k in range(4)}, coping_above=True)
    frame = Frame(tower[1], tower[2])  # +X face, towards the garage
    w, z0, z1 = TOWER_PLATE
    plaque(m, frame, frame.length / 2.0, z0, z1, w)
    if LEVEL >= 4:
        corner_plates(m, tower, APRON_TOP + 0.6, RADAR_TOWER_ROOF)
    return m.finish()


def build_radar():
    m = Module("Radar")
    x, y, z = RADAR_PIVOT
    if LEVEL < 3:
        m.add("PaintedMetal", post(x, y, z, z + 0.8, 0.45))
        m.add("PaintedMetal", box(x - 0.25, x + 0.25, y - 1.6, y + 1.6, z + 0.8, RADAR_TOP))
        return m.finish(RADAR_PIVOT)
    # Pedestal, turntable, a yoke and the phased-array face leaning back 20 degrees, tiled with its modules.
    m.add("PaintedMetal", post(x, y, z, z + 0.5, 0.45, 16))
    m.add("ArmouredSteel", post(x, y, z + 0.5, z + 0.65, 0.6, 16))
    tilt = math.radians(20.0)
    normal = Vector((math.cos(tilt), 0.0, math.sin(tilt)))
    up = Vector((-math.sin(tilt), 0.0, math.cos(tilt)))
    across = Vector((0.0, 1.0, 0.0))
    centre = Vector((x, y, RADAR_TOP - 0.8 * math.cos(tilt) - 0.12 * math.sin(tilt) - 0.02))
    for side in (-1.0, 1.0):
        m.add("ArmouredSteel", box(x - 0.15, x + 0.15, y + side * 1.0 - 0.08, y + side * 1.0 + 0.08, z + 0.65,
                                   centre.z - 0.3))
    m.add("PaintedMetal", box3(centre, across, up, normal, 1.6, 0.8, 0.12))
    m.add("ArmouredSteel", box3(centre - normal * 0.16, across, up, normal, 1.5, 0.6, 0.04))
    for i in range(8):
        for j in range(3):
            tile = centre + normal * 0.135 + across * (-1.4 + 0.4 * i) + up * (-0.52 + 0.52 * j)
            m.add("ArmouredSteel", box3(tile, across, up, normal, 0.18, 0.24, 0.015))
    return m.finish(RADAR_PIVOT)


def build_wing_dish():
    m = Module("WingDish")
    x, y, z = DISH_PIVOT
    if LEVEL < 3:
        m.add("PaintedMetal", post(x, y, z, z + 1.0, 0.3))
        centre = Vector((x, y, z + 1.0 + DISH_RADIUS))
        m.add("PaintedMetal", cylinder(centre + Vector((0.35, 0.0, 0.0)), (1.0, 0.0, 0.3), DISH_RADIUS, 0.35, 16,
                                       DISH_RADIUS * 0.4))
        return m.finish(DISH_PIVOT)
    # Pedestal, an elevation head, a parabolic reflector looking 30 degrees up towards +X, the feed on three struts.
    m.add("PaintedMetal", post(x, y, z, z + 0.9, 0.3, 12))
    m.add("ArmouredSteel", box(x - 0.35, x + 0.25, y - 0.3, y + 0.3, z + 0.9, z + 1.5))
    axis = Vector((math.cos(math.radians(30.0)), 0.0, math.sin(math.radians(30.0))))
    centre = Vector((x - 0.15, y, z + 1.0 + DISH_RADIUS))
    r = DISH_RADIUS
    m.add("PaintedMetal", lathe(centre, axis, [(0.0, 0.05), (0.03, r * 0.36), (0.11, r * 0.68), (0.26, r), (0.21, r),
                                               (0.06, r * 0.68), (-0.03, r * 0.36), (-0.06, 0.05)], 20))
    m.add("ArmouredSteel", box3(centre - axis * 0.1, Vector((0.0, 1.0, 0.0)), axis.cross(Vector((0.0, 1.0, 0.0))), axis,
                                0.3, 0.3, 0.12))
    focus = centre + axis * 0.95
    _, u, v = axis_frame(axis)
    for k in range(3):
        a = math.radians(90.0 + 120.0 * k)
        rim = centre + axis * 0.24 + (u * math.cos(a) + v * math.sin(a)) * r * 0.97
        m.add("ArmouredSteel", bar(rim, focus, 0.02, 4))
    m.add("ArmouredSteel", cylinder(focus + axis * 0.05, axis, 0.08, 0.25, 8, 0.05))
    return m.finish(DISH_PIVOT)


def build_satcom():
    m = Module("Satcom")
    x, y = SATCOM_CENTRE
    centre_z = RADOME_TOP - RADOME_RADIUS
    half = SATCOM_BASE / 2.0
    deck = centre_z - RADOME_RADIUS
    m.add("Concrete", box(x - half, x + half, y - half, y + half, APRON_TOP, 0.3))
    m.add("BareMetal", *lattice_tower(x, y, 0.3, deck - 0.1, half - 0.15, 0.8, 5))
    m.add("PaintedMetal", box(x - half, x + half, y - half, y + half, deck - 0.1, deck + 0.2))
    m.add("PaintedMetal", sphere((x, y, centre_z), RADOME_RADIUS, 20, 10))
    m.add("BareMetal", post(x + half - 0.1, y + half - 0.1, deck + 0.2, SATCOM_MAST_TIP, 0.05))
    m.add("BareMetal", post(x - half + 0.1, y + half - 0.1, deck + 0.2, SATCOM_MAST_TIP - 1.0, 0.05))
    if LEVEL >= 3:
        beacon(m, x + half - 0.1, y + half - 0.1, SATCOM_MAST_TIP - 0.3)
        beacon(m, x - half + 0.1, y + half - 0.1, SATCOM_MAST_TIP - 1.3)
        # Guard rail round the platform (posts and a top rail), a caged ladder up the +X face.
        for i in range(4):
            a, b = [(x - half + 0.05, y - half + 0.05), (x + half - 0.05, y - half + 0.05),
                    (x + half - 0.05, y + half - 0.05), (x - half + 0.05, y + half - 0.05)][i], \
                   [(x + half - 0.05, y - half + 0.05), (x + half - 0.05, y + half - 0.05),
                    (x - half + 0.05, y + half - 0.05), (x - half + 0.05, y - half + 0.05)][i]
            m.add("BareMetal", bar((a[0], a[1], deck + 1.25), (b[0], b[1], deck + 1.25), 0.025, 4))
            m.add("BareMetal", bar((a[0], a[1], deck + 0.7), (b[0], b[1], deck + 0.7), 0.02, 4))
            m.add("BareMetal", post(a[0], a[1], deck + 0.2, deck + 1.25, 0.025, 4))
        lx = x + half - 0.25
        for ly in (y - 0.25, y + 0.25):
            m.add("BareMetal", post(lx, ly, 0.3, deck - 0.1, 0.025, 4))
        for k in range(int((deck - 0.6) / 0.6)):
            zr = 0.6 + 0.6 * k
            m.add("BareMetal", bar((lx, y - 0.25, zr), (lx, y + 0.25, zr), 0.015, 4))
    return m.finish()


def build_mast():
    m = Module("Mast")
    x, y = MAST_CENTRE
    m.add("Concrete", centred_box(x, y, 1.0, 1.0, APRON_TOP, 0.4))
    m.add("ArmouredSteel" if LEVEL >= 3 else "BareMetal", post(x, y, 0.4, MAST_TIP, 0.12, 8, 0.05))
    if LEVEL >= 3:
        for z in (MAST_TIP - 0.15, 12.2, 8.0):
            beacon(m, x, y, z, 0.1 if z > 9.0 else 0.09)
        for (dx, dy), height in GUYS:
            ax, ay = x + dx, y + dy
            m.add("Concrete", centred_box(ax, ay, 0.4, 0.4, APRON_TOP, 0.25))
            m.add("BareMetal", bar((ax, ay, 0.25), (x, y, height), 0.012, 4))
        m.add("PaintedMetal", centred_box(x + 0.7, y, 0.5, 0.6, APRON_TOP, 1.1))  # equipment cabinet
    return m.finish()


def build_checkpoints():
    m = Module("Checkpoints")
    if LEVEL < 4:
        for x0, x1, y0, y1 in CHECKPOINTS:
            m.add("Concrete", box(x0, x1, y0, y1, APRON_TOP, 0.9))
            m.add("BareMetal", post((x0 + x1) / 2.0, y1 - 0.4, 0.9, CHECKPOINT_LAMP_TIP, 0.08))
        return m.finish()
    if LEVEL >= 5:
        guard_posts(m)
        return m.finish()
    # Each post: jersey barriers either side of a concrete block carrying a lamp mast with two heads, blue beacons on
    # the barrier ends; steel bollards by the courtyard steps and the garage's -Y jamb.
    for x0, x1, y0, y1 in CHECKPOINTS:
        xc, yc = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        jersey(m, xc, y0, yc - 0.5)
        jersey(m, xc, yc + 0.5, y1)
        m.add("Concrete", box(x0 + 0.05, x1 - 0.05, yc - 0.45, yc + 0.45, APRON_TOP, 0.9))
        m.add("BareMetal", post(xc, yc, 0.9, CHECKPOINT_LAMP_TIP - 0.3, 0.08, 8, 0.05))
        m.add("BareMetal", box(xc - 0.04, xc + 0.04, yc - 0.45, yc + 0.45, CHECKPOINT_LAMP_TIP - 0.4,
                               CHECKPOINT_LAMP_TIP - 0.32))
        for side in (-1.0, 1.0):
            m.add("ArmouredSteel", box(xc - 0.15, xc + 0.15, yc + side * 0.4 - 0.12, yc + side * 0.4 + 0.12,
                                       CHECKPOINT_LAMP_TIP - 0.6, CHECKPOINT_LAMP_TIP - 0.4))
            m.add("Lamp", box(xc - 0.12, xc + 0.12, yc + side * 0.4 - 0.1, yc + side * 0.4 + 0.1,
                              CHECKPOINT_LAMP_TIP - 0.62, CHECKPOINT_LAMP_TIP - 0.6))
        beacon(m, xc, yc, CHECKPOINT_LAMP_TIP - 0.15, 0.08)
        for y in (y0 + 0.15, y1 - 0.15):
            m.add("BareMetal", post(xc, y, APRON_TOP + 0.81, APRON_TOP + 1.05, 0.03, 6))
            beacon(m, xc, y, APRON_TOP + 1.1, 0.07)
    for x, y in BOLLARDS:
        m.add("Hazard", post(x, y, APRON_TOP, 1.0, 0.1, 10))
        m.add("ArmouredSteel", post(x, y, 1.0, 1.06, 0.1, 10, 0.06))
    return m.finish()


def guard_posts(m):
    """Level 5 checkpoints: a concrete guard block with vision slits and a parapet cap carrying a lamp mast with two
    heads and a beacon, a jersey barrier run on its inner side; steel bollards."""
    for (x0, x1, y0, y1), (bx, by0, by1) in GUARD_POSTS:
        xc, yc = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        m.add("Concrete", box(x0, x1, y0, y1, APRON_TOP, GUARD_HEIGHT))
        m.add("Concrete", box(x0 - 0.05, x1 + 0.05 if x1 + 0.05 <= HALF else x1, y0 - 0.05, y1 + 0.05, GUARD_HEIGHT,
                              GUARD_HEIGHT + 0.12))
        for y in (y0 + 0.5, y1 - 0.5):
            m.add("Rubber", box(x0 - 0.01, x0, y - 0.3, y + 0.3, 0.8, 0.95))
        m.add("BlueAccent", box(x0 - 0.02, x0, yc - 0.35, yc + 0.35, 0.25, 0.65))
        jersey(m, bx, by0, by1)
        top = GUARD_HEIGHT + 0.12
        m.add("BareMetal", post(xc, yc, top, CHECKPOINT_LAMP_TIP - 0.3, 0.08, 8, 0.05))
        m.add("BareMetal", box(xc - 0.04, xc + 0.04, yc - 0.5, yc + 0.5, CHECKPOINT_LAMP_TIP - 0.4, CHECKPOINT_LAMP_TIP - 0.32))
        for side in (-1.0, 1.0):
            m.add("ArmouredSteel", box(xc - 0.15, xc + 0.15, yc + side * 0.45 - 0.12, yc + side * 0.45 + 0.12,
                                       CHECKPOINT_LAMP_TIP - 0.6, CHECKPOINT_LAMP_TIP - 0.4))
            m.add("Lamp", box(xc - 0.12, xc + 0.12, yc + side * 0.45 - 0.1, yc + side * 0.45 + 0.1,
                              CHECKPOINT_LAMP_TIP - 0.62, CHECKPOINT_LAMP_TIP - 0.6))
            beacon(m, xc, yc + side * (y1 - y0) / 2.0 * 0.8, top + 0.07, 0.07)
        beacon(m, xc, yc, CHECKPOINT_LAMP_TIP - 0.15, 0.08)
    for x, y in BOLLARDS_R5:
        m.add("Hazard", post(x, y, APRON_TOP, 1.0, 0.1, 10))
        m.add("ArmouredSteel", post(x, y, 1.0, 1.06, 0.1, 10, 0.06))


def build_fuel_tanks():
    m = Module("FuelTanks")
    x0, x1 = FUEL_TANK_X
    for y, r in FUEL_TANKS:
        if LEVEL < 4:
            m.add("BareMetal", cylinder(((x0 + x1) / 2.0, y, FUEL_TANK_AXIS_Z), (1.0, 0.0, 0.0), r, x1 - x0, 16))
        else:
            half = (x1 - x0) / 2.0
            m.add("PaintedMetal", lathe(((x0 + x1) / 2.0, y, FUEL_TANK_AXIS_Z), (1.0, 0.0, 0.0),
                                        [(-half, 0.0), (-half + 0.05, r * 0.6), (-half + 0.15, r * 0.92), (-half + 0.3, r),
                                         (half - 0.3, r), (half - 0.15, r * 0.92), (half - 0.05, r * 0.6), (half, 0.0)], 16))
            for x in (x0 + 0.4, x1 - 0.4):
                m.add("ArmouredSteel", cylinder((x, y, FUEL_TANK_AXIS_Z), (1.0, 0.0, 0.0), r + 0.012, 0.06, 16))
            m.add("BareMetal", post(x0 + 0.7, y, FUEL_TANK_AXIS_Z + r - 0.05, FUEL_TANK_AXIS_Z + r + 0.12, 0.09, 8))
        for x in (x0 + 0.4, x1 - 0.4):
            m.add("Concrete", centred_box(x, y, 0.3, 2.0 * r, APRON_TOP, FUEL_TANK_AXIS_Z - r + 0.05))
    if LEVEL >= 4:
        # Spill bund round the tanks, a grated walkway over them with a hand rail.
        bx0, bx1, by0, by1, height, wall = BUND
        outline = rect(bx0, bx1, by0, by1)
        m.add("Concrete", annulus(outline, inset_outline(outline, wall), APRON_TOP, APRON_TOP + height))
        top = FUEL_TANK_AXIS_Z + FUEL_TANKS[0][1]
        ya, yb = FUEL_TANKS[0][0] - 0.2, FUEL_TANKS[-1][0] + 0.2
        m.add("ArmouredSteel", box(12.2, 13.2, ya, yb, top - 0.02, top + 0.03))
        for x in (12.25, 13.15):
            m.add("BareMetal", post(x, yb - 0.03, top + 0.03, top + 1.05, 0.025, 4))
        m.add("BareMetal", bar((12.25, yb - 0.03, top + 1.05), (13.15, yb - 0.03, top + 1.05), 0.025, 4))
    return m.finish()


def build_supply():
    m = Module("Supply")
    if LEVEL >= 5:
        for index, (kind, x0, x1, y0, y1, h) in enumerate(SUPPLY_CLUSTERS):
            if kind == "crates":
                crates(m, x0, x1, y0, y1, h, index)
            elif kind == "drums":
                drums(m, x0, x1, y0, y1)
            elif kind == "pallets":
                pallets(m, x0, x1, y0, y1, h, index)
            else:
                container(m, x0, x1, y0, y1, h)
        return m.finish()
    for index, (x0, x1, y0, y1, h) in enumerate(SUPPLY):
        if LEVEL < 4:
            m.add("Olive", box(x0, x1, y0, y1, APRON_TOP, h))
        elif x0 <= CONTAINER[0] < x1 and y1 < 0.0:
            container(m, CONTAINER[0], CONTAINER[1], y0, y1, h)
            crates(m, CONTAINER[1] + 0.2, x1, y0, y1, h, index)
        else:
            crates(m, x0, x1, y0, y1, h, index)
    return m.finish()


def build_loader():
    m = Module("Loader")
    x0, x1, y0, y1, h = LOADER
    if LEVEL < 4:
        m.add("Olive", box(x0, x1, y0, y1, APRON_TOP + 0.4, 1.5))
        m.add("Olive", box(x0 + 0.6, x0 + 1.8, y0 + 0.2, y1 - 0.2, 1.5, h))
        for x in (x0 + 0.5, x1 - 0.5):
            m.add("Rubber", cylinder((x, 0.0, 0.45), (0.0, 1.0, 0.0), 0.4, y1 - y0 - 0.02, 12))
        return m.finish()
    # A compact wheel loader parked bucket-down: four tyres with rims, chassis, engine hood with a grille, a cab with
    # glass in its ROPS frame, loader arms and the bucket at +X.
    yc = (y0 + y1) / 2.0
    tyre = 0.42
    for x in (x0 + 0.55, x0 + 2.0):
        for side in (-1.0, 1.0):
            y = yc + side * (y1 - y0 - 0.32) / 2.0
            m.add("Rubber", cylinder((x, y, APRON_TOP + tyre), (0.0, 1.0, 0.0), tyre, 0.3, 14))
            m.add("Olive", cylinder((x, y + side * 0.01, APRON_TOP + tyre), (0.0, 1.0, 0.0), 0.24, 0.3, 10))
    m.add("Olive", box(x0 + 0.2, x1 - 0.35, yc - 0.45, yc + 0.45, 0.35, 0.9))
    m.add("Olive", box(x0, x0 + 1.0, yc - 0.6, yc + 0.6, 0.9, 1.5))
    m.add("Rubber", box(x0, x0 + 0.02, yc - 0.45, yc + 0.45, 1.0, 1.4))
    m.add("BareMetal", post(x0 + 0.3, yc - 0.35, 1.5, 1.9, 0.05, 6))
    cab0, cab1 = x0 + 1.0, x0 + 2.0
    for x in (cab0, cab1 - 0.08):
        for y in (yc - 0.55, yc + 0.47):
            m.add("Olive", box(x, x + 0.08, y, y + 0.08, 0.9, h - 0.1))
    m.add("Olive", box(cab0 - 0.05, cab1 + 0.05, yc - 0.6, yc + 0.6, h - 0.1, h))
    m.add("Glass", box(cab0 + 0.08, cab1 - 0.08, yc - 0.5, yc + 0.5, 1.3, h - 0.15))
    m.add("Olive", box(cab0, cab1, yc - 0.55, yc + 0.55, 0.9, 1.3))
    for side in (-1.0, 1.0):
        y = yc + side * 0.5
        m.add("Olive", bar((cab1 - 0.1, y, 1.55), (x1 - 0.3, y, 0.55), 0.08, 6))
    m.add("ArmouredSteel", box(x1 - 0.35, x1, y0 + 0.02, y1 - 0.02, APRON_TOP + 0.01, APRON_TOP + 0.06))
    m.add("Olive", box(x1 - 0.35, x1 - 0.28, y0 + 0.02, y1 - 0.02, APRON_TOP + 0.06, 0.8),
          box(x1 - 0.35, x1, y0 + 0.02, y0 + 0.08, APRON_TOP + 0.06, 0.6),
          box(x1 - 0.35, x1, y1 - 0.08, y1 - 0.02, APRON_TOP + 0.06, 0.6))
    return m.finish()


def build_light_masts():
    m = Module("LightMasts")
    for x, y, tip in LIGHT_MASTS:
        m.add("Concrete", centred_box(x, y, 0.8, 0.8, APRON_TOP, 0.4))
        if LEVEL < 3:
            m.add("BareMetal", post(x, y, 0.4, tip - 0.4, 0.1, 8, 0.06))
            m.add("Lamp", centred_box(x, y, 0.6, 0.6, tip - 0.4, tip))
            continue
        # Tapered pole on a base plate, a crossarm square to the slab's centre with two floodlight heads aimed down.
        m.add("ArmouredSteel", centred_box(x, y, 0.5, 0.5, 0.4, 0.45))
        m.add("BareMetal", post(x, y, 0.45, tip - 0.3, 0.11, 10, 0.06))
        angle = math.degrees(math.atan2(-y, -x)) + 90.0
        m.add("BareMetal", obox(x, y, angle, FLOOD_ARM, 0.08, tip - 0.75, tip - 0.65))
        for side in (-1.0, 1.0):
            m.add("ArmouredSteel", obox(x, y, angle, 0.42, 0.34, tip - 1.0, tip - 0.75, u=side * FLOOD_ARM * 0.4))
            m.add("Lamp", obox(x, y, angle, 0.36, 0.28, tip - 1.02, tip - 1.0, u=side * FLOOD_ARM * 0.4))
        beacon(m, x, y, tip - 0.2, 0.08)
    return m.finish()


def build_clearance(collection):
    """The builder's clearance volume along the exit path, and the full 7 m exit lane: non-rendering, for --verify."""
    length, width, height = BUILDER
    x0 = SPAWN[0] - length / 2.0 - CLEAR_REAR
    clearance = Module("Clearance", collection)
    clearance.add("Hazard", box(x0, HALF, DOOR_CENTRE_Y - width / 2.0 - CLEAR_SIDE,
                                DOOR_CENTRE_Y + width / 2.0 + CLEAR_SIDE, APRON_TOP + CLEAR_FLOOR,
                                APRON_TOP + CLEAR_FLOOR + height + CLEAR_TOP))
    lane = Module("ExitLane", collection)
    lane.add("Hazard", box(DOOR_PLANE - BAY_DEPTH, HALF, DOOR_CENTRE_Y - LANE_WIDTH / 2.0,
                           DOOR_CENTRE_Y + LANE_WIDTH / 2.0, APRON_TOP + CLEAR_FLOOR, DOOR_TOP))
    objects = [clearance.finish(), lane.finish()]
    for obj in objects:
        obj.hide_render = True
        obj.display_type = "WIRE"
    return objects


# ---- assembly ----

def make_materials():
    for name, (colour, roughness, metallic, emission) in MATERIALS.items():
        if name == "Concrete" and LEVEL >= 3:
            colour = tuple(c * CONCRETE_TONE for c in colour)
        material = bpy.data.materials.new(name)
        material.use_nodes = True
        shader = material.node_tree.nodes["Principled BSDF"]
        shader.inputs["Base Color"].default_value = (*colour, 1.0)
        shader.inputs["Roughness"].default_value = roughness
        shader.inputs["Metallic"].default_value = metallic
        if emission:
            shader.inputs["Emission Color"].default_value = (*colour, 1.0)
            shader.inputs["Emission Strength"].default_value = emission
        if name == "Envelope":  # the builder's envelope in the scale proof: see-through orange
            shader.inputs["Alpha"].default_value = 0.35
        if name == "Glass":
            shader.inputs["Specular IOR Level"].default_value = 0.8
            if LEVEL >= 2:  # warm interior behind the glazing (spec section 7)
                shader.inputs["Emission Color"].default_value = (1.0, 0.72, 0.42, 1.0)
                shader.inputs["Emission Strength"].default_value = 0.35
        if LEVEL >= 2 and name in WEATHERED:
            weather(material, colour, *WEATHERED[name])


def weather(material, colour, scale, variation, bump, grime):
    """Mottled colour, a fine bump and, with grime > 0, a darker band up to that height off the ground."""
    nodes, links = material.node_tree.nodes, material.node_tree.links
    shader = nodes["Principled BSDF"]
    coords = nodes.new("ShaderNodeTexCoord")
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = 8.0
    links.new(coords.outputs["Object"], noise.inputs["Vector"])
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (*[c * (1.0 - variation) for c in colour], 1.0)
    ramp.color_ramp.elements[1].color = (*[min(1.0, c * (1.0 + variation)) for c in colour], 1.0)
    links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    result = ramp.outputs["Color"]
    if grime:
        geometry = nodes.new("ShaderNodeNewGeometry")
        split = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geometry.outputs["Position"], split.inputs[0])
        height = nodes.new("ShaderNodeMapRange")
        height.inputs["From Min"].default_value = 0.0
        height.inputs["From Max"].default_value = grime
        height.inputs["To Min"].default_value = 0.62
        height.inputs["To Max"].default_value = 1.0
        links.new(split.outputs["Z"], height.inputs["Value"])
        darken = nodes.new("ShaderNodeVectorMath")
        darken.operation = "MULTIPLY"
        links.new(result, darken.inputs[0])
        links.new(height.outputs["Result"], darken.inputs[1])
        result = darken.outputs["Vector"]
    links.new(result, shader.inputs["Base Color"])
    fine = nodes.new("ShaderNodeTexNoise")
    fine.inputs["Scale"].default_value = scale * 40.0
    links.new(coords.outputs["Object"], fine.inputs["Vector"])
    bumper = nodes.new("ShaderNodeBump")
    bumper.inputs["Strength"].default_value = bump
    bumper.inputs["Distance"].default_value = 0.01
    links.new(fine.outputs["Fac"], bumper.inputs["Height"])
    links.new(bumper.outputs["Normal"], shader.inputs["Normal"])


def flag_core_base():
    """The second flag stands on the annex roof: on its membrane deck from level 2."""
    return CONNECTOR_ROOF if LEVEL < 2 else CONNECTOR_ROOF - ANNEX_PARAPET + ROOF_MEMBRANE


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    bpy.context.preferences.filepaths.save_version = 0  # no .blend1 next to the source
    make_materials()
    modules = [build_apron(), build_core(), build_wing(), build_garage(), build_courtyard(),
               build_flag("FlagCourtyard", *FLAG_COURTYARD, COURTYARD_TOP + PLINTH[2], FLAG_COURTYARD_TIP),
               build_flag("FlagCore", *FLAG_CORE, flag_core_base(), FLAG_CORE_TIP), build_connectors(),
               build_radar_tower(), build_satcom(), build_mast(), build_checkpoints(), build_fuel_tanks(),
               build_supply(), build_loader(), build_light_masts()]
    parts = {"GarageDoor": build_garage_door(), "Radar": build_radar(), "WingDish": build_wing_dish()}
    helpers = bpy.data.collections.new("USA_HQ_Helpers")
    scene.collection.children.link(helpers)
    helpers.hide_render = True
    for key, pivot in (("GarageDoor", DOOR_PIVOT), ("Radar", RADAR_PIVOT), ("WingDish", DISH_PIVOT)):
        empty(f"Pivot_{key}", pivot, collection=helpers)
    for name, location in (("Spawn_Garage", SPAWN), ("Exit_Door", (DOOR_PLANE, DOOR_CENTRE_Y, 0.0)),
                           ("Flag_Courtyard", (*FLAG_COURTYARD, FLAG_COURTYARD_TIP)),
                           ("Flag_Core", (*FLAG_CORE, FLAG_CORE_TIP)), ("Mast_Core_Tip", (*CORE_CENTRE, CORE_MAST_TIP))):
        empty(f"Socket_{name}", location, collection=helpers)
    clearance = build_clearance(helpers)
    bpy.context.view_layer.update()
    return modules, parts, clearance


# ---- verification ----

def world_bm(obj, shrink=0.0):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    evaluated.to_mesh_clear()
    bm.transform(obj.matrix_world)
    if shrink:
        bm.normal_update()
        for vert in bm.verts:
            vert.co -= vert.normal * shrink
    return bm


def tree(obj, shrink=0.0, centres=None):
    """The object's BVH in world space, built from its triangles so overlap indices are triangle indices; centres, if
    a list, gets each triangle's centre in Unreal's frame."""
    bm = world_bm(obj, shrink)
    bm.verts.index_update()
    triangles = [[loop.vert.index for loop in tri] for tri in bm.calc_loop_triangles()]
    points = [v.co.copy() for v in bm.verts]
    bm.free()
    if centres is not None:
        centres.extend(tuple(round(c, 2) for c in blender(sum((points[i] for i in tri), Vector()) / 3.0))
                       for tri in triangles)
    return BVHTree.FromPolygons(points, triangles)


def bounds(objects):
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (Vector([min(p[i] for p in points) for i in range(3)]), Vector([max(p[i] for p in points) for i in range(3)]))


def key_of(obj):
    return obj.name[len(PREFIX):]


def verify(modules, parts, clearance):
    """Checks the model against the spec; returns (lines, failures)."""
    lines, failures = [], []

    def check(ok, text):
        lines.append(("PASS " if ok else "FAIL ") + text)
        if not ok:
            failures.append(text)

    solids = modules + list(parts.values())
    low, high = bounds(solids)
    size = high - low
    check(abs(size.x - FOOTPRINT) <= FOOTPRINT_TOLERANCE and abs(size.y - FOOTPRINT) <= FOOTPRINT_TOLERANCE
          and abs(low.x + HALF) <= FOOTPRINT_TOLERANCE and abs(low.y + HALF) <= FOOTPRINT_TOLERANCE,
          f"footprint {size.x * 100:.1f} x {size.y * 100:.1f} cm, centred (spec 2800 x 2800)")
    check(abs(low.z) <= HEIGHT_TOLERANCE and abs(high.z - TOTAL_HEIGHT) <= HEIGHT_TOLERANCE,
          f"height {low.z * 100:.1f} .. {high.z * 100:.1f} cm (spec 0 .. 2400, to the mast tip)")
    core = bpy.data.objects[PREFIX + "Core"]
    check(abs(bounds([core])[1].z - CORE_MAST_TIP) <= HEIGHT_TOLERANCE,
          "the 24 m core mast is in the static root (Core), not a part")

    # Garage door and bay, measured on the garage's own geometry by rays from the bay.
    garage = tree(bpy.data.objects[PREFIX + "Garage"])

    def hit(origin, direction):
        location, _, _, distance = garage.ray_cast(blender(origin), blender(direction))
        return distance if location is not None else float("inf")
    probe_x = DOOR_PLANE - 0.05
    left = hit((probe_x, DOOR_CENTRE_Y, 2.0), (0.0, -1.0, 0.0))
    right = hit((probe_x, DOOR_CENTRE_Y, 2.0), (0.0, 1.0, 0.0))
    door_y0, door_y1 = DOOR_CENTRE_Y - left, DOOR_CENTRE_Y + right
    door_centre = (door_y0 + door_y1) / 2.0
    check(abs(door_centre - DOOR_CENTRE_Y) <= 0.005 and abs(door_centre + 6.0) <= 0.5,
          f"door centre at Y {door_centre * 100:.1f} cm (spec -600 +/- 50): lateral exit offset confirmed")
    check(door_y1 - door_y0 >= DOOR_WIDTH - 0.001, f"door clear width {(door_y1 - door_y0) * 100:.1f} cm (spec 700)")
    door_top = 2.0 + hit((probe_x, DOOR_CENTRE_Y, 2.0), (0.0, 0.0, 1.0)) - APRON_TOP
    check(door_top >= DOOR_HEIGHT - 0.001, f"door clear height {door_top * 100:.1f} cm over the slab (spec 450)")
    back = SPAWN[0] - hit((SPAWN[0], SPAWN[1], 1.0), (-1.0, 0.0, 0.0))
    check(abs(DOOR_PLANE - back - BAY_DEPTH) <= 0.001, f"bay depth {(DOOR_PLANE - back) * 100:.1f} cm (spec 850)")
    ceiling = 1.0 + hit((SPAWN[0], SPAWN[1], 1.0), (0.0, 0.0, 1.0)) - APRON_TOP
    check(ceiling >= BAY_HEIGHT - 0.001, f"bay clear height {ceiling * 100:.1f} cm (spec 500)")
    for side in (-1.0, 1.0):
        wall = hit((SPAWN[0], SPAWN[1], 1.0), (0.0, side, 0.0))
        check(wall >= DOOR_WIDTH / 2.0 - 0.001, f"bay wall {wall * 100:.1f} cm from the spawn on {'+' if side > 0 else '-'}Y")

    # Clearance volume: no contact at all with the garage or any other module along the exit path. The door leaf is
    # excluded: it opens (rolls into the drum over the opening) before the builder spawns (spec section 5).
    clear_obj, lane_obj = clearance
    clear_tree = tree(clear_obj)
    hits = [key_of(o) for o in solids if key_of(o) != "GarageDoor" and clear_tree.overlap(tree(o))]
    length = HALF - (SPAWN[0] - BUILDER[0] / 2.0 - CLEAR_REAR)
    check(not hits, f"builder clearance volume ({length:.2f} m long, {BUILDER[1] + 2 * CLEAR_SIDE:.1f} m wide, "
                    f"{BUILDER[2] + CLEAR_TOP:.1f} m high, spawn to footprint edge) touches nothing"
                    f"{': ' + ', '.join(hits) if hits else ''}")
    lane_tree = tree(lane_obj, TOUCH_TOLERANCE)
    hits = [key_of(o) for o in solids if key_of(o) not in ("GarageDoor", ON_APRON)
            and lane_tree.overlap(tree(o, TOUCH_TOLERANCE))]
    check(not hits, f"7 m exit lane free from the bay's back wall to the footprint edge"
                    f"{': ' + ', '.join(hits) if hits else ''}")

    # Module overlap: any intersection between two modules fails; listed neighbours may share faces.
    centres = {key_of(o): [] for o in solids}
    trees = {key_of(o): (tree(o, 0.0, centres[key_of(o)]), tree(o, TOUCH_TOLERANCE)) for o in solids}
    keys = list(trees)
    overlaps = []
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            touching = ON_APRON in (a, b) or (a, b) in ADJACENT or (b, a) in ADJACENT
            ta, tb = (trees[a][1], trees[b][1]) if touching else (trees[a][0], trees[b][0])
            pairs = ta.overlap(tb)
            if pairs:
                overlaps.append(f"{a}/{b} near {centres[a][pairs[0][0]]}")
    counts = triangle_counts(solids)
    root = sum(n for k, n in counts.items() if k not in parts)
    moving = sum(counts[k] for k in parts)
    check(root <= TRIANGLE_BUDGET[0] and moving <= TRIANGLE_BUDGET[1],
          f"triangles: root {root:,} (budget {TRIANGLE_BUDGET[0]:,}), parts {moving:,} (budget {TRIANGLE_BUDGET[1]:,})")
    check(not overlaps, f"no overlapping modules ({len(keys)} objects, {len(keys) * (len(keys) - 1) // 2} pairs)"
                        f"{': ' + ', '.join(overlaps) if overlaps else ''}")
    return lines, failures


def triangle_counts(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    counts = {}
    for obj in objects:
        mesh = obj.evaluated_get(depsgraph).to_mesh()
        mesh.calc_loop_triangles()
        counts[key_of(obj)] = len(mesh.loop_triangles)
        obj.evaluated_get(depsgraph).to_mesh_clear()
    return counts


# ---- export, renders ----

def export_fbx(objects):
    os.makedirs(FBX_DIR, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(filepath=FBX, use_selection=True, object_types={"MESH"}, use_mesh_modifiers=True,
                             mesh_smooth_type="FACE", use_tspace=False, apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                             bake_space_transform=False, add_leaf_bones=False, bake_anim=False,
                             use_custom_props=False, path_mode="AUTO")


def camera_tuning():
    """The RTS camera from Config/DefaultGame.ini (URTSCameraSettings), lengths in metres."""
    values = {"FieldOfView": 60.0, "DefaultZoom": 15000.0, "MinZoom": 600.0, "MaxZoom": 45000.0,
              "TopDownPitch": -55.0, "NearPitch": -8.0, "TiltStartZoom": 6000.0}
    if os.path.exists(GAME_INI):
        text = open(GAME_INI).read()
        section = text.split("[/Script/WorldDivide.RTSCameraSettings]", 1)[-1].split("\n[", 1)[0]
        for key in values:
            match = re.search(rf"^{key}=([-0-9.]+)", section, re.M)
            if match:
                values[key] = float(match.group(1))
    for key in ("DefaultZoom", "MinZoom", "MaxZoom", "TiltStartZoom"):
        values[key] /= 100.0
    return values


def rts_pitch(tuning, arm):
    """ARTSCameraPawn::GetPitchForZoom: along the arm's logarithm, smoothstepped, degrees below the horizon."""
    near, top, low, tilt = -tuning["NearPitch"], -tuning["TopDownPitch"], tuning["MinZoom"], tuning["TiltStartZoom"]
    alpha = max(0.0, min(1.0, math.log(max(arm, low) / low) / math.log(tilt / low)))
    return near + (top - near) * alpha * alpha * (3.0 - 2.0 * alpha)


# The view's yaw (Blender, the direction the camera looks): from past the slab's (+X, +Y) corner towards the rear, square
# on to the core's entrance face as the concept draws it.
VIEW_YAW = 135.0
CONCEPT_VIEW = {"pitch": 45.0, "distance": 100.0, "fov": 30.0, "target": (0.0, 0.0, 1.5)}


def stage():
    """The M20 presentation scene at building scale."""
    camera = presentation.setup(SAMPLES)
    # Building scale: the tank's fill light pushed out ten times, the ground turned to the concept's dry soil.
    fill = bpy.data.objects["Fill"]
    fill.location = Vector(fill.location) * 10.0
    fill.data.energy *= 100.0
    fill.data.size *= 10.0
    ramp = next(n for n in bpy.data.materials["Ground"].node_tree.nodes if n.type == "VALTORGB")
    ramp.color_ramp.elements[0].color = (0.30, 0.20, 0.12, 1.0)
    ramp.color_ramp.elements[1].color = (0.42, 0.30, 0.19, 1.0)
    return camera


def render(prefix):
    os.makedirs(RENDER_DIR, exist_ok=True)
    tuning = camera_tuning()
    camera = stage()
    fov = math.radians(tuning["FieldOfView"])
    target = (0.0, 0.0, 2.0)

    def path(name):
        return os.path.join(RENDER_DIR, f"{prefix}{name}.png")
    for name, arm in (("rts_default", tuning["DefaultZoom"]), ("rts_mid", tuning["TiltStartZoom"]),
                      ("rts_close", 30.0)):
        pitch = rts_pitch(tuning, arm)
        location, aim = presentation.rts_camera(arm, pitch, VIEW_YAW, target)
        print(f"render {name}: arm {arm:.0f} m, pitch {pitch:.1f} deg")
        presentation.shoot(camera, path(name), location, aim, size=(1920, 1080), fov=fov)
    view = CONCEPT_VIEW
    location, aim = presentation.rts_camera(view["distance"], view["pitch"], VIEW_YAW, view["target"])
    presentation.shoot(camera, path("concept_view"), location, aim, size=(1402, 1122), fov=math.radians(view["fov"]))
    presentation.teardown()
    if os.path.exists(CONCEPT):
        presentation.side_by_side(path("concept_view"), CONCEPT, path("concept_compare"))


def scale_proof(prefix, parts):
    """The garage door rolled up, the 6.5 x 3.0 x 3.2 m design envelope standing at Spawn_Garage and the M20 Guardian
    (linked from its .blend) on the exit lane past the slab. Not saved into the .blend."""
    parts["GarageDoor"].hide_render = True
    length, width, height = BUILDER
    envelope = Module("BuilderEnvelope")
    envelope.add("Envelope", centred_box(SPAWN[0], SPAWN[1], length, width, APRON_TOP, APRON_TOP + height))
    envelope = envelope.finish()
    edges = envelope.copy()
    edges.data = envelope.data.copy()
    bpy.context.scene.collection.objects.link(edges)
    wire = edges.modifiers.new("Wire", "WIREFRAME")
    wire.thickness = 0.04
    added = [envelope, edges]
    if os.path.exists(M20_BLEND):
        with bpy.data.libraries.load(M20_BLEND, link=True) as (source, target):
            target.objects = [n for n in source.objects if n.startswith("M20_") and not n.endswith("_High")]
        tank = bpy.data.collections.new("M20_Proof")
        for obj in target.objects:
            tank.objects.link(obj)
        instance = bpy.data.objects.new("M20_Proof", None)
        instance.instance_type = "COLLECTION"
        instance.instance_collection = tank
        instance.location = blender(M20_SPOT)
        bpy.context.scene.collection.objects.link(instance)
        added.append(instance)
    camera = stage()

    def path(name):
        return os.path.join(RENDER_DIR, f"{prefix}{name}.png")
    presentation.shoot(camera, path("proof_garage"), blender((31.0, 6.0, 10.0)), blender((9.0, -6.0, 1.6)),
                       size=(1920, 1080), fov=math.radians(42.0))
    view = CONCEPT_VIEW
    location, aim = presentation.rts_camera(view["distance"] * 1.15, view["pitch"], VIEW_YAW, (4.0, -2.0, 1.5))
    presentation.shoot(camera, path("proof_scale"), location, aim, size=(1920, 1080), fov=math.radians(view["fov"]))
    presentation.teardown()
    for obj in added:
        bpy.data.objects.remove(obj)
    parts["GarageDoor"].hide_render = False


def main():
    modules, parts, clearance = build()
    lines, failures = verify(modules, parts, clearance)
    counts = triangle_counts(modules + list(parts.values()))
    print("\n".join(lines))
    root = sum(n for k, n in counts.items() if k not in parts)
    print(f"triangles: root {root:,}, parts {sum(counts[k] for k in parts):,}; " +
          ", ".join(f"{k} {n:,}" for k, n in counts.items()))
    print("verify:", f"FAILED ({len(failures)})" if failures else "all checks passed")
    if VERIFY_ONLY:
        sys.stdout.flush()
        if failures:
            sys.exit(1)
        return
    if failures:
        sys.exit(1)
    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)
    export_fbx(modules + list(parts.values()))
    if RENDER:
        prefix = "blockout_" if LEVEL == 1 else f"round{LEVEL}_"
        render(prefix)
        bpy.ops.wm.save_as_mainfile(filepath=BLEND)
        if LEVEL >= 5:
            scale_proof(prefix, parts)


main()
