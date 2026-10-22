"""Candidate-only orchestration of discovered native Unreal MCP tools.

No Unreal Python execution. Uses existing recipe read-only. Run stages explicitly.
"""
import json,sys,math,time,base64,hashlib
from pathlib import Path
from usa_hq_379_mcp import rpc
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
WORK=ROOT/'issue379';CAND='/Game/Art/Candidates/USA_HQ/Candidate22';PROD='/Game/Art/USA/Buildings/USAHQ'
SETS={n:f'editor_toolset.toolsets.{s}.{n}' for n,s in [('AssetTools','asset'),('ActorTools','actor'),('SceneTools','scene'),('StaticMeshTools','static_mesh'),('ObjectTools','object')]}
SETS['EditorAppToolset']='EditorToolset.EditorAppToolset'
STATE=WORK/'preview_state.json'
state=json.loads(STATE.read_text()) if STATE.exists() else {'actors':{},'receipts':[]}
def ref(path):
    if path.startswith(('/Game/','/Engine/')) and '.' not in path:path+='.'+path.rsplit('/',1)[-1]
    return {'refPath':path}
def unpack(result):
    if result.get('isError'):raise RuntimeError(result)
    for block in result.get('content',[]):
        if block['type']=='text':
            try:return json.loads(block['text']).get('returnValue')
            except (ValueError,AttributeError):raise RuntimeError(result)
    raise RuntimeError(result)
def call(s,t,**args):
    result=rpc('tools/call',{'name':'call_tool','arguments':{'toolset_name':SETS[s],'tool_name':t,'arguments':args}})
    value=unpack(result)
    if value is False and t not in ('exists','is_nanite_enabled'):raise RuntimeError(f'{t} returned false: {args}')
    state['receipts'].append({'tool':t,'args':args,'value':value})
    STATE.write_text(json.dumps(state,indent=2))
    return value
def props(path,**values):return call('ObjectTools','set_properties',instance=ref(path),values=json.dumps(values))
def component(actor):
    cs=call('ActorTools','get_components',actor=ref(actor))
    return next(c['refPath'] for c in cs if 'StaticMeshComponent' in c['refPath'])
def add(label,path,location=(0,0,0),rotation=(0,0,0),scale=(1,1,1)):
    if label in state['actors']:return state['actors'][label]
    actor=call('SceneTools','add_to_scene_from_asset',asset_path=path,name=label,
        xform={'location':dict(zip('xyz',location)),'rotation':dict(zip(('pitch','yaw','roll'),rotation)),'scale':dict(zip('xyz',scale))})['refPath']
    comp=component(actor)
    state['actors'][label]={'actor':actor,'component':comp,'mesh':path}
    props(comp,customPrimitiveData={'data':[0,0,0,0,0.025,0.16,0.8,1,1]},castShadow=True)
    return state['actors'][label]
def bind(path,slots):
    props(path,staticMaterials=[{'materialSlotName':s,'importedMaterialSlotName':s,
        'materialInterface':ref(f'{PROD}/Materials/{s if s.startswith("MI_") else "MI_"+s}')} for s in slots],naniteSettings={'bEnabled':False})
    call('StaticMeshTools','remove_collisions',mesh=ref(path))
