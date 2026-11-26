"""Bake the four working expedition mechanisms' flat-shaded voxel source meshes."""
import math
import sys
from pathlib import Path
import unreal as ue

sys.path.insert(0, str(Path(__file__).parent))
from neutral_meshes import Facets, DEST, bands, painted_face, pigment, radial_ring

TAU = math.tau


def face(mesh, points, rgb, metal=False):
    mesh.face(*points)
    color = pigment(rgb)
    color.a = .40 if metal else 1.
    mesh.colors.extend([color] * len(points))


def face_xz(mesh, polygon, back, front, paint, edge, metal=False):
    """A closed cross-section in XZ. The outline runs counter-clockwise from -Y."""
    face(mesh, [(x, back, z) for x, z in polygon], paint, metal)
    face(mesh, [(x, front, z) for x, z in reversed(polygon)], paint, metal)
    for (a, b) in zip(polygon, polygon[1:] + polygon[:1]):
        face(mesh, [(a[0], back, a[1]), (a[0], front, a[1]),
                    (b[0], front, b[1]), (b[0], back, b[1])], edge, metal)


def wind_crown():
    # Irregular wind-pruned shelves with a deep, folded underside. The tips on
    # the lee side project beyond the body and break up its silhouette.
    mesh = Facets()
    count = 12
    outline = [(51,0),(56,15),(38,20),(35,42),(11,33),(-6,49),
               (-21,33),(-44,31),(-38,8),(-49,-18),(-19,-35),(26,-39)]
    outline = outline[::-1] if sum(x*y2-x2*y for (x,y),(x2,y2) in zip(outline,outline[1:]+outline[:1]))<0 else outline
    lows = [-17,-29,-8,-23,-17,-21,-28,-19,-6,-23,-17,-25]
    rings = [[(x*.40-7,y*.43,z*.21-37) for (x,y),z in zip(outline,lows)],
             [(x*.88-3,y*.87,z) for (x,y),z in zip(outline,lows)],
             [(x,y,4+4*math.sin(i*1.9)) for i,(x,y) in enumerate(outline)],
             [(x*.71-7,y*.72,24+7*math.cos(i*1.3)) for i,(x,y) in enumerate(outline)],
             [(x*.32-8,y*.41+2,40+3*math.sin(i*1.6)) for i,(x,y) in enumerate(outline)]]
    bands(mesh,rings,[.45,.69,.94,1.],True)
    # Fan-shaped tips join one shelf; each is a broad wedge, not an individual leaf.
    for i in (0,3,6,9):
        j=(i+1)%count
        a,b=rings[2][i],rings[2][j]
        mid=tuple((a[k]+b[k])*.5 for k in range(3))
        tip=(mid[0]*1.21+6,mid[1]*1.18,mid[2]-18)
        crest=(mid[0]*.87,mid[1]*.83,mid[2]+10)
        for tri,tone in [([a,tip,crest],.93),([crest,tip,b],.80),([a,b,tip],.57)]:
            painted_face(mesh,tri,tone)
    return mesh


def bark_spar():
    mesh = Facets()
    rings = [radial_ring(9,z,r,phase,(dx,dy),.075)
             for z,r,phase,dx,dy in [(-50,49,0,0,0),(-39,43,.04,-1,0),
                                     (-11,43,.09,1,-1),(19,39,.10,4,0),
                                     (43,37,.17,2,-1),(50,34,.15,1,-2)]]
    painted_face(mesh,list(reversed(rings[0])),.6)
    for row,(lo,hi) in enumerate(zip(rings,rings[1:])):
        for i in range(9):
            j=(i+1)%9
            tone=(.7,.88,.95,.75,.92,.80,.68,.98,.83)[i]
            painted_face(mesh,[lo[i],lo[j],hi[j],hi[i]],tone*(.80 if row==0 else 1.))
    # An exposed snapped end reads as wood once the tree has fallen.
    for i in range(9):
        j=(i+1)%9
        a,b=rings[-1][i],rings[-1][j]
        painted_face(mesh,[a,b,(1.,-2.,50.)],1.08 if i%3 else .84)
    return mesh


