"""Kit lighting/12_MastBeaconSingle, SM_Kit_MastBeaconSingle: the red obstruction beacon on a mast tip (#351), remodelled
from the producer's image in #393: dome, clamp-lug collar and stem, class M with a real LOD1."""
import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from lighting_393_common import Builder,build_pair,mast_head

def mesh(name,low):
    b=Builder(name);mast_head(b,0,low);return b.finish()

def build(piece):return build_pair(piece,mesh)
