# Initial Format Notes — Evidence-Labeled

**Snapshot date:** 2026-10-21  
**Rule:** “Observed” means directly checked in supplied bytes. “Hypothesis” means structural interpretation that still needs comparative testing.

## 1. Outer `IZCA` header

Four supplied files begin with ASCII `IZCA` (`49 5A 43 41`). In each, the following size equation is exact:

`file_size = 0x10 + u32le(file_offset=0x04) + u32le(file_offset=0x08)`

| File | Actual size | `u32le @ 0x04` | `u32le @ 0x08` | Calculated total | Result |
|---|---:|---:|---:|---:|---|
| `DEMOEND.MLX` | 525,088 (`0x80320`) | 525,056 (`0x80300`) | 16 (`0x10`) | 525,088 | `VERIFIED` |
| `MA00_10_00.MLX` | 1,197,136 (`0x124450`) | 1,197,104 (`0x124430`) | 16 (`0x10`) | 1,197,136 | `VERIFIED` |
| `PLAYER00.MLD` | 2,128,400 (`0x207A10`) | 2,128,368 (`0x2079F0`) | 16 (`0x10`) | 2,128,400 | `VERIFIED` |
| `SE_STAGE_01.MLT` | 41,360 (`0xA190`) | 41,264 (`0xA130`) | 80 (`0x50`) | 41,360 | `VERIFIED` |

**Interpretation boundary:** field `0x08` behaves like an extended-header length/span for these outer files. The equation does **not** by itself prove the meanings of nested chunk records or any compression algorithm. Field `0x0C` is zero in these four headers, but its semantics are unknown.

## 2. `PLAYER00.MLD` / model container observations

| Byte-string/tag | Occurrences | Evidence status |
|---|---:|---|
| `IZCA` | 149 | Exact byte-string count in supplied file |
| `KFMD` | 112 | Exact byte-string count |
| `HTSF` | 67 | Exact byte-string count |
| `GBIX` | 67 | Exact byte-string count |
| `PVRT` | 67 | Exact byte-string count |
| `HMDL` | 56 | Exact byte-string count |
| `MLE0` | 47 | Exact byte-string count |
| `PPPL` | 4 | Exact byte-string count |
| `MLX0`, `MXTL`, `MXTF`, `EOFC` | 1 each | Exact byte-string counts |

The first top-level record includes `IZCA` at `0x00`, `MLX0` at `0x240`, `MXTL` at `0x280`, resource-like names beginning around `0x3B0`, and `HMDL` at `0x4C0`. Float-like values and structured records appear around `0xC30` onward, but this has not been converted into a complete mesh schema.

`MA00_10_00.MLX` has 14 exact `IZCA` occurrences, 11 `HMDL`, 20 each of `GBIX` and `PVRT`, 19 `HTSF`, and 5 each of `MLX0`/`MXTL`. `DEMOEND.MLX` contains `IZCA`, `MLX0`, `MXTL`, `MXTF`, `HTSF`, `GBIX`, `PVRT`, and `EOFC` once each.

**Hypotheses / not yet implemented:** nested `IZCA` entries may act as chunk arrays or directories for their contained records; `GBIX`/`PVRT` likely relate to texture resources; `HMDL`/`KFMD` appear model-related. No nested extractor or full geometry decoder is implemented in this package.

## 3. `EV00_00_00E.SOT` string table

**Observed layout for this file only:**

- Offset `0x00`: ASCII `SOT:BIN` followed by a NUL byte.
- Offset `0x0C`: little-endian `uint32` value `11`.
- Offset `0x10..0x1F`: four `0xFFFFFFFF` values.
- Offset `0x20`: 11 records, each 8 bytes (`uint32 key`, `uint32 string_offset`).
- First string offset is `0x78`; every record’s offset is within the file and reaches a NUL terminator before EOF.
- The records contain 11 English lines, including a line mentioning Hotsuma. Exact extracted records are in `results/sot_strings.tsv`.

The first dword in each record progresses as `0x45002710`, `0x4500271A`, `0x45002724`, etc. Its purpose is **unknown**; do not yet label it as a pointer, voice ID, or speaker ID.

## 4. `ECSET01_00_00.ESP`

Observed signatures and values:

