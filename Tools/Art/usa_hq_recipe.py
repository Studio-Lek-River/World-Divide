"""Writes Art/USA/buildings/usa_hq/usa_hq.json, the USA HQ layout that setup_testmap.py builds the game's HQ from
(building_art.py), out of the final art delivery's engine records in Art/USA/buildings/usa_hq/recipe/:

- final_meshes.json: every mesh, its material slots and the instance each slot gets, its LOD0 triangles and bounds;
- assembly.json: where the preview level placed each mesh, its mobility, shadow and bounds scale;
- material_instances.json: each instance's parent and parameter values;
- textures.json: each texture's compression, sRGB and build size;
- source_inspection.json: the helpers (spawn, exit, pivots) from the Blender source;
- the door curves (animation/CT_HQ20_DoorMotion.csv) for the closed door the game starts with;
- game/game_meshes.json: the game shell that stands for the meshes that never move (#358);
- Art/Shared/props/props.json and Art/Shared/flags/flags.json: the shared flag cloth and mast beacon (#351), which
  replace the delivered flag, and the flag the HQ flies.

The equipment's motion (MOTION), the beacons (BEACONS) and the flag (FLAG_*) are the game's own, set here (#351).

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
# (the bulk, the HQ's root part) and SM_HQ_ShellDetail (the small details LOD0 keeps whole, with a hand-made LOD1),
# described in game/game_meshes.json. Setup reduces the bulk to SHELL_LOD0 and SHELL_LOD1 with the engine's reduction;
# the HQ's LOD0 budget is 350,000, raised from the large building's 150,000 by the producer for the 12 m look (#358).
GAME_MESHES = os.path.join(ROOT, "game", "game_meshes.json")
SHELL = "SM_HQ_Shell"
SHELL_DETAIL = "SM_HQ_ShellDetail"
SHELL_LOD0 = 274000
SHELL_LOD1 = 22000

# The other meshes' (LOD0, LOD1) triangle budgets, per placement, reduced in setup from the delivered FBX. The building
# switches to LOD1 at SWITCH_DISTANCE cm from the camera as one (docs/art/MODEL_PERFORMANCE.md, Buildings); meshes under
# 2,000 triangles keep LOD0 only (PERFORMANCE_GUIDELINES 20). The flag stays dense enough for its wind.
BUDGETS = {
    "SM_HQ20_DoorSection_Delivery": (1200, 70),
    "SM_HQ19_Radar_Azimuth": (3000, 400),
    "SM_HQ19_WingDish_Elevation": (3000, 400),
    "SM_HQ20_DriveShaft": (1000, 150),
    "SM_HQ21_NationalFlag_Wind": (4000, 1000),
}
SWITCH_DISTANCE = 26000.0

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

# The shared props (Tools/Art/flag_beacon_blender.py) and flags.
SHARED = os.path.join(ROOT, "..", "..", "..", "Shared")
PROPS = os.path.join(SHARED, "props", "props.json")
FLAGS = os.path.join(SHARED, "flags", "flags.json")
PROP_SLOTS = {"SM_Flag_Cloth": {"Flag": "MI_Flag_{flag}"}, "SM_Beacon_Obstruction": {"Beacon": "MI_Beacon_Obstruction"}}
# Their (LOD0, LOD1) budgets: whole at LOD0, and at LOD1, past the 260 m switch where the flag's wind is long off
# (FLAG_WPO_DISABLE_DISTANCE), a flat sheet and a coarse beacon, so the HQ stays within its LOD1 budget.
PROP_BUDGETS = {"SM_Flag_Cloth": (1920, 120), "SM_Beacon_Obstruction": (314, 80)}

# The mast tips, measured on the delivered shell (the top vertex of each antenna rod; the delivery's Socket_Mast_Core_Tip,
# MastTip, stands 1.8 m off the core mast's rod): the core tower's mast at 18 m and the east wing's roof mast at 16.5 m.
# Each carries a red obstruction beacon, SM_Beacon_Obstruction, standing on it. The masts are to be enlarged (a follow-up of
# #351); the beacons move with these sockets.
MAST_TIPS = {
    "MastTipCore": [-898.7, 0.0, 1798.0],
    "MastTipEast": [-1090.0, -1995.0, 1650.0],
}
BEACONS = {"Beacon_MastTipCore": "MastTipCore", "Beacon_MastTipEast": "MastTipEast"}

# The flag (#351): the HQ flies FLAG_TYPE (flags.json) on SM_Flag_Cloth at the courtyard pole, the top of its hoist at
# FLAG_HOIST. The pole (13 cm across, 20 cm deep at its fittings, its top at 7.1 m) stands at X 1150, Y 0; the cloth
# flies along +Y (yaw 90) from just outside it, so it never passes through the pole, its top 20 cm under the pole's.
# FLAG_HEIGHT cm of hoist; the width is that times the flag's aspect, 4.18 m for the USA's 10:19. Its wind stops past
# FLAG_WPO_DISABLE_DISTANCE cm, beyond M_Flag_Cloth's fade (building_art.FLAG_FADE).
FLAG_TYPE = "USA"
FLAG_PART = "NationalFlag"
FLAG_HOIST = [1150.0, 10.5, 690.0]
FLAG_HEIGHT = 220.0
FLAG_WPO_DISABLE_DISTANCE = 12500.0
# The delivered flag and the master of its colour instances, which the shared cloth replaces.
DELIVERED_FLAG = "SM_HQ21_NationalFlag_Wind"
DELIVERED_FLAG_MASTER = "M_HQ21_FlagWind"

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
    "MastTip": "Socket_Mast_Core_Tip",
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
            "lods": [SHELL_LOD0, SHELL_LOD1] if name == SHELL else record["triangles"],
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


def sockets():
    """{socket: location in cm}. The inspection already converted Blender (metres, Y mirrored) to Unreal; the game
    reads locations only (the spawn and exit helpers face +X, the drive shaft's tilt stays in its part)."""
    helpers = load("source_inspection.json")["helpers"]
    result = {name: rounded(helpers[helper]["unreal_location_cm"]) for name, helper in SOCKETS.items()}
    result.update(MAST_TIPS)
    return result


def shared_parts(helpers, flag):
    """The HQ's parts on the shared props: a beacon on each mast tip, and the flag's cloth at the pole, as wide as its
    height times flag's aspect."""
    result = []
    for name, socket in sorted(BEACONS.items()):
        result.append({"name": name, "mesh": "SM_Beacon_Obstruction", "location": helpers[socket],
                       "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "movable": False, "cast_shadow": False,
                       "bounds_scale": 1.0, "max_draw_distance": 0.0, "wpo_disable_distance": 0.0})
    aspect = flag["ratio"][1] / flag["ratio"][0]
    result.append({"name": FLAG_PART, "mesh": "SM_Flag_Cloth", "location": FLAG_HOIST, "rotation": [0.0, 90.0, 0.0],
                   "scale": rounded([FLAG_HEIGHT * aspect / 100.0, 1.0, FLAG_HEIGHT / 100.0], 6), "movable": False,
                   "cast_shadow": True, "bounds_scale": 1.0, "max_draw_distance": 0.0,
                   "wpo_disable_distance": FLAG_WPO_DISABLE_DISTANCE})
    return result


def props():
    """The shared props' mesh records, their slots bound to the HQ's flag and the shared beacon instance."""
    records = {}
    for name, record in read(PROPS)["meshes"].items():
        slots = PROP_SLOTS[name]
        if sorted(slots) != sorted(record["slots"]):
            raise SystemExit(f"{name} has the slots {record['slots']}, not {sorted(slots)}")
        records[name] = {
            "fbx": "/".join(["..", "..", "..", "Shared", "props", record["fbx"]]),
            "slots": {slot: instance.format(flag=FLAG_TYPE) for slot, instance in slots.items()},
            "triangles": record["triangles"],
            "bounds": record["bounds"],
            "lods": list(PROP_BUDGETS[name]),
            "shared": True,
        }
    return records


def main():
    delivered = meshes()
    shell_records, sources = shell()
    delivered_parts = parts(delivered)
    mesh_records = {name: record for name, record in delivered.items() if name not in sources and name != DELIVERED_FLAG}
    mesh_records.update(shell_records)
    mesh_records.update(props())
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
    helpers = sockets()
    game_parts.extend(shared_parts(helpers, flags[FLAG_TYPE]))
    order = {"Shell": 0, "ShellDetail": 0}
    game_parts.sort(key=lambda p: (order.get(p["name"], 1), p["name"]))
    for part in game_parts:
        part["motion"] = motion(part["name"], part["location"], helpers)
    missing = set(MOTION) - {p["name"] for p in game_parts if p["motion"]}
    if missing:
        raise SystemExit(f"MOTION names parts the HQ does not have: {sorted(missing)}")
    instances = {name: record for name, record in materials().items() if record["parent"] != DELIVERED_FLAG_MASTER}
    layout = {
        "about": "Generated by Tools/Art/usa_hq_recipe.py from recipe/ and game/; read by Tools/Editor/building_art.py.",
        "footprint_cells": [38, 56],
        "reference_size_cm": [3800.0, 5600.0, 1798.0],
        "root_part": "Shell",
        "switch_distance": SWITCH_DISTANCE,
        "flag": FLAG_TYPE,
        "flag_part": FLAG_PART,
        "parts": game_parts,
        "meshes": mesh_records,
        "delivery_shell": {
            "meshes": {name: delivered[name] for name in sources},
            "parts": [part for part in delivered_parts if part["mesh"] in sources],
        },
        "material_instances": instances,
        "textures": textures(),
        "sockets": helpers,
        "collision_boxes": [
            {"name": name, "min": rounded([v * 100.0 for v in low], 2), "max": rounded([v * 100.0 for v in high], 2),
             "garage": garage}
            for name, low, high, garage in COLLISION_BOXES_M
        ],
    }
    used = {material for mesh in mesh_records.values() if not mesh.get("shared") for material in mesh["slots"].values()}
    missing = used - set(layout["material_instances"])
    if missing:
        raise SystemExit(f"Slots name instances the delivery does not have: {sorted(missing)}")
    path = os.path.join(ROOT, "usa_hq.json")
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(layout, file, indent=1)
        file.write("\n")
    print(f"Wrote {path}: {len(layout['parts'])} parts, {len(mesh_records)} meshes, "
          f"{len(layout['material_instances'])} instances, {len(layout['textures'])} textures")


if __name__ == "__main__":
    main()
