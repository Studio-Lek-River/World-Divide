"""Round4 isolated native Unreal MCP driver; existing Village preview only."""
import argparse,json,sys,shutil
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_preview as v
import village_431_round4_common as c
OUT=v.ROOT/'Art/Shared/village/issue431/round4'
CAND='/Game/Art/Candidates/Village/Final'
for folder in ['source','fbx','textures','evidence']:(OUT/folder).mkdir(parents=True,exist_ok=True)
v.OUT=OUT;v.p.OUT=OUT;v.ASSETS=CAND;v.MESHES=CAND+'/First'

def initialize():v.p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'village431round4','version':'1'}})

def inventory():
    v.p.discover()
    r={'current_level':v.call('SceneTools','get_current_level'),'round2':{}}
    for p in ['/Game/Art/Candidates/Village/L_VillagePreview','/Game/Art/Candidates/Village/Round2/Final02/SM_Village_HouseA','/Game/Art/Candidates/Village/Round2/Masters02/Final/SM_Village_HouseA_MASTER','/Game/Art/Candidates/Village/Round2/M_Round2_Surface']:
        r['round2'][p]=v.call('AssetTools','exists',path=p)
    assert all(r['round2'].values())
    (OUT/'evidence/restart_inventory.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2),flush=True)

def prepare():
    current=v.call('SceneTools','get_current_level')
    if current!=v.LEVEL:
        assert not v.call('AssetTools','is_dirty',asset_path=current)
        v.call('SceneTools','load_level',level_path=v.LEVEL)
    v.guard();v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])
    if not (OUT/'evidence/preview_state.json').exists():
        old=json.loads((OUT.parent/'round3/evidence/preview_state.json').read_text())
        # Existing actors remain untouched except parking historical geometry.
        v.p.store(old)
    for label,a in v.p.state()['actors'].items():
        if label.startswith(('SM_Village_','Master_SM_','LOD1_SM_','R3_')):move(a,(2000000,0,0))
    v.call('RTSConsoleToolset','ExecuteConsoleCommand',command='ShowFlag.BillboardSprites 0')

def move(a,loc):v.call('ActorTools','set_actor_transform',actor=v.ref(a['actor']),xform=v.xf(loc),worldspace=True)

def textures(final=False):
    from PIL import Image
    blank=OUT/'textures/Round4_White.png'
    if not blank.exists():Image.new('RGB',(8,8),(255,255,255)).save(blank)
    files=sorted((OUT/'textures').glob('*.png'));paths=[]
    for f in files:
        if f.stem.startswith('SM_') and not final:continue
        path=(c.atlas_root(f.stem) if f.stem.startswith('SM_') else CAND+'/Textures')+'/'+f.stem
        if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=path.rsplit('/',1)[0],asset_name=f.stem,source_file=str(f))
        color=f.stem.endswith('BaseColor');normal=f.stem.endswith('_Normal')
        v.props(path,sRGB=color,compressionSettings='TC_Normalmap' if normal else ('TC_Default' if color else 'TC_Masks'),maxTextureSize=2048)
        paths.append(path)
    v.call('AssetTools','save_assets',asset_paths=paths)
    print('Textures installed',len(files),flush=True)

