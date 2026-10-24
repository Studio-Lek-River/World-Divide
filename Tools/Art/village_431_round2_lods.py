"""Attach authored LODs through candidate-local import settings and native reimport.
No CDO/config edits and no native file picker. Run only after the pending picker
is cancelled. Discover/inspect the candidate import-data schema before applying.
"""
import sys,json,re,time
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_round2_preview as w
v=w.v;CAND=w.CAND;OUT=w.OUT
S='SlateInspectorToolset.SlateInspectorToolset'

def main():
    raise RuntimeError('Superseded by producer instruction: never attach candidate LODs natively or open file dialogs. Use village_431_round2_finish.py switch with separate LOD1 actors; Claude production setup attaches LOD1 at 260m.')
    v.p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'r2lods','version':'1'}});v.guard()
    receipt={}
    for name in v.NAMES:
        mesh=CAND+'/FinalLODs/'+name
        if not v.call('AssetTools','exists',path=mesh):v.call('StaticMeshTools','import_file',folder_path=CAND+'/FinalLODs',asset_name=name,source_file=str(OUT/'fbx'/(name+'_WithLODs.fbx')),import_materials=False,import_textures=False,combine_meshes=True)
        imp=v.get(mesh,'assetImportData')['assetImportData']['refPath']
        receipt[name]={'before_lods':v.call('StaticMeshTools','get_lod_count',mesh=v.ref(mesh)),'import_properties':v.call('ObjectTools','list_properties',instance=v.ref(imp))}
        v.props(imp,importMeshLODs=True)
        assert v.get(imp,'importMeshLODs')['importMeshLODs']
        v.call('AssetTools','save_assets',asset_paths=[mesh])
        v.call('EditorAppToolset','OpenEditorForAsset',assetPath=mesh)
        v.call(S,'Observe',ref='',maxDepth=40);time.sleep(.3)
        snapshot=v.call(S,'Snapshot',ref='',maxDepth=40)
        (OUT/'evidence'/('lod_ui_'+name+'.txt')).write_text(snapshot)
        windows=list(re.finditer(r'window "'+re.escape(name)+r'"[^\n]*\[ref=([^\]]+)\]',snapshot))
        assert len(windows)==1,'Select only the newly opened candidate asset editor'
        section=snapshot[windows[0].start():];section=section.split('\nwindow ')[0]
        match=re.search(r'button "Reimport Mesh"[^\n]*\[ref=([^\]]+)\]',section);assert match
        v.call(S,'Click',ref=match.group(1));time.sleep(1)
        count=v.call('StaticMeshTools','get_lod_count',mesh=v.ref(mesh));assert count==2,(mesh,count)
        receipt[name]['triangles']=[v.call('StaticMeshTools','get_triangle_count',mesh=v.ref(mesh),lod_index=i) for i in [0,1]]
        assert receipt[name]['triangles'][0]<=12000 and receipt[name]['triangles'][1]<=3000
        bounds=v.call('StaticMeshTools','get_bounds',mesh=v.ref(mesh));ext=[(bounds['max'][a]-bounds['min'][a])/2 for a in 'xyz'];radius=sum(e*e for e in ext)**.5
        # UE's ComputeBoundsScreenSize at 60 horizontal degrees, 16:9.
        threshold=radius*(3**.5)*(16/9)/26000
        v.call('StaticMeshTools','set_lod_thresholds',mesh=v.ref(mesh),thresholds=[1,threshold]);v.call('StaticMeshTools','set_nanite_enabled',mesh=v.ref(mesh),enabled=False)
        receipt[name].update({'bounds':bounds,'sphere_radius_cm':radius,'screen_threshold':threshold,'distance_cm':26000,'thresholds_readback':v.call('StaticMeshTools','get_lod_thresholds',mesh=v.ref(mesh))})
        v.call('AssetTools','save_assets',asset_paths=[mesh])
        close=re.findall(r'button "Close"[^\n]*\[ref=([^\]]+)\]',section)
        if close:v.call(S,'Click',ref=close[-1])
        print('Attached authored LOD',name,receipt[name]['triangles'],flush=True)
    (OUT/'evidence/lod_verified.json').write_text(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
