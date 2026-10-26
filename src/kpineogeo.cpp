// kpineogeo.kpi - NEOGEO (MVS/AES) sound ROM player plugin for KbMedia Player
//   plays M1 (Z80 sound program) + V (ADPCM sample) ROMs by emulating the
//   NEOGEO sound board (Z80 + YM2610) and sending sound codes to the driver.
#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "kpi.h"
#include "kpi_decoder.h"
#include "kpi_impl.h"
#include "kmp_pi.h"
#include "kpi_gcc_uuid.h"

// helper implemented in the SDK (third_party/kpisdk/kpi.cpp, built via src/kpi_sdk.cpp)
extern DWORD WINAPI kpi_CreateConfig(IKpiUnknown* pUnknown, const GUID* pGUID, DWORD* pdwPlatform, IKpiConfig** ppConfig);

#include "neoplayer.h"
#include "romfind.h"
#include "neocrypt.h"

#define KPINEOGEO_VERSION 0x00010004   // 1.04
#define SCAN_VERSION 2

static HMODULE g_hModule;

// ---------------------------------------------------------------------------
// plugin settings: stored by KbMedia Player through IKpiConfig
// ("Tools" -> "Plugin settings" -> kpineogeo.kpi). Defaults are used when the host
// does not provide IKpiConfig.
// ---------------------------------------------------------------------------
static IKpiConfig* g_cfg = nullptr;
static const wchar_t* const CFG_SECTION = L"General";

static INT64 CfgInt(const wchar_t* key, INT64 def)
{
    return g_cfg ? g_cfg->GetInt(CFG_SECTION, key, def) : def;
}
static double CfgFloat(const wchar_t* key, double def)
{
    return g_cfg ? g_cfg->GetFloat(CFG_SECTION, key, def) : def;
}
static std::wstring IniPath();

// debug log: [General] Debug=1 -> %TEMP%\kpineogeo.log
static void Log(const wchar_t* fmt, ...)
{
    if (CfgInt(L"Debug", 0) == 0) return;
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap); buf[2047] = 0;
    va_end(ap);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    FILE* f = _wfopen((std::wstring(tmp) + L"kpineogeo.log").c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"%8lu [%lu] %ls\n", GetTickCount() % 100000000, GetCurrentThreadId(), buf);
    fclose(f);
}

// ---------------------------------------------------------------------------
// settings (kpineogeo.ini next to the plugin)
// ---------------------------------------------------------------------------
static std::wstring IniPath()
{
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(g_hModule, path, MAX_PATH * 2);
    std::wstring p(path, n);
    size_t dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos) p.resize(dot);
    return p + L".ini";
}

static std::wstring IniStr(const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"")
{
    wchar_t buf[4096];
    GetPrivateProfileStringW(sec, key, def, buf, 4096, IniPath().c_str());
    return buf;
}

// "fd,0x10 20" -> bytes
static std::vector<uint8_t> ParseBytes(const std::wstring& s)
{
    std::vector<uint8_t> v;
    const wchar_t* p = s.c_str();
    while (*p)
    {
        while (*p && !iswxdigit(*p)) p++;
        if (!*p) break;
        if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) p += 2;
        wchar_t* e;
        unsigned long x = wcstoul(p, &e, 16);
        if (e == p) break;
        v.push_back((uint8_t)x);
        p = e;
    }
    return v;
}

// ---------------------------------------------------------------------------
// scan cache (memory + %LOCALAPPDATA%\kpineogeo\*.txt)
// ---------------------------------------------------------------------------
struct ScanResult
{
    std::string driver;
    std::vector<uint8_t> init, prefix;
    std::vector<NeoSong> songs;
};

static std::mutex g_cacheLock;
static std::map<std::wstring, ScanResult> g_cache;

static std::wstring CacheDir()
{
    wchar_t path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, path)))
        return L"";
    std::wstring d = std::wstring(path) + L"\\kpineogeo";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static std::wstring HexBytes(const std::vector<uint8_t>& v)
{
    std::wstring s;
    for (uint8_t b : v) { wchar_t t[4]; swprintf(t, 4, L"%02x", b); s += t; }
    return s;
}

