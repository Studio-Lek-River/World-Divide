"""The greybox of WD_RiverCrossing_01 (#154): the layout of docs/world/maps/WD_RiverCrossing_01/02_MAP_DESIGN.md as a
landscape, a Water plugin river, the concrete bridges of Tools/Art/bridge_blender.py (#155), landscape spline roads drawn
into the landscape's runtime virtual texture (#383) and the village of the in-house civilian buildings with its props
(#157).
setup_rivercrossing.py builds it only when the level does not exist yet, or anew with ue.ps1 -Task rivercrossing
-Rebuild: from then on the level is authored by hand (03_TECHNICAL_PLAN.md section 2) and this file is never run on it
again. The river carves its bed through the landscape's Water edit layer on the GPU, so the build needs a renderer
(-Render).

Coordinates are in metres with the origin at the map centre, +X east and +Y north. Every height and every actor is
point-symmetric about the centre (180 degrees): what stands at (x, y) for the west side stands at (-x, -y) for the east,
so routes, cover and supply are equal for both sides by construction.
"""

import json
import math
import os
import struct

import unreal

MAP_PATH = "/Game/Maps/WD_RiverCrossing_01"
# Landscape collision reports the strongest paint layer's physical material (LANDSCAPE_LAYERS, #156), else the
# landscape's default (#337, docs/engine/world-landscape.md).
GROUND_PHYSICAL_MATERIAL = "/Game/Materials/Physical/PM_Dirt"
CRATE_MESH = "/Game/Art/Placeholder/Meshes/SM_SupplyCrate"
BUILDER = "/Game/Data/Units/DA_TestBuilder"

# Landscape (03 section 4): 16 x 16 components of 2 x 2 sections of 63 quads, 2017 vertices a side, 1.25 m a quad, so
# 2,520 m across: the 2,000 m playable area and a 260 m border ring on every side.
COMPONENTS = 16
QUADS_PER_SECTION = 63
VERTICES = COMPONENTS * 2 * QUADS_PER_SECTION + 1
QUAD = 1.25
LANDSCAPE_HALF = (VERTICES - 1) * QUAD / 2.0
# Z scale 100: one height step is 100 / 128 cm, 32768 is the actor's Z.
HEIGHT_STEPS_PER_METRE = 128.0
PLAYABLE_HALF = 1000.0
BORDER_HEIGHT = 60.0

# Bases (02 "Bases"): the west start spot and its 350 m flat pad, the east one mirrored.
WEST_BASE = (-720.0, -220.0)
PAD_HALF = 175.0
PAD_FALLOFF = 80.0
# The west hill (15 to 25 m high, slopes under 20 degrees) overlooks the west side's home-bridge approach.
WEST_HILL = (-350.0, -650.0)
HILL_HEIGHT = 20.0
HILL_RADIUS = 140.0
# The village rise: 4 m over an ellipse straddling the river between the bridges.
VILLAGE_RISE = 4.0
VILLAGE_RADII = (220.0, 320.0)
# 2 to 8 m of rolling ground over the fields, flattened on the pads, by the bridges and along the river; raised by
# ROLLING_LIFT so no dip reaches the water level and shows as a pond.
ROLLING = ((2.0, 170.0, 210.0, "cos"), (1.5, 95.0, 130.0, "sin"))
ROLLING_LIFT = 1.5

# River (02 "River"): an S-curve x = RIVER_SWING * sin(pi * y / RIVER_HALF_PERIOD), which crosses x = 0 at the bridges
# (y = +-300) and maps onto itself under the rotation; 45 m wide at the bridges, 70 m at the bends.
RIVER_SWING = 60.0
RIVER_HALF_PERIOD = 300.0
RIVER_NARROW = 45.0
RIVER_WIDE = 70.0
RIVER_FLAT = 20.0
RIVER_FLAT_FALLOFF = 60.0
# The Water plugin river (03 section 7): its surface at WATER_LEVEL, a point every RIVER_POINT_SPACING metres edge to
# edge, RIVER_SHALLOW deep at the bridges and RIVER_DEEP at the bends. Its brush carves the bed into the landscape and
# raises the banks from RIVER_SHORE over the water at RIVER_BANK_ANGLE degrees, far steeper than the nav agents' 44.
WATER_LEVEL = -3.0
RIVER_POINT_SPACING = 40.0
RIVER_SHALLOW = 2.0
RIVER_DEEP = 3.0
RIVER_BANK_ANGLE = 60.0
RIVER_SHORE = 0.16
# The engine's river material and the instance that gives it River Crossing's blue river water. Absorption reads as a
# clarity distance per channel: blue carries furthest; low values turn the water near black.
RIVER_MATERIAL_PARENT = "/Water/Materials/WaterSurface/Water_Material_River"
RIVER_MATERIAL_DIR = "/Game/Materials/Water"
RIVER_MATERIAL = "MI_RiverCrossing_River"
RIVER_MATERIAL_VECTORS = {
    "Absorption": (20.0, 30.0, 60.0, 8.0),
    "Scattering": (0.3, 0.4, 0.45, 0.5),
    "Water Albedo": (0.3, 0.36, 0.4, 0.5),
}
# Water_ nav blockers: NavArea_Null boxes along the river, this long each, wider than the river by the margin, from the
# bed to below the bridge decks.
WATER_SEGMENT = 40.0
WATER_MARGIN = 4.0
WATER_BOTTOM = -8.0
WATER_TOP = -1.0

# Bridges (02 "Bridges"): the north one at (0, 300), the south one mirrored, along x across the river: the spans, piers
# and abutments of Tools/Art/bridge_blender.py (Art/Shared/bridge/bridge.json), BRIDGE_SPANS spans of BRIDGE_SPAN, 14 m
# wide with a 13 m carriageway, its deck's top BRIDGE_DECK_TOP over the flattened approaches, 6 m over the water. At each
# end an earthen ramp: flat at the deck's top for BRIDGE_RAMP_TOP past the abutment's approach slab (BRIDGE_ABUTMENT),
# then down to the approach over BRIDGE_RAMP (5.7 degrees, under the 8 of 02), BRIDGE_RAMP_HALF_WIDTH to each side of the
# road and falling to the ground over BRIDGE_RAMP_SIDE beyond, steeper than any agent climbs. Both keep the navmesh
# linked across its tile seam at 64 m (TileSizeUU, Config/DefaultEngine.ini), which the ramp would otherwise cross: the
# flat top runs on past it, and the steep sides give the road its own polygons there. With walkable sides the far tile
# drew the ramp and the fields round it as one polygon at field height, which Detour does not link to the road's at the
# deck's height, so the hull clearance (RTSHullClearance) read a wall across the road and every tank stopped there.
BRIDGE_Y = 300.0
BRIDGE_SPANS = 3
BRIDGE_SPAN = 30.0
BRIDGE_LENGTH = BRIDGE_SPANS * BRIDGE_SPAN
BRIDGE_DECK_TOP = 3.0
BRIDGE_ABUTMENT = 1.5
BRIDGE_RAMP_TOP = 19.5
BRIDGE_RAMP = 30.0
BRIDGE_RAMP_HALF_WIDTH = 9.0
BRIDGE_RAMP_SIDE = 2.0
BRIDGE_FLAT = (150.0, 30.0)
BRIDGE_FLAT_FALLOFF = 60.0
BRIDGE_SOURCE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Art", "Shared", "bridge")
BRIDGE_DIR = "/Game/Art/Shared/Bridge"
BRIDGE_MESHES = ("SM_Bridge_Span", "SM_Bridge_Pier", "SM_Bridge_Abutment")
# The bridge's master material: a colour and a roughness, varied over a few metres of world space so a 30 m span does not
# read as one flat grey; road dust on both (PM_Road). The two slots' instances: concrete and the asphalt carriageway.
BRIDGE_PHYSICAL_MATERIAL = "/Game/Materials/Physical/PM_Road"
BRIDGE_MASTER = "M_Bridge"
BRIDGE_VARIATION_SCALE = 450.0
BRIDGE_VARIATION = (0.82, 1.08)
BRIDGE_SLOTS = {
    "Concrete": ("MI_Bridge_Concrete", (0.24, 0.235, 0.22), 0.85),
    "Asphalt": ("MI_Bridge_Asphalt", (0.045, 0.045, 0.048), 0.75),
}

# Supply sources (02 "Bases" and "Central village"), west side: safe behind the base, forward on the far flank, and the
# village stockpile; each with its amount. The east side has the same at (-x, -y). The safe source lies 90 m behind the
# start spot, within the AI's depot reach (DepotSourceDistance in RTSAIPlayer.cpp), and holds enough to carry a side
# through a match on its own: the skirmish AI does not yet expand to the forward and village sources.
WEST_SOURCES = (
    ("Safe", (-806.0, -246.0), 15000),
    ("Forward", (-400.0, 500.0), 8000),
    ("Village", (-90.0, 40.0), 10000),
)
SUPPLY_FOOTPRINT = (12, 12)
SUPPLY_HEIGHT = 300.0
# Each spot starts with two builders this far behind it, as on the generated skirmish maps.
BUILDERS = 2
BUILDERS_BEHIND = 24.0

# The village (02 "Central village", #157): each bank's buildings of the in-house set (#408), ARTSCivilBuilding actors of
# CIVIL_DIR's DA_Civil_<Type>, as (type, x, y, garrison) along both sides of the bank's main street (the ring road at
# VILLAGE_STREET_X), each with its door (village.json's entry, on its +X face) toward the street. Both banks are given in
# the west bank's frame: the east bank's stand turned about the centre, so both have the same streets and distances to
# the river, the bridges and the stockpiles. They look different, the west bank round a church square (the gap across the
# street from the church), the east bank a barn and two workshops, one in a chain-link yard (VILLAGE_YARD), but hold the
# same count, footprints within VILLAGE_AREA_TOLERANCE and, in the garrison buildings (actor tag GARRISON_TAG, D5 of
# 05_PRODUCER_ACTIONS: tagged now for #252), the same capacity. check_layout holds the streets to 02: no face within
# VILLAGE_MAIN_STREET / 2 of the main street's centre line, VILLAGE_LANE between neighbours, VILLAGE_RIVER_CLEARANCE from
# the river.
VILLAGE_SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Art", "Shared", "village",
                              "village.json")
CIVIL_DIR = "/Game/Data/Civil"
VILLAGE = {
    "W": (
        ("HouseA", -140.0, -190.0, False),
        ("HouseC", -140.0, -160.0, True),
        ("HouseB", -140.0, -120.0, True),
        ("HouseA", -140.0, -80.0, False),
        ("Church", -135.0, 0.0, True),
        ("HouseB", -140.0, 80.0, False),
        ("Farmhouse", -135.0, 140.0, False),
        ("Farmhouse", -180.0, -150.0, False),
        ("Workshop", -180.0, -90.0, False),
        ("HouseA", -178.0, 60.0, False),
    ),
    "E": (
        ("HouseA", -140.0, -190.0, False),
        ("HouseC", -140.0, -160.0, False),
        ("HouseB", -140.0, -120.0, True),
        ("HouseA", -140.0, -80.0, False),
        ("Barn", -135.0, 0.0, True),
        ("Workshop", -138.0, 80.0, True),
        ("Farmhouse", -135.0, 140.0, False),
        ("Workshop", -182.0, -5.0, False),
        ("HouseC", -180.0, -90.0, False),
        ("HouseA", -178.0, 60.0, False),
    ),
}
GARRISON_TAG = "Garrison"
GARRISON_BUILDINGS = (3, 4)
VILLAGE_AREA_TOLERANCE = 0.1
VILLAGE_MAIN_STREET = 10.0
VILLAGE_LANE = 7.0
VILLAGE_RIVER_CLEARANCE = 15.0

