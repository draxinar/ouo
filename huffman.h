#ifndef HUFFMAN_H_
#define HUFFMAN_H_

#include <stdint.h>

/*
 * Huffman compression for server-to-client packets.
 *
 * All UO client binaries embed a 257-entry encoding table (256 byte
 * values + one flush marker), each entry a {bit_count, bit_value}
 * uint32_t pair, which the client walks into a decode tree. The server
 * uses the same table directly for compression. Compression is applied
 * in Copy_To_CSocketBuffer when socket.comp == 1 (set after POSTLOGIN);
 * login-connection responses stay uncompressed.
 */

int HuffmanCompress(uint8_t *dst, uint8_t *src, int len);

#endif /* HUFFMAN_H_ */
