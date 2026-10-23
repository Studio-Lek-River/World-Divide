"""Continuous opaque seen-through-glass field; no photographic backdrop/window."""
import math
import numpy as np
import json
_CACHE={}

def ell(q,z,cx,cz,rx,rz,soft=.035):
    d=np.sqrt(((q-cx)/rx)**2+((z-cz)/rz)**2)
    return np.clip((1-d)/soft+.5,0,1)
def blend(rgb,mask,col):return rgb*(1-mask[:,:,None])+np.asarray(col)*mask[:,:,None]
def field(c,xx,yy,u,name,out):
    arc=np.asarray(c['meridian']);zp=np.asarray(c['z_profile']);z=(np.interp(yy,arc,zp)-zp[0])/(zp[-1]-zp[0]);theta=u*2*math.pi
    amber='Warning' in name;phase=0 if amber else -math.pi/4
    q=np.sin(theta-phase);face=np.abs(np.cos(theta-phase));edge=(1-face)**2
    hue=np.array((1.,.37,.012)) if amber else np.array((1.,.012,.023))
    # Every angle has bright coloured glass, even away from the projected interior.
    body=.38+.48*face+.055*np.sin(z*math.pi)
    rgb=body[:,:,None]*hue;emission=(.007+.010*face) if amber else .002+.004*face
    height=np.zeros_like(u);vertical=.5+.5*np.cos(xx*2*math.pi/.006)
    streak=np.exp(-((q-.76)/.032)**2)+.45*np.exp(-((q+.84)/.020)**2)
    streak*=np.clip((z-.08)/.10,0,1)*np.clip((.96-z)/.08,0,1)
    rgb=blend(rgb,np.clip(streak*.40,0,.6),tuple(hue*.8+np.array((.18,.08,.03))))
    height+=.000010*vertical;rgb*=1-.035*(1-vertical)[:,:,None]
    if amber:
        # Curved gold mirror and socket, both filtered by the same amber glass.
        d=np.sqrt((q/.75)**2+((z-.55)/.32)**2);disc=np.clip((1-d)/.07+.5,0,1)*.76
        curved=.63+.30*np.sqrt(np.maximum(0,1-d*d))+.055*np.sin(q*7+z*4)
        mirror=np.stack((curved,.36*curved,.020*curved),2)
        rgb=rgb*(1-disc[:,:,None])+mirror*disc[:,:,None]
        ring=np.exp(-((d-.98)/.035)**2)*.75;rgb=blend(rgb,ring*.42,(1,.63,.065))
        holder=ell(q,z,0,.20,.22,.19,.12);rgb=blend(rgb,holder*.80,(.47,.18,.014))
        pedestal=ell(q,z,0,.19,.34,.045,.12);rgb=blend(rgb,pedestal*.85,(.66,.31,.028))
        for zz in (.29,.32,.35):rgb=blend(rgb,ell(q,z,0,zz,.20,.012,.15)*.7,(.80,.43,.052))
        bulb=ell(q,z,0,.60,.18,.235,.09);rgb=blend(rgb,bulb*.65,(1,.68,.10))
        envelope=np.exp(-((np.sqrt((q/.18)**2+((z-.60)/.235)**2)-1)/.055)**2)
        rgb=blend(rgb,envelope*.48,(1,.82,.32))
        filament=np.exp(-((abs(q)-.038)/.012)**2)*np.exp(-((z-.62)/.155)**8)
        filament=np.maximum(filament,np.exp(-(q/.043)**8)*np.exp(-((z-.765)/.009)**2))
        hot=np.exp(-(q/.085)**2)*np.exp(-((z-.62)/.19)**4)
        rgb=blend(rgb,np.clip(filament*.9+hot*.25,0,1),(1,.94,.57))
        emission+=.008*disc+.045*np.exp(-(q/.44)**2-((z-.58)/.33)**2)+.60*filament+.28*hot
    else:
        # Cylindrical emitter, diameter .2 of the complete dome, not a flat grid.
        shell=np.exp(-(q/.195)**8)*np.exp(-((z-.55)/.30)**12)
        cylinder=.37+.30*np.sqrt(np.maximum(0,1-(q/.20)**2))
        col=np.stack((cylinder,.10*cylinder,.014*cylinder),2)
        rgb=rgb*(1-shell[:,:,None]*.75)+col*shell[:,:,None]*.75
        # A connected cylindrical pedestal and rounded carrier caps give apparent depth.
        stem=np.exp(-(q/.20)**8)*np.exp(-((z-.15)/.11)**12)
        metal=.27+.35*np.sqrt(np.maximum(0,1-(q/.205)**2))+.25*np.exp(-((q+.09)/.025)**2)
        stemcol=np.stack((metal,.09*metal,.025*metal),2);rgb=rgb*(1-stem[:,:,None]*.88)+stemcol*stem[:,:,None]*.88
        pedestal=ell(q,z,0,.20,.34,.040,.14);rgb=blend(rgb,pedestal*.88,(.58,.07,.028))
        seat=ell(q,z,0,.223,.29,.018,.16);rgb=blend(rgb,seat*.8,(.98,.22,.065))
        rgb=blend(rgb,ell(q,z,0,.84,.19,.022,.15)*.8,(.85,.18,.040))
        rgb=blend(rgb,ell(q,z,0,.265,.19,.018,.15)*.8,(.62,.085,.024))
        # Long curved orange metal reflection, not a rectangular display backing.
        shine=np.exp(-((q+.128)/.017)**2)*np.exp(-((z-.55)/.295)**12)
        rgb=blend(rgb,shine*.74,(1,.31,.068))
        side=np.exp(-((q-.172)/.018)**2)*np.exp(-((z-.55)/.285)**12)
        rgb=blend(rgb,side*.67,(.40,.038,.014))
        leds=np.zeros_like(q)
        for row in range(10):
            for cx in (-.070,.070):leds=np.maximum(leds,ell(q,z,cx,.30+row*.052,.029,.017,.4))
        rgb=blend(rgb,leds*.85,(1,.74,.24))
        hot=np.exp(-(q/.13)**2)*np.exp(-((z-.54)/.29)**8)
        emission+=.075*np.exp(-(q/.26)**2-((z-.55)/.34)**4)+.66*leds+.28*hot
        # Three moulded skirt rings: uninterrupted around the entire shell.
        band=np.clip((.19-z)/.055,0,1)
        rings=.5+.5*np.cos(yy*2*math.pi/.008)
        height+=.00011*band*rings
        rgb*=1-.38*band[:,:,None]*rings[:,:,None]
        emission*=1-.22*band*rings
        for zz in (.045,.10,.16):
            line=np.exp(-((z-zz)/.009)**2);rgb=blend(rgb,line*.38,(1,.055,.035));emission+=.012*line
    if amber:
        # Only raytraced interior luminance is borrowed: no dark backdrop or window.
        key=str(out)
        if key not in _CACHE:
            from lighting_393_finish import read
            _CACHE[key]=(read(out/'source/bake_inputs/optical_master_day.png'),json.loads((out/'source/bake_inputs/optical_projection.json').read_text()))
        photo,projection=_CACHE[key];ratio=np.interp(yy,arc,np.asarray(c['r_profile']))/c['radius']
        pu=projection['centre'][0]+q*ratio*(projection['right'][0]-projection['centre'][0]);pv=projection['bottom'][1]+z*(projection['top'][1]-projection['bottom'][1])
        ih,iw=photo.shape[:2];fx=np.clip(pu*(iw-1),0,iw-1.001);fy=np.clip(pv*(ih-1),0,ih-1.001);ix=fx.astype(int);iy=fy.astype(int);tx=(fx-ix)[:,:,None];ty=(fy-iy)[:,:,None]
        sampled=(photo[iy,ix]*(1-tx)+photo[iy,ix+1]*tx)*(1-ty)+(photo[iy+1,ix]*(1-tx)+photo[iy+1,ix+1]*tx)*ty
        luma=np.max(sampled,2);optical=np.stack((.32+.68*luma,.10+.48*luma**1.3,.008+.10*luma**2),2)
        mask=np.maximum(disc,np.maximum(holder,pedestal))*.94*(1-np.clip(filament,0,1)*.90)
        rgb=rgb*(1-mask[:,:,None])+optical*mask[:,:,None]
    # Sparse hairline abrasion; no cloudy rectangle around the interior.
    rng=np.random.default_rng(393);scuff=np.zeros_like(q)
    for _ in range(30):
        sx,sz=rng.uniform(-.95,.95),rng.uniform(.08,.94);length=rng.uniform(.015,.055)
        mark=np.exp(-((q-sx-(z-sz)*.4)/.0018)**4)*np.exp(-((z-sz)/length)**8);scuff=np.maximum(scuff,mark)
    height-=.000025*scuff;rgb=blend(rgb,scuff*.16,tuple(hue*.85+.09))
    rough=.12+.12*scuff+.025*edge
    return np.clip(rgb,0,1),rough,np.repeat(np.clip(emission,0,1)[:,:,None],3,2),height
