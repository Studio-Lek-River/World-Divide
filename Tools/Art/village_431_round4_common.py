"""Pure shared Round4 names, surfaces and material intent."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue431/round4'
CAND='/Game/Art/Candidates/Village/Final'
TYPES=['HouseA','HouseB','HouseC','Farmhouse','Barn','Workshop','Church']
SLOTS=['Brick','Roof','Trim','Glass','Wood','Rubble']
WOOD_TINT={'HouseA':(.24,.21,.18),'HouseB':(.12,.24,.17),'HouseC':(.14,.20,.30),'Farmhouse':(.38,.29,.21),'Barn':(.48,.40,.30),'Workshop':(.19,.26,.30),'Church':(.27,.22,.17),'Bridge':(.24,.21,.18)}
CONFIG={
 'HouseA':('Bricks085',(.82,.70,.61),(.70,.75,.78)),
 'HouseB':('Plaster001',(.78,.75,.67),(2.0,2.1,2.2)),
 'HouseC':('Bricks085',(.68,.57,.48),(.62,.66,.70)),
 'Farmhouse':('Plaster001',(.82,.77,.65),(.85,.85,.80)),
 'Barn':('Planks023A',(.38,.33,.27),(2.0,2.1,2.2)),
 'Workshop':('Plaster001',(.59,.64,.63),(2.0,2.1,2.2)),
 'Church':('Bricks097',(.62,.53,.45),(.72,.78,.85)),
 'Bridge':('Concrete031',(.65,.64,.60),(.045,.05,.055)),
}
def names(key):
    if key=='Bridge':return ['SM_Bridge_Span_Damaged','SM_Bridge_Span_Destroyed']
    return ['SM_Village_'+key+s for s in ['', '_Damaged','_Rubble']]
def type_for(name):return 'Bridge' if name.startswith('SM_Bridge') else name.split('_')[2]
NAMES=[n for k in TYPES+['Bridge'] for n in names(k)]
def atlas_root(name):return CAND+'/Textures/'+'Atlas'

def atlas_path(stem):
    if stem.startswith('SM_Bridge_'):return CAND+'/Textures/AtlasBridge02/'+stem
    folder='Tint02' if stem in ['SM_Village_Farmhouse_Damaged_Tint','SM_Village_Church_Damaged_Tint'] else 'Atlas'
    return CAND+'/Textures/'+folder+'/'+stem

def mesh_path(name,suffix=''):
    folder='Masters' if suffix=='_MASTER' else 'Meshes'
    if name.startswith('SM_Bridge_'):folder+='02'
    return CAND+'/'+folder+'/'+name+suffix
