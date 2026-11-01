#include "aurora/disc.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aurora {
namespace {
constexpr std::uint64_t blockBytes = 2048;
constexpr std::uint64_t maxDirectory = 8 * 1024 * 1024;
constexpr std::uint64_t maxExecutable = 2 * 1024 * 1024 + 2048;

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return char(std::toupper(c));
    });
    return value;
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string displayText(const std::uint8_t* first, std::size_t count) {
    std::string text;
    for (std::size_t i = 0; i < count; ++i) {
        const auto c = first[i];
        text.push_back(c >= 32 && c < 127 ? char(c) : '?');
    }
    return trim(std::move(text));
}
std::uint32_t le32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
        (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
std::uint32_t be32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[3]) | (std::uint32_t(bytes[2]) << 8) |
        (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[0]) << 24);
}
std::uint32_t dual32(const std::uint8_t* bytes) {
    const auto value = le32(bytes);
    if (value != be32(bytes + 4)) throw std::runtime_error("ISO9660: campos little/big-endian inconsistentes.");
    return value;
}
std::uint16_t dual16(const std::uint8_t* bytes) {
    const auto little = unsigned(bytes[0]) | (unsigned(bytes[1]) << 8);
    const auto big = unsigned(bytes[3]) | (unsigned(bytes[2]) << 8);
    if (little != big) throw std::runtime_error("ISO9660: campos little/big-endian inconsistentes.");
    return std::uint16_t(little);
}

class Image {
public:
    explicit Image(const std::filesystem::path& path) : stream_(path, std::ios::binary) {
        if (!std::filesystem::is_regular_file(path) || !stream_)
            throw std::runtime_error("Imagem de disco ausente ou nao e arquivo regular.");
        size = std::filesystem::file_size(path);
        if (size > std::uint64_t(std::numeric_limits<std::streamoff>::max()))
            throw std::runtime_error("Imagem excede o limite de leitura.");
    }
    std::vector<std::uint8_t> read(std::uint64_t offset, std::size_t length) {
        if (offset > size || length > size - offset)
            throw std::runtime_error("Imagem truncada: leitura ultrapassa o arquivo.");
        std::vector<std::uint8_t> bytes(length);
        stream_.clear();
        stream_.seekg(std::streamoff(offset));
        if (!stream_) throw std::runtime_error("Falha ao posicionar leitura da imagem.");
        stream_.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(length));
        if (!stream_ || std::size_t(stream_.gcount()) != length)
            throw std::runtime_error("Falha ao ler a imagem de disco.");
        return bytes;
    }
    std::uint64_t size = 0;
private:
    std::ifstream stream_;
};

struct Source {
    std::filesystem::path path;
    std::uint64_t firstSector = 0;
    bool cue = false;
    unsigned forcedOffset = 0;
};

