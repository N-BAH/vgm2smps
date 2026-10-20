#pragma once
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <boost/container/flat_map.hpp>
#include <boost/container/static_vector.hpp>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>

#include "chips/YM2612/YM2612.hxx"
#include "utils/NoteUtils.hxx"
#include "vgm_parser/VgmEvents.hxx"
#include "vgm_parser/VgmHeader.hxx"

namespace vgmc {
enum class VgmDacMode : uint8_t { raw, dataBlock, stream };
enum class KeyPlayMode : uint8_t { hold, attack };

struct YM2612GlobalStateSnapshot {
  bool lfoEnabled;
  uint8_t lfoFrequency;
  bool dacEnabled;

  auto operator==(const YM2612GlobalStateSnapshot& other) const noexcept
      -> bool = default;
};

struct YM2612DacChannelSnapshot {
  chips::YM2612::Panning panning{chips::YM2612::Panning::center};
  // std::nullopt means disable DAC
  std::optional<uint8_t> maybeSampleIndex;
  // Used if PCM-bank approach used in VGM file
  uint32_t pcmBankIndex;

  KeyPlayMode keyPlayMode{KeyPlayMode::hold};

  auto operator==(const YM2612DacChannelSnapshot& other) const noexcept
      -> bool = default;
};

struct YM2612SnapshotLoggerSettings {
  std::bitset<chips::YM2612::CHANNELS_COUNT> channelsEnabled{"111111"};
  bool dacEnabled{true};
};

template <typename OutputAdapter, typename FMChannelExtension = std::monostate>
class YM2612SnapshotLogger {
 public:
  struct YM2612FMChannelSnapshot {
    chips::YM2612::Panning panning{chips::YM2612::Panning::center};
    int8_t ams{};
    int8_t fms{};
    uint8_t instrumentIndex{};
    int8_t volume{};
    chips::YM2612::Tone nativeTone{};
    KeyPlayMode keyPlayMode{KeyPlayMode::hold};
    bool isKeyOn{false};

    // Fill tone after full VGM parsing using filled reference Tone octave table
    SimpleNote note{};
    int16_t frequencyOffset{};

    auto operator==(const YM2612FMChannelSnapshot& other) const noexcept
        -> bool = default;
  };

  struct YM2612Snapshots {
    std::array<std::vector<std::optional<YM2612FMChannelSnapshot>>,
               chips::YM2612::CHANNELS_COUNT>
        fm;
    std::vector<std::optional<YM2612DacChannelSnapshot>> dac;

    struct {
      std::bitset<chips::YM2612::CHANNELS_COUNT> fm;
      bool dac;
    } contains;
  };

  static constexpr size_t MAX_INSTRUMENTS_COUNT{
      std::numeric_limits<uint8_t>::max()};  // should be 255 (0xFF)

  YM2612SnapshotLogger(const YM2612SnapshotLoggerSettings& settings);

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

  [[nodiscard]] auto findOrPushNewFmInstrument(
      const chips::YM2612::Instrument& instrument) -> size_t;

  [[nodiscard]] auto getFmInstruments() const noexcept
      -> std::span<const chips::YM2612::Instrument> {
    return m_fmInstruments;
  }

  [[nodiscard]] auto getYM2612Snapshots() const noexcept -> const auto& {
    return m_snapshots;
  }

  [[nodiscard]] auto getYM2612Snapshots() noexcept -> auto& {
    return m_snapshots;
  }

 private:
  struct PcmBankSampleInfo {
    // Size in bytes
    uint32_t size;
    // Length in samples (for calculating sample rate)
    uint32_t length;
  };

  chips::YM2612* m_ym2612{};
  const YM2612SnapshotLoggerSettings& m_settings;
  const VgmHeader* m_vgmHeader{};
  uint32_t m_snapshotsCount{};
  YM2612Snapshots m_snapshots{};
  boost::container::static_vector<std::span<const uint8_t>,
                                  MAX_INSTRUMENTS_COUNT>
      m_samples;

