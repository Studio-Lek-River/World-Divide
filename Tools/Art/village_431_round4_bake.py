"""Round4 batches the approved high-to-low recipe, then validates every FBX.
No Unreal operations. Input checkpoints remain recoverable in round4/source.
"""
import argparse,json,math,sys
from pathlib import Path
sys.dont_write_bytecode=True
import bpy,bmesh,numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round4 as r
import village_431_round4_common as c
OUT=c.OUT;N=2048

def select(obj):
    bpy.ops.object.select_all(action='DESELECT');obj.hide_set(False);obj.select_set(True);bpy.context.view_layer.objects.active=obj

def raster(obj):
    uv=obj.data.uv_layers['BakeUV'].data;obj.data.calc_loop_triangles()
    for tri in obj.data.loop_triangles:
        coords=np.array([uv[i].uv[:] for i in tri.loops])*N;a,b,d=coords
        x0=max(0,int(coords[:,0].min()));x1=min(N,int(coords[:,0].max())+1);y0=max(0,int(coords[:,1].min()));y1=min(N,int(coords[:,1].max())+1)
        if x1<=x0 or y1<=y0:continue
        yy,xx=np.mgrid[y0:y1,x0:x1];xx=xx+.5;yy=yy+.5;den=(b[1]-d[1])*(a[0]-d[0])+(d[0]-b[0])*(a[1]-d[1])
        if abs(den)<1e-8:continue
        u=((b[1]-d[1])*(xx-d[0])+(d[0]-b[0])*(yy-d[1]))/den;v=((d[1]-a[1])*(xx-d[0])+(a[0]-d[0])*(yy-d[1]))/den;w=1-u-v
        inside=(u>=0)&(v>=0)&(w>=0)
        vs=np.array([obj.data.vertices[i].co[:] for i in tri.vertices]);p=u[...,None]*vs[0]+v[...,None]*vs[1]+w[...,None]*vs[2]
        yield tri,(slice(y0,y1),slice(x0,x1)),inside,p

def save_map(name,array):
    image=bpy.data.images.new(name,width=N,height=N,alpha=False);image.colorspace_settings.name='Non-Color';image.pixels.foreach_set(array.ravel());image.filepath_raw=str(OUT/'textures'/(name+'.png'));image.file_format='PNG';image.save();return image