def materials(final=False):
    assert not v.call('EditorAppToolset','GetOpenAssets'),'Close asset editors before candidate shader work'
    parent=CAND+'/M_SetSurface'
    if not v.call('AssetTools','exists',path=parent):v.call('AssetTools','duplicate',path='/Game/Art/Candidates/Village/Round3/M_SetSurface',new_path=parent)
    templates={s:v.get('/Game/Art/Candidates/Village/Round2/Materials/MI_Pristine_'+s,'textureParameterValues','vectorParameterValues','scalarParameterValues') for s in c.SLOTS}
    paths=[parent]
    for n in c.NAMES:
        key=c.type_for(n);wall,brick,roof=c.CONFIG[key];damaged=n.endswith('Damaged');rubble=n.endswith('Rubble')
        for slot in c.SLOTS:
            path=CAND+'/Materials/MI_'+n+'_'+slot
            instance_parent='/Game/Art/Shared/Materials/Building/M_Bld_Glass' if slot=='Glass' else parent
            if not v.call('AssetTools','exists',path=path):v.call('MaterialInstanceTools','create',folder_path=CAND+'/Materials',asset_name=path.rsplit('/',1)[1],parent=v.ref(instance_parent))
            data=json.loads(json.dumps(templates[slot]));data['parent']=v.ref(instance_parent)
            if slot!='Glass':
                facade='Plaster006' if damaged and wall=='Plaster001' else wall
                surface={'Brick':facade,'Roof':'RoofingTiles013A','Wood':'Planks037A' if key=='Barn' else 'Planks023A','Rubble':'Concrete036' if damaged or rubble else 'Concrete031','Trim':None}[slot]
                for param in data['textureParameterValues']:
                    pname=param['parameterInfo']['name']
                    if pname in ['AtlasTint','BakeNormal','BakeAO']:
                        end={'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[pname]
                        param['parameterValue']=v.ref(c.atlas_root(n)+'/'+n+'_'+end if final else CAND+'/Textures/Round4_White')
                    elif surface:param['parameterValue']=v.ref(CAND+'/Textures/'+surface+'_'+pname)
                for param in data['scalarParameterValues']:
                    pname=param['parameterInfo']['name']
                    if pname=='BakeStrength':param['parameterValue']=(.08 if slot=='Rubble' else .35) if final else 0
                    if pname=='UseScan':param['parameterValue']=int(surface is not None and not(key=='Bridge' and slot=='Roof'))
                tint={'Brick':brick,'Roof':roof,'Wood':c.WOOD_TINT[key],'Rubble':(.57,.55,.50),'Trim':(.10,.075,.055) if key=='Bridge' else (.67,.70,.72)}[slot]
                if rubble and slot=='Brick':tint=tuple(x*.82 for x in brick)
                for param in data['vectorParameterValues']:
                    if param['parameterInfo']['name']=='SurfaceTint':param['parameterValue']=dict(zip('rgba',(*tint,1)))
            v.props(path,**data);paths.append(path)
        v.call('AssetTools','save_assets',asset_paths=paths);paths=[]
        print('Materials',n,'final' if final else 'first',flush=True)

def imports(final=False):
    v.guard();folder=CAND+('/Meshes' if final else '/First')
    qa=json.loads((OUT/'evidence/geometry_qa.json').read_text())
    for n in c.NAMES:
        for suffix in ['', '_LOD1']:
            path=folder+'/'+n+suffix
            if not v.call('AssetTools','exists',path=path):v.call('StaticMeshTools','import_file',folder_path=folder,asset_name=n+suffix,source_file=str(OUT/'fbx'/(n+suffix+'.fbx')),import_materials=False,import_textures=False,combine_meshes=True)
            v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(path),enabled=False)
            assert v.call('StaticMeshTools','get_triangle_count',mesh=v.ref(path),lod_index=0)==qa[n+suffix]['triangles']
            bounds=v.call('StaticMeshTools','get_bounds',mesh=v.ref(path));assert bounds['max']['x']>400
            for slot in v.call('StaticMeshTools','get_material_slots',mesh=v.ref(path)):v.call('StaticMeshTools','set_material',mesh=v.ref(path),slot_name=slot,material=v.ref(CAND+'/Materials/MI_'+n+'_'+slot))
            v.call('AssetTools','save_assets',asset_paths=[path])
        print('Imported',n,flush=True)

def assembly(final=False):
    folder=CAND+('/Meshes' if final else '/First')
    for n in c.NAMES:
        for suffix in ['', '_LOD1']:
            a=v.p.add('R4_'+n+suffix,asset=folder+'/'+n+suffix,loc=(1000000,0,0));v.props(a['component'],staticMesh=v.ref(folder+'/'+n+suffix));move(a,(1000000,0,0))
    v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])

def camera_check():
    # Level reload resets viewport FOV; pilot the existing review camera with the
    # editor-only shortcut. No native dialog or asset editor is opened.
    a=v.p.state()['actors']['Village_ReviewCamera'];v.props(a['component'],fieldOfView=60,aspectRatio=16/9,bConstrainAspectRatio=True)
    v.args=argparse.Namespace(phase='checks');v.capture('camera_probe',(0,0,0),15000,-55)
    result=json.loads((OUT/'evidence/checks/camera_probe.json').read_text())
    if result['cameraFOV']!=60:
        ident='SlateInspectorToolset.SlateInspectorToolset'
        schema=v.p.rpc('tools/call',{'name':'describe_toolset','arguments':{'toolset_name':ident}});(OUT/'evidence/schema_Slate.json').write_text(json.dumps(schema))
        snapshot=v.call(ident,'Snapshot',ref='',maxDepth=10);(OUT/'evidence/camera_slate.txt').write_text(snapshot)
        # The viewport splitter is identified from its persisted native snapshot;
        # fail rather than guess if the known viewport geometry changes.
        assert '[ref=sp4]' in snapshot and 'L_VillagePreview' in snapshot
        v.call(ident,'Click',ref='sp4');v.call('EditorAppToolset','SelectActors',actors=[v.ref(a['actor'])]);v.call(ident,'PressKey',key='Ctrl+Shift+P')
        v.capture('camera_piloted',(0,0,0),15000,-55);assert json.loads((OUT/'evidence/checks/camera_piloted.json').read_text())['cameraFOV']==60

