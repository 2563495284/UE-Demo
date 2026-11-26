"""Sculpt the red fox navigator in centimetres on the expedition crew's shared rest pose."""
from pathlib import Path
import math
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE.parent / "Pig"))
from sculpt import BONES, Sculpture, blend, fist, hull, ink, norm, project_x, ribbon

FUR = "A6593F"
CHEEK = "BE7551"
CREAM = "DCC8A2"
PALE = "E6D6B6"
CHARCOAL = "413D3A"
NOSE = "453B36"
EYE = "674B3D"
WAX = "674D3F"
JACKET = "566777"
LIGHT = "697B87"
SHADOW = "414F60"
HEM = "435565"
DEEP = "3D4A55"
BRASS = "B49158"  # Existing shared material recognises this metal's roughness.
LEATHER = "77563D"
TACK = "C5AD80"


def ring_x(shape):
    """A fox skull cross-section with a brow ledge, cheek corner and underslung jaw."""
    x, top, bottom, width, cheek = shape
    h = top - bottom
    return [
        (x, 0., top), (x, -width*.56, top-.08*h), (x, -width*.89, top-.23*h),
        (x, -width, cheek), (x, -width*.74, bottom+.22*h), (x, -width*.42, bottom+.07*h),
        (x, 0., bottom), (x, width*.42, bottom+.07*h), (x, width*.74, bottom+.22*h),
        (x, width, cheek), (x, width*.89, top-.23*h), (x, width*.56, top-.08*h),
    ]


def skull(m):
    # The cranial dome ends at the inner eyes. Its nasal bridge slopes into a long,
    # narrow projecting muzzle, without the short muzzle and spherical cheeks of a cub.
    shapes = [
        (-23,136,120,7,128), (-17,144.5,111,17,129), (-8,149,107,23.5,127),
        (1,149.8,105.2,25.3,126), (10,145.8,104.3,24.1,124),
        (20.2,137.8,107.,18.7,122.7), (26.5,131.6,110.1,13.8,121),
        (34.8,126.4,113.,10.2,120.4), (42.6,123.5,115.2,7.2,120.),
        (47.0,122.3,117.1,4.8,120.),
    ]
    rings = [ring_x(s) for s in shapes]
    first = len(m.faces)
    for i, (prev, cur) in enumerate(zip(rings,rings[1:])):
        for j in range(12):
            k = (j+1)%12
            color = FUR
            if j in (4,5,6,7):
                color = CREAM if i>2 else CHEEK
            elif j in (3,8):
                color = "CD9B70" if i>4 else CHEEK
            elif j in (0,11) and i in (4,5,6,7):
                color = "AE6143"
            m.surface([prev[j],prev[k],cur[k],cur[j]], [(0,1,2,3)], color, "head", .79)
    m.surface(rings[0], [tuple(reversed(range(12)))], FUR, "head")
    m.surface(rings[-1], [tuple(range(12))], NOSE, "head")
    face = m.faces[first:]
    for side, label in ((-1,"l"), (1,"r")):
        def pigment_on_face(outline, tone, bone, distance):
            ink(m, face, [(side*y,z) for y,z in outline],tone,bone,distance)
        # A broad, resting upper lid with a short outer corner reads from the dock camera.
        pigment_on_face([(8.4,134.35),(12.3,135.4),(16.9,135.7),(19.2,135.0),
                         (16.3,134.7),(11.8,134.6)],"945941","brow_"+label,.39)
        pigment_on_face([(9.1,130.6),(11.3,132.4),(14.95,133.1),(18.6,132.55),
                         (17.4,130.4),(15.1,129.35),(11.7,129.3)],EYE,"eyelid_"+label,.55)
        pigment_on_face([(11.2,130.45),(12.7,132.15),(15.1,132.55),(17.1,131.9),
                         (16.4,130.4),(14.5,129.65),(12.4,129.75)],"B69766","eyelid_"+label,.79)
        pigment_on_face([(12.65,130.9),(13.15,132.05),(14.55,132.3),(15.7,131.75),
                         (15.55,130.6),(14.45,130.0),(13.2,130.15)],"473C34","eyelid_"+label,1.15)
        pigment_on_face([(13.15,131.65),(13.5,132.05),(14.05,131.9),(13.8,131.55)],
                        "C8B997","eyelid_"+label,1.34)
        # The pale cheek ruff sweeps backward into two widely spaced guard-hair points.
        hull(m, [(-17,side*22,129),(-14,side*27,125),(-20,side*28.6,120.1),
                 (-9,side*23.3,120.4),(-14,side*25.1,114.8),(-2,side*22.1,115.6),
                 (11.1,side*21.9,117.1),(16.5,side*21.1,120.2),(11.2,side*24,123.6),
                 (-2,side*26,127.05),(-5,side*22,122.)],CREAM,"head")
        # Short dark mouth line on the light side of the muzzle. A bevel gives it width
        # from a grazing profile view and keeps it readable as her jaw moves.
        m.tube([(45.8,side*4.9,118.2,.5,.65),(41.7,side*7.4,117.5,.65,.67),
                (34.1,side*9.9,117.9,.7,.62),(29.0,side*12.7,119.0,.38,.45)],
               "745445","jaw",5)
    hull(m,[(43.9,-6.8,122),(43.9,6.8,122),(46.6,-5.55,124.0),(46.6,5.55,124.0),
            (50.05,-4.5,122.6),(50.05,4.5,122.6),(50.4,0,119.0),(47.25,0,117.45),
            (44.5,-3.85,118.75),(44.5,3.85,118.75)],NOSE,"head")
    hull(m,[(45.,0,116.6),(42.0,-6.65,116.7),(42.0,6.65,116.7),
            (30.,-11.15,116.3),(30.,11.15,116.3),(21.65,-12.5,114.0),(21.65,12.5,114.0),
            (27.,0.,109.1),(38.1,0,113.0)],PALE,"jaw")


