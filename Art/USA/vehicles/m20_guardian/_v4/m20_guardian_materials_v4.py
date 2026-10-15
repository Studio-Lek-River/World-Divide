"""Materials for the M20 Guardian (used by m20_guardian_blender.py).

Two sets:
- Procedural Cycles materials, one per surface kind. They are the bake source only, never exported:
  - Paint, Olive, Blue: painted composite armour. Base colour with a low-frequency olive-gray drift, a small colour and
    value shift per plate (the high mesh's islands), fine paint grain; then deposits in recesses and round bolts, short
    streaks on vertical faces, dust that builds up towards the ground and on top faces, heat discolouration round the
    exhaust (Paint only), edge abrasion through primer to dull steel on exposed edges, a few chips and scuffs.
  - Metal: oiled dark steel for mechanisms, oxidised only in cavities. Dark: phosphate-black fittings, grilles, cables.
  - Rubber: tyres, pads and boots. Track: steel shoes with packed dirt and polished ground-contact faces.
  - Optics: near-black coated glass with a faint blue-green tint, a darker rim and light smudges. Not emissive.
  - LightWhite, LightRed: lamps, the only emissive faces.
  High-only detail (bolts, welds) carries the face attribute "detail" = 1: it takes its plate's paint, no plate-to-plate
  shift and less edge wear, so fasteners do not turn into bright dots.
  Each bake-source material has an Emission node that the baker points at one channel (colour, roughness, metallic,
  team mask) at a time.
- Game materials, one per texture set (M20_Hull, M20_Turret, M20_Running, M20_Track) plus M20_Emissive, which reads the
  lamp colours from the mesh's colour attribute. These are the slots the FBX files carry.
"""

import bpy

# Linear colours shared by the weathering.
PRIMER = (0.085, 0.07, 0.05, 1.0)
DULL_STEEL = (0.20, 0.195, 0.185, 1.0)
DUST = (0.33, 0.29, 0.22, 1.0)
GRIME = (0.028, 0.022, 0.016, 1.0)
STREAK = (0.05, 0.04, 0.028, 1.0)
SOOT = (0.025, 0.022, 0.02, 1.0)
HEAT = (0.075, 0.05, 0.035, 1.0)
OXIDE = (0.075, 0.045, 0.028, 1.0)
EXHAUST = (-3.34, 0.0, 0.97)  # centre of the rear exhaust grille, world metres

# Painted surfaces: base, drift colour, roughness, weathering weights, team mask.
PAINTS = {
    "Paint": dict(base=(0.16, 0.122, 0.074, 1.0), drift=(0.125, 0.112, 0.078, 1.0), rough=0.62, dirt=1.0, wear=1.0,
                  panel=1.0, heat=1.0, mask=0.0),
    "Olive": dict(base=(0.075, 0.075, 0.058, 1.0), drift=(0.085, 0.08, 0.062, 1.0), rough=0.64, dirt=0.9, wear=0.8,
                  panel=0.7, heat=0.0, mask=0.0),
    "Blue": dict(base=(0.036, 0.062, 0.135, 1.0), drift=(0.045, 0.062, 0.115, 1.0), rough=0.66, dirt=0.45, wear=0.6,
                 panel=0.5, heat=0.0, mask=1.0),
}
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

    def mul(self, *values):
        result = values[0]
        for value in values[1:]:
            result = self.math("MULTIPLY", result, value)
        return result

    def mix(self, a, b, factor, data_type="RGBA"):
        node = self.nodes.new("ShaderNodeMix")
        node.data_type = data_type
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
        self.low = g.map_range(self.z, 0.30, 1.25, 1.0, 0.0, smooth=True)

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
        inside = g.nodes.new("ShaderNodeAmbientOcclusion")
        inside.inside = True
        inside.inputs["Distance"].default_value = convex_distance
        inside.samples = 12
        convex = g.map_range(inside.outputs["AO"], 0.55, 0.9, 1.0, 0.0)
        self.edge = g.math("MAXIMUM", sharp, convex)
        near, _ = edge_mask(0.045, 0.80, 0.97)
        self.near_edge = g.math("MAXIMUM", near, g.map_range(inside.outputs["AO"], 0.7, 0.98, 1.0, 0.0))

        occlusion = g.nodes.new("ShaderNodeAmbientOcclusion")
        occlusion.inputs["Distance"].default_value = 0.22
        occlusion.samples = 16
        self.cavity = g.map_range(occlusion.outputs["AO"], 0.30, 0.95, 1.0, 0.0)
        tight = g.nodes.new("ShaderNodeAmbientOcclusion")
        tight.inputs["Distance"].default_value = 0.05
        tight.samples = 12
        self.crevice = g.map_range(tight.outputs["AO"], 0.35, 0.92, 1.0, 0.0)

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


