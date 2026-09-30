# Releasing

Releases use Semantic Versioning and tags in the form `vMAJOR.MINOR.PATCH`.

## Before tagging

1. Ensure `main` is green on Windows, macOS, and Linux.
2. Update the version in:
   - `CMakeLists.txt`;
   - `QCoreApplication::setApplicationVersion` in `src/main.cpp`.
3. Move user-visible entries from `[Unreleased]` in `CHANGELOG.md` into a versioned section with the release date.
4. Review `README.md`, `README.en.md`, and known limitations.
5. Confirm the current upstream list passes `--validate-file`.
6. Confirm release binaries are intentionally signed or intentionally unsigned.

## Tag and release

Create and push an annotated release tag:

```bash
git tag -a v0.1.0 -m "Amnezia VPN Tunnel Update v0.1.0"
git push origin v0.1.0
```

The release workflow builds packages for all configured platforms, creates SHA-256 checksums, and publishes/updates the matching GitHub Release.

## After release

- Download each published package.
- Verify `SHA256SUMS.txt`.
- Smoke-test first launch.
- Test `--status`, manual update, enabling scheduled updates, and uninstalling the schedule.
- Record platform-specific limitations in the release notes if necessary.

## Signing

Unsigned builds are acceptable for early testing but can trigger Windows SmartScreen and macOS Gatekeeper warnings. Code signing/notarization should be added before broader distribution when credentials are available.
