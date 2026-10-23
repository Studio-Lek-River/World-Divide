"""Restricted 08 review adapter. Shared/approved generators remain byte-identical."""
import sys,json
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import lighting_393_review as r
import warning_beacon_finish as f
r.textures=f.textures;r.apply=f.apply;r.tint=lambda piece:(1,.22,.001)
import bpy
original_gpu=r.gpu
def review_gpu():
    original_gpu()
    from warning_beacon_cards import cards
    cards()
    scene=bpy.context.scene;tree=bpy.data.node_groups.new('08 review optical bloom','CompositorNodeTree')
    tree.interface.new_socket(name='Image',in_out='OUTPUT',socket_type='NodeSocketColor')
    src=tree.nodes.new('CompositorNodeRLayers');glow=tree.nodes.new('CompositorNodeGlare');dst=tree.nodes.new('NodeGroupOutput')
    glow.inputs['Type'].default_value='Fog Glow';glow.inputs['Quality'].default_value='High';glow.inputs['Threshold'].default_value=1.5;glow.inputs['Strength'].default_value=.45;glow.inputs['Size'].default_value=.4
    tree.links.new(src.outputs['Image'],glow.inputs['Image']);tree.links.new(glow.outputs['Image'],dst.inputs['Image']);scene.compositing_node_group=tree
    def native_no_bloom(scene,*args):scene.render.use_compositing=scene.camera.location.length<2
    bpy.app.handlers.render_pre.append(native_no_bloom)
r.gpu=review_gpu
# The legacy reglaze imports its old optics locally; route this action explicitly.
def reglaze08(piece):
    out=r.KIT/'08_WarningBeacon';bpy.ops.wm.open_mainfile(filepath=str(out/'source/WarningBeacon.blend'))
    obs=[bpy.data.objects[piece['mesh']+s] for s in ('','_LOD1')]
    f.optics(piece,obs,out);f.apply(piece,out,True)
    for im in list(bpy.data.images):
        if im.users==0:bpy.data.images.remove(im)
        elif im.source=='FILE':im.pack();im.filepath=bpy.path.relpath(im.filepath,start=str(out/'source'))
    bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/WarningBeacon.blend'))
r.reglaze=reglaze08
piece=next(p for p in r.PIECES if p['mesh']=='SM_Kit_WarningBeacon')
action=sys.argv[sys.argv.index('--')+1]
assert action in ('prepare','verify','render','shape','reglaze','sheets','stable','audit')
getattr(r,action)(piece)
out=r.KIT/'08_WarningBeacon'
if action=='prepare':
    p=out/'material_recipe.json';d=json.loads(p.read_text());d['round']='4b';d['approval']='Producer review pending'
    d['lens'].update(representation='Real Housing holder/bracket and Lens bulb/concave mirror under thin transparent amber dome; no projected interior',display_tint=[1,.22,.001],hot_core_tint=[1,.92,.48],profile='LightProfile R energy, G thin-shell mask, B reserved zero; emission uses R only. Existing hot range .55-.90 and strength 24.',thin_shell='Mix opaque optical BSDF with pow(BaseColor,.45)-tinted Transparent at G * .91 * (1-Fresnel(1.49)); actual interior geometry preserves parallax. ORM B gives mirror metallic=1.')
    p.write_text(json.dumps(d,indent=2)+'\n')
if action=='sheets':
    for mode in ('day','night'):
        (out/'review'/f'reference_left_round04b_right_{mode}.png').write_bytes((out/'review'/f'reference_left_round04_right_{mode}.png').read_bytes())

if action=='sheets':
    import numpy as np
    from lighting_393_finish import read
    from wall_lamp_materials import save_map
    def resize(a,h):
        w=round(a.shape[1]*h/a.shape[0]);return a[np.linspace(0,a.shape[0]-1,h).astype(int)[:,None],np.linspace(0,a.shape[1]-1,w).astype(int)[None,:]]
    ref=read(out/'reference/producer_reference.png');ref=ref[ref.shape[0]-1010:ref.shape[0]-110,70:600]
    aa=resize(ref,700);bb=resize(read(out/'review/lowpower_night.png'),700);canvas=np.full((700,aa.shape[1]+bb.shape[1]+16,3),.025,np.float32);canvas[:,:aa.shape[1]]=aa;canvas[:,aa.shape[1]+16:]=bb
    save_map(out/'review/reference_left_round04b_right_lowpower.png',canvas)

if action=='verify':
    p=out/'evidence/verification.json';d=json.loads(p.read_text());d['round']='4b';d['lens_shader']['representation']='Thin transparent moulded shell over real Lens bulb/concave mirror and Housing holder/bracket; no projected interior';d['lens_shader']['channels']={'R':'emission energy','G':'thin shell mask','B':'reserved zero'};p.write_text(json.dumps(d,indent=2)+'\n')

if action=='render':
    bpy.context.scene.render.use_compositing=True
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/WarningBeacon_Preview.blend'))
    p=out/'evidence/cameras.json';d=json.loads(p.read_text());d['round']='4b';d['glass_reflection_cards']='Two review-only vertical area cards at (.08,-.4,.245)/(.12,.4,.245) m, .25/.18 W; .028 x .23 m.';d['compositor']='Close views only: Fog Glow, threshold 1.5, strength .45, size .4. Native distance frames and stable ROI: disabled.';p.write_text(json.dumps(d,indent=2)+'\n')

if action=='sheets':
    p=out/'evidence/reference_comparison.json';d=json.loads(p.read_text());d['round']='4b';d['order']='Producer reference LEFT, Round 4b RIGHT; separate day, night and LowPower comparisons';p.write_text(json.dumps(d,indent=2)+'\n')
    # Preserve historical Round 4 filenames after creating the new Round 4b sheets.
    for mode in ('day','night'):
        p=out/'review'/f'reference_left_round04_right_{mode}.png';p.write_bytes((out/'archive/round04/review'/p.name).read_bytes())
if action=='audit':
    import hashlib
    p=out/'evidence/delivery.json';d=json.loads(p.read_text());d['round']='4b'
    for q in Path(__file__).parent.glob('warning_beacon*.py'):d['generator_sha256'][q.name]=hashlib.sha256(q.read_bytes()).hexdigest()
    d['scope']='08 only: real internal geometry, thin transparent amber shell, Cycles reviews and independent FBX checks; no git or Unreal. Producer review pending.'
    p.write_text(json.dumps(d,indent=2)+'\n')

if action=='prepare':
    p=out/'material_recipe.json';d=json.loads(p.read_text());d['lens'].update(json.loads("{\"coat_weight\":0.3,\"coat_roughness\":0.035,\"specular_ior_level\":0.5,\"orm_roughness\":\"shell .075 to .475 (scuffs/rim), bulb .15, mirror .16\",\"thin_shell_surface_albedo_scale\":0.18,\"transmission_colour\":\"pow(linear BaseColor,.45)\",\"channels\":{\"R\":\"radiance energy\",\"G\":\"thin shell=1; bulb/mirror=0\",\"B\":\"reserved zero\"},\"rotation\":\"Rigid local-Z material vertex rotation of Lens G<.5 and Housing z>=.177m/radius<.055m, common phase; shell fixed. See handoff.\"}"));d['uv_beacon']='08 one continuous +X toward +Y outer dome island in each LOD; separate real bulb and mirror islands. Do not sweep/wrap the entire atlas.';p.write_text(json.dumps(d,indent=2)+'\n')
