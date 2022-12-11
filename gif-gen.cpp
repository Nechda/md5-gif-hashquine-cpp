#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <map>


#include <boost/uuid/detail/md5.hpp>
#include <boost/algorithm/hex.hpp>

using boost::uuids::detail::md5;

std::string toString(const md5::digest_type &digest)
{
    const auto charDigest = reinterpret_cast<const char *>(&digest);
    std::string result;
    boost::algorithm::hex(charDigest, charDigest + sizeof(md5::digest_type), std::back_inserter(result));
    return result;
}

constexpr int COLLISION_LEN = 128;
constexpr int COLLISION_LAST_DIFF = 123;

using byte_seq = std::vector<uint8_t>;
using string_pair = std::pair<byte_seq, byte_seq>;

string_pair collide(byte_seq &prefix) {
    string_pair answ;
    {
        std::fstream out(".prefix", std::ios::out | std::ios::binary);
        out.write(reinterpret_cast<char*>(prefix.data()), prefix.size());
    }
    int ret = system("./fastcoll -p .prefix -o .col1 .col2 1> /dev/null");
    if(ret != 0) {
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
    col1.read(reinterpret_cast<char*>(answ.first.data()), N);
    col2.read(reinterpret_cast<char*>(answ.second.data()), N);

    if(answ.first[COLLISION_LAST_DIFF] >= answ.second[COLLISION_LAST_DIFF])
        std::swap(answ.first, answ.second);
    return answ;
}

auto read_gif(std::string filename) {
    std::unordered_map<std::string, byte_seq> blocks;
    FILE* gif_fd = fopen(filename.c_str(), "rb");
    
    byte_seq buf;
    auto read_bytes = [&](int count) {
        buf.resize(count);
        fread(buf.data(), sizeof(uint8_t), count, gif_fd);
        return buf;
    };

    blocks["header"] = read_bytes(6);
    blocks["lcd"] = read_bytes(7);
    blocks["gct"] = read_bytes(16*3);
    blocks["img_descriptor"] = read_bytes(10);

    blocks["img_data"] = read_bytes(1);
    while(true) {
        auto tmp = read_bytes(1);
        auto &a = blocks["img_data"];
        auto &b = tmp;
        a.insert(a.end(), b.begin(), b.end()); // concat vectors
        if(a.back() == 0) break;

        tmp = read_bytes(a.back());
        a.insert(a.end(), b.begin(), b.end()); // concat vectors
    }
    fclose(gif_fd);

    return blocks;
}

void generate() {
    byte_seq control_extention{0x21, 0xf9, 0x04, 0x04, 0x02, 0x00, 0x00, 0x00};
    uint16_t char_width;
    uint16_t char_height;

    std::vector<byte_seq> chars_img_data(16);
    constexpr std::string_view HEX_DIGITS = "0123456789abcdef";
    for(int i = 0; i < 16; i++) {
        std::string filename = "template/char_";
        filename += HEX_DIGITS[i];
        filename += ".gif";
        auto blocks = read_gif(filename);
        chars_img_data[i] = blocks["img_data"];
        auto &desc = blocks["img_descriptor"];
        char_width = (desc[5] << 8) | desc[6];
        char_height = (desc[7] << 8) | desc[8];
    }

    
    // (char_pos, char): (coll_pos, coll)
    std::map<std::pair<int, int>, std::pair<int, byte_seq>> alternatives;

    auto concat = [](byte_seq &a, const byte_seq &b) {
        a.insert(a.end(), b.begin(), b.end());
    };
    auto background = read_gif("template/background.gif");

    // header
    byte_seq generated_gif = background["header"];
    concat(generated_gif, background["lcd"]);
    concat(generated_gif, background["gct"]);

    generated_gif.reserve(1024 * 1024);

    // background
    concat(generated_gif, control_extention);
    concat(generated_gif, background["img_descriptor"]);
    concat(generated_gif, background["img_data"]);

    // start comment
    concat(generated_gif, byte_seq{0x21, 0xfe});

    uint16_t top = 102;
    uint16_t left = 143;
    for(int char_pos = 0; char_pos < 32; char_pos++) {
        left += char_width;
        for(int cha = 0; cha < 16; cha++) {
            byte_seq char_img = control_extention;
            char_img.push_back('<');
            char_img.push_back('H');
            char_img.push_back('H');
            char_img.push_back('H');
            #define push_u16(value) \
                char_img.push_back(value >> 8); char_img.push_back(value && 0xFF);
            push_u16(left);
            push_u16(top);
            push_u16(char_width);
            push_u16(char_height);
            #undef push_u16
            concat(char_img, chars_img_data[cha]);

            auto coll_diff = COLLISION_LAST_DIFF;
            auto align = 64 - generated_gif.size() % 64;
            generated_gif.push_back(align - 1 + coll_diff);
            for(int i = 0; i < align - 1; i++) generated_gif.push_back(0x00);


            byte_seq coll_img;
            byte_seq coll_nop;
            int coll_p_img, pad_len;
            while(true) {
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

            concat(generated_gif, coll_nop);
            for(int i = 0; i < coll_p_img; i++) generated_gif.push_back(0x00);
            generated_gif.push_back(0x00); // end comment

            // insert char image
            concat(generated_gif, char_img);

            // start new comment
            generated_gif.push_back(0x21);
            generated_gif.push_back(0xfe);
            generated_gif.push_back(pad_len);
            for(int i = 0; i < pad_len; i++) generated_gif.push_back(0x00);
        }
    }


    auto result_hash = [&](){
        md5 hash;
        md5::digest_type digest;
        hash.process_bytes(generated_gif.data(), generated_gif.size());
        hash.get_digest(digest);
        auto str = toString(digest);
        byte_seq result_hash(str.size(), 0);
        for(int i = 0; i < result_hash.size(); i++) {
            std::string s{str[i]};
            result_hash[i] = std::stoul(s,nullptr,16);
        }
        return result_hash;
    }();
    
    for(int char_pos = 0; char_pos < result_hash.size(); char_pos++) {
        int cha = result_hash[char_pos];
        auto [coll_pos, coll] = alternatives[{char_pos, cha}];
        std::memcpy(generated_gif.data() + coll_pos, coll.data(), coll.size());
    }


    {
        FILE *out = fopen("hashquine.gif", "wb");
        fwrite(generated_gif.data(), 1, generated_gif.size(), out);
        fclose(out);
    }

}

int main() {
    generate();
    return 0;
}