"""Build the expedition pig: authored centimetre-space sculpture, skin and editable glTF."""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

import numpy as np


HERE = Path(__file__).resolve().parent
TAU = math.tau
# The rest pose uses UE coordinates. +X forward, +Y character-right, +Z up.
BONES = {
    'root': (None,(0,0,0)), 'pelvis': ('root',(0,0,54)), 'spine': ('pelvis',(0,0,73)),
    'chest': ('spine',(-1,0,94)), 'neck': ('chest',(-1,0,106)), 'head': ('neck',(2,0,117)),
    'jaw': ('head',(23,0,114)), 'ear_l': ('head',(-7,-20,142)), 'ear_r': ('head',(-7,20,142)),
    'tail_01': ('pelvis',(-24,0,56)), 'tail_02': ('tail_01',(-33,4,60)),
}
for suffix,sign in [('l',-1),('r',1)]:
    BONES.update({
        'upperarm_'+suffix:('chest',(-2,sign*31,98)),
        'lowerarm_'+suffix:('upperarm_'+suffix,(1,sign*41,77)),
        'hand_'+suffix:('lowerarm_'+suffix,(9,sign*45,57)),
        'thigh_'+suffix:('pelvis',(0,sign*17.3,53)),
        'calf_'+suffix:('thigh_'+suffix,(-1,sign*18.5,32)),
        'foot_'+suffix:('calf_'+suffix,(4,sign*20,16)),
        'brow_'+suffix:('head',(24,sign*16,136)),
        'eyelid_'+suffix:('head',(27,(sign*15.0+1.0)*.91,131.4)),
    })
BONES['hat_brim']=('head',(15,0,145.5))


# Swapping the up axis converts between glTF's right handed frame and UE.
def convert(point):
    return [point[0], point[2], point[1]]


def norm(v):
    return v / max(float(np.linalg.norm(v)), 1.e-12)


def signed_power(v, exponent):
    return math.copysign(abs(v)**exponent, v)


def pigment(code):
    return np.array([((c+.055)/1.055)**2.4 if c > .04045 else c/12.92
                     for c in (int(code[i:i+2], 16)/255. for i in (0, 2, 4))])


