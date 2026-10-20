#pragma once

#include <array>

#include "adapters/CompositeAdapter.hxx"
#include "chips/SN76489/SN76489.hxx"
#include "chips/YM2612/YM2612.hxx"
#include "mixins/SN76489SnapshotLogger/SN76489SnapshotLogger.hxx"
#include "mixins/YM2612SnapshotLogger/YM2612SnapshotLogger.hxx"
#include "utils/NoteUtils.hxx"
#include "vgm_parser/VgmEventDispatcher.hxx"

namespace vgmc {
enum class SmpsDriverVersion : uint8_t { S1, S2, S3K };

struct SmpsAdapterBaseSettings {
  SmpsDriverVersion driverVersion{SmpsDriverVersion::S3K};
  SN76489SnapshotLoggerSettings sn76489;
  YM2612SnapshotLoggerSettings ym2612;
  bool preferDacAtFM6{false};
  uint32_t maxLength{0};
};

// Forward-declaration of all childs of SmpsAdapterBase
// It's need for declare VgmEventDispatcher<...> as friend of SmpsAdapterBase
// for keep event handlers private for everyone, except VgmEventDispatcher
class SmpsTextAdapter;

class SmpsAdapterBase
    : public CompositeAdapter<YM2612SnapshotLogger<SmpsAdapterBase>,
                              SN76489SnapshotLogger<SmpsAdapterBase>> {
 public:
  friend YM2612SnapshotLogger;
  friend SN76489SnapshotLogger;
  friend VgmEventDispatcher<SmpsTextAdapter>;

  SmpsAdapterBase(const SmpsAdapterBaseSettings& settings);

 protected:
  static constexpr uint8_t MAX_YM2612_BLOCK{7};

  const SmpsAdapterBaseSettings m_settings;
  const chips::SN76489* m_sn76489;
  chips::YM2612* m_ym2612;

  uint32_t m_firstNoteTick{};
  bool m_firstNoteTickDetected{};
  bool m_fm6ContainsDac{};
  uint32_t m_loopFrame{};
  VgmHeader m_vgmHeader{};

 private:
  static constexpr std::array<uint16_t, NOTES_IN_OCTAVE> fmFrequencyTable{
      644, 683, 723, 766, 813, 860, 911, 965, 1023, 1084, 1148, 1216,
  };

  // This is the expanded PSG frequency table from Sonic 3 & Knuckles' driver.
  // (It's a copy-paste from "Sonic 2 Clone Driver v2.asm")
  // clang-format off
  static constexpr std::array<uint16_t, NOTES_IN_OCTAVE * MAX_YM2612_BLOCK> psgFrequencies{
	  0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3F7, 0x3BE, 0x388,
	  0x356, 0x326, 0x2F9, 0x2CE, 0x2A5, 0x280, 0x25C, 0x23A, 0x21A, 0x1FB, 0x1DF, 0x1C4,
	  0x1AB, 0x193, 0x17D, 0x167, 0x153, 0x140, 0x12E, 0x11D, 0x10D,  0xFE, 0xEF,  0xE2,
	  0xD6,  0xC9,  0xBE,  0xB4,  0xA9,  0xA0,  0x97,  0x8F,  0x87,  0x7F,  0x78,  0x71,
	  0x6B,  0x65,  0x5F,  0x5A,  0x55,  0x50,  0x4B,  0x47,  0x43,  0x40,  0x3C,  0x39,
	  0x36,  0x33,  0x30,  0x2D,  0x2B,  0x28,  0x26,  0x24,  0x22,  0x20,  0x1F,  0x1D,
	  0x1B,  0x1A,  0x18,  0x17,  0x16,  0x15,  0x13,  0x12,  0x11,  0x10,  0x0,   0x0,
  };
  // clang-format on

  struct PSGNote : public SimpleNote {
    int8_t offset;
  };

  const unsigned int m_psgOctaveDownOffset;

  void onHeaderParsed(const VgmParseContext& ctx,
                      const HeaderParsedEvent& event);
  void onEndOfData(const VgmParseContext& ctx);
  void onTickAdvance(const VgmParseContext& ctx, const TickAdvanceEvent& event);
  void onDataBlockExtracted(const VgmParseContext& ctx,
                            const DataBlockExtractedEvent& event);
  void onYM2612PcmSeek(const VgmParseContext& ctx,
                       const YM2612PcmSeekEvent& event);
  void onYM2612PcmDelay(const VgmParseContext& ctx,
                        const YM2612PcmDelayEvent& event);
  void onDacStreamSetup(const VgmParseContext& ctx,
                        const DacStreamSetupEvent& event);
  void onDacStreamPlay(const VgmParseContext& ctx,
                       const DacStreamPlayEvent& event);

  // Helper fn for mixins
  [[nodiscard]] auto getMaxSnapshotsCount() const noexcept -> uint32_t {
    return m_settings.maxLength;
  }

  void onYM2612ChangeDacEnabledState(const chips::YM2612::Context& context);

  void findBestNoteAndTransform(YM2612FMChannelSnapshot& snapshot);
  void findBestNoteAndTransform(SN76489PsgChannelSnapshot& snapshot);
  void findBestNoteAndTransform(SN76489NoiseChannelSnapshot& snapshot);
  // void onDetectFirstChipChange(const TickAdvanceEvent& event);
  // [[nodiscard]] auto getFirstChipChangeDetected() const noexcept -> bool;
  // [[nodiscard]] auto getFirstNoteTick() const noexcept -> uint32_t;

  // Helper fn for removing unnecessary data from context for more efficient
  // comparation and optimization in SMPS
  static auto shouldKeepSnapshot(
      YM2612FMChannelSnapshot const& currentSnapshot,
      YM2612FMChannelSnapshot const& prevSnapshot) noexcept -> bool;

  static auto shouldKeepSnapshot(
      SN76489PsgChannelSnapshot const& currentSnapshot,
      SN76489PsgChannelSnapshot const& prevSnapshot) noexcept -> bool;

  static auto shouldKeepSnapshot(
      SN76489NoiseChannelSnapshot const& currentSnapshot,
      SN76489NoiseChannelSnapshot const& prevSnapshot) noexcept -> bool;

  static void filterInstrumentData(chips::YM2612::Instrument& instrument);
  static void filterSnapshotData(YM2612FMChannelSnapshot& snapshot);
};
}  // namespace vgmc
