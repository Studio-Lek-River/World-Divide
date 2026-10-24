"""Writes Art/USA/buildings/usa_hq/usa_hq.json, the USA HQ layout that setup_testmap.py builds the game's HQ from
(building_art.py), out of the final art delivery's engine records in Art/USA/buildings/usa_hq/recipe/:

- final_meshes.json: every mesh, its material slots and the instance each slot gets, its LOD0 triangles and bounds;
- assembly.json: where the preview level placed each mesh, its mobility, shadow and bounds scale;
- material_instances.json: each instance's parent and parameter values;
- textures.json: each texture's compression, sRGB and build size;
- source_inspection.json: the helpers (spawn, exit, pivots) from the Blender source;
- the door curves (animation/CT_HQ20_DoorMotion.csv) for the closed door the game starts with;
- game/game_meshes.json: the game shell that stands for the meshes that never move (#358);
- Art/Shared/kit/kit_catalogue.json, its build/kit_manifest.json and Art/Shared/flags/flags.json: the kit's flag cloth and
  mast beacon (#351, #389), which replace the delivered flag, and the flag the HQ flies;
- issue379/mast_manifest.json: the communication masts, taken out of the shell as parts of their own (#379);
- issue195/fixtures.json: the lamps and windows of the game shell (Tools/Art/usa_hq_light_probe.py), where the real
  lights (LIGHTS) stand (#195).

The equipment's motion (MOTION), the beacons (BEACONS) and the flag (FLAG_*) are the game's own, set here (#351), as are
the lighting groups (LIGHT_GROUPS) and the real lights (LIGHTS, #195).

The delivered meshes the shell replaces stay listed under delivery_shell, with their parts, for the bench's dense HQ
(setup -BenchHQ).

Run from the repository root with any Python 3: python Tools/Art/usa_hq_recipe.py. The output is committed; rerun it
only when the delivery records change. Everything is in Unreal centimetres, +X the front and garage exit, +Y west.
"""

import csv
import json
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "Art", "USA", "buildings", "usa_hq")
RECIPE = os.path.join(ROOT, "recipe")

# The game shell (#358): the meshes that never move, merged by Tools/Art/usa_hq_game_blender.py into game/SM_HQ_Shell
# (the bulk, the HQ's root part) and SM_HQ_ShellDetail (the small details LOD0 keeps whole), described in
# game/game_meshes.json, each with a hand-made LOD1 setup takes as it is. The bulk is the game mesh baked from the master
# (#373), within the large building's 150,000 / 40,000 triangles with the rest of the HQ.
GAME_MESHES = os.path.join(ROOT, "game", "game_meshes.json")
SHELL = "SM_HQ_Shell"
SHELL_DETAIL = "SM_HQ_ShellDetail"

# The bulk's baked normal and occlusion (#373): two 2K sets on its UV1 (textures/T_HQ_ShellBake_Set<n>_<map>.png), each slot
# drawing from the set issue373/final/texture_set_mapping.json gives it. Its slots get their own instance, SHELL_BAKED
# after the delivered one, with the building masters' ShellBake on and the set's maps, so the moving parts that share the
# delivered instance keep drawing without them. The floors' tiles and the lit interior stay as delivered (SHELL_UNBAKED):
# the occlusion blotched the large floors.
SHELL_BAKE_SETS = os.path.join(ROOT, "issue373", "final", "texture_set_mapping.json")
SHELL_BAKE_TEXTURES = {
    "BakedNormal": ("T_HQ_ShellBake_Set{}_Normal",
                    {"compression": "TC_Normalmap", "srgb": False, "lod_group": "TEXTUREGROUP_WorldNormalMap", "resize": None}),
    "BakedAO": ("T_HQ_ShellBake_Set{}_AO",
                {"compression": "TC_Masks", "srgb": False, "lod_group": "TEXTUREGROUP_World", "resize": None}),
}
SHELL_BAKED = "_Baked"
SHELL_UNBAKED = {"MI_HQ14_Paving", "MI_HQ19_Walkpad", "MI_USA_HQ_HQ20_FloorWear", "MI_USA_HQ_HQ21_PavingWear",
                 "MI_HQ17_InteriorWarm"}

