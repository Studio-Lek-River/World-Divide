"""Native MCP only; isolated Phase B preview and read-only production audit."""
import json,sys,base64,math,time
from pathlib import Path
from usa_hq_379_mcp import rpc
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
OUT=ROOT/'issue195/phase_b'; OUT.mkdir(parents=True,exist_ok=True)
CAND='/Game/Art/Candidates/USA_HQ/Issue195_PhaseB'
PROD='/Game/Art/USA/Buildings/USAHQ'
SETS={n:f'editor_toolset.toolsets.{s}.{n}' for n,s in [('AssetTools','asset'),('ActorTools','actor'),('SceneTools','scene'),('StaticMeshTools','static_mesh'),('ObjectTools','object'),('MaterialInstanceTools','material_instance')]}
SETS['EditorAppToolset']='EditorToolset.EditorAppToolset'
def ref(p):
    return {'refPath':p+'.'+p.rsplit('/',1)[-1] if p.startswith(('/Game/','/Engine/')) and '.' not in p else p}
def unpack(r):
    if r.get('isError'):raise RuntimeError(r)
    for b in r.get('content',[]):
        if b['type']=='text':return json.loads(b['text']).get('returnValue')
    raise RuntimeError(str(r)[:200])
def call(s,t,**args):
    v=unpack(rpc('tools/call',{'name':'call_tool','arguments':{'toolset_name':SETS.get(s,s),'tool_name':t,'arguments':args}}))
    if v is False and t not in ('exists','is_nanite_enabled','is_dirty'):raise RuntimeError((t,args,v))
    return v
def props(p,**kw):return call('ObjectTools','set_properties',instance=ref(p),values=json.dumps(kw))
def get(p,ks):return json.loads(call('ObjectTools','get_properties',instance=ref(p),properties=ks))
def refresh_transform(r):
    xf=call('ActorTools','get_actor_transform',actor=ref(r['actor']));moved=json.loads(json.dumps(xf));moved['location']['x']+=.01
    call('ActorTools','set_actor_transform',actor=ref(r['actor']),xform=moved)
    call('ActorTools','set_actor_transform',actor=ref(r['actor']),xform=xf)
def discover():
    listing=rpc('tools/call',{'name':'list_toolsets','arguments':{}})
    (OUT/'toolsets.json').write_text(json.dumps(listing,indent=2))
    for name in SETS:
        result=rpc('tools/call',{'name':'describe_toolset','arguments':{'toolset_name':SETS[name]}})
        (OUT/f'schema_{name}.json').write_text(json.dumps(result,indent=2))
    print('Live level:',call('SceneTools','get_current_level'),flush=True)
def audit():
    recipe=json.loads((ROOT/'usa_hq.json').read_text()); meshes={}
    for name in recipe['meshes']:
        p=f'{PROD}/Meshes/{name}'
        if not call('AssetTools','exists',path=p):
            p=recipe['meshes'][name].get('asset',p)
        n=call('StaticMeshTools','get_lod_count',mesh=ref(p))
        meshes[name]={'asset':p,'triangles':[call('StaticMeshTools','get_triangle_count',mesh=ref(p),lod_index=i if n>i else 0) for i in range(2)],'slots':call('StaticMeshTools','get_material_slots',mesh=ref(p)),'bounds':call('StaticMeshTools','get_bounds',mesh=ref(p))}
    report={'meshes':meshes,'drawn_triangles':[sum(meshes[p['mesh']]['triangles'][i] for p in recipe['parts']) for i in (0,1)],'unique_sections':sum(len(v['slots']) for v in meshes.values()),'parts':recipe['parts']}
    (OUT/'baseline_unreal.json').write_text(json.dumps(report,indent=2))
    print('BASELINE',report['drawn_triangles'],report['unique_sections'],flush=True)
LEVEL=CAND+'/L_HQ195_Fixtures'
STATE=OUT/'preview_state.json'
def component(actor,kind='StaticMeshComponent'):
    return next(c['refPath'] for c in call('ActorTools','get_components',actor=ref(actor)) if kind in c['refPath'])
