#include "aurora/ps1_memory.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace aurora {
namespace {
std::runtime_error unmapped(std::uint32_t address) {
    std::ostringstream message;
    message << "Regiao PS1 nao implementada: 0x" << std::hex << address;
    return std::runtime_error(message.str());
}
std::uint32_t physical(std::uint32_t address) {
    if ((address >= 0x20000000u && address < 0x80000000u) || address >= 0xc0000000u)
        throw unmapped(address);
    return address & 0x1fffffffu;
}
int signed16(std::uint32_t value) {
    value &= 0xffffu;
    return value & 0x8000u ? int(value) - 0x10000 : int(value);
}
int signed11(std::uint32_t value) {
    value &= 0x7ffu;
    return value & 0x400u ? int(value) - 0x800 : int(value);
}
std::uint16_t rgb555(std::uint32_t value) {
    return std::uint16_t(((value >> 3) & 31) |
        (((value >> 11) & 31) << 5) | (((value >> 19) & 31) << 10));
}
}
Ps1Memory::Ps1Memory() : ram_(2 * 1024 * 1024), vram_(1024 * 512) {}

void Ps1Memory::setPadButtons(std::uint16_t pressedMask) { padButtons_ = pressedMask; }
void Ps1Memory::beginFrame() { irqStatus_ |= 1; }

std::uint32_t Ps1Memory::gpuStatus() const {
    const auto display = ((displayMode_ & 3u) << 17) |
        ((displayMode_ & 0x3cu) << 17) | ((displayMode_ & 0x40u) << 10) |
        ((displayMode_ & 0x80u) << 7);
    return 0x14002000u | display | (displayDisabled_ ? 0x00800000u : 0) |
        (forceMask_ ? 0x800u : 0) | (checkMask_ ? 0x1000u : 0);
}

std::uint32_t Ps1Memory::joyStatus() const {
    return 5u | (receive_.empty() ? 0u : 2u);
}
std::uint8_t Ps1Memory::receiveByte() {
    if (receive_.empty()) return 0xff;
    const auto result = receive_.front();
    receive_.pop_front();
    return result;
}
void Ps1Memory::writeJoyControl(std::uint16_t value) {
    if (value & 0x40u) {
        joyMode_ = joyBaud_ = 0;
        padPhase_ = 0;
        receive_.clear();
    }
    const auto next = std::uint16_t(value & ~0x50u);
    if (!(next & 2u) || ((next ^ joyControl_) & 0x2000u)) padPhase_ = 0;
    joyControl_ = next;
}
void Ps1Memory::transmitByte(std::uint8_t value) {
    if ((joyControl_ & 3u) != 3u || (joyControl_ & 0x2000u)) return;
    std::uint8_t response = 0xff;
    switch (padPhase_) {
    case 0:
        if (value == 1) {
            padSnapshot_ = std::uint16_t(~padButtons_);
            padPhase_ = 1;
        }
        break;
    case 1:
        if (value == 0x42) { response = 0x41; padPhase_ = 2; }
        else padPhase_ = 0;
        break;
    case 2: response = 0x5a; padPhase_ = 3; break;
    case 3: response = std::uint8_t(padSnapshot_); padPhase_ = 4; break;
    case 4: response = std::uint8_t(padSnapshot_ >> 8); padPhase_ = 0; break;
    default: throw std::runtime_error("Estado de controle PS1 invalido");
    }
    if (receive_.size() < 8) receive_.push_back(response);
}

