"""Builds the USA HQ's game shell (#358) from the final delivery's FBX in Art/USA/buildings/usa_hq/fbx, for the large
building's two LODs (docs/art/MODEL_PERFORMANCE.md, Buildings): the nine meshes that never move merged into two, with one
material slot per material instance:

  blender -b --python Tools/Art/usa_hq_game_blender.py
  blender -b --python Tools/Art/usa_hq_game_blender.py -- --baked-shell    (SM_HQ_Shell alone, the rest kept)

- SM_HQ_Shell, the bulk (frames, panels, roofs, tiles): the baked game mesh made from the approved master (#373,
  issue373/), LOD0 and LOD1 by hand within the large building's budget, its fine detail in the normal and AO maps on UV1
  (T_HQ_ShellBake_Set*, usa_hq_recipe.py's SHELL_BAKE), taken in as it is but for its tangents (BAKED_SHELL);
- SM_HQ_ShellDetail, the faces of DETAIL, the small details that read at the RTS camera (solar cells, team lights,
  markings, door light strips, leaves, soil), which LOD0 keeps whole: an engine reduction over the whole shell lost the roof's
  solar cells (#349);
- for the details, a hand-made LOD1 with the same slots in the same order (SM_HQ_ShellDetail_LOD1.fbx): each source
  mesh reduced within SHELL's budget by collapse decimation, PROTECTED's faces weighted so they keep more of their
  triangles, and the details' share of it kept.

It writes them and game/game_meshes.json (each LOD's triangles per slot and its bounds) under Art/USA/buildings/usa_hq,
which Tools/Art/usa_hq_recipe.py and setup_testmap.py read. The delivery's FBX and its .blend are only read.

Everything is in the delivery's frame: +X the front, Y mirrored from Unreal in Blender, metres in Blender and centimetres
in the FBX, as m20_guardian_blender.py exports.
"""

import json
import math
import os
import re
import sys

import bmesh
import bpy
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree
from mathutils import Vector

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ROOT = os.path.join(PROJECT_DIR, "Art", "USA", "buildings", "usa_hq")
FBX_DIR = os.path.join(ROOT, "fbx")
GAME_DIR = os.path.join(ROOT, "game")
SHELL_NAME = "SM_HQ_Shell"
DETAIL_NAME = "SM_HQ_ShellDetail"
# The baked shell (#373): the accepted source and its LOD0 and LOD1 objects, the same slots in the same order. Codex's
# last round stopped in an unchecked UV edit, so this is that round's save from before it (round4/HQ373_Round4_Final.blend1).
BAKED_SHELL = os.path.join(ROOT, "issue373", "final", "HQ373_Shell_Final.blend")
BAKED_OBJECTS = ("SM_HQ373_R4_Shell", "SM_HQ373_R4_Shell_LOD1")

# The shell's source meshes and their LOD1 triangle budgets, by what reads past the 260 m switch, each with the step the
# collapse takes on the way: straight from the delivery's density it stalls far above the budget.
SHELL = {
    "SM_HQ20_Architecture_WallCorrected": (38000, 14000),
    "SM_HQ20_RoofSystems_WallCorrected": (34000, 9000),
    "SM_HQ21_Courtyard": (10000, 2600),
    "SM_HQ20_GarageInterior": (12000, 2000),
    "SM_HQ20_DoorHardware_Delivery": (5000, 2600),
    "SM_HQ21_CourtyardDetails_Delivery": (3000, 900),
    "SM_HQ20_GaragePortal": (4500, 800),
    "SM_HQ21_PlantingFinish_Delivery": (7000, 1500),
    "SM_HQ20_GarageFloor": (1000, 400),
}

