"""Materials for the M20 Guardian (used by m20_guardian_blender.py).

Two sets:
- Procedural Cycles materials, one per surface kind. They are the bake source only, never exported:
  - Paint, Olive, Blue: painted composite armour. Base colour with a low-frequency drift, macro clouds (darker grimy
    and lighter sun-faded areas a metre across), a value and hue shift per plate (the high mesh's islands), mottling,
    dusty speckle and grain; then grime packed into seams and round bolts with a dirt halo, streaks on vertical faces,
    dust low down and as a film on top faces, spray and mud flecks kicked up by the tracks, heat round the exhaust
    (Paint only); then a lighter rubbed band on exposed edges, chips through to the coat underneath (primer on the
    tan, tan on the overpainted blue), primer and dull steel on the most worn rims, and scuffs.
  - Barrel: the gun's thermal sleeve, toned per section, dark clamp bands, a heat-blackened muzzle.
  - Metal: worn gunmetal for mechanisms, rails and handles, polished where it is touched. Dark: phosphate-black
    fittings, grilles, cables. Canvas: webbing straps and the tarp.
  - Rubber: tyres, pads and boots. Track: dark steel shoes with packed dirt, rubbed bright only on contact faces.
  - Optics: coated sight glass with a retaining ring, a coating ring and a dished, tinted, partly metallic centre.
  - LightWhite, LightRed: lamps, the only emissive faces.
  High-only detail (bolts, welds) carries the face attribute "detail" = 1: it takes its plate's paint a little darker,
  no plate-to-plate shift and half the edge wear, so fasteners read as dark dots and not bright ones.
  Each bake-source material has an Emission node that the baker points at one channel (colour, roughness, metallic,
  team mask) at a time.
- Game materials, one per texture set (M20_Hull, M20_Turret, M20_Running, M20_Track) plus M20_Emissive, which reads the
  lamp colours from the mesh's colour attribute. These are the slots the FBX files carry.
"""

import bpy

# Material slots in the order every object lists them; the face attribute src_mat indexes this.
SLOTS = ("Paint", "Olive", "Blue", "Metal", "Rubber", "Track", "Dark", "Optics", "LightWhite", "LightRed", "Barrel",
         "Canvas")
# The range each material should average to in the game BaseColor (sRGB luminance, area-weighted), checked by
# m20_guardian_validate.py: tan armour, darker khaki, the blue ID, gunmetal, near-black rubber and glass, dark track.
TARGETS = {
    "Paint": (0.30, 0.46), "Olive": (0.22, 0.34), "Blue": (0.18, 0.32), "Metal": (0.25, 0.42), "Rubber": (0.08, 0.22),
    "Track": (0.12, 0.24), "Dark": (0.10, 0.22), "Optics": (0.03, 0.20), "Barrel": (0.24, 0.42), "Canvas": (0.18, 0.36),
}

# Linear colours shared by the weathering.
PRIMER = (0.085, 0.07, 0.05, 1.0)
DULL_STEEL = (0.20, 0.195, 0.185, 1.0)
DUST = (0.29, 0.235, 0.155, 1.0)
PALE_DUST = (0.35, 0.29, 0.20, 1.0)
GRIME = (0.022, 0.017, 0.012, 1.0)
DIRT = (0.06, 0.047, 0.032, 1.0)
STREAK = (0.045, 0.036, 0.025, 1.0)
MUD = (0.085, 0.068, 0.048, 1.0)
SOOT = (0.022, 0.02, 0.018, 1.0)
HEAT = (0.075, 0.05, 0.035, 1.0)
OXIDE = (0.075, 0.045, 0.028, 1.0)
EXHAUST = (-3.34, 0.0, 0.97)  # centre of the rear exhaust grille, world metres

# Painted surfaces: base, drift colour, roughness, weathering weights, team mask, and what a chip shows first (the coat
# under it: primer on the tan, the tan on the overpainted blue ID panels).
PAINTS = {
    "Paint": dict(base=(0.17, 0.125, 0.072, 1.0), drift=(0.12, 0.105, 0.07, 1.0), rough=0.62, dirt=1.0, wear=1.0,
                  panel=1.0, heat=1.0, mask=0.0, under=PRIMER),
    "Olive": dict(base=(0.095, 0.085, 0.058, 1.0), drift=(0.075, 0.072, 0.052, 1.0), rough=0.64, dirt=0.95,
                  wear=0.85, panel=1.0, heat=0.0, mask=0.0, under=PRIMER),
    "Blue": dict(base=(0.026, 0.066, 0.30, 1.0), drift=(0.03, 0.06, 0.22, 1.0), rough=0.56, dirt=0.55, wear=0.7,
                 panel=0.5, heat=0.0, mask=1.0, under=(0.15, 0.115, 0.068, 1.0), fleck=0.3),
}
# The gun (set by m20_guardian_blender.py before make_procedural): the thermal sleeve's clamp bands, where the sleeve
# ends and the muzzle, in world metres along the bore.
BARREL = dict(bands=(), sleeve_end=4.28, muzzle=4.50)
# Lamps: (base colour, roughness, emission colour, emission strength). Their colours also go into the mesh's colour
# attribute for M20_Emissive.
EMISSIVE = {
    "LightWhite": ((0.9, 0.85, 0.7, 1.0), 0.2, (1.0, 0.85, 0.55, 1.0), 3.0),
    "LightRed": ((0.5, 0.02, 0.02, 1.0), 0.2, (1.0, 0.05, 0.03, 1.0), 3.0),
}
EMISSIVE_STRENGTH = 4.0