# The other meshes' (LOD0, LOD1) triangle budgets, per placement, reduced in setup from the delivered FBX. The building
# switches to LOD1 at SWITCH_DISTANCE cm from the camera as one (docs/art/MODEL_PERFORMANCE.md, Buildings); meshes under
# 2,000 triangles keep LOD0 only (PERFORMANCE_GUIDELINES 20). The flag stays dense enough for its wind.
BUDGETS = {
    "SM_HQ20_DoorSection_Delivery": (1200, 70),
    "SM_HQ19_Radar_Azimuth": (3000, 400),
    "SM_HQ19_WingDish_Elevation": (3000, 540),
    "SM_HQ20_DriveShaft": (1000, 150),
    "SM_HQ21_NationalFlag_Wind": (4000, 1000),
}
SWITCH_DISTANCE = 26000.0
# The meshes whose LOD1 the engine's reduction breaks get a hand-made one (Tools/Art/usa_hq_lod1_blender.py, #406), its
# triangles their LOD1 budget.
HAND_LOD1 = ("SM_HQ19_WingDish_Elevation",)

# Parts too small to see from the default zoom stop drawing past this distance (cm), beyond their last LOD switch.
SMALL_PART_DRAW_DISTANCE = 12000.0
SMALL_PARTS = ("Fan", "LiftCable", "DoorTopRollers", "DoorBottomSeal", "DriveShaft")

# The equipment that turns on its own (#351, FRTSPartMotion), started from the delivery's animation/equipment_motion.json
# and set by eye: spin in degrees a second and sweep in degrees either way over sweep_period seconds, each as [pitch, yaw,
# roll] about the part's own axes; each HQ runs a part up to jitter faster or slower. The fans each at their own speed
# about the delivery's 240; the radar a turn in 5 s rather than the delivery's 24, so it reads at RTS zoom; the wing dish
# the delivery's 12 degrees of azimuth and 4 of elevation over 24 s, the elevation about its own pivot socket and riding
# on the azimuth.
MOTION = {
    "ER_Cooling_Fan1_Rotor": {"spin": [0.0, 240.0, 0.0], "jitter": 0.1},
    "WF_Condenser_Fan1_Rotor": {"spin": [0.0, 205.0, 0.0], "jitter": 0.1},
    "WM_AHU_Fan1_Rotor": {"spin": [0.0, 280.0, 0.0], "jitter": 0.1},
    "WM_AHU_Fan2_Rotor": {"spin": [0.0, 255.0, 0.0], "jitter": 0.1},
    "Radar_Azimuth": {"spin": [0.0, 72.0, 0.0], "jitter": 0.05},
    "WingDish_Azimuth": {"sweep": [0.0, 12.0, 0.0], "sweep_period": 24.0, "jitter": 0.1},
    "WingDish_Elevation": {"sweep": [4.0, 0.0, 0.0], "sweep_period": 24.0, "jitter": 0.1,
                           "pivot_socket": "WingDishElevationPivot", "follow": "WingDish_Azimuth"},
}

# The shared kit (docs/art/BUILDING_KIT.md, Tools/Art/kit_blender.py): the pieces the HQ mounts, by catalogue id, and the
# flags.
SHARED = os.path.join(ROOT, "..", "..", "..", "Shared")
KIT_CATALOGUE = os.path.join(SHARED, "kit", "kit_catalogue.json")
FLAGS = os.path.join(SHARED, "flags", "flags.json")
BEACON_PIECE = "lighting/12_MastBeaconSingle"
FLAG_PIECE = "identity/01_FlagCloth"

# The communication masts (#379): the core tower's to 28 m and the east wing's roof mast to 24 m, out of the shell as parts
# of their own with a hand-made LOD1, the producer's choice of the delivery's three heights (HANDOFF_379.md). Each stands at
# its foot (the mesh's pivot) unturned, its tip socket (the top of its antenna rod) above the foot. Each tip carries a red
# obstruction beacon, the kit's BEACON_PIECE, mounted on it at BEACON_SCALE, grown with the masts.
MAST_MANIFEST = os.path.join(ROOT, "issue379", "mast_manifest.json")
MAST_HEIGHT = "28"
MASTS = {"MastCore": "MastTipCore", "MastEast": "MastTipEast"}
BEACONS = {"Beacon_MastTipCore": "MastTipCore", "Beacon_MastTipEast": "MastTipEast"}
BEACON_SCALE = 1.75

