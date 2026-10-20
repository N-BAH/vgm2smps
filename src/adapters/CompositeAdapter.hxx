#pragma once

#include <cstddef>
#include <tuple>

#include "vgm_parser/VgmEvents.hxx"

namespace vgmc {
// Helper macro to insert boilerplate code for calling event only for mixins
// that implement concrete event
#define INSERT_MIXIN_BROADCAST_EVENT_CALL(eventName, ...) \
  (                                                       \
      [&, this]() -> void {                               \
        if constexpr (requires(Mixins& mixin) {           \
                        mixin.eventName(__VA_ARGS__);     \
                      }) {                                \
          Mixins::eventName(__VA_ARGS__);                 \
        }                                                 \
      }(),                                                \
      ...);

template <typename... Mixins>
class CompositeAdapter : public Mixins... {
 public:
  // Конструктор принимает кортеж из готовых конфигов/миксинов и
  // инициализирует базовые классы
  template <typename... Args>
  explicit CompositeAdapter(std::tuple<Args...> configs)
      : CompositeAdapter(std::move(configs),
                         std::index_sequence_for<Mixins...>{}) {}

  template <typename EventType>
  void broadcastHeaderParsed(const VgmParseContext& ctx,
                             const EventType& event) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onHeaderParsed, ctx, event)
  }

  void broadcastEndOfData(const VgmParseContext& ctx) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onEndOfData, ctx);
  }

  template <typename EventType>
  void broadcastTickAdvance(const VgmParseContext& ctx,
                            const EventType& event) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onTickAdvance, ctx, event);
  }

  template <typename EventType>
  void broadcastDataBlockExtracted(const VgmParseContext& ctx,
                                   const EventType& event) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onDataBlockExtracted, ctx, event);
  }

  template <typename EventType>
  void broadcastYM2612PcmSeek(const VgmParseContext& ctx,
                              const EventType& event) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onYM2612PcmSeek, ctx, event);
  }

  template <typename EventType>
  void broadcastYM2612PcmDelay(const VgmParseContext& ctx,
                               const EventType& event) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onYM2612PcmDelay, ctx, event);
  }

  template <typename EventType>
  void broadcastDacStreamSetup(const VgmParseContext& ctx,
                               const EventType& event) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onDacStreamSetup, ctx, event);
  }

  template <typename EventType>
  void broadcastDacStreamPlay(const VgmParseContext& ctx,
                              const EventType& event) {
    INSERT_MIXIN_BROADCAST_EVENT_CALL(onDacStreamPlay, ctx, event);
  }

 private:
  // Вспомогательный конструктор для раскрытия tuple в инициализаторы предков
  template <typename Tuple, size_t... Is>
  CompositeAdapter(Tuple&& mixinData, std::index_sequence<Is...> /*unused*/)
      : Mixins(std::get<Is>(std::forward<Tuple>(mixinData)))... {}
};

#undef INSERT_MIXIN_BROADCAST_EVENT_CALL
}  // namespace vgmc
