#include "SmpsAdapterBase.hxx"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <tuple>

constexpr bool disableSafeCheck{true};

namespace vgmc {
SmpsAdapterBase::SmpsAdapterBase(const SmpsAdapterBaseSettings& settings)
    : CompositeAdapter{
          std::make_tuple(YM2612SnapshotLogger{m_settings.ym2612},
                          SN76489SnapshotLogger{m_settings.sn76489})},
      m_settings{settings},
      m_psgOctaveDownOffset{
          m_settings.driverVersion == SmpsDriverVersion::S3K ? 0U : 1U} {}

void SmpsAdapterBase::onHeaderParsed(const VgmParseContext& ctx,
                                     const HeaderParsedEvent& event) {
  broadcastHeaderParsed(ctx, event);

  if (event.chips.sn76489.has_value()) {
    m_sn76489 = &event.chips.sn76489.value();
  }

  if (event.chips.ym2612.has_value()) {
    m_ym2612 = &event.chips.ym2612.value();
  }

  if (event.header.loopEnabled()) {
    m_loopFrame =
        static_cast<uint32_t>(std::floor(event.header.getLoopFrame()));
  }

  m_vgmHeader = event.header;
}

void SmpsAdapterBase::onTickAdvance(const VgmParseContext& ctx,
                                    const TickAdvanceEvent& event) {
  broadcastTickAdvance(ctx, event);
}

void SmpsAdapterBase::onEndOfData(const VgmParseContext& ctx) {
  broadcastEndOfData(ctx);
}

void SmpsAdapterBase::onDataBlockExtracted(
    const VgmParseContext& ctx, const DataBlockExtractedEvent& event) {
  broadcastDataBlockExtracted(ctx, event);
}

void SmpsAdapterBase::onYM2612PcmSeek(const VgmParseContext& ctx,
                                      const YM2612PcmSeekEvent& event) {
  broadcastYM2612PcmSeek(ctx, event);
}

void SmpsAdapterBase::onYM2612PcmDelay(const VgmParseContext& ctx,
                                       const YM2612PcmDelayEvent& event) {
  broadcastYM2612PcmDelay(ctx, event);
}

void SmpsAdapterBase::onDacStreamSetup(const VgmParseContext& ctx,
                                       const DacStreamSetupEvent& event) {
  broadcastDacStreamSetup(ctx, event);
}

void SmpsAdapterBase::onDacStreamPlay(const VgmParseContext& ctx,
                                      const DacStreamPlayEvent& event) {
  broadcastDacStreamPlay(ctx, event);
}

void SmpsAdapterBase::findBestNoteAndTransform(
    YM2612FMChannelSnapshot& snapshot) {
  constexpr int MAX_FREQUENCY_OFFSET{std::numeric_limits<int8_t>::max()};
  constexpr int MIN_FREQUENCY_OFFSET{std::numeric_limits<int8_t>::min() + 1};
  constexpr int MIN_SMPS_FREQUENCY{fmFrequencyTable.front() +
                                   MIN_FREQUENCY_OFFSET};
  constexpr int MAX_SMPS_FREQUENCY{fmFrequencyTable.back() +
                                   MAX_FREQUENCY_OFFSET};

  int frequency{snapshot.nativeTone.frequency};
  unsigned block{snapshot.nativeTone.block};
  int octaveOffset{};
  int minOffset{};

  auto closestIt{fmFrequencyTable.cbegin()};
  bool noteFound{false};

  while (true) {
    if (frequency < MIN_SMPS_FREQUENCY) {
      // Corner-case: frequency lower than C0 and alterNote will not help
      if (block == 0) {
        const auto& instrument{getFmInstruments()[snapshot.instrumentIndex]};

        if (disableSafeCheck or instrument.canDownOctaveSafely()) {
          auto const newInstrument{instrument.downOctave()};
          snapshot.instrumentIndex = findOrPushNewFmInstrument(newInstrument);
          frequency *= 2;
          continue;
        }

        // It's over
        snapshot.isKeyOn = false;
        snapshot.nativeTone = {};
        return;
      }

      frequency *= 2;
      block--;
      continue;
    }

    // Corner-case: frequency upper than B7 and alterNote will not help
    if (frequency > MAX_SMPS_FREQUENCY) {
      if (block == MAX_YM2612_BLOCK) {
        const auto& instrument{getFmInstruments()[snapshot.instrumentIndex]};

        if (disableSafeCheck or instrument.canUpOctaveSafely()) {
          auto const newInstrument{instrument.upOctave()};
          snapshot.instrumentIndex = findOrPushNewFmInstrument(newInstrument);
          frequency /= 2;
          continue;
        }

        // It's over
        snapshot.isKeyOn = false;
        snapshot.nativeTone = {};
        return;
      }

      frequency /= 2;
      block++;
      continue;
    }

    // If VGM's frequency in SMPS range, than vse zaebis, sunny case detected 🌞
    closestIt = std::ranges::lower_bound(fmFrequencyTable, frequency);

    auto comp = [frequency](auto lhs, auto rhs) -> bool {
      return std::abs(*lhs - frequency) < std::abs(*rhs - frequency);
    };

    if (closestIt == fmFrequencyTable.cbegin()) {
      closestIt = std::min(closestIt, std::next(closestIt), comp);
    } else if (closestIt == fmFrequencyTable.cend()) {
      closestIt = std::prev(closestIt);
    } else {
      closestIt = std::min(closestIt, std::prev(closestIt), comp);
    }

    minOffset = frequency - *closestIt;
    snapshot.note.key = static_cast<uint8_t>(
        std::distance(fmFrequencyTable.cbegin(), closestIt));
    snapshot.note.octave = static_cast<uint8_t>(block);
    snapshot.frequencyOffset = static_cast<int16_t>(minOffset);

    // If B7 Note
    if (snapshot.note.octave == MAX_YM2612_BLOCK and
        snapshot.note.key == (NOTES_IN_OCTAVE - 1)) {
      // B7 is not supported in SMPS 🤢
      const auto& instrument{getFmInstruments()[snapshot.instrumentIndex]};

      if (disableSafeCheck or instrument.canUpOctaveSafely()) {
        auto const newInstrument{instrument.upOctave()};
        snapshot.instrumentIndex = findOrPushNewFmInstrument(newInstrument);
        snapshot.note.octave--;
      } else {
        // It's over
        snapshot.isKeyOn = false;
        snapshot.nativeTone = {};
        return;
      }
    }

    break;
  }
}

void SmpsAdapterBase::findBestNoteAndTransform(
    SN76489PsgChannelSnapshot& snapshot) {
  const auto foundFrequencyIt{std::ranges::lower_bound(
      std::views::reverse(psgFrequencies), snapshot.nativeTone)};
  std::ptrdiff_t noteIndex;

  if (foundFrequencyIt == psgFrequencies.crbegin()) {
    noteIndex = psgFrequencies.size() - 1;
  } else {
    noteIndex = std::distance(foundFrequencyIt, psgFrequencies.crend()) - 1;
  }

  snapshot.note.key = noteIndex % NOTES_IN_OCTAVE;
  snapshot.note.octave = (noteIndex / NOTES_IN_OCTAVE);
  snapshot.frequencyOffset =
      static_cast<int8_t>(snapshot.nativeTone - *foundFrequencyIt);

  if (m_psgOctaveDownOffset != 0U) {
    if (snapshot.note.octave > m_psgOctaveDownOffset) {
      snapshot.note.octave -= m_psgOctaveDownOffset;
    } else {
      snapshot.note.octave = 0;
    }
  }
}

void SmpsAdapterBase::findBestNoteAndTransform(
    SN76489NoiseChannelSnapshot& snapshot) {
  if (snapshot.fullRangeNoiseMode) {
    findBestNoteAndTransform(static_cast<SN76489PsgChannelSnapshot&>(snapshot));
    return;
  }

  // If noise without full range mode, keep native tone unchanged
}

// It's an attempt to implement silence detection
// void SmpsAdapterBase::onDetectFirstChipChange(const TickAdvanceEvent& event)
// {
//   if (m_firstNoteTickDetected) {
//     return;
//   }

//   m_firstNoteTickDetected = true;
//   m_firstNoteTick = event.tick;
// }

// auto SmpsAdapterBase::getFirstChipChangeDetected() const noexcept -> bool {
//   return m_firstNoteTickDetected;
// }

// auto SmpsAdapterBase::getFirstNoteTick() const noexcept -> uint32_t {
//   return m_firstNoteTick;
// }

void SmpsAdapterBase::onYM2612ChangeDacEnabledState(
    const chips::YM2612::Context& context) {
  if (not m_fm6ContainsDac and context.dacEnabled) {
    m_fm6ContainsDac = true;
  }
}

auto SmpsAdapterBase::shouldKeepSnapshot(
    YM2612FMChannelSnapshot const& currentSnapshot,
    YM2612FMChannelSnapshot const& prevSnapshot) noexcept -> bool {
  if (currentSnapshot.isKeyOn and prevSnapshot.isKeyOn) {
    const bool significantChange =
        currentSnapshot.nativeTone != prevSnapshot.nativeTone or
        currentSnapshot.instrumentIndex != prevSnapshot.instrumentIndex or
        currentSnapshot.panning != prevSnapshot.panning or
        currentSnapshot.volume != prevSnapshot.volume or
        currentSnapshot.fms != prevSnapshot.fms or
        currentSnapshot.ams != prevSnapshot.ams;

    return significantChange or
           currentSnapshot.keyPlayMode == KeyPlayMode::attack;
  }

  return currentSnapshot.isKeyOn != prevSnapshot.isKeyOn;
}

auto SmpsAdapterBase::shouldKeepSnapshot(
    SN76489PsgChannelSnapshot const& currentSnapshot,
    SN76489PsgChannelSnapshot const& prevSnapshot) noexcept -> bool {
  if (currentSnapshot.volume != chips::SN76489::MIN_VOLUME) {
    return currentSnapshot.volume != prevSnapshot.volume or
           currentSnapshot.nativeTone != prevSnapshot.nativeTone;
  }

  return currentSnapshot.volume != prevSnapshot.volume;
}

auto SmpsAdapterBase::shouldKeepSnapshot(
    SN76489NoiseChannelSnapshot const& currentSnapshot,
    SN76489NoiseChannelSnapshot const& prevSnapshot) noexcept -> bool {
  if (shouldKeepSnapshot(
          static_cast<const SN76489PsgChannelSnapshot&>(currentSnapshot),
          static_cast<const SN76489PsgChannelSnapshot&>(prevSnapshot))) {
    return true;
  }

  return currentSnapshot.fullRangeNoiseMode !=
             prevSnapshot.fullRangeNoiseMode or
         currentSnapshot.whiteNoiseBit != prevSnapshot.whiteNoiseBit;
}

void SmpsAdapterBase::filterInstrumentData(
    chips::YM2612::Instrument& instrument) {
  // SSGEG is (still) not supperted in SMPS 😐
  for (auto&& operator_ : instrument.operators) {
    operator_.ssgeg = 0;
  }
}

void SmpsAdapterBase::filterSnapshotData(YM2612FMChannelSnapshot& snapshot) {
  // AMS is not supported in SMPS
  snapshot.ams = 0;
  // FMS is not supported natively too, but we will imitate FMS via SMPS
  // modulation
}
}  // namespace vgmc
