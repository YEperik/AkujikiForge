## Build

Requires CMake 3.16+ and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The self-contained C++ test executable covers 27 cases, including malformed input bounds, validated KFMD tail records, synthetic VIF group patterns, and the experimental-export invalid-input guard.

## Usage

```sh
./build/ps2probe path/to/PLAYER00.MLD
./build/ps2probe --verbose path/to/PLAYER00.MLD
./build/ps2probe --deep path/to/PLAYER00.MLD
./build/ps2probe path/to/0010.SFD path/to/FONT.BDA path/to/SE_STAGE_01.MLT
```

`--deep` is deliberately specific to the observed `PLAYER00.MLD` sample. It does not claim that all Sega `IZCA` variants share these headers. The VIF probe validates packet byte counts for the supported command opcodes, reports 147 non-overlapping bounded chains in the supplied sample, and can export the chain map and each matched attribute-like group to CSV. It reports recurring patterns in the trailing word of the first V4-32 lane as boundary-marker candidates, but does not infer triangle connectivity or export models.

On Windows, the executable may be at `build/Release/ps2probe.exe` depending on the generator.

Default maximum input size is 1 GiB per file to avoid accidental huge allocations. The program does not modify input files.
