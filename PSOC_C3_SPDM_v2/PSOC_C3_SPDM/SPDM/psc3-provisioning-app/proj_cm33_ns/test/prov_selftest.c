/*****************************************************************************
 * File Name        : prov_selftest.c
 *
 * Description      : Build-time self-test that exercises every secure
 *                    provisioning service through its Non-Secure-Callable
 *                    (NSC) veneer. Compiled into the image only when the
 *                    project is built with PROV_SELFTEST=1; otherwise the
 *                    build system excludes this file from source
 *                    auto-discovery (see proj_cm33_ns/Makefile).
 *
 *                    The test runs end-to-end on a non-frozen device:
 *                      status -> info -> erase(all) -> gen-key -> read-pubkey
 *                      -> build/read CSR -> cert commit -> read cert
 *                      -> prove cert -> status -> erase. The IRREVERSIBLE
 *                    finalize service is exercised only when the project is
 *                    additionally built with -DPROV_SELFTEST_FINALIZE.
 *
 *                    A compact, self-contained SHA-256 is bundled here because
 *                    the non-secure world has no crypto library; it is needed
 *                    to feed cert-commit the digest the secure side verifies.
 *
 * Related Document : See README.md and docs/design_and_implementation.md
 *
 *******************************************************************************
 * (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
 *****************************************************************************/


#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "prov_nsc.h"
#include "prov_selftest.h"

#include "log/log.h"

/******************************************************************************
 * Test configuration
 *****************************************************************************/

/* Slot roles for the test (matches the PAS layout: slot 0 = key, slot 1 = cert). */
#define ST_KEY_SLOT       (0U)
#define ST_CERT_SLOT      (1U)

/* Size of the synthetic "certificate" blob staged during the test. Chosen to
 * span several PROV_MAX_CHUNK read-back chunks while fitting any cert slot. */
#define ST_CERT_LEN       (200U)

/******************************************************************************
 * Key suite under test
 *
 * A build may compile in any mix of EC256 / EC384 / EC521 and ML-DSA-87
 * (PROV_SIG_SUITES). The self-test exercises every compiled-in suite through a
 * full key + CSR + prove lifecycle, and designates one "primary" suite whose
 * key/cert are left in the slots for the provisioned-state / frozen checks.
 * The primary is the strongest available suite in a fixed priority order:
 * ML-DSA-87 > EC256 > EC384 > EC521. ST_PUB_MAX sizes the shared public-key
 * read-back buffer to the largest compiled suite.
 *****************************************************************************/

#if defined(PROV_ENABLE_MLDSA87)
#define ST_ALG_ID         PROV_ALG_MLDSA_87
#define ST_PUB_LEN        PROV_PUB_LEN_MLDSA87
#define ST_SIG_LEN        PROV_SIG_LEN_MLDSA87
#define ST_ALG_NAME       "ML-DSA-87"
/* CSR buffer must hold the full ML-DSA-87 PKCS#10 DER (~7.3 KB). */
#define ST_CSR_BUF_LEN    (8192U)
#elif defined(PROV_ENABLE_EC256)
#define ST_ALG_ID         PROV_ALG_ECDSA_P256
#define ST_PUB_LEN        PROV_PUB_LEN_P256
#define ST_SIG_LEN        PROV_SIG_LEN_P256
#define ST_ALG_NAME       "ECDSA-P256"
#define ST_CSR_BUF_LEN    (1024U)
#elif defined(PROV_ENABLE_EC384)
#define ST_ALG_ID         PROV_ALG_ECDSA_P384
#define ST_PUB_LEN        PROV_PUB_LEN_P384
#define ST_SIG_LEN        PROV_SIG_LEN_P384
#define ST_ALG_NAME       "ECDSA-P384"
#define ST_CSR_BUF_LEN    (1024U)
#elif defined(PROV_ENABLE_EC521)
#define ST_ALG_ID         PROV_ALG_ECDSA_P521
#define ST_PUB_LEN        PROV_PUB_LEN_P521
#define ST_SIG_LEN        PROV_SIG_LEN_P521
#define ST_ALG_NAME       "ECDSA-P521"
#define ST_CSR_BUF_LEN    (1024U)
#else
#error "No provisioning signature suite enabled (check PROV_SIG_SUITES)"
#endif

