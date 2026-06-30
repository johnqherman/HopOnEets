#pragma once
// public-domain SHA-256 (Brad Conte's crypto-algorithms)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace sha256impl {
struct Ctx {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
};
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define SR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void transform(Ctx *c, const uint8_t *d) {
  uint32_t a, b, e, f, g, h, i, j, t1, t2, m[64], cc;
  for (i = 0, j = 0; i < 16; i++, j += 4)
    m[i] = (d[j] << 24) | (d[j + 1] << 16) | (d[j + 2] << 8) | d[j + 3];
  for (; i < 64; i++)
    m[i] = (SR(m[i - 2], 17) ^ SR(m[i - 2], 19) ^ (m[i - 2] >> 10)) + m[i - 7] +
           (SR(m[i - 15], 7) ^ SR(m[i - 15], 18) ^ (m[i - 15] >> 3)) + m[i - 16];
  a = c->state[0]; b = c->state[1]; cc = c->state[2]; e = c->state[4];
  uint32_t d2 = c->state[3]; f = c->state[5]; g = c->state[6]; h = c->state[7];
  for (i = 0; i < 64; i++) {
    t1 = h + (SR(e, 6) ^ SR(e, 11) ^ SR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + m[i];
    t2 = (SR(a, 2) ^ SR(a, 13) ^ SR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
    h = g; g = f; f = e; e = d2 + t1; d2 = cc; cc = b; b = a; a = t1 + t2;
  }
  c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d2;
  c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}
#undef SR
static void init(Ctx *c) {
  c->datalen = 0; c->bitlen = 0;
  c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85; c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
  c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c; c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
}
static void update(Ctx *c, const uint8_t *d, size_t len) {
  for (size_t i = 0; i < len; i++) {
    c->data[c->datalen++] = d[i];
    if (c->datalen == 64) { transform(c, c->data); c->bitlen += 512; c->datalen = 0; }
  }
}
static void final(Ctx *c, uint8_t *hash) {
  uint32_t i = c->datalen;
  c->data[i++] = 0x80;
  if (c->datalen < 56) { while (i < 56) c->data[i++] = 0; }
  else { while (i < 64) c->data[i++] = 0; transform(c, c->data); memset(c->data, 0, 56); }
  c->bitlen += (uint64_t)c->datalen * 8;
  for (int k = 7; k >= 0; k--) c->data[56 + (7 - k)] = (uint8_t)(c->bitlen >> (k * 8));
  transform(c, c->data);
  for (i = 0; i < 4; i++)
    for (int k = 0; k < 8; k++) hash[i + k * 4] = (uint8_t)(c->state[k] >> (24 - i * 8));
}
} // namespace sha256impl

// lowercase-hex SHA-256 of a file; "" on read failure
static std::string sha256_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return "";
  sha256impl::Ctx c; sha256impl::init(&c);
  uint8_t buf[65536]; size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) sha256impl::update(&c, buf, n);
  fclose(f);
  uint8_t h[32]; sha256impl::final(&c, h);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", h[i]);
  return std::string(hex, 64);
}