def ears(m):
    for s,name in ((-1,"ear_l"),(1,"ear_r")):
        # Tall pinnae turn in slightly different directions. Each is a volume with
        # a dark back/tip, a burnt-umber bowl, and a light bevel along its outer edge.
        spread = 2.8 if s<0 else 0.
        a=np.array([2.7,s*11.0,140.2]); b=np.array([-4.1,s*27.2,138.7])
        top=np.array([-9.4+spread,s*(26.3+spread),173.2-(1.6 if s<0 else 0.)])
        mid=(a+b+top)/3
        back=np.array([-16.1,s*20.,146.6])
        at=a*.33+top*.67; bt=b*.39+top*.61
        for points,color in [([a,b,bt,at],FUR),([at,bt,top],CHARCOAL),([b,back,top,bt],CHARCOAL),
                             ([a,at,top,back],"705043"),([back,b,a],FUR)]:
            outward(m,points,mid+(-2.,0.,0.),color,name)
        rim=[a*.79+mid*.21,b*.79+mid*.21,b*.33+top*.55+mid*.12,a*.32+top*.57+mid*.11]
        rim=[p+np.array([.75,0,.18]) for p in rim]
        for pts,col in [(rim,"754A3E"),([rim[0],rim[1],.33*rim[2]+.67*rim[3]],"BD8B71")]:
            outward(m,pts,back,col,name)


def outward(m,points,inside,color,bone):
    pts=np.asarray(points,float)
    if np.dot(np.cross(pts[1]-pts[0],pts[-1]-pts[0]),pts.mean(axis=0)-inside)<0.:
        pts=pts[::-1]
    m.surface(pts,[tuple(range(len(pts)))],color,bone)


