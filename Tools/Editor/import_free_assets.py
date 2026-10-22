"""Downloads and imports the CC0 assets of Tools/Editor/free_assets.json and the in-house roadside kit (#409).

- Poly Haven models (CC0): each model's 2K FBX and its base colour, DirectX normal, ARM (AO, roughness, metalness) and alpha
  maps from api.polyhaven.com, cached in Saved/FreeAssets/<id>/ (a cached file is not fetched again). They are imported to
  /Game/PolyHaven/<id>/: one Nanite static mesh SM_<object> per object of the FBX (a model's variants, a modular kit's
  parts; Poly Haven's _LOD1 and lower objects are dropped, Nanite needs none), textures and an MI_ per material of one of two masters, /Game/PolyHaven/M_PolyHavenProp (opaque) or M_PolyHavenFoliage (masked by the alpha
  map, two-sided). Props get an 18-DOP hull as collision, or their own triangles where the list says "complex" (cliffs); foliage none.
  /Game/PolyHaven is git-ignored and never cooked like the packs: every machine downloads it again with this task.
- ambientCG surfaces (CC0): each listed set's 2K PNG ZIP goes to the surfaces folder (WD_SURFACES_DIR, default
  D:\\Downloads\\textures) when it is not there yet; ue.ps1 -Task surfaces imports it like the Megascans sets.
- The roadside kit of Tools/Art/roadside_blender.py (Art/Shared/roadside, own work): signs and fences imported to the
  committed /Game/Art/Shared/Roadside, checked against roadside.json, with M_Roadside (flat colour) and M_RoadsideFace
  (the sign face textures).

The report goes to Saved/FreeAssets/imported.md. Run with Tools/Scripts/ue.ps1 -Task freeassets.
"""

import json
import os
import re
import urllib.request

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
CACHE_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "FreeAssets")
LIST = os.path.join(PROJECT_DIR, "Tools", "Editor", "free_assets.json")
SURFACES_DIR = os.environ.get("WD_SURFACES_DIR") or r"D:\Downloads\textures"
ROADSIDE_SOURCE = os.path.join(PROJECT_DIR, "Art", "Shared", "roadside")

ROOT = "/Game/PolyHaven"
PROP_MASTER = f"{ROOT}/M_PolyHavenProp"
FOLIAGE_MASTER = f"{ROOT}/M_PolyHavenFoliage"
ROADSIDE_DIR = "/Game/Art/Shared/Roadside"
ROADSIDE_MASTER = f"{ROADSIDE_DIR}/M_Roadside"
ROADSIDE_FACE_MASTER = f"{ROADSIDE_DIR}/M_RoadsideFace"
# The roadside kit's flat slots: (colour, roughness, metallic).
ROADSIDE_SLOTS = {
    "Galvanised": ((0.42, 0.43, 0.43), 0.45, 1.0),
    "Wood": ((0.20, 0.15, 0.10), 0.85, 0.0),
    "Wire": ((0.33, 0.33, 0.32), 0.5, 1.0),
}

API = "https://api.polyhaven.com"
AMBIENTCG = "https://ambientcg.com/get?file={}_2K-PNG.zip"
# Poly Haven asks API users for a user agent of their own.
USER_AGENT = "WorldDivide-freeassets/1.0 (Studio Lek River)"
RESOLUTION = "2k"
# The maps used, by the end of Poly Haven's map key ("Diffuse", "pieces_diff", "wire_nor_dx"), and the file type preferred.
MAPS = {"diffuse": ("BC", "jpg"), "diff": ("BC", "jpg"), "nor_dx": ("N", "png"), "arm": ("ARM", "jpg"), "alpha": ("A", "png")}

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def save(asset):
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


# --- Download ------------------------------------------------------------------------------------------------------


