"""Pure shared Round3 names, surfaces and material intent."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Art/Shared/village/issue431/round3'
CAND='/Game/Art/Candidates/Village/Round3'
TYPES=['HouseA','HouseB','HouseC','Farmhouse','Barn','Workshop','Church']
SLOTS=['Brick','Roof','Trim','Glass','Wood','Rubble']
CONFIG={
 'HouseA':('Bricks085',(.82,.70,.61),(1.7,1.22,.95)),
 'HouseB':('Plaster001',(.78,.75,.67),(.40,.43,.46)),
 'HouseC':('Bricks085',(.68,.57,.48),(1.5,1.12,.89)),
 'Farmhouse':('Plaster001',(.82,.77,.65),(1.6,1.15,.88)),
 'Barn':('Planks023A',(.17,.15,.12),(.40,.43,.46)),
 'Workshop':('Plaster001',(.59,.64,.63),(.43,.47,.51)),
 'Church':('Bricks097',(.62,.53,.45),(.40,.43,.48)),
 'Bridge':('Concrete031',(.65,.64,.60),(.045,.05,.055)),
}
def names(key):
    if key=='Bridge':return ['SM_Bridge_Span_Damaged','SM_Bridge_Span_Destroyed']
    return ['SM_Village_'+key+s for s in ['', '_Damaged','_Rubble']]
def type_for(name):return 'Bridge' if name.startswith('SM_Bridge') else name.split('_')[2]
NAMES=[n for k in TYPES+['Bridge'] for n in names(k)]
def atlas_root(name):return CAND+'/Textures/'+('AtlasBarn02' if type_for(name)=='Barn' else 'Atlas')
