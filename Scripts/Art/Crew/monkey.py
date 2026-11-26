"""Author the island's adult rope rigger, in centimetres (+X forward, +Z up)."""
from __future__ import annotations

import importlib.util
import math
from pathlib import Path

import numpy as np


HERE = Path(__file__).resolve().parent
# Sculpture's writer owns its bone table. A private instance keeps the rigger's
# bind pose independent of the other character generators in a batch export.
spec = importlib.util.spec_from_file_location('monkey_sculpture', HERE.parent / 'Pig' / 'sculpt.py')
art = importlib.util.module_from_spec(spec)
spec.loader.exec_module(art)

FUR = '92654C'
MANE = '76513E'
PALM = 'AD8369'
FACE = 'B79074'
MUZZLE = 'C2A185'
EYE = '342F29'
EAR = 'A77D69'
NOSE = '75594E'
SHIRT = '677969'
ROLL = '8D9881'
PANEL = '576C5F'
PANTS = '4C5860'
LEATHER = '775846'
SOLE = '474842'
ROPE = 'B59A70'
BRASS = 'A58C59'
art.MATERIAL_ROUGHNESS.update({FUR:.89, MANE:.92, PALM:.77, FACE:.78, MUZZLE:.79, EYE:.51,
    NOSE:.65, EAR:.8, SHIRT:.93, ROLL:.94, PANEL:.91, PANTS:.91, LEATHER:.67, SOLE:.82, ROPE:.96, BRASS:.35})

PIVOTS = {'pelvis':(0,0,58), 'spine':(-1,0,76),'chest':(-2,0,98), 'neck':(0,0,112),
          'head':(3,0,124), 'jaw':(19,0,119), 'ear_l':(-4,-19,136), 'ear_r':(-4,19,136),
          'tail_01':(-14,0,57), 'tail_02':(-29,5,49), 'hat_brim':(-6,4,146)}
for suffix,sign in [('l',-1),('r',1)]:
    for name,(x,y,z) in {'upperarm':(-1,25,105),'lowerarm':(1,33,79),'hand':(11,36,51),
                         'thigh':(0,12,58),'calf':(-2,13,32),'foot':(3,14,13),
                         'brow':(21,8,141),'eyelid':(23,8,136.4)}.items():
        PIVOTS[name+'_'+suffix] = (x,sign*y,z)
art.BONES = {name:(parent,PIVOTS.get(name,center)) for name,(parent,center) in art.BONES.items()}


def shell(m, sections, shape, color, bind):
    """Irregular longitudinal planes, following the face from neck to nose."""
    loops = [[(x,y*ry,z+dz*rz) for y,dz in shape] for x,z,ry,rz in sections]
    m.solid(loops,color,bind)


def flank(m, points, color, bind, thickness=1.4):
    """A convex cut of material with an outward facing edge on both sides."""
    points = np.asarray(points, dtype=float)
    normal = art.norm(np.cross(points[1]-points[0],points[-1]-points[0]))
    art.hull(m, np.concatenate([points,points-normal*thickness]),color,bind)


def rope(m, path, radii, tone, bone, sides=7):
    # Parallel transport avoids twisting the facets on the returns of the rope and tail.
    p=np.asarray(path,float); loops=[]; guide=np.array([1.,0.,0.]);
    for i,point in enumerate(p):
        tangent=art.norm(p[min(i+1,len(p)-1)]-p[max(i-1,0)])
        if abs(tangent@guide)>.93:
            guide=np.array([0.,1.,0.]) if abs(tangent[1])<.8 else np.array([0.,0.,1.])
        guide=art.norm(guide-tangent*(guide@tangent))
        cross=art.norm(np.cross(tangent,guide)); radius=radii[i] if isinstance(radii,(list,tuple)) else radii
        loops.append([point+radius*(math.cos(a)*guide+math.sin(a)*cross)
                      for a in np.arange(sides)*(math.tau/sides)])
    # A tight return must keep all knots even on the distant model. Use the budget
    # on fewer ring sides instead of cutting every second point along a curved path.
    base=m.detail; m.detail=0
    try:
        m.solid(loops,tone,bone)
    finally:
        m.detail=base


