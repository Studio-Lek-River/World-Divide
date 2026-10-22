"""Audits the marketplace packs in Content/ (#153) and builds a scale test level from them.

The packs are third-party content the public repository may not hold: each machine adds them from Fab and keeps them
git-ignored and never cooked (Config/DefaultGame.ini). This script reads what is on this machine:

- Every top-level Content folder that is not a project folder is a pack. Its assets are read from the Asset Registry
  tags, without loading them: per static or skeletal mesh the size in cm (1 uu = 1 cm), Nanite, LODs, triangles,
  collision and material slots; per texture its size; per asset whose class comes from a plugin this project lacks,
  that plugin. One CSV per pack and a summary go to Saved/PackAudit/.
- Every pack texture over 4K gets a Max Texture Size of 4K (#330), so no 8K map reaches a map or the GPU.
- /Game/Developers/PackAudit/L_PackScale puts a sample of the packs (trees, a building, stones, a bridge, the props of
  ue.ps1 -Task freeassets, the surface materials and the scanned surfaces of ue.ps1 -Task surfaces on tiles) next to the
  Abrams and the M20 Guardian at real scale. The level references local-only assets, so it is git-ignored too. After saving it, every /Game package it references is checked to exist.

Run with Tools/Scripts/ue.ps1 -Task packaudit, which fails when the summary lists a missing reference.
"""

import csv
import os
import stat

import unreal

CONTENT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
OUT_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "PackAudit")
# The project's own Content folders, as in RTSConfigTests.cpp, and the bench HQs of setup -BenchHQ (never capped: the
# bench measures them as built); every other top-level folder is a pack.
PROJECT_FOLDERS = {"Art", "Audio", "Bench", "Collections", "Data", "Developers", "Effects", "Maps", "Materials", "UI", "World",
                   "__ExternalActors__", "__ExternalObjects__"}
LEVEL_PATH = "/Game/Developers/PackAudit/L_PackScale"

# Flags in the summary.
MAX_TEXTURE = 4096
# A mesh larger than this on its longest side is probably not authored in cm (or is a whole-scene mesh).
MAX_SENSIBLE_CM = 20000.0
MIN_SENSIBLE_CM = 1.0

# The sample of the scale level: (label, asset path) per category; missing ones are skipped with a warning.
ABRAMS = ["/Game/Art/Local/Meshes/SM_Abrams_Hull", "/Game/Art/Local/Meshes/SM_Abrams_Turret"]
M20 = ["/Game/Art/USA/Vehicles/M20Guardian/Meshes/SM_M20_Hull", "/Game/Art/USA/Vehicles/M20Guardian/Meshes/SM_M20_Turret"]
GROUND = "/Game/Materials/M_Ground"
# Surface materials outside the packs' landscape folders: (folder, name filter, how many), checked against A in 01.
SURFACE_SAMPLES = [
    ("/Game/MuddyGroundPack/Materials", "MI_MuddyGround", 1),
    ("/Game/UnrealDrive_CitySample/Materials", "MI_Asphalt_Raw", 1),
    ("/Game/UnrealDrive_CitySample/Materials", "MI_FreewayAsphalt_Road", 1),
    ("/Game/UnrealDrive_CitySample/Materials", "MI_Sidewalk", 1),
    ("/Game/SM_Concrete_07/Materials", "MI_MT", 2),
    ("/Game/SM_Tiles_02/Materials", "Granite", 1),
    ("/Game/Materials_StoneTiles/Materials/Instances", "MI_StoneTiles", 2),
]
SCANNED_SURFACES = "/Game/Surfaces"
# The props of ue.ps1 -Task freeassets (#409) in a row beside the Abrams: (folder, name filter, how many, spacing in cm,
# optional yaw). Grass and flower cards, fences, a pole and the power-line cable, signs, logs and debris, then the cliffs:
# open one-sided scans, turned so their face, not the open back, looks at the camera row (#428).
PROP_SAMPLES = [
    ("/Game/PolyHaven/grass_medium_02", "SM_", 3, 150.0),
    ("/Game/PolyHaven/grass_bermuda_01", "medium", 2, 100.0),
    ("/Game/PolyHaven/dandelion_01", "SM_", 1, 150.0),
    ("/Game/PolyHaven/celandine_01", "SM_", 1, 150.0),
    ("/Game/PolyHaven/nettle_plant", "tall", 1, 200.0),
    ("/Game/PolyHaven/shrub_02", "SM_", 1, 400.0),
    ("/Game/Art/Shared/Roadside", "SM_Fence_PostRail", 1, 500.0),
    ("/Game/Art/Shared/Roadside", "SM_Fence_FieldWire", 1, 500.0),
    ("/Game/PolyHaven/modular_chainlink_fence", "SM_modular_chainlink_fence_double", 1, 500.0),
    ("/Game/PolyHaven/modular_electricity_poles", "SM_pole", 1, 300.0),
    ("/Game/Art/Shared/Roadside", "SM_PowerLine_Cable", 1, 500.0),
    ("/Game/Art/Shared/Roadside", "SM_Sign_speed_50", 1, 300.0),
    ("/Game/Art/Shared/Roadside", "SM_Sign_warning", 1, 300.0),
    ("/Game/Art/Shared/Roadside", "SM_Sign_place_name", 1, 400.0),
    ("/Game/PolyHaven/dead_tree_trunk", "SM_", 1, 800.0),
    ("/Game/PolyHaven/tree_stump_01", "SM_", 1, 400.0),
    ("/Game/PolyHaven/dry_branches_medium_01", "SM_", 1, 300.0),
    ("/Game/PolyHaven/bark_debris_01", "SM_", 1, 400.0),
    ("/Game/PolyHaven/rock_face_01", "SM_", 1, 1000.0, 180.0),
    ("/Game/PolyHaven/rock_face_02", "SM_", 1, 1000.0, 180.0),
    ("/Game/PolyHaven/mountainside", "SM_", 1, 1500.0, 180.0),
]
CUBE = "/Engine/BasicShapes/Cube.Cube"

