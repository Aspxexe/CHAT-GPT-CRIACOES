#include "aurora/media.hpp"
#include "aurora/disc.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

#ifndef AURORA_VERSION
#define AURORA_VERSION "dev"
#endif

namespace aurora {
namespace {
constexpr std::uint64_t ramSize = 2 * 1024 * 1024;
constexpr std::size_t exeHeaderSize = 0x800;

std::uint64_t sizeOf(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("A midia deve ser um arquivo regular existente.");
    }
    return std::filesystem::file_size(path);
}

std::vector<std::uint8_t> readAt(const std::filesystem::path& path,
                               std::uint64_t offset, std::size_t count) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Nao foi possivel abrir a midia.");
    if (offset > std::uint64_t(std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("Posicao do arquivo fora do limite.");
    }
    stream.seekg(std::streamoff(offset));
    if (!stream) return {};
    std::vector<std::uint8_t> result(count);
    stream.read(reinterpret_cast<char*>(result.data()), std::streamsize(count));
    if (stream.bad()) throw std::runtime_error("Erro de leitura da midia.");
    result.resize(std::size_t(stream.gcount()));
    return result;
}

std::uint32_t le32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return std::uint32_t(data.at(offset)) |
           (std::uint32_t(data.at(offset + 1)) << 8) |
           (std::uint32_t(data.at(offset + 2)) << 16) |
           (std::uint32_t(data.at(offset + 3)) << 24);
}

void put32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.at(offset + i) = std::uint8_t(value >> (i * 8));
}

bool isRam(std::uint32_t address) {
    const auto segment = address >> 29;
    return (segment == 0 || segment == 4 || segment == 5) &&
           (address & 0x1fffffffU) < ramSize;
}

bool hasExeMagic(const std::vector<std::uint8_t>& data) {
    const std::array<std::uint8_t, 8> magic{'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
    return data.size() >= magic.size() && std::equal(magic.begin(), magic.end(), data.begin());
}

std::string uppercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return char(std::toupper(c));
    });
    return text;
}

