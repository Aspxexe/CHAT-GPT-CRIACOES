#pragma once

#include <cstdint>

namespace aurora {
class Bus {
public:
    virtual ~Bus() = default;
    virtual std::uint8_t read8(std::uint32_t address) = 0;
    virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    virtual std::uint16_t read16(std::uint32_t address) {
        return std::uint16_t(std::uint16_t(read8(address)) |
                             (std::uint16_t(read8(address + 1)) << 8));
    }
    virtual std::uint32_t read32(std::uint32_t address) {
        return std::uint32_t(read16(address)) | (std::uint32_t(read16(address + 2)) << 16);
    }
    virtual void write16(std::uint32_t address, std::uint16_t value) {
        write8(address, std::uint8_t(value));
        write8(address + 1, std::uint8_t(value >> 8));
    }
    virtual void write32(std::uint32_t address, std::uint32_t value) {
        write16(address, std::uint16_t(value));
        write16(address + 2, std::uint16_t(value >> 16));
    }
};
}
