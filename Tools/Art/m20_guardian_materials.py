"""Materials for the M20 Guardian (used by m20_guardian_blender.py).

Two sets:
- Procedural Cycles materials, one per surface kind (tan paint, olive-gray paint, USA blue, steel, gunmetal, rubber, track
  steel, glass, lamps). They are the bake source: layered paint over primer and steel, edge wear, chips, scratches, AO
  grime, rain streaks and dust that builds up towards the ground. Each one also has an Emission output that the baker
  switches to one channel (colour, roughness, metallic, team mask) at a time.
- Game materials, one per texture set (M20_Hull, M20_Turret, M20_Running, M20_Track) plus M20_Emissive, which reads the
  glass and lamp colours from the mesh's colour attribute. These are the slots the FBX files carry.
"""

import bpy

# Linear colours.
WORN_STEEL = (0.34, 0.33, 0.31, 1.0)
PRIMER = (0.10, 0.075, 0.05, 1.0)
DUST = (0.24, 0.19, 0.13, 1.0)
GRIME = (0.03, 0.025, 0.018, 1.0)

# name: (dark, light, roughness, metallic, wear, dirt, chips, team mask)
SURFACES = {
    "Paint": ((0.15, 0.105, 0.05, 1.0), (0.25, 0.18, 0.09, 1.0), 0.70, 0.0, 1.0, 0.8, 1.0, 0.0),
    "Olive": ((0.06, 0.062, 0.045, 1.0), (0.10, 0.10, 0.075, 1.0), 0.72, 0.0, 1.0, 0.8, 1.0, 0.0),
    "Blue": ((0.022, 0.04, 0.10, 1.0), (0.04, 0.068, 0.15, 1.0), 0.62, 0.0, 1.0, 0.7, 1.0, 1.0),
    "Metal": ((0.22, 0.21, 0.19, 1.0), (0.34, 0.32, 0.29, 1.0), 0.42, 0.9, 0.4, 0.8, 0.0, 0.0),
    "Dark": ((0.022, 0.022, 0.02, 1.0), (0.04, 0.04, 0.036, 1.0), 0.55, 0.35, 0.6, 0.7, 0.0, 0.0),
    "Rubber": ((0.016, 0.016, 0.015, 1.0), (0.03, 0.029, 0.027, 1.0), 0.88, 0.0, 0.0, 0.4, 0.0, 0.0),
    "Track": ((0.035, 0.032, 0.028, 1.0), (0.07, 0.062, 0.052, 1.0), 0.62, 0.8, 0.8, 0.35, 0.0, 0.0),
}
# Glass and lamps: (base colour, roughness, emission colour, emission strength). Their colours also go into the mesh's
# colour attribute for M20_Emissive.
EMISSIVE = {
    "Optics": ((0.004, 0.012, 0.03, 1.0), 0.05, (0.05, 0.16, 0.36, 1.0), 0.25),
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

    def value(self, number):
        node = self.nodes.new("ShaderNodeValue")
        node.outputs[0].default_value = number
        return node.outputs[0]


def bake_outputs(graph, name, shader_out, color, roughness, metallic, mask):
    """An Emission node the baker plugs into the material output, pointed at one channel at a time."""
    emission = graph.nodes.new("ShaderNodeEmission")
    surface = shader_out.inputs["Surface"].links[0].from_socket
    CHANNELS[name] = {"color": color, "roughness": roughness, "metallic": metallic, "mask": mask,
                      "emission": emission, "surface": surface, "out": shader_out, "graph": graph}


def layered(name, dark, light, roughness, metallic, wear, dirt, chips, mask):
    """Paint (or bare material) with mottling, AO grime, streaks, dust low down, worn edges, chips and scratches."""
    material = bpy.data.materials.new(name)
    g = Graph(material)
    shader = g.nodes["Principled BSDF"]
    out = g.nodes["Material Output"]
    world = g.geometry.outputs["Position"]

    # Base: two-tone mottle and fine variation.
    ramp = g.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = dark
    ramp.color_ramp.elements[1].color = light
    ramp.color_ramp.elements[0].position = 0.3
    ramp.color_ramp.elements[1].position = 0.7
    g.link(g.noise(1.4, 6.0, 0.55), ramp.inputs["Fac"])
    colour = g.mix(ramp.outputs["Color"], dark, g.map_range(g.noise(16.0, 3.0), 0.45, 0.75, 0.0, 0.35))
    # Rain streaks: noise stretched along Z, darkening in vertical runs.
    streaks = g.map_range(g.noise(4.0, 2.0, 0.5, stretch=(6.0, 6.0, 0.5)), 0.55, 0.8, 0.0, 0.26)
    colour = g.mix(colour, GRIME, streaks)
    # Grime in crevices.
    occlusion = g.nodes.new("ShaderNodeAmbientOcclusion")
    occlusion.inputs["Distance"].default_value = 0.3
    occlusion.samples = 16
    grime = g.map_range(occlusion.outputs["AO"], 0.35, 1.0, 0.7, 0.0)
    colour = g.mix(colour, GRIME, grime)

    # Dust building up towards the ground, broken up by noise.
    separate = g.nodes.new("ShaderNodeSeparateXYZ")
    g.links.new(world, separate.inputs[0])
    height = g.map_range(separate.outputs["Z"], 0.15, 1.40, 1.0, 0.0, smooth=True)
    dust_noise = g.map_range(g.noise(3.5, 5.0, 0.6), 0.35, 0.7, 0.25, 1.0)
    dust = g.math("MULTIPLY", g.math("MULTIPLY", height, dust_noise), dirt)
    # Light dust settles on top faces everywhere.
    normal_z = g.nodes.new("ShaderNodeSeparateXYZ")
    g.links.new(g.geometry.outputs["Normal"], normal_z.inputs[0])
    top_dust = g.math("MULTIPLY", g.map_range(normal_z.outputs["Z"], 0.6, 1.0, 0.0, 0.25 * dirt),
                      g.map_range(g.noise(9.0, 4.0), 0.4, 0.7, 0.0, 1.0))
    dust = g.math("MAXIMUM", dust, top_dust)
    colour = g.mix(colour, DUST, dust)

    # Edges: where the rounded (bevel) normal leaves the true normal, broken up by noise.
    bevel = g.nodes.new("ShaderNodeBevel")
    bevel.inputs["Radius"].default_value = 0.01
    bevel.samples = 8
    dot = g.nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    g.links.new(bevel.outputs["Normal"], dot.inputs[0])
    g.links.new(g.geometry.outputs["Normal"], dot.inputs[1])
    edge = g.map_range(dot.outputs["Value"], 0.90, 0.99, 1.0, 0.0)
    edge_noise = g.map_range(g.noise(24.0, 5.0, 0.6), 0.40, 0.60, 0.0, 1.0)
    edge_wear = g.math("MULTIPLY", g.math("MULTIPLY", edge, edge_noise), wear)
    # Chips: small flakes of paint gone to primer, mostly near the edges and low down.
    chip_field = g.map_range(g.noise(38.0, 3.0, 0.7), 0.68, 0.72, 0.0, 1.0)
    chip_bias = g.math("ADD", g.math("MULTIPLY", edge, 0.6), g.math("MULTIPLY", height, 0.5))
    chip_mask = g.math("MULTIPLY", g.math("MULTIPLY", chip_field, g.map_range(chip_bias, 0.1, 0.6, 0.0, 1.0)), chips)
    # Scratches: thin cell edges of a stretched Voronoi, only in sparse patches.
    voronoi = g.nodes.new("ShaderNodeTexVoronoi")
    voronoi.feature = "DISTANCE_TO_EDGE"
    voronoi.inputs["Scale"].default_value = 7.0
    mapping = g.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0, 1.0, 2.5)
    g.links.new(g.coords.outputs["Object"], mapping.inputs["Vector"])
    g.links.new(mapping.outputs["Vector"], voronoi.inputs["Vector"])
    scratch = g.map_range(voronoi.outputs["Distance"], 0.0, 0.008, 1.0, 0.0)
    scratch = g.math("MULTIPLY", g.math("MULTIPLY", scratch, g.map_range(g.noise(2.2, 2.0), 0.62, 0.68, 0.0, 1.0)),
                     0.5 * chips)
    colour = g.mix(colour, PRIMER, chip_mask)
    colour = g.mix(colour, g.mix(light, WORN_STEEL, 0.6), scratch)
    colour = g.mix(colour, WORN_STEEL, edge_wear)

    # Roughness: base with variation, dust rougher, worn steel smoother.
    rough = g.map_range(g.noise(7.0, 4.0), 0.3, 0.7, roughness - 0.08, roughness + 0.08)
    rough = g.mix(rough, 0.92, dust, "FLOAT")
    rough = g.mix(rough, 0.82, chip_mask, "FLOAT")
    rough = g.mix(rough, 0.36, edge_wear, "FLOAT")
    metal = g.mix(metallic, 0.0, g.math("MULTIPLY", dust, 0.9), "FLOAT")
    metal = g.mix(metal, 0.95, edge_wear, "FLOAT")
    metal = g.mix(metal, 0.6, g.math("MULTIPLY", scratch, 0.6), "FLOAT")

    # Bump: rolled-plate texture, paint thickness at chips and scratches.
    height_map = g.math("ADD", g.math("MULTIPLY", g.noise(85.0, 4.0, 0.6), 0.25),
                        g.math("SUBTRACT", 1.0, g.math("MAXIMUM", chip_mask, scratch)))
    bump = g.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.35
    bump.inputs["Distance"].default_value = 0.0006
    g.link(height_map, bump.inputs["Height"])
    g.links.new(bevel.outputs["Normal"], bump.inputs["Normal"])

    g.link(colour, shader.inputs["Base Color"])
    g.link(rough, shader.inputs["Roughness"])
    g.link(metal, shader.inputs["Metallic"])
    g.links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    bake_outputs(g, name, out, colour, rough, metal, g.value(mask))
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


def plain(name, colour, roughness, metallic=0.0):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    shader = material.node_tree.nodes["Principled BSDF"]
    shader.inputs["Base Color"].default_value = colour
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Metallic"].default_value = metallic
    return material


def make_procedural():
    for name, args in SURFACES.items():
        layered(name, *args)
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
    """Principled material over the baked set: BaseColor (team mask in alpha), DirectX normal, ORM."""
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
    """Glass and lamps: colour from the mesh's colour attribute, glowing."""
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
    """The colour attribute value for a glass or lamp face (linear, scaled to its glow)."""
    glow, strength = EMISSIVE[name][2], EMISSIVE[name][3]
    scale = min(1.0, strength / EMISSIVE_STRENGTH)
    return tuple(c * scale for c in glow[:3]) + (1.0,)
