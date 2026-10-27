[English](README.md) | **日本語** | [한국어](README.ko.md) | [Bahasa Indonesia](README.id.md) | [Français](README.fr.md) | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

OBS Studio 用の拡張オーディオフィルタウィンドウです。**VST2 / VST3** の読み込み、歌枠向けの瞬間 ON/OFF 切り替え、
レイテンシ測定、チェーン図を備えています。OBS 標準の「フィルタ」ウィンドウと同じフィルタチェーンを扱うので、
どちらで開いても互換性があります。

- Windows x64、OBS Studio 31 以降 (33.0 で動作確認)
- 製作者: BlackDream feat. ChromeBread ・ ライセンス: GPL-2.0

## インストール

1. [Releases](../../releases) から `obs-vst-ex-audio-filter-1.0.0-windows-x64.zip` をダウンロード。
2. OBS を終了し、zip を `C:\ProgramData\obs-studio\plugins\` に展開
   (`C:\ProgramData\obs-studio\plugins\obs-vst-ex-audio-filter\bin\64bit\...` になります)。
3. OBS を起動。初回にサムネイル・スクショ・履歴の保存先を聞かれます。

アンインストール: OBS を終了してその `obs-vst-ex-audio-filter` フォルダを削除
(ツール > VST Ex Audio Filter > デバッグモード に削除するフォルダがすべて表示されます)。

## 機能

**フィルタ (VST Ex) ウィンドウ** — オーディオミキサーでソースを右クリック > 「フィルタ (VST Ex)」
- 大きな ON/OFF ボタン、複数選択、ドラッグで並べ替え、複製 (Ctrl+D)、すべて削除、フィルタごとのメモ
- 元に戻す / やり直し、OBS を再起動しても残る自動保存の履歴
- フィルタごとのレイテンシと合計、Sync Offset で補正するボタン
- **プリセット**: フィルタチェーン全体の保存 / 適用 / エクスポート / インポート
- **ON/OFF セット** (Sync Offset・音量つき): 歌の途中で A メロ → サビを一瞬で、ホットキーでも切り替え
- **4つ打ちで測定**: BPM120 のテスト曲に合わせて手拍子 (またはイヤホンをマイクに当てる) すると Sync Offset を算出。
  ms 目盛り付きで拡大できる波形を見ながら手動で微調整も可能

**VST プラグイン**
- VST2 と VST3、別プロセスで安全にスキャン (手動開始、所要時間の目安つき)
- リスト / サムネイル / 詳細表示のブラウザ、並べ替え、検索、お気に入り、HID (非表示)、メモ、使ったもの
- エディタを初めて開いたときにサムネイルを自動撮影、一括生成も可能。フィルタごとの個別スクショ
- エディタ画面からプラグイン設定のエクスポート / インポート

**内蔵フィルタ**
- 「VST Ex ピッチ / パン」: 半音単位のピッチとパン。デバイス間のズレ合わせで聞き分けやすくなります
- 「VST Ex 疑似ステレオ」: DAW なしでモノラルマイクに広がりを作ります (ハース効果)。1 つのフィルタで追加・削除

**ドックとツール**
- 「VST Ex」ドック: ON/OFF セットのボタンと CPU 使用率 (OBS 全体と VST の分)
- 「VST Ex Mixer」ドック: オーディオミキサーのメーターの下にソースごとの列、VST ごとのボタン
- 「VST Ex Chain View」(ツールメニュー): デバイスから出力までの全ソース、レイテンシ、レベル、サイドチェイン、
  半円イメージャー (任意)、画像として保存
- ツール > VST Ex Audio Filter: 言語 (EN / JP / KR / ID / FR / TW)、バージョン、デバッグ情報

<!-- スクリーンショット: ここに日本語 UI の小さい画像を追加 (例: docs/ja/*.png)。 -->

## ビルド

Visual Studio 2022 と CMake 3.28 以降: `cmake --preset windows-x64` のあと
`cmake --build build_x64 --config RelWithDebInfo`。
