#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "md5.hpp"

#define USE_DUMB_COLL_GENERATOR 0

constexpr int COLLISION_LEN = 128;
constexpr int COLLISION_LAST_DIFF = 123;

using byte_seq = std::vector<uint8_t>;
using string_pair = std::pair<byte_seq, byte_seq>;

void operator+=(byte_seq &lhs, const byte_seq &rhs) { lhs.insert(lhs.end(), rhs.begin(), rhs.end()); }

template <uint8_t value> struct PAD {
    PAD(size_t sz) : count{sz} {};
    size_t count = 0;
};
template <uint8_t value> void operator+=(byte_seq &lhs, PAD<value> pad) {
    lhs.resize(lhs.size() + pad.count);
    auto last = lhs.size() - 1;
    for (int i = 0; i < pad.count; i++)
        lhs[last - i] = value;
}

uint32_t seed32_1, seed32_2;
inline uint32_t xrng64() {
    uint32_t t = seed32_1 ^ (seed32_1 << 10);
    seed32_1 = seed32_2;
    seed32_2 = (seed32_2 ^ (seed32_2 >> 10)) ^ (t ^ (t >> 13));
    return seed32_1;
}

string_pair collide(byte_seq &prefix) {
/*
    This function -- wrapper for util for generation
    md5 collision.

    Download and compile it from http://www.win.tue.nl/hashclash/
*/
#if !USE_DUMB_COLL_GENERATOR
    string_pair answ;
    {
        std::fstream out(".prefix", std::ios::out | std::ios::binary);
        out.write(reinterpret_cast<char *>(prefix.data()), prefix.size());
    }
    int ret = system("./fastcoll -p .prefix -o .col1 .col2 1> /dev/null");
    if (ret != 0) {
        std::cerr << "fastcoll failed, exit!\n";
        exit(1);
    }
    std::fstream col1(".col1", std::ios::in | std::ios::binary);
    std::fstream col2(".col2", std::ios::in | std::ios::binary);

    auto N = std::filesystem::file_size(".col1");
    auto N_cpy = std::filesystem::file_size(".col2");

    assert(N == N_cpy);

    col1.seekg(prefix.size());
    col2.seekg(prefix.size());

    N -= prefix.size();
    answ.first.resize(N);
    answ.second.resize(N);
    col1.read(reinterpret_cast<char *>(answ.first.data()), N);
    col2.read(reinterpret_cast<char *>(answ.second.data()), N);

    if (answ.first[COLLISION_LAST_DIFF] >= answ.second[COLLISION_LAST_DIFF])
        std::swap(answ.first, answ.second);
    return answ;
#else
    string_pair answ;
    answ.first.resize(128);
    answ.second.resize(128);
    for (int i = 0; i < 128; i++) {
        answ.first[i] = xrng64();
        answ.second[i] = xrng64();
    }
    return answ;
#endif
}

auto read_gif(std::string filename) {
    std::map<std::string, byte_seq> blocks;
    FILE *gif_fd = fopen(filename.c_str(), "rb");

    byte_seq buf;
    auto read_bytes = [&](int count) {
        buf.resize(count);
        fread(buf.data(), sizeof(uint8_t), count, gif_fd);
        return buf;
    };

    blocks["header"] = read_bytes(6);
    blocks["lcd"] = read_bytes(7);
    blocks["gct"] = read_bytes(16 * 3);
    blocks["img_descriptor"] = read_bytes(10);

    blocks["img_data"] = read_bytes(1);
    while (true) {
        auto &block = blocks["img_data"];
        block += read_bytes(1);
        if (block.back() == 0)
            break;
        block += read_bytes(block.back());
    }
    fclose(gif_fd);

    return blocks;
}

