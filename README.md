Start with `docs/project/HANDOFF.md`, then read `PROJECT_STATE.md` and `FORMAT_SPEC.md`.

Run the read-only audit helper from this folder:

```bash
python3 tools/sample_audit.py "/path/to/GAME DATA.zip " --out-dir results
```

The original game-data assets are intentionally not copied into this package. Use the user's original archive as input; the manifest records per-file SHA-256 hashes for comparison.
