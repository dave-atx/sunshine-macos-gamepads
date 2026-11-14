# Sunshine — macOS virtual gamepad fork

A fork of [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) that makes
**virtual gamepads work on macOS**, which upstream does not support.

Moonlight clients on any platform — iPhone, Android, Windows, Linux, another Mac —
can connect with a Bluetooth or USB controller, and the game on the host sees a
real HID gamepad. No paid license, no SIP changes, no Apple Developer approval.

```
Moonlight client (Bluetooth/USB controller)
        │  Moonlight controller packets
        ▼
Sunshine  ── platf::gamepad_update ──▶  nexus-ojd bridge
        │                                    │
        │                          JSON over AF_UNIX
        │                                    ▼
        │                          OpenJoystickDriver endpoint
        │                                    │
        │                          IOHIDUserDevice (Apple-entitled)
        ▼                                    ▼
   video stream                        macOS HID ──▶ game
```

## The problem

On macOS, Sunshine cannot create virtual controllers:

| Platform | Gamepad emulation |
|---|---|
| Windows | ✅ (paid Virtual HID Broker, or ViGEmBus) |
| Linux / FreeBSD | ✅ |
| **macOS** | **❌ not supported** |

Upstream's [feature matrix](https://docs.lizardbyte.dev/projects/sunshine/latest/)
marks every emulated controller unsupported on macOS, and the getting-started
guide says plainly: *"Gamepads do not work."*

The cause is an Apple restriction. Creating a virtual HID device requires the
entitlement `com.apple.developer.hid.virtual.device`, which Apple grants per team
through an approval process that is **not** tied to account tier. A free Personal
Team cannot get it, and neither can a paid membership without approval.

