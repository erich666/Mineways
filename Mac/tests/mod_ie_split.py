"""IE split cell conservation, interpolation, bounds and manifest regressions."""
from pathlib import Path
import tempfile
import math
import struct
import unittest
from mod_assets import mod, ie_split_assets

split = mod._split_module

class SplitTests(unittest.TestCase):
    def face(self, x0=0,x1=32):
        return {'texture':'fixture:color','vertices':[(x0,2,2,0,0),(x1,2,2,16,0),(x1,2,10,16,16),(x0,2,10,0,16)]}

    def test_cell_coordinates_and_uv_interpolation(self):
        result=split.split_polygons([self.face()], [[0,0,0],[1,0,0]], mod.ImportError)
        self.assertEqual([len(v) for v in result.values()],[1,1])
        for key,faces in result.items():
            vertices=faces[0]['vertices']
            self.assertEqual((min(v[0] for v in vertices),max(v[0] for v in vertices)),(0,16))
            self.assertEqual((min(v[3] for v in vertices),max(v[3] for v in vertices)),(key[0]*8,key[0]*8+8))

    def test_coplanar_positive_side_and_overhang_clumping(self):
        p={'texture':'fixture:color','vertices':[(16,0,0,0,0),(16,8,0,16,0),(16,8,8,16,16),(16,0,8,0,16)]}
        result=split.split_polygons([p],[[0,0,0],[1,0,0]],mod.ImportError)
        self.assertFalse(result[(0,0,0)])
        self.assertEqual(len(result[(1,0,0)]),1)
        result=split.split_polygons([self.face(32,48)],[[0,0,0],[1,0,0]],mod.ImportError)
        self.assertEqual(min(v[0] for v in result[(1,0,0)][0]['vertices']),16)

    def test_reject_invalid_parts_and_work(self):
        for parts in ([],[[0,0,0]]*2,[[True,0,0]],[[17,0,0]],[[0,0]]):
            with self.subTest(parts=parts),self.assertRaises(mod.ImportError):
                split.split_polygons([self.face()],parts,mod.ImportError)
        with self.assertRaises(mod.ImportError):
            split.split_polygons([self.face(0,4096)],[[0,0,0]],mod.ImportError)

    def test_manifest_context_parts_and_source_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'source';ie_split_assets(source)
            output=Path(temp)/'pack'
            report=mod.compile_pack([source],output)
            text=(output/'manifest.mwpack').read_text()
            self.assertEqual(report['format'],6)
            self.assertIn('splitpolicy\tdummy\timmersiveengineering:cloche\t0\t0\t0\t1\n',text)
            self.assertEqual(text.count('\npart\t'),6)
            self.assertIn('state\tfixture:ie_split\t',text)
            self.assertNotIn('dummy=',text)

    def test_verified_provider_ids(self):
        self.assertEqual(split.policy_for('immersiveengineering:block/core_drill_split',[[0,0,0]],mod.ImportError)[1], 'immersiveengineering:sampledrill')
        self.assertEqual(split.policy_for('immersiveengineering:block/transformer_hv_left_mirrored_split',[[0,0,0]],mod.ImportError)[1], 'immersiveengineering:transformerhv')

    def test_java_hashset_ownership_reference(self):
        parts=[(x,y,z) for x in range(-1,2) for y in range(-1,2) for z in range(-1,2)]
        self.assertEqual(split.clump_order(parts,mod.ImportError)[:8],
            [(-1,0,0),(0,0,-1),(0,0,0),(1,0,-1),(-1,0,-1),(0,0,1),(1,0,0),(1,0,1)])

    def test_epsilon_zero_endpoints_do_not_create_opposite_slivers(self):
        # Shipped Polygon.splitAlong returns a whole face to its sole nonzero
        # sign. Points inside its 1e-5-block tolerance do not add a second side.
        vertices=[(0,0,16+split.EPS/2,0,0),(0,16,15,0,16),
                  (16,16,15,16,16),(16,0,16+split.EPS/2,16,0)]
        self.assertEqual(split._clip(vertices,2,16,False),vertices)
        self.assertEqual(split._clip(vertices,2,16,True),[])
        flat=[(x,y,16, u,v) for x,y,z,u,v in vertices]
        self.assertEqual(split._clip(flat,2,16,True),flat)
        self.assertEqual(split._clip(flat,2,16,False),[])

    def test_stable_fan_preserves_clipped_vertices_uvs_winding_and_area(self):
        # A source quad crossing a cell boundary produces a five-corner face.
        # Its first fan triangle collapses at a normal export anchor, although
        # the complete face is wide. This reproduces the recovered radio tower.
        polygon={'texture':'fixture:color','vertices':[
            (10,0,16.000016,0,16.000016),(10,16,15,16,15),
            (10,16,18,16,18),(10,0,18,0,18)]}
        pieces=split.split_polygons([polygon],[[0,0,0],[0,0,1]],mod.ImportError)[(0,0,1)]
        f32=lambda value:struct.unpack('f',struct.pack('f',value))[0]
        def world(vertex):
            # Same float anchor/scale path as the native polygon emitter.
            return tuple(f32(anchor+f32(f32(f32(value)-8)+8)/16) for anchor,value in zip((17,1,41),vertex[:3]))
        self.assertGreater(len(pieces),1)
        for piece in pieces:
            self.assertGreater(split._cross_squared(*(world(v) for v in piece['vertices'][:3])),0)
        corners=[(10,0,0.000016,0,0),(10,0.000255995904065538,0,0.000255995904065538,0.000255995904065538),
                 (10,16,0,21.333333333333332,16),(10,16,2,32,16),(10,0,2,16,0)]
        corners=[(*v[:3],v[1],v[2]) for v in corners]
        # Check fan selection itself against a known convex polygon independently
        # of the clipping interpolation. Cyclic permutation must not edit corners.
        triangles=split._stable_fan(corners)
        self.assertEqual({tuple(v) for t in triangles for v in t},set(corners))
        area=lambda a,b,c:math.sqrt(split._cross_squared(a,b,c))/2
        expected=sum(area(corners[0],corners[i],corners[i+1]) for i in range(1,len(corners)-1))
        self.assertAlmostEqual(sum(area(*t) for t in triangles),expected,places=10)
        for a,b,c in triangles:
            self.assertGreater((b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]),0)

    def test_stable_fan_leaves_nonconvex_input_order_unchanged(self):
        corners=[(0,0,0,0,0),(4,0,0,4,0),(2,1,0,2,1),(4,4,0,4,4),(0,4,0,0,4)]
        expected=[[corners[0],corners[i],corners[i+1]] for i in range(1,len(corners)-1)]
        self.assertEqual(split._stable_fan(corners),expected)

    def test_stable_fan_preserves_nonaffine_uv_interpolation(self):
        corners=[(0,0,0,0,0),(4,0,0,4,0),(5,2,0,5,2),(4,4,0,4,4),(0,4,0,0,4)]
        self.assertTrue(split._affine_uv(corners))
        corners[2]=(5,2,0,9,2)
        self.assertFalse(split._affine_uv(corners))
        self.assertEqual(split._stable_fan(corners),
            [[corners[0],corners[i],corners[i+1]] for i in range(1,len(corners)-1)])
