"""Technical validation of the M20 Guardian's game meshes and textures (used by m20_guardian_blender.py, or on its own
over the saved .blend):

  blender -b Art/USA/vehicles/m20_guardian/source/M20_Guardian_V5.blend --python Tools/Art/m20_guardian_validate.py

Checks, per texture set: UV overlaps (UV triangles rasterised at 1024, texels covered twice), UVs outside 0..1, texel
density in pixels per metre at the game size; per texture: size, channels, the normal map's DirectX encoding, ORM ranges
and the team mask; per procedural material: what it became in the game textures, against its luminance target; and the
geometry against the previous pass (triangles per FBX, pivots and sockets). Returns markdown
lines and a list of failures.
"""

import json
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Art", "USA",
                                        "vehicles", "m20_guardian"))
TEXTURE_DIR = os.path.join(OUT_DIR, "textures")
# Texture set -> (objects sharing its atlas, game size). The tracks repeat one link's UVs on purpose and the road
# wheels, sprockets and idlers are one mesh each, instanced: those overlaps are by design and not rasterised.
SETS = {
    "M20_Hull": (["M20_Hull"], 2048),
    "M20_Turret": (["M20_Turret", "M20_Gun", "M20_Barrel", "M20_RWS"], 2048),
    "M20_Running": (["M20_RoadWheel_L1", "M20_Sprocket_L", "M20_Idler_L"], 2048),
    "M20_Track": (["M20_Track_L"], 1024),
}
GRID = 1024


def triangles(obj):
    """World-space positions and UVs of every triangle: (n, 3, 3) and (n, 3, 2)."""
    mesh = obj.data
    mesh.calc_loop_triangles()
    loops = np.empty(len(mesh.loop_triangles) * 3, np.int32)
    mesh.loop_triangles.foreach_get("loops", loops)
    verts = np.empty(len(mesh.loop_triangles) * 3, np.int32)
    mesh.loop_triangles.foreach_get("vertices", verts)
    co = np.empty(len(mesh.vertices) * 3, np.float32)
    mesh.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    matrix = np.array(obj.matrix_world)
    world = co @ matrix[:3, :3].T + matrix[:3, 3]
    uv = np.empty(len(mesh.loops) * 2, np.float32)
    mesh.uv_layers.active.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    return world[verts].reshape(-1, 3, 3), uv[loops].reshape(-1, 3, 2)


def rasterise(uvs, grid=GRID):
    """How many triangles cover each texel centre (strict inside test, so shared edges do not count twice)."""
    count = np.zeros((grid, grid), np.uint16)
    pixels = uvs * grid
    for a, b, c in pixels:
        x0, y0 = np.floor(np.minimum(np.minimum(a, b), c)).astype(int)
        x1, y1 = np.ceil(np.maximum(np.maximum(a, b), c)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, grid), min(y1, grid)
        if x1 <= x0 or y1 <= y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(area) < 1e-12:
            continue
        w0 = ((b[0] - xs) * (c[1] - ys) - (b[1] - ys) * (c[0] - xs)) / area
        w1 = ((c[0] - xs) * (a[1] - ys) - (c[1] - ys) * (a[0] - xs)) / area
        w2 = 1.0 - w0 - w1
        inside = (w0 > 1e-6) & (w1 > 1e-6) & (w2 > 1e-6)
        count[y0:y1, x0:x1] += inside.astype(np.uint16)
    return count


