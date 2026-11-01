#include "aurora/disc.hpp"
#include "aurora/cpu.hpp"
#include "aurora/ps1_memory.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t block = 2048;
constexpr std::uint32_t volumeBlocks = 32;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& body) {
    try { body(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Arquivo invalido deveria ter sido rejeitado");
}
void rejectsPhysicalExtent(const std::function<void()>& body, std::uint64_t declared,
                           std::uint64_t available) {
    try {
        body();
    } catch (const std::exception& error) {
        const std::string message = error.what();
        expect(message.find(std::to_string(declared)) != std::string::npos &&
               message.find(std::to_string(available)) != std::string::npos,
               "Falha fisica deve informar setores declarados e presentes: " + message);
        expect(message.find("fisic") != std::string::npos ||
               message.find("presentes") != std::string::npos ||
               message.find("disponive") != std::string::npos,
               "Falha deve distinguir limite fisico do limite declarado: " + message);
        return;
    }
    throw std::runtime_error("Extent fisicamente ausente deveria ter sido rejeitado");
}
void le16(Bytes& bytes, std::size_t p, std::uint16_t value) {
    bytes.at(p) = std::uint8_t(value);
    bytes.at(p + 1) = std::uint8_t(value >> 8);
}
void le32(Bytes& bytes, std::size_t p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(p + i) = std::uint8_t(value >> (i * 8));
}
void be16(Bytes& bytes, std::size_t p, std::uint16_t value) {
    bytes.at(p) = std::uint8_t(value >> 8);
    bytes.at(p + 1) = std::uint8_t(value);
}
void be32(Bytes& bytes, std::size_t p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(p + i) = std::uint8_t(value >> ((3 - i) * 8));
}
void both16(Bytes& bytes, std::size_t p, std::uint16_t value) {
    le16(bytes, p, value); be16(bytes, p + 2, value);
}
void both32(Bytes& bytes, std::size_t p, std::uint32_t value) {
    le32(bytes, p, value); be32(bytes, p + 4, value);
}
Bytes record(const std::string& name, std::uint32_t extent, std::uint32_t size, bool directory = false) {
    Bytes result(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0));
    result[0] = std::uint8_t(result.size());
    both32(result, 2, extent);
    both32(result, 10, size);
    result[18] = 126; result[19] = 11; result[20] = 2;
    result[25] = directory ? 2 : 0;
    both16(result, 28, 1);
    result[32] = std::uint8_t(name.size());
    std::copy(name.begin(), name.end(), result.begin() + 33);
    return result;
}
void copy(Bytes& target, std::size_t p, const Bytes& source) {
    expect(p <= target.size() && source.size() <= target.size() - p, "Fixture ultrapassa imagem");
    std::copy(source.begin(), source.end(), target.begin() + std::ptrdiff_t(p));
}
void copyText(Bytes& target, std::size_t p, const std::string& text) {
    copy(target, p, Bytes(text.begin(), text.end()));
}
struct Fixture {
    Bytes image = Bytes(volumeBlocks * block);
    std::size_t configRecord = 21 * block;
    std::size_t exeRecord = 0;
    explicit Fixture(std::string config = "BOOT = cdrom:\\games\\demo.psx;1\r\n", bool nested = true,
                     bool includeConfig = true, bool fallback = false) {
        copyText(image, 16 * block, std::string("\1CD001\1", 7));
        std::fill(image.begin() + 16 * block + 8, image.begin() + 16 * block + 72, ' ');
        copyText(image, 16 * block + 8, "PLAYSTATION");
        copyText(image, 16 * block + 40, "AURORA_TEST");
        both32(image, 16 * block + 80, volumeBlocks);
        both16(image, 16 * block + 120, 1);
        both16(image, 16 * block + 124, 1);
        both16(image, 16 * block + 128, block);
        both32(image, 16 * block + 132, nested ? 24 : 10);
        le32(image, 16 * block + 140, 18);
        be32(image, 16 * block + 148, 19);
        copy(image, 16 * block + 156, record(std::string(1, '\0'), 20, block * 2, true));
        image[16 * block + 881] = 1;
        copyText(image, 17 * block, std::string("\xff" "CD001\1", 7));
        image[18 * block] = 1; le32(image, 18 * block + 2, 20); le16(image, 18 * block + 6, 1);
        image[19 * block] = 1; be32(image, 19 * block + 2, 20); be16(image, 19 * block + 6, 1);
        if (nested) {
            image[18 * block + 10] = 5; le32(image, 18 * block + 12, 22); le16(image, 18 * block + 16, 1);
            copyText(image, 18 * block + 18, "GAMES");
            image[19 * block + 10] = 5; be32(image, 19 * block + 12, 22); be16(image, 19 * block + 16, 1);
            copyText(image, 19 * block + 18, "GAMES");
        }
        const auto dot = record(std::string(1, '\0'), 20, block * 2, true);
        copy(image, 20 * block, dot);
        copy(image, 20 * block + dot.size(), record(std::string(1, '\1'), 20, block * 2, true));
        std::size_t rootPosition = configRecord;
        if (includeConfig) {
            const auto entry = record("SYSTEM.CNF;1", 23, std::uint32_t(config.size()));
            copy(image, rootPosition, entry);
            rootPosition += entry.size();
            copyText(image, 23 * block, config);
        }
        const auto demo = aurora::makeDemoPsxExe();
        copy(image, 24 * block, demo);
        if (nested) {
            copy(image, rootPosition, record("GAMES", 22, block, true));
            const auto subDot = record(std::string(1, '\0'), 22, block, true);
            copy(image, 22 * block, subDot);
            const auto parent = record(std::string(1, '\1'), 20, block * 2, true);
            copy(image, 22 * block + subDot.size(), parent);
            exeRecord = 22 * block + subDot.size() + parent.size();
        } else {
            exeRecord = rootPosition;
        }
        copy(image, exeRecord, record(fallback ? "PSX.EXE;1" : "DEMO.PSX;1", 24, std::uint32_t(demo.size())));
    }
};

Bytes raw(const Bytes& iso, unsigned sectorSize, unsigned mode, unsigned pregap = 0) {
    expect(iso.size() % block == 0, "ISO fixture deve ter setores completos");
    Bytes result((iso.size() / block + pregap) * sectorSize);
    for (std::size_t sector = 0; sector < iso.size() / block + pregap; ++sector) {
        const auto base = sector * sectorSize;
        unsigned payloadOffset;
        if (sectorSize == 2336) {
            result[base + 2] = 8; result[base + 6] = 8;
            payloadOffset = 8;
        } else {
            std::fill(result.begin() + std::ptrdiff_t(base + 1), result.begin() + std::ptrdiff_t(base + 11), 0xff);
            result[base + 15] = std::uint8_t(mode);
            if (mode == 2) { result[base + 18] = 8; result[base + 22] = 8; }
            payloadOffset = mode == 1 ? 16 : 24;
        }
        if (sector >= pregap)
            std::copy(iso.begin() + std::ptrdiff_t((sector - pregap) * block),
                      iso.begin() + std::ptrdiff_t((sector - pregap + 1) * block),
                      result.begin() + std::ptrdiff_t(base + payloadOffset));
    }
    return result;
}
struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("aurora-disc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { std::filesystem::create_directory(path); }
    ~Temp() { std::error_code error; std::filesystem::remove_all(path, error); }
    std::filesystem::path write(const std::string& name, const Bytes& bytes) {
        const auto target = path / name;
        std::ofstream output(target, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!output) throw std::runtime_error("Falha ao escrever fixture");
        return target;
    }
    std::filesystem::path text(const std::string& name, const std::string& text) {
        return write(name, Bytes(text.begin(), text.end()));
    }
};
void validateBoot(const std::filesystem::path& path, unsigned sectorSize, unsigned dataOffset) {
    const auto info = aurora::inspectDisc(path);
    expect(info.ps1Boot, "Boot PS1 nao localizado: " + info.description);
    expect(info.sectorSize == sectorSize, "Tamanho de setor detectado incorreto");
    expect(info.dataOffset == dataOffset, "Offset de dados detectado incorreto");
    expect(info.volumeId == "AURORA_TEST", "Identificador de volume decodificado incorretamente");
    const auto loaded = aurora::loadPs1Disc(path);
    expect(loaded.executable.entry == 0x80010000, "Ponto de entrada extraido incorreto");
    expect(loaded.executable.data.size() == 2048, "Tamanho do executavel extraido incorreto");
    expect(!loaded.disc.bootPath.empty(), "Caminho de boot nao reportado");
}
void expectVolumeWarning(const aurora::DiscInfo& info, std::uint64_t declared,
                         std::uint64_t available, std::uint64_t first = 0) {
    expect(info.ps1Boot, "Boot presente foi rejeitado por metadados de volume");
    expect(info.declaredSectors == declared, "Setores declarados nao reportados corretamente");
    expect(info.availableSectors == available, "Setores fisicos disponiveis nao reportados corretamente");
    expect(info.firstSector == first, "Inicio da faixa nao reportado corretamente");
    expect(info.description.find(std::to_string(declared)) != std::string::npos &&
           info.description.find(std::to_string(available)) != std::string::npos,
           "Descricao deve alertar a divergencia de tamanho com numeros");
}
}