def show(n,suffix=''):
    state=v.p.state();label='R4_'+n+suffix
    # Receipted active variant avoids moving every hidden mesh for every shot.
    previous=state.get('r4_visible')
    if previous and previous!=label:move(state['actors'][previous],(1000000,0,0))
    elif not previous:
        for old,a in state['actors'].items():
            if old.startswith('R4_SM_') and old!=label:move(a,(1000000,0,0))
    move(state['actors'][label],(0,0,600 if c.type_for(n)=='Bridge' else 0));state['r4_visible']=label;v.p.store(state)
    # Reuse the three existing Abrams assemblies; park two, one beside the type.
    key=c.type_for(n);offset=-2300 if key=='Bridge' else (-1800 if key in ['Barn','Workshop','Church'] else -1400)
    if n.endswith('Damaged') and key not in ['HouseA','Bridge']:offset=abs(offset)
    for i in range(3):
        for part,z in [('Hull',0),('Turret',180.681836605)]:
            a=v.p.state()['actors']['Abrams'+part+'_'+str(i)]
            v.call('ActorTools','set_actor_transform',actor=v.ref(a['actor']),xform=v.xf((100,offset,z) if i==0 else (1000000,i*2400,z),scale=(1.03599574,)*3),worldspace=True)
    if 'R4_Water' in state['actors']:
        a=state['actors']['R4_Water'];v.call('ActorTools','set_actor_transform',actor=v.ref(a['actor']),xform=v.xf((0,0,10) if key=='Bridge' else (1000000,0,0),scale=(70,40,.02)),worldspace=True)
    for i in [-1,1]:
        label='R4_Pier'+str(i)
        if label in state['actors']:
            a=state['actors'][label];v.call('ActorTools','set_actor_transform',actor=v.ref(a['actor']),xform=v.xf((i*1500,0,200) if key=='Bridge' else (1000000,0,0),scale=(.8,15,4.5)),worldspace=True)

def captures(phase='first',keys=None):
    v.guard();camera_check();keys=keys or c.TYPES+['Bridge']
    for key in keys:
        for n in c.names(key):
            show(n);v.args=argparse.Namespace(phase='warmup');v.capture(n,(0,0,0),15000,-55)
            v.args=argparse.Namespace(phase=phase)
            yaw=camera_yaw(key,n);z=600 if key=='Bridge' else 0
            v.capture(n+'_150m',(0,0,z),15000,-55,yaw=yaw)
            v.capture(n+'_threequarter',(-600 if key=='Church' else 0,-400 if key=='Bridge' else 0,1600 if key=='Church' else (350 if key=='Bridge' else z+350)),8500 if key=='Church' else (6500 if key=='Bridge' else (5200 if key=='Barn' else 4200)),-24,yaw=yaw)
            if n==c.names(key)[0]:v.capture(n+'_12m',detail_target(key,n),1200,-35,yaw=yaw)
        v.call('AssetTools','save_assets',asset_paths=[v.LEVEL]);print('CAPTURED',phase,key,flush=True)

def camera_yaw(key,n):
    if key in ['HouseA','Bridge']:return 145
    if key=='Church':return 35
    if n.endswith('Damaged'):return -145
    return 35 if key=='HouseB' else 145

def detail_target(key,n):
    # Large footprints put a centre-target 12m camera on/inside the wall. Keep
    # the exact arm/lens and aim at an exterior architectural detail instead.
    if key=='HouseB':return (0,-450,280)
    if key=='HouseC':return (475,0,240)
    if key=='Farmhouse':return (900,0,250)
    if key=='Barn':return (1000,0,300)
    if key=='Workshop':return (800,0,240)
    if key=='Church':return (0,550 if n.endswith('Damaged') else -550,100 if n.endswith('Rubble') else 440)
    return (0,0,600 if key=='Bridge' else 0)