def finish(g, name, colour, rough, metal, height, mask, bump_strength=0.3, bevel=None):
    """Wires the channels into the Principled BSDF (for presentation) and the bake outputs."""
    shader = g.nodes["Principled BSDF"]
    bump = g.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = bump_strength
    bump.inputs["Distance"].default_value = 0.0005
    g.link(height, bump.inputs["Height"])
    if bevel:
        g.links.new(bevel.outputs["Normal"], bump.inputs["Normal"])
    g.link(colour, shader.inputs["Base Color"])
    g.link(rough, shader.inputs["Roughness"])
    g.link(metal, shader.inputs["Metallic"])
    g.links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    bake_outputs(g, name, g.nodes["Material Output"], colour, rough, metal, g.value(mask))


def weather(g, m, colour, rough, metal, dirt, heat=0.0, dust_low=0.85, dust_max=0.55):
    """Deposits, streaks, dust and heat over any surface. Returns (colour, roughness, metallic, dust)."""
    # Deposits in recesses, round bolts and along panel seams.
    deposit = g.mul(g.math("MAXIMUM", g.mul(m.cavity, 0.7), g.mul(m.crevice, 0.85)),
                    g.map_range(g.noise(5.0, 4.0, 0.6), 0.3, 0.7, 0.55, 1.0), dirt)
    colour = g.mix(colour, GRIME, deposit)
    rough = g.mixf(rough, 0.82, g.mul(deposit, 0.6))
    # Short streaks on vertical faces, broken into runs.
    runs = g.map_range(g.noise(3.0, 3.0, 0.55, stretch=(9.0, 9.0, 0.45)), 0.55, 0.80, 0.0, 0.32)
    streaks = g.mul(runs, m.vertical, g.map_range(g.noise(0.9, 2.0), 0.4, 0.6, 0.3, 1.0), dirt)
    colour = g.mix(colour, STREAK, streaks)
    # Dust: thick near the ground and in the running gear, a light film on top faces, both broken up.
    patches = g.map_range(g.noise(2.0, 6.0, 0.62), 0.36, 0.62, 0.15, 1.0)
    speckle = g.map_range(g.noise(60.0, 2.0, 0.6), 0.3, 0.7, 0.75, 1.0)
    low = g.mul(m.low, patches, speckle, dust_low)
    film = g.mul(m.top, g.map_range(g.noise(7.0, 5.0, 0.6), 0.38, 0.70, 0.08, 0.5),
                 g.math("ADD", 0.45, g.mul(m.cavity, 1.2)))
    dust = g.mul(g.math("MAXIMUM", low, film), dirt, dust_max)
    colour = g.mix(colour, DUST, dust)
    rough = g.mixf(rough, 0.93, dust)
    metal = g.mixf(metal, 0.0, g.math("MULTIPLY", dust, 1.4))
    if heat:
        hot = g.mul(m.heat, heat)
        colour = g.mix(colour, HEAT, g.mul(hot, 0.65))
        colour = g.mix(colour, SOOT, g.mul(hot, g.map_range(g.noise(6.0, 4.0), 0.4, 0.65, 0.2, 0.75),
                                           g.math("ADD", 0.4, m.vertical)))
        rough = g.mixf(rough, 0.78, hot)
    return colour, rough, metal, dust


