
#pragma once

#include "VgmEvents.hxx"

namespace vgmc {

template <typename ConcreteAdapter>
class VgmEventDispatcher {
 private:
  ConcreteAdapter& m_adapter;

 public:
  explicit VgmEventDispatcher(ConcreteAdapter& adapter) : m_adapter(adapter) {}

  // Быстрый статический вызов. Если метод в адаптере/миксине есть — он
  // вызовется.
  void dispatchEndOfData(const VgmParseContext& ctx) {
    if constexpr (requires { m_adapter.onEndOfData(ctx); }) {
      m_adapter.onEndOfData(ctx);
    }
  }

  void dispatchHeaderParsed(const VgmParseContext& ctx,
                            const HeaderParsedEvent& event) {
    m_adapter.onHeaderParsed(ctx, event);
  }

  void dispatchTickAdvance(const VgmParseContext& ctx,
                           const TickAdvanceEvent& event) {
    if constexpr (requires { m_adapter.onTickAdvance(ctx, event); }) {
      m_adapter.onTickAdvance(ctx, event);
    }
  }

  void dispatchDataBlockExtracted(const VgmParseContext& ctx,
                                  const DataBlockExtractedEvent& event) {
    if constexpr (requires { m_adapter.onDataBlockExtracted(ctx, event); }) {
      m_adapter.onDataBlockExtracted(ctx, event);
    }
  }

  void dispatchYM2612PcmSeek(const VgmParseContext& ctx,
                             const YM2612PcmSeekEvent& event) {
    if constexpr (requires { m_adapter.onYM2612PcmSeek(ctx, event); }) {
      m_adapter.onYM2612PcmSeek(ctx, event);
    }
  }

  void dispatchYM2612PcmDelay(const VgmParseContext& ctx,
                              const YM2612PcmDelayEvent& event) {
    if constexpr (requires { m_adapter.onYM2612PcmDelay(ctx, event); }) {
      m_adapter.onYM2612PcmDelay(ctx, event);
    }
  }

  void dispatchDacStreamSetup(const VgmParseContext& ctx,
                              const DacStreamSetupEvent& event) {
    if constexpr (requires { m_adapter.onDacStreamSetup(ctx, event); }) {
      m_adapter.onDacStreamSetup(ctx, event);
    }
  }

  void dispatchDacStreamPlay(const VgmParseContext& ctx,
                             const DacStreamPlayEvent& event) {
    if constexpr (requires { m_adapter.onDacStreamPlay(ctx, event); }) {
      m_adapter.onDacStreamPlay(ctx, event);
    }
  }
};

}  // namespace vgmc