static std::vector<uint8_t> ParseHexA(const char* p)
{
    std::vector<uint8_t> v;
    while (p[0] && p[1] && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]))
    {
        char t[3] = { p[0], p[1], 0 };
        v.push_back((uint8_t)strtoul(t, nullptr, 16));
        p += 2;
    }
    return v;
}

static bool LoadCacheFile(const std::wstring& key, ScanResult& out)
{
    std::wstring dir = CacheDir();
    if (dir.empty()) return false;
    FILE* f = _wfopen((dir + L"\\" + key + L".txt").c_str(), L"r");
    if (!f) return false;
    out = ScanResult();
    char line[256];
    bool header = false;
    while (fgets(line, sizeof(line), f))
    {
        char val[128] = "";
        unsigned code, loop, len, keys, hash;
        if (sscanf(line, "#driver %127[^\r\n]", val) == 1) { out.driver = val; header = true; }
        else if (strncmp(line, "#init", 5) == 0) { sscanf(line, "#init %127s", val); out.init = ParseHexA(val); }
        else if (strncmp(line, "#prefix", 7) == 0) { sscanf(line, "#prefix %127s", val); out.prefix = ParseHexA(val); }
        else if (sscanf(line, "%x %u %u %u %x", &code, &loop, &len, &keys, &hash) == 5)
            out.songs.push_back({ (uint8_t)code, loop != 0, len, keys, hash });
    }
    fclose(f);
    return header;
}

static void SaveCacheFile(const std::wstring& key, const ScanResult& r)
{
    std::wstring dir = CacheDir();
    if (dir.empty()) return;
    FILE* f = _wfopen((dir + L"\\" + key + L".txt").c_str(), L"w");
    if (!f) return;
    fprintf(f, "#driver %s\n#init %ls\n#prefix %ls\n", r.driver.c_str(), HexBytes(r.init).c_str(), HexBytes(r.prefix).c_str());
    for (const auto& x : r.songs)
        fprintf(f, "%02x %u %u %u %08x\n", x.code, x.loop ? 1 : 0, x.lenMs, x.keyOns, x.hash);
    fclose(f);
}

// prof: base profile with the ini overrides applied. initForced: Init= given in the ini
// (otherwise the 0x07 enable convention is probed). loadV: loads the V ROMs on demand,
// they are needed for the scan because streamed ADPCM music has to be synthesized.
static ScanResult GetScan(NeoRomSet& roms, const NeoDriverProfile& prof, bool initForced,
                          uint32_t crc, double win, const std::function<void()>& loadV)
{
    wchar_t key[160];
    swprintf(key, 160, L"%08X_v%d_%d_%ls_%ls_%02x%02x", crc, SCAN_VERSION, (int)(win * 10),
             initForced ? (L"i" + HexBytes(prof.initCmds)).c_str() : L"auto",
             HexBytes(prof.prefix).c_str(), prof.firstCode, prof.lastCode);

    std::lock_guard<std::mutex> lk(g_cacheLock);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    ScanResult r;
    const bool useFile = CfgInt(L"Cache", 1) != 0;
    if (!useFile || !LoadCacheFile(key, r))
    {
        loadV();
        NeoDriverProfile p = prof;
        if (!initForced)
        {
            NeoDriverProfile a = NeoAutoProfile(roms);
            p.initCmds = a.initCmds;
            p.name = a.name;
        }
        r.driver = p.name;
        r.init = p.initCmds;
        r.prefix = p.prefix;
        const DWORD t0 = GetTickCount();
        r.songs = NeoScanSongs(roms, p, win);
        Log(L"  scanned %zu codes in %lu ms (driver %hs, init %ls)", r.songs.size(), GetTickCount() - t0,
            r.driver.c_str(), HexBytes(r.init).c_str());
        if (useFile) SaveCacheFile(key, r);
    }
    g_cache[key] = r;
    return r;
}

