"""Round2 final editor captures and verification. No UI, native LOD attachment,
production edits or editor restart. Separate actors demonstrate authored LOD1.
"""
import argparse,json,math,sys
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_round2_preview as w
v=w.v;OUT=w.OUT;CAND=w.CAND
GAME=CAND+'/Final02';MASTER=CAND+'/Masters02/Final'

def install_final():
    v.guard();assert not v.call('EditorAppToolset','GetOpenAssets')
    for name in v.NAMES:
        for suffix,folder in [('',GAME),('_LOD1',GAME),('_MASTER',MASTER)]:
            file=(OUT/'source' if suffix=='_MASTER' else OUT/'fbx')/(name+suffix+'.fbx');mesh=folder+'/'+name+suffix
            if not v.call('AssetTools','exists',path=mesh):v.call('StaticMeshTools','import_file',folder_path=folder,asset_name=name+suffix,source_file=str(file),import_materials=False,import_textures=False,combine_meshes=True)
            v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(mesh),enabled=False)
            for slot in v.call('StaticMeshTools','get_material_slots',mesh=v.ref(mesh)):
                state='Pristine' if name==v.NAMES[0] else ('Damaged' if name==v.NAMES[1] else 'Rubble')
                v.call('StaticMeshTools','set_material',mesh=v.ref(mesh),slot_name=slot,material=v.ref(CAND+'/Materials/MI_'+state+'_'+slot+('_MASTER' if suffix=='_MASTER' else '')))
            v.call('AssetTools','save_assets',asset_paths=[mesh]);print('Installed validated',name+suffix,flush=True)
    for file in sorted((OUT/'textures').glob('SM_*.png')):
        path=CAND+'/Textures/Atlas02/'+file.stem
        if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=CAND+'/Textures/Atlas02',asset_name=file.stem,source_file=str(file))
        v.props(path,sRGB=False,compressionSettings='TC_Masks',maxTextureSize=2048)
        v.call('AssetTools','save_assets',asset_paths=[path])
    master=CAND+'/M_Round2_Surface'
    for e in v.call('MaterialTools','get_expressions',material_or_function=v.ref(master)):
        if 'TextureSampleParameter2D' in e['refPath']:
            param=v.get(e['refPath'],'parameterName')['parameterName']
            if param in ['AtlasTint','BakeNormal','BakeAO']:
                suffix={'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[param]
                v.props(e['refPath'],texture=v.ref(CAND+'/Textures/Atlas02/SM_Village_HouseA_'+suffix))
    v.props(master,twoSided=False);v.call('MaterialTools','recompile',material_or_function=v.ref(master));v.call('AssetTools','save_assets',asset_paths=[master])
    for state,name in zip(['Pristine','Damaged','Rubble'],v.NAMES):
        for slot in v.SLOTS:
            if slot=='Glass':continue
            for suffix in ['','_MASTER']:
                path=CAND+'/Materials/MI_'+state+'_'+slot+suffix
                params=v.get(path,'textureParameterValues')['textureParameterValues']
                for param in params:
                    kind=param['parameterInfo']['name']
                    if kind in ['AtlasTint','BakeNormal','BakeAO']:
                        end={'AtlasTint':'Tint','BakeNormal':'BakeNormal','BakeAO':'BakeAO'}[kind]
                        param['parameterValue']=v.ref(CAND+'/Textures/Atlas02/'+name+'_'+end)
                v.props(path,textureParameterValues=params);v.call('AssetTools','save_assets',asset_paths=[path])
        print('Bound final atlases',state,flush=True)

def refine():
    v.guard();assert not v.call('EditorAppToolset','GetOpenAssets')
    for file in sorted((OUT/'textures').glob('*_Tint.png')):
        path=CAND+'/Textures/Tint02/'+file.stem
        if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=CAND+'/Textures/Tint02',asset_name=file.stem,source_file=str(file))
        v.props(path,sRGB=False,compressionSettings='TC_Masks',maxTextureSize=2048)
        v.call('AssetTools','save_assets',asset_paths=[path])
    master=CAND+'/M_Round2_Surface'
    for e in v.call('MaterialTools','get_expressions',material_or_function=v.ref(master)):
        if 'TextureSampleParameter2D' in e['refPath'] and v.get(e['refPath'],'parameterName')['parameterName']=='AtlasTint':v.props(e['refPath'],texture=v.ref(CAND+'/Textures/Tint02/SM_Village_HouseA_Tint'))
    v.props(master,twoSided=False);v.call('MaterialTools','recompile',material_or_function=v.ref(master));v.call('AssetTools','save_assets',asset_paths=[master])
    for state,name in zip(['Pristine','Damaged','Rubble'],v.NAMES):
        for slot in v.SLOTS:
            for suffix in ['','_MASTER']:
                path=CAND+'/Materials/MI_'+state+'_'+slot+suffix
                if slot=='Glass':
                    v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(path),name='SurfaceColor',value={'r':.025,'g':.04,'b':.05,'a':1})
                    v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(path),name='GlassOpacity',value=.5)
                else:v.call('MaterialInstanceTools','set_texture_parameter',instance=v.ref(path),name='AtlasTint',value=v.ref(CAND+'/Textures/Tint02/'+name+'_Tint'))
                v.call('AssetTools','save_assets',asset_paths=[path])
        print('Refined macro/dust and glazing',state,flush=True)