The commercial answer is [Virtual HID Broker](https://github.com/LizardByte/libvirtualhid)
(~$15/yr or $50 lifetime), which ships a signed broker that already holds the
entitlement. That works — if you want the simplest path, buy it.

**This fork is the free alternative.**

## How it works

[OpenJoystickDriver](https://github.com/xsyetopz/OpenJoystickDriver) is a
separately signed, notarized macOS app that holds the same Apple entitlement. It
exposes a documented local-socket API that lets any program drive a virtual
gamepad. Sunshine never creates HID devices; it drives OJD's.

The bridge lives in [`third-party/nexus-ojd`](third-party/nexus-ojd) and is wired
into Sunshine with ~30 lines of changes.

## Requirements

- macOS 14.2 or newer (Apple silicon or Intel)
- [OpenJoystickDriver](https://github.com/xsyetopz/OpenJoystickDriver/releases)
- A Moonlight client

## Setup

### 1. Install OpenJoystickDriver

Download the macOS DMG from the
[latest release](https://github.com/xsyetopz/OpenJoystickDriver/releases), drag
**OpenJoystickDriver.app** into Applications, and open it.

Grant the two permissions it needs:

```sh
open /Applications/OpenJoystickDriver.app
# if anything is still denied:
/Applications/OpenJoystickDriver.app/Contents/MacOS/OpenJoystickDriver permission request
```

Input Monitoring and Accessibility are both required — without them the virtual
pad is created but refuses to start, reporting `Input Monitoring denied`.

### 2. Enable the endpoint and mint a token

OJD installs no CLI by default; its own binary acts as one when invoked under the
name `ojd`:

```sh
ln -sf /Applications/OpenJoystickDriver.app/Contents/MacOS/OpenJoystickDriver /usr/local/bin/ojd

ojd access enable --force
ojd access grant --token sunshine --scope read --scope control
```

The grant prints a token **once**. Copy it, and treat it as a password: anything
holding it can control the virtual pads.

Socket path:

```sh
ojd --json access status | jq -r .socketPath
```

It lives under `$TMPDIR` and changes when that directory is cleared, so re-read it
if pads stop working after a reboot.

### 3. Configure Sunshine

Use the Web UI at `https://localhost:47990` → **Configuration → Input**, which now
has an **OpenJoystickDriver gamepads** section, or edit the config file:

```ini
controller = enabled
gamepad_driver = virtualhid
ojd_socket_path = /var/folders/.../com.openjoystickdriver.endpoint.sock
ojd_token = ojd_...
```

Both values are required. Leave either empty and the backend stays completely
inert, so behaviour is identical to upstream. Restart Sunshine afterwards.

### 4. Pair and play

Pair your Moonlight client, launch an app, use a controller.

## Verify it works

```sh
# While moving a stick in Moonlight, expect:
#   Xbox Adaptive Controller | Microsoft | 045e:0b0c
ojd --json controller list

# Or from Sunshine's log:
grep "OpenJoystickDriver backend" ~/.config/sunshine/sunshine.log
# Info: Gamepad 0 uses the OpenJoystickDriver backend
```

If no pad appears, check `gamepad_driver` is not `none`; Sunshine's input layer
drops controller packets before any backend is reached in that case.

## What works

Verified on macOS 27.2 (arm64), Sunshine `657f9b6`:

| Capability | Status |
|---|---|
| Virtual pads from Moonlight clients | ✅ |
| Bluetooth and USB controllers | ✅ transport is irrelevant — Moonlight sends packets |
| Input reaches the game | ✅ confirmed by reading back HID element values |
| Up to 4 simultaneous pads | ✅ (OJD's per-client limit) |
| Reconnect after OJD restarts | ✅ automatic, backoff 250 ms → 5 s |
| SIP / AMFI | untouched — `csrutil status` stays `enabled` |
| Rumble | ⚠️ disabled, see below |

### Known limitations

- **Rumble is disabled.** The protocol layer parses OJD's `RumbleCommand`
  correctly, but wiring it into Sunshine is unsafe with multiple viewers:
  Sunshine has one feedback queue per Moonlight session while these pads are
  process-global, so the bridge cannot tell which session owns a pad and would
  route one player's rumble to another. Fixing it needs session identity plumbed
  through the platform input entry points.
- **Four pads maximum.** OJD serves 4 virtual gamepads per endpoint client. For a
  5-player session, one player uses a controller attached to the host.
- **No motion, touchpad, or battery.** OJD chooses the virtual device profile;
  Sunshine's controller arrival metadata is not forwarded.
- **OJD is pre-release alpha.** Its endpoint `apiVersion` changed once already
  between alpha releases. All protocol strings live in one header
  (`protocol.hpp`) so an upstream bump stays contained.
- **`SDLCompatible` persona only.** The `Generic` profile's HID descriptor is
  input-only, so rumble could never work there.

## Building from source

```sh
brew install cmake ninja boost ffmpeg opus nlohmann-json miniupnpc icu4c

git clone --recursive https://github.com/<you>/sunshine-macos-gamepads.git
cd sunshine-macos-gamepads

# build-deps must be on a *tagged* commit, or FFmpeg resolves to a release that
# does not exist.
git -C third-party/build-deps fetch --depth 1 origin tag v2026.1017.131201
git -C third-party/build-deps checkout v2026.1017.131201
git -C third-party/moonlight-common-c submodule update --init --recursive

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSUNSHINE_ENABLE_TRAY=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-L/opt/homebrew/opt/icu4c@79/lib"

cmake --build build -j8
```

`-DSUNSHINE_ENABLE_TRAY=OFF` avoids Qt. The ICU flag is needed because
Boost.Locale statically links it and Homebrew's `icu4c` is keg-only.

The app is `build/Sunshine.app`.

### Component tests

The bridge builds and tests without Sunshine, OJD, or a gamepad:

```sh
cd third-party/nexus-ojd
cmake -S . -B build && cmake --build build
./build/ojd_client_test
```

30 checks: hashing against RFC vectors, JSON rendering, reply parsing, button and
axis mapping, and a full handshake against a fake endpoint.

## Differences from upstream

Nine files changed, ~150 lines:

| File | Change |
|---|---|
| `third-party/nexus-ojd/` | New: OJD endpoint client (25 files) |
| `src/platform/virtualhid_input.cpp` | Route gamepad calls to OJD when configured |
| `src/input.cpp` | `probe_gamepads()` recognises the OJD backend |
| `src/config.{h,cpp}` | `ojd_socket_path`, `ojd_token`; token redacted in logs |
| `cmake/compile_definitions/macos.cmake` | Build the component |
| `CMakeLists.txt` | Fork version |
| `src_assets/.../Home.vue` | Suppress the libvirtualhid warning when OJD is active |
| `src_assets/.../Inputs.vue` | Config UI for the two new options |
| `src_assets/.../locale/en.json` | Strings |

Upstream's `gamepad_driver` policy, ViGEmBus path, and libvirtualhid path are
untouched. Without OJD configured, behaviour is identical to upstream.

## Why not just disable SIP?

Because it does not work, and it is not necessary.

Measured on a real machine: signing a binary with the restricted entitlement and
launching it returns `Killed: 9` from AMFI **even after** `csrutil disable`,
because SIP and AMFI are separate layers. Disabling AMFI additionally requires
`amfi_get_out_of_my_way=1`, and on Apple silicon that means downgrading boot
policy to Permissive with `bputil -a` — a permanent, system-wide reduction in
security, which the tool itself warns can render a Mac unbootable.

Re-signing OJD's own app with a different certificate fails the same way: the
entitlement is bound to *their* team ID. There is no path to "just sign it
yourself."

Driving OJD's documented endpoint avoids all of it. SIP stays on.

## Credits

- [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) — the host this
  forks. GPL-3.0.
- [xsyetopz/OpenJoystickDriver](https://github.com/xsyetopz/OpenJoystickDriver) —
  the notarized virtual-HID broker and its endpoint SDK.
- [moonlight-stream](https://github.com/moonlight-stream) — the client protocol.

Upstream's original README is preserved as [README.upstream.md](README.upstream.md).

## License

GPL-3.0-or-later, inherited from Sunshine. The `third-party/nexus-ojd` component is
distributed under the same terms.

OpenJoystickDriver is licensed separately under
[PolyForm Noncommercial 1.0.0](https://github.com/xsyetopz/OpenJoystickDriver/blob/main/LICENSE).
This fork talks to it over its documented local API and does not include its
source. Noncommercial use is covered; commercial use needs a license from OJD's
author.

## Status

Working and verified end to end with a real Moonlight client on an iPhone, using a
Bluetooth Xbox controller. Not yet ready for a five-player session: four pads are
proven separately, but four *concurrent* clients have not been exercised, and
rumble is off.