def uv_checks(lines, failures):
    lines += ["", "### UV layout", "",
              "| Set | Objects | Triangles | UV coverage | Overlapping texels | Outside 0..1 | Texel density "
              "(px/m, median / 10th pct, game size) |", "|---|---|---:|---:|---:|---:|---:|"]
    for name, (objects, game) in SETS.items():
        found = [bpy.data.objects[o] for o in objects if o in bpy.data.objects]
        if not found:
            continue
        world, uvs = zip(*(triangles(o) for o in found))
        world, uvs = np.concatenate(world), np.concatenate(uvs)
        outside = int(np.count_nonzero((uvs < -1e-4) | (uvs > 1.0001)))
        world_area = 0.5 * np.linalg.norm(np.cross(world[:, 1] - world[:, 0], world[:, 2] - world[:, 0]), axis=1)
        e1, e2 = uvs[:, 1] - uvs[:, 0], uvs[:, 2] - uvs[:, 0]
        uv_area = 0.5 * np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0])
        valid = world_area > 1e-6
        density = np.sqrt(uv_area[valid] / world_area[valid]) * game
        order = np.argsort(density)
        cumulative = np.cumsum(world_area[valid][order]) / world_area[valid].sum()
        median = density[order][np.searchsorted(cumulative, 0.5)]
        p10 = density[order][np.searchsorted(cumulative, 0.1)]
        if name == "M20_Track":
            # One link's UVs repeated: check the first link only.
            n = len(uvs)
            first = next((p for p in range(1, n) if n % p == 0 and np.allclose(uvs[p:], uvs[:-p], atol=1e-5)), n)
            coverage_uvs = uvs[:first]
            overlap_note = "tiled by design" + (" (1 link checked)" if first < n else "")
        else:
            coverage_uvs = uvs
            overlap_note = None
        count = rasterise(coverage_uvs)
        overlap = int(np.count_nonzero(count > 1))
        coverage = np.count_nonzero(count) / count.size
        if overlap > count.size * 0.0005 and not overlap_note:
            failures.append(f"{name}: {overlap} overlapping texels")
        if outside:
            failures.append(f"{name}: {outside} UVs outside 0..1")
        overlap_text = f"{overlap}" + (f" ({overlap_note})" if overlap_note else "")
        lines.append(f"| {name} | {', '.join(objects)} | {len(uvs):,} | {coverage:.0%} | {overlap_text} | {outside} | "
                     f"{median:.0f} / {p10:.0f} |")


def load(path):
    image = bpy.data.images.load(path, check_existing=False)
    image.colorspace_settings.name = "Non-Color"
    data = np.empty(len(image.pixels), np.float32)
    image.pixels.foreach_get(data)
    info = (image.size[0], image.size[1], image.channels, image.depth)
    bpy.data.images.remove(image)
    return data.reshape(-1, 4), info


def texture_checks(lines, failures):
    lines += ["", "### Textures", "",
              "| Texture | Size | Channels / bits | Colour space | Check |", "|---|---:|---|---|---|"]
    for folder, label in ((TEXTURE_DIR, "game"), (os.path.join(TEXTURE_DIR, "source"), "source")):
        for name, (_, game) in SETS.items():
            for kind in ("BC", "N", "ORM"):
                path = os.path.join(folder, f"T_{name}_{kind}.png")
                if not os.path.exists(path):
                    failures.append(f"missing {path}")
                    continue
                data, (width, height, channels, depth) = load(path)
                if label == "game" and width != game:
                    failures.append(f"{path}: {width} px, expected {game}")
                if kind == "BC":
                    team = float(np.mean(data[:, 3] > 0.5))
                    check = f"sRGB albedo mean {np.mean(data[:, :3]):.2f}, team mask on {team:.1%} of texels"
                    space = "sRGB"
                elif kind == "N":
                    r, g, b = (float(np.median(data[:, i])) for i in range(3))
                    ok = abs(r - 0.5) < 0.03 and abs(g - 0.5) < 0.03 and b > 0.9
                    check = f"DirectX tangent space, median ({r:.2f}, {g:.2f}, {b:.2f})" + ("" if ok else " FAILED")
                    if not ok:
                        failures.append(f"{path}: normal map not centred")
                    space = "linear"
                else:
                    metal = data[:, 2]
                    partial = float(np.mean((metal > 0.15) & (metal < 0.85)))
                    rough = data[:, 1]
                    check = (f"AO {np.min(data[:, 0]):.2f}-{np.max(data[:, 0]):.2f}, roughness "
                             f"{np.percentile(rough, 1):.2f}-{np.percentile(rough, 99):.2f}, metal partial "
                             f"{partial:.1%}")
                    space = "linear"
                lines.append(f"| {label}/T_{name}_{kind} | {width}x{height} | {channels} / {depth // channels} | "
                             f"{space} | {check} |")


