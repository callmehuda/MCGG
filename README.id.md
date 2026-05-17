# MCGG

[English](README.md) · [Bahasa Indonesia](README.id.md)

[![CI Build](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml/badge.svg)](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml)
[![MIT License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
![Android](https://img.shields.io/badge/Android-native-brightgreen)
![ABI](https://img.shields.io/badge/ABI-arm64--v8a-blue)
![Unity](https://img.shields.io/badge/Unity-2019.4.33f1-black)
![NDK](https://img.shields.io/badge/NDK-r29-orange)

Proyek riset native Android open-source untuk Magic Chess Go Go, berfokus pada analisis runtime Unity/IL2CPP, alur build native Android, dan diagnostik runtime berbasis ImGui.

Repository ini membangun shared library `arm64-v8a` untuk lingkungan Android Unity `2019.4.33f1` IL2CPP. Proyek ini ditujukan hanya untuk pembelajaran, riset defensif, latihan reverse engineering, dan eksperimen yang memiliki otorisasi.

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
- [Packaging Rilis CI](#packaging-rilis-ci)
- [Alur Runtime](#alur-runtime)
- [Catatan Audit Runtime](#catatan-audit-runtime)
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
- Versi Unity: `2019.4.33f1`
- Android NDK: `r29`
- Build system: `ndk-build`
- Standar C++: `c++26`
- Branch utama: `master`
- Tab overlay saat ini: Info, Combat, Auto-Play, Shop, Arena, Appearance,
  Settings, dan Test

## Fitur

### Info

- Tabel runtime status untuk binding battle data, GGC, shop, Recommendation
  Lineup, Battle Power, arena, round skip, speedhack, test, spectator, synergy,
  dan placement.
- Tabel player dan next-enemy yang diurutkan dengan player lokal di posisi pertama.
- Readout kualitas GGC untuk round 7 dan round 13.
- Indikator status overlay untuk binding yang terlambat atau belum tersedia.

### Combat

- Toggle Invisible Scout.

### Auto-Play

- Controller di sisi binary yang membaca round, phase, HP, gold, level,
  population, lineup worth, fight value, target Recommendation Lineup, target
  star-up, dan opponent saat ini.
- Model tekanan strategi adaptif yang berpindah antara Economy, Balanced, dan
  Aggressive berdasarkan progress round, kehilangan HP, kondisi gold, fight
  value sendiri, fight value opponent saat ini, dan opponent terkuat yang
  terdeteksi.
- Planner gold interest yang mengevaluasi tier interest per 10 gold, breakpoint
  interest berikutnya, reserve terkonfigurasi, spend budget, tekanan population,
  tekanan HP, defisit fight value, dan strategi sebelum mengizinkan spending
  shop, bid auction, aksi level-up, target passive gold, atau assist free economy.
- Scan semua battle manager untuk menghitung opponent, mendeteksi perebutan
  target, melacak opponent saat ini, dan membandingkan board lokal dengan board
  terkuat.
- Advanced formation scorer yang membaca unit chess managed dari
  `LogicHeroContainer.m_ChessList`, mengevaluasi hero ID, star, grid position,
  metadata tank/carry role, synergy group, centroid enemy, threat kolom enemy,
  cover frontline ally, proteksi backline, dan crowding kolom, lalu melakukan
  reposition battlefield terbatas satu langkah per cooldown.
- Pemilihan target shop yang mempromosikan hero terbaik saat ini atau target
  star-up ke selected shop target sambil tetap memakai throttle buy/refresh
  yang sudah ada.
- Scoring GogoCard yang memprioritaskan resource, EXP/economy, hero/shop,
  star-up, synergy, equipment, dan combat card sesuai round, tekanan HP, focus
  synergy, dan kekuatan opponent.
- Scoring auction yang membaca phase auction, state slot, bid price, reward
  item, reward hero/equipment, dan special upgrade effect sebelum menawar opsi
  bernilai tertinggi secara terbatas.
- Startup built-in AI bersifat stateful dan memakai cooldown: `StartAI` tidak
  dipanggil terus-menerus untuk account yang sama, dan `StopAI` dipanggil saat
  Auto-Play dimatikan atau snapshot battle live belum dapat dipakai.
- Kontrol opsional untuk built-in battle AI, shop, economy, combat power, arena
  assist, smart formation, auction scoring, dan GogoCard scoring.

### Appearance

- Selector theme dengan ImGui Dark, Catppuccin Mocha, dan palette tambahan
  yang terinspirasi dari [Dear ImGui issue #707](https://github.com/ocornut/imgui/issues/707),
  termasuk Darcula, Cherry, Dracula, Visual Studio, Deep Dark, dan Maroon.
- Selector font Default dan font Noto Sans CJK embedded.
- Status kesiapan font saat font Noto Sans CJK embedded tidak tersedia.

### Settings

- Kontrol ukuran menu, posisi tetap opsional, navigasi tab yang lebih ramah
  perangkat mobile, dan interaksi window.
- HUD teks next-enemy opsional yang ditampilkan di dekat tengah bawah layar.
- Kontrol font scale, opacity, rounding, border, padding, spacing, scrollbar, dan indentation.
- Save dan load untuk kontrol visual, window, HUD, Auto-Play, Combat, Shop, dan Arena.
- Path config default berada di package game yang sedang berjalan, di-resolve sebagai `/data/data/<game-package>/files/mcgg_config.ini`.

### Shop

- Auto-buy hero gratis.
- Auto-buy target hero yang dipilih.
- Auto-buy hero dari Recommendation Lineup yang aktif.
- Auto-refresh shop dengan stop condition untuk hero gratis, target yang dipilih, atau hero Recommendation Lineup.
- Gold reserve threshold untuk automasi yang lebih aman.
- Tabel target hero dengan jumlah target yang dapat dikonfigurasi dan tanpa field search yang bergantung pada keyboard.
- Jumlah target Recommendation Lineup untuk automation shop tingkat lanjut.
- Throttle buy dan refresh untuk mengurangi aksi berulang saat automation berjalan terus-menerus.
- Pemeriksaan kesiapan UI shop yang menunggu panel shop operable, tidak delay,
  dan tidak berada pada state refresh spectate sebelum select, buy, atau refresh.

### Arena

- Spawn hero berdasarkan entry tabel dan star level.
- Grant equipment, termasuk enhanced equipment.
- Force GogoCard yang dipilih.
- Force active synergies.
- Subtab Battle Power untuk force defend win, pencegahan HP-loss, booster
  attack-ratio/fight-value player lokal, dan cripple enemy board.
- Helper level dan population 99.
- Helper outside-map placement.
- Helper enemy HP 1.
- Helper gold manual dan pasif.
- Helper free shop/upgrade economy, unlimited hero pool, dan bypass shop lock.
- Kontrol Skip Round untuk memindahkan round manager lokal ke target round yang
  dipilih, menunggu phase fight/result selesai pada skip otomatis, dan menekan
  request berulang untuk source round dan target round yang sama.
- Kontrol SpeedHack berbasis `UnityEngine.Time.set_timeScale`, dengan reset
  eksplisit ke `1.0x` saat fitur keluar dari state battle aktif.

### Test

- Kontrol manual untuk retry binding dan refresh managed reference.
- Inspeksi account berdasarkan self, opponent, atau account ID eksplisit.
- Tabel prediksi fight dengan sinyal direct, manager-derived, invasion-pair,
  urutan invader dari dump, queue/cycle, dan riwayat opponent. `Will fight`
  adalah peluang row tersebut menjadi opponent player lokal; `Current enemy`
  menampilkan opponent yang terdeteksi untuk row tersebut jika tersedia;
  `Recent` menampilkan pertemuan terbaru dari riwayat opponent per player.
- Readout runtime bertab untuk kesiapan binding, round state, identitas player,
  rank, ekonomi, state shop, field battle manager, state battle bridge, state
  panel shop, state behavior API, dan seluruh manager entry.

Feature binding di-resolve terhadap local reference artifacts dan metadata IL2CPP runtime. Method dan field yang belum tersedia akan dicoba ulang secara periodik, bukan langsung disimpan permanen sebagai unavailable. Jika binding belum siap, overlay akan menampilkan status `Waiting for ...`.

## Arsitektur

MCGG disusun sebagai native runtime layer kecil yang mengoordinasikan Unity, IL2CPP, rendering, input forwarding, dan feature binding.

Secara umum, proyek ini berisi:

- Native Android module yang dibangun dengan `ndk-build`.
- Deklarasi API Unity `2019.4.33f1` IL2CPP.
- Helper dynamic library lookup runtime.
- Integrasi function hook berbasis Dobby.
- Rendering Dear ImGui melalui OpenGL ES.
- Forwarding input touch Unity ke input mouse ImGui.
- Setup appearance runtime dengan persistence `.ini` ImGui yang dinonaktifkan.
- Persistence konfigurasi milik proyek untuk overlay dan feature state.
- State runtime primitive yang bersifat atomic dengan domain mutex terpisah
  untuk cache IL2CPP, koleksi fitur, dan string UI/config.
- Helper snapshot untuk data hero, equipment, GogoCard, selected target shop,
  opponent, unit board, auction, dan strategi yang dipakai overlay serta tick
  fitur yang di-throttle.
- Local reference artifacts untuk validasi signature method, field, dan type.

Sebagian besar logic fitur tetap berada di `jni/Main.cpp` agar native entry point, runtime state, dan retry behavior mudah diperiksa. Refactor besar sebaiknya tetap mempertahankan lifecycle binding yang ada, kecuali refactor tersebut memang secara eksplisit mengubah desain tersebut.

Shared state saat ini dipisahkan berdasarkan ownership. `RuntimeMutex::CacheMutex`
melindungi cache method dan field, `RuntimeMutex::FeatureMutex` melindungi
koleksi fitur kompleks seperti cache tabel dan selected target shop, dan
`RuntimeMutex::UiMutex` melindungi string UI/config. Flag runtime primitive,
pointer managed reference, dan counter fitur disimpan sebagai nilai
`std::atomic`. Kode yang membaca koleksi kompleks sebaiknya memakai helper
snapshot atau access yang sudah ada dan tidak menahan `FeatureMutex` saat
memanggil API IL2CPP managed.

Auto-Play memakai model tick terbatas yang sama dengan fitur runtime lain. Fitur
ini mengumpulkan snapshot lokal terlebih dahulu, membangun satu gold-interest
plan, menilai opsi strategy/formation/shop/card/auction dari data lokal, hanya
mem-publish counter ringkas dan selected target di bawah `FeatureMutex`, dan
menghindari lock proyek saat memanggil API IL2CPP managed.

Cadence runtime saat ini sengaja dipisah berdasarkan tanggung jawab:

- Retry binding: 2000 ms.
- Refresh managed reference: 100 ms.
- Check state match: 500 ms.
- Retry reload tabel: 2000 ms.
- Tick fitur Arena: 100 ms.
- Tick automation Shop: 100 ms.
- Tick Combat power: 250 ms.
- Tick Auto-Play: 250 ms.
- Tick riwayat prediksi opponent: 500 ms.
- Refresh teks HUD next-enemy: 500 ms saat HUD aktif.

Miss metadata field juga dicoba ulang dengan backoff singkat. Ini menjaga
metadata Unity yang terlambat tetap retryable tanpa membiarkan lookup field yang
hilang melakukan scan ulang pada setiap tick fitur.

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

Flag C native default mengoptimalkan ukuran dengan `-Oz` dan mendefinisikan
`NDEBUG`. Build NDK yang berorientasi debug menambahkan `-O0` saat
`NDK_DEBUG=1`.

Unity compatibility defines dikonfigurasi di `jni/Android.mk`:

```make
-DUNITY_VERSION_MAJOR=2019
-DUNITY_VERSION_MINOR=4
-DUNITY_VERSION_PATCH=33
-DUNITY_VER=194
```

Pastikan nilai tersebut tetap selaras dengan header Unity di `jni/Il2CppVersions/`.

## Packaging Rilis CI

Workflow GitHub Actions di `.github/workflows/build.yml` membangun native module
dengan Android NDK `29.0.14206865`, mengunggah zip rilis sebagai workflow
artifact, dan memublikasikan GitHub release untuk run yang bukan pull request.

Nama asset rilis memakai prefix proyek, versi berbasis tanggal UTC, metadata
workflow run, dan short commit SHA. Setiap package menyertakan
`BUILD_INFO.txt` berisi metadata repository, ref, commit, versi, run, NDK, dan
module yang digunakan untuk build tersebut.

Release notes dibuat dari Git history. Isinya mencakup konteks repository/ref
serta deskripsi commit untuk push range saat GitHub menyediakannya; jika tidak,
workflow memakai rentang dari tag `v*` sebelumnya sampai commit saat ini, lalu
fallback ke commit saat ini saja. Subject commit dan body commit akan disertakan
jika ada. Release yang sudah ada dengan tag generated yang sama akan diperbarui
dengan notes hasil generate terbaru sebelum asset diunggah ulang memakai
`--clobber`.

## Alur Runtime

Pada saat load dan selama frame presentation, `jni/Main.cpp` menjalankan urutan berikut:

1. Constructor mengonfirmasi command line process berisi `:UnityKillsMe`.
2. Setup thread detached dimulai setelah process gate.
3. Setup thread me-resolve dan melakukan hook `eglSwapBuffers` terlebih dahulu,
   sehingga rendering menjadi frame loop jangka panjang.
4. Setup thread menunggu `liblogic.so`, me-resolve export API IL2CPP Unity
   `2019.4.33f1` dari deklarasi API bundled, lalu attach ke IL2CPP domain.
5. Setup thread me-resolve dan melakukan hook `UnityEngine.Input.GetTouch` saat
   metadata method tersedia.
6. `RuntimeState::Il2CppReady` diset dan retry feature binding diminta.
7. Frame hook valid pertama membuat context ImGui, mematikan persistence `.ini`
   ImGui, me-resolve path config dari nama package game, memuat konfigurasi
   proyek jika tersedia, memuat font, dan menerapkan theme serta style settings.
8. Setiap hooked frame attach render thread ke IL2CPP bila memungkinkan sebelum
   pekerjaan fitur managed berjalan.
9. `TickFeatures()` mencoba ulang binding yang belum tersedia, me-refresh battle
   bridge dan shop panel reference, me-refresh state match, dan mencoba ulang
   loading table cache.
10. Diagnostik Info, Shop, Arena, Auto-Play, HUD Settings, dan Test hanya
    me-refresh data runtime yang dibutuhkan.
11. Auto-Play, Arena, Shop, Combat, dan riwayat opponent berjalan pada tick
    bounded masing-masing, bukan pada setiap render pass.
12. Input touch Unity diteruskan ke input mouse ImGui melalui path hook
    `GetTouch`.

Urutan ini disengaja. Render hook bisa aktif sebelum IL2CPP siap, sehingga logic
fitur managed harus tetap berada di balik readiness check. Rendering, input, dan
feature binding diinisialisasi terpisah agar overlay dapat melaporkan readiness
runtime secara parsial sementara object IL2CPP yang terlambat tetap dicoba
resolve.

## Catatan Audit Runtime

Review terbaru terhadap `jni/Main.cpp`, `jni/structures/Structures.hpp`,
`jni/Android.mk`, `.github/workflows/build.yml`, dan `dump/dump.cs` menyorot
area yang rawan bug berikut:

- Render hook dipasang sebelum `liblogic.so` dan IL2CPP siap. Kode frame-time
  harus tahan terhadap runtime managed yang belum siap dan tidak boleh memanggil
  API IL2CPP kecuali `AttachRenderIl2CppThread()` berhasil.
- Lookup method sengaja hanya memakai hasil method yang sukses sebagai cache
  reusable. Scan method kosong tetap retryable. Lookup field hanya meng-cache
  miss di balik backoff retry binding. Jangan mengubahnya menjadi failure
  permanen.
- Matching method memakai class name, method name, jumlah parameter, dan
  containment nama parameter yang case-insensitive. Binding baru yang sensitif
  terhadap overload harus dicek terhadap dump, bukan hanya compile sukses.
- Publikasi table cache bersifat all-or-nothing untuk data hero, equipment, dan
  GogoCard. Jika salah satu tabel belum tersedia setelah update game, UI
  terkait harus tetap menampilkan `Waiting for ...`, bukan menganggap tabel game
  kosong.
- Automation Shop bergantung pada operability UI live, bukan hanya method battle
  data. Aksi buy dan refresh harus tetap membutuhkan shop panel yang tidak
  delay, tidak spectate, dan diterima oleh `CanOperate(Boolean)`.
- Auto-Play sementara memiliki assist Shop, Arena, dan Combat melalui policy
  backup yang di-capture. Edit user pada toggle assist saat Auto-Play aktif
  dapat kembali ke nilai sebelum Auto-Play saat Auto-Play berhenti.
- Prediksi opponent menggabungkan pair data exact, state invasion manager,
  urutan invader berbasis dump, fallback round-robin, dan riwayat pertemuan
  terbaru. Hanya current opponent lokal yang exact yang boleh ditampilkan
  sebagai `100%`.
- SpeedHack mengubah time scale Unity global. Fitur ini harus tetap reset ke
  `1.0x` saat dinonaktifkan, saat state battle aktif hilang, atau saat feature
  state di-reset.

## Catatan Development

- Jaga perubahan native tetap fokus dan mudah di-review.
- Validasi class name, method name, jumlah parameter, return type, dan field layout terhadap local reference artifacts sebelum menambahkan IL2CPP call.
- Pertahankan runtime code fitur di `jni/Main.cpp` kecuali refactor memang diminta secara eksplisit.
- Gunakan section lokal yang jelas dan komentar singkat di sekitar IL2CPP call yang berisiko.
- Pertahankan urutan boot saat ini: process gate, setup thread, hook
  `eglSwapBuffers` lebih awal, tunggu `liblogic.so`, resolve export IL2CPP,
  attach setup thread, hook `GetTouch`, lalu inisialisasi overlay di render
  thread.
- Gunakan tab Runtime Status dan Test saat memvalidasi binding baru atau menelusuri runtime state yang terlambat tersedia.
- Untuk perubahan Arena Skip Round, verifikasi
  `MCLogicBattleData.get_logicRoundMgr`, `LogicRoundMgr.SetRound(UInt32)`, dan
  `LogicRoundMgr.NextRound(Boolean)` terhadap `dump/dump.cs`; bagian yang belum
  siap harus tetap muncul sebagai `Waiting for ...`.
- Untuk perubahan Arena SpeedHack, verifikasi
  `UnityEngine.Time.set_timeScale(Single)` terhadap `dump/dump.cs` dan reset
  scale ke normal saat fitur dinonaktifkan.
- Prediksi opponent sebaiknya memprioritaskan runtime state berbasis dump
  seperti `LogicInvasionMgr`, `LogicRealPlayerInvader.lbmList`,
  `PairGenRoundTable`/`PairGenTwoPlayerMode`, `lastRoundEnemy`, dan
  `prevRealPlayerEnemy` sebelum fallback ke urutan heuristik.
- Pertahankan diagnostik Test sebagai read-only kecuali task secara eksplisit
  meminta aksi, dan verifikasi setiap binding tambahan terhadap `dump/dump.cs`.
- Jaga overlay utama tetap mudah diakses pada display mobile sambil
  mempertahankan navigasi utama berbasis TabBar.
- Pertahankan persistence Settings tetap memakai file config milik proyek, bukan mengaktifkan persistence `.ini` ImGui.
- Pertahankan retryable binding behavior. Jangan menyimpan method atau field unresolved secara permanen sebagai missing.
- Pertahankan tick terpisah 100 ms untuk shop automation dan arena effects,
  tick 250 ms untuk Combat dan Auto-Play, serta cadence 500 ms untuk riwayat
  opponent/HUD kecuali perubahan timing memang bagian dari task.
- Pertahankan throttle shop automation untuk buy, repeat-buy, refresh, target-worth, dan pengecekan Recommendation Lineup.
- Lindungi akses langsung ke `FeatureState::Heroes`, `FeatureState::Equips`,
  `FeatureState::Cards`, dan `FeatureState::ShopSelectedHeroes` dengan
  `RuntimeMutex::FeatureMutex` atau helper snapshot/access yang sudah ada.
- Hindari menahan `RuntimeMutex::FeatureMutex` saat melakukan call IL2CPP
  managed. Kumpulkan data lokal terlebih dahulu, lalu publish hasilnya di
  bawah lock.
- Jaga perubahan cache method dan field tetap berada di bawah
  `RuntimeMutex::CacheMutex`, dan lindungi string UI/config dengan
  `RuntimeMutex::UiMutex`.
- Pertahankan scan selected-target shop tetap bounded dan berbasis snapshot.
- Pastikan nama theme Appearance dan entry palette tetap sejajar saat
  menambahkan theme. Config lama mengharapkan Catppuccin Mocha tetap berada di
  theme index `1`.
- Pertahankan default ABI sebagai `arm64-v8a`.
- Jaga kompatibilitas Unity tetap selaras dengan `2019.4.33f1`.
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

Binding yang belum tersedia bisa normal pada early startup atau sebelum managed state yang diharapkan tersedia. Overlay akan menampilkannya sebagai status `Waiting for ...` dan mencobanya ulang secara periodik. Lookup field yang belum tersedia di-throttle agar metadata yang hilang tidak melakukan scan ulang dari hot path fitur.

Saat menambahkan atau memperbarui binding, verifikasi:

- Namespace dan class name.
- Method name.
- Jumlah parameter.
- Return type.
- Field name dan declaring type.
- Akses static atau instance.
- Apakah object hanya tersedia di dalam match atau UI state tertentu.

### Shop automation tidak membeli atau refresh

Shop automation sengaja menunggu saat binding, managed reference, data coin, target count, atau data Recommendation Lineup yang dibutuhkan belum siap. Cek tab Runtime Status dan Shop untuk pesan `Waiting for ...`.

Saat menelusuri masalah penggunaan terus-menerus, verifikasi:

- Binding shop select dan shop automation sudah siap.
- Panel shop refresh sudah siap saat auto-refresh aktif.
- Panel shop operable: tidak delay, tidak dalam state refresh spectate, dan
  diterima oleh `UIPanelBattleHeroShop.CanOperate(Boolean)`.
- Binding Recommendation Lineup sudah siap saat recommendation buying atau pause-refresh aktif.
- Keep-gold reserve tidak sedang memblokir aksi.
- Target count belum tercapai.
- Cooldown buy dan refresh sudah selesai.

### Font Noto Sans CJK tidak tersedia

Tab Appearance akan fallback ke font default ImGui saat font Noto Sans CJK embedded tidak dapat dimuat. Ini tidak memblokir overlay atau native build.

### Konfigurasi tidak tersimpan atau termuat

Path config default di-resolve dari process game yang sedang berjalan dan disimpan sebagai `/data/data/<game-package>/files/mcgg_config.ini`. Jika tab Settings melaporkan kegagalan save atau load, cek apakah process dapat membaca dan menulis direktori data app game.

### CI build gagal

Workflow berjalan pada push ke `master` dan pull request yang menargetkan
`master`. Stacked branch atau side branch tetap perlu verifikasi lokal sebelum
merge.

Periksa log GitHub Actions untuk:

- Mismatch versi Android NDK.
- Submodule belum tersedia.
- File Git LFS belum ter-pull.
- Compile error di `jni/Main.cpp` atau native source pihak ketiga.
- Include path yang salah di `jni/Android.mk`.

## Batasan yang Diketahui

- Hanya `arm64-v8a` yang didukung secara default.
- Kompatibilitas Unity dipatok ke `2019.4.33f1`.
- Runtime binding dapat berubah ketika target application update.
- Ketersediaan fitur bergantung pada runtime state dan managed object yang sedang loaded.
- Render hook dapat aktif sebelum metadata managed siap, sehingga frame awal
  dapat menampilkan readiness overlay yang masih parsial.
- Resolution method IL2CPP dipandu dump tetapi saat runtime tetap berbasis nama
  dan bentuk parameter; method game yang overload atau rename membutuhkan
  validasi manual terhadap `dump/dump.cs`.
- Automation Recommendation Lineup bergantung pada data lineup match aktif yang diekspos runtime.
- Prediksi opponent bersifat probabilistik saat data current-pair belum tersedia;
  data live `m_CurPairDict` tetap menjadi prioritas saat runtime mengeksposnya.
- Font Noto Sans CJK embedded menambah ukuran input source native dan waktu build atlas font.
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