// ---------------------------------------------------------------------------
// helpers for IKpiFile / IKpiFolder
// ---------------------------------------------------------------------------
static bool ReadAll(IKpiFile* f, std::vector<uint8_t>& out, UINT64 maxSize)
{
    UINT64 size = f->GetSize();
    if (size == KPI_FILE_EOF || size == 0 || size > maxSize) return false;
    out.resize((size_t)size);
    f->Seek(0, FILE_BEGIN);
    size_t got = 0;
    while (got < out.size())
    {
        DWORD want = (DWORD)std::min<size_t>(out.size() - got, 1 << 20);
        DWORD r = f->Read(out.data() + got, want);
        if (r == 0) break;
        got += r;
    }
    out.resize(got);
    return got == size;
}

static std::wstring BaseName(const std::wstring& p)
{
    size_t s = p.find_last_of(L"/\\>");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

// ---------------------------------------------------------------------------
// decoder
// ---------------------------------------------------------------------------
class NeoDecoder : public KbKpiDecoderImpl
{
public:
    DWORD Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder);

    DWORD  WINAPI Select(DWORD dwNumber, const KPI_MEDIAINFO** ppMediaInfo,
                         IKpiTagInfo* pTagInfo, DWORD dwTagGetFlags) override;
    DWORD  WINAPI Render(BYTE* pBuffer, DWORD dwSizeSample) override;
    UINT64 WINAPI Seek(UINT64 qwPosSample, DWORD dwFlag) override;
    DWORD  WINAPI UpdateConfig(void*) override { return 0; }

private:
    void setTags(IKpiTagInfo* t, DWORD flags, int index);

    NeoRomSet m_roms;
    NeoDriverProfile m_prof;
    std::vector<NeoTrack> m_tracks;
    std::unique_ptr<NeoPlayer> m_player;
    KPI_MEDIAINFO m_info{};
    uint32_t m_rate = 48000;
    double m_gain = 1.5;
    int m_cur = -1;
    bool m_started = false;
    std::wstring m_section;   // per-game ini section (M1 CRC)
    std::wstring m_album;
    std::wstring m_m1Name;
    std::string m_driver;
    NeoCryptInfo m_crypt;
    UINT64 m_rendered = 0;
};

