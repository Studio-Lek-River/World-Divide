"""Native Unreal MCP candidate preview. No runtime/gameplay implementation.
Discover schemas before invoking setup; receipts live only in this piece's Art folder.
"""
import json
import sys
import urllib.request
import math
import base64
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'Art/Shared/kit/lighting/04_StripLight'
CAND='/Game/Art/Candidates/Kit_StripLight/Candidate01'
HEADERS={'Content-Type':'application/json','Accept':'application/json, text/event-stream'}
SETS={n:f'editor_toolset.toolsets.{s}.{n}' for n,s in [
    ('AssetTools','asset'),('ActorTools','actor'),('SceneTools','scene'),
    ('StaticMeshTools','static_mesh'),('ObjectTools','object'),('MaterialTools','material'),
    ('TextureTools','texture'),('MaterialInstanceTools','material_instance')]}
SETS['EditorAppToolset']='EditorToolset.EditorAppToolset'
SETS['RTSConsoleToolset']='WorldDivideMCP.RTSConsoleToolset'
SETS['LogsToolset']='EditorToolset.LogsToolset'

def rpc(method,params):
    data=json.dumps({'jsonrpc':'2.0','id':2,'method':method,'params':params}).encode()
    req=urllib.request.Request('http://127.0.0.1:8000/mcp',data,HEADERS)
    with urllib.request.urlopen(req,timeout=120) as response:
        if response.headers.get('Mcp-Session-Id'):HEADERS['Mcp-Session-Id']=response.headers['Mcp-Session-Id']
        result=json.load(response)
    if 'error' in result:raise RuntimeError(result)
    return result['result']

def unpack(result):
    if result.get('isError'):raise RuntimeError(result)
    for b in result.get('content',[]):
        if b['type']=='text':
            r=json.loads(b['text'])
            if 'returnValue' in r:return r['returnValue']
            return r
    raise RuntimeError(result)

def call(s,t,**args):
    r=rpc('tools/call',{'name':'call_tool','arguments':{
        'toolset_name':SETS.get(s,s),'tool_name':t,'arguments':args}})
    path=OUT/'evidence/ue_calls.jsonl'
    with path.open('a',encoding='utf8') as f:f.write(json.dumps({'set':s,'tool':t,'arguments':args,'result':r})+'\n')
    value=unpack(r)
    if value is False and t not in ('exists','is_dirty','is_nanite_enabled','IsPIERunning'):raise RuntimeError((t,args,value))
    return value

def discover():
    r=rpc('tools/call',{'name':'list_toolsets','arguments':{}})
    (OUT/'evidence/toolsets.json').write_text(json.dumps(r,indent=2))
    for name,ident in SETS.items():
        r=rpc('tools/call',{'name':'describe_toolset','arguments':{'toolset_name':ident}})
        (OUT/f'evidence/schema_{name}.json').write_text(json.dumps(r,indent=2))
    print('LIVE LEVEL',call('SceneTools','get_current_level'),flush=True)

def ref(p):
    return {'refPath':p+'.'+p.rsplit('/',1)[-1] if p.startswith(('/Game/','/Engine/')) and '.' not in p else p}

def props(p,**kw):return call('ObjectTools','set_properties',instance=ref(p),values=json.dumps(kw))

def get(p,*keys):return json.loads(call('ObjectTools','get_properties',instance=ref(p),properties=list(keys)))

def xform(loc=(0,0,0),rot=(0,0,0),scale=(1,1,1)):
    return {'location':dict(zip('xyz',loc)),'rotation':dict(zip(('pitch','yaw','roll'),rot)),
            'scale':dict(zip('xyz',scale))}

def state():return json.loads((OUT/'evidence/preview_state.json').read_text()) if (OUT/'evidence/preview_state.json').exists() else {'actors':{},'materials':{}}

def store(s):(OUT/'evidence/preview_state.json').write_text(json.dumps(s,indent=2))

def add(name,asset=None,cls=None,loc=(0,0,0),rot=(0,0,0),scale=(1,1,1),kind='StaticMeshComponent'):
    s=state()
    if name in s['actors']:return s['actors'][name]
    if asset:
        a=call('SceneTools','add_to_scene_from_asset',asset_path=asset,name=name,xform=xform(loc,rot,scale))['refPath']
    else:
        a=call('SceneTools','add_to_scene_from_class',actor_type=ref(cls),name=name,xform=xform(loc,rot,scale))['refPath']
    cs=call('ActorTools','get_components',actor=ref(a))
    comp=next((c['refPath'] for c in cs if kind in c['refPath']),None)
    s['actors'][name]={'actor':a,'component':comp};store(s)
    return s['actors'][name]

