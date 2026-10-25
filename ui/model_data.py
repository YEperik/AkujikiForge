"""Small, dependency-free loaders for ASCII OBJ and ASCII PLY preview meshes.

This module intentionally supports text-based geometry only; it does not execute
scripts or load embedded material, texture, rig, or animation payloads.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

MAX_VERTICES = 500_000
MAX_FACES = 500_000


@dataclass(frozen=True)
class MeshData:
    vertices: list[tuple[float, float, float]]
    faces: list[tuple[int, ...]]
    source_format: str


def _check_vertices(vertices: list[tuple[float, float, float]]) -> None:
    if not vertices:
        raise ValueError("No vertices were found in the selected model.")
    if len(vertices) > MAX_VERTICES:
        raise ValueError(f"Model exceeds the {MAX_VERTICES:,}-vertex preview limit.")


def load_obj(path: str | Path) -> MeshData:
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, ...]] = []
    try:
        with Path(path).open("r", encoding="utf-8", errors="strict") as stream:
            for line_no, line in enumerate(stream, 1):
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                fields = stripped.split()
                if fields[0] == "v":
                    if len(fields) < 4:
                        raise ValueError(f"OBJ line {line_no}: vertex needs x, y and z coordinates.")
                    xyz = tuple(float(value) for value in fields[1:4])
                    vertices.append(xyz)  # type: ignore[arg-type]
                    if len(vertices) > MAX_VERTICES:
                        raise ValueError(f"Model exceeds the {MAX_VERTICES:,}-vertex preview limit.")
                elif fields[0] == "f":
                    if len(fields) < 4:
                        continue
                    face: list[int] = []
                    for token in fields[1:]:
                        index = int(token.split("/", 1)[0])
                        resolved = index - 1 if index > 0 else len(vertices) + index
                        if resolved < 0 or resolved >= len(vertices):
                            raise ValueError(f"OBJ line {line_no}: face index is out of range.")
                        face.append(resolved)
                    faces.append(tuple(face))
                    if len(faces) > MAX_FACES:
                        raise ValueError(f"Model exceeds the {MAX_FACES:,}-face preview limit.")
    except UnicodeDecodeError as exc:
        raise ValueError("This OBJ is not a UTF-8 text file.") from exc
    _check_vertices(vertices)
    return MeshData(vertices, faces, "OBJ")


def load_ply(path: str | Path) -> MeshData:
    try:
        raw_lines = Path(path).read_text(encoding="ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise ValueError("Only ASCII PLY is supported by the built-in preview.") from exc

    if not raw_lines or raw_lines[0].strip() != "ply":
        raise ValueError("The selected file does not have a valid PLY header.")
    if not any(line.strip() == "format ascii 1.0" for line in raw_lines[:100]):
        raise ValueError("Only ASCII PLY 1.0 is supported; convert binary PLY to ASCII first.")

    vertex_count = None
    face_count = 0
    current_element = None
    end_header = None
    for idx, line in enumerate(raw_lines):
        fields = line.strip().split()
        if not fields:
            continue
        if fields[0] == "element" and len(fields) == 3:
            current_element = fields[1]
            if current_element == "vertex":
                vertex_count = int(fields[2])
            elif current_element == "face":
                face_count = int(fields[2])
        elif fields[0] == "end_header":
            end_header = idx + 1
            break
    if end_header is None or vertex_count is None or vertex_count <= 0:
        raise ValueError("PLY header is missing a valid vertex declaration or end_header.")
    if vertex_count > MAX_VERTICES or face_count > MAX_FACES:
        raise ValueError("Model exceeds the built-in preview size limit.")

    # The minimal viewer expects x, y, z to be the first three vertex properties.
    vertex_lines = raw_lines[end_header:end_header + vertex_count]
    if len(vertex_lines) != vertex_count:
        raise ValueError("PLY ends before all declared vertices are present.")
    vertices: list[tuple[float, float, float]] = []
    for row_no, line in enumerate(vertex_lines, end_header + 1):
        fields = line.split()
        if len(fields) < 3:
            raise ValueError(f"PLY row {row_no}: vertex needs x, y and z coordinates.")
        try:
            vertices.append((float(fields[0]), float(fields[1]), float(fields[2])))
        except ValueError as exc:
            raise ValueError(f"PLY row {row_no}: invalid vertex coordinate.") from exc

    faces: list[tuple[int, ...]] = []
    face_start = end_header + vertex_count
    for row_no, line in enumerate(raw_lines[face_start:face_start + face_count], face_start + 1):
        fields = line.split()
        if not fields:
            continue
        try:
            count = int(fields[0])
            indices = tuple(int(value) for value in fields[1:1 + count])
        except ValueError as exc:
            raise ValueError(f"PLY row {row_no}: invalid face declaration.") from exc
        if len(indices) != count or any(index < 0 or index >= len(vertices) for index in indices):
            raise ValueError(f"PLY row {row_no}: face index is out of range.")
        if len(indices) >= 3:
            faces.append(indices)
    _check_vertices(vertices)
    return MeshData(vertices, faces, "PLY (ASCII)")


def load_mesh(path: str | Path) -> MeshData:
    extension = Path(path).suffix.lower()
    if extension == ".obj":
        return load_obj(path)
    if extension == ".ply":
        return load_ply(path)
    raise ValueError("Choose a text-based .obj or ASCII .ply file.")
