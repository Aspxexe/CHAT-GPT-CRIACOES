#include "aurora/cpu.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
class Memory : public aurora::Bus {
public:
    std::array<std::uint8_t, 8192> bytes{};
    std::uint8_t read8(std::uint32_t address) override { return bytes.at(address); }
    void write8(std::uint32_t address, std::uint8_t value) override { bytes.at(address) = value; }
};

struct Machine {
    Memory memory;
    aurora::Cpu cpu{memory};
    void program(std::initializer_list<std::uint32_t> code) {
        std::uint32_t address = 0;
        for (auto word : code) {
            memory.write32(address, word);
            address += 4;
        }
    }
    void run(std::size_t count) {
        if (cpu.run(count) != count) throw std::runtime_error(cpu.fault);
    }
};

constexpr std::uint32_t r(unsigned function, unsigned rs, unsigned rt, unsigned rd, unsigned shift = 0) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shift << 6) | function;
}
constexpr std::uint32_t i(unsigned op, unsigned rs, unsigned rt, std::uint16_t immediate) {
    return (op << 26) | (rs << 21) | (rt << 16) | immediate;
}
constexpr std::uint32_t j(unsigned op, std::uint32_t target) {
    return (op << 26) | ((target >> 2) & 0x03ffffffu);
}

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void equal(std::uint64_t actual, std::uint64_t expected, const std::string& message) {
    expect(actual == expected, message + ": atual=" + std::to_string(actual) + ", esperado=" + std::to_string(expected));
}

void faultContains(aurora::Cpu& cpu, const std::string& part) {
    expect(!cpu.step(), "A instrucao deveria falhar");
    expect(cpu.fault.find(part) != std::string::npos, "Mensagem de falha incorreta: " + cpu.fault);
    expect(!cpu.step(), "Uma falha deve permanecer ate reset");
}
}