def details():
    camera_check()
    for key in c.TYPES+['Bridge']:
        for n in (c.names(key) if key=='Church' else c.names(key)[:1]):
            for suffix,phase in [('', 'final')]+([('_MASTER','master')] if key in ['Church','HouseB'] else []):
                show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,(0,0,600 if key=='Bridge' else 0),15000,-55)
                v.args=argparse.Namespace(phase=phase);v.capture(n+'_12m',detail_target(key,n),1200,-35,yaw=camera_yaw(key,n))
    overview();print('Exterior detail cameras captured at exact 12m arm / 60 degree lens',flush=True)

def first():
    prepare();textures();materials();imports();assembly();captures()

def bridge_stage():
    parent='/Game/Art/Shared/Materials/Building/M_Bld_Surface';path=CAND+'/Materials/MI_PreviewWater'
    if not v.call('AssetTools','exists',path=path):v.call('MaterialInstanceTools','create',folder_path=CAND+'/Materials',asset_name='MI_PreviewWater',parent=v.ref(parent))
    v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(path),name='SurfaceColor',value={'r':.055,'g':.105,'b':.115,'a':1});v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(path),name='Roughness',value=.28)
    a=v.p.add('R4_Water',asset='/Engine/BasicShapes/Cube',loc=(1000000,0,0),scale=(70,40,.02));v.props(a['component'],overrideMaterials=[v.ref(path)])
    concrete=CAND+'/Materials/MI_PreviewConcrete'
    if not v.call('AssetTools','exists',path=concrete):v.call('MaterialInstanceTools','create',folder_path=CAND+'/Materials',asset_name='MI_PreviewConcrete',parent=v.ref(parent))
    v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(concrete),name='SurfaceColor',value={'r':.30,'g':.31,'b':.30,'a':1})
    for i in [-1,1]:
        a=v.p.add('R4_Pier'+str(i),asset='/Engine/BasicShapes/Cube',loc=(1000000,0,0),scale=(.8,15,6));v.props(a['component'],overrideMaterials=[v.ref(concrete)])
    v.call('AssetTools','save_assets',asset_paths=[path,concrete,v.LEVEL])

def master_import():
    for n in c.names('Church')+[c.names('HouseB')[0]]:
        for slot in c.SLOTS:
            path=CAND+'/Materials/MI_'+n+'_'+slot+'_MASTER';original=path[:-7]
            if not v.call('AssetTools','exists',path=path):v.call('AssetTools','duplicate',path=original,new_path=path)
            if slot!='Glass':v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(path),name='BakeStrength',value=0)
            v.call('AssetTools','save_assets',asset_paths=[path])
        path=CAND+'/Masters/'+n+'_MASTER'
        if not v.call('AssetTools','exists',path=path):v.call('StaticMeshTools','import_file',folder_path=CAND+'/Masters',asset_name=n+'_MASTER',source_file=str(OUT/'source'/(n+'_MASTER.fbx')),import_materials=False,import_textures=False,combine_meshes=True)
        v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(path),enabled=False)
        for slot in v.call('StaticMeshTools','get_material_slots',mesh=v.ref(path)):v.call('StaticMeshTools','set_material',mesh=v.ref(path),slot_name=slot,material=v.ref(CAND+'/Materials/MI_'+n+'_'+slot+'_MASTER'))
        v.call('AssetTools','save_assets',asset_paths=[path]);v.p.add('R4_'+n+'_MASTER',asset=path,loc=(1000000,0,0))

