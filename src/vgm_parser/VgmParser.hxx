#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "VgmEventDispatcher.hxx"
#include "VgmEvents.hxx"

namespace vgmc {
// List all adapters here. Or create list in separate header file.
class SmpsTextAdapter;

struct VgmParserSettings {
  struct {
    uint32_t rate;
  } headerOverrides{};
};

template <typename OutputAdapter>
class VgmParser final {
 public:
  VgmParser(VgmParserSettings settings) : m_settings{settings} {}

  // Parse entire VGM file and call callbacks in outputAdapter
  // todo: write tests
  void parse(std::span<const uint8_t> fileData, OutputAdapter& outputAdapter);

 private:
  static constexpr size_t ONE_BYTE_INDEXED_SIZE{
      std::numeric_limits<uint8_t>::max() + 1};
  static constexpr uint8_t CHIP_TYPE_UNKNOWN{0xFF};
  static constexpr uint8_t CHIP_COUNT{3};
  static constexpr uint8_t YM2612_COMMAND_DAC_VALUE{0x2A};

  VgmParserSettings m_settings;

  // Internal parse context with additional internal info (OutputAdapter)
  struct ParseContext : public VgmParseContext {
    ParseContext(std::span<const uint8_t> fileData, OutputAdapter& adapter)
        : VgmParseContext{fileData}, dispatcher{adapter} {}

    VgmEventDispatcher<OutputAdapter> dispatcher;
  };

  using CommandHandler = void (*)(ParseContext&);

  struct HandlerInfo {
    uint8_t chipType;
    uint8_t dataSize;  // if 0 presented, handler should add to file offset
    CommandHandler handler;
  };

  static const std::array<HandlerInfo, ONE_BYTE_INDEXED_SIZE> s_handlersTable;

  // TODO: create absctract chip system if needed (like in libvgm).
  // Now it's overcomplicated

  // Parse header and do initial setup
  // After this, register chips
  void parseHeader(ParseContext& ctx);
  void handleCommand(ParseContext& ctx, uint8_t command);

  // TODO: other command handlers
  static void handleUnknown(ParseContext& ctx);
  [[noreturn]] static void handleInvalid(ParseContext& ctx);

  static void handleSN76489(ParseContext& ctx);
  static void handleYM2612WritePort0(ParseContext& ctx);
  static void handleYM2612WritePort1(ParseContext& ctx);
  static void handleDelaySamples2B(ParseContext& ctx);
  static void handleDelay60Hz(ParseContext& ctx);
  static void handleDelay50Hz(ParseContext& ctx);
  static void handleEndOfData(ParseContext& ctx);
  static void handleDataBlock(ParseContext& ctx);
  static void handlePcmRamWrite(ParseContext& ctx);
  static void handleDelaySamplesN1(ParseContext& ctx);
  static void handleYM2612PcmDelay(ParseContext& ctx);
  static void handleDacStreamControlSetup(ParseContext& ctx);
  static void handleDacStreamControlSetData(ParseContext& ctx);
  static void handleDacStreamControlSetFrequency(ParseContext& ctx);
  static void handleDacStreamControlPlayDataLocation(ParseContext& ctx);
  static void handleDacStreamControlStop(ParseContext& ctx);
  static void handleDacStreamControlPlayDataBlock(ParseContext& ctx);
  static void handleYM2612PcmSeek(ParseContext& ctx);

  static auto readRelativeOffset32(std::span<const uint8_t> data, size_t index)
      -> size_t;
};

extern template class VgmParser<SmpsTextAdapter>;
}  // namespace vgmc
