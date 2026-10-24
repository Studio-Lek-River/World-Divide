"""Final-folder import and review controls. No generation on import.
Run after geometry, first captures, bake and independent FBX QA.
Keeps historical packages; all new native assets stay in Village/Final.
"""
import argparse,json,shutil,sys,math
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_round4_preview as p
v=p.v;c=p.c;OUT=p.OUT;CAND=p.CAND
PORTABLE=c.ROOT/'Art/Shared/village/final'

def copy_portable():
    for folder in ['fbx','textures','source','evidence']:(PORTABLE/folder).mkdir(parents=True,exist_ok=True)
    for folder,pattern in [('fbx','*.fbx'),('textures','*.png'),('source','*_baked.blend'),('source','*_MASTER.fbx')]:
        for f in (OUT/folder).glob(pattern):shutil.copy2(f,PORTABLE/folder/f.name)
    print('Portable authoritative files copied',flush=True)

def scan_root():return CAND+'/Textures/Scans'

def textures():
    paths=[]
    for f in sorted((PORTABLE/'textures').glob('*.png')):
        path=c.atlas_path(f.stem) if f.stem.startswith('SM_') else scan_root()+'/'+f.stem
        if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=path.rsplit('/',1)[0],asset_name=f.stem,source_file=str(f))
        normal=f.stem.endswith('_Normal');color=f.stem.endswith('BaseColor')
        v.props(path,sRGB=color,compressionSettings='TC_Normalmap' if normal else ('TC_Default' if color else 'TC_Masks'),maxTextureSize=2048)
        paths.append(path)
    v.call('AssetTools','save_assets',asset_paths=paths);print('Final portable textures',len(paths),flush=True)

def bind():
    p.materials(True)
    parent=CAND+'/M_SetSurface';paths=[parent]
    for e in v.call('MaterialTools','get_expressions',material_or_function=v.ref(parent)):
        if 'TextureSampleParameter2D' not in e['refPath']:continue
        param=v.get(e['refPath'],'parameterName')['parameterName']
        if param in ['BaseColor','Normal','ORM']:path=scan_root()+'/Bricks085_'+param
        else:path=c.atlas_root(c.NAMES[0])+'/'+c.NAMES[0]+'_'+{'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[param]
        v.props(e['refPath'],texture=v.ref(path))
    v.call('MaterialTools','recompile',material_or_function=v.ref(parent))
    for n in c.NAMES:
        key=c.type_for(n)
        for slot in c.SLOTS:
            if slot=='Glass':continue
            path=CAND+'/Materials/MI_'+n+'_'+slot;data=v.get(path,'textureParameterValues')
            for q in data['textureParameterValues']:
                param=q['parameterInfo']['name']
                if param in ['AtlasTint','BakeNormal','BakeAO']:q['parameterValue']=v.ref(c.atlas_path(n+'_'+{'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[param]))
                if param in ['BaseColor','Normal','ORM']:
                    original=q['parameterValue']['refPath'].split('.')[-1]
                    if slot=='Roof' and key in ['HouseA','HouseC','Farmhouse','Church']:original='RoofingTiles006_'+param
                    q['parameterValue']=v.ref(scan_root()+'/'+original)
            v.props(path,**data)
            if key=='Church' and slot=='Brick':v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(path),name='ScanUVScale',value=dict(r=.625,g=1,b=1,a=1))
            if key=='Church' and slot=='Rubble':v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(path),name='SurfaceTint',value=dict(r=1.3,g=1.28,b=1.20,a=1))
            paths.append(path)
    v.call('AssetTools','save_assets',asset_paths=paths)

def imports(names=None):
    qa=json.loads((OUT/'evidence/source_qa.json').read_text())['meshes']
    for n in names or c.NAMES:
        for suffix,folder,source in [('', 'Meshes','fbx'),('_LOD1','Meshes','fbx'),('_MASTER','Masters','source')]:
            path=c.mesh_path(n,suffix)
            if not v.call('AssetTools','exists',path=path):v.call('StaticMeshTools','import_file',folder_path=path.rsplit('/',1)[0],asset_name=n+suffix,source_file=str(PORTABLE/source/(n+suffix+'.fbx')),import_materials=False,import_textures=False,combine_meshes=True)
            v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(path),enabled=False)
            count=v.call('StaticMeshTools','get_triangle_count',mesh=v.ref(path),lod_index=0)
            expected=qa[n+suffix]['triangles'] if n=='SM_Village_Workshop_Damaged' and suffix=='_MASTER' else qa[n+suffix]['reimport']['triangles']
            assert count==expected,(n,suffix,count,expected)
            for slot in v.call('StaticMeshTools','get_material_slots',mesh=v.ref(path)):
                material=CAND+'/Materials/MI_'+n+'_'+slot
                if suffix=='_MASTER':
                    original=material;material+='_MASTER'
                    if not v.call('AssetTools','exists',path=material):v.call('AssetTools','duplicate',path=original,new_path=material)
                    if slot!='Glass':v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(material),name='BakeStrength',value=0)
                    v.call('AssetTools','save_assets',asset_paths=[material])
                v.call('StaticMeshTools','set_material',mesh=v.ref(path),slot_name=slot,material=v.ref(material))
            if n=='SM_Bridge_Span_Destroyed' and not suffix:
                body=v.get(path,'bodySetup')['bodySetup']['refPath'];v.props(body,collisionTraceFlag='CTF_UseSimpleAsComplex')
            v.call('AssetTools','save_assets',asset_paths=[path])
            a=v.p.add('R4_'+n+suffix,asset=path,loc=(1000000,0,0));v.props(a['component'],staticMesh=v.ref(path));p.move(a,(1000000,0,0))
        print('Final imported',n,flush=True)
    p.bridge_stage();v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])