# The flag (#351): the HQ flies FLAG_TYPE (flags.json) on the kit's FLAG_PIECE at the courtyard pole, the top of its hoist at
# FLAG_HOIST. The pole (13 cm across, 20 cm deep at its fittings, its top at 7.1 m) stands at X 1150, Y 0; the cloth
# flies along +Y (yaw 90) from just outside it, so it never passes through the pole, its top 20 cm under the pole's.
# FLAG_HEIGHT cm of hoist; the width is that times the flag's aspect, 4.18 m for the USA's 10:19. Its wind stops where the
# kit's catalogue says (wpo_disable_distance).
FLAG_TYPE = "USA"
FLAG_PART = "NationalFlag"
FLAG_HOIST = [1150.0, 10.5, 690.0]
FLAG_HEIGHT = 220.0
# The delivered flag and the master of its colour instances, which the shared cloth replaces.
DELIVERED_FLAG = "SM_HQ21_NationalFlag_Wind"
DELIVERED_FLAG_MASTER = "M_HQ21_FlagWind"

# The lighting groups (#195): the glow of each instance of M_HQ_Final_Operational belongs to one (building_art's
# LIGHT_GROUP_INDICES, switched by ARTSBuilding::SetLightGroupOn) and rises in the dark to NightGain times its day glow.
# The warm glass glows with the rooms lit behind it at night (M_HQ17_Glass's NightGlow, linear RGB); the cool glass stays
# dark, as the rooms that are not lit.
GLASS_NIGHT_GLOW = {"MI_HQ17_WarmGlass": [0.9, 0.5, 0.16, 1.0]}

LIGHT_GROUPS = {
    "MI_HQ17_InnerLight": ("Windows", 4.0),
    "MI_HQ17_InteriorWarm": ("Windows", 4.0),
    "MI_HQ20_FixtureWarm": ("Fixtures", 4.0),
    "MI_HQ20_FixtureBlue": ("Accents", 3.0),
    "MI_HQ17_TeamLight": ("Accents", 3.0),
}

# The real lights (#195), each only at lamps or windows of the model: its fixtures, islands of fixtures.json, all of one
# slot. The light stands at the middle of their bounds, a Rect as wide as they are across its Y and as high along its Z;
# "front" puts it on the outermost face along its X instead (a window band, so it lights what is outside). Rotation
# [pitch, yaw, roll] turns its X, the way it shines. Candelas per light; none casts a shadow.
# - The garage's eight ceiling-edge lamps (two rows of four along the bay's side walls, 6.16 m up), a strip light per
#   row, shining down onto the bay floor and out of the open door onto the apron. Tilted 65 degrees down into the bay, with
#   square barn doors keeping the light within 25 degrees of that across the strip, so none of it falls past the wall
#   behind the lamp: the lights cast no shadows, so a wall does not stop them. For the same reason they are on only while
#   the door is not closed (GarageLamps, URTSGarageComponent); the lamps' glow stays.
# - The command entrance's door and its two flanking windows, each its own soft light on its own glass, as the rooms behind
#   them light the ground: dim, short and tilted a little down, barn doors half closed, so each lays a small warm pool in
#   front of its opening rather than one floodlight across the courtyard. +Y is west.
# The façade floodlights wait for lamp heads the model does not have yet (issue195/BRIEF_floodlights.md); the garage has no
# amber beacon lamp either, so it has no GarageBeacons lights.
FIXTURES = os.path.join(ROOT, "issue195", "fixtures.json")
LIGHTS = [
    {"name": "GarageLampsSouth", "group": "GarageLamps", "type": "Rect",
     "fixtures": ["FixtureWarm_302", "FixtureWarm_304", "FixtureWarm_306", "FixtureWarm_308"],
     "rotation": [-65.0, 90.0, 0.0], "across": "x", "high": "y", "color": [1.0, 0.86, 0.66], "candelas": 400.0,
     "radius": 1800.0, "barn_door_angle": 0.0, "barn_door_length": 45.0},
    {"name": "GarageLampsNorth", "group": "GarageLamps", "type": "Rect",
     "fixtures": ["FixtureWarm_303", "FixtureWarm_305", "FixtureWarm_307", "FixtureWarm_309"],
     "rotation": [-65.0, -90.0, 0.0], "across": "x", "high": "y", "color": [1.0, 0.86, 0.66], "candelas": 400.0,
     "radius": 1800.0, "barn_door_angle": 0.0, "barn_door_length": 45.0},
    {"name": "EntranceDoor", "group": "Windows", "type": "Rect", "fixtures": ["WarmGlass_254", "WarmGlass_255"],
     "front": "x", "rotation": [-20.0, 0.0, 0.0], "across": "y", "high": "z", "color": [1.0, 0.66, 0.36],
     "candelas": 120.0, "radius": 700.0, "barn_door_angle": 45.0, "barn_door_length": 60.0},
    {"name": "EntranceWindowEast", "group": "Windows", "type": "Rect", "fixtures": ["WarmGlass_248"],
     "front": "x", "rotation": [-20.0, 0.0, 0.0], "across": "y", "high": "z", "color": [1.0, 0.66, 0.36],
     "candelas": 90.0, "radius": 650.0, "barn_door_angle": 45.0, "barn_door_length": 60.0},
    {"name": "EntranceWindowWest", "group": "Windows", "type": "Rect", "fixtures": ["WarmGlass_249"],
     "front": "x", "rotation": [-20.0, 0.0, 0.0], "across": "y", "high": "z", "color": [1.0, 0.66, 0.36],
     "candelas": 90.0, "radius": 650.0, "barn_door_angle": 45.0, "barn_door_length": 60.0},
]

