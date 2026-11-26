"""Bake the expedition harbour scenery with UE's PythonScript and GeometryScripting plugins."""
from math import cos, radians, sin, sqrt, tau
from pathlib import Path
import sys
import unreal as ue

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from static_mesh import Facets

DEST = "/Game/TreasureIsland/Art/Harbour"
OAK, CEDAR, WALNUT = "B78C64", "A77954", "735541"
TAR, RED, FLAX = "4B4F4D", "B35443", "D7BE91"
JADE, TEAL, BRASS = "78AD9F", "447F7E", "C89551"
BASALT, SHALE, SALT, LICHEN = "677B7D", "89989A", "C7BFA4", "839578"
LIMESTONE, CHALK, SANDSTONE, CUTSTONE = "BCB398", "DBD0AC", "A88F73", "C3AD89"
IRON, COPPER, BONE, MARROW = "647176", "AC7350", "DBCFAB", "AB9B7D"
PAINT_ROUGHNESS={BRASS:.34, COPPER:.40, IRON:.44, JADE:.62, TEAL:.63, RED:.68,
                 OAK:.80, CEDAR:.83, WALNUT:.86, TAR:.76, FLAX:.96, BONE:.70}


def tint(pigment, factor=1.0):
    rgb = [min(1, factor * int(pigment[i:i+2], 16) / 255) for i in (0, 2, 4)]
    return ue.LinearColor(*rgb, PAINT_ROUGHNESS.get(pigment,.91))


def rectangle(rx, ry, corner=.15):
    x, y = rx * (1 - corner), ry * (1 - corner)
    return [(x,ry), (-x,ry), (-rx,y), (-rx,-y), (-x,-ry), (x,-ry), (rx,-y), (rx,y)]


def ellipse(rx, ry, sides=8, angle=0):
    return [(rx*cos(radians(angle + 360*i/sides)), ry*sin(radians(angle + 360*i/sides))) for i in range(sides)]


def offset(points, dx=0, dy=0):
    return [(x + dx, y + dy) for x,y in points]


