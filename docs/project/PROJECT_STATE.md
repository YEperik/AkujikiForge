## A. Project identity

- **Project goal (from the handoff instructions):** continue the same PS2 Game Modding Tool focused on reverse engineering proprietary PlayStation 2 game formats, binary analysis, asset extraction/reconstruction, and professional C++/CMake engineering.
- **Likely target represented by these samples:** *Shinobi* (PS2, 2002) is a high-confidence inference, not a verified disc identity. The sample `EV00_00_00E.SOT` contains a line addressing “Hotsuma”; the supplied files include `PLAYER00.MLD`; public reverse-engineering discussion describes the same game and filename. No ISO, `SYSTEM.CNF`, product ID, or repository was provided to establish region/revision conclusively.
- **Actual production language/toolchain:** C++ and CMake are specified by the project continuation instructions, but the current repository and its configured compiler standard cannot be verified from this upload.
- **Current workspace contents:** one ZIP of 13 asset samples (37,191,654 bytes total), plus the continuation-instructions text. No source tree, `CMakeLists.txt`, tests, Git metadata, build logs, or previous format research reports were supplied.

## B. Subsystem status

| Subsystem / work area | Status | What is supported by current evidence | Gap / risk |
|---|---|---|---|
| Sample intake and inventory | `VERIFIED` | ZIP CRC check passes; 13 files catalogued with SHA-256 hashes and leading bytes in `results/sample_manifest.csv`. | These samples are not a complete game image or repository. |
| Media identification | `VERIFIED` | `ffprobe` identifies `0010.SFD` as MPEG-1 video plus ADX audio, and all three `.ADX` samples as stereo 48 kHz ADX. | Playback quality/content was not manually reviewed. |
| Outer `IZCA` size invariant | `VERIFIED` | Four top-level `IZCA` samples satisfy `file_size = 16 + u32le(0x04) + u32le(0x08)`. | This is a size invariant, not a full block-table or decompression specification. |
| `.SOT` table read | `VERIFIED` for supplied sample | `EV00_00_00E.SOT` has 11 records; all 11 offsets point to in-bounds NUL-terminated strings. Extracted output is in `results/sot_strings.tsv`. | Record field at `+0` is not semantically identified; only this sample was tested. |
| `.MLD`/`.MLX` internals | `RESEARCHED` | Tag inventory includes `IZCA`, `HMDL`, `KFMD`, `MLE0`, `GBIX`, `PVRT`, `HTSF`, and others; exact byte-match counts are recorded in the manifest. | Nested `IZCA` records are not extracted; mesh/texture/animation fields are not reconstructed. |
| `.ESP` internals | `RESEARCHED` | `ESPC02`, `ESPM`, a 31-entry offset-like table, and terminal `EOFC` are observed. | Meanings of entries and subrecords remain unverified. |
| `.EVD` / `.EST` / `.BDA` internals | `RESEARCHED` | Section-like tags and embedded labels are observed; offsets are recorded in `FORMAT_SPEC.md`. | Full schemas, reference semantics, alignment, and encodings remain unknown. |
| Production C++/CMake implementation | `BLOCKED` | No production source files or build configuration are present in the provided materials. | Do not invent or replace modules. The real repository is needed for safe integration. |
| Existing automated regression suite | `BLOCKED` | No test source, fixtures layout, or prior test results were supplied. | Do not mark existing parser behavior as verified. |

## C. Binary-format findings

See [`FORMAT_SPEC.md`](FORMAT_SPEC.md) for evidence, offsets, and confidence boundaries. In short:

1. Four outer `IZCA` assets satisfy the same total-size equation, including the `.MLT` whose field at `0x08` is `0x50` rather than `0x10`.
2. `PLAYER00.MLD` has 149 exact `IZCA` byte-string occurrences and 56 `HMDL` occurrences; `MA00_10_00.MLX` has 14 `IZCA` occurrences.
3. The `.SOT` string table is directly readable without decompression using a count at `0x0C` and 8-byte records from `0x20` in this specific sample.
4. The remainder of the proprietary formats has not been fully decoded.

## D. Architecture decisions

No current module graph, public API, buffer ownership model, error type, CMake target, or architectural decision can be recovered from the supplied files. `ARCHITECTURE.md` and `DECISIONS.md` deliberately record that absence rather than making up a design.

## E. Next steps (ordered)

1. **Unblock repository integration:** obtain the existing repository/source tree or a project ZIP containing its current code, docs, tests, and build configuration. Do not restart or replace it.
2. Preserve the supplied assets as immutable test fixtures; use `sample_manifest.csv` hashes to check fixture identity.
3. In the existing codebase, add/extend tests for the outer `IZCA` size invariant against the four passing fixtures.
4. Implement or extend `.SOT` parsing in the existing module that owns binary/container parsing; test all 11 records, bounds checks, and NUL terminators.
5. Continue with `.ESP` and `.EVD` section/table schemas only after comparing offset relationships across more than one sample.
6. For models, map container/table references before decoding `HMDL`/`KFMD` mesh records; treat public forum patterns as leads rather than project-confirmed facts.
7. Update the handoff after repository integration and real build/test execution.

## F. Current continuation point

**Last completed and reproducible:** sample archive integrity check, file inventory/hash manifest, outer-`IZCA` size-invariant checks, `.SOT` string-table extraction, and optional media probing.  
**Not completed:** no production source changed; no C++ build or project test could be run because no production repository was supplied.  
**Safe next action:** integrate these fixtures and tests into the existing repository once it is available; do not create a replacement project from this research package.