# Gameplay collision: simple boxes around the building's blocks (USA_HQ_SPEC module schedule, in m), not the meshes.
# The garage bay is a box of its own, "garage", so the garage sequence (#192) can open it; the apron in front of the
# garage door (X 14.944 .. 19, Y -25.169 .. -10) stays free for the vehicle leaving it.
COLLISION_BOXES_M = [
    ("Rear", (-19.0, -28.0, 0.0), (-0.749, 28.0, 18.0), False),
    ("FrontWest", (-0.749, -10.0, 0.0), (19.0, 28.0, 8.4), False),
    ("FrontEast", (-0.749, -28.0, 0.0), (19.0, -25.169, 7.6), False),
    ("GarageBay", (-0.749, -25.169, 0.0), (14.944, -10.0, 7.6), True),
]

# Helpers the game uses, renamed from the Blender source.
SOCKETS = {
    "GarageSpawn": "Socket_Spawn_Garage",
    "GarageExit": "Socket_Exit_Door",
    "GarageDoorPivot": "Pivot_GarageDoor",
    "FlagCourtyard": "Socket_Flag_Courtyard",
    "FlagCore": "Socket_Flag_Core",
    "RadarPivot": "HQ19_Pivot_Radar_Azimuth",
    "WingDishAzimuthPivot": "HQ19_Pivot_WingDish_Azimuth",
    "WingDishElevationPivot": "HQ19_Pivot_WingDish_Elevation",
    "DriveShaftPivot": "HQ20_DriveShaft_Pivot",
}