int main() {
    unsigned passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("ISO2048: SYSTEM.CNF apos padding, subdiretorio, caixa e versao", [] {
        Temp t; validateBoot(t.write("demo.iso", Fixture().image), 2048, 0);
    });
    test("SYSTEM.CNF aceita padding NUL no fim do arquivo", [] {
        Temp t;
        std::string config = "BOOT = cdrom:\\games\\demo.psx;1\r\n";
        config.resize(block, '\0');
        validateBoot(t.write("padded.iso", Fixture(config).image), 2048, 0);
    });
    test("SYSTEM.CNF rejeita NUL que oculta texto posterior", [] {
        Temp t;
        std::string config = "BOOT = cdrom:\\games\\demo.psx;1\r\n";
        config.push_back('\0');
        config += "BOOT = cdrom:\\OUTRO.PSX;1\r\n";
        const auto path = t.write("embedded-nul.iso", Fixture(config).image);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("BIN2352 MODE1", [] {
        Temp t; validateBoot(t.write("demo.bin", raw(Fixture().image, 2352, 1)), 2352, 16);
    });
    test("BIN2352 MODE2 Form1", [] {
        Temp t; validateBoot(t.write("demo.bin", raw(Fixture().image, 2352, 2)), 2352, 24);
    });
    test("MODE2/2336", [] {
        Temp t; validateBoot(t.write("demo.iso", raw(Fixture().image, 2336, 2)), 2336, 8);
    });
    test("Setores2448 com subcanal", [] {
        Temp t; validateBoot(t.write("demo.bin", raw(Fixture().image, 2448, 2)), 2448, 24);
    });
    test("BIN direto com pregap150", [] {
        Temp t; validateBoot(t.write("demo.bin", raw(Fixture().image, 2352, 2, 150)), 2352, 24);
    });
    test("CUE com nome entre aspas e INDEX01 apos pregap", [] {
        Temp t; t.write("dados com espaco.bin", raw(Fixture().image, 2352, 2, 150));
        const auto cue = t.text("demo.cue", "FILE \"dados com espaco.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n");
        validateBoot(cue, 2352, 24);
    });
    test("CUE UTF8 BOM e FLAGS DCP preservam boot da faixa", [] {
        Temp t; t.write("demo.bin", raw(Fixture().image, 2352, 2));
        const auto cue = t.text("bom.cue", "\xef\xbb\xbf" "FILE \"demo.bin\" BINARY\r\n"
            "  TRACK 01 MODE2/2352\r\n    FLAGS DCP\r\n    INDEX 01 00:00:00\r\n");
        validateBoot(cue, 2352, 24);
    });
    test("ISO com volume maior que a imagem carrega boot presente e alerta divergencia", [] {
        Temp t; auto fixture = Fixture();
        both32(fixture.image, 16 * block + 80, 64);
        const auto path = t.write("overdeclared.iso", fixture.image);
        validateBoot(path, 2048, 0);
        expectVolumeWarning(aurora::loadPs1Disc(path).disc, 64, volumeBlocks);
    });
    test("BIN e CUE com pregap descontam inicio ao reportar setores presentes", [] {
        Temp t; auto fixture = Fixture();
        both32(fixture.image, 16 * block + 80, 64);
        const auto bin = t.write("track.bin", raw(fixture.image, 2352, 2, 150));
        const auto cue = t.text("overdeclared.cue", "FILE \"track.bin\" BINARY\n"
            "TRACK 01 MODE2/2352\nINDEX 00 00:00:00\nINDEX 01 00:02:00\n");
        validateBoot(bin, 2352, 24);
        validateBoot(cue, 2352, 24);
        expectVolumeWarning(aurora::loadPs1Disc(bin).disc, 64, volumeBlocks, 150);
        expectVolumeWarning(aurora::loadPs1Disc(cue).disc, 64, volumeBlocks, 150);
    });
    test("Padding final ausente nao impede leitura de todos os arquivos de boot", [] {
        Temp t; auto fixture = Fixture();
        fixture.image.resize(26 * block);
        const auto path = t.write("without-padding.iso", fixture.image);
        validateBoot(path, 2048, 0);
        expectVolumeWarning(aurora::loadPs1Disc(path).disc, volumeBlocks, 26);
    });
    test("Executavel dentro do volume declarado mas fora da imagem e rejeitado", [] {
        Temp t; auto fixture = Fixture();
        both32(fixture.image, 16 * block + 80, 64);
        both32(fixture.image, fixture.exeRecord + 2, 40);
        const auto path = t.write("missing-boot.iso", fixture.image);
        rejectsPhysicalExtent([&] { aurora::loadPs1Disc(path); }, 64, volumeBlocks);
    });
    test("Raiz dentro do volume declarado mas fora da imagem e rejeitada", [] {
        Temp t; auto fixture = Fixture();
        both32(fixture.image, 16 * block + 80, 64);
        both32(fixture.image, 16 * block + 156 + 2, 40);
        const auto path = t.write("missing-root.iso", fixture.image);
        rejectsPhysicalExtent([&] { aurora::loadPs1Disc(path); }, 64, volumeBlocks);
    });
    test("SYSTEM.CNF dentro do volume declarado mas fora da imagem e rejeitado", [] {
        Temp t; auto fixture = Fixture();
        both32(fixture.image, 16 * block + 80, 64);
        both32(fixture.image, fixture.configRecord + 2, 40);
        const auto path = t.write("missing-config.iso", fixture.image);
        rejectsPhysicalExtent([&] { aurora::loadPs1Disc(path); }, 64, volumeBlocks);
    });
    test("Arquivo nao solicitado fora da imagem nao e lido como parte do boot", [] {
        Temp t; auto fixture = Fixture();
        both32(fixture.image, 16 * block + 80, 64);
        std::size_t position = fixture.configRecord;
        while (fixture.image.at(position) != 0) position += fixture.image.at(position);
        copy(fixture.image, position, record("UNUSED.BIN;1", 48, block));
        const auto path = t.write("unavailable-unused.iso", fixture.image);
        validateBoot(path, 2048, 0);
        expectVolumeWarning(aurora::loadPs1Disc(path).disc, 64, volumeBlocks);
    });
    test("Descritor de boot antes do PVD", [] {
        Temp t; auto fixture = Fixture();
        Bytes pvd(fixture.image.begin() + 16 * block, fixture.image.begin() + 17 * block);
        copy(fixture.image, 26 * block, Bytes(fixture.image.begin() + 18 * block,
                                            fixture.image.begin() + 19 * block));
        copy(fixture.image, 27 * block, Bytes(fixture.image.begin() + 19 * block,
                                            fixture.image.begin() + 20 * block));
        le32(pvd, 140, 26); be32(pvd, 148, 27);
        std::fill(fixture.image.begin() + 16 * block, fixture.image.begin() + 18 * block, 0);
        copyText(fixture.image, 16 * block, std::string("\0CD001\1", 7));
        copy(fixture.image, 17 * block, pvd);
        copyText(fixture.image, 18 * block, std::string("\xff" "CD001\1", 7));
        validateBoot(t.write("demo.iso", fixture.image), 2048, 0);
    });
    test("Fallback PSX.EXE sem SYSTEM.CNF", [] {
        Temp t; validateBoot(t.write("demo.iso", Fixture("", false, false, true).image), 2048, 0);
    });
    test("ISO generico sem boot PS1 permanece somente inspecao", [] {
        Temp t; const auto path = t.write("dados.iso", Fixture("", false, false).image);
        const auto info = aurora::inspectDisc(path);
        expect(info.sectorSize == 2048 && !info.ps1Boot, "ISO generico foi aceito como PS1");
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("BOOT2 de PS2 nao e executavel PS1", [] {
        Temp t; const auto path = t.write("ps2.iso", Fixture("BOOT2 = cdrom0:\\DEMO.PSX;1\n", false).image);
        expect(!aurora::inspectDisc(path).ps1Boot, "BOOT2 tratado como boot de PS1");
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("Caminho BOOT escapando raiz rejeitado", [] {
        Temp t; const auto path = t.write("bad.iso", Fixture("BOOT = cdrom:\\..\\DEMO.PSX;1\n", false).image);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("Extent do executavel fora do volume rejeitado", [] {
        Temp t; auto fixture = Fixture(); both32(fixture.image, fixture.exeRecord + 2, 100000);
        const auto path = t.write("bad.iso", fixture.image);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("Tamanho malicioso de diretorio nao aloca memoria sem limite", [] {
        Temp t; auto fixture = Fixture(); both32(fixture.image, 16 * block + 156 + 10, 0xfffffff0);
        const auto path = t.write("bad.iso", fixture.image);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("Endian duplicado do PVD inconsistente rejeitado", [] {
        Temp t; auto fixture = Fixture(); be32(fixture.image, 16 * block + 84, volumeBlocks + 1);
        const auto path = t.write("bad.iso", fixture.image);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("Endian duplicado do diretorio inconsistente rejeitado", [] {
        Temp t; auto fixture = Fixture(); be32(fixture.image, fixture.exeRecord + 6, 25);
        const auto path = t.write("bad.iso", fixture.image);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("Imagem truncada corta payload do executavel e permanece rejeitada", [] {
        Temp t; auto fixture = Fixture(); fixture.image.resize(25 * block);
        const auto path = t.write("bad.iso", fixture.image);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("Raw MODE2 Form2 nao e interpretado como dados Form1", [] {
        Temp t; auto bytes = raw(Fixture().image, 2352, 2);
        for (std::size_t i = 0; i < bytes.size(); i += 2352) { bytes[i + 18] |= 0x20; bytes[i + 22] |= 0x20; }
        const auto path = t.write("form2.bin", bytes);
        rejects([&] { aurora::loadPs1Disc(path); });
    });
    test("CUE referencia ausente ou fora da pasta nao e aberto", [] {
        Temp t; const auto missing = t.text("missing.cue", "FILE \"missing.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::loadPs1Disc(missing); });
        Temp sibling;
        sibling.write("outside.bin", raw(Fixture().image, 2352, 1));
        const auto escapedName = "../" + sibling.path.filename().string() + "/outside.bin";
        const auto outside = t.text("outside.cue", "FILE \"" + escapedName + "\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::loadPs1Disc(outside); });
    });
    test("Imagem propria percorre disco -> loader -> MIPS -> VRAM", [] {
        Temp t; const auto path = t.write("demo.iso", Fixture().image);
        const auto boot = aurora::loadPs1Disc(path);
        aurora::Ps1Memory memory;
        memory.load(boot.executable);
        aurora::Cpu cpu(memory);
        cpu.reset(boot.executable.entry);
        cpu.regs[28] = boot.executable.gp;
        cpu.regs[29] = boot.executable.stackBase + boot.executable.stackOffset;
        expect(cpu.run(10000) == 10000, cpu.fault);
        const auto pixels = memory.framebuffer();
        expect(pixels[32 * 320 + 32] == 0xffcea542u, "Framebuffer nao corresponde a demo executada do disco");
        expect(pixels[100 * 320 + 40] != pixels[100 * 320 + 180], "Retangulos nao foram desenhados");
    });
    std::cout << passed << " testes de disco passaram; " << failed << " falharam\n";
    return failed ? 1 : 0;
}