# Roads (02 "Road network", #383), the west side's; the east side has the same turned about the centre. Each is (name,
# kind, waypoints): a polyline whose corners are rounded with ROAD_CORNER_RADIUS (02: at least 25 m) and whose straights
# carry a spline point every ROAD_POINT_SPACING, each at the greybox height, so the road follows the rolling ground. Where
# one road carries straight on into another they share the waypoint, one control point. A branch (a T junction) ends at
# the through road's edge instead: a landscape spline control point has one direction, so a shared one would kink the
# through road. The main axis runs from the base to the home (south) bridge, straight for its last 160 m; the farm track
# runs to the forward supply and on, as the flank route, to the far (north) bridge's approach, which carries straight on
# to the bridge; the village ring road links both approaches along the west bank's street; a dressing road leaves the
# map behind the base. Each bridge's ramp gets its own road from the ramp's foot to the abutment (ramp_roads).
ROAD_KINDS = {
    # kind: (width, side falloff) in metres, and the road material instance.
    "asphalt": (8.0, 6.0, "MI_Road_Asphalt"),
    "ring": (6.0, 5.0, "MI_Road_Asphalt"),
    "track": (5.0, 5.0, "MI_Road_Track"),
}
ROAD_KINDS_HALF_WIDTH = {kind: width / 2.0 for kind, (width, _, _) in ROAD_KINDS.items()}
ROAD_RAMP_FOOT = BRIDGE_LENGTH / 2.0 + BRIDGE_ABUTMENT + BRIDGE_RAMP_TOP + BRIDGE_RAMP
ROAD_ABUTMENT = BRIDGE_LENGTH / 2.0 + BRIDGE_ABUTMENT
VILLAGE_STREET_X = -157.0
FLANK_APPROACH_X = -200.0
WEST_ROADS = (
    ("Main", "asphalt", ((-640.0, -230.0), (-420.0, -260.0), (-260.0, -BRIDGE_Y), (-ROAD_RAMP_FOOT, -BRIDGE_Y))),
    ("Farm", "track", ((-690.0, -130.0), (-640.0, 150.0), (-520.0, 380.0), (-420.0, 470.0), (-250.0, BRIDGE_Y),
                       (FLANK_APPROACH_X, BRIDGE_Y))),
    ("Approach", "ring", ((FLANK_APPROACH_X, BRIDGE_Y), (-ROAD_RAMP_FOOT, BRIDGE_Y))),
    # Between the main road's edge and the north approach's edge.
    ("Ring", "ring", ((VILLAGE_STREET_X, -BRIDGE_Y + ROAD_KINDS_HALF_WIDTH["asphalt"]),
                      (VILLAGE_STREET_X, BRIDGE_Y - ROAD_KINDS_HALF_WIDTH["ring"]))),
    ("Border", "track", ((-830.0, -200.0), (-1000.0, -190.0), (-LANDSCAPE_HALF + 10.0, -180.0))),
)
ROAD_CORNER_RADIUS = 40.0
ROAD_POINT_SPACING = 30.0
# Along a bridge ramp a point every ROAD_RAMP_SPACING, so the spline follows the ramp's kinks; the ramp roads leave the
# ground as it is (bShapeGround off), so its steep sides stay.
ROAD_RAMP_SPACING = 5.0
# How far a road keeps from the river's edge and, with its falloff, from a house.
ROAD_RIVER_CLEARANCE = 20.0
ROAD_HOUSE_CLEARANCE = 1.0
# The road surfaces: one master drawn only into the landscape's runtime virtual texture, which the landscape material
# reads (#156), tiled along the splines on the engine cube (AddLandscapeSplineRoad sets its depth), ROAD_TILE long a tile.
ROAD_MATERIAL_DIR = "/Game/Materials/Road"
ROAD_MASTER = "M_RiverCrossing_Road"
ROAD_SLOTS = {
    "MI_Road_Asphalt": ((0.05, 0.05, 0.052), 0.8),
    "MI_Road_Track": ((0.2, 0.15, 0.1), 0.95),
}
ROAD_MESH = "/Engine/BasicShapes/Cube.Cube"
ROAD_TILE = 4.0

# The village's dressing (02 "Central village", #157): the in-house roadside kit of #409 in ROADSIDE_DIR, and Poly Haven's
# power poles, street lamp and chain-link fence, whose meshes and textures are copied out of the never-cooked
# /Game/PolyHaven into the committed PROPS_DIR, as the grass is, under two masters of their own (build_prop_masters). It
# is all dressing (URTSEditorScripting::SpawnPropInstances): no collision and no navmesh, so a tank drives through a
# fence as through a hedgerow; one actor per kind and bank, labelled Props_<bank>_<kind>. Positions are in the west
# bank's frame (VILLAGE); the east bank's stand turned about the centre.
ROADSIDE_DIR = "/Game/Art/Shared/Roadside"
PROPS_DIR = "/Game/World/Props"
PROP_MASTERS = {False: "M_VillageProp", True: "M_VillagePropMasked"}
# Poly Haven props: (model, the meshes taken, by name or by a name prefix ending in "_", and the offset in cm that brings
# the meshes' shared origin to the prop's foot: a modular kit's parts lie side by side in its scene).
POLYHAVEN_PROPS = {
    "Pole": ("modular_electricity_poles", ("SM_preset_02_",), (450.0, 0.0)),
    "Lamp": ("street_lamp_01", ("SM_street_lamp_01",), (0.0, 0.0)),
    "ChainLink": ("modular_chainlink_fence", ("SM_modular_chainlink_fence_double",), (195.0, 0.0)),
    "ChainLinkPost": ("modular_chainlink_fence", ("SM_modular_chainlink_fence_post",), (100.0, 100.0)),
}
# A fence bay's length: the roadside kit's (roadside.json's rail_bay_cm and wire_bay_cm) and the chain-link panel's.
RAIL_BAY = 3.0
WIRE_BAY = 3.0
CHAINLINK_BAY = 1.91
# Street lamps every VILLAGE_LAMP_SPACING along the main street's river side, VILLAGE_LAMP_SIDE off its centre line; the
# power line along its far side, a pole every VILLAGE_POLE_SPACING, two cables from the ends of each pole's cross-arm
# (POLE_ARM either side of the pole, POLE_WIRE_HEIGHT up) to the next pole's, sagging CABLE_SAG of the span.
VILLAGE_STREET_SPAN = (-270.0, 270.0)
VILLAGE_LAMP_SIDE = 5.0
VILLAGE_LAMP_SPACING = 30.0
VILLAGE_POLE_SIDE = -6.0
VILLAGE_POLE_SPACING = 40.0
POLE_ARM = 0.5
POLE_WIRE_HEIGHT = 5.8
CABLE_SAG = 0.02
CABLE_MESH = f"{ROADSIDE_DIR}/SM_PowerLine_Cable"
# The signs (roadside kit, face toward +X): (sign, x, y, yaw). Traffic keeps right. At each end of the main street a
# place-name sign and a 50 limit for the traffic coming in and a give-way for the traffic leaving onto the through road; a
# priority-road sign on the main road before the junction and a warning before the bridge ramp.
VILLAGE_SIGNS = (
    ("place_name", -152.0, -282.0, -90.0),
    ("speed_50", -152.0, -264.0, -90.0),
    ("give_way", -162.0, -284.0, 90.0),
    ("place_name", -162.0, 282.0, 90.0),
    ("speed_50", -162.0, 264.0, 90.0),
    ("give_way", -152.0, 284.0, -90.0),
    ("priority_road", -190.0, -306.0, 180.0),
    ("warning", -125.0, -306.0, 180.0),
)
# Post-and-rail garden fences VILLAGE_GARDEN off the back of every house (GARDEN_TYPES), from VILLAGE_GARDEN past one
# side to past the other; a pasture of field-wire fence beside the farm with a VILLAGE_GATE gap toward the street; and
# on the east bank the workshop's chain-link yard. Each run is a polyline of (x, y); bays are stretched to fit a leg.
GARDEN_TYPES = ("HouseA", "HouseB", "HouseC", "Farmhouse")
VILLAGE_GARDEN = 5.0
VILLAGE_PASTURE = ((-205.0, -146.0), (-205.0, -185.0), (-240.0, -185.0), (-240.0, -115.0), (-205.0, -115.0),
                   (-205.0, -142.0))
VILLAGE_YARD = {"E": ((-193.0, -20.0), (-215.0, -20.0), (-215.0, 10.0), (-193.0, 10.0))}
VILLAGE_PROP_CLEARANCE = 1.0
# The landscape's runtime virtual texture (03 section 5): base colour, normal and roughness, which the landscape material
# writes and reads (#156) and the roads draw into. RVT_TILE_COUNT and RVT_TILE_SIZE are log2: 2048 tiles of 256 texels,
# about 5 mm a texel over the landscape, finer than the closest zoom's 7 mm a pixel. Adaptive keeps the page table small.
RVT_DIR = "/Game/World/Landscape"
RVT_NAME = "RVT_WD_RiverCrossing_01"
RVT_TILE_COUNT = 11
RVT_TILE_SIZE = 2

# The landscape material (#156, 03 section 5): M_Landscape_WD_Temperate and its instance, 7 paint layers of the scanned
# surfaces of #330. The textures each layer uses are copied from the git-ignored /Game/Surfaces into
# LANDSCAPE_TEXTURE_DIR, which is committed and cooked (the producer's decision on #156, 01_ASSET_AUDIT.md). Per layer:
# (surface, its tile in metres, physical material, tint). Grass and Meadow carry grass (LANDSCAPE_GRASS).
LANDSCAPE_DIR = RVT_DIR
LANDSCAPE_TEXTURE_DIR = f"{LANDSCAPE_DIR}/Textures"
LANDSCAPE_LAYER_DIR = f"{LANDSCAPE_DIR}/Layers"
LANDSCAPE_MASTER = "M_Landscape_WD_Temperate"
LANDSCAPE_INSTANCE = "MI_Landscape_WD_Temperate"
SURFACES_DIR = "/Game/Surfaces"
PHYSICAL_DIR = "/Game/Materials/Physical"
LANDSCAPE_LAYERS = (
    ("Grass", "UncutGrass_oilpt20", 2.0, "PM_Grass", (1.0, 1.0, 1.0)),
    ("Meadow", "WildGrass_sfknaeoa", 2.0, "PM_Grass", (1.6, 1.55, 1.35)),
    ("Dirt", "MilitaryTrenchesGroundDirtFine03_yd0jchk", 2.0, "PM_Dirt", (1.0, 1.0, 1.0)),
    ("Mud", "MudPebbles", 2.0, "PM_Mud", (1.0, 1.0, 1.0)),
    ("Gravel", "GravelGround_vi0maebg", 2.0, "PM_Sand", (1.0, 1.0, 1.0)),
    ("Rock", "GougedRockCliff_vlcpcbc", 2.0, "PM_Dirt", (1.0, 1.0, 1.0)),
    ("Asphalt", "MilitaryTrenchesGroundDirtCoarse01_yd0ldai", 2.0, "PM_Road", (1.0, 1.0, 1.0)),
)
LANDSCAPE_LAYER_NAMES = tuple(layer[0] for layer in LANDSCAPE_LAYERS)
LANDSCAPE_LAYER_INDEX = {name: number for number, name in enumerate(LANDSCAPE_LAYER_NAMES)}
# Against tiling at the farthest zoom (520 m across): each layer's base colour mixes its own tile with LANDSCAPE_FAR_SCALE
# times it, LANDSCAPE_FAR_MIX of the far one, and the whole is shaded by a macro mask from LANDSCAPE_MACRO's low to its
# high: the Meadow surface's green over LANDSCAPE_MACRO_TILES metres, both samples summed, times MACRO_GAIN less
# MACRO_BIAS, so the sum's mean (about 0.07 linear) lands mid-way and two standard deviations either side span 0 to 1.
LANDSCAPE_FAR_SCALE = 6.3
LANDSCAPE_FAR_MIX = 0.4
LANDSCAPE_MACRO_TILES = (47.0, 213.0)
LANDSCAPE_MACRO = (0.82, 1.15)
MACRO_GAIN = 14.7
MACRO_BIAS = 0.53

# The paint (#156, 02 "Fields"): a patchwork of fields, FIELD_SIZE metres turned FIELD_YAW degrees, each pasture (Grass),
# meadow or fallow (Dirt) by FIELD_KINDS' shares, with a FIELD_MARGIN metres strip of meadow along its edges; mud along
# the river (RIVER_MUD over the water's edge, rough meadow to RIVER_MEADOW), trampled dirt on the base pads, pasture on
# the village rise and rock on slopes over ROCK_SLOPE degrees. Weights blend over PAINT_BLEND m. Inside a field, patches
# about FIELD_PATCH metres across of FIELD_PATCHES' other layer, up to its share, break the field's one colour.
FIELD_SIZE = (240.0, 120.0)
FIELD_YAW = 12.0
FIELD_KINDS = (("Grass", 0.45), ("Meadow", 0.25), ("Dirt", 0.3))
FIELD_MARGIN = 1.5
FIELD_PATCH = 35.0
FIELD_PATCHES = {"Grass": ("Meadow", 0.45), "Meadow": ("Grass", 0.35), "Dirt": ("Grass", 0.3)}
PAINT_BLEND = 2.5
RIVER_MUD = 8.0
RIVER_MEADOW = 30.0
ROCK_SLOPE = 30.0
# The roads' paint layers (#156): what a road lays under itself, its shoulder ROAD_SHOULDER metres to each side and,
# fading, its falloff, which also gives the road its physical material (the slabs have no collision). Painted into the
# imported weightmaps (road_paint), so every component the roads cross has the layer from the start, and by the splines,
# so a road moved in the editor takes its paint along.
ROAD_LAYERS = {"asphalt": "Asphalt", "ring": "Asphalt", "track": "Gravel"}
ROAD_SHOULDER = 1.0

