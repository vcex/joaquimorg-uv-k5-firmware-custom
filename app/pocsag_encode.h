/* Minimal POCSAG encoder header - embedded friendly */
#ifndef APP_POCSAG_ENCODE_H
#define APP_POCSAG_ENCODE_H

#include <stdint.h>
#include <stddef.h>

// Encode a message into POCSAG binary stream.
// outBuf must be provided by caller. Returns number of bytes written, or -1 on error.
int POCSAG_EncodeMessage(uint32_t address, const char *message, uint8_t *outBuf, size_t outBufSize);

#endif // APP_POCSAG_ENCODE_H
