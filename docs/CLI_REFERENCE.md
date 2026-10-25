# CLI Reference

The command-line executable is named `akuijikiforge`.

## Usage

```text
akuijikiforge [--verbose] <file> [file ...]
akuijikiforge --help
```

| Option | Description |
|---|---|
| `--help`, `-h` | Display usage information. |
| `--verbose`, `-v` | Show a longer hexadecimal header preview. |

For each regular file, the CLI reports file size, Shannon entropy, a short hexadecimal header, and any recognized signatures from the built-in common-signature list. It does not write output files or change the input.

## Exit status

- `0` — all requested files were inspected, or help was displayed.
- `1` — one or more input paths could not be inspected.
- `2` — invalid command-line option or invalid invocation.

## Limits

The default maximum file size read into memory is 1 GiB. The common-signature list is intentionally small and does not imply comprehensive format identification. The CLI does not reconstruct undocumented formats or bypass software protections.
