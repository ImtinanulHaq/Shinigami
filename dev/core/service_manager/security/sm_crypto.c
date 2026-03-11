#define _POSIX_C_SOURCE 200809L

/*
 * sm_crypto.c - HMAC-SHA256 implementation
 *
 * SHA-256 is implemented from the FIPS 180-4 specification.
 * HMAC follows RFC 2104.
 * No OpenSSL or external crypto library is required.
 */

#include "../security/sm_crypto.h"
#include "../observability/sm_logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>   /* mlock / munlock */
#include <errno.h>

/* Fallback for explicit_bzero if not available */
#ifndef explicit_bzero
void explicit_bzero(void *s, size_t n)
{
    memset(s, 0, n);
    __asm__ __volatile__("" ::: "memory");
}
#endif

/* ── SHA-256 INTERNALS ──────────────────────────────────────────────────────── */

/* SHA-256 initial hash values (first 32 bits of fractional parts of sqrt of primes 2..19) */
static const uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/* SHA-256 round constants (first 32 bits of fractional parts of cbrt of primes 2..311) */
static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Rotate right 32-bit value */
#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 logical functions */
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x)    (ROTR32(x,  2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define SIGMA1(x)    (ROTR32(x,  6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define GAMMA0(x)    (ROTR32(x,  7) ^ ROTR32(x, 18) ^ ((x) >>  3))
#define GAMMA1(x)    (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

typedef struct {
    uint32_t state[8];      /* hash state */
    uint8_t  buf[64];       /* input buffer */
    uint64_t bit_count;     /* total bits processed */
    uint32_t buf_used;      /* bytes used in buf */
} sha256_ctx_t;

/* Write a big-endian 32-bit value */
static void write_be32(uint8_t* out, uint32_t val)
{
    out[0] = (uint8_t)(val >> 24);
    out[1] = (uint8_t)(val >> 16);
    out[2] = (uint8_t)(val >>  8);
    out[3] = (uint8_t)(val);
}

/* Write a big-endian 64-bit value */
static void write_be64(uint8_t* out, uint64_t val)
{
    out[0] = (uint8_t)(val >> 56);
    out[1] = (uint8_t)(val >> 48);
    out[2] = (uint8_t)(val >> 40);
    out[3] = (uint8_t)(val >> 32);
    out[4] = (uint8_t)(val >> 24);
    out[5] = (uint8_t)(val >> 16);
    out[6] = (uint8_t)(val >>  8);
    out[7] = (uint8_t)(val);
}

/* Process one 64-byte block */
static void sha256_compress(sha256_ctx_t* ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int      i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4+0] << 24)
             | ((uint32_t)block[i*4+1] << 16)
             | ((uint32_t)block[i*4+2] <<  8)
             | ((uint32_t)block[i*4+3]);
    }
    for (i = 16; i < 64; i++) {
        w[i] = GAMMA1(w[i-2]) + w[i-7] + GAMMA0(w[i-15]) + w[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + SIGMA1(e) + CH(e, f, g) + SHA256_K[i] + w[i];
        t2 = SIGMA0(a) + MAJ(a, b, c);
        h  = g; g = f; f = e; e = d + t1;
        d  = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t* ctx)
{
    memcpy(ctx->state, SHA256_H0, sizeof(SHA256_H0));
    ctx->bit_count = 0;
    ctx->buf_used  = 0;
}

static void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t len)
{
    size_t i;

    ctx->bit_count += (uint64_t)len * 8;

    for (i = 0; i < len; i++) {
        ctx->buf[ctx->buf_used++] = data[i];
        if (ctx->buf_used == 64) {
            sha256_compress(ctx, ctx->buf);
            ctx->buf_used = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t* ctx, uint8_t out[32])
{
    uint8_t  pad[64];
    uint64_t bit_count = ctx->bit_count;
    uint32_t pad_start = ctx->buf_used;
    int      i;

    /* Append 0x80, then zeros, then 64-bit big-endian bit count */
    memset(pad, 0, sizeof(pad));
    memcpy(pad, ctx->buf, ctx->buf_used);
    pad[pad_start] = 0x80;

    if (pad_start >= 56) {
        /* No room for length in this block - need an extra block */
        sha256_compress(ctx, pad);
        memset(pad, 0, 56);
    }

    write_be64(pad + 56, bit_count);
    sha256_compress(ctx, pad);

    for (i = 0; i < 8; i++) {
        write_be32(out + i * 4, ctx->state[i]);
    }

    /* Zeroize context to prevent leaking sensitive data */
    explicit_bzero(ctx, sizeof(*ctx));
}

/* Compute SHA-256 of a single buffer */
static void sha256(const uint8_t* data, size_t len, uint8_t out[32])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

/* ── HMAC-SHA256 ────────────────────────────────────────────────────────────── */

int sm_hmac_sha256(const uint8_t* key,  size_t key_len,
                   const uint8_t* data, size_t data_len,
                   uint8_t        out[SM_HMAC_SIZE])
{
    uint8_t      k_pad[64];   /* key padded to block size */
    uint8_t      inner[32];   /* inner hash H(k XOR ipad || data) */
    sha256_ctx_t ctx;
    size_t       i;

    if (!key || !data || !out) return -1;

    /* If key is longer than block size, hash it first */
    memset(k_pad, 0, sizeof(k_pad));
    if (key_len > 64) {
        sha256(key, key_len, k_pad);
    } else {
        memcpy(k_pad, key, key_len);
    }

    /* Inner hash: H((k XOR ipad) || data) */
    sha256_init(&ctx);
    for (i = 0; i < 64; i++) k_pad[i] ^= 0x36;   /* ipad = 0x36 repeated */
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    /* Restore k_pad: XOR off ipad, XOR on opad */
    for (i = 0; i < 64; i++) k_pad[i] ^= (0x36 ^ 0x5c);

    /* Outer hash: H((k XOR opad) || inner) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);

    explicit_bzero(k_pad, sizeof(k_pad));
    explicit_bzero(inner, sizeof(inner));
    return 0;
}

/*
 * Constant-time byte comparison.
 * Returns 0 if equal, non-zero if different.
 * The loop never short-circuits, preventing timing side-channels.
 */
static int consttime_memcmp(const uint8_t* a, const uint8_t* b, size_t len)
{
    uint8_t diff = 0;
    size_t  i;
    for (i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return (int)diff;
}

int sm_hmac_verify(const uint8_t* key,      size_t key_len,
                   const uint8_t* data,     size_t data_len,
                   const uint8_t  expected[SM_HMAC_SIZE])
{
    uint8_t computed[SM_HMAC_SIZE];

    if (sm_hmac_sha256(key, key_len, data, data_len, computed) != 0)
        return -1;

    /* Constant-time compare to prevent timing attacks */
    return consttime_memcmp(computed, expected, SM_HMAC_SIZE) == 0 ? 0 : -1;
}

/* ── KEY MANAGEMENT ─────────────────────────────────────────────────────────── */

static uint8_t  g_key[SM_HMAC_KEY_SIZE];
static int      g_key_loaded = 0;

/* Read SM_HMAC_KEY_SIZE bytes from /dev/urandom */
static int generate_key(uint8_t* out, size_t len)
{
    int    fd;
    ssize_t n;

    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        sm_log(SM_LOG_ERROR, "crypto: cannot open /dev/urandom: %s", strerror(errno));
        return -1;
    }

    n = read(fd, out, len);
    close(fd);

    if (n < 0 || (size_t)n != len) {
        sm_log(SM_LOG_ERROR, "crypto: short read from /dev/urandom");
        return -1;
    }
    return 0;
}

int sm_crypto_init(void)
{
    int     fd;
    ssize_t n;
    const char* key_paths[] = {
        SM_KEY_FILE,                           /* /run/servicemanager.key */
        "/tmp/servicemanager.key",             /* fallback to /tmp */
        NULL
    };
    const char* key_file = NULL;
    int path_idx = 0;

    /* Try each key file path until one works */
    while (key_paths[path_idx] != NULL) {
        key_file = key_paths[path_idx];
        
        /* Try to load an existing key */
        fd = open(key_file, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            n = read(fd, g_key, SM_HMAC_KEY_SIZE);
            close(fd);
            if (n == SM_HMAC_KEY_SIZE) {
                g_key_loaded = 1;
                /*
                 * mlock() pins the page containing the key in RAM so the
                 * kernel never writes it to the swap partition.
                 * Non-fatal if the process lacks CAP_IPC_LOCK or the
                 * memlock rlimit is exhausted; log a warning and continue.
                 */
                if (mlock(g_key, sizeof(g_key)) != 0) {
                    sm_log(SM_LOG_WARN,
                           "crypto: mlock failed — key may be swappable: %s",
                           strerror(errno));
                }
                sm_log(SM_LOG_INFO, "crypto: key loaded from %s", key_file);
                return 0;
            }
            sm_log(SM_LOG_WARN, "crypto: key file truncated at %s, regenerating", key_file);
        }

        /* Generate a new key */
        if (generate_key(g_key, SM_HMAC_KEY_SIZE) != 0) {
            path_idx++;
            continue;
        }

        /* Write key file with restricted permissions */
        fd = open(key_file,
                  O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                  SM_KEY_FILE_MODE);
        if (fd < 0) {
            sm_log(SM_LOG_WARN, "crypto: cannot create key file %s, trying fallback", key_file);
            path_idx++;
            continue;
        }

        n = write(fd, g_key, SM_HMAC_KEY_SIZE);
        close(fd);

        if (n != SM_HMAC_KEY_SIZE) {
            sm_log(SM_LOG_WARN, "crypto: failed to write key file %s, trying fallback", key_file);
            path_idx++;
            continue;
        }

        g_key_loaded = 1;
        if (mlock(g_key, sizeof(g_key)) != 0) {
            sm_log(SM_LOG_WARN,
                   "crypto: mlock failed — key may be swappable: %s",
                   strerror(errno));
        }
        sm_log(SM_LOG_INFO, "crypto: new key generated and saved to %s", key_file);
        return 0;
    }

    /* All paths failed */
    sm_log(SM_LOG_ERROR, "crypto: failed to write key file to any location");
    explicit_bzero(g_key, sizeof(g_key));
    return -1;
}

const uint8_t* sm_crypto_get_key(void)
{
    if (!g_key_loaded) return NULL;
    return g_key;
}

void sm_crypto_cleanup(void)
{
    /*
     * Unlock before zeroing so the kernel is free to evict the now-empty
     * page.  munlock on an unlocked page is a no-op, so this is safe even
     * if mlock() failed at init time.
     */
    munlock(g_key, sizeof(g_key));
    explicit_bzero(g_key, sizeof(g_key));
    g_key_loaded = 0;
}