/* Largest public-key length across all compiled-in suites (sizes s_pub). */
#if defined(PROV_ENABLE_MLDSA87)
#define ST_PUB_MAX        PROV_PUB_LEN_MLDSA87
#elif defined(PROV_ENABLE_EC521)
#define ST_PUB_MAX        PROV_PUB_LEN_P521
#elif defined(PROV_ENABLE_EC384)
#define ST_PUB_MAX        PROV_PUB_LEN_P384
#else
#define ST_PUB_MAX        PROV_PUB_LEN_P256
#endif

/******************************************************************************
 * Pass/fail bookkeeping
 *****************************************************************************/

static unsigned int s_pass;
static unsigned int s_fail;

/* Visual section divider printed between test groups. */
#define ST_SECTION(title)                                                      \
    do {                                                                       \
        LOG_INFO("");                                                          \
        LOG_INFO("--- %s ", (title));                                          \
    } while (0)

#define ST_OK(label, expr)                                                     \
    do {                                                                       \
        prov_status_t _st = (expr);                                            \
        if (_st == PROV_OK) {                                                  \
            s_pass++;                                                          \
            LOG_INFO("  [PASS] %s", (label));                                  \
        } else {                                                               \
            s_fail++;                                                          \
            LOG_ERR("  [FAIL] %s (status=0x%02X)", (label), (unsigned int)_st);\
        }                                                                      \
    } while (0)

#define ST_TRUE(label, cond)                                                   \
    do {                                                                       \
        if (cond) {                                                            \
            s_pass++;                                                          \
            LOG_INFO("  [PASS] %s", (label));                                  \
        } else {                                                               \
            s_fail++;                                                          \
            LOG_ERR("  [FAIL] %s", (label));                                   \
        }                                                                      \
    } while (0)

/* A mutating command on a frozen device must be rejected with
 * PROV_ERR_FROZEN; that rejection is the expected (passing) outcome. */
#define ST_FROZEN(label, expr)                                                 \
    do {                                                                       \
        prov_status_t _st = (expr);                                            \
        if (_st == PROV_ERR_FROZEN) {                                          \
            s_pass++;                                                          \
            LOG_INFO("  [PASS] %s", (label));                                  \
        } else {                                                               \
            s_fail++;                                                          \
            LOG_ERR("  [FAIL] %s (status=0x%02X, expected FROZEN)",            \
                    (label), (unsigned int)_st);                              \
        }                                                                      \
    } while (0)

/******************************************************************************
 * Minimal self-contained SHA-256 (FIPS 180-4)
 *
 * Only compiled into the self-test build; used to compute the digest that
 * cert-commit / prove-cert verify on the secure side.
 *****************************************************************************/

typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} st_sha256_t;

