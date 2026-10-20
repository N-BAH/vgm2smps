#pragma once

#include <array>
#include <cstdint>

namespace vgmc::chips {
class SN76489 {
 public:
  static constexpr size_t PSG_CHANNELS_COUNT{3};
  static constexpr size_t CHANNELS_COUNT{PSG_CHANNELS_COUNT + 1};
  static constexpr size_t NOISE_CHANNEL_INDEX{PSG_CHANNELS_COUNT};
  static constexpr uint8_t MIN_VOLUME{15};

  struct Channel {
    uint8_t volume{MIN_VOLUME};
    uint16_t tone{0};

    auto operator==(const Channel& other) const -> bool = default;
  };

  struct NoiseChannel : public Channel {
    // If false, it's a periodic type noise, else white. Pon?
    bool whiteNoiseBit;
    bool fullRangeNoiseMode;

    auto operator==(const NoiseChannel& other) const -> bool = default;
  };

  struct Context {
    std::array<Channel, CHANNELS_COUNT> psg;
    NoiseChannel noise;
    auto operator==(const Context& other) const -> bool = default;
  };

  SN76489(uint32_t clock);

  void write(uint8_t reg);

  // TODO: get state etc
  [[nodiscard]] auto getContext() const noexcept -> const Context& {
    return m_context;
  }
  [[nodiscard]] auto getClock() const noexcept { return m_clock; }
  [[nodiscard]] auto convertToHz(uint16_t tone) const noexcept -> double;
  [[nodiscard]] static auto convertToHz(uint32_t clock, uint16_t tone) noexcept
      -> double;

 private:
  static constexpr uint8_t VOLUME_MASK{0b1111};

  uint32_t m_clock;
  bool m_latchVolumeBit{false};
  size_t m_selectedChannelIndex{};
  Context m_context{};

  void processLatchRegister(uint8_t reg);
  void processDataRegister(uint8_t reg);

  static auto isNoiseChannel(size_t channelIndex) noexcept -> bool;
};
}  // namespace vgmc::chips
