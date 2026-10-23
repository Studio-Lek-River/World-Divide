"""Independent #393 Round 4 checks; writes only assigned piece evidence/reviews."""
import sys,json,math,hashlib
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy,numpy as np
from mathutils import Vector
import lighting_393_review as r
from wall_lamp_astra_check import geometry,digest,outline_delta,visual_metrics
from lighting_393_finish import read

def check(piece):
 out=r.KIT/piece['id'].split('/')[-1];name=piece['mesh'].replace('SM_Kit_','');repeats=[]
 for _ in range(2):
  bpy.ops.wm.read_factory_settings(use_empty=True);obs=[o for o in r.load(piece).build(piece) if o];repeats.append([digest(o) for o in obs])
 assert repeats[0]==repeats[1]
 bpy.ops.wm.open_mainfile(filepath=str(out/'source'/f'{name}.blend'));obs=[bpy.data.objects[piece['mesh']+s] for s in ([''] if piece['class']=='S' else ['','_LOD1'])]
 records=[geometry(o) for o in obs];assert [rec['digest'] for rec in records]==repeats[0]
 outlines={str(d):{kind:outline_delta(*obs,lens_only=kind=='lens',distance=d) for kind in ('fixture','lens')} for d in (150,260)} if len(obs)==2 else {}
 bindings={};coverage={};kinds={};density={}
 for o in obs:
  density[o.name]={}
  for slot,res in ((0,2048),(1,1024)):
   values=[]
   for tri in o.data.loop_triangles:
    if o.data.polygons[tri.polygon_index].material_index!=slot:continue
    uv=[o.data.uv_layers[0].data[i].uv for i in tri.loops];u,v=uv[1]-uv[0],uv[2]-uv[0];uvarea=abs(u.x*v.y-u.y*v.x)*.5
    if tri.area>1e-12:values.append(res*math.sqrt(uvarea/tri.area))
   if slot==1:assert max(values)<=2048.1,('lens density exceeds 4x512',o.name,max(values))
   density[o.name]['Housing' if slot==0 else 'Lens']={'min_px_per_m':min(values),'median_px_per_m':float(np.median(values)),'max_px_per_m':max(values)}
 for slot,surface in enumerate(('Housing','Lens')):
  nt=bpy.data.materials[surface].node_tree;ims=[n for n in nt.nodes if n.type=='TEX_IMAGE'];bindings[surface]=[]
  for n in ims:
   assert n.image.packed_file
   expected='sRGB' if 'BaseColor' in n.image.name else 'Non-Color';assert n.image.colorspace_settings.name==expected
   uv=n.inputs['Vector'].links[0].from_node.uv_map;assert uv==('FinishBake' if 'Normal' in n.image.name else 'UVMap')
   assert list(n.image.size)==([2048,2048] if slot==0 else [1024,1024])
   bindings[surface].append(dict(image=n.image.name,colorspace=expected,uv=uv,size=list(n.image.size)))
  ns=[n for n in ims if 'Normal' in n.image.name];assert len(ns)==1 and 'Finish_Normal' in ns[0].image.name
  im=ns[0].image;pixels=np.empty(len(im.pixels),np.float32);im.pixels.foreach_get(pixels);pixels=pixels.reshape((im.size[1],im.size[0],4));coverage[surface]=[]
  for o in obs:
   vals=[]
   for f in o.data.polygons:
    if f.material_index!=slot:continue
    uv=sum((o.data.uv_layers[1].data[i].uv for i in f.loop_indices),Vector((0,0)))/len(f.loop_indices);x=min(im.size[0]-1,max(0,int(uv.x*im.size[0])));y=min(im.size[1]-1,max(0,int(uv.y*im.size[1])));vals.append(float(pixels[y,x,2]))
   assert min(vals)>.48,(name,surface,o.name,min(vals));coverage[surface].append(dict(lod=o.name,triangle_centres=len(vals),min_blue=min(vals)))
 for o in obs:
  cs=json.loads(o['chart_layout']);ks=sorted({cs[o.data.attributes['atlas_chart'].data[f.index].value]['kind'] for f in o.data.polygons if f.material_index==1});assert all(k in ('amberglass','mastglass','diamond','indicator_ribs') for k in ks),(name,ks);kinds[o.name]=ks
 neutral=None
 if name=='IndicatorLED':
  neutral={}
  for suffix in ('BaseColor','LightProfile'):
   a=read(out/'textures'/f'T_{name}_Lens_{suffix}.png');err=float(np.max(np.max(a,2)-np.min(a,2)));assert err<1e-6;neutral[suffix]=err
 result=dict(round=4,scope='Independent Blender inspection; producer and Unreal acceptance pending',factory_reset_repeat_digests=repeats,source_equals_generator=True,lods=records,projected_convex_outline_hausdorff_px=outlines,material_bindings=bindings,final_normal_bake_coverage=coverage,lens_chart_kinds=kinds,indicator_achromatic_max_channel_error=neutral,uv0_texel_density=density)
 (out/'evidence/astra_technical.json').write_text(json.dumps(result,indent=2)+'\n');print('ASTRA PASS',name,flush=True)

def sheets(piece):
 if piece['class']!='S':visual_metrics(piece);return
 from wall_lamp_materials import save_map
 out=r.KIT/piece['id'].split('/')[-1]
 for d in (12,150,260,450):
  a=read(out/'review'/f'distance_{d}m_lod0.png');h,w=a.shape[:2];radius=128 if d==12 else 24;scale=2 if d==12 else 8;crop=a[h//2-radius:h//2+radius,w//2-radius:w//2+radius];save_map(out/'review'/f'astra_{d}m_LOD0_only.png',np.repeat(np.repeat(crop,scale,0),scale,1))
 (out/'evidence/astra_distance_comparison.json').write_text(json.dumps(dict(round=4,scope='Class S has one LOD; native 1920x1080 HFOV60 crops at 12/150/260/450 m; no fabricated LOD1'),indent=2)+'\n')

if __name__=='__main__':
 args=sys.argv[sys.argv.index('--')+1:]
 for p in r.PIECES:
  if len(args)>1 and p['id'].split('/')[-1] not in args[1:]:continue
  globals()[args[0]](p)