def setup(bind_materials=True):
    recipe=json.loads((ROOT/'usa_hq.json').read_text());game=json.loads((ROOT/'game/game_meshes.json').read_text())
    for name in ('SM_HQ_ShellDetail','SM_HQ_ShellDetail_LOD1'):
        path=f'{CAND}/Shells/{name}'
        if not call('AssetTools','exists',path=path):
            call('StaticMeshTools','import_file',folder_path=f'{CAND}/Shells',asset_name=name,
                source_file=str(WORK/'preview_fbx'/f'{name}.fbx'))
        if bind_materials:bind(path,game['meshes']['SM_HQ_ShellDetail']['slots'])
    # Dense shell materials are component overrides. Avoid PostEditChange/rebuild
    # on the dense source while the far reduction is compiling.
    add('Shell',f'{CAND}/Shells/SM_HQ379_ShellReview')
    props(state['actors']['Shell']['component'],forcedLodModel=2,overrideMaterials=[ref(f'{PROD}/Materials/{s}') for s in game['meshes']['SM_HQ_Shell']['slots']])
    add('ShellDetail',f'{CAND}/Shells/SM_HQ_ShellDetail')
    for p in recipe['parts']:
        if p['name'] in ('Shell','ShellDetail') or p['name'].startswith('Beacon_'):continue
        record=add(p['name'],f'{PROD}/Meshes/{p["mesh"]}',p['location'],p['rotation'],p['scale'])
        props(record['component'],forcedLodModel=1,castShadow=p['cast_shadow'])
    previous=call('SceneTools','find_actors',name='HQ379_MastCore_H28',tag='',collision_channels=[])
    if previous:
        actor=previous[0]['refPath']
        state['actors']['MastCore']={'actor':actor,'component':component(actor)}
    else:add('MastCore',f'{CAND}/FinalMasts/SM_HQ22_MastCore_H28',(-898.71225,0,1151))
    add('MastEast',f'{CAND}/FinalMasts/SM_HQ22_MastEast_H28',(-1090,-1995,770))
    for key,xy in [('Core',(-898.71225,0)),('East',(-1090,-1995))]:
        add('Beacon'+key,f'{PROD}/Meshes/SM_Beacon_Obstruction',(*xy,2800 if key=='Core' else 2400),scale=(1.75,)*3)
        props(state['actors']['Beacon'+key]['component'],castShadow=False,forcedLodModel=1)
    for key in ('MastCore','MastEast'):props(state['actors'][key]['component'],customPrimitiveData={'data':[0,0,0,0,0.025,0.16,0.8,1,1]})
    ground=add('PreviewGround','/Engine/BasicShapes/Plane',(0,0,-4),scale=(200,200,1))
    props(ground['component'],overrideMaterials=[ref(f'{PROD}/Materials/MI_HQ14_Paving')],castShadow=False)
    props(state['actors']['MastCore']['component'],staticMesh=ref(f'{CAND}/FinalMasts/SM_HQ22_MastCore_H28'))
    state['height']=28;state['mode']='game';STATE.write_text(json.dumps(state,indent=2))
    call('AssetTools','save_assets',asset_paths=[f'{CAND}/L_HQ379_Masts',f'{CAND}/Shells/SM_HQ379_ShellReview',f'{CAND}/Shells/SM_HQ_ShellDetail',f'{CAND}/Shells/SM_HQ_ShellDetail_LOD1'])
def import_masts():
    manifest=json.loads((WORK/'mast_manifest.json').read_text())
    for records in manifest['options'].values():
        for r in records:
            path=f'{CAND}/FinalMasts/{r["mesh"]}'
            if not call('AssetTools','exists',path=path):
                call('StaticMeshTools','import_file',folder_path=f'{CAND}/FinalMasts',asset_name=r['mesh'],source_file=str(ROOT/r['fbx']))
            bind(path,r['slots'])
            actual=call('StaticMeshTools','get_triangle_count',mesh=ref(path),lod_index=0)
            assert actual==r['triangles'], (path,actual,r['triangles'])
            call('AssetTools','save_assets',asset_paths=[path])
            print('Imported final',r['mesh'],actual,flush=True)