bool isInside(const std::filesystem::path& base, const std::filesystem::path& file) {
    auto parent = file.parent_path();
    while (!parent.empty()) {
        if (std::filesystem::equivalent(base, parent)) return true;
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    return false;
}

Source sourceFor(const std::filesystem::path& input) {
    Source source{input};
    if (upper(input.extension().string()) != ".CUE") return source;
    inspectCueFile(input);
    source.cue = true;
    const auto cuePath = std::filesystem::canonical(input);
    std::ifstream cue(cuePath, std::ios::binary);
    if (!cue) throw std::runtime_error("Nao foi possivel reabrir o CUE validado.");
    std::string line;
    std::filesystem::path current;
    bool selected = false;
    const std::regex file(R"cue(^\s*FILE\s+"([^"]+)"\s+(BINARY|WAVE)\s*$)cue", std::regex::icase);
    const std::regex track(R"(^\s*TRACK\s+[0-9]{1,2}\s+(MODE1/2352|MODE2/2352|AUDIO)\s*$)", std::regex::icase);
    const std::regex index(R"(^\s*INDEX\s+01\s+([0-9]{1,3}):([0-9]{2}):([0-9]{2})\s*$)", std::regex::icase);
    std::smatch match;
    bool firstLine = true;
    while (std::getline(cue, line)) {
        if (firstLine && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        firstLine = false;
        if (std::regex_match(line, match, file)) {
            auto name = match[1].str();
            std::replace(name.begin(), name.end(), '\\', '/');
            const auto relative = std::filesystem::u8path(name);
            if (relative.is_absolute() || relative.has_root_name() || name.find(':') != std::string::npos)
                throw std::runtime_error("CUE aceita apenas referencias relativas a sua pasta.");
            current = std::filesystem::canonical(cuePath.parent_path() / relative);
            if (!isInside(cuePath.parent_path(), current))
                throw std::runtime_error("Referencia CUE escapa da pasta da midia durante leitura.");
            selected = false;
        } else if (std::regex_match(line, match, track)) {
            const auto mode = upper(match[1].str());
            selected = mode != "AUDIO";
            if (selected) source.forcedOffset = mode == "MODE1/2352" ? 16 : 24;
        } else if (selected && std::regex_match(line, match, index)) {
            if (std::stoull(match[2].str()) >= 60 || std::stoull(match[3].str()) >= 75)
                throw std::runtime_error("Tempo INDEX 01 do CUE invalido durante leitura.");
            source.path = current;
            source.firstSector = (std::stoull(match[1].str()) * 60 + std::stoull(match[2].str())) * 75 +
                                 std::stoull(match[3].str());
            return source;
        }
    }
    throw std::runtime_error("CUE nao possui faixa de dados suportada com INDEX 01.");
}

bool descriptorSignature(const std::vector<std::uint8_t>& bytes) {
    return bytes.size() >= 7 && bytes[1] == 'C' && bytes[2] == 'D' &&
        bytes[3] == '0' && bytes[4] == '0' && bytes[5] == '1' && bytes[6] == 1;
}
bool rawSync(const std::uint8_t* bytes) {
    if (bytes[0] || bytes[11]) return false;
    return std::all_of(bytes + 1, bytes + 11, [](std::uint8_t b) { return b == 0xff; });
}
struct Layout {
    std::uint32_t sector = 0, offset = 0;
    std::uint64_t first = 0;
};

std::vector<std::uint8_t> readSector(Image& image, const Layout& layout, std::uint32_t lba) {
    const auto physical = layout.first + lba;
    if (physical > image.size / layout.sector || layout.sector > image.size - physical * layout.sector)
        throw std::runtime_error("Setor do disco fora dos limites da imagem.");
    auto bytes = image.read(physical * layout.sector, layout.sector);
    if (layout.sector == 2352 || layout.sector == 2448) {
        if (!rawSync(bytes.data()) || bytes[15] != (layout.offset == 16 ? 1 : 2))
            throw std::runtime_error("Setor bruto: sincronismo ou modo inconsistente com a faixa.");
    }
    if (layout.offset == 24 || layout.sector == 2336) {
        const auto subheader = layout.sector == 2336 ? 0U : 16U;
        if (!std::equal(bytes.begin() + subheader, bytes.begin() + subheader + 4,
                        bytes.begin() + subheader + 4))
            throw std::runtime_error("Setor MODE2 com subcabecalhos inconsistentes.");
        if (bytes[subheader + 2] & 0x20)
            throw std::runtime_error("MODE2 Form2 nao pode fornecer bloco de dados ISO9660 de 2048 bytes.");
    }
    return {bytes.begin() + layout.offset, bytes.begin() + layout.offset + blockBytes};
}

struct Candidate {
    Layout layout;
    std::vector<std::uint8_t> pvd;
};

std::optional<Candidate> detectLayout(Image& image, const Source& source) {
    const std::array<Layout, 6> possibilities{{{2048, 0, 0}, {2352, 16, 0}, {2352, 24, 0},
                                             {2336, 8, 0}, {2448, 16, 0}, {2448, 24, 0}}};
    for (const auto first : {source.firstSector, source.firstSector + 150}) {
        if (source.cue && first != source.firstSector) continue;
        for (auto layout : possibilities) {
            if (source.cue && (layout.sector != 2352 || layout.offset != source.forcedOffset)) continue;
            layout.first = first;
            for (std::uint32_t lba = 16; lba < 80; ++lba) {
                const auto position = (first + lba) * layout.sector + layout.offset;
                if (position > image.size || blockBytes > image.size - position) break;
                const auto bytes = image.read(position, blockBytes);
                if (!descriptorSignature(bytes)) break;
                if (bytes[0] == 1) return Candidate{layout, readSector(image, layout, lba)};
                if (bytes[0] == 255) break;
            }
        }
    }
    return {};
}

struct Entry {
    std::uint32_t extent = 0, size = 0;
    std::string name;
    bool directory = false;
    bool unsupported = false;
};

class Volume {
public:
    Volume(Image& image, Candidate candidate) : image_(image), layout_(candidate.layout) {
        const auto& pvd = candidate.pvd;
        blocks_ = dual32(pvd.data() + 80);
        if (!blocks_ || dual16(pvd.data() + 128) != blockBytes)
            throw std::runtime_error("ISO9660: volume vazio ou bloco logico diferente de 2048 bytes.");
        if (layout_.first > image_.size / layout_.sector)
            throw std::runtime_error("ISO9660: inicio da faixa ultrapassa o arquivo da imagem.");
        availableBlocks_ = image_.size / layout_.sector - layout_.first;
        if (layout_.sector != 2048 && image_.size % layout_.sector)
            throw std::runtime_error("Imagem bruta truncada: tamanho nao e multiplo do setor detectado.");
        if (blocks_ > availableBlocks_) {
            warning = "Aviso: volume declara " + std::to_string(blocks_) +
                " setores; presentes " + std::to_string(availableBlocks_) +
                " a partir do inicio da faixa. Imagem parcial, padding ausente ou volume inconsistente; "
                "carregamento limitado aos arquivos presentes, sem confirmar integridade do disco. ";
        }
        volumeId = displayText(pvd.data() + 40, 32);
        root_ = record(pvd.data() + 156, blockBytes - 156);
        if (!root_.directory || root_.name != std::string(1, '\0') || root_.size == 0)
            throw std::runtime_error("ISO9660: registro do diretorio raiz invalido.");
        validate(root_);
        validatePhysical(root_);
    }

    std::optional<Entry> find(const std::string& path) {
        const auto parts = components(path);
        Entry directory = root_;
        std::optional<Entry> selected;
        for (std::size_t depth = 0; depth < parts.size(); ++depth) {
            if (!directory.directory) throw std::runtime_error("ISO9660: caminho BOOT atravessa arquivo, nao diretorio.");
            selected.reset();
            for (const auto& entry : entries(directory)) {
                if (entry.name == std::string(1, '\0') || entry.name == std::string(1, '\1')) continue;
                if (sameName(entry.name, parts[depth])) {
                    if (selected) throw std::runtime_error("ISO9660: nome de arquivo ambiguo no diretorio.");
                    selected = entry;
                }
            }
            if (!selected) return {};
            directory = *selected;
        }
        return selected;
    }

    std::vector<std::uint8_t> contents(const Entry& entry, std::uint64_t limit) {
        validate(entry);
        validatePhysical(entry);
        if (entry.unsupported) throw std::runtime_error("ISO9660: arquivo intercalado, multi-extent ou com atributos estendidos nao suportado.");
        if (entry.size > limit) throw std::runtime_error("ISO9660: arquivo excede o limite de leitura deste prototipo.");
        std::vector<std::uint8_t> bytes;
        bytes.reserve(entry.size);
        for (std::uint64_t done = 0; done < entry.size; done += blockBytes) {
            const auto sector = readSector(image_, layout_, entry.extent + std::uint32_t(done / blockBytes));
            const auto amount = std::min<std::uint64_t>(blockBytes, entry.size - done);
            bytes.insert(bytes.end(), sector.begin(), sector.begin() + amount);
        }
        return bytes;
    }

    std::string volumeId;
    std::string warning;
    std::uint64_t declaredSectors() const { return blocks_; }
    std::uint64_t availableSectors() const { return availableBlocks_; }
private:
    Image& image_;
    Layout layout_;
    std::uint32_t blocks_ = 0;
    std::uint64_t availableBlocks_ = 0;
    Entry root_;
    std::uint64_t directoryBudget_ = 16 * 1024 * 1024;

    static std::vector<std::string> components(std::string path) {
        if (path.size() > 1024 || path.find('\0') != std::string::npos || path.find(':') != std::string::npos)
            throw std::runtime_error("ISO9660: caminho BOOT invalido ou excessivamente longo.");
        std::replace(path.begin(), path.end(), '\\', '/');
        std::vector<std::string> result;
        std::istringstream input(path);
        std::string part;
        while (std::getline(input, part, '/')) {
            if (part.empty()) continue;
            if (part == "." || part == "..") throw std::runtime_error("ISO9660: caminho BOOT nao pode usar . ou ...");
            if (std::any_of(part.begin(), part.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                throw std::runtime_error("ISO9660: caminho BOOT possui caracteres de controle.");
            result.push_back(part);
            if (result.size() > 16) throw std::runtime_error("ISO9660: profundidade do caminho excede 16 diretorios.");
        }
        if (result.empty()) throw std::runtime_error("ISO9660: caminho BOOT vazio.");
        return result;
    }
    static bool sameName(std::string name, std::string requested) {
        name = upper(std::move(name));
        requested = upper(std::move(requested));
        if (requested.find(';') == std::string::npos) name = name.substr(0, name.find(';'));
        return name == requested;
    }
    void validate(const Entry& entry) const {
        const auto length = (std::uint64_t(entry.size) + blockBytes - 1) / blockBytes;
        if (entry.extent > blocks_ || length > blocks_ - entry.extent)
            throw std::runtime_error("ISO9660: extent de arquivo/diretorio ultrapassa o volume.");
    }
    void validatePhysical(const Entry& entry) const {
        const auto length = (std::uint64_t(entry.size) + blockBytes - 1) / blockBytes;
        if (entry.extent <= availableBlocks_ && length <= availableBlocks_ - entry.extent) return;
        const auto name = entry.name == std::string(1, '\0') ? std::string("/") :
            displayText(reinterpret_cast<const std::uint8_t*>(entry.name.data()), entry.name.size());
        throw std::runtime_error("ISO9660: arquivo/diretorio '" + name +
            "' requer LBA " + std::to_string(entry.extent) + " + " + std::to_string(length) +
            " setores, fora dos limites fisicos da imagem (presentes " + std::to_string(availableBlocks_) +
            ", volume declarado " + std::to_string(blocks_) + ", setor " + std::to_string(layout_.sector) +
            " bytes, inicio da faixa " + std::to_string(layout_.first) +
            "). Confira a integridade da imagem e a faixa selecionada no CUE.");
    }
    Entry record(const std::uint8_t* bytes, std::size_t remaining) {
        if (!remaining || bytes[0] < 34 || bytes[0] > remaining || !bytes[32] ||
            33U + bytes[32] > bytes[0])
            throw std::runtime_error("ISO9660: registro de diretorio truncado ou invalido.");
        if (dual16(bytes + 28) != 1)
            throw std::runtime_error("ISO9660: diretorio referencia outro volume nao suportado.");
        Entry result;
        result.extent = dual32(bytes + 2);
        result.size = dual32(bytes + 10);
        result.name.assign(reinterpret_cast<const char*>(bytes + 33), bytes[32]);
        result.directory = (bytes[25] & 2) != 0;
        result.unsupported = bytes[1] != 0 || bytes[26] != 0 || bytes[27] != 0 || (bytes[25] & 0x80);
        validate(result);
        return result;
    }
    std::vector<Entry> entries(const Entry& directory) {
        if (directory.size > maxDirectory || directory.size > directoryBudget_)
            throw std::runtime_error("ISO9660: diretorios excedem o limite de leitura segura.");
        directoryBudget_ -= directory.size;
        const auto bytes = contents(directory, maxDirectory);
        std::vector<Entry> result;
        for (std::size_t offset = 0; offset < bytes.size();) {
            if (bytes[offset] == 0) {
                offset = ((offset / blockBytes) + 1) * blockBytes;
                continue;
            }
            const auto remaining = std::min<std::uint64_t>(bytes.size() - offset,
                blockBytes - offset % blockBytes);
            result.push_back(record(bytes.data() + offset, remaining));
            if (result.size() > 32768) throw std::runtime_error("ISO9660: numero de entradas de diretorio excede o limite.");
            offset += bytes[offset];
        }
        return result;
    }
};

std::optional<std::string> bootFromConfig(const std::vector<std::uint8_t>& bytes, bool& ps2) {
    std::string text(bytes.begin(), bytes.end());
    const auto terminator = text.find('\0');
    if (terminator != std::string::npos) {
        if (text.find_first_not_of('\0', terminator) != std::string::npos)
            throw std::runtime_error("SYSTEM.CNF contem texto apos um byte nulo invalido.");
        text.resize(terminator);
    }
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    std::istringstream input(text);
    std::string line;
    std::optional<std::string> boot;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = upper(trim(line.substr(0, equals)));
        if (key == "BOOT2") { ps2 = true; continue; }
        if (key != "BOOT") continue;
        if (boot) throw std::runtime_error("SYSTEM.CNF possui BOOT duplicado.");
        auto value = trim(line.substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        const auto valueUpper = upper(value);
        if (valueUpper.compare(0, 6, "CDROM:") == 0) value.erase(0, 6);
        else if (valueUpper.compare(0, 7, "CDROM0:") == 0) value.erase(0, 7);
        else throw std::runtime_error("SYSTEM.CNF: BOOT deve referenciar cdrom: ou cdrom0:.");
        if (value.empty()) throw std::runtime_error("SYSTEM.CNF: caminho BOOT vazio.");
        boot = value;
    }
    return boot;
}

DiscInfo basicInfo(std::string format, std::string description) {
    DiscInfo info;
    info.format = std::move(format);
    info.description = std::move(description);
    return info;
}

DiscInfo signatureInfo(Image& image) {
    const auto prefix = image.read(0, std::size_t(std::min<std::uint64_t>(image.size, 32)));
    auto starts = [&](const std::string& signature) {
        return prefix.size() >= signature.size() && std::equal(signature.begin(), signature.end(), prefix.begin(),
            [](char a, std::uint8_t b) { return std::uint8_t(a) == b; });
    };
    if (starts("PK\x03\x04") || starts("PK\x05\x06") || starts("PK\x07\x08"))
        return basicInfo("zip", "Arquivo ZIP compactado. Extraia o CUE/BIN ou ISO antes de adicionar; ZIP nao e um disco PS1 pronto para leitura.");
    if (starts(std::string("7z\xbc\xaf\x27\x1c", 6)))
        return basicInfo("7z", "Arquivo 7z compactado. Extraia o CUE/BIN ou ISO antes de adicionar.");
    if (starts("Rar!")) return basicInfo("rar", "Arquivo RAR compactado. Extraia o CUE/BIN ou ISO antes de adicionar.");
    if (starts("MComprHD")) return basicInfo("chd", "Imagem CHD comprimida; descompressao CHD ainda nao implementada. Use CUE/BIN ou ISO sem compressao.");
    if (starts("CISO") || starts("ZISO"))
        return basicInfo("cso", "Imagem CSO/ZSO comprimida; este formato ainda nao possui descompressor no Aurora.");
    if (prefix.size() >= 32 && be32(prefix.data() + 28) == 0xc2339f3d)
        return basicInfo("gamecube", "Imagem de GameCube reconhecida pelo cabecalho; nao e um disco PS1 e seu nucleo ainda nao foi implementado.");
    if (prefix.size() >= 28 && be32(prefix.data() + 24) == 0x5d1c9ea3)
        return basicInfo("wii", "Imagem de Wii reconhecida pelo cabecalho; nao e um disco PS1 e seu nucleo ainda nao foi implementado.");
    if (prefix.size() >= 16 && rawSync(prefix.data()) && (prefix[15] == 1 || prefix[15] == 2)) {
        if (image.size < 2352) throw std::runtime_error("Setor bruto truncado: a imagem nao contem sequer um setor completo.");
        const auto sector = image.size % 2352 == 0 ? 2352U : image.size % 2448 == 0 ? 2448U : 0U;
        if (!sector) throw std::runtime_error("Imagem bruta truncada ou layout desconhecido: tamanho nao e multiplo de 2352/2448.");
        auto info = basicInfo("bin-raw", "Setores brutos reconhecidos, mas nao foi encontrado volume ISO 9660 de dados PS1. Confira CUE, faixa de dados e integridade da imagem.");
        info.sectorSize = sector;
        info.dataOffset = prefix[15] == 1 ? 16U : 24U;
        info.availableSectors = image.size / sector;
        return info;
    }
    return basicInfo("unknown", "Formato nao reconhecido pelo conteudo (" + std::to_string(image.size) +
        " bytes). Pode ser outro console, imagem comprimida ou arquivo incompleto; a extensao sozinha nao comprova ISO PS1.");
}

struct Opened {
    DiscInfo info;
    std::optional<PsxExe> executable;
};

Opened openDisc(const std::filesystem::path& path) {
    const auto source = sourceFor(path);
    Image image(source.path);
    const auto candidate = detectLayout(image, source);
    if (!candidate) {
        auto info = signatureInfo(image);
        if (source.cue) {
            info.format = "cue";
            info.description = "CUE validado, mas a faixa de dados nao possui volume ISO 9660 reconhecido. " + info.description;
        }
        return {std::move(info), {}};
    }
    Volume volume(image, *candidate);
    DiscInfo info;
    info.format = source.cue ? "cue" : candidate->layout.sector == 2048 ? "iso9660" : "bin-raw";
    info.sectorSize = candidate->layout.sector;
    info.dataOffset = candidate->layout.offset;
    info.volumeId = volume.volumeId;
    info.firstSector = candidate->layout.first;
    info.declaredSectors = volume.declaredSectors();
    info.availableSectors = volume.availableSectors();
    info.warning = volume.warning;
    const auto layoutDescription = "ISO 9660; setor " + std::to_string(info.sectorSize) +
        " bytes, dados +" + std::to_string(info.dataOffset) + ", inicio " +
        std::to_string(candidate->layout.first) + ". ";
    info.description = (source.cue ? "CUE -> " : "") + layoutDescription + info.warning;
    if (volume.find("PSP_GAME")) {
        info.description += "Estrutura PSP_GAME reconhecida: disco PSP, nao PS1. Nucleo PSP ainda nao implementado.";
        return {std::move(info), {}};
    }
    const auto config = volume.find("SYSTEM.CNF");
    if (config) {
        if (config->directory) throw std::runtime_error("SYSTEM.CNF deveria ser arquivo, nao diretorio.");
        bool ps2 = false;
        const auto boot = bootFromConfig(volume.contents(*config, 64 * 1024), ps2);
        if (ps2) {
            info.description += "SYSTEM.CNF usa BOOT2 (PS2). O nucleo atual e PS1 e nao executa este disco.";
            return {std::move(info), {}};
        }
        if (!boot) {
            info.description += "SYSTEM.CNF nao possui BOOT PS1; nao e possivel localizar o executavel inicial.";
            return {std::move(info), {}};
        }
        info.bootPath = *boot;
    } else {
        if (!volume.find("PSX.EXE")) {
            info.description += "Sem SYSTEM.CNF e sem PSX.EXE na raiz; o volume nao fornece boot PS1 reconhecido.";
            return {std::move(info), {}};
        }
        info.bootPath = "PSX.EXE";
    }
    const auto boot = volume.find(info.bootPath);
    if (!boot || boot->directory) throw std::runtime_error("BOOT PS1 aponta para executavel inexistente: " + info.bootPath);
    auto executable = parsePsxExe(volume.contents(*boot, maxExecutable));
    info.ps1Boot = true;
    info.description += "BOOT PS1 " + info.bootPath + " validado. Carregamento direto experimental; BIOS, CD-ROM em tempo real e jogos comerciais ainda nao suportados.";
    return {std::move(info), std::move(executable)};
}
}

DiscInfo inspectDisc(const std::filesystem::path& path) {
    return openDisc(path).info;
}

DiscBoot loadPs1Disc(const std::filesystem::path& path) {
    auto opened = openDisc(path);
    if (!opened.info.ps1Boot || !opened.executable)
        throw std::runtime_error(opened.info.description);
    return {std::move(*opened.executable), std::move(opened.info)};
}
}
