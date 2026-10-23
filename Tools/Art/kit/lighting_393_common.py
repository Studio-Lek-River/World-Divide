"""Issue 393 / Round 3 deterministic geometry and LOD chart ownership."""
import sys,json,math
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy
from mathutils import Vector
from wall_lamp_materials import Builder as BaseBuilder,pack_lod_pair,KIT

class Builder(BaseBuilder):
    def dome(self,center,rings,n=32,kind='beacon',half=None,peak=None):
        cx,cy,cz=center;vs=[];fs=[];uv=[];radius=rings[0][1];arc=[0.]
        for (z,r),(zz,rr) in zip(rings,rings[1:]):arc.append(arc[-1]+math.hypot(zz-z,rr-r))
        for z,r in rings:vs += [(cx+r*math.cos(2*math.pi*s/n),cy+r*math.sin(2*math.pi*s/n),cz+z) for s in range(n)]
        for j in range(len(rings)-1):
            for s in range(n):
                t=(s+1)%n;fs.append((j*n+s,j*n+t,(j+1)*n+t,(j+1)*n+s))
                uv.append([(2*math.pi*radius*s/n,arc[j]),(2*math.pi*radius*(s+1)/n,arc[j]),(2*math.pi*radius*(s+1)/n,arc[j+1]),(2*math.pi*radius*s/n,arc[j+1])])
        peak_z=peak if peak is not None else rings[-1][0]+rings[-1][1]*.14
        apex=len(vs);vs.append((cx,cy,cz+peak_z));top=arc[-1]+math.hypot(peak_z-rings[-1][0],rings[-1][1])
        for s in range(n):
            fs.append(((len(rings)-1)*n+s,(len(rings)-1)*n+(s+1)%n,apex))
            uv.append([(2*math.pi*radius*s/n,arc[-1]),(2*math.pi*radius*(s+1)/n,arc[-1]),(2*math.pi*radius*(s+.5)/n,top)])
        ci=len(self.charts);self.add(vs,fs,1,uv,smooth=True,kind=kind)
        self.charts[ci].update(dome=True,half=half,meridian=arc+[top],z_profile=[z for z,r in rings]+[peak_z],r_profile=[r for z,r in rings]+[0.],radius=radius,center=list(center))
    def finish(self):
        factor=(.22 if 'Dual' in self.name else .36) if 'MastBeacon' in self.name else (.70 if 'ServiceBay' in self.name else (.42 if 'Warning' in self.name else 1.))
        for c in self.charts:
            if c['slot']==1:c['size']=[v*factor for v in c['size']];c['metric_factor']=factor
        self.uv=[(ci,[(u*(factor if self.charts[ci]['slot']==1 else 1),v*(factor if self.charts[ci]['slot']==1 else 1)) for u,v in coords]) for ci,coords in self.uv]
        return super().finish()

def dual_charts(objects):
    # Preserve the blink half in BOTH LODs and channels, without sharing bake texels.
    for lod,o in enumerate(objects):
        cs=json.loads(o['chart_layout']);old=json.loads(o['chart_layout'])
        for c in cs:
            if c['slot']==1:
                for pre in ('','finish_'):
                    c[pre+'offset']=[.025+.5*c['half'],.025+.5*lod];c[pre+'scale']=min(.44/c['size'][0],.44/c['size'][1])
        for f in o.data.polygons:
            ci=o.data.attributes['atlas_chart'].data[f.index].value;c=cs[ci];previous=old[ci]
            if c['slot']==1:
                for channel,pre in [(0,''),(1,'finish_')]:
                    for li in f.loop_indices:
                        p=(o.data.uv_layers[channel].data[li].uv-Vector(previous[pre+'offset']))/previous[pre+'scale']
                        o.data.uv_layers[channel].data[li].uv=p*c[pre+'scale']+Vector(c[pre+'offset'])
        o['chart_layout']=json.dumps(cs)

