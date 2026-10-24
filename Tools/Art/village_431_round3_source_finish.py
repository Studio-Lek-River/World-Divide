"""Source shader-only finishing; geometry and portable FBXs remain untouched."""
import sys,json
from pathlib import Path
sys.dont_write_bytecode=True
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round3_common as c

def main():
    path=c.OUT/'source/Church_baked.blend';bpy.ops.wm.open_mainfile(filepath=str(path));seen=set();records=[]
    counts={o.name:len(o.data.polygons) for o in bpy.data.objects if o.type=='MESH'}
    for obj in bpy.data.objects:
        if obj.type!='MESH' or obj.name.startswith('UCX_'):continue
        mat=obj.data.materials[0]
        if mat.name in seen:continue
        seen.add(mat.name)
        for node in list(mat.node_tree.nodes):
            if node.type!='TEX_IMAGE' or not node.image:continue
            if Path(node.image.filepath).stem.rsplit('_',1)[-1] not in ['BaseColor','Normal','ORM']:continue
            if node.inputs[0].links:
                previous=node.inputs[0].links[0].from_node
                if previous.type=='VECT_MATH' and previous.operation=='MULTIPLY' and all(abs(a-b)<1e-6 for a,b in zip(previous.inputs[1].default_value,(.625,1,1))):continue
            uv=mat.node_tree.nodes.new('ShaderNodeUVMap');uv.uv_map='UVMap';scale=mat.node_tree.nodes.new('ShaderNodeVectorMath');scale.operation='MULTIPLY';scale.inputs[1].default_value=(.625,1,1);mat.node_tree.links.new(uv.outputs[0],scale.inputs[0]);mat.node_tree.links.new(scale.outputs[0],node.inputs[0])
        records.append(mat.name)
    assert counts=={o.name:len(o.data.polygons) for o in bpy.data.objects if o.type=='MESH'}
    bpy.ops.wm.save_as_mainfile(filepath=str(path));(c.OUT/'evidence/source_shader_finish.json').write_text(json.dumps({'materials':records,'ScanUVScale':[.625,1],'geometry_unchanged':True},indent=2))
    print('Church source shader sampling matches native calibration; geometry unchanged',flush=True)

if __name__=='__main__':main()
