# Releasing

Releases use Semantic Versioning and tags in the form `vMAJOR.MINOR.PATCH`.

## Before release

1. Ensure `main` is green on Windows, macOS, and Linux.
2. Update the version in:
   - `CMakeLists.txt`;
   - `QCoreApplication::setApplicationVersion` in `src/main.cpp`.
3. Move user-visible entries from `[Unreleased]` in `CHANGELOG.md` into a versioned section with the release date.
4. Review `README.md`, `README.en.md`, and known limitations.
5. Confirm the current upstream list passes `--validate-file`.
6. Confirm release binaries are intentionally signed or intentionally unsigned.

## Publish

Set `.github/RELEASE` to the target tag and push the change to `main`:

```text
v0.2.0
```

The release workflow verifies that the marker matches the application version, builds and tests all configured platforms, creates the tag when needed, generates SHA-256 checksums, and publishes the matching GitHub Release.

If a tag already exists but the GitHub Release was not published, the workflow reuses that tag and publishes the release from the tagged commit.

## After release

- Download each published package.
- Verify `SHA256SUMS.txt`.
- Smoke-test first launch.
- Test `--status`, manual update, enabling and disabling scheduled updates, and source selection.
- Record platform-specific limitations in the release notes if necessary.

## Signing

Unsigned builds are acceptable for early testing but can trigger Windows SmartScreen and macOS Gatekeeper warnings. Code signing/notarization should be added before broader distribution when credentials are available.