def finish_maps(obj,key,normal,ao):
    filled=np.zeros((N,N),bool);tint=np.ones((N,N,4),np.float32);repaired=0;sampled=0
    damaged=obj.name.endswith('Damaged');rubble=obj.name.endswith('Rubble');spec=r.g.BUILDINGS.get(key,{'length':30,'width':14,'eaves':1,'ridge':1});hl=spec['length']/2;hw=spec['width']/2
    for tri,sl,inside,p in raster(obj):
        x,y,z=p[:,:,0],p[:,:,1],p[:,:,2];slot=tri.material_index
        expected=np.array([tri.normal.x,-tri.normal.y,tri.normal.z]);patch=normal[sl][:,:,:3];decoded=patch*2-1;length=np.linalg.norm(decoded,axis=-1)
        bad=inside&(((decoded*expected).sum(axis=-1)/np.maximum(length,.001)<.5)|(length<.5));patch[bad]=(expected+1)/2;repaired+=int(bad.sum());sampled+=int(inside.sum())
        if abs(tri.normal.z)>.8 and tri.area>1.5:ao[sl][:,:,:3][inside]=1
        macro=.98+.014*np.sin(x*.6+y*.43)*np.cos(z*.55+x*.2);color=np.stack([macro]*3,axis=-1)
        if slot==1:
            color*=(.97+.035*np.sin(x*.52+y*.38)*np.cos(y*.47-x*.18))[...,None]
            moss=np.exp(-((abs(y)-hw)/.42)**2)*(.6+.4*np.sin(x*1.1)**2)
            color*=1-moss[...,None]*np.array([.15,.08,.19])
        # PVC/zinc members and quiet lintels carry no weathering blotch.
        if slot==2:
            color[:]=(.90,.94,.98)
            if key=='Workshop':
                industrial=(abs(y)>hw-.05)&(z>.44)&(z<2.16)
                color=np.where(industrial[...,None],np.array([.30,.40,.44]),color)
        if slot==5:
            color[:]=(.95,.94,.91)
            color*=np.where((z<.65)[...,None],.78,1)
        if key=='Bridge' and slot==2:color[:]=(.23,.24,.25)
        if key=='Bridge' and damaged and slot==5:color*=np.where(((x+.5)**2+(y-.5)**2<9)[...,None],.55,1)
        if key=='HouseA' and slot==2:
            # Preserve the approved original seven-segment number plate, 18.
            plate=(x>5.55)&(abs(y-.88)<.13)&(z>1.45)&(z<1.67)
            up=(y-.75)/.26;vp=(z-1.45)/.22;digit=(up>.46).astype(int);du=np.where(digit==0,up/.42,(up-.50)/.42)
            one=(du>.70)&(du<.90)&(vp>.18)&(vp<.82)
            eight=(((abs(vp-.2)<.055)|(abs(vp-.5)<.055)|(abs(vp-.8)<.055))&(du>.12)&(du<.86))|((((du>.1)&(du<.26))|((du>.72)&(du<.88)))&(vp>.2)&(vp<.8))
            color=np.where((plate&np.where(digit==0,one,eight))[...,None],.025,color)
        if damaged and key!='Bridge':
            if key=='HouseA':center=.03;yy=hw;bz=2.8;half=1.62
            else:center=-hl*.45+min(3,spec['length']*.25)/2;yy=-hw;bz=min(3.8,spec['eaves']-.65)/2;half=1.6
            dx=np.maximum(abs(x-center)-half,0);dz=np.maximum(abs(z-bz)-bz,0);soot=np.exp(-(dx/.65)**2-(dz/.75)**2-((y-yy)/.6)**2)*.75
            rise=spec['ridge']-spec['eaves'];roofline=spec['ridge']-abs(y)*rise/hw
            hc=1.1 if key=='HouseA' else hl*.05+min(4,spec['length']*.3)/2
            roofsoot=np.exp(-(np.maximum(abs(x-hc)-1.9,0)/.6)**2-((z-roofline)/.5)**2)*(y*yy>0)*.7
            if key in ['Farmhouse','Church']:
                # Scorch follows an irregular local tear, not a rectangular
                # full-slope stripe that conceals the broken roof silhouette.
                wandering=hc+.45*np.sin(y*1.25)+.25*np.cos(y*2.1)
                width=1.3+.35*np.cos(y*1.7)
                roofsoot=np.exp(-(np.maximum(abs(x-wandering)-width,0)/.55)**2-((z-roofline)/.5)**2)*(y*yy>0)*.38
                roofsoot*=np.clip((hw+.1-abs(y))/.85,0,1)
            if slot==1:soot=np.maximum(soot,roofsoot)
            color*=np.maximum(.23,1-soot)[...,None]
        if rubble:
            if slot==0:color*=np.clip(.74+.035*z,.70,.86)[...,None]*np.array([.94,.89,.82])
            elif slot in [1,4,5]:color*=np.clip(.84+.035*z,.78,.97)[...,None]
        tint[sl][:,:,:3][inside]=color[inside];filled[sl]|=inside
    for array in [normal,ao,tint]:
        mask=filled.copy()
        for k in range(2):
            for axis,shift in [(0,1),(0,-1),(1,1),(1,-1)]:
                valid=np.roll(mask,shift,axis)&~mask;src=np.roll(array,shift,axis);array[valid]=src[valid];mask[valid]=True
    save_map(obj.name+'_BakeNormal',normal);save_map(obj.name+'_BakeAO',ao);save_map(obj.name+'_Tint',tint)
    return {'normal_outliers_repaired':repaired,'sampled_texels':sampled,'roof_macro_amplitude':.035,'rubble_brick_dust':'standing tint ×0.82 in instance, UV1 dust .70–.86 with brown dust colour','floor_ao_bypass':True}