def sluice():
    mesh=Facets()
    # Three long gravity-fed planks between raised asymmetric side rails.
    for i in range(3):
        lo=-50+i*33.5
        hi=lo+33.
        face_xz(mesh,[(-50,-46),(49,-49),(50,9),(-47,26)],lo,hi,
                (102,72,62),[(175,119,79),(133,90,66),(196,144,92)][i])
        # Pale mineral tracks worn into the two middle grooves follow the flow to the lip.
        y=lo+8
        face(mesh,[(-39,y,26.),(30,y+1.4,13.),(44,y+3.,11.),(-37,y+5.,25.8)],(209,171,113))
    for lo,hi in [(-50,-43),(42,50)]:
        face_xz(mesh,[(-50,2),(49,-10),(47,27),(-49,50)],lo,hi,(86,74,62),(132,103,73))
    return mesh


def hopper_throat():
    mesh=Facets()
    # The two inclined cheeks concentrate the load into a recessed gate.
    # The middle stays open below the lintel, right down to the chute.
    for side in (-1,1):
        for j,(low,high) in enumerate([(-49,-27),(-27,-5),(-5,17),(17,35)]):
            inner0=20.-(low+49)*.094
            inner1=20.-(high+49)*.094
            outer0=37.+(low+49)*.14
            outer1=37.+(high+49)*.14
            p=[(side*inner0,low),(side*outer0,low),(side*outer1,high),(side*inner1,high)]
            if side<0: p.reverse()
            face_xz(mesh,p,-45+j*3.,34,[(115,66,49),(146,84,55),(175,108,65),(185,129,78)][j],(88,67,56))
        # Recess the iron liner into the real aperture, leaving the forward grain exposed.
        p=[(side*18.5,-49),(side*22.,-49),(side*14.,37),(side*10.,37)]
        if side<0: p.reverse()
        face_xz(mesh,p,-50,-38,(80,99,95),(120,130,114),True)
    face_xz(mesh,[(-49,34),(49,34),(50,49),(-50,49)],-49,47,(131,92,64),(198,144,88))
    return mesh


def hopper():
    mesh=Facets()
    # Overlapping weatherboards narrow towards the outlet. Small joints and the
    # protruding iron straps remain real geometry at shovel distance.
    for row,(bot,top) in enumerate([(-49,-26),(-26,0),(0,24),(24,50)]):
        w0=44+(bot+50)*.06
        w1=44+(top+50)*.06
        bevel=1.4
        z0 = bot+.65 if row else bot
        poly=[(-w0,z0),(w0,z0),(w1,top-bevel),(w1-.7,top),(-w1+1,top),(-w1,top-2)]
        tone=[(128,77,51),(168,99,58),(190,120,69),(196,138,84)][row]
        face_xz(mesh,poly,-34-row*2.8,34+row*2.8,tone,(132,91,64))
    for side in (-1,1):
        lo,hi=sorted((side*35,side*48))
        for x in (-34,34):
            face_xz(mesh,[(x-3.6,-46),(x+3.6,-45),(x+4.7,48),(x-3.3,47)],
                    lo,hi,(73,93,91),(108,117,103),True)
            for z in (-31,31):
                a,b=sorted((side*47,side*51))
                face_xz(mesh,[(x-2.7,z),(x,z-2.9),(x+2.7,z),(x,z+2.9)],a,b,
                        (213,176,113),(151,123,83),True)
        # Arrow cut into the centre board above the mouth, visible from both sides.
        lo,hi=sorted((side*44,side*48))
        face_xz(mesh,[(-17,14),(-3,4),(0,-1),(13,10),(17,7),(0,-19),(-18,8)],
                lo,hi,(234,201,147),(186,143,101))
    return mesh


