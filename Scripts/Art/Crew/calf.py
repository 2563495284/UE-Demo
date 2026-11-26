"""Build the dockyard riveter, in centimetres on the expedition crew rest skeleton."""
from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "Pig"))
from sculpt import BONES, Sculpture, blend, fist, hull, ink, project_x, signed_power

# Salt-washed work clothes, dark brindle coat and an ivory stripe through the poll.
COAT = "766458"
DARK_COAT = "63574F"
CREAM = "D1BE95"
HORN = "C0AB7E"
MUZZLE = "9B887C"
HOOF = "4B4541"
EYE = "393635"
CANVAS = "92704F"
APRON = "896A4C"
SHIRT = "3F4F5A"
TROUSERS = "4E5555"
LEATHER = "634F40"
BRASS = "AD8959"

ROUGHNESS = {
    COAT: .83, CREAM: .82, HORN: .66, MUZZLE: .56, HOOF: .65, EYE: .53,
    LEATHER: .70, BRASS: .35, DARK_COAT: .85,
}


class Riveter(Sculpture):
    def surface(self, verts, polys, color, binding, rough=None):
        if rough is None and isinstance(color, str):
            rough = ROUGHNESS.get(color)
        super().surface(verts, polys, color, binding, rough)


def horizontal_sections(mesh, sections, shape, paint, binding):
    """Sections perpendicular to the long axis of the muzzle or the ears."""
    # The upper lip overlaps the lower jaw in the profile. Keep those matching rings
    # in every LOD so their contact remains continuous as the camera pulls away.
    loops=[[(x, center_y+scale_y*y, center_z+scale_z*z) for y,z in shape]
           for x,center_y,center_z,scale_y,scale_z in sections]
    sides=len(shape)
    vertices=np.concatenate(loops)
    quads=[(r*sides+i,r*sides+(i+1)%sides,(r+1)*sides+(i+1)%sides,(r+1)*sides+i)
           for r in range(len(loops)-1) for i in range(sides)]
    quads.extend([tuple(reversed(range(sides))),tuple(range(len(vertices)-sides,len(vertices)))])
    mesh.surface(vertices,quads,paint,binding)


def strip_on_shell(mesh, faces, rows, paint, binding, *, offset=.7, back=False):
    # Clip the dye to the real source triangles so a mark cannot sink inside a convex
    # face where a lower-detail version has different angles or diagonal seams.
    direction = -1 if back else 1
    for i in range(len(rows)-1):
        z0, left0, right0 = rows[i]
        z1, left1, right1 = rows[i+1]
        limits = [
            lambda p: p[2]-z0,
            lambda p: z1-p[2],
            lambda p: p[1]-left0-(left1-left0)*(p[2]-z0)/(z1-z0),
            lambda p: right0+(right1-right0)*(p[2]-z0)/(z1-z0)-p[1],
        ]
        for f in faces:
            if direction * mesh.normals[f[0]][0] < .015:
                continue
            polygon = [np.asarray(mesh.vertices[j]) for j in f]
            if max(p[2] for p in polygon)<z0 or min(p[2] for p in polygon)>z1:
                continue
            for distance in limits:
                if not polygon:
                    break
                cut = []
                for a,b in zip(polygon,polygon[1:]+polygon[:1]):
                    da,db=distance(a),distance(b)
                    if da>=0:
                        cut.append(a)
                    if (da<0 and db>0) or (da>0 and db<0):
                        cut.append(a+(b-a)*da/(da-db))
                polygon=cut
            if len(polygon)>=3:
                shifted = [p+np.array([direction*offset,0,0]) for p in polygon]
                mesh.surface(shifted,[tuple(range(len(shifted)))],paint,binding)