def source_materials(obj,lod,key):
    wall,brick,roof=c.CONFIG[key];damaged=obj.name.endswith('Damaged');rubble=obj.name.endswith('Rubble')
    for i,slot in enumerate(c.SLOTS):
        mat=obj.data.materials[i].copy();mat.name=obj.name+'_'+slot;obj.data.materials[i]=mat;lod.data.materials[i]=mat
        p=mat.node_tree.nodes.get('Principled BSDF')
        tint={'Brick':tuple(x*(.82 if rubble else 1) for x in brick),'Roof':roof,'Trim':(.10,.075,.055) if key=='Bridge' else (.67,.70,.72),'Wood':c.WOOD_TINT[key],'Rubble':(.57,.55,.50),'Glass':(.025,.04,.05)}[slot]
        p.inputs['Base Color'].default_value=(*tint,1);mat.diffuse_color=(*tint,1)
        surface={'Brick':'Plaster006' if damaged and wall=='Plaster001' else wall,'Roof':None if key=='Bridge' else ('RoofingTiles006' if key in ['HouseA','HouseC','Farmhouse','Church'] else 'RoofingTiles013A'),'Trim':None,'Wood':'Planks037A' if key=='Barn' else 'Planks023A','Rubble':'Concrete036' if damaged or rubble else 'Concrete031','Glass':None}[slot]
        for node in mat.node_tree.nodes:
            if node.type=='TEX_IMAGE' and node.image:
                kind=Path(node.image.filepath).stem.rsplit('_',1)[-1]
                if surface and kind in ['BaseColor','Normal','ORM']:
                    node.image=bpy.data.images.load(str(OUT/'textures'/(surface+'_'+kind+'.png')),check_existing=True)
                    if kind!='BaseColor':node.image.colorspace_settings.name='Non-Color'
                    if key=='Church' and slot=='Brick':
                        uvscan=mat.node_tree.nodes.new('ShaderNodeUVMap');uvscan.uv_map='UVMap'
                        scale=mat.node_tree.nodes.new('ShaderNodeVectorMath');scale.operation='MULTIPLY';scale.inputs[1].default_value=(.625,1,1);mat.node_tree.links.new(uvscan.outputs[0],scale.inputs[0]);mat.node_tree.links.new(scale.outputs[0],node.inputs[0])
        if not surface:
            for link in list(p.inputs['Base Color'].links):mat.node_tree.links.remove(link)
        if slot!='Glass':
            # Match colour intent in the source; Unreal owns final normal blending.
            incoming=p.inputs['Base Color'].links[0].from_socket if p.inputs['Base Color'].links else None
            mult=mat.node_tree.nodes.new('ShaderNodeVectorMath');mult.operation='MULTIPLY';mult.inputs[1].default_value=tint
            if incoming:mat.node_tree.links.new(incoming,mult.inputs[0])
            else:mult.inputs[0].default_value=(1,1,1)
            uv=mat.node_tree.nodes.new('ShaderNodeUVMap');uv.uv_map='BakeUV'
            atlas=mat.node_tree.nodes.new('ShaderNodeTexImage');atlas.image=bpy.data.images.load(str(OUT/'textures'/(obj.name+'_Tint.png')),check_existing=True);atlas.image.colorspace_settings.name='Non-Color';mat.node_tree.links.new(uv.outputs[0],atlas.inputs[0])
            blend=mat.node_tree.nodes.new('ShaderNodeVectorMath');blend.operation='MULTIPLY';mat.node_tree.links.new(mult.outputs[0],blend.inputs[0]);mat.node_tree.links.new(atlas.outputs['Color'],blend.inputs[1]);mat.node_tree.links.new(blend.outputs[0],p.inputs['Base Color'])

