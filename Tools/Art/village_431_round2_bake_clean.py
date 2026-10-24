"""Reject selected-to-active ray misses from intersecting architectural members.
Keep valid master normals; fall back to the receiving face only for >60 degree
outliers, and pad the repaired atlas. No change to game geometry or UV layout.
"""
import bpy,json,sys
import numpy as np
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round2 as r
N=2048

def main():
    bpy.ops.wm.open_mainfile(filepath=str(r.OUT/'source/HouseA_round2_baked.blend'));report={}
    for obj in bpy.data.objects:
        if obj.type!='MESH' or obj.name.startswith('UCX_') or obj.name.endswith(('_LOD1','_MASTER')):continue
        path=r.OUT/'textures'/(obj.name+'_BakeNormal.png');img=bpy.data.images.load(str(path),check_existing=False);img.colorspace_settings.name='Non-Color'
        pixels=np.array(img.pixels[:],np.float32).reshape(N,N,4);filled=np.zeros((N,N),bool);uv=obj.data.uv_layers['BakeUV'].data;obj.data.calc_loop_triangles();count=badcount=0
        for tri in obj.data.loop_triangles:
            coords=np.array([uv[i].uv[:] for i in tri.loops])*N;a,b,c=coords
            x0=max(0,int(coords[:,0].min()));x1=min(N,int(coords[:,0].max())+1);y0=max(0,int(coords[:,1].min()));y1=min(N,int(coords[:,1].max())+1)
            yy,xx=np.mgrid[y0:y1,x0:x1];xx=xx+.5;yy=yy+.5;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den)<1e-8:continue
            u=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den;v=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den;inside=(u>=0)&(v>=0)&(1-u-v>=0)
            expected=np.array([tri.normal.x,-tri.normal.y,tri.normal.z]);patch=pixels[y0:y1,x0:x1,:3];decoded=patch*2-1
            lengths=np.linalg.norm(decoded,axis=-1);dots=(decoded*expected).sum(axis=-1)/np.maximum(lengths,.001)
            bad=inside&((dots<.5)|(lengths<.5));patch[bad]=(expected+1)/2;count+=int(inside.sum());badcount+=int(bad.sum());filled[y0:y1,x0:x1]|=inside
        for k in range(2):
            for axis,shift in [(0,1),(0,-1),(1,1),(1,-1)]:
                valid=np.roll(filled,shift,axis)&~filled;src=np.roll(pixels,shift,axis);pixels[valid]=src[valid];filled[valid]=True
        img.pixels.foreach_set(pixels.ravel());img.filepath_raw=str(path);img.file_format='PNG';img.save()
        report[obj.name]={'sampled_texels':count,'outlier_texels_repaired':badcount,'fraction':badcount/max(count,1),'criterion':'Normal differs over 60 degrees from receiver; ray miss/cross-member hit, preserve valid master bevel normals'}
    (r.OUT/'evidence/normal_bake_cleanup.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
