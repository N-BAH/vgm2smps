#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "chips/SN76489/SN76489.hxx"
#include "utils/NoteUtils.hxx"
#include "vgm_parser/VgmEvents.hxx"

namespace vgmc {
struct SN76489SnapshotLoggerSettings {
  std::bitset<chips::SN76489::CHANNELS_COUNT> channelsEnabled{"1111"};
};

struct SN76489PsgChannelSnapshot {
  uint16_t nativeTone{};
  uint8_t volume{};
  SimpleNote note{};
  int8_t frequencyOffset{};

  [[nodiscard]] auto isKeyOn() const noexcept -> bool {
    return volume != chips::SN76489::MIN_VOLUME;
  }

  auto operator==(const SN76489PsgChannelSnapshot& other) const noexcept
      -> bool = default;
};

struct SN76489NoiseChannelSnapshot : public SN76489PsgChannelSnapshot {
  bool fullRangeNoiseMode;
  bool whiteNoiseBit;

  auto operator==(const SN76489NoiseChannelSnapshot& other) const noexcept
      -> bool = default;
};

struct SN76489Snapshots {
  std::array<std::vector<std::optional<SN76489PsgChannelSnapshot>>,
             chips::SN76489::PSG_CHANNELS_COUNT>
      psg;

  std::vector<std::optional<SN76489NoiseChannelSnapshot>> noise;

  struct {
    std::bitset<chips::SN76489::PSG_CHANNELS_COUNT> psg;
    bool noise;
  } contains;
};

template <typename OutputAdapter>
class SN76489SnapshotLogger {
 public:
  SN76489SnapshotLogger(const SN76489SnapshotLoggerSettings& settings);

  void onHeaderParsed(const VgmParseContext& ctx,
                      const HeaderParsedEvent& event);
  void onTickAdvance(const VgmParseContext& ctx, const TickAdvanceEvent& event);

  [[nodiscard]] auto getSN76489Snapshots() const noexcept -> const auto& {
    return m_snapshots;
  }

  [[nodiscard]] auto getSN76489Snapshots() noexcept -> auto& {
    return m_snapshots;
  }

 private:
  chips::SN76489* m_sn76489;
  const SN76489SnapshotLoggerSettings& m_settings;
  const VgmHeader* m_vgmHeader;
  uint32_t m_snapshotsCount;
  SN76489Snapshots m_snapshots{};

  struct {
    std::array<size_t, chips::SN76489::PSG_CHANNELS_COUNT> psg;
    size_t noise;
  } m_prevSnapshotIndices;

  bool m_firstDacPlayDetected{false};
  uint32_t m_dacOnlyChangesCounter{};

  uint32_t m_currentFrame{};
  uint32_t m_prevFrame{};
  uint32_t m_tick{};
  uint32_t m_prevTick{};

  [[nodiscard]] auto asOutputAdapter() -> OutputAdapter* {
    return static_cast<OutputAdapter*>(this);
  }

  [[nodiscard]] auto asOutputAdapter() const -> const OutputAdapter* {
    return static_cast<const OutputAdapter*>(this);
  }

  [[nodiscard]] auto getSnapshotIndex() const noexcept -> auto {
    return m_prevFrame;
  }

