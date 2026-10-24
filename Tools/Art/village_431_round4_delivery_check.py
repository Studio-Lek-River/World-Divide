"""Final offline receipts plus read-only live editor checks. Run after report."""
import hashlib,json,sys
from html.parser import HTMLParser
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_round4_delivery as d
OUT=d.OUT;FINAL=d.PORTABLE;c=d.c;v=d.v
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

class Links(HTMLParser):
    def __init__(self):super().__init__();self.links=[]
    def handle_starttag(self,tag,attrs):
        self.links += [value for key,value in attrs if key in ['href','src'] and value]

def main():
    manifest=read(FINAL/'manifest.json');native=read(OUT/'evidence/unreal_verified.json');receipts=read(FINAL/'hashes.json');images=read(FINAL/'evidence/index.json')
    assert all(sha(FINAL/f)==r['sha256'] for f,r in receipts['files'].items())
    for f in receipts['files']:
        assert sha(FINAL/f)==sha(OUT/f),(f,'portable/working divergence')
    protected=read(OUT/'evidence/protected_before.json');assert all(sha(c.ROOT/f)==h for f,h in protected.items())
    count=0;dimensions={}
    for path,tags in native['source_tags'].items():
        if 'Dimensions' in tags:
            wh=[int(n) for n in tags['Dimensions'].split('x')];assert max(wh)<=2048 and tags['IsSourceValid']=='True';dimensions[path]=wh
        for item in json.loads(tags.get('AssetImportData','[]')):
            package=c.ROOT/'Content'/path.removeprefix('/Game/');file=(package.parent/item['RelativeFilename']).resolve()
            assert file.is_relative_to(FINAL) and file.exists(),(path,str(file))
            assert hashlib.md5(file.read_bytes()).hexdigest()==item['FileMD5'],(path,'native import hash mismatch')
            count+=1
    parser=Links();parser.feed((FINAL/'REVIEW.html').read_text(encoding='utf8'))
    assert all((FINAL/link).exists() for link in parser.links if not link.startswith(('http:','https:','#')))
    for file,r in images.items():
        assert sha(FINAL/file)==r['sha256'];assert r['camera']['cameraFOV']==60
    for n in c.NAMES:
        for phase,views in [('final',['12m','150m','450m','threequarter']),('master',['12m','150m','450m']),('switch',['249_6m','270_4m'])]:
            for view in views:assert 'evidence/'+phase+'/'+n+'_'+view+'.png' in images
    d.p.initialize();v.guard();dirty=v.call('AssetTools','is_dirty',asset_path=v.LEVEL);pie=v.call('EditorAppToolset','IsPIERunning');assert not dirty and not pie
    old={}
    for n in c.names('HouseA'):
        path='/Game/Art/Candidates/Village/Round2/Final02/'+n;old[path]=v.call('AssetTools','exists',path=path)
    assert all(old.values())
    final={'status':'Round4 delivered for producer approval; approval pending','portable_files_hashed':len(receipts['files']),'native_meshes':len(native['meshes']),'persisted_instances':len(native['materials']),'textures':len(dimensions),'native_import_source_md5_matches_final_files':count,'images':len(images),'all_images_60_degree_FOV':True,'all_23_states_have_required_views':True,'prior_rounds_and_greybox_files_unchanged':len(protected),'gallery_links_exist':True,'round2_assets_present':old,'editor_left_running':True,'level':v.LEVEL,'level_dirty':dirty,'pie':pie,'verification_limit':'Same editor process; no restart or historical asset deletion.'}
    (OUT/'evidence/final_delivery_check.json').write_text(json.dumps(final,indent=2));(FINAL/'evidence/final_delivery_check.json').write_text(json.dumps(final,indent=2));print(json.dumps(final,indent=2),flush=True)

if __name__=='__main__':main()