def bind(path,slots):
    props(path,staticMaterials=[{'materialSlotName':s,'importedMaterialSlotName':s,'materialInterface':ref(CAND+'/Materials/MI_HQ195_BeaconAmber' if s=='HQ195_BeaconAmber' else PROD+'/Materials/'+(s if s.startswith('MI_') else 'MI_'+s))} for s in slots],naniteSettings={'bEnabled':False})
    call('AssetTools','save_assets',asset_paths=[path])
def setup():
    if STATE.exists() and 'StaticEquipment' in json.loads(STATE.read_text()).get('actors',{}):raise RuntimeError('Initial assembly already exists. Use instanced_setup, pose or shots; do not replay initialization.')
    assert call('SceneTools','get_current_level').startswith('/Game/Art/Candidates/USA_HQ/')
    if not call('AssetTools','exists',path=LEVEL):call('AssetTools','duplicate',path='/Game/Art/Candidates/USA_HQ/Candidate22/L_HQ379_Masts',new_path=LEVEL)
    call('AssetTools','save_assets',asset_paths=[LEVEL])
    if call('SceneTools','get_current_level')!=LEVEL:call('SceneTools','load_level',level_path=LEVEL)
    original=json.loads((ROOT/'issue379/preview_state.json').read_text())
    state={'actors':{}}
    for k,r in original['actors'].items():
        state['actors'][k]={key:val.replace('/Game/Art/Candidates/USA_HQ/Candidate22/L_HQ379_Masts.L_HQ379_Masts',LEVEL+'.L_HQ195_Fixtures') for key,val in r.items()}
    STATE.write_text(json.dumps(state,indent=2))
    amber=CAND+'/Materials/MI_HQ195_BeaconAmber'
    if not call('AssetTools','exists',path=amber):call('MaterialInstanceTools','create',folder_path=CAND+'/Materials',asset_name='MI_HQ195_BeaconAmber',parent=ref(PROD+'/Materials/M_HQ_Final_Operational'))
    call('MaterialInstanceTools','set_vector_parameter',instance=ref(amber),name='EmissionColor',value={'r':2.4,'g':1,'b':.1,'a':1})
    call('MaterialInstanceTools','set_vector_parameter',instance=ref(amber),name='SurfaceColor',value={'r':.32,'g':.095,'b':.007,'a':1})
    for n,v in [('GroupFixtures',0),('GroupWindows',0),('GroupAccents',0),('Roughness',.25),('Metallic',0)]:call('MaterialInstanceTools','set_scalar_parameter',instance=ref(amber),name=n,value=v)
    # Explicit serialized parent and overrides dirty/save the candidate instance.
    props(amber,parent=ref(PROD+'/Materials/M_HQ_Final_Operational'))
    call('AssetTools','save_assets',asset_paths=[amber])
    manifest=json.loads((OUT/'fixture_manifest.json').read_text())
    for r in manifest['batches']:
        p=CAND+'/Meshes/'+r['mesh']
        if not call('AssetTools','exists',path=p):call('StaticMeshTools','import_file',folder_path=CAND+'/Meshes',asset_name=r['mesh'],source_file=str(OUT/'fbx'/(r['mesh']+'.fbx')))
        bind(p,r['slots'])
        n=call('StaticMeshTools','get_triangle_count',mesh=ref(p),lod_index=0);assert n==r['triangles'],(n,r)
    for k in ('MastCore','MastEast'):
        call('SceneTools','remove_from_scene',actor=ref(state['actors'][k]['actor']));del state['actors'][k]
    actor=call('SceneTools','add_to_scene_from_asset',asset_path=CAND+'/Meshes/SM_HQ195_StaticEquipment',name='HQ195_StaticEquipment',xform={})['refPath']
    state['actors']['StaticEquipment']={'actor':actor,'component':component(actor),'mesh':CAND+'/Meshes/SM_HQ195_StaticEquipment'}
    STATE.write_text(json.dumps(state,indent=2))
    pose('game')
    call('AssetTools','save_assets',asset_paths=[LEVEL])
    print('Candidate saved',LEVEL,flush=True)
