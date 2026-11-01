#include "aurora/execution.hpp"
#include "aurora/disc.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace aurora {
LoadedProgram loadProgram(const std::filesystem::path& path) {
    const auto info = inspectMedia(path);
    if (info.kind == "psx-exe") return {loadPsxExe(path), "PS-X EXE direto"};
    const auto boot = loadPs1Disc(path);
    return {boot.executable, boot.disc.description + "\nBoot: " + boot.disc.bootPath};
}

std::size_t runPs1(Cpu& cpu, Bus& bus, std::size_t budget, ExecutionTrace& trace) {
    std::size_t executed = 0;
    while (executed < budget && cpu.fault.empty()) {
        const auto address = cpu.pc & 0x1fffffffu;
        const auto segment = cpu.pc >> 29;
        if ((segment == 0 || segment == 4 || segment == 5) &&
            (address == 0xa0 || address == 0xb0 || address == 0xc0)) {
            std::ostringstream error;
            error << "Chamada BIOS/HLE PS1 ainda nao implementada (PC=0x" << std::hex << cpu.pc
                  << ", vetor=0x" << address << ", funcao t1=0x" << cpu.regs[9] << ").";
            cpu.fault = error.str();
            break;
        }
        const auto index = trace.cursor;
        trace.pcs[index] = cpu.pc;
        try { trace.instructions[index] = bus.read32(cpu.pc); }
        catch (const std::exception&) { trace.instructions[index] = 0; }
        trace.cursor = (trace.cursor + 1) % trace.pcs.size();
        trace.count = std::min(trace.count + 1, trace.pcs.size());
        if (!cpu.step()) break;
        ++executed;
    }
    return executed;
}

std::string executionReport(const Cpu& cpu, const ExecutionTrace& trace) {
    std::ostringstream output;
    output << "\nEstado da execucao\nInstrucoes concluidas: " << cpu.instructions
           << "\nResultado: " << (cpu.fault.empty() ? "Orcamento executado; compatibilidade nao comprovada" : cpu.fault)
           << '\n' << std::hex << std::setfill('0') << "PC: 0x" << std::setw(8) << cpu.pc
           << " HI: 0x" << std::setw(8) << cpu.hi << " LO: 0x" << std::setw(8) << cpu.lo << '\n';
    for (unsigned reg = 0; reg < cpu.regs.size(); ++reg) {
        output << 'r' << std::dec << reg << "=0x" << std::hex << std::setw(8) << cpu.regs[reg]
               << (reg % 4 == 3 ? '\n' : ' ');
    }
    output << "Ultimas instrucoes tentadas (PC/opcode, sem dados do disco):\n";
    const auto first = (trace.cursor + trace.pcs.size() - trace.count) % trace.pcs.size();
    for (std::size_t n = 0; n < trace.count; ++n) {
        const auto index = (first + n) % trace.pcs.size();
        output << "0x" << std::setw(8) << trace.pcs[index] << " 0x"
               << std::setw(8) << trace.instructions[index] << '\n';
    }
    return output.str();
}
}
