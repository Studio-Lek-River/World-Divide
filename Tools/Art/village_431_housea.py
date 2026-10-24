"""Round 1 HouseA. Background Blender only; guarded, output-root parameterised.
Rebuilds only its three named states and portable outputs. Never imports the greybox main.
"""
import argparse
import json
import math
import random
import shutil
import sys
from pathlib import Path
sys.dont_write_bytecode = True
import bpy
import bmesh
from mathutils import Vector
sys.path.insert(0, str(Path(__file__).resolve().parent))
import village_blender as grey

ROOT = Path(__file__).resolve().parents[2]
SLOTS = ['Brick', 'Roof', 'Trim', 'Glass', 'Wood', 'Rubble']
COLORS = [(0.34,.115,.065,1),(.28,.09,.045,1),(.8,.82,.77,1),(.025,.04,.045,1),(.07,.055,.04,1),(.25,.23,.20,1)]
SURFACES = {'Brick':'Bricks085','Roof':'RoofingTiles006','Wood':'Planks023A','Rubble':'Concrete036'}

def materials(out):
    for slot,color in zip(SLOTS,COLORS):
        m=bpy.data.materials.new(slot);m.diffuse_color=color;m.use_nodes=True
        p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=color
        p.inputs['Roughness'].default_value=.22 if slot=='Glass' else .8
        if slot in SURFACES:
            for kind in ['BaseColor','Normal','ORM']:
                src=ROOT/'Saved/Surfaces'/SURFACES[slot]/(kind+'.png')
                dst=out/'textures'/(SURFACES[slot]+'_'+kind+'.png');shutil.copy2(src,dst)
                tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(dst))
                if kind!='BaseColor':tex.image.colorspace_settings.name='Non-Color'
                if kind=='BaseColor':m.node_tree.links.new(tex.outputs['Color'],p.inputs['Base Color'])
                if kind=='Normal':
                    n=m.node_tree.nodes.new('ShaderNodeNormalMap');m.node_tree.links.new(tex.outputs['Color'],n.inputs['Color']);m.node_tree.links.new(n.outputs[0],p.inputs['Normal'])
                if kind=='ORM':
                    n=m.node_tree.nodes.new('ShaderNodeSeparateColor');m.node_tree.links.new(tex.outputs['Color'],n.inputs[0]);m.node_tree.links.new(n.outputs['Green'],p.inputs['Roughness'])

class Builder(grey.Builder):
    def __init__(self,name):
        super().__init__(name);self.slots=SLOTS.copy()
    def finish(self):
        bmesh.ops.triangulate(self.mesh,faces=list(self.mesh.faces))
        obj,hulls=super().finish()
        # Replace grey helper materials by the round's six stable materials.
        for i,s in enumerate(SLOTS):obj.data.materials[i]=bpy.data.materials[s]
        uv=obj.data.uv_layers.active.data
        for p in obj.data.polygons:
            n=p.normal
            for li in p.loop_indices:
                v=obj.data.vertices[obj.data.loops[li].vertex_index].co
                if p.material_index==1 and abs(n.z)>.3:
                    uv[li].uv=(v.x/2, (v.y*math.sqrt(1+(3.8/4.5)**2))/2)
                elif abs(n.z)>.8:uv[li].uv=(v.x/2,v.y/2)
                elif abs(n.x)>.7:uv[li].uv=(v.y/2,v.z/2)
                else:uv[li].uv=(v.x/2,v.z/2)
        return obj,hulls

def facebox(b,face,a,z0,z1,half,d0,d1,slot):
    b.box(*grey.face_box(face,a,z0,z1,half,d0,d1,5.5 if 'X' in face else 4.5),slot)