# Landscape Grass Types on Grass and Meadow (03 section 5): at most GRASS_HEIGHT tall, culled from GRASS_CULL metres, no
# collision and no shadows. Per layer: (grass type, Poly Haven model copied from the never-cooked /Game/PolyHaven, its
# meshes, its base colour, instances per 10 m by 10 m, the clumps' height range in metres). The copies and their master
# live in LANDSCAPE_GRASS_DIR.
LANDSCAPE_GRASS_DIR = f"{LANDSCAPE_DIR}/Grass"
POLYHAVEN_DIR = "/Game/PolyHaven"
GRASS_MASTER = "M_LandscapeGrass"
GRASS_HEIGHT = 0.6
GRASS_CULL = (120.0, 150.0)
LANDSCAPE_GRASS = {
    "Grass": ("LGT_WD_Grass", "grass_bermuda_01", ("medium_a", "medium_b", "medium_c"), "BC", 300.0, (0.15, 0.25)),
    "Meadow": ("LGT_WD_Meadow", "grass_medium_02", ("a", "b", "c"), "dry_BC", 500.0, (0.35, GRASS_HEIGHT)),
}

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editing = unreal.MaterialEditingLibrary


def smoothstep(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def rotate(point):
    """The point turned 180 degrees about the map centre: the east side's copy of a west-side position."""
    return (-point[0], -point[1])


def river_x(y):
    return RIVER_SWING * math.sin(math.pi * y / RIVER_HALF_PERIOD)


def river_slope(y):
    """dx/dy of the river's centre line."""
    return RIVER_SWING * math.pi / RIVER_HALF_PERIOD * math.cos(math.pi * y / RIVER_HALF_PERIOD)


def river_width(y):
    return RIVER_NARROW + (RIVER_WIDE - RIVER_NARROW) * abs(math.sin(math.pi * y / RIVER_HALF_PERIOD))


def river_distance(x, y):
    """About how far (x, y) is from the river's centre line, across the river, and the river's half width there."""
    return abs(x - river_x(y)) / math.sqrt(1.0 + river_slope(y) ** 2), river_width(y) / 2.0


def box_mask(dx, dy, half_x, half_y, falloff):
    """1 inside the box of half sizes half_x, half_y around a point dx, dy away, falling to 0 over falloff metres."""
    return 1.0 - smoothstep(max(abs(dx) - half_x, abs(dy) - half_y, 0.0) / falloff)


def bump(dx, dy, radius_x, radius_y, height):
    """A cosine hill of height, radius_x by radius_y across its foot."""
    r = math.sqrt((dx / radius_x) ** 2 + (dy / radius_y) ** 2)
    return height * 0.5 * (1.0 + math.cos(math.pi * r)) if r < 1.0 else 0.0


def river_depth(y):
    return RIVER_SHALLOW + (RIVER_DEEP - RIVER_SHALLOW) * abs(math.sin(math.pi * y / RIVER_HALF_PERIOD))


def ramp(x, y, bridge_y):
    """How high the ramps of the bridge at bridge_y lift (x, y): the deck's top behind each abutment's approach slab, down
    to nothing BRIDGE_RAMP further on, and nothing under the bridge, where the abutment's stem stands in front of it."""
    along = abs(x) - BRIDGE_LENGTH / 2.0
    if along < BRIDGE_ABUTMENT / 2.0:
        return 0.0
    rise = BRIDGE_DECK_TOP * min(1.0, max(0.0, 1.0 - (along - BRIDGE_ABUTMENT - BRIDGE_RAMP_TOP) / BRIDGE_RAMP))
    side = 1.0 - smoothstep((abs(y - bridge_y) - BRIDGE_RAMP_HALF_WIDTH) / BRIDGE_RAMP_SIDE)
    return rise * side


def height(x, y):
    """The greybox ground height at (x, y), in metres. Equal at (x, y) and (-x, -y). The river's bed is not in it: the
    river's water brush carves it (create_river)."""
    rolling = ROLLING_LIFT
    for amplitude, wave_x, wave_y, kind in ROLLING:
        # cos * cos and sin * sin both keep their value under (x, y) -> (-x, -y).
        wave = math.cos if kind == "cos" else math.sin
        rolling += amplitude * wave(x / wave_x) * wave(y / wave_y)

    # The base pads and the bridge approaches are flat at 0.
    flat = 0.0
    for base in (WEST_BASE, rotate(WEST_BASE)):
        flat = max(flat, box_mask(x - base[0], y - base[1], PAD_HALF + 20.0, PAD_HALF + 20.0, PAD_FALLOFF))
    for bridge_y in (BRIDGE_Y, -BRIDGE_Y):
        flat = max(flat, box_mask(x, y - bridge_y, BRIDGE_FLAT[0], BRIDGE_FLAT[1], BRIDGE_FLAT_FALLOFF))

    distance, half_width = river_distance(x, y)
    # No rolling dip by the river, so every bank stands well above the bed.
    river_flat = 1.0 - smoothstep((distance - half_width - RIVER_FLAT) / RIVER_FLAT_FALLOFF)

    features = 0.0
    for hill in (WEST_HILL, rotate(WEST_HILL)):
        features += bump(x - hill[0], y - hill[1], HILL_RADIUS, HILL_RADIUS, HILL_HEIGHT)
    features += bump(x, y, VILLAGE_RADII[0], VILLAGE_RADII[1], VILLAGE_RISE)
    features += BORDER_HEIGHT * smoothstep((max(abs(x), abs(y)) - PLAYABLE_HALF) / (LANDSCAPE_HALF - PLAYABLE_HALF))

    ground = rolling * (1.0 - max(flat, river_flat)) + features * (1.0 - flat)
    lift = max(ramp(x, y, BRIDGE_Y), ramp(x, y, -BRIDGE_Y))
    return ground + max(0.0, lift - ground)


def write_heightmap(file_name):
    """Writes the greybox heights, VERTICES x VERTICES 16-bit steps, row by row from the south-west corner along +X.
    Returns the heights in metres in the same order."""
    heights = []
    rows = []
    for row in range(VERTICES):
        y = -LANDSCAPE_HALF + row * QUAD
        metres = [height(-LANDSCAPE_HALF + column * QUAD, y) for column in range(VERTICES)]
        heights += metres
        steps = [min(65535, max(0, round(32768 + value * HEIGHT_STEPS_PER_METRE))) for value in metres]
        rows.append(struct.pack(f"<{VERTICES}H", *steps))
    os.makedirs(os.path.dirname(file_name), exist_ok=True)
    with open(file_name, "wb") as file:
        file.write(b"".join(rows))
    return heights


def cell_hash(a, b, salt=0):
    """A fixed hash of the integer cell (a, b), 0 to 1, the same on every run."""
    value = (a * 0x9E3779B1 + b * 0x85EBCA77 + salt * 0xC2B2AE3D) & 0xFFFFFFFF
    for multiplier, shift in ((0x2C1B3C6D, 15), (0x297A2D39, 12), (0x9E3779B1, 15)):
        value ^= value >> shift
        value = (value * multiplier) & 0xFFFFFFFF
    return (value ^ (value >> 16)) / float(0xFFFFFFFF)


def field_kind(x, y):
    """The field (x, y) lies in, as (its layer, how far (x, y) is from the field's edge in metres). The fields lie in rows
    FIELD_SIZE[1] deep, each row's fields its own width around FIELD_SIZE[0] and shifted along it, so the patchwork does
    not read as a grid. The fields of the east side are the west side's turned about the centre, so both sides farm alike."""
    if x > 0.0 or (x == 0.0 and y > 0.0):
        x, y = -x, -y
    yaw = math.radians(FIELD_YAW)
    v = (-x * math.sin(yaw) + y * math.cos(yaw)) / FIELD_SIZE[1]
    row = math.floor(v)
    width = FIELD_SIZE[0] * (0.6 + 0.9 * cell_hash(row, 0, 1))
    u = (x * math.cos(yaw) + y * math.sin(yaw)) / width + cell_hash(row, 0, 2)
    column = math.floor(u)
    edge = min(min(u - column, column + 1 - u) * width, min(v - row, row + 1 - v) * FIELD_SIZE[1])
    share = cell_hash(column, row)
    for kind, kind_share in FIELD_KINDS:
        if share < kind_share:
            return kind, edge
        share -= kind_share
    return FIELD_KINDS[-1][0], edge


def value_noise(x, y, size, salt):
    """Smooth value noise, 0 to 1, in blobs about size metres across."""
    u, v = x / size, y / size
    column, row = math.floor(u), math.floor(v)
    fu, fv = u - column, v - row
    fu, fv = fu * fu * (3.0 - 2.0 * fu), fv * fv * (3.0 - 2.0 * fv)
    low = cell_hash(column, row, salt) + (cell_hash(column + 1, row, salt) - cell_hash(column, row, salt)) * fu
    high = cell_hash(column, row + 1, salt) + (cell_hash(column + 1, row + 1, salt) - cell_hash(column, row + 1, salt)) * fu
    return low + (high - low) * fv


def patchiness(x, y):
    """0 to 1, in soft irregular patches about FIELD_PATCH metres across, the same at (x, y) and (-x, -y)."""
    if x > 0.0 or (x == 0.0 and y > 0.0):
        x, y = -x, -y
    noise = 0.65 * value_noise(x, y, FIELD_PATCH, 3) + 0.35 * value_noise(x, y, FIELD_PATCH * 0.37, 4)
    return smoothstep((noise - 0.45) / 0.25)


def paint_weights(x, y, slope):
    """The 7 layers' weights at (x, y), in LANDSCAPE_LAYER_NAMES' order, summing to 1; slope in degrees."""
    index = LANDSCAPE_LAYER_INDEX
    weights = [0.0] * len(LANDSCAPE_LAYER_NAMES)

    def lay(name, amount):
        if amount > 0.0:
            for number in range(len(weights)):
                weights[number] *= 1.0 - amount
            weights[index[name]] += amount

    kind, edge = field_kind(x, y)
    # The margins wander a metre or so, as a plough's edge does.
    wobble = math.sin(x * 0.31 + y * 0.17) + math.sin(x * 0.13 - y * 0.29)
    inside = smoothstep((edge - FIELD_MARGIN - 0.5 * wobble) / PAINT_BLEND)
    patch, share = FIELD_PATCHES[kind]
    patched = share * patchiness(x, y)
    weights[index[kind]] = inside * (1.0 - patched)
    weights[index[patch]] += inside * patched
    weights[index["Meadow"]] += 1.0 - inside

    r = math.sqrt((x / VILLAGE_RADII[0]) ** 2 + (y / VILLAGE_RADII[1]) ** 2)
    lay("Grass", 1.0 - smoothstep((r - 0.8) / 0.2))
    for base in (WEST_BASE, rotate(WEST_BASE)):
        lay("Dirt", box_mask(x - base[0], y - base[1], PAD_HALF + 10.0, PAD_HALF + 10.0, PAINT_BLEND * 5.0))
    distance, half_width = river_distance(x, y)
    bank = distance - half_width
    lay("Meadow", 1.0 - smoothstep((bank - RIVER_MEADOW) / (PAINT_BLEND * 4.0)))
    lay("Mud", 1.0 - smoothstep((bank - RIVER_MUD) / PAINT_BLEND))
    lay("Rock", smoothstep((slope - ROCK_SLOPE) / 5.0))
    return weights


def road_paint():
    """Every road's paint (ROAD_LAYERS) by vertex index: (layer, amount), the strongest where roads meet."""
    paint = {}
    for name, kind, waypoints in WEST_ROADS + tuple(ramp_roads()):
        width, falloff, _ = ROAD_KINDS[kind]
        layer = LANDSCAPE_LAYER_INDEX[ROAD_LAYERS[kind]]
        inner = width / 2.0 + ROAD_SHOULDER
        reach = width / 2.0 + falloff
        path = waypoints if name.startswith("Ramp") else road_path(waypoints)
        for flip in (False, True):
            points = [rotate(point) if flip else point for point in path]
            for (ax, ay), (bx, by) in zip(points, points[1:]):
                length_sq = max((bx - ax) ** 2 + (by - ay) ** 2, 1e-6)
                first_column = max(0, int((min(ax, bx) - reach + LANDSCAPE_HALF) / QUAD))
                last_column = min(VERTICES - 1, int((max(ax, bx) + reach + LANDSCAPE_HALF) / QUAD) + 1)
                first_row = max(0, int((min(ay, by) - reach + LANDSCAPE_HALF) / QUAD))
                last_row = min(VERTICES - 1, int((max(ay, by) + reach + LANDSCAPE_HALF) / QUAD) + 1)
                for row in range(first_row, last_row + 1):
                    y = -LANDSCAPE_HALF + row * QUAD
                    for column in range(first_column, last_column + 1):
                        x = -LANDSCAPE_HALF + column * QUAD
                        t = min(1.0, max(0.0, ((x - ax) * (bx - ax) + (y - ay) * (by - ay)) / length_sq))
                        distance = math.hypot(x - ax - t * (bx - ax), y - ay - t * (by - ay))
                        if distance >= reach:
                            continue
                        amount = 1.0 - smoothstep((distance - inner) / max(reach - inner, 0.1))
                        at = row * VERTICES + column
                        if amount > paint.get(at, (0, 0.0))[1]:
                            paint[at] = (layer, amount)
    return paint


def write_weightmaps(heights, file_names):
    """Writes the 7 paint layers' weights (paint_weights, then the roads' road_paint over them) to file_names, in
    LANDSCAPE_LAYER_NAMES' order, each VERTICES x VERTICES bytes as the heightmap, the 7 summing to 255 at every vertex;
    heights are write_heightmap's."""
    layers = [bytearray(VERTICES * VERTICES) for _ in LANDSCAPE_LAYER_NAMES]
    roads = road_paint()
    for row in range(VERTICES):
        y = -LANDSCAPE_HALF + row * QUAD
        above, below = min(row + 1, VERTICES - 1) * VERTICES, max(row - 1, 0) * VERTICES
        for column in range(VERTICES):
            at = row * VERTICES + column
            dx = (heights[row * VERTICES + min(column + 1, VERTICES - 1)] - heights[row * VERTICES + max(column - 1, 0)])
            dy = heights[above + column] - heights[below + column]
            slope = math.degrees(math.atan(math.hypot(dx, dy) / (2.0 * QUAD)))
            weights = paint_weights(-LANDSCAPE_HALF + column * QUAD, y, slope)
            if at in roads:
                layer, amount = roads[at]
                weights = [weight * (1.0 - amount) for weight in weights]
                weights[layer] += amount
            steps = [int(weight * 255.0) for weight in weights]
            steps[weights.index(max(weights))] += 255 - sum(steps)
            for number, step in enumerate(steps):
                layers[number][at] = step
    for file_name, data in zip(file_names, layers):
        with open(file_name, "wb") as file:
            file.write(bytes(data))


def read_village():
    """village.json's buildings (#408) by type: capacity, entry and the pristine mesh's bounds in metres."""
    with open(VILLAGE_SOURCE, encoding="utf-8") as file:
        village = json.load(file)
    types = {}
    for key, building in village["buildings"].items():
        if building["capacity"] > 0:
            low, high = village["meshes"][building["states"][0]]["bounds"]
            types[key] = {"capacity": building["capacity"], "low": [v / 100.0 for v in low], "high": [v / 100.0 for v in high]}
    return types


def village_yaw(x):
    """A building's yaw in the west bank's frame: its door (+X) toward the main street."""
    return 0.0 if x < VILLAGE_STREET_X else 180.0


def village_buildings(types):
    """Every village building, both banks, as (bank, index, type, centre, yaw, box, garrison) in world metres: box is the
    pristine footprint (min x, max x, min y, max y), turned with the building."""
    buildings = []
    for bank, entries in VILLAGE.items():
        for index, (kind, x, y, garrison) in enumerate(entries):
            low, high = types[kind]["low"], types[kind]["high"]
            yaw = village_yaw(x)
            box = (x + low[0], x + high[0], y + low[1], y + high[1]) if yaw == 0.0 else \
                (x - high[0], x - low[0], y - high[1], y - low[1])
            centre = (x, y)
            if bank == "E":
                centre, yaw = rotate(centre), (yaw + 180.0) % 360.0
                box = (-box[1], -box[0], -box[3], -box[2])
            buildings.append((bank, index, kind, centre, yaw, box, garrison))
    return buildings


def box_gap(box, other):
    """The clear distance between two (min x, max x, min y, max y) boxes along the axis they are apart on, < 0 overlapping."""
    return max(other[0] - box[1], box[0] - other[1], other[2] - box[3], box[2] - other[3])


def box_corners(box):
    return [(x, y) for x in box[:2] for y in box[2:]]


def check_layout():
    """Raises when the layout breaks a rule the design depends on (02 "Central village"): a building or a supply source in
    the river or too close to it, a building in the main street's VILLAGE_MAIN_STREET or less than VILLAGE_LANE from a
    neighbour or a stockpile, or banks that differ in count, garrison capacity or footprint."""
    types = read_village()
    buildings = village_buildings(types)
    sources = [(name, point) for name, point, _ in WEST_SOURCES] + [(name, rotate(point)) for name, point, _ in WEST_SOURCES]
    stockpiles = [(point[0] - SUPPLY_FOOTPRINT[0] / 2.0, point[0] + SUPPLY_FOOTPRINT[0] / 2.0,
                   point[1] - SUPPLY_FOOTPRINT[1] / 2.0, point[1] + SUPPLY_FOOTPRINT[1] / 2.0) for _, point in sources]
    for bank, index, kind, centre, _, box, _ in buildings:
        where = f"Village {bank}{index + 1:02d} ({kind} at {centre[0]:.0f}, {centre[1]:.0f})"
        for corner in box_corners(box):
            distance, half_width = river_distance(*corner)
            if distance < half_width + VILLAGE_RIVER_CLEARANCE:
                raise RuntimeError(f"{where} stands within {VILLAGE_RIVER_CLEARANCE} m of the river")
        street = VILLAGE_STREET_X if bank == "W" else -VILLAGE_STREET_X
        if box[0] - VILLAGE_MAIN_STREET / 2.0 < street < box[1] + VILLAGE_MAIN_STREET / 2.0:
            raise RuntimeError(f"{where} stands in the {VILLAGE_MAIN_STREET:.0f} m main street")
        for other_bank, other_index, other_kind, _, _, other, _ in buildings:
            if (other_bank, other_index) > (bank, index) and box_gap(box, other) < VILLAGE_LANE:
                raise RuntimeError(f"{where} and {other_bank}{other_index + 1:02d} ({other_kind}) leave a "
                                   f"{box_gap(box, other):.1f} m lane, under {VILLAGE_LANE:.0f}")
        for stockpile in stockpiles:
            if box_gap(box, stockpile) < VILLAGE_LANE:
                raise RuntimeError(f"{where} stands within {VILLAGE_LANE:.0f} m of a supply source")
    for name, (x, y) in sources:
        distance, half_width = river_distance(x, y)
        if distance < half_width + 20.0:
            raise RuntimeError(f"Supply source {name} lies within 20 m of the river")

    banks = {}
    for bank, _, kind, _, _, box, garrison in buildings:
        count, garrisons, capacity, area = banks.get(bank, (0, 0, 0, 0.0))
        banks[bank] = (count + 1, garrisons + garrison, capacity + (types[kind]["capacity"] if garrison else 0),
                       area + (box[1] - box[0]) * (box[3] - box[2]))
    (west_count, west_garrisons, west_capacity, west_area), (east_count, east_garrisons, east_capacity, east_area) = \
        banks["W"], banks["E"]
    if west_count != east_count or west_capacity != east_capacity:
        raise RuntimeError(f"The banks differ: {west_count} and {east_count} buildings, garrison capacity {west_capacity} "
                           f"and {east_capacity}")
    for garrisons in (west_garrisons, east_garrisons):
        if not GARRISON_BUILDINGS[0] <= garrisons <= GARRISON_BUILDINGS[1]:
            raise RuntimeError(f"A bank has {garrisons} garrison buildings, not {GARRISON_BUILDINGS[0]} to "
                               f"{GARRISON_BUILDINGS[1]}")
    if abs(west_area - east_area) > VILLAGE_AREA_TOLERANCE * max(west_area, east_area):
        raise RuntimeError(f"The banks' footprints differ: {west_area:.0f} and {east_area:.0f} m2")
    unreal.log(f"rivercrossing_greybox: village of {west_count} + {east_count} buildings, garrison capacity "
               f"{west_capacity} a bank in {west_garrisons} and {east_garrisons} buildings, footprints {west_area:.0f} and "
               f"{east_area:.0f} m2")


def cm(point, z=0.0):
    return unreal.Vector(point[0] * 100.0, point[1] * 100.0, z * 100.0)


def apply_landscape_physics(world):
    """Gives every landscape in world GROUND_PHYSICAL_MATERIAL as its default physical material and rebuilds its
    collision, so the vehicles' dust (RTSGround::TraceSurface) finds dirt on it. Returns how many changed."""
    material = unreal.load_asset(GROUND_PHYSICAL_MATERIAL)
    if not material:
        raise RuntimeError(f"Could not load {GROUND_PHYSICAL_MATERIAL}")
    return unreal.RTSEditorScripting.set_landscape_physical_material(world, material)


def spawn(actor_class, location, yaw=0.0, label=None):
    actor = actors.spawn_actor_from_class(actor_class, location, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    return actor


def spawn_nav_null(label, center, size, yaw=0.0):
    """A NavArea_Null box of size (x, y, z) metres around center; the default volume brush is a 2 m cube."""
    volume = spawn(unreal.NavModifierVolume, cm(center[:2], center[2]), yaw, label)
    volume.set_actor_scale3d(unreal.Vector(size[0] / 2.0, size[1] / 2.0, size[2] / 2.0))
    volume.set_editor_property("area_class", unreal.NavArea_Null)
    return volume


def spawn_water_blockers():
    """The Water_ nav blockers along the whole river, edge to edge past the playable area."""
    y = -LANDSCAPE_HALF
    index = 0
    while y < LANDSCAPE_HALF:
        end = min(y + WATER_SEGMENT, LANDSCAPE_HALF)
        start_x, end_x = river_x(y), river_x(end)
        length = math.hypot(end_x - start_x, end - y) + 2.0
        width = max(river_width(y), river_width(end)) + WATER_MARGIN
        yaw = math.degrees(math.atan2(end - y, end_x - start_x))
        center = ((start_x + end_x) / 2.0, (y + end) / 2.0, (WATER_BOTTOM + WATER_TOP) / 2.0)
        spawn_nav_null(f"Water_{index:03d}", center, (length, width, WATER_TOP - WATER_BOTTOM), yaw)
        y = end
        index += 1


def instance(directory, name, parent):
    """The material instance directory/name of parent, its parameters cleared, new or the one there. Does not save."""
    path = f"{directory}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        made = unreal.load_asset(path)
        editing.clear_all_material_instance_parameters(made)
    else:
        made = asset_tools.create_asset(name, directory, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(made, parent)
    return made


def build_river_material():
    """MI_RiverCrossing_River: the engine's river material with RIVER_MATERIAL_VECTORS. Does not save."""
    parent = unreal.load_asset(RIVER_MATERIAL_PARENT)
    if not parent:
        raise RuntimeError(f"Could not load {RIVER_MATERIAL_PARENT}: is the Water plugin enabled?")
    river = instance(RIVER_MATERIAL_DIR, RIVER_MATERIAL, parent)
    for name, value in sorted(RIVER_MATERIAL_VECTORS.items()):
        editing.set_material_instance_vector_parameter_value(river, name, unreal.LinearColor(*value))
    editing.update_material_instance(river)
    # Read back: a name the parent does not have is dropped without a word.
    for name, value in sorted(RIVER_MATERIAL_VECTORS.items()):
        got = editing.get_material_instance_vector_parameter_value(river, name)
        if abs(got.r - value[0]) > 1e-3 or abs(got.b - value[2]) > 1e-3:
            raise RuntimeError(f"{RIVER_MATERIAL}: {name} is {got}, not {value}; has {RIVER_MATERIAL_PARENT} renamed it?")
    return river


def create_river(world, material):
    """The Water plugin river along the centre line, edge to edge, carving its bed into the landscape (#155)."""
    points, widths, depths = [], [], []
    steps = int(math.ceil(2.0 * LANDSCAPE_HALF / RIVER_POINT_SPACING))
    for step in range(steps + 1):
        y = -LANDSCAPE_HALF + 2.0 * LANDSCAPE_HALF * step / steps
        points.append(cm((river_x(y), y), WATER_LEVEL))
        widths.append(river_width(y) * 100.0)
        depths.append(river_depth(y) * 100.0)
    river = unreal.RTSEditorScripting.create_river(world, points, widths, depths, RIVER_BANK_ANGLE, RIVER_SHORE * 100.0,
                                                   material)
    if not river:
        raise RuntimeError("The Water plugin river did not carve the landscape (CreateRiver in the log says why)")
    return river


def road_path(waypoints):
    """The (x, y) points of a road along waypoints: each corner rounded with ROAD_CORNER_RADIUS (less where a leg is too
    short), a point every few metres along the arc and every ROAD_POINT_SPACING or less along the straights. The ends and
    every straight-through waypoint stay where they are, so roads that share one meet there."""
    corners = [(waypoints[0], None)]
    for index in range(1, len(waypoints) - 1):
        before, at, after = waypoints[index - 1], waypoints[index], waypoints[index + 1]
        into = (at[0] - before[0], at[1] - before[1])
        out = (after[0] - at[0], after[1] - at[1])
        into_length, out_length = math.hypot(*into), math.hypot(*out)
        turn = math.atan2(into[0] * out[1] - into[1] * out[0], into[0] * out[0] + into[1] * out[1])
        if abs(turn) < 1e-3:
            corners.append((at, None))
            continue
        cut = min(ROAD_CORNER_RADIUS * math.tan(abs(turn) / 2.0), into_length / 2.0, out_length / 2.0)
        radius = cut / math.tan(abs(turn) / 2.0)
        start = (at[0] - into[0] / into_length * cut, at[1] - into[1] / into_length * cut)
        # The arc's centre lies to the side the road turns to, radius from the arc's start.
        side = 1.0 if turn > 0.0 else -1.0
        centre = (start[0] - side * into[1] / into_length * radius, start[1] + side * into[0] / into_length * radius)
        heading = math.atan2(start[1] - centre[1], start[0] - centre[0])
        steps = max(2, int(math.ceil(abs(turn) * radius / 8.0)))
        arc = [(centre[0] + radius * math.cos(heading + turn * step / steps),
                centre[1] + radius * math.sin(heading + turn * step / steps)) for step in range(steps + 1)]
        corners.append((arc[0], arc))
    corners.append((waypoints[-1], None))

    points = []
    for point, arc in corners:
        if points:
            last = points[-1]
            gap = math.hypot(point[0] - last[0], point[1] - last[1])
            steps = int(math.ceil(gap / ROAD_POINT_SPACING))
            points += [(last[0] + (point[0] - last[0]) * step / steps, last[1] + (point[1] - last[1]) * step / steps)
                       for step in range(1, steps)]
        points += arc if arc else [point]
    return points


def ramp_roads():
    """Each bridge ramp's road on the west side, from the ramp's foot (where its approach road ends) to the abutment, a
    point every ROAD_RAMP_SPACING. The east side has the same turned about the centre."""
    roads = []
    for name, bridge_y in (("North", BRIDGE_Y), ("South", -BRIDGE_Y)):
        steps = int(round((ROAD_RAMP_FOOT - ROAD_ABUTMENT) / ROAD_RAMP_SPACING))
        points = [(-ROAD_RAMP_FOOT + (ROAD_RAMP_FOOT - ROAD_ABUTMENT) * step / steps, bridge_y) for step in range(steps + 1)]
        roads.append((f"Ramp{name}", "asphalt" if bridge_y < 0.0 else "ring", points))
    return roads


def check_roads():
    """Raises when a road (02 "Road network") breaks a rule: a point near the river, a house in the road or its falloff, or a
    curve tighter than 25 m."""
    houses = [box for _, _, _, _, _, box, _ in village_buildings(read_village())]
    for name, kind, waypoints in WEST_ROADS:
        width, falloff, _ = ROAD_KINDS[kind]
        points = road_path(waypoints)
        for x, y in points:
            distance, half_width = river_distance(x, y)
            if distance < half_width + ROAD_RIVER_CLEARANCE:
                raise RuntimeError(f"Road {name} passes within {ROAD_RIVER_CLEARANCE} m of the river at ({x:.0f}, {y:.0f})")
            for side in (1.0, -1.0):
                for house in houses:
                    gap = box_gap(house, (side * x, side * x, side * y, side * y))
                    if gap < width / 2.0 + falloff + ROAD_HOUSE_CLEARANCE:
                        raise RuntimeError(f"Road {name} runs {gap:.1f} m from the house at ({(house[0] + house[1]) / 2.0:.0f}, "
                                           f"{(house[2] + house[3]) / 2.0:.0f})")
        # The radius through three neighbouring points (circumradius) never under 25 m.
        for a, b, c in zip(points, points[1:], points[2:]):
            cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if abs(cross) > 1e-6:
                radius = math.dist(a, b) * math.dist(b, c) * math.dist(a, c) / (2.0 * abs(cross))
                if radius < 25.0:
                    raise RuntimeError(f"Road {name} curves at a {radius:.1f} m radius near ({b[0]:.0f}, {b[1]:.0f})")


def build_road_materials():
    """M_RiverCrossing_Road, a colour and a roughness drawn both in the main pass and into a runtime virtual texture, for
    spline meshes, and its ROAD_SLOTS instances. Does not save; returns the instances by name."""
    path = f"{ROAD_MATERIAL_DIR}/{ROAD_MASTER}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
    else:
        material = asset_tools.create_asset(ROAD_MASTER, ROAD_MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    editing.delete_all_material_expressions(material)
    default_color, default_roughness = ROAD_SLOTS["MI_Road_Asphalt"]
    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(*default_color, 1.0))
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, 250)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", default_roughness)
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    output = editing.create_material_expression(material, unreal.MaterialExpressionRuntimeVirtualTextureOutput, -150, 450)
    editing.connect_material_expressions(color, "", output, "BaseColor")
    editing.connect_material_expressions(roughness, "", output, "Roughness")
    editing.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_SPLINE_MESH)
    if not editing.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_SPLINE_MESH):
        raise RuntimeError(f"{ROAD_MASTER}: could not set the spline mesh usage")
    editing.recompile_material(material)
    slots = {}
    for name, (value, rough) in ROAD_SLOTS.items():
        made = instance(ROAD_MATERIAL_DIR, name, material)
        editing.set_material_instance_vector_parameter_value(made, "Color", unreal.LinearColor(*value, 1.0))
        editing.set_material_instance_scalar_parameter_value(made, "Roughness", rough)
        editing.update_material_instance(made)
        slots[name] = made
    return slots