  struct {
    std::array<size_t, chips::YM2612::CHANNELS_COUNT> fm;
    size_t dac;
  } m_prevSnapshotIndices{};

  struct {
    bool dataBlockDetected;
    bool dacStreamSetupDetected;
    bool dacWriteStarted;
  } m_vgmDacEventsFlags{};

  VgmDacMode m_vgmDacMode{};
  bool m_firstDacPlayDetected{false};
  uint32_t m_dacOnlyChangesCounter{};
  uint32_t m_lastPcmSeek{};
  uint32_t m_prevPcmSeek{};
  uint32_t m_currentSampleStartTick{};

  boost::container::static_vector<chips::YM2612::Instrument,
                                  MAX_INSTRUMENTS_COUNT>
      m_fmInstruments;

  // Key -- PCM Bank index, value -- sample info
  // It's attempt to know about used samples and its play lengths
  // If it's not needed, replace this with set/flat_set
  boost::container::flat_map<
      uint32_t, PcmBankSampleInfo, std::less<>,
      boost::container::static_vector<std::pair<uint32_t, PcmBankSampleInfo>,
                                      MAX_INSTRUMENTS_COUNT>>
      m_pcmSeeks;

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

  void processContextChanged(
      const VgmParseContext& ctx, const chips::YM2612::Context& context,
      const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags);

  void grabSnapshotsFromContext(
      const chips::YM2612::Context& context,
      const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags);

  void grabSnapshotsFromContextForChannel(
      const chips::YM2612::Context& context,
      const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags,
      size_t channelIndex);

