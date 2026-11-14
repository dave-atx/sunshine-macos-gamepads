# Changelog

All notable changes to this fork. Upstream Sunshine's history is in
[docs/changelog.md](docs/changelog.md).

## [2026.11.0] — unreleased

First fork release. Adds virtual gamepad support on macOS.

### Added

- **OpenJoystickDriver gamepad backend for macOS.** Sunshine routes gamepad
  state to OJD's local endpoint, which creates real HID gamepads using an Apple
  entitlement this build cannot hold itself. Between 1 and 4 virtual gamepads
  are supported, which is OJD's per-client limit.
- Two configuration options: `ojd_socket_path` and `ojd_token`. Both are
  required; leaving either empty keeps the backend inert and behaviour identical
  to upstream.
- Web UI controls for both options under **Configuration → Input**.
  Previously, saving from the config page would have silently dropped any
  hand-written keys the UI did not know about.
- `ojd_token` is excluded from logs, matching Sunshine's handling of other
  secrets.
- Automatic reconnect when the OJD endpoint restarts, with exponential backoff
  from 250 ms to 5 s. A refused token or disabled endpoint stops retrying rather
  than spinning, because neither resolves without operator action.
- The home page no longer warns that Virtual HID Broker is unavailable when OJD
  is configured and working.

### Changed

- `probe_gamepads()` recognises the OJD backend, so a working configuration no
  longer logs "No gamepad input is available".
- Fork version stamped as `2026.11.0` instead of upstream's `0.0.0`, which made
  local builds report `0.0.0-dirty`.

### Known issues

- **Rumble is not wired up.** The client parses OJD's `RumbleCommand`, but
  Sunshine's feedback queues are per-session while these pads are process-global.
  Routing rumble today would send one player's rumble to another. Needs session
  identity plumbed through the platform input entry points.
- Four *concurrent* Moonlight clients have not been exercised. Single-client
  streaming with a controller is verified end to end.
- No motion, touchpad, or battery support; Sunshine's controller arrival metadata
  is not forwarded to OJD.
- OJD is alpha software and its endpoint `apiVersion` is not yet stable.

### Security

- The endpoint token grants control of the virtual gamepads. It is never logged,
  and the config file is written with mode `0600`.

### Upstream compatibility

Nine files touched, ~150 lines. The libvirtualhid path, ViGEmBus path, and
`gamepad_driver` policy are unchanged, so an unconfigured build behaves exactly
like upstream.
