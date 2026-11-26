"""Run with UnrealEditor-Cmd -run=pythonscript -script=.../import_pig.py."""
from pathlib import Path
import sys

sys.dont_write_bytecode=True
here=Path(__file__).resolve().parent
sys.path.insert(0,str(here))
from import_meshes import import_character
from pig_paint import bake as bake_paint

pig,bones=import_character(here/'SK_ExpeditionPig.glb','Pig',bake_paint())
print('PIG_SKELETAL_READY path='+pig.get_path_name()+' bones='+str(bones)+' bounds='+str(pig.get_bounds()))