def prepare():
    assert not call('EditorAppToolset','IsPIERunning')
    level=CAND+'/L_StripLight'
    if not call('AssetTools','exists',path=level):
        current=call('SceneTools','get_current_level')
        assert not call('AssetTools','is_dirty',asset_path=current),'Preserve unsaved editor work'
        call('AssetTools','duplicate',path='/Engine/Maps/Templates/Template_Default',new_path=level)
        call('AssetTools','save_assets',asset_paths=[level])
        call('SceneTools','load_level',level_path=level)
        assert call('SceneTools','get_current_level')==level
        for a in call('SceneTools','find_actors',name='',tag='',collision_channels=[]):
            if any(x in a['refPath'] for x in ('WorldSettings','Brush','BuilderBrush')):continue
            call('SceneTools','remove_from_scene',actor=a)
        call('AssetTools','save_assets',asset_paths=[level])
    else:call('SceneTools','load_level',level_path=level)
    for suffix in ('','_LOD1'):
        p=CAND+'/SM_Kit_StripLight'+suffix
        if not call('AssetTools','exists',path=p):
            call('StaticMeshTools','import_file',folder_path=CAND,asset_name='SM_Kit_StripLight'+suffix,
                 source_file=str(OUT/('SM_Kit_StripLight'+suffix+'.fbx')))
        assert call('StaticMeshTools','get_triangle_count',mesh=ref(p),lod_index=0)==300
        assert call('StaticMeshTools','get_material_slots',mesh=ref(p))==['Housing','Lens']
        props(p,naniteSettings={'bEnabled':False})
        call('StaticMeshTools','remove_collisions',mesh=ref(p))
        call('AssetTools','save_assets',asset_paths=[p])
    print('Candidate mesh bounds',call('StaticMeshTools','get_bounds',mesh=ref(CAND+'/SM_Kit_StripLight')))
    for name in ('M_StripLight_Housing','M_StripLight_Lens','M_PreviewConcrete'):
        if not call('AssetTools','exists',path=CAND+'/'+name):
            call('MaterialTools','create_material',folder_path=CAND,asset_name=name)
    print('Expression classes',call('MaterialTools','list_expression_classes',material_or_function=ref(CAND+'/M_StripLight_Housing'),search='Constant'))
    print('Texture classes',call('MaterialTools','list_expression_classes',material_or_function=ref(CAND+'/M_StripLight_Housing'),search='Texture'))

def material_pass():
    for p in (OUT/'textures').glob('*.png'):
        target=CAND+'/Textures/'+p.stem
        if not call('AssetTools','exists',path=target):
            call('TextureTools','import_file',folder_path=CAND+'/Textures',asset_name=p.stem,source_file=str(p))
        normal=p.stem.endswith('_Normal');color=p.stem.endswith('_BaseColor')
        props(target,sRGB=color,compressionSettings='TC_Normalmap' if normal else ('TC_Default' if color else 'TC_Masks'))
        call('AssetTools','save_assets',asset_paths=[target])
    for surface in ('Housing','Lens','Concrete'):
        path=CAND+('/M_PreviewConcrete' if surface=='Concrete' else '/M_StripLight_'+surface)
        for e in call('MaterialTools','get_expressions',material_or_function=ref(path)):
            call('MaterialTools','delete_expression',material_or_function=ref(path),expression=e)
        def node(kind,**values):
            e=call('MaterialTools','add_expression',material_or_function=ref(path),expression_class=ref('/Script/Engine.MaterialExpression'+kind))['refPath']
            if values:props(e,**values)
            return e
        def link(a,b,pin,out=''):
            call('MaterialTools','connect_expressions',from_expression=ref(a),from_output_name=out,to_expression=ref(b),to_input_name=pin)
        def output(a,pin,out=''):
            call('MaterialTools','connect_to_output',expression=ref(a),output_name=out,material_property='MP_'+pin)
        def scalar(v):return node('Constant',r=v)
        def vector(v):return node('Constant3Vector',constant=dict(zip('rgba',(*v,1))))
        if surface=='Concrete':
            output(vector((.22,.24,.25)),'BaseColor');output(scalar(.9),'Roughness')
        else:
            def tex(suffix,scale=1,coords=None):
                e=node('TextureSample',texture=ref(CAND+'/Textures/T_StripLight_'+surface+'_'+suffix),samplerType='SAMPLERTYPE_Normal' if suffix=='Normal' else ('SAMPLERTYPE_Color' if suffix=='BaseColor' else 'SAMPLERTYPE_Masks'))
                uv=coords or node('TextureCoordinate',uTiling=scale,vTiling=scale)
                link(uv,e,'UVs');return e
            output(tex('BaseColor'),'BaseColor')
            orm=tex('ORM',4 if surface=='Housing' else 10)
            output(orm,'Roughness','G');output(orm,'Metallic','B');output(orm,'AmbientOcclusion','R')
            output(tex('Normal',8 if surface=='Housing' else 10),'Normal')
            output(scalar(.5),'Specular')
            if surface=='Lens':
                props(path,shadingModel='MSM_Subsurface')
                output(vector((.73,.755,.77)),'SubsurfaceColor');output(scalar(.65),'Opacity')
                uv=node('TextureCoordinate',uTiling=1/1.106,vTiling=1/.146)
                offset=node('Constant2Vector',r=.5-.5/1.106,g=.5-.5/.146)
                addn=node('Add');link(uv,addn,'A');link(offset,addn,'B')
                prof=tex('LightProfile',coords=addn)
                tint=vector((5,3.895,3.075));mul=node('Multiply');link(prof,mul,'A');link(tint,mul,'B')
                level=node('ScalarParameter',parameterName='LampLevel',defaultValue=1)
                emit=node('Multiply');link(mul,emit,'A');link(level,emit,'B');output(emit,'EmissiveColor')
        call('MaterialTools','recompile',material_or_function=ref(path))
        call('AssetTools','save_assets',asset_paths=[path])
        print('Compiled',surface,flush=True)
    for on in (True,False):
        name='MI_StripLight_Lens_'+('On' if on else 'Off');path=CAND+'/'+name
        if not call('AssetTools','exists',path=path):
            call('MaterialInstanceTools','create',folder_path=CAND,asset_name=name,parent=ref(CAND+'/M_StripLight_Lens'))
        call('MaterialInstanceTools','set_scalar_parameter',instance=ref(path),name='LampLevel',value=int(on))
        props(path,parent=ref(CAND+'/M_StripLight_Lens'))
        call('AssetTools','save_assets',asset_paths=[path])
    for suffix in ('','_LOD1'):
        mesh=CAND+'/SM_Kit_StripLight'+suffix
        for slot,mat in [('Housing','M_StripLight_Housing'),('Lens','MI_StripLight_Lens_On')]:
            call('StaticMeshTools','set_material',mesh=ref(mesh),slot_name=slot,material=ref(CAND+'/'+mat))
        call('AssetTools','save_assets',asset_paths=[mesh])

