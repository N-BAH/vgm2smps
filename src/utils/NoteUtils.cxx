#include "NoteUtils.hxx"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace vgmc {
static constexpr auto referenceNoteTable = std::array{
    32.7032,  // C1
    34.6479,  // C#1
    36.7081,  // D1
    38.8909,  // D#1
    41.2035,  // E1
    43.6536,  // F1
    46.2493,  // F#1
    48.9995,  // G1
    51.913,   // G#1
    55.0,     // A1
    58.2705,  // A#1
    61.7354,  // B1
    65.4064,  // C2
    69.2957,  // C#2
    73.4162,  // D2
    77.7817,  // D#2
    82.4069,  // E2
    87.3071,  // F2
    92.4986,  // F#2
    97.9989,  // G2
    103.826,  // G#2
    110.0,    // A2
    116.541,  // A#2
    123.471,  // B2
    130.813,  // C3
    138.591,  // C#3
    146.832,  // D3
    155.563,  // D#3
    164.814,  // E3
    174.614,  // F3
    184.997,  // F#3
    195.998,  // G3
    207.652,  // G#3
    220.0,    // A3
    233.082,  // A#3
    246.942,  // B3
    261.626,  // C4
    277.183,  // C#4
    293.665,  // D4
    311.127,  // D#4
    329.628,  // E4
    349.228,  // F4
    369.994,  // F#4
    391.995,  // G4
    415.305,  // G#4
    440.0,    // A4
    466.164,  // A#4
    493.883,  // B4
    523.251,  // C5
    554.365,  // C#5
    587.33,   // D5
    622.254,  // D#5
    659.255,  // E5
    698.456,  // F5
    739.989,  // F#5
    783.991,  // G5
    830.609,  // G#5
    880.0,    // A5
    932.328,  // A#5
    987.767,  // B5
    1046.5,   // C6
    1108.73,  // C#6
    1174.66,  // D6
    1244.51,  // D#6
    1318.51,  // E6
    1396.91,  // F6
    1479.98,  // F#6
    1567.98,  // G6
    1661.22,  // G#6
    1760.0,   // A6
    1864.66,  // A#6
    1975.53,  // B6
    2093.0,   // C7
    2217.46,  // C#7
    2349.32,  // D7
    2489.02,  // D#7
    2637.02,  // E7
    2793.83,  // F7
    2959.96,  // F#7
    3135.96,  // G7
    3322.44,  // G#7
    3520.0,   // A7
    3729.31,  // A#7
    3951.07,  // B7
    4186.01,  // C8
    4434.92,  // C#8
    4698.63,  // D8
    4978.03,  // D#8
    5274.04,  // E8
    5587.65,  // F8
    5919.91,  // F#8
    6271.93,  // G8
    6644.88,  // G#8
    7040.0,   // A8
    7458.62,  // A#8
    7902.13,  // B8
};

auto findNearestNote(double frequency) noexcept -> Note {
  static_assert(referenceNoteTable.size() % NOTES_IN_OCTAVE == 0);

  double foundFrequency{referenceNoteTable.back()};
  std::ptrdiff_t foundFrequencyIndex{referenceNoteTable.size() - 1};

  if (const auto nearestNoteIt =
          std::ranges::lower_bound(referenceNoteTable, frequency);
      nearestNoteIt != referenceNoteTable.cend()) {
    foundFrequency = *nearestNoteIt;
    foundFrequencyIndex =
        std::distance(referenceNoteTable.cbegin(), nearestNoteIt);
  }

  Note result;

  result.octave =
      static_cast<uint8_t>(foundFrequencyIndex / NOTES_IN_OCTAVE) + 1;
  result.key = static_cast<uint8_t>(foundFrequencyIndex % NOTES_IN_OCTAVE);
  result.offset = foundFrequency - frequency;

  return result;
};
}  // namespace vgmc