def head(mesh):
    # Profile follows the large bony forehead forward to a long bridge. Cheekbones turn
    # away either side; the broad, bare nostril pad attaches well ahead of the jaw.
    rim = [(-26,126),(-24,138),(-15,145.6),(-5,148),(9,147.1),
           (17,143),(24,134),(29.5,127),(35.8,119),(35.4,111),
           (27.5,104),(12,101.2),(-5,104.2),(-19,111)]
    skull = [(x,0,z) for x,z in rim]
    side = [(-14,18,141),(-3,23.7,145),(11,24.0,143),(18,24.0,137),
            (22.3,23.4,132),(24,23,125),(26.2,19,119),(25,19,112),
            (15,21.4,107),(5,22.6,106),(-6,23.8,109),(-16,22.7,118),
            (-17,22,132),(-6,27.5,136.8),(6,28.9,132.5),
            (12,28.8,124),(7,27.8,116),(-5,26.8,116.3)]
    for sign in (-1,1):
        skull.extend((x,sign*y,z) for x,y,z in side)
    begin = len(mesh.faces)
    hull(mesh, skull, COAT, "head")
    shell = mesh.faces[begin:]
    strip_on_shell(mesh, shell, [
        (122.1,1.2,4.5),(125.8,-1.5,6.0),(130,-6.0,9.1),(134.8,-8.7,10.7),
        (139,-15.4,15.0),(142.5,-17.3,18.1),(144.8,-13.9,14.3),(146.5,-9.0,7.4),
    ], CREAM, "head", offset=.42)

    strip_on_shell(mesh, shell, [(111.,-12.5,-11.5),(115.5,-19.0,-8.2),
        (120.2,-22.,-9.7),(125.,-24.,-4.7),(132.5,-25.0,-6.),(137.,-23.0,-3.8),
        (141.,-19.,-8.0),(143.,-15.,-12.8)],"67594F","head",offset=.36,back=True)

    for sign, suffix in ((-1,"l"),(1,"r")):
        # Each eye has its own dark orbital rim and a heavy overhanging upper lid.
        # Their nearly level line gives the ox a patient look at gameplay distance.
        eye_center = sign * 18.4
        level = 131.3 + (.35 if sign < 0 else 0)
        def place(points):
            return [(eye_center + sign*dy, level+dz) for dy,dz in points]
        ink(mesh,shell,place([(-5.4,.6),(-4.1,-2.0),(-.7,-3.0),(2.6,-2.65),
                              (4.7,-.6),(5.0,1.15),(2.9,3.1),(-.1,3.6),(-3.6,2.65)]),
            "62544A", "head", .63)
        ink(mesh,shell,place([(-4,.2),(-2.8,-1.5),(-.35,-2.1),(2.3,-1.7),
                              (3.9,-.4),(4.15,.9),(1.7,1.8),(-1.4,1.8),(-3.4,1.2)]),
            EYE,"eyelid_"+suffix,1.03)
        ink(mesh,shell,place([(-2.1,-.65),(-.75,-.75),(-.65,.30),(-1.85,.55)]),
            "A69C86","eyelid_"+suffix,1.32)
        ink(mesh,shell,place([(-5.15,.8),(-2.3,1.35),(.75,1.25),(3.9,.63),(5.35,.2),
                              (5.1,2.0),(2.0,3.47),(-1.35,3.85),(-4.65,2.9)]),
            "806D59","brow_"+suffix,1.63)

    # Slightly upturned, soft cow muzzle, with the lower lip settling into a thinner ledge.
    jaw_shape = [(-.63,-1),(.56,-1),(.91,-.42),(1,.26),(.72,.77),
                 (.25,1),(-.5,.92),(-.94,.51),(-1,-.28)]
    horizontal_sections(mesh,[(19,0,109.3,16,6.6),(26,0,108.05,18.2,6.4),
                              (33.2,0,108.5,17.6,5.9),(39.9,0,109.3,12.7,3.9)],
                        jaw_shape,"837164","jaw")
    pad_shape = [(-.46,-.99),(.34,-1),(.73,-.82),(.98,-.34),(1,.25),
                 (.81,.68),(.44,1),(.04,.84),(-.40,.99),(-.79,.72),(-1,.27),(-.97,-.38),(-.78,-.77)]
    start = len(mesh.faces)
    horizontal_sections(mesh,[(26.4,0,118.1,11.2,7.7),(31.6,0,117.3,17.2,9.2),
                              (36.8,0,115.7,20.0,9.4),(41.0,0,115.05,20.4,8.7),
                              (43.8,0,114.7,17.6,6.7)],pad_shape,MUZZLE,"head")
    pad = mesh.faces[start:]
    for sign in (-1,1):
        ink(mesh,pad,[(sign*y,z) for y,z in [(8.1,116.9),(9.8,115.3),
            (12.6,115.5),(14.9,117.3),(15.0,119.2),(13.8,120.0),(11.25,119.4),(9.5,118.4)]],
            "5E534B","head",.48)
    ink(mesh,pad,[(-5.3,108.7),(1.0,108.3),(6.9,108.8),(9.0,109.7),
                 (6.5,109.3),(1,109.08),(-5.5,109.42)],"76665E","head",.58)

    for sign, suffix in ((-1,"l"), (1,"r")):
        # A heavy leaf-shaped ear points out and droops. Horn and ear have distinct silhouettes.
        ear = "ear_" + suffix
        outline = [(19.1,138.2),(25.8,138),(31,135.5),(38.6,130.6),(43.8,129.9),
                   (46.0,132.0),(44.4,136.1),(39.0,140.2),(30.0,142.4),(22,141.9)]
        if sign < 0:
            outline = [(y-.65*max(0,(y-24)/20),z+1.5*max(0,(y-25)/20)) for y,z in outline]
        yz = [(sign*y,z) for y,z in outline]
        if sign < 0:
            yz.reverse()
        cy,cz = np.mean(yz,axis=0)
        loops = [[(x-1.7*max(0,(abs(y)-22)/20),cy+k*(y-cy),cz+k*(z-cz))
                  for y,z in yz] for x,k in ((-11,.75),(-8.7,1.),(-4.6,.99),(-2.0,.76))]
        start = len(mesh.faces)
        mesh.solid(loops,COAT,ear)
        ear_front = mesh.faces[start:]
        mark = [(26.2,139.2),(30.0,137.0),(38.5,133.3),(42.,132.8),
                (39.5,135.75),(32.,138.9)]
        ink(mesh,ear_front,[(sign*y,z+(.5 if sign<0 else 0)) for y,z in mark],
            "907B6E",ear,.40)
        # Blunted, laterally swept horns rise only a few cm above the flat poll.
        mesh.tube([(-5.3,sign*18.9,142.0,5.9,5.),(-5.7,sign*22.7,144.6,6.,5.3),
                   (-6.7,sign*26.3,146.1,5.28,4.5)],"897B60","head",10)
        mesh.tube([(-5.8,sign*22.7,144.4,5.75,4.95),(-6.1,sign*26.1,146.5,5.33,4.5),
                   (-6.8,sign*31.25,148.,4.5,3.85),(-6.1,sign*35.6,149.2,3.52,3.15),
                   (-4.7,sign*39.1,151.2,2.64,2.47),(-2.7,sign*41.0,154.2,1.7,1.6),
                   (-2.,sign*41.3,155.,.95,.95)],HORN,"head",10)

    # Short forelock with a blunt, forward swept tip tucked into the white blaze.
    hull(mesh,[(-15,-4,145),(-12,5,147.5),(-5,8,148.5),(2,7,149.15),
               (6,4,148.5),(11,-1,146.7),(9,-1.8,145.9),(-2,-3.4,147.4),
               (-7,-7.0,147.45)],"B9A785","head")