def material_checks(lines, failures):
    """What each procedural material became in the game textures: area-weighted sRGB albedo, roughness and metallic,
    sampled at four points per face (centroid and halfway to each corner) and grouped by the face attribute src_mat.
    Catches a material that baked outside its intended range or landed on the wrong faces."""
    import m20_guardian_materials as mats
    lines += ["", "### Materials as baked (game textures)", "",
              "| Material | Area (m²) | sRGB albedo (R, G, B) | Luminance | Roughness | Metallic | Target |",
              "|---|---:|---|---:|---:|---:|---|"]
    totals = {}
    for name, (objects, game) in SETS.items():
        found = [bpy.data.objects[o] for o in objects if o in bpy.data.objects]
        if not found:
            continue
        bc, _ = load(os.path.join(TEXTURE_DIR, f"T_{name}_BC.png"))
        orm, _ = load(os.path.join(TEXTURE_DIR, f"T_{name}_ORM.png"))
        bc, orm = bc.reshape(game, game, 4), orm.reshape(game, game, 4)
        for obj in found:
            if "src_mat" not in obj.data.attributes:
                continue
            mesh = obj.data
            mesh.calc_loop_triangles()
            world, uvs = triangles(obj)
            polygon = np.empty(len(mesh.loop_triangles), np.int32)
            mesh.loop_triangles.foreach_get("polygon_index", polygon)
            source = np.empty(len(mesh.polygons), np.int32)
            mesh.attributes["src_mat"].data.foreach_get("value", source)
            source = source[polygon]
            area = 0.5 * np.linalg.norm(np.cross(world[:, 1] - world[:, 0], world[:, 2] - world[:, 0]), axis=1)
            centre = uvs.mean(axis=1)
            points = [centre] + [(centre + uvs[:, i]) * 0.5 for i in range(3)]
            colour = np.zeros((len(uvs), 3))
            rough = np.zeros(len(uvs))
            metal = np.zeros(len(uvs))
            for p in points:
                px = np.clip((np.mod(p, 1.0) * game).astype(int), 0, game - 1)
                colour += bc[px[:, 1], px[:, 0], :3] / len(points)
                rough += orm[px[:, 1], px[:, 0], 1] / len(points)
                metal += orm[px[:, 1], px[:, 0], 2] / len(points)
            for index in np.unique(source):
                pick = source == index
                entry = totals.setdefault(mats.SLOTS[index], np.zeros(6))
                weight = area[pick]
                entry[0] += weight.sum()
                entry[1:4] += (colour[pick] * weight[:, None]).sum(axis=0)
                entry[4] += (rough[pick] * weight).sum()
                entry[5] += (metal[pick] * weight).sum()
    for material in mats.SLOTS:
        if material not in totals or totals[material][0] <= 0:
            continue
        area, r, g, b, rough, metal = totals[material]
        r, g, b, rough, metal = r / area, g / area, b / area, rough / area, metal / area
        luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b
        target = mats.TARGETS.get(material)
        verdict = ""
        if target:
            lo, hi = target
            verdict = f"{lo:.2f}-{hi:.2f}" + ("" if lo <= luminance <= hi else " OUT")
            if not lo <= luminance <= hi:
                failures.append(f"{material} luminance {luminance:.2f} outside {lo:.2f}-{hi:.2f}")
        lines.append(f"| {material} | {area:.2f} | ({r:.2f}, {g:.2f}, {b:.2f}) | {luminance:.2f} | {rough:.2f} | "
                     f"{metal:.2f} | {verdict} |")


def geometry_checks(lines, failures, previous="_v4"):
    lines += ["", "### Geometry against the previous pass", ""]
    for file_name in ("export_triangles.json", "m20_guardian.json"):
        now = json.load(open(os.path.join(OUT_DIR, file_name)))
        before_path = os.path.join(OUT_DIR, previous, file_name)
        if not os.path.exists(before_path):
            lines.append(f"- {file_name}: no {previous} baseline")
            continue
        before = json.load(open(before_path))
        same = now == before
        lines.append(f"- {file_name}: {'identical to ' + previous if same else 'DIFFERENT from ' + previous}")
        if not same:
            failures.append(f"{file_name} differs from {previous}")
    total = sum(json.load(open(os.path.join(OUT_DIR, "export_triangles.json"))).values())
    lines.append(f"- Exported triangles (one of each FBX): {total:,}")


def validate():
    lines, failures = ["", "## Validation", ""], []
    uv_checks(lines, failures)
    texture_checks(lines, failures)
    material_checks(lines, failures)
    geometry_checks(lines, failures)
    lines += ["", f"**Result: {'PASS' if not failures else 'FAIL: ' + '; '.join(failures)}**", ""]
    print("\n".join(lines))
    return lines, failures


if __name__ == "__main__":
    validate()
