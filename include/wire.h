/*!
    @note raw wire-format primitives (byte order, frame size limit) with no dependency on
    tetrissh itself, so libraries that only need these (e.g. logger) don't have to pull in the
    handshake/crypto layer - and so tetrissh can depend on them without creating a cycle.
*/

#ifndef CORESTACK_WIRE_H
#define CORESTACK_WIRE_H

#include <stdint.h>

#define FRAME_MAX ((64u * 1024u) - sizeof(uint32_t))

uint32_t decode_u32_be(const uint8_t buf[4]);
void encode_u32_be(uint8_t buf[4], uint32_t value);

#endif
