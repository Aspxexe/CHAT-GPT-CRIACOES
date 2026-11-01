#pragma once

#include "aurora/cpu.hpp"
#include "aurora/media.hpp"
#include <array>

namespace aurora {
struct LoadedProgram {
    PsxExe executable;
    std::string origin;
};
struct ExecutionTrace {
    std::array<std::uint32_t, 32> pcs{}, instructions{};
    std::size_t count = 0, cursor = 0;
};
LoadedProgram loadProgram(const std::filesystem::path& path);
std::size_t runPs1(Cpu& cpu, Bus& bus, std::size_t budget, ExecutionTrace& trace);
std::string executionReport(const Cpu& cpu, const ExecutionTrace& trace);
}