def root_defaults():
    parent=CAND+'/M_SetSurface'
    for e in v.call('MaterialTools','get_expressions',material_or_function=v.ref(parent)):
        if 'TextureSampleParameter2D' not in e['refPath']:continue
        param=v.get(e['refPath'],'parameterName')['parameterName']
        if param in ['BaseColor','Normal','ORM']:path=CAND+'/Textures/Bricks085_'+param
        else:path=CAND+'/Textures/Atlas/SM_Village_HouseA_'+{'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[param]
        v.props(e['refPath'],texture=v.ref(path))
    v.props(parent,twoSided=False);v.call('MaterialTools','recompile',material_or_function=v.ref(parent));v.call('AssetTools','save_assets',asset_paths=[parent])

def comparisons():
    camera_check()
    for key in c.TYPES+['Bridge']:
        n=c.names(key)[0];z=600 if key=='Bridge' else 0
        for suffix,d,tag in [('',24960,'249_6m'),('_LOD1',27040,'270_4m')]:
            show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,(0,0,z),15000,-55)
            v.args=argparse.Namespace(phase='switch');v.capture(n+'_'+tag,(0,0,z),d,-55,yaw=camera_yaw(key,n))
    for n in c.names('Church')+[c.names('HouseB')[0]]:
        key=c.type_for(n);yaw=-35 if n.endswith('Damaged') else 35
        for suffix,phase in [('', 'final'),('_MASTER','master')]:
            show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,(0,0,0),15000,-55)
            v.args=argparse.Namespace(phase=phase)
            for label,d,p in [('12m',1200,-35),('150m',15000,-55),('450m',45000,-55)]:v.capture(n+'_'+label,(0,0,0),d,p,yaw=yaw)
    # Church landmark from the opposite base direction as well.
    show(c.names('Church')[0]);v.args=argparse.Namespace(phase='final');v.capture('Church_opposite_base',(0,0,0),15000,-55,yaw=-145)

def overview():
    for label,a in v.p.state()['actors'].items():
        if label.startswith('R4_SM_'):move(a,(1000000,0,0))
    for i,key in enumerate(c.TYPES):
        # Put the long farmhouse in the back row; the near fourth column would
        # cross the lower edge of the fixed 150m overview's 16:9 content.
        loc=(9600,3800,0) if key=='Farmhouse' else ((i%4)*3200,(i//4)*3800,0)
        move(v.p.state()['actors']['R4_'+c.names(key)[0]],loc)
    for i in range(3):
        for part,z in [('Hull',0),('Turret',180.681836605)]:
            a=v.p.state()['actors']['Abrams'+part+'_'+str(i)];v.call('ActorTools','set_actor_transform',actor=v.ref(a['actor']),xform=v.xf((i*3200,-1600,z),scale=(1.03599574,)*3),worldspace=True)
    for label in ['R4_Water','R4_Pier-1','R4_Pier1']:
        if label in v.p.state()['actors']:move(v.p.state()['actors'][label],(1000000,0,0))
    v.args=argparse.Namespace(phase='warmup');v.capture('overview',(4200,1900,0),15000,-55)
    v.args=argparse.Namespace(phase='final');v.capture('whole_set_150m',(4200,1900,0),15000,-55)
    state=v.p.state();state.pop('r4_visible',None);v.p.store(state);v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])

def final():
    textures(True);root_defaults();materials(True);imports(True);assembly(True);master_import();bridge_stage();captures('final');comparisons();overview()

