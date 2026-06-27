/*
 * crypto.h — XOR-CBC 轻量通信加密 (无外部依赖)
 * 服务端 server.py 使用相同密钥
 */

#pragma once
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

static const uint8_t XOR_KEY[16] = {
    'I','m','G','u','i','O','v','e','r','l','a','y','2','0','2','6'
};
static uint8_t xor_iv[16] = {
    'I','n','i','t','V','e','c','t','o','r','1','2','3','4','5','6'
};

inline std::string xor_encrypt(const std::string& plain) {
    size_t pad = 16 - (plain.size() % 16);
    std::vector<uint8_t> buf(plain.size() + pad);
    memcpy(buf.data(), plain.data(), plain.size());
    for (size_t i = plain.size(); i < buf.size(); i++) buf[i] = (uint8_t)pad;

    std::vector<uint8_t> result(buf.size());
    uint8_t prev[16]; memcpy(prev, xor_iv, 16);
    for (size_t i = 0; i < buf.size(); i += 16) {
        for (int j = 0; j < 16; j++) {
            result[i+j] = buf[i+j] ^ XOR_KEY[j] ^ prev[j];
            prev[j] = result[i+j];
        }
    }

    std::string hex;
    hex.reserve(result.size() * 2);
    for (uint8_t b : result) {
        hex += "0123456789ABCDEF"[b >> 4];
        hex += "0123456789ABCDEF"[b & 0xF];
    }
    return hex;
}

inline std::string xor_decrypt(const std::string& hex) {
    if (hex.size() % 2 != 0) return "";
    std::vector<uint8_t> buf(hex.size() / 2);
    for (size_t i = 0; i < buf.size(); i++) {
        auto h2b = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return 0;
        };
        buf[i] = (h2b(hex[i*2]) << 4) | h2b(hex[i*2+1]);
    }

    std::vector<uint8_t> result(buf.size());
    uint8_t prev[16]; memcpy(prev, xor_iv, 16);
    for (size_t i = 0; i < buf.size(); i += 16) {
        uint8_t next_prev[16]; memcpy(next_prev, buf.data() + i, 16);
        for (int j = 0; j < 16; j++) {
            result[i+j] = buf[i+j] ^ XOR_KEY[j] ^ prev[j];
        }
        memcpy(prev, next_prev, 16);
    }

    uint8_t pad = result.back();
    if (pad > 0 && pad <= 16) result.resize(result.size() - pad);
    return std::string((char*)result.data(), result.size());
}
