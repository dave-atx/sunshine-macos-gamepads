// Declarations for the Sunshine-side OJD adapter.
//
// Kept separate from src/sunshine_adapter.cpp so Sunshine's input backend can
// call these without including Sunshine headers itself. The definitions live in
// the adapter TU, which does include src/platform/common.h.
#pragma once

#include <cstdint>

// Forward declaration keeps this header usable from Sunshine's backend.
namespace platf {
struct gamepad_state_t;
}

namespace nexus::ojd::sunshine {

/// Allocate a virtual pad in `slot`. Returns 0 on success, -1 when the backend
/// is unavailable or the slot is out of range.
int alloc_gamepad(int slot);

/// Release the virtual pad in `slot`. Safe to call for a slot with no pad.
void free_gamepad(int slot);

/// Submit a gamepad state. Returns true when the OJD backend handled it, false
/// when the caller should fall back to libvirtualhid.
bool submit_gamepad(int slot, const platf::gamepad_state_t &state);

}  // namespace nexus::ojd::sunshine
