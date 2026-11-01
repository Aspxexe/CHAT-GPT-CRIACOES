#pragma once

#include "aurora/media.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

namespace aurora {
struct DiscInfo {
    std::string format;
    std::string description;
    std::string volumeId;
    std::string bootPath;
    std::uint32_t sectorSize = 0;
    std::uint32_t dataOffset = 0;
    bool ps1Boot = false;
    std::uint64_t firstSector = 0;
    std::uint64_t declaredSectors = 0;
    std::uint64_t availableSectors = 0;
    std::string warning{};
};
struct DiscBoot {
    PsxExe executable;
    DiscInfo disc;
};
DiscInfo inspectDisc(const std::filesystem::path& path);
DiscBoot loadPs1Disc(const std::filesystem::path& path);
}
