"""08-only inspection views and physical attachment measurements."""
import sys,json,math
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy,numpy as np
from mathutils import Vector
from wall_lamp_materials import KIT,save_map
from wall_lamp_render import gpu,aim
from lighting_393_finish import read
out=KIT/'08_WarningBeacon'
bpy.ops.wm.open_mainfile(filepath=str(out/'source/WarningBeacon_Preview.blend'))
s=bpy.context.scene;gpu();s.cycles.samples=96;s.cycles.use_denoising=True;s.render.resolution_x=700;s.render.resolution_y=700;s.render.use_compositing=True
s.camera.data.lens=58;s.camera.data.type='PERSP'
bpy.data.materials['Lens'].node_tree.nodes['LampLevel'].outputs[0].default_value=.05
bg=next(n for n in s.world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs['Color'].default_value=(.65,.72,.85,1);bg.inputs['Strength'].default_value=.65
for n,e in [('REVIEW key',12),('REVIEW strip',8),('REVIEW fill',5)]:bpy.data.objects[n].data.energy=e
bpy.data.objects['REVIEW sun'].hide_render=False;bpy.data.objects['REVIEW fixture'].hide_render=True
for o in s.objects:
    if o.name.startswith('REVIEW support'):o.hide_render=True
for label,angle in [('front',0),('threequarter',-60),('side',90),('rear',180)]:
    a=math.radians(angle);target=Vector((0,0,.225));s.camera.location=target+Vector((math.cos(a)*.45,math.sin(a)*.45,.025));aim(s.camera,target)
    s.render.filepath=str(out/'review'/f'construction_{label}_day.png');bpy.ops.render.render(write_still=True)
imgs=[read(out/'review'/f'construction_{label}_day.png') for label in ('front','threequarter','side','rear')]
save_map(out/'review/construction_angles_day.png',np.concatenate([np.concatenate(imgs[2:],axis=1),np.concatenate(imgs[:2],axis=1)],axis=0))
# Inspect connected mesh components rather than accepting a projected painting.
bpy.ops.wm.open_mainfile(filepath=str(out/'source/WarningBeacon.blend'))
records={}
for suffix in ('','_LOD1'):
    o=bpy.data.objects['SM_Kit_WarningBeacon'+suffix];parent=list(range(len(o.data.vertices)))
    def find(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    for e in o.data.edges:parent[find(e.vertices[0])]=find(e.vertices[1])
    groups={}
    for v in o.data.vertices:groups.setdefault(find(v.index),[]).append(v.co)
    parts={}
    for vs in groups.values():
        lo=[min(v[i] for v in vs) for i in range(3)];hi=[max(v[i] for v in vs) for i in range(3)]
        if abs(lo[2]-.178)<1e-5 and abs(hi[2]-.232)<1e-5:parts['holder']=[lo,hi]
        if abs(lo[2]-.178)<1e-5 and abs(hi[2]-.238)<1e-5:parts['bracket']=[lo,hi]
        if abs(lo[2]-.231)<1e-5 and abs(hi[2]-.283)<1e-5:parts['bulb']=[lo,hi]
        if abs(lo[2]-.210)<1e-5 and abs(hi[2]-.306)<1e-5:parts['mirror']=[lo,hi]
    assert set(parts)=={'holder','bracket','bulb','mirror'},parts
    assert parts['holder'][0][2]<.180 and parts['bracket'][0][2]<.180
    assert parts['holder'][1][2]>parts['bulb'][0][2]
    # Exact concave lower meridian at z=.235 intersects the bracket volume.
    z=.235;rad=.258-z;x=-.034+.007*(rad-.002)/(.026-.002)
    br=parts['bracket'];assert br[0][0]<x<br[1][0] and br[0][2]<z<br[1][2]
    parts['mirror_bracket_intersection_witness_m']=[x,0,z];records[suffix or 'LOD0']=parts
(out/'evidence/construction_round04b.json').write_text(json.dumps(dict(round='4b',parts=records,interpretation='Real mesh components in both LODs; holder and bracket overlap collar top, socket overlaps bulb, bracket intersects concave mirror. No projected interior remains.'),indent=2)+'\n')
