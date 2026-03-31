/**
 * @file pack.h
 * @brief Stream packet packing and unpacking utilities
 */
#pragma once

/**
 * @union stream_u
 * @brief 64-bit container for structured binary payloads.
 *
 * Layout & Mixed-Type Composition
 * Enables "mixed-mode" packing within an 8-byte block. A single block can
 * partition memory into any combination of 8, 16, or 32-bit fields
 * (e.g., a uint8_t header followed by three uint16_t values).
 *
 * The first byte (u8[0]) is a 'Control Header'.
 * For payloads exceeding 64 bits, stream_t objects are processed as an array:
 * - Continue Bit (BIT 0): By convention, BIT(0) of the header (u8[0])
 *   indicates that the next stream_t element in the array contains a
 *   continuation of the current payload.
 * - End of Chain: A cleared BIT(0) in the current block's header
 *   signals that it is the 'last packet' in the chain.
 * Bits in this header act as a presence mask for the payload fields:
 * - Field Validation: A bit is set in the header only if the
 *   corresponding field contains valid data.
 * - Consumer Parsing: The receiver uses this bitmask to determine
 *   which offsets to read, ignoring "empty" or "don't care" bytes.
 * Bulk/Raw Data Extension
 *  The chain can be extended to carry raw 64-bit payloads at the user's
 *  discretion. A field within the "last" standard packet (the one with
 *  BIT(0) cleared) can be defined to contain the length, size, or count
 *  of subsequent raw 64-bit blocks. This allows for a hybrid approach
 *  where structured metadata is followed by a contiguous raw buffer.
 */
typedef union stream_u {
    uint32_t u32[2];
    uint16_t u16[4];
    uint8_t  u8[8];
    uint64_t all;
} stream_t;