# Material name -> {"color", "roughness", "metallic", "mask", "surface": sockets; "emission", "out": nodes}.
CHANNELS = {}


class Graph:
    """Small helpers over one node tree."""

    def __init__(self, material):
        material.use_nodes = True
        self.tree = material.node_tree
        self.nodes, self.links = self.tree.nodes, self.tree.links
        self.coords = self.nodes.new("ShaderNodeTexCoord")
        self.geometry = self.nodes.new("ShaderNodeNewGeometry")

    def link(self, source, target):
        if isinstance(source, (int, float)):
            target.default_value = source
        elif isinstance(source, tuple):
            target.default_value = source
        else:
            self.links.new(source, target)

    def noise(self, scale, detail=4.0, roughness=0.5, stretch=None, vector=None):
        node = self.nodes.new("ShaderNodeTexNoise")
        node.inputs["Scale"].default_value = scale
        node.inputs["Detail"].default_value = detail
        node.inputs["Roughness"].default_value = roughness
        source = vector or self.coords.outputs["Object"]
        if stretch:
            mapping = self.nodes.new("ShaderNodeMapping")
            mapping.inputs["Scale"].default_value = stretch
            self.links.new(source, mapping.inputs["Vector"])
            source = mapping.outputs["Vector"]
        self.links.new(source, node.inputs["Vector"])
        return node.outputs["Fac"]

    def map_range(self, source, from_min, from_max, to_min=0.0, to_max=1.0, smooth=False):
        node = self.nodes.new("ShaderNodeMapRange")
        if smooth:
            node.interpolation_type = "SMOOTHSTEP"
        for name, value in (("From Min", from_min), ("From Max", from_max), ("To Min", to_min), ("To Max", to_max)):
            node.inputs[name].default_value = value
        self.link(source, node.inputs["Value"])
        return node.outputs["Result"]

    def math(self, operation, a, b=0.0, clamp=True):
        node = self.nodes.new("ShaderNodeMath")
        node.operation = operation
        node.use_clamp = clamp
        self.link(a, node.inputs[0])
        self.link(b, node.inputs[1])
        return node.outputs["Value"]

    def mul(self, *values, clamp=True):
        """Product; clamp=False keeps signed values (the Math node clamps to 0..1 by default)."""
        result = values[0]
        for value in values[1:]:
            result = self.math("MULTIPLY", result, value, clamp=clamp)
        return result

    def add(self, *values):
        result = values[0]
        for value in values[1:]:
            result = self.math("ADD", result, value, clamp=False)
        return result

    def mix(self, a, b, factor, data_type="RGBA", blend="MIX"):
        node = self.nodes.new("ShaderNodeMix")
        node.data_type = data_type
        node.blend_type = blend
        kind = "Color" if data_type == "RGBA" else "Float"

        def socket(sockets, identifier):
            return next(s for s in sockets if s.identifier == identifier)
        self.link(a, socket(node.inputs, f"A_{kind}"))
        self.link(b, socket(node.inputs, f"B_{kind}"))
        self.link(factor, socket(node.inputs, "Factor_Float"))
        return socket(node.outputs, f"Result_{kind}")

    def mixf(self, a, b, factor):
        return self.mix(a, b, factor, "FLOAT")

    def value(self, number):
        node = self.nodes.new("ShaderNodeValue")
        node.outputs[0].default_value = number
        return node.outputs[0]

    def hsv(self, colour, hue=0.5, saturation=1.0, value=1.0):
        node = self.nodes.new("ShaderNodeHueSaturation")
        self.link(colour, node.inputs["Color"])
        self.link(hue, node.inputs["Hue"])
        self.link(saturation, node.inputs["Saturation"])
        self.link(value, node.inputs["Value"])
        return node.outputs["Color"]

    def white_noise(self, w):
        node = self.nodes.new("ShaderNodeTexWhiteNoise")
        node.noise_dimensions = "1D"
        self.link(w, node.inputs["W"])
        return node.outputs["Value"]

    def ao(self, distance, samples=16, inside=False):
        node = self.nodes.new("ShaderNodeAmbientOcclusion")
        node.inputs["Distance"].default_value = distance
        node.samples = samples
        node.inside = inside
        return node.outputs["AO"]


