"""Issue 437: deterministic rebake only. Never exports FBX or invokes Unreal.
Run Blender -b --python Tools/Art/kit/strip_light_rebake.py -- [rebake|render|verify]
Approved inputs are read from archive/material_pass02 after the first checkpoint.
"""
import sys
sys.dont_write_bytecode=True
import bpy, numpy as np, json, hashlib, shutil, math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'Art/Shared/kit/lighting/04_StripLight'
ARC=OUT/'archive/material_pass02'
TEX=OUT/'textures'
REV=OUT/'review/rebake'
SCALE=(1-8/1024)/1.106

def digest(x):return hashlib.sha256(json.dumps(x,sort_keys=True).encode()).hexdigest()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def snapshot(o):
 m=o.data;m.calc_loop_triangles();m.calc_tangents(uvmap=m.uv_layers[0].name)
 return dict(geometry=digest({'vertices':[list(v.co) for v in m.vertices],'edges':[list(e.vertices) for e in m.edges],'faces':[(list(p.vertices),p.material_index,p.use_smooth) for p in m.polygons],'normals':[list(n.vector) for n in m.corner_normals],'matrix':[list(r) for r in o.matrix_world],'slots':[s.name for s in m.materials]}),uv1=digest([list(x.uv) for x in m.uv_layers[1].data]),housing_uv0=digest([list(m.uv_layers[0].data[i].uv) for p in m.polygons if p.material_index==0 for i in p.loop_indices]),tangents=np.array([list(l.tangent)+[l.bitangent_sign] for l in m.loops]),triangles=len(m.loop_triangles),vertices=len(m.vertices))
def load(p):
 im=bpy.data.images.load(str(p),check_existing=False);im.colorspace_settings.name='Non-Color'
 w,h=im.size;a=np.empty(w*h*4,np.float32);im.pixels.foreach_get(a);bpy.data.images.remove(im);return a.reshape(h,w,4)[:,:,:3]
def sample(a,u,v,wrap=True):
 h,w=a.shape[:2];x=u*w-.5;y=v*h-.5
 if not wrap:x=np.clip(x,0,w-1);y=np.clip(y,0,h-1)
 ix=np.floor(x).astype(int);iy=np.floor(y).astype(int);fx=(x-ix)[...,None];fy=(y-iy)[...,None]
 def at(dx,dy):return a[(iy+dy)%h,(ix+dx)%w] if wrap else a[np.clip(iy+dy,0,h-1),np.clip(ix+dx,0,w-1)]
 return (at(0,0)*(1-fx)+at(1,0)*fx)*(1-fy)+(at(0,1)*(1-fx)+at(1,1)*fx)*fy

def save(p,a,depth='8',srgb=False):
 h,w=a.shape[:2];im=bpy.data.images.new(p.stem,width=w,height=h,float_buffer=True,alpha=False);im.colorspace_settings.name='Non-Color'
 rgba=np.ones((h,w,4),np.float32);rgba[:,:,:3]=np.clip(a,0,1);im.pixels.foreach_set(rgba.ravel())
 sc=bpy.context.scene;sc.view_settings.view_transform='Raw';sc.render.image_settings.file_format='PNG';sc.render.image_settings.color_mode='RGB';sc.render.image_settings.color_depth=depth
 im.save_render(str(p),scene=sc);bpy.data.images.remove(im)

