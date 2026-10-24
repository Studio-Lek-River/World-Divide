"""Native MCP Round2 shader/bind pass. Requires no asset/material editor open.
Extends the current shared Surface parent only inside Round2; adds atlas maps.
"""
import sys,json
from pathlib import Path
sys.dont_write_bytecode=True
import village_431_round2_preview as w
v=w.v;OUT=w.OUT;CAND=w.CAND

def main():
    v.p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'r2materials','version':'1'}});v.guard()
    master=CAND+'/M_Round2_Surface'
    for f in sorted((OUT/'textures').glob('SM_*.png')):
        path=CAND+'/Textures/'+f.stem
        if not v.call('AssetTools','exists',path=path):v.call('TextureTools','import_file',folder_path=CAND+'/Textures',asset_name=f.stem,source_file=str(f))
        # Object-space normals are encoded RGB, not UE tangent-normal compression.
        v.props(path,sRGB=False,compressionSettings='TC_Masks',maxTextureSize=2048)
        v.call('AssetTools','save_assets',asset_paths=[path]);print('Imported atlas',f.name,flush=True)
    if not v.call('AssetTools','exists',path=master):
        v.call('AssetTools','duplicate',path='/Game/Art/Shared/Materials/Building/M_Bld_Surface',new_path=master)
    if not v.call('AssetTools','exists',path=CAND+'/Materials/MI_Pristine_Brick'):
        for expression in v.call('MaterialTools','get_expressions',material_or_function=v.ref(master)):
            v.call('MaterialTools','delete_expression',material_or_function=v.ref(master),expression=expression)
        def node(kind,**values):
            e=v.call('MaterialTools','add_expression',material_or_function=v.ref(master),expression_class=v.ref('/Script/Engine.MaterialExpression'+kind))['refPath']
            if values:v.props(e,**values)
            return e
        def link(a,b,pin,out=''):
            names=v.call('MaterialTools','get_expression_input_names',expression=v.ref(b))
            if pin not in names and len(names)==1:pin=names[0]
            assert pin in names,(b,pin,names)
            v.call('MaterialTools','connect_expressions',from_expression=v.ref(a),from_output_name=out,to_expression=v.ref(b),to_input_name=pin)
        def output(a,pin,out=''):v.call('MaterialTools','connect_to_output',expression=v.ref(a),output_name=out,material_property='MP_'+pin)
        def scalar(value):return node('Constant',r=value)
        def vec(x,y,z):return node('Constant3Vector',constant={'r':x,'g':y,'b':z,'a':1})
        def param(name,value):return node('ScalarParameter',parameterName=name,defaultValue=value)
        def multiply(a,b):
            n=node('Multiply');link(a,n,'A');link(b,n,'B');return n
        def lerp(a,b,alpha):
            n=node('LinearInterpolate');link(a,n,'A');link(b,n,'B');link(alpha,n,'Alpha');return n
        uv1=node('TextureCoordinate',coordinateIndex=1)
        def tex(name,file,sampler='Masks',atlas=False):
            n=node('TextureSampleParameter2D',parameterName=name,texture=v.ref(CAND+'/Textures/'+file),samplerType='SAMPLERTYPE_'+sampler)
            if atlas:link(uv1,n,'UVs')
            return n
        base=tex('BaseColor','Bricks085_BaseColor','Color');normal=tex('Normal','Bricks085_Normal','Normal');orm=tex('ORM','Bricks085_ORM')
        tint=tex('AtlasTint','SM_Village_HouseA_Tint',atlas=True);bnormal=tex('BakeNormal','SM_Village_HouseA_BakeNormal',atlas=True);ao=tex('BakeAO','SM_Village_HouseA_BakeAO',atlas=True)
        use=param('UseScan',1);strength=param('BakeStrength',1)
        color=node('VectorParameter',parameterName='SurfaceTint',defaultValue={'r':1,'g':1,'b':1,'a':1})
        output(multiply(multiply(lerp(vec(1,1,1),base,use),color),tint),'BaseColor')
        # Decode the object-space master normal, then transform into UE's tangent
        # frame before adding the scanned micro-normal. No UV1 tangent assumption.
        decode=node('Multiply',constB=2);link(bnormal,decode,'A','RGB')
        bias=node('Add',constB=-1);link(decode,bias,'A')
        transform=node('Transform',transformSourceType='TRANSFORMSOURCE_Local',transformType='TRANSFORM_Tangent');link(bias,transform,'Input')
        detail=multiply(normal,use);detailbias=node('Subtract');link(detail,detailbias,'A');link(multiply(vec(0,0,1),use),detailbias,'B')
        add=node('Add');link(lerp(vec(0,0,1),transform,strength),add,'A');link(detailbias,add,'B')
        normalize=node('Normalize');link(add,normalize,'VectorInput');output(normalize,'Normal')
        flatrough=param('FlatRoughness',.55)
        rough=node('LinearInterpolate');link(flatrough,rough,'A');link(orm,rough,'B','G');link(use,rough,'Alpha');output(rough,'Roughness')
        ormAO=node('ComponentMask',r=True);link(orm,ormAO,'Input')
        bakeAO=node('ComponentMask',r=True);link(ao,bakeAO,'Input')
        output(multiply(lerp(scalar(1),ormAO,use),lerp(scalar(1),bakeAO,strength)),'AmbientOcclusion')
        output(scalar(0),'Metallic');v.props(master,twoSided=False);v.call('MaterialTools','recompile',material_or_function=v.ref(master));v.call('AssetTools','save_assets',asset_paths=[master]);print('Compiled Round2 surface',flush=True)
    for state,name in zip(['Pristine','Damaged','Rubble'],v.NAMES):
        for review in [False,True]:
            for slot in v.SLOTS:
                path=CAND+'/Materials/MI_'+state+'_'+slot+('_MASTER' if review else '')
                parent='/Game/Art/Shared/Materials/Building/M_Bld_Glass' if slot=='Glass' else master
                if not v.call('AssetTools','exists',path=path):v.call('MaterialInstanceTools','create',folder_path=CAND+'/Materials',asset_name=path.rsplit('/',1)[-1],parent=v.ref(parent))
                if slot!='Glass':
                    for param,suffix in [('AtlasTint','Tint'),('BakeNormal','BakeNormal'),('BakeAO','BakeAO')]:v.call('MaterialInstanceTools','set_texture_parameter',instance=v.ref(path),name=param,value=v.ref(CAND+'/Textures/'+name+'_'+suffix))
                    if slot in v.SURFACES:
                        for param in ['BaseColor','Normal','ORM']:v.call('MaterialInstanceTools','set_texture_parameter',instance=v.ref(path),name=param,value=v.ref(CAND+'/Textures/'+v.SURFACES[slot]+'_'+param))
                    v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(path),name='UseScan',value=int(slot in v.SURFACES))
                    v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(path),name='BakeStrength',value=0 if review else .35)
                    tint={'Brick':(.82,.70,.61,1),'Roof':(1.7,1.22,.95,1),'Trim':(.67,.70,.65,1),'Wood':(.24,.21,.18,1),'Rubble':(.48,.44,.38,1)}[slot]
                    v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(path),name='SurfaceTint',value=dict(zip('rgba',tint)))
                else:
                    v.call('MaterialInstanceTools','set_vector_parameter',instance=v.ref(path),name='SurfaceColor',value={'r':.025,'g':.04,'b':.05,'a':1})
                    v.call('MaterialInstanceTools','set_scalar_parameter',instance=v.ref(path),name='GlassOpacity',value=.5)
                v.props(path,parent=v.ref(parent));v.call('AssetTools','save_assets',asset_paths=[path])
            for suffix in (['_MASTER'] if review else ['','_LOD1']):
                mesh=CAND+('/Masters02/Final/' if review else '/Final02/')+name+suffix
                for slot in v.call('StaticMeshTools','get_material_slots',mesh=v.ref(mesh)):
                    v.call('StaticMeshTools','set_material',mesh=v.ref(mesh),slot_name=slot,material=v.ref(CAND+'/Materials/MI_'+state+'_'+slot+('_MASTER' if review else '')))
                v.call('AssetTools','save_assets',asset_paths=[mesh])
        print('Bound',state,flush=True)
if __name__=='__main__':main()
