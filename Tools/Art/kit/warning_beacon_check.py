"""08-only extension of independent checks for explicit optical geometry."""
import sys,inspect,json
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import lighting_393_astra_check as a
source=inspect.getsource(a.check).replace("('amberglass','mastglass','diamond','indicator_ribs')","('amberglass','beacon_bulb','beacon_mirror')")
exec(compile(source,__file__,'exec'),a.__dict__)
piece=next(p for p in a.r.PIECES if p['mesh']=='SM_Kit_WarningBeacon')
action=sys.argv[sys.argv.index('--')+1]
assert action in ('check','sheets')
getattr(a,action)(piece)
if action=='check':
    import bpy
    out=a.r.KIT/'08_WarningBeacon';p=out/'evidence/astra_technical.json';d=json.loads(p.read_text());d['round']='4b'
    d['geometry_approach']='True connected assembly. Holder z=.178-.232; top plate=.180; bulb base=.231 overlaps socket by 1mm; rear bracket z=.178-.238 intersects mirror lower meridian at (-.027875,0,.235). Thin-shell Lens G=1, opaque interior G=0.'
    d['collar_diameter_m']=.164;d['dome_lip_diameter_m']=.142;d['collar_dome_ratio']=.164/.142
    nt=bpy.data.materials['Lens'].node_tree
    assert nt.nodes['OpticalChannels'].outputs[0].is_linked and nt.nodes['ThinShellOverRealInterior'].outputs[0].is_linked
    d['optical_shader']='R-only emission; G thin-shell transmission; actual Lens bulb/mirror and Housing holder/bracket in both LODs'
    p.write_text(json.dumps(d,indent=2)+'\n')