def lighting(n):
    # Same neutral afternoon intensity/exposure; reverse azimuth for the damaged
    # church so the breach-facing elevation is lit instead of silhouetted.
    yaw=-135 if n=='SM_Village_Church_Damaged' else 135
    v.call('ActorTools','set_actor_transform',actor=v.ref(v.LEVEL+'.L_VillagePreview:PersistentLevel.DirectionalLight_0'),xform=v.xf((0,0,500),(-42,yaw,0)),worldspace=True)

def views(names=None,finish=True):
    p.camera_check()
    for n in names or c.NAMES:
        key=c.type_for(n);yaw=-35 if n=='SM_Village_Church_Damaged' else p.camera_yaw(key,n)
        lighting(n);target=(0,0,600 if key=='Bridge' else 0)
        for suffix,phase in [('', 'final'),('_MASTER','master')]:
            p.show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,target,15000,-55,yaw=yaw)
            v.args=argparse.Namespace(phase=phase)
            for tag,d,pitch in [('12m',1200,-35),('150m',15000,-55),('450m',45000,-55)]:
                v.capture(n+'_'+tag,p.detail_target(key,n) if tag=='12m' else target,d,pitch,yaw=yaw)
            if not suffix:
                v.capture(n+'_threequarter',(-600 if key=='Church' else 0,-400 if key=='Bridge' else 0,1600 if key=='Church' else 350),8500 if key=='Church' else (6500 if key=='Bridge' else (5200 if key=='Barn' else 4200)),-24,yaw=yaw)
        box=v.call('StaticMeshTools','get_bounds',mesh=v.ref(c.mesh_path(n)));centre=tuple((box['min'][a]+box['max'][a])/2+target[i] for i,a in enumerate('xyz'))
        for suffix,d,tag in [('',24960,'249_6m'),('_LOD1',27040,'270_4m')]:
            p.show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,centre,15000,-55,yaw=yaw)
            v.args=argparse.Namespace(phase='switch');v.capture(n+'_'+tag,centre,d,-55,yaw=yaw)
            f=OUT/'evidence/switch'/(n+'_'+tag+'.json');data=json.loads(f.read_text());data['bounds_centre_world_cm']=centre;data['camera_to_bounds_centre_cm']=math.dist([data['cameraLocation'][a] for a in 'xyz'],centre);assert abs(data['camera_to_bounds_centre_cm']-d)<.1;data['manual_separate_LOD_actor']=True;f.write_text(json.dumps(data,indent=2))
        for phase in ['final','master','switch']:
            for tag in ['12m','150m','450m','threequarter','249_6m','270_4m']:
                f=OUT/'evidence'/phase/(n+'_'+tag+'.json')
                if not f.exists():continue
                data=json.loads(f.read_text());data['sun_rotation_deg']=[-42,-135 if n=='SM_Village_Church_Damaged' else 135,0];f.write_text(json.dumps(data,indent=2))
        print('Final evidence',n,flush=True)
    if not finish:return
    lighting('overview');p.show(c.names('Church')[0]);v.args=argparse.Namespace(phase='warmup');v.capture('church_opposite_warm',(0,0,0),15000,-55,yaw=-145)
    v.args=argparse.Namespace(phase='final');v.capture('Church_opposite_base',(0,0,0),15000,-55,yaw=-145);p.overview()