DWORD NeoDecoder::Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder)
{
    wchar_t name[MAX_PATH * 2];
    if (pFile->GetFileName(name, sizeof(name)) == 0) return 0;
    m_m1Name = BaseName(name);
    Log(L"Open name=[%ls] request=%p folder=%p size=%lld", name, cpRequest, pFolder, (long long)pFile->GetSize());
    if (!NeoIsM1Name(m_m1Name)) return 0;

    if (!ReadAll(pFile, m_roms.m1, 0x100000)) { Log(L"  M1 read failed"); return 0; }
    if (m_roms.m1.size() < 0x4000) return 0;
    // a Z80 program never starts with an erased area
    bool allFF = true;
    for (size_t i = 0; i < 0x100; i++) if (m_roms.m1[i] != 0xff) { allFF = false; break; }
    if (allFF) return 0;

    const uint32_t crc = NeoCrc32(m_roms.m1.data(), m_roms.m1.size());
    wchar_t sec[16];
    swprintf(sec, 16, L"%08X", crc);
    m_section = sec;

    // encrypted sets (CMC50 M1 / NEO-PCM2 V: KOF2000 and later)
    {
        std::wstring c = IniStr(sec, L"Crypt", L"auto");
        const int mode = (c == L"none") ? 0 : (c == L"cmc50") ? 2 : 1;
        m_crypt = NeoPrepareM1(m_roms.m1, crc, mode);
        if (m_crypt.cmc50) Log(L"  CMC50 M1 decrypted (%hs), V: type %d value %d", m_crypt.name, m_crypt.pcm2Type, m_crypt.pcm2Value);
    }

    // driver profile (+ per-game override)
    m_prof = DetectDriver(m_roms.m1);
    std::wstring s = IniStr(sec, L"Init", L"*");
    const bool initForced = s != L"*";
    if (initForced) m_prof.initCmds = ParseBytes(s);
    s = IniStr(sec, L"Prefix", L"*");
    if (s != L"*") m_prof.prefix = ParseBytes(s);
    std::wstring range = IniStr(sec, L"Range");
    if (!range.empty())
    {
        auto r = ParseBytes(range);
        if (r.size() >= 2) { m_prof.firstCode = r[0]; m_prof.lastCode = r[1]; }
    }

    m_gain = CfgFloat(L"Gain", 1.5);
    if (m_gain < 0.1) m_gain = 0.1;
    if (m_gain > 8.0) m_gain = 8.0;
    m_album = IniStr(sec, L"Album");
    if (m_album.empty() && pFolder)
    {
        wchar_t fn[MAX_PATH * 2];
        if (pFolder->GetFolderName(fn, sizeof(fn)) > 0) m_album = fn;
    }

    // V ROMs are loaded on demand (playback, or the first scan of this M1)
    bool vLoaded = false;
    auto loadV = [&]() {
        if (vLoaded || !pFolder) return;
        vLoaded = true;
        std::vector<std::wstring> folder, la, lb;
        wchar_t fn[MAX_PATH * 2];
        for (DWORD i = 0; pFolder->EnumFiles(i, fn, sizeof(fn), 0) != 0; i++)
        {
            size_t n = wcslen(fn);
            if (n == 0 || fn[n - 1] == L'/' || fn[n - 1] == L'>') continue;
            folder.push_back(fn);
        }
        for (auto& f : folder) Log(L"  folder: [%ls]", f.c_str());
        NeoFindVRoms(m_m1Name, folder, la, lb);
        auto load = [&](const std::vector<std::wstring>& list, std::vector<uint8_t>& dst) {
            for (const auto& n : list)
            {
                IKpiFile* vf = nullptr;
                if (!pFolder->OpenFile(n.c_str(), &vf) || !vf) { Log(L"  OpenFile failed [%ls]", n.c_str()); break; }
                std::vector<uint8_t> d;
                bool ok = ReadAll(vf, d, 0x1000000);
                Log(L"  V [%ls] size=%lld read=%zu ok=%d", n.c_str(), (long long)vf->GetSize(), d.size(), ok);
                vf->Release();
                if (!ok) break;
                dst.insert(dst.end(), d.begin(), d.end());
            }
        };
        load(la, m_roms.va);
        load(lb, m_roms.vb);
        NeoPrepareV(m_roms.va, m_crypt);
        NeoPrepareV(m_roms.vb, m_crypt);
    };

    // song list (and the driver's init commands, which the scan determines)
    double win = CfgFloat(L"ScanSeconds", 6.0);
    if (win < 3.0) win = 3.0;
    if (win > 30.0) win = 30.0;
    ScanResult scan = GetScan(m_roms, m_prof, initForced, crc, win, loadV);
    m_prof.initCmds = scan.init;
    m_prof.prefix = scan.prefix;
    m_driver = scan.driver;
    m_tracks = NeoMakeTrackList(scan.songs, (int)CfgInt(L"ListMode", 0),
                                CfgInt(L"Dedupe", 1) != 0);

    std::wstring codes = IniStr(sec, L"Codes");
    if (!codes.empty())
    {
        // "04,05,18:3200" -> loop tracks unless a length in ms is given
        m_tracks.clear();
        const wchar_t* p = codes.c_str();
        while (*p)
        {
            while (*p && !iswxdigit(*p)) p++;
            if (!*p) break;
            wchar_t* e;
            unsigned long c = wcstoul(p, &e, 16);
            if (e == p) break;
            p = e;
            NeoTrack t{ (uint8_t)c, true, 0 };
            if (*p == L':') { t.lenMs = wcstoul(p + 1, &e, 10); t.loop = t.lenMs == 0; p = e; }
            m_tracks.push_back(t);
        }
    }
    if (m_tracks.empty()) return 0;

    if (cpRequest)   // opened for playback
    {
        loadV();
        if (cpRequest->dwSampleRate) m_rate = cpRequest->dwSampleRate;
        else m_rate = (uint32_t)CfgInt(L"SampleRate", 48000);
        if (m_rate < 8000) m_rate = 8000;
        if (m_rate > 192000) m_rate = 192000;
        m_player.reset(new NeoPlayer(&m_roms, m_prof, m_rate, m_gain));
    }
    Log(L"  -> %zu tracks, va=%zu vb=%zu", m_tracks.size(), m_roms.va.size(), m_roms.vb.size());
    return (DWORD)m_tracks.size();
}

