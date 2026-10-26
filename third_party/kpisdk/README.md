# KbMedia Player Plugin SDK

KbMedia Player に同梱されている `SDK/kpisdk.7z`（2017/03/06 版、本体 Ver 3.02 用）のうち、
デコーダプラグインのビルドに必要なファイルです。

- `kpi.h` / `kpi_decoder.h` / `kpi_impl.h` / `kmp_pi.h` / `kpi.cpp`
- 著作権は KbMedia Player の作者 Kobarin 氏にあります（© Kobarin）
- 文字コードを CP932 から UTF-8 に変換しただけで、内容は変更していません
  （MSVC では `/utf-8` を付けてビルドします）

SDK を新しい版に差し替える場合は、KbMedia Player の配布物に含まれる `kpisdk.7z` を展開し、
上記ファイルを UTF-8 に変換してこのディレクトリに上書きしてください。

```sh
7z x kpisdk.7z -otmp
for f in kpi.h kpi_decoder.h kpi_impl.h kmp_pi.h kpi.cpp; do
  iconv -f CP932 -t UTF-8 "$(find tmp -name $f)" > $f
done
```