def import_detail_clean():
    log=Path(__file__).resolve().parents[2]/'Saved/Logs/WorldDivide.log'
    start=log.stat().st_size
    path=f'{CAND}/Shells/SM_HQ_ShellDetail_LOD1_Final'
    call('StaticMeshTools','import_file',folder_path=f'{CAND}/Shells',asset_name='SM_HQ_ShellDetail_LOD1_Final',source_file=str(WORK/'preview_fbx/SM_HQ_ShellDetail_LOD1.fbx'))
    game=json.loads((ROOT/'game/game_meshes.json').read_text())
    bind(path,game['meshes']['SM_HQ_ShellDetail']['slots'])
    count=call('StaticMeshTools','get_triangle_count',mesh=ref(path),lod_index=0)
    assert count==game['meshes']['SM_HQ_ShellDetail']['triangles'][1],count
    call('AssetTools','save_assets',asset_paths=[path])
    with log.open('rb') as f:
        f.seek(start);excerpt=f.read().decode('utf8',errors='replace')
    (WORK/'unreal_detail_clean_import.log').write_text(excerpt)
    warnings=[line for line in excerpt.splitlines() if 'Warning:' in line and ('tangent' in line.lower() or 'binormal' in line.lower() or 'bi-normal' in line.lower())]
    receipt={'asset':path,'triangles':count,'tangent_warnings':warnings,'source_sha256':hashlib.sha256((ROOT/'game/SM_HQ_ShellDetail_LOD1.fbx').read_bytes()).hexdigest(),'saved':True}
    (WORK/'unreal_tangent_import_check.json').write_text(json.dumps(receipt,indent=2))
    assert not warnings,warnings
    print('Clean detail LOD1 imported',count,flush=True)
def save():
    paths=call('AssetTools','find_assets',folder_path=CAND,name='')
    call('AssetTools','save_assets',asset_paths=paths)
def pose(height,mode='game'):
    far=mode=='far';east={24:21,28:24,32:27}[height];width={24:1.5,28:1.75,32:2}[height]
    for key,tip in [('Core',height),('East',east)]:
        path=f'{CAND}/FinalMasts/SM_HQ22_Mast{key}_H{height}'+('_LOD1' if far else '')
        props(state['actors']['Mast'+key]['component'],staticMesh=ref(path))
        call('ActorTools','set_actor_transform',actor=ref(state['actors']['Beacon'+key]['actor']),
            xform={'location':{'x':-898.71225 if key=='Core' else -1090,'y':0 if key=='Core' else -1995,'z':tip*100},'scale':dict(zip('xyz',[width]*3))})
        props(state['actors']['Beacon'+key]['component'],forcedLodModel=2 if far else 1)
    props(state['actors']['Shell']['component'],staticMesh=ref(f'{CAND}/Shells/SM_HQ379_Shell'+('Far' if far else 'Review')),forcedLodModel=1 if mode=='master' else 2)
    props(state['actors']['ShellDetail']['component'],staticMesh=ref(f'{CAND}/Shells/SM_HQ_ShellDetail'+('_LOD1_Final' if far else '')))
    for label,r in state['actors'].items():
        if label in ('Shell','ShellDetail','MastCore','MastEast','BeaconCore','BeaconEast','PreviewGround'):continue
        lods=call('StaticMeshTools','get_lod_count',mesh=ref(r['mesh']))
        props(r['component'],forcedLodModel=2 if far and lods>1 else 1)
    state['height']=height;state['mode']=mode;STATE.write_text(json.dumps(state,indent=2))
def capture(name,distance,target=(0,0,400),pitch=-55,yaw=135):
    rad=math.pi/180;d=distance*100
    direction=(math.cos(pitch*rad)*math.cos(yaw*rad),math.cos(pitch*rad)*math.sin(yaw*rad),math.sin(pitch*rad))
    xf={'location':dict(zip('xyz',[target[i]-d*direction[i] for i in range(3)])),
        'rotation':{'pitch':pitch,'yaw':yaw,'roll':0}}
    call('EditorAppToolset','SetCameraTransform',transform=xf)
    time.sleep(2)
    args={'captureTransform':xf,'annotations':{'gridSpacing':0,'gridExtent':0,'gridHeight':0,'maxLabelDistance':0,'classFilter':ref(''),'maxLabels':0},'bShowUI':False}
    result=rpc('tools/call',{'name':'call_tool','arguments':{'toolset_name':SETS['EditorAppToolset'],'tool_name':'CaptureViewport','arguments':args}})
    value=unpack(result);img=value.pop('image');out=ROOT/'renders/issue379';out.mkdir(exist_ok=True)
    (out/f'{name}_raw.png').write_bytes(base64.b64decode(img['data']))
    value.update({'arm_m':distance,'target_cm':target,'mode':state['mode'],'height':state['height'],'capture_type':'Unreal editor; manually synchronized LOD selection; not gameplay'})
    (out/f'{name}.json').write_text(json.dumps(value,indent=2))
    print('Captured',name,flush=True)
