"""Read native final assets, save/reload, validate references. No cleanup or dialogs."""
import json,sys
sys.dont_write_bytecode=True
import village_431_round4_delivery as d
p=d.p;v=d.v;c=d.c;OUT=d.OUT;CAND=d.CAND

def main():
    p.initialize();v.guard();qa=json.loads((OUT/'evidence/source_qa.json').read_text())['meshes'];geo=json.loads((OUT/'evidence/geometry_qa.json').read_text())
    report={'level':v.LEVEL,'meshes':{},'materials':{},'textures':{},'dependencies':{},'source_tags':{}}
    paths=[v.LEVEL,CAND+'/M_SetSurface']
    for n in c.NAMES:
        for suffix,folder in [('', 'Meshes'),('_LOD1','Meshes'),('_MASTER','Masters')]:
            path=c.mesh_path(n,suffix);count=v.call('StaticMeshTools','get_triangle_count',mesh=v.ref(path),lod_index=0)
            expected=qa[n+suffix]['triangles'] if n=='SM_Village_Workshop_Damaged' and suffix=='_MASTER' else qa[n+suffix]['reimport']['triangles']
            assert count==expected,(n,suffix,count,expected)
            if suffix!='_MASTER':assert count==qa[n+suffix]['triangles']
            rec={'asset':path,'source_triangles':qa[n+suffix]['triangles'],'fbx_triangles':qa[n+suffix]['reimport']['triangles'],'unreal_triangles':count,'budget_triangles':max(count,qa[n+suffix]['triangles'],qa[n+suffix]['reimport']['triangles']),'bounds_cm':v.call('StaticMeshTools','get_bounds',mesh=v.ref(path)),'slots':v.call('StaticMeshTools','get_material_slots',mesh=v.ref(path)),'bindings':v.get(path,'staticMaterials'),'nanite':v.call('StaticMeshTools','is_nanite_enabled',mesh=v.ref(path)),'native_lod_count':v.call('StaticMeshTools','get_lod_count',mesh=v.ref(path))}
            assert not rec['nanite'] and rec['native_lod_count']==1 and len(rec['slots'])<=6
            if not suffix:
                body=v.get(path,'bodySetup')['bodySetup']['refPath'];data=v.get(body,'aggGeom','collisionTraceFlag');rec['collision']={'convex_hulls':len(data['aggGeom']['convexElems']),'trace_flag':data['collisionTraceFlag'],'data':data}
                assert rec['collision']['convex_hulls']==geo[n]['collision_boxes']
                if n=='SM_Bridge_Span_Destroyed':assert rec['collision']['convex_hulls']==2 and data['collisionTraceFlag']=='CTF_UseSimpleAsComplex'
            report['meshes'][n+suffix]=rec;paths.append(path)
        for slot in c.SLOTS:
            for suffix in ['', '_MASTER']:
                if suffix and slot not in report['meshes'][n+'_MASTER']['slots']:continue
                path=CAND+'/Materials/MI_'+n+'_'+slot+suffix;report['materials'][path]=v.get(path,'parent','textureParameterValues','scalarParameterValues','vectorParameterValues');paths.append(path)
        print('Native checked',n,flush=True)
    for f in sorted((d.PORTABLE/'textures').glob('*.png')):
        path=c.atlas_path(f.stem) if f.stem.startswith('SM_') else d.scan_root()+'/'+f.stem
        report['textures'][path]=v.get(path,'sRGB','compressionSettings','maxTextureSize','mipGenSettings','neverStream');paths.append(path)
    v.call('AssetTools','save_assets',asset_paths=paths)
    # Native registry traversal verifies actual closure, not filename assumptions.
    queue=[r['asset'] for r in report['meshes'].values()];seen=set()
    while queue:
        path=queue.pop()
        if path in seen:continue
        seen.add(path);deps=v.call('AssetTools','get_dependencies',asset_path=path);report['dependencies'][path]=deps
        for dep in deps:
            assert '/Village/Round' not in dep and '/Final/First' not in dep,(path,dep)
            if dep.startswith('/Game/'):queue.append(dep)
    for path in paths:
        if path==v.LEVEL or '/Materials/' in path or path.endswith('M_SetSurface'):continue
        report['source_tags'][path]=v.call('AssetTools','get_asset_tags',asset_path=path)
        tags=report['source_tags'][path]
        if 'AssetImportData' in tags:
            assert 'Art/Shared/village/final/' in tags['AssetImportData'].replace('\\','/'),(path,tags['AssetImportData'])
    report['external_referencers']={path:v.call('AssetTools','get_referencers',asset_path=path) for path in [CAND+'/M_SetSurface',CAND+'/Meshes/SM_Village_Church']}
    parent=CAND+'/M_SetSurface';graph={'asset':parent,'nodes':{},'outputs':{}}
    for e in v.call('MaterialTools','get_expressions',material_or_function=v.ref(parent)):
        schema=v.call('ObjectTools','list_properties',instance=e)
        if isinstance(schema,str):schema=json.loads(schema)
        graph['nodes'][e['refPath']]={'class':v.call('ObjectTools','get_class',instance=e),'properties':v.get(e['refPath'],*schema.keys()),'inputs':v.call('MaterialTools','get_expression_inputs',material_or_function=v.ref(parent),expression=e)}
    for prop in ['BaseColor','Normal','Roughness','Metallic','AmbientOcclusion']:
        graph['outputs'][prop]=v.call('MaterialTools','get_property_input',material=v.ref(parent),material_property='MP_'+prop)
    (OUT/'evidence/material_graph.json').write_text(json.dumps(graph,indent=2))
    v.call('SceneTools','load_level',level_path=v.LEVEL);report['persisted_level']=v.call('SceneTools','get_current_level')
    report['persisted_materials']={}
    for path,expected in report['materials'].items():
        actual=v.get(path,*expected.keys());assert actual==expected,(path,'persistence mismatch');report['persisted_materials'][path]=actual
    report['restart']='Not permitted: existing editor stays running. Historical packages retained; no deletions.'
    report['scope']='Native editor candidate, separate manual LOD1 actors; no production or runtime acceptance.'
    (OUT/'evidence/unreal_verified.json').write_text(json.dumps(report,indent=2));print('All native assets, closure and persisted bindings verified',flush=True)

if __name__=='__main__':main()
