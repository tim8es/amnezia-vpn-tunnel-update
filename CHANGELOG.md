# Changelog

All notable changes to this project will be documented in this file.

The project follows [Semantic Versioning](https://semver.org/) for tagged releases and uses the general structure of [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- Selectable built-in sources: domain, IP Lite, and full IP/CIDR lists.
- Custom HTTPS JSON subscriptions persisted for scheduled updates.
- GUI control to disable previously enabled automatic updates.
- Safe source switching that replaces updater-owned entries while preserving user entries.
- CLI `--source <url>` option.

## [0.1.0] - 2026-09-30

### Added

- Independent cross-platform updater for Amnezia VPN split-tunneling exclusions.
- Windows Task Scheduler, macOS LaunchAgent, and Linux systemd user-timer integration.
- GUI and CLI modes.
- Safe merge of upstream-managed and user-managed entries.
- Atomic updater state/pending writes and settings backups.
- ETag/SHA-256 based no-op detection.
- Deferred apply while Amnezia VPN is running.
- Suspicious upstream shrink protection.
- Cross-platform CI and package artifacts.
- Project documentation and issue forms.
