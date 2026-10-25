# Contributing to AkuijikiForge

Thank you for helping improve AkuijikiForge. Issues and pull requests should be focused, reproducible, and respectful of third-party rights.

## Before opening a pull request

1. Build the C++ target and run CTest.
2. Run Python tests with `python -m unittest discover -s tests -p 'test_*.py' -v`.
3. Run `python tools/audit_public_release.py`.
4. Review the diff for local paths, private research, copied game assets, generated output, credentials, and unnecessary binary files.

## Code and documentation

- Prefer clear names, bounded input processing, explicit error messages, and tests for new parsing logic.
- Preserve the read-only behavior of the CLI; do not silently patch or rewrite supplied inputs.
- Keep UI code native to Qt Widgets. Do not add HTML, an embedded browser, or a web UI without an explicit project decision.
- Keep public documentation focused on how to use the released tool. Do not commit private reverse-engineering notes, raw dumps, or non-redistributable output.
- Use small synthetic fixtures created specifically for tests. Do not use copyrighted game media as test fixtures.

## Licensing

By submitting a contribution, you agree that the contribution can be distributed under the repository's MIT License, subject to the license and notices applicable to any third-party material you introduce. Do not submit material unless you have the rights needed to contribute it.