def sheave():
    mesh=Facets()
    # A six-spoke wheel with open intervals and a chamfered iron tyre.
    count=12
    sections=[(35,-34),(46,-48),(50,-29),(50,29),(46,48),(35,34)]
    loops=[[(r*math.cos(TAU*i/count),y,r*math.sin(TAU*i/count)) for i in range(count)]
           for r,y in sections]
    for k,ring in enumerate(loops):
        nxt=loops[(k+1)%len(loops)]
        for i in range(count):
            j=(i+1)%count
            face(mesh,[ring[i],nxt[i],nxt[j],ring[j]],
                 [(158,114,77),(112,116,99),(63,85,82),(117,128,108),
                  (160,118,80),(109,80,62)][k], k in (1,2,3))
    for i in range(6):
        a=TAU*i/6
        def board(radius,halfwidth):
            return (radius*math.cos(a)-halfwidth*math.sin(a),radius*math.sin(a)+halfwidth*math.cos(a))
        face_xz(mesh,[board(5,-4),board(39,-3),board(39,3),board(5,4)],-18,18,
                (160,114,78),(114,83,64))
    hub=[(10*math.cos(TAU*i/8),10*math.sin(TAU*i/8)) for i in range(8)]
    face_xz(mesh,hub,-50,50,(212,174,111),(106,109,90),True)
    return mesh


def sand_heap():
    mesh=Facets()
    rings=[radial_ring(9,z,r,p,o,.1) for z,r,p,o in [
        (-48,38,.07,(0,0)),(-27,50,.10,(0,0)),(11,42,.20,(-3,1)),
        (31,28,.31,(-6,3)),(46,7,.4,(-10,5))]]
    bands(mesh,rings,[.72,.9,1.,.96],True)
    return mesh


def cloth_panel(signal=False):
    mesh=Facets()
    # Coloured polygons follow the actual creases. Registration lines, the
    # compass and the wind bars survive folding and disappear with each cut.
    ys=[-50,-38,-25,-12,0,12,25,38,50]
    zs=[-50,-35,-21,-9,3,15,27,39,50]
    def point(side,y,z):
        fold=(.85*math.cos(y*.14)+.22*math.sin(y*.26+.6))*(1.-abs(z)*.009)
        x=side*27 + fold*18 + (1.-(y/52)**2)*8
        hem=(math.cos(y*.10+.3)*3+(y+50)*.025) * max(0.,1.-(z+50)/28.)
        return x,y,z+hem
    base=(96,105,143) if not signal else (102,105,144)
    for side in (-1,1):
        for i,(l,r) in enumerate(zip(ys,ys[1:])):
            for j,(b,t) in enumerate(zip(zs,zs[1:])):
                q=[(l,b),(r,b),(r,t),(l,t)]
                for poly in ([q[0],q[1],q[2]],[q[0],q[2],q[3]]):
                    y=sum(v[0] for v in poly)/3
                    z=sum(v[1] for v in poly)/3
                    color=base
                    if signal:
                        dy,dz=abs(y),abs(z-3)
                        if (dz/33+dy/11<1 or dy/29+dz/11<1):
                            color=(235,204,150) if y+z>0 else (194,151,102)
                        elif dy/15+dz/16<1: color=(192,153,111)
                        elif abs(z)>36: color=(67,81,113)
                    else:
                        if abs(z-.25*y+25)<7: color=(204,180,143)
                        elif abs(z-.25*y+15)<5: color=(74,128,131)
                        elif z>37: color=(66,81,113)
                    if i in (0,7): color=tuple(round(c*.90) for c in color)
                    pts=[point(side,u,v) for u,v in poly]
                    face(mesh,pts if side>0 else pts[::-1], color)
    for z in (-50,50):
        for l,r in zip(ys,ys[1:]):
            q=[point(-1,l,z),point(-1,r,z),point(1,r,z),point(1,l,z)]
            face(mesh,q if z<0 else q[::-1],(142,134,119))
    for y in (-50,50):
        for b,t in zip(zs,zs[1:]):
            q=[point(-1,y,b),point(1,y,b),point(1,y,t),point(-1,y,t)]
            face(mesh,q if y<0 else q[::-1],(107,111,132))
    return mesh


