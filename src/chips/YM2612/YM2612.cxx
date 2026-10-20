#include "YM2612.hxx"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>

namespace vgmc::chips {
YM2612::YM2612(uint32_t clock) : m_clock{clock} {}

auto YM2612::Instrument::getSlotMask() const -> std::bitset<OPERATORS_COUNT> {
  static constexpr std::array<std::bitset<OPERATORS_COUNT>, 8> slotMap{
      0b1000, 0b1000, 0b1000, 0b1000, 0b1010, 0b1110, 0b0111, 0b1111};
  return slotMap[algorithm];
}

bool YM2612::Instrument::sameTimbreAs(const Instrument& other) const noexcept {
  if (algorithm != other.algorithm or feedback != other.feedback) {
    return false;
  }

  // Based on algorithm parameter
  const std::bitset<OPERATORS_COUNT> slotMask{getSlotMask()};

  // Check if non-slot operators has different volumes
  if ((!slotMask.test(0) and (operators[0].tl != other.operators[0].tl)) or
      (!slotMask.test(1) and (operators[1].tl != other.operators[1].tl)) or
      (!slotMask.test(2) and (operators[2].tl != other.operators[2].tl))) {
    return false;
  }

  for (size_t i = 0; i < OPERATORS_COUNT; ++i) {
    const auto& op1{operators[i]};
    const auto& op2{other.operators[i]};

    if ((op1.ar != op2.ar) or (op1.d1r != op2.d1r) or (op1.d2r != op2.d2r) or
        (op1.rr != op2.rr) or (op1.sl != op2.sl) or (op1.mul != op2.mul) or
        (op1.dt != op2.dt) or (op1.ssgeg != op2.ssgeg) or (op1.rs != op2.rs)) {
      return false;
    }
  }

  return true;
}

auto YM2612::Instrument::getVolume() const noexcept -> int8_t {
  return operators[getLoudestSlotIndex(getSlotMask())].tl;
}

auto YM2612::Instrument::makeNormalized() const noexcept -> Instrument {
  Instrument result{*this};
  std::bitset slotMask{result.getSlotMask()};
  const size_t loudestSlotIndex{result.getLoudestSlotIndex(slotMask)};
  Operator& loudestSlot{result.operators[loudestSlotIndex]};
  const int8_t volume{loudestSlot.tl};

  loudestSlot.tl = 0;
  slotMask.reset(loudestSlotIndex);

  for (size_t i = 0; i < OPERATORS_COUNT; ++i) {
    if (slotMask.test(i)) {
      result.operators[i].tl =
          static_cast<int8_t>(result.operators[i].tl - volume);
    }
  }

  return result;
}

auto YM2612::Instrument::upOctave() const noexcept -> Instrument {
  Instrument result{*this};

  for (Operator& operator_ : result.operators) {
    if (operator_.mul == 0) {
      operator_.mul = 1;
    } else if (operator_.mul != Operator::MAX_MUL) {
      operator_.mul =
          std::min(static_cast<uint8_t>(operator_.mul * 2), Operator::MAX_MUL);
    }
  }

  return result;
}

auto YM2612::Instrument::downOctave() const noexcept -> Instrument {
  Instrument result{*this};

  for (Operator& operator_ : result.operators) {
    if (operator_.mul <= 1) {
      operator_.mul = 0;
    } else {
      operator_.mul /= 2;
    }
  }

  return result;
}

auto YM2612::Instrument::canUpOctaveSafely() const noexcept -> bool {
  return std::ranges::all_of(operators, [](const Operator& operator_) -> bool {
    return operator_.mul != Operator::MAX_MUL;
  });
}

auto YM2612::Instrument::canDownOctaveSafely() const noexcept -> bool {
  return std::ranges::all_of(operators, [](const Operator& operator_) -> bool {
    return operator_.mul != Operator::MIN_MUL;
  });
}

auto YM2612::Instrument::isFullSilent() const noexcept -> bool {
  return std::ranges::all_of(operators, [](const Operator& operator_) -> bool {
    return operator_.tl == Operator::MAX_TL;
  });
}

auto YM2612::Instrument::getLoudestSlotIndex(
    const std::bitset<OPERATORS_COUNT>& slotMask) const noexcept -> size_t {
  size_t result{OPERATORS_COUNT - 1};

  if (slotMask.test(2) and operators[2].tl < operators[result].tl) {
    result = 2;
  }

  if (slotMask.test(1) and operators[1].tl < operators[result].tl) {
    result = 1;
  }

  if (slotMask.test(0) and operators[0].tl < operators[result].tl) {
    result = 0;
  }

  return result;
}

auto YM2612::operatorNumberToIndex(uint8_t operatorNumber) -> uint8_t {
  if (operatorNumber == 1) {
    return 2;
  }

  if (operatorNumber == 2) {
    return 1;
  }

  return operatorNumber;
}

void YM2612::writePort0(WriteData writeData) {
  constexpr uint8_t port{0};
  const uint8_t regHi{static_cast<uint8_t>(writeData.reg & 0xF0)};
  const uint8_t regLo{static_cast<uint8_t>(writeData.reg & 0x0F)};

  // Reg regEnum{writeData.reg};

  if (writeData.reg == 0x22) {
    // LFO
    processLfoChange(writeData);
  } else if (writeData.reg == 0x27) {
    // Timers; Ch3 / 6 mode
    processCh3Change(writeData);
  } else if (writeData.reg == 0x28) {
    // Key on/off
    processKeyOnOff(writeData);
  } else if (writeData.reg == 0x2A) {
    // DAC data
    processDacData(writeData);
  } else if (writeData.reg == 0x2B) {
    // DAC enable/disable
    processDacSetState(writeData);
  } else if (writeData.reg >= 0x30 && writeData.reg < 0xA0) {
    const size_t channelIndex(regLo % 4);
    const size_t operatorIndex{operatorNumberToIndex(regLo / 4)};
    m_changeTrackingFlags.channelsChanged.set(channelIndex);
    processOperatorRegister(writeData,
                            m_context.channels.at(channelIndex)
                                .instrument.operators.at(operatorIndex));
  } else if (writeData.reg >= 0xA0 && writeData.reg < 0xA8) {
    // frequency
    processFrequencyChange(writeData, port);
  } else if (writeData.reg >= 0xB0 && writeData.reg < 0xB4) {
    // feedback; algorithm
    processFeedbackAlgorithmSettings(writeData, port);
  } else if (writeData.reg >= 0xB4 && writeData.reg < 0xC0) {
    // stereo; LFO sensitivity
    processPanLfoSettings(writeData, port);
  } else {
    // TODO: log warning about unknown command
  }
}

void YM2612::writePort1(WriteData writeData) {
  constexpr uint8_t port{1};
  const uint8_t regLo{static_cast<uint8_t>(writeData.reg & 0x0F)};

  if (writeData.reg >= 0x30 && writeData.reg < 0xA0) {
    constexpr uint8_t CHANNEL_OFFSET{3};
    const size_t channelIndex(std::min((regLo & 0b11) + CHANNEL_OFFSET, 5));
    const size_t operatorIndex{operatorNumberToIndex(regLo / 0x04)};
    processOperatorRegister(writeData,
                            m_context.channels.at(channelIndex)
                                .instrument.operators.at(operatorIndex));
    m_changeTrackingFlags.channelsChanged.set(channelIndex);
  } else if (writeData.reg >= 0xA0 && writeData.reg < 0xA8) {
    // frequency
    processFrequencyChange(writeData, port);
  } else if (writeData.reg >= 0xB0 && writeData.reg < 0xB4) {
    // B0H+ - feedback; algorithm
    processFeedbackAlgorithmSettings(writeData, port);
  } else if (writeData.reg >= 0xB4 && writeData.reg < 0xC0) {
    // B4H+ - stereo; LFO sensitivity
    processPanLfoSettings(writeData, port);
  } else {
    // TODO: log warning about unknown command
  }
}

auto YM2612::convertToHz(uint8_t block, uint16_t frequency) const -> double {
  return convertToHz(m_clock, block, frequency);
}

auto YM2612::convertToHz(uint32_t clock, uint8_t block, uint16_t frequency)
    -> double {
  // TODO: Explain this magic constants (22 and 72)
  constexpr uint32_t CLOCK_BIT_MASK{0x3FFFFFFF};
  constexpr auto MAGIC1{72};
  constexpr auto MAGIC2{22};
  const auto fsam2612 = (clock & CLOCK_BIT_MASK) / MAGIC1;
  return frequency * fsam2612 * std::pow(2, (block - MAGIC2));
}

void YM2612::processLfoChange(WriteData writeData) {
  m_context.lfoEnabled = (writeData.reg & 0b1000) != 0;
  m_context.lfoFrequency = writeData.reg & 0b0111;
  m_changeTrackingFlags.globalChanged = true;
}

void YM2612::processCh3Change(WriteData writeData) {
  // D6 & D7 bits
  const uint8_t neededBits{static_cast<uint8_t>((writeData.val >> 4) & 0b11)};

  if (neededBits != 0b10 && neededBits != 0b11) {
    m_context.channel3SpecialModeEnabled = (neededBits & 1) != 0;

    if (m_context.channel3SpecialModeEnabled and
        not m_warningMessagesToggles.ch3SpecialNotSupported) {
      SPDLOG_WARN(
          "Channel 3 special mode is not supported, but it is used in the "
          "track!");
      m_warningMessagesToggles.ch3SpecialNotSupported = true;
    }
  } else {
    // incorrect ch3 settings
  }

  m_changeTrackingFlags.globalChanged = true;
}

void YM2612::processKeyOnOff(WriteData writeData) {
  // Create normal channel index according to table:
  // D2 D1 D0 Channel
  // 0  0  0  1
  // 0  0  1  2
  // 0  1  0  3
  // 1  0  0  4
  // 1  0  1  5
  // 1  1  0  6
  const uint8_t channelIndex(std::min(
      (writeData.val & 0b11) + ((writeData.val & 0b100) != 0 ? 3 : 0), 5));

  Channel& channel{m_context.channels.at(channelIndex)};

  // Update operators state according to table:
  //     D7 D6 D5 D4 D3 D2 D1 D0
  // 28H Operator       Channel
  //     4  3  2  1
  const bool prevIsKeyOn{channel.isKeyOn()};
  channel.operatorEnabled = std::bitset<OPERATORS_COUNT>{
      static_cast<unsigned long long>(writeData.val >> 4)};

  if (channel.isKeyOn() and not prevIsKeyOn) {
    m_changeTrackingFlags.attackDetected.set(channelIndex);
  }

  m_changeTrackingFlags.channelsChanged.set(channelIndex);
}

void YM2612::processDacData(WriteData writeData) {
  m_context.dacValue = writeData.val;
  m_changeTrackingFlags.dacValueChanged = true;
}

void YM2612::processDacSetState(WriteData writeData) {
  const auto prevValue{m_context.dacEnabled};
  m_context.dacEnabled = (writeData.val & 0b1000'0000) != 0;
  m_changeTrackingFlags.dacEnabledChanged = m_context.dacEnabled != prevValue;
}

void YM2612::processOperatorRegister(WriteData writeData, Operator& operator_) {
  const uint8_t regHi{static_cast<uint8_t>(writeData.reg & 0xF0)};

  switch (regHi) {
    case 0x30: {
      // detune & multiple
      const uint8_t detuneRaw((writeData.val >> 4) & 0b111);
      operator_.dt = static_cast<int8_t>((detuneRaw & 0b011) *
                                         ((detuneRaw & 0b100) != 0 ? -1 : 1));
      operator_.mul = writeData.val & 0b1111;
      break;
    }

    case 0x40:
      // total level
      operator_.tl = static_cast<int8_t>(writeData.val & 0b0111'1111);
      break;

    case 0x50:
      // rate scaling; attack rate
      operator_.rs = writeData.val >> 6;
      operator_.ar = writeData.val & 0b1'1111;
      break;

    case 0x60:
      // first decay rate; amplitude modulation
      operator_.d1r = writeData.val & 0b0001'1111;
      operator_.am = (writeData.val & 0b1000'0000) != 0;
      break;

    case 0x70:
      // secondary decay rate
      operator_.d2r = writeData.val & 0b0001'1111;
      break;

    case 0x80:
      // secondary amplitude; release rate
      operator_.sl = writeData.val >> 4;
      operator_.rr = writeData.val & 0b1111;
      break;

    case 0x90:
      // proprietary (SSG-EG)
      operator_.ssgeg = writeData.val & 0b1111;
      break;

    default:
      break;
  }
}

void YM2612::processFrequencyChange(WriteData writeData, uint8_t port) {
  const uint8_t regHi{static_cast<uint8_t>(writeData.reg & 0xF0)};
  const uint8_t regLo{static_cast<uint8_t>(writeData.reg & 0x0F)};
  // A0H+	Frequency number LSB
  // 0b000 = channel base + 0
  // 0b001 = channel base + 1
  // 0b010 = channel base + 2
  //
  // A4H+		Block	Frequency number MSB
  // 0b100 = channel base + 0
  // 0b101 = channel base + 1
  // 0b110 = channel base + 1

  static constexpr uint8_t MSB_CHANNEL_OFFSET{4};
  static constexpr uint8_t PORT1_CHANNEL_OFFSET{3};

  if ((regLo > 2 and regLo < MSB_CHANNEL_OFFSET) or
      (regLo > MSB_CHANNEL_OFFSET + 2)) {
    return;
  }

  const bool isMsbPart{regLo >= MSB_CHANNEL_OFFSET};
  const size_t channelIndex((isMsbPart ? (regLo - MSB_CHANNEL_OFFSET) : regLo) +
                            (port != 0 ? PORT1_CHANNEL_OFFSET : 0));

  if (channelIndex == 2 and m_context.channel3SpecialModeEnabled) {
    return;
  }

  Channel& channel{m_context.channels.at(channelIndex)};

  // Check at compile time that we safely can iterate over arrays by
  // OPERATORS_COUNT range
  static_assert(std::tuple_size_v<decltype(channel.tone)> == OPERATORS_COUNT);

  // TODO: impl Channel 3 Special Mode if needed
  if (isMsbPart) {
    // A4H+		Block	Frequency number MSB
    for (size_t i = 0; i < OPERATORS_COUNT; ++i) {
      const uint16_t msb(writeData.val & 0b111);
      const uint16_t block((writeData.val >> 3) & 0b111);
      Tone& tone{channel.tone[i]};
      tone.frequency =
          (tone.frequency & std::numeric_limits<uint8_t>::max()) | (msb << 8);
      tone.block = block;
    }

    m_changeTrackingFlags.channelsChanged.set(channelIndex);
  } else {
    // A0H+	Frequency number LSB
    for (size_t i = 0; i < OPERATORS_COUNT; ++i) {
      Tone& tone{channel.tone[i]};
      tone.frequency =
          ((tone.frequency & (std::numeric_limits<uint8_t>::max() << 8)) |
           writeData.val);
    }
  }
}

void YM2612::processFeedbackAlgorithmSettings(WriteData writeData,
                                              uint8_t port) {
  const uint8_t regLo(writeData.reg & 0x0F);
  const size_t channelIndex((regLo & 0b11) + (port == 0 ? 0 : 3));
  Channel& channel{m_context.channels.at(channelIndex)};
  channel.instrument.algorithm = writeData.val & 0b111;
  channel.instrument.feedback = (writeData.val >> 3) & 0b111;
  m_changeTrackingFlags.channelsChanged.set(channelIndex);
}

void YM2612::processPanLfoSettings(WriteData writeData, uint8_t port) {
  const uint8_t regLo(writeData.reg & 0x0F);
  const size_t channelIndex((regLo & 0b11) + (port == 0 ? 0 : 3));
  Channel& channel{m_context.channels.at(channelIndex)};
  channel.panning = static_cast<Panning>((writeData.val >> 6) & 0b11);
  channel.fms = static_cast<int8_t>(writeData.val & 0b111);
  channel.ams = static_cast<int8_t>((writeData.val >> 3) & 0b111);
  m_changeTrackingFlags.channelsChanged.set(channelIndex);
}
}  // namespace vgmc::chips