def barnfix():
    # Refinement checkpoint: hay opening remains above the 4.5m main door.
    # Its re-packed atlases use a fresh path; preserve earlier imports as history.
    paths=[]
    for n in c.names('Barn'):
        for end in ['Tint','BakeNormal','BakeAO']:
            path=c.atlas_root(n)+'/'+n+'_'+end
            if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=c.atlas_root(n),asset_name=n+'_'+end,source_file=str(OUT/'textures'/(n+'_'+end+'.png')))
            v.props(path,sRGB=False,compressionSettings='TC_Masks',maxTextureSize=2048);paths.append(path)
        for slot in c.SLOTS:
            if slot=='Glass':continue
            path=CAND+'/Materials/MI_'+n+'_'+slot;params=v.get(path,'textureParameterValues')['textureParameterValues']
            for param in params:
                key=param['parameterInfo']['name']
                if key in ['AtlasTint','BakeNormal','BakeAO']:param['parameterValue']=v.ref(c.atlas_root(n)+'/'+n+'_'+{'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[key])
            v.props(path,textureParameterValues=params);paths.append(path)
    v.call('AssetTools','save_assets',asset_paths=paths);captures('final',['Barn'])
    n=c.names('Barn')[0]
    for suffix,d,label in [('',24960,'249_6m'),('_LOD1',27040,'270_4m')]:
        show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n,(0,0,0),15000,-55)
        v.args=argparse.Namespace(phase='switch');v.capture(n+'_'+label,(0,0,0),d,-55,yaw=camera_yaw('Barn',n))
    overview();print('Barn hay-door refinement atlases and captures installed',flush=True)

def church_scale():
    v.guard();assert not v.call('EditorAppToolset','GetOpenAssets')
    parent=CAND+'/M_SetSurface';expressions=v.call('MaterialTools','get_expressions',material_or_function=v.ref(parent));exists=False
    for e in expressions:
        if 'VectorParameter' in e['refPath'] and v.get(e['refPath'],'parameterName')['parameterName']=='ScanUVScale':exists=True
    if not exists:
        def node(kind,**values):
            e=v.call('MaterialTools','add_expression',material_or_function=v.ref(parent),expression_class=v.ref('/Script/Engine.MaterialExpression'+kind))['refPath']
            if values:v.props(e,**values)
            return e
        def link(a,b,pin,out=''):
            names=v.call('MaterialTools','get_expression_input_names',expression=v.ref(b))
            if pin not in names and len(names)==1:pin=names[0]
            v.call('MaterialTools','connect_expressions',from_expression=v.ref(a),from_output_name=out,to_expression=v.ref(b),to_input_name=pin)
        uv=node('TextureCoordinate',coordinateIndex=0);scale=node('VectorParameter',parameterName='ScanUVScale',defaultValue={'r':1,'g':1,'b':1,'a':1});mask=node('ComponentMask',r=True,g=True);link(scale,mask,'Input');multiply=node('Multiply');link(uv,multiply,'A');link(mask,multiply,'B')
        for e in expressions:
            if 'TextureSampleParameter2D' in e['refPath'] and v.get(e['refPath'],'parameterName')['parameterName'] in ['BaseColor','Normal','ORM']:link(multiply,e['refPath'],'UVs')
        v.props(parent,twoSided=False);v.call('MaterialTools','recompile',material_or_function=v.ref(parent));v.call('AssetTools','save_assets',asset_paths=[parent])
    paths=[]
    for n in c.names('Church'):
        for suffix in ['','_MASTER']:
            path=CAND+'/Materials/MI_'+n+'_Brick'+suffix;v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(path),name='ScanUVScale',value={'r':.625,'g':1,'b':1,'a':1});paths.append(path)
    v.call('AssetTools','save_assets',asset_paths=paths)
    for n in c.names('Church'):
        yaw=camera_yaw('Church',n)
        for suffix,phase in [('', 'final'),('_MASTER','master')]:
            show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,(0,0,0),15000,-55)
            v.args=argparse.Namespace(phase=phase)
            for label,d,p in [('12m',1200,-35),('150m',15000,-55),('450m',45000,-55)]:v.capture(n+'_'+label,detail_target('Church',n) if label=='12m' else (0,0,0),d,p,yaw=yaw)
            if not suffix:v.capture(n+'_threequarter',(-600,0,1600),8500,-24,yaw=yaw)
    n=c.names('Church')[0]
    for suffix,d,label in [('',24960,'249_6m'),('_LOD1',27040,'270_4m')]:
        show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n,(0,0,0),15000,-55);v.args=argparse.Namespace(phase='switch');v.capture(n+'_'+label,(0,0,0),d,-55,yaw=35)
    show(n);v.args=argparse.Namespace(phase='final');v.capture('Church_opposite_base',(0,0,0),15000,-55,yaw=-145)
    (OUT/'evidence/church_brick_scale.json').write_text(json.dumps({'scan_uv_scale':[.625,1],'horizontal_repeat_m':1.6,'vertical_repeat_m':1,'calibration':'Bricks097 headers approximately 10cm / stretchers 20cm / courses 6–6.5cm; source image 2048x1024','scope':'Only scanned BaseColor/Normal/ORM UV0; unique geometric bake atlas unchanged'},indent=2));overview()

def calm_concrete():
    paths=[]
    for n in c.NAMES:
        path=CAND+'/Materials/MI_'+n+'_Rubble';v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(path),name='BakeStrength',value=.08);paths.append(path)
    v.call('AssetTools','save_assets',asset_paths=paths)
    captures('final');details()
    (OUT/'evidence/concrete_bake_weight.json').write_text(json.dumps({'Rubble_BakeStrength':.08,'other_game_slots':.35,'master':0,'reason':'Calm excessive normal/AO contrast on thin concrete lintels/sills; retain scan normal and faint master-edge bake','switch_views_unchanged':'All unique normal/AO contribution is already zero past 230m'},indent=2))

