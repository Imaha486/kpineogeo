// kpihost - load kpineogeo.kpi like KbMedia Player does and render tracks to WAV
// usage: kpihost plugin.kpi path\to\224-m1.bin [track(1-based, 0=list)] [seconds] [out.wav] [rate] [seekSec]
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "kpi.h"
#include "kpi_decoder.h"
#include "kpi_impl.h"
#include "kpi_mingw_uuid.h"

class DiskFile : public KbKpiFileImpl
{
public:
    std::vector<BYTE> d; UINT64 pos = 0; std::wstring name;
    DWORD WINAPI Read(void* p, DWORD n) override
    {
        if (pos >= d.size()) return 0;
        DWORD k = (DWORD)std::min<UINT64>(n, d.size() - pos);
        memcpy(p, d.data() + pos, k); pos += k; return k;
    }
    UINT64 WINAPI Seek(INT64 i, DWORD o) override
    {
        INT64 b = o == FILE_BEGIN ? 0 : o == FILE_CURRENT ? (INT64)pos : (INT64)d.size();
        if (b + i < 0 || (UINT64)(b + i) > d.size()) return KPI_FILE_EOF;
        return pos = b + i;
    }
    UINT64 WINAPI GetSize() override { return d.size(); }
    DWORD WINAPI GetFileName(wchar_t* p, DWORD sz) override
    {
        DWORD need = (DWORD)((name.size() + 1) * 2);
        if (sz < need) { p[0] = 0; return need; }
        wcscpy(p, name.c_str()); return need;
    }
};

static DiskFile* LoadFile(const std::wstring& path, const std::wstring& name)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return nullptr;
    DiskFile* df = new DiskFile(); df->name = name;
    BYTE buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) df->d.insert(df->d.end(), buf, buf + n);
    fclose(f);
    return df;
}

class DiskFolder : public KbKpiFolderImpl
{
public:
    std::wstring dir; std::vector<std::wstring> files;
    DWORD WINAPI GetFolderName(wchar_t* p, DWORD sz) override
    {
        std::wstring n = dir.substr(dir.find_last_of(L"\\/") + 1);
        DWORD need = (DWORD)((n.size() + 1) * 2); if (sz < need) { p[0] = 0; return need; }
        wcscpy(p, n.c_str()); return need;
    }
    DWORD WINAPI EnumFiles(DWORD i, wchar_t* p, DWORD sz, DWORD) override
    {
        if (i >= files.size()) { p[0] = 0; return 0; }
        DWORD need = (DWORD)((files[i].size() + 1) * 2); if (sz < need) { p[0] = 0; return need; }
        wcscpy(p, files[i].c_str()); return need;
    }
    BOOL WINAPI OpenFile(const wchar_t* n, IKpiFile** pp) override
    {
        *pp = LoadFile(dir + L"\\" + n, n); return *pp != nullptr;
    }
    BOOL WINAPI OpenFolder(const wchar_t*, IKpiFolder** pp) override { *pp = nullptr; return FALSE; }
};

