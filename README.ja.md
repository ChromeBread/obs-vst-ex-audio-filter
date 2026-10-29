[English](README.md) | **日本語** | [한국어](README.ko.md) | [Bahasa Indonesia](README.id.md) | [Français](README.fr.md) | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

OBS Studio 用の拡張オーディオフィルタウィンドウです。歌枠向けの瞬間 ON/OFF 切り替え、レイテンシ測定、チェーン図を備え、
独自の **VST2 / VST3** ホスト **VST Enigmax Loader** が付属します。「フィルタ (VST Ex)」ウィンドウは OBS 標準の
「フィルタ」ウィンドウと同じフィルタチェーンを扱うので、どちらからでも操作できます。

OBS プラグインは 2 つ入ります: **VST Ex** (ウィンドウ・ドック・内蔵フィルタ) と **VST Enigmax Loader** (VST ホスト)。
VST フィルタは VST Enigmax Loader のものなので、これを削除すると OBS はそのフィルタを読み込めなくなります。
削除する前に「引き継ぎ」(下記) で OBS 標準の VST 2.x フィルタか atkAudio へ移してください。

- Windows x64、OBS Studio 31 以降 (33.0 で動作確認)
- 製作者: BlackDream feat. ChromeBread ・ ライセンス: GPL-2.0

## インストール

1. [Releases](../../releases) から `obs-vst-ex-audio-filter-<バージョン>-windows-x64-setup.exe` をダウンロード。
2. OBS を終了して実行。「フル インストール」はすべて、「カスタム インストール」は次から選べます:
   - VST Ex Audio Filter (必須): 「フィルタ (VST Ex)」、ピッチ / 疑似ステレオ フィルタ、ツール > VST Ex Audio Filter
   - VST Enigmax Loader: VST2 / VST3 プラグイン
   - VST Ex Chain View、VST Ex Mixer ドック、VST Ex Switcher ドック

   VST Ex の任意の部分は、あとから ツール > VST Ex Audio Filter でオン / オフできます (OBS 再起動後に反映)。
3. OBS を起動。初回にサムネイル・スクリーンショット・履歴の保存先を聞かれます。

**インストール不要版**: `obs-vst-ex-audio-filter-<バージョン>-windows-x64-portable.zip`。OBS を終了し、
`C:\ProgramData\obs-studio\plugins\` に展開すると `obs-vst-ex-audio-filter` (VST Ex) と `vst-enigmax-loader`
(VST Enigmax Loader) の 2 フォルダができます。不要な方はフォルダごと入れなくて構いません。削除は OBS を終了してフォルダを
消すだけです (VST フィルタは先に引き継いでください。下記参照)。

**アンインストール** (Windows の設定 > アプリ > OBS VST Ex Audio Filter)。アンインストーラーは:
1. シーンコレクションに VST Enigmax Loader のフィルタが残っていれば警告し、先に引き継げるようにします
2. プラグインのサムネイル、高解像度スクリーンショット、設定・プリセット・履歴を削除するかを別々に尋ねます
   (「全てエクスポートしてから削除する」も選べます)
3. フォルダ内にプラグインが作ったものではないファイル (別のファイルや `.bak` / `.org` など名前を変えた複製)
   があれば一覧にし、1 つずつ削除するか残すかを選べます

## 機能

**フィルタ (VST Ex) ウィンドウ** — オーディオミキサーでソースを右クリック > 「フィルタ (VST Ex)」
- 大きな ON/OFF ボタン、複数選択、ドラッグで並べ替え、複製 (Ctrl+D)、すべて削除、フィルタごとのメモ
- 元に戻す / やり直し、OBS を再起動しても残る自動保存の履歴
- フィルタごとのレイテンシと合計、Sync Offset で補正するボタン
- **プリセット**: フィルタチェーン全体の保存 / 適用 / エクスポート / インポート
- **ON/OFF セット** (Sync Offset・音量つき): 歌の途中で A メロ → サビを一瞬で、ホットキーでも切り替え
- **4つ打ちで測定**: BPM120 のテスト曲に合わせて手拍子 (またはイヤホンをマイクに当てる) すると Sync Offset を算出。
  ms 目盛り付きで拡大できる波形を見ながら手動で微調整も可能

**VST Enigmax Loader** (VST2 / VST3)
- VST2 と VST3、別プロセスで安全にスキャン (手動開始、所要時間の目安つき)
- リスト / サムネイル / 詳細表示のブラウザ、並べ替え、検索、お気に入り、HID (非表示)、メモ、使ったもの
- エディタを初めて開いたときにサムネイルを自動撮影、一括生成も可能。フィルタごとの個別スクリーンショット
- エディタ画面からプラグイン設定のエクスポート / インポート
- **引き継ぎ** (ツール > VST Enigmax Loader > デバッグモード): VST フィルタを設定ごとその場で、OBS 標準の「VST 2.x プラグイン」
  フィルタ (VST2) や「atkAudio PluginHost」(VST3) との間で移します
- サムネイルと高解像度スクリーンショットは別々のフォルダに保存

**内蔵フィルタ**
- 「VST Ex ピッチ / パン」: 半音単位のピッチとパン。デバイス間のズレ合わせで聞き分けやすくなります
- 「VST Ex 疑似ステレオ」: DAW なしでモノラルマイクに広がりを作ります (ハース効果)。1 つのフィルタで追加・削除

**ドックとツール**
- 「VST Ex Switcher」ドック: ON/OFF セットのボタンと CPU 使用率 (OBS 全体と VST の分)
- 「VST Ex Mixer」ドック: オーディオミキサーのメーターの下にソースごとの列、VST ごとのボタン
- 「VST Ex Chain View」(ツールメニュー): デバイスから出力までの全ソース、レイテンシ、レベル、サイドチェイン、
  半円イメージャー (任意)、画像として保存
- ツール > VST Ex Audio Filter: 言語 (EN / JP / KR / ID / FR / TW)、バージョン、任意の部分のオン / オフ、デバッグ情報、
  全設定のエクスポート / インポート (デバッグモード)

## プレビュー

![VST Ex Mixer ドックと VST Ex Switcher ドックを置いた OBS](docs/images/preview-obs.png)
*VST Ex Mixer ドックと VST Ex Switcher ドックを置いた OBS*

![フィルタ (VST Ex) ウィンドウ](docs/images/preview-filters.png)
*フィルタ (VST Ex) ウィンドウ*

![フィルタ (VST Ex) ウィンドウ、複数選択](docs/images/preview-filters-select.png)
*フィルタ (VST Ex) ウィンドウ、複数選択*

![履歴](docs/images/preview-history.png)
*履歴*

![4つ打ちで Sync Offset を測定](docs/images/preview-sync.png)
*4つ打ちで Sync Offset を測定*

![VST プラグインの選択 (サムネイル表示)](docs/images/preview-browser-thumbnails.png)
*VST プラグインの選択 (サムネイル表示)*

![VST プラグインの選択 (リスト表示)](docs/images/preview-browser-list.png)
*VST プラグインの選択 (リスト表示)*

![VST プラグインの選択 (詳細表示)](docs/images/preview-browser-details.png)
*VST プラグインの選択 (詳細表示)*

![VST Ex Chain View](docs/images/preview-chain.png)
*VST Ex Chain View*

## ビルド

Visual Studio 2022 と CMake 3.28 以降: `cmake --preset windows-x64` のあと
`cmake --build build_x64 --config RelWithDebInfo`。

VST は Steinberg Media Technologies GmbH の登録商標です。