def prism(b,face,coords,slot):
    # Extrude a convex wall profile with complete reveal/return/end faces.
    axis,side,other=grey.FACES[face];extent=5.5 if axis==0 else 4.5
    layers=[]
    for d in [-.3,0]:
        vs=[]
        for a,z in coords:
            co=[0.,0.,z];co[axis]=side*(extent+d);co[other]=a
            vs.append(b.mesh.verts.new(co))
        layers.append(vs)
    faces=[b.mesh.faces.new(layers[0][::-1]),b.mesh.faces.new(layers[1])]
    for i in range(len(coords)):faces.append(b.mesh.faces.new([layers[0][i],layers[0][(i+1)%len(coords)],layers[1][(i+1)%len(coords)],layers[1][i]]))
    bmesh.ops.recalc_face_normals(b.mesh,faces=faces)
    for f in faces:f.material_index=SLOTS.index(slot)

def shell(b,found,state):
    for face in grey.FACES:
        span=4.5 if 'X' in face else 5.5
        ops=[o for o in found if o[0]==face]
        holes=[(a-w/2,a+w/2,z,z+h) for _,a,z,w,h,_,_ in ops]
        if state=='Damaged' and face=='+Y':holes.append((-1.6,1.7,.35,5.2))
        cuts=sorted(set([-span,0,span]+[v for h in holes for v in h[:2]]))
        for a,c in zip(cuts,cuts[1:]):
            mid=(a+c)/2
            top=lambda x: 9.6-abs(x)*3.8/4.5 if 'X' in face else 5.8
            maxz=min(top(a),top(c))
            intervals=[(-.5,10)]
            for x0,x1,z0,z1 in holes:
                if x0<mid<x1:
                    intervals=[v for l,h in intervals for v in ((l,min(h,z0)),(max(l,z1),h)) if v[1]-v[0]>.015]
            for l,h in intervals:
                if l>=max(top(a),top(c))-.01:continue
                if h<=maxz:
                    facebox(b,face,mid,l,h,(c-a)/2,-.3,0,'Rubble' if h<=.4 else 'Brick')
                else:
                    # Clip the strip's roofline against horizontal interval boundaries.
                    poly=[(a,l),(c,l),(c,top(c)),(a,top(a))]
                    for level,keep_above in [(l,True),(h,False)]:
                        new=[]
                        for p,q in zip(poly,poly[1:]+poly[:1]):
                            pin=p[1]>=level if keep_above else p[1]<=level
                            qin=q[1]>=level if keep_above else q[1]<=level
                            if pin:new.append(p)
                            if pin!=qin:
                                t=(level-p[1])/(q[1]-p[1]);new.append((p[0]+t*(q[0]-p[0]),level))
                        poly=new
                    clean=[]
                    for p in poly:
                        if not clean or math.dist(p,clean[-1])>.01:clean.append(p)
                    if len(clean)>2 and math.dist(clean[0],clean[-1])<.01:clean.pop()
                    if len(clean)>=3:prism(b,face,clean,'Brick')

def windows(b,found,state):
    for i,(face,a,z,w,h,kind,_) in enumerate(found):
        if state=='Damaged' and face=='+Y' and -1.6<a<1.7:continue # breach replaces these apertures
        # Four frame members, inset glazing, dark five-sided backing room.
        for along in [a-w/2+.045,a+w/2-.045]:facebox(b,face,along,z,z+h,.045,-.15,.025,'Trim')
        for zz in [z,z+h-.09]:facebox(b,face,a,zz,zz+.09,w/2,-.15,.025,'Trim')
        if kind=='window':
            facebox(b,face,a,z,z+h,.035,-.13,.015,'Trim')
            facebox(b,face,a,z-.13,z-.02,w/2+.12,-.04,.18,'Trim')
        if state!='Damaged' or i%3==0:
            facebox(b,face,a,z+.08,z+h-.08,w/2-.085,-.19,-.17,'Glass' if kind=='window' else 'Wood')
        # No ground-through strips behind broken glazing: back, sides, floor, ceiling.
        facebox(b,face,a,z,z+h,w/2,-1.05,-1.0,'Wood')
        for aa in [a-w/2+.025,a+w/2-.025]:facebox(b,face,aa,z,z+h,.025,-1.0,-.3,'Wood')
        for zz in [z,z+h-.05]:facebox(b,face,a,zz,zz+.05,w/2,-1.0,-.3,'Wood')
        if kind=='door':
            facebox(b,face,a,.02,.17,.78,.0,.8,'Rubble')
            facebox(b,face,a+.36,.9,1.05,.035,-.15,-.1,'Trim')