registry = unreal.AssetRegistryHelpers.get_asset_registry()
assets = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def tag(data, name):
    value = data.get_tag_value(name)
    if isinstance(value, tuple):
        value = value[1] if value[0] else None
    return str(value) if value not in (None, "") else ""


def class_name(data):
    return str(data.asset_class_path.asset_name)


def pack_roots():
    return sorted(name for name in os.listdir(CONTENT_DIR)
                  if os.path.isdir(os.path.join(CONTENT_DIR, name)) and name not in PROJECT_FOLDERS)


def folder_size_mb(name):
    total = 0
    for root, _, files in os.walk(os.path.join(CONTENT_DIR, name)):
        total += sum(os.path.getsize(os.path.join(root, file)) for file in files)
    return total / (1024.0 * 1024.0)


def size_cm(data):
    """The ApproxSize tag ('1950x1830x2790') as three floats, or None."""
    parts = tag(data, "ApproxSize").split("x")
    try:
        return tuple(float(part) for part in parts) if len(parts) == 3 else None
    except ValueError:
        return None


def mesh_row(data):
    size = size_cm(data)
    nanite = tag(data, "NaniteEnabled")
    lods = tag(data, "LODs")
    collision = tag(data, "CollisionPrims")
    complexity = tag(data, "CollisionComplexity")
    flags = []
    if size and max(size) > MAX_SENSIBLE_CM:
        flags.append("huge")
    if size and max(size) < MIN_SENSIBLE_CM:
        flags.append("tiny")
    if class_name(data) == "StaticMesh":
        if collision in ("", "0") and "UseComplexAsSimple" not in complexity:
            flags.append("no simple collision")
        if nanite != "True" and lods in ("", "1"):
            flags.append("no Nanite, 1 LOD")
    return {
        "size_cm": "x".join(f"{value:.0f}" for value in size) if size else "",
        "nanite": nanite,
        "lods": lods,
        "triangles": tag(data, "Triangles"),
        "collision_prims": collision,
        "collision_complexity": complexity,
        "materials": tag(data, "Materials"),
        "flags": "; ".join(flags),
    }


def texture_row(data):
    dimensions = tag(data, "Dimensions")
    flags = []
    try:
        if max(int(value) for value in dimensions.split("x")[:2]) > MAX_TEXTURE:
            flags.append(f"over {MAX_TEXTURE}")
    except ValueError:
        pass
    return {"dimensions": dimensions, "format": tag(data, "Format"), "flags": "; ".join(flags)}


COLUMNS = ["package", "class", "size_cm", "nanite", "lods", "triangles", "collision_prims", "collision_complexity",
           "materials", "dimensions", "format", "needs_plugin", "flags"]


