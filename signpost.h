#ifndef SIGNPOST_H_
#define SIGNPOST_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CSignpost CSignpost;
__extension__ typedef struct VectNode VectNode;
void CSignpost_Delete(CItem *item); // 0x00484DF9
CSignpost *CSignpost_Constructor(CSignpost *this); // 0x0048AD46
void CSignpost_Destructor(CSignpost *this); // 0x0048AE85
void PlotOnMap(CSignpost *this, int command, int arg, uint16_t plotX, uint16_t plotY); // 0x0048AF39
void CSignpost_SendMapCommand(CSignpost *this, CItem *player, uint32_t playerSerial); // 0x0048B296
void CSignpost_SendMapDisplay(CSignpost *this, CItem *player, uint32_t playerSerial); // 0x0048B32A
CLocation *CSignpost_MapPinToWorldCoord(CSignpost *sp, VectNode *node); // 0x0048B3A3
int CSignpost_GetValue_VT(CItem *self, int useResource, int normalize); // 0x0048B4B3
void *CSignpost_ScalarDelete(CSignpost *sp, int flags); // 0x004912F0

#endif /* SIGNPOST_H_ */
