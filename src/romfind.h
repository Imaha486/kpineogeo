#pragma once
#include <string>
#include <vector>

// Returns true if 'name' (file name without directory) looks like a NEOGEO M1 ROM.
bool NeoIsM1Name(const std::wstring& name);

// Given the M1 file name and the names in the same folder, pick the V ROMs.
// listA: ADPCM-A ROMs in order; listB: ADPCM-B ROMs (empty = shared with A)
void NeoFindVRoms(const std::wstring& m1Name, const std::vector<std::wstring>& folder,
                  std::vector<std::wstring>& listA, std::vector<std::wstring>& listB);