def head(m):
    # The forehead, temples and cheek ruffs have different slopes. A longer mandible,
    # brow shelf and short protruding lip establish an adult macaque in side profile.
    points=[(-18,0,132),(-15,0,144),(-8,0,150),(0,0,153),(10,0,151),(16,0,146),
            (18,0,137),(19,0,120),(10,0,112),(-1,0,112),(-13,0,119)]
    for side in [-1,1]:
        points.extend((x,side*y,z) for x,y,z in [(-14,12,141),(-8,15,147),(2,15,150),
            (12,13,146),(17,13,139),(15,18.8,135),(12,18.3,126),(9,14,117),
            (1,14,115),(-10,15,120),(-14.8,17.0,134),(-4,21.4,134),(2,21,129),
            (7,19,122),(9,12.5,116),(-4,17,120),(-8,20,127)])
    art.hull(m,points,FUR,'head')
    # The naked muzzle emerges from a light two-lobed cheek mask. Each side narrows
    # into an eye socket, leaving a clear cinnamon hairline around the face.
    shape=[(-.67,-.83),(0,-1),(.67,-.83),(1,-.26),(.94,.44),(.52,.91),(0,1),(-.52,.91),(-.94,.44),(-1,-.26)]
    for sign,suffix in [(-1,'l'),(1,'r')]:
        landmarks=[(13,0,139),(16,3,143.5),(14.6,10,144.3),(11,15.1,141.2),
           (12,17.7,137),(13.8,17.2,132),(16,14.8,127),(17,11,123),(18,3,122),
           (20,0.5,131),(21,4,137),(22,9.2,137.7),(20.2,13.4,135.1),(20.6,9.7,128.),(19.7,4,126)]
        start=len(m.faces)
        art.hull(m,[(x,sign*y,z) for x,y,z in landmarks],FACE,'head'); mask=m.faces[start:]
        eye=[(-2.55,-.18),(-1.75,-1.11),(.25,-1.39),(2.16,-.98),(2.82,-.12),(2.36,.76),(.60,1.27),(-1.36,1.18),(-2.45,.64)]
        eye_y=sign*8.7+.4; eye_z=136.25+sign*.37
        art.ink(m,mask,[(eye_y+y,eye_z+z) for y,z in eye],EYE,'eyelid_'+suffix,.63)
        lid=[(-3.08,.16),(-1.30,.72),(.95,.82),(2.72,.35),(3.24,1.39),(.84,2.33),(-1.70,2.18),(-3.32,1.22)]
        art.ink(m,mask,[(eye_y+y,eye_z+z) for y,z in lid],'A17C65','brow_'+suffix,.86)
        # A blunt temporal crest descends from ear to cheek, projecting past the jaw.
        mane=[(-8,17.5,141),(0,20.8,143),(7,20.5,140),(10,18,137),(9.2,19.7,132),
              (10.1,18.,127),(7.5,17.6,123.0),(6.2,14.8,118),(-.8,16.6,120.4),
              (-6.7,18.0,124.0),(-9.5,17.6,132),(1,21.0,135)]
        art.hull(m,[(x,sign*y,z) for x,y,z in mane],MANE,'head')
        # Rounded, scalloped ears angle outward. The inner concha has a broad single
        # bevel, with an attached lower lobe instead of a stuffed round sphere.
        outer=[(-5,-6.4),(1.6,-7.0),(6.1,-4.9),(8.0,-1),(7.4,4.2),(3.2,7.4),
               (-1.8,7.9),(-5.7,5.6),(-7.0,.8),(-6.6,-3.1)]
        loops=[]
        for x,k in [(-9.2,.80),(-6.7,1),(-3.2,.99),(-1.9,.78)]:
            row=[(x-.17*y,sign*(22.7+y*k),136.1+z*k+sign*.75) for y,z in outer]
            if sign<0:row.reverse()
            loops.append(row)
        start=len(m.faces);m.solid(loops,FUR,'ear_'+suffix);ear=m.faces[start:]
        inset=[(0,-4.5),(3.5,-3.2),(5.5,-.1),(4.5,3.5),(1.1,5.1),(-2.7,4.1),(-3.9,1.1),(-2.2,-1.7),(-.7,-1.2)]
        art.ink(m,ear,[(sign*(22.7+y),136.1+z+sign*.75) for y,z in inset],EAR,'ear_'+suffix,.40)
    beginning=len(m.faces)
    shell(m,[(11.9,123.,10.9,9.3),(16.4,122.9,13.65,10.),(21.3,121.7,13.1,8.2),
             (26.3,121.75,11.1,6.7),(28.4,121.8,9.1,4.6)],shape,MUZZLE,'jaw')
    muzzle=m.faces[beginning:]
    mouth=[(-10.,-.50),(-6.4,-1.28),(-2.2,-1.55),(3.5,-1.55),(8.8,-1.08),(10.5,-.38),
           (9.1,-1.68),(4.2,-2.49),(-2.0,-2.48),(-7.5,-2.16)]
    art.ink(m,muzzle,[(y,121.55+z) for y,z in mouth],'896A58','jaw',.34)
    start=len(m.faces)
    shell(m,[(18.9,131.3,5.1,4.4),(23.2,130.25,6.7,3.9),(26.4,129.1,6.2,3.04),
             (27.75,128.9,4.95,1.91)],shape,NOSE,'head')
    nasal=m.faces[start:]
    for sign in [-1,1]:
        art.ink(m,nasal,[(sign*(3.04+y),128.9+z) for y,z in [(-1.18,-.28),(-.64,-.73),(.69,-.62),(1.20,.14),(.58,.70),(-.53,.60)]],
                '4E443B','head',.31)
    # Low indigo kerchief binds a ridge of short crown fur. Its small nape knot
    # follows the existing secondary-motion joint used for a hat brim on the pig.
    def slope(v,a):
        return (v[0],v[1],v[2]-.082*v[1]+.11*v[0])
    m.tiers([(-2,0,142.7,16.2,16.9),(-2.2,0,143.6,17.8,18.0),(-2.9,0,148.2,16.0,16.6),(-3.1,0,149.0,14.5,15.2)],'4E6970','head',12,.75,slope)
    art.hull(m,[(-16,-7,144),(-20,-2,146),(-20,1,142),(-18,2,138),(-15,-3,139),(-15,-8,143)],'526F77','hat_brim')
    flank(m,[(-17.5,-3,142),(-20.,-.5,141),(-22.2,-1.6,133.8),(-20.4,-3.9,130.9),(-18.8,-4,134.5)],'43616A','hat_brim')


