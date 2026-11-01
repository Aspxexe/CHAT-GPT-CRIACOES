#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace aurora {
inline constexpr std::uint32_t HomebrewStateAddress = 0x80080000;
inline constexpr std::uint32_t HomebrewPlayerX = 0;
inline constexpr std::uint32_t HomebrewPlayerY = 4;
inline constexpr std::uint32_t HomebrewScore = 8;
inline constexpr std::uint32_t HomebrewPhase = 12;
inline constexpr std::uint32_t HomebrewEnemyX = 16;
inline constexpr std::uint32_t HomebrewEnemyDirection = 20;
inline constexpr std::uint32_t HomebrewOldButtons = 24;
inline constexpr std::uint32_t HomebrewFrameCounter = 28;
inline constexpr std::uint32_t HomebrewPlaying = 0;
inline constexpr std::uint32_t HomebrewWon = 1;
inline constexpr std::uint32_t HomebrewLost = 2;
inline constexpr std::uint32_t HomebrewInitialPlayerX = 32;
inline constexpr std::uint32_t HomebrewInitialPlayerY = 64;
inline constexpr std::uint32_t HomebrewInitialEnemyX = 144;
inline constexpr std::uint32_t HomebrewEnemyY = 112;
inline constexpr std::uint32_t HomebrewPlayerSize = 16;
struct HomebrewPoint { std::uint32_t x, y; };
inline constexpr std::array<HomebrewPoint, 5> HomebrewCrystals{{
    {64, 64}, {240, 64}, {240, 176}, {64, 176}, {152, 48}
}};

std::vector<std::uint8_t> makeHomebrewPsxExe();
}
