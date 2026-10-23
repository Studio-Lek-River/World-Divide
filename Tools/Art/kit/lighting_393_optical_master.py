"""Round 2 high-detail optical masters, baked as reference-view opaque lens maps.
Writes ONLY 08/12/13 source/bake_inputs and separate editable optical-master blends.
"""
import sys,math,json
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy
from mathutils import Vector
from wall_lamp_materials import KIT
from wall_lamp_render import gpu,aim

def mat(name,color,metal=0,rough=.3,trans=0,emit=0):
    m=bpy.data.materials.new(name);m.use_nodes=True;p=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough;p.inputs['Transmission Weight'].default_value=trans;p.inputs['IOR'].default_value=1.49;p.inputs['Emission Color'].default_value=(*color,1);p.inputs['Emission Strength'].default_value=emit;return m

def cylinder(name,loc,r,depth,material,n=64):
    bpy.ops.mesh.primitive_cylinder_add(vertices=n,radius=r,depth=depth,location=loc);o=bpy.context.object;o.name=name;o.data.materials.append(material)
    for p in o.data.polygons:p.use_smooth=abs(p.normal.z)<.5
    bevel=o.modifiers.new('Master machined edge','BEVEL');bevel.width=.006;bevel.segments=3;o.modifiers.new('Master normals','WEIGHTED_NORMAL');return o

def sphere(name,loc,size,material):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=64,ring_count=32,radius=1,location=loc);o=bpy.context.object;o.name=name;o.scale=size;o.data.materials.append(material)
    for p in o.data.polygons:p.use_smooth=True
    return o

def tube(name,points,r,material):
    cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.resolution_u=2;cu.bevel_depth=r;cu.bevel_resolution=3;sp=cu.splines.new('POLY');sp.points.add(len(points)-1)
    for p,co in zip(sp.points,points):p.co=(*co,1)
    o=bpy.data.objects.new(name,cu);bpy.context.collection.objects.link(o);o.data.materials.append(material);return o

