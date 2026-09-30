# Architecture

## Purpose

Amnezia VPN Tunnel Update keeps Amnezia VPN's split-tunneling exception list synchronized with a selected remote JSON list without modifying the Amnezia client. Sources may contain domains, IPv4 addresses, or IPv4 CIDR networks.

The application is intentionally small: there is no daemon, privileged service, database, or embedded web server.

## Data flow

```text
selected JSON source
        |
        v
 network fetch (ETag)
        |
        v
 parse + validate
        |
        +---- invalid / empty / suspicious shrink ----> stop, no settings write
        |
        v
 SHA-256 comparison
        |
        +---- unchanged ------------------------------> stop
        |
        v
 Is Amnezia VPN running?
        |
   yes  +----> prompt: restart now / skip
        |           |
        |           +---- skip ----> store skipped SHA/ETag -> stop
        |           |
        |           +---- restart -> graceful stop -> continue
        |
    no
        v
 load current Conf/ExceptSites
        |
        v
 separate user entries from previous updater-managed entries
        |
        v
 merge user entries + new managed entries
        |
        v
 backup current settings
        |
        v
 write Conf/ExceptSites via QSettings
        |
        v
 atomically persist updater state
```

## Main components

### `ListCodec`

Parses and validates the upstream JSON, normalizes hostnames/IP values, computes the managed representation, and handles backup serialization.

### `AmneziaSettings`

Uses Qt `QSettings("AmneziaVPN.ORG", "AmneziaVPN")` to interact with the same settings namespace as Amnezia VPN.

The updater manages `Conf/ExceptSites` only. It reads routing state for status display but does not intentionally change:

- `Conf/routeMode`;
- `Conf/sitesSplitTunnelingEnabled`;
- VPN server configuration;
- VPN protocol configuration.

### `StateStore`

Stores updater-owned state separately from Amnezia settings. State includes the selected source URL, last applied digest, ETag, the last explicitly skipped digest, managed-entry identity, and source-transition state.

Backups are stored in the updater's per-user data directory.

### `Updater`

Coordinates source selection, network fetch, restart decisions, validation, safety checks, merging, backup, write, rollback attempts, and state persistence. When the source changes, the updater keeps ownership of the previous managed set long enough to remove it safely, clears source-specific ETag/hash state, and skips the ordinary shrink guard for that one intentional transition.

### `Installer` and `Scheduler`

Copies the packaged updater to a stable per-user location and registers short periodic executions:

| OS | Scheduler |
| --- | --- |
| Windows | Task Scheduler |
| macOS | LaunchAgent |
| Linux | systemd user timer |

The updater is not intended to stay resident in memory.

## Ownership model

The updater remembers which entries it previously managed.

```text
user entries = current Amnezia entries - previous managed set
result       = user entries + new managed set
```

This lets upstream removals take effect while preserving unrelated entries created manually by the user.

Hostname identity is the ownership boundary in the current version. A manually modified IP list for a hostname that is also upstream-managed may still be treated as updater-owned.

## Failure behavior

The design favors no change over an uncertain change.

Examples:

- invalid JSON -> no write;
- empty valid JSON -> no write;
- no recognizable Amnezia configuration -> no write;
- Amnezia is running -> no settings write until the user explicitly approves a restart;
- large unexpected upstream shrink -> no write;
- state-save failure after a settings write -> rollback attempt.

## Trust boundaries

### Upstream data

The upstream repository is trusted to choose the intended domains. Local validation verifies format and guards against several accidental/corrupt states, but does not establish editorial correctness of each domain.

### Local Amnezia settings

The updater assumes the compatible QSettings schema remains available. Schema changes in Amnezia VPN may require updater changes.

### Amnezia restart

For an approved update, the updater requests a normal application shutdown, waits for the Amnezia GUI process to exit, applies the validated list, and starts Amnezia again. It never force-kills the client as part of the normal update flow.

### Operating-system scheduler

Scheduler registration is per-user and does not require a privileged background service. Scheduled checks use the same restart/skip decision when a new list is found while Amnezia is running.

## Privacy

See [../PRIVACY.md](../PRIVACY.md).
