# VIF V3-32 Q23 Triple Correlation — `PLAYER00.MLD` (v0.8.0)

## Scope

This report documents a numeric correlation in the 35 fully count-matched VIF groups with opcode sequence `[0x6C, 0x6C, 0x68, 0x64]` inside the supplied `PLAYER00.MLD`. The analyzer exports each third-packet V3-32 element as three raw little-endian 32-bit words and tests a candidate normalization by dividing each unsigned word by `2^23` (`8,388,608`). This is a reproducible arithmetic observation, not a decoded format specification.

## Results

| Measurement | Result |
|---|---:|
| Matched groups containing V3-32 | 35 |
| V3-32 triples exported | 131 |
| Components inside `[0,1]` | 393 / 393 |
| Distinct raw triples | 39 |
| Triple sums within `2e-5` of 1 after division by `2^23` | 131 / 131 |
| Triple sums within `1e-5` of 1 | 128 / 131 |
| Minimum observed component | `0.000000000` |
| Maximum observed component | `0.949997067` |
| Minimum normalized sum | `0.999988794` |
| Maximum normalized sum | `1.000005841` |
| Third raw component equals zero | 108 / 131 |

Representative examples:

| Raw components (hex) | Components divided by `2^23` | Sum |
|---|---|---:|
| `0x599981, 0x266654, 0x0` | `0.699997067, 0.299997807, 0` | `0.999994874` |
| `0x666641, 0x199984, 0x0` | `0.799995542, 0.199997425, 0` | `0.999992967` |
| `0x400001, 0x400004, 0x0` | approximately `0.500000119, 0.500000477, 0` | approximately `1.000000596` |

The repeated near-unit sum is consistent with three normalized nonnegative weights. The raw values, their normalization, and their observed counts are confirmed by the sample and CSV. The semantic hypothesis that these are weights is plausible, but no per-element bone-index map has been established. Therefore this report does **not** claim the values are skeletal skinning weights, blend weights, barycentric coordinates, or any specific gameplay/renderer attribute.

## Reproduction

From the project root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/ps2probe --vif-v3-csv path/to/PLAYER00.MLD docs/vif_v3_q23_candidates.csv
```

The command emits one row per matched V3-32 element. Key fields:

- `raw_component_0..2`: source words preserved as unsigned hexadecimal values.
- `q23_component_0..2`: raw values divided by `8,388,608`.
- `q23_sum`: sum of the three normalized components.
- `components_in_0_1`: all three normalized values fall in the inclusive range `[0,1]`.
- `sum_within_2e-5_of_1`: arithmetic threshold flag, not a semantic classification.
- `first_v4_w_raw`, `second_v4_w_raw`, `uv_u`, `uv_v`: neighboring data exported for later correlation; their roles remain hypotheses.

The current source sample produced 131 data rows plus the header. Input that lacks a complete matching chain produces header-only output. Exported values do not establish vertex order, bone indices, or mesh connectivity.

## Next experiment

Find a separate index/skin record or a stable per-model relationship between the raw V3 triples and the candidate KFMD tail IDs `0x1C..0x31`. Verify that relationship on a second known model or another sample before implementing a skin-weight decoder. Until then, retain raw fields and avoid labeling this data as confirmed skinning.