std::uint8_t Ps1Memory::read8(std::uint32_t address) {
    const auto p = physical(address);
    if (p < 0x00800000) return ram_[p & 0x1fffff];
    if (p >= 0x1f800000 && p < 0x1f800400) return scratch_[p - 0x1f800000];
    if (p == 0x1f801040) return receiveByte();
    if (p > 0x1f801040 && p < 0x1f801044) return 0;
    if (p >= 0x1f801044 && p < 0x1f801048)
        return std::uint8_t(joyStatus() >> ((p - 0x1f801044) * 8));
    if (p == 0x1f801048 || p == 0x1f801049) return std::uint8_t(joyMode_ >> ((p & 1u) * 8));
    if (p == 0x1f80104a || p == 0x1f80104b) return std::uint8_t(joyControl_ >> ((p & 1u) * 8));
    if (p == 0x1f80104e || p == 0x1f80104f) return std::uint8_t(joyBaud_ >> ((p & 1u) * 8));
    if (p >= 0x1f801070 && p < 0x1f801074) return std::uint8_t(std::uint32_t(irqStatus_) >> ((p & 3u) * 8));
    if (p >= 0x1f801074 && p < 0x1f801078) return std::uint8_t(std::uint32_t(irqMask_) >> ((p & 3u) * 8));
    if (p >= 0x1f801814 && p < 0x1f801818)
        return std::uint8_t(gpuStatus() >> ((p - 0x1f801814) * 8));
    throw unmapped(address);
}
void Ps1Memory::write8(std::uint32_t address, std::uint8_t value) {
    const auto p = physical(address);
    if (p < 0x00800000) ram_[p & 0x1fffff] = value;
    else if (p >= 0x1f800000 && p < 0x1f800400) scratch_[p - 0x1f800000] = value;
    else if (p == 0x1f801040) transmitByte(value);
    else if (p == 0x1f801048 || p == 0x1f801049) {
        const auto shift = (p & 1u) * 8;
        joyMode_ = std::uint16_t((joyMode_ & ~(0xffu << shift)) | (std::uint32_t(value) << shift));
    } else if (p == 0x1f80104a || p == 0x1f80104b) {
        const auto shift = (p & 1u) * 8;
        writeJoyControl(std::uint16_t((joyControl_ & ~(0xffu << shift)) | (std::uint32_t(value) << shift)));
    } else if (p == 0x1f80104e || p == 0x1f80104f) {
        const auto shift = (p & 1u) * 8;
        joyBaud_ = std::uint16_t((joyBaud_ & ~(0xffu << shift)) | (std::uint32_t(value) << shift));
    } else if (p >= 0x1f801070 && p < 0x1f801074) {
        const auto shift = (p & 3u) * 8;
        irqStatus_ &= std::uint16_t((std::uint32_t(value) << shift) | ~(0xffu << shift));
    } else if (p >= 0x1f801074 && p < 0x1f801078) {
        const auto shift = (p & 3u) * 8;
        irqMask_ = std::uint16_t(((std::uint32_t(irqMask_) & ~(0xffu << shift)) |
            (std::uint32_t(value) << shift)) & 0x7ffu);
    }
    else throw unmapped(address);
}
std::uint16_t Ps1Memory::read16(std::uint32_t address) {
    const auto p = physical(address);
    if (p == 0x1f801040) return receiveByte();
    if (p == 0x1f801044) return std::uint16_t(joyStatus());
    if (p == 0x1f801046) return std::uint16_t(joyStatus() >> 16);
    if (p == 0x1f801048) return joyMode_;
    if (p == 0x1f80104a) return joyControl_;
    if (p == 0x1f80104e) return joyBaud_;
    if (p == 0x1f801070) return irqStatus_;
    if (p == 0x1f801072) return 0;
    if (p == 0x1f801074) return irqMask_;
    if (p == 0x1f801076) return 0;
    if (p == 0x1f801814) return std::uint16_t(gpuStatus());
    if (p == 0x1f801816) return std::uint16_t(gpuStatus() >> 16);
    return Bus::read16(address);
}
void Ps1Memory::write16(std::uint32_t address, std::uint16_t value) {
    const auto p = physical(address);
    if (p == 0x1f801040) { transmitByte(std::uint8_t(value)); return; }
    if (p == 0x1f801048) { joyMode_ = value; return; }
    if (p == 0x1f80104a) { writeJoyControl(value); return; }
    if (p == 0x1f80104e) { joyBaud_ = value; return; }
    if (p == 0x1f801070) { irqStatus_ &= value; return; }
    if (p == 0x1f801072 || p == 0x1f801076) return;
    if (p == 0x1f801074) { irqMask_ = value & 0x7ffu; return; }
    Bus::write16(address, value);
}
std::uint32_t Ps1Memory::read32(std::uint32_t address) {
    const auto p = physical(address);
    if (p == 0x1f801040) return receiveByte();
    if (p == 0x1f801044) return joyStatus();
    if (p == 0x1f801048) return std::uint32_t(joyMode_) | (std::uint32_t(joyControl_) << 16);
    if (p == 0x1f80104c) return std::uint32_t(joyBaud_) << 16;
    if (p == 0x1f801070) return irqStatus_;
    if (p == 0x1f801074) return irqMask_;
    if (p == 0x1f801814) return gpuStatus();
    return Bus::read32(address);
}
void Ps1Memory::write32(std::uint32_t address, std::uint32_t value) {
    const auto p = physical(address);
    if (p == 0x1f801040) { transmitByte(std::uint8_t(value)); return; }
    if (p == 0x1f801048) {
        joyMode_ = std::uint16_t(value);
        writeJoyControl(std::uint16_t(value >> 16));
        return;
    }
    if (p == 0x1f80104c) { joyBaud_ = std::uint16_t(value >> 16); return; }
    if (p == 0x1f801070) { irqStatus_ &= std::uint16_t(value); return; }
    if (p == 0x1f801074) { irqMask_ = std::uint16_t(value & 0x7ffu); return; }
    if (p == 0x1f801810) { gp0(value); return; }
    if (p == 0x1f801814) {
        gp1(value);
        return;
    }
    Bus::write32(address, value);
}
void Ps1Memory::load(const PsxExe& executable) {
    const auto offset = physical(executable.loadAddress);
    if (offset >= ram_.size() || executable.data.size() > ram_.size() - offset)
        throw std::runtime_error("Payload PS-X EXE fora dos 2 MiB de RAM");
    std::copy(executable.data.begin(), executable.data.end(), ram_.begin() + offset);
}
void Ps1Memory::gp0(std::uint32_t word) {
    if (packetSize_ == 0) {
        const auto command = word >> 24;
        if (command == 0x00 || command == 0x01) return;
        if (command == 0xe3) {
            drawLeft_ = int(word & 0x3ffu);
            drawTop_ = int((word >> 10) & 0x1ffu);
            return;
        }
        if (command == 0xe4) {
            drawRight_ = int(word & 0x3ffu);
            drawBottom_ = int((word >> 10) & 0x1ffu);
            return;
        }
        if (command == 0xe5) {
            drawOffsetX_ = signed11(word);
            drawOffsetY_ = signed11(word >> 11);
            return;
        }
        if (command == 0xe6) {
            forceMask_ = (word & 1u) != 0;
            checkMask_ = (word & 2u) != 0;
            return;
        }
        if (command != 0x02 && command != 0x60)
            throw std::runtime_error("GPU PS1: comando GP0 nao implementado (apenas retangulos solidos e ambiente de desenho)");
    }
    packet_[packetSize_++] = word;
    if (packetSize_ != 3) return;
    packetSize_ = 0;
    const auto color = rgb555(packet_[0]);
    if ((packet_[0] >> 24) == 0x60) {
        const int x = signed16(packet_[1]) + drawOffsetX_;
        const int y = signed16(packet_[1] >> 16) + drawOffsetY_;
        const int width = int(packet_[2] & 0xffffu);
        const int height = int(packet_[2] >> 16);
        const int left = std::max({0, drawLeft_, x});
        const int top = std::max({0, drawTop_, y});
        const int right = std::min({1024, drawRight_ + 1, x + width});
        const int bottom = std::min({512, drawBottom_ + 1, y + height});
        for (int row = top; row < bottom; ++row) {
            for (int column = left; column < right; ++column) {
                auto& pixel = vram_[std::size_t(row) * 1024 + column];
                if (!checkMask_ || !(pixel & 0x8000u))
                    pixel = std::uint16_t(color | (forceMask_ ? 0x8000u : 0));
            }
        }
        return;
    }
    const auto x = (packet_[1] & 0x3ffu) & ~15u;
    const auto y = (packet_[1] >> 16) & 0x1ffu;
    const auto width = ((packet_[2] & 0x3ffu) + 15u) & ~15u;
    const auto height = (packet_[2] >> 16) & 0x1ffu;
    for (std::uint32_t row = 0; row < height; ++row)
        for (std::uint32_t col = 0; col < width; ++col)
            vram_[((y + row) & 511u) * 1024 + ((x + col) & 1023u)] = color;
}