class Masks:
    """The geometric inputs every surface shares: height, facing, edges, cavities, plates and detail."""

    def __init__(self, g, edge_radius=0.012, convex_distance=0.012):
        position = g.nodes.new("ShaderNodeSeparateXYZ")
        g.links.new(g.geometry.outputs["Position"], position.inputs[0])
        normal = g.nodes.new("ShaderNodeSeparateXYZ")
        g.links.new(g.geometry.outputs["Normal"], normal.inputs[0])
        self.x, self.z = position.outputs["X"], position.outputs["Z"]
        self.nz = normal.outputs["Z"]
        self.vertical = g.map_range(g.math("ABSOLUTE", self.nz), 0.15, 0.5, 1.0, 0.0)
        self.top = g.map_range(self.nz, 0.55, 0.95, 0.0, 1.0)
        self.down = g.map_range(self.nz, -0.4, -0.85, 0.0, 1.0)
        # Ground proximity: 1 at the tracks, 0 above the skirts.
        self.low = g.map_range(self.z, 0.30, 1.45, 1.0, 0.0, smooth=True)

        def edge_mask(radius, lo, hi):
            bevel = g.nodes.new("ShaderNodeBevel")
            bevel.inputs["Radius"].default_value = radius
            bevel.samples = 8
            dot = g.nodes.new("ShaderNodeVectorMath")
            dot.operation = "DOT_PRODUCT"
            g.links.new(bevel.outputs["Normal"], dot.inputs[0])
            g.links.new(g.geometry.outputs["Normal"], dot.inputs[1])
            return g.map_range(dot.outputs["Value"], lo, hi, 1.0, 0.0), bevel
        sharp, self.bevel = edge_mask(edge_radius, 0.86, 0.985)
        # The high meshes carry real rounded bevels, which the Bevel node barely sees: convexity from AO cast inside
        # the mesh finds them (rays leave through the neighbouring face within a centimetre of a convex edge).
        inside = g.ao(convex_distance, 12, inside=True)
        convex = g.map_range(inside, 0.55, 0.9, 1.0, 0.0)
        self.edge = g.math("MAXIMUM", sharp, convex)
        near, _ = edge_mask(0.045, 0.80, 0.97)
        self.near_edge = g.math("MAXIMUM", near, g.map_range(inside, 0.7, 0.98, 1.0, 0.0))

        self.cavity = g.map_range(g.ao(0.22, 16), 0.30, 0.95, 1.0, 0.0)
        self.crevice = g.map_range(g.ao(0.05, 12), 0.35, 0.92, 1.0, 0.0)

        self.island = g.geometry.outputs["Random Per Island"]
        attribute = g.nodes.new("ShaderNodeAttribute")
        attribute.attribute_type = "GEOMETRY"
        attribute.attribute_name = "detail"
        self.detail = attribute.outputs["Fac"]
        self.plate = g.math("SUBTRACT", 1.0, self.detail)

        # Distance from the exhaust grille, for heat and soot on the rear plate.
        offset = g.nodes.new("ShaderNodeVectorMath")
        offset.operation = "DISTANCE"
        g.links.new(g.geometry.outputs["Position"], offset.inputs[0])
        offset.inputs[1].default_value = EXHAUST
        rear = g.map_range(self.x, -3.05, -3.28, 0.0, 1.0)
        self.heat = g.mul(g.map_range(offset.outputs["Value"], 0.30, 0.85, 1.0, 0.0, smooth=True), rear)


def bake_outputs(graph, name, shader_out, color, roughness, metallic, mask):
    """An Emission node the baker plugs into the material output, pointed at one channel at a time."""
    emission = graph.nodes.new("ShaderNodeEmission")
    surface = shader_out.inputs["Surface"].links[0].from_socket
    CHANNELS[name] = {"color": color, "roughness": roughness, "metallic": metallic, "mask": mask,
                      "emission": emission, "surface": surface, "out": shader_out, "graph": graph}


def finish(g, name, colour, rough, metal, height, mask, bump_strength=0.3, bevel=None, bump_distance=0.0005):
    """Wires the channels into the Principled BSDF (for presentation) and the bake outputs."""
    shader = g.nodes["Principled BSDF"]
    bump = g.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = bump_strength
    bump.inputs["Distance"].default_value = bump_distance
    g.link(height, bump.inputs["Height"])
    if bevel:
        g.links.new(bevel.outputs["Normal"], bump.inputs["Normal"])
    g.link(colour, shader.inputs["Base Color"])
    g.link(rough, shader.inputs["Roughness"])
    g.link(metal, shader.inputs["Metallic"])
    g.links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    bake_outputs(g, name, g.nodes["Material Output"], colour, rough, metal, g.value(mask))
    return bump


