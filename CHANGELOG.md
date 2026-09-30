# Changelog

All notable changes to this project will be documented in this file.

The project follows [Semantic Versioning](https://semver.org/) for tagged releases and uses the general structure of [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [0.2.2] - 2026-09-30

### Fixed

- Kept the GUI responsive during update, source-apply, and scheduler operations.
- Moved Windows scheduler status/install/uninstall checks out of the UI thread.
- Replaced whole-form disabling with explicit operation status while only action buttons are locked.

## [0.2.1] - 2026-09-30

### Changed

- Replaced deferred pending updates with an explicit restart-or-skip flow when Amnezia VPN is running.
- Scheduled checks remember a skipped SHA-256 and ask again only when a newer list appears.
- GUI network checks now run off the UI thread so Windows remains responsive while checking sources.
- Approved updates gracefully stop Amnezia VPN, apply the already validated list, and start Amnezia VPN again.

### Fixed

- Fixed the Windows GUI appearing to freeze when applying a source while Amnezia VPN is running.

## [0.2.0] - 2026-09-30

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
