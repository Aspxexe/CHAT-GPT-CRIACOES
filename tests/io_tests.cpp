#include "aurora/ps1_memory.hpp"
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint32_t data = 0x1f801040, stat = 0x1f801044, control = 0x1f80104a;
constexpr std::uint32_t gp0 = 0x1f801810, gp1 = 0x1f801814;
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& body) {
    try { body(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Recurso ausente foi aceito silenciosamente");
}
std::uint8_t exchange(aurora::Ps1Memory& memory, std::uint8_t byte) {
    memory.write8(data, byte);
    expect((memory.read32(stat) & 2u) != 0, "RX deveria conter resposta");
    const auto response = memory.read8(data);
    expect((memory.read16(stat) & 2u) == 0, "RX deveria esvaziar apos leitura");
    return response;
}
std::array<std::uint8_t, 5> poll(aurora::Ps1Memory& memory) {
    memory.write16(control, 0);
    memory.write16(control, 3);
    return {exchange(memory, 1), exchange(memory, 0x42), exchange(memory, 0),
            exchange(memory, 0), exchange(memory, 0)};
}
void area(aurora::Ps1Memory& memory, unsigned left = 0, unsigned top = 0,
          unsigned right = 319, unsigned bottom = 239) {
    memory.write32(gp0, 0xe3000000u | (top << 10) | left);
    memory.write32(gp0, 0xe4000000u | (bottom << 10) | right);
}
void rectangle(aurora::Ps1Memory& memory, std::uint32_t rgb, int x, int y,
               unsigned width, unsigned height) {
    memory.write32(gp0, 0x60000000u | rgb);
    memory.write32(gp0, std::uint32_t(std::uint16_t(x)) | (std::uint32_t(std::uint16_t(y)) << 16));
    memory.write32(gp0, width | (height << 16));
}
}

int main() {
    unsigned passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    test("Digital pad responde FF 41 5A e botoes ativos baixos", [] {
        aurora::Ps1Memory memory;
        expect(memory.read32(stat) == 5, "Status TX ocioso incorreto");
        expect(poll(memory) == std::array<std::uint8_t, 5>{0xff, 0x41, 0x5a, 0xff, 0xff},
               "Identificador digital ou estado solto incorreto");
        memory.setPadButtons(0x4018);
        expect(poll(memory) == std::array<std::uint8_t, 5>{0xff, 0x41, 0x5a, 0xe7, 0xbf},
               "Dpad Start ou Cross nao chegou no protocolo");
        memory.setPadButtons(0);
        expect(poll(memory)[3] == 0xff && poll(memory)[4] == 0xff, "Soltar botoes nao restaurou bits");
    });
    test("Botoes permanecem coerentes durante uma transacao", [] {
        aurora::Ps1Memory memory;
        memory.setPadButtons(0x0010);
        memory.write16(control, 3);
        expect(exchange(memory, 1) == 0xff, "Selecao inicial incorreta");
        memory.setPadButtons(0x4000);
        expect(exchange(memory, 0x42) == 0x41 && exchange(memory, 0) == 0x5a, "Cabecalho incorreto");
        expect(exchange(memory, 0) == 0xef && exchange(memory, 0) == 0xff,
               "Estado mudou no meio da leitura dos dois bytes");
        const auto next = poll(memory);
        expect(next[3] == 0xff && next[4] == 0xbf, "Proxima transacao nao capturou novo estado");
    });
    test("Deselecao e reset abortam protocolo sem pad fantasma na porta2", [] {
        aurora::Ps1Memory memory;
        memory.write16(control, 3);
        exchange(memory, 1);
        exchange(memory, 0x42);
        memory.write16(control, 0);
        memory.write16(control, 3);
        expect(exchange(memory, 1) == 0xff && exchange(memory, 0x42) == 0x41,
               "Deselecao nao reiniciou protocolo");
        memory.write8(data, 0);
        memory.write16(control, 0x40);
        expect((memory.read32(stat) & 2u) == 0, "Reset nao limpou FIFO");
        expect(memory.read16(control) == 0, "Bit reset nao foi auto-limpo");
        memory.write16(control, 0x2003);
        memory.write8(data, 1);
        expect((memory.read32(stat) & 2u) == 0 && memory.read8(data) == 0xff,
               "Porta2 respondeu como controle conectado");
        memory.write16(control, 3);
        expect(exchange(memory, 1) == 0xff && exchange(memory, 0x42) == 0x41,
               "Retorno a porta1 nao reiniciou protocolo");
    });
    test("TX desabilitado nao produz resposta e comando ausente nao vira controle", [] {
        aurora::Ps1Memory memory;
        memory.write16(control, 2);
        memory.write8(data, 1);
        expect(memory.read32(stat) == 5, "TX desabilitado criou resposta");
        memory.write16(control, 3);
        expect(exchange(memory, 1) == 0xff, "Inicio invalido");
        expect(exchange(memory, 0x43) == 0xff, "Comando de configuracao nao implementado foi atendido");
        expect(exchange(memory, 0x42) == 0xff, "Protocolo deveria ter sido abortado");
    });
    test("FIFO RX e limitada e leituras largas consomem so um byte", [] {
        aurora::Ps1Memory memory;
        memory.write16(control, 3);
        for (unsigned n = 0; n < 100; ++n) memory.write8(data, 0);
        for (unsigned n = 0; n < 8; ++n) {
            expect((memory.read32(stat) & 2u) != 0, "FIFO perdeu byte antes do limite");
            expect(memory.read32(data) == 0xff, "Leitura larga de dado incorreta");
        }
        expect((memory.read16(stat) & 2u) == 0, "FIFO excedeu oito bytes");
        memory.write32(data, 1);
        memory.write16(data, 0x42);
        expect(memory.read16(data) == 0xff && memory.read32(data) == 0x41,
               "Transferencias de 16/32 bits consumiram quantidade errada de bytes");
    });
    test("Registradores JOY preservam byte lanes e aliases", [] {
        aurora::Ps1Memory memory;
        memory.write16(0x1f801048, 0x000d);
        memory.write16(0x1f80104e, 0x0088);
        memory.write8(0x1f801049, 0x12);
        memory.write8(0x1f80104f, 0x34);
        memory.write16(control, 3);
        expect(memory.read32(0x9f801048) == 0x0003120d, "MODE/CTRL largo ou alias incorreto");
        expect(memory.read32(0x1f80104c) == 0x34880000, "BAUD ou byte lane incorreto");
        memory.write32(0x1f801048, 0x0003000d);
        memory.write32(0x1f80104c, 0x00880000);
        expect(memory.read16(0x1f801048) == 0x0d && memory.read16(control) == 3 &&
               memory.read16(0x1f80104e) == 0x88, "Escrita larga registradores incorreta");
    });
    test("VBlank fica pendente ate acknowledge AND sem alterar RAM", [] {
        aurora::Ps1Memory memory;
        memory.write32(0x80020000, 0x12345678);
        memory.beginFrame();
        memory.beginFrame();
        expect(memory.read32(0x1f801070) == 1, "VBlank nao gerou pedido latente");
        memory.write16(0x1f801070, 0xffff);
        expect(memory.read32(0x1f801070) == 1, "Escrever1 apagou IRQ incorretamente");
        memory.write32(0x1f801070, 0xfffffffe);
        expect(memory.read16(0x1f801070) == 0, "Acknowledge nao apagou bit VBlank");
        memory.write32(0x1f801074, 0xffffffff);
        expect(memory.read32(0x1f801074) == 0x7ff, "I_MASK aceitou bits inexistentes");
        memory.write8(0x1f801075, 0);
        expect(memory.read16(0x1f801074) == 0xff, "Byte lane IRQ mask incorreto");
        memory.beginFrame();
        memory.write8(0x1f801070, 0);
        expect(memory.read32(0x1f801070) == 0, "Acknowledge byte incorreto");
        expect(memory.read32(0x80020000) == 0x12345678, "Host alterou estado do jogo na RAM");
    });
    test("Retangulo GP0 60 permite movimento de um pixel", [] {
        aurora::Ps1Memory memory;
        area(memory);
        rectangle(memory, 0x0000ff, 5, 7, 3, 2);
        expect(memory.vram()[7 * 1024 + 5] == 31, "Origem foi alinhada em16 como Fill Rectangle");
        expect(memory.vram()[8 * 1024 + 7] == 31, "Largura/altura exatas incorretas");
        expect(memory.vram()[7 * 1024 + 4] == 0 && memory.vram()[9 * 1024 + 5] == 0,
               "Retangulo escreveu fora dos limites");
    });
    test("Area inclusiva e offset assinado recortam primitivas", [] {
        aurora::Ps1Memory memory;
        area(memory, 10, 10, 12, 12);
        memory.write32(gp0, 0xe5000000u | 0x7feu | (2u << 11));
        rectangle(memory, 0x00ff00, 10, 6, 8, 8);
        expect(memory.vram()[10 * 1024 + 10] == (31u << 5), "Offset negativo ou clip incorreto");
        expect(memory.vram()[12 * 1024 + 12] == (31u << 5), "Extremo inclusivo nao foi desenhado");
        expect(memory.vram()[10 * 1024 + 9] == 0 && memory.vram()[13 * 1024 + 10] == 0,
               "Primitiva ignorou area de desenho");
        rectangle(memory, 0xff0000, -32768, -32768, 1, 1);
        expect(memory.vram()[0] == 0, "Coordenadas negativas causaram wrap");
    });
    test("Mascara de pixel protege e forca bit15 somente em primitivas", [] {
        aurora::Ps1Memory memory;
        area(memory);
        memory.write32(gp0, 0xe6000001);
        rectangle(memory, 0x0000ff, 1, 1, 1, 1);
        expect(memory.vram()[1025] == 0x801f, "Set mask nao foi aplicado");
        memory.write32(gp0, 0xe6000002);
        rectangle(memory, 0x00ff00, 1, 1, 2, 1);
        expect(memory.vram()[1025] == 0x801f && memory.vram()[1026] == (31u << 5),
               "Check mask nao preservou apenas pixel protegido");
        memory.write32(gp0, 0x02ff0000);
        memory.write32(gp0, 0);
        memory.write32(gp0, (2u << 16) | 16u);
        expect(memory.vram()[1025] == (31u << 10), "Fill Rectangle deveria ignorar mascara de primitivas");
    });
    test("Retangulo enorme permanece limitado a VRAM e area", [] {
        aurora::Ps1Memory memory;
        area(memory, 0, 0, 1023, 511);
        rectangle(memory, 0xffffff, -1, -1, 65535, 65535);
        expect(memory.vram().front() == 0x7fff && memory.vram().back() == 0x7fff,
               "Clipping de tamanho extremo incorreto");
    });
    test("GP1 reset cancela pacote parcial e preserva VRAM", [] {
        aurora::Ps1Memory memory;
        area(memory);
        rectangle(memory, 0xff, 12, 12, 2, 2);
        memory.write32(gp0, 0x600000ff);
        memory.write32(gp1, 0);
        expect(memory.vram()[12 * 1024 + 12] == 31, "Reset GPU apagou VRAM");
        rectangle(memory, 0xff00, 12, 12, 2, 2);
        expect(memory.vram()[12 * 1024 + 12] == 31, "Reset nao restaurou area inicial");
        area(memory);
        rectangle(memory, 0xff00, 12, 12, 2, 2);
        expect(memory.vram()[12 * 1024 + 12] == (31u << 5), "Pacote parcial contaminou desenho apos reset");
    });
    test("Inicio de display seleciona janela de VRAM", [] {
        aurora::Ps1Memory memory;
        area(memory, 0, 0, 1023, 511);
        rectangle(memory, 0x0000ff, 100, 200, 1, 1);
        memory.write32(gp1, 0x05000000u | (200u << 10) | 100u);
        memory.write32(gp1, 0x06000000u | 0x260u | (0xc60u << 12));
        memory.write32(gp1, 0x07000000u | 0x10u | (0x100u << 10));
        memory.write32(gp1, 0x08000001);
        memory.write32(gp1, 0x03000000);
        expect(memory.framebuffer(1, 1)[0] == 0xff0000ffu, "Inicio de display nao selecionou origem");
    });
    test("GPUSTAT espelha reset, display, mascara e prontidao sincrona", [] {
        aurora::Ps1Memory memory;
        expect(memory.read32(gp1) == 0x14802000u, "Valor reset GPUSTAT incorreto");
        expect(memory.read16(gp1) == 0x2000 && memory.read16(gp1 + 2) == 0x1480,
               "Leituras halfword GPUSTAT inconsistentes");
        expect(memory.read8(gp1 + 1) == 0x20 && memory.read8(gp1 + 3) == 0x14,
               "Leituras byte GPUSTAT inconsistentes");
        memory.write32(gp1, 0x03000000);
        expect(memory.read32(gp1) == 0x14002000u, "Enable display nao limpou bit23");
        memory.write32(gp0, 0xe6000003);
        expect((memory.read32(gp1) & 0x1800u) == 0x1800u, "GPUSTAT nao espelha E6");
        memory.write32(gp1, 0x080000ff);
        expect((memory.read32(gp1) & 0x007f4000u) == 0x007f4000u,
               "GPUSTAT nao espelha modo display GP1");
        area(memory);
        rectangle(memory, 0xff, 1, 1, 1, 1);
        expect((memory.read32(gp1) & 0x14000000u) == 0x14000000u,
               "GPU sincrona nao esta pronta apos concluir comando");
        memory.write32(gp1, 0);
        expect(memory.read32(gp1) == 0x14802000u, "Reset nao restaurou GPUSTAT");
    });
    test("Comandos e perifericos ausentes continuam falhando", [] {
        aurora::Ps1Memory memory;
        rejects([&] { memory.write32(gp0, 0x62000000); });
        rejects([&] { memory.write32(gp1, 0x04000000); });
        rejects([&] { memory.read32(gp0); });
        rejects([&] { memory.read32(0x1f801100); });
    });
    std::cout << passed << " testes IO passaram; " << failed << " falharam\n";
    return failed ? 1 : 0;
}
