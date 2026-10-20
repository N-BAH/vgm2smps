#include "SN76489.hxx"

namespace vgmc::chips {
SN76489::SN76489(uint32_t clock) : m_clock{clock} {}

void SN76489::write(uint8_t reg) {
  if (const bool latchBit{(reg & 0b1000'0000) != 0}; latchBit) {
    processLatchRegister(reg);
  } else {
    processDataRegister(reg);
  }
}

auto SN76489::convertToHz(uint32_t clock, uint16_t tone) noexcept -> double {
  // TODO: explain magic constant "32"
  constexpr uint8_t SN76489_MAGIC{32};
  return tone == 0 ? 0.0 : (clock / static_cast<double>(SN76489_MAGIC)) / tone;
}

void SN76489::processLatchRegister(uint8_t reg) {
  // Interpret reg as latch
  // %1cctdddd
  //   |||````-- Data
  //   ||`------ Type
  //   ``------- Channel
  const uint8_t data(reg & VOLUME_MASK);

  m_selectedChannelIndex = (reg >> 5) & 0b11;
  m_latchVolumeBit = (reg & 0b1'0000) != 0;

  if (isNoiseChannel(m_selectedChannelIndex)) {
    NoiseChannel& channel{m_context.noise};

    if (m_latchVolumeBit) {
      // Volume
      channel.volume = data;
    } else {
      // Tone/Noise
      static constexpr int NOISE_CH2_TONE_MODE{0b11};
      const auto tone{data & 0b11};

      channel.whiteNoiseBit = ((data & 0b100) != 0);

      if (tone == NOISE_CH2_TONE_MODE) {
        channel.tone = 0;
        channel.fullRangeNoiseMode = true;
      } else {
        channel.tone = tone;
        channel.fullRangeNoiseMode = false;
      }
    }

  } else {
    Channel& channel{m_context.psg[m_selectedChannelIndex]};

    if (m_latchVolumeBit) {
      // Volume
      channel.volume = data;
    } else {
      // Tone/Noise
      channel.tone = data;
    }
  }
}

void SN76489::processDataRegister(uint8_t reg) {
  // Interpret reg as data
  // %0-DDDDDD
  //   |``````-- Data
  //   `-------- Unused
  const uint8_t data(reg & 0b11'1111);
  const bool ch3SubstituteMode{m_selectedChannelIndex == 2 and
                               m_context.noise.fullRangeNoiseMode and
                               not m_latchVolumeBit};

  if (isNoiseChannel(m_selectedChannelIndex) or ch3SubstituteMode) {
    NoiseChannel& channel{m_context.noise};

    if (m_latchVolumeBit) {
      channel.volume = data & VOLUME_MASK;
    } else {
      // Tone/Noise
      if (ch3SubstituteMode) {
        Channel& channel2{m_context.psg[PSG_CHANNELS_COUNT - 1]};
        channel2.tone |= ((data << 4) & 0x3FF);
        channel.tone = channel2.tone;
        channel2.tone = 0;
      } else {
        channel.whiteNoiseBit = ((data & 0b100) != 0);
        channel.tone = data & 0b11;
      }
    }
  } else {
    Channel& channel{m_context.psg[m_selectedChannelIndex]};

    if (m_latchVolumeBit) {
      channel.volume = data & VOLUME_MASK;
    } else {
      // Tone/Noise
      channel.tone |= ((data << 4) & 0x3FF);
    }
  }
}

auto SN76489::isNoiseChannel(size_t channelIndex) noexcept -> bool {
  return channelIndex == NOISE_CHANNEL_INDEX;
}
}  // namespace vgmc::chips
