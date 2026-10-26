// kpihost - minimal KbMedia Player stand-in for testing kpineogeo.kpi (Windows / Wine)
//
//   kpihost [-k plugin.kpi] [-r rate] [-s seconds] [-p seek_seconds] [-o out.wav]
//           [-c Key=Value ...] m1file [track]
//
//   track omitted : list the tracks (tag-only open, like the Media Explorer)
//   track given   : open for playback, render, optionally write a 16-bit WAV
//   -c Key=Value  : plugin setting (General section) served through IKpiConfig
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "kpi.h"
#include "kpi_decoder.h"
#include "kpi_impl.h"
#include "kpi_gcc_uuid.h"

namespace {

std::wstring dirOf(const std::wstring& p)
{
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}
std::wstring nameOf(const std::wstring& p)
{
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}
DWORD putString(const std::wstring& s, wchar_t* out, DWORD bytes)
{
    const DWORD need = DWORD((s.size() + 1) * sizeof(wchar_t));
    if (bytes < need) { if (bytes >= 2) out[0] = 0; return need; }
    memcpy(out, s.c_str(), need);
    return need;
}

// whole file in memory; good enough for ROM images
class MemFile : public KbKpiFileImpl
{
public:
    static MemFile* load(const std::wstring& path)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return nullptr;
        LARGE_INTEGER size;
        GetFileSizeEx(h, &size);
        MemFile* f = new MemFile();
        f->name_ = nameOf(path);
        f->data_.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ReadFile(h, f->data_.data(), (DWORD)f->data_.size(), &got, nullptr);
        CloseHandle(h);
        return f;
    }
    DWORD WINAPI Read(void* buf, DWORD n) override
    {
        const UINT64 left = pos_ < data_.size() ? data_.size() - pos_ : 0;
        const DWORD k = (DWORD)(n < left ? n : left);
        memcpy(buf, data_.data() + pos_, k);
        pos_ += k;
        return k;
    }
    UINT64 WINAPI Seek(INT64 off, DWORD origin) override
    {
        INT64 base = origin == FILE_CURRENT ? (INT64)pos_ : origin == FILE_END ? (INT64)data_.size() : 0;
        INT64 np = base + off;
        if (np < 0 || (UINT64)np > data_.size()) return KPI_FILE_EOF;
        return pos_ = (UINT64)np;
    }
    UINT64 WINAPI GetSize() override { return data_.size(); }
    DWORD WINAPI GetFileName(wchar_t* out, DWORD bytes) override { return putString(name_, out, bytes); }
private:
    std::vector<BYTE> data_;
    UINT64 pos_ = 0;
    std::wstring name_;
};