def torso(mesh):
    core = blend("pelvis","chest",51,94)
    start = len(mesh.faces)
    mesh.tiers([(-1,0,52,19.0,23),(.4,0,60,25.6,28.6),(-.3,0,69,25.7,29.6),
                (-1.4,0,81,23.1,31.5),(-2.3,0,92.2,21.,33.0),(-3.5,0,98,18.3,33.4),
                (-4,0,101.7,15.7,27.6),(-3.8,0,104.4,12.1,16.0)],
               SHIRT,core,14,.77)
    mesh.tiers([(-1,0,46,18.5,21.8),(-.5,0,51.8,24,27),(.2,0,61,26.2,29.1),
                (-.2,0,65,25.1,28.7)],TROUSERS,core,12,.70)
    # The sleeveless vest curves away before reaching the dark blue throat. On the back,
    # the broad shoulder yoke runs uninterrupted between the two arm holes.
    bands = [(56.4,0,26.6,30.9,.31),(59.,0,27.3,31.2,.33),(66.,-.4,27.4,31.2,.34),
             (78.,-1.3,26.8,33.9,.39),(87.,-2.2,26.3,36,.44),
             (95.8,-3.2,23.7,38.2,.53),(100,-3.7,20.1,34,.64),
             (103.3,-3.9,14.8,24,.70)]
    sides = (20,16,12)[mesh.detail]
    verts = []
    for z,x,rx,ry,cut in bands:
        for i in range(sides+1):
            angle = cut + (math.tau-2*cut)*i/sides
            front = max(0,math.cos(angle))
            level = z - max(0,65-z)*.33*front
            verts.append((x + rx*signed_power(math.cos(angle),.76),
                          ry*signed_power(math.sin(angle),.74), level))
    stride = sides+1
    vest_start = len(mesh.faces)
    mesh.surface(verts,[(r*stride+j,r*stride+j+1,(r+1)*stride+j+1,(r+1)*stride+j)
                       for r in range(len(bands)-1) for j in range(sides)],CANVAS,core)
    vest = mesh.faces[vest_start:]
    vest_and_shirt = mesh.faces[start:]

    # Cut the oxhide apron around the hips and leave the knees free. Mesh follows the
    # belly across its full width even where the garment hangs clear of the trousers.
    apron_rows = [(36.8,-15,15,23.5),(40.5,-20.3,20.4,23.2),(51.,-24,23.2,24.0),
                  (56.,-25.5,24.2,25.1),(63.,-25.5,24.3,27.8),(69.8,-22.5,21.5,28.45),
                  (75.,-17.3,17.,28.35),(80.,-15.,14.7,27.7),(86.,-14.4,14.1,26.45),
                  (91.,-13.2,12.8,25.0),(92.3,-11.6,11.3,24.8)]
    stride = (13,11,9)[mesh.detail]
    layers = []
    for dz, inset in ((.1,-.7),(0.,0.)):
        sheet = []
        for index,(z,left,right,forward) in enumerate(apron_rows):
            for y in np.linspace(left,right,stride):
                # compensate the straight apron side's approach to the convex work shirt
                curved = forward - 8.2*(abs(y)/26.0)**2
                try:
                    curved = max(curved,project_x(mesh,vest_and_shirt,y,z)+1.15)
                except ValueError:
                    pass
                level = z + dz
                if index < 3:
                    level += (1-index*.35)*(2.1*abs(y)/24. - .6*y/24.)
                sheet.append((curved+inset,y,level))
        layers.append(sheet)
    layersize = len(layers[0]);nrows=len(apron_rows)
    quads=[(i*stride+j,i*stride+j+1,(i+1)*stride+j+1,(i+1)*stride+j)
           for i in range(nrows-1) for j in range(stride-1)]
    apron_start = len(mesh.faces)
    mesh.surface(layers[1],quads,APRON,core)
    edge = [*range(stride),*[r*stride+stride-1 for r in range(1,nrows)],
            *range(layersize-2,layersize-stride-1,-1),*[r*stride for r in range(nrows-2,0,-1)]]
    mesh.surface(layers[0]+layers[1],[(a,b,b+layersize,a+layersize)
                  for a,b in zip(edge,edge[1:]+edge[:1])],LEATHER,core)
    apron_shell = mesh.faces[apron_start:]
    # Hand-hammered work tag and a low, creased tool slot are offset from the centre line.
    ink(mesh,apron_shell,[(-18.,47.4),(-7.2,46.5),(-5.9,49.6),(-6,57.0),(-18.7,58.2)],
        "816345",core,.53)
    ink(mesh,apron_shell,[(-18.7,56.8),(-6.,55.7),(-6.,57.8),(-18.7,59.0)],
        "AD8860",core,.85)
    for sign in (-1,1):
        ink(mesh,vest_and_shirt,[(sign*y,z) for y,z in [(9.9,91.),(15.3,90.6),
            (20.8,99.6),(16.6,101.4),(13.5,97.6)]],"735A43",core,.84)
    ink(mesh,apron_shell,[(4,85.7),(10.2,85.4),(10.8,86.4),(10.4,89.25),
                          (4.4,89.6),(3.6,88.6)],BRASS,core,.72)

    # A back yoke and leather waist ties remain visible from the shoulder camera.
    strip_on_shell(mesh,vest,[(88.,-16.,16.),(91.,-25.,25.),(94.,-29.,29.),
        (97.5,-27.5,27.5),(99.8,-21.,21.)],"9D7B58",core,offset=.72,back=True)
    # The waist ties start at the sides and return around the back. The bib itself is
    # uninterrupted, as on a riveter's leather apron.
    tie_vertices=[]
    segments=(22,18,14)[mesh.detail]
    for z,rx,ry in ((60.3,28.7,32.8),(61.1,28.95,33.1),(64.0,29.,33.15),(64.6,28.7,32.8)):
        for a in np.linspace(.99,math.tau-.99,segments+1):
            tie_vertices.append((-.5+rx*signed_power(math.cos(a),.74),
                                 ry*signed_power(math.sin(a),.72),z))
    n=segments+1
    mesh.surface(tie_vertices,[(r*n+j,r*n+j+1,(r+1)*n+j+1,(r+1)*n+j)
                 for r in range(3) for j in range(segments)],LEATHER,core)
    for sign in (-1,1):
        # Dark blue short collar, low below the jowl.
        part=[(12,sign*8,102),(6,sign*16,104.2),(-3,sign*19.3,103.),
              (7,sign*21.7,99.6),(17.2,sign*17.4,93.7),(19.2,sign*13.4,91.8),
              (18.2,sign*10.2,96.1)]
        hull(mesh,part,"50606A","chest")
    mesh.tiers([(-1,0,99,13.8,17.4),(-1,0,104.5,14.6,17.3),
                (2,0,111.3,14.,16)],COAT,"neck",12,.8)


