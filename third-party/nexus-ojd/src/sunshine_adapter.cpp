// Sunshine adapter: the only translation unit that includes Sunshine headers.
//
// It implements the gamepad entry points Sunshine's macOS backend needs when
// libvirtualhid cannot create pads (no Apple-approved HID entitlement), by
// forwarding to an OpenJoystickDriver endpoint instead.
//
// The four functions here mirror the ones in src/platform/virtualhid_input.cpp
// under the `#ifndef _WIN32` block. See INTEGRATION.md for the patch.
#include "nexus/ojd/sunshine_adapter.hpp"
#include "nexus/ojd/sunshine_bridge.hpp"

// Sunshine. Included after the bridge so this file fails loudly if the bridge
// header ever grows a Sunshine dependency.
#include "src/platform/common.h"

namespace nexus::ojd::sunshine {

// Pin the duplicated Moonlight constants to Sunshine's real ones. If upstream
// renumbers a button, the build fails here instead of silently misrouting input.
namespace {
static_assert(moonlight::dpad_up == platf::DPAD_UP);
static_assert(moonlight::dpad_down == platf::DPAD_DOWN);
static_assert(moonlight::dpad_left == platf::DPAD_LEFT);
static_assert(moonlight::dpad_right == platf::DPAD_RIGHT);
static_assert(moonlight::start == platf::START);
static_assert(moonlight::back == platf::BACK);
static_assert(moonlight::left_stick == platf::LEFT_STICK);
static_assert(moonlight::right_stick == platf::RIGHT_STICK);
static_assert(moonlight::left_button == platf::LEFT_BUTTON);
static_assert(moonlight::right_button == platf::RIGHT_BUTTON);
static_assert(moonlight::home == platf::HOME);
static_assert(moonlight::a == platf::A);
static_assert(moonlight::b == platf::B);
static_assert(moonlight::x == platf::X);
static_assert(moonlight::y == platf::Y);
static_assert(moonlight::paddle1 == platf::PADDLE1);
static_assert(moonlight::paddle2 == platf::PADDLE2);
static_assert(moonlight::paddle3 == platf::PADDLE3);
static_assert(moonlight::paddle4 == platf::PADDLE4);
static_assert(moonlight::touchpad_button == platf::TOUCHPAD_BUTTON);
static_assert(moonlight::misc_button == platf::MISC_BUTTON);
}  // namespace

/// Sunshine entry point: allocate a virtual pad in `id.globalIndex`.
int alloc_gamepad(int slot) {
  // OJD creates the pad on the first frame, so allocation only records intent.
  // A failure surfaces on the first update, which is where OJD reports
  // permission and license problems.
  if (global_bridge().availability() != Availability::ready) {
    return -1;
  }
  if (slot < 0 || slot >= kMaxVirtualPads) {
    return -1;
  }
  return 0;
}

/// Sunshine entry point: release a virtual pad.
void free_gamepad(int slot) {
  global_bridge().release(slot);
}

/// Sunshine entry point: submit a full gamepad state.
bool submit_gamepad(int slot, const platf::gamepad_state_t &state) {
  return global_bridge().update(
      slot, state.buttonFlags, state.lt, state.rt,
      state.lsX, state.lsY, state.rsX, state.rsY);
}

// Rumble is intentionally not wired to Sunshine's feedback queue yet.
//
// The obstacle is scope, not protocol: OJD reports rumble per pad, and the
// protocol layer parses it correctly, but Sunshine has one feedback queue per
// Moonlight session while these pads are process-global. The bridge therefore
// cannot tell which session owns a pad, and with several viewers connected a
// rumble for one player would be delivered to another (or to a queue that was
// already destroyed when its client disconnected).
//
// Wiring it safely needs the bridge to learn each session's gamepad slots and
// queue, which means plumbing session identity through the platform input
// entry points. Until that exists, rumble stays off for every pad rather than
// routed to the wrong controller. See COLLABORATION.md.

}  // namespace nexus::ojd::sunshine
