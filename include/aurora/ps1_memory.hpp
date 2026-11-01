#pragma once

#include "aurora/bus.hpp"
#include "aurora/media.hpp"
#include <array>
#include <deque>
#include <vector>

namespace aurora {
class Ps1Memory final : public Bus {
public:
    Ps1Memory();
    std::uint8_t read8(std::uint32_t address) override;
    void write8(std::uint32_t address, std::uint8_t value) override;
    std::uint16_t read16(std::uint32_t address) override;
    void write16(std::uint32_t address, std::uint16_t value) override;
    std::uint32_t read32(std::uint32_t address) override;
    void write32(std::uint32_t address, std::uint32_t value) override;
    void load(const PsxExe& executable);
    void setPadButtons(std::uint16_t pressedMask);
    void beginFrame();
    std::vector<std::uint32_t> framebuffer(std::uint32_t width = 320,
                                           std::uint32_t height = 240) const;
    const std::vector<std::uint16_t>& vram() const { return vram_; }
private:
    std::vector<std::uint8_t> ram_;
    std::array<std::uint8_t, 1024> scratch_{};
    std::vector<std::uint16_t> vram_;
    std::array<std::uint32_t, 3> packet_{};
    std::size_t packetSize_ = 0;
    std::uint16_t padButtons_ = 0;
    std::uint16_t padSnapshot_ = 0xffff;
    std::uint16_t joyMode_ = 0, joyControl_ = 0, joyBaud_ = 0;
    unsigned padPhase_ = 0;
    std::deque<std::uint8_t> receive_;
    std::uint16_t irqStatus_ = 0, irqMask_ = 0;
    int drawLeft_ = 0, drawTop_ = 0, drawRight_ = 0, drawBottom_ = 0;
    int drawOffsetX_ = 0, drawOffsetY_ = 0;
    bool forceMask_ = false, checkMask_ = false, displayDisabled_ = true;
    std::uint32_t displayStart_ = 0, displayHorizontal_ = 0, displayVertical_ = 0, displayMode_ = 0;
    void gp0(std::uint32_t word);
    void gp1(std::uint32_t word);
    void resetGpu();
    std::uint32_t gpuStatus() const;
    std::uint32_t joyStatus() const;
    std::uint8_t receiveByte();
    void transmitByte(std::uint8_t value);
    void writeJoyControl(std::uint16_t value);
};
}