def read(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def motion(name, location, helpers):
    """The part's FRTSPartMotion record, or None for one that does not turn on its own."""
    record = MOTION.get(name)
    if not record:
        return None
    pivot = [0.0, 0.0, 0.0]
    if "pivot_socket" in record:
        pivot = rounded([p - l for p, l in zip(helpers[record["pivot_socket"]], location)])
    return {
        "spin_rate": record.get("spin", [0.0, 0.0, 0.0]),
        "sweep_amplitude": record.get("sweep", [0.0, 0.0, 0.0]),
        "sweep_period": record.get("sweep_period", 0.0),
        "pivot_offset": pivot,
        "follow_part": record.get("follow", ""),
        "rate_jitter": record["jitter"],
    }


def load(name):
    with open(os.path.join(RECIPE, name), encoding="utf-8") as file:
        return json.load(file)


def leaf(path):
    """The asset name of a package path or object reference."""
    text = path["refPath"] if isinstance(path, dict) else path
    return text.rstrip("'}").split("/")[-1].split(".")[-1]


def rounded(values, digits=4):
    return [round(float(value), digits) for value in values]


def door_rest():
    """The closed door at time 0 of the delivered door cycle: each section's X, Z and pitch, the cable pitch and length
    scale, and the drive shaft's X, Z and pitch."""
    with open(os.path.join(ROOT, "animation", "CT_HQ20_DoorMotion.csv"), encoding="utf-8") as file:
        rows = list(csv.reader(file))
    return {row[0]: float(row[1]) for row in rows[1:]}


def materials():
    """{instance: {parent, scalars, vectors}} and the parents' names."""
    instances = {}
    for path, record in load("material_instances.json").items():
        instances[leaf(path)] = {
            "parent": leaf(record["Parent"]),
            "scalars": {value["parameterInfo"]["name"]: round(value["parameterValue"], 6)
                        for value in record["ScalarParameterValues"]},
            "vectors": {value["parameterInfo"]["name"]: rounded([value["parameterValue"][c] for c in "rgba"], 6)
                        for value in record["VectorParameterValues"]},
        }
    return instances


def textures():
    result = {}
    for path, record in load("textures.json").items():
        resize = record.get("PowerOfTwoMode") == "ResizeToSpecificResolution"
        result[leaf(path)] = {
            "compression": record["CompressionSettings"],
            "srgb": record["SRGB"],
            "lod_group": record["LODGroup"],
            "resize": [record["ResizeDuringBuildX"], record["ResizeDuringBuildY"]] if resize else None,
        }
    return result


def meshes():
    """Every delivered mesh: its FBX, slots and the instance of each, triangles, bounds and (LOD0, LOD1) budget."""
    result = {}
    for path, record in load("final_meshes.json").items():
        name = leaf(path)
        low, high = record["bounds"]["min"], record["bounds"]["max"]
        result[name] = {
            "fbx": f"fbx/{name}.fbx",
            "slots": {slot: leaf(material) for slot, material in sorted(record["slots"].items())},
            "triangles": record["triangles"],
            "bounds": [rounded([low[c] for c in "xyz"], 2), rounded([high[c] for c in "xyz"], 2)],
            "lods": list(BUDGETS.get(name, ())),
        }
        if name in HAND_LOD1:
            result[name]["lod1_fbx"] = f"fbx/{name}_LOD1.fbx"
    return result


def shell():
    """The game shell's two mesh records, their slots named after their instances, and the delivered meshes they stand
    for."""
    with open(GAME_MESHES, encoding="utf-8") as file:
        game = json.load(file)
    records = {}
    for name, record in game["meshes"].items():
        records[name] = {
            "fbx": record["fbx"],
            "lod1_fbx": record["lod1_fbx"],
            "slots": {slot: slot for slot in record["slots"]},
            "triangles": record["triangles"][0],
            "bounds": record["bounds"],
            "lods": record["triangles"],
        }
    return records, sorted(game["sources"])


def parts(mesh_records):
    """One part per mesh component in the preview level, at its place there, the door closed."""
    rest = door_rest()
    result = []
    sections = 0
    for actor in load("assembly.json"):
        for component in actor["components"]:
            properties = component.get("properties", {})
            mesh = properties.get("StaticMesh")
            if not mesh or leaf(mesh) not in mesh_records:
                continue
            name = leaf(mesh)
            if name == DELIVERED_FLAG:
                continue
            transform = actor["transform"]
            location = [transform["location"][c] for c in "xyz"]
            rotation = [transform["rotation"][c] for c in ("pitch", "yaw", "roll")]
            scale = [transform["scale"][c] for c in "xyz"]
            part = name.replace("SM_HQ19_", "").replace("SM_HQ20_", "").replace("SM_HQ21_", "")
            for suffix in ("_Delivery", "_WallCorrected", "_Wind"):
                part = part.replace(suffix, "")
            # The preview level stood with the door open: put the door's pieces where the cycle starts, closed.
            if name == "SM_HQ20_DoorSection_Delivery":
                index = sections
                sections += 1
                part = f"DoorSection{index:02d}"
                location = [rest[f"S{index:02d}_X"], location[1], rest[f"S{index:02d}_Z"]]
                rotation = [rest[f"S{index:02d}_P"], 0.0, 0.0]
            elif name == "SM_HQ20_DoorBottomSeal":
                location = [rest["S00_X"], location[1], rest["S00_Z"]]
                rotation = [rest["S00_P"], 0.0, 0.0]
            elif name == "SM_HQ20_DoorTopRollers":
                location = [rest["S15_X"], location[1], rest["S15_Z"]]
                rotation = [rest["S15_P"], 0.0, 0.0]
            elif name == "SM_HQ20_LiftCable":
                part = "LiftCableEast" if location[1] < -1758.45 else "LiftCableWest"
                location = [rest["S00_X"], location[1], rest["S00_Z"]]
                rotation = [rest["Cable_P"], 0.0, 0.0]
                scale = [1.0, 1.0, rest["Cable_L"]]
            elif name == "SM_HQ20_DriveShaft":
                location = [rest["Drive_X"], location[1], rest["Drive_Z"]]
                rotation = [rest["Drive_P"], 0.0, 0.0]
            movable = properties.get("Mobility") == "Movable"
            result.append({
                "name": part,
                "mesh": name,
                "location": rounded(location),
                "rotation": rounded(rotation),
                "scale": rounded(scale, 6),
                "movable": movable,
                "cast_shadow": bool(properties.get("CastShadow", True)),
                # Every mesh's bounds cover the building's (setup), which hold the flag's wave too.
                "bounds_scale": 1.0,
                "max_draw_distance": SMALL_PART_DRAW_DISTANCE if any(s in part for s in SMALL_PARTS) else 0.0,
                "wpo_disable_distance": 0.0,
            })
    order = {"Shell": 0, "Architecture": 0}
    result.sort(key=lambda p: (order.get(p["name"], 1), p["name"]))
    names = [p["name"] for p in result]
    if len(set(names)) != len(names):
        raise SystemExit(f"Duplicate part names: {names}")
    return result


def masts():
    """The masts' mesh records, their parts and their tip sockets, from the delivery's manifest at MAST_HEIGHT. Its
    bounds are in the Blender source's metres with Y mirrored; the masts are symmetric about their X axis."""
    entries = {entry["mesh"]: entry for entry in read(MAST_MANIFEST)["options"][MAST_HEIGHT]}
    records, result, tips = {}, [], {}
    for part, socket in sorted(MASTS.items()):
        name = f"SM_HQ22_{part}_H{MAST_HEIGHT}"
        lod0, lod1 = entries[name], entries[f"{name}_LOD1"]
        if lod0["socket_name"] != socket or lod0["beacon_scale"] != BEACON_SCALE:
            raise SystemExit(f"{name}: the manifest has {lod0['socket_name']} at beacon scale {lod0['beacon_scale']}")
        low, high = lod0["local_bounds_m"]
        records[name] = {
            "fbx": f"fbx/{name}.fbx",
            "lod1_fbx": f"fbx/{name}_LOD1.fbx",
            "slots": {slot: f"MI_{slot}" for slot in lod0["slots"]},
            "triangles": lod0["triangles"],
            "bounds": [rounded([low[0] * 100.0, -high[1] * 100.0, low[2] * 100.0], 2),
                       rounded([high[0] * 100.0, -low[1] * 100.0, high[2] * 100.0], 2)],
            "lods": [lod0["triangles"], lod1["triangles"]],
        }
        result.append({"name": part, "mesh": name, "location": [v + 0.0 for v in rounded(lod0["foot_cm"])], "rotation": [0.0, 0.0, 0.0],
                       "scale": [1.0, 1.0, 1.0], "movable": False, "cast_shadow": True, "bounds_scale": 1.0,
                       "max_draw_distance": 0.0, "wpo_disable_distance": 0.0})
        tips[socket] = [v + 0.0 for v in rounded(lod0["tip_cm"])]
    return records, result, tips


def sockets(mast_tips):
    """{socket: location in cm}. The inspection already converted Blender (metres, Y mirrored) to Unreal; the game
    reads locations only (the spawn and exit helpers face +X, the drive shaft's tilt stays in its part)."""
    helpers = load("source_inspection.json")["helpers"]
    result = {name: rounded(helpers[helper]["unreal_location_cm"]) for name, helper in SOCKETS.items()}
    result.update(mast_tips)
    return result


def mounts(flag):
    """The kit's pieces the HQ mounts (docs/art/BUILDING_KIT.md, Mounts; ARTSBuilding draws them through RTSKit::Expand):
    a beacon on each mast tip socket, and the flag's cloth at the pole, as wide as its height times flag's aspect."""
    catalogue = {piece["id"] for piece in read(KIT_CATALOGUE)["pieces"]}
    missing = {BEACON_PIECE, FLAG_PIECE} - catalogue
    if missing:
        raise SystemExit(f"The kit catalogue has no {sorted(missing)}")
    result = []
    for name, socket in sorted(BEACONS.items()):
        result.append({"name": name, "piece": BEACON_PIECE, "socket": socket,
                       "transform": [[0.0, 0.0, 0.0], [0.0, 0.0, 0.0], [BEACON_SCALE] * 3]})
    aspect = flag["ratio"][1] / flag["ratio"][0]
    result.append({"name": FLAG_PART, "piece": FLAG_PIECE,
                   "transform": [FLAG_HOIST, [0.0, 90.0, 0.0],
                                 rounded([FLAG_HEIGHT * aspect / 100.0, 1.0, FLAG_HEIGHT / 100.0], 6)]})
    return result


def lights():
    """The real lights at the lamps (LIGHTS), from the probed fixtures; each stands within its fixtures' bounds."""
    fixtures = {f["id"]: f for f in read(FIXTURES)["fixtures"]}
    result = []
    for spec in LIGHTS:
        missing = [f for f in spec["fixtures"] if f not in fixtures]
        if missing:
            raise SystemExit(f"{spec['name']}: fixtures.json has no {missing}")
        group = [fixtures[f] for f in spec["fixtures"]]
        if len({f["slot"] for f in group}) != 1:
            raise SystemExit(f"{spec['name']}: its fixtures are not all of one slot")
        low = [min(f["centre"][i] - f["size"][i] / 2.0 for f in group) for i in range(3)]
        high = [max(f["centre"][i] + f["size"][i] / 2.0 for f in group) for i in range(3)]
        location = [(low[i] + high[i]) / 2.0 for i in range(3)]
        if "front" in spec:
            axis = "xyz".index(spec["front"])
            location[axis] = high[axis]
        record = {"name": spec["name"], "group": spec["group"], "fixture": "+".join(spec["fixtures"]),
                  "type": spec["type"], "location": rounded(location, 1), "rotation": spec["rotation"],
                  "color": spec["color"], "candelas": spec["candelas"], "radius": spec["radius"]}
        for key in ("barn_door_angle", "barn_door_length"):
            if key in spec:
                record[key] = spec[key]
        if spec["type"] == "Rect":
            record["source_width"] = round(high["xyz".index(spec["across"])] - low["xyz".index(spec["across"])], 1)
            record["source_height"] = round(min(f["size"]["xyz".index(spec["high"])] for f in group)
                                            if "front" not in spec else
                                            high["xyz".index(spec["high"])] - low["xyz".index(spec["high"])], 1)
        result.append(record)
    return result


def shell_bake(shell_record, instances):
    """The bulk's slots on their baked instances (SHELL_BAKE_SETS), those instances, and the maps' textures."""
    sets = read(SHELL_BAKE_SETS)
    baked, maps = {}, {}
    for slot, instance in shell_record["slots"].items():
        if instance in SHELL_UNBAKED:
            continue
        if slot not in sets:
            raise SystemExit(f"{SHELL_BAKE_SETS} gives the shell's slot {slot} no texture set")
        record = json.loads(json.dumps(instances[instance]))
        record["textures"] = {}
        for parameter, (texture, settings) in SHELL_BAKE_TEXTURES.items():
            name = texture.format(sets[slot])
            record["textures"][parameter] = name
            maps[name] = dict(settings)
        record["switches"] = {"ShellBake": True}
        baked[instance + SHELL_BAKED] = record
        shell_record["slots"][slot] = instance + SHELL_BAKED
    return baked, maps


def main():
    delivered = meshes()
    shell_records, sources = shell()
    delivered_parts = parts(delivered)
    mesh_records = {name: record for name, record in delivered.items() if name not in sources and name != DELIVERED_FLAG}
    mesh_records.update(shell_records)
    mast_records, mast_parts, mast_tips = masts()
    mesh_records.update(mast_records)
    flags = read(FLAGS)["flags"]
    if FLAG_TYPE not in flags:
        raise SystemExit(f"flags.json has no flag {FLAG_TYPE}")
    game_parts = [{"name": part, "mesh": mesh, "location": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0],
                   "scale": [1.0, 1.0, 1.0], "movable": False, "cast_shadow": True, "bounds_scale": 1.0,
                   "max_draw_distance": 0.0, "wpo_disable_distance": 0.0}
                  for part, mesh in (("Shell", SHELL), ("ShellDetail", SHELL_DETAIL))]
    for part in delivered_parts:
        if part["mesh"] in sources:
            if part["location"] != [0.0, 0.0, 0.0] or part["rotation"] != [0.0, 0.0, 0.0] or part["movable"]:
                raise SystemExit(f"{part['name']} is not a static part at the origin, so it cannot be in the shell")
        else:
            game_parts.append(part)
    game_parts.extend(mast_parts)
    helpers = sockets(mast_tips)
    order = {"Shell": 0, "ShellDetail": 0}
    game_parts.sort(key=lambda p: (order.get(p["name"], 1), p["name"]))
    for part in game_parts:
        part["motion"] = motion(part["name"], part["location"], helpers)
    missing = set(MOTION) - {p["name"] for p in game_parts if p["motion"]}
    if missing:
        raise SystemExit(f"MOTION names parts the HQ does not have: {sorted(missing)}")
    instances = {name: record for name, record in materials().items() if record["parent"] != DELIVERED_FLAG_MASTER}
    for name, (group, gain) in LIGHT_GROUPS.items():
        if instances[name]["parent"] != "M_HQ_Final_Operational":
            raise SystemExit(f"{name} is not an instance of M_HQ_Final_Operational, so it has no lighting group")
        instances[name]["scalars"].update({f"Group{group}": 1.0, "NightGain": gain})
    for name, glow in GLASS_NIGHT_GLOW.items():
        instances[name]["vectors"]["NightGlow"] = glow
    baked_instances, baked_textures = shell_bake(mesh_records[SHELL], instances)
    instances.update(baked_instances)
    layout = {
        "about": "Generated by Tools/Art/usa_hq_recipe.py from recipe/ and game/; read by Tools/Editor/building_art.py.",
        "footprint_cells": [38, 56],
        "reference_size_cm": [3800.0, 5600.0, 1343.08],
        "root_part": "Shell",
        "switch_distance": SWITCH_DISTANCE,
        "size": "Large",
        "flag": FLAG_TYPE,
        "flag_part": FLAG_PART,
        "parts": game_parts,
        "mounts": mounts(flags[FLAG_TYPE]),
        "meshes": mesh_records,
        "delivery_shell": {
            "meshes": {name: delivered[name] for name in sources},
            "parts": [part for part in delivered_parts if part["mesh"] in sources],
        },
        "material_instances": instances,
        "textures": dict(textures(), **baked_textures),
        "sockets": helpers,
        "lights": lights(),
        "collision_boxes": [
            {"name": name, "min": rounded([v * 100.0 for v in low], 2), "max": rounded([v * 100.0 for v in high], 2),
             "garage": garage}
            for name, low, high, garage in COLLISION_BOXES_M
        ],
    }
    used = {material for mesh in mesh_records.values() for material in mesh["slots"].values()}
    missing = used - set(layout["material_instances"])
    if missing:
        raise SystemExit(f"Slots name instances the delivery does not have: {sorted(missing)}")
    path = os.path.join(ROOT, "usa_hq.json")
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(layout, file, indent=1)
        file.write("\n")
    print(f"Wrote {path}: {len(layout['parts'])} parts, {len(layout['mounts'])} mounts, {len(mesh_records)} meshes, "
          f"{len(layout['material_instances'])} instances, {len(layout['textures'])} textures")


if __name__ == "__main__":
    main()
