#pragma once

#include <cstdint>

namespace vgmc {
static constexpr uint32_t VGM_TICK_RATE{44100};

struct VgmHeader {
  uint32_t fileVersion;

  size_t gd3Index;
  size_t eofIndex;
  size_t dataIndex;

  uint32_t rate;
  uint32_t loopOffset;
  uint32_t loopSamples;
  uint32_t totalSamples;

  uint32_t sn76489Clock;
  uint32_t ym2612Clock;

  [[nodiscard]] auto ym2612Enabled() const noexcept -> bool {
    return ym2612Clock != 0;
  }

  [[nodiscard]] auto sn76489Enabled() const noexcept -> bool {
    return sn76489Clock != 0;
  }

  [[nodiscard]] auto getSamplesPerFrame() const noexcept -> uint32_t {
    return VGM_TICK_RATE / rate;
  }

  [[nodiscard]] auto getTotalLengthInFrames() const noexcept -> double {
    return static_cast<double>(totalSamples) /
           (static_cast<double>(VGM_TICK_RATE) / static_cast<double>(rate));
  }

  [[nodiscard]] auto tickToFrame(uint32_t tickPosition) const noexcept
      -> double {
    return static_cast<double>(tickPosition) /
           (VGM_TICK_RATE / static_cast<double>(rate));
  }

  [[nodiscard]] auto loopEnabled() const noexcept -> bool {
    return loopOffset != 0 or loopSamples != 0;
  }

  [[nodiscard]] auto getLoopFrame() const noexcept -> double {
    return static_cast<double>(totalSamples - loopSamples) /
           static_cast<double>(getSamplesPerFrame());
  }
};
}  // namespace vgmc
