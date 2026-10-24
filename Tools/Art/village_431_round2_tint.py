"""In-house UV1 tint masks: soot, contact dust, restrained macro variation, number 18.
Deterministic rasterization from source geometry; CC0 scans remain untouched.
"""
import bpy,json,sys,math
import numpy as np
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round2 as r
N=2048

def main():
    out=r.OUT;bpy.ops.wm.open_mainfile(filepath=str(out/'source/HouseA_round2_baked.blend'))
    for obj in list(bpy.data.objects):
        if obj.type!='MESH' or obj.name.startswith('UCX_') or obj.name.endswith(('_LOD1','_MASTER')):continue
        pixels=np.ones((N,N,4),np.float32);filled=np.zeros((N,N),bool);uv=obj.data.uv_layers['BakeUV'].data
        obj.data.calc_loop_triangles();damaged=obj.name.endswith('Damaged');rubble=obj.name.endswith('Rubble')
        for tri in obj.data.loop_triangles:
            coords=np.array([uv[i].uv[:] for i in tri.loops])*N
            x0=max(0,int(coords[:,0].min()));x1=min(N,int(coords[:,0].max())+1);y0=max(0,int(coords[:,1].min()));y1=min(N,int(coords[:,1].max())+1)
            yy,xx=np.mgrid[y0:y1,x0:x1];xx=xx+.5;yy=yy+.5
            a,b,c=coords;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den)<1e-8:continue
            u=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
            v=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den;w=1-u-v;inside=(u>=0)&(v>=0)&(w>=0)
            vs=np.array([obj.data.vertices[i].co[:] for i in tri.vertices]);p=u[...,None]*vs[0]+v[...,None]*vs[1]+w[...,None]*vs[2]
            x,y,z=p[:,:,0],p[:,:,1],p[:,:,2];slot=tri.material_index
            macro=.95+.035*np.sin(x*1.2+y*.7)*np.cos(z*.8+x*.4)
            color=np.stack([macro,macro,macro],axis=-1)
            if slot==1:
                roofmacro=.86+.13*np.sin(x*.64+y*.42)*np.cos(y*.60-x*.23)
                color*=roofmacro[...,None]*np.where(y[...,None]>0,.96,1.04)
            if damaged:
                # Soft soot around irregular breach and roof opening, strongest at breaks.
                dx=np.maximum(np.abs(x-.03)-1.62,0);dz=np.maximum(np.abs(z-2.8)-2.55,0)
                breach=np.exp(-(dx/.60)**2-(dz/.7)**2-((y-4.5)/.7)**2)*.78
                t=y*math.sqrt(1+(3.8/4.5)**2)
                hx=np.maximum(np.abs(x-1.1)-2.22,0);ht=np.maximum(t-4.45,0)
                roof=np.exp(-(hx/.6)**2-(ht/.6)**2-((z-(9.64-y*3.8/4.5))/.45)**2)*(y>-.1)*.8
                soot=np.maximum(breach,roof)
                # Rising smoke above each blown window, including pristine gables.
                for i,(face,a,sill,ww,hh,k,fl) in enumerate(r.opening_list()):
                    if i%3==0 or k!='window':continue
                    axis,side,other=r.g.FACES[face];extent=5.5 if axis==0 else 4.5
                    normalcoord=x if axis==0 else y;along=y if axis==0 else x
                    streak=np.exp(-((along-a)/(.43+.12*np.maximum(z-sill-hh,0)))**2-((normalcoord-side*extent)/.1)**2)*np.clip(1-(z-sill-hh)/1.3,0,1)*(z>=sill+hh)*.65
                    soot=np.maximum(soot,streak)
                color*=np.maximum(.15,1-soot)[...,None]
                if slot in [0,1,4,5]:color*=np.where(z[...,None]<.65,.65,1)
            if rubble:
                dust=np.clip(.72+.10*z,.62,.98) if slot==0 else np.clip(.86+.035*z,.8,.98)
                color*=dust[...,None]
                if slot==5:color*=np.array([.90,.85,.78])
            # Modern number plate at +X: legible original 7-segment digits 18.
            if slot==2:
                plate=(x>5.55)&(np.abs(y-.88)<.13)&(z>1.45)&(z<1.67)
                uplate=(y-.75)/.26;vplate=(z-1.45)/.22
                digit=(uplate>.46).astype(int);du=np.where(digit==0,uplate/.42,(uplate-.50)/.42)
                vertical=((du>.70)&(du<.90)&(vplate>.18)&(vplate<.82))
                eight=(((np.abs(vplate-.2)<.055)|(np.abs(vplate-.5)<.055)|(np.abs(vplate-.8)<.055))&(du>.12)&(du<.86))|(((du>.1)&(du<.26)|(du>.72)&(du<.88))&(vplate>.2)&(vplate<.8))
                ink=plate&np.where(digit==0,vertical,eight);color=np.where(ink[...,None],.025,color)
            patch=pixels[y0:y1,x0:x1,:3];patch[inside]=color[inside];filled[y0:y1,x0:x1]|=inside
        # Two-pixel atlas gutters match the final bake packing.
        for k in range(2):
            for axis,shift in [(0,1),(0,-1),(1,1),(1,-1)]:
                valid=np.roll(filled,shift,axis)&~filled;src=np.roll(pixels,shift,axis);pixels[valid]=src[valid];filled[valid]=True
        img=bpy.data.images.new(obj.name+'_Tint',width=N,height=N,alpha=False);img.colorspace_settings.name='Non-Color';img.pixels.foreach_set(pixels.ravel());img.filepath_raw=str(out/'textures'/(obj.name+'_Tint.png'));img.file_format='PNG';img.save()
        print('TINT',obj.name,flush=True)
if __name__=='__main__':main()
