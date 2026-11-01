#include "aurora/homebrew.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace aurora {
namespace {
constexpr std::uint32_t entry = 0x80010000;
enum Register : unsigned {
    zero = 0, v0 = 2, v1 = 3, a0 = 4,
    t0 = 8, t1 = 9, t2 = 10, t3 = 11, t4 = 12, t5 = 13, t6 = 14, t7 = 15,
    s0 = 16, s1 = 17, s2 = 18, s3 = 19, s4 = 20, s5 = 21, s6 = 22,
    sp = 29, ra = 31
};

class Assembler {
public:
    void word(std::uint32_t value) { code.push_back(value); }
    std::string unique(const std::string& prefix) { return prefix + std::to_string(serial_++); }
    void label(const std::string& name) {
        if (!labels.emplace(name, code.size()).second) throw std::logic_error("Label MIPS duplicado: " + name);
    }
    void r(unsigned function, unsigned rs, unsigned rt, unsigned rd, unsigned shift = 0) {
        word((rs << 21) | (rt << 16) | (rd << 11) | (shift << 6) | function);
    }
    void i(unsigned op, unsigned rs, unsigned rt, std::uint16_t immediate) {
        word((op << 26) | (rs << 21) | (rt << 16) | immediate);
    }
    void constant(unsigned reg, std::uint32_t value) {
        i(15, zero, reg, std::uint16_t(value >> 16));
        i(13, reg, reg, std::uint16_t(value));
    }
    void address(unsigned reg, const std::string& target) {
        fixups.push_back({Kind::address, code.size(), target});
        constant(reg, 0);
    }
    void move(unsigned destination, unsigned source) { r(0x21, source, zero, destination); }
    void add(unsigned destination, unsigned source, int value) {
        i(9, source, destination, std::uint16_t(value));
    }
    void load(unsigned op, unsigned reg, unsigned base, int offset) {
        i(op, base, reg, std::uint16_t(offset));
        word(0);
    }
    void store(unsigned op, unsigned reg, unsigned base, int offset) {
        i(op, base, reg, std::uint16_t(offset));
    }
    void branch(bool equal, unsigned first, unsigned second, const std::string& target) {
        fixups.push_back({Kind::branch, code.size(), target});
        i(equal ? 4 : 5, first, second, 0);
        word(0);
    }
    void jump(const std::string& target, bool link = false) {
        fixups.push_back({Kind::jump, code.size(), target});
        word((link ? 3u : 2u) << 26);
        word(0);
    }
    void call(const std::string& target) { jump(target, true); }
    void ret() { r(8, ra, zero, zero); word(0); }
    void saveReturn() { add(sp, sp, -4); store(0x2b, ra, sp, 0); }
    void restoreReturn() { load(0x23, ra, sp, 0); add(sp, sp, 4); ret(); }
    std::vector<std::uint32_t> finish() {
        for (const auto& fixup : fixups) {
            const auto found = labels.find(fixup.target);
            if (found == labels.end()) throw std::logic_error("Label MIPS ausente: " + fixup.target);
            const auto target = entry + std::uint32_t(found->second * 4);
            if (fixup.kind == Kind::branch) {
                const auto distance = std::int64_t(found->second) - std::int64_t(fixup.index) - 1;
                if (distance < std::numeric_limits<std::int16_t>::min() ||
                    distance > std::numeric_limits<std::int16_t>::max())
                    throw std::logic_error("Branch MIPS fora do alcance");
                code.at(fixup.index) |= std::uint16_t(distance);
            } else if (fixup.kind == Kind::jump) {
                code.at(fixup.index) |= (target >> 2) & 0x03ffffffu;
            } else {
                code.at(fixup.index) |= target >> 16;
                code.at(fixup.index + 1) |= target & 0xffff;
            }
        }
        return code;
    }
private:
    enum class Kind { branch, jump, address };
    struct Fixup { Kind kind; std::size_t index; std::string target; };
    std::vector<std::uint32_t> code;
    std::map<std::string, std::size_t> labels;
    std::vector<Fixup> fixups;
    unsigned serial_ = 0;
};

using Stream = std::vector<std::uint32_t>;
void rectangle(Stream& stream, std::uint32_t rgb, unsigned x, unsigned y, unsigned width, unsigned height) {
    stream.push_back(0x60000000u | rgb);
    stream.push_back((y << 16) | x);
    stream.push_back((height << 16) | width);
}

std::array<unsigned, 5> glyph(char c) {
    switch (c) {
    case 'A': return {2,5,7,5,5};
    case 'C': return {7,4,4,4,7};
    case 'D': return {6,5,5,5,6};
    case 'E': return {7,4,6,4,7};
    case 'F': return {7,4,6,4,4};
    case 'G': return {7,4,5,5,7};
    case 'H': return {5,5,7,5,5};
    case 'I': return {7,2,2,2,7};
    case 'J': return {1,1,1,5,7};
    case 'L': return {4,4,4,4,7};
    case 'M': return {5,7,7,5,5};
    case 'N': return {5,7,7,7,5};
    case 'O': return {7,5,5,5,7};
    case 'P': return {6,5,6,4,4};
    case 'R': return {6,5,6,5,5};
    case 'S': return {7,4,7,1,7};
    case 'T': return {7,2,2,2,2};
    case 'U': return {5,5,5,5,7};
    case 'V': return {5,5,5,5,2};
    case '5': return {7,4,7,1,7};
    case '!': return {2,2,2,0,2};
    case ':': return {0,2,0,2,0};
    case ' ': return {0,0,0,0,0};
    default: throw std::logic_error("Glifo proprio nao definido: " + std::string(1, c));
    }
}
void text(Stream& stream, const std::string& value, unsigned x, unsigned y,
          unsigned scale, std::uint32_t color) {
    for (char c : value) {
        const auto rows = glyph(c);
        for (unsigned row = 0; row < 5; ++row)
            for (unsigned column = 0; column < 3; ++column)
                if (rows[row] & (4u >> column))
                    rectangle(stream, color, x + column * scale, y + row * scale, scale, scale);
        x += 4 * scale;
    }
}
void dataStream(Assembler& a, const std::string& name, const Stream& stream) {
    a.label(name);
    a.word(std::uint32_t(stream.size()));
    for (auto word : stream) a.word(word);
}
void gpuWord(Assembler& a, unsigned reg) {
    const auto ready = a.unique("gpu_ready_");
    a.label(ready);
    a.load(0x23, t6, s1, 4);
    a.r(2, zero, t6, t6, 26);
    a.i(0x0c, t6, t6, 1);
    a.branch(true, t6, zero, ready);
    a.store(0x2b, reg, s1, 0);
}
void sendGpu(Assembler& a, std::uint32_t value, int offset = 0) {
    a.constant(t0, value);
    if (offset == 0) gpuWord(a, t0);
    else a.store(0x2b, t0, s1, offset);
}
void drawRectangle(Assembler& a, unsigned x, unsigned y, unsigned width, unsigned height,
                   std::uint32_t color) {
    a.constant(t0, x);
    a.constant(t1, y);
    a.constant(t2, width);
    a.constant(t3, height);
    a.constant(t4, color);
    a.call("draw_rectangle");
}
void drawAt(Assembler& a, unsigned xReg, unsigned yReg, unsigned width, unsigned height,
            std::uint32_t color) {
    a.move(t0, xReg);
    a.move(t1, yReg);
    a.constant(t2, width);
    a.constant(t3, height);
    a.constant(t4, color);
    a.call("draw_rectangle");
}
void selectCrystal(Assembler& a) {
    a.load(0x23, t0, s0, HomebrewScore);
    a.r(0, zero, t0, t0, 3);
    a.address(t1, "crystal_positions");
    a.r(0x21, t1, t0, t1);
    a.load(0x23, s5, t1, 0);
    a.load(0x23, s6, t1, 4);
}
void rejectNonOverlap(Assembler& a, unsigned x1, unsigned y1, unsigned x2, unsigned y2,
                      const std::string& reject) {
    a.add(t5, x2, 16);
    a.r(0x2b, x1, t5, t6);
    a.branch(true, t6, zero, reject);
    a.add(t5, x1, 16);
    a.r(0x2b, x2, t5, t6);
    a.branch(true, t6, zero, reject);
    a.add(t5, y2, 16);
    a.r(0x2b, y1, t5, t6);
    a.branch(true, t6, zero, reject);
    a.add(t5, y1, 16);
    a.r(0x2b, y2, t5, t6);
    a.branch(true, t6, zero, reject);
}
void clamp(Assembler& a, unsigned reg, unsigned minimum, unsigned maximum, const std::string& prefix) {
    a.i(0x0a, reg, t2, std::uint16_t(minimum));
    a.branch(true, t2, zero, prefix + "_low_ok");
    a.constant(reg, minimum);
    a.label(prefix + "_low_ok");
    a.i(0x0a, reg, t2, std::uint16_t(maximum + 1));
    a.branch(false, t2, zero, prefix + "_high_ok");
    a.constant(reg, maximum);
    a.label(prefix + "_high_ok");
}
void store32(std::vector<std::uint8_t>& bytes, std::size_t position, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(position + i) = std::uint8_t(value >> (8 * i));
}
}

