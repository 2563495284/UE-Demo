"""Bake the main island's expedition coffer, compass and lashed rescue float."""
from math import cos, sin, tau
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from island_crew import Carving, paint, ue

CREW = "/Game/TreasureIsland/Art/Crew"
HARBOUR = "/Game/TreasureIsland/Art/Harbour"
FINISH = "/Game/TreasureIsland/Art/Neutral/M_WreckFinish"


def ink(mesh, color, *points):
    mesh.pigment = paint(color)
    mesh.face(*points)


def walls(mesh, lower, upper, colors):
    for i in range(len(lower)):
        j = (i + 1) % len(lower)
        ink(mesh, colors[i % len(colors)], lower[i], lower[j], upper[j], upper[i])


def octagon(x0, x1, y0, y1, cut, z):
    return [(x0+cut,y0,z),(x1-cut,y0,z),(x1,y0+cut,z),(x1,y1-cut,z),
            (x1-cut,y1,z),(x0+cut,y1,z),(x0,y1-cut,z),(x0,y0+cut,z)]


def coffer_body():
    mesh=Carving()
    def ring(size, c, z):
        return octagon(-size,size,-size,size,c,z)
    # Recess the caulk lines between three strakes; leave a recessed, watertight interior.
    profile=[(-50,43,10,'675047'),(-43,48,11,'926849'),(-37,48,11,'B28366'),
             (-34,47,10,'493C33'),(-31,49,11,'94674E'),(-8,49,11,'A77753'),
             (-6,48,11,'4C4038'),(-4,49,11,'916A50'),(22,50,11,'BB8964'),
             (24,49,10,'594D40'),(26,50,11,'739991'),(39,50,11,'688881'),
             (40,51,11,'8D694C'),(47,51,11,'CEAA7F'),(50,49,11,'DEBB89')]
    rings=[ring(s,c,z) for z,s,c,_ in profile]
    ink(mesh,'604738',*reversed(rings[0]))
    for a,b,row in zip(rings,rings[1:],profile[1:]):
        walls(mesh,a,b,[row[3]])
    lip, floor=ring(43,8,50),ring(42,7,-37)
    walls(mesh,rings[-1],lip,['D3AA79'])
    walls(mesh,lip,floor,['674E41','805D49'])
    ink(mesh,'735544',*floor)
    # Salt-worn bronze boots wrap the lower corners, with a hammered pin on each face.
    for sx in (-1,1):
        for sy in (-1,1):
            outline=[(sx*27,sy*48,-42),(sx*39,sy*48,-42),(sx*48,sy*39,-42),(sx*48,sy*27,-42)]
            top=[(x*1.045,y*1.045,-15 if i in (1,2) else -32) for i,(x,y,z) in enumerate(outline)]
            for i in range(3):
                points=[outline[i],outline[i+1],top[i+1],top[i]]
                ink(mesh,'7B8270' if i!=1 else 'B69E73',*(points if sx*sy<0 else reversed(points)))
            mesh.spar('D2AF7B',(sx*37,sy*48,-30),(sx*37,sy*50,-30),1.9,1.6,7)
    return mesh


def coffer_lid(hoop=False):
    mesh=Carving()
    cross=[(-50,-50),(50,-50),(50,-10),(42,15),(28,35),(12,48),(0,50),
           (-12,48),(-28,35),(-42,15),(-50,-10)]
    left=[(-50,y,z) for y,z in cross]
    right=[(50,y,z) for y,z in cross]
    # Axis is X; this cross-section is counter-clockwise when viewed along X.
    ink(mesh,'917157' if not hoop else 'A9B6A8',*reversed(left))
    ink(mesh,'A07E62' if not hoop else 'A9B6A8',*right)
    planks=['675449','967254','B48D6B','CBAB86','E0C4A0','D6B693',
            'E1C5A3','C6A07C','B69171','AA7F60','916D54']
    for i in range(len(cross)):
        j=(i+1)%len(cross)
        ink(mesh,('CFD5C8' if i%2 else 'B2C1B5') if hoop else planks[i],left[i],left[j],right[j],right[i])
    if not hoop:
        # Lay a compass rose across the crown; its front half wraps down the lid.
        def crown(y):
            t=abs(y)
            if t<=12.: return 50.-t/6.
            if t<=28.: return 48.-(t-12.)*13/16.
            return 35.-(t-28.)*20/14.
        def point(x,y,lift): return (x,y,crown(y)+lift)
        for half in (-1,1):
            for xa,xb in [(-21,0),(0,21)]:
                poly=[point(xa,0,1.),point(xb,0,1.),point(xb,half*17,1.),point(xa,half*17,1.)]
                ink(mesh,'416C69',*(poly if half>0 else reversed(poly)))
        ring=[(0,21),(4.2,5.),(16,0),(4.,-4.),(0,-21),(-4,-4),(-16,0),(-4,5)]
        for i in range(8):
            ink(mesh,'F9D6A0' if i%2==0 else 'C48F62',point(0,0,3.1),point(*ring[(i+1)%8],2.3),point(*ring[i],2.3))
    return mesh