def paint(name, base, drift, rough, dirt, wear, panel, heat, mask):
    """Painted armour: plate-to-plate variation, grain, weathering, then edge wear through primer to dull steel."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g)

    # Base: low-frequency drift towards the olive-gray, then a hue and value shift per plate and a fine grain.
    colour = g.mix(base, drift, g.map_range(g.noise(0.7, 4.0, 0.5), 0.40, 0.68, 0.0, 0.55))
    plate_shift = g.mul(g.math("SUBTRACT", m.island, 0.5), 0.24 * panel, m.plate)
    hue_shift = g.mul(g.math("SUBTRACT", g.math("FRACT", g.mul(m.island, 7.31), clamp=False), 0.5), 0.012 * panel,
                      m.plate)
    grain = g.map_range(g.noise(45.0, 4.0, 0.6), 0.3, 0.7, -0.05, 0.05)
    mottle = g.math("ADD", g.map_range(g.noise(9.0, 4.0, 0.5), 0.35, 0.65, -0.04, 0.04),
                    g.map_range(g.noise(2.5, 3.0, 0.5), 0.38, 0.62, -0.08, 0.06), clamp=False)
    value = g.math("ADD", g.math("ADD", 1.0, plate_shift, clamp=False), g.math("ADD", grain, mottle, clamp=False),
                   clamp=False)
    colour = g.hsv(colour, g.math("ADD", 0.5, hue_shift, clamp=False), 1.0, value)
    roughness = g.math("ADD", rough, g.math("ADD", g.mul(g.math("SUBTRACT", m.island, 0.5), 0.10, m.plate),
                                            g.math("ADD", g.map_range(g.noise(6.0, 3.0), 0.3, 0.7, -0.05, 0.05),
                                                   g.map_range(g.noise(22.0, 4.0), 0.3, 0.7, -0.06, 0.06),
                                                   clamp=False), clamp=False),
                       clamp=False)
    metal = g.value(0.0)

    colour, roughness, metal, dust = weather(g, m, colour, roughness, metal, dirt, heat)

    # Edge abrasion: only the exposed rims, broken up, weaker on bolts and welds, rubbed down where dust lies.
    breakup = g.map_range(g.noise(26.0, 5.0, 0.62), 0.42, 0.62, 0.0, 1.0)
    exposure = g.map_range(g.noise(1.1, 2.0), 0.35, 0.65, 0.45, 1.0)
    edge_wear = g.mul(m.edge, breakup, exposure, g.math("SUBTRACT", 1.0, g.mul(m.detail, 0.75)), wear)
    rubbed = g.map_range(edge_wear, 0.05, 0.25, 0.0, 0.85)
    primer = g.map_range(edge_wear, 0.30, 0.48, 0.0, 1.0)
    steel = g.map_range(edge_wear, 0.55, 0.80, 0.0, 1.0)
    # Chips: small flakes near edges and low down; scuffs: light scratches in sparse patches, paint only.
    flakes = g.map_range(g.noise(48.0, 2.0, 0.7), 0.71, 0.74, 0.0, 1.0)
    chip_bias = g.math("ADD", g.mul(m.near_edge, 0.7), g.mul(m.low, 0.45))
    chips = g.mul(flakes, g.map_range(chip_bias, 0.15, 0.7, 0.0, 1.0), m.plate, wear)
    voronoi = g.nodes.new("ShaderNodeTexVoronoi")
    voronoi.feature = "DISTANCE_TO_EDGE"
    voronoi.inputs["Scale"].default_value = 6.0
    mapping = g.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0, 1.0, 2.5)
    g.links.new(g.coords.outputs["Object"], mapping.inputs["Vector"])
    g.links.new(mapping.outputs["Vector"], voronoi.inputs["Vector"])
    scuff = g.mul(g.map_range(voronoi.outputs["Distance"], 0.0, 0.006, 1.0, 0.0),
                  g.map_range(g.noise(2.0, 2.0), 0.63, 0.69, 0.0, 1.0), m.plate, 0.6 * wear)

    colour = g.mix(colour, g.hsv(colour, 0.5, 0.75, 1.35), g.math("MAXIMUM", scuff, rubbed))
    colour = g.mix(colour, PRIMER, g.math("MAXIMUM", chips, primer))
    colour = g.mix(colour, DULL_STEEL, steel)
    roughness = g.mixf(roughness, 0.52, g.math("MAXIMUM", scuff, rubbed))
    roughness = g.mixf(roughness, 0.74, g.math("MAXIMUM", chips, primer))
    roughness = g.mixf(roughness, 0.40, steel)
    metal = g.mixf(metal, 1.0, steel)

    # Height: orange-peel and grain, flakes and worn rims dip below the paint.
    height = g.math("ADD", g.math("ADD", g.mul(g.noise(90.0, 3.0, 0.55), 0.15), g.mul(g.noise(30.0, 4.0), 0.2)),
                    g.math("SUBTRACT", 1.0, g.math("MAXIMUM", g.math("MAXIMUM", chips, primer), scuff)))
    finish(g, name, colour, roughness, metal, height, mask, 0.45, m.bevel)
    return material


def metal(name):
    """Oiled dark steel: polished on edges, dry and oily patches, oxide only deep in cavities."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.008, 0.006)
    colour = g.mix((0.085, 0.08, 0.074, 1.0), (0.13, 0.122, 0.11, 1.0), g.map_range(g.noise(3.0, 5.0), 0.35, 0.65))
    oily = g.map_range(g.noise(4.0, 4.0, 0.6), 0.55, 0.70, 0.0, 1.0)
    roughness = g.math("ADD", g.map_range(g.noise(12.0, 4.0), 0.3, 0.7, 0.48, 0.62), g.mul(oily, -0.14), clamp=False)
    colour = g.mix(colour, (0.09, 0.085, 0.08, 1.0), g.mul(oily, 0.6))
    metallic = g.value(1.0)
    oxide = g.mul(m.crevice, g.map_range(g.noise(20.0, 4.0), 0.45, 0.7, 0.0, 0.7))
    colour = g.mix(colour, OXIDE, oxide)
    roughness = g.mixf(roughness, 0.75, oxide)
    metallic = g.mixf(metallic, 0.0, oxide)
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, metallic, 0.6, dust_max=0.45)
    polish = g.mul(m.edge, g.map_range(g.noise(30.0, 4.0), 0.45, 0.6, 0.0, 0.8))
    colour = g.mix(colour, (0.22, 0.21, 0.2, 1.0), g.mul(polish, 0.7))
    roughness = g.mixf(roughness, 0.36, g.mul(polish, 0.7))
    finish(g, name, colour, roughness, metallic, g.noise(120.0, 2.0), 0.0, 0.15, m.bevel)
    return material