std::vector<std::uint8_t> makeHomebrewPsxExe() {
    Assembler a;
    a.constant(sp, 0x801fff00);
    a.constant(s0, HomebrewStateAddress);
    a.constant(s1, 0x1f801810);
    a.constant(s2, 0x1f801070);
    a.constant(s3, 0x1f801040);
    a.constant(s4, 0);
    a.store(0x2b, zero, s2, 4);
    sendGpu(a, 0x00000000, 4);
    sendGpu(a, 0x05000000, 4);
    sendGpu(a, 0x06000000 | (0xc60 << 12) | 0x260, 4);
    sendGpu(a, 0x07000000 | (0x100 << 10) | 0x10, 4);
    sendGpu(a, 0x08000001, 4);
    sendGpu(a, 0x03000000, 4);
    sendGpu(a, 0xe3000000);
    sendGpu(a, 0xe4000000 | (239 << 10) | 319);
    sendGpu(a, 0xe5000000);
    sendGpu(a, 0xe6000000);
    a.constant(t0, 0x40);
    a.store(0x29, t0, s3, 10);
    a.constant(t0, 0x0d);
    a.store(0x29, t0, s3, 8);
    a.constant(t0, 0x88);
    a.store(0x29, t0, s3, 14);
    a.store(0x29, zero, s3, 10);
    a.call("reset_game");

    a.label("wait_frame");
    a.load(0x23, t0, s2, 0);
    a.i(0x0c, t0, t0, 1);
    a.branch(true, t0, zero, "wait_frame");
    a.store(0x2b, zero, s2, 0);
    a.call("read_pad");
    a.load(0x23, t0, s0, HomebrewOldButtons);
    a.store(0x2b, s4, s0, HomebrewOldButtons);
    a.r(0x27, t0, zero, t1);
    a.r(0x24, t1, s4, t1);
    a.i(0x0c, t1, t1, 8);
    a.branch(true, t1, zero, "no_restart");
    a.call("reset_game");
    a.jump("finish_frame");
    a.label("no_restart");
    a.load(0x23, t0, s0, HomebrewPhase);
    a.branch(false, t0, zero, "finish_frame");
    a.load(0x23, t0, s0, HomebrewPlayerX);
    a.load(0x23, t1, s0, HomebrewPlayerY);
    for (const auto& direction : std::array<std::pair<unsigned, int>, 4>{{{0x10, -2}, {0x40, 2}, {0x80, -2}, {0x20, 2}}}) {
        const auto next = "movement_" + std::to_string(direction.first);
        a.i(0x0c, s4, t2, std::uint16_t(direction.first));
        a.branch(true, t2, zero, next);
        const auto reg = direction.first == 0x10 || direction.first == 0x40 ? t1 : t0;
        a.add(reg, reg, direction.second);
        a.label(next);
    }
    clamp(a, t0, 16, 288, "player_x");
    clamp(a, t1, 40, 208, "player_y");
    a.store(0x2b, t0, s0, HomebrewPlayerX);
    a.store(0x2b, t1, s0, HomebrewPlayerY);
    a.load(0x23, t2, s0, HomebrewEnemyX);
    a.load(0x23, t3, s0, HomebrewEnemyDirection);
    a.r(0, zero, t3, t3, 1);
    a.r(0x21, t2, t3, t2);
    a.i(0x0a, t2, t4, 16);
    a.branch(true, t4, zero, "enemy_not_left");
    a.constant(t2, 16);
    a.constant(t3, 1);
    a.store(0x2b, t3, s0, HomebrewEnemyDirection);
    a.label("enemy_not_left");
    a.i(0x0a, t2, t4, 289);
    a.branch(false, t4, zero, "enemy_in_bounds");
    a.constant(t2, 288);
    a.constant(t3, 0xffffffffu);
    a.store(0x2b, t3, s0, HomebrewEnemyDirection);
    a.label("enemy_in_bounds");
    a.store(0x2b, t2, s0, HomebrewEnemyX);
    a.constant(t3, HomebrewEnemyY);
    rejectNonOverlap(a, t0, t1, t2, t3, "no_enemy_collision");
    a.constant(t0, HomebrewLost);
    a.store(0x2b, t0, s0, HomebrewPhase);
    a.jump("finish_frame");
    a.label("no_enemy_collision");
    selectCrystal(a);
    a.load(0x23, t0, s0, HomebrewPlayerX);
    a.load(0x23, t1, s0, HomebrewPlayerY);
    rejectNonOverlap(a, t0, t1, s5, s6, "finish_frame");
    a.load(0x23, t0, s0, HomebrewScore);
    a.add(t0, t0, 1);
    a.store(0x2b, t0, s0, HomebrewScore);
    a.constant(t1, std::uint32_t(HomebrewCrystals.size()));
    a.branch(false, t0, t1, "finish_frame");
    a.constant(t0, HomebrewWon);
    a.store(0x2b, t0, s0, HomebrewPhase);
    a.label("finish_frame");
    a.load(0x23, t0, s0, HomebrewFrameCounter);
    a.add(t0, t0, 1);
    a.store(0x2b, t0, s0, HomebrewFrameCounter);
    a.call("render_frame");
    a.jump("wait_frame");

    a.label("reset_game");
    a.constant(t0, HomebrewInitialPlayerX);
    a.store(0x2b, t0, s0, HomebrewPlayerX);
    a.constant(t0, HomebrewInitialPlayerY);
    a.store(0x2b, t0, s0, HomebrewPlayerY);
    a.store(0x2b, zero, s0, HomebrewScore);
    a.store(0x2b, zero, s0, HomebrewPhase);
    a.constant(t0, HomebrewInitialEnemyX);
    a.store(0x2b, t0, s0, HomebrewEnemyX);
    a.constant(t0, 1);
    a.store(0x2b, t0, s0, HomebrewEnemyDirection);
    a.store(0x2b, s4, s0, HomebrewOldButtons);
    a.store(0x2b, zero, s0, HomebrewFrameCounter);
    a.ret();

    a.label("read_pad");
    a.saveReturn();
    a.store(0x29, zero, s3, 10);
    a.constant(t0, 3);
    a.store(0x29, t0, s3, 10);
    for (unsigned command : {1u, 0x42u, 0u}) {
        a.constant(a0, command);
        a.call("pad_byte");
    }
    a.constant(a0, 0);
    a.call("pad_byte");
    a.move(t7, v0);
    a.call("pad_byte");
    a.r(0, zero, v0, v0, 8);
    a.r(0x25, t7, v0, s4);
    a.i(0x0e, s4, s4, 0xffff);
    a.store(0x29, zero, s3, 10);
    a.restoreReturn();

    a.label("pad_byte");
    a.label("pad_wait_tx");
    a.load(0x23, v0, s3, 4);
    a.i(0x0c, v0, v0, 1);
    a.branch(true, v0, zero, "pad_wait_tx");
    a.store(0x28, a0, s3, 0);
    a.label("pad_wait_rx");
    a.load(0x23, v0, s3, 4);
    a.i(0x0c, v0, v0, 2);
    a.branch(true, v0, zero, "pad_wait_rx");
    a.load(0x24, v0, s3, 0);
    a.constant(v1, 128);
    a.label("pad_settle");
    a.add(v1, v1, -1);
    a.branch(false, v1, zero, "pad_settle");
    a.ret();

    a.label("draw_rectangle");
    a.constant(t5, 0x60000000);
    a.r(0x25, t5, t4, t5);
    gpuWord(a, t5);
    a.r(0, zero, t1, t5, 16);
    a.r(0x25, t5, t0, t5);
    gpuWord(a, t5);
    a.r(0, zero, t3, t5, 16);
    a.r(0x25, t5, t2, t5);
    gpuWord(a, t5);
    a.ret();

    a.label("draw_stream");
    a.load(0x23, t0, a0, 0);
    a.add(a0, a0, 4);
    a.label("draw_stream_word");
    a.branch(true, t0, zero, "draw_stream_done");
    a.load(0x23, t1, a0, 0);
    gpuWord(a, t1);
    a.add(a0, a0, 4);
    a.add(t0, t0, -1);
    a.jump("draw_stream_word");
    a.label("draw_stream_done");
    a.ret();

    a.label("render_frame");
    a.saveReturn();
    a.address(a0, "background_stream");
    a.call("draw_stream");
    a.load(0x23, s5, s0, HomebrewEnemyX);
    a.constant(s6, HomebrewEnemyY);
    drawAt(a, s5, s6, 16, 16, 0x00605be8);
    a.add(s5, s5, 3);
    a.add(s6, s6, 4);
    drawAt(a, s5, s6, 3, 3, 0x00eef4ff);
    a.add(s5, s5, 7);
    drawAt(a, s5, s6, 3, 3, 0x00eef4ff);
    a.load(0x23, t0, s0, HomebrewScore);
    a.i(0x0b, t0, t1, 5);
    a.branch(true, t1, zero, "render_no_crystal");
    selectCrystal(a);
    a.add(s5, s5, 2);
    a.add(s6, s6, 2);
    drawAt(a, s5, s6, 12, 12, 0x004fd9ff);
    a.add(s5, s5, 4);
    a.add(s6, s6, 4);
    drawAt(a, s5, s6, 4, 4, 0x00d5f5ff);
    a.label("render_no_crystal");
    a.load(0x23, s5, s0, HomebrewPlayerX);
    a.load(0x23, s6, s0, HomebrewPlayerY);
    drawAt(a, s5, s6, 16, 16, 0x00b6df72);
    a.add(s5, s5, 9);
    a.add(s6, s6, 4);
    drawAt(a, s5, s6, 4, 4, 0x002b2610);
    for (unsigned pip = 0; pip < HomebrewCrystals.size(); ++pip) {
        a.load(0x23, t5, s0, HomebrewScore);
        a.i(0x0b, t5, t6, std::uint16_t(pip + 1));
        a.branch(true, t6, zero, "pip_collected_" + std::to_string(pip));
        drawRectangle(a, 200 + pip * 20, 12, 12, 8, 0x0051473c);
        a.jump("pip_done_" + std::to_string(pip));
        a.label("pip_collected_" + std::to_string(pip));
        drawRectangle(a, 200 + pip * 20, 12, 12, 8, 0x004fd9ff);
        a.label("pip_done_" + std::to_string(pip));
    }
    a.load(0x23, t0, s0, HomebrewPhase);
    a.constant(t1, HomebrewWon);
    a.branch(true, t0, t1, "render_win");
    a.constant(t1, HomebrewLost);
    a.branch(true, t0, t1, "render_lost");
    a.jump("render_done");
    a.label("render_win");
    a.address(a0, "win_stream");
    a.call("draw_stream");
    a.jump("render_done");
    a.label("render_lost");
    a.address(a0, "lost_stream");
    a.call("draw_stream");
    a.label("render_done");
    a.restoreReturn();

    Stream background;
    rectangle(background, 0x00201610, 0, 0, 320, 240);
    rectangle(background, 0x00392c24, 8, 32, 304, 2);
    rectangle(background, 0x00392c24, 8, 222, 304, 2);
    rectangle(background, 0x00392c24, 8, 32, 2, 192);
    rectangle(background, 0x00392c24, 310, 32, 2, 192);
    for (unsigned y = 48; y <= 208; y += 32)
        for (unsigned x = 32; x <= 288; x += 32)
            rectangle(background, 0x00392c24, x, y, 1, 1);
    rectangle(background, 0x002b182f, 16, HomebrewEnemyY + 7, 288, 2);
    text(background, "AURORA COLETA", 16, 8, 2, 0x00edeee5);
    text(background, "COLETE 5", 16, 228, 2, 0x00a5b3b0);
    text(background, "EVITE O VERMELHO", 168, 228, 2, 0x00a5b3b0);
    dataStream(a, "background_stream", background);
    Stream win;
    rectangle(win, 0x004fd9ff, 38, 80, 244, 82);
    rectangle(win, 0x00352d19, 40, 82, 240, 78);
    text(win, "VOCE VENCEU!", 88, 96, 3, 0x004fd9ff);
    text(win, "START REINICIA", 104, 132, 2, 0x00edeee5);
    dataStream(a, "win_stream", win);
    Stream lost;
    rectangle(lost, 0x00605be8, 38, 80, 244, 82);
    rectangle(lost, 0x00352d19, 40, 82, 240, 78);
    text(lost, "FIM DE JOGO", 94, 96, 3, 0x00605be8);
    text(lost, "START REINICIA", 104, 132, 2, 0x00edeee5);
    dataStream(a, "lost_stream", lost);
    a.label("crystal_positions");
    for (const auto& position : HomebrewCrystals) { a.word(position.x); a.word(position.y); }

    const auto words = a.finish();
    const auto payloadSize = (words.size() * 4 + 0x7ff) & ~std::size_t(0x7ff);
    if (std::uint64_t(entry & 0x1fffffff) + payloadSize >= (HomebrewStateAddress & 0x1fffffff))
        throw std::logic_error("Programa caseiro sobrepoe a area de estado");
    std::vector<std::uint8_t> result(0x800 + payloadSize);
    const std::array<std::uint8_t, 8> magic{'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
    std::copy(magic.begin(), magic.end(), result.begin());
    store32(result, 0x10, entry);
    store32(result, 0x18, entry);
    store32(result, 0x1c, std::uint32_t(payloadSize));
    store32(result, 0x30, 0x801fff00);
    for (std::size_t index = 0; index < words.size(); ++index) store32(result, 0x800 + index * 4, words[index]);
    return result;
}
}