def pose(mode):
    assert call('SceneTools','get_current_level')==LEVEL
    state=json.loads(STATE.read_text());far=mode=='far'
    for k,r in state['actors'].items():
        if k=='PreviewGround':continue
        if k=='Shell':p=(state.get('master_reference','/Game/Art/Candidates/USA_HQ/Candidate22/Shells/SM_HQ379_ShellReview') if mode=='master' else PROD+'/Meshes/SM_HQ_Shell')
        elif k=='ShellDetail':p=PROD+'/Meshes/SM_HQ_ShellDetail'
        elif k=='StaticEquipment':p=CAND+'/Meshes/SM_HQ195_StaticEquipment'+('_LOD1' if far else '')
        elif k=='MastsFixed' or k.startswith(('Flood','GarageBeacon')):p=r['mesh']+('_LOD1' if far else '')
        else:p=r['mesh']
        props(r['component'],staticMesh=ref(p),forcedLodModel=2 if far and k not in ('StaticEquipment','MastsFixed') and not k.startswith(('Flood','GarageBeacon')) else 1)
        if k=='Shell':
            mats=[ref(PROD+'/Materials/'+s) for s in json.loads((ROOT/'game/game_meshes.json').read_text())['meshes']['SM_HQ_Shell']['slots']] if mode=='master' else []
            props(r['component'],overrideMaterials=mats)
        if not state.get('mode'):
            data=get(r['component'],['customPrimitiveData'])['customPrimitiveData']['data']
            if len(data)!=13:props(r['component'],customPrimitiveData={'data':[]})
            props(r['component'],customPrimitiveData={'data':[0,0,0,0,.025,.16,.8,1,1,1,1,1,1]})
        if k.startswith(('Flood','GarageBeacon')):refresh_transform(r)
    state['mode']=mode;STATE.write_text(json.dumps(state,indent=2))
def capture(name,distance,target=(0,0,400),pitch=-55,yaw=135):
    d=distance*100;rad=math.pi/180;direction=(math.cos(pitch*rad)*math.cos(yaw*rad),math.cos(pitch*rad)*math.sin(yaw*rad),math.sin(pitch*rad))
    xf={'location':dict(zip('xyz',[target[i]-d*direction[i] for i in range(3)])),'rotation':{'pitch':pitch,'yaw':yaw,'roll':0}}
    call('EditorAppToolset','SetCameraTransform',transform=xf);time.sleep(3)
    value=call('EditorAppToolset','CaptureViewport',captureTransform=xf,annotations={'gridSpacing':0,'gridExtent':0,'gridHeight':0,'maxLabelDistance':0,'classFilter':ref(''),'maxLabels':0},bShowUI=False)
    im=value.pop('image');out=ROOT/'renders/issue195/phase_b';out.mkdir(exist_ok=True)
    (out/(name+'_raw.png')).write_bytes(base64.b64decode(im['data']))
    value.update(arm_m=distance,target_cm=target,transform=xf,capture_type='Actual Unreal editor; manual LOD selection; not gameplay',mode=json.loads(STATE.read_text()).get('mode'))
    (out/(name+'.json')).write_text(json.dumps(value,indent=2));print('Captured',name,flush=True)
def shots():
    for mode in ('master','game','far'):
        pose(mode)
        ds=(12,150,450) if mode=='master' else ((12,150,249.6) if mode=='game' else (270.4,450))
        for d in ds:capture(f'{mode}_{str(d).replace(".","p")}m',d,(1200,-1050,780) if d==12 else (0,0,400))
    pose('game');capture('overview_day',85,(0,0,700),-35)
    capture('flood_east_close_day',1.4,(1195,-1045,802),-20,-35)
    capture('flood_west_close_day',1.4,(795,1280,750),-20,35)
    capture('beacon_south_close_day',1.1,(1568,-2438.4518,649.6),-15,160)
    capture('beacon_north_close_day',1.1,(1568,-1078.4518,649.6),-15,160)
    call('AssetTools','save_assets',asset_paths=[LEVEL])