def drainage(b,state):
    # PVC U-profile gutters and 12cm pipes, with physical shoulders at the eaves.
    for side in [-1,1]:
        ranges=[(-5.85,5.85)] if state!='Damaged' or side==-1 else [(-5.85,-1.2),(3.3,5.85)]
        y=side*4.84
        for a,c in ranges:
            b.box((a,y-.10,5.48),(c,y+.10,5.52),'Trim')
            for yy in [y-.10,y+.07]:b.box((a,yy,5.50),(c,yy+.03,5.63),'Trim')
        for x in [-5.25,5.25]:
            b.box((x-.06,y-.06,5.25),(x+.06,y+.06,5.52),'Trim')
            b.box((x-.06,min(y,side*4.57)-.06,5.25),(x+.06,max(y,side*4.57)+.06,5.37),'Trim')
            b.box((x-.06,side*4.57-.06,.10),(x+.06,side*4.57+.06,5.37),'Trim')

def roof(b,state):
    slope=math.atan2(3.8,4.5);run=math.hypot(4.5,3.8)+.38
    def slab(x0,x1,t0,t1,side,slot='Roof'):
        d=(t0+t1)/2
        b.turned(((x0+x1)/2,side*d*math.cos(slope),9.64-d*math.sin(slope)),(x1-x0,t1-t0,.17),(-side*math.degrees(slope),0,0),slot)
    for side in [-1,1]:
        if state=='Damaged' and side==1:
            # A large open break through the ridge and a ragged lower edge.
            slab(-5.85,-1.05,0,run,side);slab(3.3,5.85,0,run,side)
            for j in range(5):slab(-1.05+j*.87,-1.05+(j+1)*.87,4.1+(.22 if j%2 else 0),run,side)
            for j,x in enumerate([-.55,.35,1.25,2.15,2.95]):
                end=4.5 if j%2 else 3.4
                b.turned((x,1.8,8.1),(.12,end,.20),(-math.degrees(slope),0,0),'Wood')
            b.box((-1.1,.15,5.65),(3.3,4.1,5.82),'Wood')
            b.box((-1.6,3.1,.05),(1.7,4.19,.18),'Wood')
            b.box((-1.6,3.1,5.1),(1.7,4.19,5.25),'Wood')
        else:slab(-5.85,5.85,0,run,side)
        # Full thickness eave fascia and continuous gutters; damaged span hangs away.
        ranges=[(-5.85,5.85)] if state!='Damaged' or side==-1 else [(-5.85,-1.2),(3.3,5.85)]
        for x0,x1 in ranges:
            b.box((x0,side*4.82-.065,5.42),(x1,side*4.82+.065,5.59),'Wood')
            b.box((x0,side*4.7-.055,5.53),(x1,side*4.7+.055,5.76),'Trim')
        for x in [-5.72,5.72]:
            b.turned((x,side*2.32,7.68),(.17,run,.21),(-side*math.degrees(slope),0,0),'Trim')
    # Geometric ridge tiles, coarse 12cm curvature reads in silhouette.
    for j in range(26):
        x=-5.85+j*.45
        if state=='Damaged' and -1.15<x<3.35:continue
        b.turned((x+.22,0,9.76),(.46,.26,.13),(0,0,0),'Roof')
    # Hollow brick chimney above rear slope with concrete capping and dark flue.
    b.box((-4.0,-1.25,7.9),(-3.2,-.5,10.32),'Brick')
    for lo,hi in [((-4.12,-1.37,10.32),(-3.08,-1.15,10.48)),((-4.12,-.6,10.32),(-3.08,-.38,10.48)),((-4.12,-1.15,10.32),(-3.9,-.6,10.48)),((-3.3,-1.15,10.32),(-3.08,-.6,10.48))]:b.box(lo,hi,'Rubble')
    b.box((-3.9,-1.15,10.31),(-3.3,-.6,10.33),'Wood')

