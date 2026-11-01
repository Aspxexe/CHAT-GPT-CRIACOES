#include "aurora/cpu.hpp"
#include "aurora/execution.hpp"
#include "aurora/homebrew.hpp"
#include "aurora/media.hpp"
#include "aurora/ps1_memory.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint16_t up = 0x0010, right = 0x0020, down = 0x0040, left = 0x0080;
constexpr std::uint16_t start = 0x0008;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Machine {
    aurora::Ps1Memory memory;
    aurora::Cpu cpu{memory};
    aurora::ExecutionTrace trace;

    Machine() {
        const auto program = aurora::parsePsxExe(aurora::makeHomebrewPsxExe());
        memory.load(program);
        cpu.reset(program.entry);
        cpu.regs[28] = program.gp;
        if (program.stackBase)
            cpu.regs[29] = cpu.regs[30] = program.stackBase + program.stackOffset;
        pump();
    }

    void pump() {
        const auto executed = aurora::runPs1(cpu, memory, 50000, trace);
        expect(cpu.fault.empty(), cpu.fault);
        expect(executed == 50000, "A CPU encerrou antes do orcamento sem motivo de falha");
    }

    void tick(std::uint16_t pressed = 0) {
        memory.setPadButtons(pressed);
        memory.beginFrame();
        pump();
    }

    void frames(unsigned count, std::uint16_t pressed = 0) {
        for (unsigned n = 0; n < count; ++n) tick(pressed);
    }

    std::uint32_t state(std::uint32_t offset) {
        return memory.read32(aurora::HomebrewStateAddress + offset);
    }

    std::uint16_t pixel(unsigned x, unsigned y) const {
        return memory.vram().at(y * 1024 + x);
    }
};
}

