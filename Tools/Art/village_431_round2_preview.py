"""Round2 native MCP driver. Does not edit Round1 assets or create levels."""
import argparse,json,sys,shutil
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_preview as v
OUT=v.ROOT/'Art/Shared/village/issue431/round2'
CAND='/Game/Art/Candidates/Village/Round2'
v.OUT=OUT;v.p.OUT=OUT;v.ASSETS=CAND;v.MESHES=CAND+'/First'
v.SURFACES['Roof']='RoofingTiles013A'

def setup():
    v.guard();v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])
    # Clone the existing approved preview actor receipt; rebind only the three houses.
    if not (OUT/'evidence/preview_state.json').exists():shutil.copy2(OUT.parent/'round1/evidence/preview_state.json',OUT/'evidence/preview_state.json')
    if not v.call('AssetTools','exists',path=v.MESHES+'/'+v.NAMES[0]):v.imports()
    # Keep Round1's proven scanned parent as a recoverable starting point, copied for
    # Round2 tint/macro/bake extensions. No shared or Round1 graph is modified.
    if not v.call('AssetTools','exists',path=CAND+'/M_Village_Scanned'):
        v.call('AssetTools','duplicate',path=v.CAND+'/Round1/Revision02/M_Village_Scanned',new_path=CAND+'/M_Village_Scanned')
    bind=v.bind;v.bind=lambda:None
    try:v.materials();v.scorch()
    finally:v.bind=bind
    v.bind()
    for slot,tint in [('Brick',(0.74,.63,.54,1)),('Roof',(.60,.46,.37,1)),('Rubble',(.31,.29,.26,1)),('Wood',(.10,.095,.085,1))]:
        v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(CAND+'/MI_'+slot),name='ScanTint',value=dict(zip('rgba',tint)))
        v.call('AssetTools','save_assets',asset_paths=[CAND+'/MI_'+slot])
    v.assembly()

def final_import():
    v.guard();v.call('AssetTools','save_assets',asset_paths=[v.LEVEL])
    for name in v.NAMES:
        for suffix,folder in [('',v.MESHES),('_LOD1',v.MESHES),('_MASTER',CAND+'/Masters')]:
            file=(OUT/'source' if suffix=='_MASTER' else OUT/'fbx')/(name+suffix+'.fbx')
            if not v.call('AssetTools','exists',path=folder+'/'+name+suffix):v.call('StaticMeshTools','import_file',folder_path=folder,asset_name=name+suffix,source_file=str(file),import_materials=False,import_textures=False,combine_meshes=True)
            mesh=folder+'/'+name+suffix
            v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(mesh),enabled=False)
            bounds=v.call('StaticMeshTools','get_bounds',mesh=v.ref(mesh));assert bounds['max']['x']>500,bounds
            v.call('AssetTools','save_assets',asset_paths=[mesh])
            print('Final import',name+suffix,flush=True)
    v.bind();v.assembly()

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('stage');ap.add_argument('--phase',default='first');ap.add_argument('--mesh-folder',default='First');args=ap.parse_args()
    v.args=args;v.MESHES=CAND+'/'+args.mesh_folder
    v.p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'village431round2','version':'1'}})
    {'setup':setup,'final_import':final_import,'shots':v.shots,'verify':v.verify,'imports':v.imports,'assembly':v.assembly,'bind':v.bind}[args.stage]()