class PrintTags : public KbKpiUnknownImpl<IKpiTagInfo>
{
public:
    PrintTags() {}
    DWORD WINAPI GetTagInfo(IKpiFile*, IKpiFolder*, DWORD, DWORD) override { return 1; }
    DWORD WINAPI GetValue(const wchar_t*, wchar_t* p, int) override { p[0] = 0; return 0; }
    void WINAPI SetOverwrite(BOOL) override {}
    void WINAPI SetPicture(DWORD, const wchar_t*, const wchar_t*, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD) override {}
    void WINAPI aSetValueA(const char*, int, const char*, int) override {}
    void WINAPI aSetValueW(const char*, int, const wchar_t*, int) override {}
    void WINAPI aSetValueU8(const char*, int, const char*, int) override {}
    void WINAPI wSetValueA(const wchar_t*, int, const char*, int) override {}
    void WINAPI wSetValueW(const wchar_t* n, int, const wchar_t* v, int) override { wprintf(L"   %ls=%ls\n", n, v); }
    void WINAPI wSetValueU8(const wchar_t*, int, const char*, int) override {}
    void WINAPI u8SetValueA(const char*, int, const char*, int) override {}
    void WINAPI u8SetValueW(const char*, int, const wchar_t*, int) override {}
    void WINAPI u8SetValueU8(const char*, int, const char*, int) override {}
};

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    HMODULE h = LoadLibraryW(argv[1]);
    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    typedef HRESULT (WINAPI *PFN)(REFIID, void**, IKpiUnknown*);
    PFN create = (PFN)GetProcAddress(h, "kpi_CreateInstance");
    IKpiDecoderModule* mod = nullptr;
    if (!create || create(IID_IKpiDecoderModule, (void**)&mod, nullptr) != S_OK) { printf("create failed\n"); return 1; }
    const KPI_DECODER_MODULEINFO* mi; mod->GetModuleInfo(&mi);
    wprintf(L"exts=%ls multi=%ls\n", mi->cszSupportExts, mi->cszMultiSongExts);

    std::wstring path = argv[2];
    size_t s = path.find_last_of(L"\\/");
    DiskFolder* folder = new DiskFolder(); folder->dir = path.substr(0, s);
    WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW((folder->dir + L"\\*").c_str(), &fd);
    if (fh != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) folder->files.push_back(fd.cFileName); } while (FindNextFileW(fh, &fd)); FindClose(fh); }

    int track = argc > 3 ? _wtoi(argv[3]) : 0;
    double sec = argc > 4 ? _wtof(argv[4]) : 10;
    DWORD rate = argc > 6 ? _wtoi(argv[6]) : 44100;
    double seekSec = argc > 7 ? _wtof(argv[7]) : 0;

    DiskFile* file = LoadFile(path, path.substr(s + 1));
    KPI_MEDIAINFO req; kpi_InitMediaInfo(&req); req.dwSampleRate = rate;
    IKpiDecoder* dec = nullptr;
    DWORD t0 = GetTickCount();
    DWORD count = mod->Open(track ? &req : nullptr, file, folder, &dec);
    printf("Open -> %lu tracks (%lu ms)\n", count, GetTickCount() - t0);
    if (!count) return 1;
    PrintTags tags;
    if (track == 0)
    {
        dec->Select(0, nullptr, &tags, KPI_TAGGET_FLAG_ALL);
        for (DWORD i = 1; i <= count; i++) { printf("#%lu\n", i); dec->Select(i, nullptr, &tags, KPI_TAGGET_FLAG_TITLE | KPI_TAGGET_FLAG_COMMENT); }
        return 0;
    }
    const KPI_MEDIAINFO* info = nullptr;
    dec->Select(track, &info, &tags, KPI_TAGGET_FLAG_ALL);
    printf("rate=%lu bits=%d ch=%lu len=%lld loop=%lld\n", info->dwSampleRate, info->nBitsPerSample, info->dwChannels,
           (long long)info->qwLength, (long long)info->qwLoop);
    t0 = GetTickCount();
    UINT64 sp = dec->Seek((UINT64)(seekSec * info->dwSampleRate), KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE);
    printf("seek -> %llu (%lu ms)\n", (unsigned long long)sp, GetTickCount() - t0);
    std::vector<float> pcm;
    UINT64 want = (UINT64)(sec * info->dwSampleRate);
    std::vector<float> buf(1000 * 2);
    t0 = GetTickCount();
    while (pcm.size() / 2 < want)
    {
        DWORD r = dec->Render((BYTE*)buf.data(), 1000);
        pcm.insert(pcm.end(), buf.begin(), buf.begin() + r * 2);
        if (r < 1000) { printf("ended at %.2f s\n", pcm.size() / 2.0 / info->dwSampleRate); break; }
    }
    printf("render %.1f s in %lu ms\n", pcm.size() / 2.0 / info->dwSampleRate, GetTickCount() - t0);
    float peak = 0; for (float x : pcm) peak = std::max(peak, x < 0 ? -x : x);
    printf("peak=%.3f\n", peak);
    if (argc > 5 && argv[5][0])
    {
        FILE* f = _wfopen(argv[5], L"wb");
        DWORD ds = (DWORD)pcm.size() * 2, t;
        WORD w;
        fwrite("RIFF", 1, 4, f); t = 36 + ds; fwrite(&t, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        t = 16; fwrite(&t, 4, 1, f); w = 1; fwrite(&w, 2, 1, f); w = 2; fwrite(&w, 2, 1, f);
        fwrite(&info->dwSampleRate, 4, 1, f); t = info->dwSampleRate * 4; fwrite(&t, 4, 1, f); w = 4; fwrite(&w, 2, 1, f); w = 16; fwrite(&w, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
        for (float x : pcm) { int v = (int)(x * 32767); v = v > 32767 ? 32767 : v < -32768 ? -32768 : v; short sv = (short)v; fwrite(&sv, 2, 1, f); }
        fclose(f);
    }
    dec->Release(); mod->Release();
    return 0;
}
