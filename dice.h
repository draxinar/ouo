#ifndef DICE_H_
#define DICE_H_

#include <stdint.h>

/*
 * Dice expressions ("4d8+3", "30+1d70", ...) used for weapon damage,
 * armor class, hitpoints, and NPC natural damage. A negative numDice
 * selects the "B+NdF" parse form.
 */

__extension__ typedef struct CString CString;
struct CString;

/*
 * Dice roll stored inline in a CContainer weapon slot (3 bytes).
 */
__extension__ typedef struct CWeaponDice CWeaponDice;
struct CWeaponDice {
	uint8_t numDice;        // 0x00
	uint8_t diceFaces;      // 0x01
	uint8_t bonus;          // 0x02
};

/*
 * Signed 4-byte dice roll used by CMobile for armor rating and natural
 * damage (trailing byte is padding).
 */
__extension__ typedef struct CDiceRoll CDiceRoll;
struct CDiceRoll {
	int8_t numDice;         // 0x00
	uint8_t diceFaces;      // 0x01
	int8_t bonus;           // 0x02
	uint8_t _dr_pad;        // 0x03
};

CWeaponDice *CDiceRoll_Constructor(CWeaponDice *d); // 0x00459840
CWeaponDice *CDiceRoll_InitParse(CWeaponDice *d, const char *s); // 0x00459872
int CDiceRoll_Average(CWeaponDice *d); // 0x00459896
int CDiceRoll_Roll(CWeaponDice *d); // 0x004598C3
void CDiceRoll_Clear(CWeaponDice *d); // 0x00459938
void CDiceRoll_ToString(CWeaponDice *d, CString *out); // 0x00459953
void CDiceRoll_Init(CWeaponDice *d, int8_t numDice, uint8_t faces, int8_t bonus, uint8_t unused); // 0x00459A15
CWeaponDice *CDiceRoll_InitThiscall(CWeaponDice *d, int numDice, int faces, int bonus, int unused); // 0x004599ED
int8_t CDiceRoll_GetNumDice(CWeaponDice *d); // 0x00459A3C
uint8_t CDiceRoll_GetDiceFaces(CWeaponDice *d); // 0x00459A4C
int8_t CDiceRoll_GetBonus(CWeaponDice *d); // 0x00459A5D
uint8_t CDiceRoll_GetField4(CWeaponDice *d); // 0x00459A6E
void CDiceRoll_Parse(CWeaponDice *d, const char *s); // 0x00459A7B
void CDiceRoll_Copy(CWeaponDice *dst, CWeaponDice *src); // 0x00459CAE
int CDiceRoll_IsZero(CWeaponDice *d); // 0x00459CEA

#endif /* DICE_H_ */
