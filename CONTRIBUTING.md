# Contributing

Contributions are welcome. This project is intentionally small, so changes should stay focused, testable, and easy to audit.

## Before you start

- Use [Issues](https://github.com/tim8es/amnezia-vpn-tunnel-update/issues) for reproducible bugs and feature proposals.
- Read [SECURITY.md](SECURITY.md) before reporting a vulnerability.
- For non-security questions, see [SUPPORT.md](SUPPORT.md).
- Keep pull requests small where practical.

## Development requirements

- CMake 3.21+
- C++17 compiler
- Qt 6.5+ with Core, Network, Widgets, and Test

Build and run tests:

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build -C Release --output-on-failure
```

Validate an upstream-format list:

```bash
./build/amnezia-vpn-tunnel-update --validate-file amnezia.json
```

## Design rules

Changes must preserve these safety properties unless the change explicitly redesigns them and includes tests:

1. Never modify the VPN server, protocol, `Conf/routeMode`, or `Conf/sitesSplitTunnelingEnabled`.
2. Never write an invalid or empty upstream list to Amnezia settings.
3. Preserve user-managed domains that are not owned by the updater.
4. Keep a backup before changing `Conf/ExceptSites`.
5. If Amnezia VPN is running, defer the settings write.
6. Fail closed when the Amnezia configuration cannot be identified safely.
7. Avoid requiring administrator/root privileges.
8. Do not add telemetry or unrelated network calls without an explicit design discussion.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the current data flow and trust boundaries.

## Code style

- Follow the existing C++/Qt style.
- Use 4-space indentation.
- Prefer small functions and explicit error paths.
- Keep platform-specific scheduler/packaging logic isolated.
- Add or update tests for behavior changes.
- User-facing strings may be Russian; implementation identifiers and technical documentation should remain understandable to international contributors.

## Pull requests

A pull request should include:

- what changed and why;
- tests performed;
- affected platforms;
- any change to persistence, scheduling, networking, or Amnezia settings;
- screenshots for visible GUI changes when useful.

CI must pass on Windows, macOS, and Linux before merge.

## Releases

Release procedure is documented in [docs/RELEASING.md](docs/RELEASING.md).

## License

By contributing, you agree that your contribution is licensed under the repository's [MIT License](LICENSE).
