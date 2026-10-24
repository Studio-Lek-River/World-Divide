"""Read validated Round 1 outputs and make the portable receipt/review gallery."""
import ast
import hashlib
import json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Art/Shared/village/issue431/round1'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    source=json.loads((OUT/'evidence/mesh_manifest.json').read_text())
    unreal=json.loads((OUT/'evidence/unreal_verified.json').read_text())
    textures=[]
    for f in sorted((OUT/'textures').glob('*.png')):
        with Image.open(f) as im:size=list(im.size)
        assert max(size)<=2048
        surface,kind=f.stem.rsplit('_',1);original=ROOT/'Saved/Surfaces'/surface/(kind+'.png')
        assert sha(f)==sha(original)
        textures.append({'file':str(f.relative_to(OUT)).replace('\\','/'),'size':size,'sha256':sha(f),'source':str(original.relative_to(ROOT)).replace('\\','/'),'surface':surface,'license':'CC0; ambientCG surface identified in Tools/Editor/free_assets.json; copied unchanged'})
    meshes={}
    for name,record in source['meshes'].items():
        ue=unreal['meshes'][name]
        assert ue['triangles']==record['reimport']['triangles']==record['triangles']
        assert not ue['nanite']
        assert ue['triangles']<=(3000 if name.endswith('_LOD1') else 12000)
        assert len(ue['slots'])<=6
        meshes[name]={'fbx':'fbx/'+name+'.fbx','source_triangles':record['triangles'],'fbx_reimport_triangles':record['reimport']['triangles'],'unreal_triangles':ue['triangles'],'portable_slots':record['slots'],'unreal_slots':ue['slots'],'unreal_sections':len(ue['slots']),'bounds_cm':ue['bounds'],'unreal_asset':'/Game/Art/Candidates/Village/Round1/Revision04/'+name,'sha256':sha(OUT/'fbx'/(name+'.fbx'))}
    evidence=[]
    for state in ['pristine','damaged','rubble']:
        for view in ['12m','150m','450m','threequarter']:
            stem=state+'_'+view;path=OUT/'evidence/final'/(stem+'.png');meta=json.loads(path.with_suffix('.json').read_text())
            assert meta['cameraFOV']==60
            with Image.open(path) as im:resolution=list(im.size)
            evidence.append({'image':str(path.relative_to(OUT)).replace('\\','/'),'resolution':resolution,'sha256':sha(path),'camera':meta,'projection':'Native constrained CameraActor, 16:9 content with black letterbox bars in editor capture'})
    manifest={'issue':431,'round':1,'status':'Architecture review gate; no visual approval yet','source':'source/HouseA_round1.blend','source_sha256':sha(OUT/'source/HouseA_round1.blend'),'meshes':meshes,'textures':textures,'evidence':evidence,'garrison':source['openings'],'LOD_status':'Separate first-pass LOD1 FBX; Unreal preview uses LOD0. Final bake/master and 260m switching comparisons start in Round 2 per BRIEF.md.','dependencies':unreal.get('dependencies',{})}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    body='''<!doctype html><meta charset="utf-8"><title>Village #431 — Round 1</title><style>body{background:#171c1d;color:#edf0e8;font:16px system-ui;margin:28px}h1{font-size:26px}h2{margin-top:40px}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}img{width:100%;display:block;background:black}figure{margin:0}figcaption{padding:8px 0;color:#bbc5c1}a{color:#b4d8e4}p{max-width:1000px}</style><h1>HouseA — Round 1 architecture review</h1><p>Native Unreal editor captures in L_VillagePreview. Pristine / damaged / rubble, each beside the reference Abrams. 60° horizontal FOV, constrained 16:9. At 12 m the close zoom intentionally crops the architecture; the three-quarter views show each full building and tank. LOD0 shown throughout. No gameplay or final LOD acceptance claimed.</p><p><a href="../ROUND_1.md">Round report</a> · <a href="manifest.json">Manifest and camera receipts</a></p>'''
    for view,label in [('150m','150 m — primary architecture gate'),('threequarter','Three-quarter — whole building and Abrams'),('12m','12 m — exact near zoom / detail'),('450m','450 m — far zoom')]:
        body+='<h2>'+label+'</h2><div class="grid">'
        for state in ['pristine','damaged','rubble']:
            path='evidence/final/'+state+'_'+view+'.png'
            body+='<figure><a href="'+path+'"><img src="'+path+'"></a><figcaption>'+state.title()+' — '+view+'</figcaption></figure>'
        body+='</div>'
    (OUT/'REVIEW.html').write_text(body,encoding='utf8')
    scripts=['village_431_housea.py','village_431_preview.py','village_431_sourceqa.py','village_431_report.py']
    for script in scripts:ast.parse((ROOT/'Tools/Art'/script).read_text())
    receipt={'source':manifest['source_sha256'],'fbx':{k:v['sha256'] for k,v in meshes.items()},'scripts':{s:sha(ROOT/'Tools/Art'/s) for s in scripts},'textures':{t['file']:t['sha256'] for t in textures},'checks':'Six FBXs agree between source, independent Blender reimport and live Unreal. 12 final camera images exist at FOV 60. 12 PNG textures <=2K and match staged CC0 files. Generator syntax parsed.'}
    (OUT/'evidence/delivery_receipt.json').write_text(json.dumps(receipt,indent=2))
    print('Validated',len(meshes),'FBXs,',len(textures),'textures,',len(evidence),'final captures')

if __name__=='__main__':main()