byte_seq generate() {
    // position of md5 hash in generated gif
    uint16_t top = 102;
    uint16_t left = 0;

    byte_seq control_extention{0x21, 0xf9, 0x04, 0x04, 0x02, 0x00, 0x00, 0x00};
    uint16_t char_width;
    uint16_t char_height;

    // read initial gifs
    auto background = read_gif("background.gif");

    std::vector<byte_seq> chars_img_data(16);
    constexpr std::string_view HEX_DIGITS = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        std::string filename = "template/char_";
        filename += HEX_DIGITS[i];
        filename += ".gif";
        auto blocks = read_gif(filename);
        chars_img_data[i] = blocks["img_data"];
        auto &desc = blocks["img_descriptor"];
        char_width = desc[5] | (desc[6] << 8);
        char_height = desc[7] | (desc[8] << 8);
    }

    // (char_pos, char) -> (coll_pos, coll)
    std::map<std::pair<int, int>, std::pair<int, byte_seq>> alternatives;

    // header
    byte_seq generated_gif = background["header"];
    generated_gif += background["lcd"];
    generated_gif += background["gct"];

    // background
    generated_gif += control_extention;
    generated_gif += background["img_descriptor"];
    generated_gif += background["img_data"];

    // start comment
    generated_gif += byte_seq{0x21, 0xfe};

    for (int char_pos = 0; char_pos < 32; char_pos++) {
        for (int cha = 0; cha < 16; cha++) {
            byte_seq char_img = control_extention;
            char_img.push_back(0x2c);
#define push_u16(value)                                                                                                \
    char_img.push_back(value & 0xFF);                                                                                  \
    char_img.push_back(value >> 8);
            push_u16(left);
            push_u16(top);
            push_u16(char_width);
            push_u16(char_height);
#undef push_u16
            char_img.push_back(0x00);

            char_img += chars_img_data[cha];

            auto coll_diff = COLLISION_LAST_DIFF;
            auto align = 64 - generated_gif.size() % 64;
            generated_gif.push_back(align - 1 + coll_diff);
            generated_gif += PAD<0x00>(align - 1);

            byte_seq coll_img;
            byte_seq coll_nop;
            int coll_p_img, pad_len;
            while (true) {
                std::cerr << "Generate collision " << char_pos * 16 + cha + 1 << std::endl;
                auto [coll_img_tmp, coll_nop_tmp] = collide(generated_gif);
                coll_img = coll_img_tmp;
                coll_nop = coll_nop_tmp;

                auto offset = COLLISION_LEN - coll_diff - 1;
                coll_p_img = coll_img[coll_diff] - offset;
                auto coll_p_nop = coll_nop[coll_diff] - offset;
                pad_len = coll_p_nop - coll_p_img - char_img.size() - 4;
                if (coll_p_img >= 0 && pad_len >= 0) {
                    break;
                }
                std::cerr << "Unsatisfying collision, trying again\n";
            }

            alternatives[{char_pos, cha}] = {generated_gif.size(), coll_img};

            generated_gif += coll_nop;
            generated_gif += PAD<0x00>(coll_p_img);
            generated_gif.push_back(0x00); // end comment

            // insert char image
            generated_gif += char_img;

            // start new comment
            generated_gif.push_back(0x21);
            generated_gif.push_back(0xfe);
            generated_gif.push_back(pad_len);
            generated_gif += PAD<0x00>(pad_len);
        }
        left += char_width;
    }

    auto result_hash = [&]() {
        auto hash = MD5()(generated_gif.data(), generated_gif.size());
        byte_seq result_hash(hash.size(), 0);
        for (int i = 0; i < hash.size(); i++) {
            result_hash[i] = HEX_DIGITS.find(std::tolower(hash[i]), 0);
        }
        std::cout << "md5 = " << hash << std::endl;
        return result_hash;
    }();

    for (int char_pos = 0; char_pos < result_hash.size(); char_pos++) {
        int cha = result_hash[char_pos];
        auto [coll_pos, coll] = alternatives[{char_pos, cha}];
        std::memcpy(generated_gif.data() + coll_pos, coll.data(), coll.size());
    }

    return generated_gif;
}

int main() {

    seed32_1 = time(NULL);
    seed32_2 = 0x12345678;

    auto generated_gif = generate();

    FILE *out = fopen("hashquine.gif", "wb");
    fwrite(generated_gif.data(), sizeof(uint8_t), generated_gif.size(), out);
    fclose(out);

    return 0;
}