def weather(g, m, colour, rough, metal, dirt, heat=0.0, dust_low=1.0, dust_max=0.6, spray=1.0):
    """Deposits, streaks, dust, kicked-up spray and heat over any surface. Returns (colour, roughness, metallic, dust).
    """
    # Deposits: dark grime packed into seams, round bolts and in corners, a softer dirt halo round them.
    tight = g.map_range(g.mul(m.crevice, g.map_range(g.noise(5.0, 4.0, 0.6), 0.3, 0.7, 0.6, 1.0)), 0.05, 0.55)
    deposit = g.mul(g.math("MAXIMUM", g.mul(m.cavity, 0.75), tight), dirt)
    halo = g.mul(g.map_range(m.cavity, 0.0, 0.5), g.map_range(g.noise(3.0, 5.0, 0.6), 0.35, 0.65, 0.3, 0.8), dirt)
    # Darken blends: grime never lightens a surface that is already darker than it (rubber, black fittings).
    colour = g.mix(colour, DIRT, g.mul(halo, 0.55), blend="DARKEN")
    colour = g.mix(colour, GRIME, g.mul(deposit, 0.9), blend="DARKEN")
    rough = g.mixf(rough, 0.84, g.mul(deposit, 0.7))
    # Road grime over the lower hull and skirts: a darker band under the dust.
    road = g.mul(m.low, g.map_range(g.noise(1.4, 5.0, 0.6), 0.3, 0.7, 0.35, 1.0), dirt, spray, 0.5)
    colour = g.mix(colour, DIRT, road, blend="DARKEN")
    # Streaks on vertical faces, broken into runs, a few long ones under ledges.
    runs = g.map_range(g.noise(3.0, 3.0, 0.55, stretch=(9.0, 9.0, 0.45)), 0.52, 0.80, 0.0, 0.5)
    streaks = g.mul(runs, m.vertical, g.map_range(g.noise(0.9, 2.0), 0.4, 0.6, 0.3, 1.0), dirt)
    colour = g.mix(colour, STREAK, streaks, blend="DARKEN")
    rough = g.mixf(rough, 0.78, g.mul(streaks, 0.5))
    # Dust: thick near the ground and in the running gear, a light film on top faces that gathers against walls.
    patches = g.map_range(g.noise(2.0, 6.0, 0.62), 0.34, 0.62, 0.2, 1.0)
    speckle = g.map_range(g.noise(60.0, 2.0, 0.6), 0.3, 0.7, 0.7, 1.0)
    low = g.mul(m.low, patches, speckle, dust_low)
    film = g.mul(m.top, g.map_range(g.noise(7.0, 5.0, 0.6), 0.40, 0.72, 0.05, 0.4),
                 g.math("ADD", 0.3, g.mul(m.cavity, 1.6)))
    # Spray kicked up by the tracks: vertical runs that rise from the lower edges and fade out by the skirt top.
    rise = g.map_range(g.noise(4.0, 4.0, 0.6, stretch=(14.0, 14.0, 0.8)), 0.42, 0.72, 0.0, 1.0)
    spray_mask = g.mul(rise, g.map_range(m.z, 0.45, 1.15, 1.0, 0.0, smooth=True), m.vertical, spray)
    dust = g.mul(g.math("MAXIMUM", g.math("MAXIMUM", low, film), g.mul(spray_mask, 0.8)), dirt, dust_max)
    pale = g.map_range(g.noise(11.0, 3.0), 0.35, 0.65)
    colour = g.mix(colour, g.mix(DUST, PALE_DUST, pale), dust)
    rough = g.mixf(rough, 0.93, dust)
    metal = g.mixf(metal, 0.0, g.math("MULTIPLY", dust, 1.4))
    # Mud flecks low down, thrown by the tracks.
    flecks = g.mul(g.map_range(g.noise(38.0, 3.0, 0.7), 0.66, 0.72, 0.0, 1.0),
                   g.map_range(m.z, 0.35, 0.9, 1.0, 0.0), dirt, spray)
    colour = g.mix(colour, MUD, g.mul(flecks, 0.8))
    rough = g.mixf(rough, 0.9, flecks)
    if heat:
        hot = g.mul(m.heat, heat)
        colour = g.mix(colour, HEAT, g.mul(hot, 0.65))
        colour = g.mix(colour, SOOT, g.mul(hot, g.map_range(g.noise(6.0, 4.0), 0.4, 0.65, 0.2, 0.75),
                                           g.math("ADD", 0.4, m.vertical)))
        rough = g.mixf(rough, 0.78, hot)
    return colour, rough, metal, dust


def painted_base(g, m, base, drift, rough, panel, fleck=1.0):
    """Paint before weathering: a low-frequency drift, sun-faded and dusty clouds over the whole vehicle, a value and
    hue shift per plate, mottling and grain. Returns (colour, roughness)."""
    colour = g.mix(base, drift, g.map_range(g.noise(0.7, 4.0, 0.5), 0.40, 0.68, 0.0, 0.55))
    plate_shift = g.mul(g.math("SUBTRACT", m.island, 0.5, clamp=False), 0.34 * panel, m.plate, clamp=False)
    hue_shift = g.mul(g.math("SUBTRACT", g.math("FRACT", g.mul(m.island, 7.31), clamp=False), 0.5, clamp=False),
                      0.016 * panel, m.plate, clamp=False)
    grain = g.map_range(g.noise(45.0, 4.0, 0.6), 0.3, 0.7, -0.06, 0.06)
    mottle = g.add(g.map_range(g.noise(9.0, 4.0, 0.5), 0.35, 0.65, -0.06, 0.06),
                   g.map_range(g.noise(2.5, 3.0, 0.5), 0.38, 0.62, -0.10, 0.07))
    # Macro clouds, 0.5 to 1.5 m across: darker grimy and lighter sun-faded areas that break up every large plate.
    clouds = g.noise(0.9, 6.0, 0.62)
    macro = g.map_range(clouds, 0.34, 0.66, -0.24, 0.12)
    value = g.add(1.0, plate_shift, grain, mottle, macro)
    colour = g.hsv(colour, g.math("ADD", 0.5, hue_shift, clamp=False), 1.0, value)
    # Speckle as on the sheet: pale dusty flecks and dark grimy ones, in irregular patches.
    patches = g.map_range(g.noise(1.8, 4.0), 0.35, 0.65, 0.2, 0.7)
    speckle = g.mul(g.map_range(g.noise(34.0, 5.0, 0.7), 0.54, 0.68, 0.0, 1.0), patches, fleck)
    dark_fleck = g.mul(g.map_range(g.noise(52.0, 4.0, 0.7, vector=g.geometry.outputs["Position"]), 0.56, 0.70, 0.0,
                                   1.0), patches, fleck)
    colour = g.mix(colour, g.hsv(colour, 0.5, 0.6, 1.4), speckle)
    colour = g.mix(colour, g.hsv(colour, 0.5, 1.05, 0.55), g.mul(dark_fleck, 0.8))
    roughness = g.add(rough, g.mul(g.math("SUBTRACT", m.island, 0.5, clamp=False), 0.10, m.plate, clamp=False),
                      g.map_range(g.noise(6.0, 3.0), 0.3, 0.7, -0.06, 0.06),
                      g.map_range(g.noise(22.0, 4.0), 0.3, 0.7, -0.06, 0.06),
                      g.map_range(clouds, 0.36, 0.64, 0.06, -0.06), g.mul(speckle, 0.08))
    return colour, roughness


