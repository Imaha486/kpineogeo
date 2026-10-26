# Third-party notices

## ymfm — `third_party/ymfm/`

- https://github.com/aaronsgiles/ymfm (commit 81aec25ccbb98f4873a255f7551ac4dadac59b4a)
- Copyright (c) 2021, Aaron Giles
- BSD 3-Clause License — [third_party/ymfm/LICENSE](third_party/ymfm/LICENSE)
- Unmodified.

## z80 — `third_party/z80/`

- https://github.com/superzazu/z80 (commit d64fe10a2274e5e40019b1086bf7d8990cbc5f23)
- Copyright (c) 2019 Nicolas Allemand
- MIT License — [third_party/z80/LICENSE](third_party/z80/LICENSE)
- Modified: I/O port callbacks take the full 16-bit port address (needed for NEOGEO bank
  switching), and the `fprintf` calls (interrupt mode / unknown opcodes) were removed.
  See [third_party/z80/kpineogeo.patch](third_party/z80/kpineogeo.patch).

## MAME — `src/neocrypt.cpp`

- https://github.com/mamedev/mame (src/devices/bus/neogeo/prot_cmc.cpp, prot_pcm2.cpp)
- copyright-holders: S. Smith, David Haywood, Fabio Priuli
- BSD 3-Clause License
- The CMC50 M1 address scrambling tables/algorithm and the NEO-PCM2 V ROM decryption were ported from these files.

## auPiECE.kpi (acknowledgement)

- https://github.com/autch/auPiECE.kpi — Copyright (c) 2003, 2026 Autch — MIT License
  ([third_party/auPiECE/LICENSE](third_party/auPiECE/LICENSE))
- Versions up to 1.03 contained `src/kpi_mingw_uuid.h` (a modified copy of its file of the same name) and
  `tools/kpihost.cpp` derived from its `tools/kpi-render.cpp`, and the build script downloaded the SDK
  headers from that repository. Since 1.04 these have been replaced with independently written code
  (`src/kpi_gcc_uuid.h`, `tools/kpihost.cpp`) and the SDK is bundled, so nothing is fetched from it.
  The license file is kept because the earlier versions remain in the history.
- The way a v2 (`kpi_CreateInstance`) decoder plugin is put together was learned from it. Thank you.

## KbMedia Player Plugin SDK — `third_party/kpisdk/`

- © Kobarin — https://hwm5.gyao.ne.jp/kobarin/
- `kpi.h`, `kpi_decoder.h`, `kpi_impl.h`, `kmp_pi.h`, `kpi.cpp` from `kpisdk.7z` (2017/03/06) that ships with
  KbMedia Player, converted from CP932 to UTF-8, otherwise unmodified.
  See [third_party/kpisdk/README.md](third_party/kpisdk/README.md).
