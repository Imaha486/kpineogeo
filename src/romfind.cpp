#include "romfind.h"
#include <algorithm>
#include <cwctype>
#include <cwchar>

static std::wstring lower(std::wstring s)
{
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

static void splitExt(const std::wstring& n, std::wstring& stem, std::wstring& ext)
{
    const size_t p = n.find_last_of(L'.');
    if (p == std::wstring::npos) { stem = n; ext.clear(); }
    else { stem = n.substr(0, p); ext = n.substr(p); }
}

static bool isSep(wchar_t c) { return c == L'-' || c == L'_' || c == L'.' || c == L' '; }

// position of the "m1" token in the stem, or npos
static size_t m1Token(const std::wstring& stemLower)
{
    const size_t n = stemLower.size();
    if (n >= 2 && stemLower.compare(n - 2, 2, L"m1") == 0 && (n == 2 || isSep(stemLower[n - 3])))
        return n - 2;
    return std::wstring::npos;
}

bool NeoIsM1Name(const std::wstring& name)
{
    std::wstring stem, ext;
    splitExt(lower(name), stem, ext);
    if (ext == L".m1") return true;
    if (ext == L".bin" || ext == L".rom" || ext == L".m")
        return m1Token(stem) != std::wstring::npos;
    return false;
}

void NeoFindVRoms(const std::wstring& m1Name, const std::vector<std::wstring>& folder,
                  std::vector<std::wstring>& listA, std::vector<std::wstring>& listB)
{
    listA.clear(); listB.clear();
    std::wstring stem, ext;
    splitExt(m1Name, stem, ext);
    const std::wstring stemL = lower(stem), extL = lower(ext);

    // case-insensitive lookup in the folder listing
    auto findOne = [&](const std::wstring& want) -> std::wstring {
        if (want.empty()) return L"";
        const std::wstring w = lower(want);
        for (const auto& f : folder)
        {
            std::wstring fl = lower(f);
            const size_t slash = fl.find_last_of(L"/\\");
            const std::wstring base = slash == std::wstring::npos ? fl : fl.substr(slash + 1);
            if (base == w) return f;
        }
        return L"";
    };

    // name patterns for "vNN"
    auto makeNames = [&](const std::wstring& vtok) -> std::vector<std::wstring> {
        std::vector<std::wstring> n;
        const size_t p = m1Token(stemL);
        if (extL == L".m1")
        {
            if (p != std::wstring::npos)
                n.push_back(stem.substr(0, p) + vtok + L"." + vtok);   // 089-m1.m1 -> 089-v1.v1
            n.push_back(stem + L"." + vtok);                            // game.m1 -> game.v1
        }
        if (p != std::wstring::npos)
            n.push_back(stem.substr(0, p) + vtok + ext);                // 224-m1.bin -> 224-v1.bin
        return n;
    };
    auto find = [&](const std::vector<std::wstring>& names) -> std::wstring {
        for (const auto& n : names) { std::wstring f = findOne(n); if (!f.empty()) return f; }
        return L"";
    };
    auto makeName = makeNames;

    // split A/B naming (v11, v12, ... / v21, v22, ...)
    std::wstring f11 = find(makeName(L"v11"));
    std::wstring f21 = find(makeName(L"v21"));
    if (!f11.empty() && !f21.empty())
    {
        for (int i = 1; i <= 9; i++)
        {
            std::wstring f = find(makeName(L"v1" + std::to_wstring(i)));
            if (f.empty()) break;
            listA.push_back(f);
        }
        for (int i = 1; i <= 9; i++)
        {
            std::wstring f = find(makeName(L"v2" + std::to_wstring(i)));
            if (f.empty()) break;
            listB.push_back(f);
        }
        return;
    }
    for (int i = 1; i <= 16; i++)
    {
        std::wstring f = find(makeName(L"v" + std::to_wstring(i)));
        if (f.empty()) break;
        listA.push_back(f);
    }
    if (!listA.empty()) return;

    // fallback: names differ between M1 and V (e.g. MAME sets "sprit_m1.rom" + "224-v1.bin").
    // take every file whose name ends with a "vN" token (stem "...-v1" or extension ".v1")
    std::vector<std::pair<int, std::wstring>> cand;
    for (const auto& f : folder)
    {
        std::wstring fl = lower(f);
        const size_t slash = fl.find_last_of(L"/\\");
        if (slash != std::wstring::npos) fl = fl.substr(slash + 1);
        std::wstring st, ex;
        splitExt(fl, st, ex);
        auto tokenNum = [](const std::wstring& t, bool needSep) -> int {
            // t ends with v<digits>; returns the number or -1
            size_t e = t.size(), b = e;
            while (b > 0 && iswdigit(t[b - 1])) b--;
            if (b == e || b == 0 || t[b - 1] != L'v') return -1;
            if (needSep && b >= 2 && !isSep(t[b - 2])) return -1;
            return (int)wcstol(t.c_str() + b, nullptr, 10);
        };
        int n = -1;
        if (ex.size() > 2 && ex[1] == L'v') n = tokenNum(ex.substr(1), false);   // game.v1
        if (n < 0) n = tokenNum(st, true);                                       // 224-v1.bin
        if (n > 0) cand.push_back({ n, f });
    }
    std::sort(cand.begin(), cand.end());
    bool split = false;
    for (auto& c : cand) if (c.first >= 11) split = true;
    for (auto& c : cand)
    {
        if (split)
        {
            if (c.first >= 11 && c.first <= 19) listA.push_back(c.second);
            else if (c.first >= 21 && c.first <= 29) listB.push_back(c.second);
        }
        else if (listA.empty() || cand[&c - &cand[0] - 1].first != c.first)
            listA.push_back(c.second);
    }
}
