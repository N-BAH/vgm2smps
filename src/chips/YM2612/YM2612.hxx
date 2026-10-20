#pragma once

#include <array>
#include <bitset>
#include <cstdint>

namespace vgmc::chips {
class YM2612 {
 public:
  struct WriteData {
    uint8_t reg;
    uint8_t val;
  };

  enum class Panning : uint8_t {
    none = 0b00,
    right = 0b01,
    left = 0b10,
    center = 0b11,
  };

  struct Operator {
    static constexpr uint8_t MIN_MUL{0};
    static constexpr uint8_t MAX_MUL{15};
    static constexpr int8_t MAX_TL{127};

    int8_t dt;      // detune
    uint8_t mul;    // multiple
    int8_t tl;      // total
    uint8_t rs;     // rate
    uint8_t ar;     // attack
    uint8_t d1r;    // first
    bool am;        // amplitude
    uint8_t d2r;    // secondary
    uint8_t sl;     // secondary
    uint8_t rr;     // release
    uint8_t ssgeg;  // SSGEG

    auto operator==(const Operator& other) const -> bool = default;
  };

  static constexpr int8_t MIN_VOLUME{Operator::MAX_TL};
  static constexpr size_t CHANNELS_COUNT{6};
  static constexpr size_t OPERATORS_COUNT{4};

  struct Instrument {
    uint8_t feedback;
    uint8_t algorithm;
    std::array<Operator, OPERATORS_COUNT> operators;

    auto operator==(const Instrument& other) const -> bool = default;

    [[nodiscard]] auto sameTimbreAs(const Instrument& other) const noexcept
        -> bool;
    [[nodiscard]] auto getVolume() const noexcept -> int8_t;
    [[nodiscard]] auto getSlotMask() const -> std::bitset<OPERATORS_COUNT>;
    [[nodiscard]] auto makeNormalized() const noexcept -> Instrument;
    [[nodiscard]] auto upOctave() const noexcept -> Instrument;
    [[nodiscard]] auto downOctave() const noexcept -> Instrument;
    [[nodiscard]] auto canUpOctaveSafely() const noexcept -> bool;
    [[nodiscard]] auto canDownOctaveSafely() const noexcept -> bool;
    [[nodiscard]] [[maybe_unused]] auto isFullSilent() const noexcept -> bool;

   private:
    auto getLoudestSlotIndex(
        const std::bitset<OPERATORS_COUNT>& slotMask) const noexcept -> size_t;
  };

  struct Tone {
    uint16_t frequency;
    uint8_t block;

    auto operator==(const Tone& other) const noexcept -> bool = default;
  };

  struct Channel {
    Instrument instrument;
    Panning panning;
    int8_t ams;
    int8_t fms;
    std::bitset<OPERATORS_COUNT> operatorEnabled;
    std::array<Tone, OPERATORS_COUNT> tone;

    // For ch3 special mode you should check every operator separately
    [[nodiscard]] auto isKeyOn() const noexcept -> bool {
      // Should be checked via .all, but it not works. Find some workaround like
      // this
      return operatorEnabled.any();
    }

    auto operator==(const Channel& other) const -> bool = delete;
  };

  struct Context {
    // Only necessary states. Add more precise state tracking if needed.
    std::array<Channel, CHANNELS_COUNT> channels;

    bool lfoEnabled;
    uint8_t lfoFrequency;
    bool dacEnabled;
    uint8_t dacValue;
    bool channel3SpecialModeEnabled;
  };

  // 4 Optimization
  struct ChangeTrackingFlags {
    bool dacValueChanged;
    bool dacEnabledChanged;
    bool globalChanged;
    std::bitset<CHANNELS_COUNT> channelsChanged;
    std::bitset<CHANNELS_COUNT> attackDetected;

    auto operator==(const ChangeTrackingFlags& other) const noexcept
        -> bool = default;

    [[nodiscard]] auto dacValueOnlyChanged() const noexcept -> bool {
      return dacValueChanged and not dacEnabledChanged and not globalChanged and
             channelsChanged.none();
    }

    [[nodiscard]] auto anyChanged() const noexcept -> bool {
      return dacEnabledChanged or dacEnabledChanged or globalChanged or
             channelsChanged.any();
    }
  };

  YM2612(uint32_t clock);

  void writePort0(WriteData writeData);
  void writePort1(WriteData writeData);

  [[nodiscard]] auto getContext() const noexcept -> const Context& {
    return m_context;
  }
  [[nodiscard]] auto getClock() const noexcept { return m_clock; }
  [[nodiscard]] auto getChangeTrackingFlags() const noexcept {
    return m_changeTrackingFlags;
  }
  void resetChangeTrackingFlags() noexcept { m_changeTrackingFlags = {}; }

  [[nodiscard]] auto convertToHz(uint8_t block, uint16_t frequency) const
      -> double;
  [[nodiscard]] static auto convertToHz(uint32_t clock, uint8_t block,
                                        uint16_t frequency) -> double;

 private:
  uint32_t m_clock;
  Context m_context{};
  std::bitset<CHANNELS_COUNT> m_frequencyPreparing;
  ChangeTrackingFlags m_changeTrackingFlags{};

  struct {
    bool ch3SpecialNotSupported{false};
  } m_warningMessagesToggles;

  void processLfoChange(WriteData writeData);
  void processCh3Change(WriteData writeData);
  void processKeyOnOff(WriteData writeData);
  void processDacData(WriteData writeData);
  void processDacSetState(WriteData writeData);

  static void processOperatorRegister(WriteData writeData, Operator& operator_);
  void processFrequencyChange(WriteData writeData, uint8_t port);
  void processFeedbackAlgorithmSettings(WriteData writeData, uint8_t port);
  void processPanLfoSettings(WriteData writeData, uint8_t port);

  static inline auto operatorNumberToIndex(uint8_t operatorNumber) -> uint8_t;
};
}  // namespace vgmc::chips