- `ESPC02` at offset `0x00`.
- `u32le @ 0x04 = 0x3230`; `u32le @ 0x08 = 0x10`.
- `ESPM` at offset `0x10`.
- `u32le @ 0x14 = 31`.
- `u32le @ 0x18 = 0xAC`.
- 31 increasing 32-bit values begin at `0x20`; first is `0x33C`, last is `0x3104`.
- `EOFC` appears at offset `0x3240`, immediately before the 16-byte end record at EOF (`file_size=0x3250`).

This is consistent with a table of 31 offset-like entries followed by subrecords, but record types, whether all offsets are absolute, and field meanings are not fully proven. Bounds/order can be turned into tests; do not implement archive extraction based only on naming.

## 5. `EV00_00_00.EVD`

Four-byte tag locations observed:

| Offset | Tag | `u32le @ +4` | Other observation |
|---:|---|---:|---|
| `0x000` | `EVDT` | `0x0000` | header-like value at `+8` is `0x10` |
| `0x010` | `EVAC` | `0x0020` | header-like value at `+8` is `0x10` |
| `0x040` | `ECMD` | `0x01C0` | header-like value at `+8` is `0x10` |
| `0x210` | `ENAM` | not fully mapped here | string/name-like section candidate |
| `0x228` | `EMSG` | not fully mapped here | message-like section candidate |
| `0x238` | `NPJ_` | not fully mapped here | meaning unknown |
| `0x2D4` | `EMSG` | not fully mapped here | second occurrence |
| `0x2E4` | `GNE_` | not fully mapped here | meaning unknown |
| `0x380` | `EOFC` | — | final 16-byte record starts here |

Tag names are literal byte evidence. Their semantic meanings and links between sections are not yet decoded.

## 6. `MA00_10_00.EST`

- Begins with `MAPA`.
- `u32le @ 0x04 = 0x1C90`; `u32le @ 0x08 = 0x10`.
- Contains readable identifiers such as `fenvLightAmbient`, `fenvFog`, `fenvLightParallel`, repeated `factGround`, `GimmickMlxPool`, `Create_KekkaiChu`, and `TaskGen_ExitStage`.
- The arithmetic `0x10 + 0x1C90` does not equal the whole file length (`0x1E30`), so do not assume that the first `MAPA` field describes all trailing bytes.

## 7. `FONT.BDA`

Readable names include `B Data`, `gaiji.ada`, `gaiji_t.ada`, `sjis.ada`, `ansi.ada`, `A Data`, `.fon`, `.tbl`, `MFNT`, `MFGT`, and `The End of Bda`. The layout, offset units, character encoding, and font bitmap tables remain unparsed.

## 8. Media

`ffprobe` completed successfully for all four media files:

| File | Detected streams | Duration |
|---|---|---:|
| `0010.SFD` | MPEG-1 video, 640×320 at 30000/1001 fps; stereo ADX at 48 kHz | 88.816667 s |
| `BGM00_00_01T.ADX` | stereo ADX at 48 kHz | 150.212 s |
| `GAME_START.ADX` | stereo ADX at 48 kHz | 8.162667 s |
| `PC_TATE_DENGEKI.ADX` | stereo ADX at 48 kHz | 1.360000 s |

The `.SFD` sample also contains readable CRI/Sofdec metadata such as `SofdecStream` and `CRI_SFM Ver.2.03`. This identifies media technology but not the game’s disc revision.

## 9. External references (research leads, not project source of truth)

- ResHax, “Shinobi and Nightshade/Kunoichi | PlayStation 2” (created 2024-01-04): community discussion of `PLAYER00.MLD`, `IZCA`, `HMDL`, and model-format clues: https://reshax.com/topic/392-shinobi-and-nightshadekunoichi-playstation-2/
- ResHax, “PS2 - Shinobi (2002) - Sega/OverWorks - Need help to rip models” (2026-07): discusses the same title and `PLAYER00.MLD`, while still describing extraction/model reconstruction issues: https://reshax.com/topic/19320-ps2-shinobi-2002-segaoverworks-need-help-to-rip-models/
- ZenHAX, “SEGA NN Framework *.BNK”: describes an `IZCA`/Izumi Chunk Array family in a different SEGA NN framework context. Treat it as related research only; its sample layout is not identical to the headers observed here: https://zenhax.com/viewtopic.php@t=13225.html
