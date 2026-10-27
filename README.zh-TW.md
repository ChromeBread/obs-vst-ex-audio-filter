[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Bahasa Indonesia](README.id.md) | [Français](README.fr.md) | **繁體中文**

# OBS VST Ex Audio Filter

OBS Studio 的擴充音訊濾鏡視窗，內建 **VST2 / VST3** 外掛支援、適合歌唱直播的瞬間 ON/OFF 切換、延遲工具與濾鏡鏈圖。
它與 OBS 原生「濾鏡」視窗操作同一條濾鏡鏈，兩者可以互相搭配使用。

- Windows x64，OBS Studio 31 以上 (已在 33.0 測試)
- 製作者: BlackDream feat. ChromeBread · 授權: GPL-2.0

## 安裝

1. 從 [Releases](../../releases) 下載 `obs-vst-ex-audio-filter-1.0.0-windows-x64.zip`。
2. 關閉 OBS，把 zip 解壓縮到 `C:\ProgramData\obs-studio\plugins\`
   (會得到 `C:\ProgramData\obs-studio\plugins\obs-vst-ex-audio-filter\bin\64bit\...`)。
3. 啟動 OBS。第一次啟動時會詢問縮圖、截圖與紀錄的儲存位置。

解除安裝: 關閉 OBS 後刪除該 `obs-vst-ex-audio-filter` 資料夾
(工具 > VST Ex Audio Filter > 除錯模式 會列出所有要刪除的資料夾)。

## 功能

**濾鏡 (VST Ex) 視窗** — 在音訊混音器對來源按右鍵 >「濾鏡 (VST Ex)」
- 大型 ON/OFF 按鈕、多選、拖曳排序、複製 (Ctrl+D)、全部刪除、每個濾鏡的備註
- 復原 / 重做，以及重新啟動 OBS 後仍保留的自動儲存紀錄
- 每個濾鏡的延遲與合計，並可一鍵用同步偏移補償
- **預設**: 儲存 / 套用 / 匯出 / 匯入整條濾鏡鏈
- **ON/OFF 組合** (含同步偏移與音量): 唱到一半瞬間切換主歌 → 副歌，也可用快速鍵
- **以四拍測量**: 播放 BPM120 測試曲，跟著拍手 (或把耳機貼在麥克風上) 即可算出同步偏移；
  可縮放、附毫秒刻度的波形，方便手動微調

**VST 外掛**
- VST2 與 VST3，在獨立程序中安全掃描 (手動開始，附預估時間)
- 清單 / 縮圖 / 詳細檢視的瀏覽器，排序、搜尋、我的最愛、HID (隱藏) 標記、備註、最近使用
- 第一次開啟編輯器時自動擷取縮圖，也可一次全部產生；每個濾鏡的個別截圖
- 在編輯器視窗匯出 / 匯入外掛設定

**內建濾鏡**
-「VST Ex 移調 / 聲像」: 以半音為單位的音高與聲像，對齊裝置時容易分辨
-「VST Ex 假立體聲」: 不用 DAW 就能讓單聲道麥克風變寬 (哈斯效應)，一個濾鏡即可新增或移除

**停駐視窗與工具**
-「VST Ex」停駐視窗: ON/OFF 組合按鈕與 CPU 使用率 (OBS 整體與 VST 部分)
-「VST Ex Mixer」停駐視窗: 在音訊混音器音量表下方，每個來源一欄、每個 VST 一個按鈕
-「VST Ex Chain View」(工具選單): 從裝置到輸出的所有來源、延遲、音量、側鏈、半圓聲像儀 (可選)、另存為圖片
- 工具 > VST Ex Audio Filter: 語言 (EN / JP / KR / ID / FR / TW)、版本、除錯資訊

## 預覽

![濾鏡 (VST Ex) 視窗、VST Ex Mixer 與 VST Ex 停駐視窗](docs/images/preview-filters.jpg)
*濾鏡 (VST Ex) 視窗、VST Ex Mixer 與 VST Ex 停駐視窗*

![選擇 VST 外掛 (縮圖檢視)](docs/images/preview-browser.jpg)
*選擇 VST 外掛 (縮圖檢視)*

## 建置

Visual Studio 2022 與 CMake 3.28 以上: `cmake --preset windows-x64`，接著
`cmake --build build_x64 --config RelWithDebInfo`。