def bake_type(key,report):
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'source'/(key+'_geometry.blend')))
    r.SPEC=r.g.BUILDINGS.get(key,{'length':30,'width':14,'eaves':1,'ridge':1})
    for col in bpy.data.collections:col.hide_viewport=False;col.hide_render=False
    for o in bpy.data.objects:o.hide_set(False)
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=4;scene.cycles.device='CPU';scene.render.bake.margin=2;scene.render.bake.use_selected_to_active=True;scene.render.bake.cage_extrusion=.035;scene.render.bake.max_ray_distance=.08
    for name in c.names(key):
        obj=bpy.data.objects[name]
        for o in bpy.data.objects:
            if o.type=='MESH':o.hide_render=True
        obj.hide_render=False;select(obj)
        if obj.data.uv_layers.get('BakeUV'):obj.data.uv_layers.remove(obj.data.uv_layers['BakeUV'])
        obj.data.uv_layers.new(name='BakeUV');obj.data.uv_layers.active_index=1
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.0015,scale_to_bounds=True);bpy.ops.object.mode_set(mode='OBJECT');obj.data.uv_layers.active_index=0
        high=obj.copy();high.data=obj.data.copy();high.name=name+'_MASTER';bpy.context.collection.objects.link(high);high.hide_render=False
        bm=bmesh.new();bm.from_mesh(high.data);weights=bm.edges.layers.float.new('bevel_weight_edge');hl=r.SPEC['length']/2;hw=r.SPEC['width']/2
        for e in bm.edges:
            stable=len(e.link_faces)==2 and 1.4<e.calc_face_angle()<1.75 and e.calc_length()>.2 and min(f.calc_area() for f in e.link_faces)>.10
            if stable:stable=min(f.calc_area()/max(q.calc_length() for q in f.edges) for f in e.link_faces)>.065
            if stable and not name.endswith('Rubble') and key!='Bridge' and any(f.material_index==0 for f in e.link_faces):
                m=(e.verts[0].co+e.verts[1].co)/2;stable=abs(abs(m.x)-hl)<.02 and abs(abs(m.y)-hw)<.02
            e[weights]=int(stable)
        bm.to_mesh(high.data);bm.free();select(high);bevel=high.modifiers.new('Master_20mm_edge_chamfer','BEVEL');bevel.width=.02;bevel.segments=1;bevel.use_clamp_overlap=False;bevel.affect='EDGES';bevel.limit_method='WEIGHT';bpy.ops.object.modifier_apply(modifier=bevel.name)
        bm=bmesh.new();bm.from_mesh(high.data);bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.005);bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(high.data);bm.free();high.data.update();r.qa(high)
        links=[]
        for mat in high.data.materials:
            p=mat.node_tree.nodes.get('Principled BSDF')
            for link in list(p.inputs['Normal'].links):links.append((mat,link.from_socket,link.to_socket));mat.node_tree.links.remove(link)
        maps=[]
        for kind in ['Normal','AO']:
            image=bpy.data.images.new(name+'_work'+kind,width=N,height=N,alpha=False);image.colorspace_settings.name='Non-Color';nodes=[]
            for mat in obj.data.materials:
                n=mat.node_tree.nodes.new('ShaderNodeTexImage');n.image=image;mat.node_tree.nodes.active=n;nodes.append((mat,n))
            select(high);obj.select_set(True);bpy.context.view_layer.objects.active=obj
            if kind=='Normal':bpy.ops.object.bake(type='NORMAL',normal_space='OBJECT',uv_layer='BakeUV')
            else:bpy.ops.object.bake(type='AO',uv_layer='BakeUV')
            pixels=np.array(image.pixels[:],np.float32).reshape(N,N,4)
            if kind=='Normal':pixels[:,:,1]=1-pixels[:,:,1]
            maps.append(pixels)
            for mat,n in nodes:mat.node_tree.nodes.remove(n)
            bpy.data.images.remove(image)
        for mat,a,b in links:mat.node_tree.links.new(a,b)
        receipt=finish_maps(obj,key,*maps);high.hide_render=True
        lod=bpy.data.objects[name+'_LOD1'];obj.data.calc_loop_triangles();tris=list(obj.data.loop_triangles);tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[t.vertices for t in tris],all_triangles=True)
        layer=lod.data.uv_layers.get('BakeUV') or lod.data.uv_layers.new(name='BakeUV')
        for p in lod.data.polygons:
            for li in p.loop_indices:
                co=lod.data.vertices[lod.data.loops[li].vertex_index].co;point,n,idx,dist=tree.find_nearest(co);tri=tris[idx]
                value=barycentric_transform(point,*[obj.data.vertices[i].co for i in tri.vertices],*[Vector((*obj.data.uv_layers['BakeUV'].data[i].uv,0)) for i in tri.loops]);layer.data[li].uv=(value.x,value.y)
        lod.data.uv_layers.active_index=0
        hulls=[o for o in bpy.data.objects if o.name.startswith('UCX_'+name+'_') and o.name[len('UCX_'+name+'_'):].isdigit()]
        for o in [obj,lod,high]+hulls:o.hide_set(False)
        r.r1.export([obj]+hulls,OUT/'fbx'/(name+'.fbx'));r.r1.export([lod],OUT/'fbx'/(name+'_LOD1.fbx'));r.r1.export([high],OUT/'source'/(name+'_MASTER.fbx'))
        report[name]={'game_triangles':r.qa(obj)['triangles'],'master_triangles':r.qa(high)['triangles'],'bake':'Cycles selected-to-active geometric object normal + AO, UV1, 2048, 4 samples, 20mm master chamfers','cleanup_and_tint':receipt}
        # Portable FBX retains the six stable slot names; source-only material
        # copies are applied after export to show each state's native colour intent.
        source_materials(obj,lod,key)
        for i in range(len(high.data.materials)):high.data.materials[i]=obj.data.materials[i]
        print('BAKED',name,flush=True)
        (OUT/'evidence/bake_receipt.json').write_text(json.dumps(report,indent=2))
    for img in bpy.data.images:
        if img.source=='FILE':img.filepath='//../textures/'+Path(img.filepath).name
    for col in bpy.data.collections:col.hide_viewport=col.name not in ['Collection',c.names(key)[0]];col.hide_render=col.hide_viewport
    for obj in bpy.data.objects:
        if obj.name.endswith('_MASTER'):obj.hide_set(True);obj.hide_render=True
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source'/(key+'_baked.blend')))

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--types',nargs='+',default=c.TYPES+['Bridge']);args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    p=OUT/'evidence/bake_receipt.json';report=json.loads(p.read_text()) if p.exists() else {}
    for key in args.types:bake_type(key,report)

if __name__=='__main__':main()
