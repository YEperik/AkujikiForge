# Security Policy

## Supported scope

AkuijikiForge's public edition is designed for local, read-only file inspection and basic preview workflows. It does not intentionally modify input files, communicate with a game process, bypass access controls, or evade anti-cheat systems.

## Reporting a security concern

Please avoid posting exploit details, credentials, proprietary binaries, game dumps, personal data, or private reverse-engineering material in a public issue. Use GitHub's **private vulnerability reporting / security advisory** feature if it is enabled for the repository. Otherwise, contact the maintainer through a private contact method listed on the repository owner's GitHub profile.

Include a concise impact description, affected version, reproduction steps using synthetic or redistributable samples, and a proposed mitigation where possible. Do not attach copyrighted game assets or confidential material.

## Release hygiene

Before publishing a release, run:

```bash
python tools/audit_public_release.py
```

The audit is a guardrail, not a guarantee. A human review is still required before pushing, especially when adding fixtures, screenshots, sample files, generated exports, logs, or documentation. Do not rely on `.gitignore` alone: files already tracked by Git remain tracked until explicitly removed from the index.
