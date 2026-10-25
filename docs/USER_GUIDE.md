# User Guide

## 1. Build the command-line inspector

Install CMake 3.16 or newer and a C++17 compiler, then run from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Run `build/akuijikiforge --help` for the supported options. The CLI currently provides generic metadata and file-signature checks; it does not claim to decode every game-specific format.

## 2. Install and launch the desktop application

Install Python 3.10+ and the packages listed in `requirements.txt`:

```bash
python -m venv .venv
python -m pip install -r requirements.txt
python ui/akuijikiforge_app.py
```

Activate `.venv` before the install/launch commands where your shell requires it. On Linux, the desktop environment may also need Qt Multimedia system libraries. On all operating systems, audio/video playback depends on the codecs supported by the installed Qt and OS media backends.

## 3. Inspect a file

Open **File Inspector**, choose one or more files, and run the inspection. Use the longer header option for an extended hexadecimal preview. If the desktop app cannot locate the compiled CLI, use **Choose CLI binary…** to select it.

The reported entropy is a statistical measure. A high entropy value alone does not establish encryption, compression, or file validity. Signature detection checks only a small list of common file headers and is not a comprehensive file-format database.

## 4. Preview geometry

Open **3D Preview** and select a text-based OBJ or ASCII PLY file. Drag to rotate and use the mouse wheel to zoom. The viewer is intentionally lightweight. It reads coordinates and face indexes only; it does not load material libraries, textures, skinning, or animation. Binary PLY files must be converted to ASCII before use.

## 5. Play media

Open **Media Player** and select a local audio or video file. Play, pause, stop, and seek are available. If no sound or picture appears, check the file itself and confirm that the system's Qt Multimedia backend supports its codec. The app does not upload the selected media.

## 6. Convert a local media container

From the Media Player page, select **Convert / extract preview…**, choose a local source file, and choose an output folder. The tool invokes FFmpeg and ffprobe without a shell, leaves the source untouched, and writes an MP4 compatibility preview for video, a WAV decode for audio, and a JPEG thumbnail when video is present. Install FFmpeg with both `ffmpeg` and `ffprobe` available on `PATH`. A successful conversion does not imply permission to redistribute the result.

## 7. Keep generated assets out of Git

Use local folders for source assets and generated exports. Before committing, run:

```bash
python tools/audit_public_release.py
```

Then review Git's staged and unstaged changes manually. The ignore rules reduce accidental staging but do not replace a release review.