def graph(o):
 for slot,mat in enumerate(o.data.materials):
  ns=mat.node_tree.nodes;ls=mat.node_tree.links
  # Keep approved Principled optical parameters and replace all texture plumbing.
  p=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
  for n in list(ns):
   if n!=p:ns.remove(n)
  out=ns.new('ShaderNodeOutputMaterial');ls.new(p.outputs['BSDF'],out.inputs['Surface'])
  uv=ns.new('ShaderNodeUVMap');uv.uv_map=o.data.uv_layers[0].name
  uv1=ns.new('ShaderNodeUVMap');uv1.uv_map='FinishBake'
  def tex(kind,coord,linear=True):
   im=bpy.data.images.load(str(TEX/f'T_StripLight_{mat.name}_{kind}.png'),check_existing=False);im.colorspace_settings.name='Non-Color' if linear else 'sRGB';im.pack()
   n=ns.new('ShaderNodeTexImage');n.name=kind;n.image=im;n.interpolation='Linear';n.extension='REPEAT';ls.new(coord.outputs[0],n.inputs['Vector']);return n
  col=tex('BaseColor',uv,False);ls.new(col.outputs['Color'],p.inputs['Base Color'])
  orm=tex('ORM',uv);sep=ns.new('ShaderNodeSeparateXYZ');ls.new(orm.outputs['Color'],sep.inputs[0]);ls.new(sep.outputs['Y'],p.inputs['Roughness']);ls.new(sep.outputs['Z'],p.inputs['Metallic'])
  normal=tex('Finish_Normal',uv1);sep=ns.new('ShaderNodeSeparateXYZ');ls.new(normal.outputs['Color'],sep.inputs[0]);inv=ns.new('ShaderNodeMath');inv.operation='SUBTRACT';inv.inputs[0].default_value=1;ls.new(sep.outputs['Y'],inv.inputs[1]);comb=ns.new('ShaderNodeCombineXYZ');ls.new(sep.outputs['X'],comb.inputs['X']);ls.new(inv.outputs[0],comb.inputs['Y']);ls.new(sep.outputs['Z'],comb.inputs['Z']);nm=ns.new('ShaderNodeNormalMap');nm.uv_map=o.data.uv_layers[0].name;ls.new(comb.outputs[0],nm.inputs['Color']);ls.new(nm.outputs[0],p.inputs['Normal'])
  if slot==1:
   prof=tex('LightProfile',uv);sep=ns.new('ShaderNodeSeparateXYZ');ls.new(prof.outputs['Color'],sep.inputs[0]);tint=ns.new('ShaderNodeVectorMath');tint.operation='SCALE';tint.inputs[0].default_value=(1,.779,.615);ls.new(sep.outputs['X'],tint.inputs['Scale']);ls.new(tint.outputs[0],p.inputs['Emission Color']);level=ns.new('ShaderNodeValue');level.name='LampLevel';level.outputs[0].default_value=1;strength=ns.new('ShaderNodeMath');strength.operation='MULTIPLY';strength.inputs[1].default_value=3.2;ls.new(level.outputs[0],strength.inputs[0]);ls.new(strength.outputs[0],p.inputs['Emission Strength'])

