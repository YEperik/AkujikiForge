# Engineering Handoff

**Handoff snapshot:** 2026-10-21  
**Primary language:** Bahasa Indonesia for explanations; English identifiers and format tags preserved.

## Project context

Continue the existing PS2 Game Modding Tool. The provided continuation instructions explicitly prohibit restarting the project or inventing old decisions. The uploaded ZIP is asset data, not a source-code repository. Do not overwrite/recreate the user's active repository based on this package.

## Current evidence

- Archive: 13 files, 37,191,654 bytes; ZIP integrity check passes.
- Likely target: *Shinobi* PS2, but region/revision is unconfirmed.
- Outer `IZCA` sizing: 4/4 files pass `size = 16 + u32le(+4) + u32le(+8)`.
- `.SOT`: 11/11 string offsets parse and terminate within file.
- Media: `.SFD` contains MPEG-1 video and stereo 48 kHz ADX audio; all `.ADX` samples probe correctly.
- Model/container tag inventory is available in `results/sample_manifest.csv`.
- `tools/sample_audit.py` is a read-only, research-only helper. It does not extract nested `IZCA` payloads and is not the app's production parser.
- Its small unit-test suite passes 6/6 cases; ZIP vs extracted-directory manifest comparison passed for all 13 file hashes.

## Files in this package

- `docs/project/PROJECT_STATE.md` — current status and known blockers.
- `docs/project/FORMAT_SPEC.md` — observed headers/tags and confidence boundaries.
- `docs/project/RESEARCH_LOG.md` — reproducible experiments and results.
- `docs/project/ARCHITECTURE.md` — what is and is not known about the actual code architecture.
- `docs/project/DECISIONS.md` — recovered constraints/decisions, without invented history.
- `docs/project/TODO.md` — prioritized next work with acceptance criteria.
- `docs/project/TESTING.md` — reproduction command and required integration tests.
- `tools/sample_audit.py` — standard-library audit helper.
- `results/sample_manifest.csv` — sizes, SHA-256, first bytes, tag counts.
- `results/sot_strings.tsv` — the 11 extracted strings and offsets.
- `results/analysis_summary.json` — machine-readable audit summary and media metadata.

## Exact next step

Inspect the real current repository before editing anything. Integrate the four outer-`IZCA` fixtures and the `.SOT` table as tests in the existing parser/module structure. Build and run existing tests to establish a baseline before touching implementation.

## Verification caveat

No production C++ source, CMake target, public API, or existing tests were available here, so none was modified or executed. The completed milestone is **sample research and reproducible audit only**, not a production-code milestone. External reverse-engineering pages are research leads, not replacements for byte-level evidence or original project source.
