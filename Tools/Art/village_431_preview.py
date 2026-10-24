"""Isolated #431 preview using only advertised native Unreal MCP tools.
Import is safe; CLI stages mutate only /Game/Art/Candidates/Village/.
"""
import argparse
import base64
import importlib.util
import json
import math
import sys
import time
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('preview_rpc',ROOT/'Tools/Art/kit/strip_light_preview.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
OUT=ROOT/'Art/Shared/village/issue431/round1'
CAND='/Game/Art/Candidates/Village'
ASSETS=CAND+'/Round1/Revision02'
MESHES=CAND+'/Round1/Revision04'
LEVEL=CAND+'/L_VillagePreview'
p.OUT=OUT
call=p.call;ref=p.ref;props=p.props;get=p.get;xf=p.xform
NAMES=['SM_Village_HouseA','SM_Village_HouseA_Damaged','SM_Village_HouseA_Rubble']
SLOTS=['Brick','Roof','Trim','Glass','Wood','Rubble']
SURFACES={'Brick':'Bricks085','Roof':'RoofingTiles006','Wood':'Planks023A','Rubble':'Concrete036'}

def guard():assert call('SceneTools','get_current_level')==LEVEL

def prepare():
    assert not call('EditorAppToolset','IsPIERunning')
    if not call('AssetTools','exists',path=LEVEL):
        current=call('SceneTools','get_current_level')
        assert not call('AssetTools','is_dirty',asset_path=current),'Do not discard unsaved shared editor work'
        call('AssetTools','duplicate',path='/Engine/Maps/Templates/Template_Default',new_path=LEVEL)
        call('AssetTools','save_assets',asset_paths=[LEVEL])
    call('SceneTools','load_level',level_path=LEVEL);guard()
    print('Candidate level loaded',flush=True)
    for a in call('SceneTools','find_actors',name='',tag='',collision_channels=[]):
        print(a,flush=True)

def imports():
    guard()
    for f in sorted((OUT/'fbx').glob('*.fbx')):
        call('StaticMeshTools','import_file',folder_path=MESHES,asset_name=f.stem,source_file=str(f),import_materials=False,import_textures=False,combine_meshes=True)
        call('StaticMeshTools','set_nanite_enabled',mesh=ref(MESHES+'/'+f.stem),enabled=False)
        call('AssetTools','save_assets',asset_paths=[MESHES+'/'+f.stem]);print('Imported',f.stem,flush=True)

def materials():
    guard()
    for f in sorted((OUT/'textures').glob('*.png')):
        path=ASSETS+'/Textures/'+f.stem
        if not call('AssetTools','exists',path=path):call('TextureTools','import_file',folder_path=ASSETS+'/Textures',asset_name=f.stem,source_file=str(f))
        normal=f.stem.endswith('Normal');color=f.stem.endswith('BaseColor')
        props(path,sRGB=color,compressionSettings='TC_Normalmap' if normal else ('TC_Default' if color else 'TC_Masks'),maxTextureSize=2048)
        call('AssetTools','save_assets',asset_paths=[path]);print('Texture',f.stem,flush=True)
    # Current shared flat parent has no scanned albedo/ORM parameters. Extend a copy,
    # preserving the authoritative shared asset; trim/glass use shared parents directly.
    master=ASSETS+'/M_Village_Scanned'
    if not call('AssetTools','exists',path=master):
        call('AssetTools','duplicate',path='/Game/Art/Shared/Materials/Building/M_Bld_Surface',new_path=master)
        def node(kind,**values):
            e=call('MaterialTools','add_expression',material_or_function=ref(master),expression_class=ref('/Script/Engine.MaterialExpression'+kind))['refPath']
            props(e,**values);return e
        def output(e,prop,out=''):
            call('MaterialTools','connect_to_output',expression=ref(e),output_name=out,material_property='MP_'+prop)
        for param,sampler in [('BaseColor','Color'),('Normal','Normal'),('ORM','Masks')]:
            e=node('TextureSampleParameter2D',parameterName=param,texture=ref(ASSETS+'/Textures/Bricks085_'+param),samplerType='SAMPLERTYPE_'+sampler)
            if param=='BaseColor':output(e,'BaseColor','RGB')
            elif param=='Normal':output(e,'Normal','RGB')
            else:
                output(e,'AmbientOcclusion','R');output(e,'Roughness','G');output(e,'Metallic','B')
        call('MaterialTools','recompile',material_or_function=ref(master))
        call('AssetTools','save_assets',asset_paths=[master])
    for slot in SLOTS:
        path=ASSETS+'/MI_'+slot
        parent=master if slot in SURFACES else '/Game/Art/Shared/Materials/Building/M_Bld_'+('Glass' if slot=='Glass' else 'Surface')
        if not call('AssetTools','exists',path=path):call('MaterialInstanceTools','create',folder_path=ASSETS,asset_name='MI_'+slot,parent=ref(parent))
        if slot in SURFACES:
            for param in ['BaseColor','Normal','ORM']:call('MaterialInstanceTools','set_texture_parameter',instance=ref(path),name=param,value=ref(ASSETS+'/Textures/'+SURFACES[slot]+'_'+param))
        else:
            color=(.72,.74,.70,1) if slot=='Trim' else (.025,.04,.05,1)
            call('MaterialInstanceTools','set_vector_parameter',instance=ref(path),name='SurfaceColor',value=dict(zip('rgba',color)))
            call('MaterialInstanceTools','set_scalar_parameter',instance=ref(path),name='Roughness',value=.45 if slot=='Trim' else .18)
            if slot=='Glass':call('MaterialInstanceTools','set_scalar_parameter',instance=ref(path),name='GlassOpacity',value=.5)
        props(path,parent=ref(parent));call('AssetTools','save_assets',asset_paths=[path])
        print('Material',slot,flush=True)
    bind()

def bind():
    for name in NAMES:
        for suffix in ['', '_LOD1']:
            mesh=MESHES+'/'+name+suffix
            # Importers may strip unreferenced slots on rubble. Bind only advertised slots.
            for slot in call('StaticMeshTools','get_material_slots',mesh=ref(mesh)):
                material='MI_'+slot+('_Damaged' if name.endswith('_Damaged') and slot in ('Brick','Roof') else '')
                call('StaticMeshTools','set_material',mesh=ref(mesh),slot_name=slot,material=ref(ASSETS+'/'+material))
            call('AssetTools','save_assets',asset_paths=[mesh])

def scorch():
    guard();master=ASSETS+'/M_Village_Scanned'
    existing=call('MaterialTools','get_expressions',material_or_function=ref(master))
    def node(kind,**values):
        e=call('MaterialTools','add_expression',material_or_function=ref(master),expression_class=ref('/Script/Engine.MaterialExpression'+kind))['refPath']
        if values:props(e,**values)
        return e
    def link(a,b,pin,out=''):call('MaterialTools','connect_expressions',from_expression=ref(a),from_output_name=out,to_expression=ref(b),to_input_name=pin)
    base=None;done=False
    for e in existing:
        if 'TextureSampleParameter2D' in e['refPath']:
            if get(e['refPath'],'parameterName').get('parameterName')=='BaseColor':base=e['refPath']
        if 'ScalarParameter' in e['refPath']:
            if get(e['refPath'],'parameterName').get('parameterName')=='Scorch':done=True
    if not done:
        world=node('WorldPosition');actor=node('ActorPositionWS')
        offset=node('Constant3Vector',constant={'r':0,'g':-450,'b':420,'a':1})
        center=node('Add');link(actor,center,'A');link(offset,center,'B')
        mask=node('SphereMask',attenuationRadius=370,hardnessPercent=45);link(world,mask,'A');link(center,mask,'B')
        amount=node('ScalarParameter',parameterName='Scorch',defaultValue=0)
        weight=node('Multiply');link(mask,weight,'A');link(amount,weight,'B')
        tint=node('VectorParameter',parameterName='ScanTint',defaultValue={'r':1,'g':1,'b':1,'a':1})
        colored=node('Multiply');link(base,colored,'A','RGB');link(tint,colored,'B','RGB')
        black=node('Constant3Vector',constant={'r':.008,'g':.006,'b':.004,'a':1})
        blend=node('LinearInterpolate');link(colored,blend,'A');link(black,blend,'B');link(weight,blend,'Alpha')
        call('MaterialTools','connect_to_output',expression=ref(blend),output_name='',material_property='MP_BaseColor')
        call('MaterialTools','recompile',material_or_function=ref(master));call('AssetTools','save_assets',asset_paths=[master])
    call('MaterialInstanceTools','set_vector_parameter',instance=ref(ASSETS+'/MI_Wood'),name='ScanTint',value={'r':.14,'g':.12,'b':.10,'a':1})
    call('AssetTools','save_assets',asset_paths=[ASSETS+'/MI_Wood'])
    for slot in ['Brick','Roof']:
        path=ASSETS+'/MI_'+slot+'_Damaged'
        if not call('AssetTools','exists',path=path):call('MaterialInstanceTools','create',folder_path=ASSETS,asset_name='MI_'+slot+'_Damaged',parent=ref(ASSETS+'/MI_'+slot))
        call('MaterialInstanceTools','set_scalar_parameter',instance=ref(path),name='Scorch',value=1)
        props(path,parent=ref(ASSETS+'/MI_'+slot));call('AssetTools','save_assets',asset_paths=[path])
    print('Scorch material pass saved',flush=True)

def assembly():
    guard()
    # Existing template lighting is retained; all additions belong to the candidate.
    floor=p.add('Village_Ground',asset='/Engine/BasicShapes/Cube',loc=(0,0,-60),scale=(1200,1200,1))
    for i,name in enumerate(NAMES):
        y=(i-1)*2400
        p.add(name,asset=MESHES+'/'+name,loc=(0,y,0))
        props(p.state()['actors'][name]['component'],staticMesh=ref(MESHES+'/'+name))
        p.add('AbramsHull_'+str(i),asset='/Game/Art/Local/Meshes/SM_Abrams_Hull',loc=(100,y-1000,0))
        p.add('AbramsTurret_'+str(i),asset='/Game/Art/Local/Meshes/SM_Abrams_Turret',loc=(100,y-1000,180.681836605))
        call('ActorTools','set_actor_transform',actor=ref(p.state()['actors']['AbramsHull_'+str(i)]['actor']),xform=xf((100,y-1000,0),scale=(1.03599574,)*3),worldspace=True)
        call('ActorTools','set_actor_transform',actor=ref(p.state()['actors']['AbramsTurret_'+str(i)]['actor']),xform=xf((100,y-1000,180.681836605),scale=(1.03599574,)*3),worldspace=True)
    call('AssetTools','save_assets',asset_paths=[LEVEL]);print('Assembly saved',flush=True)

def presentation():
    guard()
    # Remove only the template's checker floor from this duplicated candidate.
    for a in call('SceneTools','find_actors',name='Floor',tag='',collision_channels=[]):
        if a['refPath'].endswith('Floor_0'):call('SceneTools','remove_from_scene',actor=a)
    path=ASSETS+'/MI_PreviewGround'
    if not call('AssetTools','exists',path=path):call('MaterialInstanceTools','create',folder_path=ASSETS,asset_name='MI_PreviewGround',parent=ref('/Game/Art/Shared/Materials/Building/M_Bld_Surface'))
    call('MaterialInstanceTools','set_vector_parameter',instance=ref(path),name='SurfaceColor',value={'r':.22,'g':.245,'b':.205,'a':1})
    call('MaterialInstanceTools','set_scalar_parameter',instance=ref(path),name='Roughness',value=.95)
    call('AssetTools','save_assets',asset_paths=[path])
    a=p.state()['actors']['Village_Ground'];props(a['component'],overrideMaterials=[ref(path)])
    call('ActorTools','set_actor_transform',actor=ref(a['actor']),xform=xf((0,0,-50),scale=(1200,1200,1)),worldspace=True)
    call('ActorTools','set_actor_transform',actor=ref(LEVEL+'.L_VillagePreview:PersistentLevel.DirectionalLight_0'),xform=xf((0,0,500),(-42,135,0)),worldspace=True)
    call('RTSConsoleToolset','ExecuteConsoleCommand',command='ShowFlag.BillboardSprites 0')
    call('AssetTools','save_assets',asset_paths=[LEVEL]);print('Presentation saved',flush=True)

def camera():
    guard()
    a=p.add('Village_ReviewCamera',cls='/Script/Engine.CameraActor',loc=(3000,-2000,1500),kind='CameraComponent')
    props(a['component'],fieldOfView=60,aspectRatio=16/9,bConstrainAspectRatio=True)
    call('EditorAppToolset','SelectActors',actors=[ref(a['actor'])])
    call('AssetTools','save_assets',asset_paths=[LEVEL]);print(a,flush=True)

def capture(name,target,distance,pitch,yaw=145):
    guard();r=math.radians;cp=math.cos(r(pitch));sp=math.sin(r(pitch))
    loc=(target[0]-distance*cp*math.cos(r(yaw)),target[1]-distance*cp*math.sin(r(yaw)),target[2]-distance*sp)
    pose=xf(loc,(pitch,yaw,0))
    if 'Village_ReviewCamera' in p.state()['actors']:
        call('ActorTools','set_actor_transform',actor=ref(p.state()['actors']['Village_ReviewCamera']['actor']),xform=pose,worldspace=True)
    call('EditorAppToolset','SetCameraTransform',transform=pose);time.sleep(1.5)
    result=p.unpack(p.rpc('tools/call',{'name':'call_tool','arguments':{'toolset_name':p.SETS['EditorAppToolset'],'tool_name':'CaptureViewport','arguments':{'captureTransform':pose,'annotations':{'gridSpacing':0,'gridExtent':0,'gridHeight':0,'maxLabelDistance':0,'classFilter':ref(''),'maxLabels':0},'bShowUI':False}}}))
    folder=OUT/'evidence'/args.phase;folder.mkdir(exist_ok=True)
    image=result.pop('image');(folder/(name+'.png')).write_bytes(base64.b64decode(image['data']))
    result.update({'target_cm':target,'arm_length_cm':distance,'label':'Unreal editor preview; not gameplay'})
    (folder/(name+'.json')).write_text(json.dumps(result,indent=2));print('Captured',name,'FOV',result.get('cameraFOV'),flush=True)

def shots():
    guard()
    for i,state in enumerate(['pristine','damaged','rubble']):
        target=(0,(i-1)*2400,0)
        capture(state+'_12m',target,1200,-35)
        capture(state+'_150m',target,15000,-55)
        capture(state+'_450m',target,45000,-55)
        capture(state+'_threequarter',(0,(i-1)*2400-200,400),2900,-24)

def verify():
    guard();call('AssetTools','save_assets',asset_paths=[LEVEL]);call('SceneTools','load_level',level_path=LEVEL)
    report={'level':call('SceneTools','get_current_level'),'meshes':{},'materials':{}}
    for name in NAMES:
        for suffix in ['','_LOD1']:
            path=MESHES+'/'+name+suffix
            report['meshes'][name+suffix]={'triangles':call('StaticMeshTools','get_triangle_count',mesh=ref(path),lod_index=0),'bounds':call('StaticMeshTools','get_bounds',mesh=ref(path)),'slots':call('StaticMeshTools','get_material_slots',mesh=ref(path)),'bindings':get(path,'staticMaterials'),'nanite':call('StaticMeshTools','is_nanite_enabled',mesh=ref(path))}
    for slot in SLOTS+['Brick_Damaged','Roof_Damaged']:report['materials'][slot]=get(ASSETS+'/MI_'+slot,'parent','textureParameterValues','vectorParameterValues','scalarParameterValues')
    report['dependencies']={path:call('AssetTools','get_dependencies',asset_path=path) for path in [LEVEL,MESHES+'/SM_Village_HouseA_Damaged',ASSETS+'/MI_Brick_Damaged',ASSETS+'/M_Village_Scanned']}
    report['camera']=get(p.state()['actors']['Village_ReviewCamera']['component'],'fieldOfView','aspectRatio','bConstrainAspectRatio')
    report['scope']='Native editor candidate; save/reload in same running editor. No restart or production/runtime acceptance.'
    report['collision']={}
    for name in NAMES:
        body=get(MESHES+'/'+name,'bodySetup')['bodySetup']['refPath']
        data=get(body,'aggGeom','collisionTraceFlag')
        report['collision'][name]={'convex_hulls':len(data['aggGeom']['convexElems']),'trace_flag':data['collisionTraceFlag']}
    report['texture_settings']={}
    for f in sorted((OUT/'textures').glob('*.png')):
        report['texture_settings'][f.name]=get(ASSETS+'/Textures/'+f.stem,'sRGB','compressionSettings','maxTextureSize','neverStream','mipGenSettings')
    report['log_search']=call('LogsToolset','GetLogEntries',category='',pattern='(HouseA|Village).*([Ww]arning|[Ee]rror|degenerate|tangent)|([Ww]arning|[Ee]rror|degenerate|tangent).*(HouseA|Village)',maxEntries=60)
    (OUT/'evidence/unreal_verified.json').write_text(json.dumps(report,indent=2))
    print('Saved verification:',len(report['meshes']),'meshes,',len(report['texture_settings']),'textures; level',report['level'],flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('stage');ap.add_argument('--phase',default='baseline');args=ap.parse_args()
    (OUT/'evidence').mkdir(parents=True,exist_ok=True)
    p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'village431','version':'1'}})
    {'discover':p.discover,'prepare':prepare,'imports':imports,'materials':materials,'scorch':scorch,'bind':bind,'assembly':assembly,'presentation':presentation,'camera':camera,'shots':shots,'verify':verify}[args.stage]()