def move(entry,loc):v.call('ActorTools','set_actor_transform',actor=v.ref(entry['actor']),xform=v.xf(loc),worldspace=True)

def restore():
    v.guard();v.MESHES=GAME
    v.assembly()
    v.call('RTSConsoleToolset','ExecuteConsoleCommand',command='ShowFlag.BillboardSprites 0')
    for i,name in enumerate(v.NAMES):
        y=(i-1)*2400
        for label,asset in [('Master_'+name,MASTER+'/'+name+'_MASTER'),('LOD1_'+name,GAME+'/'+name+'_LOD1')]:
            a=v.p.add(label,asset=asset,loc=(1000000,y,0));v.props(a['component'],staticMesh=v.ref(asset));move(a,(1000000,y,0))
    v.call('AssetTools','save_assets',asset_paths=[v.LEVEL]);print('Restored game; master/LOD1 actors parked off-camera',flush=True)

def variant(kind):
    actors=v.p.state()['actors']
    for i,name in enumerate(v.NAMES):
        y=(i-1)*2400
        for k,key in [('game',name),('master','Master_'+name),('lod1','LOD1_'+name)]:
            move(actors[key],(0 if k==kind else 1000000,y,0))

def capture_set(kind,phase):
    v.guard();variant(kind);warm(kind);v.args=argparse.Namespace(phase=phase)
    if kind=='game':v.shots()
    else:
        for i,state in enumerate(['pristine','damaged','rubble']):
            target=(0,(i-1)*2400,0)
            for suffix,d,pitch in [('12m',1200,-35),('150m',15000,-55),('450m',45000,-55)]:v.capture(state+'_'+suffix,target,d,pitch)
    variant('game')

def warm(kind):
    # Moving an actor in from the off-camera parking column invalidates GPU
    # occlusion history. Draw the full three-state row twice before evidence.
    v.args=argparse.Namespace(phase='warmup')
    for i in range(2):v.capture(kind+'_'+str(i),(0,0,0),15000,-55)

def switches():
    v.guard();v.args=argparse.Namespace(phase='switch')
    for kind in ['game','lod1']:
        variant(kind);warm(kind);v.args=argparse.Namespace(phase='switch')
        for i,state in enumerate(['pristine','damaged','rubble']):
            for suffix,d in [('249_6m',24960),('270_4m',27040)]:v.capture(state+'_'+kind+'_'+suffix,(0,(i-1)*2400,0),d,-55)
    variant('game')
    # Inspection rows: games at X0, masters at X24m, LOD1 at X48m, same units/pivots.
    actors=v.p.state()['actors']
    for i,name in enumerate(v.NAMES):
        for x,key in [(2400,'Master_'+name),(4800,'LOD1_'+name)]:move(actors[key],(x,(i-1)*2400,0))
    v.call('AssetTools','save_assets',asset_paths=[v.LEVEL]);print('Saved three inspection columns; LODs remain separate',flush=True)

def smoke():
    v.guard();variant('game');warm('game');v.args=argparse.Namespace(phase='persisted_smoke')
    for i,state in enumerate(['pristine','damaged','rubble']):
        y=(i-1)*2400
        v.capture(state+'_150m',(0,y,0),15000,-55)
        v.capture(state+'_threequarter',(0,y-200,400),2900,-24)
    actors=v.p.state()['actors']
    for i,name in enumerate(v.NAMES):
        for x,key in [(2400,'Master_'+name),(4800,'LOD1_'+name)]:move(actors[key],(x,(i-1)*2400,0))
    # Leave a useful whole-assembly view in the running editor.
    v.capture('inspection_columns',(2400,0,0),19000,-55)
    v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])
    report={'level':v.call('SceneTools','get_current_level'),'materials':{},'actors':{},'parent':v.call('AssetTools','get_dependencies',asset_path=CAND+'/M_Round2_Surface')}
    for name in v.NAMES:
        for key in [name,'Master_'+name,'LOD1_'+name]:report['actors'][key]=v.get(actors[key]['component'],'staticMesh')
    for state in ['Pristine','Damaged','Rubble']:
        for slot in v.SLOTS:
            for suffix in ['','_MASTER']:
                path=CAND+'/Materials/MI_'+state+'_'+slot+suffix
                report['materials'][path]=v.get(path,'parent','textureParameterValues','scalarParameterValues')
    (OUT/'evidence/persisted_smoke/bindings.json').write_text(json.dumps(report,indent=2))
    print('Post-reload smoke complete; saved inspection columns; editor left open',flush=True)

