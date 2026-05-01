#ifndef ANIM_H_
#define ANIM_H_

#include <stdint.h>

#include "location.h"

__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct SeqLocNode SeqLocNode;
__extension__ typedef struct SeqCmdNode SeqCmdNode;
__extension__ typedef struct AnimSequence AnimSequence;

/*
 * Singly-linked list node holding a location for AnimSequence broadcast.
 */
struct SeqLocNode {
	CLocation loc;
	uint16_t pad;
	struct SeqLocNode *next;
};

/*
 * Singly-linked list node holding a typed command with raw payload.
 * The payload length is not stored; each handler knows its data size.
 */
struct SeqCmdNode {
	uint8_t type;
	uint8_t _pad[3];
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4];      // 64-bit alignment pad
#endif
	uint8_t *data;
	struct SeqCmdNode *next;
};

/*
 * Queued animation sequence: a list of locations to broadcast on and a
 * list of effect/animation/sound commands to dispatch.
 */
struct AnimSequence {
	int state;
#if __SIZEOF_POINTER__ == 8
	int _pad64;             // 64-bit alignment pad
#endif
	SeqLocNode *locList;
	SeqCmdNode *cmdList;
};

void AnimSequence_Process(uint8_t actionId); // 0x004CF68E
void AnimSequence_Clear(void); // 0x004D0099
void AnimSequence_AddLocation(CLocation *loc); // 0x004D013D
void AnimSequence_AddCommand(uint8_t type, const uint8_t *data, uint32_t dataSize); // 0x004D024D

#endif /* ANIM_H_ */
