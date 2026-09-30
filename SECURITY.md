# Security Policy

## Supported versions

Until the project has multiple maintained release lines, security fixes target:

| Version | Supported |
| --- | --- |
| Latest release | Yes |
| `main` | Best effort |
| Older releases | No guarantee |

## Reporting a vulnerability

Please do **not** publish exploit details, secrets, private configuration data, or a proof of concept in a public issue.

Preferred reporting path:

1. Use GitHub's private vulnerability reporting / Security Advisory flow for this repository when available.
2. If a private reporting option is not available, open a minimal public issue that says you need a private security contact. Do not include sensitive technical details in that issue.

Include, when relevant:

- affected version/commit;
- operating system;
- attack preconditions;
- expected impact;
- reproduction steps;
- whether the issue can modify Amnezia settings, scheduler configuration, updater state, or downloaded data.

## Security model

The updater is designed to minimize impact:

- it does not require administrator/root privileges;
- it only manages the Amnezia split-tunneling exception list;
- it validates downloaded JSON before changing settings;
- it rejects empty/invalid lists and suspicious large list shrinkage;
- it preserves user-owned entries;
- it writes a backup before applying an update;
- it defers writes while Amnezia VPN is running;
- updater state and pending data are written atomically where applicable.

The configured upstream list is a trust dependency. Validation protects the file shape and several failure modes, but it cannot prove that every domain in a legitimately formatted upstream list is correct.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for trust boundaries.

## Out of scope

Please report these to their respective projects instead:

- vulnerabilities in Amnezia VPN itself;
- the editorial correctness of the upstream domain list;
- GitHub platform vulnerabilities;
- expected SmartScreen/Gatekeeper warnings caused solely by unsigned test builds.

Security reports about how this updater consumes upstream data or changes local settings are in scope.
