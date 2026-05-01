#ifndef BBOARD_H_
#define BBOARD_H_

#include <stdint.h>

__extension__ typedef struct CBulletinBoard CBulletinBoard;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CPlayer CPlayer;
CItem *FindClosestBBoardItem(CLocation *loc); // 0x00431B48
void CBulletinBoard_Constructor(CBulletinBoard *bb); // 0x00431BE8
void CBulletinBoard_Destructor(CBulletinBoard *bb); // 0x00431C49
void OpenWritableBook(CItem *entity, CItem *player, uint32_t playerSerial); // 0x00431D4A
void CBulletinBoard_SendBoardSummary(CBulletinBoard *bb, CPlayer *player, CItem *post); // 0x00431DA7
void CBulletinBoard_SendPostBody(CBulletinBoard *bb, CPlayer *player, CItem *post); // 0x00431DEC
void CBulletinBoard_PostMessage(CBulletinBoard *bb, CPlayer *player, CItem *replyTo, char *subject, int numLines, uintptr_t *lines); // 0x00431E31
void CBulletinBoard_RemovePost(CBulletinBoard *bb, CItem *post, CMobile *mob); // 0x00432135
void CBulletinBoard_PurgeDayOldPosts(CBulletinBoard *bb, uint32_t newDay); // 0x004321CE
void *CBulletinBoard_ScalarDelete(CBulletinBoard *bb, int flags); // 0x00432230

#endif /* BBOARD_H_ */
