"""Round 3 continuous optical fields and reference-specific PBR finishes."""
import bpy,json,math,copy
import numpy as np
from wall_lamp_materials import make_textures,save_map,apply_pbr,bake_finish

def tint(piece):
    n=piece['mesh'];return (1,.10,.002) if 'Warning' in n else ((1,.001,.0004) if 'Mast' in n else ((1,1,1) if 'Indicator' in n else (.89,.94,1)))
def strength(piece):return 24 if 'Warning' in piece['mesh'] else (14 if 'Mast' in piece['mesh'] else (10 if 'Indicator' in piece['mesh'] else 3.5))
def apply(piece,out,finished=True):
    proxy=copy.deepcopy(piece);proxy['light']={'kelvin':4000};apply_pbr(proxy,out,use_finish=finished)
    nt=bpy.data.materials['Lens'].node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
    p.inputs['Emission Color'].default_value=(*tint(piece),1)
    tn=nt.nodes.new('ShaderNodeRGB');tn.name='LensTint';tn.label='Per-mount linear emission tint';tn.outputs[0].default_value=(*tint(piece),1)
    profile=next(n for n in nt.nodes if n.type=='TEX_IMAGE' and 'LightProfile' in n.image.name)
    hot=nt.nodes.new('ShaderNodeMapRange');hot.name='HotCore';hot.clamp=True;hot.inputs['From Min'].default_value=.55;hot.inputs['From Max'].default_value=.90
    nt.links.new(profile.outputs['Color'],hot.inputs['Value'])
    mix=nt.nodes.new('ShaderNodeMixRGB');mix.name='CoreColour';mix.blend_type='MIX';mix.inputs[2].default_value=(*( (1,.48,.08) if 'Warning' in piece['mesh'] else ((1,.22,.035) if 'Mast' in piece['mesh'] else (1,1,1))),1)
    nt.links.new(hot.outputs[0],mix.inputs[0]);nt.links.new(tn.outputs[0],mix.inputs[1]);nt.links.new(mix.outputs[0],p.inputs['Emission Color'])
    mul=next(n for n in nt.nodes if n.type=='MATH' and n.operation=='MULTIPLY' and n.inputs[1].default_value==5);mul.inputs[1].default_value=strength(piece)
    glass='Beacon' in piece['mesh'];p.inputs['Subsurface Weight'].default_value=0 if glass else .025
    p.inputs['Coat Weight'].default_value=.22 if glass else .25;p.inputs['Coat Roughness'].default_value=.06 if glass else .15;p.inputs['IOR'].default_value=1.49
    p.inputs['Specular IOR Level'].default_value=.20 if glass else .5
    p=next(n for n in bpy.data.materials['Housing'].node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    p.inputs['Specular IOR Level'].default_value=.28 if 'Warning' in piece['mesh'] else .42

def read(path):
    im=bpy.data.images.load(str(path),check_existing=False);im.colorspace_settings.name='Non-Color';a=np.empty(len(im.pixels),np.float32);im.pixels.foreach_get(a);a=a.reshape((im.size[1],im.size[0],4))[:,:,:3].copy();bpy.data.images.remove(im);return a

def data16(path,array):
    # Linear 16-bit emission profiles avoid the quantized contour bands of Round 1.
    scene=bpy.context.scene;old=(scene.view_settings.view_transform,scene.render.image_settings.color_depth,scene.render.image_settings.color_mode)
    scene.view_settings.view_transform='Raw';scene.render.image_settings.color_depth='16';scene.render.image_settings.color_mode='RGB';scene.render.image_settings.file_format='PNG'
    h,w=array.shape[:2];im=bpy.data.images.new(path.stem,width=w,height=h,float_buffer=True,alpha=False);im.colorspace_settings.name='Non-Color';rgba=np.ones((h,w,4),np.float32);rgba[:,:,:3]=np.clip(array,0,1);im.pixels.foreach_set(rgba.ravel());im.save_render(str(path),scene=scene);bpy.data.images.remove(im)
    scene.view_settings.view_transform,scene.render.image_settings.color_depth,scene.render.image_settings.color_mode=old

def pad(array,x,y,w,h):
    for xx in range(1,4):array[y:y+h,x-xx]=array[y:y+h,x];array[y:y+h,x+w-1+xx]=array[y:y+h,x+w-1]
    array[y-3:y,x-3:x+w+3]=array[y:y+1,x-3:x+w+3];array[y+h:y+h+3,x-3:x+w+3]=array[y+h-1:y+h,x-3:x+w+3]

def region(c,res,finish):
    pre='finish_' if finish else '';sc=c[pre+'scale'];x,y=[int(v*res) for v in c[pre+'offset']];w,h=[max(2,int(v*sc*res)+1) for v in c['size']];yy,xx=np.mgrid[:h,:w].astype(np.float32)
    factor=c.get('metric_factor',1.);return x,y,w,h,xx/(sc*res*factor),yy/(sc*res*factor),np.s_[y:y+h,x:x+w],sc*factor

def normal_from_height(height,scale,res,old=None):
    dy,dx=np.gradient(height);nx=-dx*scale*res;ny=dy*scale*res
    if old is not None:
        nz=np.maximum(old[:,:,2]*2-1,.25);nx+=(old[:,:,0]*2-1)/nz;ny+=(old[:,:,1]*2-1)/nz
    mag=np.sqrt(nx*nx+ny*ny+1);return np.stack((nx/mag*.5+.5,ny/mag*.5+.5,1/mag*.5+.5),2)

def housing(piece,objects,out):
    name=piece['mesh'].replace('SM_Kit_','');res=2048;cs=[c for o in objects for c in json.loads(o['chart_layout'])]
    for finish in (False,True):
        if finish:path=out/'source/bake_inputs'/f'T_{name}_Housing_Micro_Normal.png';normal=read(path)
        else:color=read(out/'textures'/f'T_{name}_Housing_BaseColor.png');orm=read(out/'textures'/f'T_{name}_Housing_ORM.png')
        for ci,c in enumerate(cs):
            if c['slot']!=0:continue
            x,y,w,h,xx,yy,dst,scale=region(c,res,finish);rng=np.random.default_rng(9393+ci*173+sum(map(ord,name)));height=np.zeros((h,w),np.float32);pit=np.zeros_like(height);scratch=np.zeros_like(height)
            wm,hm=c['size'];edge=np.minimum.reduce((xx,wm-xx,yy,hm-yy))
            for _ in range(min(130,max(2,int(wm*hm*1700)))):
                px,py=rng.random(2)*[wm,hm];r=rng.uniform(.0004,.0012);mark=np.exp(-((np.hypot(xx-px,yy-py)/r)**4));pit=np.maximum(pit,mark);height-=rng.uniform(.00012,.0003)*mark
            for _ in range(max(1,min(10,int((wm+hm)*7)))):
                px,py=rng.random(2)*[wm,hm];length=rng.uniform(.004,.030);width=rng.uniform(.00012,.00045);slope=rng.uniform(-.4,.4)
                mark=np.exp(-((yy-py-(xx-px)*slope)/width)**4)*np.exp(-((xx-px)/length)**8);scratch=np.maximum(scratch,mark);height-=.00008*mark
            kind=c['kind']
            if kind=='slotted' and abs(wm-hm)<.001:
                sl=(abs(xx-wm/2)<.010)*(abs(yy-hm/2)<.0015);height-=.0008*sl
            steel='Indicator' in name or kind in ('plate_steel','latch');enamel=kind=='enamel'
            if finish:
                if enamel:normal[dst]=normal_from_height(height*.15,scale,res)
                else:normal[dst]=normal_from_height(height,scale,res,normal[dst])
            else:
                original=color[dst].copy();bright=np.clip((original.max(2)-.23)/.35,0,1);wear=np.maximum(bright,np.clip((.0018-edge)/.0018,0,1)*(.35+.65*(.5+.5*np.sin(xx*1771+np.sin(yy*823)))))
                if enamel:
                    color[dst]=np.array((.71,.72,.69))*(.95+.05*np.sin(xx*277+yy*183))[:,:,None];orm[dst]=np.stack((.98+0*xx,.32+0*xx,0*xx),2)
                elif steel:
                    grain=.018*np.sin(xx*1930+np.sin(yy*2773))+.014*np.sin(yy*317+xx*79)
                    base=.38+grain;color[dst]=np.repeat(base[:,:,None],3,2)*np.array((.98,1,1.025));color[dst]*=(1-.94*pit[:,:,None]);color[dst]=color[dst]*(1-wear[:,:,None])+np.array((.90,.92,.94))*wear[:,:,None]
                    color[dst]=color[dst]*(1-.70*scratch[:,:,None])+np.array((.65,.67,.68))*.70*scratch[:,:,None];orm[dst]=np.stack((1-.4*pit,.48+.12*pit-.16*wear,.76+.19*wear),2)
                    # Local oxide at fasteners, never a uniform orange plate.
                    if kind=='plate_steel':
                        rust=np.zeros_like(xx)
                        for sx,sy,r in c.get('screws',[]):rust=np.maximum(rust,np.exp(-((np.hypot(xx-sx,yy-sy)/(r*2.1))**2)))
                        rust*=.45+.35*np.sin(xx*1337+yy*847)**2
                        color[dst]=color[dst]*(1-rust[:,:,None]*.55)+np.array((.23,.075,.023))*rust[:,:,None]*.55
                        orm[dst][:,:,1]+=rust*.13
                    # Retain the authored fastener/slot recess pattern, not just its broad colour.
                    dark=original.max(2)<.040;color[dst][dark]=original[dark]*1.4;orm[dst][:,:,0][dark]=.55
                else:
                    factor=.42 if 'Warning' in name else .70
                    color[dst]=original*factor;color[dst]=color[dst]*(1-wear[:,:,None])+np.array((.66,.69,.71))*wear[:,:,None];color[dst]*=1-.6*pit[:,:,None]
                    orm[dst][:,:,1]=np.clip(orm[dst][:,:,1]+(.15 if 'Warning' in name else .08)+.13*pit-.10*wear,.24,.88);orm[dst][:,:,2]=np.maximum(orm[dst][:,:,2],wear*.85)
                if kind=='slotted' and abs(wm-hm)<.001:
                    u=xx-wm/2;v=yy-hm/2;sl=(abs(u)<.010)*(abs(v)<.0015);color[dst]*=1-.95*sl[:,:,None];orm[dst][:,:,0]*=1-.7*sl
            for a in ([normal] if finish else [color,orm]):pad(a,x,y,w,h)
        if finish:save_map(path,normal)
        else:
            save_map(out/'textures'/f'T_{name}_Housing_BaseColor.png',color);save_map(out/'textures'/f'T_{name}_Housing_ORM.png',orm)

def rect(x,z,cx,cz,hx,hz,aa=.008):return np.clip(np.minimum(hx-np.abs(x-cx),hz-np.abs(z-cz))/aa+.5,0,1)
def ellipse(x,z,cx,cz,rx,rz,aa=.012):return np.clip((1-np.sqrt(((x-cx)/rx)**2+((z-cz)/rz)**2))/aa+.5,0,1)
def paint_layer(rgb,mask,tint):return rgb*(1-mask[:,:,None])+np.array(tint)*mask[:,:,None]

def optics(piece,objects,out):
    name=piece['mesh'].replace('SM_Kit_','');res=1024;cs=[c for o in objects for c in json.loads(o['chart_layout'])]
    for finish in (False,True):
        if finish:path=out/'source/bake_inputs'/f'T_{name}_Lens_Micro_Normal.png';normal=read(path)
        else:
            color=np.zeros((res,res,3),np.float32);orm=np.ones((res,res,3),np.float32);profile=np.zeros((res,res,3),np.float32)
        for ci,c in enumerate(cs):
            if c['slot']!=1:continue
            x,y,w,h,xx,yy,dst,scale=region(c,res,finish);wm,hm=[v/c.get('metric_factor',1.) for v in c['size']];u=xx/wm;v=yy/hm;height=np.zeros_like(u)
            if c.get('dome'):
                from lighting_393_glass import field
                rgb,rough,values,height=field(c,xx,yy,u,name,out)

            else:
                # Neutral diffusers: tint belongs solely to Emission Color.
                edge=np.minimum.reduce((xx,wm-xx,yy,hm-yy));atten=.3+.7*np.clip(edge/.010,0,1)
                centre=np.exp(-((u-.5)/.52)**4)*np.exp(-((v-.5)/.52)**4)
                if c['kind']=='diamond':
                    # A square-pyramid / diamond prismatic surface; LED dots seen behind it.
                    a=(xx+yy)/.0038;b=(xx-yy)/.0038;diamond=1-np.maximum(abs((a%1)-.5),abs((b%1)-.5))*2
                    height=.00020*diamond
                    dots=np.exp(-((((xx/.017)%1)-.5)/.17)**2-((((yy/.017)%1)-.5)/.17)**2)
                    rgb=np.repeat((.62+.15*diamond)[:,:,None],3,2);rough=.24+.10*(1-diamond)
                    values=np.repeat((atten*(.012+.50*centre**2.5)*(.70+.20*diamond+.10*dots))[:,:,None],3,2)
                else:
                    rib=(.5+.5*np.cos(yy*2*math.pi/.0032))**1.4;height=.00036*rib
                    rgb=np.repeat((.58+.23*rib)[:,:,None],3,2);rough=.23+.16*(1-rib)
                    core=np.exp(-((v-.5)/.30)**4)*np.exp(-((u-.5)/.66)**4)
                    values=np.repeat((atten*(.025+.90*core)*(.16+.84*rib))[:,:,None],3,2)
            if finish:normal[dst]=normal_from_height(height,scale,res)
            else:color[dst]=np.clip(rgb,0,1);orm[dst]=np.stack((np.ones_like(u),rough,np.zeros_like(u)),2);profile[dst]=values
            for array in ([normal] if finish else [color,orm,profile]):pad(array,x,y,w,h)
        if finish:save_map(path,normal)
        else:
            save_map(out/'textures'/f'T_{name}_Lens_BaseColor.png',color);save_map(out/'textures'/f'T_{name}_Lens_ORM.png',orm);data16(out/'textures'/f'T_{name}_Lens_LightProfile.png',profile)

def textures(piece,objects,out):
    records=make_textures(piece,objects,out);housing(piece,objects,out);optics(piece,objects,out)
    apply(piece,out,False);bake_finish(piece,objects,out);apply(piece,out,True)
    name=piece['mesh'].replace('SM_Kit_','')
    for surface,res in [('Housing',2048),('Lens',1024)]:records.append(dict(path=f'textures/T_{name}_{surface}_Finish_Normal.png',size=[res,res],slot=surface,map='Finish_Normal',srgb=False))
    return records
