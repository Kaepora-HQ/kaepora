#include "program_patch.h"

#include "bytes.h"

#include <array>
#include <bit>
#include <cstdint>
#include <span>

namespace {

struct Code {
    uint32_t address;
    std::span<const uint32_t> instructions;
};

// The program's code ends at 0x3C461D0 and its read-only data starts at 0x3C47000. The emulator maps the space between
// as code and fills it with zeros, and the routines below live there. Its own built-in patches use this space too, and
// it skips a built-in patch whose bytes a mod's patch has changed (Lichii's src/core/file_sys/patch_manager.cpp), so
// the routines stand.

// The function that adds an item to Link's pouch writes the lift level here. For an item whose PouchActorInfo row has
// the ExpansionAvility LiftGradeUP, it calls the game's set-enum function at 0x9B3BA0 with the GameData manager in x0,
// the hash of the row's LiftableGrade in w1, the PlayerStatus handle in x2 and the hash of "LiftGrade" in w3.
constexpr uint32_t LiftLevelWrite = 0x11103C0;
constexpr std::array CallKeepHigher{
    0x94ACDAD5u, // bl   0x3C46F14            KeepHigher, in place of bl 0x9B3BA0
};

constexpr std::array KeepHigherInstructions{
    0xA9BC7BFDu, // stp  x29, x30, [sp, #-0x40]!
    0x910003FDu, // mov  x29, sp
    0xA90107E0u, // stp  x0, x1, [sp, #0x10]   keeps the set-enum arguments
    0xA9020FE2u, // stp  x2, x3, [sp, #0x20]
    0xB90033FFu, // str  wzr, [sp, #0x30]
    0x9100C3E1u, // add  x1, sp, #0x30         where the get-enum function puts the level's hash
    0x9714D419u, // bl   0x17BF90              get enum, with the same manager, handle and member; true when it reads
    0xB94033E4u, // ldr  w4, [sp, #0x30]       w4 is the level's hash
    0x7200001Fu, // tst  w0, #0x1
    0xA9420FE2u, // ldp  x2, x3, [sp, #0x20]
    0xA94107E0u, // ldp  x0, x1, [sp, #0x10]
    0xA8C47BFDu, // ldp  x29, x30, [sp], #0x40  x30 returns to 0x11103C4, after the call it replaces
    0x54000200u, // b.eq set                   a level it cannot read is set, as the game sets it
    0x90001585u, // adrp x5, 0x3EF6000
    0x910540A5u, // add  x5, x5, #0x150        the game's hashes of None, Low, Middle and High, in that order
    0xAA1F03E6u, // mov  x6, xzr               x6 counts through them
    0x2A1F03E7u, // mov  w7, wzr               w7 becomes the level's rank
    0x2A1F03E8u, // mov  w8, wzr               w8 becomes the item's rank
    0xB86678A9u, // rank: ldr w9, [x5, x6, lsl #2]
    0x6B04013Fu, // cmp  w9, w4
    0x1A8700C7u, // csel w7, w6, w7, eq
    0x6B01013Fu, // cmp  w9, w1
    0x1A8800C8u, // csel w8, w6, w8, eq
    0x910004C6u, // add  x6, x6, #0x1
    0xF10010DFu, // cmp  x6, #0x4
    0x54FFFF21u, // b.ne rank
    0x6B0800FFu, // cmp  w7, w8
    0x54000048u, // b.hi keep                  a level above the item's grade stays
    0x1735B307u, // set: b 0x9B3BA0            set enum, with the arguments of the call it replaces
    0xD65F03C0u, // keep: ret
};

// The same function writes the dive depth here. For an item whose PouchActorInfo row has the ExpansionAvility
// DiveDepthUP, it calls the game's set-float function at 0x44C5D0 with the GameData manager in x0, the PlayerStatus
// handle in x1, the hash of "DiveDepth" in w2 and the row's DiveDepth in s0.
constexpr uint32_t DiveDepthWrite = 0x1110480;
constexpr std::array CallKeepDeeper{
    0x94ACD9E0u, // bl   0x3C46C00            KeepDeeper, in place of bl 0x44C5D0
};

constexpr std::array KeepDeeperInstructions{
    0xA9BD7BFDu, // stp  x29, x30, [sp, #-0x30]!
    0x910003FDu, // mov  x29, sp
    0xA90107E0u, // stp  x0, x1, [sp, #0x10]   keeps the set-float arguments
    0xB90023E2u, // str  w2, [sp, #0x20]
    0xBD0027E0u, // str  s0, [sp, #0x24]
    0x2A0203E3u, // mov  w3, w2
    0xAA0103E2u, // mov  x2, x1
    0x9100A3E1u, // add  x1, sp, #0x28         where the get-float function puts the depth Link has
    0x972704D4u, // bl   0x607F70              get float, with the same manager, handle and member; true when it reads
    0x2A0003E4u, // mov  w4, w0
    0xBD402BE1u, // ldr  s1, [sp, #0x28]       s1 is the depth Link has
    0xBD4027E0u, // ldr  s0, [sp, #0x24]
    0xB94023E2u, // ldr  w2, [sp, #0x20]
    0xA94107E0u, // ldp  x0, x1, [sp, #0x10]
    0xA8C37BFDu, // ldp  x29, x30, [sp], #0x30  x30 returns to 0x1110484, after the call it replaces
    0x36000064u, // tbz  w4, #0, set           a depth it cannot read is set, as the game sets it
    0x1E202020u, // fcmp s1, s0
    0x5400004Cu, // b.gt keep                  a depth deeper than the item's stays
    0x17201662u, // set: b 0x44C5D0            set float, with the arguments of the call it replaces
    0xD65F03C0u, // keep: ret
};

// The same function branches on the item's PouchSpecialDeal. The map, compass and boss key are NotPushToPouch, whose
// case starts here and records the item as got with no call for its dungeon. A small chest gives every item through
// this function. A large chest gives none of these three through it, so it does not set a value twice: its event flow
// calls the game's OneShotDungeonGetMap, OneShotDungeonGetCompass or OneShotDungeonGetBossKey instead.
constexpr uint32_t NotPushToPouchCase = 0x110ED88;
constexpr std::array JumpToAwardDungeonItem{
    0x14ACE00Au, // b    0x3C46DB0            AwardDungeonItem, in place of ldp x20, x8, [sp, #0x90]
};

// For the map, compass and boss key, does what the item's OneShot node does, and nothing for any other item. The jump
// lands inside the function, so the routine keeps x0 to x19, x30 and the flags, and the game functions it calls keep
// x20 to x29. It keeps no floating point register, as none is live at the jump.
constexpr std::array AwardDungeonItemInstructions{
    0xD10303FFu, // sub  sp, sp, #0xC0
    0xA90007E0u, // stp  x0, x1, [sp]
    0xA9010FE2u, // stp  x2, x3, [sp, #0x10]
    0xA90217E4u, // stp  x4, x5, [sp, #0x20]
    0xA9031FE6u, // stp  x6, x7, [sp, #0x30]
    0xA90427E8u, // stp  x8, x9, [sp, #0x40]
    0xA9052FEAu, // stp  x10, x11, [sp, #0x50]
    0xA90637ECu, // stp  x12, x13, [sp, #0x60]
    0xA9073FEEu, // stp  x14, x15, [sp, #0x70]
    0xA90847F0u, // stp  x16, x17, [sp, #0x80]
    0xA9097BF2u, // stp  x18, x30, [sp, #0x90]
    0xD53B4209u, // mrs  x9, nzcv
    0xA90A27F3u, // stp  x19, x9, [sp, #0xA0]
    0xF940AFF3u, // ldr  x19, [sp, #0x158]      the item's name, at [sp, #0x98] in the function's frame
    0xD0000D01u, // adrp x1, 0x3DE8000
    0x911DA421u, // add  x1, x1, #0x769         the game's string Item_Dungeon_Map
    0x9400002Eu, // bl   name_is
    0x350002A0u, // cbnz w0, map
    0xD503201Fu, // nop
    0x706966A1u, // adr  x1, 0x3D19AD3          Item_Dungeon_Compass
    0x9400002Au, // bl   name_is
    0x350002C0u, // cbnz w0, compass
    0x900008A1u, // adrp x1, 0x3D5A000
    0x911FFC21u, // add  x1, x1, #0x7FF         Item_Dungeon_BossKey
    0x94000026u, // bl   name_is
    0x340002C0u, // cbz  w0, done
    0x9400002Eu, // bl   dungeon
    0xB4000060u, // cbz  x0, boss_key_bool
    0x52800021u, // mov  w1, #0x1
    0x97D8F0AFu, // bl   0x32830E0              sets the dungeon's IsGetBossKey
    0x900081C8u, // boss_key_bool: adrp x8, 0x4C7E000
    0xF940C108u, // ldr  x8, [x8, #0x180]
    0xF9400100u, // ldr  x0, [x8]               the GameData manager
    0x52800021u, // mov  w1, #0x1
    0x52953702u, // mov  w2, #0xA9B8
    0x72BF5322u, // movk w2, #0xFA99, lsl #16   an unsaved Bool the boss key node sets too
    0x9728EF38u, // bl   0x682B20               set bool, by its hash
    0x1400000Au, // b    done
    0x94000022u, // map: bl dungeon
    0xB4000100u, // cbz  x0, done
    0x52800021u, // mov  w1, #0x1
    0x97D8F063u, // bl   0x3282FE0              sets the dungeon's IsGetMap
    0x14000005u, // b    done
    0x9400001Du, // compass: bl dungeon
    0xB4000060u, // cbz  x0, done
    0x52800021u, // mov  w1, #0x1
    0x97D8F07Eu, // bl   0x3283060              sets the dungeon's IsGetCompass
    0xA94A27F3u, // done: ldp x19, x9, [sp, #0xA0]
    0xD51B4209u, // msr  nzcv, x9
    0xA94007E0u, // ldp  x0, x1, [sp]
    0xA9410FE2u, // ldp  x2, x3, [sp, #0x10]
    0xA94217E4u, // ldp  x4, x5, [sp, #0x20]
    0xA9431FE6u, // ldp  x6, x7, [sp, #0x30]
    0xA94427E8u, // ldp  x8, x9, [sp, #0x40]
    0xA9452FEAu, // ldp  x10, x11, [sp, #0x50]
    0xA94637ECu, // ldp  x12, x13, [sp, #0x60]
    0xA9473FEEu, // ldp  x14, x15, [sp, #0x70]
    0xA94847F0u, // ldp  x16, x17, [sp, #0x80]
    0xA9497BF2u, // ldp  x18, x30, [sp, #0x90]
    0x910303FFu, // add  sp, sp, #0xC0
    0xA94923F4u, // ldp  x20, x8, [sp, #0x90]   the instruction the jump replaced
    0x17531FBAu, // b    0x110ED8C              the rest of the case
    0xAA1303E0u, // name_is: mov x0, x19        w0 = 1 when the string at x1 is the item's name
    0x38401402u, // next: ldrb w2, [x0], #0x1
    0x38401423u, // ldrb w3, [x1], #0x1
    0x6B03005Fu, // cmp  w2, w3
    0x54000081u, // b.ne differ
    0x35FFFF82u, // cbnz w2, next
    0x52800020u, // mov  w0, #0x1
    0xD65F03C0u, // ret
    0x2A1F03E0u, // differ: mov w0, wzr
    0xD65F03C0u, // ret
    0xA9BF7BFDu, // dungeon: stp x29, x30, [sp, #-0x10]!   x0 = the dungeon Link stands in, or 0, found as the nodes do
    0x910003FDu, // mov  x29, sp
    0x900081C8u, // adrp x8, 0x4C7E000
    0xF940BD08u, // ldr  x8, [x8, #0x178]
    0xF9400100u, // ldr  x0, [x8]
    0x973B900Fu, // bl   0xB2AF20
    0xB4000040u, // cbz  x0, found
    0x973B9035u, // bl   0xB2AFC0
    0xA8C17BFDu, // found: ldp x29, x30, [sp], #0x10
    0xD65F03C0u, // ret
};

// The routines lie in three pieces of the zero space after the program's code, around two places they leave alone.
// In the order of the checks below, the pieces run from Lichii's FPS Unlock movie routine to the code tomvita's cheats
// write after the game loads, from that code to Lichii's text speed routine, and from the text speed routine to
// Lichii's Epona jump routine (src/core/file_sys/patch_manager.cpp at nx2 8e7348b260, and
// docs/nx2/ocarina-dynamic-fps.md for the cheats).
constexpr uint32_t KeepDeeper = 0x3C46C00;
constexpr uint32_t AwardDungeonItem = 0x3C46DB0;
constexpr uint32_t KeepHigher = 0x3C46F14;
static_assert(0x3C46BFC <= KeepDeeper && KeepDeeper + 4 * KeepDeeperInstructions.size() <= 0x3C46CC0);
static_assert(0x3C46DB0 <= AwardDungeonItem && AwardDungeonItem + 4 * AwardDungeonItemInstructions.size() <= 0x3C46F00);
static_assert(0x3C46F14 <= KeepHigher && KeepHigher + 4 * KeepHigherInstructions.size() <= 0x3C46FB4);

// The emulator patches the program as its 0x100-byte NSO header followed by its memory, so an IPS offset is an address
// plus 0x100. These offsets pass 0xFFFFFF, the most plain IPS holds, so the file is IPS32.
constexpr uint32_t NsoHeaderSize = 0x100;

constexpr std::array Records{
    Code{ LiftLevelWrite, CallKeepHigher },
    Code{ KeepHigher, KeepHigherInstructions },
    Code{ DiveDepthWrite, CallKeepDeeper },
    Code{ KeepDeeper, KeepDeeperInstructions },
    Code{ NotPushToPouchCase, JumpToAwardDungeonItem },
    Code{ AwardDungeonItem, AwardDungeonItemInstructions },
};

} // namespace

std::string ProgramPatch() {
    std::string ips = "IPS32";
    for (const Code& code : Records) {
        const uint32_t offset = code.address + NsoHeaderSize;
        const uint16_t size = uint16_t(code.instructions.size_bytes());
        const size_t record = ips.size();
        ips.resize(record + sizeof(offset) + sizeof(size));
        WriteAt(ips, record, ToByteOrder(offset, std::endian::big));
        WriteAt(ips, record + sizeof(offset), ToByteOrder(size, std::endian::big));
        ips.append(reinterpret_cast<const char*>(code.instructions.data()), code.instructions.size_bytes());
    }
    return ips + "EEOF";
}
