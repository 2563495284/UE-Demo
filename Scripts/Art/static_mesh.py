"""Write flat-shaded mesh faces to an Unreal StaticMesh, preserving its authored collision."""
import math
import unreal as ue


class Facets:
    """Faces use outward, counter-clockwise vertices in Unreal's XYZ coordinates."""

    def __init__(self):
        self.vertices = []
        self.normals = []
        self.uvs = []
        self.colors = []
        self.triangles = []

    def face(self, *points):
        a, b, c = (ue.Vector(*p) for p in points[:3])
        cross = (b - a).cross(c - a)
        length = math.sqrt(cross.dot(cross))
        if length < 1.e-6:
            raise ValueError(f"Degenerate face at {points[:3]}")
        normal = cross / length
        start = len(self.vertices)
        self.vertices.extend(ue.Vector(*p) for p in points)
        self.normals.extend([normal] * len(points))
        # Each hard face owns its corners. A planar UV gives UE a well-defined tangent.
        axis = max(range(3), key=lambda i: abs((normal.x, normal.y, normal.z)[i]))
        self.uvs.extend(ue.Vector2D(p[(axis+1)%3]/100., p[(axis+2)%3]/100.) for p in points)
        # In UE a front-facing triangle winds clockwise.
        self.triangles.extend(ue.IntVector(start, start+i+1, start+i) for i in range(1, len(points)-1))

    def write(self, path, source_shape):
        stock = "/Engine/BasicShapes/" + source_shape
        asset = ue.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else None
        if not asset:
            asset = ue.EditorAssetLibrary.duplicate_asset(stock, path)
        if not asset:
            raise RuntimeError(f"Cannot create {path}")

        buffers = ue.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = self.vertices
        buffers.normals = self.normals
        buffers.uv0 = self.uvs
        if self.colors:
            buffers.vertex_colors = self.colors
        buffers.triangles = self.triangles
        model = ue.DynamicMesh()
        ue.GeometryScript_MeshEdits.append_buffers_to_mesh(model, buffers)
        options = ue.GeometryScriptCopyMeshToAssetOptions()
        options.enable_recompute_normals = False
        options.enable_recompute_tangents = True
        mesh, result = ue.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(
            model, asset, options, ue.GeometryScriptMeshWriteLOD())
        if result != ue.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError(f"Cannot build {path}: {result}")
        # The render LOD is replaced on a duplicate of the stock mesh. Its simple shapes
        # and baseline material survive, so existing pawn casts and palette overrides still work.
        if "/Harbour/" in path or "/Neutral/" in path or "/SM_Coffer" in path:
            asset.set_editor_property("allow_cpu_access", True)
        if not ue.EditorAssetLibrary.save_loaded_asset(asset):
            raise RuntimeError(f"Cannot save {path}")
        print(f"FACET_ASSET {path}: {len(self.triangles)} triangles")
        return asset
