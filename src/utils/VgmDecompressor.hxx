#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vgmc {
[[nodiscard]] auto decompressVgmFileToRam(const std::string& filename)
    -> std::vector<uint8_t>;
}
