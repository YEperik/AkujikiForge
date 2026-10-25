#!/usr/bin/env python3
"""Convert locally supplied media to broadly playable MP4/WAV previews using FFmpeg."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=True, text=True, capture_output=True, encoding="utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="local media/container file")
    parser.add_argument("--out-dir", required=True, type=Path, help="folder for converted previews")
    parser.add_argument("--preview-time", type=float, default=1.0, help="video thumbnail timestamp in seconds")
    args = parser.parse_args()

    if not args.input.is_file():
        print(f"ERROR: input file does not exist: {args.input}", file=sys.stderr)
        return 2
    if args.preview_time < 0:
        print("ERROR: preview time must be non-negative", file=sys.stderr)
        return 2
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        print("ERROR: install FFmpeg (ffmpeg and ffprobe) and ensure both are on PATH", file=sys.stderr)
        return 2

    try:
        probe = run([ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", str(args.input)])
        metadata = json.loads(probe.stdout)
        streams = metadata.get("streams", [])
        video = any(stream.get("codec_type") == "video" for stream in streams)
        audio = any(stream.get("codec_type") == "audio" for stream in streams)
        if not video and not audio:
            raise ValueError("No audio or video stream was recognized by FFmpeg.")

        args.out_dir.mkdir(parents=True, exist_ok=True)
        outputs: list[Path] = []
        stem = args.input.stem
        if video:
            mp4 = args.out_dir / f"{stem}_converted.mp4"
            run([ffmpeg, "-hide_banner", "-y", "-v", "error", "-i", str(args.input),
                 "-map", "0:v:0", "-map", "0:a:0?", "-c:v", "libx264", "-preset", "veryfast",
                 "-crf", "21", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
                 "-movflags", "+faststart", str(mp4)])
            outputs.append(mp4)
            thumb = args.out_dir / f"{stem}_preview.jpg"
            duration = float(metadata.get("format", {}).get("duration") or 0.0)
            timestamp = max(0.0, min(args.preview_time, max(0.0, duration - 0.05))) if duration else args.preview_time
            run([ffmpeg, "-hide_banner", "-y", "-v", "error", "-ss", str(timestamp), "-i", str(mp4),
                 "-frames:v", "1", "-q:v", "2", str(thumb)])
            outputs.append(thumb)
        if audio:
            wav = args.out_dir / f"{stem}_audio.wav"
            run([ffmpeg, "-hide_banner", "-y", "-v", "error", "-i", str(args.input),
                 "-map", "0:a:0", "-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2", str(wav)])
            outputs.append(wav)

        print("Conversion complete. Original input was not modified.")
        for output in outputs:
            print(f"Wrote: {output} ({output.stat().st_size:,} bytes)")
        print("Review usage rights before redistributing converted content.")
        return 0
    except (OSError, subprocess.CalledProcessError, ValueError, json.JSONDecodeError) as exc:
        message = getattr(exc, "stderr", None) or str(exc)
        print(f"ERROR: {message.strip()}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
