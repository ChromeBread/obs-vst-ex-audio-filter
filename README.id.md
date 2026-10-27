[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | **Bahasa Indonesia** | [Français](README.fr.md) | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

Jendela filter audio yang diperluas untuk OBS Studio dengan dukungan **VST2 / VST3**, peralihan ON/OFF instan untuk
siaran menyanyi, alat latensi, dan tampilan rantai. Bekerja pada rantai filter yang sama dengan jendela *Filter*
bawaan OBS, jadi keduanya tetap kompatibel.

- Windows x64, OBS Studio 31 atau lebih baru (diuji di 33.0)
- Pembuat: BlackDream feat. ChromeBread · Lisensi: GPL-2.0

## Instalasi

1. Unduh `obs-vst-ex-audio-filter-1.0.0-windows-x64.zip` dari [Releases](../../releases).
2. Tutup OBS dan ekstrak zip ke `C:\ProgramData\obs-studio\plugins\`
   (hasilnya `C:\ProgramData\obs-studio\plugins\obs-vst-ex-audio-filter\bin\64bit\...`).
3. Jalankan OBS. Saat pertama kali, OBS menanyakan tempat menyimpan gambar mini, tangkapan layar, dan riwayat.

Hapus instalasi: tutup OBS dan hapus folder `obs-vst-ex-audio-filter` itu
(Alat > VST Ex Audio Filter > Mode debug menampilkan semua folder yang perlu dihapus).

## Fitur

**Jendela Filter (VST Ex)** — Audio Mixer: klik kanan sumber > *Filter (VST Ex)*
- Tombol ON/OFF besar, pilih banyak, seret untuk mengurutkan, duplikat (Ctrl+D), hapus semua, catatan per filter
- Urungkan / Ulangi dan riwayat tersimpan otomatis yang tetap ada setelah OBS dimulai ulang
- Latensi per filter, total latensi, dan tombol untuk mengompensasinya lewat Sync Offset
- **Preset**: simpan / terapkan / ekspor / impor seluruh rantai filter
- **Set ON/OFF** dengan Sync Offset dan volume: ganti verse → chorus seketika di tengah lagu, juga lewat hotkey
- **Ukur dengan 4 ketukan**: lagu tes BPM120 diputar, Anda bertepuk mengikuti (atau tempelkan earphone ke mik)
  dan Sync Offset dihitung; gelombang yang bisa di-zoom dengan penggaris milidetik untuk penyetelan manual

**Plugin VST**
- VST2 dan VST3, pemindaian aman di proses terpisah (dimulai oleh Anda, dengan perkiraan waktu)
- Penjelajah dengan tampilan daftar / gambar mini / detail, urutkan, cari, favorit, tanda HID (sembunyi),
  catatan, terakhir dipakai
- Gambar mini otomatis saat editor pertama kali dibuka, atau sekaligus; tangkapan layar per filter
- Ekspor / impor pengaturan plugin dari jendela editor

**Filter bawaan**
- *VST Ex Pitch / Pan*: pitch per semitone dan pan, memudahkan membedakan perangkat saat menyelaraskan
- *VST Ex Pseudo Stereo*: melebarkan mik mono tanpa DAW (efek Haas), satu filter untuk ditambah atau dihapus

**Dock dan alat**
- Dock *VST Ex*: tombol set ON/OFF dan penggunaan CPU (total OBS dan bagian VST)
- Dock *VST Ex Mixer*: satu kolom per sumber di bawah meter Audio Mixer, satu tombol per VST
- *VST Ex Chain View* (menu Alat): semua sumber dari perangkat ke output, latensi, level, sidechain,
  imager setengah lingkaran (opsional), simpan sebagai gambar
- Alat > VST Ex Audio Filter: bahasa (EN / JP / KR / ID / FR / TW), versi, info debug

## Pratinjau

![Jendela Filter (VST Ex), dock VST Ex Mixer dan VST Ex](docs/images/preview-filters.jpg)
*Jendela Filter (VST Ex), dock VST Ex Mixer dan VST Ex*

![Pilih Plugin VST (tampilan gambar mini)](docs/images/preview-browser.jpg)
*Pilih Plugin VST (tampilan gambar mini)*

## Build

Visual Studio 2022 dan CMake 3.28+: `cmake --preset windows-x64` lalu
`cmake --build build_x64 --config RelWithDebInfo`.
