#pragma once

#include "aurora/bus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace aurora {
class Cpu {
public:
    explicit Cpu(Bus& bus);
    void reset(std::uint32_t entry);
    bool step();
    std::size_t run(std::size_t maxInstructions);

    std::array<std::uint32_t, 32> regs{};
    std::uint32_t hi = 0;
    std::uint32_t lo = 0;
    std::uint32_t pc = 0;
    std::string fault;
    std::uint64_t instructions = 0;

private:
    Bus& bus_;
    std::uint32_t nextPc_ = 4;
    std::uint32_t pendingValue_ = 0;
    unsigned pendingRegister_ = 0;

    bool fail(const std::string& reason, std::uint32_t instruction = 0);
};
}
