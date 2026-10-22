"""Material-only refinement of kit 04. No geometry, UV or layout edits.

Run bundled Python on this file to regenerate deterministic texture maps.
Blender calls apply_materials() from strip_light.py's materials stage.
"""
import json
import hashlib
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'Art/Shared/kit/lighting/04_StripLight'
TEX=OUT/'textures'


def textures():
    from PIL import Image, ImageFilter
    TEX.mkdir(exist_ok=True)
    rng=np.random.default_rng(39304)
    files=[]
    def noise(n, grid, blur=0):
        a=Image.fromarray(rng.integers(0,256,(grid,grid),dtype=np.uint8))
        a=a.resize((n,n),Image.Resampling.BICUBIC)
        if blur:a=a.filter(ImageFilter.GaussianBlur(blur))
        v=np.asarray(a,dtype=np.float32)/255
        return (v-v.mean())/max(v.std(),.001)
    def save(name,a):
        p=TEX/(name+'.png')
        Image.fromarray(np.uint8(np.clip(a,0,1)*255+.5)).save(p)
        files.append({'file':str(p.relative_to(OUT)),'size':list(Image.open(p).size),
                      'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    def srgb(a):return np.where(a<=.0031308,a*12.92,1.055*np.maximum(a,0)**(1/2.4)-.055)
    n=2048
    macro=noise(n,24,8); grain=noise(n,1000,.7); orange=noise(n,330,1.8)
    # Intact powder coating is a dielectric over metal: Metallic=0, F0 about 4%.
    base=np.array([.030,.035,.039])[None,None,:]*(1+.028*macro[:,:,None])
    save('T_StripLight_Housing_BaseColor',srgb(base))
    rough=np.clip(.39+.035*orange+.018*grain,.30,.51)
    save('T_StripLight_Housing_ORM',np.stack((np.ones_like(rough),rough,np.zeros_like(rough)),axis=-1))
    dx=(np.roll(grain,-1,1)-np.roll(grain,1,1))*.016
    dy=(np.roll(grain,-1,0)-np.roll(grain,1,0))*.016
    z=np.sqrt(np.maximum(1-dx*dx-dy*dy,0))
    # DirectX tangent convention for UE; Blender reverses G in its node graph.
    save('T_StripLight_Housing_Normal',np.stack((-dx,dy,z),axis=-1)*.5+.5)
    n=1024
    frost=noise(n,750,.65); cloud=noise(n,35,5)
    c=np.array([.73,.755,.77])[None,None,:]*(1+.006*cloud[:,:,None])
    save('T_StripLight_Lens_BaseColor',srgb(c))
    rough=np.clip(.43+.015*frost,.38,.48)
    save('T_StripLight_Lens_ORM',np.stack((np.ones_like(rough),rough,np.zeros_like(rough)),axis=-1))
    dx=(np.roll(frost,-1,1)-np.roll(frost,1,1))*.009
    dy=(np.roll(frost,-1,0)-np.roll(frost,1,0))*.009
    z=np.sqrt(np.maximum(1-dx*dx-dy*dy,0))
    save('T_StripLight_Lens_Normal',np.stack((-dx,dy,z),axis=-1)*.5+.5)
    v,u=np.mgrid[0:n,0:n]/(n-1)
    # Concealed LED board, blurred by opal acrylic. Smooth broad distribution,
    # darkened enclosure edges, no visible LED dots, stripes, ribs or baked dirt.
    width=.48+.52*np.exp(-((v-.5)/.32)**4)
    ends=.48+.52*(1-np.exp(-np.minimum(u,1-u)/.045))
    mask=np.clip(width*ends*(1+.008*cloud),0,1)
    save('T_StripLight_Lens_LightProfile',np.stack((mask,mask,mask),axis=-1))
    settings={'housing':{'roughness_range':[.30,.51],'metallic':0,'ior':1.5,
                         'normal_uv_scale':8,'orm_uv_scale':4},
              'lens':{'ior':1.49,'roughness':.43,'transmission':.08,
                      'subsurface_weight':.18,'scatter_distance_m':.0025,
                      'normal_uv_scale':10,'profile_uv_scale':[1/1.106,1/.146],
                      'profile_uv_offset':[.5-.5/1.106,.5-.5/.146]},
              'emission':{'kelvin':3500,'linear_rgb':[1,.779,.615],
                          'strength':3.2,'profile':'T_StripLight_Lens_LightProfile'},
              'textures':files,'normal_convention':'DirectX; invert G in Blender only',
              'color_management':'BaseColor sRGB; Normal, ORM and LightProfile linear',
              'ao':'ORM R=1; use spatial contact shadows/SSAO, never paint dirt into base colour'}
    (OUT/'material_recipe.json').write_text(json.dumps(settings,indent=2))
    print('Generated',len(files),'PBR maps, 2K housing / 1K lens',flush=True)


def apply_materials():
    import bpy
    def setup(name):
        m=bpy.data.materials[name];m.use_nodes=True;m.node_tree.nodes.clear()
        ns=m.node_tree.nodes;ls=m.node_tree.links
        out=ns.new('ShaderNodeOutputMaterial')
        p=ns.new('ShaderNodeBsdfPrincipled');ls.new(p.outputs['BSDF'],out.inputs['Surface'])
        uv=ns.new('ShaderNodeTexCoord')
        return m,ns,ls,p,uv
    def uvscale(ns,ls,uv,scale,offset=(0,0,0)):
        mult=ns.new('ShaderNodeVectorMath');mult.operation='MULTIPLY'
        ls.new(uv.outputs['UV'],mult.inputs[0]);mult.inputs[1].default_value=scale
        add=ns.new('ShaderNodeVectorMath');add.operation='ADD'
        ls.new(mult.outputs[0],add.inputs[0]);add.inputs[1].default_value=offset
        return add.outputs[0]
    def image(ns,ls,name,uv,linear=False):
        p=str(TEX/(name+'.png'));im=bpy.data.images.load(p,check_existing=True)
        if im.packed_file:im.unpack(method='REMOVE')
        im.reload()
        im.colorspace_settings.name='Non-Color' if linear else 'sRGB'
        node=ns.new('ShaderNodeTexImage');node.image=im
        ls.new(uv,node.inputs['Vector']);return node
    for surface in ('Housing','Lens'):
        m,ns,ls,p,uv=setup(surface)
        prefix='T_StripLight_'+surface+'_'
        col=image(ns,ls,prefix+'BaseColor',uv.outputs['UV'])
        ls.new(col.outputs['Color'],p.inputs['Base Color'])
        p.inputs['Metallic'].default_value=0
        p.inputs['IOR'].default_value=1.5 if surface=='Housing' else 1.49
        n_uv=uvscale(ns,ls,uv,(8,8,1) if surface=='Housing' else (10,10,1))
        n=image(ns,ls,prefix+'Normal',n_uv,True)
        split=ns.new('ShaderNodeSeparateXYZ');ls.new(n.outputs['Color'],split.inputs[0])
        invert=ns.new('ShaderNodeMath');invert.operation='SUBTRACT';invert.inputs[0].default_value=1
        ls.new(split.outputs['Y'],invert.inputs[1])
        combine=ns.new('ShaderNodeCombineXYZ')
        ls.new(split.outputs['X'],combine.inputs['X']);ls.new(invert.outputs[0],combine.inputs['Y']);ls.new(split.outputs['Z'],combine.inputs['Z'])
        norm=ns.new('ShaderNodeNormalMap');ls.new(combine.outputs[0],norm.inputs['Color'])
        ls.new(norm.outputs['Normal'],p.inputs['Normal'])
        r_uv=uvscale(ns,ls,uv,(4,4,1) if surface=='Housing' else (10,10,1))
        orm=image(ns,ls,prefix+'ORM',r_uv,True)
        splitorm=ns.new('ShaderNodeSeparateXYZ');ls.new(orm.outputs['Color'],splitorm.inputs[0])
        ls.new(splitorm.outputs['Y'],p.inputs['Roughness'])
        if surface=='Lens':
            p.inputs['Transmission Weight'].default_value=.08
            p.inputs['Subsurface Weight'].default_value=.18
            p.inputs['Subsurface Radius'].default_value=(.8,.9,1)
            p.inputs['Subsurface Scale'].default_value=.0025
            profile_uv=uvscale(ns,ls,uv,(1/1.106,1/.146,1),(.5-.5/1.106,.5-.5/.146,0))
            prof=image(ns,ls,prefix+'LightProfile',profile_uv,True);prof.extension='EXTEND'
            tint=ns.new('ShaderNodeVectorMath');tint.operation='MULTIPLY'
            ls.new(prof.outputs['Color'],tint.inputs[0]);tint.inputs[1].default_value=(1,.779,.615)
            ls.new(tint.outputs[0],p.inputs['Emission Color'])
            level=ns.new('ShaderNodeValue');level.name='LampLevel';level.label='Lamp on/off (0/1)';level.outputs[0].default_value=1
            strength=ns.new('ShaderNodeMath');strength.operation='MULTIPLY';strength.inputs[1].default_value=3.2
            ls.new(level.outputs[0],strength.inputs[0]);ls.new(strength.outputs[0],p.inputs['Emission Strength'])
        for im in bpy.data.images:
            if im.source=='FILE' and str(TEX) in str(Path(im.filepath).resolve()):im.pack()


def geometry_digest(o):
    # Include topology, positions, UVs, shading flags, material assignment and transform.
    value={'vertices':[list(v.co) for v in o.data.vertices],
           'faces':[(list(p.vertices),p.material_index,p.use_smooth) for p in o.data.polygons],
           'uv':[[list(x.uv) for x in layer.data] for layer in o.data.uv_layers],
           'matrix':[list(r) for r in o.matrix_world]}
    return hashlib.sha256(json.dumps(value,sort_keys=True).encode()).hexdigest()


if __name__=='__main__':textures()
