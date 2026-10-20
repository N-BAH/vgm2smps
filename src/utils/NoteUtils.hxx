#pragma once

#include <cstddef>
#include <cstdint>

namespace vgmc {
constexpr size_t NOTES_IN_OCTAVE{12};

struct SimpleNote {
  uint8_t octave;
  uint8_t key;
  auto operator==(const SimpleNote& other) const -> bool = default;
};

struct Note : public SimpleNote {
  double offset;
  auto operator==(const Note& other) const -> bool = default;
};

auto findNearestNote(double frequency) noexcept -> Note;
}  // namespace vgmc
