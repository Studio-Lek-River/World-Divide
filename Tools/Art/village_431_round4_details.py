"""Round4 construction overrides. Installed explicitly; no build at import."""
import math,random

def install(r):
    old_windows=r.windows;old_roof=r.roof;old_build=r.build

    def beam(b,face,a,z,da,dz,width=.09,slot='Wood',depth=.08,offset=-.12):
        # Closed rectangular diagonal in the facade plane, full thickness.
        length=math.hypot(da,dz);pa=-dz/length*width/2;pz=da/length*width/2
        before=set(b.mesh.verts)
        r.prism(b,face,[(a+pa,z+pz),(a+da+pa,z+dz+pz),(a+da-pa,z+dz-pz),(a-pa,z-pz)],slot,depth)
        axis,side,other=r.g.FACES[face]
        for v in set(b.mesh.verts)-before:v.co[axis]+=side*offset

    def windows(b,ops,state,key):
        before=set(b.mesh.faces)
        old_windows(b,[q for q in ops if key!='Church' or q[5]!='window'],state,key)
        if key=='Barn':
            for f in set(b.mesh.faces)-before:
                if f.material_index==2:f.material_index=4
        if key=='Church':
            for face,a,z,w,h,kind,floor in ops:
                if kind!='window':continue
                radius=w/2;spring=z+h-radius
                r.fb(b,face,a,z,z+h,w/2,-1.05,-1,'Wood')
                r.fb(b,face,a,z+.08,z+h-.08,w/2-.07,-.20,-.17,'Glass')
                for side in [-1,1]:
                    r.fb(b,face,a+side*(w/2-.055),z,spring,.055,-.07,.045,'Rubble')
                    points=[(a+side*radius,z+h),(a,z+h)]
                    points += [(a+side*radius*math.sin(t*math.pi/8),spring+radius*math.cos(t*math.pi/8)) for t in range(1,5)]
                    r.prism(b,face,points,'Brick')
                    for j in range(4):
                        t=j*math.pi/8;u=(j+1)*math.pi/8
                        beam(b,face,a+side*radius*math.sin(t),spring+radius*math.cos(t),side*radius*(math.sin(u)-math.sin(t)),radius*(math.cos(u)-math.cos(t)),.11,'Rubble',.12,.045)
                r.fb(b,face,a,z+.10,spring+.25,.035,-.10,-.035,'Trim')
                r.fb(b,face,a,z-.10,z-.015,w/2+.1,-.03,.12,'Rubble')
        if key=='Barn':
            for face,a,z,w,h,kind,floor in ops:
                if kind!='door' and floor!=1:continue
                # Pair of timber leaves overlays the old inset door panel.
                for side in [-1,1]:
                    r.fb(b,face,a+side*w*.24,z+.12,z+h-.12,w*.235,-.20,-.16,'Wood')
                    beam(b,face,a+side*.12,z+.28,side*(w*.44-.12),h-.58,.12)
                r.fb(b,face,a,z+.1,z+h-.1,.025,-.15,-.12,'Wood')
                for zz in [z+.35,z+h-.4]:
                    for side in [-1,1]:r.fb(b,face,a+side*w*.32,zz,zz+.09,w*.12,-.15,-.12,'Trim')
                if floor==1:
                    r.fb(b,face,a,z+h+.15,z+h+.34,.12,0,1.1,'Wood')
                    r.fb(b,face,a,z+h-.8,z+h+.15,.027,.95,1.005,'Trim')
                    r.fb(b,face,a,z+h-.95,z+h-.75,.09,.89,1.07,'Wood')
            # Board-and-batten relief, clipped around every existing aperture.
            for face in r.g.FACES:
                extent=r.span(face);count=int(extent*2/.80)
                for j in range(1,count):
                    a=-extent+j*2*extent/count;intervals=[(.45,r.SPEC['eaves']-.15)]
                    for f,x,z,w,h,k,fl in ops:
                        if f==face and abs(a-x)<w/2+.10:
                            intervals=[v for lo,hi in intervals for v in [(lo,min(hi,z-.08)),(max(lo,z+h+.12),hi)] if v[1]-v[0]>.12]
                    if state=='Damaged' and face=='-Y' and -4.8<a<-.8:continue
                    for lo,hi in intervals:r.fb(b,face,a,lo,hi,.045,.005,.065,'Wood')
        if key=='Workshop':
            for face,a,z,w,h,kind,floor in ops:
                if kind=='door' and w>2.5:
                    r.fb(b,face,a,z+h+.10,z+h+.50,w/2+.15,0,.35,'Trim')
                    for side in [-1,1]:r.fb(b,face,a+side*(w/2+.08),.05,z+h,.055,0,.16,'Trim')
                    for zz in [.3,.9,1.5,2.1,2.7,3.3,3.8]:r.fb(b,face,a,zz,zz+.045,w/2-.12,-.15,-.12,'Trim')
            # Personnel door in a free bay; a shallow inset framed construction,
            # original firing apertures remain untouched.
            r.fb(b,'+Y',5.4,.12,2.45,.48,.01,.04,'Wood');r.ring(b,'+Y',5.4,.08,1.04,2.46,.09,.04,.10)
            r.fb(b,'+Y',5.73,1.10,1.20,.055,.10,.18,'Trim')
            r.fb(b,'+X',0,4.65,5.15,1.35,.03,.11,'Wood')
            # Low industrial skirt with profiled ribs between openings.
            for face in ['+Y','-Y']:
                sections=[(-8,-3.9),(-.2,8)] if state=='Damaged' and face=='-Y' else [(-8,8)]
                for lo,hi in sections:r.fb(b,face,(lo+hi)/2,.45,2.15,(hi-lo)/2,.001,.025,'Trim')
                for j in range(26):
                    a=-7.6+j*.6
                    if state=='Damaged' and face=='-Y' and -3.9<a<-.2:continue
                    r.fb(b,face,a,.45,2.15,.035,.005,.065,'Trim')

    def roof(b,state,key):
        old_roof(b,state,key)
        hl=r.SPEC['length']/2;hw=r.SPEC['width']/2;ridge=r.SPEC['ridge'];eave=r.SPEC['eaves']
        # A faceted ridge cap has a curved silhouette without hundreds of tiles.
        h0=hl*.05;h1=h0+min(4,r.SPEC['length']*.3)
        if key in ['Farmhouse','Church']:h0-=1.1;h1+=.8
        ranges=[(-hl-.3,hl+.3)] if state!='Damaged' else [(-hl-.3,h0),(h1,hl+.3)]
        for lo,hi in ranges:
            if hi-lo<.2:continue
            profile=[(-.17,0),(-.12,.12),(0,.17),(.12,.12),(.17,0)]
            layers=[[b.mesh.verts.new((x,y,ridge+.15+z)) for y,z in profile] for x in [lo,hi]]
            faces=[b.mesh.faces.new(layers[0][::-1]),b.mesh.faces.new(layers[1])]
            for i in range(5):j=(i+1)%5;faces.append(b.mesh.faces.new([layers[0][i],layers[0][j],layers[1][j],layers[1][i]]))
            r.bmesh.ops.recalc_face_normals(b.mesh,faces=faces)
            for f in faces:f.material_index=1
        if key=='Workshop':
            slope=(ridge-eave)/hw;ang=math.degrees(math.atan(slope))
            # Glazed ridge-side skylight strip, dark insulated glazing and frames.
            for side in [-1,1]:
                x=-4.8;y=side*2.6;z=ridge-abs(y)*slope+.18
                b.turned((x,y,z),(3.4,1.1,.14),(-side*ang,0,0),'Trim')
                b.turned((x,y,z+.085),(3.16,.91,.04),(-side*ang,0,0),'Wood')
                b.turned((x,y,z+.13),(3.16,.91,.04),(-side*ang,0,0),'Glass')
                for dx in [-.8,0,.8]:b.turned((x+dx,y,z+.13),(.05,1.0,.06),(-side*ang,0,0),'Trim')
        if key in ['HouseB','HouseC']:
            # Window boxes on the ground-floor street side, outside firing rays.
            ops,_=r.openings(key)
            for face,a,z,w,h,kind,floor in [q for q in ops if q[0]=='+X' and q[5]=='window' and q[6]==0][:2]:
                r.fb(b,face,a,z-.28,z-.07,w*.45,.10,.38,'Wood')
                r.fb(b,face,a,z-.07,z-.03,w*.42,.13,.35,'Brick')
            if key=='HouseB':
                # Compact bin cabinet within the existing projecting door-step envelope.
                b.box((6.92,-3.8,.08),(7.45,-2.6,1.12),'Wood')
                b.box((6.9,-3.85,1.12),(7.49,-2.55,1.20),'Trim')

    def tower(b,state):
        nave=r.SPEC;side,height,spire=nave['tower'];cx=-nave['length']/2-side/2
        top=height if state=='Pristine' else (height-6 if state=='Damaged' else height/3)
        before=set(b.mesh.verts);r.SPEC={'length':side,'width':side,'eaves':top,'ridge':top}
        ops=[('-X',0,.05,1.9,3.7,'door',0)] if state!='Rubble' else []
        if state=='Pristine':ops += [(f,0,20,1.8,3,'window',6) for f in r.g.FACES]
        r.shell(b,ops,'Pristine')
        if state!='Pristine':
            # Warp only the complete upper masonry rim into an asymmetric break.
            # Thick walls remain closed and none of the old square merlons survive.
            for v in set(b.mesh.verts)-before:
                if abs(v.co.z-top)<.01:
                    v.co.z+=.65*math.sin(v.co.x*1.3+v.co.y*.8)+.48*math.cos(v.co.y*1.9)-.4
            for face in r.g.FACES:
                for j in range(3):
                    a=-2.6+j*2.1
                    r.prism(b,face,[(a,top-.8),(a+.65,top-.8),(a+.50,top+.4+(j%2)*.4)],'Brick')
        if state!='Rubble':
            start=set(b.mesh.faces)
            r.ring(b,'-X',0,.05,1.9,3.7,.16,-.05,.08)
            for f in set(b.mesh.faces)-start:f.material_index=5
            r.fb(b,'-X',0,.15,3.62,.78,-.22,-.18,'Wood')
            r.fb(b,'-X',0,.15,3.62,.025,-.18,-.12,'Trim')
            r.fb(b,'-X',0,3.82,4.12,1.2,-.02,.26,'Rubble')
            r.fb(b,'-X',0,0,.15,1.25,0,.65,'Rubble')
        for face in r.g.FACES:
            gaps=[(-1,1)] if face=='-X' and state!='Rubble' else []
            for a,c in ([(-3.25,-1),(1,3.25)] if gaps else [(-3.25,3.25)]):r.fb(b,face,(a+c)/2,-.5,.85,(c-a)/2,.005,.16,'Rubble')
            for z in [8.2,17.7,25.3]:
                if z+.3<top-1:r.fb(b,face,0,z,z+.30,3.38,.005,.20,'Rubble')
            for a in [-3.08,3.08]:
                for j in range(int((top-1)/1.25)):
                    z=.9+j*1.25;r.fb(b,face,a,z,z+.46,.18 if j%2 else .30,.005,.12,'Rubble')
            if state=='Pristine':
                r.ring(b,face,0,20,1.8,3,.13)
                r.fb(b,face,0,20,23,.9,-.6,-.55,'Wood')
                for zz in [20.25,20.75,21.25,21.75,22.25,22.75]:r.fb(b,face,0,zz,zz+.12,.78,-.1,.08,'Wood')
                # Recessed square clock surround with actual raised hour hands.
                r.fb(b,face,0,14.1,16.7,1.30,.005,.10,'Rubble')
                r.fb(b,face,0,14.3,16.5,1.10,.10,.13,'Wood')
                for aa,zz,ww,hh in [(0,16.17,.055,.22),(0,14.41,.055,.22),(-.93,15.35,.12,.06),(.93,15.35,.12,.06)]:r.fb(b,face,aa,zz,zz+hh,ww,.13,.16,'Trim')
                beam(b,face,0,15.4,.65,.36,.08,'Trim',.045,.18);beam(b,face,0,15.4,0,.80,.075,'Trim',.045,.18)
        if state=='Pristine':b.pyramid((0,0),side/2+.18,top,spire,'Roof')
        for v in set(b.mesh.verts)-before:v.co.x+=cx
        r.SPEC=nave;b.collision.append(((cx-side/2,-side/2,-.5),(cx+side/2,side/2,top)))
        if state!='Pristine':
            rng=random.Random(994)
            for j in range(12):r.r2.fragment(b,(cx+rng.uniform(-4,1),rng.uniform(-4,4),rng.uniform(.15,.7)),(rng.uniform(.5,1.3),.6,.5),(j*11,j*7,j*23),'Brick' if j%3 else 'Rubble')

    r.windows=windows;r.roof=roof;r.church_tower=tower