def inspect_scene():
    print(call('SceneTools','find_actors',name='',tag='',collision_channels=[]),flush=True)
def night_setup():
    assert call('SceneTools','get_current_level')==LEVEL
    state=json.loads(STATE.read_text());manifest=json.loads((OUT/'fixture_manifest.json').read_text())
    base=LEVEL+'.L_HQ195_Fixtures:PersistentLevel.'
    sun=base+'DirectionalLight_0.LightComponent0';sky=base+'SkyLight_0.SkyLightComponent0'
    if 'day_lighting' not in state:state['day_lighting']={'sun':get(sun,['intensity','lightColor']),'sky':get(sky,['intensity','lightColor'])}
    state.setdefault('lights',{})
    for name,h in manifest['helpers'].items():
        if name in state['lights']:continue
        flood='Flood' in name;pos=h['location_cm'];d=h['beam_direction']
        rotation={'pitch':math.degrees(math.asin(d[2])),'yaw':math.degrees(math.atan2(d[1],d[0])),'roll':0} if flood else {'pitch':0,'yaw':0,'roll':0}
        actor=call('SceneTools','add_to_scene_from_class',actor_type=ref('/Script/Engine.SpotLight' if flood else '/Script/Engine.PointLight'),name='PreviewOnly_'+name,xform={'location':dict(zip('xyz',pos)),'rotation':rotation})['refPath']
        comp=component(actor,'LightComponent')
        props(comp,castShadows=False,intensity=10000 if flood else 25,attenuationRadius=2300 if flood else 180,lightColor={'r':1,'g':.9 if flood else .42,'b':.72 if flood else .04,'a':1},intensityUnits='Lumens',sourceRadius=5 if flood else 4)
        if flood:props(comp,innerConeAngle=9,outerConeAngle=16)
        state['lights'][name]={'actor':actor,'component':comp}
        STATE.write_text(json.dumps(state,indent=2))
    STATE.write_text(json.dumps(state,indent=2))
    set_lighting('night')
def set_lighting(mode):
    state=json.loads(STATE.read_text());base=LEVEL+'.L_HQ195_Fixtures:PersistentLevel.';night=mode=='night'
    sun=base+'DirectionalLight_0.LightComponent0';sky=base+'SkyLight_0.SkyLightComponent0'
    props(sun,**({'intensity':.4,'lightColor':{'r':.52,'g':.66,'b':1,'a':1}} if night else state['day_lighting']['sun']))
    props(sky,**({'intensity':.3} if night else state['day_lighting']['sky']))
    for comp in (sun,sky):
        props(comp,bVisible=False);props(comp,bVisible=True)
    for name,r in state['lights'].items():props(r['component'],intensity=(500 if 'Flood' in name else 8) if night else 0)
    if state.get('exposure_volume'):props(state['exposure_volume'],bEnabled=night,blendWeight=1 if night else 0)
    for k,r in state['actors'].items():
        if k=='PreviewGround':continue
        operational=1 if night or not k.startswith(('Flood','GarageBeacon')) else 0
        props(r['component'],customPrimitiveData={'data':[0,0,0,0,.025,.16,.8,1,operational,1,1,1,1 if night else 0]})
        if k.startswith(('Flood','GarageBeacon')):
            # Native nested CPD property writes persist but do not always update
            # the render proxy. Reassign the tiny mesh to refresh the preview.
            current=get(r['component'],['staticMesh'])['staticMesh']
            props(r['component'],staticMesh=ref('/Engine/BasicShapes/Cube'))
            props(r['component'],staticMesh=current)
            refresh_transform(r)
    state['lighting']=mode;STATE.write_text(json.dumps(state,indent=2))
    time.sleep(3)