def assembly():
    assert call('SceneTools','get_current_level')==CAND+'/L_StripLight'
    lamp=add('StripLight',asset=CAND+'/SM_Kit_StripLight',loc=(0,0,270),rot=(-90,0,0))
    props(lamp['component'],castShadow=True)
    for name,loc,scale in [('Ceiling',(0,0,276),(2.7,3,.12)),('Wall',(-63,0,134),(.12,3,2.7)),('Floor',(0,0,-7),(2.7,3,.12))]:
        a=add(name,asset='/Engine/BasicShapes/Cube',loc=loc,scale=scale)
        props(a['component'],overrideMaterials=[ref(CAND+'/M_PreviewConcrete')])
    a=add('LampLight',cls='/Script/Engine.RectLight',loc=(0,0,259.3),rot=(-90,0,0),kind='LightComponent')
    props(a['component'],intensityUnits='Candelas',intensity=180,temperature=3500,bUseTemperature=True,
          attenuationRadius=600,sourceWidth=108,sourceHeight=13,castShadows=False,indirectLightingIntensity=0)
    a=add('PreviewKey',cls='/Script/Engine.RectLight',loc=(150,-140,230),rot=(8,137,0),kind='LightComponent')
    props(a['component'],intensityUnits='Candelas',intensity=120,sourceWidth=160,sourceHeight=160,attenuationRadius=1000,castShadows=True)
    a=add('PreviewFill',cls='/Script/Engine.RectLight',loc=(40,180,180),rot=(23,-103,0),kind='LightComponent')
    props(a['component'],intensityUnits='Candelas',intensity=40,sourceWidth=150,sourceHeight=150,attenuationRadius=1000,castShadows=True)
    a=add('PreviewExposure',cls='/Script/Engine.PostProcessVolume',kind='none')
    props(a['actor'],bUnbound=True,settings={'bOverride_AutoExposureMethod':True,'autoExposureMethod':'AEM_Manual',
        'bOverride_AutoExposureApplyPhysicalCameraExposure':True,'autoExposureApplyPhysicalCameraExposure':False,
        'bOverride_AutoExposureBias':True,'autoExposureBias':0,
        'bOverride_BloomIntensity':True,'bloomIntensity':.15,
        'bOverride_AmbientOcclusionIntensity':True,'ambientOcclusionIntensity':.7,
        'bOverride_AmbientOcclusionRadius':True,'ambientOcclusionRadius':12})
    call('AssetTools','save_assets',asset_paths=[CAND+'/L_StripLight'])
    print('Preview assembly saved',flush=True)