def fetch(url, target):
    """Downloads url to target unless it is there already; returns target."""
    if os.path.exists(target) and os.path.getsize(target) > 0:
        return target
    os.makedirs(os.path.dirname(target), exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response, open(target + ".part", "wb") as file:
        file.write(response.read())
    os.replace(target + ".part", target)
    unreal.log(f"FreeAssets: downloaded {url}")
    return target


def fetch_json(url, target):
    with open(fetch(url, target), encoding="utf-8") as file:
        return json.load(file)


def map_groups(files):
    """{group: {kind: url}} from a Poly Haven files listing; the group is the key's prefix ('' for the main material)."""
    groups = {}
    for key, resolutions in files.items():
        if key in ("blend", "gltf", "fbx", "usd") or RESOLUTION not in resolutions:
            continue
        lower = key.lower()
        for suffix, (kind, preferred) in MAPS.items():
            if lower == suffix or lower.endswith("_" + suffix):
                group = lower[:-len(suffix)].rstrip("_")
                formats = resolutions[RESOLUTION]
                chosen = preferred if preferred in formats else next(f for f in ("png", "jpg") if f in formats)
                groups.setdefault(group, {})[kind] = formats[chosen]["url"]
                break
    return groups


def download_model(model_id):
    folder = os.path.join(CACHE_DIR, model_id)
    files = fetch_json(f"{API}/files/{model_id}", os.path.join(folder, "files.json"))
    info = fetch_json(f"{API}/info/{model_id}", os.path.join(folder, "info.json"))
    fbx = fetch(files["fbx"][RESOLUTION]["fbx"]["url"], os.path.join(folder, f"{model_id}.fbx"))
    groups = {}
    for group, kinds in map_groups(files).items():
        groups[group] = {kind: fetch(url, os.path.join(folder, url.rsplit("/", 1)[1])) for kind, url in kinds.items()}
    if not any("BC" in kinds for kinds in groups.values()):
        raise RuntimeError(f"{model_id}: no base colour map in {RESOLUTION}")
    return fbx, groups, info


# --- Materials -----------------------------------------------------------------------------------------------------


def node(material, kind, x, y):
    return editing.create_material_expression(material, kind, x, y)


def import_texture(source, folder, name, kind):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{folder}/{name}")
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    if kind == "BC":
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    elif kind == "N":
        # Poly Haven's nor_dx is DirectX (green down) already, as Unreal wants it.
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    else:
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR)
    save(texture)
    return texture


def material_at(path):
    if assets.does_asset_exist(path):
        material = unreal.load_asset(path)
        editing.delete_all_material_expressions(material)
        return material
    folder, name = path.rsplit("/", 1)
    return tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())