def night_shots():
    set_lighting('night');time.sleep(2)
    capture('night_150m',150)
    capture('night_facade',30,(-130,0,570),-20,180)
    capture('flood_east_close_night',1.4,(1195,-1045,802),-20,-35)
    capture('flood_west_close_night',1.4,(795,1280,750),-20,35)
    capture('beacon_south_close_night',1.1,(1568,-2438.4518,649.6),-15,160)
    capture('beacon_north_close_night',1.1,(1568,-1078.4518,649.6),-15,160)
    capture('garage_night',18,(1560,-1758,420),-12,180)
    call('AssetTools','save_assets',asset_paths=[LEVEL])
def instanced_setup():
    assert call('SceneTools','get_current_level')==LEVEL
    state=json.loads(STATE.read_text());manifest=json.loads((OUT/'fixture_manifest.json').read_text());delivery=manifest['preferred_instanced_delivery']
    if state.get('instanced'):raise RuntimeError('Instanced setup already complete; use pose/shots.')
    for r in delivery['fixed_batches']+list(delivery['shared_parts'].values()):
        names=[r['mesh']] if r in delivery['fixed_batches'] else [r['mesh'],r['mesh']+'_LOD1']
        for name in names:
            path=CAND+'/Meshes/'+name
            if not call('AssetTools','exists',path=path):call('StaticMeshTools','import_file',folder_path=CAND+'/Meshes',asset_name=name,source_file=str(OUT/'fbx'/(name+'.fbx')))
            bind(path,r['slots'])
    # Use the existing dense reference read-only. Duplicating its derived LOD data
    # provoked a long rebuild; component material overrides are sufficient.
    state['master_reference']='/Game/Art/Candidates/USA_HQ/Candidate22/Shells/SM_HQ379_ShellReview'
    call('SceneTools','remove_from_scene',actor=ref(state['actors']['StaticEquipment']['actor']));del state['actors']['StaticEquipment']
    def add(label,mesh,location=(0,0,0),rotation=(0,0,0),shadow=False):
        path=CAND+'/Meshes/'+mesh
        actor=call('SceneTools','add_to_scene_from_asset',asset_path=path,name='HQ195_'+label,xform={'location':dict(zip('xyz',location)),'rotation':dict(zip(('pitch','yaw','roll'),rotation))})['refPath'];comp=component(actor)
        props(comp,castShadow=shadow,customPrimitiveData={'data':[0,0,0,0,.025,.16,.8,1,1,1,1,1,1]})
        state['actors'][label]={'actor':actor,'component':comp,'mesh':path}
    add('MastsFixed','SM_HQ195_MastsFixed',shadow=True)
    for inst in manifest['instances']:
        p=inst['location_m'];rot=(0,0,0)
        if inst['kind']=='flood':
            w,x,y,z=inst['quaternion_wxyz'];roll=math.atan2(2*(w*x+y*z),1-2*(x*x+y*y));pitch=math.asin(max(-1,min(1,2*(w*y-z*x))));yaw=math.atan2(2*(w*z+x*y),1-2*(y*y+z*z));rot=tuple(-math.degrees(v) for v in (pitch,yaw,roll))
        add(('Flood' if inst['kind']=='flood' else 'GarageBeacon')+inst['name'],delivery['shared_parts'][inst['kind']]['mesh'],(p[0]*100,-p[1]*100,p[2]*100),rot)
    # Drop inherited component overrides that referred to the old shell's slot indices.
    for k,r in state['actors'].items():
        if k!='PreviewGround':props(r['component'],overrideMaterials=[])
    state['instanced']=True;STATE.write_text(json.dumps(state,indent=2));pose('game');call('AssetTools','save_assets',asset_paths=[LEVEL])
    print('Instanced fixture candidate saved; 80 sections including both shared parts.',flush=True)
