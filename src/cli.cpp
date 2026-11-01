#include "aurora/cpu.hpp"
#include "aurora/execution.hpp"
#include "aurora/homebrew.hpp"
#include "aurora/media.hpp"
#include "aurora/ps1_memory.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int run(const std::vector<std::string>& arguments) {
    const int argc = int(arguments.size());
    const auto& argv = arguments;
    try {
        if (argc == 3 && (argv[1] == "--make-demo" || argv[1] == "--make-homebrew")) {
            const bool game = argv[1] == "--make-homebrew";
            const auto data = game ? aurora::makeHomebrewPsxExe() : aurora::makeDemoPsxExe();
            std::ofstream output(std::filesystem::u8path(argv[2]), std::ios::binary);
            if (!output.write(reinterpret_cast<const char*>(data.data()), data.size()))
                throw std::runtime_error("Nao foi possivel salvar o PS-X EXE");
            std::cout << (game ? "Aurora Coleta PS-X EXE salvo. Jogo original sem BIOS.\n" :
                                  "Demo PS-X EXE propria salva. Nenhuma BIOS ou jogo comercial incluido.\n");
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--inspect") {
            const auto info = aurora::inspectMedia(std::filesystem::u8path(argv[2]));
            std::cout << info.kind << ": " << info.description << '\n'
                      << "Loader experimental disponivel: " << (info.executable ? "sim" : "nao") << '\n';
            return 0;
        }
        if (argc == 3 && argv[1] == "--diagnose") {
            std::cout << aurora::diagnoseMedia(std::filesystem::u8path(argv[2]));
            return 0;
        }
        if ((argc == 3 || (argc == 5 && argv[3] == "--report")) && argv[1] == "--probe") {
            const auto path = std::filesystem::u8path(argv[2]);
            std::string report = aurora::diagnoseMedia(path);
            bool passed = false;
            try {
                const auto program = aurora::loadProgram(path);
                aurora::Ps1Memory memory;
                memory.load(program.executable);
                aurora::Cpu cpu(memory);
                cpu.reset(program.executable.entry);
                cpu.regs[28] = program.executable.gp;
                if (program.executable.stackBase)
                    cpu.regs[29] = cpu.regs[30] = program.executable.stackBase + program.executable.stackOffset;
                aurora::ExecutionTrace trace;
                memory.beginFrame();
                aurora::runPs1(cpu, memory, 250000, trace);
                report += "\nOrigem: " + program.origin + aurora::executionReport(cpu, trace);
                passed = cpu.fault.empty();
            } catch (const std::exception& error) {
                report += "\nFalha no carregamento: " + std::string(error.what()) + '\n';
            }
            if (argc == 5) {
                std::ofstream output(std::filesystem::u8path(argv[4]), std::ios::binary);
                if (!(output << report)) throw std::runtime_error("Falha ao salvar diagnostico");
            }
            std::cout << report;
            return passed ? 0 : 1;
        }
        if (argc == 5 && std::string(argv[1]) == "--run" &&
            std::string(argv[3]) == "--ppm") {
            const auto program = aurora::loadProgram(std::filesystem::u8path(argv[2]));
            const auto& exe = program.executable;
            aurora::Ps1Memory memory;
            memory.load(exe);
            aurora::Cpu cpu(memory);
            cpu.reset(exe.entry);
            cpu.regs[28] = exe.gp;
            if (exe.stackBase) cpu.regs[29] = cpu.regs[30] = exe.stackBase + exe.stackOffset;
            aurora::ExecutionTrace trace;
            memory.beginFrame();
            aurora::runPs1(cpu, memory, 50000, trace);
            if (!cpu.fault.empty()) throw std::runtime_error(aurora::executionReport(cpu, trace));
            const auto pixels = memory.framebuffer();
            std::ofstream output(std::filesystem::u8path(argv[4]), std::ios::binary);
            output << "P6\n320 240\n255\n";
            for (const auto pixel : pixels) {
                const char rgb[] = {char(pixel), char(pixel >> 8), char(pixel >> 16)};
                output.write(rgb, 3);
            }
            if (!output) throw std::runtime_error("Falha ao salvar framebuffer");
            std::cout << "Executadas " << cpu.instructions
                      << " instrucoes MIPS; framebuffer salvo. Isso nao valida jogos comerciais.\n";
            return 0;
        }
        std::cout << "Aurora: nucleo PS1 experimental proprio\n"
                     "  aurora-cli --make-demo demo.ps-exe\n"
                     "  aurora-cli --make-homebrew aurora-coleta.ps-exe\n"
                     "  aurora-cli --inspect ARQUIVO\n"
                     "  aurora-cli --diagnose ARQUIVO\n"
                     "  aurora-cli --probe ARQUIVO --report diagnostico.txt\n"
                     "  aurora-cli --run ARQUIVO --ppm frame.ppm\n";
        return argc == 1 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Erro: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) arguments.push_back(std::filesystem::path(argv[i]).u8string());
    return run(arguments);
}
#else
int main(int argc, char** argv) {
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
