#include "VgmDecompressor.hxx"

#include <zlib.h>

#include <fstream>
#include <new>
#include <stdexcept>

namespace vgmc {
// Размер чанка для декомпрессии (64 КБ — оптимально для L1/L2 кэша процессора)
constexpr size_t CHUNK_SIZE =
    1024 * std::hardware_destructive_interference_size;

constexpr std::string_view VGM_IDENT{"Vgm "};

auto decompressVgmFileToRam(const std::string& filename)
    -> std::vector<uint8_t> {
  // 1. Быстро и полностью считываем СЖАТЫЙ файл в память через std::ifstream
  std::ifstream file(filename, std::ios::binary | std::ios::ate);

  if (not file.is_open()) {
    throw std::runtime_error{"Can't open file"};
  }

  std::streamsize compressedSize = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> compressedData(compressedSize);

  if (!file.read(reinterpret_cast<char*>(compressedData.data()),
                 compressedSize)) {
    throw std::runtime_error{"File read error"};
  }

  if (compressedData.size() < VGM_IDENT.size()) {
    throw std::runtime_error{"File incorrect: file too small"};
  }

  const std::string_view readVgmIdent{
      reinterpret_cast<const char*>(compressedData.data()), VGM_IDENT.size()};

  if (readVgmIdent == VGM_IDENT) {
    // This is uncompressed file
    return compressedData;
  }

  // 2. Инициализация структуры zlib (inflate)
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = compressedData.size();
  strm.next_in = compressedData.data();

  // 15 + 32 автоматически определяет формат: gzip (.gz) или zlib
  if (inflateInit2(&strm, 15 + 32) != Z_OK) {
    throw std::runtime_error{"ZLIB init error."};
  }

  // Резервируем буфер под несжатые данные.
  // Обычно коэффициент сжатия ~2-4х, берем сжатый размер * 3 как базовый
  // ориентир.
  std::vector<uint8_t> uncompressedData;
  uncompressedData.reserve(compressedSize * 3);

  int ret;
  size_t currentOffset = 0;

  // 3. Цикл декомпрессии напрямую в RAM
  do {
    // Гарантируем, что в векторе достаточно места для следующего чанка
    if (uncompressedData.size() - currentOffset < CHUNK_SIZE) {
      // Экспоненциальное расширение вектора (удвоение емкости) снижает
      // накладные расходы
      const size_t newCapacity = uncompressedData.capacity() == 0
                                     ? CHUNK_SIZE
                                     : uncompressedData.capacity() * 2;
      uncompressedData.resize(newCapacity);
    }

    strm.avail_out = uncompressedData.size() - currentOffset;
    strm.next_out = uncompressedData.data() + currentOffset;

    ret = inflate(&strm, Z_NO_FLUSH);

    switch (ret) {
      case Z_NEED_DICT:
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
        inflateEnd(&strm);
        throw std::runtime_error{"Ошибка zlib при декомпрессии."};

      default:
        // Continue work
        break;
    }

    // Вычисляем, сколько байт было реально записано на этом шаге
    size_t written = (uncompressedData.size() - currentOffset) - strm.avail_out;
    currentOffset += written;
  } while (ret != Z_STREAM_END);

  // Освобождаем ресурсы zlib
  inflateEnd(&strm);

  // Обрезаем вектор до реального размера несжатых данных (shrink_to_fit не
  // вызываем, чтобы не копировать память)
  uncompressedData.resize(currentOffset);

  return uncompressedData;
}
}  // namespace vgmc
