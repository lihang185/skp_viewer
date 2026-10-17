"""ctypes reader for x64 slapi.dll; optional project mesh extension."""
import ctypes as C
from pathlib import Path


class Ref(C.Structure):
    _fields_ = [('ptr', C.c_void_p)]


class Point(C.Structure):
    _fields_ = [('x', C.c_double), ('y', C.c_double), ('z', C.c_double)]


class Transform(C.Structure):
    _fields_ = [('values', C.c_double * 16)]


class Color(C.Structure):
    _fields_ = [('red', C.c_ubyte), ('green', C.c_ubyte), ('blue', C.c_ubyte), ('alpha', C.c_ubyte)]


IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def multiply(a, b):
    return [sum(a[k * 4 + row] * b[col * 4 + k] for k in range(4))
            for col in range(4) for row in range(4)]


def transformed(m, p):
    v = [p.x, p.y, p.z, 1.0]
    q = [sum(m[col * 4 + row] * v[col] for col in range(4)) for row in range(4)]
    if abs(q[3]) < 1e-15:
        raise ValueError('Invalid homogeneous transformation')
    return [q[i] / q[3] for i in range(3)]


class EdgeReader:
    def __init__(self, dll_path):
        if C.sizeof(C.c_void_p) != 8:
            raise RuntimeError('Use 64-bit Python with the x64 DLL.')
        self.dll = C.CDLL(str(Path(dll_path).resolve()))
        self.api = {}
        signatures = {
            'SUModelCreateFromFile': [C.POINTER(Ref), C.c_char_p],
            'SUModelRelease': [C.POINTER(Ref)],
            'SUModelGetEntities': [Ref, C.POINTER(Ref)],
            'SUEntitiesGetNumEdges': [Ref, C.c_bool, C.POINTER(C.c_size_t)],
            'SUEntitiesGetEdges': [Ref, C.c_bool, C.c_size_t, C.POINTER(Ref), C.POINTER(C.c_size_t)],
            'SUVertexGetPosition': [Ref, C.POINTER(Point)],
            'SKPEdgeGetHidden': [Ref, C.POINTER(C.c_bool)],
            'SKPEdgeGetSoft': [Ref, C.POINTER(C.c_bool)],
        }
        for name in ('SUEdgeGetStartVertex', 'SUEdgeGetEndVertex', 'SUGroupGetEntities',
                     'SUComponentInstanceGetDefinition', 'SUComponentDefinitionGetEntities'):
            signatures[name] = [Ref, C.POINTER(Ref)]
        for name in ('SUGroupGetTransform', 'SUComponentInstanceGetTransform'):
            signatures[name] = [Ref, C.POINTER(Transform)]
        for kind in ('Groups', 'Instances', 'Faces'):
            signatures['SUEntitiesGetNum' + kind] = [Ref, C.POINTER(C.c_size_t)]
            signatures['SUEntitiesGet' + kind] = [Ref, C.c_size_t, C.POINTER(Ref), C.POINTER(C.c_size_t)]
        for name, args in signatures.items():
            fn = getattr(self.dll, name)
            fn.argtypes, fn.restype = args, C.c_int
            self.api[name] = fn
        for name in ('SUInitialize', 'SUTerminate'):
            fn = getattr(self.dll, name)
            fn.argtypes, fn.restype = [], None

    def call(self, name, *args):
        result = self.api[name](*args)
        if result:
            raise RuntimeError('%s failed (SUResult=%d). Unsupported or damaged SKP file.' % (name, result))

    def ref(self, name, value):
        out = Ref()
        self.call(name, value, C.byref(out))
        return out

    def items(self, entities, kind):
        n = C.c_size_t()
        extra = (False,) if kind == 'Edges' else ()  # All edges, including face boundaries.
        self.call('SUEntitiesGetNum' + kind, entities, *extra, C.byref(n))
        if n.value > 2000000:
            raise RuntimeError('Too many entities for this viewer')
        array = (Ref * n.value)()
        if n.value:
            self.call('SUEntitiesGet' + kind, entities, *extra, n.value, array, C.byref(n))
        return array[:n.value]

    def load(self, path, with_faces=False, with_textures=False):
        with_faces = with_faces or with_textures
        model = Ref()
        self.dll.SUInitialize()
        try:
            self.call('SUModelCreateFromFile', C.byref(model), str(Path(path).resolve()).encode('utf-8'))
            segments, active = [], set()
            triangles, mesh_cache = [], {}
            materials, batches = {}, {}
            uv_fallbacks = set()
            if with_faces:
                for name, args in {
                    'SKPFaceCreateMesh': [Ref, C.POINTER(Ref)],
                    'SKPMeshRelease': [C.POINTER(Ref)],
                    'SKPMeshGetSizes': [Ref, C.POINTER(C.c_size_t), C.POINTER(C.c_size_t)],
                    'SKPMeshGetData': [Ref, C.c_size_t, C.POINTER(Point), C.c_size_t, C.POINTER(C.c_uint)],
                }.items():
                    fn = getattr(self.dll, name)
                    fn.argtypes, fn.restype = args, C.c_int
                    self.api[name] = fn

            if with_textures:
                signatures = {
                    'SUMaterialGetColor': [Ref, C.POINTER(Color)],
                    'SUMaterialGetOpacity': [Ref, C.POINTER(C.c_double)],
                    'SUMaterialGetUseOpacity': [Ref, C.POINTER(C.c_bool)],
                    'SUMaterialGetType': [Ref, C.POINTER(C.c_int)],
                    'SKPMaterialGetColorizeShift': [Ref, C.POINTER(C.c_bool)],
                    'SKPTextureGetDataSize': [Ref, C.POINTER(C.c_size_t)],
                    'SKPTextureGetData': [Ref, C.c_size_t, C.POINTER(C.c_ubyte)],
                    'SKPFaceGetUVQ': [Ref, C.c_bool, Ref, C.c_size_t, C.POINTER(Point), C.POINTER(Point)],
                }
                for name in ('SUFaceGetFrontMaterial', 'SUFaceGetBackMaterial',
                             'SKPGroupGetMaterial', 'SKPInstanceGetMaterial', 'SUMaterialGetTexture'):
                    signatures[name] = [Ref, C.POINTER(Ref)]
                for name, args in signatures.items():
                    fn = getattr(self.dll, name)
                    fn.argtypes, fn.restype = args, C.c_int
                    self.api[name] = fn

            def optional_ref(name, source):
                result = Ref()
                code = self.api[name](source, C.byref(result))
                if code not in (0, 9):
                    raise RuntimeError('%s failed: %d' % (name, code))
                return result

            def read_material(ref):
                key = ref.ptr or 0
                if key in materials:
                    return key
                record = {'rgba': (1.0, 1.0, 1.0, 1.0), 'image': b'', 'type': 0}
                if ref.ptr:
                    color, opacity, use_opacity, kind = Color(), C.c_double(), C.c_bool(), C.c_int()
                    self.call('SUMaterialGetColor', ref, C.byref(color))
                    self.call('SUMaterialGetOpacity', ref, C.byref(opacity))
                    self.call('SUMaterialGetUseOpacity', ref, C.byref(use_opacity))
                    self.call('SUMaterialGetType', ref, C.byref(kind))
                    record['rgba'] = (color.red/255, color.green/255, color.blue/255,
                                      max(0, min(1, opacity.value)) if use_opacity.value else 1.0)
                    record['type'] = kind.value
                    shift = C.c_bool()
                    self.call('SKPMaterialGetColorizeShift', ref, C.byref(shift))
                    record['colorize_shift'] = shift.value
                    texture = optional_ref('SUMaterialGetTexture', ref)
                    if texture.ptr:
                        size = C.c_size_t()
                        self.call('SKPTextureGetDataSize', texture, C.byref(size))
                        if size.value > 128 * 1024 * 1024:
                            raise RuntimeError('Texture exceeds viewer limit')
                        if size.value:
                            data = (C.c_ubyte * size.value)()
                            self.call('SKPTextureGetData', texture, size.value, data)
                            record['image'] = bytes(data)
                materials[key] = record
                return key

            def visit(entities, matrix, depth=0, inherited=None):
                if depth > 128 or entities.ptr in active:
                    raise RuntimeError('Cyclic/deep entity hierarchy')
                active.add(entities.ptr)
                if with_faces:
                    for face in self.items(entities, 'Faces'):
                        if face.ptr not in mesh_cache:
                            mesh = self.ref('SKPFaceCreateMesh', face)
                            try:
                                nv, ni = C.c_size_t(), C.c_size_t()
                                self.call('SKPMeshGetSizes', mesh, C.byref(nv), C.byref(ni))
                                if nv.value > 2000000 or ni.value > 6000000:
                                    raise RuntimeError('Face mesh exceeds viewer limit')
                                points, indices = (Point * nv.value)(), (C.c_uint * ni.value)()
                                self.call('SKPMeshGetData', mesh, nv.value, points, ni.value, indices)
                                mesh_cache[face.ptr] = (points, indices)
                            finally:
                                self.call('SKPMeshRelease', C.byref(mesh))
                        points, indices = mesh_cache[face.ptr]
                        if len(triangles) + len(indices) // 3 > 2000000:
                            raise RuntimeError('Too many displayed triangles')
                        transformed_points = [transformed(matrix, p) for p in points]
                        triangles.extend([[transformed_points[indices[i+j]] for j in range(3)]
                                          for i in range(0, len(indices), 3)])
                        if with_textures:
                            # Negative scale reverses winding, but not front/back material identity.
                            m = matrix
                            determinant = (m[0]*(m[5]*m[10]-m[9]*m[6]) -
                                           m[4]*(m[1]*m[10]-m[9]*m[2]) +
                                           m[8]*(m[1]*m[6]-m[5]*m[2]))
                            for back, getter in ((False, 'SUFaceGetFrontMaterial'), (True, 'SUFaceGetBackMaterial')):
                                material = optional_ref(getter, face)
                                if not material.ptr:
                                    material = inherited or Ref()
                                key = read_material(material)
                                uvq = (Point * len(points))()
                                if materials[key]['image']:
                                    code = self.api['SKPFaceGetUVQ'](face, back, material, len(points), points, uvq)
                                    if code == 16:
                                        uv_fallbacks.add((face.ptr, back))
                                    elif code:
                                        raise RuntimeError('Face UV mapping failed (SUResult=%d)' % code)
                                batch = batches.setdefault((key, back, determinant < 0),
                                    {'material': key, 'back': back, 'mirrored': determinant < 0,
                                     'points': [], 'uvq': []})
                                for index in indices:
                                    batch['points'].append(transformed_points[index])
                                    q = uvq[index]
                                    batch['uvq'].append((q.x, q.y, 0.0, q.z) if materials[key]['image'] else (0, 0, 0, 1))
                for edge in self.items(entities, 'Edges'):
                    hidden = C.c_bool()
                    soft = C.c_bool()
                    self.call('SKPEdgeGetHidden', edge, C.byref(hidden))
                    self.call('SKPEdgeGetSoft', edge, C.byref(soft))
                    if hidden.value or soft.value:
                        continue
                    endpoints = []
                    for getter in ('SUEdgeGetStartVertex', 'SUEdgeGetEndVertex'):
                        vertex = self.ref(getter, edge)
                        point = Point()
                        self.call('SUVertexGetPosition', vertex, C.byref(point))
                        endpoints.append(transformed(matrix, point))
                    segments.append(endpoints)
                    if len(segments) > 2000000:
                        raise RuntimeError('Too many displayed edges')
                for kind in ('Groups', 'Instances'):
                    for item in self.items(entities, kind):
                        prefix = 'SUGroup' if kind == 'Groups' else 'SUComponentInstance'
                        transform = Transform()
                        self.call(prefix + 'GetTransform', item, C.byref(transform))
                        if kind == 'Groups':
                            children = self.ref('SUGroupGetEntities', item)
                        else:
                            definition = self.ref('SUComponentInstanceGetDefinition', item)
                            children = self.ref('SUComponentDefinitionGetEntities', definition)
                        material = inherited
                        if with_textures:
                            own = optional_ref('SKPGroupGetMaterial' if kind == 'Groups' else 'SKPInstanceGetMaterial', item)
                            if own.ptr:
                                material = own
                        visit(children, multiply(matrix, list(transform.values)), depth + 1, material)
                active.remove(entities.ptr)

            visit(self.ref('SUModelGetEntities', model), IDENTITY)
            if with_textures:
                return segments, triangles, {'materials': materials, 'batches': list(batches.values()),
                                              'uv_fallbacks': len(uv_fallbacks)}
            return (segments, triangles) if with_faces else segments
        finally:
            if model.ptr:
                self.call('SUModelRelease', C.byref(model))
            self.dll.SUTerminate()