def debris(b,rng,n,extent,center=(0,0),roof_pieces=False):
    for i in range(n):
        x=center[0]+rng.uniform(-extent[0],extent[0]);y=center[1]+rng.uniform(-extent[1],extent[1])
        sx=rng.uniform(.25,1.2);sy=rng.uniform(.2,.8);sz=rng.uniform(.18,.5)
        b.turned((x,y,sz/2+.05),(sx,sy,sz),(rng.uniform(-18,18),rng.uniform(-18,18),rng.uniform(0,180)),'Brick' if i%3 else 'Rubble')

def build(state,spec):
    name='SM_Village_HouseA'+('' if state=='Pristine' else '_'+state)
    b=Builder(name);rng=random.Random(431)
    found=grey.openings(spec)
    if state!='Rubble':
        shell(b,found,state);windows(b,found,state);roof(b,state);drainage(b,state)
        # A dark brick/concrete damp course, with the front door kept clear.
        for face in grey.FACES:
            span=4.5 if 'X' in face else 5.5
            ranges=[(-span,span)] if face!='+X' else [(-span,-.56),(.56,span)]
            for a,c in ranges:facebox(b,face,(a+c)/2,-.5,.35,(c-a)/2,0,.045,'Rubble')
        b.collision=[((-5.5,-4.5,-.5),(5.5,4.5,5.8)),((-5.5,-2.25,5.8),(5.5,2.25,9.6))]
        if state=='Damaged':
            debris(b,rng,36,(2.5,.85),(0,4.9))
            # Broken brick teeth at the breach, not a smooth rectangular cut.
            for j in range(10):
                x=-1.7 if j%2 else 1.7;z=.4+(j//2)*.92
                b.turned((x,4.37,z),(.35,.32,.3),(0,rng.uniform(-18,18),0),'Brick')
    else:
        # Jagged closed wall stumps, including clear surviving window cuts.
        for face in grey.FACES:
            span=4.5 if 'X' in face else 5.5
            for j in range(7):
                a=-span+2*span*j/7;c=-span+2*span*(j+1)/7
                if j in (2,4):continue
                h=rng.uniform(.65,2.85);prism(b,face,[(a,-.5),(c,-.5),(c,h-.25),(a,h)],'Brick')
        # Low structural heap stays within inherited collision; no pyramid primitive.
        for j in range(26):
            x=rng.uniform(-4.3,4.3);y=rng.uniform(-3.3,3.3);z=max(.3,1.1-math.hypot(x/5,y/4)*.5)
            b.turned((x,y,z),(rng.uniform(.8,2.5),rng.uniform(.6,1.8),rng.uniform(.3,.8)),(rng.uniform(-28,28),rng.uniform(-25,25),rng.uniform(0,180)),'Rubble' if j%3 else 'Brick')
        for x,y,angle in [(-2,-1,24),(2,1,-27),(.5,-2,13)]:
            b.turned((x,y,1.2),(3.8,2.7,.18),(angle,12,x*20),'Roof')
            b.turned((x,y,1.0),(3.5,.18,.25),(angle,12,x*20),'Wood')
        debris(b,rng,42,(5,4))
        b.collision=[((-5.5,-4.5,-.5),(5.5,4.5,1.3)),((-5,-3,1.3),(-3,3,2.7))]
    return b.finish()

def validate(obj):
    obj.data.calc_loop_triangles();uv=obj.data.uv_layers.active.data
    bad=[];bad_uv=[]
    for t in obj.data.loop_triangles:
        a,b,c=[obj.data.vertices[v].co for v in t.vertices]
        if (b-a).cross(c-a).length<1e-7:bad.append(t.index)
        a,b,c=[uv[i].uv for i in t.loops]
        if abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))<1e-9:bad_uv.append(t.index)
    assert not bad,(obj.name,'degenerate triangles',bad)
    assert not bad_uv,(obj.name,'degenerate UV',bad_uv[:10])
    assert len(obj.data.materials)==6
    return {'triangles':len(obj.data.loop_triangles),'slots':SLOTS,'degenerate_triangles':len(bad),'degenerate_uv_triangles':len(bad_uv)}

