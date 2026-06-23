/**
 * nvm.cpp - alignment calibration persisted in MCU internal flash.
 *
 * Uses the LAST flash sector (sector 7, 128 KB @ 0x08060000 on the 512 KB STM32F407VET6) — far
 * from the ~33 KB of firmware at the start of flash, so code can grow a lot before colliding.
 * A magic word + checksum guards against an erased/garbage sector. We never read-after-write in
 * the same session (load at boot, save at align), so the flash ART cache needs no flushing.
 */
#include "nvm.h"
#include "main.h" /* HAL flash */

namespace nvm {
namespace {

constexpr uint32_t kAddr   = 0x08060000;     /* sector 7 base */
constexpr uint32_t kSector = FLASH_SECTOR_7; /* last sector of the 512 KB device */
constexpr uint32_t kMagic  = 0x52444A31;     /* 'RDJ1' */

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

bool load(Cal& out)
{
    const Blob* b = reinterpret_cast<const Blob*>(kAddr);
    if (b->magic != kMagic || checksum(*b) != b->crc) return false;
    out.zero_offset = b->zero_offset;
    out.direction   = b->direction;
    return true;
}

bool save(const Cal& in)
{
    Blob b{ kMagic, in.zero_offset, in.direction, 0 };
    b.crc = checksum(b);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef er{};
    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* 2.7-3.6 V -> 32-bit word programming */
    er.Sector       = kSector;
    er.NbSectors    = 1;

    uint32_t bad = 0;
    bool ok = (HAL_FLASHEx_Erase(&er, &bad) == HAL_OK);
    if (ok) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(&b);
        for (uint32_t i = 0; i < sizeof(Blob) / 4 && ok; ++i)
            ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, kAddr + i * 4, w[i]) == HAL_OK);
    }

    HAL_FLASH_Lock();
    return ok;
}

}  // namespace nvm