def create_virtual_texture(world):
    """The landscape's runtime virtual texture (03 section 5) and its volume over the landscape. Does not save."""
    texture = unreal.RTSEditorScripting.create_landscape_virtual_texture(world, f"{RVT_DIR}/{RVT_NAME}")
    if not texture:
        raise RuntimeError("Could not give the landscape a runtime virtual texture (CreateLandscapeVirtualTexture in the log)")
    texture.set_editor_property("material_type", unreal.RuntimeVirtualTextureMaterialType.BASE_COLOR_NORMAL_ROUGHNESS)
    texture.set_editor_property("tile_count", RVT_TILE_COUNT)
    texture.set_editor_property("tile_size", RVT_TILE_SIZE)
    texture.set_editor_property("adaptive", True)
    return texture


def copy_asset(source, target):
    """The asset target, copied from source when it does not exist yet: the copies are committed, so a checkout without
    the git-ignored packs still builds. Does not save."""
    if unreal.EditorAssetLibrary.does_asset_exist(target):
        return unreal.load_asset(target)
    if not unreal.EditorAssetLibrary.does_asset_exist(source):
        raise RuntimeError(f"Neither {target} nor its source {source} exists: import the packs first (ue.ps1 -Task surfaces, "
                           "-Task freeassets)")
    copied = unreal.EditorAssetLibrary.duplicate_asset(source, target)
    if not copied:
        raise RuntimeError(f"Could not copy {source} to {target}")
    return copied


