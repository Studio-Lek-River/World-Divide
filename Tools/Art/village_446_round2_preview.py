"""Issue446 round2 native MCP preview. Existing level only, no native dialogs."""
import sys,json,shutil,argparse,math
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_preview as v
p=v.p; ROOT=v.ROOT; OUT=ROOT/'Art/Shared/village/issue446/round2'
CAND='/Game/Art/Candidates/Village/Rebar446/R2/Review01'; OLD='/Game/Art/Candidates/Village/Final'
NAME='SM_Bridge_Span_Destroyed'
def mesh_path(suffix=''):return CAND+('/Masters02/' if suffix=='_MASTER' else '/Meshes/')+NAME+suffix
for folder in ['source','fbx','textures','evidence']:(OUT/folder).mkdir(parents=True,exist_ok=True)
v.OUT=OUT;p.OUT=OUT
if not (OUT/'evidence/preview_state.json').exists():
    shutil.copy2(ROOT/'Art/Shared/village/issue446/round1/evidence/preview_state.json',OUT/'evidence/preview_state.json')
def move(a,loc,scale=(1,1,1)):
    v.call('ActorTools','set_actor_transform',actor=v.ref(a['actor']),xform=v.xf(loc,scale=scale),worldspace=True)
def init():
    p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'rebar446','version':'1'}})
    p.discover()
    current=v.call('SceneTools','get_current_level')
    assert not v.call('EditorAppToolset','IsPIERunning')
    if current!=v.LEVEL:
        assert not v.call('AssetTools','is_dirty',asset_path=current),'Preserve unsaved editor work'
        v.call('SceneTools','load_level',level_path=v.LEVEL)
    v.guard()
def show(suffix='',baseline=False):
    state=p.state()
    if state.get('r446_active'):
        move(state['actors'][state['r446_active']],(1000000,0,0))
    else:
        for label,a in state['actors'].items():
            if label.startswith(('R4_SM_','R446R2_')):move(a,(1000000,0,0))
    if baseline:a=state['actors']['R4_'+NAME+suffix]
    else:a=state['actors']['R446R2_'+NAME+suffix]
    move(a,(0,0,600))
    state['r446_active']=('R4_' if baseline else 'R446R2_')+NAME+suffix;p.store(state)
    # Preserve accepted preview water above the ground to avoid coplanar z-fighting.
    move(state['actors']['R4_Water'],(0,0,10),(70,40,.02))
    for i in [-1,1]:move(state['actors']['R4_Pier'+str(i)],(i*1500,0,200),(.8,15,4.5))
    for i in range(3):
        for part,z in [('Hull',0),('Turret',180.681836605)]:move(state['actors']['Abrams'+part+'_'+str(i)],(100,-2300,z) if i==0 else (1000000,i*2400,z),(1.03599574,)*3)
    v.call('ActorTools','set_actor_transform',actor=v.ref(v.LEVEL+'.L_VillagePreview:PersistentLevel.DirectionalLight_0'),xform=v.xf((0,0,500),(-42,135,0)),worldspace=True)
    v.call('RTSConsoleToolset','ExecuteConsoleCommand',command='ShowFlag.BillboardSprites 0')
def capture(phase,tag,target,distance,pitch=-55):
    v.args=argparse.Namespace(phase=phase)
    v.capture(NAME+'_'+tag,target,distance,pitch,yaw=145)
    f=OUT/'evidence'/phase/(NAME+'_'+tag+'.json');data=json.loads(f.read_text());data['sun_rotation_deg']=[-42,135,0];f.write_text(json.dumps(data,indent=2))
    assert data['cameraFOV']==60,('FOV',data['cameraFOV'])
def cameras():
    a=p.state()['actors']['Village_ReviewCamera'];v.props(a['component'],fieldOfView=60,aspectRatio=16/9,bConstrainAspectRatio=True)
    v.args=argparse.Namespace(phase='checks');v.capture('camera_probe',(0,0,600),15000,-55)
    data=json.loads((OUT/'evidence/checks/camera_probe.json').read_text())
    if data['cameraFOV']!=60:
        ident='SlateInspectorToolset.SlateInspectorToolset'
        schema=p.rpc('tools/call',{'name':'describe_toolset','arguments':{'toolset_name':ident}});(OUT/'evidence/schema_Slate.json').write_text(json.dumps(schema))
        snap=v.call(ident,'Snapshot',ref='',maxDepth=10);(OUT/'evidence/camera_slate.txt').write_text(snap)
        v.call(ident,'Observe',ref='w1',maxDepth=30)
        snap=v.call(ident,'Snapshot',ref='w1',maxDepth=20);(OUT/'evidence/camera_slate_deep.txt').write_text(snap)
        assert 'L_VillagePreview' in snap and '[ref=sp4]' in snap
        v.call(ident,'Click',ref='sp4');v.call('EditorAppToolset','SelectActors',actors=[v.ref(a['actor'])]);v.call(ident,'PressKey',key='Ctrl+Shift+P')
        v.capture('camera_piloted',(0,0,600),15000,-55)
        assert json.loads((OUT/'evidence/checks/camera_piloted.json').read_text())['cameraFOV']==60
    return True
