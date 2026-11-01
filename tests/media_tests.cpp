#include "aurora/media.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void save(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!output) throw std::runtime_error("failed to write fixture");
}

void saveText(const std::filesystem::path& path, const std::string& text) {
    save(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

void put32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.at(offset + i) = std::uint8_t(value >> (i * 8));
}

template<class Function>
void rejects(Function operation, const std::string& message) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
}

int main() {
    const auto unique = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("aurora-media-test-" + unique);
    std::filesystem::create_directories(root / "disc");
    unsigned checks = 0;
    try {
        const auto exePath = root / "demo.bin";
        const auto demo = aurora::makeDemoPsxExe();
        save(exePath, demo);
        const auto loaded = aurora::loadPsxExe(exePath);
        require(loaded.entry == 0x80010000 && loaded.loadAddress == 0x80010000 &&
                loaded.stackBase == 0x801fff00 && loaded.data.size() == 0x800,
                "demo PS-X EXE header/payload"); ++checks;
        const auto identifiedExe = aurora::inspectMedia(exePath);
        require(identifiedExe.executable && identifiedExe.kind == "psx-exe", "magic must determine EXE, not BIN extension"); ++checks;

        auto corrupt = demo;
        corrupt.resize(0x820);
        save(root / "truncated.exe", corrupt);
        rejects([&] { aurora::loadPsxExe(root / "truncated.exe"); }, "truncated EXE payload accepted"); ++checks;
        rejects([&] { aurora::inspectMedia(root / "truncated.exe"); }, "inspection marked truncated EXE executable"); ++checks;
        corrupt = demo;
        put32(corrupt, 0x1c, 0xffffffff);
        save(root / "size-overflow.exe", corrupt);
        rejects([&] { aurora::loadPsxExe(root / "size-overflow.exe"); }, "overflow EXE payload size accepted"); ++checks;
        corrupt = demo;
        put32(corrupt, 0x18, 0x801fff00);
        save(root / "ram-overflow.exe", corrupt);
        rejects([&] { aurora::loadPsxExe(root / "ram-overflow.exe"); }, "EXE outside RAM accepted"); ++checks;
        corrupt = demo;
        put32(corrupt, 0x10, 0x80010001);
        save(root / "entry-alignment.exe", corrupt);
        rejects([&] { aurora::loadPsxExe(root / "entry-alignment.exe"); }, "unaligned EXE entry accepted"); ++checks;
        corrupt = demo;
        put32(corrupt, 0x18, 0x80010001);
        save(root / "load-alignment.exe", corrupt);
        rejects([&] { aurora::loadPsxExe(root / "load-alignment.exe"); }, "unaligned EXE load address accepted"); ++checks;
        corrupt = demo;
        put32(corrupt, 0x18, 0x20010000);
        save(root / "invalid-segment.exe", corrupt);
        rejects([&] { aurora::loadPsxExe(root / "invalid-segment.exe"); }, "invalid RAM segment accepted"); ++checks;
        corrupt = demo;
        put32(corrupt, 0x30, 0xfffffff0);
        put32(corrupt, 0x34, 0x40);
        save(root / "stack-overflow.exe", corrupt);
        rejects([&] { aurora::loadPsxExe(root / "stack-overflow.exe"); }, "overflow stack accepted"); ++checks;

        save(root / "package.pkg", {0x7f, 'P', 'K', 'G', 0, 0, 0, 0});
        const auto pkg = aurora::inspectMedia(root / "package.pkg");
        require(pkg.kind == "pkg" && !pkg.executable, "PKG must be recognized without being executable"); ++checks;
        save(root / "fake.pkg", {1, 2, 3, 4});
        require(aurora::inspectMedia(root / "fake.pkg").kind == "unknown", "PKG extension trusted without magic"); ++checks;

        std::vector<std::uint8_t> iso(17 * 2048);
        const std::vector<std::uint8_t> pvd{1, 'C', 'D', '0', '0', '1', 1};
        std::copy(pvd.begin(), pvd.end(), iso.begin() + 16 * 2048);
        save(root / "disc.iso", iso);
        rejects([&] { aurora::inspectMedia(root / "disc.iso"); },
                "PVD signature without valid volume/root must be rejected"); ++checks;
        iso[16 * 2048 + 1] = 'X';
        save(root / "false.iso", iso);
        require(aurora::inspectMedia(root / "false.iso").kind == "unknown", "invalid PVD accepted"); ++checks;

        std::vector<std::uint8_t> raw(17 * 2352, 0);
        for (unsigned i = 1; i <= 10; ++i) raw[i] = 0xff;
        raw[15] = 1;
        std::copy(pvd.begin(), pvd.end(), raw.begin() + 16 * 2352 + 16);
        save(root / "disc" / "track.bin", raw);
        rejects([&] { aurora::inspectMedia(root / "disc" / "track.bin"); },
                "raw PVD signature with corrupt volume must be rejected"); ++checks;
        std::fill(raw.begin() + 16 * 2352, raw.end(), 0);
        save(root / "disc" / "track.bin", raw);
        raw[15] = 2;
        std::fill(raw.begin() + 16 * 2352, raw.begin() + 17 * 2352, 0);
        std::copy(pvd.begin(), pvd.end(), raw.begin() + 16 * 2352 + 24);
        save(root / "mode2.bin", raw);
        rejects([&] { aurora::inspectMedia(root / "mode2.bin"); },
                "mode2 PVD signature with corrupt volume must be rejected"); ++checks;
        std::fill(raw.begin() + 16 * 2352, raw.end(), 0);
        save(root / "mode2.bin", raw);
        save(root / "raw-truncated.bin", std::vector<std::uint8_t>(raw.begin(), raw.begin() + 16));
        rejects([&] { aurora::inspectMedia(root / "raw-truncated.bin"); }, "truncated raw sector accepted"); ++checks;

        const auto cuePath = root / "disc" / "disc.cue";
        saveText(cuePath, "FILE \"track.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
        const auto cue = aurora::inspectMedia(cuePath);
        require(cue.kind == "cue" && !cue.executable, "valid CUE parse/launch boundary"); ++checks;
        save(root / "disc" / "mode2.bin", raw);
        saveText(cuePath, "FILE \"mode2.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
        const auto mode2Cue = aurora::inspectMedia(cuePath);
        require(mode2Cue.kind == "cue" && !mode2Cue.executable, "MODE2/2352 CUE recognition/launch boundary"); ++checks;
#ifdef _WIN32
        saveText(cuePath, "FILE \"../DISC/TRACK.BIN\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
        require(aurora::inspectMedia(cuePath).kind == "cue", "case-insensitive Windows path containment"); ++checks;
#endif
        saveText(cuePath, "FILE \"../mode2.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE parent traversal accepted"); ++checks;
        saveText(cuePath, "FILE \"C:\\outside.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE Windows absolute path accepted"); ++checks;
        saveText(cuePath, "FILE \"missing.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE missing file accepted"); ++checks;
        saveText(cuePath, "FILE \"track.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:74\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE out-of-file sector accepted"); ++checks;
        saveText(cuePath, "FILE \"track.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:60:00\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE invalid seconds accepted"); ++checks;
        saveText(cuePath, "FILE \"track.bin\" BINARY\nTRACK 01 MODE3/2352\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "unsupported CUE mode accepted"); ++checks;
        saveText(cuePath, "FILE \"track.bin\" BINARY\nTRACK 01 MODE1/2352\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE missing INDEX01 accepted"); ++checks;
        saveText(cuePath, "FILE \"track.bin\" BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE invalid track order accepted"); ++checks;

#ifndef _WIN32
        std::filesystem::create_symlink(root / "mode2.bin", root / "disc" / "outside.bin");
        saveText(cuePath, "FILE \"outside.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n");
        rejects([&] { aurora::inspectMedia(cuePath); }, "CUE symlink escape accepted"); ++checks;
#endif
        rejects([&] { aurora::inspectMedia(root / "missing.iso"); }, "missing media accepted"); ++checks;
        std::filesystem::remove_all(root);
        std::cout << "media: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "media test failed: " << error.what() << '\n';
        return 1;
    }
}
