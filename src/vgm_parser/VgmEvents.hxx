#pragma once
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "VgmHeader.hxx"
#include "chips/SN76489/SN76489.hxx"
#include "chips/YM2612/YM2612.hxx"

namespace vgmc {
struct VgmChips {
  std::optional<chips::SN76489> sn76489;
  std::optional<chips::YM2612> ym2612;
  // TODO: list all supported chips
};

// Same meaning as PCMBANK in libvgm btw
struct PcmBank {
  // TODO: store data in vector, if compressed blocks will add
  std::vector<std::span<const uint8_t>> blocks;
};

struct DacStream {
  uint32_t sampleRate;
  uint8_t pcmBankId;
  uint16_t soundId;
  bool isActive;
  std::span<const uint8_t> data;
};

struct HeaderParsedEvent {
  VgmHeader const& header;
  VgmChips& chips;
};

struct TickAdvanceEvent {
  uint32_t tick;
  uint32_t prevTick;
};

struct YM2612PcmSeekEvent {
  std::span<const uint8_t> pcmBank;
  uint32_t offset;
  uint32_t prevOffset;
};

struct YM2612PcmDelayEvent {
  std::span<const uint8_t> pcmBank;
  uint32_t offset;
  uint8_t ticksWait;
};

struct DacStreamSetupEvent {
  uint8_t streamId;
};

struct DacStreamPlayEvent {
  uint8_t streamId;
  const DacStream& dacStream;
};

struct DataBlockExtractedEvent {
  std::span<const uint8_t> dataBlock;
  uint32_t pcmBankIndex;
  uint32_t dataBlockIndex;
};

struct VgmParseContext {
  static constexpr uint8_t MAX_PCM_BANK_COUNT{0x40};

  VgmParseContext(std::span<const uint8_t> fileData) : fileData{fileData} {}

  std::span<const uint8_t> fileData;
  size_t cursor{};
  uint32_t tick{};
  VgmChips chips{};
  VgmHeader header{};

  // TODO: maybe use flat_map from boost or similar
  std::unordered_map<uint8_t, DacStream> dacStreams;

  // At this moment only uncompressed data blocks are supported
  // And by this condition we can store spans in map
  std::array<PcmBank, MAX_PCM_BANK_COUNT> pcmBanks;
  // Current sample position in PCM Bank 0 for YM2612
  uint32_t ym2612PcmBankPosition{0};

  [[nodiscard]] auto getYM2612PcmBank() const -> std::span<const uint8_t> {
    return pcmBanks[0].blocks.at(0);
  }
};
}  // namespace vgmc
