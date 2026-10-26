#include "songscan.h"
#include "neocrypt.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

static std::vector<uint8_t> readf(const char* p)
{
    std::vector<uint8_t> v; FILE* f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); v.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(v.data(), 1, v.size(), f) != v.size()) exit(1);
    fclose(f); return v;
}

int main(int argc, char** argv)
{
    NeoRomSet r;
    r.m1 = readf(argv[1]);
    for (int i = 2; i < argc; i++) { auto v = readf(argv[i]); r.va.insert(r.va.end(), v.begin(), v.end()); }
    { NeoCryptInfo ci = NeoPrepareM1(r.m1, NeoCrc32(r.m1.data(), r.m1.size())); NeoPrepareV(r.va, ci); if (ci.cmc50) printf("CMC50 %s pcm2=%d/%d\n", ci.name, ci.pcm2Type, ci.pcm2Value); }
    auto prof = NeoAutoProfile(r);
    if (getenv("INIT")) { prof.initCmds.clear(); for (char* q = getenv("INIT"); *q; ) { prof.initCmds.push_back((uint8_t)strtol(q, &q, 16)); if (*q) q++; } }
    if (getenv("QUIET")) { auto songs = NeoScanSongs(r, prof, 6.0, 0); int l=0; for (auto& s : songs) l += s.loop; printf("init=%s loops=%d found=%zu\n", getenv("INIT"), l, songs.size()); return 0; }
    printf("driver=%s init=%zu crc=%08x\n", prof.name, prof.initCmds.size(), NeoCrc32(r.m1.data(), r.m1.size()));
    auto t0 = std::chrono::steady_clock::now();
    auto songs = NeoScanSongs(r, prof, 6.0, getenv("TH") ? atoi(getenv("TH")) : 0);
    auto t1 = std::chrono::steady_clock::now();
    for (auto& s : songs) printf("%02x %s %5u ms keyons=%u\n", s.code, s.loop ? "LOOP" : "    ", s.lenMs, s.keyOns);
    printf("%zu found, %.2f s\n", songs.size(), std::chrono::duration<double>(t1 - t0).count());
}