bool isBelow(const std::filesystem::path& base, const std::filesystem::path& child) {
    auto ancestor = child.parent_path();
    while (!ancestor.empty()) {
        if (std::filesystem::equivalent(base, ancestor)) return true;
        const auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    return false;
}

MediaInfo inspectCue(const std::filesystem::path& path, std::uint64_t size) {
    if (size == 0 || size > 1024 * 1024) throw std::runtime_error("CUE vazio ou maior que 1 MiB.");
    const auto bytes = readAt(path, 0, std::size_t(size));
    std::string text(bytes.begin(), bytes.end());
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    if (text.find('\0') != std::string::npos) throw std::runtime_error("CUE contem bytes nulos.");
    const auto base = std::filesystem::canonical(path).parent_path();
    const std::regex filePattern(R"cue(^\s*FILE\s+"([^"]+)"\s+(BINARY|WAVE)\s*$)cue", std::regex::icase);
    const std::regex trackPattern(R"(^\s*TRACK\s+([0-9]{1,2})\s+(MODE1/2352|MODE2/2352|AUDIO)\s*$)", std::regex::icase);
    const std::regex indexPattern(R"(^\s*INDEX\s+(00|01)\s+([0-9]{1,3}):([0-9]{2}):([0-9]{2})\s*$)", std::regex::icase);
    const std::regex gapPattern(R"(^\s*(PREGAP|POSTGAP)\s+([0-9]{1,3}):([0-9]{2}):([0-9]{2})\s*$)", std::regex::icase);
    std::istringstream input(text);
    std::string line;
    std::size_t tracks = 0;
    std::size_t files = 0;
    std::uint64_t currentSize = 0;
    std::uint64_t previousIndex = 0;
    bool haveIndex = false;
    bool haveIndexOne = false;
    bool wave = false;
    bool fileHasTrack = false;
    while (std::getline(input, line)) {
        if (line.size() > 4096) throw std::runtime_error("Linha CUE maior que o limite.");
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        std::istringstream tokens(line.substr(start));
        std::string command;
        tokens >> command;
        command = uppercase(command);
        std::smatch match;
        if (command == "FILE") {
            if (!std::regex_match(line, match, filePattern)) throw std::runtime_error("FILE no CUE deve usar nome entre aspas e tipo BINARY ou WAVE.");
            if (files && !fileHasTrack) throw std::runtime_error("FILE no CUE sem TRACK.");
            if (tracks && !haveIndexOne) throw std::runtime_error("TRACK no CUE sem INDEX 01.");
            std::string name = match[1].str();
            std::replace(name.begin(), name.end(), '\\', '/');
            const auto relative = std::filesystem::u8path(name);
            if (relative.is_absolute() || relative.has_root_name() || name.find(':') != std::string::npos) {
                throw std::runtime_error("CUE aceita apenas caminhos relativos a sua pasta.");
            }
            const auto resolved = std::filesystem::canonical(base / relative);
            if (!isBelow(base, resolved)) throw std::runtime_error("Referencia CUE escapa da pasta da midia.");
            currentSize = sizeOf(resolved);
            wave = uppercase(match[2].str()) == "WAVE";
            if (currentSize < (wave ? 44U : 2352U)) throw std::runtime_error("Arquivo de faixa CUE truncado.");
            if (++files > 99) throw std::runtime_error("CUE excede 99 arquivos.");
            haveIndex = false;
            fileHasTrack = false;
        } else if (command == "TRACK") {
            if (!files || !std::regex_match(line, match, trackPattern)) throw std::runtime_error("TRACK exige FILE e modo AUDIO, MODE1/2352 ou MODE2/2352.");
            if (tracks && !haveIndexOne) throw std::runtime_error("TRACK no CUE sem INDEX 01.");
            if (std::stoul(match[1].str()) != tracks + 1 || tracks >= 99) throw std::runtime_error("Numeracao TRACK deve ser sequencial, de 01 a 99.");
            if (wave && uppercase(match[2].str()) != "AUDIO") throw std::runtime_error("Arquivo WAVE deve conter faixa AUDIO.");
            ++tracks;
            haveIndexOne = false;
            fileHasTrack = true;
        } else if (command == "INDEX") {
            if (!fileHasTrack || !std::regex_match(line, match, indexPattern)) throw std::runtime_error("INDEX CUE invalido.");
            const auto seconds = std::stoul(match[3].str());
            const auto frames = std::stoul(match[4].str());
            if (seconds >= 60 || frames >= 75) throw std::runtime_error("Tempo INDEX CUE invalido.");
            const std::uint64_t position = (std::stoul(match[2].str()) * 60 + seconds) * 75 + frames;
            if ((haveIndex && position < previousIndex) || (!wave && position >= currentSize / 2352)) {
                throw std::runtime_error("INDEX CUE fora dos limites do arquivo de faixa.");
            }
            if (match[1].str() == "01") {
                if (haveIndexOne) throw std::runtime_error("INDEX 01 duplicado.");
                haveIndexOne = true;
            } else if (haveIndexOne) {
                throw std::runtime_error("INDEX 00 deve preceder INDEX 01.");
            }
            haveIndex = true;
            previousIndex = position;
        } else if (command == "PREGAP" || command == "POSTGAP") {
            if (!fileHasTrack || !std::regex_match(line, match, gapPattern) ||
                std::stoul(match[3].str()) >= 60 || std::stoul(match[4].str()) >= 75) {
                throw std::runtime_error("Intervalo CUE invalido.");
            }
        } else if (command == "FLAGS") {
            if (!fileHasTrack) throw std::runtime_error("FLAGS exige uma faixa TRACK.");
            std::string flag;
            bool any = false;
            while (tokens >> flag) {
                flag = uppercase(flag);
                if (flag != "DCP" && flag != "PRE" && flag != "SCMS" && flag != "4CH")
                    throw std::runtime_error("Flag CUE nao reconhecida: " + flag);
                any = true;
            }
            if (!any) throw std::runtime_error("FLAGS CUE vazio.");
        } else if (command != "REM" && command != "TITLE" && command != "PERFORMER" &&
                   command != "SONGWRITER" && command != "CATALOG" && command != "ISRC") {
            throw std::runtime_error("Diretiva CUE ainda nao suportada: " + command);
        }
    }
    if (!tracks || !fileHasTrack || !haveIndexOne) throw std::runtime_error("CUE sem faixas completas com INDEX 01.");
    return {"cue", "CUE validado: " + std::to_string(tracks) + " faixa(s), " +
                   std::to_string(files) + " arquivo(s). Leitor de CD-ROM ainda nao implementado.", false};
}
}

PsxExe parsePsxExe(const std::vector<std::uint8_t>& image) {
    const auto size = image.size();
    if (size < exeHeaderSize || size > ramSize + exeHeaderSize) {
        throw std::runtime_error("PS-X EXE truncado ou maior que a memoria RAM suportada.");
    }
    const auto& header = image;
    if (!hasExeMagic(header)) throw std::runtime_error("Assinatura PS-X EXE invalida.");
    PsxExe exe;
    exe.entry = le32(header, 0x10);
    exe.gp = le32(header, 0x14);
    exe.loadAddress = le32(header, 0x18);
    const auto payloadSize = le32(header, 0x1c);
    exe.stackBase = le32(header, 0x30);
    exe.stackOffset = le32(header, 0x34);
    const auto physicalLoad = exe.loadAddress & 0x1fffffffU;
    if (!payloadSize || (exe.loadAddress & 3) || !isRam(exe.loadAddress) ||
        std::uint64_t(physicalLoad) + payloadSize > ramSize ||
        std::uint64_t(exeHeaderSize) + payloadSize > size) {
        throw std::runtime_error("Payload PS-X EXE truncado ou fora da RAM de 2 MiB.");
    }
    const auto physicalEntry = exe.entry & 0x1fffffffU;
    if ((exe.entry & 3) || !isRam(exe.entry) || physicalEntry < physicalLoad ||
        std::uint64_t(physicalEntry) >= std::uint64_t(physicalLoad) + payloadSize) {
        throw std::runtime_error("Entrada PS-X EXE desalinhada ou fora do programa carregado.");
    }
    if (exe.stackBase) {
        const auto stack = std::uint64_t(exe.stackBase) + exe.stackOffset;
        if (stack > std::numeric_limits<std::uint32_t>::max() || (stack & 3) ||
            !isRam(std::uint32_t(stack))) {
            throw std::runtime_error("Pilha PS-X EXE invalida para este prototipo.");
        }
    }
    exe.data.assign(image.begin() + exeHeaderSize, image.begin() + exeHeaderSize + payloadSize);
    return exe;
}

PsxExe loadPsxExe(const std::filesystem::path& path) {
    const auto size = sizeOf(path);
    if (size < exeHeaderSize || size > ramSize + exeHeaderSize)
        throw std::runtime_error("PS-X EXE truncado ou maior que a memoria RAM suportada.");
    const auto image = readAt(path, 0, std::size_t(size));
    if (image.size() != size) throw std::runtime_error("PS-X EXE truncado durante a leitura.");
    return parsePsxExe(image);
}

MediaInfo inspectCueFile(const std::filesystem::path& path) {
    return inspectCue(path, sizeOf(path));
}

MediaInfo inspectMedia(const std::filesystem::path& path) {
    sizeOf(path);
    const auto prefix = readAt(path, 0, 16);
    if (hasExeMagic(prefix)) {
        const auto exe = loadPsxExe(path);
        return {"psx-exe", "Executavel PS-X EXE validado; " + std::to_string(exe.data.size()) +
                           " bytes. Exige apenas instrucoes e dispositivos implementados.", true};
    }
    if (prefix.size() >= 4 && prefix[0] == 0x7f && prefix[1] == 'P' &&
        prefix[2] == 'K' && prefix[3] == 'G') {
        return {"pkg", "Assinatura PKG reconhecida. Console, integridade e conteudo nao validados; instalacao e execucao nao implementadas.", false};
    }
    const auto disc = inspectDisc(path);
    return {disc.format, disc.description, disc.ps1Boot};
}

std::string diagnoseMedia(const std::filesystem::path& path) {
    std::ostringstream output;
    output << "Aurora " AURORA_VERSION " - diagnostico da ultima tentativa\nArquivo: " << path.u8string() << '\n';
    try {
        output << "Tamanho: " << sizeOf(path) << " bytes\nExtensao: " << path.extension().u8string() << '\n';
        output << "Assinatura inicial (16 bytes, hexadecimal):";
        for (auto byte : readAt(path, 0, 16))
            output << ' ' << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
        output << std::dec << '\n';
        const auto info = inspectMedia(path);
        output << "Formato: " << info.kind << "\nInspecao: " << info.description
               << "\nLoader experimental disponivel: " << (info.executable ? "sim" : "nao") << '\n';
        if (info.kind != "psx-exe" && info.kind != "pkg") {
            const auto disc = inspectDisc(path);
            output << "Setor fisico: " << disc.sectorSize << " bytes\nOffset de dados no setor: "
                   << disc.dataOffset << "\nVolume: " << disc.volumeId
                   << "\nExecutavel de boot: " << disc.bootPath << '\n';
            output << "Inicio da faixa (setores): " << disc.firstSector
                   << "\nSetores declarados pelo volume: " << disc.declaredSectors
                   << "\nSetores disponiveis na imagem a partir da faixa: " << disc.availableSectors
                   << "\nAvisos: " << (disc.warning.empty() ? "nenhum" : disc.warning) << '\n';
        }
    } catch (const std::exception& error) {
        output << "Falha na inspecao: " << error.what() << '\n';
    }
    output << "Carregar um executavel de disco nao valida jogo comercial. BIOS/HLE, GTE, DMA, CD-ROM durante execucao e GPU completa ainda ausentes.\n";
    return output.str();
}

std::vector<std::uint8_t> makeDemoPsxExe() {
    std::vector<std::uint32_t> code;
    auto emit = [&](std::uint32_t instruction) { code.push_back(instruction); };
    auto constant = [&](unsigned reg, std::uint32_t value) {
        emit(0x3c000000U | (reg << 16) | (value >> 16));
        emit(0x34000000U | (reg << 21) | (reg << 16) | (value & 0xffff));
    };
    constant(8, 0x1f801810);
    auto fill = [&](std::uint32_t rgb, unsigned x, unsigned y, unsigned w, unsigned h) {
        constant(9, 0x02000000U | rgb);
        emit(0xad090000U);
        constant(9, (y << 16) | x);
        emit(0xad090000U);
        constant(9, (h << 16) | w);
        emit(0xad090000U);
    };
    fill(0x00201610, 0, 0, 320, 240);
    fill(0x00cba340, 32, 32, 256, 48);
    fill(0x0040cb8a, 32, 96, 112, 112);
    fill(0x00cb608a, 176, 96, 112, 112);
    const auto loopAddress = 0x80010000U + std::uint32_t(code.size() * 4);
    emit(0x08000000U | ((loopAddress >> 2) & 0x03ffffffU));
    emit(0);
    const std::size_t payloadSize = (code.size() * 4 + 0x7ff) & ~std::size_t(0x7ff);
    std::vector<std::uint8_t> result(exeHeaderSize + payloadSize, 0);
    const std::array<std::uint8_t, 8> magic{'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
    std::copy(magic.begin(), magic.end(), result.begin());
    put32(result, 0x10, 0x80010000);
    put32(result, 0x18, 0x80010000);
    put32(result, 0x1c, std::uint32_t(payloadSize));
    put32(result, 0x30, 0x801fff00);
    for (std::size_t i = 0; i < code.size(); ++i) put32(result, exeHeaderSize + i * 4, code[i]);
    return result;
}

}
