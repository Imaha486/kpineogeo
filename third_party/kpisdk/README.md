# KbMedia Player Plugin SDK (not included)

このディレクトリには KbMedia Player プラグイン SDK (© Kobarin) のヘッダを置きます。
SDK は再配布条件が明示されていないため、このリポジトリには含めていません。

必要なファイル (UTF-8 に変換したもの):

- `kpi.h`
- `kpi_decoder.h`
- `kpi_impl.h`
- `kmp_pi.h`

## 用意のしかた

KbMedia Player の配布物に含まれる `SDK/kpisdk.7z` から取り出す (推奨):

```sh
scripts/fetch-kpisdk.sh /path/to/KbMediaPlayer/SDK/kpisdk.7z
```

引数なしで実行すると、[auPiECE.kpi](https://github.com/autch/auPiECE.kpi) に含まれている
UTF-8 版 (SDK 2017/03/06 版) をダウンロードします。

```sh
scripts/fetch-kpisdk.sh
```

Windows で手作業で用意する場合は、`kpisdk.7z` を展開して上記 4 ファイルを
UTF-8 で保存し直してこのディレクトリに置いてください。
