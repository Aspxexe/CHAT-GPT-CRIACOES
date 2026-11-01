#include "aurora/cpu.hpp"

#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>

namespace aurora {
namespace {
std::int64_t signed32(std::uint32_t value) {
    return (value & 0x80000000u) ? std::int64_t(value) - 0x100000000ll : value;
}

std::uint32_t arithmeticShift(std::uint32_t value, unsigned amount) {
    if (amount == 0) return value;
    return (value >> amount) | ((value & 0x80000000u) ? (0xffffffffu << (32 - amount)) : 0);
}

bool signedOverflow(std::int64_t value) {
    return value < std::numeric_limits<std::int32_t>::min() ||
           value > std::numeric_limits<std::int32_t>::max();
}
}

Cpu::Cpu(Bus& bus) : bus_(bus) {
    reset(0);
}

void Cpu::reset(std::uint32_t entry) {
    regs.fill(0);
    hi = lo = 0;
    pc = entry;
    nextPc_ = entry + 4;
    pendingRegister_ = 0;
    pendingValue_ = 0;
    instructions = 0;
    fault.clear();
}

bool Cpu::fail(const std::string& reason, std::uint32_t instruction) {
    std::ostringstream message;
    message << reason << " (PC=0x" << std::hex << std::setw(8) << std::setfill('0') << pc
            << ", instrucao=0x" << std::setw(8) << instruction << ')';
    fault = message.str();
    regs[0] = 0;
    return false;
}

bool Cpu::step() {
    if (!fault.empty()) return false;
    regs[0] = 0;
    if (pc & 3u) return fail("Busca de instrucao desalinhada");

    std::uint32_t code = 0;
    try {
        code = bus_.read32(pc);
        const unsigned op = code >> 26;
        const unsigned rs = (code >> 21) & 31;
        const unsigned rt = (code >> 16) & 31;
        const unsigned rd = (code >> 11) & 31;
        const unsigned shift = (code >> 6) & 31;
        const std::uint32_t a = regs[rs];
        const std::uint32_t b = regs[rt];
        const std::int64_t signedImmediate = (code & 0x8000u) ?
            std::int64_t(code & 0xffffu) - 0x10000 : code & 0xffffu;
        const auto immediate = static_cast<std::uint32_t>(signedImmediate);
        const std::uint32_t address = a + immediate;
        const std::uint32_t followingPc = nextPc_;
        std::uint32_t followingNextPc = nextPc_ + 4;

        const unsigned completingRegister = pendingRegister_;
        if (pendingRegister_ != 0) regs[pendingRegister_] = pendingValue_;
        pendingRegister_ = 0;

        const auto write = [&](unsigned reg, std::uint32_t value) {
            if (reg != 0) regs[reg] = value;
        };
        const auto load = [&](std::uint32_t value) {
            // No R3000A, um novo load para o mesmo rt cancela o writeback anterior.
            if (rt != 0 && rt == completingRegister) regs[rt] = b;
            pendingRegister_ = rt;
            pendingValue_ = value;
        };
        const auto branch = [&](bool take) {
            if (take) followingNextPc = pc + 4 + immediate * 4u;
        };

        switch (op) {
        case 0: {
            switch (code & 63u) {
            case 0x00: write(rd, b << shift); break;
            case 0x02: write(rd, b >> shift); break;
            case 0x03: write(rd, arithmeticShift(b, shift)); break;
            case 0x04: write(rd, b << (a & 31)); break;
            case 0x06: write(rd, b >> (a & 31)); break;
            case 0x07: write(rd, arithmeticShift(b, a & 31)); break;
            case 0x08: followingNextPc = a; break;
            case 0x09: write(rd, pc + 8); followingNextPc = a; break;
            case 0x0c: return fail("SYSCALL: excecoes de sistema ainda nao implementadas", code);
            case 0x0d: return fail("BREAK: excecoes de depuracao ainda nao implementadas", code);
            case 0x10: write(rd, hi); break;
            case 0x11: hi = a; break;
            case 0x12: write(rd, lo); break;
            case 0x13: lo = a; break;
            case 0x18: {
                const auto product = static_cast<std::uint64_t>(signed32(a) * signed32(b));
                lo = static_cast<std::uint32_t>(product);
                hi = static_cast<std::uint32_t>(product >> 32);
                break;
            }
            case 0x19: {
                const auto product = std::uint64_t(a) * b;
                lo = static_cast<std::uint32_t>(product);
                hi = static_cast<std::uint32_t>(product >> 32);
                break;
            }
            case 0x1a:
                if (b == 0) {
                    lo = signed32(a) < 0 ? 1u : 0xffffffffu;
                    hi = a;
                } else {
                    lo = static_cast<std::uint32_t>(signed32(a) / signed32(b));
                    hi = static_cast<std::uint32_t>(signed32(a) % signed32(b));
                }
                break;
            case 0x1b:
                lo = b ? a / b : 0xffffffffu;
                hi = b ? a % b : a;
                break;
            case 0x20: {
                const auto value = signed32(a) + signed32(b);
                if (signedOverflow(value)) return fail("Overflow em ADD", code);
                write(rd, static_cast<std::uint32_t>(value));
                break;
            }
            case 0x21: write(rd, a + b); break;
            case 0x22: {
                const auto value = signed32(a) - signed32(b);
                if (signedOverflow(value)) return fail("Overflow em SUB", code);
                write(rd, static_cast<std::uint32_t>(value));
                break;
            }
            case 0x23: write(rd, a - b); break;
            case 0x24: write(rd, a & b); break;
            case 0x25: write(rd, a | b); break;
            case 0x26: write(rd, a ^ b); break;
            case 0x27: write(rd, ~(a | b)); break;
            case 0x2a: write(rd, signed32(a) < signed32(b)); break;
            case 0x2b: write(rd, a < b); break;
            default: return fail("Instrucao SPECIAL nao implementada", code);
            }
            break;
        }
        case 0x01:
            if (rt != 0 && rt != 1 && rt != 16 && rt != 17)
                return fail("Instrucao REGIMM nao implementada", code);
            if (rt & 16) write(31, pc + 8);
            branch((rt & 1) ? signed32(a) >= 0 : signed32(a) < 0);
            break;
        case 0x02: followingNextPc = ((pc + 4) & 0xf0000000u) | ((code & 0x03ffffffu) << 2); break;
        case 0x03:
            write(31, pc + 8);
            followingNextPc = ((pc + 4) & 0xf0000000u) | ((code & 0x03ffffffu) << 2);
            break;
        case 0x04: branch(a == b); break;
        case 0x05: branch(a != b); break;
        case 0x06:
            if (rt != 0) return fail("Codificacao BLEZ invalida", code);
            branch(signed32(a) <= 0); break;
        case 0x07:
            if (rt != 0) return fail("Codificacao BGTZ invalida", code);
            branch(signed32(a) > 0); break;
        case 0x08: {
            const auto value = signed32(a) + signedImmediate;
            if (signedOverflow(value)) return fail("Overflow em ADDI", code);
            write(rt, static_cast<std::uint32_t>(value));
            break;
        }
        case 0x09: write(rt, a + immediate); break;
        case 0x0a: write(rt, signed32(a) < signedImmediate); break;
        case 0x0b: write(rt, a < immediate); break;
        case 0x0c: write(rt, a & (code & 0xffffu)); break;
        case 0x0d: write(rt, a | (code & 0xffffu)); break;
        case 0x0e: write(rt, a ^ (code & 0xffffu)); break;
        case 0x0f: write(rt, (code & 0xffffu) << 16); break;
        case 0x10: return fail("COP0 ainda nao implementado: BIOS/excecoes indisponiveis", code);
        case 0x11: return fail("COP1 nao disponivel no PS1", code);
        case 0x12: return fail("COP2/GTE ainda nao implementado", code);
        case 0x20: {
            const auto value = bus_.read8(address);
            load((value & 0x80u) ? 0xffffff00u | value : value);
            break;
        }
        case 0x21: {
            if (address & 1u) return fail("LH desalinhado", code);
            const auto value = bus_.read16(address);
            load((value & 0x8000u) ? 0xffff0000u | value : value);
            break;
        }
        case 0x23:
            if (address & 3u) return fail("LW desalinhado", code);
            load(bus_.read32(address)); break;
        case 0x24: load(bus_.read8(address)); break;
        case 0x25:
            if (address & 1u) return fail("LHU desalinhado", code);
            load(bus_.read16(address)); break;
        case 0x28: bus_.write8(address, static_cast<std::uint8_t>(b)); break;
        case 0x29:
            if (address & 1u) return fail("SH desalinhado", code);
            bus_.write16(address, static_cast<std::uint16_t>(b)); break;
        case 0x2b:
            if (address & 3u) return fail("SW desalinhado", code);
            bus_.write32(address, b); break;
        default: return fail("Opcode ainda nao implementado", code);
        }

        regs[0] = 0;
        pc = followingPc;
        nextPc_ = followingNextPc;
        ++instructions;
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("Falha no barramento: ") + error.what(), code);
    } catch (...) {
        return fail("Falha desconhecida no barramento", code);
    }
}

std::size_t Cpu::run(std::size_t maxInstructions) {
    std::size_t executed = 0;
    while (executed < maxInstructions && step()) ++executed;
    return executed;
}
}
