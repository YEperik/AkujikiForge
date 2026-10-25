from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from ui.model_data import load_mesh


class ModelDataTests(unittest.TestCase):
    def test_obj_vertices_and_faces(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "tiny.obj"
            path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="utf-8")
            mesh = load_mesh(path)
            self.assertEqual(mesh.source_format, "OBJ")
            self.assertEqual(len(mesh.vertices), 3)
            self.assertEqual(mesh.faces, [(0, 1, 2)])

    def test_obj_negative_indices(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "negative.obj"
            path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n", encoding="utf-8")
            mesh = load_mesh(path)
            self.assertEqual(mesh.faces, [(0, 1, 2)])

    def test_ascii_ply(self) -> None:
        content = """ply
format ascii 1.0
element vertex 3
property float x
property float y
property float z
element face 1
property list uchar int vertex_indices
end_header
0 0 0
1 0 0
0 1 0
3 0 1 2
"""
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "tiny.ply"
            path.write_text(content, encoding="ascii")
            mesh = load_mesh(path)
            self.assertEqual(mesh.source_format, "PLY (ASCII)")
            self.assertEqual(mesh.faces, [(0, 1, 2)])

    def test_binary_ply_is_refused(self) -> None:
        content = "ply\nformat binary_little_endian 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "binary.ply"
            path.write_bytes(content.encode("ascii") + b"\0" * 12)
            with self.assertRaisesRegex(ValueError, "Only ASCII PLY"):
                load_mesh(path)

    def test_unsupported_extension_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "sample.stl"
            path.write_text("not supported", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Choose a text-based"):
                load_mesh(path)


if __name__ == "__main__":
    unittest.main()
