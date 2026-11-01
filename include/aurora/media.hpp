#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aurora {

struct MediaInfo {
    std::string kind;
    std::string description;
    bool executable = false;
};

struct PsxExe {
    std::uint32_t entry = 0;
    std::uint32_t gp = 0;
    std::uint32_t loadAddress = 0;
    std::uint32_t stackBase = 0;
    std::uint32_t stackOffset = 0;
    std::vector<std::uint8_t> data;
};

MediaInfo inspectMedia(const std::filesystem::path& path);
PsxExe loadPsxExe(const std::filesystem::path& path);
PsxExe parsePsxExe(const std::vector<std::uint8_t>& image);
MediaInfo inspectCueFile(const std::filesystem::path& path);
std::string diagnoseMedia(const std::filesystem::path& path);
std::vector<std::uint8_t> makeDemoPsxExe();

}