def wear_paint(g, m, colour, roughness, metal, wear, under, low_chips=0.45):
    """Edge abrasion (a lighter rubbed band, the coat under it, primer, dull steel), chips and scuffs. Returns
    (colour, roughness, metallic, damage) where damage is where the paint is broken."""
    breakup = g.map_range(g.noise(26.0, 5.0, 0.62), 0.40, 0.62, 0.0, 1.0)
    exposure = g.map_range(g.noise(1.1, 2.0), 0.35, 0.65, 0.5, 1.0)
    edge_wear = g.mul(m.edge, breakup, exposure, g.math("SUBTRACT", 1.0, g.mul(m.detail, 0.5)), wear)
    # Rubbed: the whole exposed rim, not broken up as hard, so edges catch the light at range.
    rubbed = g.mul(m.edge, g.map_range(g.noise(9.0, 3.0), 0.3, 0.7, 0.45, 1.0), exposure, wear)
    coat = g.map_range(edge_wear, 0.30, 0.45, 0.0, 1.0)
    primer = g.map_range(edge_wear, 0.48, 0.62, 0.0, 1.0)
    steel = g.map_range(edge_wear, 0.66, 0.85, 0.0, 1.0)
    flakes = g.map_range(g.noise(48.0, 2.0, 0.7), 0.70, 0.735, 0.0, 1.0)
    chip_bias = g.math("ADD", g.mul(m.near_edge, 0.75), g.mul(m.low, low_chips))
    chips = g.mul(flakes, g.map_range(chip_bias, 0.15, 0.7, 0.0, 1.0), m.plate, wear)
    voronoi = g.nodes.new("ShaderNodeTexVoronoi")
    voronoi.feature = "DISTANCE_TO_EDGE"
    voronoi.inputs["Scale"].default_value = 7.0
    mapping = g.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0, 1.0, 2.5)
    g.links.new(g.coords.outputs["Object"], mapping.inputs["Vector"])
    g.links.new(mapping.outputs["Vector"], voronoi.inputs["Vector"])
    scuff = g.mul(g.map_range(voronoi.outputs["Distance"], 0.0, 0.006, 1.0, 0.0),
                  g.map_range(g.noise(2.0, 2.0), 0.60, 0.68, 0.0, 1.0), m.plate, 0.7 * wear)

    colour = g.mix(colour, g.hsv(colour, 0.5, 0.7, 1.5), g.mul(rubbed, 0.75))
    colour = g.mix(colour, g.hsv(colour, 0.5, 0.75, 1.3), scuff)
    colour = g.mix(colour, under, g.math("MAXIMUM", coat, g.mul(chips, 0.6)))
    colour = g.mix(colour, PRIMER, g.math("MAXIMUM", primer, g.mul(chips, 0.5)))
    colour = g.mix(colour, DULL_STEEL, steel)
    roughness = g.mixf(roughness, 0.50, g.math("MAXIMUM", scuff, g.mul(rubbed, 0.6)))
    roughness = g.mixf(roughness, 0.72, g.math("MAXIMUM", chips, primer))
    roughness = g.mixf(roughness, 0.38, steel)
    metal = g.mixf(metal, 1.0, steel)
    damage = g.math("MAXIMUM", g.math("MAXIMUM", chips, coat), scuff)
    return colour, roughness, metal, damage


def paint(name, base, drift, rough, dirt, wear, panel, heat, mask, under, fleck=1.0):
    """Painted armour: plate-to-plate variation, clouds, grain, weathering, then edge wear."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g)
    colour, roughness = painted_base(g, m, base, drift, rough, panel, fleck)
    metal = g.value(0.0)
    colour, roughness, metal, dust = weather(g, m, colour, roughness, metal, dirt, heat)
    # Bolts and welds: a little darker than their plate, so they read as the sheet's dark dots.
    colour = g.mix(colour, g.hsv(colour, 0.5, 1.0, 0.62), g.mul(m.detail, 0.8))
    colour, roughness, metal, damage = wear_paint(g, m, colour, roughness, metal, wear, under)
    # Height: orange peel and grain; flakes and worn rims dip below the paint.
    height = g.add(g.mul(g.noise(90.0, 3.0, 0.55), 0.18), g.mul(g.noise(30.0, 4.0), 0.22),
                   g.math("SUBTRACT", 1.0, damage))
    finish(g, name, colour, roughness, metal, height, mask, 0.6, m.bevel)
    return material


def barrel(name):
    """The gun's thermal sleeve: sections in slightly different tan tones, dark clamp bands, a darker fume extractor
    and a heat-blackened muzzle with soot inside it."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g)
    args = PAINTS["Paint"]
    x = m.x  # the gun bakes at rest, its bore along world X
    colour, roughness = painted_base(g, m, args["base"], args["drift"], 0.58, 0.4)
    # Each sleeve section between two bands gets its own tone.
    section = g.value(0.0)
    for band in BARREL["bands"]:
        section = g.math("ADD", section, g.math("GREATER_THAN", x, band), clamp=False)
    tone = g.map_range(g.white_noise(section), 0.0, 1.0, 0.86, 1.1)
    colour = g.hsv(colour, 0.5, g.map_range(g.white_noise(g.math("ADD", section, 7.0, clamp=False)), 0.0, 1.0, 0.85, 1.1), tone)
    metal = g.value(0.0)
    colour, roughness, metal, dust = weather(g, m, colour, roughness, metal, 0.6, spray=0.0)
    band_mask = g.value(0.0)
    for band in BARREL["bands"]:
        band_mask = g.math("MAXIMUM", band_mask, g.map_range(g.math("ABSOLUTE", g.math("SUBTRACT", x, band, clamp=False), clamp=False),
                                                             0.022, 0.03, 1.0, 0.0))
    colour = g.mix(colour, (0.045, 0.042, 0.038, 1.0), g.mul(band_mask, 0.85))
    roughness = g.mixf(roughness, 0.5, band_mask)
    # Muzzle: the last stretch blackened by heat and blast, soot fading back along the sleeve.
    muzzle = g.map_range(x, BARREL["sleeve_end"] - 0.1, BARREL["sleeve_end"] + 0.06, 0.0, 1.0, smooth=True)
    soot = g.mul(g.map_range(x, BARREL["sleeve_end"] - 0.6, BARREL["muzzle"], 0.0, 0.7),
                 g.map_range(g.noise(5.0, 4.0), 0.3, 0.7, 0.4, 1.0))
    colour = g.mix(colour, SOOT, soot)
    gunmetal = g.mix((0.05, 0.045, 0.04, 1.0), (0.09, 0.07, 0.05, 1.0), g.map_range(g.noise(14.0, 4.0), 0.35, 0.65))
    colour = g.mix(colour, gunmetal, muzzle)
    roughness = g.mixf(roughness, 0.55, muzzle)
    metal = g.mixf(metal, 0.6, g.mul(muzzle, g.map_range(g.noise(20.0, 3.0), 0.4, 0.6, 0.4, 1.0)))
    colour, roughness, metal, damage = wear_paint(g, m, colour, roughness, metal, 0.7, PRIMER, low_chips=0.0)
    height = g.add(g.mul(g.noise(80.0, 3.0, 0.55), 0.15), g.math("SUBTRACT", 1.0, damage))
    finish(g, name, colour, roughness, metal, height, 0.0, 0.45, m.bevel)
    return material