void Ps1Memory::resetGpu() {
    packetSize_ = 0;
    drawLeft_ = drawTop_ = drawRight_ = drawBottom_ = 0;
    drawOffsetX_ = drawOffsetY_ = 0;
    forceMask_ = checkMask_ = false;
    displayDisabled_ = true;
    displayStart_ = displayHorizontal_ = displayVertical_ = displayMode_ = 0;
}
void Ps1Memory::gp1(std::uint32_t word) {
    switch (word >> 24) {
    case 0x00: resetGpu(); break;
    case 0x01: packetSize_ = 0; break;
    case 0x03: displayDisabled_ = (word & 1u) != 0; break;
    case 0x05: displayStart_ = word & 0x7ffffu; break;
    case 0x06: displayHorizontal_ = word & 0xffffffu; break;
    case 0x07: displayVertical_ = word & 0xfffffu; break;
    case 0x08: displayMode_ = word & 0xffu; break;
    default: throw std::runtime_error("Comando GP1 nao implementado");
    }
}
std::vector<std::uint32_t> Ps1Memory::framebuffer(std::uint32_t width,
                                                std::uint32_t height) const {
    if (width == 0 || height == 0 || width > 1024 || height > 512)
        throw std::runtime_error("Dimensoes de framebuffer invalidas");
    std::vector<std::uint32_t> pixels(std::size_t(width) * height);
    auto expand = [](std::uint32_t c) { return (c << 3) | (c >> 2); };
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto sourceX = ((displayStart_ & 0x3ffu) + x) & 1023u;
            const auto sourceY = (((displayStart_ >> 10) & 0x1ffu) + y) & 511u;
            const auto v = vram_[sourceY * 1024 + sourceX];
            pixels[y * width + x] = 0xff000000u | expand(v & 31) |
                (expand((v >> 5) & 31) << 8) | (expand((v >> 10) & 31) << 16);
        }
    return pixels;
}
}