def views(baseline=False):
    assert cameras(),'Pilot review camera using observed viewport ref, then retry'
    for suffix,phase in [('', 'before' if baseline else 'after'),('_MASTER','before_master' if baseline else 'master')]:
        show(suffix,baseline);capture('warmup','warm',(0,0,600),15000)
        for tag,d,pitch in [('12m',1200,-35),('150m',15000,-55),('450m',45000,-55)]:capture(phase,tag,(0,0,600),d,pitch)
    path=(OLD+'/Meshes02/' if baseline else CAND+'/Meshes/')+NAME
    box=v.call('StaticMeshTools','get_bounds',mesh=v.ref(path));centre=tuple((box['min'][a]+box['max'][a])/2+(600 if i==2 else 0) for i,a in enumerate('xyz'))
    for suffix,d,tag in [('',24960,'249_6m'),('_LOD1',27040,'270_4m')]:
        show(suffix,baseline);capture('before_switch' if baseline else 'switch',tag,centre,d)
    show('',baseline)
    v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])
def imports():
    assert not v.call('EditorAppToolset','GetOpenAssets'),'Close material editors before edits'
    paths=[]
    for end in ['Tint','BakeNormal','BakeAO']:
        stem=NAME+'_'+end;path=CAND+'/Textures/'+stem
        assert not v.call('AssetTools','exists',path=path),'Fresh import path required'
        v.call('TextureTools','import_file',folder_path=CAND+'/Textures',asset_name=stem,source_file=str(OUT/'textures'/(stem+'.png')))
        v.props(path,sRGB=False,compressionSettings='TC_Masks',maxTextureSize=2048);paths.append(path)
    for suffix in ['', '_MASTER']:
        for slot in ['Brick','Roof','Trim','Rubble']:
            name='MI_'+NAME+'_'+slot+suffix;path=CAND+'/Materials/'+name
            v.call('AssetTools','duplicate',path=OLD+'/Materials/'+name,new_path=path)
            for param,end in [('AtlasTint','Tint'),('BakeNormal','BakeNormal'),('BakeAO','BakeAO')]:v.call('MaterialInstanceTools','set_texture_parameter',instance=v.ref(path),name=param,value=v.ref(CAND+'/Textures/'+NAME+'_'+end))
            paths.append(path)
    for suffix,folder in [('', 'fbx'),('_LOD1','fbx'),('_MASTER','source')]:
        path=mesh_path(suffix)
        v.call('StaticMeshTools','import_file',folder_path=CAND+'/Meshes',asset_name=NAME+suffix,source_file=str(OUT/folder/(NAME+suffix+'.fbx')),import_materials=False,import_textures=False,combine_meshes=True)
        v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(path),enabled=False)
        assert v.call('StaticMeshTools','get_material_slots',mesh=v.ref(path))==['Brick','Roof','Trim','Rubble']
        for slot in ['Brick','Roof','Trim','Rubble']:v.call('StaticMeshTools','set_material',mesh=v.ref(path),slot_name=slot,material=v.ref(CAND+'/Materials/MI_'+NAME+'_'+slot+('_MASTER' if suffix=='_MASTER' else '')))
        if suffix=='':
            body=v.get(path,'bodySetup')['bodySetup']['refPath'];v.props(body,collisionTraceFlag='CTF_UseSimpleAsComplex')
        a=p.add('R446R2_'+NAME+suffix,asset=path,loc=(1000000,0,0));v.props(a['component'],staticMesh=v.ref(path));paths.append(path)
        print('IMPORTED',path,flush=True)
    v.call('AssetTools','save_assets',asset_paths=paths+[v.LEVEL])
