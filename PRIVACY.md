# Privacy

Amnezia VPN Tunnel Update is designed without analytics or telemetry.

## Network requests

The updater makes a network request when checking for a new split-tunneling list. The default sources are hosted on GitHub, and the user may configure a custom HTTPS JSON source.

Normal network metadata is visible to the server hosting the selected source and to network intermediaries according to their own policies.

## Local data

The application reads the local Amnezia VPN settings needed to manage `Conf/ExceptSites`.

It also stores local updater state such as:

- the selected source URL;
- the last source SHA-256;
- ETag;
- updater-managed entries (domains or IPv4/CIDR networks);
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
