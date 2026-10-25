# PS2 Asset Inspector

A small, dependency-free, read-only C++17 tool for evidence-driven inspection of PS2/game asset binaries. Version 0.10.0 adds `--vif-layout-csv`, which groups accepted VIF UNPACK packets only when opcode sequence, element count, consecutive VU addresses, invariant control bits, and packet boundaries agree. The observed sample partitions all accepted packet headers into recurring groups, but the tool does not infer mesh topology or attribute semantics from those groups. Version 0.8.0 added `--vif-v3-csv` and quantitative diagnostics for candidate KFMD transform-like records. Version 0.7.0 added `--kfmd-tail-csv` for the single observed `PLAYER00.MLD` KFMD tail pattern: a consecutive ID list, a 16-byte-per-entry reference table, and bounded `0x40`-byte records. It emits those records only when header arithmetic, table IDs, expected relative offsets, fixed stride, and exact payload-end closure all validate. It reports raw words plus non-semantic lane statistics; it does not call these records bones, matrices, vertex indices, or triangle data. Earlier features cover raw top-level KFMD headers, VIF packet/group evidence, and correlations between `MLE0`, `METL`, `HMDL`, `METF`, and `HTSF` chunks.

This is an inspection foundation, not a complete model/animation/texture extractor. Analysis bundle v0.16 added a deliberately conservative point-cloud export for validated top-level VIF attribute groups. Analysis bundle v0.17 adds separate, explicitly speculative triangle-strip hypotheses and an offline 3D viewer, but does not claim to decode the original mesh connectivity, materials, UVs, skeleton, or animation. The C++ `--export-model-experiment` command remains the conservative point-cloud exporter; the Python wrapper builds the separate hypotheses for visual experiments. The indexed-directory parser only accepts the variant observed in `PLAYER00.MLD`; other files with an `IZCA` signature are not assumed to share that structure.

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

## Sample interpretation

See [`docs/VIF_PACKET_GROUP_ANALYSIS.md`](docs/VIF_PACKET_GROUP_ANALYSIS.md) for the `v0.10.0` packet grouping rules and sample counts. See [`docs/INITIAL_BINARY_FINDINGS.md`](docs/INITIAL_BINARY_FINDINGS.md) for the initial inventory and [`docs/DEEP_MLD_FINDINGS.md`](docs/DEEP_MLD_FINDINGS.md) for the extended child-directory, HMDL/KFMD, and VIF packet analysis. The deep CLI output is in [`docs/deep_cli_output.txt`](docs/deep_cli_output.txt). CSV maps: [`docs/hierarchy_vif_correlations.csv`](docs/hierarchy_vif_correlations.csv) (hierarchy-wide HMDL/VIF/tail-ID comparison), [`docs/vif_packet_groups.csv`](docs/vif_packet_groups.csv) (validated recurring packet groups across top-level and embedded HMDLs), [`docs/vif_chains.csv`](docs/vif_chains.csv) (147 top-level chains), [`docs/vif_attribute_groups.csv`](docs/vif_attribute_groups.csv) (510 matched groups), [`docs/vif_v3_q23_candidates.csv`](docs/vif_v3_q23_candidates.csv) (131 V3 triples), [`docs/kfmd_header_words.csv`](docs/kfmd_header_words.csv) (20 raw words per top-level KFMD), and [`docs/kfmd_tail_records.csv`](docs/kfmd_tail_records.csv) (22 candidate records). KFMD header and tail analysis is documented in [`docs/KFMD_HEADER_FINDINGS.md`](docs/KFMD_HEADER_FINDINGS.md) and [`docs/KFMD_TAIL_FINDINGS.md`](docs/KFMD_TAIL_FINDINGS.md). VIF decoding, numeric distributions, Q23 correlation, flag correlations, and limitations are documented in [`docs/VIF_GROUP_FINDINGS.md`](docs/VIF_GROUP_FINDINGS.md) and [`docs/VIF_V3_Q23_FINDINGS.md`](docs/VIF_V3_Q23_FINDINGS.md). The tail records' transform-like measurements are detailed in [`docs/KFMD_TRANSFORM_CANDIDATE_FINDINGS.md`](docs/KFMD_TRANSFORM_CANDIDATE_FINDINGS.md). The cross-hierarchy result, exact ID-match counts, and unsupported nested VIF patterns are summarized in [`docs/HIERARCHY_VIF_FINDINGS.md`](docs/HIERARCHY_VIF_FINDINGS.md).