def canvas(name):
    """Webbing straps and the rolled tarp: khaki fabric with a weave, very rough, dirty in the folds."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.006, 0.006)
    colour = g.mix((0.11, 0.095, 0.062, 1.0), (0.085, 0.078, 0.055, 1.0), g.map_range(g.noise(3.0, 4.0), 0.35, 0.65))
    colour = g.hsv(colour, 0.5, 1.0, g.map_range(m.island, 0.0, 1.0, 0.85, 1.12))
    roughness = g.map_range(g.noise(12.0, 3.0), 0.3, 0.7, 0.86, 0.96)
    colour, roughness, metal, _ = weather(g, m, colour, roughness, g.value(0.0), 1.0, dust_max=0.5, spray=0.0)
    weave = g.math("MAXIMUM", g.noise(140.0, 1.0, 0.5, stretch=(1.0, 8.0, 8.0)),
                   g.noise(140.0, 1.0, 0.5, stretch=(8.0, 1.0, 8.0)))
    finish(g, name, colour, roughness, metal, weave, 0.0, 0.35, m.bevel)
    return material


def metal(name):
    """Worn gunmetal for mechanisms, rails and handles: oily and dry patches, oxide only deep in cavities, polished
    where hands, boots and cables rub (the top of rails and handles, sharp edges)."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.008, 0.006)
    colour = g.mix((0.10, 0.095, 0.088, 1.0), (0.155, 0.148, 0.138, 1.0), g.map_range(g.noise(3.0, 5.0), 0.35, 0.65))
    oily = g.map_range(g.noise(4.0, 4.0, 0.6), 0.55, 0.70, 0.0, 1.0)
    roughness = g.math("ADD", g.map_range(g.noise(12.0, 4.0), 0.3, 0.7, 0.42, 0.58), g.mul(oily, -0.14), clamp=False)
    colour = g.mix(colour, (0.075, 0.072, 0.068, 1.0), g.mul(oily, 0.6))
    metallic = g.value(1.0)
    oxide = g.mul(m.crevice, g.map_range(g.noise(20.0, 4.0), 0.45, 0.7, 0.0, 0.7))
    colour = g.mix(colour, OXIDE, oxide)
    roughness = g.mixf(roughness, 0.75, oxide)
    metallic = g.mixf(metallic, 0.0, oxide)
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, metallic, 0.6, dust_max=0.3, spray=0.5)
    polish = g.mul(g.math("MAXIMUM", m.edge, g.mul(m.top, 0.7)), g.map_range(g.noise(30.0, 4.0), 0.4, 0.6, 0.2, 1.0))
    colour = g.mix(colour, (0.36, 0.35, 0.335, 1.0), g.mul(polish, 0.6))
    roughness = g.mixf(roughness, 0.28, g.mul(polish, 0.7))
    metallic = g.mixf(metallic, 1.0, polish)
    finish(g, name, colour, roughness, metallic, g.noise(120.0, 2.0), 0.0, 0.15, m.bevel)
    return material