def dark(name):
    """Phosphate-black fittings, grilles, cables and frames: dielectric, worn to steel on the sharpest edges."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.006, 0.006)
    colour = g.mix((0.028, 0.028, 0.026, 1.0), (0.045, 0.044, 0.04, 1.0), g.map_range(g.noise(5.0, 4.0), 0.35, 0.65))
    roughness = g.map_range(g.noise(9.0, 3.0), 0.3, 0.7, 0.52, 0.66)
    metallic = g.value(0.0)
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, metallic, 0.7, heat=1.0, dust_max=0.5)
    worn = g.map_range(g.mul(m.edge, g.map_range(g.noise(30.0, 5.0), 0.45, 0.62)), 0.45, 0.8, 0.0, 1.0)
    colour = g.mix(colour, DULL_STEEL, worn)
    roughness = g.mixf(roughness, 0.42, worn)
    metallic = g.mixf(metallic, 1.0, worn)
    finish(g, name, colour, roughness, metallic, g.noise(100.0, 2.0), 0.0, 0.15, m.bevel)
    return material


def rubber(name):
    """Rubber: matte, worn lighter and smoother on contact faces, dusty low down, darker in the grooves."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.008, 0.006)
    colour = g.mix((0.020, 0.019, 0.018, 1.0), (0.032, 0.030, 0.028, 1.0), g.map_range(g.noise(8.0, 4.0), 0.35, 0.65))
    roughness = g.map_range(g.noise(14.0, 3.0), 0.3, 0.7, 0.84, 0.92)
    contact = g.math("MAXIMUM", g.mul(m.down, 0.9), g.mul(m.edge, 0.5))
    colour = g.mix(colour, (0.042, 0.04, 0.037, 1.0), contact)
    roughness = g.mixf(roughness, 0.74, contact)
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, g.value(0.0), 1.0, dust_low=1.0,
                                             dust_max=0.15)
    finish(g, name, colour, roughness, metallic, g.noise(160.0, 2.0), 0.0, 0.2, m.bevel)
    return material