def furled_canvas():
    mesh=Facets()
    # Longitudinal gathered rolls, squeezed down where the lashings cross the yard.
    stations=[(-50,.70),(-45,.96),(-34,.81),(-32,.55),(-30,.6),(-23,.95),(-9,1.),
              (-2,.63),(1,.56),(3,.65),(13,.95),(24,1.05),(29,.63),(32,.59),(35,.79),(46,.97),(50,.73)]
    rings=[]
    for i,(y,r) in enumerate(stations):
        ring=[]
        for k in range(8):
            a=TAU*k/8+.09*math.sin(y*.075)
            ring.append((math.cos(a)*46*r,y, math.sin(a)*42*r-5*r+2*math.sin(i)))
        rings.append(ring)
    for i,(lo,hi) in enumerate(zip(rings,rings[1:])):
        for k in range(8):
            j=(k+1)%8
            tone=(213,185,145) if i in (3,7,8,12) else [(88,91,125),(131,119,151),
                (159,143,162),(111,113,153),(72,87,120),(76,83,114),(86,89,119),(121,112,139)][k]
            face(mesh,[lo[k],hi[k],hi[j],lo[j]],tone)
    face(mesh,rings[0],(128,116,145))
    face(mesh,rings[-1][::-1],(107,100,128))
    return mesh


def wind_pennon():
    mesh=Facets()
    # Continuous cloth from the hoist to the forked fly, creased across the diagonal.
    stations=[(-50,-40,38,0),(-29,-29,48,10),(-5,-17,45,5),(29,-8,25,-13),(49,-12,28,-3)]
    for side in (-1,1):
        top=[]; low=[]; mid=[]
        for i,(y,b,t,x) in enumerate(stations):
            top.append((x+side*4,y,t));low.append((x+side*4,y,b))
            mid.append((x+side*4+6,y if i!=4 else 29,(b+t)*.5))
        for i in range(4):
            for strip,color in [(top,(206,177,120) if i>=2 else (66,113,120)),
                                (low,(221,194,143) if i>=2 else (76,137,139))]:
                q=[strip[i],strip[i+1],mid[i+1],mid[i]]
                if (strip is top)==(side>0): q.reverse()
                face(mesh,q,color)
    return mesh


def caldera_lip():
    mesh=Facets()
    # One radial segment. Local -X is the mouth: its low, salt-worn sill slopes
    # up to a broken outer basalt column, leaving the centre open to the shovel.
    outline=[(-50,-28),(-32,-40),(20,-52),(45,-39),(50,-13),(40,42),(5,50),(-35,38),(-50,22)]
    levels=[(-48,.87),(-31,1.),(-14,.94),(-8,1.),(3,.94),(13,.92),(36,.79)]
    rings=[]
    for z,f in levels:
        ring=[]
        for i,(x,y) in enumerate(outline):
            shelf = (x+50)/100.*18+(math.cos(i*2.2)*7)
            ring.append((x*f,y*f,z+shelf*(.16 if z<0 else 1.)))
        rings.append(ring)
    colors=[(53,77,86),(76,103,111),(113,136,131),(66,96,103),(96,120,122),(115,139,135)]
    face(mesh,rings[0][::-1],(56,81,90))
    for row,(lo,hi) in enumerate(zip(rings,rings[1:])):
        for i in range(len(lo)):
            j=(i+1)%len(lo)
            tint=colors[row]
            if row==3 and i in (0,1,7,8): tint=(162,172,149)
            face(mesh,[lo[i],lo[j],hi[j],hi[i]],tint)
    for i,a in enumerate(rings[-1]):
        face(mesh,[a,rings[-1][(i+1)%len(outline)],(5,0,47)],
             (153,162,145) if i in (0,1,8) else (115,139,139))
    return mesh


def spring_pool():
    mesh=Facets()
    rings=[radial_ring(13,-40,42,.07,wobble=.07),radial_ring(13,-3,49,.07,wobble=.07),
           radial_ring(13,10,35,.07,wobble=.055),radial_ring(13,12,23,.07,wobble=.07)]
    for i,(lo,hi) in enumerate(zip(rings,rings[1:])):
        for j in range(13):
            k=(j+1)%13
            face(mesh,[lo[j],lo[k],hi[k],hi[j]],[(52,111,115),(87,153,145),(60,131,132)][i])
    face(mesh,rings[-1],(42,109,118))
    return mesh


