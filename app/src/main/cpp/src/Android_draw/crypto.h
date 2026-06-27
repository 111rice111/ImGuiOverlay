/*
 * crypto.h — 轻量 AES-128-CBC 加密 (无外部依赖)
 * 基于公共领域 tiny-AES-c, 仅用于通信加密
 */

#pragma once
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

// 固定密钥和 IV (可用脚本随机生成替换)
static const uint8_t AES_KEY[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};
static const uint8_t AES_IV[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

// ==================== AES 核心实现 ====================
#define Nb 4
#define Nk 4
#define Nr 10

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t Rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

struct AES_ctx { uint8_t RoundKey[176]; uint8_t Iv[16]; };

static void KeyExpansion(uint8_t* RoundKey, const uint8_t* Key) {
    memcpy(RoundKey, Key, 16);
    for (int i = 4; i < 44; i++) {
        uint8_t t[4]; memcpy(t, RoundKey + (i-1)*4, 4);
        if (i % 4 == 0) {
            uint8_t u = t[0]; t[0] = sbox[t[1]] ^ Rcon[i/4]; t[1] = sbox[t[2]]; t[2] = sbox[t[3]]; t[3] = sbox[u];
        }
        for (int j = 0; j < 4; j++) RoundKey[i*4+j] = RoundKey[(i-4)*4+j] ^ t[j];
    }
}

static void AddRoundKey(uint8_t* state, const uint8_t* RoundKey, int round) {
    for (int i = 0; i < 16; i++) state[i] ^= RoundKey[round*16+i];
}

static void SubBytes(uint8_t* state) { for (int i = 0; i < 16; i++) state[i] = sbox[state[i]]; }
static void ShiftRows(uint8_t* state) {
    uint8_t t = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t;
    t = state[2]; state[2] = state[10]; uint8_t u = state[6]; state[6] = state[14]; state[14] = t; state[10] = u;
    t = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t;
}

static uint8_t xtime(uint8_t x) { return (x<<1) ^ ((x>>7) ? 0x1b : 0); }
static void MixColumns(uint8_t* state) {
    for (int i = 0; i < 4; i++) {
        uint8_t a[4]; for (int j = 0; j < 4; j++) a[j] = state[i+j*4];
        state[i] = xtime(a[0]) ^ (xtime(a[1])^a[1]) ^ a[2] ^ a[3];
        state[i+4] = a[0] ^ xtime(a[1]) ^ (xtime(a[2])^a[2]) ^ a[3];
        state[i+8] = a[0] ^ a[1] ^ xtime(a[2]) ^ (xtime(a[3])^a[3]);
        state[i+12] = (xtime(a[0])^a[0]) ^ a[1] ^ a[2] ^ xtime(a[3]);
    }
}

static void AES_init_ctx_iv(AES_ctx* ctx, const uint8_t* key, const uint8_t* iv) {
    KeyExpansion(ctx->RoundKey, key);
    memcpy(ctx->Iv, iv, 16);
}

static void AES_CBC_encrypt_buffer(AES_ctx* ctx, uint8_t* buf, size_t length) {
    size_t i; uint8_t* iv = ctx->Iv;
    for (i = 0; i < length; i += 16) {
        for (int k = 0; k < 16; k++) buf[i+k] ^= iv[k];
        uint8_t state[16]; memcpy(state, buf+i, 16);
        AddRoundKey(state, ctx->RoundKey, 0);
        for (int r=1; r<Nr; r++) { SubBytes(state); ShiftRows(state); MixColumns(state); AddRoundKey(state, ctx->RoundKey, r); }
        SubBytes(state); ShiftRows(state); AddRoundKey(state, ctx->RoundKey, Nr);
        memcpy(buf+i, state, 16);
        iv = buf+i;
    }
    memcpy(ctx->Iv, iv, 16);
}

static void AES_CBC_decrypt_buffer(AES_ctx* ctx, uint8_t* buf, size_t length) {
    size_t i; uint8_t* iv = ctx->Iv; uint8_t nextIv[16];
    for (i = 0; i < length; i += 16) {
        memcpy(nextIv, buf+i, 16);
        uint8_t state[16]; memcpy(state, buf+i, 16);
        AddRoundKey(state, ctx->RoundKey, Nr);
        for (int r=Nr-1; r>0; r--) { ShiftRows(state); SubBytes(state); AddRoundKey(state, ctx->RoundKey, r); MixColumns(state); }
        ShiftRows(state); SubBytes(state); AddRoundKey(state, ctx->RoundKey, 0);
        for (int k = 0; k < 16; k++) state[k] ^= iv[k];
        memcpy(buf+i, state, 16);
        memcpy(iv, nextIv, 16);
    }
}

// ==================== 高层接口 ====================

// AES-128-CBC 加密，返回 base64
inline std::string aes_encrypt(const std::string& plain) {
    // PKCS7 padding
    size_t pad = 16 - (plain.size() % 16);
    std::vector<uint8_t> buf(plain.size() + pad);
    memcpy(buf.data(), plain.data(), plain.size());
    for (size_t i = plain.size(); i < buf.size(); i++) buf[i] = (uint8_t)pad;

    AES_ctx ctx;
    AES_init_ctx_iv(&ctx, AES_KEY, AES_IV);
    AES_CBC_encrypt_buffer(&ctx, buf.data(), buf.size());

    // 简单 hex 编码 (比 base64 简单，无依赖)
    std::string result;
    result.reserve(buf.size() * 2);
    for (uint8_t b : buf) {
        result += "0123456789ABCDEF"[b >> 4];
        result += "0123456789ABCDEF"[b & 0xF];
    }
    return result;
}

// AES-128-CBC 解密 (hex 输入)
inline std::string aes_decrypt(const std::string& hex) {
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

    AES_ctx ctx;
    AES_init_ctx_iv(&ctx, AES_KEY, AES_IV);
    AES_CBC_decrypt_buffer(&ctx, buf.data(), buf.size());

    // 去 PKCS7 padding
    uint8_t pad = buf.back();
    if (pad > 0 && pad <= 16) buf.resize(buf.size() - pad);
    return std::string((char*)buf.data(), buf.size());
}
