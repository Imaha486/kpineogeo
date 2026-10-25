#include "songscan.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>

static constexpr uint32_t RATE = NeoSound::YM_CLOCK / 144;   // 55555
static constexpr uint32_t BOOT_FRAMES = RATE / 2;            // 0.5 s
static constexpr uint32_t CMD_GAP = RATE / 60;               // one 68k frame

static bool contains(const std::vector<uint8_t>& d, const char* s)
{
    const size_t n = std::strlen(s);
    if (d.size() < n) return false;
    return std::search(d.begin(), d.end(), s, s + n) != d.end();
}

NeoDriverProfile DetectDriver(const std::vector<uint8_t>& m1)
{
    NeoDriverProfile p;
    p.skipCodes = { 0x01, 0x03 };   // slot switch / soft reset (BIOS protocol)
    if (contains(m1, "ADK Corporation") || contains(m1, "ADK CORPORATION"))
    {
        // ADK driver: 0xFD selects the music command table persistently
        p.name = "ADK";
        p.initCmds = { 0xfd };
        p.firstCode = 0x02;
        p.lastCode = 0xef;
    }
    return p;
}

void NeoBoot(NeoSound& ns, const NeoRomSet& roms, const NeoDriverProfile& prof)
{
    ns.setRoms(&roms.m1, &roms.va, roms.adpcmB());
    ns.reset();
    ns.skip(BOOT_FRAMES);
    for (uint8_t c : prof.initCmds)
    {
        ns.sendCommand(c);
        ns.skip(CMD_GAP);
    }
}

void NeoStartSong(NeoSound& ns, const NeoDriverProfile& prof, uint8_t code)
{
    for (uint8_t c : prof.prefix)
    {
        ns.sendCommand(c);
        ns.skip(CMD_GAP);
    }
    ns.sendCommand(code);
}

static std::vector<int> CandidateCodes(const NeoDriverProfile& prof)
{
    std::vector<int> codes;
    for (int c = prof.firstCode; c <= prof.lastCode; c++)
        if (std::find(prof.skipCodes.begin(), prof.skipCodes.end(), c) == prof.skipCodes.end())
            codes.push_back(c);
    return codes;
}

static void RunParallel(size_t count, unsigned threads, const std::function<void(NeoSound&, size_t)>& fn,
                        const NeoRomSet& roms)
{
    std::atomic<size_t> next{ 0 };
    auto worker = [&]()
    {
        std::unique_ptr<NeoSound> ns(new NeoSound());
        ns->setRoms(&roms.m1, &roms.va, roms.adpcmB());
        for (;;)
        {
            const size_t i = next++;
            if (i >= count) break;
            fn(*ns, i);
        }
    };
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    threads = std::min<unsigned>(threads, 16);
    std::vector<std::thread> th;
    for (unsigned t = 1; t < threads; t++) th.emplace_back(worker);
    worker();
    for (auto& t : th) t.join();
}

// phase 1: does the code cause any key-on within 1 s? (Z80 only, very fast)
static bool QuickActive(NeoSound& ns, const NeoSound::State& init, const NeoDriverProfile& prof, uint8_t code)
{
    ns.load(init);
    for (uint8_t c : prof.prefix) { ns.sendCommand(c); ns.runFast(CMD_GAP); }
    ns.sendCommand(code);
    ns.clearActivity();
    for (uint32_t t = 0; t < RATE; t += RATE / 20)
    {
        ns.runFast(RATE / 20);
        if (ns.keyOnCount() > 0) return true;
    }
    return false;
}

static void Snapshot(const NeoRomSet& roms, const NeoDriverProfile& prof, NeoSound::State& st)
{
    std::unique_ptr<NeoSound> boot(new NeoSound());
    NeoBoot(*boot, roms, prof);
    boot->save(st);
}