class Sculpture:
    def __init__(self, detail=0):
        self.detail=detail
        self.vertices, self.normals, self.colors, self.faces = [], [], [], []
        self.joints, self.weights = [], []
        self.bones = list(BONES)

    def surface(self, verts, polys, color, binding, rough=None):
        """Each plane owns a normal, including the broad bevels around cheeks and hooves."""
        verts=np.asarray(verts,dtype=float)
        for polygon in polys:
            plane=verts[list(polygon)]
            normal=norm(np.cross(plane[1]-plane[0],plane[-1]-plane[0]))
            if np.linalg.norm(normal)<.9:
                continue
            start=len(self.vertices)
            for point in plane:
                self.vertices.append(point)
                self.normals.append(normal)
                tone=color(point) if callable(color) else color
                self.colors.append((*pigment(tone),MATERIAL_ROUGHNESS.get(tone,.90) if rough is None else rough))
                skin=binding(point) if callable(binding) else binding
                skin={skin:1.} if isinstance(skin,str) else skin
                weights=[(self.bones.index(name),float(w)) for name,w in skin.items() if w>0]
                weights.sort(key=lambda pair:-pair[1])
                weights+=[(0,0.)]*(4-len(weights))
                self.joints.append([j for j,w in weights])
                self.weights.append([w for j,w in weights])
            self.faces.extend((start,start+i,start+i+1) for i in range(1,len(plane)-1))

    def solid(self, loops, color, binding, rough=None):
        # Simplify clothing tubes and limb rings with distance. The individually authored
        # forehead, cheeks, eyes and mouth are preserved by their dedicated surface topology.
        if self.detail and binding not in ('ear_l','ear_r'):
            original=len(loops[0])
            wanted=max(5,round(original*(.76 if self.detail==1 else .57)))
            selected=sorted({(i*original)//wanted for i in range(wanted)}) if original>wanted else range(original)
            step=2 if self.detail==1 else 3
            tiers=sorted({0,len(loops)-1,*range(1,len(loops)-1,step)}) if len(loops)>=5 else range(len(loops))
            loops=[[loops[i][j] for j in selected] for i in tiers]
        sides=len(loops[0])
        points=np.concatenate(loops)
        polys=[(r*sides+j,r*sides+(j+1)%sides,(r+1)*sides+(j+1)%sides,(r+1)*sides+j)
               for r in range(len(loops)-1) for j in range(sides)]
        polys.extend((tuple(reversed(range(sides))),tuple(range(len(points)-sides,len(points)))))
        self.surface(points,polys,color,binding,rough)

    def tiers(self, sections, color, binding, sides=10, power=.82, warp=None):
        loops=[]
        for x,y,z,rx,ry in sections:
            loop=[]
            for j in range(sides):
                a=(j+.5)*TAU/sides
                v=np.array([x+rx*signed_power(math.cos(a),power),y+ry*signed_power(math.sin(a),power),z])
                loop.append(warp(v,a) if warp else v)
            loops.append(loop)
        self.solid(loops,color,binding)

    def tube(self, route, color, binding, sides=8):
        points=np.array([p[:3] for p in route],float)
        loops=[]
        for i,p in enumerate(route):
            tangent=norm(points[min(i+1,len(route)-1)]-points[max(0,i-1)])
            guide=np.array([1.,0.,0.]) if abs(tangent[0])<.85 else np.array([0.,1.,0.])
            across=norm(guide-tangent*(guide@tangent))
            up=norm(np.cross(tangent,across))
            loops.append([points[i]+p[3]*math.cos((j+.5)*TAU/sides)*across+p[4]*math.sin((j+.5)*TAU/sides)*up for j in range(sides)])
        self.solid(loops,color,binding)
    def glb(self, filename, skinned=True):
        binary=bytearray()
        buffer_views,accessors=[],[]
        def array(values,dtype,component,layout,bounds=False):
            data=np.array(values,dtype=dtype)
            start=len(binary)
            binary.extend(data.tobytes())
            size=len(binary)-start
            while len(binary)%4: binary.append(0)
            view=len(buffer_views)
            buffer_views.append({'buffer':0,'byteOffset':start,'byteLength':size})
            acc={'bufferView':view,'count':len(data),'componentType':component,'type':layout}
            if bounds:
                acc['min']=data.min(axis=0).tolist(); acc['max']=data.max(axis=0).tolist()
            accessors.append(acc)
            return len(accessors)-1
        attrs={
            'POSITION':array([np.array(convert(v))*.01 for v in self.vertices],'<f4',5126,'VEC3',True),
            'NORMAL':array([convert(v) for v in self.normals],'<f4',5126,'VEC3'),
            'COLOR_0':array(self.colors,'<f4',5126,'VEC4'),
        }
        if skinned:
            attrs['JOINTS_0']=array(self.joints,'<u2',5123,'VEC4')
            attrs['WEIGHTS_0']=array(self.weights,'<f4',5126,'VEC4')
        # Stable tangent basis for the engine. The painted character uses no texture atlas.
        attrs['TEXCOORD_0']=array([(v[1]/113.+.5,1.-v[2]/176.) for v in self.vertices],'<f4',5126,'VEC2')
        # The second, unused UV channel carries the brass mask to UE without another draw call.
        attrs['TEXCOORD_1']=array([(.72 if abs(c[3]-.35)<.001 else 0.,0.) for c in self.colors],
                                 '<f4',5126,'VEC2')
        idx=array(np.array(self.faces)[:,[0,2,1]].ravel(),'<u4',5125,'SCALAR')
        nodes=[]
        inverse=[]
        for name,(parent,center) in (BONES.items() if skinned else ()):
            ctr=np.array(center,dtype=float)
            rel=ctr-(np.array(BONES[parent][1]) if parent else 0.)
            node={'name':name,'translation':(np.array(convert(rel))*.01).tolist()}
            children=[self.bones.index(child) for child,(ancestor,p) in BONES.items() if ancestor==name]
            if children: node['children']=children
            nodes.append(node)
            mat=np.identity(4)
            mat[:3,3]=-np.array(convert(ctr))*.01
            inverse.append(mat.T.flatten())
        nodes.append({'name':filename.stem,'mesh':0})
        desc={'asset':{'version':'2.0','generator':'Expedition pig sculpt.py'},
              'scene':0,'scenes':[{'nodes':[0,len(BONES)] if skinned else [0]}],'nodes':nodes,
              'meshes':[{'name':filename.stem,'primitives':[{'attributes':attrs,'indices':idx,'material':0}]}],
              'materials':[{'name':'PigPaint','pbrMetallicRoughness':{'metallicFactor':0,'roughnessFactor':.7}}]}
        if skinned:
            nodes[-1]['skin']=0
            ibm=array(inverse,'<f4',5126,'MAT4')
            desc['skins']=[{'name':'ExpeditionPigRig','joints':list(range(len(BONES))),'skeleton':0,'inverseBindMatrices':ibm}]
        desc.update(buffers=[{'byteLength':len(binary)}],bufferViews=buffer_views,accessors=accessors)
        js=json.dumps(desc,separators=(',',':')).encode()
        js+=b' '*((-len(js))%4)
        total=12+8+len(js)+8+len(binary)
        filename.write_bytes(struct.pack('<4sII',b'glTF',2,total)+struct.pack('<I4s',len(js),b'JSON')+js+
                             struct.pack('<I4s',len(binary),b'BIN\0')+binary)



SKIN='BD8E82'
INNER='AE7B74'
CHEEK='BD897B'
EYE='413B3C'
SNOUT='B47F75'
NOSTRIL='765855'
CREAM='C7BDA4'
BLUE='414D63'
MID_BLUE='596477'
DARK_BLUE='354155'
SCARF='9D5340'
HOOF='554B48'
BRASS='B49158'
JACKET='8E826E'
OILSKIN='465743'
LEATHER='80664D'
MATERIAL_ROUGHNESS={SKIN:.72,INNER:.76,CHEEK:.73,EYE:.57,SNOUT:.51,NOSTRIL:.79,HOOF:.62,
                    CREAM:.94,BLUE:.92,MID_BLUE:.92,DARK_BLUE:.94,SCARF:.91,BRASS:.35,
                    OILSKIN:.63,LEATHER:.69,JACKET:.88}


def blend(low,high,z0,z1):
    def weights(v):
        t=float(np.clip((v[2]-z0)/(z1-z0),0.,1.))
        return {low:1.-t,high:t}
    return weights


def project_x(mesh, faces, y,z, back=False):
    """Place each graphic directly onto the sculpted face, even across its shallow bevel."""
    options=[]
    for f in faces:
        v=np.array([mesh.vertices[i] for i in f])
        mat=(v[1:,1:]-v[0,1:]).T
        if abs(np.linalg.det(mat))<1.e-7: continue
        u,w=np.linalg.solve(mat,np.array([y,z])-v[0,1:])
        if min(u,w,1-u-w)>=-.001:
            options.append(v[0,0]+u*(v[1,0]-v[0,0])+w*(v[2,0]-v[0,0]))
    return min(options) if back else max(options)


def ink(mesh,reference,outline,color,bone,offset=.53):
    points=[(project_x(mesh,reference,y,z)+offset,y,z) for y,z in outline]
    # The slightly raised centre follows the face when a graphic crosses its bevel.
    y,z=np.mean(np.array(outline),axis=0)
    points.append((project_x(mesh,reference,y,z)+offset+.10,y,z))
    center=len(outline)
    polys=[]
    for i in range(center):
        j=(i+1)%center
        a,b,c=(np.array(points[k]) for k in (center,i,j))
        polys.append((center,i,j) if np.cross(b-a,c-a)[0]>0. else (center,j,i))
    mesh.surface(points,polys,color,bone)


def primitive_face(mesh,plane_x,outline,color,bone):
    points=[(plane_x,y,z) for y,z in outline]
    mesh.surface(points,[tuple(range(len(points)))],color,bone)


def hull(m,points,tone,bone):
    """Fit one convex envelope to anatomical landmarks; flat normals follow the resulting planes."""
    from scipy.spatial import ConvexHull
    points=np.asarray(points)
    core=points.mean(axis=0)
    for indices in ConvexHull(points).simplices:
        plane=points[indices]
        if np.dot(np.cross(plane[1]-plane[0],plane[2]-plane[0]),plane.mean(axis=0)-core)<0.:
            plane=plane[[0,2,1]]
        m.surface(plane,[(0,1,2)],tone,bone)


def ears(m):
    # Unequal ears sit in the band of a low work cap. The worn left tip curls forwards.
    silhouettes=[
        (-1,[(18.5,144),(22,141),(29.3,140.6),(34,138.8),(36.7,141.8),(35.3,147),(30.8,151),(24.,150.7),(20.3,148.5)],
         [(22.2,145.2),(25.1,143.6),(29.9,143.7),(33.5,141.4),(33.3,145.9),(29.4,148.4),(25.,148.2)]),
        (1,[(18.5,143.6),(26.1,142.),(33.5,146.7),(41.8,153.0),(43.,156.3),(37.1,156.6),(26.1,152.6),(19.9,149.6)],
         [(22.,146.7),(25.2,145.1),(31.6,148.4),(39.1,153.6),(38.6,154.7),(30.1,152.45),(23.6,150.9)])]
    for s,outline,mark in silhouettes:
        bone='ear_l' if s<0 else 'ear_r'
        points=[(s*y,z) for y,z in outline]
        if s<0: points.reverse()
        center=np.mean(points,axis=0)
        loops=[]
        for x,k in ((-13.4,.74),(-10.5,1.),(-5.9,.96),(-3.9,.80)):
            ring=[]
            for y,z in points:
                y,z=center+(np.array((y,z))-center)*k
                fold=float(np.clip((abs(y)-27.)/7.,0.,1.))
                ring.append((x+(9.1 if s<0 else -3.2)*fold,y,z))
            loops.append(ring)
        first=len(m.faces)
        m.solid(loops,SKIN,bone)
        ink(m,m.faces[first:],[(s*y,z) for y,z in mark],INNER,bone,.30)


def face(m):
    first=len(m.faces)
    # Elongated coastal boar skull: sloping frontal bone, high temple, pronounced muzzle
    # saddle, long jaw and a cut underneath the jowl. Its width sits inside the shoulders.
    core=[(-7,0,149.4),(12.,0,146.5),(22.,0,139.7),(28.1,0,128.6),(35.4,0,120.2),
          (36.5,0,112.),(29.4,0,106.1),(14.6,0,102.8),(-2.5,0,103.5),(-17.7,0,108.2),
          (-28.7,0,121.8),(-28.8,0,134.0),(-19.,0,144.6)]
    for s in (-1,1):
        landmarks=[(-2.,15.,148.8),(12.8,16.6,144.1),(20.6,17.7,136.6),(25.6,17.2,129.2),
                   (29.4,19.5,120.2),(29.7,18.8,112.2),(23.3,19.4,107.1),(10.2,19.7,104.8),
                   (-8.,19.,106.2),(-19.2,17.8,112.),(-24.2,17.1,126.2),(-22.,16.0,137.0),
                   (-12.3,16.8,144.8),(-2.,25.1,142.6),(10.,26.4,138.5),(16.5,26.9,131.6),
                   (18.2,28.5,124.6),(18.9,28.5,115.5),(10.3,27.2,109.6),(-4.5,26.0,109.3),
                   (-14.,26.0,115.),(-18.3,25.6,126.1),(-12.1,25.4,138.),
                   (-1.,30.9,131.1),(4.7,32.2,122.0),(-3.,29.6,113.6)]
        core.extend((x,s*y,z+(.40 if s<0 and y>26. else 0.)) for x,y,z in landmarks)
    hull(m,core,SKIN,'head')
    skin=m.faces[first:]
    for s in (-1,1):
        bone='eyelid_l' if s<0 else 'eyelid_r'
        eye_y=s*15.0+1.
        level=-.75 if s<0 else .3
        # Low horizontal dark eyes sit under separate fleshy lids. Their outer ends are
        # level; the tired expression comes from coverage rather than an angry brow.
        eye=[(-3.9,-.16),(-3.1,-1.45),(-1.25,-2.25),(1.9,-2.16),(3.32,-1.23),
             (3.92,-.08),(3.15,1.26),(.75,1.95),(-1.8,1.72),(-3.35,1.02)]
        ink(m,skin,[(eye_y+x,131.4+z+level) for x,z in eye],EYE,bone,.69)
        lid=[(-4.65,.20),(-3.15,.81),(-.1,1.16),(2.75,.92),(4.65,.38),(5.1,1.72),
             (2.25,3.62),(-.62,4.12),(-3.50,3.55),(-4.98,1.74)]
        lidpoints=[]
        for x,z in lid:
            y=eye_y+x;h=131.15+z+level
            lidpoints.append((project_x(m,skin,y,h)+1.09,y,h))
        cy=eye_y;cz=133.64
        mid=np.array([project_x(m,skin,cy,cz)+3.0,cy,cz])
        for i,v in enumerate(lidpoints):
            tri=[mid,v,lidpoints[(i+1)%len(lidpoints)]]
            if np.cross(np.subtract(tri[1],tri[0]),np.subtract(tri[2],tri[0]))[0]<0:tri.reverse()
            m.surface(tri,[(0,1,2)],'9E776B' if i in (0,1,2,3) else 'B38675','head',.76)
        cheek=[(20.3,118.6),(23.4,116.5),(28.4,118.0),(29.3,121.0),(26.0,122.0),(22.1,120.9)]
        ink(m,skin,[(s*y,z) for y,z in cheek],CHEEK,'head',.34)
    # The long nasal bone sweeps continuously from the bridge between the eyes to the
    # upper edge of the nose pad, with two dominant turning planes meeting down its crest.
    ridge=[(26.65,135.4,4.2,25.0,134.0),(30.25,131.9,6.0,27.4,130.0),
           (35.0,127.8,7.8,30.2,125.5),(40.9,124.7,9.35,35.1,122.25),
           (45.85,121.85,9.6,42.1,120.05)]
    for ss in (-1,1):
        for j in range(len(ridge)-1):
            a,b=ridge[j:j+2]
            quad=[(a[0],0.,a[1]),(a[3],ss*a[2],a[4]),(b[3],ss*b[2],b[4]),(b[0],0.,b[1])]
            if np.cross(np.subtract(quad[1],quad[0]),np.subtract(quad[2],quad[0]))[2]<0:quad.reverse()
            m.surface(quad,[(0,1,2,3)],'C1907C' if ss<0 else 'B98877','head',.72)
    # The nasal bridge is a sturdy forward wedge. A lighter bevel on top and a darker
    # bottom plane give the snout volume in a close gameplay camera.
    section=[(-10.9,-6.0),(10.8,-6.0),(15.65,-2.6),(14.6,3.7),(9.4,6.8),(-9.4,7.1),(-14.6,4.1),(-15.7,-2.45)]
    sections=[(30.,.65,120.0),(35.8,.84,119.65),(43.15,1.,118.1),(47.85,.95,116.75),(49.15,.78,116.35)]
    loops=[[(x-.19*z,y*k,zc+z*k) for y,z in section] for x,k,zc in sections]
    m.solid(loops,SNOUT,'head')
    # A tiny face inset, below its top bevel. Nose planes are swept to match that slope.
    for s in (-1,1):
        pit=[(-2.5,-.3),(-1.25,-1.65),(1.00,-1.75),(2.40,-.65),(2.5,.60),(1.15,1.65),(-1.,1.82),(-2.23,1.0)]
        points=[(49.46-.19*(.20+z)/.78,s*6.0+y,116.55+z) for y,z in pit]
        m.surface(points,[tuple(range(len(points)))],NOSTRIL,'head',.76)
    # A short cut runs from the side of the lip into the jowl. Its inner end sits
    # immediately behind the snout so his thinking expression is readable in profile.
    line=[(12.7,109.90),(16.65,109.45),(20.85,110.2),(23.7,111.8),
          (24.35,112.65),(23.73,110.6),(21.8,109.38),(18.4,108.85),(15.65,108.72)]
    for sign in (-1,1):
        ink(m,skin,[(sign*y,z+(.4 if sign<0 else 0.)) for y,z in line],'865F55','jaw',.60)
    ears(m)


def cap(m):
    def angle(p,a):
        p[2]-=.072*p[0]+.098*p[1]
        return p
    # A flat-topped waxed work cap. The crown slopes forward to a discrete short bill,
    # leaving the offside ear folded over the seam.
    m.tiers([(-5.1,0,143.3,20.5,23.2),(-5.4,0,145.3,22.2,24.6),(-6.9,0,151.6,22.1,24.3),
             (-9.3,0,155.1,19.8,23.0),(-10.6,0,156.5,14.9,17.7)],OILSKIN,'head',12,.77,angle)
    m.tiers([(-4.9,0,143.1,20.8,23.9),(-5.,0,144.0,22.,25.05),(-5.4,0,146.25,22.4,25.15)],
            '36493F','head',12,.74,angle)
    # Short visor is supported by its dedicated root and narrows toward the front.
    bill=[(0.,-22.,145.0),(12.,-21.3,144.5),(25.7,-15.1,143.4),(33.8,-7.1,143.1),(34.2,3.4,143.25),
          (28.4,12.55,143.6),(17.,19.1,144.4),(3.8,22.5,144.6),(-1.,16.,145.0),(5.5,0.,145.4),(-2.,-13.,145.1)]
    def brim(v):
        t=float(np.clip((v[0]-12.)/16.,0.,1.))
        return {'head':1.-t,'hat_brim':t}
    upper=[(x,y,z+.5-.07*y-1.8*x/29.) for x,y,z in bill]
    lower=[(x-.4,y*.98,z-.9-.07*y-1.8*x/29.) for x,y,z in bill]
    n=len(bill);center=(12.,-.25,146.9)
    m.surface(upper+[center],[(n,i,(i+1)%n) for i in range(n)],'586C52',brim,.65)
    m.surface(lower,[tuple(reversed(range(n)))],'35463A',brim,.72)
    m.surface(lower+upper,[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],'3E4E3E',brim,.66)


def ribbon(m,path,width,thickness,color,bone):
    """A real double-sided strap, swept with its broad face across the torso."""
    route=np.asarray(path,float)
    rings=[]
    for i,c in enumerate(route):
        forward=norm(route[min(i+1,len(route)-1)]-route[max(i-1,0)])
        outward=norm(np.array([c[0],c[1],0.]))
        face=norm(outward-forward*(outward@forward))
        side=norm(np.cross(forward,face))
        rings.append([c+side*w+face*d for w,d in
                      ((-width/2+1.,-thickness/2),(width/2-1.,-thickness/2),(width/2,0.),
                       (width/2-.6,thickness/2),(-width/2+.6,thickness/2),(-width/2,0.))])
    m.solid(rings,color,bone)



def workcoat(m,core):
    # The two hanging front edges expose the bib and separate from the body at the hip.
    # A wider back yoke, asymmetrical hem and low lapel points give the side view its weight.
    design=[(48.3,1.,26.3,31.0,.64),(51.,.2,27.,31.55,.62),(62.,0.,27.15,31.2,.61),
            (76.,-.5,26.25,31.55,.63),(87.,-1.5,25.8,33.6,.65),(96.7,-2.5,22.85,36.8,.63),
            (101.2,-3.,17.65,33.,.58),(104.75,-2.85,13.12,24.9,.47)]
    bands=[]
    sides=16 if m.detail==0 else 11 if m.detail==1 else 9
    for z,x,rx,ry,cut in design:
        row=[]
        for i in range(sides+1):
            a=cut+(TAU-2*cut)*i/sides
            h=z+(1.5*math.cos(a)-.7*math.sin(a))*(max(0.,66-z)/18.)
            row.append((x+rx*signed_power(math.cos(a),.77),ry*signed_power(math.sin(a),.77),h))
        bands.append(row)
    stride=sides+1;outer=np.array(bands).reshape((-1,3))
    faces=[(j*stride+i,j*stride+i+1,(j+1)*stride+i+1,(j+1)*stride+i)
           for j in range(len(bands)-1) for i in range(sides)]
    m.surface(outer,faces,JACKET,core)
    # The twin hem edges are visibly a layer of cloth, including their dark turned-under lining.
    for end in (0,sides):
        path=[row[end] for row in bands]
        inner=[(x-.78,y*.965,z+.05) for x,y,z in path]
        n=len(path); verts=path+inner
        m.surface(verts,[(j,j+1,j+n+1,j+n) if end else (j+n,j+n+1,j+1,j) for j in range(n-1)],'756B59',core)
    for ss in (-1,1):
        lapel=[(11.,ss*12.,104.2),(7.8,ss*21.,103.1),(14.2,ss*24.7,99.2),
               (18.7,ss*23.0,96.3),(17.8,ss*20.,96.8),(22.55,ss*17.55,87.6),
               (24.3,ss*15.65,83.1),(21.2,ss*16.5,95.6)]
        n=len(lapel)
        center=(17.0,ss*19.3,97.6)
        for i in range(n):
            tri=[center,lapel[i],lapel[(i+1)%n]]
            normv=np.cross(np.subtract(tri[1],tri[0]),np.subtract(tri[2],tri[0]))
            if normv[0]<0:tri.reverse()
            m.surface(tri,[(0,1,2)],'A09377','chest',.87)

def clothes(m):
    core=blend('pelvis','chest',54.,92.)
    garment_begin=len(m.faces)
    # Straight work trousers rise to a leather belt. The adult shirt has a strong yoke
    # and a slightly forward working belly, tapering into its waistband.
    m.tiers([(0,0,51.,18.,20.),(1.,0,58.4,23.6,26.6),(2.2,0,66.,24.2,28.1),
             (.6,0,75.0,24.2,29.2),(-.9,0,83.6,24.25,30.7),(-2,0,94.6,21.7,34.1),
             (-2.8,0,99.5,17.7,33.5),(-2.6,0,102.4,14.2,26.),(-1.6,0,105.0,11.,14.9)],
            CREAM,core,12,.79)
    m.tiers([(-.2,0,40.5,16.0,20.),(.6,0,45.7,21.3,25.6),(.7,0,54.,24.4,28.0),
             (.3,0,60.7,24.2,28.3),(.15,0,63.2,22.9,27.2)],BLUE,core,12,.76)
    m.tiers([(.25,0,59.7,24.4,28.45),(.25,0,60.35,24.82,28.8),(.15,0,63.65,24.,28.1)],
            '6D5C4B',core,12,.75)
    # Single substantial workman’s buckle, worn brass, slightly off-centre.
    brass=[(-3.6,-1.55),(3.3,-1.65),(4.,-.75),(3.9,1.3),(2.8,1.9),(-3.5,1.65),(-4.05,.75)]
    m.solid([[(x,y*k+2.4,61.5+z*k) for y,z in brass] for x,k in ((22.7,.85),(24.9,1.),(25.43,.8))],
            BRASS,core,.35)
    # Thin visible neck above the shirt, shortened by a restrained rust neckerchief.
    m.tiers([(-1.,0,97.4,11.7,14.5),(-.3,0,101.,13.2,15.2),(.7,0,108.2,12.9,15.0),
             (2.,0,112.2,10.2,13.0)],SKIN,'neck',10,.80)
    def dip(v,a):
        v[2]-=1.8*max(0.,math.cos(a))
        return v
    m.tiers([(-.7,0,98.2,13.4,17.8),(-.6,0,100.1,15.7,18.8),(-.4,0,103.2,15.0,18.0),
             (-.15,0,104.0,13.3,16.2)],SCARF,'chest',10,.78,dip)
    # Two linen collar folds intersect the tie; its short point sits inside the collar.
    for s in (-1,1):
        panel=[(10.9,s*11.,103.5),(5.4,s*17.8,103.1),(12.4,s*22.7,99.7),
               (18.35,s*16.4,91.4),(17.95,s*9.,97.7)]
        if s>0: panel.reverse()
        reverse=np.cross(np.subtract(panel[1],panel[0]),np.subtract(panel[2],panel[0]))[0]<0
        m.surface(panel,[tuple(reversed(range(5))) if reverse else tuple(range(5))],'A4967E','chest')
    m.tube([(15.,5.0,100.7,3.3,2.0),(16.6,4.4,98.4,4.1,2.25),
            (19.0,2.0,94.7,2.8,1.5),(20.2,-.5,92.4,.45,.45)],'A85C45','chest',5)
    # Canvas chest pocket, trimmed in the same fabric. No large baby bib.
    pocket=[(20.45,-9.1,84.2),(18.5,-18.8,83.7),(18.1,-19.6,91.0),(20.0,-9.2,91.6)]
    m.surface(pocket,[(3,2,1,0)],'AB9E83',core)
    # The long indigo bib sits beneath an open canvas jacket. It rises straight from
    # the trousers, with brass adjusters on the two surviving straps.
    front=m.faces[garment_begin:]
    # A concave patch would clip through the belly if triangulated as a flat fan. Sample
    # the work shirt across the full width so the actual cloth follows its changing radius.
    stages=[(59.5,21.),(63.,20.1),(68.0,18.4),(73.5,16.8),(80.,16.05),(84.6,15.),
            (86.8,11.6)]
    segments=10 if not m.detail else 8
    grid=[]
    for z,wide in stages:
        for y in np.linspace(-wide,wide,segments+1):
            grid.append((project_x(m,front,y,z)+1.11,y,z))
    n=segments+1
    m.surface(grid,[(r*n+c,r*n+c+1,(r+1)*n+c+1,(r+1)*n+c) for r in range(len(stages)-1) for c in range(segments)],BLUE,core)
    for sign in (-1,1):
        strip=[(sign*12.7,83.1),(sign*17.5,82.6),(sign*17.4,93.3),(sign*14.8,98.3),(sign*11.6,98.)]
        ink(m,front,strip,DARK_BLUE,core,.91)
        lock=[(sign*13.65,87.1),(sign*17.2,87.0),(sign*17.0,89.2),(sign*13.8,89.3)]
        ink(m,front,lock,BRASS,core,1.20)
    workcoat(m,core)
    # Broad satchel strap on the right shoulder crosses to the lower left hip.
    path=[(-18.,-23.,58.5),(-24.8,-13.,70.3),(-26.9,-3.8,79.2),(-24.6,8.,89.1),
          (-17.1,19.2,98.8),(-7.8,23.,102.1),(3.2,22.4,102.9),(12.3,18.5,100.0),
          (18.8,12.7,95.1),(23.5,4.3,85.5),(25.8,-5.4,76.3),(23.9,-15.4,67.2),
          (15.2,-24.0,58.3),(3.0,-28.,54.8),(-6.2,-28.7,54.5)]
    shell=m.faces[garment_begin:]
    continuous=[]
    for start,end in zip(np.asarray(path)[:-1],np.asarray(path)[1:]):
        count=max(1,int(np.ceil(np.linalg.norm(end-start)/3.8)))
        continuous.extend(start+(end-start)*i/count for i in range(count))
    continuous.append(path[-1])
    fitted=[]
    for x,y,z in continuous:
        try:
            if x>5: x=max(x,project_x(m,shell,y,z)+2.30)
            if x<-5: x=min(x,project_x(m,shell,y,z,back=True)-1.95)
        except ValueError:
            pass
        fitted.append((x,y,z))
    ribbon(m,fitted,5.5,1.55,LEATHER,core)
    # Rugged document pouch at the left hip. Its trapezoidal flap is a single saturated
    # accent in the leather; reaching for it never obscures the map or facial silhouette.
    m.tiers([(-10.8,-27.3,40.9,7.0,3.6),(-10.3,-28.4,43.8,11.8,6.1),(-9.5,-28.1,55.3,12.1,6.1),
             (-9.4,-27.,59.4,11.6,5.1)],'765944','pelvis',8,.64)
    flap=[(-20.8,-32.3,56.9),(-.6,-34.55,57.5),(1.25,-34.4,51.6),(-6.,-35.25,48.5),(-18.6,-33.4,50.1)]
    m.surface(flap,[tuple(range(5)),tuple(reversed(range(5)))],'89694C','pelvis',.70)
    # A single folded chart peeks just clear of the pouch flap.
    p=[(-19.3,-29.,59.2),(-7.8,-31.,60.1),(-8.9,-30.8,63.8),(-17.8,-28.6,62.6)]
    m.surface(p,[(0,1,2,3),(3,2,1,0)],'C5B794','pelvis',.94)


def limbs(m):
    for s,side in ((-1,'l'),(1,'r')):
        upper,fore,hand='upperarm_'+side,'lowerarm_'+side,'hand_'+side
        # Thick shoulders, fabric hung from the elbow, sleeves rolled into one deliberate cuff.
        m.tube([(-2.,s*22.,98.5,7.1,8.8),(-3.,s*29.,98.1,10.0,10.2),
                (-2.8,s*35.2,93.9,10.8,10.7),(-1.9,s*38.4,86.4,9.9,10.0),
                (.15,s*40.4,80.9,9.3,9.3),(1.2,s*41.5,77.2,8.65,8.9)],
               JACKET,upper,10)
        m.tube([(.2,s*40.3,81.6,9.2,9.3),(1.0,s*41.0,80.0,10.05,10.05),
                (2.05,s*41.8,76.65,10.15,9.98),(2.5,s*42.1,75.0,9.2,9.4)],'AA9A7B',fore,10)
        m.tube([(1.25,s*41.0,78.5,7.7,7.8),(2.5,s*42.1,75.5,8.95,9.0),
                (4.75,s*44.6,70.3,9.85,9.4),(7.1,s*45.8,62.7,8.75,8.1),
                (8.45,s*45.65,57.1,7.05,7.25)],SKIN,fore,10)
        m.tiers([(9.15,s*46.0,46.3,5.85,6.0),(9.5,s*46.55,49.1,8.5,8.45),
                 (9.25,s*46.0,54.8,9.15,8.8),(8.5,s*45.3,59.,7.75,7.4),
                 (8.25,s*45.4,60.4,6.35,6.1)],SKIN,hand,9,.68)
        for t in (-1,1):
            m.tiers([(9.95+3.22*t,s*46.6,44.95,2.6,4.6),(9.9+3.2*t,s*46.8,46.4,3.6,6.1),
                     (9.5+3.33*t,s*46.8,51.15,4.05,6.8),(9.3+3.45*t,s*46.4,53.15,3.75,6.2)],
                    HOOF,hand,7,.65)
        # The opposable thumb follows the hand, maintaining a functional gripping profile in first person.
        m.tube([(12.2,s*40.7,55.1,3.3,3.6),(15.,s*39.8,53.65,3.5,3.7),
                (16.0,s*40.5,49.1,2.55,2.8)],SKIN,hand,7)
        hip,knee,foot='thigh_'+side,'calf_'+side,'foot_'+side
        sign_angle=.72*s
        # Full-length navy workwear, two broad knee planes, cuffs resting on sturdy boots.
        m.tiers([(.5,s*17.7,28.4,11.6,11.9),(-.1,s*18.2,33.,13.5,13.4),
                 (.75,s*17.0,39.3,14.2,15.0),(.7,s*15.2,47.7,15.5,15.6),
                 (.35,s*14.7,53.2,13.1,14.1),(.25,s*14.4,57.,11.7,12.9)],BLUE,hip,9,.64,warp=lambda v,a: (v[0]+1.45*math.sin(2*a),v[1],v[2]+2.4*math.cos(a+sign_angle)))
        m.tiers([(.9,s*19.,15.7,9.5,9.7),(.55,s*19.1,21.,11.3,10.75),
                 (-1.5,s*18.7,26.2,12.1,12.4),(-.0,s*18.3,31.9,13.2,13.3),
                 (.65,s*17.8,34.4,10.65,11.3)],BLUE,blend(knee,hip,26.,34.),8,.64,warp=lambda v,a:(v[0],v[1],v[2]+1.5*math.cos(a-.75*s)))
        m.tiers([(.75,s*19.05,17.5,10.4,10.2),(.75,s*19.05,18.6,11.65,11.3),(.3,s*19.,21.1,11.6,11.2)],
                MID_BLUE,knee,9,.75)
        # Block heel, angled boot toe and a dark welt. No exposed baby ankles.
        m.tiers([(6.9,s*19.7,.6,15.7,11.8),(7.2,s*19.7,1.55,16.9,12.3),
                 (7.0,s*19.7,4.05,17.,12.2)],'4D4B45',foot,10,.59)
        m.tiers([(7.2,s*19.7,3.25,16.65,12.15),(6.9,s*19.6,6.75,16.65,12.3),
                 (5.3,s*19.55,9.8,14.75,12.05),(2.45,s*19.3,13.15,11.5,11.25),
                 (.8,s*19.0,18.7,10.25,10.4),(.5,s*19.,21.1,9.9,10.1)],'695D50',foot,10,.63)
    tail=[(-19.,.8,53.8,2.8,2.85),(-25.,.1,54.8,3.1,3.1),(-30.3,1.8,56.1,2.8,2.75),
          (-33.4,4.6,59.4,2.35,2.35),(-32.2,7.7,62.0,2.1,2.1),(-29.4,8.5,61.1,1.83,1.75),
          (-29.2,6.6,58.5,1.35,1.35),(-31.5,5.0,58.9,.60,.60)]
    m.tube(tail,'B48679',blend('tail_01','tail_02',54.,60.),7)


def character(detail=0):
    mesh=Sculpture(detail)
    clothes(mesh)
    limbs(mesh)
    first=len(mesh.vertices)
    face(mesh)
    cap(mesh)
    # The narrower adult skull sits well within the wide, rolled shoulders. Keep its
    # full nose depth and jaw height, and transform surface normals by the inverse scale.
    for i in range(first,len(mesh.vertices)):
        v=mesh.vertices[i];mesh.vertices[i]=np.array([v[0],v[1]*.91,v[2]])
        vn=mesh.normals[i];mesh.normals[i]=norm(np.array([vn[0],vn[1]/.91,vn[2]]))
    return mesh

def fist(full):
    # Use the palm, split hoof and thumb of the actual animal in the player's eye camera.
    # +X points to the tip and +Z to the knuckles. The arm, skin and hoof stay contiguous.
    result=Sculpture()
    hand_index=full.bones.index('hand_r')
    faces=[f for f in full.faces if all(full.joints[i][0]==hand_index and full.weights[i][0]>.999 for i in f)]
    used=sorted(set(np.asarray(faces).ravel()))
    indices={old:new for new,old in enumerate(used)}
    points=np.asarray(full.vertices)[used]
    center=(points.max(axis=0)+points.min(axis=0))*.5
    rot=np.array([[0.,0.,-1.],[0.,1.,0.],[1.,0.,0.]])
    result.vertices=((points-center)@rot.T)*(100./np.max(np.ptp(points,axis=0)))
    result.normals=np.asarray(full.normals)[used]@rot.T
    result.colors=np.asarray(full.colors)[used]
    result.faces=[tuple(indices[i] for i in f) for f in faces]
    return result


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=HERE/'SK_ExpeditionPig.glb')
    output=parser.parse_args().output
    model=character()
    model.glb(output)
    fist(model).glb(output.with_name('SM_PigFist.glb'),skinned=False)
    for tier in (1,2):
        distant=character(tier)
        distant.glb(output.with_name(output.stem+f'_LOD{tier}.glb'))
        print(f'LOD{tier}: {len(distant.faces)} triangles, face topology preserved')
    print(f'{output}: {len(model.vertices)} vertices, {len(model.faces)} triangles, {len(BONES)} named bones')
