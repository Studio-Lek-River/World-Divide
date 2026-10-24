"""Repaint only two scorch atlases from their saved UVs; no mesh/bake changes."""
import sys,json
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy,numpy as np
import village_431_round4_bake as b

def main():
    original=b.save_map
    b.save_map=lambda name,array:original(name,array) if name.endswith('_Tint') else None
    receipts={}
    for key in ['Farmhouse','Church']:
        bpy.ops.wm.open_mainfile(filepath=str(b.OUT/'source'/(key+'_baked.blend')));name='SM_Village_'+key+'_Damaged';obj=bpy.data.objects[name]
        maps=[]
        for end in ['BakeNormal','BakeAO']:
            image=bpy.data.images.load(str(b.OUT/'textures'/(name+'_'+end+'.png')),check_existing=False);image.colorspace_settings.name='Non-Color';maps.append(np.array(image.pixels[:],np.float32).reshape(b.N,b.N,4))
        receipts[name]=b.finish_maps(obj,key,*maps)
    (b.OUT/'evidence/scorch_refinement.json').write_text(json.dumps(receipts,indent=2));print('Only Farmhouse/Church damaged Tint PNGs refined',flush=True)

if __name__=='__main__':main()