int main() {
    unsigned passed = 0, failed = 0;
    auto test = [&](const char* name, const std::function<void()>& body) {
        try {
            body();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    test("PS-X EXE inicializa estado e aguarda VBlank", [] {
        Machine m;
        expect(m.state(aurora::HomebrewPlayerX) == aurora::HomebrewInitialPlayerX, "X inicial incorreto");
        expect(m.state(aurora::HomebrewPlayerY) == aurora::HomebrewInitialPlayerY, "Y inicial incorreto");
        expect(m.state(aurora::HomebrewEnemyX) == aurora::HomebrewInitialEnemyX, "Inimigo moveu sem quadro");
        expect(m.state(aurora::HomebrewScore) == 0, "Pontuacao inicial incorreta");
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewPlaying, "Fase inicial incorreta");
        expect(m.state(aurora::HomebrewFrameCounter) == 0, "Loop processou quadro antes do VBlank");
        m.tick();
        expect(m.state(aurora::HomebrewFrameCounter) == 1, "VBlank nao processou exatamente um quadro");
        expect(m.state(aurora::HomebrewEnemyX) == aurora::HomebrewInitialEnemyX + 2,
               "Inimigo nao avancou um passo pela CPU");
    });

    test("Quadro da CPU desenha jogador, cristal e inimigo na VRAM", [] {
        Machine m;
        m.tick();
        const auto background = m.pixel(24, 72);
        const auto x = m.state(aurora::HomebrewPlayerX);
        const auto y = m.state(aurora::HomebrewPlayerY);
        const auto enemy = m.state(aurora::HomebrewEnemyX);
        expect(m.pixel(x + 8, y + 8) != background, "Jogador nao apareceu no framebuffer");
        expect(m.pixel(aurora::HomebrewCrystals[0].x + 8, aurora::HomebrewCrystals[0].y + 8) != background,
               "Primeiro cristal nao apareceu");
        expect(m.pixel(enemy + 8, aurora::HomebrewEnemyY + 8) != background, "Inimigo nao apareceu");
        const auto before = m.memory.framebuffer();
        m.tick(right);
        expect(before != m.memory.framebuffer(), "O quadro nao mudou apos entrada real pelo controle");
    });

    test("D-pad move dois pixels, soltar para e VBlank limita atualizacoes", [] {
        Machine m;
        m.tick(right);
        expect(m.state(aurora::HomebrewPlayerX) == 34, "RIGHT nao percorreu o protocolo do pad");
        expect(m.state(aurora::HomebrewFrameCounter) == 1, "Um VBlank gerou varios movimentos");
        m.pump();
        expect(m.state(aurora::HomebrewPlayerX) == 34 && m.state(aurora::HomebrewFrameCounter) == 1,
               "CPU extra sem novo VBlank moveu o jogador");
        m.tick();
        expect(m.state(aurora::HomebrewPlayerX) == 34, "Soltar RIGHT nao parou o jogador");
        m.tick(left);
        expect(m.state(aurora::HomebrewPlayerX) == 32, "LEFT incorreto");
        m.tick(up);
        expect(m.state(aurora::HomebrewPlayerY) == 62, "UP incorreto");
        m.tick(down);
        expect(m.state(aurora::HomebrewPlayerY) == 64, "DOWN incorreto");
    });

    test("Segurar direcoes respeita os quatro limites da arena", [] {
        Machine m;
        m.frames(13, left);
        expect(m.state(aurora::HomebrewPlayerX) == 16, "Limite esquerdo incorreto");
        m.frames(141, right);
        expect(m.state(aurora::HomebrewPlayerX) == 288, "Limite direito incorreto");
        m.frames(17, up);
        expect(m.state(aurora::HomebrewPlayerY) == 40, "Limite superior incorreto");
        m.frames(89, down);
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewPlaying, "Rota de limites encontrou perigo inesperado");
        expect(m.state(aurora::HomebrewPlayerY) == 208, "Limite inferior incorreto");
    });

    test("Derrota ocorre por movimento e colisao real sem editar a RAM", [] {
        Machine m;
        m.frames(24, down);
        expect(m.state(aurora::HomebrewPlayerX) == 32 && m.state(aurora::HomebrewPlayerY) == 112,
               "Rota de colisao nao posicionou o jogador");
        for (unsigned frame = 0; frame < 300 && m.state(aurora::HomebrewPhase) == aurora::HomebrewPlaying; ++frame)
            m.tick();
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewLost, "Inimigo atravessou o jogador sem derrota");
        const auto playerX = m.state(aurora::HomebrewPlayerX);
        const auto enemyX = m.state(aurora::HomebrewEnemyX);
        expect(playerX < enemyX + 16 && playerX + 16 > enemyX, "Derrota ocorreu sem sobreposicao horizontal");
        m.frames(4, right);
        expect(m.state(aurora::HomebrewPlayerX) == playerX && m.state(aurora::HomebrewEnemyX) == enemyX,
               "Cena de derrota continuou movimentando objetos");
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewLost, "Movimento descartou a derrota");
        m.tick(start);
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewPlaying, "START nao reiniciou apos derrota");
        expect(m.state(aurora::HomebrewPlayerX) == 32 && m.state(aurora::HomebrewPlayerY) == 64,
               "Reinicio nao restaurou a posicao inicial");
        expect(m.state(aurora::HomebrewScore) == 0, "Reinicio nao zerou pontuacao");
    });

    test("Percurso completo com entradas do controle coleta cinco cristais e vence", [] {
        Machine m;
        m.tick();
        m.frames(104, right);
        expect(m.state(aurora::HomebrewScore) == 2, "Dois cristais superiores nao foram coletados");
        m.frames(56, down);
        expect(m.state(aurora::HomebrewScore) == 3, "Cristal inferior direito nao foi coletado");
        m.frames(88, left);
        expect(m.state(aurora::HomebrewScore) == 4, "Cristal inferior esquerdo nao foi coletado");
        m.frames(64, up);
        m.frames(44, right);
        expect(m.state(aurora::HomebrewScore) == 5, "Rota completa nao coletou cinco cristais");
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewWon, "Cinco cristais nao acionaram vitoria");
        const auto playerX = m.state(aurora::HomebrewPlayerX);
        const auto playerY = m.state(aurora::HomebrewPlayerY);
        m.frames(4, left | down);
        expect(m.state(aurora::HomebrewPlayerX) == playerX && m.state(aurora::HomebrewPlayerY) == playerY,
               "Movimento continuou apos vitoria");
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewWon && m.state(aurora::HomebrewScore) == 5,
               "Movimento descartou a vitoria ou a pontuacao final");
        m.tick(start);
        expect(m.state(aurora::HomebrewPhase) == aurora::HomebrewPlaying && m.state(aurora::HomebrewScore) == 0,
               "START nao reiniciou apos vitoria");
    });

    test("START tem deteccao de borda e nao reinicia todo quadro ao segurar", [] {
        Machine m;
        m.frames(4, right);
        expect(m.state(aurora::HomebrewPlayerX) == 40, "Preparacao do teste nao moveu jogador");
        m.tick(start);
        expect(m.state(aurora::HomebrewPlayerX) == 32 && m.state(aurora::HomebrewFrameCounter) == 1,
               "Primeira borda START nao reiniciou");
        m.tick(start | right);
        expect(m.state(aurora::HomebrewPlayerX) == 34 && m.state(aurora::HomebrewFrameCounter) == 2,
               "START segurado reiniciou novamente");
        m.tick(start | right);
        expect(m.state(aurora::HomebrewPlayerX) == 36, "START segurado impediu movimento");
        m.tick();
        m.tick(start);
        expect(m.state(aurora::HomebrewPlayerX) == 32 && m.state(aurora::HomebrewFrameCounter) == 1,
               "Soltar START nao rearmou a proxima borda");
    });

    std::cout << passed << " testes de jogabilidade passaram; " << failed << " falharam\n";
    return failed ? 1 : 0;
}