def dark(name):
    """Phosphate-black fittings, grilles, cables and frames: dielectric, worn to steel on the sharpest edges."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.006, 0.006)
    colour = g.mix((0.02, 0.02, 0.019, 1.0), (0.034, 0.033, 0.03, 1.0), g.map_range(g.noise(5.0, 4.0), 0.35, 0.65))
    roughness = g.map_range(g.noise(9.0, 3.0), 0.3, 0.7, 0.55, 0.70)
    metallic = g.value(0.0)
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, metallic, 0.55, heat=1.0, dust_max=0.25)
    worn = g.map_range(g.mul(m.edge, g.map_range(g.noise(30.0, 5.0), 0.45, 0.62)), 0.6, 0.9, 0.0, 0.8)
    colour = g.mix(colour, DULL_STEEL, worn)
    roughness = g.mixf(roughness, 0.42, worn)
    metallic = g.mixf(metallic, 1.0, worn)
    finish(g, name, colour, roughness, metallic, g.noise(100.0, 2.0), 0.0, 0.15, m.bevel)
    return material


def rubber(name):
    """Rubber: near-black, matte sidewalls; contact faces worn smoother and a little lighter; dust low down."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.008, 0.006)
    colour = g.mix((0.013, 0.0125, 0.012, 1.0), (0.021, 0.02, 0.019, 1.0), g.map_range(g.noise(8.0, 4.0), 0.35, 0.65))
    roughness = g.map_range(g.noise(14.0, 3.0), 0.3, 0.7, 0.84, 0.93)
    contact = g.math("MAXIMUM", g.mul(m.down, 0.9), g.mul(m.edge, 0.5))
    colour = g.mix(colour, (0.026, 0.025, 0.023, 1.0), contact)
    roughness = g.mixf(roughness, 0.72, contact)
    # Cuts and chunking on the tread.
    cuts = g.mul(g.map_range(g.noise(70.0, 2.0, 0.7), 0.68, 0.72, 0.0, 1.0), contact)
    colour = g.mix(colour, (0.008, 0.008, 0.008, 1.0), cuts)
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, g.value(0.0), 0.7, dust_max=0.12, spray=0.4)
    height = g.math("SUBTRACT", g.noise(160.0, 2.0), cuts)
    finish(g, name, colour, roughness, metallic, height, 0.0, 0.25, m.bevel)
    return material


def track(name):
    """Track steel: dark, dirt and dust packed into the recesses, rubbed bright only where it meets the ground, the
    wheels and the sprocket."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.006, 0.006)
    colour = g.mix((0.032, 0.03, 0.027, 1.0), (0.05, 0.045, 0.04, 1.0), g.map_range(g.noise(6.0, 4.0), 0.35, 0.65))
    roughness = g.map_range(g.noise(10.0, 4.0), 0.3, 0.7, 0.55, 0.70)
    metallic = g.value(0.85)
    mud = g.mul(g.math("MAXIMUM", m.cavity, g.mul(m.crevice, 1.3)),
                g.map_range(g.noise(9.0, 5.0), 0.3, 0.6, 0.55, 1.0))
    colour = g.mix(colour, (0.042, 0.034, 0.025, 1.0), mud)
    roughness = g.mixf(roughness, 0.95, mud)
    metallic = g.mixf(metallic, 0.0, g.math("MULTIPLY", mud, 1.5))
    rust = g.mul(g.map_range(g.noise(16.0, 4.0, 0.6), 0.58, 0.72, 0.0, 0.4), g.math("SUBTRACT", 1.0, mud))
    colour = g.mix(colour, OXIDE, rust)
    roughness = g.mixf(roughness, 0.85, rust)
    metallic = g.mixf(metallic, 0.2, rust)
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, metallic, 0.6, dust_low=0.5, dust_max=0.2,
                                             spray=0.0)
    # Only the ground-contact faces and the sharpest edges are rubbed bright, in scraped streaks.
    scrape = g.map_range(g.noise(40.0, 3.0, 0.6, stretch=(0.3, 6.0, 6.0)), 0.45, 0.75, 0.0, 1.0)
    contact = g.mul(g.math("MAXIMUM", g.map_range(m.down, 0.6, 1.0), g.mul(m.edge, 0.35)), scrape)
    colour = g.mix(colour, (0.26, 0.25, 0.235, 1.0), g.mul(contact, 0.85))
    roughness = g.mixf(roughness, 0.3, contact)
    metallic = g.mixf(metallic, 1.0, contact)
    finish(g, name, colour, roughness, metallic, g.noise(90.0, 3.0), 0.0, 0.3, m.bevel)
    return material


def optics(name):
    """Coated sight glass. The housing round each window shades it (AO across the window), which gives a radial
    position: a dark anodised retaining ring at the frame, then a thin amber-green coating ring, then the deep blue-
    violet coated glass, near black at the centre. The coating is partly metallic so its reflection is tinted the way
    multi-coated optics reflect, which carries into Unreal. The same radial mask dishes the glass in the normal map."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.01)
    shade = g.ao(0.07, 24)
    rim = g.map_range(shade, 0.5, 0.97, 1.0, 0.0)
    tint = g.map_range(g.noise(1.5, 2.0), 0.35, 0.65)
    centre = g.mix((0.003, 0.004, 0.008, 1.0), (0.004, 0.003, 0.009, 1.0), tint)
    coat = g.mix((0.012, 0.02, 0.055, 1.0), (0.03, 0.012, 0.05, 1.0), tint)
    colour = g.mix(centre, coat, g.map_range(rim, 0.05, 0.45))
    ring = g.mul(g.map_range(rim, 0.40, 0.55), g.map_range(rim, 0.70, 0.58))
    colour = g.mix(colour, (0.05, 0.05, 0.016, 1.0), g.mul(ring, 0.8))
    retainer = g.math("MAXIMUM", g.map_range(rim, 0.74, 0.86), g.mul(m.edge, 0.9))
    colour = g.mix(colour, (0.012, 0.012, 0.012, 1.0), retainer)
    smudge = g.map_range(g.noise(14.0, 5.0, 0.65), 0.55, 0.75, 0.0, 1.0)
    roughness = g.math("ADD", 0.025, g.mul(smudge, 0.10))
    roughness = g.mixf(roughness, 0.48, retainer)
    metallic = g.mixf(g.value(0.45), 0.0, retainer)
    specks = g.mul(g.map_range(g.noise(60.0, 2.0), 0.68, 0.74, 0.0, 0.5), g.math("ADD", 0.3, m.low))
    colour = g.mix(colour, DUST, specks)
    roughness = g.mixf(roughness, 0.9, specks)
    metallic = g.mixf(metallic, 0.0, specks)
    height = g.math("ADD", g.mul(rim, 0.8), g.mul(retainer, 0.6))
    finish(g, name, colour, roughness, metallic, height, 0.0, 0.5, None, bump_distance=0.004)
    shader = g.nodes["Principled BSDF"]
    shader.inputs["Coat Weight"].default_value = 0.5
    shader.inputs["Coat Tint"].default_value = (0.75, 0.95, 0.9, 1.0)
    return material


