"""Final material finish, invoked from strip_light.py. No added geometry.
Adds a second UV channel for a portable normal bake; preserves all original UV0.
Object-normal bake is converted into ORIGINAL UV0 tangent basis, sampled on UV1.
"""
import bpy, json, hashlib, shutil, math
import numpy as np
from pathlib import Path
from strip_light_materials import OUT, TEX, apply_materials

def shape(o):
    d={'vertices':[list(v.co) for v in o.data.vertices],
       'faces':[(list(p.vertices),p.material_index,p.use_smooth) for p in o.data.polygons],
       'uv0':[list(x.uv) for x in o.data.uv_layers[0].data],
       'matrix':[list(r) for r in o.matrix_world]}
    return hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest()

def finish():
    path=OUT/'source/StripLight.blend'
    bpy.ops.wm.open_mainfile(filepath=str(path));o=bpy.data.objects['SM_Kit_StripLight']
    before=shape(o);apply_materials()
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    uv0=o.data.uv_layers[0].name
    if 'FinishBake' not in o.data.uv_layers:o.data.uv_layers.new(name='FinishBake')
    o.data.uv_layers.active=o.data.uv_layers['FinishBake']
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.012)
    bpy.ops.object.mode_set(mode='OBJECT')
    size=2048
    bake=bpy.data.images.new('Finish_ObjectNormal',width=size,height=size,float_buffer=True)
    bake.colorspace_settings.name='Non-Color'
    for mat in o.data.materials:
        ns=mat.node_tree.nodes;ls=mat.node_tree.links
        # Keep every original texture lookup on UV0 while baking on UV1.
        tc=next(n for n in ns if n.type=='TEX_COORD')
        uv=ns.new('ShaderNodeUVMap');uv.uv_map=uv0
        for link in list(tc.outputs['UV'].links):ls.new(uv.outputs['UV'],link.to_socket)
        p=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
        prev=p.inputs['Normal'].links[0].from_socket
        bevel=ns.new('ShaderNodeBevel');bevel.samples=8
        bevel.inputs['Radius'].default_value=.0012 if mat.name=='Housing' else .0018
        ls.new(prev,bevel.inputs['Normal']);ls.new(bevel.outputs['Normal'],p.inputs['Normal'])
        target=ns.new('ShaderNodeTexImage');target.image=bake;ns.active=target
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=16
    scene.render.bake.margin=12;scene.render.bake.use_clear=True
    bpy.ops.object.bake(type='NORMAL',normal_space='OBJECT',uv_layer='FinishBake')
    pixels=np.empty(size*size*4,dtype=np.float32);bake.pixels.foreach_get(pixels)
    src=pixels.reshape(size,size,4)[:,:,:3]*2-1
    dest=np.zeros((size,size,3),np.float32);dest[:,:,2]=1
    filled=np.zeros((size,size),bool)
    me=o.data;me.calc_loop_triangles();me.calc_tangents(uvmap=uv0)
    for tri in me.loop_triangles:
        uv=np.array([me.uv_layers['FinishBake'].data[i].uv for i in tri.loops])*size-.5
        lo=np.maximum(np.floor(uv.min(axis=0)).astype(int),0);hi=np.minimum(np.ceil(uv.max(axis=0)).astype(int)+1,size)
        if np.any(hi<=lo):continue
        yy,xx=np.mgrid[lo[1]:hi[1],lo[0]:hi[0]]
        a,b,c=uv;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-10:continue
        w0=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
        w1=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
        mask=(w0>=0)&(w1>=0)&(w0+w1<=1)
        loop=me.loops[tri.loops[0]];t=np.array(loop.tangent);n=np.array(tri.normal);bt=np.cross(n,t)*loop.bitangent_sign
        sample=src[lo[1]:hi[1],lo[0]:hi[0]]
        tangent=np.stack((sample@t,sample@bt,sample@n),axis=-1)
        tangent/=np.maximum(np.linalg.norm(tangent,axis=-1,keepdims=True),1e-8)
        dest[lo[1]:hi[1],lo[0]:hi[0]][mask]=tangent[mask]
        filled[lo[1]:hi[1],lo[0]:hi[0]]|=mask
    # Dilate each UV island's converted normals into its texture gutter.
    for _ in range(10):
        old=filled.copy()
        for axis,shift in ((0,1),(0,-1),(1,1),(1,-1)):
            take=(~filled)&np.roll(old,shift,axis)
            dest[take]=np.roll(dest,shift,axis)[take];filled[take]=True
    def save(name,normal):
        im=bpy.data.images.new(name,width=size,height=size)
        im.colorspace_settings.name='Non-Color'
        rgba=np.ones((size,size,4),np.float32);rgba[:,:,:3]=normal*.5+.5
        im.pixels.foreach_set(rgba.ravel());im.filepath_raw=str(TEX/(name+'.png'));im.file_format='PNG';im.save();im.pack();return im
    gl=save('T_StripLight_Finish_NormalGL',dest)
    dx=dest.copy();dx[:,:,1]*=-1;save('T_StripLight_Finish_Normal',dx)
    for mat in o.data.materials:
        ns=mat.node_tree.nodes;ls=mat.node_tree.links
        p=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
        uv=ns.new('ShaderNodeUVMap');uv.uv_map='FinishBake'
        tex=ns.new('ShaderNodeTexImage');tex.image=gl;ls.new(uv.outputs[0],tex.inputs['Vector'])
        normal=ns.new('ShaderNodeNormalMap');normal.uv_map=uv0
        ls.new(tex.outputs['Color'],normal.inputs['Color']);ls.new(normal.outputs[0],p.inputs['Normal'])
        if mat.name=='Housing':
            # Tiny satin change confined to the baked rounded edges, not chipped paint.
            split=ns.new('ShaderNodeSeparateXYZ');ls.new(tex.outputs['Color'],split.inputs[0])
            edge=ns.new('ShaderNodeMath');edge.operation='SUBTRACT';edge.inputs[0].default_value=1
            ls.new(split.outputs['Z'],edge.inputs[1])
            amount=ns.new('ShaderNodeMath');amount.operation='MULTIPLY';amount.inputs[1].default_value=.12
            ls.new(edge.outputs[0],amount.inputs[0])
            rough=p.inputs['Roughness'].links[0].from_socket
            sub=ns.new('ShaderNodeMath');sub.operation='SUBTRACT';ls.new(rough,sub.inputs[0]);ls.new(amount.outputs[0],sub.inputs[1]);ls.new(sub.outputs[0],p.inputs['Roughness'])
    me.uv_layers.active=me.uv_layers[0];me.uv_layers[0].active_render=True
    assert shape(o)==before
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
    for suffix in ('','_LOD1'):
        bpy.ops.export_scene.fbx(filepath=str(OUT/f'SM_Kit_StripLight{suffix}.fbx'),use_selection=True,object_types={'MESH'},mesh_smooth_type='FACE',use_tspace=True,colors_type='NONE',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',bake_anim=False)
    receipt={'before':before,'after':shape(o),'triangles':len(me.loop_triangles),'vertices':len(me.vertices),
             'geometry_uv0_unchanged':True,'added_uv_channel':'FinishBake (UV1)',
             'normal_basis':'original UV0 tangents; sample finish normal on UV1; DirectX map for Unreal',
             'bevel_radius_m':{'Housing':.0012,'Lens':.0018},
             'normal_nonflat_fraction':float(np.mean(np.abs(dest[:,:,:2]).max(axis=-1)>.03))}
    (OUT/'evidence/finish_verification.json').write_text(json.dumps(receipt,indent=2))
    recipe=json.loads((OUT/'material_recipe.json').read_text())
    recipe['textures']=[t for t in recipe['textures'] if 'T_StripLight_Finish_' not in t['file']]
    recipe['finish']={'uv_channel':1,'tangent_basis_uv_channel':0,'normal_directx':'textures/T_StripLight_Finish_Normal.png',
                      'normal_opengl':'textures/T_StripLight_Finish_NormalGL.png','bevel_radius_m':receipt['bevel_radius_m'],
                      'edge_roughness_adjustment':'subtract 0.12 * (1 - baked normal blue)'}
    for name in ('T_StripLight_Finish_Normal','T_StripLight_Finish_NormalGL'):
        p=TEX/(name+'.png');recipe['textures'].append({'file':str(p.relative_to(OUT)),'size':[2048,2048],'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    (OUT/'material_recipe.json').write_text(json.dumps(recipe,indent=2))
    manifest=json.loads((OUT/'model_manifest.json').read_text());manifest['textures']=recipe['textures']
    manifest['finish']=recipe['finish'];manifest['verification']='evidence/finish_verification.json'
    (OUT/'model_manifest.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps(receipt),flush=True)