void NeoDecoder::setTags(IKpiTagInfo* t, DWORD flags, int index)
{
    t->GetTagInfo(nullptr, nullptr, KPI_TAGTYPE_NONE, flags);
    if (index < 0)
    {
        if ((flags & KPI_TAGGET_FLAG_TITLE) && !m_album.empty())
            t->wSetValueW(SZ_KMP_NAME_TITLE_W, -1, m_album.c_str(), -1);
        return;
    }
    const NeoTrack& tr = m_tracks[index];
    if (flags & KPI_TAGGET_FLAG_TITLE)
    {
        wchar_t key[16], def[64];
        swprintf(key, 16, L"Title%02X", tr.code);
        swprintf(def, 64, L"%ls %02X", tr.loop ? L"BGM" : L"Sound", tr.code);
        std::wstring title = IniStr(m_section.c_str(), key, def);
        t->wSetValueW(SZ_KMP_NAME_TITLE_W, -1, title.c_str(), -1);
    }
    if ((flags & KPI_TAGGET_FLAG_ALBUM) && !m_album.empty())
        t->wSetValueW(SZ_KMP_NAME_ALBUM_W, -1, m_album.c_str(), -1);
    if (flags & KPI_TAGGET_FLAG_TRACK)
    {
        wchar_t n[16];
        swprintf(n, 16, L"%d", index + 1);
        t->wSetValueW(SZ_KMP_NAME_TRACKNUMBER_W, -1, n, -1);
        swprintf(n, 16, L"%d", (int)m_tracks.size());
        t->wSetValueW(SZ_KMP_NAME_TRACKTOTAL_W, -1, n, -1);
    }
    if (flags & KPI_TAGGET_FLAG_COMMENT)
    {
        wchar_t c[128];
        swprintf(c, 128, L"sound code 0x%02X / driver: %hs%hs / M1 CRC32 %ls", tr.code, m_driver.c_str(), m_crypt.cmc50 ? " (CMC50)" : "", m_section.c_str());
        t->wSetValueW(SZ_KMP_NAME_COMMENT_W, -1, c, -1);
    }
}

DWORD NeoDecoder::Select(DWORD dwNumber, const KPI_MEDIAINFO** ppMediaInfo,
                         IKpiTagInfo* pTagInfo, DWORD dwTagGetFlags)
{
    if (ppMediaInfo) *ppMediaInfo = nullptr;
    Log(L"Select %lu info=%p tag=%p flags=%08lx", dwNumber, ppMediaInfo, pTagInfo, dwTagGetFlags);
    if (dwNumber == 0)
    {
        if (pTagInfo) setTags(pTagInfo, dwTagGetFlags, -1);
        return 0;
    }
    if (dwNumber > m_tracks.size()) return 0;
    const int idx = (int)dwNumber - 1;
    const NeoTrack& tr = m_tracks[idx];
    m_cur = idx;
    m_started = false;

    kpi_InitMediaInfo(&m_info);
    m_info.dwNumber = dwNumber;
    m_info.dwCount = (DWORD)m_tracks.size();
    m_info.dwFormatType = KPI_MEDIAINFO::FORMAT_PCM;
    m_info.dwSampleRate = m_rate;
    m_info.nBitsPerSample = -32;
    m_info.dwChannels = 2;
    if (tr.loop)
    {
        // length unknown, loops forever: the player applies its default length + fade
        m_info.qwLength = (UINT64)-1;
        m_info.qwLoop = (UINT64)-1;
        m_info.qwFadeOut = (UINT64)-1;
    }
    else
    {
        m_info.qwLength = (UINT64)tr.lenMs * 10000;
        m_info.qwLoop = 0;
        m_info.qwFadeOut = 0;
    }
    m_info.dwUnitSample = 0;
    m_info.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE |
                             KPI_MEDIAINFO::SEEK_FLAGS_ACCURATE | KPI_MEDIAINFO::SEEK_FLAGS_ROUGH;
    if (pTagInfo) setTags(pTagInfo, dwTagGetFlags, idx);
    if (ppMediaInfo) *ppMediaInfo = &m_info;
    return dwNumber;
}