## VIF packet grouping evidence (v0.10.0)

The packet-group exporter is a structural diagnostic, not a mesh decoder. On the supplied `PLAYER00.MLD`, the accepted 359 VIF chains contain 4,117 parsed UNPACK packets. A greedy longest-pattern scan partitions all packets into recognized groups: top-level `[6C,6C,68,64]` x35, `[6C,6C,64]` x475, `[6C,64]` x8; embedded `[6C,6C,6C]` x256, `[6C,64]` x802, `[6C,6C]` x82. Each group has matched element counts, sequential low-10-bit VU addresses, equal immediate control bits outside the address field, and contiguous packet boundaries. These exact counts are sample-specific observations and are not assumed to describe every IZCA variant. See `docs/VIF_PACKET_GROUP_ANALYSIS.md` and `docs/vif_packet_groups.csv`.


## VIF Payload Profile (analysis bundle v0.11)

See `docs/PS2_VIF_Payload_Profile_Analysis_v0.11.md` for measured payload distributions from accepted VIF packets. The analysis deliberately keeps V4 lane 3 as raw bits and does not label V2 payloads as UVs or V3 Q23 values as skinning weights without independent evidence. CSV exports are included in `docs/`.


## GIF/GS path audit (analysis companion v0.12)

The next-stage audit is reproducible with `tools/audit_gif_candidates.py`. It uses the core CLI's validated HMDL/KFMD payload ranges and accepted VIF packet map, then checks 16-byte-aligned GIF-tag candidates whose computed data footprint fits inside the same KFMD payload. It marks candidates that overlap accepted VIF packet bytes and follows adjacent candidate tags to test whether they terminate at EOP. Every row remains explicitly unconfirmed: ordinary metadata tables can imitate GIF bit fields.

For the supplied `PLAYER00.MLD`, the audit inspected 56 bounded payload spans (9 top-level, 47 embedded; 560,672 bytes) and found 1,456 length-bounded candidates. Only four candidate starting offsets followed two adjacent tags through EOP; none reached three tags. These results do **not** establish a reliable stored GIF/GS stream. See [`docs/GIF_GS_TRACE_AUDIT_v0.12.md`](docs/GIF_GS_TRACE_AUDIT_v0.12.md) and [`docs/gif_candidate_audit_v0.12.csv`](docs/gif_candidate_audit_v0.12.csv).

Run it after building the CLI:

```sh
python3 tools/audit_gif_candidates.py --probe build/ps2probe --input /path/to/PLAYER00.MLD \
  --csv docs/gif_candidate_audit_v0.12.csv --report docs/GIF_GS_TRACE_AUDIT_v0.12.md
```

The script requires Python 3.10+ and does not modify input binaries. The next meaningful trace point is the game executable or a runtime capture of VIF/VU1/GIF activity, which was not included in the four-file sample archive.

## Executable render-path analysis (analysis bundle v0.13)

This is an **analysis-bundle version 0.13**; the C++ CLI core remains at CMake version `0.11.0` because its compiled core features were not changed in this step. A dependency-free ELF32 little-endian analyzer is included at `tools/analyze_ps2_elf.py`. It reads the ELF header, program segments, section table, and symbol table; maps symbol virtual addresses back to file offsets where the bytes are file-backed; and exports graphics/render-related named symbols to CSV. It is a **static symbol map**, not an emulator, decompiler, runtime tracer, or asset-to-draw call graph.

```sh
python3 tools/analyze_ps2_elf.py --input /path/to/SLUS_204.59 \
  --csv docs/slus_render_symbols_v0.13.csv \
  --sections-csv docs/slus_elf_sections_v0.13.csv \
  --callsites-csv docs/slus_render_callsites_v0.13.csv \
  --report docs/SLUS_204_59_RENDER_PATH_ANALYSIS_v0.13.md
```

