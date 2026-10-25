# kpineogeo.kpi

NEOGEO (MVS/AES) のサウンド ROM（**M1** = Z80 サウンドプログラム、**V** = ADPCM サンプル）を
[KbMedia Player](https://hwm5.gyao.ne.jp/kobarin/) で直接再生するデコーダプラグインです。

NEOGEO のサウンド基板（Z80 4MHz + YM2610 8MHz）をエミュレートし、68000 の代わりに
サウンドコードを送って曲を鳴らします。

*A KbMedia Player decoder plugin that plays NEOGEO sound ROMs (M1 + V) by emulating the Z80 + YM2610 sound board.*

## 特長

- M1 を開くだけで、同じフォルダ（または ZIP 内）の V ROM を自動で探して再生
  - `224-m1.bin` → `224-v1.bin`, `224-v2.bin` / `game.m1` → `game.v1` …
  - `v11/v12…` + `v21/v22…`（ADPCM-A/B 分割）にも対応
  - 名前が対応しないセット（例: `sprit_m1.rom` + `224-v1.bin`）でも `vN` で終わるファイルを探して使用
- 1つの M1 を複数曲（`ファイル名::曲番号`）として展開
- サウンドコードを総当たりで送り、BGM / ジングルを自動検出（結果はキャッシュ）
- 同じ曲を鳴らす別コードを自動でまとめる（YM2610 への書き込み列のハッシュで判定）
- ADK サウンドドライバ（ティンクルスタースプライツ等）を自動判別
- SNK 標準系ドライバの「0x07 を受けるまで無音」仕様を自動判定（パルスター、アイアンクラッド等）
- ADPCM ストリーム BGM も検出（曲の検出時に実際に音を合成して判定）
- 32bit float ステレオ出力、サンプル単位の正確なシーク
- ゲームごとの曲名・曲リストを ini で指定可能
- 32bit / 64bit 版 KbMedia Player に対応（新プラグイン仕様 `kpi_CreateInstance`）

## 使い方

利用者向けの説明は [dist/readme.txt](dist/readme.txt)、設定例は [dist/kpineogeo.ini](dist/kpineogeo.ini) を参照してください。
ビルド済みのプラグインは [Releases](../../releases) から入手できます。

> ROM イメージはこのリポジトリには含まれていません。ご自身が所有するカートリッジから吸い出したものを使用してください。

## ビルド

### 1. SDK ヘッダの用意

KbMedia Player プラグイン SDK は同梱していません（[third_party/kpisdk/README.md](third_party/kpisdk/README.md)）。

```sh
scripts/fetch-kpisdk.sh /path/to/KbMediaPlayer/SDK/kpisdk.7z   # KbMedia Player 同梱の SDK から
# または
scripts/fetch-kpisdk.sh                                         # auPiECE.kpi の UTF-8 版をダウンロード
```

### 2a. MinGW-w64（Linux / MSYS2）

```sh
make            # bin/x86/kpineogeo.kpi, bin/x64/kpineogeo.kpi
make package    # dist/kpineogeo_vX.XX.zip
```

### 2b. Visual Studio 2022

```sh
cmake -S . -B build-x64 -A x64   && cmake --build build-x64 --config Release
cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
```

（リリースは MinGW でビルドしています。MSVC ビルドは未検証です）

## 検証用ツール

| ツール | 内容 |
|---|---|
| `tools/kpihost.cpp` | KbMedia Player と同じ手順で .kpi をロードし、曲一覧の表示・WAV 書き出し・シークを試す Windows 用ホスト（`make tools`） |
| `tools/scan.cpp` | M1 のサウンドコードを総当たりして検出結果を表示（Linux ネイティブ、`make linux-tools`） |
| `tools/rip.cpp` | 指定コードを鳴らして WAV に書き出す（Linux ネイティブ） |

```sh
make linux-tools
bin/scan 224-m1.bin 224-v1.bin 224-v2.bin
bin/rip  224-m1.bin 224-v1.bin 224-v2.bin 0x1c 30 out.wav 0xfd
```

デバッグ時は ini の `[General]` に `Debug=1` を書くと `%TEMP%\kpineogeo.log` に
Open/Select/Seek の呼び出しや V ROM の検索結果が出力されます。

## ソース構成

```
src/
  kpineogeo.cpp     KPI インターフェース（IKpiDecoderModule / IKpiDecoder）、ini、キャッシュ
  neosnd.*          サウンド基板エミュレーション（Z80 メモリマップ・バンク・NMI/IRQ・YM2610 タイマー）
  songscan.*        ドライバ判別と曲の自動検出（キー・オン監視、マルチスレッド）
  neoplayer.*       再生・リサンプリング（Catmull-Rom）・シーク
  romfind.*         M1 / V ROM のファイル名判定と検索
  kpi_mingw_uuid.h  MinGW 用 __uuidof シム
third_party/
  ymfm/             YM2610 エミュレーション（Aaron Giles, BSD-3-Clause, 無改変）
  z80/              Z80 エミュレーション（Nicolas Allemand, MIT, 16bit I/O ポート対応に改変）
  kpisdk/           KbMedia Player SDK を置く場所（同梱なし）
tools/              検証用ツール
dist/               配布用 readme / ini サンプル
```

## 既知の制限

- 暗号化された M1/V（CMC50 系の後期タイトルなど）は未対応
- 曲の検出はヒューリスティック。拾えない曲がある場合は ini の `Codes=` で指定してください
- ドライバ別の対応は ADK と SNK 標準系（0x07 有効化）のみ。それ以外は汎用処理

## ライセンス

[MIT License](LICENSE)。同梱しているサードパーティのコードについては [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