def track(name):
    """Track steel: dirt packed into the recesses, dust over it, ground-contact and guide faces polished bright."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.006, 0.006)
    colour = g.mix((0.05, 0.046, 0.042, 1.0), (0.08, 0.072, 0.064, 1.0), g.map_range(g.noise(6.0, 4.0), 0.35, 0.65))
    roughness = g.map_range(g.noise(10.0, 4.0), 0.3, 0.7, 0.52, 0.64)
    metallic = g.value(1.0)
    mud = g.mul(g.math("MAXIMUM", m.cavity, g.mul(m.crevice, 1.2)), g.map_range(g.noise(9.0, 5.0), 0.3, 0.6, 0.5, 1.0))
    colour = g.mix(colour, (0.11, 0.085, 0.06, 1.0), mud)
    roughness = g.mixf(roughness, 0.95, mud)
    metallic = g.mixf(metallic, 0.0, g.math("MULTIPLY", mud, 1.5))
    colour, roughness, metallic, _ = weather(g, m, colour, roughness, metallic, 0.7, dust_low=0.6, dust_max=0.4)
    # The link's ground and wheel-path faces and the guide horn flanks are rubbed clean.
    scrape = g.map_range(g.noise(40.0, 3.0, 0.6, stretch=(0.3, 6.0, 6.0)), 0.3, 0.7, 0.55, 1.0)
    contact = g.mul(g.math("MAXIMUM", m.down, g.mul(m.edge, 0.3)), scrape)
    colour = g.mix(colour, (0.30, 0.29, 0.275, 1.0), contact)
    roughness = g.mixf(roughness, 0.26, contact)
    metallic = g.mixf(metallic, 1.0, contact)
    finish(g, name, colour, roughness, metallic, g.noise(90.0, 3.0), 0.0, 0.25, m.bevel)
    return material


def optics(name):
    """Coated glass: near black with a faint blue-green tint, smooth with light smudges, a dark rim at the frame."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    m = Masks(g, 0.01)
    tint = g.map_range(g.noise(1.5, 2.0), 0.35, 0.65)
    colour = g.mix((0.004, 0.011, 0.010, 1.0), (0.006, 0.009, 0.020, 1.0), tint)
    smudge = g.map_range(g.noise(14.0, 5.0, 0.65), 0.55, 0.75, 0.0, 1.0)
    roughness = g.math("ADD", 0.035, g.mul(smudge, 0.12))
    rim = g.math("MAXIMUM", m.crevice, g.mul(m.edge, 0.8))
    colour = g.mix(colour, (0.002, 0.002, 0.002, 1.0), rim)
    roughness = g.mixf(roughness, 0.5, g.mul(rim, 0.6))
    specks = g.mul(g.map_range(g.noise(60.0, 2.0), 0.68, 0.74, 0.0, 0.5), g.math("ADD", 0.3, m.low))
    colour = g.mix(colour, DUST, specks)
    roughness = g.mixf(roughness, 0.9, specks)
    finish(g, name, colour, roughness, g.value(0.0), g.value(0.0), 0.0, 0.0, None)
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
