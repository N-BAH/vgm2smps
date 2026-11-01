#include "VgmParser.hxx"

#include <spdlog/spdlog.h>

#include <stdexcept>

#include "VgmHeader.hxx"
#include "adapters/SmpsTextAdapter/SmpsTextAdapter.hxx"
#include "utils/readBinary.hxx"

namespace vgmc {
template <typename OutputAdapter>
void VgmParser<OutputAdapter>::parse(std::span<const uint8_t> fileData,
                                     OutputAdapter& adapter) {
  ParseContext ctx{fileData, adapter};

  parseHeader(ctx);

  while (ctx.cursor < fileData.size()) {
    const uint8_t command{fileData[ctx.cursor]};
    handleCommand(ctx, command);
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::parseHeader(ParseContext& ctx) {
  // Magic consts
  constexpr std::string_view VGM_IDENT{"Vgm "};
  constexpr size_t EOF_OFFSET_INDEX{4};
  constexpr size_t FILE_VERSION_INDEX{8};
  constexpr size_t GD3_OFFSET_INDEX{0x14};
  constexpr size_t TOTAL_SAMPLES_INDEX{0x18};
  constexpr size_t LOOP_OFFSET_INDEX{0x1C};
  constexpr size_t LOOP_SAMPLES_INDEX{0x20};
  constexpr size_t RATE_INDEX{0x24};
  constexpr size_t DATA_OFFSET_INDEX{0x34};
  constexpr uint32_t FILE_VERSION_1_50{0x150};
  constexpr size_t FILE_VERSION_1_00_DATA_INDEX{0x40};
  constexpr size_t MIN_DATA_INDEX{0x38};

  constexpr size_t YM2612_CLOCK_INDEX{0x2C};
  constexpr size_t SN76489_CLOCK_INDEX{0x0C};

  if (ctx.fileData.size() < MIN_DATA_INDEX) {
    throw std::runtime_error{"corrupted VGM file: too small"};
  }

  if (std::memcmp(ctx.fileData.data(), VGM_IDENT.data(), VGM_IDENT.size()) !=
      0) {
    throw std::runtime_error{"corrupted VGM file: this is not a VGM file"};
  }

  ctx.header.eofIndex = readRelativeOffset32(ctx.fileData, EOF_OFFSET_INDEX);

  if (ctx.header.eofIndex > ctx.fileData.size()) {
    throw std::runtime_error{
        "corrupted VGM file: EOF offset larger than file size"};
  }

  ctx.header.fileVersion =
      readBinary<uint32_t>(ctx.fileData, FILE_VERSION_INDEX);
  ctx.header.rate = m_settings.headerOverrides.rate != 0
                        ? m_settings.headerOverrides.rate
                        : readBinary<uint32_t>(ctx.fileData, RATE_INDEX);
  ctx.header.gd3Index = readRelativeOffset32(ctx.fileData, GD3_OFFSET_INDEX);

  ctx.header.totalSamples =
      readBinary<uint32_t>(ctx.fileData, TOTAL_SAMPLES_INDEX);
  ctx.header.loopOffset = readBinary<uint32_t>(ctx.fileData, LOOP_OFFSET_INDEX);
  ctx.header.loopSamples =
      readBinary<uint32_t>(ctx.fileData, LOOP_SAMPLES_INDEX);

  // Read file data index according to libvgm source code
  ctx.header.dataIndex =
      ctx.header.fileVersion >= FILE_VERSION_1_50
          ? readRelativeOffset32(ctx.fileData, DATA_OFFSET_INDEX)
          : 0;

  if (ctx.header.dataIndex == 0) {
    // offset not set - assume v1.00 header size
    ctx.header.dataIndex = FILE_VERSION_1_00_DATA_INDEX;
  }

  if (ctx.header.dataIndex < MIN_DATA_INDEX) {
    SPDLOG_WARN("Invalid Data Offset 0x{:02X}!", ctx.header.dataIndex);
    ctx.header.dataIndex = MIN_DATA_INDEX;
  }

  ctx.header.ym2612Clock =
      readBinary<uint32_t>(ctx.fileData, YM2612_CLOCK_INDEX);
  ctx.header.sn76489Clock =
      readBinary<uint32_t>(ctx.fileData, SN76489_CLOCK_INDEX);

  // Prepare chip$
  if (ctx.header.ym2612Enabled()) {
    ctx.chips.ym2612.emplace(ctx.header.ym2612Clock);
  }

  if (ctx.header.sn76489Enabled()) {
    ctx.chips.sn76489.emplace(ctx.header.sn76489Clock);
  }

  // TODO: Extra header parsing
  ctx.cursor = ctx.header.dataIndex;

  ctx.dispatcher.dispatchHeaderParsed(
      ctx, {.header = ctx.header, .chips = ctx.chips});
}

template <typename OutputAdapter>
auto VgmParser<OutputAdapter>::readRelativeOffset32(
    const std::span<const uint8_t> data, const size_t index) -> size_t {
  return readBinary<uint32_t>(data, index) + index;
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleCommand(ParseContext& ctx,
                                             uint8_t command) {
  const HandlerInfo handlerInfo{s_handlersTable[command]};
  const auto prevTick{ctx.tick};

  handlerInfo.handler(ctx);

  if (ctx.tick != prevTick) {
    TickAdvanceEvent event;
    event.tick = ctx.tick;
    event.prevTick = prevTick;
    ctx.dispatcher.dispatchTickAdvance(ctx, event);
  }

  ctx.cursor += handlerInfo.dataSize;
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleUnknown(ParseContext& ctx) {
  SPDLOG_WARN("Unknown command {:02X} at offset {:08X}",
              ctx.fileData[ctx.cursor], ctx.cursor);
}

template <typename OutputAdapter>
[[noreturn]] void VgmParser<OutputAdapter>::handleInvalid(ParseContext& ctx) {
  throw std::runtime_error{"invalid command"};
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleSN76489(ParseContext& ctx) {
  if (ctx.chips.sn76489.has_value()) {
    const uint8_t value{ctx.fileData[ctx.cursor + 1]};
    ctx.chips.sn76489->write(value);
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleYM2612WritePort0(ParseContext& ctx) {
  if (ctx.chips.ym2612.has_value()) {
    const uint8_t reg{ctx.fileData[ctx.cursor + 0x01]};
    const uint8_t value{ctx.fileData[ctx.cursor + 0x02]};
    ctx.chips.ym2612->writePort0({reg, value});
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleYM2612WritePort1(ParseContext& ctx) {
  if (ctx.chips.ym2612.has_value()) {
    const uint8_t reg{ctx.fileData[ctx.cursor + 0x01]};
    const uint8_t value{ctx.fileData[ctx.cursor + 0x02]};
    ctx.chips.ym2612->writePort1({reg, value});
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDelaySamples2B(ParseContext& ctx) {
  ctx.tick += readBinary<uint16_t>(ctx.fileData, ctx.cursor + 1);
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDelay60Hz(ParseContext& ctx) {
  // Should be 735
  constexpr size_t TO_DELAY{VGM_TICK_RATE / 60};
  ctx.tick += TO_DELAY;
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDelay50Hz(ParseContext& ctx) {
  // Should be 882
  constexpr size_t TO_DELAY{VGM_TICK_RATE / 50};
  ctx.tick += TO_DELAY;
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleEndOfData(ParseContext& ctx) {
  ctx.dispatcher.dispatchEndOfData(ctx);
  ctx.cursor = ctx.fileData.size();
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDataBlock(ParseContext& ctx) {
  constexpr size_t DATA_OFFSET{0x07};
  const uint8_t dataBlockType{ctx.fileData[ctx.cursor + 0x02]};
  const uint8_t compressionType{static_cast<uint8_t>(dataBlockType & 0xC0)};
  const uint32_t dataBlockLength{
      readBinary<uint32_t>(ctx.fileData, ctx.cursor + 0x03) & 0x7F'FF'FF'FF};
  ctx.cursor += DATA_OFFSET;

  // uncompressed data block
  if (compressionType == 0x00) {
    SPDLOG_INFO("process data block. type:{:02X}, size:{:08X}", dataBlockType,
                dataBlockLength);
    const size_t pcmBankIndex{
        dataBlockType &
        static_cast<size_t>(ParseContext::MAX_PCM_BANK_COUNT - 1)};
    PcmBank& pcmBank{ctx.pcmBanks[pcmBankIndex]};
    pcmBank.blocks.push_back(ctx.fileData.subspan(ctx.cursor, dataBlockLength));

    {
      DataBlockExtractedEvent event;
      event.dataBlock = pcmBank.blocks.back();
      event.dataBlockIndex = pcmBank.blocks.size() - 1;
      event.pcmBankIndex = pcmBankIndex;

      ctx.dispatcher.dispatchDataBlockExtracted(ctx, event);
    }
  } else {
    // If any other than "uncompressed data block"
    // TODO: add compressed or other data block types support if needed
    SPDLOG_WARN(
        "Unknown data block at offset:0x{:8X}: compressionType:0x{:02X} "
        "skipped",
        ctx.cursor, compressionType);
  }

  ctx.cursor += dataBlockLength;
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handlePcmRamWrite(ParseContext& ctx) {
  constexpr uint32_t DEFAULT_DATA_SIZE{0x0100'0000};
  const uint8_t chipType{ctx.fileData[ctx.cursor + 2]};
  const uint32_t readOffset{readBinary24(ctx.fileData, ctx.cursor + 3)};
  const uint32_t writeOffset{readBinary24(ctx.fileData, ctx.cursor + 6)};
  uint32_t dataSize{readBinary24(ctx.fileData, ctx.cursor + 9)};

  if (dataSize == 0) {
    dataSize = DEFAULT_DATA_SIZE;
  }

  SPDLOG_DEBUG(
      "PCM RAM Write: chipType:{:02X}, readOffset:{:08X}, writeOffset:{:08X}, "
      "dataSize:{:08X}",
      chipType, readOffset, writeOffset, dataSize);
};

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDelaySamplesN1(ParseContext& ctx) {
  ctx.tick += 1 + (ctx.fileData[ctx.cursor] & 0x0F);
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleYM2612PcmDelay(ParseContext& ctx) {
  const uint8_t ticksWait{
      static_cast<uint8_t>(ctx.fileData[ctx.cursor] & 0x0F)};

  ctx.tick += ticksWait;

  if (not ctx.chips.ym2612.has_value()) {
    return;
  }

  const auto dataBlock{ctx.getYM2612PcmBank()};

  if (ctx.ym2612PcmBankPosition >= dataBlock.size()) {
    return;
  }

  const uint8_t data{dataBlock[ctx.ym2612PcmBankPosition]};
  ctx.chips.ym2612->writePort0({YM2612_COMMAND_DAC_VALUE, data});
  ++ctx.ym2612PcmBankPosition;

  {
    YM2612PcmDelayEvent event;
    event.pcmBank = dataBlock;
    event.offset = ctx.ym2612PcmBankPosition;
    event.ticksWait = ticksWait;
    ctx.dispatcher.dispatchYM2612PcmDelay(ctx, event);
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDacStreamControlSetup(ParseContext& ctx) {
  const uint8_t streamId{ctx.fileData[ctx.cursor + 1]};
  const uint8_t chipType{
      static_cast<uint8_t>(ctx.fileData[ctx.cursor + 2] & 0x7F)};
  const bool isSecondChip{(ctx.fileData[ctx.cursor + 2] >> 7) != 0};

  if (not isSecondChip) {
    DacStream dacStream;

    const uint8_t cmd{ctx.fileData[ctx.cursor + 3]};
    const uint8_t reg{ctx.fileData[ctx.cursor + 4]};

    ctx.dacStreams[streamId] = DacStream{};

    if (cmd != 0 or reg != YM2612_COMMAND_DAC_VALUE) {
      SPDLOG_WARN(
          "Incorrect DAC stream for YM2612 in VGM file. cmd:{:02X}, "
          "reg:{:02X}. Should be cmd:{:02X}, reg:{:02X}",
          cmd, reg, 0, YM2612_COMMAND_DAC_VALUE);
    }

    SPDLOG_INFO(
        "setup dac stream control for chip type:{:02X}, streamId:{:02X}, "
        "cmd:{:02X}, reg:{:02X}",
        chipType, streamId, cmd, reg);
  } else {
    SPDLOG_ERROR("Second chip is not supported");
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDacStreamControlSetData(
    ParseContext& ctx) {
  const uint8_t streamId{ctx.fileData[ctx.cursor + 1]};
  const uint8_t pcmBankId{ctx.fileData[ctx.cursor + 2]};
  const uint8_t stepSize{ctx.fileData[ctx.cursor + 3]};
  const uint8_t stepBase{ctx.fileData[ctx.cursor + 4]};

  if (stepSize != 0x01) {
    SPDLOG_WARN(
        "[{:016X}] StreamID:{}\tStep Size for DAC Stream other than 1 is not "
        "supported.",
        ctx.cursor, streamId);
  }

  if (stepBase != 0) {
    SPDLOG_WARN(
        "[{:016X}] StreamID:{}\tStep Base for DAC Stream other than 0 is not "
        "supported.",
        ctx.cursor, streamId);
  }

  if (auto dacStreamIt{ctx.dacStreams.find(streamId)};
      dacStreamIt != ctx.dacStreams.end()) {
    dacStreamIt->second.pcmBankId = pcmBankId;

    if (pcmBankId >= ParseContext::MAX_PCM_BANK_COUNT) {
      SPDLOG_WARN(
          "Wrong number of PCM Bank:{} in stream with id:{}. Max number of PCM "
          "Banks is {}",
          pcmBankId, streamId, ParseContext::MAX_PCM_BANK_COUNT);
    }
  }

  SPDLOG_INFO(
      "streamId:{:02X}, pcmBankId:{:02X}, stepSize:{:02X}, stepBase:{:02X}",
      streamId, pcmBankId, stepSize, stepBase);
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDacStreamControlSetFrequency(
    ParseContext& ctx) {
  const uint8_t streamId{ctx.fileData[ctx.cursor + 1]};
  const uint32_t frequency{readBinary<uint32_t>(ctx.fileData, ctx.cursor + 2)};

  if (auto dacStreamIt{ctx.dacStreams.find(streamId)};
      dacStreamIt != ctx.dacStreams.end()) {
    dacStreamIt->second.sampleRate = frequency;

    ctx.dispatcher.dispatchDacStreamSetup(ctx, {streamId});
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDacStreamControlPlayDataLocation(
    ParseContext& ctx) {
  const uint8_t streamId{ctx.fileData[ctx.cursor + 1]};
  const uint32_t dataStartOffset{
      readBinary<uint32_t>(ctx.fileData, ctx.cursor + 2)};
  const uint8_t lengthMode{ctx.fileData[ctx.cursor + 6]};
  const uint32_t dataLength{readBinary<uint32_t>(ctx.fileData, ctx.cursor + 7)};
  SPDLOG_WARN(
      "TODO: impl vgm command 0x93. streamId{:02X}, dataStartOffset:{:08X}, "
      "lengthMode:{:02X}, "
      "dataLength:{:08X}",
      streamId, dataStartOffset, lengthMode, dataLength);
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDacStreamControlStop(ParseContext& ctx) {
  constexpr uint8_t DAC_STREAM_ID_ALL{0xFF};
  const uint8_t streamId{ctx.fileData[ctx.cursor + 1]};
  SPDLOG_INFO("stop stream id:{:02X}", streamId);

  if (streamId == DAC_STREAM_ID_ALL) {
    for (auto& [key, dacStream] : ctx.dacStreams) {
      dacStream.isActive = false;
    }

    return;
  }

  if (auto dacStreamIt{ctx.dacStreams.find(streamId)};
      dacStreamIt != ctx.dacStreams.end()) {
    dacStreamIt->second.isActive = false;
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleDacStreamControlPlayDataBlock(
    ParseContext& ctx) {
  const uint8_t streamId{ctx.fileData[ctx.cursor + 1]};

  if (auto dacStreamIt{ctx.dacStreams.find(streamId)};
      dacStreamIt != ctx.dacStreams.end()) {
    dacStreamIt->second.data = {};

    if (dacStreamIt->second.pcmBankId >= ParseContext::MAX_PCM_BANK_COUNT) {
      return;
    }

    const uint16_t soundId{readBinary<uint16_t>(ctx.fileData, ctx.cursor + 2)};
    const uint8_t flags{ctx.fileData[ctx.cursor + 4]};
    PcmBank& pcmBank{ctx.pcmBanks[dacStreamIt->second.pcmBankId]};

    if (soundId >= pcmBank.blocks.size()) {
      // This soundId was skipped in handleDataBlock stage. It's normal now.
      return;
    }

    dacStreamIt->second.soundId = soundId;
    dacStreamIt->second.data = pcmBank.blocks[soundId];

    if (flags != 0) {
      SPDLOG_WARN(
          "Unknown flags for DAC Stream id:{}, soundId:{}, flags:{:02X}",
          streamId, soundId, flags);
    }

    ctx.dispatcher.dispatchDacStreamPlay(ctx, {streamId, dacStreamIt->second});
  }
}

template <typename OutputAdapter>
void VgmParser<OutputAdapter>::handleYM2612PcmSeek(ParseContext& ctx) {
  if (not ctx.chips.ym2612.has_value()) {
    return;
  }

  const uint32_t offset{readBinary<uint32_t>(ctx.fileData, ctx.cursor + 1)};
  const auto prevOffset{ctx.ym2612PcmBankPosition};
  SPDLOG_DEBUG("Seek PCM from {} to {}", ctx.ym2612PcmBankPosition, offset);

  ctx.ym2612PcmBankPosition = offset;

  ctx.dispatcher.dispatchYM2612PcmSeek(
      ctx, {ctx.getYM2612PcmBank(), ctx.ym2612PcmBankPosition, prevOffset});
}

// clang-format off
template <typename OutputAdapter>
const std::array<
  typename VgmParser<OutputAdapter>::HandlerInfo,
  VgmParser<OutputAdapter>::ONE_BYTE_INDEXED_SIZE
> VgmParser<OutputAdapter>::s_handlersTable = {{
  {0xFF, 0x01, &VgmParser::handleUnknown},                          // 00
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 01
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 02
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 03
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 04
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 05
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 06
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 07
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 08
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 09
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 0A
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 0B
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 0C
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 0D
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 0E
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 0F
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 10
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 11
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 12
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 13
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 14
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 15
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 16
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 17
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 18
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 19
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 1A
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 1B
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 1C
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 1D
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 1E
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 1F
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 20
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 21
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 22
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 23
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 24
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 25
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 26
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 27
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 28
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 29
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 2A
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 2B
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 2C
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 2D
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 2E
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 2F
  {0x00, 0x02, &VgmParser::handleUnknown},                          // 30 SN76489 register write (2nd chip)
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 31 AY8910 stereo mask [chip type depends on data]
  {0x2C, 0x02, &VgmParser::handleUnknown},                          // 32 MSM5205 register write
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 33
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 34
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 35
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 36
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 37
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 38
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 39
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 3A
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 3B
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 3C
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 3D
  {0xFF, 0x02, &VgmParser::handleUnknown},                          // 3E
  {0x00, 0x02, &VgmParser::handleUnknown},                          // 3F GameGear stereo mask (2nd chip)
  {0x29, 0x03, &VgmParser::handleUnknown},                          // 40 Mikey register write
  {0x2A, 0x03, &VgmParser::handleUnknown},                          // 41 K007232 register write
  {0x2B, 0x03, &VgmParser::handleUnknown},                          // 42 K005289 register write
  {0x2D, 0x03, &VgmParser::handleUnknown},                          // 43 MSM5232 register write
  {0x2F, 0x03, &VgmParser::handleUnknown},                          // 44 ICS2115 register write
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 45
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 46
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 47
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 48
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 49
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 4A
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 4B
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 4C
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 4D
  {0xFF, 0x03, &VgmParser::handleUnknown},                          // 4E
  {0x00, 0x02, &VgmParser::handleUnknown},                          // 4F GameGear stereo mask
  {0x00, 0x02, &VgmParser::handleSN76489},                          // 50 SN76489 register write
  {0x01, 0x03, &VgmParser::handleUnknown},                          // 51 YM2413 register write
  {0x02, 0x03, &VgmParser::handleYM2612WritePort0},                 // 52 YM2612 register write, port 0
  {0x02, 0x03, &VgmParser::handleYM2612WritePort1},                 // 53 YM2612 register write, port 1
  {0x03, 0x03, &VgmParser::handleUnknown},                          // 54 YM2151 register write
  {0x06, 0x03, &VgmParser::handleUnknown},                          // 55 YM2203 register write
  {0x07, 0x03, &VgmParser::handleUnknown},                          // 56 YM2608 register write, port 0
  {0x07, 0x03, &VgmParser::handleUnknown},                          // 57 YM2608 register write, port 1
  {0x08, 0x03, &VgmParser::handleUnknown},                          // 58 YM2610 register write, port 0
  {0x08, 0x03, &VgmParser::handleUnknown},                          // 59 YM2610 register write, port 1
  {0x09, 0x03, &VgmParser::handleUnknown},                          // 5A YM3812 register write
  {0x0A, 0x03, &VgmParser::handleUnknown},                          // 5B YM3526 register write
  {0x0B, 0x03, &VgmParser::handleUnknown},                          // 5C Y8950 register write
  {0x0F, 0x03, &VgmParser::handleUnknown},                          // 5D YMZ280 register write
  {0x0C, 0x03, &VgmParser::handleUnknown},                          // 5E YMF262 register write, port 0
  {0x0C, 0x03, &VgmParser::handleUnknown},                          // 5F YMF262 register write, port 1
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 60
  {0xFF, 0x03, &VgmParser::handleDelaySamples2B},                   // 61 wait N samples (2 byte parameter)
  {0xFF, 0x01, &VgmParser::handleDelay60Hz},                        // 62 wait 735 samples (1/60 second)
  {0xFF, 0x01, &VgmParser::handleDelay50Hz},                        // 63 wait 882 samples (1/50 second)
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 64
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 65
  {0xFF, 0x00, &VgmParser::handleEndOfData},                        // 66 end of command data [length 0 due to special handling]
  {0xFF, 0x00, &VgmParser::handleDataBlock},                        // 67 data block [length 0 due to special handling]
  {0xFF, 0x0C, &VgmParser::handlePcmRamWrite},                      // 68 PCM RAM write
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 69
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 6A
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 6B
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 6C
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 6D
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 6E
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 6F
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 70 wait 1 sample
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 71 wait 2 samples
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 72 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 73 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 74 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 75 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 76 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 77 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 78 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 79 .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 7A .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 7B .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 7C .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 7D .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 7E .
  {0xFF, 0x01, &VgmParser::handleDelaySamplesN1},                   // 7F wait 16 samples
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 80 write YM2612 PCM data from data block 0
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 81 write YM2612 PCM data from data block 0, then wait 1 sample
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 82 write YM2612 PCM data from data block 0, then wait 2 samples
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 83 .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 84 .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 85 .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 86 .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 87 .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 88 .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 89 .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 8A .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 8B .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 8C .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 8D .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 8E .
  {0x02, 0x01, &VgmParser::handleYM2612PcmDelay},                   // 8F write YM2612 PCM data from data block 0, then wait 15 samples
  {0xFF, 0x05, &VgmParser::handleDacStreamControlSetup},            // 90 DAC Stream Control: Setup Chip
  {0xFF, 0x05, &VgmParser::handleDacStreamControlSetData},          // 91 DAC Stream Control: Set Data
  {0xFF, 0x06, &VgmParser::handleDacStreamControlSetFrequency},     // 92 DAC Stream Control: Set Playback Frequency
  {0xFF, 0x0B, &VgmParser::handleDacStreamControlPlayDataLocation}, // 93 DAC Stream Control: Play Data (using start positon/length)
  {0xFF, 0x02, &VgmParser::handleDacStreamControlStop},             // 94 DAC Stream Control: Stop
  {0xFF, 0x05, &VgmParser::handleDacStreamControlPlayDataBlock},    // 95 DAC Stream Control: Play Data (single data block)
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 96
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 97
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 98
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 99
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 9A
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 9B
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 9C
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 9D
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 9E
  {0xFF, 0x00, &VgmParser::handleInvalid},                          // 9F
  {0x12, 0x03, &VgmParser::handleUnknown},                          // A0 AY8910 register write
  {0x01, 0x03, &VgmParser::handleUnknown},                          // A1 YM2413 register write (2nd chip)
  {0x02, 0x03, &VgmParser::handleUnknown},                          // A2 YM2612 register write, port 0 (2nd chip)
  {0x02, 0x03, &VgmParser::handleUnknown},                          // A3 YM2612 register write, port 1 (2nd chip)
  {0x03, 0x03, &VgmParser::handleUnknown},                          // A4 YM2151 register write (2nd chip)
  {0x06, 0x03, &VgmParser::handleUnknown},                          // A5 YM2203 register write (2nd chip)
  {0x07, 0x03, &VgmParser::handleUnknown},                          // A6 YM2608 register write, port 0 (2nd chip)
  {0x07, 0x03, &VgmParser::handleUnknown},                          // A7 YM2608 register write, port 1 (2nd chip)
  {0x08, 0x03, &VgmParser::handleUnknown},                          // A8 YM2610 register write, port 0 (2nd chip)
  {0x08, 0x03, &VgmParser::handleUnknown},                          // A9 YM2610 register write, port 1 (2nd chip)
  {0x09, 0x03, &VgmParser::handleUnknown},                          // AA YM3812 register write (2nd chip)
  {0x0A, 0x03, &VgmParser::handleUnknown},                          // AB YM3526 register write (2nd chip)
  {0x0B, 0x03, &VgmParser::handleUnknown},                          // AC Y8950 regiVgmParserster write (2nd chip)
  {0x0F, 0x03, &VgmParser::handleUnknown},                          // AD YMZ280 register write (2nd chip)
  {0x0C, 0x03, &VgmParser::handleUnknown},                          // AE YMF262 register write, port 0 (2nd chip)
  {0x0C, 0x03, &VgmParser::handleUnknown},                          // AF YMF262 register write, port 1 (2nd chip)
  {0x05, 0x03, &VgmParser::handleUnknown},                          // B0 RF5C68 register write
  {0x10, 0x03, &VgmParser::handleUnknown},                          // B1 RF5C164 register write
  {0x11, 0x03, &VgmParser::handleUnknown},                          // B2 PWM register write
  {0x13, 0x03, &VgmParser::handleUnknown},                          // B3 GameBoy DMG register write
  {0x14, 0x03, &VgmParser::handleUnknown},                          // B4 NES APU register write
  {0x15, 0x03, &VgmParser::handleUnknown},                          // B5 YMW258 (MultiPCM) register write
  {0x16, 0x03, &VgmParser::handleUnknown},                          // B6 uPD7759 register write
  {0x17, 0x03, &VgmParser::handleUnknown},                          // B7 OKIM6258 register write
  {0x18, 0x03, &VgmParser::handleUnknown},                          // B8 OKIM6295 register write
  {0x1B, 0x03, &VgmParser::handleUnknown},                          // B9 HuC6280 register write
  {0x1D, 0x03, &VgmParser::handleUnknown},                          // BA K053260 register write
  {0x1E, 0x03, &VgmParser::handleUnknown},                          // BB Pokey register write
  {0x21, 0x03, &VgmParser::handleUnknown},                          // BC WonderSwan register write
  {0x23, 0x03, &VgmParser::handleUnknown},                          // BD SAA1099 register write
  {0x25, 0x03, &VgmParser::handleUnknown},                          // BE ES5506 register write (8-bit data)
  {0x28, 0x03, &VgmParser::handleUnknown},                          // BF GA20 register write
  {0x04, 0x04, &VgmParser::handleUnknown},                          // C0 Sega PCM memory write
  {0x05, 0x04, &VgmParser::handleUnknown},                          // C1 RF5C68 memory write
  {0x10, 0x04, &VgmParser::handleUnknown},                          // C2 RF5C164 memory write
  {0x15, 0x04, &VgmParser::handleUnknown},                          // C3 set YMW258 (MultiPCM) bank
  {0x1F, 0x04, &VgmParser::handleUnknown},                          // C4 QSound register write
  {0x20, 0x04, &VgmParser::handleUnknown},                          // C5 SCSP register write
  {0x21, 0x04, &VgmParser::handleUnknown},                          // C6 WonderSwan memory write
  {0x22, 0x04, &VgmParser::handleUnknown},                          // C7 VSU-VUE (Virtual Boy) register write
  {0x26, 0x04, &VgmParser::handleUnknown},                          // C8 X1-010 register write
  {0x2E, 0x04, &VgmParser::handleUnknown},                          // C9 BSMT2000 register write
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // CA
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // CB
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // CC
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // CD
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // CE
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // CF
  {0x0D, 0x04, &VgmParser::handleUnknown},                          // D0 YMF278B register write
  {0x0E, 0x04, &VgmParser::handleUnknown},                          // D1 YMF271 register write
  {0x19, 0x04, &VgmParser::handleUnknown},                          // D2 K051649 (SCC1) register write
  {0x1A, 0x04, &VgmParser::handleUnknown},                          // D3 K054539 register write
  {0x1C, 0x04, &VgmParser::handleUnknown},                          // D4 C140 register write
  {0x24, 0x04, &VgmParser::handleUnknown},                          // D5 ES5503 register write
  {0x25, 0x04, &VgmParser::handleUnknown},                          // D6 ES5506 register write (16-bit data)
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // D7
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // D8
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // D9
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // DA
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // DB
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // DC
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // DD
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // DE
  {0xFF, 0x04, &VgmParser::handleUnknown},                          // DF
  {0xFF, 0x05, &VgmParser::handleYM2612PcmSeek},                    // E0 set YM2612 PCM data offset
  {0x27, 0x05, &VgmParser::handleUnknown},                          // E1 C352 register write
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E2
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E3
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E4
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E5
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E6
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E7
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E8
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // E9
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // EA
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // EB
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // EC
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // ED
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // EE
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // EF
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F0
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F1
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F2
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F3
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F4
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F5
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F6
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F7
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F8
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // F9
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // FA
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // FB
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // FC
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // FD
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // FE
  {0xFF, 0x05, &VgmParser::handleUnknown},                          // FF
}};
// clang-format on

// List all used adapters here
template class VgmParser<SmpsTextAdapter>;
}  // namespace vgmc