  void grabSnapshotsFromContextForDacChannel(
      const chips::YM2612::Context& context,
      const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags,
      size_t channelIndex);
};

template <typename OutputAdapter, typename FMChannelExtension>
YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::YM2612SnapshotLogger(
    const YM2612SnapshotLoggerSettings& settings)
    : m_settings{settings} {}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::onHeaderParsed(
    const VgmParseContext& ctx, const HeaderParsedEvent& event) {
  m_vgmHeader = &event.header;

  if (not event.chips.ym2612.has_value()) {
    return;
  }

  m_ym2612 = &event.chips.ym2612.value();

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

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::onEndOfData(
    const VgmParseContext& ctx) {
  if (m_vgmDacMode != VgmDacMode::dataBlock) {
    return;
  }

#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_DEBUG
  SPDLOG_DEBUG("Found samples in PCM bank:");

  for (const auto& [seek, sampleInfo] : m_pcmSeeks) {
    const float sampleRate{static_cast<float>(VGM_TICK_RATE) *
                           (static_cast<float>(sampleInfo.size) /
                            static_cast<float>(sampleInfo.length))};
    SPDLOG_DEBUG(
        "from:{}\t| size:{}, length:(samples:{}, seconds:{}), sampleRate:{}",
        seek, sampleInfo.size, sampleInfo.length,
        sampleInfo.length / static_cast<float>(VGM_TICK_RATE), sampleRate);
  }
#endif

  auto dacSnapshots =
      m_snapshots.dac |
      std::views::filter(
          [](std::optional<YM2612DacChannelSnapshot>& maybeSnapshot) -> bool {
            return maybeSnapshot.has_value();
          }) |
      std::views::transform(
          [](std::optional<YM2612DacChannelSnapshot>& maybeSnapshot)
              -> YM2612DacChannelSnapshot& { return maybeSnapshot.value(); }) |
      std::views::filter([](YM2612DacChannelSnapshot& snapshots) -> bool {
        return snapshots.maybeSampleIndex.has_value();
      });

  for (YM2612DacChannelSnapshot& snapshot : dacSnapshots) {
    auto& sampleIndex{snapshot.maybeSampleIndex.value()};
    const auto iterator{m_pcmSeeks.find(snapshot.pcmBankIndex)};
    sampleIndex = std::distance(m_pcmSeeks.begin(), iterator);
  }
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::onTickAdvance(
    const VgmParseContext& ctx, const TickAdvanceEvent& event) {
  if (m_ym2612 == nullptr or m_vgmHeader == nullptr) {
    return;
  }

  if (event.tick != 0 and event.prevTick == 0) {
    // Skip first YM2612 setup snapshot
    if (std::ranges::none_of(
            m_ym2612->getContext().channels,
            [](const auto& chl) -> bool { return chl.isKeyOn(); })) {
      m_ym2612->resetChangeTrackingFlags();
    }
  }

  // If we want to track first note tick offset for frame alignment in
  // OutputAdapter
  if constexpr (requires(OutputAdapter& adapter) {
                  adapter.onDetectFirstChipChange(event);
                  adapter.getFirstChipChangeDetected();
                  adapter.getFirstNoteTick();
                }) {
    if (not asOutputAdapter()->getFirstChipChangeDetected()) {
      if (m_ym2612->getChangeTrackingFlags().anyChanged()) {
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

  const auto& changeTrackingFlags{m_ym2612->getChangeTrackingFlags()};

  if (changeTrackingFlags.anyChanged()) {
    processContextChanged(ctx, m_ym2612->getContext(), changeTrackingFlags);
    m_ym2612->resetChangeTrackingFlags();
  }

  m_prevTick = m_tick;
  m_prevFrame = m_currentFrame;
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::
    onDataBlockExtracted(const VgmParseContext& ctx,
                         const DataBlockExtractedEvent& event) {
  if (m_ym2612 == nullptr) {
    return;
  }

  SPDLOG_DEBUG("Data block# {} extracted. Size:{}", event.dataBlockIndex,
               event.dataBlock.size());

  m_vgmDacEventsFlags.dataBlockDetected = true;
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::onYM2612PcmSeek(
    const VgmParseContext& ctx, const YM2612PcmSeekEvent& event) {
  if (m_ym2612 == nullptr or not m_settings.dacEnabled) {
    return;
  }

  SPDLOG_DEBUG("Seek PCM. offset:{}", event.offset);
  m_prevPcmSeek = event.prevOffset;
  m_vgmDacEventsFlags.dacWriteStarted = false;
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::onYM2612PcmDelay(
    const VgmParseContext& ctx, const YM2612PcmDelayEvent& event) {
  if (m_ym2612 == nullptr or not m_settings.dacEnabled) {
    return;
  }

  if (m_vgmDacEventsFlags.dacWriteStarted) {
    return;
  }

  m_vgmDacEventsFlags.dacWriteStarted = true;

  if (not m_firstDacPlayDetected) {
    m_firstDacPlayDetected = true;

    if (m_vgmDacMode != VgmDacMode::raw) {
      // TODO: use magic enum
      SPDLOG_WARN("WTF?! Change DAC mode from {} to block?",
                  static_cast<int>(m_vgmDacMode));
    }

    m_vgmDacMode = VgmDacMode::dataBlock;
    SPDLOG_INFO("DAC MODE DETECTED: DATABLOCK + SEEK (MID TIER)");
  }

  size_t& prevSnapshotIndex{m_prevSnapshotIndices.dac};
  const std::optional<YM2612DacChannelSnapshot>& maybePrevSnapshot{
      m_snapshots.dac[prevSnapshotIndex]};
  YM2612DacChannelSnapshot snapshot{};
  const auto sampleStartOffset{event.offset - 1};

  snapshot.keyPlayMode = KeyPlayMode::attack;
  snapshot.pcmBankIndex = m_lastPcmSeek;

  if (not m_pcmSeeks.empty()) {
    PcmBankSampleInfo& sampleInfo{m_pcmSeeks.at(m_lastPcmSeek)};
    sampleInfo.size = std::max(sampleInfo.size, m_prevPcmSeek - m_lastPcmSeek);
    sampleInfo.length =
        std::max(sampleInfo.length, ctx.tick - m_currentSampleStartTick);
  }

  m_lastPcmSeek = sampleStartOffset;
  m_currentSampleStartTick = ctx.tick;

  if (not m_pcmSeeks.contains(m_lastPcmSeek)) {
    if (m_pcmSeeks.size() == m_pcmSeeks.max_size()) {
      SPDLOG_ERROR("Too much PCM Bank jumps in the VGM file. Sample will skip");
      return;
    }

    m_pcmSeeks[m_lastPcmSeek] = {};
  }

  // Emplace placeholder. Exact sample index will be known at endOfData stage
  snapshot.maybeSampleIndex.emplace();
  m_snapshots.dac[getSnapshotIndex()].emplace(std::move(snapshot));
  m_snapshots.contains.dac = true;
  prevSnapshotIndex = getSnapshotIndex();

  SPDLOG_DEBUG("Dac write started at offset:{}", m_lastPcmSeek);
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::onDacStreamSetup(
    const VgmParseContext& ctx, const DacStreamSetupEvent& event) {
  if (m_ym2612 == nullptr or not m_settings.dacEnabled) {
    return;
  }

  m_vgmDacEventsFlags.dacStreamSetupDetected = true;
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::onDacStreamPlay(
    const VgmParseContext& ctx, const DacStreamPlayEvent& event) {
  if (m_ym2612 == nullptr or not m_settings.dacEnabled) {
    return;
  }

  if (not m_firstDacPlayDetected) {
    m_firstDacPlayDetected = true;

    if (m_vgmDacMode != VgmDacMode::raw) {
      // TODO: use magic enum
      SPDLOG_WARN("WTF?! Change DAC mode from {} to stream?",
                  static_cast<int>(m_vgmDacMode));
    }

    m_vgmDacMode = VgmDacMode::stream;
    SPDLOG_INFO("DAC MODE DETECTED: STREAM (TOP)");
  }

  size_t& prevSnapshotIndex{m_prevSnapshotIndices.dac};
  const std::optional<YM2612DacChannelSnapshot>& maybePrevSnapshot{
      m_snapshots.dac[prevSnapshotIndex]};
  YM2612DacChannelSnapshot snapshot{};

  snapshot.keyPlayMode = KeyPlayMode::attack;

  if (maybePrevSnapshot.has_value()) {
    snapshot.panning = maybePrevSnapshot->panning;
  } else {
    snapshot.panning = m_ym2612->getContext().channels.back().panning;
  }

  // TODO: overflow handling
  snapshot.maybeSampleIndex.emplace(
      static_cast<uint8_t>(event.dacStream.soundId));
  m_snapshots.dac[getSnapshotIndex()].emplace(std::move(snapshot));
  m_snapshots.contains.dac = true;
  prevSnapshotIndex = getSnapshotIndex();
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<
    OutputAdapter, FMChannelExtension>::preallocateSnapshots(size_t count) {
  if (m_ym2612 == nullptr) {
    return;
  }

  for (size_t channelIndex{0}; channelIndex < chips::YM2612::CHANNELS_COUNT;
       ++channelIndex) {
    if (m_settings.channelsEnabled.test(channelIndex)) {
      m_snapshots.fm[channelIndex].resize(count);
    }
  }

  if (m_settings.dacEnabled and
      m_settings.channelsEnabled.test(chips::YM2612::CHANNELS_COUNT - 1)) {
    m_snapshots.dac.resize(count);
  }
}

template <typename OutputAdapter, typename FMChannelExtension>
auto YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::
    findOrPushNewFmInstrument(const chips::YM2612::Instrument& instrument)
        -> size_t {
  bool instrumentFound{false};
  // Track instrument state
  for (size_t i{0}; i < m_fmInstruments.size(); ++i) {
    const auto& existingInstrument{m_fmInstruments[i]};

    if (instrument == existingInstrument) {
      return i;
    }
  }

  if (m_fmInstruments.size() >= decltype(m_fmInstruments)::capacity()) {
    throw std::runtime_error{"Too many YM2612 Instruments in VGM file"};
  }

  m_fmInstruments.push_back(instrument);
  return m_fmInstruments.size() - 1;
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::
    processContextChanged(
        const VgmParseContext& ctx, const chips::YM2612::Context& context,
        const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags) {
  // Check if only DAC value changes
  if (changeTrackingFlags.dacValueOnlyChanged()) {
    // Skip DAC buffer changes
    ++m_dacOnlyChangesCounter;
    return;
  }

  if (m_settings.dacEnabled and changeTrackingFlags.dacEnabledChanged) {
    if (context.dacEnabled) {
      if (not m_firstDacPlayDetected) {
        m_firstDacPlayDetected = true;

        if (m_vgmDacMode == VgmDacMode::raw) {
          SPDLOG_INFO(
              "DAC MODE DETECTED: RAW (SHIT TIER. Emulator recordings or "
              "similar)");
        }
      }
    } else {
      YM2612DacChannelSnapshot snapshot{};
      snapshot.panning = context.channels.back().panning;
      m_snapshots.dac[getSnapshotIndex()] = snapshot;
      m_snapshots.contains.dac = true;

      if (m_vgmDacMode == VgmDacMode::dataBlock) {
        PcmBankSampleInfo& sampleInfo{m_pcmSeeks.at(m_lastPcmSeek)};
        sampleInfo.size = std::max(sampleInfo.size,
                                   ctx.ym2612PcmBankPosition - m_lastPcmSeek);
        sampleInfo.length =
            std::max(sampleInfo.length, ctx.tick - m_currentSampleStartTick);
      }
    }

    if constexpr (requires(OutputAdapter& adapter) {
                    adapter.onYM2612ChangeDacEnabledState(context);
                  }) {
      asOutputAdapter()->onYM2612ChangeDacEnabledState(context);
    }

    SPDLOG_DEBUG("{:010}\tSwitch dac to {}", m_tick, context.dacEnabled);
  }

  grabSnapshotsFromContext(context, changeTrackingFlags);
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::
    grabSnapshotsFromContext(
        const chips::YM2612::Context& context,
        const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags) {
  for (size_t channelIndex{0}; channelIndex < chips::YM2612::CHANNELS_COUNT;
       ++channelIndex) {
    grabSnapshotsFromContextForChannel(context, changeTrackingFlags,
                                       channelIndex);
  }

  if (const size_t channelIndex{chips::YM2612::CHANNELS_COUNT - 1};
      m_settings.dacEnabled and m_settings.channelsEnabled.test(channelIndex)) {
    grabSnapshotsFromContextForDacChannel(context, changeTrackingFlags,
                                          channelIndex);
  }
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::
    grabSnapshotsFromContextForChannel(
        const chips::YM2612::Context& context,
        const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags,
        size_t channelIndex) {
  if (not m_settings.channelsEnabled.test(channelIndex) or
      not changeTrackingFlags.channelsChanged.test(channelIndex)) {
    return;
  }

  const auto& channel{context.channels[channelIndex]};
  size_t& prevSnapshotIndex{m_prevSnapshotIndices.fm[channelIndex]};
  auto& snapshots{m_snapshots.fm[channelIndex]};
  const auto& prevMaybeSnapshot{snapshots[prevSnapshotIndex]};
  KeyPlayMode keyPlayMode{};

  YM2612FMChannelSnapshot snapshot{};
  snapshot.isKeyOn =
      channel.isKeyOn() and
      (not context.channel3SpecialModeEnabled or channelIndex != 2);

  if (snapshot.isKeyOn) {
    // Track key state
    snapshot.nativeTone = channel.tone.front();

    if (snapshot.nativeTone.block == 0 and snapshot.nativeTone.frequency == 0) {
      SPDLOG_WARN("Workaround skip YM2612 snapshot: KeyOn and Tone == 0");
      return;
    }

    if (changeTrackingFlags.attackDetected.test(channelIndex)) {
      snapshot.keyPlayMode = KeyPlayMode::attack;
    } else {
      snapshot.keyPlayMode = KeyPlayMode::hold;
    }

    chips::YM2612::Instrument instrumentCandidate{
        channel.instrument.makeNormalized()};

    if constexpr (requires(OutputAdapter& adapter) {
                    adapter.filterYM2612InstrumentData(instrumentCandidate);
                  }) {
      asOutputAdapter()->filterYM2612InstrumentData(instrumentCandidate);
    }

    snapshot.instrumentIndex = findOrPushNewFmInstrument(instrumentCandidate);
    snapshot.volume = channel.instrument.getVolume();

    // Track ams fms etc
    snapshot.panning = channel.panning;
    snapshot.ams = channel.ams;
    snapshot.fms = channel.fms;

    // TODO: track FM3 Special Mode, Timers and other complex logic if it needed
    // for some driver/format

    if constexpr (requires(OutputAdapter& adapter) {
                    adapter.filterSnapshotData(snapshot);
                  }) {
      asOutputAdapter()->filterSnapshotData(snapshot);
    }

    if constexpr (requires(OutputAdapter& adapter) {
                    adapter.findBestNoteAndTransform(snapshot);
                  }) {
      // Call via CRTP
      asOutputAdapter()->findBestNoteAndTransform(snapshot);

      if (not snapshot.isKeyOn and
          snapshot.nativeTone == chips::YM2612::Tone{}) {
        if (snapshot.instrumentIndex == m_fmInstruments.size() - 1) {
          m_fmInstruments.pop_back();
          snapshot.instrumentIndex = 0;
        }
      }
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
      keepSnapshot = snapshot.isKeyOn;
    }
  } else {
    keepSnapshot = snapshot != prevMaybeSnapshot;
  }

  if (keepSnapshot) {
    auto const snapshotIndex{getSnapshotIndex()};
    snapshots[snapshotIndex] = snapshot;
    m_snapshots.contains.fm.set(channelIndex);
    prevSnapshotIndex = snapshotIndex;
  }
}

template <typename OutputAdapter, typename FMChannelExtension>
void YM2612SnapshotLogger<OutputAdapter, FMChannelExtension>::
    grabSnapshotsFromContextForDacChannel(
        const chips::YM2612::Context& context,
        const chips::YM2612::ChangeTrackingFlags& changeTrackingFlags,
        size_t channelIndex) {
  if (not context.dacEnabled) {
    return;
  }

  auto& snapshots{m_snapshots.dac};
  auto& maybeSnapshot{snapshots[getSnapshotIndex()]};

  if (maybeSnapshot.has_value()) {
    // Panning already set in DAC handling section
    return;
  }

  const auto& channel{context.channels[channelIndex]};
  size_t& prevSnapshotIndex{m_prevSnapshotIndices.dac};
  const auto& prevMaybeSnapshot{snapshots[prevSnapshotIndex]};

  if (not prevMaybeSnapshot.has_value()) {
    // First sample is not played
    return;
  }

  const auto& prevSnapshot{prevMaybeSnapshot.value()};

  // If sample not playing
  if (not prevSnapshot.maybeSampleIndex.has_value()) {
    // Skip change check
    return;
  }

  // If pannig changed while sample is playing
  if (channel.panning != prevSnapshot.panning) {
    auto& snapshot{maybeSnapshot.emplace(prevSnapshot)};
    m_snapshots.contains.dac = true;
    snapshot.panning = channel.panning;
    snapshot.keyPlayMode = KeyPlayMode::hold;
  }
}
}  // namespace vgmc
