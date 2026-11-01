#include "aurora/cpu.hpp"
#include "aurora/execution.hpp"
#include "aurora/media.hpp"
#include "aurora/ps1_memory.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& body) {
    try { body(); } catch (const std::exception&) { return; }
    throw std::runtime_error("A operacao deveria rejeitar recurso nao implementado");
}
struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("aurora-system-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { std::filesystem::create_directory(path); }
    ~Temp() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}
int main() {
    unsigned passed = 0, failed = 0;
    auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("Demo PS-X EXE executa MIPS ate GP0 e produz cores esperadas", [] {
        Temp temporary;
        const auto path = temporary.path / "demo.ps-exe";
        const auto demo = aurora::makeDemoPsxExe();
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(demo.data()), demo.size());
        }
        expect(aurora::inspectMedia(path).executable, "Demo nao reconhecida como executavel");
        const auto exe = aurora::loadPsxExe(path);
        aurora::Ps1Memory memory;
        memory.load(exe);
        aurora::Cpu cpu(memory);
        cpu.reset(exe.entry);
        cpu.regs[28] = exe.gp;
        cpu.regs[29] = exe.stackBase + exe.stackOffset;
        expect(cpu.run(10000) == 10000, cpu.fault.c_str());
        const auto& vram = memory.vram();
        auto color = [](std::uint32_t rgb) {
            return ((rgb >> 3) & 31u) | (((rgb >> 11) & 31u) << 5) | (((rgb >> 19) & 31u) << 10);
        };
        expect(vram[0] == color(0x201610), "Cor de fundo incorreta");
        expect(vram[32 * 1024 + 32] == color(0xcba340), "Primeiro retangulo incorreto");
        expect(vram[100 * 1024 + 40] == color(0x40cb8a), "Retangulo esquerdo incorreto");
        expect(vram[100 * 1024 + 180] == color(0xcb608a), "Retangulo direito incorreto");
        expect(vram[240 * 1024] == 0, "Demo escreveu fora da area pretendida");
        const auto framebuffer = memory.framebuffer();
        expect(framebuffer.size() == 320 * 240, "Dimensao de framebuffer incorreta");
        expect(framebuffer[32 * 320 + 32] == 0xffcea542u, "Expansao RGB555 para RGBA incorreta");
    });
    test("RAM little endian, aliases e espelhamento", [] {
        aurora::Ps1Memory memory;
        memory.write32(0x80010000, 0x12345678);
        expect(memory.read32(0xa0010000) == 0x12345678, "Alias KSEG1 incorreto");
        expect(memory.read32(0x00210000) == 0x12345678, "Espelho RAM incorreto");
        expect(memory.read8(0x00010000) == 0x78, "Endian incorreto");
        memory.write16(0x1f800002, 0xabcd);
        expect(memory.read16(0x9f800002) == 0xabcd, "Scratchpad incorreto");
    });
    test("GPU alinha X e largura, converte RGB e envolve VRAM", [] {
        aurora::Ps1Memory memory;
        memory.write32(0x1f801810, 0x020000ff);
        memory.write32(0x1f801810, (511u << 16) | 1023u);
        memory.write32(0x1f801810, (2u << 16) | 17u);
        expect(memory.vram()[511 * 1024 + 1008] == 31, "Alinhamento X incorreto");
        expect(memory.vram()[511 * 1024 + 15] == 31, "Wrap horizontal incorreto");
        expect(memory.vram()[1008] == 31, "Wrap vertical incorreto");
        expect(memory.vram()[16] == 0, "Largura arredondada incorreta");
    });
    test("Recursos ausentes geram falha explicita", [] {
        aurora::Ps1Memory memory;
        rejects([&] { memory.read32(0xbfc00000); });
        expect((memory.read32(0x1f801814) & (1u << 26)) != 0,
               "GPU sincrona deve aceitar comandos quando esta pronta");
        rejects([&] { memory.write32(0x1f801810, 0x20000000); });
        rejects([&] { memory.framebuffer(1025, 240); });
        rejects([&] { memory.read8(0xfffe0130); });
        rejects([&] { memory.read32(0x20000000); });
        rejects([&] { memory.write32(0x40000000, 42); });
    });
    test("BIOS ausente interrompe antes de executar RAM zerada", [] {
        aurora::Ps1Memory memory;
        aurora::Cpu cpu(memory);
        cpu.reset(0x800000a0);
        cpu.regs[9] = 0x3c;
        aurora::ExecutionTrace trace;
        expect(aurora::runPs1(cpu, memory, 1000, trace) == 0, "BIOS ausente executou instrucoes");
        expect(cpu.fault.find("BIOS/HLE") != std::string::npos, "Motivo BIOS nao identificado");
        expect(cpu.fault.find("3c") != std::string::npos, "Funcao BIOS nao reportada");
        expect(cpu.instructions == 0, "Contagem indica falsa execucao BIOS");
    });
    test("Trace limitado captura opcode e recurso ausente", [] {
        aurora::Ps1Memory memory;
        memory.write32(0x80010000, 0x08004000);
        memory.write32(0x80010004, 0);
        aurora::Cpu cpu(memory);
        cpu.reset(0x80010000);
        aurora::ExecutionTrace trace;
        expect(aurora::runPs1(cpu, memory, 100, trace) == 100, "Loop nao executou");
        expect(trace.count == 32, "Trace nao foi limitado a 32 entradas");
        const auto report = aurora::executionReport(cpu, trace);
        expect(report.find("0x80010000 0x08004000") != std::string::npos, "Trace sem PC/opcode esperado");
        expect(report.find("compatibilidade nao comprovada") != std::string::npos,
               "Orcamento nao pode afirmar compatibilidade");
    });
    std::cout << passed << " testes de integracao passaram; " << failed << " falharam\n";
    return failed ? 1 : 0;
}
