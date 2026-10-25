# AkuijikiForge

**A local-first asset inspection workbench with a native desktop interface.**

AkuijikiForge is an independent, community-built toolkit for examining files, reviewing text-based 3D geometry, and playing local media during asset-research and preservation workflows. The project is not developed by, endorsed by, or affiliated with any game developer or publisher.

> **Public-release scope:** This is a deliberately sanitized public baseline, not a feature-for-feature copy of the local research workspace. It contains no extracted game assets, game dumps, private reverse-engineering notes, specialized format-research reports, or research-result exports. Bring your own locally stored files, and publish only material you have permission to redistribute. Keep any parser or research method that must remain confidential outside this public source tree.

## Highlights

- **Native desktop UI** built with PySide6 / Qt Widgets—no HTML UI, browser shell, or GitHub Pages dependency.
- **Read-only file inspection** with file size, Shannon entropy, a bounded hexadecimal header preview, and recognition of common file signatures.
- **Lightweight 3D preview** for text-based Wavefront OBJ and ASCII PLY geometry, with mouse rotation and wheel zoom.
- **Local media playback** for audio and video formats supported by the installed Qt / operating-system codecs.
- **Optional media conversion** through FFmpeg/ffprobe to create local MP4/WAV previews and a video thumbnail.
- **C++17 command-line tool** for scripted file inventory and quick diagnostics.
- **Public-release audit** to flag common game-derived dumps, private research directories, generated model/media files, HTML UI files, and oversized files before publishing.
- **Continuous integration** with CMake builds, C++ unit tests, Python UI-data tests, and the release-content audit.

## Interface

The desktop app has four workspaces: **Overview**, **File Inspector**, **3D Preview**, and **Media Player**. File paths are selected locally. The tool does not automatically upload selected files.

The 3D preview currently accepts text-based `.obj` and **ASCII** `.ply` files. Binary PLY, textures, materials, rigs, and animation are outside the built-in preview scope. Media playback depends on the codecs available on the target system.

## Build the C++ CLI

Requirements: CMake 3.16+ and a C++17 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The executable is `build/akuijikiforge` on most Linux/macOS generators and is commonly `build/Release/akuijikiforge.exe` on Windows with Visual Studio.

Example:

```bash
./build/akuijikiforge --help
./build/akuijikiforge path/to/your-file.bin
./build/akuijikiforge --verbose path/to/your-file.bin
```

The CLI reports basic metadata and common signatures; it does not patch source files. Entropy is a statistical indicator, not proof that a file is encrypted or compressed.

## Run the desktop UI

Requirements: Python 3.10+ and a working Qt Multimedia backend.

Create a virtual environment and install the UI dependency:

```bash
python -m venv .venv
# Windows PowerShell:
.\.venv\Scripts\Activate.ps1
# macOS / Linux:
# source .venv/bin/activate
python -m pip install -r requirements.txt
python ui/akuijikiforge_app.py
```

Build the C++ CLI first if you want to use the File Inspector page. If the app does not find it automatically, choose the compiled executable from **Choose CLI binary…**. The model preview and media player can be used independently of the CLI.

## Repository layout

```text
AkuijikiForge/
├── .github/workflows/ci.yml    # build, test, and release-content audit
├── docs/                       # public usage and release policy
├── include/akuijikiforge/      # C++ public headers
├── src/                        # C++17 CLI and core library
├── tests/                      # C++ and Python tests
├── tools/                      # release audit utilities
└── ui/                         # native PySide6 desktop application
```

## License and third-party content

The original source code and documentation in this repository are released under the **MIT License**; see [`LICENSE`](LICENSE). This does **not** grant a license to third-party game data, copyrighted audio/video, extracted models, trademarks, or other material that is not included here. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the project boundary and [`docs/PUBLIC_RELEASE_POLICY.md`](docs/PUBLIC_RELEASE_POLICY.md) for asset-handling guidance.

AkuijikiForge is an independent community project. Product names and trademarks remain the property of their respective owners. This project makes no claim of official status.

## Contributing

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request. Run the tests and public-release audit before proposing a change:

```bash
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests -p 'test_*.py' -v
python tools/audit_public_release.py
```

## Security and responsible use

The public edition is intended for local, read-only inspection and interoperability work. It is not a bypass, DRM-removal, online-cheat, or anti-cheat evasion tool. Do not commit proprietary inputs, private research, personal data, credentials, or files you do not have permission to share. See [`SECURITY.md`](SECURITY.md).