def shots(part='all'):
    for h in (24,28,32):
        pose(h,'game')
        call('AssetTools','save_assets',asset_paths=[f'{CAND}/L_HQ379_Masts'])
        option_level=f'{CAND}/L_HQ379_H{h}'
        if not call('AssetTools','exists',path=option_level):call('AssetTools','duplicate',path=f'{CAND}/L_HQ379_Masts',new_path=option_level)
        call('AssetTools','save_assets',asset_paths=[option_level])
        for d in (150,12):
            capture(f'H{h}_game_{d}m',d,(-898.71225,0,h*100-300) if d==12 else (0,0,400))
        capture(f'H{h}_game_249p6m',249.6)
        capture(f'H{h}_east_12m',12,(-1090,-1995,{24:2100,28:2400,32:2700}[h]-300))
        capture(f'H{h}_overview',85,(0,0,700),-35)
        pose(h,'master')
        for d in (12,150,450):capture(f'H{h}_master_{d}m',d,(-898.71225,0,h*100-300) if d==12 else (0,0,400))
        if part=='all':
            pose(h,'far')
            capture(f'H{h}_game_270p4m',270.4);capture(f'H{h}_game_450m',450)
    pose(28,'game');capture('H28_review_default',150)
    call('AssetTools','save_assets',asset_paths=[f'{CAND}/L_HQ379_Masts'])

def far_build():
    path=f'{CAND}/Shells/SM_HQ379_ShellFar'
    if not call('AssetTools','exists',path=path):
        call('AssetTools','duplicate',path=f'{CAND}/Shells/SM_HQ379_ShellReview',new_path=path)
    call('StaticMeshTools','generate_lods',mesh=ref(path),triangle_percents=[19000/2915364])
    # Finish this exact mesh before touching its source duplicate.
    count=call('StaticMeshTools','get_triangle_count',mesh=ref(path),lod_index=1)
    print('Far triangles',count,flush=True)
    assert count<=19000
    call('AssetTools','save_assets',asset_paths=[path])

def far_shots():
    for h in (24,28,32):
        pose(h,'far')
        capture(f'H{h}_game_270p4m',270.4);capture(f'H{h}_game_450m',450)
    counts();pose(28,'game');counts();capture('H28_review_default',150)
    call('AssetTools','save_assets',asset_paths=[f'{CAND}/L_HQ379_Masts'])
def counts():
    result={}
    for label,r in state['actors'].items():
        if label=='PreviewGround':continue
        p=unpack(rpc('tools/call',{'name':'call_tool','arguments':{'toolset_name':SETS['ObjectTools'],'tool_name':'get_properties','arguments':{'instance':ref(r['component']),'properties':['staticMesh','forcedLodModel']}}}))
        p=json.loads(p);mesh=p['staticMesh'];lod=max(0,p['forcedLodModel']-1)
        result[label]={'mesh':mesh,'lod':lod,'triangles':call('StaticMeshTools','get_triangle_count',mesh=mesh,lod_index=lod)}
    (WORK/f'counts_{state.get("mode","unknown")}.json').write_text(json.dumps({'total':sum(r['triangles'] for r in result.values()),'meshes':result},indent=2))
    print('Whole HQ triangles',sum(r['triangles'] for r in result.values()),flush=True)

