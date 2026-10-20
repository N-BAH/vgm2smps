#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>

namespace vgmc {
enum class FileEndian : uint8_t { Little, Big };

template <typename T>
  requires std::integral<T>
[[nodiscard]] inline auto byteswap(T value) -> T {
#ifdef __cpp_lib_byteswap
  // Для C++23, либо используйте встроенные функции компилятора
  return std::byteswap(value);

// Альтернатива byteswap для C++20 через встроенные функции компиляторов:
#elif defined(__clang__) || defined(__GNUC__)
  if constexpr (sizeof(T) == sizeof(uint32_t)) {
    return __builtin_bswap32(value);
  } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
    return __builtin_bswap64(value);
  } else if constexpr (sizeof(T) == sizeof(uint16_t)) {
    return __builtin_bswap16(value);
  } else {
    throw std::runtime_error{
        "TODO: Write correct byteswap implementation for this data "
        "size"};
  }
#elif defined(_MSC_VER)
  if constexpr (sizeof(T) == sizeof(uint32_t)) {
    return _byteswap_ulong(value);
  } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
    return _byteswap_uint64(value);
  } else if constexpr (sizeof(T) == sizeof(uint16_t)) {
    return _byteswap_ushort(value);
  } else {
    throw std::runtime_error{
        "TODO: Write correct byteswap implementation for this data "
        "size"};
  }
#else
#error \
    "TODO: Write correct byteswap implementation for this compiler/cxx standard!"
#endif
}

template <FileEndian Endian = FileEndian::Little>
[[nodiscard]] constexpr auto isByteswapNeededFor() -> bool {
  return (Endian == FileEndian::Little &&
          std::endian::native == std::endian::big) ||
         (Endian == FileEndian::Big &&
          std::endian::native == std::endian::little);
}

// Используем концепты (C++20), чтобы функция принимала только тривиально
// копируемые типы
template <typename T, FileEndian Endian = FileEndian::Little>
  requires std::integral<T>  // Ограничиваем только целочисленными типами для
                             // корректного байтсвапа
[[nodiscard]] inline auto readBinary(std::span<const uint8_t> buffer,
                                     size_t offset = 0) -> T {
  if (offset + sizeof(T) > buffer.size()) {
    throw std::out_of_range("Выход за границы буфера");
  }

  T value;
  std::memcpy(&value, buffer.data() + offset, sizeof(T));

  // Если порядок байт в файле отличается от родного порядка байт процессора
  if constexpr (isByteswapNeededFor<Endian>()) {
    return byteswap(value);
  }

  return value;
}

template <FileEndian Endian = FileEndian::Little>
[[nodiscard]] inline auto readBinary24(std::span<const uint8_t> buffer,
                                       size_t offset = 0) -> uint32_t {
  uint32_t result;
  static_assert(not isByteswapNeededFor<Endian>(),
                "readBinary24 for different endianness than native not "
                "implemented! TODO: implement this!");
  std::memcpy(&result, buffer.data() + offset, 3);
  return result;
}
}  // namespace vgmc