def tail(m):
    # A brush on a long sweep; its distal third bends out to her right so both the
    # cream tip and the left-shoulder chart tube remain visible from behind.
    path=[(-14.5,1,52,4.1,3.9),(-23,1.5,52.8,5.8,5.5),(-35,3.,52.,9.,8.5),
          (-46,7.5,47.3,13.,13.),(-54.1,14.6,39.8,15.4,15.7),
          (-60.1,23.9,34.0,16.1,16.5),(-65.8,35.4,31.0,15.4,15.7),
          (-69.1,45.8,31.8,12.,12.7),(-72.1,53.2,35.8,8.4,8.),
          (-73,60.3,40.3,3.8,4.15),(-72.8,64.6,44.55,.38,.5)]
    spine=np.array([p[:3] for p in path]); rings=[]
    sides=12 if m.detail==0 else (9 if m.detail==1 else 7)
    for i,p in enumerate(path):
        direction=norm(spine[min(i+1,len(path)-1)]-spine[max(i-1,0)])
        u=norm(np.cross([0.,0.,1.],direction)); v=norm(np.cross(direction,u))
        rings.append([spine[i]+u*p[3]*math.cos(a)+v*p[4]*math.sin(a)+
                      (direction*1.9*math.cos(3*a+.3) if i==6 else 0.)
                      for a in (2*math.pi*j/sides for j in range(sides))])
    def skin(p):
        t=float(np.clip((-p[0]-24.)/22.,0.,1.))
        return {"tail_01":1.-t,"tail_02":t}
    for i,(a,b) in enumerate(zip(rings,rings[1:])):
        for j in range(sides):
            k=(j+1)%sides
            shade=CREAM if i>=6 else ("97523B" if j>=sides//2 else FUR)
            if i==7 and 1<=j<=sides//2:shade="DECEAE"
            m.surface([a[j],a[k],b[k],b[j]],[(0,1,2,3)],shade,skin,.82)
    m.surface(rings[-1],[tuple(range(sides))],PALE,"tail_02")


def jacket(m):
    torso=blend("pelvis","chest",55,93)
    m.tiers([(.25,0,49,13.0,20.5),(.3,0,55,15.45,22.0),(.4,0,61.75,15.1,20.3),
             (.3,0,63.65,14.5,19.7)],DEEP,"pelvis",14,.82)
    m.tiers([(.53,0,59.8,15.55,20.7),(.45,0,61.1,15.65,20.8),(.25,0,63.5,15.3,20.35)],
            "645447","pelvis",14,.80)
    m.tiers([(.3,0,62.6,14.25,19.2),(.1,0,68.3,13.1,17.9),(-.7,0,79,14.1,18.5),(-1.8,0,92.7,15.,24.5),
             (-2,0,100.9,11.3,20),(-1.9,0,104.,9.,12.)], "B3AB90",torso,14,.85)
    m.tiers([(-.8,0,98.9,9.5,11.),(-.9,0,107,9.6,10.),(2.3,0,115.0,8.75,9.7)],
            CREAM,blend("neck","head",105.,115.),10)
    # Open front and a short double-turned hem. A sailor's jacket is cut square across
    # narrow hips with room at the shoulder; no decorative waist cinch or exposed chest.
    sections=[(57.0,.2,17.8,24.5,.41),(60.15,0,18.25,24.5,.43),(66,-.3,17.8,23.7,.42),
              (79.3,-.9,17.65,22.95,.40),(90.5,-2.,18.3,27.9,.38),
              (97.85,-2.,17.15,29.5,.42),(101.1,-2.,14.4,24.7,.48),
              (104.25,-1.7,10.4,15.7,.62)]
    count=(24,16,12)[m.detail]
    loops=[]
    for z,x,rx,ry,opening in sections:
        loops.append([np.array([x+rx*math.cos(a),ry*math.sin(a),z]) for a in
                      np.linspace(opening,2*math.pi-opening,count)])
    for i,(prev,cur) in enumerate(zip(loops,loops[1:])):
        for j in range(count-1):
            shade=HEM if i==0 else (LIGHT if i==len(loops)-2 else JACKET)
            m.surface([prev[j],prev[j+1],cur[j+1],cur[j]],[(0,1,2,3)],shade,torso)
    for side in (-1,1):
        path=[s[0 if side>0 else -1] for s in loops]
        ribbon(m,path,2.3,.95,SHADOW,torso)
        points=[(11.5,side*11.4,103.15),(13.2,side*18.0,100.),
                (14.1,side*17.45,95.2),(15.1,side*13.4,95.7),
                (16.,side*13.75,92.45),(16.65,side*8.05,85.7),(15.9,side*8.6,95.1)]
        outward(m,points,np.array([0.,0.,90.]),LIGHT,"chest")
    # Broad squared collar over her back. An embroidered north arrow lies flat on
    # the left shoulder blade, opposite the rising chart case.
    collar=[(-8.7,-12.1,103),(-11.8,-21.,99.5),(-16.95,-16.6,94.25),
            (-21.2,-8.5,92.15),(-21.2,8.5,92.15),(-16.95,16.6,94.25),
            (-11.8,21.,99.5),(-8.7,12.1,103),(-10.8,0.,104.2)]
    outward(m,collar,np.array([0.,0.,87.]),LIGHT,"chest")
    # Fit these four marks to the actual cross-section of the back at each height.
    def back(y,z):
        vals=[]
        for L,R in zip(sections,sections[1:]):
            if L[0] <= z <= R[0]:
                t=(z-L[0])/(R[0]-L[0]); p=np.array(L)*(1.-t)+np.array(R)*t
                vals.append((p[1]-p[2]*math.sqrt(max(0.,1.-(y/p[3])**2))-.44,y,z))
        return vals[0]
    for outline,tone in [([(8.15,88.75),(11.8,82.1),(8.3,83.45),(5.55,80.9)],TACK),
                         ([(8.3,83.45),(11.8,82.1),(8.9,77.1)],"939C93"),
                         ([(4.4,82.15),(5.15,82.6),(5.15,83.35),(4.25,83.1)],TACK),
                         ([(12.45,83.1),(13.4,83.5),(13.4,82.8),(12.5,82.4)],TACK)]:
        outward(m,[back(-y-5.5,z+3.) for y,z in outline],np.array([0.,0.,84.]),tone,"chest")
    # Wrap a dull ochre scarf low around the neck without widening the profile.
    m.tiers([(.1,0,99.8,10.1,12.25),(-.7,0,102.8,11.65,13.2),(-.7,0,105.,10.55,11.8)],
            "A78755","neck",11,.85)
    pts=[(10.85,-1.,103.35),(10.4,-6.2,102.5),(14.65,-9.3,95.6),(15.15,-6.3,93.7),(13.,-1.7,99.4)]
    outward(m,pts,np.array([0,0,98]),"A78755","chest")


def gear(m):
    # A leather strap from her right collarbone to her left hip. The back return
    # meets the cap of a diagonal sealed chart tube, away from the brush of her tail.
    strap=[(-17.9,-21.0,60.0),(-18.7,-15.4,68.3),(-19.5,-4.4,78.6),(-19.9,6.,88.9),
           (-16.8,14.4,97.9),(-7.,17.7,103.0),(.4,19.2,104.25),(9.9,19.5,102.),
           (15.8,15.5,95.7),(17.9,8.5,87.8),(18.05,.1,79.2),(17.9,-8.3,71.1),
           (15.95,-14.4,65.1),(10.,-23.1,59.15),(-1.,-25.2,57.95),(-10.,-24.9,58.25),
           (-17.9,-21.0,60.)]
    ribbon(m,strap,4.65,1.25,LEATHER,blend("pelvis","chest",57.,90.))
    def chartpiece(a,b,radius,tone,bone="chest"):
        m.tube([(*a,radius,radius),(*b,radius,radius)],tone,bone,10)
    origin=np.array([-18.8,-23.5,57.5]); top=np.array([-18.9,15.7,101.75])
    axis=norm(top-origin)
    chartpiece(origin,top,5.8,"80664D")
    for offset, span, rad, tone in ((0,2.1,6.35,BRASS),(4.1,1.35,5.95,WAX),
                                   (13.,2.0,6.05,"5B493C"),(39,2.,6.05,"5B493C"),
                                   (50,1.5,6.1,WAX),(55.2,4.65,6.65,BRASS)):
        chartpiece(origin+axis*offset,origin+axis*(offset+span),rad,tone)
    chartpiece(top-axis*.2,top+axis*.65,4.65,"6D604E")
    # Small brass hand-bearing compass, suspended at her right lower breast. Its
    # dark dial and cardinal needle stay readable, with the rim visible in profile.
    c=np.array([15.2,15.3,74.1]); n=norm(np.array([.88,.48,.13]))
    chartpiece(c-n*1.1,c+n*1.25,5.05,BRASS,"chest")
    chartpiece(c+n*1.28,c+n*1.53,3.95,"4A5B5A","chest")
    up=norm(np.array([0.,0.,1.])-n*n[2]); across=norm(np.cross(n,up))
    ctr=c+n*1.7
    outward(m,[ctr+3.2*up+across,ctr+1.0*across,ctr-2.6*up-.9*across,ctr-.67*across],
            c,CREAM,"chest")
    m.tube([(15.,14.6,78.45,.55,.55),(15.25,14.45,80.35,.64,.64),
            (14.8,16.,81.25,.61,.61),(14.8,17.35,80.3,.63,.63),
            (15.,17.0,78.4,.55,.55)],BRASS,"chest",6)


def limbs(m):
    for s,side in ((-1,"l"),(1,"r")):
        shoulder,elbow,palm=("upperarm_"+side,"lowerarm_"+side,"hand_"+side)
        thigh,knee,foot=("thigh_"+side,"calf_"+side,"foot_"+side)
        m.tube([(-2,s*21.2,98.2,5.2,6.),(-2.4,s*28.2,97.6,8.3,8.7),
                (-2.7,s*32.1,95.7,8.65,9.2),(-2.,s*36.,89.,7.6,7.95),
                (-.5,s*39.6,81.4,7.2,7.4),(1.1,s*41.6,77.,6.95,7.15)],
               JACKET,shoulder,10)
        m.tube([(.2,s*40.2,80.2,7.1,7.3),(2.2,s*42.,75.,7.4,7.4),
                (4.4,s*43.7,70.,6.8,6.9),(6.1,s*44.7,66.35,6.15,6.35)],JACKET,elbow,9)
        m.tube([(5.45,s*44.45,68.15,6.9,7.),(5.95,s*44.6,67.35,7.15,7.15),
                (6.75,s*44.95,64.7,6.9,7.0)],LIGHT,elbow,9)
        m.tube([(6.15,s*44.75,66.,5.75,5.85),(7.15,s*45.2,62.35,5.5,5.5),
                (8.5,s*45.15,56.85,4.4,4.85)],"83543E",elbow,9)
        m.tiers([(9.75,s*45.7,46.4,4.65,4.4),(10.15,s*46.,49.5,6.55,6.15),
                 (9.55,s*45.7,54.1,6.8,6.0),(8.8,s*45.2,57.8,4.9,4.65),
                 (8.6,s*45.2,59.1,4.2,4.3)],"51443B",palm,10,.7)
        for dx,drop in ((-3.15,.25),(.25,-.65),(3.55,.0)):
            m.tiers([(10.+dx,s*46.65,45.8+drop,1.65,2.7),(10.35+dx,s*47.,47.5+drop,2.15,3.35),
                     (10.35+dx,s*46.95,50.4+drop,2.15,3.35)],CHARCOAL,palm,7,.72)
        m.tube([(12.65,s*42.6,54.5,2.6,2.7),(15.0,s*41.6,53.0,2.65,2.85),
                (16.0,s*42.,49.3,1.7,2.05)],CHARCOAL,palm,7)
        m.tiers([(.1,s*17.2,30.2,8.05,8.3),(-.25,s*17.5,34.7,9.65,9.75),
                 (0.,s*16.75,41.,10.45,11.25),(.0,s*15.8,50.9,11.3,12.05),
                 (0.,s*14.6,56.3,11.5,12.4),(.2,s*13.55,59.7,11.25,11.5)],
                DEEP,thigh,10,.76)
        m.tiers([(-.75,s*18.9,15.55,6.3,7.2),(-1.0,s*18.9,20.75,7.0,7.45),
                 (-1.4,s*18.6,26.1,8.2,8.35),(-1.1,s*18.3,33.7,9.0,9.15),
                 (-.35,s*18.2,35.8,8.25,8.35)],DEEP,blend(knee,thigh,27.,35.),9,.76)
        m.tiers([(.1,s*18.95,16.3,7.3,7.65),(0,s*18.9,18.,7.75,8.0),
                 (-.4,s*18.85,19.45,7.5,7.9)],HEM,knee,9,.70)
        m.tiers([(5.7,s*19.2,.5,12.75,8.25),(5.8,s*19.3,1.7,13.55,8.85),
                 (5.65,s*19.3,3.35,13.5,8.8)],CHARCOAL,foot,10,.66)
        m.tiers([(5.7,s*19.3,2.9,13.0,8.65),(5.4,s*19.3,5.9,13.,8.8),
                 (3.3,s*19.15,9.6,11.0,8.55),(.8,s*19.,13.55,8.6,7.8),
                 (-.2,s*18.9,18.5,7.05,7.45)],"6C5847",foot,10,.68)


def character(detail=0):
    model=Sculpture(detail)
    jacket(model)
    limbs(model)
    tail(model)
    gear(model)
    skull(model)
    ears(model)
    return model


if __name__ == "__main__":
    dest=HERE/"Fox"
    dest.mkdir(exist_ok=True)
    for level in range(3):
        model=character(level)
        name="SK_ExpeditionFox"+(f"_LOD{level}" if level else "")
        model.glb(dest/(name+".glb"))
        print(f"{name}: {len(model.faces)} triangles, {len(BONES)} bones")
        if not level:
            first_person=fist(model)
            first_person.glb(dest/"SM_FoxFist.glb",skinned=False)
            print(f"SM_FoxFist: {len(first_person.faces)} triangles")
