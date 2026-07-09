/*
 * crypto.h — XOR-CBC 轻量通信加密 (无外部依赖)
 * 服务端 server.py 使用相同密钥
 * v3.x 加固: 密钥/IV 编译时通过异或掩码混淆，运行时解回
 */

#pragma once
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

// 编译时 XOR mask — 构建时修改此值即可更改混淆模式
#define CRYPTO_MASK  0x6D

// 密钥和 IV 以混淆形式存储（bytes 与 CRYPTO_MASK 异或）
// 原始值: "ImGuiOverlay2026" / "InitVector123456"
static const uint8_t _obf_key[16] = {
    0x24, 0x00, 0x2A, 0x18, 0x04, 0x22, 0x1B, 0x08, 0x1F, 0x01, 0x0C, 0x14, 0x5F, 0x5D, 0x5F, 0x5B
};
static const uint8_t _obf_iv[16] = {
    0x24, 0x03, 0x04, 0x19, 0x3B, 0x08, 0x0E, 0x19, 0x02, 0x1F, 0x5C, 0x5F, 0x5E, 0x59, 0x58, 0x5B
};

// 运行时获取真实密钥/IV（inline 可消除调用开销）
inline void _unobfuscated_key(uint8_t out[16]) {
    for (int i = 0; i < 16; i++) out[i] = _obf_key[i] ^ CRYPTO_MASK;
}
inline void _unobfuscated_iv(uint8_t out[16]) {
    for (int i = 0; i < 16; i++) out[i] = _obf_iv[i] ^ CRYPTO_MASK;
}

inline std::string xor_encrypt(const std::string& plain) {
    uint8_t key[16]; _unobfuscated_key(key);
    uint8_t iv[16]; _unobfuscated_iv(iv);
    size_t pad = 16 - (plain.size() % 16);
    std::vector<uint8_t> buf(plain.size() + pad);
    memcpy(buf.data(), plain.data(), plain.size());
    for (size_t i = plain.size(); i < buf.size(); i++) buf[i] = (uint8_t)pad;

    std::vector<uint8_t> result(buf.size());
    uint8_t prev[16]; memcpy(prev, iv, 16);
    for (size_t i = 0; i < buf.size(); i += 16) {
        for (int j = 0; j < 16; j++) {
            result[i+j] = buf[i+j] ^ key[j] ^ prev[j];
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
    uint8_t key[16]; _unobfuscated_key(key);
    uint8_t iv[16]; _unobfuscated_iv(iv);
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
    uint8_t prev[16]; memcpy(prev, iv, 16);
    for (size_t i = 0; i < buf.size(); i += 16) {
        uint8_t next_prev[16]; memcpy(next_prev, buf.data() + i, 16);
        for (int j = 0; j < 16; j++) {
            result[i+j] = buf[i+j] ^ key[j] ^ prev[j];
        }
        memcpy(prev, next_prev, 16);
    }

    uint8_t pad = result.back();
    if (pad > 0 && pad <= 16) result.resize(result.size() - pad);
    return std::string((char*)result.data(), result.size());
}
