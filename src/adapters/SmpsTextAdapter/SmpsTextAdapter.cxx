#include "SmpsTextAdapter.hxx"

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/printf.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <ranges>
#include <string>

#include "adapters/SmpsAdapterBase/SmpsAdapterBase.hxx"
#include "chips/SN76489/SN76489.hxx"
#include "chips/YM2612/YM2612.hxx"

namespace vgmc {
SmpsTextAdapter::SmpsTextAdapter(const SmpsAdapterBaseSettings& settings)
    : SmpsAdapterBase{settings},
      m_shouldWriteRetranslators{settings.driverVersion !=
                                 SmpsDriverVersion::S1} {}

// TODO: Reduce cognitive complexity of this loop
template <typename SnapshotType>
void SmpsTextAdapter::printSnapshots(
    std::ostream& out, std::string_view label, std::string_view loopLabel,
    const std::vector<std::optional<SnapshotType>>& snapshots,
    size_t channelIndex) {
  bool shouldPrintNoteLengthAfterLoop{false};
  size_t prevSnapshotIndex{0};
  uint8_t actualNoteLength{0};
  bool prevSnapshotKeyOn{false};
  bool loopSetuped{false};
  std::optional<SnapshotType> maybePrevSnapshot;
  std::optional<SnapshotType> maybePrevSnapshotWithKeyOn;
  std::optional<SnapshotType> maybeBeforeLoopSnapshot;

  // Print initial state
  // If first snapshot contains data
  if (const std::optional<SnapshotType>& maybeSnapshot{snapshots.front()};
      maybeSnapshot.has_value()) {
    const SnapshotType& snapshot{maybeSnapshot.value()};

    if (isKeyOn(snapshot)) {
      printInitialSnapshot(out, snapshot, channelIndex);
      printNoteFromSnapshot(out, snapshot);
      prevSnapshotKeyOn = true;
      maybePrevSnapshotWithKeyOn = snapshot;
    } else {
      printNoteOff(out);
    }

    maybePrevSnapshot = snapshot;
  } else {
    printNoteOff(out);
  }

  for (size_t frame{1}; frame < snapshots.size(); ++frame) {
    const bool isLoopFrame{frame == m_loopFrame};
    std::optional<SnapshotType> maybeSnapshot{snapshots[frame]};

    if (isLoopFrame) {
      if (not maybeSnapshot.has_value()) {
        if (maybePrevSnapshot.has_value()) {
          SnapshotType& snapshot{
              maybeSnapshot.emplace(maybePrevSnapshot.value())};

          if constexpr (requires { disableKeyAttack(snapshot); }) {
            if (isKeyOn(snapshot)) {
              disableKeyAttack(snapshot);
            }
          }
        } else {
          if constexpr (requires(SnapshotType& snapshot) {
                          initDisabledSnapshot(snapshot);
                        }) {
            initDisabledSnapshot(maybeSnapshot.emplace());
          } else {
            maybeSnapshot = SnapshotType{};
          }
        }
      }

      maybeBeforeLoopSnapshot = maybePrevSnapshotWithKeyOn;
      loopSetuped = true;
    } else if (not maybeSnapshot.has_value()) {
      continue;
    }

    const SnapshotType& snapshot{maybeSnapshot.value()};
    const size_t sameStateLength{frame - prevSnapshotIndex};

    if (sameStateLength != 0) {
      actualNoteLength =
          printLength(out, prevSnapshotKeyOn, shouldPrintNoteLengthAfterLoop,
                      actualNoteLength, sameStateLength);
      shouldPrintNoteLengthAfterLoop = false;
    }

    if (isLoopFrame) {
      shouldPrintNoteLengthAfterLoop = true;
      fmt::println(out, "{}:", loopLabel);
    }

    if (not maybePrevSnapshot.has_value() or
        not maybePrevSnapshotWithKeyOn.has_value()) {
      prevSnapshotIndex = frame;
      maybePrevSnapshot = snapshot;

      if (isKeyOn(snapshot)) {
        printInitialSnapshot(out, snapshot, channelIndex);
        printNoteFromSnapshot(out, snapshot);
        maybePrevSnapshotWithKeyOn = snapshot;
      } else {
        printNoteOff(out);
      }

      prevSnapshotKeyOn = isKeyOn(snapshot);
      continue;
    }

    const auto& prevSnapshot{maybePrevSnapshotWithKeyOn.value()};

    if (isKeyOn(snapshot)) {
      printSnapshotDiff(out, snapshot, prevSnapshot, channelIndex);

      if (isKeyAttack(snapshot)) {
        printNoteFromSnapshot(out, snapshot);
      } else {
        fmt::println(out, "\tdc.b\tsmpsNoAttack");
        printNoteFromSnapshot(out, snapshot);
      }

      maybePrevSnapshotWithKeyOn = snapshot;
    } else {
      printNoteOff(out);
    }

    prevSnapshotIndex = frame;
    maybePrevSnapshot = snapshot;
    prevSnapshotKeyOn = isKeyOn(snapshot);
  }

  if (const auto sameStateLength{snapshots.size() - prevSnapshotIndex};
      sameStateLength != 0) {
    actualNoteLength =
        printLength(out, prevSnapshotKeyOn, shouldPrintNoteLengthAfterLoop,
                    actualNoteLength, sameStateLength);
  }

  if (m_vgmHeader.loopEnabled()) {
    const std::optional<SnapshotType>& maybeActualSnapshot{
        maybePrevSnapshotWithKeyOn};

    if (maybeActualSnapshot.has_value()) {
      const SnapshotType& actualSnapshot{maybeActualSnapshot.value()};

      // Reset channel state before loop. It's simplier to handle than keep
      // snapshot state before loop
      printSnapshotDiff(out, maybeBeforeLoopSnapshot.value_or(SnapshotType{}),
                        actualSnapshot, channelIndex);
    }

    fmt::println(out, "\tsmpsJump\t{}",
                 m_loopFrame == 0 or not loopSetuped ? label : loopLabel);
  } else {
    fmt::println(out, "\tsmpsStop");
  }

  fmt::println(out, "");
}

void SmpsTextAdapter::printRetranslators(std::ostream& out) {
  fmt::println(out,
               "; Channel retranslators for avoiding compiler error about too "
               "long jump");

  bool anyRetranslatorPrinted{};
  static auto printRetranslator = [](std::ostream& out,
                                     const std::string& retranslatorLabel,
                                     const std::string& channelLabel) -> void {
    fmt::println(out, "{}", retranslatorLabel);
    fmt::println(out, "\tsmpsJump\t{}", channelLabel);
    fmt::println(out, "");
  };

  if (not m_labels.retranslators.dac.empty()) {
    printRetranslator(out, m_labels.retranslators.dac, m_labels.channels.dac);
    anyRetranslatorPrinted = true;
  }

  for (size_t channelIndex{0}; channelIndex != chips::YM2612::CHANNELS_COUNT;
       ++channelIndex) {
    if (not m_labels.retranslators.fm[channelIndex].empty()) {
      printRetranslator(out, m_labels.retranslators.fm[channelIndex],
                        m_labels.channels.fm[channelIndex]);
      anyRetranslatorPrinted = true;
    }
  }

  for (size_t channelIndex{0};
       channelIndex != chips::SN76489::PSG_CHANNELS_COUNT; ++channelIndex) {
    if (not m_labels.retranslators.psg[channelIndex].empty()) {
      printRetranslator(out, m_labels.retranslators.psg[channelIndex],
                        m_labels.channels.psg[channelIndex]);
      anyRetranslatorPrinted = true;
    }
  }

  if (not m_labels.retranslators.noise.empty()) {
    printRetranslator(out, m_labels.retranslators.noise,
                      m_labels.channels.noise);
    anyRetranslatorPrinted = true;
  }

  if (not anyRetranslatorPrinted) {
    fmt::println(out, "");
  }
}

void SmpsTextAdapter::printFmInstruments(std::ostream& out) {
  // Print voices
  fmt::println(out, "{}:", m_labels.voices);
  const auto fmInstruments{getFmInstruments()};

  for (size_t i{0}; i < fmInstruments.size(); ++i) {
    const chips::YM2612::Instrument& instrument{fmInstruments[i]};

    fmt::println(out, "; Voice {}", i);
    printFmInstrument(out, instrument);
    fmt::println(out, "");
  }
}

void SmpsTextAdapter::printDacChannel(std::ostream& out) {
  if (m_needPrintDac) {
    const auto& label{m_labels.channels.dac};
    const auto& loopLabel{m_labels.loop.dac};
    const auto& snapshots{getYM2612Snapshots().dac};

    fmt::println(out, "{}:", label);
    printSnapshots(out, label, loopLabel, snapshots);
  }

  if (m_shouldWriteRetranslators and
      ++m_printedChannelIndex == m_retranslatorChannelIndex) {
    printRetranslators(out);
  }
}

void SmpsTextAdapter::printFmChannels(std::ostream& out) {
  const bool shouldSkipFM6{
      (m_settings.driverVersion != SmpsDriverVersion::S3K) and
      m_settings.ym2612.dacEnabled and m_settings.preferDacAtFM6 and
      m_fm6ContainsDac};

  for (size_t channelIndex{0}; channelIndex < chips::YM2612::CHANNELS_COUNT;
       ++channelIndex) {
    if (m_needPrintFm.test(channelIndex)) {
      const auto& label{m_labels.channels.fm[channelIndex]};
      const auto& loopLabel{m_labels.loop.fm[channelIndex]};
      const auto& snapshots{getYM2612Snapshots().fm[channelIndex]};

      fmt::println(out, "{}:", label);
      printSnapshots(out, label, loopLabel, snapshots, channelIndex);
    }

    if (m_shouldWriteRetranslators and
        ++m_printedChannelIndex == m_retranslatorChannelIndex) {
      printRetranslators(out);
    }
  }
}

void SmpsTextAdapter::printPsgChannels(std::ostream& out) {
  for (size_t channelIndex{0};
       channelIndex <
       chips::SN76489::PSG_CHANNELS_COUNT - (not m_needPrintNoise ? 0 : 1);
       ++channelIndex) {
    if (m_needPrintPsg.test(channelIndex)) {
      const auto& label{m_labels.channels.psg[channelIndex]};
      const auto& loopLabel{m_labels.loop.psg[channelIndex]};
      const auto& snapshots{getSN76489Snapshots().psg[channelIndex]};

      fmt::println(out, "{}:", label);
      printSnapshots(out, label, loopLabel, snapshots, channelIndex);
    }

    if (m_shouldWriteRetranslators and
        ++m_printedChannelIndex == m_retranslatorChannelIndex) {
      printRetranslators(out);
    }
  }
}

void SmpsTextAdapter::printNoiseChannel(std::ostream& out) {
  if (m_needPrintNoise) {
    const auto& label{m_labels.channels.noise};
    const auto& loopLabel{m_labels.loop.noise};
    const auto& snapshots{getSN76489Snapshots().noise};

    fmt::println(out, "{}:", label);
    printSnapshots(out, label, loopLabel, snapshots,
                   chips::SN76489::NOISE_CHANNEL_INDEX);
  }

  if (m_shouldWriteRetranslators and
      ++m_printedChannelIndex == m_retranslatorChannelIndex) {
    printRetranslators(out);
  }
}

void SmpsTextAdapter::printSmpsPan(std::ostream& out,
                                   chips::YM2612::Panning panning) {
  fmt::println(out, "\tsmpsPan\t\t{}, 0", panningToString(panning));
}

void SmpsTextAdapter::printSmpsFms(std::ostream& out, int8_t fms,
                                   std::optional<int8_t>& prevFms) {
  if (fms == 0 and prevFms.has_value()) {
    fmt::println(out, "\tsmpsModOff");
    return;
  }

  if (fms == prevFms) {
    fmt::println(out, "\tsmpsModOn");
    return;
  }

  static constexpr std::array<uint8_t, 8> fmsToModAmp{
      0, 1, 2, 3, 3, 4, 7, 16,
  };

  // TODO: use normal AMS and FMS instead of this hack
  if (m_settings.driverVersion == SmpsDriverVersion::S3K) {
    fmt::println(out, "\tsmpsModSet\t1, 2, {}, 8", fmsToModAmp[fms]);
  } else {
    fmt::println(out, "\tsmpsModSet\t0, 2, {}, 3", fmsToModAmp[fms]);
  }

  prevFms = fms;
}

auto SmpsTextAdapter::printLength(std::ostream& out, bool isKeyOn,
                                  bool forcePrint, uint8_t actualNoteLength,
                                  uint32_t length) -> uint8_t {
  constexpr uint8_t MAX_SMPS_NOTE_LENGTH{0x7F};

  if (not forcePrint and length == actualNoteLength) {
    fmt::println(out, "");
    return actualNoteLength;
  }

  for (; length > MAX_SMPS_NOTE_LENGTH; length -= MAX_SMPS_NOTE_LENGTH) {
    fmt::print(out, ", ${:02X}", MAX_SMPS_NOTE_LENGTH);

    if (isKeyOn) {
      fmt::print(out, ", smpsNoAttack");
    } else {
      fmt::print(out, ", nRst");
    }
  }

  actualNoteLength = length;
  fmt::println(out, ", ${:02X}", actualNoteLength);
  return actualNoteLength;
}

void SmpsTextAdapter::printSmpsSetVoice(std::ostream& out,
                                        uint8_t instrumentIndex) {
  fmt::println(out, "\tsmpsSetvoice\t{}", instrumentIndex);
}

void SmpsTextAdapter::printSmpsAlterVol(std::ostream& out, int8_t diff) {
  fmt::println(out, "\tsmpsAlterVol\t{}", diff);
}

void SmpsTextAdapter::printSmpsPSGAlterVol(std::ostream& out, int8_t diff) {
  fmt::println(out, "\tsmpsPSGAlterVol\t{}", diff);
}

void SmpsTextAdapter::printNote(std::ostream& out, const SimpleNote& note) {
  static const std::array<std::string, NOTES_IN_OCTAVE> tonesBase{
      "C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B",
  };

  fmt::print(out, "\tdc.b\tn{}{}", tonesBase[note.key], note.octave);
}

void SmpsTextAdapter::printNoteOff(std::ostream& out) {
  fmt::print(out, "\tdc.b\tnRst");
}

void SmpsTextAdapter::printDacSample(std::ostream& out, uint8_t sampleIndex) {
  constexpr uint8_t MAX_NOTE_VALUE{MAX_YM2612_BLOCK * NOTES_IN_OCTAVE};

  if (sampleIndex > MAX_NOTE_VALUE) {
    SPDLOG_WARN(
        "Sample index is out of range: {:02X}. Sample will be incorrect.",
        sampleIndex);
    sampleIndex %= MAX_NOTE_VALUE;
  }

  const SimpleNote note{
      .octave = static_cast<uint8_t>(sampleIndex / NOTES_IN_OCTAVE),
      .key = static_cast<uint8_t>(sampleIndex % NOTES_IN_OCTAVE),
  };

  // Print a note macro instead of DAC sample macro
  // TODO: use DAC sample mapping from user in GUI or CLI
  printNote(out, note);
}

void SmpsTextAdapter::printSmpsAlterNote(std::ostream& out,
                                         int frequencyOffset) {
  fmt::println(out, "\tsmpsAlterNote {}", frequencyOffset);
}

auto SmpsTextAdapter::panningToString(chips::YM2612::Panning panning)
    -> std::string_view {
  switch (panning) {
    case chips::YM2612::Panning::none:
      return "panNone";
    case chips::YM2612::Panning::right:
      return "panRight";
    case chips::YM2612::Panning::left:
      return "panLeft";
    case chips::YM2612::Panning::center:
      return "panCentre";
  }

  return "panNone";
}

auto SmpsTextAdapter::driverVersionToString(SmpsDriverVersion driverVersion)
    -> std::string_view {
  switch (driverVersion) {
    case SmpsDriverVersion::S1:
      return "1";
    case SmpsDriverVersion::S2:
      return "2";
    case SmpsDriverVersion::S3K:
      return "3";
    default:
      break;
  }

  return "1";
}

void SmpsTextAdapter::printSmpsPSGform(std::ostream& out, int psgForm) {
  fmt::println(out, "\tsmpsPSGform ${:02X}", psgForm);
}

void SmpsTextAdapter::printNoteFromSnapshot(
    std::ostream& out, const SN76489NoiseChannelSnapshot& snapshot) {
  if (snapshot.fullRangeNoiseMode) {
    printNoteFromSnapshot(
        out, static_cast<const SN76489PsgChannelSnapshot&>(snapshot));
    return;
  }

  const int psgForm{0xE0 + (snapshot.whiteNoiseBit ? 4 : 0) +
                    snapshot.nativeTone};
  printSmpsPSGform(out, psgForm);
  fmt::print(out, "\tdc.b\tnMaxPSG");
}

void SmpsTextAdapter::printSnapshotDiff(
    std::ostream& out, const YM2612FMChannelSnapshot& snapshot,
    const YM2612FMChannelSnapshot& prevSnapshot, size_t channelIndex) {
  if (snapshot.instrumentIndex != prevSnapshot.instrumentIndex) {
    printSmpsSetVoice(out, snapshot.instrumentIndex);
  }

  if (snapshot.fms != prevSnapshot.fms) {
    printSmpsFms(out, snapshot.fms, m_prevFms[channelIndex]);
  }

  if (snapshot.panning != prevSnapshot.panning) {
    printSmpsPan(out, snapshot.panning);
  }

  if (snapshot.volume != prevSnapshot.volume) {
    const int8_t diff{
        static_cast<int8_t>(snapshot.volume - prevSnapshot.volume)};
    printSmpsAlterVol(out, diff);
  }

  if (snapshot.frequencyOffset != prevSnapshot.frequencyOffset) {
    printSmpsAlterNote(out, snapshot.frequencyOffset);
  }
}

void SmpsTextAdapter::printInitialSnapshot(
    std::ostream& out, const YM2612FMChannelSnapshot& snapshot,
    size_t channelIndex) {
  printSmpsSetVoice(out, snapshot.instrumentIndex);
  printSmpsFms(out, snapshot.fms, m_prevFms[channelIndex]);
  printSmpsPan(out, snapshot.panning);

  if (snapshot.volume != 0) {
    printSmpsAlterVol(out, snapshot.volume);
  }

  if (snapshot.frequencyOffset != 0) {
    printSmpsAlterNote(out, snapshot.frequencyOffset);
  }
}

void SmpsTextAdapter::printSnapshotDiff(
    std::ostream& out, const YM2612DacChannelSnapshot& snapshot,
    const YM2612DacChannelSnapshot& prevSnapshot, size_t /*channelIndex*/) {
  if (snapshot.panning != prevSnapshot.panning) {
    printSmpsPan(out, snapshot.panning);
  }
}

void SmpsTextAdapter::printSnapshotDiff(
    std::ostream& out, const SN76489PsgChannelSnapshot& snapshot,
    const SN76489PsgChannelSnapshot& prevSnapshot, size_t /*channelIndex*/) {
  if (snapshot.volume != prevSnapshot.volume) {
    printSmpsPSGAlterVol(
        out, static_cast<int8_t>(snapshot.volume - prevSnapshot.volume));
  }

  if (snapshot.frequencyOffset != prevSnapshot.frequencyOffset) {
    printSmpsAlterNote(out, snapshot.frequencyOffset);
  }
}

void SmpsTextAdapter::printSnapshotDiff(
    std::ostream& out, const SN76489NoiseChannelSnapshot& snapshot,
    const SN76489NoiseChannelSnapshot& prevSnapshot, size_t channelIndex) {
  if (snapshot.fullRangeNoiseMode and
      (snapshot.whiteNoiseBit != prevSnapshot.whiteNoiseBit)) {
    printSmpsPSGform(
        out, snapshot.whiteNoiseBit ? NOISE_MODE_WHITE : NOISE_MODE_PERIODIC);
  }

  printSnapshotDiff(out,
                    static_cast<const SN76489PsgChannelSnapshot&>(snapshot),
                    static_cast<const SN76489PsgChannelSnapshot&>(prevSnapshot),
                    channelIndex);
}

void SmpsTextAdapter::printInitialSnapshot(
    std::ostream& out, const SN76489PsgChannelSnapshot& snapshot,
    size_t channelIndex) {
  printSnapshotDiff(out, snapshot, SN76489PsgChannelSnapshot{}, channelIndex);
}

void SmpsTextAdapter::printInitialSnapshot(
    std::ostream& out, const SN76489NoiseChannelSnapshot& snapshot,
    size_t channelIndex) {
  if (snapshot.fullRangeNoiseMode) {
    printSmpsPSGform(
        out, snapshot.whiteNoiseBit ? NOISE_MODE_WHITE : NOISE_MODE_PERIODIC);
  }

  printInitialSnapshot(out,
                       static_cast<const SN76489PsgChannelSnapshot&>(snapshot),
                       channelIndex);
}

void SmpsTextAdapter::printFmInstrument(
    std::ostream& out, const chips::YM2612::Instrument& instrument) {
  // Print FM Voice (YM2612 Instrument) according to SMPS2ASM macro example:
  // smpsVcAlgorithm     $00
  // smpsVcFeedback      $03
  // smpsVcUnusedBits    $00
  // smpsVcDetune        $03, $03, $03, $03
  // smpsVcCoarseFreq    $01, $00, $00, $07
  // smpsVcRateScale     $02, $00, $03, $02
  // smpsVcAttackRate    $1C, $1C, $1C, $1E
  // smpsVcAmpMod        $00, $00, $00, $00
  // smpsVcDecayRate1    $01, $04, $06, $0D
  // smpsVcDecayRate2    $05, $03, $0A, $08
  // smpsVcDecayLevel    $02, $03, $0B, $0B
  // smpsVcReleaseRate   $0F, $0F, $0F, $0F
  // smpsVcTotalLevel    $00, $14, $22, $2C
  // , but with decimal values

  static constexpr auto printSmpsArray = [](std::ostream& out,
                                            auto const& array) {
    // I hope 🤞🌞🙏 C++ compiler will inline this
    fmt::print(out, "{:n:2}", std::views::reverse(array));
  };

  std::array<decltype(chips::YM2612::Operator::dt),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcDetune{};
  std::array<decltype(chips::YM2612::Operator::mul),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcCoarseFreq{};
  std::array<decltype(chips::YM2612::Operator::rs),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcRateScale{};
  std::array<decltype(chips::YM2612::Operator::ar),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcAttackRate{};
  std::array<uint8_t, chips::YM2612::OPERATORS_COUNT> smpsVcAmpMod{};
  std::array<decltype(chips::YM2612::Operator::d1r),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcDecayRate1{};
  std::array<decltype(chips::YM2612::Operator::d2r),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcDecayRate2{};
  std::array<decltype(chips::YM2612::Operator::sl),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcDecayLevel{};
  std::array<decltype(chips::YM2612::Operator::rr),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcReleaseRate{};
  std::array<decltype(chips::YM2612::Operator::tl),
             chips::YM2612::OPERATORS_COUNT>
      smpsVcTotalLevel{};

  for (size_t i{0}; i < instrument.operators.size(); ++i) {
    const chips::YM2612::Operator& operator_{instrument.operators[i]};

    smpsVcDetune[i] = operator_.dt;
    smpsVcCoarseFreq[i] = operator_.mul;
    smpsVcRateScale[i] = operator_.rs;
    smpsVcAttackRate[i] = operator_.ar;
    smpsVcAmpMod[i] = operator_.am ? 1 : 0;
    smpsVcDecayRate1[i] = operator_.d1r;
    smpsVcDecayRate2[i] = operator_.d2r;
    smpsVcDecayLevel[i] = operator_.sl;
    smpsVcReleaseRate[i] = operator_.rr;
    smpsVcTotalLevel[i] = operator_.tl;
  }

  fmt::println(out, "\t{:20}{:2}", "smpsVcAlgorithm", instrument.algorithm);
  fmt::println(out, "\t{:20}{:2}", "smpsVcFeedback", instrument.feedback);
  fmt::println(out, "\t{:20}{:2}", "smpsVcUnusedBits", 0);

  fmt::print(out, "\t{:20}", "smpsVcDetune");
  printSmpsArray(out, smpsVcDetune);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcCoarseFreq");
  printSmpsArray(out, smpsVcCoarseFreq);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcRateScale");
  printSmpsArray(out, smpsVcRateScale);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcAttackRate");
  printSmpsArray(out, smpsVcAttackRate);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcAmpMod");
  printSmpsArray(out, smpsVcAmpMod);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcDecayRate1");
  printSmpsArray(out, smpsVcDecayRate1);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcDecayRate2");
  printSmpsArray(out, smpsVcDecayRate2);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcDecayLevel");
  printSmpsArray(out, smpsVcDecayLevel);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcReleaseRate");
  printSmpsArray(out, smpsVcReleaseRate);
  fmt::println(out, "");
  fmt::print(out, "\t{:20}", "smpsVcTotalLevel");
  printSmpsArray(out, smpsVcTotalLevel);
  fmt::println(out, "");
}

void SmpsTextAdapter::writeTo(std::ostream& out) {
  uint32_t songId;

  {
    std::random_device generator;
    std::uniform_int_distribution<uint32_t> distribution;
    songId = distribution(generator);
  }

  m_labels.songId = std::to_string(songId);
  m_labels.voices = fmt::format("Mus_{}_Voices", m_labels.songId);
  m_labels.empty = fmt::format("Mus_{}_Empty", m_labels.songId);

  const bool hasFm{m_ym2612 != nullptr};
  const bool hasPsg{m_sn76489 != nullptr};

  const bool preferNoise{m_settings.sn76489.channelsEnabled.test(
      chips::SN76489::NOISE_CHANNEL_INDEX)};
  m_needPrintNoise = preferNoise and getSN76489Snapshots().contains.noise;
  const bool shouldSkipDac{
      not m_settings.ym2612.dacEnabled or
      (m_settings.driverVersion != SmpsDriverVersion::S3K and
       (not m_settings.preferDacAtFM6 and
        getYM2612Snapshots().contains.fm.test(chips::YM2612::CHANNELS_COUNT -
                                              1)))};
  const bool shouldSkipFM6{
      (m_settings.driverVersion != SmpsDriverVersion::S3K) and
      m_settings.ym2612.dacEnabled and m_settings.preferDacAtFM6 and
      m_fm6ContainsDac};

  if (m_shouldWriteRetranslators) {
    size_t usedChannelsCount{0};
    usedChannelsCount +=
        not shouldSkipDac and getYM2612Snapshots().contains.dac ? 1 : 0;
    usedChannelsCount += getYM2612Snapshots().contains.fm.count();
    usedChannelsCount += getSN76489Snapshots().contains.psg.count();
    usedChannelsCount +=
        preferNoise and getSN76489Snapshots().contains.noise ? 1 : 0;

    // Put retranslator at the middle of the file
    m_retranslatorChannelIndex = usedChannelsCount / 2;
    m_shouldWriteRetranslators = m_retranslatorChannelIndex != 0;
  }

  fmt::println(out, "Mus_{}_Header:", m_labels.songId);
  fmt::println(out, "\tsmpsHeaderStartSong {}",
               driverVersionToString(m_settings.driverVersion));
  fmt::println(out, "\tsmpsHeaderVoice\t{}", m_labels.voices);
  fmt::println(out, "\tsmpsHeaderChan\t7, 3");
  fmt::print(out, "\tsmpsHeaderTempo\t");

  if (m_settings.driverVersion == SmpsDriverVersion::S2) {
    // Workaround. Tested in s2disam and it works with this values
    fmt::println(out, "1, {}", m_vgmHeader.rate * 4);
  } else {
    // S1 and S3K works with this.
    fmt::println(out, "1, 0");
  }

  fmt::println(out, "");

  if (not shouldSkipDac and getYM2612Snapshots().contains.dac) {
    m_needPrintDac = true;
    m_labels.channels.dac = fmt::format("Mus_{}_DAC", m_labels.songId);

    if (m_shouldWriteRetranslators) {
      m_labels.retranslators.dac =
          fmt::format("{}_Retranslator", m_labels.channels.dac);
    }

    fmt::println(out, "\tsmpsHeaderDAC\t{}",
                 m_shouldWriteRetranslators ? m_labels.retranslators.dac
                                            : m_labels.channels.dac);

    if (m_loopFrame != 0) {
      m_labels.loop.dac = fmt::format("{}_Loop", m_labels.channels.dac);
    }
  } else {
    fmt::println(out, "\tsmpsHeaderDAC\t{}", m_labels.empty);
  }

  for (size_t i{0}; i < chips::YM2612::CHANNELS_COUNT; ++i) {
    const bool isFM6{i == chips::YM2612::CHANNELS_COUNT - 1};

    if (hasFm and m_settings.ym2612.channelsEnabled.test(i) and
        getYM2612Snapshots().contains.fm.test(i) and
        not(shouldSkipFM6 and isFM6)) {
      m_needPrintFm.set(i);
      m_labels.channels.fm[i] =
          fmt::format("Mus_{}_FM{}", m_labels.songId, i + 1);

      if (m_loopFrame != 0) {
        m_labels.loop.fm[i] = fmt::format("{}_Loop", m_labels.channels.fm[i]);
      }

      if (m_shouldWriteRetranslators) {
        m_labels.retranslators.fm[i] =
            fmt::format("{}_Retranslator", m_labels.channels.fm[i]);
      }

      fmt::println(out, "\tsmpsHeaderFM\t{},\t0, 0",
                   m_shouldWriteRetranslators ? m_labels.retranslators.fm[i]
                                              : m_labels.channels.fm[i]);
    } else {
      fmt::println(out, "\tsmpsHeaderFM\t{},\t0, 0", m_labels.empty);
    }
  }

  if (hasPsg) {
    for (size_t i{0};
         i < chips::SN76489::PSG_CHANNELS_COUNT - (m_needPrintNoise ? 1 : 0);
         ++i) {
      if (m_settings.sn76489.channelsEnabled.test(i) and
          getSN76489Snapshots().contains.psg.test(i)) {
        m_needPrintPsg.set(i);
        m_labels.channels.psg[i] =
            fmt::format("Mus_{}_PSG{}", m_labels.songId, i + 1);

        if (m_loopFrame != 0) {
          m_labels.loop.psg[i] =
              fmt::format("{}_Loop", m_labels.channels.psg[i]);
        }

        if (m_shouldWriteRetranslators) {
          m_labels.retranslators.psg[i] =
              fmt::format("{}_Retranslator", m_labels.channels.psg[i]);
        }

        fmt::println(out, "\tsmpsHeaderPSG\t{},\t0, 0, 0, 0",
                     m_shouldWriteRetranslators ? m_labels.retranslators.psg[i]
                                                : m_labels.channels.psg[i]);
      } else {
        fmt::println(out, "\tsmpsHeaderPSG\t{},\t0, 0, 0, 0", m_labels.empty);
      }
    }

    if (m_needPrintNoise) {
      m_labels.channels.noise = fmt::format("Mus_{}_Noise", m_labels.songId);

      if (m_loopFrame != 0) {
        m_labels.loop.noise = fmt::format("{}_Loop", m_labels.channels.noise);
      }

      if (m_shouldWriteRetranslators) {
        m_labels.retranslators.noise =
            fmt::format("{}_Retranslator", m_labels.channels.noise);
      }

      fmt::println(out, "\tsmpsHeaderPSG\t{},\t0, 0, 0, 0",
                   m_shouldWriteRetranslators ? m_labels.retranslators.noise
                                              : m_labels.channels.noise);
    }
  } else {
    for (size_t i{0}; i < chips::SN76489::PSG_CHANNELS_COUNT; ++i) {
      fmt::println(out, "\tsmpsHeaderPSG\t{},\t0, 0, 0, 0", m_labels.empty);
    }
  }

  fmt::println(out, "");

  fmt::println(out, "; Empty channel");
  fmt::println(out, "{}:", m_labels.empty);
  fmt::println(out, "\tsmpsStop");
  fmt::println(out, "");

  printFmInstruments(out);
  printDacChannel(out);
  printFmChannels(out);
  printPsgChannels(out);
  printNoiseChannel(out);
}

}  // namespace vgmc