def arms(mesh, sign, suffix):
    upper, lower, hand = ("upperarm_"+suffix,"lowerarm_"+suffix,"hand_"+suffix)
    # Keep the shoulder and wrist on the crew skeleton. The extra breadth sits mainly
    # behind the ox's arm; his forearm consequently reads as solid in the side view.
    mesh.tube([(-3,sign*23,97.3,9.6,10),(-4,sign*30,97,12.9,12.1),
               (-3.7,sign*36.7,92.8,12.7,12.3),(-2.2,sign*40.0,87.3,11.4,11.0),
               (-1.1,sign*41.4,82.6,10.6,10.4)],
              SHIRT,upper,12)
    mesh.tube([(-1.7,sign*40.9,85.1,10.75,10.6),(-1.3,sign*41.3,83.8,11.4,11.4),
               (-.8,sign*41.7,81.8,11.1,11.15)],"5B6469",upper,12)
    mesh.tube([(-1.0,sign*41,83,8.8,8.7),(1.4,sign*42.7,77.5,10.35,9.6),
               (3.4,sign*44.35,72.6,10.9,10),(5.75,sign*45.4,66.2,9.8,8.8),
               (8.6,sign*45.6,57.4,8.2,7.5)],COAT,
              blend(lower,upper,75,83),11)
    # The broad, tapered bracer protects the wrist; woven dark edges frame the hide.
    mesh.tube([(5.3,sign*44.9,69.0,10.15,9.45),(5.8,sign*45.3,67.8,10.7,9.75),
               (7.85,sign*45.7,59.4,9.15,8.45),(8.45,sign*45.7,58.2,8.4,7.85)],
              "735B47",lower,11)
    mesh.tube([(5.3,sign*45,68.9,10.26,9.60),(5.6,sign*45.15,68.35,10.75,9.93),
               (5.96,sign*45.3,66.9,10.57,9.83)],"574B40",lower,11)

    mesh.tiers([(10.0,sign*46.4,46.4,6.9,6.1),(10.2,sign*46.3,49.7,9.65,8.25),
                (9.65,sign*46.1,53.7,10.,8.7),(8.7,sign*45.7,58.3,8.6,8.0),
                (8.1,sign*45.75,60.7,7.6,7.6)],COAT,hand,10,.67)
    # Two distinct keratin knuckles meet at a dark cleft, each with a worn polished bevel.
    for finger in (-1,1):
        x = 10.25 + finger*4.1
        mesh.tiers([(x+.32,sign*46.5,43.6+(.5 if finger<0 else 0),2.98,5.55),
                    (x+.15,sign*46.6,44.8,3.85,7.0),(x,sign*46.6,48.2,4.05,7.55),
                    (x-.27,sign*46.3,51.1,4.35,7.45),(x-.5,sign*46.2,52.1,3.7,6.9)],
                   HOOF,hand,10,.66)
    mesh.tube([(12.0,sign*40.65,55.0,4.,3.8),(15.85,sign*39.2,53.9,4.1,4.1),
               (17.8,sign*39.6,50.0,3.5,3.6),(17.3,sign*40.2,47.6,2.55,2.6)],
              DARK_COAT,hand,8)
    mesh.tube([(17.25,sign*39.7,51.,3.47,3.55),(18,sign*40,48.9,3.12,3.13),
               (17.55,sign*40.35,47.,2.0,2.05)],HOOF,hand,8)


