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
./build/ps2probe --vif-csv path/to/PLAYER00.MLD docs/vif_chains.csv
./build/ps2probe --vif-groups-csv path/to/PLAYER00.MLD docs/vif_attribute_groups.csv
./build/ps2probe --kfmd-header-csv path/to/PLAYER00.MLD docs/kfmd_header_words.csv
./build/ps2probe --kfmd-tail-csv path/to/PLAYER00.MLD docs/kfmd_tail_records.csv
./build/ps2probe --vif-v3-csv path/to/PLAYER00.MLD docs/vif_v3_q23_candidates.csv
./build/ps2probe --hierarchy-vif-csv path/to/PLAYER00.MLD docs/hierarchy_vif_correlations.csv
./build/ps2probe --vif-layout-csv path/to/PLAYER00.MLD docs/vif_packet_groups.csv
./build/ps2probe path/to/0010.SFD path/to/FONT.BDA path/to/SE_STAGE_01.MLT
```

`--deep` is deliberately specific to the observed `PLAYER00.MLD` sample. It does not claim that all Sega `IZCA` variants share these headers. The VIF probe validates packet byte counts for the supported command opcodes, reports 147 non-overlapping bounded chains in the supplied sample, and can export the chain map and each matched attribute-like group to CSV. It reports recurring patterns in the trailing word of the first V4-32 lane as boundary-marker candidates, but does not infer triangle connectivity or export models.

On Windows, the executable may be at `build/Release/ps2probe.exe` depending on the generator.

Default maximum input size is 1 GiB per file to avoid accidental huge allocations. The program does not modify input files.

## Confirmed scope and limitations

- Signature scanning is byte-exact and reports absolute file offsets.
- The `IZCA` directory candidate uses little-endian 32-bit fields at `0x04`, `0x08`, `0x10`, then `(table_offset, count)` records beginning at `0x18`.
- The `FONT.BDA` candidate expects an entry count at `0x00`, the `B Data` marker at `0x10`, and 0x20-byte entries starting at `0x20`, with offset/size values at `+0x10` and `+0x1C`. The fields at `+0x14` and `+0x18` are reported without assigning them meanings.
- The directory parser can inspect a container at a nonzero base offset, resolving indexed targets relative to that container; the report records the actual evidence for this behavior.
- Every directory range and every 4-byte target tag is checked before use.
- Semantic labels for unknown fields are intentionally omitted.
- `--deep` validates 100 shifted-header child directories and 47 standard-header child directories in the supplied sample, along with their indexed chunk ranges.
- The `HMDL`/`KFMD` check is structural. `--vif-v3-csv` measures a Q23-normalized triple correlation that is weight-like, but bone indices, skinning semantics, triangle topology, animation semantics, and textures are not decoded. `--hierarchy-vif-csv` expands the scan to the 9 indexed top-level and 47 embedded `HMDL` blocks observed in this sample; it does not assume other `IZCA` variants share this hierarchy.
- `--kfmd-header-csv` writes the raw 20 little-endian 32-bit words at relative payload offsets `0x00` through `0x4C` for each bounded top-level `KFMD`; it intentionally assigns no field meanings.
- `--vif-v3-csv` exports each matched V3-32 triple from `[6C,6C,68,64]` groups, including raw words and a tested `raw / 2^23` normalized-sum candidate. A near-one sum supports a weight-like interpretation but does not prove skinning or bone mapping.
- `--hierarchy-vif-csv` writes one row per accepted bounded VIF chain, plus one row for each observed top-level or embedded HMDL that has no accepted chain. Its tail-ID match columns compare only direct and bit-masked numeric values against a structurally validated ID list; they do not assign those values an index meaning.
- `--vif-layout-csv` provides a packet-level grouping pass over accepted chains: observed groups are `[6C,6C,68,64]`, `[6C,6C,64]`, `[6C,6C,6C]`, `[6C,64]`, and `[6C,6C]`. Each recognized group also requires equal counts, low-10-bit VU addresses incrementing by one, equal control bits above the address field, and contiguous packet data. Any packet that fails every group rule is emitted as an unmatched singleton rather than silently skipped.
- `--kfmd-tail-csv` also reports pairwise dot products, a candidate basis determinant, and tolerance flags for the 22 record vectors. These metrics test a transform hypothesis without assigning a semantic label.
- `--kfmd-tail-csv` recognizes only the observed tail pattern where `payload_size - word(+0x1C) == word(+0x08)`, word `+0x18 == word(+0x1C) + 4`, the initial IDs are consecutive, reference entries match those IDs, each target advances by `0x40`, and the final record ends exactly at the KFMD payload boundary. Unrecognized variants produce a header-only CSV rather than guessed records. `lane0`–`lane3` are storage-lane labels, not semantic field names.
- The VIF scan currently decodes sequence lengths for UNPACK `V4-32` (`0x6C`), `V3-32` (`0x68`), and `V2-32` (`0x64`) only; other observed or possible UNPACK variants remain outside this pass. It decodes the 10-bit VU address separately from the `USN` and `FLG` bits in the VIF immediate. Numeric lane roles are reported as hypotheses, not a confirmed normals/positions/UV layout. The material-link scan validates `METF` as one count plus three 32-bit fields per record, and reports rather than assigns meanings to the first two fields.
- It does not modify game files. The C++ v0.16/v0.17 point-cloud command writes candidate points and raw source offsets without inferring faces. The v0.17 Python companion can emit alternate face hypotheses, but these are not decoded original connectivity and do not establish material assignments, UVs, skeletons, or animation. Its `METL`/`METF`/`HTSF` positional correlation is evidence-supported by count and ordinal agreement but does not prove every field semantic.