UINT64 NeoDecoder::Seek(UINT64 qwPosSample, DWORD)
{
    Log(L"Seek %llu player=%p cur=%d", (unsigned long long)qwPosSample, m_player.get(), m_cur);
    if (!m_player || m_cur < 0) return 0;
    m_started = true;
    m_rendered = qwPosSample;
    if (qwPosSample == 0) { m_player->start(m_tracks[m_cur]); return 0; }
    m_player->start(m_tracks[m_cur]);   // seek() restarts from the track it was given
    return m_player->seek(qwPosSample);
}

DWORD NeoDecoder::Render(BYTE* pBuffer, DWORD dwSizeSample)
{
    static int cnt = 0;
    if (cnt++ < 3) Log(L"Render %lu player=%p", dwSizeSample, m_player.get());
    if (!m_player || m_cur < 0) return 0;
    if (!m_started) { m_player->start(m_tracks[m_cur]); m_started = true; }
    if (pBuffer == nullptr)
    {
        std::vector<float> tmp(4096 * 2);
        DWORD done = 0;
        while (done < dwSizeSample)
        {
            DWORD n = std::min<DWORD>(dwSizeSample - done, 4096);
            DWORD r = m_player->render(tmp.data(), n);
            done += r;
            if (r < n) break;
        }
        return done;
    }
    const DWORD r = m_player->render(reinterpret_cast<float*>(pBuffer), dwSizeSample);
    m_rendered += r;
    if (r < dwSizeSample || (m_rendered / m_rate) != ((m_rendered - r) / m_rate) && (m_rendered / m_rate) % 10 == 0)
        Log(L"Render track %d: %llu samples (%.1f s)%ls", m_cur + 1, (unsigned long long)m_rendered,
            double(m_rendered) / m_rate, r < dwSizeSample ? L" END" : L"");
    return r;
}

// ---------------------------------------------------------------------------
// module
// ---------------------------------------------------------------------------
// {6E3B2A51-0D8C-4F5B-9B7E-5A1C2E7F4D60}
static const GUID KPINEOGEO_GUID =
{ 0x6e3b2a51, 0x0d8c, 0x4f5b, { 0x9b, 0x7e, 0x5a, 0x1c, 0x2e, 0x7f, 0x4d, 0x60 } };

static const wchar_t* const SUPPORT_EXTS = L".m1/.bin/.rom";

static KPI_DECODER_MODULEINFO g_info = {
    sizeof(KPI_DECODER_MODULEINFO),
    KPI_DECODER_MODULE_VERSION,
    KPINEOGEO_VERSION,
    KPI_MULTINST_INFINITE,
    KPINEOGEO_GUID,
    L"NEOGEO sound ROM (M1+V) decoder plugin  - Z80 + YM2610 (ymfm) emulation",
    L"kpineogeo (c) 2026 / ymfm (c) Aaron Giles (BSD-3) / z80 (c) Nicolas Allemand (MIT) / CMC50,PCM2 decryption from MAME (BSD-3) / KbMedia Player SDK (c) Kobarin",
    SUPPORT_EXTS,
    SUPPORT_EXTS,
    nullptr, nullptr,
    KPI_DECODER_MODULEINFO::TAG_TITLE,
    1,                                  // settings via IKpiConfig
    { 0, 0, 0, 0 }
};

