#ifndef SHA256_H_
#define SHA256_H_

#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
	uint32_t state[8];
	uint64_t count;
	uint8_t buf[SHA256_BLOCK_SIZE];
} Sha256Ctx;

void sha256_init(Sha256Ctx *ctx);
void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256Ctx *ctx, uint8_t *digest);

#endif /* SHA256_H_ */