def missing_module(data):
    """The /Script module of the asset's class or Blueprint parent when that class is not loaded (a plugin the pack needs
    and this project lacks, e.g. MetaRoad for UnrealDrive_CitySample), else ''."""
    paths = [str(data.asset_class_path.package_name) + "." + class_name(data), tag(data, "ParentClass").strip("'\"")]
    for path in paths:
        path = path.split("'")[1] if "'" in path else path
        # The module's package, not the class: registry class paths can be stale redirects (PhysicalMaterial).
        module = path.split(".")[0]
        if module.startswith("/Script/") and "." in path and unreal.find_object(None, module) is None:
            return module[len("/Script/"):]
    return ""


def audit_pack(name):
    rows = []
    for data in registry.get_assets_by_path(f"/Game/{name}", recursive=True):
        row = {"package": str(data.package_name), "class": class_name(data), "needs_plugin": missing_module(data)}
        if row["class"] in ("StaticMesh", "SkeletalMesh"):
            row.update(mesh_row(data))
        elif row["class"] in ("Texture2D", "VirtualTexture2D"):
            row.update(texture_row(data))
        rows.append(row)
    rows.sort(key=lambda row: (row["class"], row["package"]))
    with open(os.path.join(OUT_DIR, f"{name}.csv"), "w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, COLUMNS, restval="")
        writer.writeheader()
        writer.writerows(rows)
    return rows


def cap_textures(name):
    """Limits every texture of the pack over MAX_TEXTURE to it (#330): the Scans Matter, UnrealDrive and Deko packs ship
    8K maps. The source stays in the asset; the editor and any cook build the mips from 4K down. Returns the count changed
    and the count already capped."""
    changed = kept = 0
    for data in registry.get_assets_by_path(f"/Game/{name}", recursive=True):
        if class_name(data) != "Texture2D":
            continue
        try:
            if max(int(value) for value in tag(data, "Dimensions").split("x")[:2]) <= MAX_TEXTURE:
                continue
        except ValueError:
            continue
        texture = unreal.load_asset(str(data.package_name))
        if 0 < texture.get_editor_property("max_texture_size") <= MAX_TEXTURE:
            kept += 1
            continue
        texture.set_editor_property("max_texture_size", MAX_TEXTURE)
        # Some packs install read-only (Deko, Megaplant). They are local, git-ignored copies, never LFS files.
        file = os.path.join(CONTENT_DIR, str(data.package_name)[len("/Game/"):] + ".uasset")
        if os.path.exists(file) and not os.access(file, os.W_OK):
            os.chmod(file, stat.S_IWRITE | stat.S_IREAD)
        if not assets.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {data.package_name}")
        changed += 1
    return changed, kept


def summarize(name, rows, capped=(0, 0)):
    counts = {}
    for row in rows:
        counts[row["class"]] = counts.get(row["class"], 0) + 1
    meshes = [row for row in rows if row["class"] in ("StaticMesh", "SkeletalMesh")]
    textures = [row for row in rows if row["class"] in ("Texture2D", "VirtualTexture2D")]
    sizes = [row["dimensions"] for row in textures if row.get("dimensions")]
    lines = [f"## {name}", "",
             f"- Size on disk: {folder_size_mb(name):.0f} MB, {len(rows)} assets",
             "- Classes: " + ", ".join(f"{cls} {count}" for cls, count in sorted(counts.items(), key=lambda item: -item[1])),
             f"- Meshes: {len(meshes)}, Nanite {sum(1 for row in meshes if row.get('nanite') == 'True')}, "
             f"more than 1 LOD {sum(1 for row in meshes if row.get('lods') not in ('', '1'))}",
             "- Texture sizes: " + ", ".join(f"{size} x{sizes.count(size)}" for size in sorted(set(sizes), key=lambda s: -sizes.count(s)))]
    if sum(capped):
        lines.append(f"- Textures over {MAX_TEXTURE} capped at {MAX_TEXTURE}: {sum(capped)} ({capped[0]} by this run)")
    plugins = sorted({row["needs_plugin"] for row in rows if row.get("needs_plugin")})
    if plugins:
        lines.append(f"- Needs plugin: {', '.join(plugins)} (not in this project; "
                     f"{sum(1 for row in rows if row.get('needs_plugin'))} assets unusable without it)")
    flagged = [row for row in rows if row.get("flags")]
    if flagged:
        lines.append(f"- Flagged: {len(flagged)}")
        for row in flagged[:15]:
            lines.append(f"  - `{row['package']}` ({row['class']} {row.get('size_cm') or row.get('dimensions')}): {row['flags']}")
        if len(flagged) > 15:
            lines.append(f"  - ... {len(flagged) - 15} more in {name}.csv")
    lines.append("")
    return lines


# --- Scale level ---------------------------------------------------------------------------------------------------


def new_level(path):
    """As in setup_testmap.py: remove the old level's file and registry entry first, which 5.8 leaves behind."""
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
        file = os.path.join(CONTENT_DIR, path[len("/Game/"):] + ".umap")
        if os.path.exists(file):
            os.remove(file)
            registry.scan_files_synchronous([file], force_rescan=True)
    if not levels.new_level(path):
        raise RuntimeError(f"Could not create {path}")


def spawn(actor_class, location, yaw=0.0, label=None):
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    return actor


def place(path, location, label, yaw=0.0):
    """Places a static mesh, skeletal mesh or Blueprint; returns False when the asset is missing."""
    if not assets.does_asset_exist(path):
        unreal.log_warning(f"PackAudit: {path} is not on this machine, left out of the level")
        return False
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.StaticMesh):
        actor = spawn(unreal.StaticMeshActor, location, yaw, label)
        actor.static_mesh_component.set_static_mesh(asset)
    elif isinstance(asset, unreal.SkeletalMesh):
        actor = spawn(unreal.SkeletalMeshActor, location, yaw, label)
        actor.skeletal_mesh_component.set_skeletal_mesh_asset(asset)
    elif isinstance(asset, unreal.Blueprint):
        actor = spawn(asset.generated_class(), location, yaw, label)
    else:
        unreal.log_warning(f"PackAudit: {path} is a {type(asset).__name__}, not placeable")
        return False
    return True