class Mesh(Facets):
    def painted_face(self, points, color, shade=1.0):
        super().face(*points)
        self.colors.extend([tint(color, shade)] * len(points))

    def loft(self, levels, sides, cap):
        """Connect equal-count CCW contours. Shared corners retain a flat face normal."""
        rings = [[(x,y,z) for x,y in rim] for z,rim in levels]
        self.painted_face(list(reversed(rings[0])),sides[0],.82)
        for course, (low,high) in enumerate(zip(rings, rings[1:])):
            for i in range(len(low)):
                j = (i+1)%len(low)
                shade = (1., .97, .93, 1.02, .96, 1.)[(3*course+i)%6]
                self.painted_face([low[i],low[j],high[j],high[i]],sides[course],shade)
        self.painted_face(rings[-1],cap)

    def block(self, x,y,z, sx,sy,sz, color, edge=None, cut=.12):
        outer = offset(rectangle(sx/2,sy/2,cut),x,y)
        inner = offset(rectangle(sx*.486,sy*.486,cut),x,y)
        self.loft([(z-sz/2,outer),(z+sz*.38,outer),(z+sz/2,inner)],[color, edge or color],color)

    def paint_top(self, points, z, pigment):
        self.painted_face([(x,y,z) for x,y in points],pigment)

    def spar(self, a, b, base_radius, tip_radius, color, *, sides=6, scale=(1,1,1)):
        # Some meshes are stretched eighteen times more vertically than horizontally.
        # Size the cross section in the scaled frame so a brace still looks like timber.
        def unit(vector):
            length=sqrt(sum(c*c for c in vector))
            return tuple(c/length for c in vector)
        def cross(u,v):
            return (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        axis=unit(tuple((b[k]-a[k])*scale[k] for k in range(3)))
        normal=unit(cross(axis,(0,0,1) if abs(axis[2])<.93 else (1,0,0)))
        across=cross(axis,normal)
        def ring(center,r):
            return [tuple(center[k]+r*(cos(tau*i/sides)*normal[k]+sin(tau*i/sides)*across[k])/scale[k]
                          for k in range(3)) for i in range(sides)]
        low,high=ring(a,base_radius),ring(b,tip_radius)
        self.painted_face(list(reversed(low)),color,.90)
        for i in range(sides):
            j=(i+1)%sides
            self.painted_face([low[i],low[j],high[j],high[i]],color,(.98,1.02,.94)[i%3])
        self.painted_face(high,color)

    def pennant(self, points, color, thickness=.8):
        # A closed, folded piece of fabric can be scooped away wherever it is hit.
        area=sum(p[1]*points[(i+1)%len(points)][2] - p[2]*points[(i+1)%len(points)][1]
                 for i,p in enumerate(points))
        if area>0:
            points=list(reversed(points))
        front=[(x-thickness/2,y,z) for x,y,z in points]
        back=[(x+thickness/2,y,z) for x,y,z in points]
        self.painted_face(front,color,.97)
        self.painted_face(list(reversed(back)),color)
        for i in range(len(points)):
            j=(i+1)%len(points)
            self.painted_face([front[i],back[i],back[j],front[j]],color,.85)


def jetty():
    m=Mesh()
    # The centre is reserved for eight arrivals. Posts, chocks and the boarding
    # rope live on the outer corners, leaving the inland end walkable at deck height.
    m.block(0,0,-15,99.0,97.6,70,WALNUT,cut=.055)
    for i in range(14):
        y=-46.8+(i+.5)*93.6/14
        seam=(-23.,7.,-8.,22.,-15.)[i%5]
        for j,(lo,hi) in enumerate(((-49.8,seam-.12),(seam+.12,49.8))):
            m.block((lo+hi)*.5,y,28.,hi-lo,93.6/14-.16,44.,
                    (OAK,"B9916B",CEDAR,"A48364","C09B76")[(2*i+j*3)%5],cut=.035)
    for side in (-1,1):
        m.block(0,side*47.8,6.,98.,3.2,57.,WALNUT,cut=.12)
        m.block(0,side*48.7,36.,98.,2.1,27.,TEAL,cut=.10)
        for x in (-40.,29.):
            y=side*45.7
            for z0,z1,r0,r1,col in [(-48,92,1.65,1.55,CEDAR),(90,112,1.77,1.77,FLAX),
                                      (111,155,1.55,1.44,WALNUT),(154,172,1.52,1.21,RED)]:
                m.loft([(z0,offset(ellipse(r0,r0*1.30,8,19),x,y)),
                        (z1,offset(ellipse(r1,r1*1.30,8,19),x,y))],[col],col)
        rope=[(-40,side*45.7,115),(-35,side*42.0,72),(-31,side*42.5,53),(-23,side*45.8,51)]
        for a,b in zip(rope,rope[1:]):
            m.spar(a,b,.30,.30,FLAX,sides=5,scale=(1,.765,.047))
    # Set into the boards; the split chevron points inland along local +X.
    for x in (32.,38.):
        m.paint_top([(x-3,-6),(x-1,-6),(x+2,0),(x-1,6),(x-3,6),(x,0)],50.8,FLAX)
    return m


def timber(cargo=False, round_post=False):
    m=Mesh()
    if cargo:
        m.block(0,0,-2,97,95,94,WALNUT)
        for i in range(5):
            m.block(-38+i*19,0,0,18.5,97.5,88,(OAK,CEDAR,"AE845D")[i%3],cut=.06)
            m.block(-38+i*19,0,46,18.3,96,8,(OAK,CEDAR,"BC946B")[i%3],cut=.055)
        for y in (-49,49):
            for x in (-35,35):
                m.block(x,y,0,7,4,97,TEAL)
            for z in (-43,39):
                m.block(0,y,z,97,3.5,9,CEDAR,FLAX)
            m.spar((-33,y,34),(32,y,-34),4.6,4.3,OAK,sides=4)
        for x in (-35,35):
            m.block(x,0,48.5,7,99,4,TEAL)
        return m
    if round_post:
        levels=[(-50,41),(-44,45),(-16,42),(-14,40),(21,36),(36,37),(39,47),(46,49),(50,40)]
        m.loft([(z,ellipse(w,w,9,20)) for z,w in levels],
               [TAR,WALNUT,TEAL,CEDAR,CEDAR,WALNUT,OAK,FLAX],OAK)
        # A continuous spiral wraps around the cleat, with the bitter end hanging down.
        strand=[]
        for i in range(27):
            t=i*tau/9
            strand.append((39*cos(t),39*sin(t),22.+i*.41))
        strand.extend([(39,-6,22),(40,-12,13)])
        for a,b in zip(strand,strand[1:]):
            m.spar(a,b,6.3,6.3,FLAX,sides=5,scale=(1,1,5.55))
    else:
        m.block(0,0,-1,96,100,98,OAK,cut=.17)
        for y in (-39,33):
            m.block(0,y,0,99,4,100,IRON,BRASS,cut=.20)
    return m


def jetty_board():
    m=Mesh()
    outline=[(47,41),(40,48),(9,49),(-18,47),(-36,50),(-48,44),
             (-49,-42),(-31,-48),(-11,-49),(28,-48),(47,-44)]
    m.loft([(-50,[(x*.96,y*.99) for x,y in outline]),(-38,outline),(37,outline),
            (50,[(x*.94,y*.993) for x,y in outline])],[WALNUT,CEDAR,OAK],OAK)
    # Sun checks in the cut end of a plank. Keep them off the central walking path.
    for y in (-44,44):
        for x in (-23,23):
            m.paint_top(offset(ellipse(2.75,.62,6),x,y),50.65,IRON)
    m.paint_top([(-19,-47),(-14,-45),(-17,-17),(-20,-29)],50.36,WALNUT)
    m.paint_top([(26,48),(23,46),(25,29),(28,24)],50.36,CEDAR)
    return m


def crane_mast():
    m=Mesh()
    levels=[(-50,46),(-48,50),(-38,50),(-36,43),(25,33),(27,39),(31,38),(33,33),(47,33),(50,27)]
    m.loft([(z,rectangle(w,w,.27)) for z,w in levels],
           [IRON,TEAL,BRASS,CEDAR,WALNUT,TEAL,BRASS,OAK,FLAX],OAK)
    scale=(1,1,11.31)
    for x in (-24,24):
        m.spar((x,20,12),(x,190,44),15.5,10.5,WALNUT,scale=scale,sides=5)
        m.spar((x,130,32),(x,128,44),7.1,6.9,OAK,scale=scale)
    m.spar((-33,20,13),(33,20,13),8.,8.,BRASS,scale=scale,sides=8)
    return m


def arch_pier():
    """Large interlocked, slightly offset stones with a dark footing and chipped edges."""
    m=Mesh()
    m.loft([(-50,rectangle(48,48,.29)),(-44,rectangle(50,50,.27)),
            (-28,offset(rectangle(45,45,.29),-2))],[BASALT,SHALE],SHALE)
    # A stone core bridges eroded joints, so damage can travel between courses.
    m.block(0,0,10,60,60,80,SANDSTONE,cut=.30)
    for k,(low,top,rx,ry,dx,dy,col) in enumerate([
            (-30,-11,46,45,-1,1,"AFA88F"),(-12,10,42,45,1,0,LIMESTONE),
            (9,29,44,40,2,-2,SALT),(28,44,40,43,-2,-1,"B6AD94"),
            (43,50,46,47,0,0,CHALK)]):
        m.loft([(low,offset(rectangle(rx-2,ry-2,.25),dx,dy)),
                (low+2,offset(rectangle(rx,ry,.19+.035*(k%3)),dx,dy)),
                (top-2,offset(rectangle(rx-1,ry-2,.28),dx,dy)),
                (top,offset(rectangle(rx-4,ry-4,.31),dx,dy))],
               [SANDSTONE,col,SALT],SALT)
    return m


def arch_lintel():
    m=Mesh()
    # The east pier is 90 cm shorter and further from the lintel origin. Its
    # haunch descends to that actual shoulder. Joints radiate from the archway.
    m.block(-7,7,23,57,115,35,"AC9F83",cut=.04)
    divisions=[-50,-34,-18,-4,13,31,49,66]
    for i in range(7):
        lo,hi=divisions[i:i+2]
        def ring(y):
            t=abs(y)/(66 if y>0 else 50)
            z=3-(140 if y>0 else 80)*t*t
            inner_y=y*.88
            if i==3: z-=7
            x=48.5 if i==3 else 45.5
            top=(56 if i==3 else 49)-13*t
            return [(-x+3,inner_y,z),(-x,inner_y,z+5),(-x,y,top-7),(-x+5,y,top),
                    (x-6,y,top),(x,y,top-6),(x,inner_y,z+6),(x-5,inner_y,z)]
        near,far=ring(lo+.15),ring(hi-.15)
        # A run along Y; this profile travels clockwise as seen from +Y.
        col=(LIMESTONE,SALT,"BAB097",CHALK,"B8AA8B",SALT,LIMESTONE)[i]
        m.painted_face(list(reversed(near)),SANDSTONE)
        for j in range(8):
            k=(j+1)%8
            m.painted_face([near[j],near[k],far[k],far[j]],(SANDSTONE,col,SALT,SALT,CHALK,col,SALT,SANDSTONE)[j])
        m.painted_face(far,SANDSTONE)
    return m


def signal_needle(short=False):
    m=Mesh()
    # A solid weather-beaten sea mark at its base, a timber frame open to the sky above.
    cut=-5 if short else -14
    m.loft([(-50,ellipse(46,46,7,12)),(-46,ellipse(50,49,7,12)),
            (-30,ellipse(41,42,7,12)),(cut-4,ellipse(32,34,7,12)),
            (cut,ellipse(37,38,7,12))],[BASALT,SHALE,LIMESTONE,SALT],SALT)
    frame=(1,1,4.3 if short else 5.9)
    top=36 if short else 39
    for x,y in [(-1,-1),(-1,1),(1,-1),(1,1)]:
        m.spar((x*22,y*22,cut-2),(x*16,y*16,top+4),6.4,4.5,
               WALNUT if y<0 else CEDAR,scale=frame)
    for side in (-1,1):
        m.spar((-21,side*21,cut+3),(17,side*17,top-7),3.6,2.6,OAK,scale=frame)
        m.spar((side*20,-20,cut+6),(side*17,17,top-2),3.7,3.,RED if short else OAK,scale=frame)
    m.loft([(top-2,rectangle(24,23,.18)),(top+1,rectangle(28,26,.18)),
            (top+4,rectangle(26,25,.18)),(top+5,rectangle(24,23,.18))],
           [WALNUT,TEAL,BRASS],OAK)
    if short:
        m.spar((0,0,top+2),(0,0,54),5.4,4.0,IRON,scale=frame)
        # Tattered two-tone signal flag, folded on its sewn edge.
        m.pennant([(0,0,53),(0,0,42),(-2,39,44),(-2,35,52)],RED,1.4)
        m.pennant([(-2,35,52),(-2,39,44),(-7,59,46),(-7,47,49),(-7,62,53)],FLAX,1.2)
    else:
        m.loft([(top+3,ellipse(20,21,8)),(48,ellipse(16,17,8)),(50,ellipse(18,18,8))],
               [IRON,BRASS],BRASS)
    return m


def signal_crown():
    m=Mesh()
    m.loft([(-50,ellipse(20,20)),(-17,ellipse(33,33)),(0,ellipse(49,49)),
            (10,ellipse(46,46))],[IRON,COPPER,BRASS],TEAL)
    # A bronze compass rose, its long arrow deliberately breaking the circular rim.
    m.spar((0,0,6),(0,0,96),5.,3.8,COPPER,scale=(1,1,.24))
    m.pennant([(0,-52,69),(0,12,65),(0,12,82),(0,-29,82)],BRASS,5.)
    m.pennant([(0,11,65),(0,59,41),(0,45,84),(0,11,82)],TEAL,4.)
    m.loft([(88,ellipse(6,6,6)),(105,ellipse(8,8,6)),(119,ellipse(2,2,6))],[BRASS,BRASS],BRASS)
    return m


def quarry(hoist=False):
    m=Mesh()
    # The stair tread is continuous; the split joints and exposed beds read on its tall ends.
    outline=[(47,37),(39,49),(-30,50),(-47,43),(-50,25),(-48,-35),(-34,-49),(33,-47),(49,-34)]
    for j,(bottom,top,ox,col) in enumerate([(-50,-28,-1,BASALT),(-29,4,0,"A2927B"),(3,19,-.8,"AA9577"),
                                           (18,42,0,SANDSTONE),(41,50,0,CUTSTONE)]):
        def shelf(s):
            return [(x*s+ox,y*s) for x,y in outline]
        m.loft([(bottom,shelf(.96)),(bottom+2,shelf(1.)),(top-2,shelf(.994)),(top,shelf(.97))],
               [SHALE if not j else SANDSTONE,col,CUTSTONE if j==4 else col],col)
    if hoist:
        # The old quarry derrick occupies the rear corner of the highest landing.
        # Its raking boom and empty hook distinguish the quarry at walking distance.
        scale=(1,2.81,3.39)
        m.spar((-29,-31,47),(-26,-22,87),5.1,3.6,WALNUT,scale=scale)
        m.spar((26,-32,47),(-26,-22,87),4.7,3.0,OAK,scale=scale)
        m.spar((-26,-22,85),(-26,6,98),5.2,3.4,CEDAR,scale=scale)
        m.spar((-26,-27,61),(-26,-5,93),3.1,2.8,OAK,scale=scale)
        m.spar((-26,4,98),(-26,4,68),.94,.94,FLAX,scale=scale)
        m.spar((-26,4,69),(-22,4,64),1.9,1.8,IRON,scale=scale)
        m.spar((-22,4,64),(-18,4,68),1.8,1.2,IRON,scale=scale)
        m.block(18,-18,55,21,12,12,SALT,cut=.15)
    return m


def fossil_fin():
    m=Mesh()
    # A swept, scalloped crest. Each station has a thick central spine, a sharp
    # leading edge and a weathered trailing edge; the tip leans farther as it tapers.
    def blade(stations,root_color):
        rings=[]
        for z,front,back,width in stations:
            length=back-front
            rings.append([(x,y,z) for x,y in [(3,back),(-width*.70,front+.76*length),
                           (-width,front+.37*length),(-1,front),(width*.56,front+.09*length),
                           (width,front+.38*length),(width*.65,front+.74*length)]])
        m.painted_face(list(reversed(rings[0])),root_color)
        for i,(lo,hi) in enumerate(zip(rings,rings[1:])):
            for j,color in enumerate([MARROW,BONE,CHALK,SALT,BONE,"CBBC9B",MARROW]):
                m.painted_face([lo[j],lo[(j+1)%7],hi[(j+1)%7],hi[j]],root_color if i==0 else color)
        m.painted_face(rings[-1],BONE)
    blade([(-50,-49,36,39),(-39,-45,37,45),(-22,-40,29,38),(-10,-31,35,32),
           (-4,-30,18,26),(8,-22,24,24),(20,-10,33,18),(35,4,43,11),
           (44,19,47,6),(50,40,48,3)],SHALE)
    blade([(-50,-16,49,26),(-43,5,49,28),(-32,14,43,20),(-16,21,47,10),(-7,39,49,3)],BASALT)
    return m


def fossil_heel():
    m=Mesh()
    m.loft([(-50,ellipse(46,46,8,17)),(-32,ellipse(50,49,8,17)),
            (13,offset(ellipse(47,43,8,17),-1)),(30,offset(ellipse(40,38,8,17),-4))],
           [BASALT,SHALE,"A5977E"],SANDSTONE)
    # Exposed vertebrae emerge from the same rock that carries the sail.
    for i,(x,y,h) in enumerate([(-23,-15,52),(-3,1,57),(18,13,46)]):
        base=offset(ellipse(14,19,7,12),x,y)
        top=offset(ellipse(11,17,7,12),x+1,y+1)
        m.loft([(19,base),(h-6,base),(h,top)],[MARROW,BONE],BONE)
    return m


def cairn():
    m=Mesh()
    m.loft([(-50,ellipse(39,38,7,7)),(-35,ellipse(49,46,7,7)),(-22,ellipse(47,44,7,7)),
            (-12,offset(ellipse(34,35,7,7),-3))],[BASALT,"85918A",SHALE],SHALE)
    m.loft([(-15,offset(ellipse(28,30,7,31),-4)),(-5,offset(ellipse(42,39,7,31),-6)),
            (8,offset(ellipse(37,32,7,31),-6)),(13,offset(ellipse(23,22,7,31),-11))],
           [SANDSTONE,LIMESTONE,SALT],SALT)
    # An eccentric flat cap points along the coast and is visible at player height.
    profile=[(42,0),(10,23),(-22,24),(-36,9),(-28,-17),(1,-23),(22,-13)]
    m.loft([(10,[(x*.65-3,y*.65) for x,y in profile]),(24,profile),
            (32,[(x*.96,y*.90) for x,y in profile]),(39,[(x*.65-4,y*.62) for x,y in profile])],
           [BASALT,TEAL,JADE],SHALE)
    m.loft([(36,offset(ellipse(13,12,6),-9)),(44,offset(ellipse(18,14,6),-8)),
            (50,offset(ellipse(10,9,6),-10))],[SANDSTONE,CHALK],CHALK)
    return m


def trail_beacon():
    m=Mesh()
    m.loft([(-50,ellipse(39,40,7,21)),(-43,ellipse(49,48,7,21)),(-31,ellipse(40,38,7,21)),
            (-25,ellipse(30,30,7,21))],[BASALT,SHALE,SALT],SALT)
    m.loft([(-29,ellipse(26,25,8)),(4,offset(ellipse(21,19,8),-2)),
            (13,offset(ellipse(18,19,8),1))],[CEDAR,OAK],OAK)
    frame=(1,1,3.82)
    m.spar((-2,4,7),(1,27,42),13.,8.5,OAK,scale=frame)
    m.spar((2,-6,3),(-4,-28,51),13.,8.,WALNUT,scale=frame)
    m.spar((-4,-28,46),(-4,-28,52),9.8,8.2,RED,scale=frame)
    m.spar((2,22,36),(2,26,43),10.,8.8,BRASS,scale=frame)
    m.spar((-2,-22,35),(1,23,34),4.,3.3,FLAX,scale=frame)
    m.pennant([(-2,-18,35),(2,3,22),(-1,21,34)],TEAL,3.)
    m.loft([(0,ellipse(22,22,8)),(4,ellipse(22,22,8)),(6,ellipse(20,20,8))],
           [FLAX,TEAL],TEAL)
    return m


def islet(headland=False):
    m=Mesh()
    levels=[(-50,37,40,-3),(-30,50,46,0),(5,48,49,1),(17,47,46,1),(32,42,43,0),
            (48,35,38,-2),(50,32,36,-2)]
    m.loft([(z,offset(ellipse(rx,ry,9,9),dx)) for z,rx,ry,dx in levels],
           [BASALT,SHALE,SANDSTONE,SALT,LICHEN,"8EAA7B"],"91AB7B")
    if headland:
        # A saddle between two differently eroded bluffs breaks the horizon.
        for x,y,height,rad in [(-13,12,76,18),(12,4,63,15)]:
            m.loft([(44,offset(ellipse(rad*1.48,rad,7,18),x,y)),
                    (height-10,offset(ellipse(rad,rad*.86,7,18),x+2,y-2)),
                    (height,offset(ellipse(rad*.69,rad*.59,7,18),x+4,y-3))],
                   ["969B81",LICHEN],"92A27E")
    return m
    return m


def make_paint():
    name="M_SceneryPaint"
    path=f"{DEST}/{name}"
    edit=ue.MaterialEditingLibrary
    if ue.EditorAssetLibrary.does_asset_exist(path):
        asset=ue.load_asset(path)
    else:
        asset=ue.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST,ue.Material,ue.MaterialFactoryNew())
    rough=edit.get_material_property_input_node(asset,ue.MaterialProperty.MP_ROUGHNESS)
    metal=edit.get_material_property_input_node(asset,ue.MaterialProperty.MP_METALLIC)
    if isinstance(rough,ue.MaterialExpressionVertexColor) and isinstance(metal,ue.MaterialExpressionSaturate):
        return asset
    colors=edit.create_material_expression(asset,ue.MaterialExpressionVertexColor,-650,-100)
    # Pigments are authored in display RGB. VertexColor passes their stored channel values
    # directly into the shader, while a lit Base Color input expects linear reflectance.
    linear=edit.get_material_property_input_node(asset,ue.MaterialProperty.MP_BASE_COLOR)
    if not isinstance(linear,ue.MaterialExpressionPower):
        linear=edit.create_material_expression(asset,ue.MaterialExpressionPower,-320,0)
        linear.set_editor_property("const_exponent",2.2)
        assert edit.connect_material_expressions(colors,"",linear,"Base")
        assert edit.connect_material_property(linear,"",ue.MaterialProperty.MP_BASE_COLOR)
    # Alpha travels with the split triangles and newly exposed cuts. Only
    # metal swatches have roughness below .48; wood/stone stay dielectric.
    assert edit.connect_material_property(colors,"A",ue.MaterialProperty.MP_ROUGHNESS)
    bias=edit.create_material_expression(asset,ue.MaterialExpressionSubtract,-410,210)
    bias.set_editor_property("const_a",.48)
    assert edit.connect_material_expressions(colors,"A",bias,"B")
    gain=edit.create_material_expression(asset,ue.MaterialExpressionMultiply,-220,210)
    gain.set_editor_property("const_b",6.)
    assert edit.connect_material_expressions(bias,"",gain,"A")
    metal=edit.create_material_expression(asset,ue.MaterialExpressionSaturate,-42,210)
    assert edit.connect_material_expressions(gain,"",metal,"")
    assert edit.connect_material_property(metal,"",ue.MaterialProperty.MP_METALLIC)
    edit.recompile_material(asset)
    ue.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def bake(name, mesh, shape, paint):
    path=f"{DEST}/SM_{name}"
    asset = mesh.write(path,shape)
    asset.set_material(0,paint)
    if not ue.EditorAssetLibrary.save_loaded_asset(asset,False):
        raise RuntimeError("Failed to save "+path)
    ue.log(f"HARBOUR_BAKED {name} triangles={len(mesh.triangles)} collision={shape}")


def main():
    ue.EditorAssetLibrary.make_directory(DEST)
    paint=make_paint()
    for name,model,shape in (
        ("Landing",jetty(),"Cube"), ("Plank",timber(),"Cube"), ("JettyBoard",jetty_board(),"Cube"), ("CraneMast",crane_mast(),"Cube"), ("Mooring",timber(round_post=True),"Cylinder"),
        ("Crate",timber(cargo=True),"Cube"), ("ArchStone",arch_pier(),"Cube"),
        ("ArchLintel",arch_lintel(),"Cube"), ("SignalNeedle",signal_needle(),"Cylinder"),
        ("SignalMast",signal_needle(short=True),"Cylinder"), ("SignalDish",signal_crown(),"Cylinder"),
        ("QuarryStep",quarry(),"Cube"), ("QuarryDerrick",quarry(hoist=True),"Cube"),
        ("Fin",fossil_fin(),"Cube"), ("FossilHeel",fossil_heel(),"Cube"),
        ("Cairn",cairn(),"Cube"), ("TrailBeacon",trail_beacon(),"Cylinder"),
        ("Islet",islet(headland=True),"Cube"), ("ReefShelf",islet(),"Cylinder")):
        bake(name,model,shape,paint)


if __name__ == "__main__":
    main()