def verify():
    v.guard();paths=[v.LEVEL];report={'level':v.LEVEL,'scope':'Native editor preview. LOD1 is a separate actor by producer instruction; production attachment at 260m remains Claude setup.','meshes':{},'materials':{},'textures':{}}
    qa=json.loads((OUT/'evidence/source_qa.json').read_text())
    for name in v.NAMES:
        for suffix,folder in [('',GAME),('_LOD1',GAME),('_MASTER',MASTER)]:
            mesh=folder+'/'+name+suffix;count=v.call('StaticMeshTools','get_triangle_count',mesh=v.ref(mesh),lod_index=0)
            assert count==qa[name+suffix]['triangles'],(mesh,count,qa[name+suffix]['triangles'])
            bounds=v.call('StaticMeshTools','get_bounds',mesh=v.ref(mesh));assert bounds['max']['x']>500
            report['meshes'][name+suffix]={'asset':mesh,'source_fbx_triangles':qa[name+suffix]['triangles'],'unreal_triangles':count,'bounds_cm':bounds,'slots':v.call('StaticMeshTools','get_material_slots',mesh=v.ref(mesh)),'bindings':v.get(mesh,'staticMaterials'),'nanite':v.call('StaticMeshTools','is_nanite_enabled',mesh=v.ref(mesh)),'native_lod_count':v.call('StaticMeshTools','get_lod_count',mesh=v.ref(mesh))}
            if suffix=='':
                body=v.get(mesh,'bodySetup')['bodySetup']['refPath'];collision=v.get(body,'aggGeom','collisionTraceFlag')
                report['meshes'][name+suffix]['collision']={'convex_hulls':len(collision['aggGeom']['convexElems']),'trace_flag':collision['collisionTraceFlag']}
            assert not report['meshes'][name+suffix]['nanite'];paths.append(mesh)
    for state in ['Pristine','Damaged','Rubble']:
        for slot in v.SLOTS:
            for suffix in ['','_MASTER']:
                path=CAND+'/Materials/MI_'+state+'_'+slot+suffix
                report['materials'][path]=v.get(path,'parent','textureParameterValues','vectorParameterValues','scalarParameterValues');paths.append(path)
    for file in (OUT/'textures').glob('*.png'):
        path=CAND+('/Textures/Atlas02/' if file.stem.startswith('SM_') else '/Textures/')+file.stem
        report['textures'][path]=v.get(path,'sRGB','compressionSettings','maxTextureSize','neverStream','mipGenSettings');paths.append(path)
    paths.append(CAND+'/M_Round2_Surface');v.call('AssetTools','save_assets',asset_paths=paths)
    v.call('SceneTools','load_level',level_path=v.LEVEL)
    report['persisted_level']=v.call('SceneTools','get_current_level')
    report['persisted_master_dependencies']=v.call('AssetTools','get_dependencies',asset_path=CAND+'/M_Round2_Surface')
    report['persisted_instance_parent']=v.get(CAND+'/Materials/MI_Pristine_Brick','parent','textureParameterValues')
    report['camera']=v.get(v.p.state()['actors']['Village_ReviewCamera']['component'],'fieldOfView','aspectRatio','bConstrainAspectRatio')
    report['log']=v.call('LogsToolset','GetLogEntries',category='',pattern='(Round2|HouseA|M_Round2).*([Ww]arning|[Ee]rror|degenerate|tangent)|([Ww]arning|[Ee]rror|degenerate|tangent).*(Round2|HouseA|M_Round2)',maxEntries=100)
    (OUT/'evidence/unreal_final_verified.json').write_text(json.dumps(report,indent=2));print('Verified source/UE counts, slots, bounds, texture settings and saved bindings',flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('stage',choices=['install_final','refine','restore','game','master','switch','verify','smoke']);args=ap.parse_args()
    v.p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'r2finish','version':'1'}})
    if args.stage in ['game','master']:capture_set(args.stage,'final' if args.stage=='game' else 'master')
    else:{'install_final':install_final,'refine':refine,'restore':restore,'switch':switches,'verify':verify,'smoke':smoke}[args.stage]()
