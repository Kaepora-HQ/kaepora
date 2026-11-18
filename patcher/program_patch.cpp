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

// The function that adds an item to Link's pouch writes the lift level here. For an item whose PouchActorInfo row has
// the ExpansionAvility LiftGradeUP, it calls the game's set-enum function at 0x9B3BA0 with the GameData manager in x0,
// the hash of the row's LiftableGrade in w1, the PlayerStatus handle in x2 and the hash of "LiftGrade" in w3.
constexpr uint32_t LiftLevelWrite = 0x11103C0;
constexpr std::array CallKeepHigher{
    0x94ACDAD0u, // bl   0x3C46F00            KeepHigher, in place of bl 0x9B3BA0
};

// The program's code ends at 0x3C461D0 and its read-only data starts at 0x3C47000. The emulator maps the space between
// as code and fills it with zeros. Its own built-in patches use this space too, and it skips a built-in patch whose
// bytes a mod's patch has changed (Lichii's src/core/file_sys/patch_manager.cpp), so this routine stands.
constexpr uint32_t KeepHigher = 0x3C46F00;
constexpr std::array KeepHigherInstructions{
    0xA9BC7BFDu, // stp  x29, x30, [sp, #-0x40]!
    0x910003FDu, // mov  x29, sp
    0xA90107E0u, // stp  x0, x1, [sp, #0x10]   keeps the set-enum arguments
    0xA9020FE2u, // stp  x2, x3, [sp, #0x20]
    0xB90033FFu, // str  wzr, [sp, #0x30]
    0x9100C3E1u, // add  x1, sp, #0x30         where the get-enum function puts the level's hash
    0x9714D41Eu, // bl   0x17BF90              get enum, with the same manager, handle and member; true when it reads
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
    0x1735B30Cu, // set: b 0x9B3BA0            set enum, with the arguments of the call it replaces
    0xD65F03C0u, // keep: ret
};

// The emulator patches the program as its 0x100-byte NSO header followed by its memory, so an IPS offset is an address
// plus 0x100. These offsets pass 0xFFFFFF, the most plain IPS holds, so the file is IPS32.
constexpr uint32_t NsoHeaderSize = 0x100;

} // namespace

std::string ProgramPatch() {
    std::string ips = "IPS32";
    for (const Code& code : { Code{ LiftLevelWrite, CallKeepHigher }, Code{ KeepHigher, KeepHigherInstructions } }) {
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