def compass(dial):
    mesh=Carving()
    def circle(radius,z):
        return [(radius*cos(tau*i/24),radius*sin(tau*i/24),z) for i in range(24)]
    profile=([(-48,43,'6B7370'),(-36,50,'BCBA9B'),(35,50,'FFF0CA'),(48,44,'EEE4CE')]
             if not dial else [(-50,47,'747D75'),(-30,50,'909D88'),(30,50,'EFE4C0'),(50,47,'F1E8C9')])
    rings=[circle(r,z) for z,r,c in profile]
    for a,b,row in zip(rings,rings[1:],profile[1:]): walls(mesh,a,b,[row[2]])
    for side,rim in [(1,rings[-1]),(-1,rings[0])]:
        z=rim[0][2]
        for i in range(24):
            j=(i+1)%24
            pts=[(0,0,z),rim[i],rim[j]]
            ink(mesh,('F2EACF' if i%3 else 'DBD8B9') if dial else 'BDAC82',*(pts if side>0 else reversed(pts)))
        if not dial: continue
        for i in range(24):
            a=tau*i/24;u=(cos(a),sin(a));v=(-u[1],u[0])
            def pt(r,w):return (r*u[0]+w*v[0],r*u[1]+w*v[1],z+side*.9)
            pts=[pt(34 if i%3==0 else 39,-1.4),pt(45,-1.4),pt(45,1.4),pt(34 if i%3==0 else 39,1.4)]
            ink(mesh,'4B7975' if i%3==0 else '967C64',*(pts if side>0 else reversed(pts)))
    return mesh


def compass_hand():
    mesh=Carving()
    corners=[(-24,0,-49),(0,-46,-28),(32,0,-46),(0,46,-28)]
    tip=(0,0,50)
    for i,c in enumerate(corners):ink(mesh,['F1D6A1','9C967D','C89367','FAE6B3'][i],c,corners[(i+1)%4],tip)
    ink(mesh,'746758',*reversed(corners))
    return mesh


def hasp():
    mesh=Carving()
    outline=[(-47,45),(47,45),(47,-7),(22,-43),(0,-50),(-22,-43),(-47,-7)]
    front=[(x,42,z) for x,z in outline];back=[(x,-38,z) for x,z in outline]
    ink(mesh,'A08F76',*reversed(back))
    for i in range(len(front)):
        j=(i+1)%len(front)
        ink(mesh,'D5BA92',back[i],back[j],front[j],front[i])
        ink(mesh,'F2DCB3' if i in (0,4,5) else 'C8B591',front[i],front[j],(0,50,8))
    for x in (-26,26):
        mesh.spar('FAE4BE',(x,39,30),(x,51,30),6.5,5.,7)
    return mesh


def keyway():
    mesh=Carving()
    # Convex key bow and its tapered slot sit on the raised hasp.
    outline=[(-23,3),(-37,18),(-35,38),(-17,49),(18,49),(34,37),(36,17),(20,3)]
    for shape in (outline,[(-13,11),(13,11),(21,-46),(-22,-46)]):
        fore=[(x,50,z) for x,z in shape];back=[(x,-50,z) for x,z in shape]
        ink(mesh,'332F2B',*fore);ink(mesh,'332F2B',*reversed(back))
        for i in range(len(shape)):
            j=(i+1)%len(shape)
            ink(mesh,'624D3C',fore[i],back[i],back[j],fore[j])
    return mesh


