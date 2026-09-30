## Summary

Describe what changed.

## Why

Describe the problem or motivation.

## Testing

- [ ] Unit tests pass locally or CI covers the change.
- [ ] I tested the affected platform(s), where practical.
- [ ] I added or updated tests for behavior changes.

Commands/results:

```text
# paste relevant output
```

## Safety checklist

- [ ] This change does not unexpectedly modify VPN server/protocol settings.
- [ ] This change does not unexpectedly modify `Conf/routeMode` or `Conf/sitesSplitTunnelingEnabled`.
- [ ] Failure paths prefer leaving existing Amnezia settings unchanged.
- [ ] User-managed domains remain preserved, or the ownership-model change is documented.
- [ ] No new telemetry, privilege requirement, or remote service was introduced without documentation.

## User-facing changes

Describe UI, CLI, packaging, scheduler, or documentation changes. Add screenshots for visible UI changes when useful.

## Related issues

Closes #