The supplied executable was ELF32 little-endian with entry point `0x00180008`, ELF flags `0x20924000`, 24,379 parsed symbol entries, and 11,909 named function entries. Its symbols include many `njCnk` rendering functions, GIF tag writers, `sceGifPkOpenGifTag`/`sceGifPkCloseGifTag`, VU0 routines, `MakeTagVu1`/`MakeTailVu1`, and repeated `SetVtxNumtoDMATag` definitions. The direct-call map shows `ConvertPlist_Texture` and `ConvertPlist_Polygon` invoking `MakeTagVu1`/`MakeTailVu1`; the `MakeTagVu1` code also constructs a word with opcode byte `0x6C`, consistent with the VIF UNPACK V4-32 family found in the model scan. This supports a static code/data-format connection, but it does **not** by itself prove which `PLAYER00.MLD` chain reaches which runtime function.

See [`docs/SLUS_204_59_RENDER_PATH_ANALYSIS_v0.13.md`](docs/SLUS_204_59_RENDER_PATH_ANALYSIS_v0.13.md), [`docs/slus_render_symbols_v0.13.csv`](docs/slus_render_symbols_v0.13.csv), [`docs/slus_elf_sections_v0.13.csv`](docs/slus_elf_sections_v0.13.csv), [`docs/slus_render_callsites_v0.13.csv`](docs/slus_render_callsites_v0.13.csv), and the selected static code excerpts in [`docs/slus_render_disassembly_v0.13.txt`](docs/slus_render_disassembly_v0.13.txt). The executable is an input sample and is not bundled in this source archive; pass its path as `--input`.

Optional smoke test for the ELF analyzer (pass the same executable path):

```sh
python3 tests/test_elf_analyzer.py --input /path/to/SLUS_204.59
```

## Analysis bundle v0.14 — vertex dispatch and VIF builder trace

This step adds `tools/trace_render_builders.py`, a dependency-free static audit for the supplied `SLUS_204.59` ELF. It follows the format-indexed `SetVertex_*` tables, pairs full converter callbacks with their output-stride table, inventories `jalr` instructions inside named function ranges, and checks how `MakeTagVu1` reads the adjacent `0x004610D0` table.

The v0.14 report and generated artifacts are checked into `docs/`:

- `SLUS_204_59_RENDER_BUILDER_TRACE_v0.14.md`
- `slus_render_tables_v0.14.csv`
- `slus_render_jalr_v0.14.csv`
- `slus_dispatch_route_v0.14.csv`

Run the new audit against the ELF you supplied (the executable is not bundled in this source ZIP):

```sh
python3 tools/trace_render_builders.py \
  --input /path/to/SLUS_204.59 \
  --out-dir docs
```

The script validates the four table payloads and selected instruction words before generating output. It uses Python's standard library and the existing `tools/analyze_ps2_elf.py` parser. It does not emulate the game, execute or patch the ELF, or claim a runtime trace from model bytes to final GS output. **Bundle version is 0.14; the compiled C++ CLI core remains version 0.11.0**, because this addition extends the analysis tools/docs rather than changing the C++ CLI.


## Analysis bundle v0.15 — PCSX2 runtime trace preparation

This bundle prepares the statically verified v0.14 renderer route for an actual PCSX2 debugger session. The new `tools/export_pcsx2_symbols.py` reads the supplied `SLUS_204.59` ELF (without modifying it), exports a focused PCSX2 text `.sym` file, a CSV manifest, and a 17-entry breakpoint plan. A blank CSV worksheet is included to record a repeatable runtime capture.

Outputs stored in `docs/`:

- `PCSX2_RUNTIME_TRACE_GUIDE_v0.15.md`
- `pcsx2_render_trace_symbols_v0.15.sym`
- `pcsx2_render_trace_symbols_v0.15.csv`
- `pcsx2_breakpoint_plan_v0.15.csv`
- `pcsx2_capture_template_v0.15.csv`