def legs(mesh, sign, suffix):
    thigh,knee,foot = "thigh_"+suffix,"calf_"+suffix,"foot_"+suffix
    mesh.tiers([(-1,sign*18.6,26.6,11.7,12.1),(-.7,sign*18.45,31.6,13.2,13.8),
                (.2,sign*17.6,38.8,15.0,15.2),(.1,sign*16.3,45.9,15.4,16.0),
                (.3,sign*14.7,52.4,14.1,14.7),(.2,sign*14.6,56.4,11.4,12.2)],
               TROUSERS,thigh,11,.70)
    mesh.tiers([(-.4,sign*19.3,20.0,9.0,9.4),(-.1,sign*19,22.2,11.0,11.1),
                (-1.2,sign*18.8,28.2,12.5,12.4),(-.8,sign*18.4,32.7,13.6,13.3),
                (-.3,sign*18.2,35,11.5,11.7)],TROUSERS,blend(knee,thigh,28,35),11,.72)
    mesh.tiers([(-.1,sign*19.0,19.6,11.2,11.),(-.1,sign*19.,20.2,11.9,11.65),
                (-.25,sign*19,23.4,12.05,11.85)],"626661",knee,11,.72)
    # Wide shipyard boots: a thick rubber sole, iron-dark toe cap and tight shaft.
    mesh.tiers([(6.9,sign*20.1,.6,16.1,11.9),(7.5,sign*20.1,1.3,17.9,12.5),
                (7.45,sign*20.1,4.65,17.9,12.55),(7.35,sign*20.1,5.2,17.5,12.4)],
               "464741",foot,12,.61)
    boot_start=len(mesh.faces)
    mesh.tiers([(7.4,sign*20,4.3,17.65,12.4),(7.6,sign*20,6.6,17.65,12.45),
                (6.7,sign*19.9,9.6,16.25,12.15),(3.9,sign*19.8,12.25,13.3,11.6),
                (1.15,sign*19.5,14.3,11.45,11.0),(.1,sign*19.2,19.9,9.95,9.95),
                (.1,sign*19.1,22.1,9.9,10.0)],"665B4D",foot,12,.64)
    boot = mesh.faces[boot_start:]
    center=sign*20
    strip_on_shell(mesh,boot,[(5.5,center-8.5,center+8.5),(7.6,center-9.2,center+9.2),
                              (8.9,center-8.4,center+8.4),(9.7,center-4.9,center+4.9)],
                   "6F6556",foot,offset=.26)


