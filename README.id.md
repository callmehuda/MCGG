# MCGG

[English](README.md) · [Bahasa Indonesia](README.id.md)

[![CI Build](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml/badge.svg)](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml)
[![MIT License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
![Android](https://img.shields.io/badge/Android-native-brightgreen)
![ABI](https://img.shields.io/badge/ABI-arm64--v8a-blue)
![Unity](https://img.shields.io/badge/Unity-2019.4.22f1-black)
![NDK](https://img.shields.io/badge/NDK-r29-orange)

Proyek riset native Android open-source untuk Magic Chess Go Go, berfokus pada analisis runtime Unity/IL2CPP, alur build native Android, dan diagnostik runtime berbasis ImGui.

Repository ini membangun shared library `arm64-v8a` untuk lingkungan Android Unity `2019.4.22f1` IL2CPP. Proyek ini ditujukan hanya untuk pembelajaran, riset defensif, latihan reverse engineering, dan eksperimen yang memiliki otorisasi.

## Daftar Isi

- [Penggunaan yang Bertanggung Jawab](#penggunaan-yang-bertanggung-jawab)
- [Status Proyek](#status-proyek)
- [Fitur](#fitur)
- [Arsitektur](#arsitektur)
- [Kebutuhan](#kebutuhan)
- [Quick Start](#quick-start)
- [Build](#build)
- [Struktur Repository](#struktur-repository)
- [Konfigurasi Build](#konfigurasi-build)
- [Alur Runtime](#alur-runtime)
- [Catatan Development](#catatan-development)
- [Troubleshooting](#troubleshooting)
- [Batasan yang Diketahui](#batasan-yang-diketahui)
- [Keamanan](#keamanan)
- [Kontribusi](#kontribusi)
- [Komponen Pihak Ketiga](#komponen-pihak-ketiga)
- [Lisensi](#lisensi)

## Penggunaan yang Bertanggung Jawab

Proyek ini disediakan hanya untuk tujuan edukasi dan riset.

Sebelum menggunakan atau memodifikasi proyek ini, baca dan ikuti Terms of Service Magic Chess Go Go:

https://us.skystone.games/mcgg-tos

Jangan gunakan repository ini untuk merusak layanan live, mengganggu pemain lain, melewati access control, melanggar aturan platform, atau melakukan aktivitas tanpa otorisasi. Semua pengujian harus dibatasi pada environment dan perangkat yang Anda miliki atau memang memiliki izin eksplisit untuk dianalisis.

README ini sengaja hanya mendokumentasikan proses build, struktur kode, dan workflow engineering. README ini tidak menyediakan instruksi deployment runtime, injection, evasion, bypass, atau instruksi operasional yang dapat disalahgunakan.

## Status Proyek

MCGG adalah proyek native Android eksperimental. Simbol game internal, metadata, layout managed object, dan detail runtime Unity dapat berubah di antara rilis game. Karena itu, feature binding diperlakukan sebagai proses retryable dan dapat muncul sebagai unavailable sampai state runtime yang dibutuhkan tersedia.

Target default yang didukung:

- Android ABI: `arm64-v8a`
- Versi Unity: `2019.4.22f1`
- Android NDK: `r29`
- Build system: `ndk-build`
- Standar C++: `c++26`
- Branch utama: `master`
- Tab overlay saat ini: Info, Combat, Appearance, Settings, Shop, Arena, dan Test

## Fitur

### Info

- Tabel runtime status untuk binding battle data, GGC, shop, arena, test, spectator, synergy, dan placement.
- Tabel player dan next-enemy yang diurutkan dengan player lokal di posisi pertama.
- Readout kualitas GGC untuk round 7 dan round 13.
- Indikator status overlay untuk binding yang terlambat atau belum tersedia.

### Combat

- Toggle Invisible Scout.

### Appearance

- Selector theme ImGui Dark dan Catppuccin Mocha.
- Selector font Default dan font Roboto opsional.
- Status kesiapan font saat `Roboto-Medium.ttf` tidak tersedia dari direktori font ImGui.

### Settings

- Kontrol ukuran menu, posisi tetap opsional, dan interaksi window.
- Kontrol font scale, opacity, rounding, border, padding, spacing, scrollbar, dan indentation.
- Save dan load untuk visual settings serta state Combat, Shop, dan Arena.
- Path config default berada di package game yang sedang berjalan, di-resolve sebagai `/data/data/<game-package>/files/mcgg_config.ini`.

### Shop

- Auto-buy hero gratis.
- Auto-buy target hero yang dipilih.
- Auto-refresh shop dengan stop condition untuk hero gratis atau target hero yang dipilih.
- Gold reserve threshold untuk automasi yang lebih aman.
- Tabel target hero dengan jumlah target yang dapat dikonfigurasi.

### Arena

- Spawn hero berdasarkan entry tabel dan star level.
- Grant equipment, termasuk enhanced equipment.
- Force GogoCard yang dipilih.
- Force active synergies.
- Helper level 99.
- Helper outside-map placement.
- Helper enemy HP 1.
- Helper gold grant.

### Test

- Kontrol manual untuk retry binding dan refresh managed reference.
- Inspeksi account berdasarkan self, opponent, atau account ID eksplisit.
- Tabel prediksi fight dengan sinyal direct, manager-derived, invasion-pair, dan round-robin.
- Readout runtime untuk round state, field battle manager, state behavior API, dan seluruh manager entry.

Feature binding di-resolve terhadap local reference artifacts dan metadata IL2CPP runtime. Method dan field yang belum tersedia akan dicoba ulang secara periodik, bukan langsung disimpan permanen sebagai unavailable. Jika binding belum siap, overlay akan menampilkan status `Waiting for ...`.

## Arsitektur

MCGG disusun sebagai native runtime layer kecil yang mengoordinasikan Unity, IL2CPP, rendering, input forwarding, dan feature binding.

Secara umum, proyek ini berisi:

- Native Android module yang dibangun dengan `ndk-build`.
- Deklarasi API Unity `2019.4.22f1` IL2CPP.
- Helper dynamic library lookup runtime.
- Integrasi function hook berbasis Dobby.
- Rendering Dear ImGui melalui OpenGL ES.
- Forwarding input touch Unity ke input mouse ImGui.
- Setup appearance runtime dengan persistence `.ini` ImGui yang dinonaktifkan.
- Persistence konfigurasi milik proyek untuk overlay dan feature state.
- Local reference artifacts untuk validasi signature method, field, dan type.

Sebagian besar logic fitur tetap berada di `jni/Main.cpp` agar native entry point, runtime state, dan retry behavior mudah diperiksa. Refactor besar sebaiknya tetap mempertahankan lifecycle binding yang ada, kecuali refactor tersebut memang secara eksplisit mengubah desain tersebut.

## Kebutuhan

Pastikan tool berikut sudah tersedia sebelum build:

- Git
- Git LFS
- Android SDK
- Android NDK r29
- `ndk-build` tersedia di `PATH`
- Environment target Android `arm64-v8a`

Workflow CI menggunakan:

```sh
ANDROID_NDK_VERSION=29.0.14206865
```

Termux bukan target build resmi untuk repository ini.

## Quick Start

Clone repository dengan submodule:

```sh
git clone --recursive https://github.com/Yan-0001/MCGG.git
cd MCGG
```

Jika repository sudah terlanjur di-clone tanpa submodule, inisialisasi submodule secara manual:

```sh
git submodule update --init --recursive
```

Install dan pull asset Git LFS:

```sh
git lfs install
git lfs pull
```

Build dari root repository:

```sh
ndk-build -C jni
```

Output native utama akan dibuat di:

```text
libs/arm64-v8a/libmain.so
```

## Build

Command build standar:

```sh
ndk-build -C jni
```

Untuk clean rebuild:

```sh
ndk-build -C jni clean
ndk-build -C jni
```

Jika `ndk-build` tidak tersedia di shell, export path Android SDK dan NDK terlebih dahulu:

```sh
export ANDROID_SDK_ROOT=/path/to/android-sdk
export PATH="$ANDROID_SDK_ROOT/ndk/29.0.14206865:$PATH"
```

Lalu verifikasi tool yang digunakan:

```sh
which ndk-build
ndk-build --version
```

## Struktur Repository

```text
.github/workflows/            Workflow build GitHub Actions
jni/Android.mk                Konfigurasi build native module
jni/Application.mk            Pengaturan ABI, platform, STL, dan NDK
jni/Main.cpp                  Hook setup, helper IL2CPP, runtime state, dan overlay ImGui
jni/structures/Structures.hpp Helper type Unity, Mono, delegate, event, dan collection
jni/dobby/                    Header Dobby dan static library arm64
jni/Il2CppVersions/           Header Unity IL2CPP dan deklarasi API
jni/imgui/                    Source Dear ImGui
jni/xDL/                      Utility dynamic loader Android xDL
libs/                         Output generated native shared library
obj/                          Output intermediate build NDK
```

`libs/` dan `obj/` adalah direktori hasil build dan tidak sebaiknya di-commit.

## Konfigurasi Build

Native module didefinisikan di `jni/Android.mk`:

```make
LOCAL_MODULE := main
```

Target Android aktif dikonfigurasi di `jni/Application.mk`:

```make
APP_ABI := arm64-v8a
APP_PLATFORM := android-21
APP_STL := c++_static
APP_OPTIM := release
APP_THIN_ARCHIVE := false
APP_PIE := true
```

Mode bahasa C++ aktif dikonfigurasi di `jni/Android.mk`:

```make
-std=c++26
```

Unity compatibility defines dikonfigurasi di `jni/Android.mk`:

```make
-DUNITY_VERSION_MAJOR=2019
-DUNITY_VERSION_MINOR=4
-DUNITY_VERSION_PATCH=22
-DUNITY_VER=194
```

Pastikan nilai tersebut tetap selaras dengan header Unity di `jni/Il2CppVersions/`.

## Alur Runtime

Pada saat load dan selama frame presentation, `jni/Main.cpp` menjalankan urutan berikut:

1. Mengonfirmasi bahwa process saat ini adalah process target Unity yang diharapkan.
2. Memulai setup thread.
3. Menunggu `liblogic.so`.
4. Me-resolve export API IL2CPP.
5. Melampirkan native thread ke IL2CPP domain.
6. Melakukan hook `eglSwapBuffers`.
7. Membuat context ImGui dan me-resolve path config dari nama package game.
8. Memuat konfigurasi proyek yang tersimpan jika file config tersedia.
9. Memuat font appearance serta menerapkan theme dan style settings yang dipilih.
10. Merender overlay ImGui selama frame presentation.
11. Melakukan hook `UnityEngine.Input.GetTouch`.
12. Meneruskan input touch Unity ke input mouse ImGui.
13. Me-resolve method dan field fitur melalui `ResolveFeatureBindings()`.
14. Mencoba ulang binding method dan field yang belum tersedia secara periodik.
15. Me-refresh managed reference seperti battle bridge dan shop panel state.
16. Me-reload cache tabel hero, equipment, dan GogoCard saat masuk match.
17. Menjalankan shop automation dan arena effects pada tick terpisah 100 ms.

Urutan ini disengaja. Rendering dan input diinisialisasi terpisah dari feature binding agar overlay dapat melaporkan readiness runtime secara parsial sementara object IL2CPP yang terlambat tetap dicoba resolve.

## Catatan Development

- Jaga perubahan native tetap fokus dan mudah di-review.
- Validasi class name, method name, jumlah parameter, return type, dan field layout terhadap local reference artifacts sebelum menambahkan IL2CPP call.
- Pertahankan runtime code fitur di `jni/Main.cpp` kecuali refactor memang diminta secara eksplisit.
- Gunakan section lokal yang jelas dan komentar singkat di sekitar IL2CPP call yang berisiko.
- Gunakan tab Runtime Status dan Test saat memvalidasi binding baru atau menelusuri runtime state yang terlambat tersedia.
- Pertahankan persistence Settings tetap memakai file config milik proyek, bukan mengaktifkan persistence `.ini` ImGui.
- Pertahankan retryable binding behavior. Jangan menyimpan method atau field unresolved secara permanen sebagai missing.
- Pertahankan tick terpisah 100 ms untuk shop automation dan arena effects, kecuali perubahan timing memang bagian dari task.
- Pertahankan default ABI sebagai `arm64-v8a`.
- Jaga kompatibilitas Unity tetap selaras dengan `2019.4.22f1`.
- Jaga mode bahasa native tetap selaras dengan `c++26` kecuali konfigurasi build memang diubah secara sengaja.
- Jangan commit output generated `obj/` atau `libs/`.
- Hindari menambahkan instruksi deployment runtime atau instruksi yang berorientasi penyalahgunaan ke dokumentasi proyek.

## Troubleshooting

### File submodule hilang

Jalankan:

```sh
git submodule update --init --recursive
```

### File Git LFS muncul sebagai pointer file

Install dan pull asset LFS:

```sh
git lfs install
git lfs pull
```

### `ndk-build` tidak ditemukan

Export path Android SDK dan NDK:

```sh
export ANDROID_SDK_ROOT=/path/to/android-sdk
export PATH="$ANDROID_SDK_ROOT/ndk/29.0.14206865:$PATH"
```

Lalu cek:

```sh
which ndk-build
```

### Output ABI salah

Pastikan `jni/Application.mk` berisi:

```make
APP_ABI := arm64-v8a
```

Lalu clean dan rebuild:

```sh
ndk-build -C jni clean
ndk-build -C jni
```

### Runtime binding belum tersedia

Binding yang belum tersedia bisa normal pada early startup atau sebelum managed state yang diharapkan tersedia. Overlay akan menampilkannya sebagai status `Waiting for ...` dan mencobanya ulang secara periodik.

Saat menambahkan atau memperbarui binding, verifikasi:

- Namespace dan class name.
- Method name.
- Jumlah parameter.
- Return type.
- Field name dan declaring type.
- Akses static atau instance.
- Apakah object hanya tersedia di dalam match atau UI state tertentu.

### Font Roboto tidak tersedia

Tab Appearance akan fallback ke font default ImGui saat `jni/imgui/misc/fonts/Roboto-Medium.ttf` tidak dapat dibaca. Ini tidak memblokir overlay atau native build.

### Konfigurasi tidak tersimpan atau termuat

Path config default di-resolve dari process game yang sedang berjalan dan disimpan sebagai `/data/data/<game-package>/files/mcgg_config.ini`. Jika tab Settings melaporkan kegagalan save atau load, cek apakah process dapat membaca dan menulis direktori data app game.

### CI build gagal

Periksa log GitHub Actions untuk:

- Mismatch versi Android NDK.
- Submodule belum tersedia.
- File Git LFS belum ter-pull.
- Compile error di `jni/Main.cpp` atau native source pihak ketiga.
- Include path yang salah di `jni/Android.mk`.

## Batasan yang Diketahui

- Hanya `arm64-v8a` yang didukung secara default.
- Kompatibilitas Unity dipatok ke `2019.4.22f1`.
- Runtime binding dapat berubah ketika target application update.
- Ketersediaan fitur bergantung pada runtime state dan managed object yang sedang loaded.
- Font Roboto opsional bergantung pada isi submodule ImGui yang ter-checkout.
- Termux tidak dikelola sebagai target build resmi.
- Dokumentasi sengaja tidak menyertakan instruksi deployment runtime dan instruksi yang berorientasi penyalahgunaan.

## Keamanan

Jangan melaporkan security issue melalui public issue jika laporan berisi detail sensitif, exploit path, atau informasi yang dapat disalahgunakan. Gunakan komunikasi privat dengan maintainer jika memungkinkan.

Saat berkontribusi pada perubahan terkait keamanan, hindari menyertakan secret, device-specific identifier, dump privat, asset proprietary, atau instruksi operasional yang memungkinkan penggunaan tanpa otorisasi.

## Kontribusi

Kontribusi diterima jika meningkatkan kualitas kode, reliabilitas build, kejelasan dokumentasi, atau maintainability proyek.

Sebelum membuka pull request:

1. Build proyek secara lokal dengan `ndk-build -C jni`.
2. Jaga scope perubahan tetap jelas.
3. Hindari commit output generated build.
4. Hindari commit asset proprietary atau data runtime privat.
5. Dokumentasikan perubahan behavior di README atau komentar kode jika relevan.

Contoh kontribusi yang baik:

- Perbaikan build.
- Error handling yang lebih aman.
- Peningkatan dokumentasi.
- Refactor yang mempertahankan behavior runtime.
- Status reporting yang lebih baik untuk delayed binding.
- Maintenance workflow CI.

Jangan submit perubahan yang menambahkan instruksi deployment berorientasi penyalahgunaan, logic stealth, gangguan layanan, credential handling, atau workflow unauthorized access.

## Komponen Pihak Ketiga

Repository ini dapat menyertakan atau mereferensikan komponen pihak ketiga seperti:

- Dear ImGui
- Dobby
- xDL
- Header Unity IL2CPP atau deklarasi kompatibilitas
- Android NDK dan platform headers

Setiap komponen pihak ketiga tetap mengikuti ketentuan lisensinya masing-masing. Lisensi MIT untuk repository ini hanya berlaku pada kode original proyek kecuali ada file atau direktori yang menyatakan sebaliknya.

Sebelum mendistribusikan binary atau source package, tinjau lisensi dan notice untuk seluruh komponen pihak ketiga yang disertakan.

## Lisensi

Proyek ini dilisensikan di bawah MIT License. Lihat [LICENSE](LICENSE) untuk teks lengkapnya.
