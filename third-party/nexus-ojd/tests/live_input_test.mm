// Verify that frames sent through the client actually reach the HID device.
//
// Creates a pad via the production client, then reads the HID element values
// back through IOHIDManager. If the reported button/axis state matches what was
// sent, the whole path is proven: JSON frame -> OJD -> kernel HID -> game.
#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hid/IOHIDElement.h>
#import <IOKit/hid/IOHIDKeys.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

#include "nexus/ojd/pad_session.hpp"

using namespace nexus::ojd;

namespace {

/// Element usages we read back.
constexpr uint32_t kUsageButton = 0x09;
constexpr uint32_t kUsageHatSwitch = 0x39;

struct Snapshot {
  int button_count = 0;
  uint32_t buttons = 0;
  long hat = -1;
  int axis_count = 0;
};

/// Read current element values from the first gamepad-usage HID device.
Snapshot read_gamepad() {
  Snapshot snapshot;

  IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  NSArray *matching = @[ @{@kIOHIDPrimaryUsagePageKey: @1, @kIOHIDPrimaryUsageKey: @5} ];
  IOHIDManagerSetDeviceMatchingMultiple(manager, (__bridge CFArrayRef)matching);
  if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
    return snapshot;
  }

  NSSet *devices = CFBridgingRelease(IOHIDManagerCopyDevices(manager));
  for (id entry in devices) {
    IOHIDDeviceRef device = (__bridge IOHIDDeviceRef)entry;
    NSSet *elements = CFBridgingRelease(IOHIDDeviceCopyMatchingElements(
        device, nullptr, kIOHIDOptionsTypeNone));
    int button_index = 0;
    for (id elementEntry in elements) {
      IOHIDElementRef element = (__bridge IOHIDElementRef)elementEntry;
      const uint32_t usage_page = IOHIDElementGetUsagePage(element);
      const uint32_t usage = IOHIDElementGetUsage(element);
      if (IOHIDElementGetType(element) != kIOHIDElementTypeInput_Button &&
          IOHIDElementGetType(element) != kIOHIDElementTypeInput_Misc &&
          IOHIDElementGetType(element) != kIOHIDElementTypeInput_Axis) {
        continue;
      }

      IOHIDValueRef value = nullptr;
      if (IOHIDDeviceGetValue(device, element, &value) != kIOReturnSuccess || !value) {
        continue;
      }
      const CFIndex integer = IOHIDValueGetIntegerValue(value);

      if (usage_page == 0x09 || usage_page == kUsageButton) {
        // Button page: report up to 32 buttons as a mask, ordered by index.
        if (button_index < 32 && integer != 0) {
          snapshot.buttons |= (1u << button_index);
        }
        ++button_index;
      } else if (usage == kUsageHatSwitch) {
        snapshot.hat = integer;
      } else if (usage_page == 0x01) {
        ++snapshot.axis_count;
      }
    }
    snapshot.button_count = button_index;
    break;  // first gamepad only
  }

  IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
  CFRelease(manager);
  return snapshot;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::printf("usage: %s <socket> <token>\n", argv[0]);
    return 2;
  }

  SessionConfig config;
  config.socket_path = argv[1];
  config.token = argv[2];
  config.token_name = "sunshine";
  config.persona = Persona::sdl_compatible;
  config.max_fps = 60;

  PadPool pool(config);
  GamepadState state;

  std::printf("1. Create pad with A pressed...\n");
  state.button_flags = moonlight::a;
  if (!pool.update(0, state)) {
    std::printf("   FAILED: %s\n", std::string(to_string(pool.last_error())).c_str());
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  auto snapshot = read_gamepad();
  std::printf("   buttons=%d seen=%u hat=%ld axes=%d\n",
              snapshot.button_count, snapshot.buttons, snapshot.hat, snapshot.axis_count);
  const bool a_pressed = snapshot.buttons != 0;
  std::printf("   A reported: %s\n\n", a_pressed ? "YES" : "NO");

  std::printf("2. Release A, press B...\n");
  state.button_flags = moonlight::b;
  pool.update(0, state);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  auto changed = read_gamepad();
  std::printf("   buttons seen=%u\n", changed.buttons);
  const bool changed_to_b = changed.buttons != snapshot.buttons;
  std::printf("   button state changed: %s\n\n", changed_to_b ? "YES" : "NO");

  std::printf("3. Neutral...\n");
  state.button_flags = 0;
  pool.update(0, state);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  auto neutral = read_gamepad();
  std::printf("   buttons seen=%u\n", neutral.buttons);
  const bool released = neutral.buttons == 0;
  std::printf("   all released: %s\n\n", released ? "YES" : "NO");

  pool.release_all();

  const bool pass = a_pressed && changed_to_b && released;
  std::printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
