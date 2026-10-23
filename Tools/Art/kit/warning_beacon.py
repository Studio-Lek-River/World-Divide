import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
"""Round 4b: retained cast housing; moulded amber shell over a real grounded assembly."""
from lighting_393_common import Builder,build_pair
from wall_lamp_materials import bolt

def mesh(name,low):
    b=Builder(name);n=12 if low else 32
    b.box((0,0,.004),(.18,.18,.008),0 if low else .0015)
    b.lathe((0,0,0),(0,0,1),[(.008,.043),(.021,.043),(.027,.031),(.103,.031),(.109,.049)],n=n,kind='castiron')
    b.lathe((0,0,0),(0,0,1),[(.106,.073),(.111,.082),(.148,.082),(.151,.081),(.155,.081),(.158,.082),(.178,.082),(.180,.082)],n=n,kind='castiron')
    ch=b.charts[-3];ch['screws']=[[ch['size'][0]*q,v,.0048] for q in (.125,.375,.625,.875) for v in (.030,.075)]
    b.dome((0,0,0),[(.180,.070),(.185,.071),(.280,.069),(.300,.0606),(.31464,.035),(.31939,.01215)] if low else [(.180,.070),(.185,.071),(.280,.069),(.295,.065),(.307,.052),(.316,.032),(.3196,.011)],n=16 if low else 64,kind='amberglass',peak=.320)
    # Round 4b: real attached rotating assembly, both LODs.
    b.lathe((0,0,0),(0,0,1),[(.178,.020),(.220,.020),(.226,.014),(.232,.014)],n=6 if low else 16,kind='castiron')
    b.box((-.031,0,.208),(.008,.014,.060),kind='castiron')
    b.lathe((0,0,0),(0,0,1),[(.231,.006),(.240,.008),(.274,.008),(.283,.003)],n=6 if low else 16,slot=1,kind='beacon_bulb')
    start=len(b.f)
    b.lathe((-.034,0,.258),(1,0,0),[(0,.002),(.007,.026),(.026,.048)],n=8 if low else 24,slot=1,kind='beacon_mirror',cap=False)
    # Inner concave side faces the bulb and +X.
    for i in range(start,len(b.f)):
        b.f[i]=tuple(reversed(b.f[i]));ci,uv=b.uv[i];b.uv[i]=(ci,list(reversed(uv)))
    for x in (-.066,.066):
        for y in (-.066,.066):bolt(b,(x,y,.008),(0,0,1),.0075,n=6 if low else 12)
    return b.finish()

def build(piece):
    pair=build_pair(piece,mesh)
    from warning_beacon_finish import apply
    from wall_lamp_materials import KIT
    apply(piece,KIT/'08_WarningBeacon',True)
    return pair