Run the exporter with the exact executable version being tested:

```sh
python3 tools/export_pcsx2_symbols.py --input /path/to/SLUS_204.59 --out-dir docs
python3 tests/test_pcsx2_export.py --input /path/to/SLUS_204.59
```

The smoke test confirms the exported key symbols, the breakpoint at `0x00207EF4`, the table label at `0x00461070`, and clean failure on invalid input. The provided workspace did not contain a runnable PCSX2 executable or a bootable disc/BIOS setup, so this is **runtime-trace preparation**, not a claim that live VIF/GIF execution has been captured. Use a matching game build, capture the EE-side function/buffer state, and separately capture a GS dump where useful. See the guide for current PCSX2 UI paths and the limits of each capture type.

The compiled C++ CLI core remains at CMake version `0.11.0`; v0.15 adds analysis tooling and capture documentation only.


See [`docs/asset_bundle_inventory_v0.16.csv`](docs/asset_bundle_inventory_v0.16.csv) for a source-derived classification of the four supplied files. See [`docs/SE_STAGE_01_MLT_Structure_v0.16.md`](docs/SE_STAGE_01_MLT_Structure_v0.16.md) for the alternate stage-container layout and raw ISBD payload extraction. The sample-generated point cloud and viewer are included under [`docs/model_experiment_demo/`](docs/model_experiment_demo/).


## Analysis bundle v0.17 — experimental model viewing and topology comparisons

The v0.17 analysis tools build on the conservative point-cloud export from `PLAYER00.MLD`. The offline HTML viewer opens model index 0 first and can toggle the eight observed indexes, color points by group/raw fourth-lane masks, show candidate normal vectors, and inspect raw values plus source offsets. It uses no external JS libraries or network requests. See [`docs/MODEL_TOPOLOGY_EXPERIMENT_v0.17.md`](docs/MODEL_TOPOLOGY_EXPERIMENT_v0.17.md) for the observed per-index counts and explicit limitations.

Run the complete experiment after building `ps2probe`:

```sh
python3 tools/export_model_experiment.py \
  --probe build/ps2probe \
  --input /path/to/PLAYER00.MLD \
  --out-dir /path/to/model_experiment_output
```

The wrapper writes:

- A conservative point-cloud OBJ/PLY and source-offset CSVs; these do not fabricate faces.
- `experimental_model_viewer.html`, an offline interactive point/mesh-hypothesis viewer.
- An audit of bit correlations in the raw fourth lane of the first `0x6C` packet. The CSV name `normal_w_raw` reflects the initial candidate-vector interpretation only; its semantics are not confirmed.
- `experimental_model_candidate_triangle_strip.obj/.ply`: an all-group triangle-strip hypothesis.
- `experimental_model_candidate_startflag_strip.obj/.ply`: a comparison variant limited to groups whose first raw fourth lane has bit `0x4000` set. This bit is **not** proven to be a start flag.
- `model_index_exports/model_index_XX/`: per-index point-cloud exports and both candidate mesh variants, plus a manifest. Index values are observed structural indexes, not proof of eight distinct characters/models.

For the supplied sample, the exporter sees **2,304 points in 510 groups**, spread across eight model indexes with points. It writes **1,331** all-group strip candidate faces and **1,174** filtered candidate faces. Model index 0 has 1,150 points; the two triangle variants have 663 and 556 faces respectively. On visual inspection, the all-group variant has a long-edge artifact, while the filtered variant remains fragmented and is not a complete reconstructed model. Treat the mesh outputs as experiments only; keep the point cloud as the evidence baseline.

The `SE_STAGE_01.MLT` input has now been reclassified as a high-confidence SCEI-style PS-ADPCM sound-bank candidate after its `Vagi` table and 16-byte frame headers were validated. `FONT.BDA` remains a font/resource directory and `0010.SFD` a video-stream candidate; those are not treated as model inputs by the model exporter. See [`docs/asset_bundle_inventory_v0.19.csv`](docs/asset_bundle_inventory_v0.19.csv). The v0.17 bundle extends tools/docs/viewer only; the compiled C++ CLI version is unchanged.