def capture(name,on=True,night=False,distance=None):
    import time
    s=state()['actors']
    props(s['StripLight']['component'],overrideMaterials=[ref(CAND+'/M_StripLight_Housing'),ref(CAND+'/MI_StripLight_Lens_'+('On' if on else 'Off'))])
    props(s['LampLight']['component'],intensity=180 if on else 0)
    props(s['PreviewKey']['component'],intensity=8 if night else 120)
    props(s['PreviewFill']['component'],intensity=4 if night else 40)
    loc=(95,95,205)
    if distance:
        loc=(distance*.65,-distance*.65,259+distance*.394)
    dx,dy,dz=(-loc[0],-loc[1],259-loc[2])
    rot=(math.degrees(math.atan2(dz,math.hypot(dx,dy))),math.degrees(math.atan2(dy,dx)),0)
    call('EditorAppToolset','SetCameraTransform',transform=xform(loc,rot))
    time.sleep(2)
    r=unpack(rpc('tools/call',{'name':'call_tool','arguments':{'toolset_name':SETS['EditorAppToolset'],'tool_name':'CaptureViewport','arguments':{'captureTransform':xform(loc,rot),'annotations':{'gridSpacing':0,'gridExtent':0,'gridHeight':0,'maxLabelDistance':0,'classFilter':ref(''),'maxLabels':0},'bShowUI':False}}}))
    (OUT/'review'/f'{name}.png').write_bytes(base64.b64decode(r.pop('image')['data']))
    (OUT/'evidence'/f'{name}.json').write_text(json.dumps(r,indent=2))
    print('Captured',name,flush=True)

def shots():
    s=state()['actors']
    props(s['PreviewExposure']['actor'],settings={'bOverride_AutoExposureMethod':True,'autoExposureMethod':'AEM_Manual','bOverride_AutoExposureApplyPhysicalCameraExposure':True,'autoExposureApplyPhysicalCameraExposure':False,'bOverride_AutoExposureBias':True,'autoExposureBias':-5,'bOverride_BloomIntensity':True,'bloomIntensity':.12,'bOverride_AmbientOcclusionIntensity':True,'ambientOcclusionIntensity':.7,'bOverride_AmbientOcclusionRadius':True,'ambientOcclusionRadius':12})
    call('RTSConsoleToolset','ExecuteConsoleCommand',command='ShowFlag.BillboardSprites 0')
    for name,on,night in [('ue_day_on',True,False),('ue_day_off',False,False),('ue_night_on',True,True),('ue_night_off',False,True)]:capture(name,on,night)
    capture('ue_review',True,False)
    call('AssetTools','save_assets',asset_paths=[CAND+'/L_StripLight'])

def verify():
    call('SceneTools','load_level',level_path=CAND+'/L_StripLight')
    result={'level':call('SceneTools','get_current_level'),'meshes':{},'instances':{}}
    for suffix in ('','_LOD1'):
        p=CAND+'/SM_Kit_StripLight'+suffix
        n=call('StaticMeshTools','get_triangle_count',mesh=ref(p),lod_index=0)
        slots=call('StaticMeshTools','get_material_slots',mesh=ref(p))
        assert n==300 and slots==['Housing','Lens']
        result['meshes'][p]={'triangles':n,'slots':slots,'bindings':get(p,'staticMaterials')}
    for state_name in ('On','Off'):
        p=CAND+'/MI_StripLight_Lens_'+state_name
        result['instances'][state_name]=get(p,'parent','scalarParameterValues')
    s=state()['actors']
    result['fixture_light']=get(s['LampLight']['component'],'intensity','intensityUnits','temperature','sourceWidth','sourceHeight','attenuationRadius','castShadows')
    result['scope']='Art candidate; no production or gameplay integration; level reloaded in same editor process'
    (OUT/'evidence/unreal_verified.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)

if __name__=='__main__':
    rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'strip04','version':'1'}})
    if len(sys.argv)<2 or sys.argv[1]=='discover':discover()
    elif sys.argv[1]=='prepare':prepare()
    elif sys.argv[1]=='materials':material_pass()
    elif sys.argv[1]=='assembly':assembly()
    elif sys.argv[1]=='shots':shots()
    elif sys.argv[1]=='verify':verify()
    elif sys.argv[1]=='query':
        print('ENGINE WORLDS',call('AssetTools','find_assets',folder_path='/Engine/Maps',name='',asset_type=ref('/Script/Engine.World')))
        print('CANDIDATE WORLDS',call('AssetTools','find_assets',folder_path='/Game/Art/Candidates',name='',asset_type=ref('/Script/Engine.World')))
        print('PIE',call('EditorAppToolset','IsPIERunning'))