def tail(mesh):
    path = [(-17.5,0,61.8,4.15,4.25),(-23.5,0,60.0,4.05,4.10),
            (-28.5,1.,55.0,3.45,3.42),(-30.3,2.8,48.2,2.75,2.75),
            (-29.8,5.2,41.8,2.3,2.35),(-28.9,8.0,35.9,1.9,1.9),
            (-26.8,10.2,31.8,1.63,1.7)]
    mesh.tube(path,COAT,blend("tail_02","tail_01",45,56),9)
    mesh.tube([(-27.9,9.2,33.4,1.95,2.),(-26.6,10.6,31.4,3.0,3.15),
               (-25.7,11.,28.5,3.2,3.),(-24.5,11.6,25.65,2.15,2.35),
               (-23.2,12.4,23.8,.6,.7)],"534C44","tail_02",8)


def character(detail=0):
    mesh = Riveter(detail)
    torso(mesh)
    for sign,suffix in ((-1,"l"),(1,"r")):
        arms(mesh,sign,suffix)
        legs(mesh,sign,suffix)
    tail(mesh)
    head(mesh)
    return mesh


def main():
    output = HERE / "Calf"
    output.mkdir(exist_ok=True)
    for detail in range(3):
        model = character(detail)
        suffix = f"_LOD{detail}" if detail else ""
        model.glb(output/f"SK_ExpeditionCalf{suffix}.glb")
        print(f"LOD {detail}: {len(model.faces)} triangles, {len(BONES)} bones")
        if not detail:
            hand = fist(model)
            hand.glb(output/"SM_CalfFist.glb",skinned=False)
            print(f"First-person hoof: {len(hand.faces)} triangles")


if __name__ == "__main__":
    main()