def damage_references():
    # Put the Abrams on the camera-facing flank of the tall damaged buildings.
    # This is preview staging only; neither mesh nor camera distance changes.
    for key in c.TYPES[1:]:
        n=c.names(key)[1];show(n);v.args=argparse.Namespace(phase='warmup');v.capture(n,(0,0,0),15000,-55)
        v.args=argparse.Namespace(phase='final');v.capture(n+'_150m',(0,0,0),15000,-55,yaw=camera_yaw(key,n));v.capture(n+'_threequarter',(-600 if key=='Church' else 0,0,1600 if key=='Church' else 350),8500 if key=='Church' else (5200 if key=='Barn' else 4200),-24,yaw=camera_yaw(key,n))
    n=c.names('Church')[1]
    for suffix,phase in [('', 'final'),('_MASTER','master')]:
        show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,(0,0,0),15000,-55);v.args=argparse.Namespace(phase=phase)
        for label,d,p in [('12m',1200,-35),('150m',15000,-55),('450m',45000,-55)]:v.capture(n+'_'+label,detail_target('Church',n) if label=='12m' else (0,0,0),d,p,yaw=camera_yaw('Church',n))
    overview()

def bounds_switches():
    camera_check()
    for key in c.TYPES+['Bridge']:
        n=c.names(key)[0];box=v.call('StaticMeshTools','get_bounds',mesh=v.ref(CAND+'/Meshes/'+n))
        centre=tuple((box['min'][a]+box['max'][a])/2 for a in 'xyz');centre=(centre[0],centre[1],centre[2]+(600 if key=='Bridge' else 0))
        for suffix,d,label in [('',24960,'249_6m'),('_LOD1',27040,'270_4m')]:
            show(n,suffix);v.args=argparse.Namespace(phase='warmup');v.capture(n+suffix,centre,15000,-55)
            v.args=argparse.Namespace(phase='switch');shot=n+'_'+label;v.capture(shot,centre,d,-55,yaw=camera_yaw(key,n))
            file=OUT/'evidence/switch'/(shot+'.json');data=json.loads(file.read_text());data['distance_reference']='LOD0 whole-building bounds centre, shared by both actors';data['bounds_centre_world_cm']=centre
            camera=data['cameraLocation'];data['camera_to_bounds_centre_cm']=sum((camera[a]-centre[i])**2 for i,a in enumerate('xyz'))**.5
            assert abs(data['camera_to_bounds_centre_cm']-d)<.1;file.write_text(json.dumps(data,indent=2))
    overview()