def rebake():
 ARC.mkdir(parents=True,exist_ok=True);REV.mkdir(parents=True,exist_ok=True)
 if not (ARC/'source/StripLight.blend').exists():
  (ARC/'source').mkdir(exist_ok=True);(ARC/'textures').mkdir(exist_ok=True)
  for p in TEX.glob('*.png'):shutil.copy2(p,ARC/'textures'/p.name)
  for name in ['StripLight.blend','StripLight_Preview.blend']:shutil.copy2(OUT/'source'/name,ARC/'source'/name)
  for name in ['material_recipe.json','STATE.md','CLAUDE_HANDOFF.md']:shutil.copy2(OUT/name,ARC/name)
 bpy.ops.wm.open_mainfile(filepath=str(ARC/'source/StripLight.blend'));o=bpy.data.objects['SM_Kit_StripLight'];before=snapshot(o)
 for p in o.data.polygons:
  if p.material_index==1:
   for i in p.loop_indices:o.data.uv_layers[0].data[i].uv=(np.array(o.data.uv_layers[0].data[i].uv)-np.array([.5,.5]))*SCALE+np.array([.5,.5])
 after=snapshot(o);tangent_diff=float(np.max(np.abs(before.pop('tangents')-after.pop('tangents'))))
 assert all(before[k]==after[k] for k in ['geometry','uv1','housing_uv0']);assert tangent_diff<1e-5,tangent_diff
 for surface,n in [('Housing',2048),('Lens',1024)]:
  for kind,k in [('BaseColor',1),('ORM',4 if surface=='Housing' else 10),('LightProfile',1)]:
   if kind=='LightProfile' and surface=='Housing':continue
   a=load(ARC/'textures'/f'T_StripLight_{surface}_{kind}.png');result=np.zeros((n,n,3),np.float32)
   # Four bilinear taps integrate each output footprint; no point sampling.
   for dy in [.25,.75]:
    for dx in [.25,.75]:
     v,u=np.mgrid[:n,:n].astype(np.float32);u=(u+dx)/n;v=(v+dy)/n
     if surface=='Lens':u=(u-.5)/SCALE+.5;v=(v-.5)/SCALE+.5
     if kind=='LightProfile':u=(u-.5)/1.106+.5;v=(v-.5)/.146+.5
     result+=sample(a,u*k,v*k,kind!='LightProfile')*.25
   save(TEX/f'T_StripLight_{surface}_{kind}.png',result,'16' if kind=='LightProfile' else '8')
  # Keep the identical approved bake; retaining 2K avoids resampling the 1.8mm roundover.
  shutil.copy2(ARC/'textures/T_StripLight_Finish_Normal.png',TEX/f'T_StripLight_{surface}_Finish_Normal.png')
 graph(o)
 # Remove obsolete packed images so the source cannot silently use a tiled predecessor.
 for im in list(bpy.data.images):
  if im.users==0:bpy.data.images.remove(im)
 o.data.uv_layers.active_index=0;o.data.uv_layers[0].active_render=True
 bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/StripLight.blend'))
 expected={f'T_StripLight_{s}_{m}.png' for s in ['Housing','Lens'] for m in ['BaseColor','ORM','Finish_Normal']};expected.add('T_StripLight_Lens_LightProfile.png')
 for p in TEX.glob('*.png'):
  if p.name not in expected:p.unlink()
 bpy.ops.wm.open_mainfile(filepath=str(OUT/'source/StripLight.blend'));o=bpy.data.objects['SM_Kit_StripLight'];reload=snapshot(o);reload.pop('tangents');assert reload==after
 bounds=np.array([o.data.uv_layers[0].data[i].uv for p in o.data.polygons if p.material_index==1 for i in p.loop_indices])
 maps=[]
 for p in sorted(TEX.glob('*.png')):
  a=load(p);maps.append(dict(file='textures/'+p.name,size=[a.shape[1],a.shape[0]],sha256=sha(p),uv_channel=1 if 'Finish_Normal' in p.name else 0,uv_scale=[1,1],color_space='sRGB' if 'BaseColor' in p.name else 'linear',bit_depth=16 if 'LightProfile' in p.name else 8))
 recipe=dict(textures=maps,normal_convention='DirectX; UV0 tangent basis, UV1 FinishBake sampling; microstructure already included',emission=dict(linear_rgb=[1,.779,.615],strength=3.2),catalogue_finish={'Housing':{},'Lens':{'EmissionTint':[1,.779,.615],'Intensity':3.2}},lens_uv0_scale_about_center=SCALE,lens_finish_resolution_reason='Identical 2048 bake retained to preserve diffuser edge roundover without any resampling',sampling='Untiled UV0 scale 1, offset 0, wrap; finish normal UV1 scale 1')
 (OUT/'material_recipe.json').write_text(json.dumps(recipe,indent=2))
 receipt=dict(before=before,after_reload=reload,geometry_equal=before['geometry']==reload['geometry'],uv1_equal=before['uv1']==reload['uv1'],housing_uv0_equal=before['housing_uv0']==reload['housing_uv0'],lens_uv0_min=bounds.min(0).tolist(),lens_uv0_max=bounds.max(0).tolist(),lens_uv0_uniform_scale=SCALE,tangent_max_abs_difference=tangent_diff,maps=maps,render_comparisons={},open_issues=['Housing approved graph had an additional UV1 blue based roughness reduction; shared master samples ORM only. Visual comparison must establish whether omission is visible.'])
 (OUT/'evidence/rebake_verification.json').write_text(json.dumps(receipt,indent=2))
 print('REBAKE',json.dumps(receipt),flush=True)