  void preallocateSnapshots(size_t count);
  void processContextChanged(const chips::SN76489::Context& context);
  void grabPsgSnapshotsFromContext(const chips::SN76489::Context& context);
  void grabNoiseSnapshotsFromContext(const chips::SN76489::Context& context);
};

template <typename OutputAdapter>
SN76489SnapshotLogger<OutputAdapter>::SN76489SnapshotLogger(
    const SN76489SnapshotLoggerSettings& settings)
    : m_settings{settings} {}

template <typename OutputAdapter>
void SN76489SnapshotLogger<OutputAdapter>::onHeaderParsed(
    const VgmParseContext& ctx, const HeaderParsedEvent& event) {
  if (not event.chips.sn76489.has_value()) {
    return;
  }

  m_sn76489 = &event.chips.sn76489.value();
  m_vgmHeader = &event.header;
  m_snapshotsCount =
      static_cast<uint32_t>(std::ceil(event.header.getTotalLengthInFrames()));

  if constexpr (requires(const OutputAdapter& adapter) {
                  adapter.getMaxSnapshotsCount();
                }) {
    if (const uint32_t maxSnapshotsCount{
            asOutputAdapter()->getMaxSnapshotsCount()};
        maxSnapshotsCount != 0) {
      m_snapshotsCount = std::min(m_snapshotsCount, maxSnapshotsCount);
    }
  }

  preallocateSnapshots(m_snapshotsCount);
}

template <typename OutputAdapter>
void SN76489SnapshotLogger<OutputAdapter>::onTickAdvance(
    const VgmParseContext& ctx, const TickAdvanceEvent& event) {
  if (m_sn76489 == nullptr or m_vgmHeader == nullptr) {
    return;
  }

  // If we want to track first note tick offset for frame alignment in
  // OutputAdapter
  if constexpr (requires(OutputAdapter& adapter) {
                  adapter.onDetectFirstChipChange(event);
                  adapter.getFirstChipChangeDetected();
                  adapter.getFirstNoteTick();
                }) {
    if (not asOutputAdapter()->getFirstChipChangeDetected()) {
      const auto& noiseChannel{m_sn76489->getContext().noise};

      if (std::ranges::any_of(
              m_sn76489->getContext().psg,
              [](const chips::SN76489::Channel& channel) -> bool {
                return channel.volume != chips::SN76489::MIN_VOLUME and
                       channel.tone != 0;
              }) or
          (noiseChannel.volume != chips::SN76489::MIN_VOLUME and
           noiseChannel.tone != 0)) {
        asOutputAdapter()->onDetectFirstChipChange(event);
      } else {
        return;
      }
    }

    m_tick = event.tick - asOutputAdapter()->getFirstNoteTick();
  } else {
    m_tick = event.tick;
  }

  m_currentFrame =
      static_cast<uint32_t>(std::floor(m_vgmHeader->tickToFrame(m_tick)));

  if (m_currentFrame == m_prevFrame or m_currentFrame >= m_snapshotsCount) {
    return;
  }

  processContextChanged(m_sn76489->getContext());

  m_prevTick = m_tick;
  m_prevFrame = m_currentFrame;
}

template <typename OutputAdapter>
void SN76489SnapshotLogger<OutputAdapter>::preallocateSnapshots(size_t count) {
  if (m_sn76489 == nullptr) {
    return;
  }

  for (size_t channelIndex{0};
       channelIndex < chips::SN76489::PSG_CHANNELS_COUNT; ++channelIndex) {
    if (m_settings.channelsEnabled.test(channelIndex)) {
      m_snapshots.psg[channelIndex].resize(count);
    }
  }

  if (m_settings.channelsEnabled.test(chips::SN76489::NOISE_CHANNEL_INDEX)) {
    m_snapshots.noise.resize(count);
  }
}

template <typename OutputAdapter>
void SN76489SnapshotLogger<OutputAdapter>::processContextChanged(
    const chips::SN76489::Context& context) {
  grabPsgSnapshotsFromContext(context);
  grabNoiseSnapshotsFromContext(context);
}

template <typename OutputAdapter>
void SN76489SnapshotLogger<OutputAdapter>::grabPsgSnapshotsFromContext(
    const chips::SN76489::Context& context) {
  for (size_t channelIndex{0};
       channelIndex < chips::SN76489::PSG_CHANNELS_COUNT; ++channelIndex) {
    if (not m_settings.channelsEnabled.test(channelIndex)) {
      continue;
    }

    const auto& channel{context.psg[channelIndex]};
    size_t& prevSnapshotIndex{m_prevSnapshotIndices.psg[channelIndex]};
    auto& snapshots{m_snapshots.psg[channelIndex]};
    const std::optional<SN76489PsgChannelSnapshot>& prevMaybeSnapshot{
        snapshots[prevSnapshotIndex]};
    SN76489PsgChannelSnapshot snapshot{};

    snapshot.nativeTone = channel.tone;
    snapshot.volume = channel.volume;
    bool tryToCalculateNote{true};

    if (prevMaybeSnapshot.has_value()) {
      const SN76489PsgChannelSnapshot& prevSnapshot{prevMaybeSnapshot.value()};

      if (snapshot.nativeTone == prevSnapshot.nativeTone) {
        snapshot.note = prevSnapshot.note;
        snapshot.frequencyOffset = prevSnapshot.frequencyOffset;
        tryToCalculateNote = false;
      }
    }

    if constexpr (requires(OutputAdapter& adapter) {
                    adapter.findBestNoteAndTransform(snapshot);
                  }) {
      if (tryToCalculateNote) {
        // Call via CRTP
        asOutputAdapter()->findBestNoteAndTransform(snapshot);
      }
    }

    bool keepSnapshot{true};

    if constexpr (requires(OutputAdapter& adapter) {
                    adapter.shouldKeepSnapshot(snapshot, *prevMaybeSnapshot);
                  }) {
      if (prevMaybeSnapshot.has_value()) {
        // Call via CRTP
        keepSnapshot =
            asOutputAdapter()->shouldKeepSnapshot(snapshot, *prevMaybeSnapshot);
      } else {
        keepSnapshot = snapshot.isKeyOn();
      }
    } else {
      keepSnapshot = prevMaybeSnapshot.has_value()
                         ? snapshot != prevMaybeSnapshot
                         : snapshot.isKeyOn();
    }

    if (keepSnapshot) {
      const size_t snapshotIndex{getSnapshotIndex()};
      snapshots[snapshotIndex] = snapshot;
      m_snapshots.contains.psg.set(channelIndex);
      prevSnapshotIndex = snapshotIndex;
    }
  }
}

template <typename OutputAdapter>
void SN76489SnapshotLogger<OutputAdapter>::grabNoiseSnapshotsFromContext(
    const chips::SN76489::Context& context) {
  static constexpr auto channelIndex{chips::SN76489::NOISE_CHANNEL_INDEX};

  if (not m_settings.channelsEnabled.test(channelIndex)) {
    return;
  }

  const auto& channel{context.noise};
  size_t& prevSnapshotIndex{m_prevSnapshotIndices.noise};
  auto& snapshots{m_snapshots.noise};
  const std::optional<SN76489NoiseChannelSnapshot>& prevMaybeSnapshot{
      snapshots[prevSnapshotIndex]};
  SN76489NoiseChannelSnapshot snapshot{};

  snapshot.nativeTone = channel.tone;
  snapshot.volume = channel.volume;
  snapshot.fullRangeNoiseMode = channel.fullRangeNoiseMode;
  snapshot.whiteNoiseBit = channel.whiteNoiseBit;

  bool tryToCalculateNote{channel.fullRangeNoiseMode};

  if (tryToCalculateNote and prevMaybeSnapshot.has_value()) {
    const SN76489NoiseChannelSnapshot& prevSnapshot{prevMaybeSnapshot.value()};

    if (snapshot.nativeTone == prevSnapshot.nativeTone) {
      snapshot.note = prevSnapshot.note;
      snapshot.frequencyOffset = prevSnapshot.frequencyOffset;
      tryToCalculateNote = false;
    }
  }

  if constexpr (requires(OutputAdapter& adapter) {
                  adapter.findBestNoteAndTransform(snapshot);
                }) {
    if (tryToCalculateNote) {
      // Call via CRTP
      asOutputAdapter()->findBestNoteAndTransform(snapshot);
    }
  }

  bool keepSnapshot{true};

  if constexpr (requires(OutputAdapter& adapter) {
                  adapter.shouldKeepSnapshot(snapshot, *prevMaybeSnapshot);
                }) {
    if (prevMaybeSnapshot.has_value()) {
      // Call via CRTP
      keepSnapshot =
          asOutputAdapter()->shouldKeepSnapshot(snapshot, *prevMaybeSnapshot);
    } else {
      keepSnapshot = snapshot.isKeyOn();
    }
  } else {
    keepSnapshot = prevMaybeSnapshot.has_value() ? snapshot != prevMaybeSnapshot
                                                 : snapshot.isKeyOn();
  }

  if (keepSnapshot) {
    const size_t snapshotIndex{getSnapshotIndex()};
    snapshots[snapshotIndex] = snapshot;
    m_snapshots.contains.noise = true;
    prevSnapshotIndex = snapshotIndex;
  }
}
}  // namespace vgmc