def torso(m):
    body=art.blend('pelvis','chest',57.,98.)
    m.tiers([(-.3,0,51,13.8,17),(.0,0,55,16.9,20),(.4,0,64.0,16.0,19.6),(.1,0,68,14.0,18)],PANTS,body,11,.81)
    start=len(m.faces)
    m.tiers([(.1,0,60.6,17.0,20.5),(.3,0,64,17.7,21.2),(.5,0,72.5,15.5,18.8),
             (-.7,0,81.,16.3,20.6),(-1.7,0,93.,18.4,24.4),(-1.9,0,102.8,17.8,27),
             (-2.1,0,106.8,14.2,25.1),(-1,0,110.5,10.,13.)],SHIRT,body,14,.8)
    shirt=m.faces[start:]
    # A wrap-front short overshirt. The diagonal edge and three toggles are clear
    # from the dock picker as a separate silhouette from the surveyor's overalls.
    seam=[(-8.8,105.5),(-4.6,102.),(5.5,95.),(10.6,88.),(11.5,76.),(10.7,67.),(7.9,68.),(8.1,81.),(7.0,88.5),(1.8,94.4),(-7.4,100.9)]
    art.ink(m,shirt,seam,PANEL,body,.75)
    for y,z in [(3.1,97.5),(9.6,89.1),(10.7,78.2)]:
        art.ink(m,shirt,[(y+a,z+b) for a,b in [(-2.8,-.55),(2.6,-.75),(3.1,.35),(2.2,.91),(-2.8,.67)]],
                'AB9B79',body,1.24)
    m.tiers([(.1,0,58.7,15.7,20.4),(.5,0,60.,16.9,21.3),(.2,0,64.5,17.9,21.8),(.0,0,65.4,17.3,21.0)],LEATHER,body,12,.75)
    art.ink(m,shirt,[(-5,63.1),(-2.7,62.25),(1.5,62.5),(2.1,65.6),(-1.0,66.1),(-4.9,65.7)],BRASS,body,1.46)
    m.tiers([(-.4,0,104.5,10.,12),(1.0,0,108,10.5,11.7),(3.2,0,118,8.3,9.7)],FUR,'neck',9,.82)
    for sign in [-1,1]:
        flap=[(10.8,sign*3,104.4),(4.2,sign*7.8,111.5),(-1.2,sign*13.5,110.7),
              (6.1,sign*18.5,106.55),(13.0,sign*13.,100.),(13.8,sign*8.,103.)]
        flank(m,flap,ROLL,'chest')
    # A coil hangs from a short leather keeper on the left hip — stout enough to
    # read in silhouette without a noisy braid texture. The marlinespike is sheathed.
    coil_sides=7-m.detail
    for offset in [0.,2.45]:
        p=[(-6.8,-19.75-offset,65.4),(-12.,-21.8-offset,63.8),(-15.,-22.7-offset,57.2),
           (-15.0,-22.8-offset,50.6),(-11.4,-22.5-offset,46.1),(-6.25,-22.0-offset,45.5),
           (-2.1,-21.1-offset,48.4),(.05,-20.9-offset,55.2),(-.64,-21.0-offset,60.8),(-4.2,-20.4-offset,64.6),(-6.8,-19.75-offset,65.4)]
        rope(m,p,1.64,ROPE,'pelvis',coil_sides)
    m.tube([(-8.5,-20.4,61.5,2.1,4.2),(-7.6,-20.8,66.3,2.7,4.5),(-6.3,-19.6,68.0,2.1,3.5)],LEATHER,'pelvis',6)
    m.tube([(-12,17.1,48.7,1.1,1.6),(-10.4,19.8,55.,2.2,3.1),(-8.1,21.6,65.4,2.1,3.3)],'675B49','pelvis',7)
    m.tube([(-8.6,21.7,64.2,1.6,2.2),(-7.7,22.2,67.9,1.9,2.2),(-6.0,23.1,72.3,2.4,2.25),(-5.8,23.3,73.2,1.9,2.)],BRASS,'pelvis',7)