def emissive(name, base, roughness, glow, strength):
    material = bpy.data.materials.new(name)
    g = Graph(material)
    shader = g.nodes["Principled BSDF"]
    shader.inputs["Base Color"].default_value = base
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Emission Color"].default_value = glow
    shader.inputs["Emission Strength"].default_value = strength
    colour = g.nodes.new("ShaderNodeRGB")
    colour.outputs[0].default_value = base
    bake_outputs(g, name, g.nodes["Material Output"], colour.outputs[0], g.value(roughness), g.value(0.0),
                 g.value(0.0))
    return material


def make_procedural():
    for name, args in PAINTS.items():
        paint(name, **args)
    metal("Metal")
    dark("Dark")
    rubber("Rubber")
    track("Track")
    optics("Optics")
    barrel("Barrel")
    canvas("Canvas")
    for name, args in EMISSIVE.items():
        emissive(name, *args)


def set_bake_channel(channel):
    """Plugs every procedural material's output into one channel through its Emission node, or back into its shader
    with channel None."""
    for entry in CHANNELS.values():
        graph, out = entry["graph"], entry["out"]
        if channel is None:
            graph.links.new(entry["surface"], out.inputs["Surface"])
            continue
        target = entry["emission"].inputs["Color"]
        for link in list(target.links):
            graph.links.remove(link)
        graph.links.new(entry[channel], target)
        entry["emission"].inputs["Strength"].default_value = 1.0
        graph.links.new(entry["emission"].outputs["Emission"], out.inputs["Surface"])


# ---- game materials ----

def textured(name, base_color, normal, orm):
    """Principled material over the baked set, built like the Unreal one: BaseColor (team mask in alpha), DirectX
    normal, ORM (AO, roughness, metallic). AO only darkens what the renderer cannot shadow itself, so it is left out
    here as Unreal leaves it to indirect light."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    shader = g.nodes["Principled BSDF"]

    def image(path, colour_space):
        node = g.nodes.new("ShaderNodeTexImage")
        node.image = bpy.data.images.load(path, check_existing=True)
        node.image.colorspace_settings.name = colour_space
        node.image.alpha_mode = "CHANNEL_PACKED"
        return node

    bc = image(base_color, "sRGB")
    g.links.new(bc.outputs["Color"], shader.inputs["Base Color"])
    packed = image(orm, "Non-Color")
    split = g.nodes.new("ShaderNodeSeparateColor")
    g.links.new(packed.outputs["Color"], split.inputs[0])
    g.links.new(split.outputs[1], shader.inputs["Roughness"])
    g.links.new(split.outputs[2], shader.inputs["Metallic"])
    # The maps are baked for Unreal (DirectX, green down); flip green back for Blender.
    nm = image(normal, "Non-Color")
    rgb = g.nodes.new("ShaderNodeSeparateColor")
    g.links.new(nm.outputs["Color"], rgb.inputs[0])
    flip = g.math("SUBTRACT", 1.0, rgb.outputs[1])
    combine = g.nodes.new("ShaderNodeCombineColor")
    g.links.new(rgb.outputs[0], combine.inputs[0])
    g.link(flip, combine.inputs[1])
    g.links.new(rgb.outputs[2], combine.inputs[2])
    normal_map = g.nodes.new("ShaderNodeNormalMap")
    g.links.new(combine.outputs[0], normal_map.inputs["Color"])
    g.links.new(normal_map.outputs["Normal"], shader.inputs["Normal"])
    return material


def emissive_game():
    """Lamps: colour from the mesh's colour attribute, glowing."""
    material = bpy.data.materials.new("M20_Emissive")
    g = Graph(material)
    shader = g.nodes["Principled BSDF"]
    attribute = g.nodes.new("ShaderNodeVertexColor")
    attribute.layer_name = "Col"
    g.links.new(attribute.outputs["Color"], shader.inputs["Base Color"])
    g.links.new(attribute.outputs["Color"], shader.inputs["Emission Color"])
    shader.inputs["Emission Strength"].default_value = EMISSIVE_STRENGTH
    shader.inputs["Roughness"].default_value = 0.1
    return material


def emissive_colour(name):
    """The colour attribute value for a lamp face (linear, scaled to its glow)."""
    glow, strength = EMISSIVE[name][2], EMISSIVE[name][3]
    scale = min(1.0, strength / EMISSIVE_STRENGTH)
    return tuple(c * scale for c in glow[:3]) + (1.0,)
