"""08-only Round 4b: real optics; thin amber shell, no projected interior."""
import bpy,json,math
import numpy as np
from lighting_393_finish import region,pad,read,data16,normal_from_height,housing
from lighting_393_finish import apply as original_apply
from wall_lamp_materials import make_textures,save_map,bake_finish

def apply(piece,out,finished=True):
    assert piece['mesh']=='SM_Kit_WarningBeacon'
    original_apply(piece,out,finished)
    nt=bpy.data.materials['Lens'].node_tree;ns=nt.nodes;ln=nt.links
    p=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
    profile=next(n for n in ns if n.type=='TEX_IMAGE' and 'LightProfile' in n.image.name)
    sep=ns.new('ShaderNodeSeparateColor');sep.name='OpticalChannels';ln.new(profile.outputs['Color'],sep.inputs[0])
    # R is energy, G identifies the moulded thin shell. B reserved, zero.
    for link in list(profile.outputs['Color'].links):
        if link.to_node!=sep:ln.new(sep.outputs[0],link.to_socket)
    ns['CoreColour'].inputs[2].default_value=(1,.92,.48,1)
    ns['LensTint'].outputs[0].default_value=(1,.22,.001,1)
    p.inputs['Coat Weight'].default_value=.3;p.inputs['Coat Roughness'].default_value=.035
    p.inputs['Specular IOR Level'].default_value=.5
    base=next(n for n in ns if n.type=='TEX_IMAGE' and 'BaseColor' in n.image.name)
    dark=ns.new('ShaderNodeMixRGB');dark.blend_type='MULTIPLY';ln.new(sep.outputs[1],dark.inputs[0]);ln.new(base.outputs['Color'],dark.inputs[1]);dark.inputs[2].default_value=(.18,.18,.18,1);ln.new(dark.outputs[0],p.inputs['Base Color'])
    trans=ns.new('ShaderNodeBsdfTransparent');trans.name='AmberThinTransmission'
    absorption=ns.new('ShaderNodeGamma');absorption.name='ThinWallAbsorption';absorption.inputs['Gamma'].default_value=.45;ln.new(base.outputs['Color'],absorption.inputs['Color']);ln.new(absorption.outputs[0],trans.inputs['Color'])
    fres=ns.new('ShaderNodeFresnel');fres.inputs['IOR'].default_value=1.49
    nm=next(n for n in ns if n.type=='NORMAL_MAP');ln.new(nm.outputs['Normal'],fres.inputs['Normal'])
    # Retain a little scatter in the body; strong reflected glass at grazing angles.
    inv=ns.new('ShaderNodeMath');inv.operation='SUBTRACT';inv.inputs[0].default_value=1;ln.new(fres.outputs[0],inv.inputs[1])
    fac=ns.new('ShaderNodeMath');fac.operation='MULTIPLY';ln.new(inv.outputs[0],fac.inputs[0]);ln.new(sep.outputs[1],fac.inputs[1])
    gain=ns.new('ShaderNodeMath');gain.operation='MULTIPLY';gain.inputs[1].default_value=.91;ln.new(fac.outputs[0],gain.inputs[0])
    mix=ns.new('ShaderNodeMixShader');mix.name='ThinShellOverRealInterior';ln.new(gain.outputs[0],mix.inputs[0]);ln.new(p.outputs[0],mix.inputs[1]);ln.new(trans.outputs[0],mix.inputs[2])
    outnode=next(n for n in ns if n.type=='OUTPUT_MATERIAL');ln.new(mix.outputs[0],outnode.inputs['Surface'])

def optics(piece,objects,out):
    res=1024;name='WarningBeacon';cs=[c for o in objects for c in json.loads(o['chart_layout'])]
    for finish in (False,True):
        if finish:
            path=out/'source/bake_inputs/T_WarningBeacon_Lens_Micro_Normal.png';normal=read(path)
        else:
            color=np.zeros((res,res,3),np.float32);orm=np.ones_like(color);profile=np.zeros_like(color)
        for ci,c in enumerate(cs):
            if c['slot']!=1:continue
            x,y,w,h,xx,yy,dst,scale=region(c,res,finish)
            wm,hm=[v/c.get('metric_factor',1.) for v in c['size']];u=xx/wm;v=yy/hm
            height=np.zeros_like(u);rough=np.full_like(u,.10);metal=np.zeros_like(u);values=np.zeros((h,w,3),np.float32)
            if c.get('dome'):
                z=np.interp(yy,c['meridian'],c['z_profile']);rim=np.exp(-((z-.184)/.0035)**2)
                rib=.5+.5*np.cos(xx*2*math.pi/.006)
                height=.00010*rib+.00025*rim
                rng=np.random.default_rng(39308);scuff=np.zeros_like(u)
                for j in range(160):
                    sx,sy=rng.random(2)*[wm,hm];length=rng.uniform(.002,.014)
                    mark=np.exp(-((xx-sx-(yy-sy)*.5)/.00065)**2)*np.exp(-((yy-sy)/length)**8)
                    scuff=np.maximum(scuff,mark)
                height-=.00010*scuff;rough=.075+.38*scuff+.02*rim
                rgb=np.ones((h,w,3))*np.array((.97,.66,.20))
                rgb*= (1-.20*rim-.035*(1-rib))[:,:,None]
                # Amber scatter extends to the rim; not an image of the interior.
                values[:,:,0]=.008+.018*np.exp(-((z-.259)/.054)**2)
                values[:,:,1]=1
                rgb=rgb*(1-.50*scuff[:,:,None])+np.array((1,.83,.50))*.50*scuff[:,:,None]
            elif c['kind']=='beacon_bulb':
                core=.045+.955*np.exp(-(np.cos(u*2*math.pi)/.38)**4)*np.exp(-((v-.55)/.40)**8)
                values[:,:,0]=core
                rgb=np.ones((h,w,3))*np.array((1,.70,.20));rough[:]=.15
            elif c['kind']=='beacon_mirror':
                rgb=np.ones((h,w,3))*np.array((.95,.80,.42));metal[:]=1;rough[:]=.16
                # Reflected radiance from the compact bulb. Spatially curved, not flat fill.
                values[:,:,0]=.003+.012*np.exp(-((u-.5)/.44)**4)*np.exp(-((v-.40)/.30)**2)
            else:raise AssertionError(c['kind'])
            if finish:normal[dst]=normal_from_height(height,scale,res)
            else:
                color[dst]=rgb;orm[dst]=np.stack((np.ones_like(u),rough,metal),2);profile[dst]=values
            for a in ([normal] if finish else [color,orm,profile]):pad(a,x,y,w,h)
        if finish:save_map(path,normal)
        else:
            for label,a in [('BaseColor',color),('ORM',orm)]:save_map(out/'textures'/f'T_{name}_Lens_{label}.png',a)
            data16(out/'textures'/f'T_{name}_Lens_LightProfile.png',profile)

def textures(piece,objects,out):
    records=make_textures(piece,objects,out);housing(piece,objects,out);optics(piece,objects,out)
    apply(piece,out,False);bake_finish(piece,objects,out);apply(piece,out,True)
    for surface,res in [('Housing',2048),('Lens',1024)]:records.append(dict(path=f'textures/T_WarningBeacon_{surface}_Finish_Normal.png',size=[res,res],slot=surface,map='Finish_Normal',srgb=False))
    return records