# The instances whose faces keep their triangles, as a weight per LOD: small details that read at 150 m keep all of
# theirs at LOD0; beyond the 260 m switch only the smallest (cells, team lights, RF panels) keep all, the rest give some up.
PROTECTED = {
    "MI_HQ19_SolarCell": (1.0, 1.0), "MI_HQ17_TeamLight": (1.0, 1.0), "MI_HQ17_TeamPanel": (1.0, 1.0),
    "MI_HQ19_RFPanel": (1.0, 1.0), "MI_HQ17_Marking": (1.0, 0.5), "MI_HQ17_Chips": (1.0, 0.5),
    "MI_HQ21_Leaf0": (1.0, 0.5), "MI_HQ21_Leaf1": (1.0, 0.5), "MI_HQ21_Leaf2": (1.0, 0.5),
    "MI_HQ20_FixtureBlue": (1.0, 0.5), "MI_HQ20_FixtureWarm": (1.0, 0.5),
    # The planters' soil, a thin sheet the engine's reduction sinks below their rims.
    "MI_USA_HQ_HQ21_Soil": (1.0, 0.5),
    # The railings and grilles, which a reduction shrinks into gaps.
    "MI_HQ19_SafetyOchre": (0.5, 0.5), "MI_HQ20_SafetyOchre": (0.5, 0.5), "MI_HQ19_DrainGrate": (0.5, 0.5),
}
# The instances LOD0 keeps whole, in SM_HQ_ShellDetail: those PROTECTED keeps whole at LOD0.
DETAIL = {name for name, (weight, _) in PROTECTED.items() if weight >= 1.0}
# How strongly the protected faces resist the collapse (the decimate modifier's vertex group factor, at most 1000).
PROTECT_FACTOR = 1000.0
# Vertices closer than this (m) are welded before the collapse: the delivery splits them at every hard edge and seam,
# which leaves the collapse almost nothing to remove. The normals come back from the source afterwards.
# LOD1 welds wider, so pieces that touch (laps on the membrane, bars on their frames) fuse and can be reduced together;
# the protected faces' vertices are left out of that wider weld.
WELD_DISTANCE = (0.0001, 0.05)
# LOD1 leaves out the loose pieces smaller than this (m) across: bolts, clips and brackets, thousands of
# closed boxes the collapse cannot reduce, about a pixel from the 260 m switch (15 cm a pixel at 1080p). The pieces of
# PROTECTED's instances stay, but of the leaves only one in LOD1_LEAF_SHARE: the planters keep their volume and colour
# with a third of the cards.
LOD1_SMALLEST_PIECE = 0.20
LOD1_LEAVES = {"MI_HQ21_Leaf0", "MI_HQ21_Leaf1", "MI_HQ21_Leaf2"}
LOD1_LEAF_SHARE = 3
# Faces within this of each other (radians, 1 degree) count as one flat face.
FLAT_ANGLE = 0.01745
# Edges whose faces meet at more than this (radians, 35 degrees) shade sharp, the rest smooth.
SHARP_ANGLE = 0.6109
# A budget is met when the result is within it and no more than this share under it.
BUDGET_SLACK = 0.04


def slot_instances():
    """{mesh: {slot: material instance}} from the delivery's recipe/final_meshes.json."""
    with open(os.path.join(ROOT, "recipe", "final_meshes.json"), encoding="utf-8") as file:
        records = json.load(file)
    leaf = lambda text: text.rstrip("'}").split("/")[-1].split(".")[-1]
    return {leaf(path): {slot: leaf(material["refPath"]) for slot, material in record["slots"].items()}
            for path, record in records.items()}


def triangles(obj):
    """The triangles obj draws with its modifiers."""
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    mesh.calc_loop_triangles()
    count = len(mesh.loop_triangles)
    evaluated.to_mesh_clear()
    return count