def rescue_float():
    mesh=Carving()
    # Five parallel timber floats, linked by two transverse cleats and tarred lashings.
    for i,x in enumerate((-40,-20,0,20,40)):
        y0=(-47,-49,-50,-48,-46)[i]; y1=(46,49,50,47,49)[i]
        sections=[]
        for y,rad in [(y0,.73),(y0+4,1.),(y1-5,1.),(y1,.76)]:
            sections.append([(x+9.9*rad*cos(tau*k/8+tau/16),y,-13+39*rad*sin(tau*k/8+tau/16)) for k in range(8)])
        for a,b in zip(sections,sections[1:]):
            for k in range(8):
                j=(k+1)%8
                ink(mesh,['846449','B28A63','C39D72','9F7854','7D6654','6E5846','715744','9A7956'][(k+i%2)%8],a[k],b[k],b[j],a[j])
        ink(mesh,'D3A67A',*sections[0]);ink(mesh,'D3A67A',*reversed(sections[-1]))
    for y in (-27,28):
        lo=octagon(-49,49,y-5,y+5,2.2,12); hi=octagon(-48,48,y-4.5,y+4.5,2,48)
        walls(mesh,lo,hi,['78604F']);ink(mesh,'CFB08A',*hi);ink(mesh,'785E49',*reversed(lo))
        for x in (-40,40):
            for off in (-2,2):
                mesh.spar('537D78',(x+off,y-6.8,17),(x+off+1,y-3.2,49),1.6,1.6,5)
                mesh.spar('65918A',(x+off+1,y-3.2,49),(x+off-1,y+4.8,49),1.6,1.6,5)
                mesh.spar('537D78',(x+off-1,y+4.8,49),(x+off,y+6.8,17),1.6,1.6,5)
    # Painted cork rescue ring on a cleat. Leave the centre of the raft walkable.
    cx,cy=23.,-22.
    for i in range(16):
        def section(k):
            a=tau*k/16
            return [(cx+(12+2.6*cos(tau*j/6))*cos(a),cy+(14+3.0*cos(tau*j/6))*sin(a),38+8*sin(tau*j/6)) for j in range(6)]
        a,b=section(i),section(i+1)
        color='D76648' if (i//2)%2==0 else 'EFD7AE'
        for j in range(6):
            k=(j+1)%6
            ink(mesh,color,a[j],b[j],b[k],a[k])
    mesh.spar('71634B',(32,-16,39),(36,-9,35),1.4,1.3,5)
    mesh.spar('71634B',(36,-9,35),(40,-4,19),1.3,1.3,5)
    return mesh


def finish(name, color, roughness, metallic):
    path=CREW+'/'+name
    asset=ue.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else ue.AssetToolsHelpers.get_asset_tools().create_asset(name,CREW,ue.MaterialInstanceConstant,ue.MaterialInstanceConstantFactoryNew())
    ed=ue.MaterialEditingLibrary
    ed.set_material_instance_parent(asset,ue.load_asset(FINISH))
    ed.set_material_instance_vector_parameter_value(asset,'Color',paint(color))
    ed.set_material_instance_scalar_parameter_value(asset,'Roughness',roughness)
    ed.set_material_instance_scalar_parameter_value(asset,'Metallic',metallic)
    ed.update_material_instance(asset)
    assert ue.EditorAssetLibrary.save_loaded_asset(asset,False)
    return asset


def bake():
    clue=finish('MI_CofferLacquer','B8D6CC',.59,.04)
    prize=finish('MI_CofferGilding','FFE8A8',.48,.25)
    metal=finish('MI_CofferBrass','FFC87E',.37,.66)
    iron=finish('MI_CofferBronze','71938C',.48,.38)
    dial=finish('MI_CofferEnamel','E4F1DD',.43,.07)
    timber=finish('MI_CofferRaftTimber','FFFFFF',.88,0.)
    for name,model,stock,material in (
        ('Body',coffer_body(),'Cube',clue),('Lid',coffer_lid(),'Cube',clue),
        ('Hoop',coffer_lid(True),'Cube',iron),('Hasp',hasp(),'Cube',metal),('Keyway',keyway(),'Cube',iron),
        ('Rim',compass(False),'Cylinder',metal),('Dial',compass(True),'Cylinder',dial),
        ('Hand',compass_hand(),'Cone',metal)):
        asset=model.write(CREW+'/SM_Coffer'+name,stock)
        asset.set_material(0,material)
        assert ue.EditorAssetLibrary.save_loaded_asset(asset,False)
    asset=rescue_float().write(HARBOUR+'/SM_RescueRaft','Cube')
    asset.set_material(0,timber)
    assert ue.EditorAssetLibrary.save_loaded_asset(asset,False)


if __name__=='__main__':
    bake()
