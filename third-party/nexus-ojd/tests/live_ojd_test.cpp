// Live integration check against a real OpenJoystickDriver endpoint.
//
// Uses the production FeedClient/PadSession (not a fake) and enumerates real HID
// devices while a pad exists, proving the frame actually reaches macOS.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

#include "nexus/ojd/pad_session.hpp"

using namespace nexus::ojd;

namespace {

std::string read_file(const char *path) {
  std::ifstream file(path);
  std::string value;
  std::getline(file, value);
  return value;
}

int count_gamepads() {
  FILE *pipe = ::popen(
      "/private/tmp/commandcode-501/-Users-srikanthamsa-dev-nexus/7864f97f-789d-43c8-84b6-e076db78258f/scratchpad/hidlist",
      "r");
  if (!pipe) return -1;
  char buffer[512];
  int count = -1;
  while (std::fgets(buffer, sizeof(buffer), pipe)) {
    std::printf("    %s", buffer);
    const char *marker = std::strstr(buffer, "devices: ");
    if (marker) {
      count = std::atoi(marker + 9);
    }
  }
  ::pclose(pipe);
  return count;
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
  config.max_fps = 120;
  config.keepalive_after = std::chrono::milliseconds(500);

  std::printf("1. Baseline HID enumeration (no pad):\n");
  const int before = count_gamepads();
  std::printf("   gamepads: %d\n\n", before);

  PadPool pool(config);

  GamepadState state;
  state.button_flags = moonlight::a | moonlight::dpad_up;
  state.lx = -24000;   // stick left
  state.rt = 255;      // right trigger fully pressed

  std::printf("2. Starting pad session...\n");
  const bool started = pool.update(0, state);
  std::printf("   started=%s last_error=%s\n\n", started ? "yes" : "NO",
              std::string(to_string(pool.last_error())).c_str());
  if (!started) {
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  std::printf("3. HID enumeration WITH pad alive:\n");
  const int during = count_gamepads();
  std::printf("   gamepads: %d\n\n", during);

  std::printf("4. Driving: A + DpadUp, stick left, RT full. Holding 2s...\n");
  for (int i = 0; i < 4; ++i) {
    pool.update(0, state);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  // Change state to prove updates flow.
  state.button_flags = moonlight::b;
  state.lx = 24000;
  state.rt = 0;
  std::printf("5. Changing to B, stick right, RT released...\n");
  for (int i = 0; i < 2; ++i) {
    pool.update(0, state);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  std::printf("6. Releasing pad...\n");
  pool.release_all();
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  const int after = count_gamepads();
  std::printf("   gamepads after release: %d\n\n", after);

  const bool pass = (before == 0) && (during >= 1) && (after == 0);
  std::printf("RESULT: %s (before=%d during=%d after=%d)\n",
              pass ? "PASS" : "FAIL", before, during, after);
  return pass ? 0 : 1;
}