def final_audit():
    assert call('SceneTools','get_current_level')==LEVEL
    state=json.loads(STATE.read_text());manifest=json.loads((OUT/'fixture_manifest.json').read_text());baseline=json.loads((OUT/'baseline_unreal.json').read_text());results={}
    paths=[(r['mesh'],r['triangles'],r['slots']) for r in manifest['preferred_instanced_delivery']['fixed_batches']]
    for r in manifest['preferred_instanced_delivery']['shared_parts'].values():
        paths.extend((r['mesh']+suffix,r['triangles_per_instance'],r['slots']) for suffix in ('','_LOD1'))
    for name,n,slots in paths:
        path=state.get('asset_map',{}).get(name,CAND+'/Meshes/'+name);p=get(path,['staticMaterials','naniteSettings']);actual=call('StaticMeshTools','get_triangle_count',mesh=ref(path),lod_index=0)
        assert actual==n,(name,actual,n);assert [s['materialSlotName'] for s in p['staticMaterials']]==slots
        assert not p['naniteSettings']['bEnabled'],name
        assert all('WorldGridMaterial' not in s['materialInterface']['refPath'] for s in p['staticMaterials'])
        results[name]={'asset':path,'source_fbx':'issue195/phase_b/fbx/'+name+'.fbx','triangles':actual,'properties':p,'bounds_cm':call('StaticMeshTools','get_bounds',mesh=ref(path))}
    results['amber']=get(CAND+'/Materials/MI_HQ195_BeaconAmber',['parent','scalarParameterValues','vectorParameterValues'])
    results['fixture_actor_transforms']={k:call('ActorTools','get_actor_transform',actor=ref(r['actor'])) for k,r in state['actors'].items() if k.startswith(('Flood','GarageBeacon'))}
    results['helper_delivery']='Four exact SOCKET_ empties reimport-verified in FBX; native StaticMeshSocket serialization is not certified.'
    for name,xf in results['fixture_actor_transforms'].items():
        if not name.startswith('Flood'):continue
        rot=xf['rotation'];pitch=math.radians(rot['pitch']);yaw=math.radians(rot['yaw']);forward=(math.cos(pitch)*math.cos(yaw),math.cos(pitch)*math.sin(yaw),math.sin(pitch));expected=manifest['helpers']['SOCKET_Flood_'+name.removeprefix('Flood')]['beam_direction']
        assert sum(a*b for a,b in zip(forward,expected))>.99999,(name,forward,expected)
    results['fixture_components']={k:get(r['component'],['staticMesh','castShadow','customPrimitiveData','boundsScale']) for k,r in state['actors'].items() if k.startswith(('Flood','GarageBeacon'))}
    results['preview_lights']={k:get(r['component'],['intensity','intensityUnits','castShadows','attenuationRadius','lightColor']) for k,r in state['lights'].items()}
    results['whole_hq_triangles']=[baseline['drawn_triangles'][i]+400 for i in (0,1)]
    results['instanced_material_sections']=baseline['unique_sections']-6+3+2+2
    assert results['whole_hq_triangles'][0]<=350000 and results['whole_hq_triangles'][1]<=40000 and results['instanced_material_sections']<=80
    results['author_scope']='This art stage writes only its Art/Tools inputs and isolated candidate packages; concurrent repository changes are outside this receipt.'
    results['preview_is_draw_call_benchmark']=False
    results['dependency_policy']='Candidate meshes/amber material; read-only production shell, moving meshes, flag, red beacons and existing material parents/instances. Dense reference copy is preview only.'
    (OUT/'final_unreal_audit.json').write_text(json.dumps(results,indent=2));print('Final budget',results['whole_hq_triangles'],results['instanced_material_sections'],flush=True)
def final_review():
    set_lighting('day');shots();night_shots();set_lighting('day');pose('game');capture('final_reloaded_day',150)
    call('AssetTools','save_assets',asset_paths=[LEVEL])
    call('SceneTools','load_level',level_path=LEVEL)
    final_audit()

