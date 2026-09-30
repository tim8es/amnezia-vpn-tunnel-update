<p align="center">
  <img src="assets/amnezia-vpn-tunnel-update.svg" width="96" alt="Amnezia VPN Tunnel Update icon">
</p>

<h1 align="center">Amnezia VPN Tunnel Update</h1>

<p align="center">
  Automatic maintenance of the Amnezia VPN split-tunneling exclusion list without modifying the Amnezia client.
</p>

<p align="center">
  <a href="https://github.com/tim8es/amnezia-vpn-tunnel-update/actions/workflows/ci.yml"><img src="https://github.com/tim8es/amnezia-vpn-tunnel-update/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C.svg" alt="C++17">
  <img src="https://img.shields.io/badge/Qt-6.5%2B-41CD52.svg" alt="Qt 6.5+">
</p>

<p align="center">
  <a href="README.md">Русский</a> · English
</p>

> [!IMPORTANT]
> This is an independent, unofficial project. It is not part of, endorsed by, or affiliated with the Amnezia VPN project. See [NOTICE.md](NOTICE.md).

## What it does

**Amnezia VPN Tunnel Update** automatically keeps the split-tunneling exclusion list up to date.

The current data source is:

`https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia.json`

The utility does not patch the Amnezia client, install drivers, or stay resident as a daemon.

## Update flow

1. Fetch the latest `amnezia.json`.
2. Parse and validate it.
3. Compare its SHA-256 with the last applied source.
4. Preserve entries not owned by the updater.
5. Back up the current `Conf/ExceptSites`.
6. Update the exclusion list only.
7. Let the operating system run a short check every six hours.

| OS | Scheduler |
| --- | --- |
| Windows | Task Scheduler |
| macOS | LaunchAgent |
| Linux | systemd user timer |

No administrator/root privileges are required.

## Safety boundaries

The updater is not intended to automatically modify:

- VPN server configuration;
- VPN protocol configuration;
- `Conf/routeMode`;
- `Conf/sitesSplitTunnelingEnabled`;
- unrelated Amnezia VPN settings.

Before a settings change it rejects invalid/empty data, protects against suspicious large list shrinkage, preserves user entries, creates a backup, defers writes while Amnezia is running, and fails closed on unknown configurations.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [SECURITY.md](SECURITY.md).

## Installation

Use [GitHub Releases](https://github.com/tim8es/amnezia-vpn-tunnel-update/releases) for published versions.

If there is no tagged release yet, artifacts from the latest successful [CI run](https://github.com/tim8es/amnezia-vpn-tunnel-update/actions/workflows/ci.yml) are development builds.

Current package formats:

- Windows: portable x64 ZIP;
- macOS: DMG;
- Linux: x86_64 AppImage.

Development builds are currently unsigned, so Windows SmartScreen or macOS Gatekeeper may show a warning.

## CLI

```text
amnezia-vpn-tunnel-update --install
amnezia-vpn-tunnel-update --update --silent
amnezia-vpn-tunnel-update --status
amnezia-vpn-tunnel-update --uninstall
amnezia-vpn-tunnel-update --validate-file amnezia.json
```

Running without arguments opens the GUI.

## User-owned entries

The updater tracks the hostnames it manages:

```text
user entries = current Amnezia entries - previous managed set
result       = user entries + new managed set
```

This allows upstream removals to take effect while preserving unrelated user entries.

Current limitation: hostname identity is the ownership boundary. Manual IP changes for a hostname that is also upstream-managed may still be treated as updater-managed.

## Build from source

Requirements:

- CMake 3.21+;
- C++17;
- Qt 6.5+ with Core, Network, Widgets, and Test.

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Project documentation

- [CONTRIBUTING.md](CONTRIBUTING.md) — contribution workflow
- [SECURITY.md](SECURITY.md) — vulnerability reporting and security model
- [PRIVACY.md](PRIVACY.md) — local data and network requests
- [SUPPORT.md](SUPPORT.md) — support scope
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) — community expectations
- [CHANGELOG.md](CHANGELOG.md) — project changes
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — design and trust boundaries
- [docs/RELEASING.md](docs/RELEASING.md) — release process

## Known limitations

- development builds are unsigned;
- a universal macOS binary is not currently guaranteed;
- the Linux artifact currently targets x86_64;
- the utility updates the list, not itself;
- upstream list correctness remains a separate trust dependency.

## Contributing

Issues and pull requests are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first.

For problems with the **contents of the domain list itself**, use the upstream repository: [lib4u/amnezia-tunneling-ru](https://github.com/lib4u/amnezia-tunneling-ru).

## License

[MIT](LICENSE). Copyright © 2026 [tim8es](https://github.com/tim8es).