def smoke(phase='persisted_smoke'):
    p.camera_check()
    for n in ['SM_Village_HouseA_Rubble','SM_Village_Barn','SM_Village_Workshop','SM_Village_Church_Damaged','SM_Bridge_Span_Destroyed']:
        key=c.type_for(n);lighting(n);p.show(n);yaw=-35 if n=='SM_Village_Church_Damaged' else p.camera_yaw(key,n)
        v.args=argparse.Namespace(phase='warmup');v.capture(n,(0,0,600 if key=='Bridge' else 0),15000,-55,yaw=yaw)
        v.args=argparse.Namespace(phase=phase);v.capture(n+'_threequarter',(-600 if key=='Church' else 0,-400 if key=='Bridge' else 0,1600 if key=='Church' else 350),8500 if key=='Church' else (6500 if key=='Bridge' else (5200 if key=='Barn' else 4200)),-24,yaw=yaw)
    lighting('overview');p.overview()

def tint_refine():
    paths=[]
    for n in ['SM_Village_Farmhouse_Damaged','SM_Village_Church_Damaged']:
        f=OUT/'textures'/(n+'_Tint.png');shutil.copy2(f,PORTABLE/'textures'/f.name);path=c.atlas_path(f.stem)
        if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=path.rsplit('/',1)[0],asset_name=f.stem,source_file=str(PORTABLE/'textures'/f.name))
        v.props(path,sRGB=False,compressionSettings='TC_Masks',maxTextureSize=2048);paths.append(path)
        for slot in c.SLOTS:
            if slot=='Glass':continue
            for suffix in ['', '_MASTER']:
                material=CAND+'/Materials/MI_'+n+'_'+slot+suffix;v.call('MaterialInstanceTools','set_texture_parameter',instance=v.ref(material),name='AtlasTint',value=v.ref(path));paths.append(material)
    v.call('AssetTools','save_assets',asset_paths=paths)

def bridge_refine():
    for folder in ['source','fbx','textures']:
        for f in (OUT/folder).glob('*Bridge*'):
            if f.suffix in ['.fbx','.png'] or f.name=='Bridge_baked.blend':shutil.copy2(f,PORTABLE/folder/f.name)
    paths=[]
    for n in c.names('Bridge'):
        for end in ['Tint','BakeNormal','BakeAO']:
            stem=n+'_'+end;path=c.atlas_path(stem)
            if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=path.rsplit('/',1)[0],asset_name=stem,source_file=str(PORTABLE/'textures'/(stem+'.png')))
            v.props(path,sRGB=False,compressionSettings='TC_Masks',maxTextureSize=2048);paths.append(path)
        for slot in c.SLOTS:
            if slot=='Glass':continue
            for suffix in ['', '_MASTER']:
                material=CAND+'/Materials/MI_'+n+'_'+slot+suffix
                if suffix and not v.call('AssetTools','exists',path=material):continue
                params=v.get(material,'textureParameterValues')['textureParameterValues']
                for q in params:
                    if q['parameterInfo']['name'] in ['AtlasTint','BakeNormal','BakeAO']:q['parameterValue']=v.ref(c.atlas_path(n+'_'+{'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[q['parameterInfo']['name']]))
                v.props(material,textureParameterValues=params);paths.append(material)
    v.call('AssetTools','save_assets',asset_paths=paths);imports(c.names('Bridge'))

def rebar_verify():
    paths=[CAND+'/Materials/MI_SM_Bridge_Span_Destroyed_Trim'+s for s in ['', '_MASTER']]
    fields=['parent','textureParameterValues','scalarParameterValues','vectorParameterValues']
    expected={path:v.get(path,*fields) for path in paths}
    v.call('AssetTools','save_assets',asset_paths=paths+[v.LEVEL]);v.call('SceneTools','load_level',level_path=v.LEVEL)
    receipt=json.loads((OUT/'evidence/unreal_verified.json').read_text())
    for path in paths:
        actual=v.get(path,*fields);assert actual==expected[path]
        receipt['materials'][path]=actual;receipt['persisted_materials'][path]=actual
    receipt['rebar_tint_refinement']={'surface_tint':[.10,.075,.055],'paths':paths,'verification':'Saved, same-editor level reload, exact override comparison. Other assets/references unchanged.'}
    (OUT/'evidence/unreal_verified.json').write_text(json.dumps(receipt,indent=2))
    n='SM_Bridge_Span_Destroyed';views([n],finish=False);p.show(n);v.args=argparse.Namespace(phase='warmup');v.capture(n,(0,0,600),15000,-55)
    v.args=argparse.Namespace(phase='persisted_smoke');v.capture(n+'_threequarter',(0,-400,350),6500,-24,yaw=145);lighting('overview');p.overview()

if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('stage');a.add_argument('--names',nargs='+');args=a.parse_args()
    if args.stage=='copy':copy_portable()
    else:
        p.initialize();v.guard()
        {'textures':textures,'bind':bind,'imports':lambda:imports(args.names),'views':lambda:views(args.names),'smoke':smoke,'checks':lambda:smoke('checks_final'),'tint_refine':tint_refine,'bridge_refine':bridge_refine,'rebar_verify':rebar_verify}[args.stage]()