def select_only(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def import_source(name, instances):
    """The delivered mesh as one object in world space, its slots bound to one material per instance."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=os.path.join(FBX_DIR, f"{name}.fbx"))
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if len(new) != 1:
        raise RuntimeError(f"{name}.fbx holds {len(new)} meshes, not one")
    obj = new[0]
    for other in set(bpy.data.objects) - before - {obj}:
        bpy.data.objects.remove(other)
    select_only(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.name = f"{name}_Source"
    for slot in obj.material_slots:
        slot_name = re.sub(r"\.\d{3}$", "", slot.material.name)
        if slot_name not in instances:
            raise RuntimeError(f"{name}: slot {slot_name} has no instance in final_meshes.json")
        instance = instances[slot_name]
        slot.material = bpy.data.materials.get(instance) or bpy.data.materials.new(instance)
    return obj


def remove_separate_masts(obj, source_name):
    """Exclude #379's exact approved mast vertices before either shell/LOD split.

    The mask is recorded from the evaluated approved source, not a broad roof
    bounding box. Thus neighbouring dishes, roof finishes and equipment survive.
    """
    groups = {'SM_HQ20_Architecture_WallCorrected': 'Core',
              'SM_HQ20_RoofSystems_WallCorrected': 'East'}
    if source_name not in groups:
        return 0
    path = os.path.join(ROOT, 'issue379', 'mast_removal_mask.json')
    with open(path, encoding='utf8') as file:
        record = json.load(file)[groups[source_name]]
    tree = KDTree(len(record['vertices']))
    for i, co in enumerate(record['vertices']):
        tree.insert(co, i)
    tree.balance()
    surface = BVHTree.FromPolygons(record['vertices'], record['faces'], all_triangles=True)
    hits = {v.index for v in obj.data.vertices if tree.find(v.co)[2] < 0.00015 or
            (surface.find_nearest(v.co, 0.00015)[0] is not None)}
    doomed = [p for p in obj.data.polygons if all(v in hits for v in p.vertices)]
    removed = sum(len(p.vertices)-2 for p in doomed)
    # The wall-corrected roof FBX subdivides the original mast rods. Its face
    # count differs, but the complete surface area must agree with the master.
    expected_area = sum((Vector(record['vertices'][b])-Vector(record['vertices'][a])).cross(
        Vector(record['vertices'][c])-Vector(record['vertices'][a])).length/2
        for a,b,c in record['faces'])
    removed_area = sum(p.area for p in doomed)
    if abs(removed_area-expected_area) > max(0.0001, expected_area*0.00001):
        raise RuntimeError(f'{source_name}: mask area {removed_area}, expected {expected_area}; refuse partial removal')
    print(f'usa_hq_game: removed mast {removed} triangles; surface {removed_area:.8f} / {expected_area:.8f} m2', flush=True)
    obj.data.vertices.foreach_set('select', [False]*len(obj.data.vertices))
    obj.data.edges.foreach_set('select', [False]*len(obj.data.edges))
    obj.data.polygons.foreach_set('select', [all(v in hits for v in p.vertices) for p in obj.data.polygons])
    select_only(obj)
    bpy.context.scene.tool_settings.mesh_select_mode = (False, False, True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.delete(type='FACE'); bpy.ops.object.mode_set(mode='OBJECT')
    return removed


def protect_group(obj, lod):
    """A vertex group of the vertices of obj's PROTECTED faces at their weight for lod (the highest a vertex touches)."""
    weights = {i: PROTECTED[slot.material.name][lod] for i, slot in enumerate(obj.material_slots)
               if slot.material.name in PROTECTED}
    group = obj.vertex_groups.new(name="Protected")
    vertices = {}
    for polygon in obj.data.polygons:
        weight = weights.get(polygon.material_index)
        if weight:
            for v in polygon.vertices:
                vertices[v] = max(weight, vertices.get(v, 0.0))
    for weight in sorted(set(vertices.values())):
        group.add([v for v, w in vertices.items() if w == weight], weight, "REPLACE")
    return group


def drop_small_pieces(obj, smallest):
    """Deletes obj's loose pieces less than smallest across, except those of PROTECTED's instances, and all but one in
    LOD1_LEAF_SHARE leaves. Returns how many faces it deleted."""
    kept = {i for i, slot in enumerate(obj.material_slots) if slot.material.name in PROTECTED}
    leaves = {i for i, slot in enumerate(obj.material_slots) if slot.material.name in LOD1_LEAVES}
    leaf_count = 0
    pieces = bmesh.new()
    pieces.from_mesh(obj.data)
    seen = set()
    doomed = []
    for face in pieces.faces:
        if face in seen:
            continue
        stack, piece = [face], []
        seen.add(face)
        while stack:
            current = stack.pop()
            piece.append(current)
            for edge in current.edges:
                for other in edge.link_faces:
                    if other not in seen:
                        seen.add(other)
                        stack.append(other)
        corners = [v.co for f in piece for v in f.verts]
        size = max(max(c[i] for c in corners) - min(c[i] for c in corners) for i in range(3))
        if piece[0].material_index in leaves:
            leaf_count += 1
            if leaf_count % LOD1_LEAF_SHARE:
                doomed.extend(piece)
        elif size < smallest and not any(f.material_index in kept for f in piece):
            doomed.extend(piece)
    bmesh.ops.delete(pieces, geom=doomed, context="FACES")
    pieces.to_mesh(obj.data)
    pieces.free()
    return len(doomed)


def reduce(start, source, target, lod, label):
    """A copy of start reduced to at most target triangles (collapse, protected faces weighted for lod), kept inside
    source's bounds, shaded by angle."""
    obj = start.copy()
    obj.data = start.data.copy()
    obj.name = label
    bpy.context.collection.objects.link(obj)
    if lod == 1:
        drop_small_pieces(obj, LOD1_SMALLEST_PIECE)
    base = triangles(obj)
    if base > target:
        welded = bmesh.new()
        welded.from_mesh(obj.data)
        protected = {i for i, slot in enumerate(obj.material_slots) if slot.material.name in PROTECTED}
        fixed = {v for face in welded.faces if face.material_index in protected for v in face.verts}
        bmesh.ops.remove_doubles(welded, verts=[v for v in welded.verts if v not in fixed], dist=WELD_DISTANCE[0])
        if WELD_DISTANCE[lod] > WELD_DISTANCE[0]:
            bmesh.ops.remove_doubles(welded, verts=[v for v in welded.verts if v.is_valid and v not in fixed],
                                     dist=WELD_DISTANCE[lod])
        # Flat faces split into many (tiles, panels, bevel runs) merge first, losing nothing: the collapse then spends
        # the budget on the shapes.
        bmesh.ops.dissolve_limit(welded, angle_limit=FLAT_ANGLE, verts=welded.verts, edges=welded.edges,
                                 delimit={"MATERIAL", "SEAM", "UV"})
        bmesh.ops.triangulate(welded, faces=welded.faces)
        welded.to_mesh(obj.data)
        welded.free()
        print(f"usa_hq_game:   {label} {base} -> {triangles(obj)} after merging flat faces")
        group = protect_group(obj, lod)
        modifier = obj.modifiers.new("Decimate", "DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.use_collapse_triangulate = True
        modifier.vertex_group = group.name
        modifier.vertex_group_factor = PROTECT_FACTOR
        # The modifier collapses a group's vertices first; inverted, the group is what it keeps.
        modifier.invert_vertex_group = True
        base = triangles(obj)
        low, high = 0.0, min(1.0, target / base)
        modifier.ratio = high
        # The protected faces resist, so the ratio that meets the budget is found by halving.
        for _ in range(12):
            count = triangles(obj)
            if target * (1.0 - BUDGET_SLACK) <= count <= target:
                break
            if count > target:
                high = modifier.ratio
            else:
                low = modifier.ratio
            modifier.ratio = (low + high) / 2.0
        if triangles(obj) > target:
            modifier.ratio = low
        select_only(obj)
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        obj.vertex_groups.remove(obj.vertex_groups["Protected"])
        # The collapse can place a vertex past a sharp corner, which would move the building's bounds and its base.
        low_corner = [min(v.co[i] for v in source.data.vertices) for i in range(3)]
        high_corner = [max(v.co[i] for v in source.data.vertices) for i in range(3)]
        for vertex in obj.data.vertices:
            vertex.co = [min(max(vertex.co[i], low_corner[i]), high_corner[i]) for i in range(3)]
    else:
        select_only(obj)
        bpy.ops.object.modifier_add(type="TRIANGULATE")
        bpy.ops.object.modifier_apply(modifier=obj.modifiers[-1].name)

    # Hard-surface shading from the reduced faces: smooth within SHARP_ANGLE, sharp beyond it.
    select_only(obj)
    if obj.data.has_custom_normals:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    obj.data.shade_smooth()
    obj.data.set_sharp_from_angle(angle=SHARP_ANGLE)
    return obj


def join(objects, name, order):
    """objects joined into one named name, its slots exactly order (instance names), in that order."""
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.name = name
    # A slot no face uses goes; its index is never read.
    index = {material.name: order.index(material.name) if material.name in order else 0
             for material in obj.data.materials}
    remap = [index[material.name] for material in obj.data.materials]
    faces = [0] * len(obj.data.polygons)
    obj.data.polygons.foreach_get("material_index", faces)
    obj.data.materials.clear()
    for material_name in order:
        obj.data.materials.append(bpy.data.materials[material_name])
    obj.data.polygons.foreach_set("material_index", [remap[i] for i in faces])
    obj.data.update()
    return obj


def per_slot(obj):
    counts = {material.name: 0 for material in obj.data.materials}
    obj.data.calc_loop_triangles()
    for triangle in obj.data.loop_triangles:
        counts[obj.data.materials[triangle.material_index].name] += 1
    return counts


def bounds(obj):
    """(min, max) in Unreal centimetres: Blender's metres with Y mirrored back."""
    xs, ys, zs = zip(*(vertex.co for vertex in obj.data.vertices))
    return [[round(min(xs) * 100.0, 2), round(-max(ys) * 100.0, 2), round(min(zs) * 100.0, 2)],
            [round(max(xs) * 100.0, 2), round(-min(ys) * 100.0, 2), round(max(zs) * 100.0, 2)]]


def export(obj, path):
    select_only(obj)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, use_mesh_modifiers=False,
                             mesh_smooth_type="FACE", use_tspace=True, colors_type="NONE", apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                             bake_space_transform=False, add_leaf_bones=False, bake_anim=False, use_custom_props=False,
                             path_mode="AUTO")


def repair_detail_tangent_uvs(obj, _pass=0):
    """Repair collapsed UV faces with a coherent 2 m world box projection.

    Invalid UVs are reprojected. Needle faces narrower than 1 mm are removed
    because Unreal's tangent recomputation rejects some of them; surviving positions,
    normals and material assignments remain intact. This covers planter sides
    whose inherited projection collapsed during LOD reduction.
    """
    mesh = obj.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active.data
    mesh.calc_tangents(uvmap=mesh.uv_layers.active.name)
    bad = {p.index for p in mesh.polygons if any(mesh.loops[i].tangent.length < 1e-4 or
           mesh.loops[i].bitangent.length < 1e-4 for i in p.loop_indices)}
    for triangle in mesh.loop_triangles:
        a, b, c = (uv[i].uv.copy() for i in triangle.loops)
        if abs((b-a).cross(c-a)) < 2e-10:
            bad.add(triangle.polygon_index)
    for index in bad:
        face = mesh.polygons[index]
        dominant = max(range(3), key=lambda i: abs(face.normal[i]))
        axes = [i for i in range(3) if i != dominant]
        center = obj.matrix_world @ face.center
        origin = [math.floor(center[axis]/2.0) for axis in axes]
        for loop in face.loop_indices:
            co = obj.matrix_world @ mesh.vertices[mesh.loops[loop].vertex_index].co
            uv[loop].uv = (co[axes[0]]/2.0-origin[0], co[axes[1]]/2.0-origin[1])
    mesh.update()
    mesh.free_tangents()
    mesh.calc_tangents(uvmap=mesh.uv_layers.active.name)
    zero = sum(loop.tangent.length < 1e-4 or loop.bitangent.length < 1e-4 for loop in mesh.loops)
    slivers = {p.index for p in mesh.polygons if 2*p.area/max(
        (mesh.vertices[a].co-mesh.vertices[b].co).length for a,b in p.edge_keys)<0.001}
    if zero or slivers:
        faces = slivers | {p.index for p in mesh.polygons if any(mesh.loops[i].tangent.length < 1e-4 or
                 mesh.loops[i].bitangent.length < 1e-4 for i in p.loop_indices)}
        widths=[]
        for index in faces:
            p=mesh.polygons[index]
            longest=max((mesh.vertices[a].co-mesh.vertices[b].co).length for a,b in p.edge_keys)
            widths.append(2*p.area/longest)
        if max(widths)>0.001:
            raise RuntimeError('Refusing to remove a non-sliver face for tangent repair')
        select_only(obj)
        mesh.vertices.foreach_set('select',[False]*len(mesh.vertices))
        mesh.edges.foreach_set('select',[False]*len(mesh.edges))
        mesh.polygons.foreach_set('select',[p.index in faces for p in mesh.polygons])
        bpy.context.scene.tool_settings.mesh_select_mode=(False,False,True)
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.delete(type='FACE');bpy.ops.object.mode_set(mode='OBJECT')
        obj['issue379_removed_sliver_faces']=obj.get('issue379_removed_sliver_faces',0)+len(faces)
        obj['issue379_max_sliver_width_m']=max(obj.get('issue379_max_sliver_width_m',0),max(widths))
        mesh=obj.data
        mesh.update();mesh.free_tangents();mesh.calc_tangents(uvmap=mesh.uv_layers.active.name)
        zero = sum(loop.tangent.length < 1e-4 or loop.bitangent.length < 1e-4 for loop in mesh.loops)
    if zero:
        if _pass<2:
            return len(bad)+repair_detail_tangent_uvs(obj,_pass+1)
        uv=mesh.uv_layers.active.data
        diagnostics=[]
        for triangle in mesh.loop_triangles:
            if any(mesh.loops[i].tangent.length < 1e-4 for i in triangle.loops):
                a,b,c=(uv[i].uv.copy() for i in triangle.loops)
                coords=[mesh.vertices[i].co for i in triangle.vertices]
                edges=[(coords[(i+1)%3]-coords[i]).length for i in range(3)]
                diagnostics.append((triangle.area,abs((b-a).cross(c-a))/2,triangle.polygon_index,edges))
        print('Remaining tangent diagnostic area/UV/index:',diagnostics[:24],flush=True)
        raise RuntimeError(f'{obj.name}: {zero} invalid tangent frames remain after UV repair')
    return len(bad)


def finalize_detail_lod1(obj, path):
    # Round-trip the FBX once before the final count: its tessellator drops
    # collapsed source faces, so pre-export polygon counts can be misleading.
    before = set(bpy.data.objects)
    original_materials=list(obj.data.materials)
    bpy.ops.import_scene.fbx(filepath=path)
    imported = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    if len(imported) != 1:
        raise RuntimeError('Detail LOD1 round-trip must contain exactly one mesh')
    fresh = imported[0]
    select_only(fresh)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(fresh.data.materials)!=len(original_materials):
        raise RuntimeError('Detail LOD1 round-trip changed its material slot count')
    for i,material in enumerate(original_materials):
        if re.sub(r'\.\d{3}$','',fresh.data.materials[i].name)!=material.name:
            raise RuntimeError('Detail LOD1 round-trip changed its material slot order')
        fresh.data.materials[i]=material
    obj.data = fresh.data.copy()
    for other in set(bpy.data.objects)-before:
        bpy.data.objects.remove(other, do_unlink=True)
    fixed = repair_detail_tangent_uvs(obj)
    export(obj, path)
    print(f'usa_hq_game: repaired {fixed} collapsed detail LOD1 UV faces; final tangents valid', flush=True)


def split(obj, detail, label):
    """A copy of obj with only DETAIL's faces (detail) or only the others."""
    part = obj.copy()
    part.data = obj.data.copy()
    part.name = label
    bpy.context.collection.objects.link(part)
    wanted = {i for i, slot in enumerate(part.material_slots) if (slot.material.name in DETAIL) == detail}
    # The faces to delete, alone selected, in face select mode: edit mode would otherwise derive them from the vertices.
    part.data.vertices.foreach_set("select", [False] * len(part.data.vertices))
    part.data.edges.foreach_set("select", [False] * len(part.data.edges))
    part.data.polygons.foreach_set("select", [p.material_index not in wanted for p in part.data.polygons])
    select_only(part)
    bpy.context.scene.tool_settings.mesh_select_mode = (False, False, True)
    # In edit mode, which keeps the delivery's custom normals on the faces that stay.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.delete(type="FACE")
    bpy.ops.object.mode_set(mode="OBJECT")
    return part


def record_for(mesh, joined, order):
    """game_meshes.json's record of mesh: its FBX, slots, triangles per LOD and slot, bounds."""
    slots = [per_slot(obj) for obj in joined]
    record = {
        "fbx": f"game/{mesh}.fbx",
        "lod1_fbx": f"game/{mesh}_LOD1.fbx" if len(joined) > 1 else None,
        "slots": order,
        "triangles": [sum(counts.values()) for counts in slots],
        "slot_triangles": slots,
        "bounds": bounds(joined[0]),
    }
    print(f"usa_hq_game: {mesh} {record['triangles']} triangles, {len(order)} slots, bounds {record['bounds']}")
    for slot in order:
        print(f"usa_hq_game:   {slot} " + " / ".join(str(counts[slot]) for counts in slots))
    return record


def baked_shell():
    """SM_HQ_Shell and its LOD1 from BAKED_SHELL (#373) as they are, the two LODs' slots in one order, each face's UV0
    whose tangents collapse reprojected (repair_detail_tangent_uvs: Unreal rebuilds them and warns), and its record."""
    with bpy.data.libraries.load(BAKED_SHELL, link=False) as (source, target):
        missing = set(BAKED_OBJECTS) - set(source.objects)
        if missing:
            raise RuntimeError(f"{BAKED_SHELL} has no {sorted(missing)}")
        target.objects = list(BAKED_OBJECTS)
    joined, order = [], None
    for obj, path in zip(target.objects, (f"{SHELL_NAME}.fbx", f"{SHELL_NAME}_LOD1.fbx")):
        bpy.context.collection.objects.link(obj)
        slots = [re.sub(r"\.\d{3}$", "", material.name) for material in obj.data.materials]
        if slots != sorted(slots) or (order is not None and slots != order):
            raise RuntimeError(f"{obj.name}: slots {slots} are not the shell's, in order")
        order = slots
        if [layer.name for layer in obj.data.uv_layers] != ["UV0", "UV1"]:
            raise RuntimeError(f"{obj.name}: UV channels {[layer.name for layer in obj.data.uv_layers]}, not UV0 and UV1")
        obj.data.uv_layers.active_index = 0
        print(f"usa_hq_game: {obj.name} repaired {repair_detail_tangent_uvs(obj)} UV faces whose tangents collapse")
        export(obj, os.path.join(GAME_DIR, path))
        joined.append(obj)
    return record_for(SHELL_NAME, joined, order)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    instances = slot_instances()
    os.makedirs(GAME_DIR, exist_ok=True)
    pieces = {DETAIL_NAME: ([], [])}
    report = {}
    for name, (step, budget) in SHELL.items():
        source = import_source(name, instances[name])
        removed_mast_triangles = remove_separate_masts(source, name)
        halfway = reduce(source, source, step, 0, f"{name}_Step")
        lod1 = reduce(halfway, source, budget, 1, f"{name}_LOD1")
        bpy.data.objects.remove(halfway)
        report[name] = {"source": triangles(source), "lod1": triangles(lod1), "budget": budget,
                        "removed_mast_triangles": removed_mast_triangles}
        print(f"usa_hq_game: {name} {report[name]['source']} -> LOD1 {report[name]['lod1']} (budget {budget})")
        for mesh, detail in ((DETAIL_NAME, True),):
            for lod, obj in enumerate((source, lod1)):
                piece = split(obj, detail, f"{name}_{mesh}_LOD{lod}")
                if piece.data.polygons:
                    pieces[mesh][lod].append(piece)
                else:
                    bpy.data.objects.remove(piece)
        bpy.data.objects.remove(source)
        bpy.data.objects.remove(lod1)

    record = {
        "about": "Generated by Tools/Art/usa_hq_game_blender.py from the delivery's fbx/ (#358) and the baked shell (#373); "
                 "read by usa_hq_recipe.py.",
        "meshes": {SHELL_NAME: baked_shell()},
    }
    for mesh, (lod0, lod1) in pieces.items():
        order = sorted({obj.data.materials[polygon.material_index].name for obj in lod0 for polygon in obj.data.polygons})
        joined = [join(lod0, mesh, order), join(lod1, f"{mesh}_LOD1", order)]
        for obj, path in zip(joined, (f"{mesh}.fbx", f"{mesh}_LOD1.fbx")):
            export(obj, os.path.join(GAME_DIR, path))
            if obj.name == f'{DETAIL_NAME}_LOD1':
                finalize_detail_lod1(obj, os.path.join(GAME_DIR, path))
        record["meshes"][mesh] = record_for(mesh, joined, order)
    record["sources"] = report
    with open(os.path.join(GAME_DIR, "game_meshes.json"), "w", encoding="utf-8", newline="\n") as file:
        json.dump(record, file, indent=1)
        file.write("\n")


def main_baked_shell():
    """--baked-shell: SM_HQ_Shell alone again from BAKED_SHELL, game_meshes.json's other records kept."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    path = os.path.join(GAME_DIR, "game_meshes.json")
    with open(path, encoding="utf-8") as file:
        record = json.load(file)
    record["about"] = ("Generated by Tools/Art/usa_hq_game_blender.py from the delivery's fbx/ (#358) and the baked shell "
                       "(#373); read by usa_hq_recipe.py.")
    record["meshes"][SHELL_NAME] = baked_shell()
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(record, file, indent=1)
        file.write("\n")


if __name__ == "__main__":
    main_baked_shell() if "--baked-shell" in sys.argv else main()