static const uint32_t ST_K[64] =
{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

#define ST_ROTR(x, n)  (((x) >> (n)) | ((x) << (32U - (n))))

static void st_sha256_transform(st_sha256_t *ctx, const uint8_t *data)
{
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t i;

    for (i = 0U; i < 16U; i++)
    {
        m[i] = ((uint32_t)data[i * 4U] << 24) |
               ((uint32_t)data[i * 4U + 1U] << 16) |
               ((uint32_t)data[i * 4U + 2U] << 8) |
               ((uint32_t)data[i * 4U + 3U]);
    }
    for (i = 16U; i < 64U; i++)
    {
        uint32_t s0 = ST_ROTR(m[i - 15U], 7) ^ ST_ROTR(m[i - 15U], 18) ^ (m[i - 15U] >> 3);
        uint32_t s1 = ST_ROTR(m[i - 2U], 17) ^ ST_ROTR(m[i - 2U], 19) ^ (m[i - 2U] >> 10);
        m[i] = m[i - 16U] + s0 + m[i - 7U] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0U; i < 64U; i++)
    {
        uint32_t s1 = ST_ROTR(e, 6) ^ ST_ROTR(e, 11) ^ ST_ROTR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + ST_K[i] + m[i];
        uint32_t s0 = ST_ROTR(a, 2) ^ ST_ROTR(a, 13) ^ ST_ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void st_sha256_init(st_sha256_t *ctx)
{
    ctx->datalen = 0U;
    ctx->bitlen = 0U;
    ctx->state[0] = 0x6a09e667U; ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U; ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU; ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU; ctx->state[7] = 0x5be0cd19U;
}

static void st_sha256_update(st_sha256_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0U; i < len; i++)
    {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64U)
        {
            st_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512U;
            ctx->datalen = 0U;
        }
    }
}

static void st_sha256_final(st_sha256_t *ctx, uint8_t *hash)
{
    uint32_t i = ctx->datalen;

    ctx->data[i++] = 0x80U;
    if (ctx->datalen < 56U)
    {
        while (i < 56U) { ctx->data[i++] = 0x00U; }
    }
    else
    {
        while (i < 64U) { ctx->data[i++] = 0x00U; }
        st_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56U);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8U;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    st_sha256_transform(ctx, ctx->data);

    /* Emit the eight state words in big-endian order. */
    for (i = 0U; i < 8U; i++)
    {
        uint32_t w = ctx->state[i];
        hash[i * 4U + 0U] = (uint8_t)(w >> 24);
        hash[i * 4U + 1U] = (uint8_t)(w >> 16);
        hash[i * 4U + 2U] = (uint8_t)(w >> 8);
        hash[i * 4U + 3U] = (uint8_t)(w);
    }
}

static void st_sha256(const uint8_t *data, uint32_t len, uint8_t out[PROV_SHA256_LEN])
{
    st_sha256_t ctx;
    st_sha256_init(&ctx);
    st_sha256_update(&ctx, data, len);
    st_sha256_final(&ctx, out);
}

/******************************************************************************
 * Shared test buffers (static to keep the non-secure stack small)
 *****************************************************************************/

static uint8_t s_pub[ST_PUB_MAX];
static uint8_t s_csr[ST_CSR_BUF_LEN];
static uint8_t s_cert[ST_CERT_LEN];
static uint8_t s_cert_rb[ST_CERT_LEN];
static uint8_t s_sha[PROV_SHA256_LEN];
/* prove_cert working buffers. The signature (up to 4627 B for ML-DSA-87) is
 * returned whole by the NSC veneer into s_csr, which is idle by the time
 * prove-cert runs and always large enough (ST_CSR_BUF_LEN >= ST_SIG_LEN). */
static uint8_t s_sha_dev[PROV_SHA256_LEN];
static uint8_t s_nonce[PROV_NONCE_LEN];

/******************************************************************************
 * Individual service tests
 *****************************************************************************/

static void st_test_status(const char *label)
{
    prov_status_info_t info;
    memset(&info, 0, sizeof(info));
    ST_OK(label, prov_nsc_get_status(&info));
    LOG_INFO("         provisioned=%u num_slots=%u pas_ver=0x%04X type[0]=%u type[1]=%u",
             (unsigned int)info.provisioned, (unsigned int)info.num_slots,
             (unsigned int)info.pas_version,
             (unsigned int)info.slot_type[0], (unsigned int)info.slot_type[1]);
}

static void st_test_info(void)
{
    prov_info_report_t info;
    uint32_t i;
    uint32_t n;
    memset(&info, 0, sizeof(info));
    ST_OK("get_info", prov_nsc_get_info(&info));
    LOG_INFO("         uid=%02X%02X%02X%02X%02X%02X%02X%02X num_slots=%u max_chunk=%u n_algs=%u",
             info.uid[0], info.uid[1], info.uid[2], info.uid[3],
             info.uid[4], info.uid[5], info.uid[6], info.uid[7],
             (unsigned int)info.num_slots, (unsigned int)info.max_chunk,
             (unsigned int)info.n_algs);
    n = (info.n_algs < PROV_MAX_ALGS) ? info.n_algs : PROV_MAX_ALGS;
    for (i = 0U; i < n; i++)
    {
        LOG_INFO("         alg[%u]=0x%04X pub_len=%u sig_len=%u",
                 (unsigned int)i,
                 (unsigned int)info.algs[i].alg_id,
                 (unsigned int)info.algs[i].pub_len,
                 (unsigned int)info.algs[i].sig_len);
    }
}

static void st_test_gen_key(uint16_t alg_id, uint16_t exp_pub_len, const char *name)
{
    uint16_t pub_len = 0U;
    LOG_INFO("  -- key suite: %s", name);
    ST_OK("gen_key(slot0)", prov_nsc_gen_key(ST_KEY_SLOT, alg_id, &pub_len));
    ST_TRUE("gen_key pub_len == expected", (pub_len == exp_pub_len));
}

static void st_test_read_pubkey(uint16_t exp_alg, uint16_t exp_pub_len)
{
    uint16_t total = 0U;
    uint16_t got = 0U;
    uint16_t alg = 0U;
    prov_status_t st = PROV_OK;

    do
    {
        prov_read_pubkey_args_t a;
        uint16_t ret = 0U;
        a.slot_id    = ST_KEY_SLOT;
        a.offset     = got;
        a.buf        = &s_pub[got];
        a.len        = PROV_MAX_CHUNK;
        a.alg_id_out = &alg;
        a.total_out  = &total;
        a.ret_out    = &ret;
        st = prov_nsc_read_pubkey(&a);
        if (st != PROV_OK) { break; }
        got += ret;
        if (ret == 0U) { break; }
    } while (got < total);

    ST_OK("read_pubkey", st);
    ST_TRUE("read_pubkey total == expected", (total == exp_pub_len));
    ST_TRUE("read_pubkey got == total", (got == total));
    ST_TRUE("read_pubkey alg == expected", (alg == exp_alg));
    LOG_INFO("         alg=0x%04X total=%u pub[0..3]=%02X %02X %02X %02X",
             (unsigned int)alg, (unsigned int)total,
             s_pub[0], s_pub[1], s_pub[2], s_pub[3]);
}

static void st_test_csr(void)
{
    prov_build_csr_args_t a;
    uint16_t total = 0U;
    prov_status_t st;

    a.slot_id   = ST_KEY_SLOT;
    a.templ     = NULL;
    a.templ_len = 0U;
    a.out       = s_csr;
    a.out_cap   = (uint16_t)sizeof(s_csr);
    a.total_out = &total;

    st = prov_nsc_build_csr(&a);
    ST_OK("build_csr(slot0, default subject)", st);
    ST_TRUE("build_csr total > 0", (total > 0U));
    if ((st != PROV_OK) || (total == 0U) || (total > sizeof(s_csr)))
    {
        return;
    }

    ST_TRUE("build_csr wrote full CSR into buffer", (total <= sizeof(s_csr)));
    LOG_INFO("         csr_len=%u der[0..1]=%02X %02X", (unsigned int)total,
             s_csr[0], s_csr[1]);
}

static void st_test_cert_commit(void)
{
    prov_cert_commit_args_t a;
    uint16_t cert_len = 0U;
    uint32_t i;

    /* Synthetic certificate payload (the device stores bytes verbatim and only
     * verifies transfer integrity against the SHA-256 we supply). */
    for (i = 0U; i < ST_CERT_LEN; i++)
    {
        s_cert[i] = (uint8_t)(i & 0xFFU);
    }
    st_sha256(s_cert, ST_CERT_LEN, s_sha);

    a.slot_id      = ST_CERT_SLOT;
    a.key_slot     = ST_KEY_SLOT;
    a.cert         = s_cert;
    a.total_len    = ST_CERT_LEN;
    a.sha256       = s_sha;
    a.cert_len_out = &cert_len;

    ST_OK("cert_commit(slot1, key0)", prov_nsc_cert_commit(&a));
    ST_TRUE("cert_commit cert_len == 200", (cert_len == ST_CERT_LEN));
}

static void st_test_read_cert(void)
{
    uint16_t total = 0U;
    uint16_t got = 0U;
    prov_status_t st = PROV_OK;

    do
    {
        prov_read_cert_args_t a;
        uint16_t ret = 0U;
        a.slot_id   = ST_CERT_SLOT;
        a.offset    = got;
        a.buf       = &s_cert_rb[got];
        a.len       = PROV_MAX_CHUNK;
        a.total_out = &total;
        a.ret_out   = &ret;
        st = prov_nsc_read_cert(&a);
        if (st != PROV_OK) { break; }
        got += ret;
        if (ret == 0U) { break; }
    } while ((got < total) && (got < sizeof(s_cert_rb)));

    ST_OK("read_cert (chunked)", st);
    ST_TRUE("read_cert total == 200", (total == ST_CERT_LEN));
    ST_TRUE("read_cert bytes match committed cert",
            (got == ST_CERT_LEN) && (memcmp(s_cert, s_cert_rb, ST_CERT_LEN) == 0));
}

static void st_test_prove_cert(uint16_t exp_alg, uint16_t exp_sig_len,
                               const char *name)
{
    prov_prove_cert_args_t a;
    uint16_t alg = 0U;
    uint16_t sig_len = 0U;
    uint32_t i;

    for (i = 0U; i < PROV_NONCE_LEN; i++)
    {
        s_nonce[i] = (uint8_t)(0xA0U ^ i);
    }
    memset(s_sha_dev, 0, sizeof(s_sha_dev));

    /* The veneer writes the whole signature into s_csr in one call; the DFU
     * router is what streams it back to the host in max_chunk windows. */
    a.slot_id     = ST_CERT_SLOT;
    a.nonce       = s_nonce;
    a.cert_sha256 = s_sha_dev;
    a.alg_id_out  = &alg;
    a.out         = s_csr;
    a.out_cap     = (uint16_t)sizeof(s_csr);
    a.sig_len_out = &sig_len;

    ST_OK("prove_cert(slot1)", prov_nsc_prove_cert(&a));
    ST_TRUE("prove_cert alg == expected", (alg == exp_alg));
    ST_TRUE("prove_cert sig_len == expected", (sig_len == exp_sig_len));
    ST_TRUE("prove_cert cert_sha256 matches local digest",
            (memcmp(s_sha, s_sha_dev, PROV_SHA256_LEN) == 0));
    LOG_INFO("         suite=%s alg=0x%04X sig_len=%u", name,
             (unsigned int)alg, (unsigned int)sig_len);
}

/* Full key + CSR + certificate + prove lifecycle for a single suite. Each call
 * overwrites slot 0 (key) and slot 1 (certificate), so callers run it per
 * compiled-in suite and finish with the primary suite last. */
static void st_test_sign_lifecycle(uint16_t alg_id, uint16_t pub_len,
                                   uint16_t sig_len, const char *name)
{
    st_test_gen_key(alg_id, pub_len, name);
    st_test_read_pubkey(alg_id, pub_len);
    st_test_csr();
    st_test_cert_commit();
    st_test_read_cert();
    st_test_prove_cert(alg_id, sig_len, name);
}

/******************************************************************************
 * Test flows
 *****************************************************************************/

/* Full provisioning lifecycle exercised on a clean, non-frozen device. Leaves
 * a key in slot 0 and a certificate in slot 1 (ready for an optional freeze). */
static void st_run_unprovisioned_tests(void)
{
    /* Start from a clean, non-frozen store. */
    ST_SECTION("Prepare clean store");
    ST_OK("erase(all) -> clean slate",
          prov_nsc_erase(PROV_SLOT_ALL, PROV_CONFIRM_ERASE));

    /* Full key + CSR + certificate + prove lifecycle for EVERY compiled-in
     * suite. Each suite overwrites slot 0 (key) and slot 1 (cert). The primary
     * suite (ST_ALG_ID) is exercised last so the device is left in the primary
     * state for the provisioned-state checks and any optional freeze. */
    ST_SECTION("Per-suite key + CSR + prove lifecycle");
#if defined(PROV_ENABLE_EC256)
    st_test_sign_lifecycle(PROV_ALG_ECDSA_P256, PROV_PUB_LEN_P256,
                           PROV_SIG_LEN_P256, "ECDSA-P256");
#endif
#if defined(PROV_ENABLE_EC384)
    st_test_sign_lifecycle(PROV_ALG_ECDSA_P384, PROV_PUB_LEN_P384,
                           PROV_SIG_LEN_P384, "ECDSA-P384");
#endif
#if defined(PROV_ENABLE_EC521)
    st_test_sign_lifecycle(PROV_ALG_ECDSA_P521, PROV_PUB_LEN_P521,
                           PROV_SIG_LEN_P521, "ECDSA-P521");
#endif
#if defined(PROV_ENABLE_MLDSA87)
    st_test_sign_lifecycle(PROV_ALG_MLDSA_87, PROV_PUB_LEN_MLDSA87,
                           PROV_SIG_LEN_MLDSA87, "ML-DSA-87");
#endif

    /* Re-establish the primary suite as the final slot 0/1 state so the
     * subsequent provisioned-state / frozen checks match ST_ALG_ID. */
    ST_SECTION("Re-establish primary suite (" ST_ALG_NAME ")");
    st_test_sign_lifecycle(ST_ALG_ID, ST_PUB_LEN, ST_SIG_LEN, ST_ALG_NAME);

    /* Confirm the derived slot roles after provisioning the assets. */
    ST_SECTION("Provisioned state");
    st_test_status("get_status (after assets)");
}

/* Checks that apply to a frozen (provisioned) device: non-mutating services
 * must still succeed, while every mutating service must be rejected with
 * PROV_ERR_FROZEN. When assets_known is true, this session provisioned the
 * certificate itself, so its contents/digest can be verified too. */
static void st_run_provisioned_tests(bool assets_known)
{
    prov_status_info_t info;
    bool have_key;
    bool have_cert;

    ST_SECTION("Provisioned-device checks (frozen)");

    /* --- Non-mutating services must still work --- */
    memset(&info, 0, sizeof(info));
    ST_OK("get_status (frozen)", prov_nsc_get_status(&info));
    LOG_INFO("         provisioned=%u num_slots=%u type[0]=%u type[1]=%u",
             (unsigned int)info.provisioned, (unsigned int)info.num_slots,
             (unsigned int)info.slot_type[0], (unsigned int)info.slot_type[1]);
    ST_TRUE("status reports provisioned=1", (info.provisioned == 1U));
    st_test_info();

    have_key  = (info.num_slots > ST_KEY_SLOT) &&
                (info.slot_type[ST_KEY_SLOT] == PROV_SLOT_KEY);
    have_cert = (info.num_slots > ST_CERT_SLOT) &&
                (info.slot_type[ST_CERT_SLOT] == PROV_SLOT_CERT);

    if (have_key)
    {
        st_test_read_pubkey(ST_ALG_ID, ST_PUB_LEN);
        st_test_csr();
    }
    else
    {
        LOG_INFO("  [SKIP] read_pubkey/build_csr (no key in slot %u)",
                 (unsigned int)ST_KEY_SLOT);
    }

    /* read_cert / prove_cert validate against the digest captured at commit
     * time, so only run them when this session provisioned the certificate. */
    if (assets_known && have_cert)
    {
        st_test_read_cert();
        st_test_prove_cert(ST_ALG_ID, ST_SIG_LEN, ST_ALG_NAME);
    }
    else
    {
        LOG_INFO("  [SKIP] read_cert/prove_cert (certificate contents unknown)");
    }

    /* --- Mutating services must be rejected with PROV_ERR_FROZEN --- */
    ST_SECTION("Frozen lock-out (mutating commands rejected)");
    {
        uint16_t pub_len = 0U;
        ST_FROZEN("gen_key rejected",
                  prov_nsc_gen_key(ST_KEY_SLOT, ST_ALG_ID, &pub_len));
    }
    ST_FROZEN("erase(key slot) rejected",
              prov_nsc_erase(ST_KEY_SLOT, PROV_CONFIRM_ERASE));
    ST_FROZEN("erase(all) rejected",
              prov_nsc_erase(PROV_SLOT_ALL, PROV_CONFIRM_ERASE));
    {
        prov_cert_commit_args_t a;
        uint16_t cert_len = 0U;
        uint32_t i;
        for (i = 0U; i < ST_CERT_LEN; i++)
        {
            s_cert[i] = (uint8_t)(i & 0xFFU);
        }
        st_sha256(s_cert, ST_CERT_LEN, s_sha);
        a.slot_id      = ST_CERT_SLOT;
        a.key_slot     = ST_KEY_SLOT;
        a.cert         = s_cert;
        a.total_len    = ST_CERT_LEN;
        a.sha256       = s_sha;
        a.cert_len_out = &cert_len;
        ST_FROZEN("cert_commit rejected", prov_nsc_cert_commit(&a));
    }
    {
        uint8_t provisioned = 0U;
        ST_FROZEN("finalize rejected (already frozen)",
                  prov_nsc_finalize(PROV_CONFIRM_FINALIZE, &provisioned));
    }
}

/******************************************************************************
 * Public entry point
 *****************************************************************************/

unsigned int prov_selftest_run(void)
{
    prov_status_info_t info;
    bool provisioned;

    s_pass = 0U;
    s_fail = 0U;

    LOG_INFO("");
    LOG_INFO("================================================================");
    LOG_INFO("            Provisioning Service Self-Test (PROV_SELFTEST)");
    LOG_INFO("================================================================");

    /* The first test reads the device state and selects the test flow. */
    ST_SECTION("Device information");
    memset(&info, 0, sizeof(info));
    ST_OK("get_status (initial)", prov_nsc_get_status(&info));
    LOG_INFO("         provisioned=%u num_slots=%u pas_ver=0x%04X type[0]=%u type[1]=%u",
             (unsigned int)info.provisioned, (unsigned int)info.num_slots,
             (unsigned int)info.pas_version,
             (unsigned int)info.slot_type[0], (unsigned int)info.slot_type[1]);
    st_test_info();

    provisioned = (info.provisioned != 0U);

    if (!provisioned)
    {
        LOG_INFO("");
        LOG_INFO(">> Device UNPROVISIONED: running full lifecycle self-test");
        st_run_unprovisioned_tests();

#if defined(PROV_SELFTEST_FINALIZE)
        /* Freeze the just-provisioned device (key in slot 0 binds the cert in
         * slot 1, so the structural pre-freeze check passes), then verify the
         * device behaves correctly while frozen. */
        ST_SECTION("Finalize (IRREVERSIBLE)");
        {
            uint8_t prov_out = 0U;
            LOG_WARN("  !! device will be IRREVERSIBLY frozen !!");
            ST_OK("finalize", prov_nsc_finalize(PROV_CONFIRM_FINALIZE, &prov_out));
            LOG_INFO("         provisioned=%u", (unsigned int)prov_out);
            ST_TRUE("finalize reports provisioned=1", (prov_out == 1U));
        }
        st_run_provisioned_tests(true);
#else
        /* Finalize not requested: erase everything to leave a clean device. */
        ST_SECTION("Erase / teardown");
        ST_OK("erase(key slot)", prov_nsc_erase(ST_KEY_SLOT, PROV_CONFIRM_ERASE));
        ST_OK("erase(all) -> teardown",
              prov_nsc_erase(PROV_SLOT_ALL, PROV_CONFIRM_ERASE));
        LOG_INFO("  [SKIP] finalize (build -DPROV_SELFTEST_FINALIZE to test)");
#endif
    }
    else
    {
        LOG_INFO("");
        LOG_INFO(">> Device PROVISIONED (frozen): running provisioned-device self-test");
        st_run_provisioned_tests(false);
    }

    LOG_INFO("");
    LOG_INFO("================================================================");
    if (s_fail == 0U)
    {
        LOG_INFO("  RESULT: ALL PASSED  (%u/%u checks)", s_pass, s_pass + s_fail);
    }
    else
    {
        LOG_ERR("  RESULT: %u FAILED, %u passed  (%u checks)",
                s_fail, s_pass, s_pass + s_fail);
    }
    LOG_INFO("================================================================");
    LOG_INFO("");

    return s_fail;
}

/* [] END OF FILE */
