# Publish AkuijikiForge to GitHub

This project ships as a native desktop application and C++ CLI. **GitHub Pages is not required**; the repository does not use HTML as its application UI.

## Create the repository

1. Create a repository named `AkuijikiForge` under the intended GitHub account or organization.
2. Keep it private while performing your final review if you have not yet audited the complete history and staging area.
3. Extract this release package and use the contents of the `AkuijikiForge/` folder as the repository root.
4. Commit and push the source tree to the repository's `main` branch.

Typical first push:

```bash
git init
git add .
python tools/audit_public_release.py
git status --short
git commit -m "Initial public release of AkuijikiForge"
git branch -M main
git remote add origin https://github.com/OWNER/AkuijikiForge.git
git push -u origin main
```

Replace `OWNER` with the GitHub account or organization that owns the repository. Review `git status --short` before committing; do not blindly add files from a local research workspace.

## After the first push

- Confirm the **Build and test** workflow passes under the repository's Actions tab.
- Enable private vulnerability reporting if it is available for the repository.
- Add a release tag only after the build, tests, and release-content audit pass.
- Keep the GitHub Pages setting disabled unless a separate, intentional website is added in a future change.

## Important

If sensitive files were previously committed, deleting them from the current working tree is not enough to purge them from history. Review the full Git history before changing a private repository to public.
