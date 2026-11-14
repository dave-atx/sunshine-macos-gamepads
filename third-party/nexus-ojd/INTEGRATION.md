# Applying this to a Sunshine fork

Three edits. Total added code in Sunshine's own tree: ~15 lines.

## 1. Vendor the component

```sh
cp -R sunshine_ojd /path/to/Sunshine/third-party/nexus-ojd
```

## 2. Build it

In Sunshine's `CMakeLists.txt`, after the main `sunshine` target exists:

```cmake
add_subdirectory(third-party/nexus-ojd)
target_link_libraries(sunshine PRIVATE ojd_client)
target_include_directories(sunshine PRIVATE third-party/nexus-ojd/include)
```

`sunshine_ojd/CMakeLists.txt` builds the portable core as `ojd_client`. The
adapter TU (`src/sunshine_adapter.cpp`) is compiled separately because it needs
Sunshine's headers:

```cmake
configure_file(
  third-party/nexus-ojd/src/sunshine_adapter.cpp
  ${CMAKE_BINARY_DIR}/nexus_sunshine_adapter.cpp COPYONLY)
list(APPEND PLATFORM_TARGET_FILES "${CMAKE_BINARY_DIR}/nexus_sunshine_adapter.cpp")
```

## 3. Route macOS gamepad calls

In `src/platform/virtualhid_input.cpp`, inside the existing `#ifndef _WIN32`
block, add the include near the other local includes:

```cpp
#if defined(__APPLE__)
  #include "nexus/ojd/sunshine_bridge.hpp"
#endif
```

Then make the four gamepad entry points prefer OJD when it is configured. Keep
libvirtualhid as the fallback so a licensed install still works:

```cpp
int alloc_gamepad(input_t &input, const gamepad_id_t &id,
                  const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
#if defined(__APPLE__)
  if (nexus::ojd::sunshine::global_bridge().availability() ==
      nexus::ojd::sunshine::Availability::ready) {
    if (nexus::ojd::sunshine::alloc_gamepad(id.globalIndex) == 0) {
      return 0;
    }
  }
#endif
  return virtualhid::alloc_gamepad(virtualhid::get_input_context(input), id,
                                   metadata, std::move(feedback_queue));
}

void free_gamepad(input_t &input, int nr) {
#if defined(__APPLE__)
  nexus::ojd::sunshine::free_gamepad(nr);
#endif
  virtualhid::free_gamepad(virtualhid::get_input_context(input), nr);
}

void gamepad_update(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
#if defined(__APPLE__)
  if (nexus::ojd::sunshine::submit_gamepad(nr, gamepad_state)) {
    return;
  }
#endif
  virtualhid::gamepad_update(virtualhid::get_input_context(input), nr, gamepad_state);
}
```

`rebind_gamepad` needs no change: OJD pads are created on demand and rebound by
their slot number.

## 4. Configure the endpoint

At session start, once, before the first gamepad packet:

```cpp
nexus::ojd::sunshine::global_bridge().configure(socket_path, token, token_name);
```

Sources for the three values:

```sh
SOCK=$(ojd --json access status | jq -r .socketPath)
ojd access grant --token sunshine --scope read --scope control
```

The socket path is per-user and lives under `$TMPDIR`, so read it at runtime
rather than hardcoding it. The token is a secret: store it like a password and
never log it.

Ideally this becomes two Sunshine config options (`ojd_socket_path`,
`ojd_token`). That plumbing is not done; see `COLLABORATION.md`.

## Verifying the integration

```sh
# Unit tests, no OJD required
./build/ojd_client_test

# Live: needs OJD installed, endpoint on, token with control scope
./build/live_ojd_test "$SOCK" "$TOKEN"
./build/live_input_test "$SOCK" "$TOKEN"
```

Then connect a Moonlight client with a controller and check the host under
**System Settings > Game Controllers**, or run `ojd controller list`.

## Rollback

Remove the three `#if defined(__APPLE__)` branches and the
`add_subdirectory`. The component is additive: nothing else in Sunshine is
modified, and libvirtualhid remains the fallback path.