def verify():
    v.guard();qa=json.loads((OUT/'evidence/source_qa.json').read_text())['meshes'];geo=json.loads((OUT/'evidence/geometry_qa.json').read_text());report={'level':v.LEVEL,'scope':'Native editor preview; separate LOD1 actors. No production/runtime or automatic switch claim.','meshes':{},'materials':{},'textures':{},'dependencies':{}}
    paths=[v.LEVEL,CAND+'/M_SetSurface']
    for n in c.NAMES:
        for suffix,folder in [('',CAND+'/Meshes'),('_LOD1',CAND+'/Meshes')]+([('_MASTER',CAND+'/Masters')] if n in c.names('Church')+[c.names('HouseB')[0]] else []):
            path=folder+'/'+n+suffix;count=v.call('StaticMeshTools','get_triangle_count',mesh=v.ref(path),lod_index=0)
            assert count==qa[n+suffix]['reimport']['triangles']
            if suffix!='_MASTER':assert count==qa[n+suffix]['triangles']
            rec={'asset':path,'source_triangles':qa[n+suffix]['triangles'],'fbx_triangles':qa[n+suffix]['reimport']['triangles'],'unreal_triangles':count,'budget_triangles':max(count,qa[n+suffix]['triangles']),'bounds_cm':v.call('StaticMeshTools','get_bounds',mesh=v.ref(path)),'slots':v.call('StaticMeshTools','get_material_slots',mesh=v.ref(path)),'bindings':v.get(path,'staticMaterials'),'nanite':v.call('StaticMeshTools','is_nanite_enabled',mesh=v.ref(path)),'native_lod_count':v.call('StaticMeshTools','get_lod_count',mesh=v.ref(path))}
            assert not rec['nanite'] and rec['native_lod_count']==1 and len(rec['slots'])<=6
            if suffix=='':
                body=v.get(path,'bodySetup')['bodySetup']['refPath']
                if n=='SM_Bridge_Span_Destroyed':v.props(body,collisionTraceFlag='CTF_UseSimpleAsComplex')
                data=v.get(body,'aggGeom','collisionTraceFlag');rec['collision']={'convex_hulls':len(data['aggGeom']['convexElems']),'trace_flag':data['collisionTraceFlag']}
                assert rec['collision']['convex_hulls']==geo[n]['collision_boxes'],(n,rec['collision'],geo[n]['collision_boxes'])
                if n=='SM_Bridge_Span_Destroyed':assert rec['collision']['convex_hulls']==2
            report['meshes'][n+suffix]=rec;paths.append(path)
        print('Verified meshes',n,flush=True)
    for n in c.NAMES:
        for slot in c.SLOTS:
            for suffix in ['']+(['_MASTER'] if n in c.names('Church')+[c.names('HouseB')[0]] else []):
                path=CAND+'/Materials/MI_'+n+'_'+slot+suffix
                report['materials'][path]=v.get(path,'parent','textureParameterValues','scalarParameterValues','vectorParameterValues');paths.append(path)
    for f in sorted((OUT/'textures').glob('*.png')):
        path=(c.atlas_root(f.stem) if f.stem.startswith('SM_') else CAND+'/Textures')+'/'+f.stem
        report['textures'][path]=v.get(path,'sRGB','compressionSettings','maxTextureSize','mipGenSettings','neverStream');paths.append(path)
    for path in [CAND+'/M_SetSurface',CAND+'/Materials/MI_SM_Village_HouseA_Brick',CAND+'/Materials/MI_SM_Village_Church_Damaged_Brick',CAND+'/Meshes/SM_Village_Church']:
        report['dependencies'][path]=v.call('AssetTools','get_dependencies',asset_path=path)
    v.call('AssetTools','save_assets',asset_paths=paths);v.call('SceneTools','load_level',level_path=v.LEVEL)
    report['persisted_level']=v.call('SceneTools','get_current_level');report['persisted_materials']={}
    for path in report['materials']:
        persisted=v.get(path,'parent','textureParameterValues','scalarParameterValues','vectorParameterValues');assert all(persisted[k]==report['materials'][path][k] for k in persisted);report['persisted_materials'][path]=persisted
    report['log']=v.call('LogsToolset','GetLogEntries',category='',pattern='(Round4|HouseB|Church|Bridge_Span).*([Ww]arning|[Ee]rror|degenerate|tangent)|([Ww]arning|[Ee]rror|degenerate|tangent).*(Round4|HouseB|Church|Bridge_Span)',maxEntries=100)
    (OUT/'evidence/unreal_verified.json').write_text(json.dumps(report,indent=2));print('Saved and reloaded preview; all material bindings persisted',flush=True)

def smoke():
    camera_check()
    for n in ['SM_Village_HouseA_Rubble','SM_Village_Barn','SM_Village_Church_Damaged','SM_Bridge_Span_Destroyed']:
        key=c.type_for(n);show(n);v.args=argparse.Namespace(phase='warmup');v.capture(n,(0,0,600 if key=='Bridge' else 0),15000,-55)
        v.args=argparse.Namespace(phase='persisted_smoke');v.capture(n+'_threequarter',(-600 if key=='Church' else 0,-400 if key=='Bridge' else 0,1600 if key=='Church' else 350),8500 if key=='Church' else (6500 if key=='Bridge' else (5200 if key=='Barn' else 4200)),-24,yaw=camera_yaw(key,n))
    overview();print('Post-reload smoke and overview saved. Editor remains running.',flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('stage');ap.add_argument('--final',action='store_true');ap.add_argument('--types',nargs='+');args=ap.parse_args();initialize()
    if args.stage=='captures':captures('final' if args.final else 'first',args.types)
    else:{'inventory':inventory,'prepare':prepare,'textures':lambda:textures(args.final),'materials':lambda:materials(args.final),'imports':lambda:imports(args.final),'assembly':lambda:assembly(args.final),'camera':camera_check,'first':first,'final':final,'bridge_stage':bridge_stage,'comparisons':comparisons,'overview':overview,'verify':verify,'smoke':smoke,'barnfix':barnfix,'details':details,'church_scale':church_scale,'calm_concrete':calm_concrete,'damage_references':damage_references,'bounds_switches':bounds_switches}[args.stage]()