def build_pair(piece,mesh):
    bpy.context.scene.unit_settings.system='METRIC'
    a=mesh(piece['mesh'],False);b=None
    if piece['class']!='S':
        b=mesh(piece['mesh']+'_LOD1',True);pack_lod_pair(a,b);b.hide_render=True
    if 'Dual' in piece['mesh']:dual_charts([a,b])
    out=KIT/piece['id'].split('/')[-1]
    if (out/'material_recipe.json').exists():
        from lighting_393_finish import apply
        apply(piece,out,True)
    return a,b

def tetra_tab(b,center,angle,scale=1):
    # Closed four-triangle clamp tab: its silhouette survives in the small LOD.
    r=.22*scale;w=.020*scale;z=.208*scale;h=.030*scale;d=.014*scale
    u=Vector((math.cos(angle),math.sin(angle),0));t=Vector((-u.y,u.x,0));c=Vector(center)+u*r+Vector((0,0,z))
    vs=[c-t*w-Vector((0,0,h/2)),c+t*w-Vector((0,0,h/2)),c+Vector((0,0,h/2)),c+u*d]
    b.add(vs,[(0,2,1),(0,1,3),(1,2,3),(2,0,3)],kind='clamp')

def mast_head(b,y,low=False,scale=1,dual=False):
    # Shared physical head; only circumference and meridian sampling vary by LOD.
    n=16 if low else (24 if dual else 32);s=scale;c=(0,y,.030 if dual else 0)
    def rings(ps):return [(z*s,r*s) for z,r in ps]
    if low and dual:
        # Dome keeps 16 meridians; broad shoulder + shallow pole fan, cast collar 16.
        b.lathe(c,(0,0,1),rings([(-.040,.070),(.025,.098),(.115,.075)]),n=6,kind='gland',cap=False)
        b.lathe(c,(0,0,1),rings([(.110,.075),(.170,.22),(.230,.22)]),n=n,kind='castiron',cap='front')
        for a in (0,math.pi/2,math.pi,3*math.pi/2):tetra_tab(b,c,a,s)
        b.dome(c,rings([(.23,.17),(.500,.165)]),n=n,kind='mastglass',half=0 if y<0 else 1,peak=.56*s)
    else:
        b.lathe(c,(0,0,1),rings([(-.040,.070),(.015,.070),(.020,.098),(.045,.098),(.115,.075)] if dual else [(0,.070),(.040,.070),(.055,.098),(.080,.098),(.115,.075)]),n=6 if dual else (8 if low else 16),kind='gland')
        b.lathe(c,(0,0,1),rings([(.110,.075),(.170,.22),(.215,.22),(.230,.205)] if dual else [(.110,.075),(.160,.205),(.170,.22),(.215,.22),(.230,.205)]),n=20 if dual else n,kind='castiron',cap='front')
        for a in (0,math.pi/2,math.pi,3*math.pi/2):
            u=Vector((math.cos(a),math.sin(a),0));p=Vector(c)+u*(.219*s)+Vector((0,0,.205*s))
            # Bolt tab boxes have true overhang; fine socket goes in the finish.
            from mathutils import Matrix
            tr=Matrix.Translation(p)@Matrix.Rotation(a,4,'Z')
            b.box((0,0,0),(.024*s,.038*s,.040*s),0,transform=tr,kind='clamp')
        b.dome(c,rings([(.23,.17),(.490,.165),(.520,.150),(.542,.115),(.555,.065),(.559,.020)] if not low else [(.23,.17),(.490,.165),(.545,.095)]),n=n,kind='mastglass',half=0 if y<0 else 1,peak=.56*s)
    # Painted fasteners are on the vertical cast band.
    for ch in b.charts:
        if ch['kind']=='castiron' and ch['size'][0]>.3*s:
            ch['screws']=[[ch['size'][0]*q,ch['size'][1]*.82,.010*s] for q in (.125,.375,.625,.875)]