def master(folder,amber):
    bpy.ops.wm.read_factory_settings(use_empty=True);out=KIT/folder;scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=192;scene.cycles.use_denoising=True;gpu();scene.cycles.max_bounces=12;scene.cycles.transmission_bounces=10;scene.render.resolution_x=1024;scene.render.resolution_y=1200;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_depth='16';scene.view_settings.view_transform='AgX'
    world=bpy.data.worlds.new('Optical bake studio');world.use_nodes=True;scene.world=world;bg=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs['Color'].default_value=(.15,.18,.22,1)
    steel=mat('Optical internal black steel',(.10,.11,.12),.75,.27);silver=mat('Optical internal silver',(.45,.48,.50),.92,.18);gold=mat('Curved polished reflector',(.80,.55,.19),.98,.13)
    glass=mat('Actual master dome glass',(1,.58,.10) if amber else (1,.045,.025),0,.06,1);clear=mat('Bulb envelope',(.96,.97,.99),0,.04,1)
    # Double wall thickness, no overlapping internal closed caps.
    n=128;rings=[(0,1),(1.50,.98),(1.70,.91),(1.87,.69),(1.97,.32),(1.998,.035)]
    vs=[];fs=[]
    for z,r in rings:
        for i in range(n):a=2*math.pi*i/n;vs.append((r*math.cos(a),r*math.sin(a),z))
    for j in range(len(rings)-1):
        for i in range(n):k=(i+1)%n;fs.append((j*n+i,j*n+k,(j+1)*n+k,(j+1)*n+i))
    fs.append(tuple((len(rings)-1)*n+i for i in range(n)))
    me=bpy.data.meshes.new('Dense rounded optical dome');me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new('Dense rounded optical dome',me);bpy.context.collection.objects.link(o);me.materials.append(glass)
    for p in me.polygons:p.use_smooth=True
    solid=o.modifiers.new('Real 2mm master glass wall','SOLIDIFY');solid.thickness=.014;solid.offset=-1
    # Fine vertical optics, lower horizontal rib band and 3D scuff normal.
    nt=glass.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED');tex=nt.nodes.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=280;tex.inputs['Detail'].default_value=2;bn=nt.nodes.new('ShaderNodeBump');bn.inputs['Strength'].default_value=.03;bn.inputs['Distance'].default_value=.00008;nt.links.new(tex.outputs['Fac'],bn.inputs['Height']);nt.links.new(bn.outputs['Normal'],p.inputs['Normal'])
    # A thin coloured-window master: trace the real interior without magnifying it.
    # Its raytraced reflections are baked; runtime remains the required opaque Lens.
    p.inputs['Metallic'].default_value=1;p.inputs['Transmission Weight'].default_value=0;p.inputs['Base Color'].default_value=(1,1,1,1);p.inputs['Roughness'].default_value=.025
    transparent=nt.nodes.new('ShaderNodeBsdfTransparent');transparent.inputs[0].default_value=(1,.88,.66,1) if amber else (1,.55,.50,1)
    fresnel=nt.nodes.new('ShaderNodeFresnel');fresnel.inputs['IOR'].default_value=1.49;mix=nt.nodes.new('ShaderNodeMixShader');nt.links.new(fresnel.outputs[0],mix.inputs[0]);nt.links.new(transparent.outputs[0],mix.inputs[1]);nt.links.new(p.outputs[0],mix.inputs[2]);output=next(n for n in nt.nodes if n.type=='OUTPUT_MATERIAL');nt.links.new(mix.outputs[0],output.inputs['Surface'])
    backdrop=mat('Black optical backdrop',(.002,.002,.002),0,1);bpy.ops.mesh.primitive_plane_add(size=20,location=(-2,0,1),rotation=(0,math.pi/2,0));bpy.context.object.data.materials.append(backdrop)
    emitters=[]
    if amber:
        # Concave paraboloid behind the bulb, real polished mirror geometry.
        vs=[];faces=[];nr=24;ns=96
        for j in range(nr):
            r=.003+.72*j/(nr-1)
            for i in range(ns):a=2*math.pi*i/ns;vs.append((-.39+.27*(r/.72)**2,r*math.cos(a),1.12+r*math.sin(a)))
        for j in range(nr-1):
            for i in range(ns):k=(i+1)%ns;faces.append((j*ns+i,(j+1)*ns+i,(j+1)*ns+k,j*ns+k))
        me=bpy.data.meshes.new('Concave reflector');me.from_pydata(vs,[],faces);me.update();ob=bpy.data.objects.new('Concave reflector',me);bpy.context.collection.objects.link(ob);me.materials.append(gold)
        for p in me.polygons:p.use_smooth=True
        tube('Rolled mirror rim',[(-.12,.72*math.cos(2*math.pi*i/96),1.12+.72*math.sin(2*math.pi*i/96)) for i in range(97)],.018,silver)
        cylinder('Pedestal',(0,0,.16),.27,.12,steel);cylinder('Bulb holder stem',(0,0,.40),.11,.48,steel);cylinder('Socket',(0,0,.69),.18,.16,silver)
        for z in (.62,.66,.70,.74):cylinder('Socket rolled ring',(0,0,z),.186,.015,steel)
        sphere('Real clear bulb envelope',(.035,0,1.19),(.17,.17,.44),clear)
        filament=mat('Filament warm emitter',(1,.83,.45),.0,.26,0,0)
        tube('Left filament support',[(.02,-.052,.77),(.02,-.052,1.43)],.006,silver);tube('Right filament support',[(.02,.052,.77),(.02,.052,1.43)],.006,silver)
        points=[(.055+.012*math.cos(i*.8),-.055+.11*i/140,1.39+.012*math.sin(i*.8)) for i in range(141)];tube('Fine coiled filament',points,.005,filament);emitters=[filament]
        sphere('Filament rear bulb glow',(-.015,0,1.33),(.05,.07,.16),filament)
    else:
        cylinder('Round internal pedestal',(0,0,.19),.37,.14,steel);cylinder('Pedestal silver seat',(0,0,.27),.30,.034,silver);cylinder('Vertical LED carrier',(0,0,1.05),.17,1.25,gold,n=48)
        led=mat('Warm white LED ceramic',(1,.85,.57),0,.22,0,0);emitters=[led]
        for j in range(11):
            for i in range(8):
                a=2*math.pi*i/8;loc=(.173*math.cos(a),.173*math.sin(a),.51+j*.105)
                bpy.ops.mesh.primitive_cube_add(size=1,location=loc);q=bpy.context.object;q.name='Individual LED chip';q.scale=(.018,.055,.057);q.rotation_euler.z=a;q.data.materials.append(led);bevel=q.modifiers.new('Chip ceramic edge','BEVEL');bevel.width=.006;bevel.segments=2
        cylinder('Lower visible dome ring',(0,0,.055),.89,.045,steel);cylinder('Lower silver locking ring',(0,0,.085),.87,.014,silver)
    camera=bpy.data.cameras.new('Optical orthographic projection');cam=bpy.data.objects.new('Optical orthographic projection',camera);scene.collection.objects.link(cam);cam.location=(5,0,1);aim(cam,(0,0,1));camera.type='ORTHO';camera.ortho_scale=2.6;scene.camera=cam
    lamps=[]
    for name,pos,power,size in [('Tall softbox',(2,-2,2.3),130,.20),('Glass edge',(1.4,2,1.5),90,.10),('Top bounce',(0,-.3,3.0),80,.60)]:
        d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='RECTANGLE';d.size=size;d.size_y=2.0;q=bpy.data.objects.new(name,d);scene.collection.objects.link(q);q.location=pos;aim(q,(0,0,1));lamps.append(q)
    for night in (False,True):
        bg.inputs['Strength'].default_value=.005 if night else .45
        for q,pow in zip(lamps,[130,90,80]):q.data.energy=.15 if night else pow
        for m in emitters:next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED').inputs['Emission Strength'].default_value=22 if night else .04
        scene.render.filepath=str(out/'source/bake_inputs'/f'optical_master_{"night" if night else "day"}.png');bpy.ops.render.render(write_still=True)
    bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(out/'source'/('WarningBeacon_OpticalMaster.blend' if amber else 'MastBeaconSingle_OpticalMaster.blend')))
    projection(folder)
    print('OPTICAL MASTER',folder,flush=True)

def projection(folder):
    from bpy_extras.object_utils import world_to_camera_view
    out=KIT/folder;amber='Warning' in folder;path=out/'source'/('WarningBeacon_OpticalMaster.blend' if amber else 'MastBeaconSingle_OpticalMaster.blend')
    bpy.ops.wm.open_mainfile(filepath=str(path));scene=bpy.context.scene;cam=scene.camera
    pts={name:list(world_to_camera_view(scene,cam,Vector(pos))) for name,pos in [('centre',(0,0,1)),('right',(0,1,1)),('bottom',(0,0,0)),('top',(0,0,2))]}
    (out/'source/bake_inputs/optical_projection.json').write_text(json.dumps(pts,indent=2)+'\n')

if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:]
    if args[0]=='projection':
        for folder in args[1:]:projection(folder)
    else:
        for folder in args:master(folder,'Warning' in folder)
