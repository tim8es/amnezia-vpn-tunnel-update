# Privacy

Amnezia VPN Tunnel Update is designed without analytics or telemetry.

## Network requests

The updater makes a network request when checking for a new split-tunneling list. The current source is:

`https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia.json`

Normal network metadata may therefore be visible to GitHub and network intermediaries according to their own policies.

## Local data

The application reads the local Amnezia VPN settings needed to manage `Conf/ExceptSites`.

It also stores local updater state such as:

- the last source SHA-256;
- ETag;
- updater-managed domain names;
- pending-update checksum/data;
- local backups created before settings changes.

These files are stored in the application's per-user data location selected by Qt / the operating system.

## What is not collected by this project

The application does not intentionally send the maintainer:

- VPN credentials;
- VPN server configuration;
- browsing history;
- analytics events;
- crash telemetry;
- user-created split-tunneling domains.

A future change that introduces telemetry or additional remote services should be documented here before release.