class NeoDecoderModule : public KbKpiUnknownImpl<IKpiDecoderModule>
{
public:
    void WINAPI GetModuleInfo(const KPI_DECODER_MODULEINFO** ppInfo) override { *ppInfo = &g_info; }
    DWORD WINAPI Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder,
                      IKpiDecoder** ppDecoder) override
    {
        *ppDecoder = nullptr;
        NeoDecoder* d = new NeoDecoder();
        DWORD n = 0;
        try { n = d->Open(cpRequest, pFile, pFolder); }
        catch (...) { n = 0; }
        if (n == 0) { d->Release(); return 0; }
        *ppDecoder = d;
        return n;
    }
    BOOL WINAPI EnumConfig(IKpiConfigEnumerator* e) override
    {
        KPI_CFG_SECTION sec = { CFG_SECTION, L"NEOGEO サウンド",
            L"ゲームごとの曲名・曲リスト等は kpineogeo.kpi と同じフォルダの kpineogeo.ini に書きます" };
        e->EnumSection(&sec);
        auto key = [&](DWORD type, const wchar_t* k, const wchar_t* desc, const wchar_t* def,
                       const wchar_t* mn, const wchar_t* mx, const wchar_t* list,
                       const wchar_t* listDesc, const wchar_t* help) {
            KPI_CFG_KEY c = { type, CFG_SECTION, k, desc, def, mn, mx, list, listDesc, help };
            e->EnumKey(&c);
        };
        key(KPI_CFG_TYPE_INT, L"ListMode", L"曲一覧に含めるもの", L"0", nullptr, nullptr,
            L"0\t1", L"BGM とジングル\t効果音・ボイスも含めて全部",
            L"自動検出したサウンドコードのうち、どれを曲として一覧に出すか");
        key(KPI_CFG_TYPE_BOOL, L"Dedupe", L"同じ曲になるコードをまとめる", L"1", nullptr, nullptr,
            nullptr, nullptr, L"別のサウンドコードでも同じ曲が鳴る場合は 1 曲にまとめる");
        key(KPI_CFG_TYPE_FLOAT, L"Gain", L"音量の倍率", L"1.5", L"0.1", L"8.0", nullptr, nullptr, nullptr);
        key(KPI_CFG_TYPE_FLOAT, L"ScanSeconds", L"BGM とみなす秒数", L"6.0", L"3.0", L"30.0", nullptr, nullptr,
            L"曲の自動検出で、この秒数を超えて鳴り続けたものを BGM(ループ曲)とみなす");
        key(KPI_CFG_TYPE_INT, L"SampleRate", L"既定の出力周波数", L"48000", L"8000", L"192000",
            L"44100\t48000\t55556\t96000", nullptr,
            L"本体から周波数の指定がないときの出力周波数 (55556 は実機の YM2610 と同じ)");
        key(KPI_CFG_TYPE_BOOL, L"Cache", L"検出結果をキャッシュする", L"1", nullptr, nullptr, nullptr, nullptr,
            L"曲の自動検出結果を %LOCALAPPDATA%\\kpineogeo に保存して、次回から検出を省略する");
        key(KPI_CFG_TYPE_BOOL, L"Debug", L"動作ログを出力する", L"0", nullptr, nullptr, nullptr, nullptr,
            L"%TEMP%\\kpineogeo.log に動作ログを出力する(不具合調査用)");
        return TRUE;
    }
    DWORD WINAPI ApplyConfig(const wchar_t*, const wchar_t*, INT64, double, const wchar_t*) override
    {
        // all settings take effect the next time a file is opened
        return KPI_CFGRET_RELOAD_DATA;
    }
};

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = (HMODULE)hModule;
        DisableThreadLibraryCalls((HMODULE)hModule);
    }
    else if (reason == DLL_PROCESS_DETACH && g_cfg)
    {
        g_cfg->Release();
        g_cfg = nullptr;
    }
    return TRUE;
}

extern "C" HRESULT WINAPI kpi_CreateInstance(REFIID riid, void** ppvObject, IKpiUnknown* pUnknown)
{
    if (IsEqualIID(riid, IID_IKpiDecoderModule))
    {
        // IKpiConfig has to be obtained before returning from here (SDK rule)
        if (!g_cfg && pUnknown)
            kpi_CreateConfig(pUnknown, &KPINEOGEO_GUID, nullptr, &g_cfg);
        *ppvObject = static_cast<IKpiDecoderModule*>(new NeoDecoderModule());
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}
