#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "adapters/SmpsAdapterBase/SmpsAdapterBase.hxx"
#include "chips/SN76489/SN76489.hxx"
#include "chips/YM2612/YM2612.hxx"
#include "utils/NoteUtils.hxx"

namespace vgmc {
class SmpsTextAdapter : public SmpsAdapterBase {
 public:
  SmpsTextAdapter(const SmpsAdapterBaseSettings& settings);
  void writeTo(std::ostream& out);

 private:
  static constexpr int NOISE_MODE_PERIODIC{0xE3};
  static constexpr int NOISE_MODE_WHITE{0xE7};

  struct ChannelLabels {
    std::string dac;
    std::array<std::string, chips::YM2612::CHANNELS_COUNT> fm;
    std::array<std::string, chips::SN76489::PSG_CHANNELS_COUNT> psg;
    std::string noise;
  };

  struct Labels {
    ChannelLabels channels;
    ChannelLabels loop;
    ChannelLabels retranslators;

    std::string songId;
    std::string empty;
    std::string voices;
  };

  std::array<std::optional<int8_t>, chips::YM2612::CHANNELS_COUNT> m_prevFms;

  Labels m_labels{};
  std::bitset<chips::YM2612::CHANNELS_COUNT> m_needPrintFm;
  bool m_needPrintDac{};
  std::bitset<chips::SN76489::PSG_CHANNELS_COUNT> m_needPrintPsg;
  bool m_needPrintNoise{};

  bool m_shouldWriteRetranslators{};
  size_t m_retranslatorChannelIndex{};
  size_t m_printedChannelIndex{};

  void printFmInstruments(std::ostream& out);
  void printFmChannels(std::ostream& out);
  void printDacChannel(std::ostream& out);
  void printPsgChannels(std::ostream& out);
  void printNoiseChannel(std::ostream& out);
  static void printSmpsPan(std::ostream& out, chips::YM2612::Panning panning);
  void printSmpsFms(std::ostream& out, int8_t fms,
                    std::optional<int8_t>& prevFms);
  [[nodiscard]] static auto printLength(std::ostream& out, bool isKeyOn,
                                        bool forcePrint,
                                        uint8_t actualNoteLength,
                                        uint32_t length) -> uint8_t;
  static void printSmpsSetVoice(std::ostream& out, uint8_t instrumentIndex);
  static void printSmpsAlterVol(std::ostream& out, int8_t diff);
  static void printSmpsPSGAlterVol(std::ostream& out, int8_t diff);
  static void printNote(std::ostream& out, const SimpleNote& note);
  static void printNoteOff(std::ostream& out);
  static void printDacSample(std::ostream& out, uint8_t sampleIndex);
  static void printSmpsAlterNote(std::ostream& out, int frequencyOffset);
  static void printSmpsNoAttack(std::ostream& out);
  [[nodiscard]] static auto panningToString(chips::YM2612::Panning panning)
      -> std::string_view;
  [[nodiscard]] static auto driverVersionToString(
      SmpsDriverVersion driverVersion) -> std::string_view;
  static void printSmpsPSGform(std::ostream& out, int psgForm);

  static auto isKeyOn(const YM2612FMChannelSnapshot& snapshot) noexcept
      -> bool {
    return snapshot.isKeyOn;
  }

  static auto isKeyOn(const YM2612DacChannelSnapshot& snapshot) noexcept
      -> bool {
    return snapshot.maybeSampleIndex.has_value();
  }

  static auto isKeyOn(const SN76489PsgChannelSnapshot& snapshot) noexcept
      -> bool {
    return snapshot.isKeyOn();
  }

  static auto isKeyAttack(const YM2612FMChannelSnapshot& snapshot) noexcept
      -> bool {
    return snapshot.keyPlayMode == KeyPlayMode::attack;
  }

  static auto isKeyAttack(const YM2612DacChannelSnapshot& snapshot) noexcept
      -> bool {
    return snapshot.keyPlayMode == KeyPlayMode::attack;
  }

  static auto isKeyAttack(const SN76489PsgChannelSnapshot& snapshot) noexcept
      -> bool {
    return true;
  }

  static void printNoteFromSnapshot(std::ostream& out,
                                    const YM2612FMChannelSnapshot& snapshot) {
    printNote(out, snapshot.note);
  }

  static void printNoteFromSnapshot(std::ostream& out,
                                    const YM2612DacChannelSnapshot& snapshot) {
    if (not snapshot.maybeSampleIndex.has_value()) {
      return;
    }

    printDacSample(out, snapshot.maybeSampleIndex.value());
  }

  static void printNoteFromSnapshot(std::ostream& out,
                                    const SN76489PsgChannelSnapshot& snapshot) {
    printNote(out, snapshot.note);
  }

  static void printNoteFromSnapshot(
      std::ostream& out, const SN76489NoiseChannelSnapshot& snapshot);

  template <typename SnapshotType>
  void printSnapshots(std::ostream& out, std::string_view label,
                      std::string_view loopLabel,
                      const std::vector<std::optional<SnapshotType>>& snapshots,
                      size_t channelIndex = 0);

  void printRetranslators(std::ostream& out);

  void printSnapshotDiff(std::ostream& out,
                         const YM2612FMChannelSnapshot& snapshot,
                         const YM2612FMChannelSnapshot& prevSnapshot,
                         size_t channelIndex);

  void printInitialSnapshot(std::ostream& out,
                            const YM2612FMChannelSnapshot& snapshot,
                            size_t channelIndex);

  void printSnapshotDiff(std::ostream& out,
                         const YM2612DacChannelSnapshot& snapshot,
                         const YM2612DacChannelSnapshot& prevSnapshot,
                         size_t channelIndex);

  void printInitialSnapshot(std::ostream& out,
                            const YM2612DacChannelSnapshot& snapshot,
                            size_t channelIndex) {
    printSnapshotDiff(out, snapshot, YM2612DacChannelSnapshot{}, channelIndex);
  }

  static void printSnapshotDiff(std::ostream& out,
                                const SN76489PsgChannelSnapshot& snapshot,
                                const SN76489PsgChannelSnapshot& prevSnapshot,
                                size_t channelIndex);

  static void printSnapshotDiff(std::ostream& out,
                                const SN76489NoiseChannelSnapshot& snapshot,
                                const SN76489NoiseChannelSnapshot& prevSnapshot,
                                size_t channelIndex);

  static void printInitialSnapshot(std::ostream& out,
                                   const SN76489PsgChannelSnapshot& snapshot,
                                   size_t channelIndex);

  static void printInitialSnapshot(std::ostream& out,
                                   const SN76489NoiseChannelSnapshot& snapshot,
                                   size_t channelIndex);

  static void initDisabledSnapshot(SN76489PsgChannelSnapshot& snapshot) {
    snapshot.volume = chips::SN76489::MIN_VOLUME;
  }

  static void disableKeyAttack(YM2612FMChannelSnapshot& snapshot) {
    snapshot.keyPlayMode = KeyPlayMode::hold;
  }

  static void disableKeyAttack(YM2612DacChannelSnapshot& snapshot) {
    // DAC in SMPS does not support smpsNoAttack :(
    snapshot.maybeSampleIndex.reset();
  }

  static void printFmInstrument(std::ostream& out,
                                const chips::YM2612::Instrument& instrument);
};

}  // namespace vgmc
