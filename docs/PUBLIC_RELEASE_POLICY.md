# Public Release and Asset-Handling Policy

AkuijikiForge is independent open-source software. The public repository is deliberately smaller than a local research workspace: it contains the tool code, public usage documentation, tests, and release safeguards—not game-derived datasets or private analysis artifacts.

## Never commit by default

- Disc images, game executables, raw asset containers, memory dumps, or proprietary input files.
- Extracted game audio/video, model meshes, textures, animation data, or other content whose redistribution rights have not been established.
- Private research notes, unpublished findings, raw offsets/tables, emulator or debugger traces, and experiment outputs that are not intended for public release.
- User-specific paths, logs with personal information, credentials, access tokens, crash dumps, or local machine configuration.
- Screenshots, preview images, or test fixtures that reproduce protected game content unless permission to redistribute that specific content has been confirmed.

## Recommended workflow

1. Keep source inputs, full research notes, and generated exports outside the public repository.
2. Test with synthetic fixtures or material you are allowed to redistribute.
3. Run `python tools/audit_public_release.py`.
4. Inspect `git status --short` and `git diff --stat` before committing.
5. Review every new asset and document manually. The audit cannot determine copyright status or identify every sensitive disclosure.
6. If an asset is needed for reproducibility but cannot be published, document the input requirements without attaching the asset or its extracted contents.

## If sensitive material was committed

Deleting a file in a later commit does not remove its earlier copies from Git history. Remove the material from the index, review repository history, rotate exposed credentials, and follow GitHub's guidance for sensitive-data removal. Consider the repository compromised until the history and any mirrors/forks have been assessed.

## Important limitation

Because this is an open-source repository, anything deliberately included in its source code is public and can be inspected. If a specific algorithm or research result must remain confidential, do not include it in the public source tree; retain it in a separate private workspace instead.