def sampler(material, parameter, texture, sampler_type, x, y):
    made = node(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    made.set_editor_property("parameter_name", parameter)
    made.set_editor_property("texture", texture)
    made.set_editor_property("sampler_type", sampler_type)
    return made


def build_master(path, defaults, foliage):
    """Base colour, normal and the ARM's AO, roughness and metalness on UV 0; foliage also masks by the alpha map's red
    and is two-sided. defaults are imported textures, for the samplers' types."""
    material = material_at(path)
    color = sampler(material, "BaseColor", defaults["BC"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -600, -300)
    normal = sampler(material, "Normal", defaults["N"], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -600, 50)
    arm = sampler(material, "ARM", defaults["ARM"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -600, 400)
    editing.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(arm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    editing.connect_material_property(arm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.connect_material_property(arm, "B", unreal.MaterialProperty.MP_METALLIC)
    if foliage:
        alpha = sampler(material, "Alpha", defaults["A"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -600, 750)
        editing.connect_material_property(alpha, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        material.set_editor_property("two_sided", True)
    editing.recompile_material(material)
    save(material)
    return material


def build_instance(folder, name, master, parameters):
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        instance = unreal.load_asset(path)
    else:
        instance = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, master)
    for parameter, value in parameters.items():
        if isinstance(value, unreal.Texture):
            editing.set_material_instance_texture_parameter_value(instance, parameter, value)
        elif isinstance(value, unreal.LinearColor):
            editing.set_material_instance_vector_parameter_value(instance, parameter, value)
        else:
            editing.set_material_instance_scalar_parameter_value(instance, parameter, value)
    editing.update_material_instance(instance)
    save(instance)
    return instance


# --- Meshes --------------------------------------------------------------------------------------------------------


def import_fbx(source, folder, name, combine, collision):
    """The FBX's meshes as Nanite static meshes in folder, without materials; returns them. With collision, UCX_ meshes
    become the collision, and a mesh without them gets the pipeline's fallback, an 18-DOP hull."""
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    common = pipeline.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    meshes = pipeline.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL if combine
                               else unreal.InterchangeCombineStaticMeshesBehavior.DO_NOT_COMBINE)
    meshes.set_editor_property("import_skeletal_meshes", False)
    meshes.set_editor_property("collision", collision)
    meshes.set_editor_property("import_collision_according_to_mesh_name", collision)
    meshes.set_editor_property("build_nanite", True)
    materials = pipeline.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    parameters = unreal.ImportAssetParameters()
    parameters.is_automated = True
    parameters.replace_existing = True
    if combine:
        parameters.destination_name = name
    parameters.override_pipelines.append(unreal.SoftObjectPath(pipeline.get_path_name()))
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    manager.import_asset(folder, unreal.InterchangeManager.create_source_data(source), parameters)
    found = [unreal.load_asset(path.split(".")[0]) for path in assets.list_assets(folder, recursive=False)]
    found = [mesh for mesh in found if isinstance(mesh, unreal.StaticMesh)]
    if combine:
        found = [mesh for mesh in found if mesh.get_name() == name]
    if not found:
        raise RuntimeError(f"Could not import {source} as static meshes")
    for mesh in found:
        nanite = mesh.get_editor_property("nanite_settings")
        if not nanite.enabled:
            nanite.enabled = True
            mesh.set_editor_property("nanite_settings", nanite)
    return found


def size_cm(mesh):
    box = mesh.get_bounding_box()
    return (box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z)


def import_model(entry, fbx, groups):
    model_id = entry["id"]
    folder = f"{ROOT}/{model_id}"
    foliage = entry["kind"] == "foliage"
    # A rerun starts from an empty folder: the folder is local, and the objects of a model may have changed.
    if assets.does_directory_exist(folder):
        assets.delete_directory(folder)
    textures = {}
    for group, kinds in groups.items():
        base = f"T_{model_id}" + (f"_{group}" if group else "")
        textures[group] = {kind: import_texture(path, folder, f"{base}_{kind}", kind) for kind, path in kinds.items()}
    meshes = []
    for mesh in import_fbx(fbx, folder, f"SM_{model_id}", False, not foliage):
        name = mesh.get_name()
        if re.search(r"_LOD[1-9]$", name):
            assets.delete_asset(f"{folder}/{name}")
            continue
        wanted = "SM_" + re.sub(r"_LOD0$", "", name)
        if not assets.rename_asset(f"{folder}/{name}", f"{folder}/{wanted}"):
            raise RuntimeError(f"Could not rename {folder}/{name} to {wanted}")
        meshes.append(unreal.load_asset(f"{folder}/{wanted}"))
    return folder, foliage, textures, meshes


def finish_model(entry, folder, foliage, textures, meshes, masters):
    """The model's instances and their slots, and its collision. Returns the mesh rows of the report."""
    model_id = entry["id"]
    instances = {}
    for group, kinds in textures.items():
        if "BC" not in kinds:
            continue
        masked = foliage and "A" in kinds
        master = masters["foliage" if masked else "prop"]
        parameters = {"BaseColor": kinds["BC"]}
        for kind, parameter in (("N", "Normal"), ("ARM", "ARM"), ("A", "Alpha")):
            if kind in kinds and (kind != "A" or masked):
                parameters[parameter] = kinds[kind]
        instances[group] = build_instance(folder, f"MI_{model_id}" + (f"_{group}" if group else ""), master, parameters)
    rows = []
    for mesh in meshes:
        for index, slot in enumerate(mesh.static_materials):
            slot_name = str(slot.material_slot_name).lower()
            group = slot_name[len(model_id) + 1:] if slot_name.startswith(model_id.lower() + "_") else ""
            mesh.set_material(index, instances.get(group) or instances.get("") or next(iter(instances.values())))
        if entry.get("collision") == "complex":
            mesh.get_editor_property("body_setup").set_editor_property("collision_trace_flag",
                                                                       unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        save(mesh)
        rows.append((mesh, size_cm(mesh)))
    return rows


# --- Roadside kit --------------------------------------------------------------------------------------------------


def build_roadside_masters():
    flat = material_at(ROADSIDE_MASTER)
    color = node(flat, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(*ROADSIDE_SLOTS["Wood"][0], 1.0))
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for index, (name, value) in enumerate((("Roughness", 0.8), ("Metallic", 0.0))):
        scalar = node(flat, unreal.MaterialExpressionScalarParameter, -500, 250 + 150 * index)
        scalar.set_editor_property("parameter_name", name)
        scalar.set_editor_property("default_value", value)
        editing.connect_material_property(scalar, "", unreal.MaterialProperty.MP_ROUGHNESS if name == "Roughness"
                                          else unreal.MaterialProperty.MP_METALLIC)
    editing.recompile_material(flat)
    save(flat)
    return flat


def build_roadside_face_master(default_face):
    face = material_at(ROADSIDE_FACE_MASTER)
    texture = sampler(face, "Face", default_face, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -500, 0)
    editing.connect_material_property(texture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    # Retroreflective sheeting: smooth, not metallic.
    roughness = node(face, unreal.MaterialExpressionConstant, -300, 300)
    roughness.set_editor_property("r", 0.4)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(face)
    save(face)
    return face


def import_roadside():
    with open(os.path.join(ROADSIDE_SOURCE, "roadside.json"), encoding="utf-8") as file:
        records = json.load(file)["meshes"]
    flat = build_roadside_masters()
    slots = {slot: build_instance(ROADSIDE_DIR, f"MI_Roadside_{slot}", flat,
                                  {"Color": unreal.LinearColor(*color, 1.0), "Roughness": roughness, "Metallic": metallic})
             for slot, (color, roughness, metallic) in ROADSIDE_SLOTS.items()}
    faces = {name: import_texture(os.path.join(ROADSIDE_SOURCE, record["face"]), f"{ROADSIDE_DIR}/Textures",
                                  os.path.splitext(record["face"])[0], "BC")
             for name, record in records.items() if record.get("face")}
    face_master = build_roadside_face_master(next(iter(faces.values())))
    rows = []
    for name, record in records.items():
        # The cable has no UCX_ boxes and stays without collision.
        collides = record["collision_boxes"] > 0
        mesh = import_fbx(os.path.join(ROADSIDE_SOURCE, record["fbx"]), ROADSIDE_DIR, name, True, collides)[0]
        wanted = record["bounds"][1][0] - record["bounds"][0][0]
        if abs(size_cm(mesh)[0] - wanted) > 2.0:
            raise RuntimeError(f"{name} imported {size_cm(mesh)[0]:.1f} cm along X instead of {wanted:.1f}: check the FBX units")
        if collides:
            shapes = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
            collision = len(shapes.get_editor_property("convex_elems")) + len(shapes.get_editor_property("box_elems"))
            if collision != record["collision_boxes"]:
                raise RuntimeError(f"{name} imported {collision} collision shapes, not its {record['collision_boxes']} UCX_ boxes")
            mesh.get_editor_property("body_setup").set_editor_property("collision_trace_flag",
                                                                       unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        for index, slot in enumerate(mesh.static_materials):
            slot_name = str(slot.material_slot_name)
            if slot_name == "Face":
                material = build_instance(ROADSIDE_DIR, f"MI_{name.replace('SM_', '')}_Face", face_master, {"Face": faces[name]})
            elif slot_name in slots:
                material = slots[slot_name]
            else:
                raise RuntimeError(f"{name}: no material for slot {slot_name}")
            mesh.set_material(index, material)
        save(mesh)
        rows.append((mesh, size_cm(mesh)))
    return rows


# --- Main ----------------------------------------------------------------------------------------------------------


def main():
    with open(LIST, encoding="utf-8") as file:
        wanted = json.load(file)
    os.makedirs(CACHE_DIR, exist_ok=True)

    surfaces = []
    for set_id in wanted["ambientcg"]:
        surfaces.append(fetch(AMBIENTCG.format(set_id), os.path.join(SURFACES_DIR, f"{set_id}_2K-PNG.zip")))

    imported = []
    for entry in wanted["polyhaven"]:
        unreal.log(f"FreeAssets: {entry['id']}")
        fbx, groups, info = download_model(entry["id"])
        imported.append((entry, info) + import_model(entry, fbx, groups))

    def first(kind, foliage_only=False):
        for entry, _, _, foliage, textures, _ in imported:
            if foliage_only and not foliage:
                continue
            for kinds in textures.values():
                if kind in kinds:
                    return kinds[kind]
        raise RuntimeError(f"No imported {kind} texture for the masters' defaults")
    defaults = {kind: first(kind) for kind in ("BC", "N", "ARM")}
    masters = {"prop": build_master(PROP_MASTER, defaults, False),
               "foliage": build_master(FOLIAGE_MASTER, dict(defaults, A=first("A", True)), True)}

    lines = ["# Free assets", "", "Generated by Tools/Editor/import_free_assets.py from Tools/Editor/free_assets.json.", "",
             "## Poly Haven (CC0)", "", "| Mesh | Category | Kind | Size (cm) | Source triangles | Authors | Source |",
             "|---|---|---|---|---|---|---|"]
    for entry, info, folder, foliage, textures, meshes in imported:
        for mesh, size in finish_model(entry, folder, foliage, textures, meshes, masters):
            lines.append(f"| `{mesh.get_path_name().split('.')[0]}` | {entry['category']} | {entry['kind']} | "
                         f"{size[0]:.0f}x{size[1]:.0f}x{size[2]:.0f} | {info.get('polycount', '')} | "
                         f"{', '.join(info.get('authors', {}))} | https://polyhaven.com/a/{entry['id']} |")
    lines += ["", "## ambientCG (CC0), for ue.ps1 -Task surfaces", ""] + [f"- `{path}`" for path in surfaces]
    lines += ["", "## Roadside kit (own work)", "", "| Mesh | Size (cm) |", "|---|---|"]
    for mesh, size in import_roadside():
        lines.append(f"| `{mesh.get_path_name().split('.')[0]}` | {size[0]:.0f}x{size[1]:.0f}x{size[2]:.0f} |")
    with open(os.path.join(CACHE_DIR, "imported.md"), "w", encoding="utf-8") as file:
        file.write("\n".join(lines) + "\n")
    unreal.log(f"FreeAssets: {len(imported)} Poly Haven models, {len(surfaces)} surface ZIPs, roadside kit imported")


main()
