"""Rebuild the wind-worn expedition props in UE with PythonScriptPlugin and GeometryScripting."""
import math
import os
import sys
import unreal as ue

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from static_mesh import Facets

DEST = "/Game/TreasureIsland/Art/Neutral/"


def painted_face(mesh, points, shade=1.):
    mesh.face(*points)
    mesh.colors.extend([ue.LinearColor(shade, shade, shade, 1.)] * len(points))


def radial_ring(sides, z, radius, phase=0., offset=(0., 0.), wobble=0.):
    ring = []
    for i in range(sides):
        a = math.tau * i / sides + phase
        r = radius * (1. + wobble * (math.sin(3*a + .7) + .55*math.cos(2*a - .4)))
        ring.append((offset[0] + r*math.cos(a), offset[1] + r*math.sin(a), z))
    return ring


def bands(mesh, rings, shades=None, facet=False):
    count = len(rings[0])
    painted_face(mesh, list(reversed(rings[0])), .84)
    for j, (low, high) in enumerate(zip(rings, rings[1:])):
        shade = shades[j] if shades else 1.
        for i in range(count):
            k = (i+1) % count
            p = [low[i], low[k], high[k], high[i]]
            tone = shade * (1.-.025*((i+2*j)%4))
            if facet:
                painted_face(mesh, p[:3], tone)
                painted_face(mesh, [p[0],p[2],p[3]], tone*.97)
            else:
                painted_face(mesh, p, tone)
    painted_face(mesh, rings[-1])


def hewn_block():
    mesh = Facets()
    def section(w, z):
        c = .14*w
        return [(w,w-c,z),(w-c,w,z),(-w+c,w,z),(-w,w-c,z),(-w,-w+c,z),(-w+c,-w,z),(w-c,-w,z),(w,-w+c,z)]
    bands(mesh, [section(46,-50),section(50,-45),section(50,45),section(46,50)], [.76,.98,1.])
    return mesh


def post():
    mesh = Facets()
    bands(mesh, [radial_ring(8,z,r) for z,r in [(-50,46),(-47,50),(47,50),(50,46)]], [.82,1.,1.])
    return mesh


def stone():
    mesh = Facets()
    rings = [radial_ring(7,-50,22,.06,(-4,-1),.08), radial_ring(7,-30,45,.08,(1,0),.07),
             radial_ring(7,9,47,.24,(0,2),.12), radial_ring(7,34,37,.34,(-2,3),.11),
             radial_ring(7,49,19,.38,(-5,4),.08)]
    bands(mesh, rings, [.70,.86,.93,1.], True)
    return mesh


def droplet():
    mesh=Facets()
    bands(mesh,[radial_ring(7,-50,7),radial_ring(7,-32,32),radial_ring(7,-8,46,.15),
                radial_ring(7,14,40,.26),radial_ring(7,46,9,.4,(3,1))],[.85,.94,1.,.98],True)
    return mesh


def spire():
    mesh=Facets()
    rings=[radial_ring(6,-50,46),radial_ring(6,-40,50),radial_ring(6,-13,33,.06,(-2,0)),
           radial_ring(6,15,20,.13,(-3,0)),radial_ring(6,46,3,.21,(-4,1))]
    bands(mesh,rings,[.76,.96,1.,.94])
    return mesh


def cloth():
    # Shallow alternating pleats allow the upper edge to stay straight as the
    # actual sight-blocking panel shortens and swings on its rope.
    mesh=Facets()
    def point(side,i,j):
        y=-50+100*i/6
        z=(-50, -33, 13, 39, 50)[j]
        pleat=(0,11,-7,9,-8,11,0)[i]
        x=side*38+pleat*(1.-abs(z)/80)
        return (x,y,z)
    for side in (-1,1):
        for i in range(6):
            for j in range(4):
                quad=[point(side,i,j),point(side,i+1,j),point(side,i+1,j+1),point(side,i,j+1)]
                if side<0:quad.reverse()
                painted_face(mesh,quad,(.78,.99,.89,.95)[j]*(1.-.029*(i%3)))
    for i in range(6):
        for j in (0,4):
            quad=[point(-1,i,j),point(-1,i+1,j),point(1,i+1,j),point(1,i,j)]
            if j==4:quad.reverse()
            painted_face(mesh,quad,.81)
    for i in (0,6):
        for j in range(4):
            quad=[point(-1,i,j),point(1,i,j),point(1,i,j+1),point(-1,i,j+1)]
            if i==6:quad.reverse()
            painted_face(mesh,quad,.78)
    return mesh


def pigment(rgb, shade=1.):
    values = [((v/255.+.055)/1.055)**2.4 if v > 10 else v/3294.6 for v in rgb]
    return ue.LinearColor(*(shade*v for v in values), 1.)


def build_neutral_stock():
    for name, maker, stock in (
        ("SM_HewnBlock", hewn_block, "Cube"), ("SM_TimberPost", post, "Cylinder"),
        ("SM_ReefStone", stone, "Sphere"), ("SM_Spray", droplet, "Sphere"),
        ("SM_WindCanvas", cloth, "Cube"), ("SM_CrystalSpire", spire, "Cone"),
    ):
        maker().write(DEST + name, stock)


if __name__ == "__main__":
    from mechanism_meshes import build_mechanisms
    build_neutral_stock()
    build_mechanisms()
