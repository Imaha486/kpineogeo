// NEOGEO sound ROM decryption (CMC50 M1, NEO-PCM2 V)
#pragma once
#include <cstdint>
#include <vector>

// CMC50-encrypted M1 (KOF2000 and later). Decrypts in place.
void NeoCmc50M1Decrypt(std::vector<uint8_t>& m1);

// NEO-PCM2 V ROM decryption
void NeoPcm2Decrypt(std::vector<uint8_t>& v, int value);   // mslug4 / rotd / pnyaa type
void NeoPcm2Swap(std::vector<uint8_t>& v, int value);      // kof2002 / matrim / samsho5 / mslug5 / svc / kof2003 type

// heuristic: does this look like a NEOGEO Z80 sound driver (plain, not encrypted)?
int NeoZ80DriverScore(const std::vector<uint8_t>& m1);

struct NeoCryptInfo
{
    bool cmc50 = false;     // M1 is CMC50 encrypted
    int pcm2Type = 0;       // 0 = none, 1 = decrypt(value), 2 = swap(value)
    int pcm2Value = 0;
    const char* name = "";
};
// known encrypted sets, looked up by the CRC32 of the (encrypted) M1 file
bool NeoLookupCrypt(uint32_t m1crc, NeoCryptInfo& info);

// decrypt the M1 in place when it is a known/detected encrypted set.
// mode: 0 = never, 1 = auto, 2 = force CMC50
NeoCryptInfo NeoPrepareM1(std::vector<uint8_t>& m1, uint32_t rawCrc, int mode = 1);
// decrypt the (concatenated) V ROM according to the info returned by NeoPrepareM1
void NeoPrepareV(std::vector<uint8_t>& v, const NeoCryptInfo& info);