def audit():
    result={}
    for records in json.loads((WORK/'mast_manifest.json').read_text())['options'].values():
        for r in records:
            path=f'{CAND}/FinalMasts/{r["mesh"]}'
            p=json.loads(call('ObjectTools','get_properties',instance=ref(path),properties=['staticMaterials','naniteSettings']))
            mats=p['staticMaterials']
            assert [m['materialSlotName'] for m in mats]==r['slots'],(path,mats)
            assert all(m['materialInterface']['refPath'].endswith('MI_'+s+'.MI_'+s) for m,s in zip(mats,r['slots'])),(path,mats)
            assert not call('StaticMeshTools','is_nanite_enabled',mesh=ref(path))
            result[r['mesh']]={'properties':p,'bounds_cm':call('StaticMeshTools','get_bounds',mesh=ref(path)),'triangles':call('StaticMeshTools','get_triangle_count',mesh=ref(path),lod_index=0)}
    (WORK/'unreal_final_audit.json').write_text(json.dumps(result,indent=2))

def baseline_counts():
    recipe=json.loads((ROOT/'usa_hq.json').read_text());meshes={}
    for name in sorted({p['mesh'] for p in recipe['parts']}):
        mesh=ref(f'{PROD}/Meshes/{name}');n=call('StaticMeshTools','get_lod_count',mesh=mesh)
        meshes[name]=[call('StaticMeshTools','get_triangle_count',mesh=mesh,lod_index=0),call('StaticMeshTools','get_triangle_count',mesh=mesh,lod_index=1 if n>1 else 0)]
    result={'meshes':meshes,'whole':[sum(meshes[p['mesh']][i] for p in recipe['parts']) for i in (0,1)]}
    (WORK/'baseline_unreal_counts.json').write_text(json.dumps(result,indent=2));print(result,flush=True)

def reload_check():
    call('AssetTools','save_assets',asset_paths=[f'{CAND}/L_HQ379_Masts'])
    call('SceneTools','load_level',level_path=f'{CAND}/L_HQ379_Masts')
    result={}
    for label in ('Shell','ShellDetail','MastCore','MastEast','BeaconCore','BeaconEast'):
        result[label]=json.loads(call('ObjectTools','get_properties',instance=ref(state['actors'][label]['component']),properties=['staticMesh','forcedLodModel','customPrimitiveData','overrideMaterials']))
    assert result['MastCore']['staticMesh']['refPath'].endswith('SM_HQ22_MastCore_H28.SM_HQ22_MastCore_H28')
    assert result['MastEast']['staticMesh']['refPath'].endswith('SM_HQ22_MastEast_H28.SM_HQ22_MastEast_H28')
    assert result['Shell']['forcedLodModel']==2
    (WORK/'reload_verification.json').write_text(json.dumps(result,indent=2))
    capture('H28_selected_reloaded',150)
if __name__=='__main__':
    rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'hq379preview','version':'1'}})
    assert call('SceneTools','get_current_level')==f'{CAND}/L_HQ379_Masts','Refusing edits outside candidate preview'
    stage=sys.argv[1]
    if stage=='import_masts':import_masts()
    elif stage=='import_detail_clean':import_detail_clean()
    elif stage=='setup':setup()
    elif stage=='assemble':setup(False)
    elif stage=='pose':pose(int(sys.argv[2]),sys.argv[3])
    elif stage=='shots':shots()
    elif stage=='nearshots':shots('near')
    elif stage=='far_build':far_build()
    elif stage=='farshots':far_shots()
    elif stage=='counts':counts()
    elif stage=='audit':audit()
    elif stage=='baseline_counts':baseline_counts()
    elif stage=='reload_check':reload_check()
    elif stage=='capture':capture(sys.argv[2],float(sys.argv[3]))