def limbs(m):
    for s,suff in [(-1,'l'),(1,'r')]:
        upper,fore,hand=('upperarm_'+suff,'lowerarm_'+suff,'hand_'+suff)
        m.tube([(-1.,s*17.6,104.8,7.3,8.2),(-1.7,s*25.,103.0,9.0,9.35),(-1.4,s*28.5,97.4,8.15,8.6),
                (-.5,s*31.8,86.1,7.35,7.5),(1,s*33,79.4,6.46,6.8)],SHIRT,upper,10)
        m.tube([(-.1,s*31.7,85.3,7.6,7.6),(.9,s*32.6,83.3,8.1,8.25),
                (1.7,s*33.1,78.8,7.7,7.83),(2.05,s*33.5,77.4,6.8,7.0)],ROLL,upper,9)
        # Dark oval abrasion patch sits on the rear of the elbow, visible in a game camera.
        patch=[(-7.,s*31.1,92.8),(-8.1,s*33.5,88.7),(-7.45,s*34.1,82.8),(-5.0,s*33.5,78.7),
               (-3.9,s*31.0,80.1),(-6.5,s*30.,86.2),(-6.3,s*29.7,91.2)]
        flank(m,patch,'6B6250',upper,1.6)
        m.tube([(.3,s*32.7,80.1,5.8,6.05),(1.8,s*33.3,76.6,6.5,7.1),
                (4.6,s*35.2,67,7.15,6.6),(8.3,s*35.9,58.2,5.92,5.75),(11,s*36.0,50.5,4.9,4.62)],FUR,fore,10)
        m.tube([(10.0,s*36,54.4,5.2,4.85),(11,s*36,52.4,5.8,5.2),(11.5,s*36.,49.2,5.65,5.)],LEATHER,hand,9)
        m.tiers([(12.2,s*35.9,38.7,4.85,4.25),(12.1,s*36.45,40.8,6.45,5.25),(11.8,s*36.9,46.1,6.65,5.05),
                 (11.5,s*36.3,50.4,5.55,5.00),(11.,s*36.,52.2,4.8,4.35)],'765949',hand,9,.68)
        # Crooked calloused fingers emerge separately from the fingerless working glove.
        # Clenched silhouette reads from +X when exported to the owner's eye camera.
        for dx,drop,w in [(-4.4,.7,1.55),(-1.58,1.68,1.79),(1.43,2.13,1.87),(4.25,1.28,1.76)]:
            m.tube([(12.3+dx,s*37.6,43.7,w*1.15,2.02),(12.3+dx,s*38.6,40.6-drop,w*1.12,2.03),
                    (12.3+dx,s*37.0,38.3-drop,w,1.96),(12.0+dx,s*34.3,38.45-drop,w*.84,1.59)],PALM,hand,6)
        m.tube([(14.27,s*33.25,47.6,2.3,2.38),(16.72,s*32.1,45.6,2.52,2.57),
                (17.9,s*32.2,42.25,2.56,2.41),(16.9,s*33.0,40.6,1.94,1.96)],PALM,hand,7)
        thigh,calf,foot='thigh_'+suff,'calf_'+suff,'foot_'+suff
        m.tiers([(-1.5,s*13.,31,8.50,8.65),(-2.05,s*13.25,34.,10.1,9.6),(-.6,s*12.5,44.3,10.0,10.4),
                 (.4,s*11.25,52.2,11.2,11.3),(.0,s*11.35,59.0,10.8,10.95)],PANTS,thigh,9,.73)
        m.tiers([(-.15,s*14.,12.5,6.95,7.0),(-.65,s*14.,18.,7.4,7.25),(-1.8,s*13.8,24.8,8.55,8.55),
                 (-2.2,s*13.4,31.9,9.65,9.37),(-1.45,s*13.,35.0,8.25,8.40)],PANTS,art.blend(calf,thigh,29,35.),9,.74)
        m.tiers([(.4,s*14.0,12.4,7.5,7.1),(.35,s*14.0,13.3,8.2,7.9),(-.1,s*14.,16.6,8.30,8.0)],'69736F',calf,9,.73)
        m.tiers([(5.6,s*14.0,.20,13.7,8.3),(5.7,s*14.,1.2,14.4,8.7),(5.6,s*14.,3.09,14.3,8.65)],SOLE,foot,10,.64)
        m.tiers([(5.4,s*14.,2.55,14.1,8.55),(5.,s*14.,5.6,14.,8.60),(3.75,s*14.,7.4,12.6,8.5),
                 (1.2,s*14.,10.3,9.4,7.82),(.6,s*14.,14.15,7.52,7.25)],'776B55',foot,10,.65)
    # The tail exits the jacket, dips, then opens on character-right in a single big curl.
    tail=[(-12.,-.5,57.5),(-19.,-.75,56.),(-25.,1.0,51.25),(-29.,5.,49.0),(-32.,13.,48.4),
          (-33.7,21.,51.5),(-32.0,26.3,57.5),(-29.4,28.0,65.4),(-26.0,25.0,71.),
          (-24.75,20.,72.7),(-25.55,16.2,69.4),(-27.55,16.1,65.3),(-28.2,18.85,63.5)]
    radii=[4.7,5.0,4.7,4.5,4.35,4.1,3.8,3.55,3.4,3.18,2.8,2.4,1.48]
    def bind(point):
        weight=float(np.clip((point[1]-0.5)/10.5,0.,1.))
        return {'tail_01':1.-weight,'tail_02':weight}
    rope(m,tail,radii,FUR,bind,9-2*m.detail)


def character(detail=0):
    mesh=art.Sculpture(detail)
    torso(mesh); limbs(mesh); head(mesh)
    return mesh


def export():
    out=HERE/'Monkey'; out.mkdir(exist_ok=True)
    for level in range(3):
        model=character(level)
        name='SK_ExpeditionMonkey'+('' if not level else f'_LOD{level}')
        model.glb(out/(name+'.glb'))
        print(f'{name}: {len(model.faces)} triangles, {len(model.bones)} bones')
        if level==0:
            hand=art.fist(model); hand.glb(out/'SM_MonkeyFist.glb',skinned=False)
            print(f'SM_MonkeyFist: {len(hand.faces)} triangles')


if __name__=='__main__':
    export()