def find(root, class_names, contains=""):
    """The packages under root of one of class_names whose name contains the text, sorted."""
    found = []
    for data in registry.get_assets_by_path(root, recursive=True):
        if class_name(data) in class_names and contains.lower() in str(data.asset_name).lower():
            found.append(str(data.package_name))
    return sorted(found)


def spawn_lighting():
    sun = spawn(unreal.DirectionalLight, (0.0, 0.0, 2000.0), label="Sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-35.0, yaw=35.0), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("atmosphere_sun_light", True)
    sky = spawn(unreal.SkyLight, (0.0, 0.0, 1000.0), label="SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), label="SkyAtmosphere")
    spawn(unreal.ExponentialHeightFog, (0.0, 0.0, 0.0), label="Fog")


def spawn_tile(label, location, size_m, material):
    tile = spawn(unreal.StaticMeshActor, location, label=label)
    tile.static_mesh_component.set_static_mesh(unreal.load_asset(CUBE))
    tile.set_actor_scale3d(unreal.Vector(size_m, size_m, 0.02))
    if material:
        tile.static_mesh_component.set_material(0, unreal.load_asset(material))


def build_level():
    """A 300 x 120 m strip: vehicles at the origin, rows of trees, a building, stones and a bridge along +x, and the
    packs' surface materials on 10 m tiles along -y."""
    new_level(LEVEL_PATH)
    spawn_lighting()
    spawn_tile("Ground", (0.0, 0.0, -1.0), 300.0, GROUND)

    placed, broken = [], {}
    def put(path, location, label, yaw=0.0):
        """Places the asset unless it is missing or references a missing package itself (a pack defect)."""
        missing = missing_references(path) if assets.does_asset_exist(path) else []
        if missing:
            broken[path] = missing
            unreal.log_warning(f"PackAudit: {path} references {len(missing)} missing package(s), left out of the level")
            return False
        if place(path, location, label, yaw):
            placed.append(path)
            return True
        return False

    for index, path in enumerate(ABRAMS):
        put(path, (0.0, -800.0, 0.0), f"Abrams_{index}")
    for index, path in enumerate(M20):
        put(path, (0.0, 800.0, 0.0), f"M20_{index}")

    # One of each Megaplant species (the first variant), then the high-poly tree.
    x = 2000.0
    for species in sorted(find("/Game/Megaplant_Library", {"SkeletalMesh", "StaticMesh"}, "_01_A")):
        put(species, (x, 0.0, 0.0), os.path.basename(species))
        x += 1500.0
    put("/Game/HighPoly_Tree_Model/Meshes/SM_HP_Tree", (x + 1000.0, 0.0, 0.0), "SM_HP_Tree")

    buildings = find("/Game/ProceduralBuildingGenerator/Prefabs", {"Blueprint"}, "PBG_Building")
    # The first building whose materials are complete.
    next((building for building in buildings if put(building, (-3000.0, 0.0, 0.0), "Building")), None)
    for index, stone in enumerate(find("/Game/Real_Stones_Pack_01/Meshes", {"StaticMesh"}, "SM_Stone")[:4]):
        put(stone, (1500.0, -2500.0 + 500.0 * index, 0.0), os.path.basename(stone))
    bridges = find("/Game/StylizedBridge/StaticMesh/Bridge", {"StaticMesh"})
    if bridges:
        put(bridges[0], (0.0, 5000.0, 0.0), "Bridge")
    # The free props (#409), from the Abrams along -x.
    x = -1000.0
    for root, contains, count, spacing, *yaw in PROP_SAMPLES:
        for prop in find(root, {"StaticMesh"}, contains)[:count]:
            put(prop, (x, -800.0, 0.0), os.path.basename(prop), yaw[0] if yaw else 0.0)
            x -= spacing

    # Surface candidates: the material instances in the packs' landscape folders, then the samples of the surface packs.
    surfaces = [path for pack in pack_roots() for path in find(f"/Game/{pack}", {"MaterialInstanceConstant"})
                if "/landscape/" in path.lower() and "/vegetation/" not in path.lower()][:12]
    for root, contains, count in SURFACE_SAMPLES:
        surfaces += find(root, {"MaterialInstanceConstant"}, contains)[:count]
    # The scanned surfaces of ue.ps1 -Task surfaces (#330), all of them, after the packs' rows.
    surfaces += [""] * (-len(surfaces) % 12) + find(SCANNED_SURFACES, {"MaterialInstanceConstant"}, "MI_")
    for index, surface in enumerate(surfaces):
        if not surface:
            continue
        if missing_references(surface):
            broken[surface] = missing_references(surface)
            continue
        row, column = divmod(index, 12)
        spawn_tile(f"Surface_{os.path.basename(surface)}", (-6000.0 + 1100.0 * column, -5000.0 - 1100.0 * row, -0.5), 10.0,
                   surface)
        placed.append(surface)

    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {LEVEL_PATH}")
    return placed, broken


def missing_references(package):
    """Every /Game package the level depends on, recursively, that has no asset in the registry."""
    options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
    seen, todo, missing = set(), [package], []
    while todo:
        current = todo.pop()
        for dependency in registry.get_dependencies(current, options) or []:
            name = str(dependency)
            if name in seen or not name.startswith("/Game/"):
                continue
            seen.add(name)
            if not registry.get_assets_by_package_name(name, include_only_on_disk_assets=True):
                missing.append(f"{name} (from {current})")
            else:
                todo.append(name)
    return sorted(missing)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    registry.search_all_assets(True)
    lines = ["# Pack audit", "", f"Engine {unreal.SystemLibrary.get_engine_version()}. Generated by Tools/Editor/audit_packs.py.", ""]
    for name in pack_roots():
        unreal.log(f"PackAudit: auditing {name}")
        capped = cap_textures(name)
        lines += summarize(name, audit_pack(name), capped)

    placed, broken = build_level()
    missing = missing_references(LEVEL_PATH)
    lines += ["## L_PackScale", "", f"- {len(placed)} assets placed:"] + [f"  - `{path}`" for path in placed]
    lines.append(f"- Left out, they reference packages the pack does not contain: {len(broken)}")
    for path, entries in sorted(broken.items()):
        lines.append(f"  - `{path}`: {len(entries)}, e.g. {entries[0]}")
    lines += [f"- Missing references: {len(missing)}"] + [f"  - {entry}" for entry in missing]
    with open(os.path.join(OUT_DIR, "summary.md"), "w", encoding="utf-8") as file:
        file.write("\n".join(lines) + "\n")
    unreal.log(f"PackAudit: wrote {OUT_DIR}, {len(missing)} missing references")
    for entry in missing:
        unreal.log_error(f"PackAudit: missing {entry}")


main()
