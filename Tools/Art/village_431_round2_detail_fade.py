"""Fade sub-10cm normal/AO atlas detail below a pixel before the 260m LOD.
The reduced mesh's merged surfaces cross unique atlas charts; scans and material
slots remain the same, while near-only bevel/AO detail fades from 180 to 230m.
"""
import sys,json
sys.dont_write_bytecode=True
import village_431_round2_preview as w
v=w.v
def main():
    v.p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'r2detailfade','version':'1'}});v.guard();assert not v.call('EditorAppToolset','GetOpenAssets')
    master=w.CAND+'/M_Round2_Surface';expressions=v.call('MaterialTools','get_expressions',material_or_function=v.ref(master));strength=None
    for e in expressions:
        if 'ScalarParameter' in e['refPath']:
            name=v.get(e['refPath'],'parameterName')['parameterName']
            if name=='DetailFadeStartCm':print('Detail fade already installed');return
            if name=='BakeStrength':strength=e['refPath']
    assert strength
    def node(kind,**values):
        e=v.call('MaterialTools','add_expression',material_or_function=v.ref(master),expression_class=v.ref('/Script/Engine.MaterialExpression'+kind))['refPath']
        if values:v.props(e,**values)
        return e
    def link(a,b,pin):
        names=v.call('MaterialTools','get_expression_input_names',expression=v.ref(b))
        if pin not in names and len(names)==1:pin=names[0]
        v.call('MaterialTools','connect_expressions',from_expression=v.ref(a),from_output_name='',to_expression=v.ref(b),to_input_name=pin)
    camera=node('CameraPositionWS');center=node('ObjectPositionWS');distance=node('Distance');link(camera,distance,'A');link(center,distance,'B')
    start=node('ScalarParameter',parameterName='DetailFadeStartCm',defaultValue=18000)
    subtract=node('Subtract');link(distance,subtract,'A');link(start,subtract,'B')
    scale=node('Multiply',constB=1/5000);link(subtract,scale,'A')
    saturate=node('Saturate');link(scale,saturate,'Input')
    invert=node('OneMinus');link(saturate,invert,'Input')
    weight=node('Multiply');link(invert,weight,'A');link(strength,weight,'B')
    rewired=[]
    for e in expressions:
        if 'LinearInterpolate' not in e['refPath']:continue
        for inp in v.call('MaterialTools','get_expression_inputs',material_or_function=v.ref(master),expression=e):
            if inp['expression']['refPath']==strength:
                link(weight,e['refPath'],inp['input_name']);rewired.append(e['refPath'])
    assert len(rewired)==2
    v.props(master,twoSided=False);v.call('MaterialTools','recompile',material_or_function=v.ref(master));v.call('AssetTools','save_assets',asset_paths=[master])
    (w.OUT/'evidence/detail_fade.json').write_text(json.dumps({'start_cm':18000,'end_cm':23000,'distance':'camera to bounds centre','channels':'unique geometric normal and AO only; scanned detail and tint remain','reason':'Subpixel fine detail; prevent LOD1 merged faces interpolating across incompatible near-detail atlas charts','rewired':rewired},indent=2));print('Installed 180-230m atlas detail fade',flush=True)
if __name__=='__main__':main()
