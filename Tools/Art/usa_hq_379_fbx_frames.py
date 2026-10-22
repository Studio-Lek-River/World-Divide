import json,math
from pathlib import Path
from io_scene_fbx import parse_fbx
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq'
result={}
for rel in ('game/SM_HQ_ShellDetail_LOD1.fbx','issue379/preview_fbx/SM_HQ_ShellDetail_LOD1.fbx'):
    root,version=parse_fbx.parse(str(ROOT/rel));records={}
    def walk(e):
        if e.id in (b'Tangents',b'Binormals',b'Normals'):
            a=e.props[0];lengths=[math.sqrt(sum(a[i+j]**2 for j in range(3))) for i in range(0,len(a),3)]
            records[e.id.decode()]={'vectors':len(lengths),'zero':sum(x<1e-4 for x in lengths),'min':min(lengths),'max':max(lengths)}
        for c in e.elems:walk(c)
    walk(root);result[rel]=records
(ROOT/'issue379/fbx_frame_vectors.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