def landscape_textures():
    """Each layer's base colour, normal and ORM textures, by layer name, copied from /Game/Surfaces."""
    textures = {}
    for name, surface, _, _, _ in LANDSCAPE_LAYERS:
        textures[name] = {kind: copy_asset(f"{SURFACES_DIR}/{surface}/T_{surface}_{kind}",
                                           f"{LANDSCAPE_TEXTURE_DIR}/T_{surface}_{kind}") for kind in ("BC", "N", "ORM")}
    return textures


def create_layer_infos():
    """The 7 paint layers' layer infos with their physical materials, in LANDSCAPE_LAYERS' order. Does not save."""
    layers = []
    for name, _, _, physical, _ in LANDSCAPE_LAYERS:
        material = unreal.load_asset(f"{PHYSICAL_DIR}/{physical}")
        if not material:
            raise RuntimeError(f"Could not load {PHYSICAL_DIR}/{physical} (setup_testmap.py makes it)")
        layer = unreal.RTSEditorScripting.create_landscape_layer_info(f"{LANDSCAPE_LAYER_DIR}/LI_{name}", name, material)
        if not layer:
            raise RuntimeError(f"Could not make the layer info of {name}")
        layers.append(layer)
    return layers


def material_node(material, kind, x, y):
    return editing.create_material_expression(material, kind, x, y)


def texture_sample(material, texture, uv, sampler_type, x, y):
    """A sample of texture at uv through the shared wrap sampler (03 section 5: a landscape material soon reaches the 16
    samplers otherwise)."""
    sample = material_node(material, unreal.MaterialExpressionTextureSample, x, y)
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", sampler_type)
    sample.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    editing.connect_material_expressions(uv, "", sample, "UVs")
    return sample


def world_uv(material, world_xy, tile_m, x, y):
    """World XY over tile_m metres: a texture's UVs at its real size, from above."""
    size = material_node(material, unreal.MaterialExpressionConstant, x - 200, y + 80)
    size.set_editor_property("r", tile_m * 100.0)
    uv = material_node(material, unreal.MaterialExpressionDivide, x, y)
    editing.connect_material_expressions(world_xy, "", uv, "A")
    editing.connect_material_expressions(size, "", uv, "B")
    return uv


def layer_blend(material, name, x, y):
    """A height-blended landscape layer blend of the 7 layers."""
    blend = material_node(material, unreal.MaterialExpressionLandscapeLayerBlend, x, y)
    inputs = []
    for layer in LANDSCAPE_LAYER_NAMES:
        entry = unreal.LayerBlendInput()
        entry.set_editor_property("layer_name", layer)
        entry.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_HEIGHT_BLEND)
        entry.set_editor_property("preview_weight", 1.0 if layer == "Grass" else 0.0)
        inputs.append(entry)
    blend.set_editor_property("layers", inputs)
    blend.set_editor_property("desc", name)
    return blend


def connect_layer(material, source, output, blend, layer):
    if not editing.connect_material_expressions(source, output, blend, f"Layer {layer}"):
        raise RuntimeError(f"{LANDSCAPE_MASTER}: could not connect the layer input of {layer}")


