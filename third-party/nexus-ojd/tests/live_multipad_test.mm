// Live check: 4 simultaneous virtual pads and rumble, against real OJD.
//
// Answers the two questions that matter for a 5-player session: can the bridge
// hold 4 pads at once, and does rumble reach the client for each pad.
#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hid/IOHIDDevice.h>
#import <IOKit/hid/IOHIDKeys.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>

#include "nexus/ojd/pad_session.hpp"

using namespace nexus::ojd;

namespace {

std::atomic<int> g_rumble_count{0};
std::mutex g_rumble_mutex;
std::vector<int> g_rumble_slots;

int count_gamepads() {
  IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  NSArray *matching = @[ @{@kIOHIDPrimaryUsagePageKey: @1, @kIOHIDPrimaryUsageKey: @5} ];
  IOHIDManagerSetDeviceMatchingMultiple(mgr, (__bridge CFArrayRef)matching);
  IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
  NSSet *devices = CFBridgingRelease(IOHIDManagerCopyDevices(mgr));
  const int count = devices ? (int)devices.count : 0;
  IOHIDManagerClose(mgr, kIOHIDOptionsTypeNone);
  CFRelease(mgr);
  return count;
}

/// Write a GIP rumble report to the Nth gamepad device.
/// The buffer must include the report ID byte, so the length is 13, not 12.
bool send_rumble(int index, uint8_t left, uint8_t right) {
  IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  NSArray *matching = @[ @{@kIOHIDPrimaryUsagePageKey: @1, @kIOHIDPrimaryUsageKey: @5} ];
  IOHIDManagerSetDeviceMatchingMultiple(mgr, (__bridge CFArrayRef)matching);
  IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
  NSSet *devices = CFBridgingRelease(IOHIDManagerCopyDevices(mgr));

  bool ok = false;
  int position = 0;
  for (id entry in devices) {
    if (position++ != index) continue;
    IOHIDDeviceRef device = (__bridge IOHIDDeviceRef)entry;
    IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);

    uint8_t report[13] = {0x09, 0x00, 0x00, 0x09, 0x00, 0x03,
                          0x00, 0x00, left, right, 0x19, 0x00, 0xEB};
    const IOReturn result = IOHIDDeviceSetReport(
        device, kIOHIDReportTypeOutput, 9, report, sizeof(report));
    ok = result == kIOReturnSuccess;
    break;
  }

  IOHIDManagerClose(mgr, kIOHIDOptionsTypeNone);
  CFRelease(mgr);
  return ok;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::printf("usage: %s <socket> <token>\n", argv[0]);
    return 2;
  }

  std::printf("1. Baseline pads: %d\n", count_gamepads());

  SessionConfig config;
  config.socket_path = argv[1];
  config.token = argv[2];
  config.token_name = "probe";
  config.persona = Persona::sdl_compatible;
  config.max_fps = 60;
  config.max_pads = 4;
  config.on_rumble = [](int slot, const RumbleCommand &command) {
    std::lock_guard lock(g_rumble_mutex);
    g_rumble_slots.push_back(command.set ? command.low_frequency : -1);
    ++g_rumble_count;
  };

  PadPool pool(config);

  std::printf("2. Starting 4 pads...\n");
  int started = 0;
  for (int slot = 0; slot < 4; ++slot) {
    GamepadState state;
    // A different button per slot, so the pads are distinguishable.
    state.button_flags = 1u << (12 + slot);
    if (pool.update(slot, state)) {
      ++started;
    } else {
      std::printf("   slot %d FAILED: %s\n", slot,
                  std::string(to_string(pool.last_error())).c_str());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
  }
  std::printf("   pads started: %d/4\n", started);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  const int live = count_gamepads();
  std::printf("3. macOS sees %d gamepads\n", live);

  std::printf("4. Sending rumble to each pad...\n");
  for (int index = 0; index < live; ++index) {
    const bool ok = send_rumble(index, 200 + index, 100 + index);
    std::printf("   pad %d rumble write: %s\n", index, ok ? "OK" : "FAIL");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(700));
  std::printf("5. Rumble commands received: %d\n", g_rumble_count.load());

  std::printf("6. Releasing all pads...\n");
  pool.release_all();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  const int after = count_gamepads();
  std::printf("   pads after release: %d\n", after);

  const bool pass = (started == 4) && (live == 4) && (after == 0) &&
                    (g_rumble_count.load() > 0);
  std::printf("\nRESULT: %s (started=%d live=%d after=%d rumble=%d)\n",
              pass ? "PASS" : "FAIL", started, live, after, g_rumble_count.load());
  return pass ? 0 : 1;
}
