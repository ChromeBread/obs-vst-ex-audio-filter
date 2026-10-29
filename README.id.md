[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | **Bahasa Indonesia** | [Français](README.fr.md) | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

Jendela filter audio tambahan untuk OBS Studio dengan ON/OFF instan untuk siaran bernyanyi, alat latensi, dan
tampilan rantai, ditambah **VST Enigmax Loader**, host **VST2 / VST3** miliknya sendiri. Jendela *Filter (VST Ex)*
bekerja pada rantai filter yang sama dengan jendela *Filter* bawaan OBS, jadi keduanya bisa dipakai.

Dua plugin OBS dipasang: **VST Ex** (jendela, dock, dan filter bawaan) dan **VST Enigmax Loader** (host VST).
Filter VST milik VST Enigmax Loader: jika dihapus, OBS tidak bisa memuatnya lagi. Pindahkan dulu ke filter VST 2.x
bawaan OBS atau ke atkAudio dengan *Serah terima* (lihat di bawah).

- Windows x64, OBS Studio 31 atau lebih baru (diuji di 33.0)
- Pembuat: BlackDream feat. ChromeBread · Lisensi: GPL-2.0

## Instalasi

1. Unduh `obs-vst-ex-audio-filter-<versi>-windows-x64-setup.exe` dari [Releases](../../releases).
2. Tutup OBS lalu jalankan. *Instalasi penuh* memasang semuanya; *Instalasi kustom* bisa memilih:
   - VST Ex Audio Filter (wajib): *Filter (VST Ex)*, filter pitch / pseudo stereo, Alat > VST Ex Audio Filter
   - VST Enigmax Loader: plugin VST2 / VST3
   - VST Ex Chain View, dock VST Ex Mixer, dock VST Ex Switcher

   Bagian opsional VST Ex bisa dinyalakan / dimatikan nanti di Alat > VST Ex Audio Filter (berlaku setelah OBS dimulai ulang).
3. Jalankan OBS. Saat pertama kali, OBS menanyakan tempat menyimpan thumbnail, tangkapan layar, dan riwayat.

**Portabel (tanpa installer)**: `obs-vst-ex-audio-filter-<versi>-windows-x64-portable.zip`. Tutup OBS lalu ekstrak
ke `C:\ProgramData\obs-studio\plugins\`: muncul folder `obs-vst-ex-audio-filter` (VST Ex) dan `vst-enigmax-loader`
(VST Enigmax Loader). Folder yang tidak diperlukan boleh tidak disalin. Untuk menghapus, tutup OBS dan hapus foldernya
(serahterimakan filter VST dulu, lihat di bawah).

**Hapus instalasi** (Pengaturan Windows > Aplikasi > OBS VST Ex Audio Filter). Penghapus instalasi:
1. memperingatkan jika koleksi adegan masih memakai filter VST Enigmax Loader, agar bisa diserahterimakan dulu;
2. menanyakan terpisah apakah menghapus thumbnail plugin, tangkapan layar resolusi tinggi, serta pengaturan /
   preset / riwayat, dengan pilihan mengekspor semuanya dulu;
3. menampilkan file di foldernya yang bukan buatannya (file lain, salinan berganti nama seperti `.bak` / `.org`)
   dan membiarkan Anda menghapus atau menyimpan masing-masing.

## Fitur

**Jendela Filter (VST Ex)** — Audio Mixer: klik kanan sumber > *Filter (VST Ex)*
- Tombol ON/OFF besar, pilih banyak, seret untuk mengurutkan, duplikat (Ctrl+D), hapus semua, catatan per filter
- Urungkan / Ulangi dan riwayat tersimpan otomatis yang tetap ada setelah OBS dimulai ulang
- Latensi per filter, total latensi, dan tombol untuk mengompensasinya lewat Sync Offset
- **Preset**: simpan / terapkan / ekspor / impor seluruh rantai filter
- **Set ON/OFF** dengan Sync Offset dan volume: ganti verse → chorus seketika di tengah lagu, juga lewat hotkey
- **Ukur dengan 4 ketukan**: lagu tes BPM120 diputar, Anda bertepuk mengikuti (atau tempelkan earphone ke mik)
  dan Sync Offset dihitung; gelombang yang bisa di-zoom dengan penggaris milidetik untuk penyetelan manual

**VST Enigmax Loader** (VST2 / VST3)
- VST2 dan VST3, pemindaian aman di proses terpisah (dimulai oleh Anda, dengan perkiraan waktu)
- Penjelajah dengan tampilan daftar / gambar mini / detail, urutkan, cari, favorit, tanda HID (sembunyi),
  catatan, terakhir dipakai
- Gambar mini otomatis saat editor pertama kali dibuka, atau sekaligus; tangkapan layar per filter
- Ekspor / impor pengaturan plugin dari jendela editor
- **Serah terima** (Alat > VST Enigmax Loader > Mode debug): memindahkan filter VST di tempat, beserta pengaturannya, ke dan dari
  filter *VST 2.x Plug-in* bawaan OBS (VST2) dan *atkAudio PluginHost* (VST3).
- Thumbnail dan tangkapan layar resolusi tinggi disimpan di folder terpisah

**Filter bawaan**
- *VST Ex Pitch / Pan*: pitch per semitone dan pan, memudahkan membedakan perangkat saat menyelaraskan
- *VST Ex Pseudo Stereo*: melebarkan mik mono tanpa DAW (efek Haas), satu filter untuk ditambah atau dihapus

**Dock dan alat**
- Dock *VST Ex Switcher*: tombol set ON/OFF dan penggunaan CPU (total OBS dan bagian VST)
- Dock *VST Ex Mixer*: satu kolom per sumber di bawah meter Audio Mixer, satu tombol per VST
- *VST Ex Chain View* (menu Alat): semua sumber dari perangkat ke output, latensi, level, sidechain,
  imager setengah lingkaran (opsional), simpan sebagai gambar
- Alat > VST Ex Audio Filter: bahasa (EN / JP / KR / ID / FR / TW), versi, bagian opsional, info debug,
  ekspor / impor semua pengaturan (mode debug)

## Pratinjau

![Jendela Filter (VST Ex), dock VST Ex Mixer dan VST Ex Switcher](docs/images/preview-filters.jpg)
*Jendela Filter (VST Ex), dock VST Ex Mixer dan VST Ex Switcher*

![Pilih Plugin VST (tampilan gambar mini)](docs/images/preview-browser.jpg)
*Pilih Plugin VST (tampilan gambar mini)*

## Build

Visual Studio 2022 dan CMake 3.28+: `cmake --preset windows-x64` lalu
`cmake --build build_x64 --config RelWithDebInfo`.

VST adalah merek dagang terdaftar milik Steinberg Media Technologies GmbH.