int main() {
    unsigned passed = 0;
    unsigned failed = 0;
    const auto test = [&](const std::string& name, const std::function<void()>& body) {
        try {
            body();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    test("reset, zero e contagem", [] {
        Machine m;
        m.cpu.regs[0] = 123;
        m.program({i(9, 0, 0, 17), i(9, 0, 1, 10)});
        m.run(2);
        equal(m.cpu.regs[0], 0, "$zero imutavel");
        equal(m.cpu.regs[1], 10, "Leitura de $zero");
        equal(m.cpu.pc, 8, "PC sequencial");
        equal(m.cpu.instructions, 2, "Contagem de instrucoes");
        m.cpu.reset(64);
        equal(m.cpu.pc, 64, "PC reset");
        equal(m.cpu.regs[1], 0, "Registradores reset");
        equal(m.cpu.instructions, 0, "Contagem reset");
    });

    test("ADDU SUBU e ADDIU modulo 32 bits", [] {
        Machine m;
        m.cpu.regs[1] = 0xffffffffu;
        m.cpu.regs[2] = 2;
        m.program({r(0x21, 1, 2, 3), r(0x23, 2, 1, 4), i(9, 0, 5, 0xffff)});
        m.run(3);
        equal(m.cpu.regs[3], 1, "ADDU wrap");
        equal(m.cpu.regs[4], 3, "SUBU wrap");
        equal(m.cpu.regs[5], 0xffffffffu, "ADDIU sinal");
    });

    test("ADD e SUB com sinal", [] {
        Machine m;
        m.cpu.regs[1] = std::uint32_t(-42);
        m.cpu.regs[2] = 12;
        m.program({r(0x20, 1, 2, 3), r(0x22, 2, 1, 4)});
        m.run(2);
        equal(m.cpu.regs[3], std::uint32_t(-30), "ADD negativo");
        equal(m.cpu.regs[4], 54, "SUB negativo");
    });

    test("ADD overflow nao grava destino", [] {
        Machine m;
        m.cpu.regs[1] = 0x7fffffffu;
        m.cpu.regs[2] = 1;
        m.cpu.regs[3] = 123;
        m.program({r(0x20, 1, 2, 3)});
        faultContains(m.cpu, "Overflow em ADD");
        equal(m.cpu.regs[3], 123, "Destino preservado");
        equal(m.cpu.pc, 0, "PC da falha");
        equal(m.cpu.instructions, 0, "Falha nao contada");
    });

    test("SUB e ADDI overflow", [] {
        Machine m;
        m.cpu.regs[1] = 0x80000000u;
        m.cpu.regs[2] = 1;
        m.program({r(0x22, 1, 2, 3)});
        faultContains(m.cpu, "Overflow em SUB");
        m.cpu.reset(0);
        m.cpu.regs[1] = 0x80000000u;
        m.program({i(8, 1, 2, 0xffff)});
        faultContains(m.cpu, "Overflow em ADDI");
    });

    test("Shifts fixos e variaveis", [] {
        Machine m;
        m.cpu.regs[1] = 0x80000001u;
        m.cpu.regs[2] = 36;
        m.program({r(0, 0, 1, 3, 1), r(2, 0, 1, 4, 4), r(3, 0, 1, 5, 4),
                   r(4, 2, 1, 6), r(6, 2, 1, 7), r(7, 2, 1, 8), r(3, 0, 1, 9, 0)});
        m.run(7);
        equal(m.cpu.regs[3], 2, "SLL");
        equal(m.cpu.regs[4], 0x08000000u, "SRL");
        equal(m.cpu.regs[5], 0xf8000000u, "SRA");
        equal(m.cpu.regs[6], 16, "SLLV mascara");
        equal(m.cpu.regs[7], 0x08000000u, "SRLV mascara");
        equal(m.cpu.regs[8], 0xf8000000u, "SRAV mascara");
        equal(m.cpu.regs[9], 0x80000001u, "SRA zero");
    });

    test("AND OR XOR NOR e imediatos", [] {
        Machine m;
        m.cpu.regs[1] = 0xaaaa000fu;
        m.cpu.regs[2] = 0x555500ffu;
        m.program({r(0x24, 1, 2, 3), r(0x25, 1, 2, 4), r(0x26, 1, 2, 5),
                   r(0x27, 1, 2, 6), i(0x0c, 1, 7, 0x800f), i(0x0d, 0, 8, 0x8000),
                   i(0x0e, 1, 9, 0xffff), i(0x0f, 0, 10, 0xabcd)});
        m.run(8);
        equal(m.cpu.regs[3], 15, "AND");
        equal(m.cpu.regs[4], 0xffff00ffu, "OR");
        equal(m.cpu.regs[5], 0xffff00f0u, "XOR");
        equal(m.cpu.regs[6], 0x0000ff00u, "NOR");
        equal(m.cpu.regs[7], 15, "ANDI");
        equal(m.cpu.regs[8], 0x8000, "ORI zero extend");
        equal(m.cpu.regs[9], 0xaaaafff0u, "XORI zero extend");
        equal(m.cpu.regs[10], 0xabcd0000u, "LUI");
    });

    test("SLT SLTU SLTI SLTIU diferenciam sinal", [] {
        Machine m;
        m.cpu.regs[1] = 0xffffffffu;
        m.cpu.regs[2] = 1;
        m.program({r(0x2a, 1, 2, 3), r(0x2b, 1, 2, 4), i(0x0a, 1, 5, 0),
                   i(0x0b, 2, 6, 0xffff), i(0x0b, 1, 7, 0xffff)});
        m.run(5);
        equal(m.cpu.regs[3], 1, "SLT negativo");
        equal(m.cpu.regs[4], 0, "SLTU");
        equal(m.cpu.regs[5], 1, "SLTI");
        equal(m.cpu.regs[6], 1, "SLTIU immediate sign extend");
        equal(m.cpu.regs[7], 0, "SLTIU igual");
    });

    test("BEQ tomado executa branch delay", [] {
        Machine m;
        m.program({i(4, 0, 0, 2), i(9, 0, 1, 11), i(9, 0, 1, 22), i(9, 0, 2, 33)});
        m.run(3);
        equal(m.cpu.regs[1], 11, "Delay executado e caminho pulado");
        equal(m.cpu.regs[2], 33, "Destino executado");
        equal(m.cpu.pc, 16, "PC apos destino");
    });

    test("BNE nao tomado segue sequencialmente", [] {
        Machine m;
        m.program({i(5, 0, 0, 2), i(9, 0, 1, 11), i(9, 0, 2, 22)});
        m.run(3);
        equal(m.cpu.regs[1], 11, "Delay nao tomado");
        equal(m.cpu.regs[2], 22, "Fallthrough");
    });

    test("Branch relativo negativo", [] {
        Machine m;
        m.program({i(9, 1, 1, 1), i(5, 1, 2, 0xfffe), 0});
        m.cpu.regs[2] = 3;
        m.run(9);
        equal(m.cpu.regs[1], 3, "Loop relativo");
        equal(m.cpu.pc, 12, "Loop terminou");
    });

    test("Branches BLEZ BGTZ BLTZ BGEZ", [] {
        for (unsigned op : {6u, 7u, 1u}) {
            Machine m;
            m.cpu.regs[1] = op == 7 ? 1 : 0xffffffffu;
            m.program({i(op, 1, 0, 2), i(9, 0, 2, 11), i(9, 0, 2, 22), i(9, 0, 3, 33)});
            m.run(3);
            equal(m.cpu.regs[2], 11, "Branch condicional tomou desvio");
            equal(m.cpu.regs[3], 33, "Destino condicional");
        }
        Machine m;
        m.program({i(1, 0, 1, 2), 0, i(9, 0, 1, 1), i(9, 0, 2, 2)});
        m.run(3);
        equal(m.cpu.regs[1], 0, "BGEZ zero pula");
        equal(m.cpu.regs[2], 2, "BGEZ destino");
    });

    test("J JAL JR e JALR com delay e link", [] {
        Machine m;
        m.program({j(3, 16), i(9, 0, 1, 1), j(2, 28), 0,
                   r(8, 31, 0, 0), i(9, 0, 2, 2), i(9, 0, 3, 3), i(9, 0, 4, 4)});
        m.run(7);
        equal(m.cpu.regs[31], 8, "JAL link PC+8");
        equal(m.cpu.regs[1], 1, "JAL delay");
        equal(m.cpu.regs[2], 2, "JR delay");
        equal(m.cpu.regs[3], 0, "J pulou instrucao");
        equal(m.cpu.regs[4], 4, "J destino");
        m.cpu.reset(0);
        m.cpu.regs[1] = 16;
        m.program({r(9, 1, 0, 1), i(9, 0, 2, 2), 0, 0, i(9, 0, 3, 3)});
        m.run(3);
        equal(m.cpu.regs[1], 8, "JALR rs=rd link");
        equal(m.cpu.regs[3], 3, "JALR usa alvo anterior ao link");
    });

    test("BLTZAL link mesmo sem desvio", [] {
        Machine m;
        m.program({i(1, 0, 16, 2), 0, i(9, 0, 1, 1)});
        m.run(3);
        equal(m.cpu.regs[31], 8, "Link REGIMM");
        equal(m.cpu.regs[1], 1, "Branch nao tomado");
    });

    test("LW tem um load delay", [] {
        Machine m;
        m.cpu.regs[1] = 7;
        m.memory.write32(1024, 42);
        m.program({i(0x23, 0, 1, 1024), r(0x21, 1, 0, 2), r(0x21, 1, 0, 3)});
        m.run(1);
        equal(m.cpu.regs[1], 7, "Load ainda pendente");
        m.run(1);
        equal(m.cpu.regs[2], 7, "Instrucao seguinte le valor antigo");
        equal(m.cpu.regs[1], 42, "Load commit");
        m.run(1);
        equal(m.cpu.regs[3], 42, "Proxima instrucao le novo valor");
    });

    test("Write no delay sobrepoe load", [] {
        Machine m;
        m.memory.write32(1024, 42);
        m.program({i(0x23, 0, 1, 1024), i(9, 0, 1, 99), r(0x21, 1, 0, 2)});
        m.run(3);
        equal(m.cpu.regs[1], 99, "Overwrite vence load");
        equal(m.cpu.regs[2], 99, "Overwrite persistente");
    });

    test("Load consecutivo mesmo rt cancela writeback anterior", [] {
        Machine m;
        m.cpu.regs[1] = 7;
        m.memory.write32(1024, 42);
        m.memory.write32(1028, 84);
        m.program({i(0x23, 0, 1, 1024), i(0x23, 0, 1, 1028),
                   r(0x21, 1, 0, 2), r(0x21, 1, 0, 3)});
        m.run(2);
        equal(m.cpu.regs[1], 7, "Primeiro load foi cancelado");
        m.run(2);
        equal(m.cpu.regs[2], 7, "Segundo load ainda tem delay e le valor anterior aos dois");
        equal(m.cpu.regs[3], 84, "Segundo load visivel");
    });

    test("Loads consecutivos destinos diferentes completam ambos", [] {
        Machine m;
        m.cpu.regs[1] = 7;
        m.cpu.regs[2] = 9;
        m.memory.write32(1024, 42);
        m.memory.write32(1028, 84);
        m.program({i(0x23, 0, 1, 1024), i(0x23, 0, 2, 1028),
                   r(0x21, 1, 2, 3), r(0x21, 1, 2, 4)});
        m.run(4);
        equal(m.cpu.regs[1], 42, "Primeiro load com destino diferente completa");
        equal(m.cpu.regs[2], 84, "Segundo load completa");
        equal(m.cpu.regs[3], 51, "Instrucao no delay le primeiro resultado e segundo valor antigo");
        equal(m.cpu.regs[4], 126, "Resultados dos dois loads disponiveis");
    });

    test("Loads de larguras diferentes mesmo rt tambem cancelam", [] {
        Machine m;
        m.cpu.regs[1] = 7;
        m.memory.write32(1024, 42);
        m.memory.write8(1028, 0x80);
        m.program({i(0x23, 0, 1, 1024), i(0x20, 0, 1, 1028),
                   r(0x21, 1, 0, 2), 0});
        m.run(4);
        equal(m.cpu.regs[2], 7, "LB cancela LW anterior sem expor valor intermedio");
        equal(m.cpu.regs[1], 0xffffff80u, "LB completa apos seu delay");
    });

    test("Store no load delay usa operando antigo", [] {
        Machine m;
        m.cpu.regs[1] = 7;
        m.memory.write32(1024, 42);
        m.program({i(0x23, 0, 1, 1024), i(0x2b, 0, 1, 1028), 0});
        m.run(3);
        equal(m.memory.read32(1028), 7, "SW usa valor anterior");
        equal(m.cpu.regs[1], 42, "LW commit ocorreu");
    });

    test("LB LBU LH LHU e little endian", [] {
        Machine m;
        m.memory.write32(1024, 0x8001ff80u);
        m.program({i(0x20, 0, 1, 1024), 0, i(0x24, 0, 2, 1024), 0,
                   i(0x21, 0, 3, 1026), 0, i(0x25, 0, 4, 1026), 0});
        m.run(8);
        equal(m.cpu.regs[1], 0xffffff80u, "LB sign extend");
        equal(m.cpu.regs[2], 128, "LBU zero extend");
        equal(m.cpu.regs[3], 0xffff8001u, "LH sign extend");
        equal(m.cpu.regs[4], 0x8001, "LHU zero extend");
    });

    test("SB SH SW e imediato negativo", [] {
        Machine m;
        m.cpu.regs[1] = 1024;
        m.cpu.regs[2] = 0x1234abcdu;
        m.program({i(0x28, 1, 2, 0xffff), i(0x29, 1, 2, 0), i(0x2b, 1, 2, 4)});
        m.run(3);
        equal(m.memory.read8(1023), 0xcd, "SB mascara e offset negativo");
        equal(m.memory.read16(1024), 0xabcd, "SH mascara");
        equal(m.memory.read32(1028), 0x1234abcd, "SW");
    });

    test("Loads e stores desalinhados falham", [] {
        for (unsigned op : {0x21u, 0x25u, 0x23u, 0x29u, 0x2bu}) {
            Machine m;
            m.program({i(op, 0, 1, 1025)});
            faultContains(m.cpu, "desalinhado");
        }
    });

    test("Alvo JR desalinhado executa delay antes da falha", [] {
        Machine m;
        m.cpu.regs[1] = 17;
        m.program({r(8, 1, 0, 0), i(9, 0, 2, 2)});
        m.run(2);
        equal(m.cpu.regs[2], 2, "JR delay executado");
        equal(m.cpu.pc, 17, "PC desalinhado apos delay");
        faultContains(m.cpu, "Busca de instrucao desalinhada");
    });

    test("MULT e MULTU HI LO", [] {
        Machine m;
        m.cpu.regs[1] = 0xfffffffeu;
        m.cpu.regs[2] = 3;
        m.program({r(0x18, 1, 2, 0), r(0x10, 0, 0, 3), r(0x12, 0, 0, 4),
                   r(0x19, 1, 2, 0), r(0x10, 0, 0, 5), r(0x12, 0, 0, 6)});
        m.run(6);
        equal(m.cpu.regs[3], 0xffffffffu, "MULT HI assinado");
        equal(m.cpu.regs[4], 0xfffffffau, "MULT LO assinado");
        equal(m.cpu.regs[5], 2, "MULTU HI");
        equal(m.cpu.regs[6], 0xfffffffau, "MULTU LO");
    });

    test("DIV DIVU e sinal do resto", [] {
        Machine m;
        m.cpu.regs[1] = std::uint32_t(-13);
        m.cpu.regs[2] = 3;
        m.program({r(0x1a, 1, 2, 0), r(0x10, 0, 0, 3), r(0x12, 0, 0, 4),
                   r(0x1b, 1, 2, 0)});
        m.run(4);
        equal(m.cpu.regs[3], 0xffffffffu, "DIV resto negativo");
        equal(m.cpu.regs[4], std::uint32_t(-4), "DIV quociente");
        equal(m.cpu.lo, std::uint32_t(-13) / 3, "DIVU quociente");
        equal(m.cpu.hi, std::uint32_t(-13) % 3, "DIVU resto");
    });

    test("DIV limites sem overflow C++", [] {
        Machine m;
        m.cpu.regs[1] = 0x80000000u;
        m.cpu.regs[2] = 0xffffffffu;
        m.program({r(0x1a, 1, 2, 0)});
        m.run(1);
        equal(m.cpu.lo, 0x80000000u, "INT_MIN / -1");
        equal(m.cpu.hi, 0, "Resto overflow");
        for (auto a : {0u, 13u, 0xfffffff3u}) {
            m.cpu.reset(0);
            m.cpu.regs[1] = a;
            m.program({r(0x1a, 1, 0, 0)});
            m.run(1);
            equal(m.cpu.lo, (a & 0x80000000u) ? 1u : 0xffffffffu, "DIV por zero");
            equal(m.cpu.hi, a, "DIV por zero HI");
        }
        m.cpu.reset(0);
        m.cpu.regs[1] = 0x80000000u;
        m.program({r(0x1b, 1, 0, 0)});
        m.run(1);
        equal(m.cpu.lo, 0xffffffffu, "DIVU por zero");
        equal(m.cpu.hi, 0x80000000u, "DIVU por zero HI");
    });

    test("MTHI MTLO MFHI MFLO", [] {
        Machine m;
        m.cpu.regs[1] = 0x12345678u;
        m.cpu.regs[2] = 0x87654321u;
        m.program({r(0x11, 1, 0, 0), r(0x13, 2, 0, 0), r(0x10, 0, 0, 3), r(0x12, 0, 0, 4)});
        m.run(4);
        equal(m.cpu.regs[3], 0x12345678u, "HI");
        equal(m.cpu.regs[4], 0x87654321u, "LO");
    });

    test("Falha do barramento nao e ignorada", [] {
        Machine m;
        m.program({i(0x2b, 0, 0, 0x7ffc)});
        faultContains(m.cpu, "Falha no barramento");
        m.cpu.reset(0x10000);
        faultContains(m.cpu, "Falha no barramento");
    });

    test("Load $zero ainda acessa barramento", [] {
        Machine m;
        m.program({i(0x23, 0, 0, 0x7ffc)});
        faultContains(m.cpu, "Falha no barramento");
    });

    test("COP0 GTE syscall e opcode desconhecido explicitos", [] {
        Machine m;
        const std::array<std::pair<std::uint32_t, const char*>, 5> unsupported{{
            {0x40000000u, "COP0"}, {0x48000000u, "GTE"}, {0x0000000cu, "SYSCALL"},
            {0x0000000du, "BREAK"}, {0xfc000000u, "Opcode"}
        }};
        for (const auto& item : unsupported) {
            m.cpu.reset(0);
            m.program({item.first});
            faultContains(m.cpu, item.second);
        }
    });

    test("Run respeita budget inclusive loop infinito", [] {
        Machine m;
        m.program({j(2, 0), 0});
        equal(m.cpu.run(0), 0, "Budget zero");
        equal(m.cpu.run(100), 100, "Budget loop");
        equal(m.cpu.instructions, 100, "Contagem loop");
        expect(m.cpu.fault.empty(), "Loop nao e falha");
    });

    std::cout << passed << " passed; " << failed << " failed\n";
    return failed == 0 && passed != 0 ? 0 : 1;
}