def export(objects,path):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH'},use_mesh_modifiers=False,mesh_smooth_type='FACE',use_tspace=True,colors_type='NONE',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',bake_space_transform=False,add_leaf_bones=False,bake_anim=False,use_custom_props=False,path_mode='AUTO')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output-root',type=Path,default=ROOT/'Art/Shared/village/issue431/round1');args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    out=args.output_root.resolve()
    assert out.is_relative_to(ROOT/'Art/Shared/village/issue431')
    for p in ['textures','evidence','fbx','source']:(out/p).mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system='METRIC'
    bpy.context.scene.unit_settings.scale_length=1.0
    materials(out)
    spec=grey.BUILDINGS['HouseA'];report={};objects=[]
    for state in ['Pristine','Damaged','Rubble']:
        obj,hulls=build(state,spec);objects.append((obj,hulls));report[obj.name]=validate(obj)
        assert report[obj.name]['triangles']<=12000
        export([obj]+hulls,out/'fbx'/(obj.name+'.fbx'))
        report[obj.name]['collision_boxes']=len(hulls)
        # A first-pass, separately portable LOD1; retain all six slots.
        lod=obj.copy();lod.data=obj.data.copy();lod.name=obj.name+'_LOD1';bpy.context.collection.objects.link(lod)
        bpy.context.view_layer.objects.active=lod
        mod=lod.modifiers.new('Round1_LOD1_firstpass','DECIMATE');mod.ratio=min(1,2800/report[obj.name]['triangles']);mod.use_collapse_triangulate=True
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bm=bmesh.new();bm.from_mesh(lod.data)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.01)
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        seen=set();duplicates=[]
        for f in bm.faces:
            key=tuple(sorted(tuple(round(c,5) for c in v.co) for v in f.verts))
            if key in seen:duplicates.append(f)
            seen.add(key)
        bmesh.ops.delete(bm,geom=duplicates,context='FACES_ONLY')
        bm.to_mesh(lod.data);bm.free();lod.data.update()
        report[lod.name]=validate(lod);assert report[lod.name]['triangles']<=3000
        export([lod],out/'fbx'/(lod.name+'.fbx'));bpy.data.objects.remove(lod,do_unlink=True)
    # Keep the source in local coordinates, each state in its own named collection.
    for obj,hulls in objects:
        col=bpy.data.collections.new(obj.name);bpy.context.scene.collection.children.link(col)
        for o in [obj]+hulls:
            for old in list(o.users_collection):old.objects.unlink(o)
            col.objects.link(o)
            if o!=obj:o.hide_set(True);o.hide_render=True
        if obj!=objects[0][0]:col.hide_viewport=True;col.hide_render=True
    for image in bpy.data.images:
        if image.source=='FILE':image.filepath='//../textures/'+Path(image.filepath).name
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/HouseA_round1.blend'))
    # Independently reimport every portable FBX and compare render counts/UVs.
    for path in sorted((out/'fbx').glob('*.fbx')):
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(path))
        meshes=[o for o in bpy.context.scene.objects if o.type=='MESH' and not o.name.startswith('UCX_')]
        assert len(meshes)==1
        record=validate(meshes[0]);assert record['triangles']==report[path.stem]['triangles'],(path.stem,record['triangles'],report[path.stem]['triangles'])
        report[path.stem]['reimport']=record
    (out/'evidence/mesh_manifest.json').write_text(json.dumps({'meshes':report,'units':'Blender metres; FBX cm; Unreal mirrors Y','texture_period_metres':2,'openings':grey.garrison(spec),'surfaces':SURFACES},indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
