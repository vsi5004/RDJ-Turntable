/**
 * nvm.cpp - per-motor alignment calibration persisted in MCU internal flash.
 *
 * Each motor's alignment lives in its OWN dedicated 128 KB sector near the top of the 512 KB
 * STM32F407VET6 flash, far from the ~33 KB of firmware at the start — so they erase/program
 * independently (no read-modify-write) and code can grow a lot before colliding:
 *   - Platter -> sector 7 @ 0x08060000
 *   - Tonearm -> sector 6 @ 0x08040000
 * A magic word + checksum guards against an erased/garbage sector. We never read-after-write in
 * the same session (load at boot, save at align), so the flash ART cache needs no flushing.
 */
#include "nvm.h"
#include "main.h" /* HAL flash */

namespace nvm {
namespace {

constexpr uint32_t kMagic = 0x52444A31; /* 'RDJ1' */

struct SlotInfo {
    uint32_t addr;
    uint32_t sector;
};

constexpr SlotInfo kSlots[] = {
    { 0x08060000, FLASH_SECTOR_7 }, /* Slot::Platter */
    { 0x08040000, FLASH_SECTOR_6 }, /* Slot::Tonearm */
};

const SlotInfo& slot_info(Slot slot)
{
    return kSlots[static_cast<uint8_t>(slot)];
}

struct Blob {
    uint32_t magic;
    float    zero_offset;
    int32_t  direction;
    uint32_t crc;        /* sum of the three words above */
};

uint32_t checksum(const Blob& b)
{
    const uint32_t* w = reinterpret_cast<const uint32_t*>(&b);
    return w[0] + w[1] + w[2];
}

}  // namespace

bool load(Slot slot, Cal& out)
{
    const Blob* b = reinterpret_cast<const Blob*>(slot_info(slot).addr);
    if (b->magic != kMagic || checksum(*b) != b->crc) return false;
    out.zero_offset = b->zero_offset;
    out.direction   = b->direction;
    return true;
}

bool save(Slot slot, const Cal& in)
{
    const uint32_t addr = slot_info(slot).addr;
    Blob b{ kMagic, in.zero_offset, in.direction, 0 };
    b.crc = checksum(b);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef er{};
    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* 2.7-3.6 V -> 32-bit word programming */
    er.Sector       = slot_info(slot).sector;
    er.NbSectors    = 1;

    uint32_t bad = 0;
    bool ok = (HAL_FLASHEx_Erase(&er, &bad) == HAL_OK);
    if (ok) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(&b);
        for (uint32_t i = 0; i < sizeof(Blob) / 4 && ok; ++i)
            ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i * 4, w[i]) == HAL_OK);
    }

    HAL_FLASH_Lock();
    return ok;
}

}  // namespace nvm
