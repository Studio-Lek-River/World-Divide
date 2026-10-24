"""Shader-only reconciliation of saved sources to final native tint overrides."""
import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy
import village_431_round4_common as c

def main():
    for key in (sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else c.TYPES+['Bridge']):
        path=c.OUT/'source'/(key+'_baked.blend');bpy.ops.wm.open_mainfile(filepath=str(path))
        for mat in bpy.data.materials:
            slot='Wood' if '_Wood' in mat.name else ('Rubble' if key=='Church' and '_Rubble' in mat.name and mat.name.rsplit('_',1)[-1]=='Rubble' else None)
            if key=='Bridge' and mat.name.endswith('_Trim'):slot='Trim'
            if not slot:continue
            tint=c.WOOD_TINT[key] if slot=='Wood' else ((.10,.075,.055) if slot=='Trim' else (1.3,1.28,1.20))
            mat.diffuse_color=(*tint,1)
            for node in mat.node_tree.nodes:
                if node.type=='VECT_MATH' and node.operation=='MULTIPLY' and not node.inputs[1].is_linked:node.inputs[1].default_value=tint
        for image in bpy.data.images:
            if image.source=='FILE':image.filepath='//../textures/'+Path(image.filepath).name
        bpy.ops.wm.save_as_mainfile(filepath=str(path))
    print('Sources match final tint overrides; geometry and FBX unchanged',flush=True)

if __name__=='__main__':main()