def build_landscape_material(textures, virtual_texture):
    """M_Landscape_WD_Temperate (03 section 5) and its instance, which the landscape draws in. Into virtual_texture, the
    landscape's runtime virtual texture, it writes the 7 layers height-blended (each layer's ORM ambient occlusion as its
    height) at their real size from above, each base colour mixed with its own far tile and shaded by the macro mask;
    the roads draw over that. In the main pass it only reads the texture, so the layers cost nothing per frame once a
    page is drawn. It also places the grass of Grass and Meadow. Does not save; returns the instance."""
    # Made anew each time: rebuilt in place, the landscape's material kept a stale texture list and failed to compile.
    for name in (LANDSCAPE_INSTANCE, LANDSCAPE_MASTER):
        if unreal.EditorAssetLibrary.does_asset_exist(f"{LANDSCAPE_DIR}/{name}"):
            if not unreal.EditorAssetLibrary.delete_asset(f"{LANDSCAPE_DIR}/{name}"):
                raise RuntimeError(f"Could not delete {LANDSCAPE_DIR}/{name} to make it anew (LFS lock it)")
    material = asset_tools.create_asset(LANDSCAPE_MASTER, LANDSCAPE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    # The virtual texture holds the world-space normal.
    material.set_editor_property("tangent_space_normal", False)

    world = material_node(material, unreal.MaterialExpressionWorldPosition, -4200, 0)
    world_xy = material_node(material, unreal.MaterialExpressionComponentMask, -4000, 0)
    world_xy.set_editor_property("r", True)
    world_xy.set_editor_property("g", True)
    editing.connect_material_expressions(world, "", world_xy, "")

    colors = layer_blend(material, "Base colour", -800, -600)
    normals = layer_blend(material, "Normal", -800, 0)
    roughness = layer_blend(material, "Roughness", -800, 600)
    far_mix = material_node(material, unreal.MaterialExpressionConstant, -2600, -900)
    far_mix.set_editor_property("r", LANDSCAPE_FAR_MIX)
    for index, (name, _, tile_m, _, tint_default) in enumerate(LANDSCAPE_LAYERS):
        y = -2400 + index * 700
        near = world_uv(material, world_xy, tile_m, -3600, y)
        far = world_uv(material, world_xy, tile_m * LANDSCAPE_FAR_SCALE, -3600, y + 250)
        layer = textures[name]
        color_near = texture_sample(material, layer["BC"], near, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -3200, y)
        color_far = texture_sample(material, layer["BC"], far, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -3200, y + 200)
        normal = texture_sample(material, layer["N"], near, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -3200, y + 400)
        masks = texture_sample(material, layer["ORM"], near, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -3200, y + 600)
        mixed = material_node(material, unreal.MaterialExpressionLinearInterpolate, -2800, y)
        editing.connect_material_expressions(color_near, "RGB", mixed, "A")
        editing.connect_material_expressions(color_far, "RGB", mixed, "B")
        editing.connect_material_expressions(far_mix, "", mixed, "Alpha")
        tint = material_node(material, unreal.MaterialExpressionVectorParameter, -2800, y + 150)
        tint.set_editor_property("parameter_name", f"{name}Tint")
        tint.set_editor_property("group", "Layers")
        tint.set_editor_property("default_value", unreal.LinearColor(*tint_default, 1.0))
        tinted = material_node(material, unreal.MaterialExpressionMultiply, -2500, y)
        editing.connect_material_expressions(mixed, "", tinted, "A")
        editing.connect_material_expressions(tint, "", tinted, "B")
        connect_layer(material, tinted, "", colors, name)
        connect_layer(material, normal, "RGB", normals, name)
        connect_layer(material, masks, "G", roughness, name)
        for blend in (colors, normals, roughness):
            if not editing.connect_material_expressions(masks, "R", blend, f"Height {name}"):
                raise RuntimeError(f"{LANDSCAPE_MASTER}: could not connect the height input of {name}")
    macro_texture = textures["Meadow"]["BC"]
    macro = None
    for index, tiles in enumerate(LANDSCAPE_MACRO_TILES):
        uv = world_uv(material, world_xy, tiles, -2400, -1800 + index * 250)
        sample = texture_sample(material, macro_texture, uv, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -2100,
                                -1800 + index * 250)
        if macro is None:
            macro = sample
            continue
        summed = material_node(material, unreal.MaterialExpressionAdd, -1800, -1700)
        editing.connect_material_expressions(macro, "G", summed, "A")
        editing.connect_material_expressions(sample, "G", summed, "B")
        macro = summed
    stretch = material_node(material, unreal.MaterialExpressionMultiply, -1650, -1700)
    stretch.set_editor_property("const_b", MACRO_GAIN)
    editing.connect_material_expressions(macro, "", stretch, "A")
    shifted = material_node(material, unreal.MaterialExpressionSubtract, -1550, -1700)
    shifted.set_editor_property("const_b", MACRO_BIAS)
    editing.connect_material_expressions(stretch, "", shifted, "A")
    clamped = material_node(material, unreal.MaterialExpressionSaturate, -1450, -1700)
    editing.connect_material_expressions(shifted, "", clamped, "")
    shade = material_node(material, unreal.MaterialExpressionLinearInterpolate, -1300, -1700)
    for pin, value, parameter in (("A", LANDSCAPE_MACRO[0], "MacroLow"), ("B", LANDSCAPE_MACRO[1], "MacroHigh")):
        bound = material_node(material, unreal.MaterialExpressionScalarParameter, -1450, -1900 if pin == "A" else -1800)
        bound.set_editor_property("parameter_name", parameter)
        bound.set_editor_property("group", "Macro")
        bound.set_editor_property("default_value", value)
        editing.connect_material_expressions(bound, "", shade, pin)
    editing.connect_material_expressions(clamped, "", shade, "Alpha")
    shaded = material_node(material, unreal.MaterialExpressionMultiply, -500, -600)
    editing.connect_material_expressions(colors, "", shaded, "A")
    editing.connect_material_expressions(shade, "", shaded, "B")
    world_normal = material_node(material, unreal.MaterialExpressionTransform, -500, 0)
    world_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
    world_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    editing.connect_material_expressions(normals, "", world_normal, "")

    output = material_node(material, unreal.MaterialExpressionRuntimeVirtualTextureOutput, -200, -300)
    editing.connect_material_expressions(shaded, "", output, "BaseColor")
    editing.connect_material_expressions(world_normal, "", output, "Normal")
    editing.connect_material_expressions(roughness, "", output, "Roughness")
    sample = material_node(material, unreal.MaterialExpressionRuntimeVirtualTextureSample, -200, 300)
    sample.set_editor_property("virtual_texture", virtual_texture)
    sample.set_editor_property("material_type", unreal.RuntimeVirtualTextureMaterialType.BASE_COLOR_NORMAL_ROUGHNESS)
    editing.connect_material_property(sample, "BaseColor", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(sample, "Normal", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(sample, "Roughness", unreal.MaterialProperty.MP_ROUGHNESS)

    grass = material_node(material, unreal.MaterialExpressionLandscapeGrassOutput, -200, 900)
    grass_inputs = []
    for layer, (grass_name, *_rest) in LANDSCAPE_GRASS.items():
        entry = unreal.GrassInput()
        entry.set_editor_property("name", layer)
        entry.set_editor_property("grass_type", unreal.load_asset(f"{LANDSCAPE_GRASS_DIR}/{grass_name}"))
        grass_inputs.append(entry)
    grass.set_editor_property("grass_types", grass_inputs)
    for index, layer in enumerate(LANDSCAPE_GRASS):
        weight = material_node(material, unreal.MaterialExpressionLandscapeLayerSample, -500, 900 + index * 150)
        weight.set_editor_property("parameter_name", layer)
        if not editing.connect_material_expressions(weight, "", grass, layer):
            raise RuntimeError(f"{LANDSCAPE_MASTER}: could not connect the grass of {layer}")
    editing.recompile_material(material)

    made = instance(LANDSCAPE_DIR, LANDSCAPE_INSTANCE, material)
    for name, _, _, _, tint_default in LANDSCAPE_LAYERS:
        editing.set_material_instance_vector_parameter_value(made, f"{name}Tint", unreal.LinearColor(*tint_default, 1.0))
    editing.update_material_instance(made)
    return made


def build_grass_master(defaults):
    """M_LandscapeGrass: masked, two-sided foliage (as /Game/PolyHaven's M_PolyHavenFoliage), base colour, normal, the
    ARM's AO and roughness and the alpha map's red as the mask, for instanced grass. defaults are textures by kind."""
    path = f"{LANDSCAPE_GRASS_DIR}/{GRASS_MASTER}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
    else:
        material = asset_tools.create_asset(GRASS_MASTER, LANDSCAPE_GRASS_DIR, unreal.Material, unreal.MaterialFactoryNew())
    editing.delete_all_material_expressions(material)
    samplers = {}
    for index, (parameter, kind, sampler_type) in enumerate((
            ("BaseColor", "BC", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
            ("Normal", "N", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
            ("ARM", "ARM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS),
            ("Alpha", "A", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS))):
        sample = material_node(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, -300 + 350 * index)
        sample.set_editor_property("parameter_name", parameter)
        sample.set_editor_property("texture", defaults[kind])
        sample.set_editor_property("sampler_type", sampler_type)
        samplers[kind] = sample
    editing.connect_material_property(samplers["BC"], "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(samplers["BC"], "RGB", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    editing.connect_material_property(samplers["N"], "RGB", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(samplers["ARM"], "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    editing.connect_material_property(samplers["ARM"], "G", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.connect_material_property(samplers["A"], "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    # The Poly Haven grass meshes are Nanite.
    for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE):
        editing.set_material_usage(material, usage)
        if not editing.has_material_usage(material, usage):
            raise RuntimeError(f"{GRASS_MASTER}: could not set the usage {usage}")
    editing.recompile_material(material)
    return material


def per_platform(kind, value):
    made = kind()
    made.set_editor_property("default", value)
    return made


def build_grass_types():
    """LANDSCAPE_GRASS's grass types, their meshes, textures and instances copied out of /Game/PolyHaven, each mesh
    scaled to its type's height range. Does not save."""
    master = None
    for layer, (grass_name, model, meshes, color, density, heights) in LANDSCAPE_GRASS.items():
        source = f"{POLYHAVEN_DIR}/{model}"
        textures = {kind: copy_asset(f"{source}/T_{model}_{suffix}", f"{LANDSCAPE_GRASS_DIR}/T_{model}_{suffix}")
                    for kind, suffix in (("BC", color), ("N", "N"), ("ARM", "ARM"), ("A", "A"))}
        if master is None:
            master = build_grass_master(textures)
        made = instance(LANDSCAPE_GRASS_DIR, f"MI_{grass_name}", master)
        for parameter, kind in (("BaseColor", "BC"), ("Normal", "N"), ("ARM", "ARM"), ("Alpha", "A")):
            editing.set_material_instance_texture_parameter_value(made, parameter, textures[kind])
        editing.update_material_instance(made)

        path = f"{LANDSCAPE_GRASS_DIR}/{grass_name}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            grass = unreal.load_asset(path)
        else:
            grass = asset_tools.create_asset(grass_name, LANDSCAPE_GRASS_DIR, unreal.LandscapeGrassType,
                                             unreal.LandscapeGrassTypeFactory())
        varieties = []
        for suffix in meshes:
            mesh = copy_asset(f"{source}/SM_{model}_{suffix}", f"{LANDSCAPE_GRASS_DIR}/SM_{model}_{suffix}")
            for index in range(len(mesh.static_materials)):
                mesh.set_material(index, made)
            box = mesh.get_bounding_box()
            tall = max(box.max.z - box.min.z, 1.0)
            variety = unreal.GrassVariety()
            variety.set_editor_property("grass_mesh", mesh)
            variety.set_editor_property("grass_density", per_platform(unreal.PerPlatformFloat, density / len(meshes)))
            variety.set_editor_property("start_cull_distance", per_platform(unreal.PerPlatformInt, int(GRASS_CULL[0] * 100.0)))
            variety.set_editor_property("end_cull_distance", per_platform(unreal.PerPlatformInt, int(GRASS_CULL[1] * 100.0)))
            variety.set_editor_property("scaling", unreal.GrassScaling.UNIFORM)
            variety.set_editor_property("scale_x", unreal.FloatInterval(heights[0] * 100.0 / tall, heights[1] * 100.0 / tall))
            variety.set_editor_property("random_rotation", True)
            variety.set_editor_property("align_to_surface", True)
            variety.set_editor_property("cast_dynamic_shadow", False)
            variety.set_editor_property("cast_contact_shadow", False)
            varieties.append(variety)
        grass.set_editor_property("grass_varieties", varieties)


def create_roads(world, materials, texture):
    """Both sides' roads as landscape splines that shape the ground to them (#383) and paint their layer (ROAD_LAYERS,
    #156), in materials drawn into texture only, then applied to the landscape. Needs a renderer."""
    check_roads()
    mesh = unreal.load_asset(ROAD_MESH)
    segments = 0
    for name, kind, waypoints in WEST_ROADS + tuple(ramp_roads()):
        width, falloff, slot = ROAD_KINDS[kind]
        # The ramps keep their ground: a road's falloff would soften their steep sides into walkable slopes.
        shape = not name.startswith("Ramp")
        path = waypoints if not shape else road_path(waypoints)
        for flip in (False, True):
            points = [rotate(point) if flip else point for point in path]
            added = unreal.RTSEditorScripting.add_landscape_spline_road(
                world, [cm(point, height(*point)) for point in points], width * 100.0, falloff * 100.0, shape, mesh,
                ROAD_TILE * 100.0, materials[slot], texture, ROAD_LAYERS[kind])
            if added == 0:
                raise RuntimeError(f"Could not lay road {name} (AddLandscapeSplineRoad in the log says why)")
            segments += added
    if not unreal.RTSEditorScripting.apply_landscape_splines(world):
        raise RuntimeError("The road splines did not shape the landscape (ApplyLandscapeSplines in the log says why)")
    unreal.log(f"rivercrossing_greybox: {segments} road segments")


def read_bridge_records():
    with open(os.path.join(BRIDGE_SOURCE_DIR, "bridge.json"), encoding="utf-8") as file:
        return json.load(file)


def build_bridge_assets():
    """M_Bridge, its two instances and the bridge's meshes from Art/Shared/bridge (Nanite, their UCX_ collision kept),
    checked against bridge.json. Does not save; returns the meshes by name."""
    records = read_bridge_records()["meshes"]
    master = build_bridge_master()
    slots = {}
    for slot, (name, color, roughness) in BRIDGE_SLOTS.items():
        made = instance(BRIDGE_DIR, name, master)
        editing.set_material_instance_vector_parameter_value(made, "Color", unreal.LinearColor(*color, 1.0))
        editing.set_material_instance_scalar_parameter_value(made, "Roughness", roughness)
        editing.update_material_instance(made)
        slots[slot] = made
    return {name: import_bridge_mesh(os.path.join(BRIDGE_SOURCE_DIR, records[name]["fbx"]), name, records[name], slots)
            for name in BRIDGE_MESHES}


def build_bridge_master():
    path = f"{BRIDGE_DIR}/{BRIDGE_MASTER}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
    else:
        material = asset_tools.create_asset(BRIDGE_MASTER, BRIDGE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    editing.delete_all_material_expressions(material)
    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(*BRIDGE_SLOTS["Concrete"][1], 1.0))
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 300)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", BRIDGE_SLOTS["Concrete"][2])
    # Gradient noise over world space, 0 to 1 and clamped (its own output range is not a bound), shading the colour from
    # BRIDGE_VARIATION's low to its high: weathering a few metres across.
    noise = editing.create_material_expression(material, unreal.MaterialExpressionNoise, -1000, 150)
    noise.set_editor_property("scale", 1.0 / BRIDGE_VARIATION_SCALE)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)
    noise.set_editor_property("turbulence", False)
    noise.set_editor_property("levels", 3)
    clamped = editing.create_material_expression(material, unreal.MaterialExpressionSaturate, -850, 150)
    editing.connect_material_expressions(noise, "", clamped, "")
    low = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -850, 250)
    low.set_editor_property("r", BRIDGE_VARIATION[0])
    high = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -850, 330)
    high.set_editor_property("r", BRIDGE_VARIATION[1])
    variation = editing.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -600, 150)
    editing.connect_material_expressions(low, "", variation, "A")
    editing.connect_material_expressions(high, "", variation, "B")
    editing.connect_material_expressions(clamped, "", variation, "Alpha")
    shade = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -350, 0)
    editing.connect_material_expressions(color, "", shade, "A")
    editing.connect_material_expressions(variation, "", shade, "B")
    editing.connect_material_property(shade, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    material.set_editor_property("phys_material", unreal.load_asset(BRIDGE_PHYSICAL_MATERIAL))
    # Its result is not whether the flag is set (building_art.allow_nanite); ask afterwards.
    editing.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE)
    if not editing.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE):
        raise RuntimeError(f"{BRIDGE_MASTER}: could not set the Nanite usage")
    editing.recompile_material(material)
    return material


def import_bridge_mesh(source, name, record, slots, directory=BRIDGE_DIR, nanite=True):
    """One bridge part's FBX as a static mesh in directory, Nanite unless nanite is off, with its UCX_ boxes as its
    collision, the slots bound by name; the village set (#408) imports the same way, its greybox without Nanite."""
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    common = pipeline.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    meshes = pipeline.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    meshes.set_editor_property("import_skeletal_meshes", False)
    meshes.set_editor_property("collision", True)
    meshes.set_editor_property("import_collision_according_to_mesh_name", True)
    meshes.set_editor_property("build_nanite", nanite)
    materials = pipeline.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    parameters = unreal.ImportAssetParameters()
    parameters.is_automated = True
    parameters.replace_existing = True
    parameters.destination_name = name
    parameters.override_pipelines.append(unreal.SoftObjectPath(pipeline.get_path_name()))
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    manager.import_asset(directory, unreal.InterchangeManager.create_source_data(source), parameters)
    mesh = unreal.load_asset(f"{directory}/{name}")
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Could not import {source} as a static mesh")

    box = mesh.get_bounding_box()
    size, wanted = box.max.x - box.min.x, record["bounds"][1][0] - record["bounds"][0][0]
    if abs(size - wanted) > 2.0:
        raise RuntimeError(f"{source} imported {size:.1f} cm long instead of {wanted:.1f}: check the FBX units")
    if mesh.get_num_triangles(0) < record["triangles"]:
        raise RuntimeError(f"{source} imported {mesh.get_num_triangles(0)} triangles instead of {record['triangles']}")
    # Each UCX_ box comes in as a convex hull.
    shapes = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    collision = len(shapes.get_editor_property("convex_elems")) + len(shapes.get_editor_property("box_elems"))
    if collision != record["collision_boxes"]:
        raise RuntimeError(f"{source} imported {collision} collision shapes, not its {record['collision_boxes']} UCX_ boxes")
    for index, slot in enumerate(mesh.static_materials):
        slot_name = str(slot.material_slot_name)
        if slot_name not in slots:
            raise RuntimeError(f"{source}: no material for slot {slot_name}")
        mesh.set_material(index, slots[slot_name])
    settings = mesh.get_editor_property("nanite_settings")
    if settings.enabled != nanite:
        settings.enabled = nanite
        mesh.set_editor_property("nanite_settings", settings)
    # Traces and the navmesh use the boxes, never the render triangles.
    mesh.get_editor_property("body_setup").set_editor_property("collision_trace_flag",
                                                               unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    unreal.log(f"rivercrossing_greybox: {name} {mesh.get_num_triangles(0)} triangles, {collision} collision boxes, "
               f"Nanite {'on' if nanite else 'off'}")
    return mesh


def spawn_bridges(meshes):
    """Each bridge's spans, piers and abutments along x across the river, labelled Bridge_<North|South>_<part>, every part
    world-static and blocking all, so ground traces (RTSGround) and the navmesh find the deck. The piers' tops and the
    abutments' seats carry the girders' feet, deck_depth_cm under the road. The spans are ARTSCivilBuilding actors of
    DA_Civil_BridgeSpan (#444): shot down, a span's rubble leaves a gap the navmesh drops; piers and abutments stay."""
    half = BRIDGE_LENGTH / 2.0
    deck_depth = read_bridge_records()["deck_depth_cm"] / 100.0
    path = f"{CIVIL_DIR}/DA_Civil_BridgeSpan"
    span_data = unreal.load_asset(path)
    if not span_data:
        raise RuntimeError(f"{path} is missing: run ue.ps1 -Task setup first (#408)")
    for name, bridge_y in (("North", BRIDGE_Y), ("South", -BRIDGE_Y)):
        for span in range(BRIDGE_SPANS):
            x = -half + BRIDGE_SPAN * (span + 0.5)
            building = spawn(unreal.RTSCivilBuilding, cm((x, bridge_y), BRIDGE_DECK_TOP), 0.0, f"Bridge_{name}_Span{span + 1}")
            building.set_editor_property("data", span_data)
        parts = [(f"Pier{pier}", "SM_Bridge_Pier", -half + BRIDGE_SPAN * pier, BRIDGE_DECK_TOP - deck_depth, 0.0)
                 for pier in range(1, BRIDGE_SPANS)]
        parts += [("AbutmentW", "SM_Bridge_Abutment", -half, BRIDGE_DECK_TOP, 180.0),
                  ("AbutmentE", "SM_Bridge_Abutment", half, BRIDGE_DECK_TOP, 0.0)]
        for part, mesh, x, z, yaw in parts:
            actor = spawn(unreal.StaticMeshActor, cm((x, bridge_y), z), yaw, f"Bridge_{name}_{part}")
            component = actor.static_mesh_component
            component.set_static_mesh(meshes[mesh])
            component.set_collision_profile_name("BlockAll")


def spawn_village():
    """Both banks' buildings (VILLAGE) as ARTSCivilBuilding actors of their DA_Civil_<Type>, labelled Village_<bank><nn>,
    the garrison buildings tagged GARRISON_TAG. Each stands on its footprint's lowest corner, its plinth (half a metre
    under the mesh's origin) closing the gap on a slope, under a null nav area of its footprint and height, so Recast
    leaves no island on its roof."""
    types = read_village()
    for bank, index, kind, centre, yaw, box, garrison in village_buildings(types):
        path = f"{CIVIL_DIR}/DA_Civil_{kind}"
        data = unreal.load_asset(path)
        if not data:
            raise RuntimeError(f"{path} is missing: run ue.ps1 -Task setup first (#408)")
        base = min(height(*corner) for corner in box_corners(box))
        label = f"Village_{bank}{index + 1:02d}"
        building = spawn(unreal.RTSCivilBuilding, cm(centre, base), yaw, label)
        building.set_editor_property("data", data)
        if garrison:
            building.set_editor_property("tags", [unreal.Name(GARRISON_TAG)])
        tall = types[kind]["high"][2] + 1.0
        spawn_nav_null(f"{label}_NavNull", ((box[0] + box[1]) / 2.0, (box[2] + box[3]) / 2.0, base + tall / 2.0 - 1.0),
                       (box[1] - box[0], box[3] - box[2], tall + 1.0))


def build_prop_masters(defaults):
    """PROP_MASTERS, the Poly Haven props' materials (as /Game/PolyHaven's M_PolyHavenProp and M_PolyHavenPropMasked, which
    are not cooked): base colour, normal and the ARM's AO, roughness and metalness; the masked one is two-sided and masked
    by the alpha map's red. defaults are textures by kind, for the samplers' types. Returns them by masked."""
    masters = {}
    for masked, name in PROP_MASTERS.items():
        path = f"{PROPS_DIR}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            material = unreal.load_asset(path)
        else:
            material = asset_tools.create_asset(name, PROPS_DIR, unreal.Material, unreal.MaterialFactoryNew())
        editing.delete_all_material_expressions(material)
        samplers = {}
        kinds = (("BaseColor", "BC", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                 ("Normal", "N", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                 ("ARM", "ARM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS))
        if masked:
            kinds += (("Alpha", "A", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS),)
        for index, (parameter, kind, sampler_type) in enumerate(kinds):
            sample = material_node(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, -300 + 350 * index)
            sample.set_editor_property("parameter_name", parameter)
            sample.set_editor_property("texture", defaults[kind])
            sample.set_editor_property("sampler_type", sampler_type)
            samplers[kind] = sample
        editing.connect_material_property(samplers["BC"], "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        editing.connect_material_property(samplers["N"], "RGB", unreal.MaterialProperty.MP_NORMAL)
        editing.connect_material_property(samplers["ARM"], "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        editing.connect_material_property(samplers["ARM"], "G", unreal.MaterialProperty.MP_ROUGHNESS)
        editing.connect_material_property(samplers["ARM"], "B", unreal.MaterialProperty.MP_METALLIC)
        if masked:
            editing.connect_material_property(samplers["A"], "R", unreal.MaterialProperty.MP_OPACITY_MASK)
            material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
            material.set_editor_property("two_sided", True)
        # The Poly Haven meshes are Nanite, drawn as instances.
        for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE):
            editing.set_material_usage(material, usage)
            if not editing.has_material_usage(material, usage):
                raise RuntimeError(f"{name}: could not set the usage {usage}")
        editing.recompile_material(material)
        masters[masked] = material
    return masters


def build_prop_assets():
    """POLYHAVEN_PROPS' meshes copied from /Game/PolyHaven into PROPS_DIR, each slot's instance remade there under
    PROP_MASTERS with its textures copied too, so nothing of the level points into the never-cooked folder. Does not save;
    returns the meshes by prop."""
    sources = {}
    for prop, (model, wanted, _) in POLYHAVEN_PROPS.items():
        folder = f"{POLYHAVEN_DIR}/{model}"
        names = sorted(path.split(".")[0].rsplit("/", 1)[1] for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=False))
        meshes = [name for name in names if any(name.startswith(w) if w.endswith("_") else name == w for w in wanted)]
        if not meshes:
            raise RuntimeError(f"No {wanted} in {folder}: run ue.ps1 -Task freeassets (#409)")
        sources[prop] = (model, meshes)

    def texture(source, parameter):
        if source is None:
            return None
        found = editing.get_material_instance_texture_parameter_value(source, parameter)
        if not found or found.get_path_name().startswith(PROPS_DIR):
            return found
        return copy_asset(found.get_path_name().split(".")[0], f"{PROPS_DIR}/{found.get_name()}")

    # The slots' instances by name, with their textures, from the source meshes (or a copy made before).
    slots = {}
    for prop, (model, meshes) in sources.items():
        for name in meshes:
            source = unreal.load_asset(f"{POLYHAVEN_DIR}/{model}/{name}")
            for slot in source.static_materials:
                interface = slot.material_interface
                if interface and interface.get_name() not in slots:
                    slots[interface.get_name()] = {parameter: texture(interface, parameter)
                                                   for parameter in ("BaseColor", "Normal", "ARM", "Alpha")}
    defaults = {}
    for textures in slots.values():
        for parameter, kind in (("BaseColor", "BC"), ("Normal", "N"), ("ARM", "ARM"), ("Alpha", "A")):
            if textures[parameter] and kind not in defaults:
                defaults[kind] = textures[parameter]
    masters = build_prop_masters(defaults)
    made = {}
    for name, textures in slots.items():
        made[name] = instance(PROPS_DIR, name, masters[textures["Alpha"] is not None])
        for parameter, value in textures.items():
            if value:
                editing.set_material_instance_texture_parameter_value(made[name], parameter, value)
        editing.update_material_instance(made[name])

    props = {}
    for prop, (model, meshes) in sources.items():
        props[prop] = []
        for name in meshes:
            source = unreal.load_asset(f"{POLYHAVEN_DIR}/{model}/{name}")
            mesh = copy_asset(f"{POLYHAVEN_DIR}/{model}/{name}", f"{PROPS_DIR}/{name}")
            for index, slot in enumerate(source.static_materials):
                if slot.material_interface:
                    mesh.set_material(index, made[slot.material_interface.get_name()])
            props[prop].append(mesh)
    return props


def prop_transform(point, z, yaw, offset=(0.0, 0.0), length=1.0):
    """A dressing instance at point (metres, z the ground) turned yaw, its meshes' origin offset cm in its own frame, and
    stretched length times along its X."""
    turn = math.radians(yaw)
    along = offset[0] * length
    shift = (along * math.cos(turn) - offset[1] * math.sin(turn), along * math.sin(turn) + offset[1] * math.cos(turn))
    location = unreal.Vector(point[0] * 100.0 + shift[0], point[1] * 100.0 + shift[1], z * 100.0)
    return unreal.Transform(location, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), unreal.Vector(length, 1.0, 1.0))


def fence_bays(points, bay):
    """A fence along the polyline points: (start, yaw, stretch) of each bay, a whole number of bays a leg stretched to fit
    it, and the corners and both ends, where the posts close the run."""
    bays = []
    for start, end in zip(points, points[1:]):
        length = math.dist(start, end)
        count = max(1, round(length / bay))
        yaw = math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))
        for step in range(count):
            at = (start[0] + (end[0] - start[0]) * step / count, start[1] + (end[1] - start[1]) * step / count)
            bays.append((at, yaw, length / count / bay))
    return bays, list(points)


def village_props(bank, buildings):
    """The dressing of one bank, in world metres: {kind: [(point, yaw, stretch)]} for the kinds spawn_props lays, and the
    cables as (start, end) pairs (x, y, z). Raises when a prop stands in a building, in the river or on a supply source."""
    flip = (lambda point: rotate(point)) if bank == "E" else (lambda point: point)
    boxes = [box for other_bank, _, _, _, _, box, _ in buildings if other_bank == bank]
    props = {kind: [] for kind in ("Lamp", "Pole", "PostRail", "PostRailEnd", "FieldWire", "FieldWireEnd", "ChainLink",
                                   "ChainLinkPost")}
    props.update({f"Sign_{sign}": [] for sign, _, _, _ in VILLAGE_SIGNS})
    turn = 180.0 if bank == "E" else 0.0

    def add(kind, point, yaw, stretch=1.0):
        props[kind].append((flip(point), yaw + turn, stretch))

    def lay(kinds, points, bay):
        bays, posts = fence_bays(points, bay)
        for at, yaw, stretch in bays:
            add(kinds[0], at, yaw, stretch)
        for post in posts:
            add(kinds[1], post, 0.0)

    steps = int((VILLAGE_STREET_SPAN[1] - VILLAGE_STREET_SPAN[0]) / VILLAGE_LAMP_SPACING)
    for step in range(steps + 1):
        add("Lamp", (VILLAGE_STREET_X + VILLAGE_LAMP_SIDE, VILLAGE_STREET_SPAN[0] + VILLAGE_LAMP_SPACING * step), 0.0)
    poles = []
    steps = int((VILLAGE_STREET_SPAN[1] - VILLAGE_STREET_SPAN[0]) / VILLAGE_POLE_SPACING)
    for step in range(steps + 1):
        pole = (VILLAGE_STREET_X + VILLAGE_POLE_SIDE, VILLAGE_STREET_SPAN[0] + VILLAGE_POLE_SPACING * step)
        add("Pole", pole, 0.0)
        poles.append(pole)
    cables = []
    for start, end in zip(poles, poles[1:]):
        for side in (-POLE_ARM, POLE_ARM):
            ends = [flip((point[0] + side, point[1])) for point in (start, end)]
            cables.append(tuple((x, y, height(x, y) + POLE_WIRE_HEIGHT) for x, y in ends))
    for sign, x, y, yaw in VILLAGE_SIGNS:
        add(f"Sign_{sign}", (x, y), yaw)
    for _, _, kind, centre, yaw, box, _ in [b for b in buildings if b[0] == bank and b[2] in GARDEN_TYPES]:
        # The back is the face away from the street, in the west bank's frame.
        west = box if bank == "W" else (-box[1], -box[0], -box[3], -box[2])
        back = west[0] - VILLAGE_GARDEN if (west[0] + west[1]) / 2.0 < VILLAGE_STREET_X else west[1] + VILLAGE_GARDEN
        lay(("PostRail", "PostRailEnd"), ((back, west[2] - VILLAGE_GARDEN), (back, west[3] + VILLAGE_GARDEN)), RAIL_BAY)
    lay(("FieldWire", "FieldWireEnd"), VILLAGE_PASTURE, WIRE_BAY)
    if bank in VILLAGE_YARD:
        lay(("ChainLink", "ChainLinkPost"), VILLAGE_YARD[bank], CHAINLINK_BAY)

    sources = [point for _, point, _ in WEST_SOURCES] + [rotate(point) for _, point, _ in WEST_SOURCES]
    for kind, placed in props.items():
        for point, _, _ in placed:
            spot = (point[0], point[0], point[1], point[1])
            distance, half_width = river_distance(*point)
            if distance < half_width + VILLAGE_RIVER_CLEARANCE:
                raise RuntimeError(f"Village prop {kind} at ({point[0]:.0f}, {point[1]:.0f}) stands by the river")
            for box in boxes:
                if box_gap(box, spot) < VILLAGE_PROP_CLEARANCE:
                    raise RuntimeError(f"Village prop {kind} at ({point[0]:.0f}, {point[1]:.0f}) stands in a building")
            for source in sources:
                if max(abs(point[0] - source[0]), abs(point[1] - source[1])) < SUPPLY_FOOTPRINT[0] / 2.0 + VILLAGE_PROP_CLEARANCE:
                    raise RuntimeError(f"Village prop {kind} at ({point[0]:.0f}, {point[1]:.0f}) stands on a supply source")
    return props, cables


def spawn_props(world, props):
    """Both banks' dressing (village_props) in world, each kind as one Props_<bank>_<kind> actor of instances
    (POLYHAVEN_PROPS' copies in props, the roadside kit's meshes), and the power line's cables as Props_<bank>_Cables."""
    meshes = dict(props)
    for kind, name in (("PostRail", "SM_Fence_PostRail"), ("PostRailEnd", "SM_Fence_PostRail_End"),
                       ("FieldWire", "SM_Fence_FieldWire"), ("FieldWireEnd", "SM_Fence_FieldWire_End")):
        meshes[kind] = [unreal.load_asset(f"{ROADSIDE_DIR}/{name}")]
    for sign, _, _, _ in VILLAGE_SIGNS:
        meshes[f"Sign_{sign}"] = [unreal.load_asset(f"{ROADSIDE_DIR}/SM_Sign_{sign}")]
    cable = unreal.load_asset(CABLE_MESH)
    for kind, found in meshes.items():
        if not found or None in found:
            raise RuntimeError(f"The village's {kind} meshes are missing: run ue.ps1 -Task freeassets (#409)")
    if not cable:
        raise RuntimeError(f"{CABLE_MESH} is missing: run ue.ps1 -Task freeassets (#409)")

    buildings = village_buildings(read_village())
    for bank in VILLAGE:
        placed, cables = village_props(bank, buildings)
        for kind, instances in placed.items():
            if not instances:
                continue
            offset = POLYHAVEN_PROPS[kind][2] if kind in POLYHAVEN_PROPS else (0.0, 0.0)
            transforms = [prop_transform(point, height(*point), yaw, offset, stretch) for point, yaw, stretch in instances]
            if not unreal.RTSEditorScripting.spawn_prop_instances(world, f"Props_{bank}_{kind}", meshes[kind], transforms):
                raise RuntimeError(f"Could not lay the village's {kind} (SpawnPropInstances in the log says why)")
        starts = [unreal.Vector(x * 100.0, y * 100.0, z * 100.0) for (x, y, z), _ in cables]
        ends = [unreal.Vector(x * 100.0, y * 100.0, z * 100.0) for _, (x, y, z) in cables]
        if not unreal.RTSEditorScripting.spawn_cables(world, f"Props_{bank}_Cables", cable, starts, ends, CABLE_SAG):
            raise RuntimeError("Could not string the village's power line (SpawnCables in the log says why)")


def spawn_side(spot, base, yaw):
    start = spawn(unreal.RTSStartSpot, cm(base, height(*base) + 1.0), yaw, f"StartSpot_{spot + 1}")
    start.set_editor_property("spot_index", spot)
    forward = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    behind = (base[0] - BUILDERS_BEHIND * forward[0], base[1] - BUILDERS_BEHIND * forward[1])
    spawner = spawn(unreal.RTSUnitSpawner, cm(behind, height(*behind)), yaw, f"Spawn_Team{spot}_Builders")
    spawner.set_editor_property("unit_data", unreal.load_asset(BUILDER))
    spawner.set_editor_property("count", BUILDERS)
    spawner.set_editor_property("team_id", spot)
    crate = unreal.load_asset(CRATE_MESH)
    for name, west, amount in WEST_SOURCES:
        point = west if spot == 0 else rotate(west)
        source = spawn(unreal.RTSSupplySource, cm(point, height(*point)), label=f"SupplySource_{name}_{spot + 1}")
        source.set_editor_property("amount", amount)
        source.set_editor_property("footprint", unreal.IntPoint(*SUPPLY_FOOTPRINT))
        source.set_editor_property("height", SUPPLY_HEIGHT)
        source.set_editor_property("mesh", crate)


def spawn_lighting():
    """The project's base lighting (setup_testmap.py spawn_lighting) with the afternoon sun of 03 section 10."""
    sun = spawn(unreal.DirectionalLight, cm((0.0, 0.0), 20.0), label="Sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-45.0, yaw=35.0), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("atmosphere_sun_light", True)
    sky = spawn(unreal.SkyLight, cm((0.0, 0.0), 10.0), label="SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, cm((0.0, 0.0)), label="SkyAtmosphere")


def build_greybox(world, heightmap_file, river_material, bridge_meshes, road_materials, props):
    """Fills world, a new empty level, with the greybox: One File Per Actor, the landscape with its paint layers, grass and
    material (#156), the river in river_material
    and its Water_ blockers, the bridges of bridge_meshes (build_bridge_assets), the runtime virtual texture and the roads
    in road_materials (build_road_materials), the village (#157) and its dressing of props (build_prop_assets), both
    sides' start spots, builders and supply sources, the lighting and the navmesh bounds. Does not save."""
    check_layout()
    if not unreal.RTSEditorScripting.use_external_actors(world):
        raise RuntimeError(f"Could not turn on One File Per Actor in {MAP_PATH}")
    heights = write_heightmap(heightmap_file)
    weightmap_files = [f"{os.path.splitext(heightmap_file)[0]}_{name}.r8" for name in LANDSCAPE_LAYER_NAMES]
    write_weightmaps(heights, weightmap_files)
    del heights
    corner = unreal.Vector(-LANDSCAPE_HALF * 100.0, -LANDSCAPE_HALF * 100.0, 0.0)
    landscape = unreal.RTSEditorScripting.create_landscape(world, heightmap_file, COMPONENTS, QUADS_PER_SECTION, corner,
                                                           unreal.Vector(QUAD * 100.0, QUAD * 100.0, 100.0), None,
                                                           create_layer_infos(), weightmap_files)
    if not landscape:
        raise RuntimeError(f"Could not create the landscape of {MAP_PATH}")
    apply_landscape_physics(world)
    # The material reads the runtime virtual texture, which needs the landscape for its bounds.
    texture = create_virtual_texture(world)
    build_grass_types()
    landscape.set_editor_property("landscape_material", build_landscape_material(landscape_textures(), texture))

    create_river(world, river_material)
    spawn_water_blockers()
    spawn_bridges(bridge_meshes)
    create_roads(world, road_materials, texture)
    spawn_village()
    spawn_props(world, props)
    yaw = math.degrees(math.atan2(-WEST_BASE[1], -WEST_BASE[0]))
    spawn_side(0, WEST_BASE, yaw)
    spawn_side(1, rotate(WEST_BASE), yaw + 180.0)
    spawn_lighting()

    # The playable area: units path, the camera pans and the preview shows exactly the 2,000 m square.
    nav = spawn(unreal.NavMeshBoundsVolume, cm((0.0, 0.0), 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(PLAYABLE_HALF, PLAYABLE_HALF, 100.0))
    world.get_world_settings().set_editor_property("default_game_mode", unreal.RTSSkirmishGameMode.static_class())