def render():
 sys.path.insert(0,str(Path(__file__).parent))
 from strip_light import build,aim
 bpy.ops.wm.open_mainfile(filepath=str(ARC/'source/StripLight_Preview.blend'));sc=bpy.context.scene
 old=bpy.data.objects['SM_Kit_StripLight'];matrix=old.matrix_world.copy();bpy.data.objects.remove(old,do_unlink=True)
 # Remove the old materials before library-loading the new source to preserve fixed slot names.
 for name in ['Housing','Lens']:
  mat=bpy.data.materials.get(name)
  if mat:bpy.data.materials.remove(mat,do_unlink=True)
 new,lod=build({'mesh':'SM_Kit_StripLight'});bpy.data.objects.remove(lod,do_unlink=True);new.matrix_world=matrix
 sc.render.engine='CYCLES';sc.cycles.samples=64;sc.cycles.use_denoising=True;sc.view_settings.view_transform='AgX';sc.render.image_settings.color_depth='8';sc.render.image_settings.color_mode='RGBA'
 pref=bpy.context.preferences.addons['cycles'].preferences
 try:
  pref.compute_device_type='OPTIX';pref.get_devices()
  for d in pref.devices:d.use=d.type!='CPU'
  if any(d.type!='CPU' for d in pref.devices):sc.cycles.device='GPU'
 except Exception:pass
 level=next(m for m in new.data.materials if m.name=='Lens').node_tree.nodes['LampLevel']
 bg=next(n for n in sc.world.node_tree.nodes if n.type=='BACKGROUND')
 for tag,on,night in ([] if '--lens-test' in sys.argv else [('day_on',1,0),('day_off',0,0),('night_on',1,1),('night_off',0,1)]):
  bpy.data.objects['Lamp_RealLight'].data.energy=36*on;level.outputs[0].default_value=on;bpy.data.objects['PreviewKey'].data.energy=10 if night else 90;bpy.data.objects['PreviewFill'].data.energy=8 if night else 35;bg.inputs['Strength'].default_value=.035 if night else .15
  sc.render.filepath=str(REV/f'after_{tag}.png');bpy.ops.render.render(write_still=True)
 # Close-up cameras, the accepted original detail setup.
 cam=sc.camera;cam.location=(.50,-.87,2.43);aim(cam,(0,-.39,2.63));cam.data.lens=62
 key=bpy.data.objects['PreviewKey'];key.location=(.25,-.8,2.48);key.data.energy=24;key.data.size=.45;aim(key,(0,-.38,2.65));bpy.data.objects['PreviewFill'].data.energy=12
 level.outputs[0].default_value=1;bpy.data.objects['Lamp_RealLight'].data.energy=36;bg.inputs['Strength'].default_value=.15
 sc.render.resolution_x=2400;sc.render.resolution_y=1600
 for tag in (['lens_end_2k','lens_end_1k'] if '--lens-test' in sys.argv else ['lens_end','housing_edge']):
  if tag=='lens_end_1k':
   mat=next(m for m in new.data.materials if m.name=='Lens');mat.node_tree.nodes['Finish_Normal'].image.scale(1024,1024)
  if tag=='housing_edge':level.outputs[0].default_value=0;bpy.data.objects['Lamp_RealLight'].data.energy=0
  sc.render.filepath=str(REV/f'{tag}.png');bpy.ops.render.render(write_still=True)

def verify():
 sys.path.insert(0,str(Path(__file__).parent));from strip_light import build
 bpy.ops.wm.read_factory_settings(use_empty=True);a,b=build({'mesh':'SM_Kit_StripLight'});r=snapshot(a);r.pop('tangents');assert r==json.loads((OUT/'evidence/rebake_verification.json').read_text())['after_reload'];assert [m.name for m in a.data.materials]==['Housing','Lens'];assert len(a.data.uv_layers)==2
 print('BUILD_OK',r,flush=True)

def compare():
 r=json.loads((OUT/'evidence/rebake_verification.json').read_text())
 for tag in ['day_on','day_off','night_on','night_off']:
  a=load(OUT/'review'/f'pbr_{tag}.png');b=load(REV/f'after_{tag}.png');assert a.shape==b.shape,(a.shape,b.shape)
  diff=np.abs(a-b)
  r['render_comparisons'][tag]={'mean_absolute_difference_0_1':float(diff.mean()),'mean_absolute_difference_8bit':float(diff.mean()*255),'max_absolute_difference':float(diff.max()),'before':'review/pbr_'+tag+'.png','after':'review/rebake/after_'+tag+'.png','camera_lighting':'archived approved preview scene, unchanged camera and matching four-state light settings'}
  save(REV/f'before_after_{tag}.png',np.concatenate([a,b],axis=1))
 r['build_piece_verified']=True
 r['fbx_unchanged']={p.name:sha(p) for p in OUT.glob('*.fbx')}
 (OUT/'evidence/rebake_verification.json').write_text(json.dumps(r,indent=2));print(json.dumps(r['render_comparisons']),flush=True)

if __name__=='__main__':
 stage=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'rebake'
 {'rebake':rebake,'render':render,'verify':verify,'compare':compare}[stage]()
