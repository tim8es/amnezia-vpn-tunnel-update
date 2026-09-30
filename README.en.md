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

The default source is the public auto-updating `amnezia.json` list. The GUI can switch between three built-in sources or subscribe to a compatible JSON list using a custom HTTPS URL.

| List | Contents | Use case |
| --- | --- | --- |
| `amnezia.json` | 2000+ domains and local zones | Targeted bypass for known services |
| `amnezia-ip-lite.json` | ~650 IPv4/CIDR networks | Compact IP list |
| `amnezia-ip.json` | 12,800+ IPv4/CIDR networks | Maximum IP coverage |

The utility does not patch the Amnezia client, install drivers, or stay resident as a daemon.

## Update flow

1. Fetch the selected JSON list.
2. Parse and validate it.
3. Compare its SHA-256 with the last applied or skipped version.
4. If Amnezia VPN is closed, apply the update immediately.
5. If Amnezia VPN is running, ask whether to **Restart now** or **Skip**.
6. On approval, restart Amnezia VPN gracefully, back up the current settings, and update `Conf/ExceptSites`.
7. A skipped SHA-256 is not prompted again; the next changed list can prompt again.
8. Let the operating system run a short check every six hours.

| OS | Scheduler |
| --- | --- |
| Windows | Task Scheduler |
| macOS | LaunchAgent |
| Linux | systemd user timer |

No administrator/root privileges are required. The updater does not wait for a future Amnezia shutdown and does not keep a watcher process alive.

## Installation

Use [GitHub Releases](https://github.com/tim8es/amnezia-vpn-tunnel-update/releases) for published versions.

Current package formats:

- Windows: portable x64 ZIP;
- macOS: DMG;
- Linux: x86_64 AppImage.

Development builds are currently unsigned, so Windows SmartScreen or macOS Gatekeeper may show a warning.

## CLI

```text
amnezia-vpn-tunnel-update --install
amnezia-vpn-tunnel-update --update --silent
amnezia-vpn-tunnel-update --source https://example.com/list.json --update
amnezia-vpn-tunnel-update --status
amnezia-vpn-tunnel-update --uninstall
amnezia-vpn-tunnel-update --validate-file amnezia.json
```

Running without arguments opens the GUI.

## User-owned entries

The updater tracks the domain and IP/CIDR entries it manages:

```text
user entries = current Amnezia entries - previous managed set
result       = user entries + new managed set
```

This allows source-list removals to take effect while preserving unrelated user entries.

Current limitation: hostname identity is the ownership boundary. Manual IP changes for a hostname that is also upstream-managed may still be treated as updater-managed.

## Build from source

Requirements:

- CMake 3.21+;
- C++17;
- Qt 6.5+ with Core, Network, Widgets, Concurrent, and Test.

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Project documentation

- [PRIVACY.md](PRIVACY.md) — local data and network requests
- [SUPPORT.md](SUPPORT.md) — support scope
- [CHANGELOG.md](CHANGELOG.md) — project changes
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — design and trust boundaries
- [docs/RELEASING.md](docs/RELEASING.md) — release process

## Contributing

Issues and pull requests are welcome.

For problems with the **contents of the domain list itself**, use the upstream repository: [lib4u/amnezia-tunneling-ru](https://github.com/lib4u/amnezia-tunneling-ru).

## License

[MIT](LICENSE). Copyright © 2026 [tim8es](https://github.com/tim8es).
