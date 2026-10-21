#!/usr/bin/env python3
"""Read-only audit helper for PS2 sample assets; not a production format parser.

Usage:
  python sample_audit.py '/path/to/GAME DAtA.zip' --out-dir results
  python sample_audit.py '/path/to/extracted/assets' --out-dir results

Only Python's standard library is required. ffprobe metadata is optional.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path
from typing import Iterable

TAGS = [
    b"IZCA", b"MLX0", b"MXTL", b"MXTF", b"HTSF", b"GBIX", b"PVRT",
    b"HMDL", b"KFMD", b"MLE0", b"PPPL", b"EOFC", b"ESPC", b"ESPM",
    b"EVDT", b"EVAC", b"ECMD", b"ENAM", b"EMSG", b"NPJ_", b"GNE_",
    b"MAPA", b"ISHD", b"ISBD", b"MFNT", b"MFGT",
]
MEDIA_EXTS = {".sfd", ".adx"}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_files(source: Path) -> tuple[list[tuple[str, bytes]], dict]:
    if source.is_file() and zipfile.is_zipfile(source):
        with zipfile.ZipFile(source, "r") as zf:
            bad = zf.testzip()
            files = [(i.filename, zf.read(i)) for i in zf.infolist() if not i.is_dir()]
        return files, {"kind": "zip", "path": str(source), "sha256": sha256(source.read_bytes()), "crc_check": "OK" if bad is None else f"BAD:{bad}"}
    if source.is_dir():
        files = [(p.relative_to(source).as_posix(), p.read_bytes()) for p in sorted(source.rglob("*")) if p.is_file()]
        return files, {"kind": "directory", "path": str(source), "sha256": None, "crc_check": "not_applicable"}
    raise SystemExit(f"Input must be a readable ZIP archive or directory: {source}")


def classify(name: str, data: bytes) -> str:
    ext = Path(name).suffix.upper()
    if data.startswith(b"SOT:BIN"):
        return "SOT string/dialogue table (signature observed)"
    if data.startswith(b"ESP"):
        return "ESP container/archive (signature observed)"
    if data.startswith(b"EVDT"):
        return "EVD event/script data (signature observed)"
    if data.startswith(b"MAPA"):
        return "EST map/scene data (MAPA signature observed)"
    if data.startswith(b"IZCA"):
        return f"IZCA container ({ext or 'no extension'})"
    if ext == ".SFD" and data.startswith(b"\x00\x00\x01\xBA"):
        return "MPEG system stream / CRI Sofdec candidate"
    if ext == ".ADX" and data.startswith(b"\x80\x00"):
        return "CRI ADX audio candidate"
    if ext == ".BDA":
        return "BDA data (layout not reconstructed)"
    return "Unclassified / proprietary data"


def inspect_outer_izca(data: bytes) -> dict:
    """Check only the observed outer-size invariant; does not decode chunks."""
    if not data.startswith(b"IZCA"):
        return {"applicable": False, "status": "not_applicable"}
    if len(data) < 16:
        return {"applicable": True, "status": "invalid_short_header", "actual_size": len(data)}
    payload_size, header_span = struct.unpack_from("<II", data, 4)
    calculated = 16 + payload_size + header_span
    return {
        "applicable": True, "payload_size": payload_size, "header_span": header_span,
        "calculated_size": calculated, "actual_size": len(data),
        "status": "pass" if calculated == len(data) else "mismatch",
    }


def parse_sot(name: str, data: bytes) -> tuple[list[dict], dict]:
    out: list[dict] = []
    info = {"file": name, "status": "not_applicable"}
    if not data.startswith(b"SOT:BIN"):
        return out, info
    if len(data) < 0x20:
        return out, {"file": name, "status": "invalid_short_header"}
    count = struct.unpack_from("<I", data, 0x0C)[0]
    table_start = 0x20
    max_records = max(0, (len(data) - table_start) // 8)
    if count > max_records:
        return out, {"file": name, "status": "invalid_record_count", "count": count, "max_records": max_records}
    invalid = 0
    for i in range(count):
        key, off = struct.unpack_from("<II", data, table_start + i * 8)
        if off >= len(data):
            text = ""
            valid = False
        else:
            nul = data.find(b"\0", off)
            if nul < 0:
                text = data[off:].decode("utf-8", "replace")
                valid = False
            else:
                text = data[off:nul].decode("utf-8", "replace")
                valid = True
        invalid += not valid
        out.append({"file": name, "index": i, "key_u32": f"0x{key:08X}", "string_offset": f"0x{off:X}", "valid_nul_terminated": valid, "text": text})
    info = {"file": name, "status": "parsed", "record_count": count, "valid_strings": count - invalid, "invalid_strings": invalid, "table_start": "0x20"}
    return out, info


def ffprobe(name: str, data: bytes) -> dict | None:
    if Path(name).suffix.lower() not in MEDIA_EXTS or shutil.which("ffprobe") is None:
        return None
    # ffprobe needs a real file path; create a short-lived local temp file only if needed.
    import tempfile
    suffix = Path(name).suffix.lower()
    with tempfile.NamedTemporaryFile(suffix=suffix) as tmp:
        tmp.write(data)
        tmp.flush()
        proc = subprocess.run([
            "ffprobe", "-v", "error", "-show_entries",
            "format=format_name,duration,size:stream=codec_type,codec_name,sample_rate,channels,width,height,avg_frame_rate",
            "-of", "json", tmp.name,
        ], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        return {"file": name, "status": "ffprobe_error", "error": proc.stderr.strip()}
    try:
        return {"file": name, "status": "ok", "metadata": json.loads(proc.stdout)}
    except json.JSONDecodeError:
        return {"file": name, "status": "invalid_ffprobe_json"}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, help="ZIP archive or extracted sample directory")
    ap.add_argument("--out-dir", type=Path, default=Path("sample_audit_results"))
    args = ap.parse_args()
    files, source_info = source_files(args.source)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    manifest = []
    sot_rows = []
    sot_info = []
    media = []
    izca_checks = []
    for name, data in files:
        sigs = {tag.decode("ascii"): data.count(tag) for tag in TAGS if data.count(tag)}
        check = "not_applicable"
        field4 = field8 = None
        izca = inspect_outer_izca(data)
        if izca.get("applicable"):
            field4, field8 = struct.unpack_from("<II", data, 4) if len(data) >= 16 else (None, None)
            check = izca["status"] if izca["status"] == "pass" else f"{izca['status']}: expected {izca.get('calculated_size', 'n/a')}, actual {len(data)}"
            izca_checks.append({"file": name, "size_bytes": len(data), "field_0x04": field4, "field_0x08": field8, "calculated_size": izca.get("calculated_size"), "status": izca["status"]})
        manifest.append({
            "file": name, "size_bytes": len(data), "sha256": sha256(data),
            "first_16_bytes_hex": data[:16].hex(" ").upper(),
            "classification": classify(name, data),
            "IZCA_occurrences": data.count(b"IZCA"),
            "outer_IZCA_size_check": check,
            "outer_IZCA_field_0x04": field4 if field4 is not None else "",
            "outer_IZCA_field_0x08": field8 if field8 is not None else "",
            "observed_tags": ";".join(f"{k}:{v}" for k, v in sigs.items()),
        })
        rows, info = parse_sot(name, data)
        sot_rows.extend(rows)
        if info["status"] != "not_applicable":
            sot_info.append(info)
        m = ffprobe(name, data)
        if m is not None:
            media.append(m)

    if manifest:
        with (args.out_dir / "sample_manifest.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(manifest[0].keys()))
            w.writeheader(); w.writerows(manifest)
    with (args.out_dir / "sot_strings.tsv").open("w", newline="", encoding="utf-8") as f:
        fields = ["file", "index", "key_u32", "string_offset", "valid_nul_terminated", "text"]
        w = csv.DictWriter(f, fieldnames=fields, delimiter="\t", lineterminator="\n")
        w.writeheader(); w.writerows(sot_rows)

    summary = {
        "tool": "sample_audit.py",
        "scope_note": "Read-only exploratory helper; it is not a production parser and does not modify source assets.",
        "source": source_info,
        "file_count": len(files),
        "total_bytes": sum(len(data) for _, data in files),
        "izca_outer_checks": izca_checks,
        "sot_tables": sot_info,
        "media_probe": media,
        "known_limitations": [
            "No C++/CMake source repository was present in the supplied files.",
            "Observed four-byte tags are evidence of structure, not complete definitions of their semantics.",
            "Nested IZCA occurrences are counted but not extracted or decompressed by this helper.",
            "The SOT table layout is verified only against the supplied EV00_00_00E.SOT sample.",
        ],
    }
    (args.out_dir / "analysis_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Audited {len(files)} files / {sum(len(d) for _, d in files):,} bytes")
    print(f"Source integrity: {source_info.get('crc_check', 'not checked')}")
    print(f"Outer IZCA size checks: {sum(x['status']=='pass' for x in izca_checks)}/{len(izca_checks)} pass")
    print(f"SOT tables: {sot_info}")
    print(f"Wrote results to: {args.out_dir.resolve()}")

if __name__ == "__main__":
    main()