def finish_saved_preview():
    """Finish an interrupted capture set without repeating completed views."""
    state=json.loads(STATE.read_text())
    props(state['actors']['GarageBeaconNorth']['component'],boundsScale=2)
    capture('garage_night',18,(1560,-1758,420),-12,180)
    capture('night_150m',150)
    set_lighting('day');pose('game')
    call('AssetTools','save_assets',asset_paths=[LEVEL])
    call('SceneTools','load_level',level_path=LEVEL)
    capture('final_reloaded_day',150)
    final_audit()
def refresh_final():
    """Update only this task's changed mount export and instance/helper transforms."""
    state=json.loads(STATE.read_text());assert state.get('instanced')
    manifest=json.loads((OUT/'fixture_manifest.json').read_text())
    for r in manifest['preferred_instanced_delivery']['fixed_batches']:
        name='SM_HQ195_MastsFixed_Final'+('_LOD1' if r['mesh'].endswith('_LOD1') else '');path=CAND+'/Meshes/'+name
        if not call('AssetTools','exists',path=path):call('StaticMeshTools','import_file',folder_path=CAND+'/Meshes',asset_name=name,source_file=str(OUT/'fbx'/(r['mesh']+'.fbx')))
        bind(path,r['slots']);state.setdefault('asset_map',{})[r['mesh']]=path
    state['actors']['MastsFixed']['mesh']=CAND+'/Meshes/SM_HQ195_MastsFixed_Final'
    STATE.write_text(json.dumps(state,indent=2));pose('game');state=json.loads(STATE.read_text())
    for inst in manifest['instances']:
        if inst['kind']!='flood':continue
        w,x,y,z=inst['quaternion_wxyz'];roll=math.atan2(2*(w*x+y*z),1-2*(x*x+y*y));pitch=math.asin(2*(w*y-z*x));yaw=math.atan2(2*(w*z+x*y),1-2*(y*y+z*z));p=inst['location_m'];rot=dict(zip(('pitch','yaw','roll'),[-math.degrees(v) for v in (pitch,yaw,roll)]))
        call('ActorTools','set_actor_transform',actor=ref(state['actors']['Flood'+inst['name']]['actor']),xform={'location':{'x':p[0]*100,'y':-p[1]*100,'z':p[2]*100},'rotation':rot})
        h=manifest['helpers']['SOCKET_Flood_'+inst['name']];d=h['beam_direction'];r={'pitch':math.degrees(math.asin(d[2])),'yaw':math.degrees(math.atan2(d[1],d[0])),'roll':0}
        call('ActorTools','set_actor_transform',actor=ref(state['lights']['SOCKET_Flood_'+inst['name']]['actor']),xform={'location':dict(zip('xyz',h['location_cm'])),'rotation':r})
    if not state.get('exposure_volume'):
        state['exposure_volume']=call('SceneTools','add_to_scene_from_class',actor_type=ref('/Script/Engine.PostProcessVolume'),name='PreviewOnly_Exposure',xform={})['refPath']
        STATE.write_text(json.dumps(state,indent=2))
    props(state['exposure_volume'],bUnbound=True,settings={'bOverride_AutoExposureMethod':True,'autoExposureMethod':'AEM_Manual','bOverride_AutoExposureApplyPhysicalCameraExposure':True,'autoExposureApplyPhysicalCameraExposure':False,'bOverride_AutoExposureBias':True,'autoExposureBias':0,'bOverride_BloomIntensity':True,'bloomIntensity':.15})
    for r in state['lights'].values():
        for c in call('ActorTools','get_components',actor=ref(r['actor'])):
            if any(n in c['refPath'] for n in ('Billboard','Arrow','Sprite')):props(c['refPath'],bVisible=False)
    call('AssetTools','save_assets',asset_paths=[LEVEL]);print('Final transforms, beam origins and preview exposure updated.',flush=True)
if __name__=='__main__':
    import urllib.error
    for attempt in range(15):
        try:
            rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'HQ195PhaseB','version':'1'}});break
        except (urllib.error.URLError,ConnectionError):
            if attempt==14:raise
            time.sleep(2)
    globals()[sys.argv[1]](*sys.argv[2:])
