#ifndef MAGICFACTORY_H_
#define MAGICFACTORY_H_

#include <stdint.h>

#include "resbank.h"

__extension__ typedef struct CEffectTableEntry CEffectTableEntry;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CItemEffectDef CItemEffectDef;
__extension__ typedef struct CMagicItemFactory CMagicItemFactory;
__extension__ typedef struct CMagicItemList CMagicItemList;
__extension__ typedef struct CNamedProperty CNamedProperty;
__extension__ typedef struct CSmartPtr CSmartPtr;
__extension__ typedef struct CString CString;
__extension__ typedef struct CStringList CStringList;

/*
 * One named property or effect the magic-item factory can attach to
 * an item (0x50 bytes). minVal/maxVal default to -1 meaning unset.
 */
struct CItemEffectDef {
	CString name;           // 0x00
	CString category;       // 0x10
	CString subcat;         // 0x20
	int minVal;             // 0x30
	int maxVal;             // 0x34
	CResList sublist1;      // 0x38
	CResList sublist2;      // 0x44
};

/*
 * Weighted effect-table row used during magic-item generation
 * (0x2C bytes).
 */
struct CEffectTableEntry {
	int type;               // 0x00
#if __SIZEOF_POINTER__ == 8
	int _pad64;             // 64-bit alignment pad
#endif
	CString name;           // 0x04
	CStringList entries;    // 0x14
	int weight;             // 0x24
	int direction;          // 0x28 - init 1
};

/*
 * Small named value slot (0x18 bytes) used by the factory's property
 * lists.
 */
struct CNamedProperty {
	CString name;           // 0x00
	uint32_t field_10;      // 0x10
	uint32_t field_14;      // 0x14
};

/*
 * Singleton magic-item factory at g_MagicItemFactory (0x00697A50,
 * 0x884 bytes). Holds the four CResManager banks that drive item
 * generation plus global effect-ID bounds.
 */
struct CMagicItemFactory {
	CResList globalScripts;  // 0x000
	CResList itemList;       // 0x00C
	CResManager includeRM;   // 0x018
	CResManager keysRM;      // 0x230
	CResManager propsRM;     // 0x448
	CResManager effectTblRM; // 0x660
	int minEffectId;         // 0x878 - init -1
	int maxEffectId;         // 0x87C - init -1
	int debugFlag;           // 0x880
};

void CMagicItemFactory_Constructor(CMagicItemFactory *factory); // 0x004384E0
int CMagicItemFactory_ExecuteScript(CMagicItemFactory *factory, CString *buf); // 0x0043B6BC
int CMagicItemFactory_Create(CMagicItemList *magicList, int minLevel, int maxLevel, void *searchCtx, int forceThreshold, int maxEffects); // 0x0043D10D
int CMagicItemFactory_AttachGlobalScripts(CMagicItemFactory *factory, CItem *entity); // 0x0043D4CE
CItemEffectDef *CItemEffectDef_ScalarDelete(CItemEffectDef *def, int flags); // 0x0043D550
CEffectTableEntry *CEffectTableEntry_ConstructorWeight(CEffectTableEntry *this, int weight); // 0x0043F360
CEffectTableEntry *CEffectTableEntry_ScalarDeleteE(CEffectTableEntry *this, int flags); // 0x00441C20
void *CString_ScalarDelete_MF(CString *this, int flags); // 0x00466EA0
void *SmartPtr_CString_ScalarDelete(CSmartPtr *this, int flags); // 0x00466FA0

#endif /* MAGICFACTORY_H_ */