## Analysis bundle v0.18 — stage-audio bank and raw-lane correction

This bundle adds `tools/analyze_stage_audio.py`, which parses the observed `Vagi` IECS chunk inside `SE_STAGE_01.MLT`, validates its 8-byte sample records, verifies the aligned PS-ADPCM frame headers, and exports every indexed sample as headerless `.adpcm` plus mono PCM16 `.wav`. It also emits a local HTML audio catalogue. The decoder is a static format experiment; no emulator playback or subjective listening test was performed.

Run it with the supplied stage file:

```sh
python3 tools/analyze_stage_audio.py --input /path/to/SE_STAGE_01.MLT --out-dir /path/to/stage_audio_export
python3 tests/test_stage_audio.py --tool tools/analyze_stage_audio.py --input /path/to/SE_STAGE_01.MLT
```

**Historical v0.18 result, superseded by v0.19:** the initial reader interpreted the `Vagi` header word `0x0C` as a count, emitted 12 entries, and misclassified the 13th entry as a tail candidate. A deeper offset-table check showed that `0x0C` is consistent with the highest inclusive index in this sample, so the table contains 13 entries with indexes `0..12`. Do not use the initial v0.18 count or its extra-tail label; v0.19 regenerates the audio, CSV, viewer, and tests from the corrected read. The meanings of all `Smpl`, `Sset`, and `Prog` structures remain not fully decoded.

The corrected v0.19 audio test checks all 13 source offsets, frame counts, WAV properties, exact end markers, adjacent stream boundaries, and malformed-input rejection. Indexed raw outputs preserve the original ADPCM frame bytes through each sample's end frame. The exported WAVs use the sample rate stored in each Vagi record.

The raw vertex fourth-lane audit also tightens the model-topology wording: bit `0x8000` occurs at the final element of all 510 groups and at no interior position (strong group-end correlation). Bit `0x4000` is not a unique start marker, because it also occurs inside groups and is absent from 107 group-first elements. The legacy filename `experimental_model_candidate_startflag_strip.*` is retained for compatibility, but that mesh is now described in the UI and reports as a **0x4000-mask-filtered diagnostic hypothesis**. Neither mesh hypothesis is established as the game's original topology. See [`docs/MODEL_TOPOLOGY_EXPERIMENT_v0.18.md`](docs/MODEL_TOPOLOGY_EXPERIMENT_v0.18.md) and [`docs/stage_audio_experiment/SE_STAGE_01_Audio_Analysis_v0.19.md`](docs/stage_audio_experiment/SE_STAGE_01_Audio_Analysis_v0.19.md).


## Analysis bundle v0.19 — corrected Vagi index, raw IECS tables, and SFD media extraction

This bundle corrects the initial v0.18 interpretation of `SE_STAGE_01.MLT`: the word at `Vagi +0x0C` is consistent with an inclusive last index (`0x0C` -> indexes 0 through 12), not a record count. The 13 valid offset pointers lead to 13 eight-byte records. `Vagi[12]` points to ISBD offset `0x7FA0`, has a source-stored rate of 16,000 Hz, and spans 424 PS-ADPCM frames ending at the final marker. All 13 entries are now exported as indexed samples; the earlier extra-tail label was incorrect and was removed from the current export.

Use the audio extractor:

```sh
python3 tools/analyze_stage_audio.py --input /path/to/SE_STAGE_01.MLT --out-dir /path/to/stage_audio_export
python3 tests/test_stage_audio.py --tool tools/analyze_stage_audio.py --input /path/to/SE_STAGE_01.MLT
```

The additional `tools/export_stage_index_tables.py` exports bounded raw spans for the observed `Vagi`, `Smpl`, `Sset`, and `Prog` offset tables. Its `header_word + 1` slots are an evidence-led layout hypothesis; the tool keeps opaque bytes and sentinel slots without assigning semantic labels or claiming a sample-to-instrument relationship:

```sh
python3 tools/export_stage_index_tables.py --input /path/to/SE_STAGE_01.MLT --out-dir /path/to/stage_table_export
python3 tests/test_stage_index_tables.py --tool tools/export_stage_index_tables.py --input /path/to/SE_STAGE_01.MLT
```

For this sample, 81 slot rows cover four chunks, with 41 present offsets. The raw record slices are preserved under `docs/stage_table_export/iecs_records/`; spans may include padding at the end and therefore are not labeled as semantic records.

The new optional `tools/extract_sfd_media.py` uses external FFmpeg tools to inspect and convert `0010.SFD`. The supplied file probes as an MPEG program stream containing MPEG-1 video at 640×320 and CRI ADX stereo audio at 48,000 Hz, about 88.8 seconds long. The MP4 output is a compatibility transcode to H.264/AAC, while WAV is decoded PCM; neither is a bit-preserving copy of the source.

```sh
python3 tools/extract_sfd_media.py --input /path/to/0010.SFD --out-dir /path/to/sfd_export --preview-time 5
python3 tests/test_sfd_media.py --tool tools/extract_sfd_media.py --input /path/to/0010.SFD
```

`ffmpeg` and `ffprobe` must be installed separately for this optional media tool; the core C++ build does not depend on them. See [`docs/sfd_media_experiment/0010_Media_Analysis_v0.19.md`](docs/sfd_media_experiment/0010_Media_Analysis_v0.19.md). The updated input inventory is [`docs/asset_bundle_inventory_v0.19.csv`](docs/asset_bundle_inventory_v0.19.csv).


## Analysis bundle v0.20 — reversible model-viewer transforms and adjusted export

The model viewer now supports visual repair experiments that do not mutate the source binary or source point CSV. Its new **Repair transforms** section applies a uniform scale, flips any combination of X/Y/Z, and offsets the three axes. **Reset transform** restores the source-space view. The OBJ/PLY buttons export the currently enabled model indexes with those transforms applied.

- With `Point cloud only` selected, exports contain vertices/point records and no generated faces.
- With either `Surface experiment` mesh mode selected, the export contains the corresponding triangle-strip hypothesis. These faces remain unconfirmed and must not be mistaken for recovered original topology.
- Export filenames include a timestamp. Each export identifies itself as experimental and notes that the original MLD was not changed.
- The viewer is self-contained/offline. No JavaScript CDN, external library, or network access is required.

Use the normal workflow:

```sh
python3 tools/export_model_experiment.py \
  --probe build/ps2probe \
  --input /path/to/PLAYER00.MLD \
  --out-dir /path/to/model_export
```

Then open `model_export/experimental_model_viewer.html`. For an orientation/scale check, change one control at a time; use `Point cloud only` to export the conservative baseline, and use a mesh mode only when you explicitly want that topology hypothesis.

Run the new standard-library integration test (it regenerates the viewer and checks the control/export code):

```sh
python3 tests/test_model_repair_viewer.py \
  --probe build/ps2probe \
  --input /path/to/PLAYER00.MLD \
  --viewer-script tools/build_pointcloud_viewer.py
```

For additional optional interactive validation in a local Python environment with Playwright and Chromium installed, use `python3 tests/test_model_repair_viewer_browser.py --html /path/to/experimental_model_viewer.html`. The v0.20 development run additionally checked coordinate math and actual OBJ/PLY browser downloads. See [`docs/MODEL_REPAIR_VIEWER_v0.20.md`](docs/MODEL_REPAIR_VIEWER_v0.20.md) and [`docs/TEST_REPORT_v0.20.md`](docs/TEST_REPORT_v0.20.md).

The analysis bundle also includes regenerated sample exports in `docs/model_experiment_demo/`, the 13 indexed stage-audio clips and local catalogue in `docs/stage_audio_experiment/`, the raw bounded IECS table candidates in `docs/stage_table_export/`, and the SFD probe/thumbnail metadata in `docs/sfd_media_experiment/`. Heavy SFD MP4/WAV transcodes are provided separately in the asset explorer bundle; they are not source-preserving copies. The compiled C++ CLI core remains at its existing semantic version because v0.20 extends tooling/viewer output rather than changing the core CLI API.