def water_spout():
    mesh=Facets()
    rings=[radial_ring(9,z,r,rot,offset,.08) for z,r,rot,offset in [
        (-50,47,.08,(0,0)),(-30,38,.08,(1,-1)),(-8,26,.23,(3,0)),
        (18,24,.28,(7,-2)),(33,29,.38,(6,-3)),(45,22,.42,(3,-2))]]
    bands(mesh,rings,[.74,.82,.94,.91,1.],True)
    return mesh


def foam_crown():
    mesh=Facets()
    n=14
    base=radial_ring(n,-45,25,.05,wobble=.09)
    outer=radial_ring(n,0,46,.12,wobble=.1)
    peaks=[43,5,32,-2,27,7,49,9,21,4,38,-5,25,2]
    rim=[(x,y,z) for (x,y,_),z in zip(outer,peaks)]
    inner=[(x*.64,y*.64,z-8) for x,y,z in rim]
    foot=[(x*.55,y*.55,-40) for x,y,z in base]
    for lo,hi,tone in [(base,rim,.77),(rim,inner,1.),(inner,foot,.9),(foot,base,.67)]:
        for i in range(n):
            j=(i+1)%n
            painted_face(mesh,[lo[i],lo[j],hi[j],hi[i]],tone)
    return mesh


def build_clod_pigment():
    from terrain_materials import ISLAND_COLOR, LIB, custom, expression
    name="M_ClodPigment"
    path=DEST+name
    material=ue.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else (
        ue.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST.rstrip("/"),ue.Material,ue.MaterialFactoryNew()))
    base=LIB.get_material_property_input_node(material,ue.MaterialProperty.MP_BASE_COLOR)
    code=ISLAND_COLOR.replace("abs(N.z)", "abs(Slope)")
    if not base:
        point=expression(material,ue.MaterialExpressionPerInstanceCustomData3Vector,-920,0)
        depth=expression(material,ue.MaterialExpressionPerInstanceCustomData,-920,170)
        depth.set_editor_property("data_index",3)
        slope=expression(material,ue.MaterialExpressionPerInstanceCustomData,-920,330)
        slope.set_editor_property("data_index",4)
        slope.set_editor_property("const_default_value",1.)
        shade=custom(material,"Palette at the struck layer",code,{"P":point,"Slope":slope,"Cut":depth},0)
        base=expression(material,ue.MaterialExpressionVertexInterpolator,-100,0)
        assert LIB.connect_material_expressions(shade,"",base,"")
        assert LIB.connect_material_property(base,"",ue.MaterialProperty.MP_BASE_COLOR)
        rough=expression(material,ue.MaterialExpressionConstant,-100,180)
        rough.set_editor_property("r",.91)
        assert LIB.connect_material_property(rough,"",ue.MaterialProperty.MP_ROUGHNESS)
    else:
        shade=LIB.get_inputs_for_material_expression(material,base)[0]
        shade.set_editor_property("code",code)
    material.set_editor_property("used_with_instanced_static_meshes",True)
    LIB.recompile_material(material)
    assert ue.EditorAssetLibrary.save_loaded_asset(material,False)


def build_mechanisms():
    for name,maker,stock in (
        ("SM_LeafCrown",wind_crown,"Sphere"), ("SM_BarkSpar",bark_spar,"Cylinder"),
        ("SM_HopperPanel",hopper,"Cube"), ("SM_SluiceChute",sluice,"Cube"),
        ("SM_HopperThroat",hopper_throat,"Cube"),
        ("SM_SheaveWheel",sheave,"Cylinder"), ("SM_SandHeap",sand_heap,"Sphere"),
        ("SM_SignalCanvas",lambda:cloth_panel(True),"Cube"), ("SM_WindSidePanel",cloth_panel,"Cube"),
        ("SM_RolledCanvas",furled_canvas,"Cylinder"), ("SM_WindPennon",wind_pennon,"Cube"),
        ("SM_Basalt",caldera_lip,"Cube"), ("SM_TidePool",spring_pool,"Sphere"),
        ("SM_TideJet",water_spout,"Sphere"), ("SM_FoamCrest",foam_crown,"Sphere"),
    ):
        maker().write(DEST+name,stock)
    build_clod_pigment()


if __name__=="__main__":
    build_mechanisms()