NeoDriverProfile NeoAutoProfile(const NeoRomSet& roms, unsigned threads)
{
    NeoDriverProfile base = DetectDriver(roms.m1);
    if (std::strcmp(base.name, "generic") != 0)
        return base;

    // Many SNK-standard drivers (e.g. "by Mr.Pac", "by MAKOTO") stay muted until the game
    // sends 0x07. Try without and with it and keep whichever wakes up more codes.
    auto countActive = [&](const NeoDriverProfile& p) -> size_t {
        NeoSound::State init;
        Snapshot(roms, p, init);
        // the init commands alone must not start a sound
        {
            std::unique_ptr<NeoSound> ns(new NeoSound());
            ns->setRoms(&roms.m1, &roms.va, roms.adpcmB());
            ns->load(init);
            ns->clearActivity();
            ns->runFast(RATE * 3 / 2);
            if (!p.initCmds.empty() && ns->keyOnCount() > 0) return 0;
        }
        std::vector<int> codes = CandidateCodes(p);
        std::atomic<size_t> n{ 0 };
        RunParallel(codes.size(), threads, [&](NeoSound& ns, size_t i) {
            if (QuickActive(ns, init, p, (uint8_t)codes[i])) n++;
        }, roms);
        return n;
    };
    const size_t plain = countActive(base);
    NeoDriverProfile en = base;
    en.name = "SNK (07 enable)";
    en.initCmds = { 0x07 };
    const size_t enabled = countActive(en);
    return enabled > plain * 2 + 3 ? en : base;
}

std::vector<NeoSong> NeoScanSongs(const NeoRomSet& roms, const NeoDriverProfile& prof,
                                  double windowSec, unsigned threads)
{
    const std::vector<int> codes = CandidateCodes(prof);
    NeoSound::State init;
    Snapshot(roms, prof, init);

    std::vector<NeoSong> result(codes.size());
    std::vector<char> valid(codes.size(), 0);

    const uint32_t chunk = RATE / 20;
    const uint64_t window = uint64_t(windowSec * RATE);
    const uint64_t silentEnd = RATE * 2;        // silent (and no key-on) for 2 s -> finished
    const bool haveV = !roms.va.empty();

    RunParallel(codes.size(), threads, [&](NeoSound& ns, size_t i)
    {
        const uint8_t code = (uint8_t)codes[i];
        if (!QuickActive(ns, init, prof, code)) return;

        // phase 2: real synthesis, so streamed ADPCM music (one key-on, then minutes of sound)
        // and ADPCM end-of-sample interrupts behave as on the real board
        ns.load(init);
        for (uint8_t c : prof.prefix) { ns.sendCommand(c); ns.skip(CMD_GAP); }
        ns.sendCommand(code);
        ns.clearActivity();
        std::vector<int32_t> buf(chunk * 2);
        uint64_t t = 0, lastActive = 0;
        bool any = false, loop = false;
        for (;;)
        {
            const uint32_t keysBefore = ns.keyOnCount();
            ns.generate(buf.data(), chunk);
            t += chunk;
            bool active = ns.keyOnCount() != keysBefore;
            if (haveV || !active)
            {
                int64_t e = 0;
                for (uint32_t k = 0; k < chunk * 2; k++) e += int64_t(buf[k]) * buf[k];
                if (e / (chunk * 2) > 64 * 64) active = true;
            }
            if (active) { any = true; lastActive = t; }
            if (!any && t >= RATE) break;
            if (any && t - lastActive >= silentEnd) break;
            if (t >= window) { loop = any && t - lastActive < RATE; break; }
        }
        if (!any) return;
        NeoSong s;
        s.code = code;
        s.loop = loop;
        s.keyOns = ns.keyOnCount();
        s.hash = ns.writeHash();
        s.lenMs = (uint32_t)(lastActive * 1000 / RATE);
        result[i] = s;
        valid[i] = 1;
    }, roms);

    std::vector<NeoSong> out;
    for (size_t i = 0; i < codes.size(); i++)
        if (valid[i]) out.push_back(result[i]);
    return out;
}

uint32_t NeoCrc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < n; i++)
    {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320 & (0u - (c & 1)));
    }
    return ~c;
}
