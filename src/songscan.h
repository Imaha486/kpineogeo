#pragma once
#include <cstdint>
#include <vector>
#include "neosnd.h"

struct NeoRomSet
{
    std::vector<uint8_t> m1;
    std::vector<uint8_t> va;   // ADPCM-A space
    std::vector<uint8_t> vb;   // ADPCM-B space (empty = same as va)
    const std::vector<uint8_t>* adpcmB() const { return vb.empty() ? &va : &vb; }
};

struct NeoDriverProfile
{
    const char* name = "generic";
    std::vector<uint8_t> initCmds;   // sent once after boot (persistent mode switches etc.)
    std::vector<uint8_t> prefix;     // sent before every song code
    int firstCode = 0x04;
    int lastCode = 0xff;
    std::vector<uint8_t> skipCodes;  // codes never used as songs (reset etc.)
};

NeoDriverProfile DetectDriver(const std::vector<uint8_t>& m1);
// DetectDriver + probing of the SNK "0x07 sound enable" convention (runs the driver briefly)
NeoDriverProfile NeoAutoProfile(const NeoRomSet& roms, unsigned threads = 0);

struct NeoSong
{
    uint8_t code;
    bool loop;            // still sounding at the end of the scan window
    uint32_t lenMs;       // estimated length (non-loop)
    uint32_t keyOns;
    uint32_t hash;        // YM write stream hash (detects duplicate codes)
};

// boot the driver and send initCmds. Leaves 'ns' ready for a song command.
void NeoBoot(NeoSound& ns, const NeoRomSet& roms, const NeoDriverProfile& prof);
// start a song on a booted driver
void NeoStartSong(NeoSound& ns, const NeoDriverProfile& prof, uint8_t code);

// scan every candidate code; threads = 0 -> hardware concurrency
std::vector<NeoSong> NeoScanSongs(const NeoRomSet& roms, const NeoDriverProfile& prof,
                                  double windowSec = 6.0, unsigned threads = 0);

uint32_t NeoCrc32(const uint8_t* p, size_t n);