def verify():
    report={'meshes':{},'materials':{},'textures':{}}
    for suffix in ['','_LOD1','_MASTER']:
        path=mesh_path(suffix)
        rec={'path':path,'triangles':v.call('StaticMeshTools','get_triangle_count',mesh=v.ref(path),lod_index=0),'bounds_cm':v.call('StaticMeshTools','get_bounds',mesh=v.ref(path)),'slots':v.call('StaticMeshTools','get_material_slots',mesh=v.ref(path)),'bindings':v.get(path,'staticMaterials'),'nanite':v.call('StaticMeshTools','is_nanite_enabled',mesh=v.ref(path)),'native_lods':v.call('StaticMeshTools','get_lod_count',mesh=v.ref(path)),'source_tags':v.call('AssetTools','get_asset_tags',asset_path=path)}
        expected=json.loads((OUT/'evidence/source_qa.json').read_text())[NAME+suffix]['triangles']; assert rec['triangles']==expected and not rec['nanite'] and rec['native_lods']==1
        if not suffix:
            body=v.get(path,'bodySetup')['bodySetup']['refPath'];rec['collision']=v.get(body,'aggGeom','collisionTraceFlag');assert len(rec['collision']['aggGeom']['convexElems'])==2
            oldbody=v.get(OLD+'/Meshes02/'+NAME,'bodySetup')['bodySetup']['refPath'];rec['collision_matches_baseline']=rec['collision']==v.get(oldbody,'aggGeom','collisionTraceFlag');assert rec['collision_matches_baseline']
        report['meshes'][NAME+suffix]=rec
    for suffix in ['','_MASTER']:
        for slot in ['Brick','Roof','Trim','Rubble']:
            path=CAND+'/Materials/MI_'+NAME+'_'+slot+suffix
            fields=['parent','textureParameterValues','scalarParameterValues','vectorParameterValues'];data=v.get(path,*fields)
            original=v.get(OLD+'/Materials/MI_'+NAME+'_'+slot+suffix,*fields)
            for field in ['parent','scalarParameterValues','vectorParameterValues']:assert data[field]==original[field],(slot,field)
            report['materials'][path]=data
    for end in ['Tint','BakeNormal','BakeAO']:
        path=CAND+'/Textures/'+NAME+'_'+end;report['textures'][path]=v.get(path,'sRGB','compressionSettings','maxTextureSize','mipGenSettings','neverStream')
    paths=[r['path'] for r in report['meshes'].values()]+list(report['materials'])+list(report['textures'])+[v.LEVEL]
    v.call('AssetTools','save_assets',asset_paths=paths)
    v.call('SceneTools','load_level',level_path=v.LEVEL)
    for path,data in report['materials'].items():assert v.get(path,*data.keys())==data
    report['saved_and_same_editor_reload_verified']=True;report['scope']='Unreal editor preview; separate manual LOD1, no production/runtime proof.'
    report['direct_dependencies']={path:v.call('AssetTools','get_dependencies',asset_path=path) for path in [r['path'] for r in report['meshes'].values()]+list(report['materials'])}
    report['log']=v.call('LogsToolset','GetLogEntries',category='',pattern='(Rebar446|Review03).*([Ww]arning|[Ee]rror|degenerate|tangent)|([Ww]arning|[Ee]rror|degenerate|tangent).*(Rebar446|Review03)',maxEntries=100)
    (OUT/'evidence/unreal_verified.json').write_text(json.dumps(report,indent=2));print('NATIVE VERIFIED',flush=True)
if __name__=='__main__':
    init();stage=sys.argv[1]
    if stage=='before':views(True)
    elif stage=='round1_before':
        assert cameras();show('',True);state=p.state();move(state['actors']['R4_'+NAME],(1000000,0,0))
        label='R446_'+NAME;move(state['actors'][label],(0,0,600));state['r446_active']=label;p.store(state)
        capture('round1_before','150m',(0,0,600),15000)
        capture('round1_before','12m',(0,0,600),1200,-35)
    elif stage=='after':views(False)
    elif stage=='probe':
        assert cameras();show('',False);capture('probe','150m',(0,0,600),15000);capture('probe','12m',(0,0,600),1200,-35)
    elif stage=='master_probe':
        assert cameras();show('_MASTER',False);capture('master_probe','12m',(0,0,600),1200,-35)
    elif stage=='all_views':views(True);views(False)
    elif stage=='imports':imports()
    elif stage=='master_uv_import':
        path=mesh_path('_MASTER');v.call('StaticMeshTools','import_file',folder_path=path.rsplit('/',1)[0],asset_name=NAME+'_MASTER',source_file=str(OUT/'source'/(NAME+'_MASTER.fbx')),import_materials=False,import_textures=False,combine_meshes=True)
        v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(path),enabled=False)
        for slot in ['Brick','Roof','Trim','Rubble']:v.call('StaticMeshTools','set_material',mesh=v.ref(path),slot_name=slot,material=v.ref(CAND+'/Materials/MI_'+NAME+'_'+slot+'_MASTER'))
        actor=p.state()['actors']['R446R2_'+NAME+'_MASTER'];v.props(actor['component'],staticMesh=v.ref(path));v.call('AssetTools','save_assets',asset_paths=[path,v.LEVEL])
    elif stage=='verify':verify()
    elif stage=='finish':
        assert cameras();show('',False);capture('persisted_smoke','150m',(0,0,600),15000)
        v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])
        receipt={'level':v.call('SceneTools','get_current_level'),'dirty':v.call('AssetTools','is_dirty',asset_path=v.LEVEL),'pie':v.call('EditorAppToolset','IsPIERunning'),'candidate':CAND,'editor_left_running':True}
        assert not receipt['dirty'] and not receipt['pie'];(OUT/'evidence/final_editor_state.json').write_text(json.dumps(receipt,indent=2))
    elif stage in ['pilot','pilot_after']:
        v.call('SlateInspectorToolset.SlateInspectorToolset','Click',ref='sp4')
        v.call('EditorAppToolset','SelectActors',actors=[v.ref(p.state()['actors']['Village_ReviewCamera']['actor'])])
        v.call('SlateInspectorToolset.SlateInspectorToolset','PressKey',key='Ctrl+Shift+P')
        views(stage=='pilot')