// a plain directory
class DirFolder : public KbKpiFolderImpl
{
public:
    explicit DirFolder(const std::wstring& dir) : dir_(dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names_.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    DWORD WINAPI GetFolderName(wchar_t* out, DWORD bytes) override { return putString(nameOf(dir_), out, bytes); }
    DWORD WINAPI EnumFiles(DWORD i, wchar_t* out, DWORD bytes, DWORD) override
    {
        if (i >= names_.size()) { if (bytes >= 2) out[0] = 0; return 0; }
        return putString(names_[i], out, bytes);
    }
    BOOL WINAPI OpenFile(const wchar_t* name, IKpiFile** pp) override
    {
        *pp = MemFile::load(dir_ + L"\\" + name);
        return *pp != nullptr;
    }
    BOOL WINAPI OpenFolder(const wchar_t*, IKpiFolder** pp) override { *pp = nullptr; return FALSE; }
private:
    std::wstring dir_;
    std::vector<std::wstring> names_;
};

// settings given on the command line
class MapConfig : public KbKpiUnknownImpl<IKpiConfig>
{
public:
    std::map<std::wstring, std::wstring> values;
    const std::wstring* find(const wchar_t* key) const
    {
        auto it = values.find(key);
        return it == values.end() ? nullptr : &it->second;
    }
    void   WINAPI SetInt(const wchar_t*, const wchar_t* k, INT64 v) override { values[k] = std::to_wstring(v); }
    INT64  WINAPI GetInt(const wchar_t*, const wchar_t* k, INT64 d) override { auto v = find(k); return v ? _wtoi64(v->c_str()) : d; }
    void   WINAPI SetFloat(const wchar_t*, const wchar_t* k, double v) override { values[k] = std::to_wstring(v); }
    double WINAPI GetFloat(const wchar_t*, const wchar_t* k, double d) override { auto v = find(k); return v ? _wtof(v->c_str()) : d; }
    void   WINAPI SetStr(const wchar_t*, const wchar_t* k, const wchar_t* v) override { values[k] = v; }
    DWORD  WINAPI GetStr(const wchar_t*, const wchar_t* k, wchar_t* out, DWORD bytes, const wchar_t* d) override
    {
        auto v = find(k);
        return putString(v ? *v : std::wstring(d ? d : L""), out, bytes);
    }
    void   WINAPI SetBin(const wchar_t*, const wchar_t*, const BYTE*, DWORD) override {}
    DWORD  WINAPI GetBin(const wchar_t*, const wchar_t*, BYTE* p, DWORD n) override { memset(p, 0, n); return 0; }
};

class Provider : public KbKpiUnknownImpl<IKpiUnkProvider>
{
public:
    explicit Provider(MapConfig* c) : cfg_(c) {}
    DWORD WINAPI CreateInstance(REFIID riid, void*, void*, void*, void*, void** out) override
    {
        if (IsEqualIID(riid, IID_IKpiConfig)) { cfg_->AddRef(); *out = static_cast<IKpiConfig*>(cfg_); return 1; }
        *out = nullptr;
        return 0;
    }
private:
    MapConfig* cfg_;
};

// prints the tags the plugin reports
class TagPrinter : public KbKpiUnknownImpl<IKpiTagInfo>
{
public:
    DWORD WINAPI GetTagInfo(IKpiFile*, IKpiFolder*, DWORD, DWORD) override { return 1; }
    DWORD WINAPI GetValue(const wchar_t*, wchar_t* p, int) override { p[0] = 0; return 0; }
    void WINAPI SetOverwrite(BOOL) override {}
    void WINAPI SetPicture(DWORD, const wchar_t*, const wchar_t*, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD) override {}
    void WINAPI aSetValueA(const char*, int, const char*, int) override {}
    void WINAPI aSetValueW(const char*, int, const wchar_t*, int) override {}
    void WINAPI aSetValueU8(const char*, int, const char*, int) override {}
    void WINAPI wSetValueA(const wchar_t*, int, const char*, int) override {}
    void WINAPI wSetValueW(const wchar_t* n, int, const wchar_t* v, int) override { wprintf(L"    %ls = %ls\n", n, v); }
    void WINAPI wSetValueU8(const wchar_t*, int, const char*, int) override {}
    void WINAPI u8SetValueA(const char*, int, const char*, int) override {}
    void WINAPI u8SetValueW(const char*, int, const wchar_t*, int) override {}
    void WINAPI u8SetValueU8(const char*, int, const char*, int) override {}
};

// prints what the plugin offers for the settings dialog
class ConfigLister : public KbKpiUnknownImpl<IKpiConfigEnumerator>
{
public:
    BOOL WINAPI EnumSection(const KPI_CFG_SECTION* s) override { wprintf(L"[%ls] %ls\n", s->cszSection, s->cszSecDesc ? s->cszSecDesc : L""); return TRUE; }
    BOOL WINAPI EnumKey(const KPI_CFG_KEY* k) override
    {
        wprintf(L"  %-12ls type=%lu default=%ls  (%ls)\n", k->cszKey, k->dwType, k->cszDefault ? k->cszDefault : L"",
                k->cszKeyDesc ? k->cszKeyDesc : L"");
        return TRUE;
    }
};

void writeWav(const wchar_t* path, const std::vector<float>& pcm, DWORD rate)
{
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    const DWORD bytes = DWORD(pcm.size() * 2);
    auto u32 = [&](DWORD v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](WORD v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    for (float x : pcm)
    {
        int v = (int)(x * 32767.0f);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        u16((WORD)(short)v);
    }
    fclose(f);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring kpi = L"kpineogeo.kpi", out, m1;
    DWORD rate = 44100, track = 0;
    double seconds = 10, seekSec = 0;
    MapConfig* cfg = new MapConfig();
    for (int i = 1; i < argc; i++)
    {
        std::wstring a = argv[i];
        if (a == L"-k" && i + 1 < argc) kpi = argv[++i];
        else if (a == L"-r" && i + 1 < argc) rate = _wtoi(argv[++i]);
        else if (a == L"-s" && i + 1 < argc) seconds = _wtof(argv[++i]);
        else if (a == L"-p" && i + 1 < argc) seekSec = _wtof(argv[++i]);
        else if (a == L"-o" && i + 1 < argc) out = argv[++i];
        else if (a == L"-c" && i + 1 < argc)
        {
            std::wstring kv = argv[++i];
            size_t eq = kv.find(L'=');
            if (eq != std::wstring::npos) cfg->values[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        else if (m1.empty()) m1 = a;
        else track = _wtoi(a.c_str());
    }
    if (m1.empty()) { fwprintf(stderr, L"usage: kpihost [-k kpi] [-r rate] [-s sec] [-p seek] [-o wav] [-c Key=Value] m1 [track]\n"); return 2; }

    HMODULE dll = LoadLibraryW(kpi.c_str());
    if (!dll) { fwprintf(stderr, L"cannot load %ls (%lu)\n", kpi.c_str(), GetLastError()); return 1; }
    typedef HRESULT(WINAPI* CreateFn)(REFIID, void**, IKpiUnknown*);
    CreateFn create = (CreateFn)GetProcAddress(dll, "kpi_CreateInstance");
    Provider* provider = new Provider(cfg);
    IKpiDecoderModule* module = nullptr;
    if (!create || create(IID_IKpiDecoderModule, (void**)&module, provider) != S_OK || !module)
    {
        fwprintf(stderr, L"kpi_CreateInstance failed\n");
        return 1;
    }
    const KPI_DECODER_MODULEINFO* info = nullptr;
    module->GetModuleInfo(&info);
    wprintf(L"%ls\n  exts %ls, settings %lu\n", info->cszDescription, info->cszSupportExts, info->dwSupportConfig);
    if (info->dwSupportConfig)
    {
        ConfigLister lister;
        module->EnumConfig(&lister);
    }

    DirFolder* folder = new DirFolder(dirOf(m1));
    MemFile* file = MemFile::load(m1);
    if (!file) { fwprintf(stderr, L"cannot read %ls\n", m1.c_str()); return 1; }

    KPI_MEDIAINFO req;
    kpi_InitMediaInfo(&req);
    req.dwSampleRate = rate;
    IKpiDecoder* dec = nullptr;
    const DWORD t0 = GetTickCount();
    const DWORD count = module->Open(track ? &req : nullptr, file, folder, &dec);
    wprintf(L"Open: %lu tracks (%lu ms)\n", count, GetTickCount() - t0);
    if (!count || !dec) return 1;

    TagPrinter tags;
    if (track == 0)
    {
        dec->Select(0, nullptr, &tags, KPI_TAGGET_FLAG_ALL);
        for (DWORD i = 1; i <= count; i++)
        {
            wprintf(L"#%lu\n", i);
            dec->Select(i, nullptr, &tags, KPI_TAGGET_FLAG_TITLE | KPI_TAGGET_FLAG_COMMENT);
        }
    }
    else
    {
        const KPI_MEDIAINFO* mi = nullptr;
        if (dec->Select(track, &mi, &tags, KPI_TAGGET_FLAG_ALL) == 0 || !mi) { fwprintf(stderr, L"Select failed\n"); return 1; }
        wprintf(L"%lu Hz, %d bit, %lu ch, length %lld, loop %lld\n", mi->dwSampleRate, mi->nBitsPerSample,
                mi->dwChannels, (long long)mi->qwLength, (long long)mi->qwLoop);
        DWORD t1 = GetTickCount();
        const UINT64 at = dec->Seek((UINT64)(seekSec * mi->dwSampleRate), KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE);
        wprintf(L"Seek -> %llu (%lu ms)\n", (unsigned long long)at, GetTickCount() - t1);
        std::vector<float> pcm, chunk(2048 * 2);
        const size_t want = (size_t)(seconds * mi->dwSampleRate);
        t1 = GetTickCount();
        while (pcm.size() / 2 < want)
        {
            const DWORD got = dec->Render((BYTE*)chunk.data(), 2048);
            pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + got * 2);
            if (got < 2048) { wprintf(L"end of track at %.2f s\n", pcm.size() / 2.0 / mi->dwSampleRate); break; }
        }
        float peak = 0;
        for (float x : pcm) peak = (x > peak) ? x : (-x > peak ? -x : peak);
        wprintf(L"rendered %.1f s in %lu ms, peak %.3f\n", pcm.size() / 2.0 / mi->dwSampleRate, GetTickCount() - t1, peak);
        if (!out.empty()) writeWav(out.c_str(), pcm, mi->dwSampleRate);
    }
    dec->Release();
    module->Release();
    file->Release();
    folder->Release();
    return 0;
}
