"""Import the three harbour workers through the same pipeline as the dock surveyor."""
from pathlib import Path
import sys
import unreal as ue

sys.dont_write_bytecode=True
here=Path(__file__).resolve().parent
sys.path.insert(0,str(here.parent/'Pig'))
from import_meshes import import_character

paint=ue.load_asset('/Game/TreasureIsland/Art/Crew/Pig/M_PigPaint')
if not isinstance(paint,ue.Material):
    raise RuntimeError('Import the surveyor before the other harbour workers')
for species in ('Monkey','Fox','Calf'):
    model,bones=import_character(here/species/f'SK_Expedition{species}.glb',species,paint)
    print(f'CREW_SKIN_READY {species} bones={len(bones)} path={model.get_path_name()}')
