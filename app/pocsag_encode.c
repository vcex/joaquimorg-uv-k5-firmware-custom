/* Minimal embedded POCSAG encoder - adapted from pocsag-tool pocsag.c
 * This implementation avoids dynamic allocation and file I/O.
 */
#include "app/pocsag_encode.h"
#include <string.h>
#include <stdint.h>

// Constants from pocsag-tool
#define PREAMBLE_LENGTH 72
#define PREAMBLE_FILL 0xAA
#define FRAMESYNC_CODEWORD 0x7CD215D8U
#define IDLE_CODEWORD 0x7A89C197U
#define FUNCTION_CODE 0x03
#define NUM_BITS_INT (sizeof(uint32_t)*8 - 1)

// For simplicity, limit message length to 40 (like pocsag-tool)
#define MAX_MSG_LENGTH 40

static uint8_t bitReverse8(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

// BCH 31,21,5 (polynomial g(x)=10010110111 -> 0x769)
static void calculateBCH3121sum (uint32_t *x)
{
    #define ADDRESS_MASK 0xFFFFF800U
    #define G_X 0x769U
    const int k = 21;
    uint32_t generator = G_X << k;
    const int n = NUM_BITS_INT; //31

    *x &= ADDRESS_MASK;
    uint32_t dividend = *x;
    uint32_t mask = 1u << n;
    for (int i = 0; i < k; i++) {
        if (dividend & mask) dividend ^= generator;
        generator >>= 1;
        mask >>= 1;
    }
    *x |= dividend;
}

static void calculateEvenParity (uint32_t *x)
{
    int count = 0;
    for (unsigned int i = 1; i < NUM_BITS_INT; i++) if ((*x) & (1u << i)) count++;
    count = count % 2; // 0 if even
    *x |= (uint32_t)count;
}

static uint32_t encodeAddress(uint32_t address)
{
    address >>= 3;
    address &= 0x0007FFFFU;
    address <<= 2;
    address |= FUNCTION_CODE;
    address <<= 11;
    calculateBCH3121sum(&address);
    calculateEvenParity(&address);
    // network order in tool; for embedded we'll keep host endian when writing bytes explicitly
    return address;
}

// Packs 7-bit ascii into a buffer. outBuf must be large enough.
// Returns number of bytes written.
static int ascii7bit_pack(const char *message, uint8_t *outBuf, int outBufLen)
{
    int length = (int)strlen(message);
    if (length > MAX_MSG_LENGTH) length = MAX_MSG_LENGTH;
    int encoded_length = (int)((float)7/8 * length) + 1;
    if (encoded_length > outBufLen) return -1;
    memset(outBuf, 0, encoded_length);
    int shift = 1;
    uint8_t *curr = outBuf;
    for (int i = 0; i < length; i++) {
        uint16_t tmp = bitReverse8((uint8_t)message[i]);
        tmp &= 0x00fe;
        tmp >>= 1;
        tmp <<= shift;
        *curr |= (uint8_t)(tmp & 0x00ff);
        if (curr > outBuf) *(curr - 1) |= (uint8_t)((tmp & 0xff00) >> 8);
        shift++;
        if (shift == 8) shift = 0; else if (length > 1) curr++;
    }
    return encoded_length;
}

// Build codewords (simplified): take packed 7-bit, split into 20-bit message chunks with BCH and parity
// outWords is an array of uint32_t codewords; returns number of codewords.
static int build_codewords(uint8_t *packed, int packed_len, uint32_t *outWords, int maxWords)
{
    int chunks = (packed_len / 3) + 1;
    if (chunks > maxWords) return -1;
    memset(outWords, 0, sizeof(uint32_t) * chunks);
    uint8_t *curr = packed;
    uint8_t *end = packed + packed_len;
    for (int i = 0; i < chunks; i++) {
        uint32_t w = 0;
        int have = end - curr;
        if (have >= 3) {
            memcpy((uint8_t*)&w, curr, 3);
        } else if (have > 0) {
            memcpy((uint8_t*)&w, curr, have);
        }
        // perform similar shifts as original tool
        if (!(i % 2)) {
            if (have >= 3 && (end - curr) >= 3) curr += 2;
            w &= 0xfffff000u;
            w >>= 1;
        } else {
            if (have >= 3 && (end - curr) >= 3) curr += 3;
            w &= 0x0fffff00u;
            w <<= 3;
        }
        w |= (1u << NUM_BITS_INT);
        calculateBCH3121sum(&w);
        calculateEvenParity(&w);
        outWords[i] = w;
    }
    return chunks;
}

// Main encode function: writes preamble + framesync + batches to outBuf
int POCSAG_EncodeMessage(uint32_t address, const char *message, uint8_t *outBuf, size_t outBufSize)
{
    if (!outBuf || !message) return -1;
    // temporary buffers on stack - keep small
    uint8_t packed[64]; // should be enough for MAX_MSG_LENGTH
    int packed_len = ascii7bit_pack(message, packed, sizeof(packed));
    if (packed_len < 0) return -1;

    // build codewords
    uint32_t codewords[64];
    int nwords = build_codewords(packed, packed_len, codewords, (int)(sizeof(codewords)/sizeof(codewords[0])));
    if (nwords < 0) return -1;

    // Build output: preamble, framesync (big endian), then codewords (big endian)
    size_t idx = 0;
    if (outBufSize < PREAMBLE_LENGTH + 4 + (size_t)nwords*4) return -1;
    for (int i = 0; i < PREAMBLE_LENGTH && idx < outBufSize; i++) outBuf[idx++] = PREAMBLE_FILL;

    uint32_t fs = FRAMESYNC_CODEWORD;
    outBuf[idx++] = (uint8_t)((fs >> 24) & 0xFF);
    outBuf[idx++] = (uint8_t)((fs >> 16) & 0xFF);
    outBuf[idx++] = (uint8_t)((fs >> 8) & 0xFF);
    outBuf[idx++] = (uint8_t)(fs & 0xFF);

    // add address codeword
    uint32_t addrcw = encodeAddress(address);
    outBuf[idx++] = (uint8_t)((addrcw >> 24) & 0xFF);
    outBuf[idx++] = (uint8_t)((addrcw >> 16) & 0xFF);
    outBuf[idx++] = (uint8_t)((addrcw >> 8) & 0xFF);
    outBuf[idx++] = (uint8_t)(addrcw & 0xFF);

    for (int i = 0; i < nwords && (idx + 4) <= outBufSize; i++) {
        uint32_t w = codewords[i];
        outBuf[idx++] = (uint8_t)((w >> 24) & 0xFF);
        outBuf[idx++] = (uint8_t)((w >> 16) & 0xFF);
        outBuf[idx++] = (uint8_t)((w >> 8) & 0xFF);
        outBuf[idx++] = (uint8_t)(w & 0xFF);
    }

    return (int)idx;
}
