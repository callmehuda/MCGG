#include <jni.h>
#include <errno.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <mutex>
#include <atomic>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "structures/Structures.hpp"
#include "xdl.h"
#include "dobby/dobby.h"

#include "imgui/imconfig.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"
#include "imgui/backends/imgui_impl_android.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#define DO_API(ret, name, args) ret (*name) args;
#include "Il2CppVersions/api/2019.4.33f1.h"
#undef DO_API

#ifndef MCGG_BUILD_REPOSITORY
#define MCGG_BUILD_REPOSITORY "Yan-0001/MCGG"
#endif

#ifndef MCGG_BUILD_VERSION
#define MCGG_BUILD_VERSION "unknown"
#endif

#ifndef MCGG_BUILD_COMMIT
#define MCGG_BUILD_COMMIT "unknown"
#endif

#ifndef MCGG_BUILD_REF
#define MCGG_BUILD_REF "unknown"
#endif

// Unity input and value layouts used by native hooks.
enum class TouchPhase {
    Began,
    Moved,
    Stationary,
    Ended,
    Canceled
};

enum class TouchType {
    Direct,
    Indirect,
    Stylus
};

struct Touch {
    int m_FingerId;
    Unity::Vector2 m_Position;
    Unity::Vector2 m_RawPosition;
    Unity::Vector2 m_PositionDelta;
    float m_TimeDelta;
    int m_TapCount;
    TouchPhase m_Phase;
    TouchType m_Type;
    float m_Pressure;
    float m_maximumPossiblePressure;
    float m_Radius;
    float m_RadiusVariance;
    float m_AltitudeAngle;
    float m_AzimuthAngle;
};

// Value type layout from dump/dump.cs: MCLogicHeroShopItemData.
struct MCLogicHeroShopItemData {
    int m_iSlot;
    int m_iHeroId;
    int m_iStarLv;
    int m_iPrice;
    int m_iOneStarBasePrice;
    int m_eRuleType;
};

static_assert(sizeof(MCLogicHeroShopItemData) == 24);

struct AstarInt2 {
    int x;
    int y;
};

static_assert(sizeof(AstarInt2) == 8);

struct UnityVector3 {
    float x;
    float y;
    float z;
};

static_assert(sizeof(UnityVector3) == 12);

// Cached table rows used by menu lists and automation.
struct HeroTableEntry {
    int id = 0;
    std::string name;
    int quality = 0;
    int isTank = 0;
    int occupation = 0;
    int attackType = 0;
    int heroType = 0;
    std::vector<int> groups;
    bool valid = false;
};

struct EquipTableEntry {
    int id = 0;
    std::string name;
};

struct CardTableEntry {
    int id = 0;
    std::string name;
};

struct RelationTableEntry {
    int id = 0;
    std::string name;
};

enum class UpdateCheckStatus {
    Waiting,
    Checking,
    UpToDate,
    UpdateAvailable,
    Failed,
    Malformed,
    UnknownLocalVersion
};

struct ReleaseInfo {
    std::string tag;
    std::string name;
    std::string publishedAt;
    std::string targetCommitish;
    std::string summary;
    std::string body;
    bool draft = false;
    bool prerelease = false;
};

struct UpdateCheckSnapshot {
    UpdateCheckStatus status = UpdateCheckStatus::Waiting;
    std::string localVersion;
    std::string localCommit;
    std::string localRef;
    std::string repository;
    std::string latestVersion;
    std::string latestName;
    std::string latestPublishedAt;
    std::string latestSummary;
    std::string lastCheckText;
    std::string lastError;
    int failureCount = 0;
    bool checkInProgress = false;
    std::vector<ReleaseInfo> releases;
};

struct TableCacheCounts {
    int heroes = 0;
    int equips = 0;
    int cards = 0;
    int relations = 0;
};

struct HeroAutomationState {
    bool selected = false;
    int targetCount = 9;
};

struct {
    void* liblogic = nullptr;
} handle;

int GLWidth = 0;
int GLHeight = 0;

void* UnityLibraryHandle = nullptr;

std::unordered_map<std::string, std::vector<MethodInfo*>> MultiMethodCache;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> MethodMissCache;
std::unordered_map<std::string, FieldInfo*> FieldCache;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> FieldMissCache;

namespace RuntimeConfig {
    constexpr int BindingRetryMs = 2000;
    constexpr int MissingMethodRetryMs = 5000;
    constexpr int ReferenceRefreshMs = 100;
    constexpr int MatchStateCheckMs = 500;
    constexpr int TableRetryMs = 2000;
    constexpr int ArenaTickMs = 100;
    constexpr int ShopTickMs = 100;
    constexpr int CombatTickMs = 250;
    constexpr int OpponentPredictionTickMs = 500;
    constexpr int GgcInfoRefreshMs = 500;
    constexpr int GgcRoundScanStart = 1;
    constexpr int GgcRoundScanEnd = 99;
    constexpr int FeatureFrameBudgetMs = 12;
    constexpr int FeatureManagedWorkBudgetUnits = 256;
    constexpr int TableLoadManagedWorkBudgetUnits = 2048;
    constexpr int ShopActionCooldownMs = 350;
    constexpr int ShopRepeatBuyCooldownMs = 1500;
    constexpr int ShopRefreshCooldownMs = 650;
    constexpr int ShopWorthCheckMs = 500;
    constexpr int RecommendLineupCheckMs = 500;
    constexpr int ScavengerCheckMs = 250;
    constexpr int ScavengerMinimumActiveCount = 2;
    constexpr int ScavengerDynamicEnumRefreshShopCost = 1;
    constexpr int ArenaSkipCooldownMs = 750;
    constexpr int UpdateCheckRefreshMs = 6 * 60 * 60 * 1000;
    constexpr int UpdateCheckRetryBaseMs = 5 * 60 * 1000;
    constexpr int UpdateCheckRetryMaxMs = 60 * 60 * 1000;
    constexpr int MaxShopTargetChecks = 256;
    constexpr int MaxManagedDictionaryEntries = 8192;
    constexpr int MaxManagedListItems = 2048;
    constexpr int MaxManagedStringChars = 4096;
    constexpr int MaxOpponentHistoryRounds = 64;
    constexpr int MaxOpponentForecastRounds = 8;
    constexpr size_t MaxReleaseResponseBytes = 512 * 1024;
    constexpr size_t MaxReleaseBodyPreviewChars = 12000;
    constexpr size_t MaxInstanceFieldOffsetBytes = 1024 * 1024;
}

namespace RuntimeMutex {
    std::mutex CacheMutex;
    std::mutex FeatureMutex;
    std::mutex ManagedHandleMutex;
    std::mutex UpdateMutex;
    std::recursive_mutex UiMutex;
}

namespace RuntimeState {
    std::atomic<bool> Il2CppReady{false};
    std::atomic<bool> BindingRetryRequested{false};
    std::atomic<bool> BindingResolveInProgress{false};
}

namespace RuntimeBudget {
    thread_local int ManagedWorkUnits = 0;
    thread_local int ManagedWorkUnitLimit = 0;
}

void TickOpponentPredictionHistory(uint64_t selfAccountId);
bool RefreshCachedOpponentPredictionRows(
    uint64_t selfAccountId,
    std::chrono::steady_clock::time_point now,
    bool force = false
);
void RefreshGgcInfo(bool force);
void RefreshInfoPlayerRows(bool force);
void RefreshNextEnemyHudText(uint64_t selfAccountId);
void DrawOpponentPredictionSection(uint64_t selfAccountId);
bool IsBattleActive(uint64_t selfAccountId);
bool HasShopSelectBinding();

// Feature toggles, cached managed references, and throttled runtime state.
namespace FeatureState {
    std::atomic<bool> CombatInvisibleScout{false};
    std::atomic<bool> CombatForceWin{false};
    std::atomic<bool> CombatPreventHpLoss{false};
    std::atomic<bool> CombatBoostAttackRatio{false};
    std::atomic<bool> CombatCrippleEnemies{false};
    std::atomic<int> CombatAttackRatioPercent{5000};
    std::atomic<int> CombatEnemyAttackRatioPercent{1};
    std::atomic<int> CombatFightValue{999999999};

    std::atomic<bool> ShopBuyFreeHero{false};
    std::atomic<bool> ShopBuySelectedHero{false};
    std::atomic<bool> ShopBuyRecommendLineup{false};
    std::atomic<bool> ShopForceScavengerExpensiveHero{false};
    std::atomic<bool> ShopRefresh{false};
    std::atomic<bool> ShopStopRefreshAtFreeHero{false};
    std::atomic<bool> ShopStopRefreshAtSelectedHero{false};
    std::atomic<bool> ShopStopRefreshAtRecommendLineup{false};
    std::atomic<bool> ShopKeepGold{false};
    std::atomic<int> ShopKeepGoldAt{20};
    std::atomic<int> ShopRecommendTargetCount{9};
    std::unordered_map<int, HeroAutomationState> ShopSelectedHeroes;
    std::unordered_map<int, int> ShopRecommendLineupTargetCounts;
    std::vector<int> CachedRecommendLineupHeroIds;

    std::atomic<int> ArenaHeroStar{1};
    std::atomic<bool> ArenaItemEnhanced{false};
    std::atomic<bool> ArenaGogoCardEnabled{false};
    std::atomic<int> ArenaGogoCardSelected1{-1};
    std::atomic<int> ArenaGogoCardSelected2{-1};
    std::atomic<bool> ArenaForceActiveSynergy{false};
    std::atomic<bool> ArenaForceLevel99{false};
    std::atomic<bool> ArenaOutsideMapPlacement{false};
    std::atomic<bool> ArenaAllEnemyHpOne{false};
    std::atomic<bool> ArenaForceCompleteAchievements{false};
    std::atomic<int> ArenaPrice{5};
    std::atomic<bool> ArenaSkipRound{false};
    std::atomic<int> ArenaSkipTargetRound{1};
    std::atomic<uint32_t> ArenaLastSkipSourceRound{0};
    std::atomic<int> ArenaLastSkipTargetRound{0};
    std::atomic<uint32_t> CachedGameRound{0};
    std::atomic<bool> ArenaSpeedHack{false};
    std::atomic<float> ArenaTimeScale{2.0f};

    std::atomic<void*> BattleBridge{nullptr};
    std::atomic<void*> HeroShopPanel{nullptr};
    std::atomic<void*> HeroShopItemList{nullptr};
    std::atomic<void*> LoadResInstance{nullptr};
    std::atomic<uint32_t> BattleBridgeHandle{0};
    std::atomic<uint32_t> HeroShopPanelHandle{0};
    std::atomic<uint32_t> HeroShopItemListHandle{0};
    std::atomic<uint32_t> LoadResInstanceHandle{0};
    std::unordered_map<void*, uint32_t> ManagedObjectHandles;
    std::vector<uint32_t> MatchManagedHandles;

    std::atomic<bool> TableDataLoaded{false};
    std::atomic<bool> WasInMatch{false};
    std::atomic<uint64_t> LastSelfAccountId{0};
    std::unordered_map<int, HeroTableEntry> Heroes;
    std::unordered_map<int, EquipTableEntry> Equips;
    std::unordered_map<int, CardTableEntry> Cards;
    std::unordered_map<int, RelationTableEntry> Relations;

    std::chrono::steady_clock::time_point LastBindingRetry{};
    std::chrono::steady_clock::time_point LastReferenceRefresh{};
    std::chrono::steady_clock::time_point LastArenaTick{};
    std::chrono::steady_clock::time_point LastShopTick{};
    std::chrono::steady_clock::time_point LastCombatTick{};
    std::chrono::steady_clock::time_point LastOpponentPredictionTick{};
    std::chrono::steady_clock::time_point LastMatchStateCheck{};
    std::chrono::steady_clock::time_point LastTableLoadAttempt{};
    std::chrono::steady_clock::time_point LastShopAction{};
    std::chrono::steady_clock::time_point LastShopBuyAttempt{};
    std::chrono::steady_clock::time_point LastShopRefreshAttempt{};
    std::chrono::steady_clock::time_point LastShopWorthCheck{};
    std::chrono::steady_clock::time_point LastRecommendLineupCheck{};
    std::chrono::steady_clock::time_point LastScavengerCheck{};
    std::chrono::steady_clock::time_point LastArenaSkipAttempt{};
    std::atomic<bool> CachedShopHasWorthwhileTarget{false};
    std::atomic<int> CachedRecommendLineupHeroId{0};
    std::atomic<int> CachedScavengerRelationId{0};
    std::atomic<int> CachedScavengerActiveCount{-1};
    std::atomic<bool> ShopScavengerAutoRefreshPending{false};
    std::atomic<bool> ShopScavengerProcessing{false};
    std::atomic<uint64_t> LastShopBuyAccountId{0};
    std::atomic<int> LastShopBuySlot{-1};
    std::atomic<int> LastShopBuyHeroId{0};
    std::atomic<int> LastShopBuyPrice{0};
    std::atomic<int> LastShopBuyOwnCount{-1};
    std::atomic<bool> LastShopBuyWasFree{false};
}

namespace UiState {
    std::string TestAccountId;
    std::string ConfigPath;
    std::string ConfigStatus;
    std::atomic<int> MainTabIndex{0};
    std::atomic<int> ThemeIndex{1};
    std::atomic<int> FontIndex{1};
    std::atomic<int> LanguageIndex{0};
    std::atomic<bool> ShopShowSelectedOnly{false};
    std::atomic<bool> ShowNextEnemyHud{false};
    std::atomic<bool> MoveFromTitleBarOnly{true};
    std::atomic<bool> ResizeFromEdges{false};
    std::atomic<bool> UseFixedMenuPosition{false};
    std::atomic<float> MenuWidth{760.0f};
    std::atomic<float> MenuHeight{560.0f};
    std::atomic<float> MenuPosX{20.0f};
    std::atomic<float> MenuPosY{20.0f};
    std::atomic<float> FontScale{1.0f};
    std::atomic<float> WindowAlpha{1.0f};
    std::atomic<float> WindowRounding{7.0f};
    std::atomic<float> ChildRounding{6.0f};
    std::atomic<float> FrameRounding{5.0f};
    std::atomic<float> PopupRounding{6.0f};
    std::atomic<float> ScrollbarRounding{6.0f};
    std::atomic<float> GrabRounding{5.0f};
    std::atomic<float> TabRounding{5.0f};
    std::atomic<float> ScrollbarSize{14.0f};
    std::atomic<float> WindowBorderSize{1.0f};
    std::atomic<float> FrameBorderSize{0.0f};
    std::atomic<float> FramePaddingX{4.0f};
    std::atomic<float> FramePaddingY{3.0f};
    std::atomic<float> ItemSpacingX{8.0f};
    std::atomic<float> ItemSpacingY{4.0f};
    std::atomic<float> IndentSpacing{21.0f};
}

namespace UpdateState {
    UpdateCheckStatus Status = UpdateCheckStatus::Waiting;
    bool CheckInProgress = false;
    int FailureCount = 0;
    std::string LatestVersion;
    std::string LatestName;
    std::string LatestPublishedAt;
    std::string LatestSummary;
    std::string LastCheckText;
    std::string LastError;
    std::vector<ReleaseInfo> Releases;
    std::chrono::steady_clock::time_point LastCheckAttempt{};
    std::chrono::steady_clock::time_point NextAllowedCheck{};
}

enum MainTabIndex {
    MainTabInfo = 0,
    MainTabCombat = 1,
    MainTabShop = 2,
    MainTabArena = 3,
    MainTabAppearance = 4,
    MainTabSettings = 5
};

namespace AppearanceState {
    ImFont* DefaultFont = nullptr;
    ImFont* NotoCjkFont = nullptr;
    int AppliedThemeIndex = -1;
    int AppliedFontIndex = -1;
}

constexpr int kDefaultThemeIndex = 1;

constexpr const char* kAppearanceThemes[] = {
    "ImGui Dark",
    "Catppuccin Mocha",
    "Codz01 Midnight",
    "Doug Dark",
    "Microsoft Light",
    "Darcula",
    "Unreal Grey",
    "Cherry",
    "Light Green",
    "Photoshop Charcoal",
    "Corporate Grey",
    "Raikiri Dark",
    "Steam VGUI",
    "Deus Ex Gold",
    "Visual Studio",
    "OverShifted Dark",
    "FontStudio Green",
    "FontStudio Red",
    "Deep Dark",
    "Dracula",
    "Maroon"
};

constexpr int kAppearanceThemeCount =
    static_cast<int>(sizeof(kAppearanceThemes) / sizeof(kAppearanceThemes[0]));
constexpr int kIssue707ThemeOffset = 2;

constexpr int kLanguageEnglish = 0;
constexpr int kLanguageIndonesian = 1;

constexpr const char* kMenuLanguages[] = {
    "English",
    "Bahasa Indonesia"
};

constexpr int kMenuLanguageCount =
    static_cast<int>(sizeof(kMenuLanguages) / sizeof(kMenuLanguages[0]));

struct MenuI18nEntry {
    const char* key;
    const char* en;
    const char* id;
    const char* tooltipEn;
    const char* tooltipId;
};

static const MenuI18nEntry kMenuI18nEntries[] = {
    {"English", "English", "Inggris", "Use English menu text.", "Gunakan teks menu bahasa Inggris."},
    {"Bahasa Indonesia", "Bahasa Indonesia", "Bahasa Indonesia", "Use Indonesian menu text.", "Gunakan teks menu bahasa Indonesia."},
    {"Info", "Info", "Info", "Show match overview, GGC rounds, current enemies, and predictions.", "Tampilkan ringkasan match, round GGC, enemy saat ini, dan prediksi."},
    {"Combat", "Combat", "Combat", "Open combat visibility controls.", "Buka kontrol visibilitas combat."},
    {"Shop", "Shop", "Shop", "Configure shop buying, refreshing, and hero targets.", "Atur pembelian shop, refresh, dan target hero."},
    {"Arena", "Arena", "Arena", "Open arena helpers for heroes, items, cards, rounds, synergy, placement, and resources.", "Buka helper arena untuk hero, item, card, round, synergy, placement, dan resource."},
    {"Appearance", "Appearance", "Tampilan", "Adjust theme, font, and menu language.", "Atur theme, font, dan bahasa menu."},
    {"Settings", "Settings", "Pengaturan", "Save configuration and adjust window behavior.", "Simpan konfigurasi dan atur perilaku window."},
    {"Prev", "Prev", "Sebelumnya", "Switch to the previous overlay tab.", "Pindah ke tab overlay sebelumnya."},
    {"Next", "Next", "Berikutnya", "Switch to the next overlay tab.", "Pindah ke tab overlay berikutnya."},
    {"Config", "Config", "Konfig", "Save, load, and review configuration state.", "Simpan, load, dan tinjau state konfigurasi."},
    {"Window", "Window", "Window", "Adjust overlay size, position, HUD, and window interaction.", "Atur ukuran overlay, posisi, HUD, dan interaksi window."},
    {"Style", "Style", "Style", "Tune font scale, opacity, spacing, borders, and rounding.", "Atur skala font, opacity, spacing, border, dan rounding."},
    {"State", "State", "State", "Reset feature state or clear selected runtime lists.", "Reset state fitur atau bersihkan daftar runtime yang dipilih."},
    {"Prediction", "Prediction", "Prediksi", "Inspect weighted opponent prediction rows and the eight-round forecast.", "Periksa baris prediksi opponent berbobot dan forecast delapan round."},
    {"Predict", "Predict", "Prediksi", "Inspect weighted next-opponent prediction rows.", "Periksa baris prediksi next-opponent berbobot."},
    {"Forecast", "Forecast", "Forecast", "Inspect the next eight predicted opponent rounds.", "Periksa delapan round opponent berikutnya yang diprediksi."},
    {"Ahead", "Ahead", "Ke Depan", "Shows how many rounds ahead this forecast row is.", "Menampilkan berapa round ke depan untuk row forecast ini."},
    {"Confidence", "Confidence", "Confidence", "Shows the weighted confidence for this forecast row.", "Menampilkan confidence berbobot untuk row forecast ini."},
    {"Source", "Source", "Sumber", "Shows the strongest prediction signal for this forecast row.", "Menampilkan sinyal prediksi terkuat untuk row forecast ini."},
    {"Bindings", "Bindings", "Binding", "Inspect resolved native and managed binding readiness.", "Periksa kesiapan binding native dan managed."},
    {"Round", "Round", "Round", "Inspect round state or configure round helpers.", "Periksa state round atau atur helper round."},
    {"Player", "Player", "Player", "Inspect player economy, rank, and shop state.", "Periksa economy, rank, dan state shop player."},
    {"Manager", "Manager", "Manager", "Inspect the selected battle manager.", "Periksa battle manager yang dipilih."},
    {"Bridge", "Bridge", "Bridge", "Inspect battle bridge state.", "Periksa state battle bridge."},
    {"Shop UI", "Shop UI", "UI Shop", "Inspect shop panel readiness and diagnostic readers.", "Periksa kesiapan panel shop dan reader diagnostik."},
    {"Behavior", "Behavior", "Behavior", "Inspect behavior API state.", "Periksa state behavior API."},
    {"Managers", "Managers", "Manager", "Inspect all detected battle managers.", "Periksa semua battle manager yang terdeteksi."},
    {"Automation", "Automation", "Automasi", "Configure automated shop decisions.", "Atur keputusan shop otomatis."},
    {"Hero Targets", "Hero Targets", "Target Hero", "Select heroes and target counts for shop automation.", "Pilih hero dan jumlah target untuk automasi shop."},
    {"Heroes", "Heroes", "Hero", "Spawn heroes from the loaded hero table.", "Spawn hero dari tabel hero yang sudah dimuat."},
    {"Items", "Items", "Item", "Grant equipment from the loaded item table.", "Berikan equipment dari tabel item yang sudah dimuat."},
    {"GogoCards", "GogoCards", "GogoCard", "Force selected Go Go Cards.", "Paksa Go Go Card yang dipilih."},
    {"Other", "Other", "Lainnya", "Configure arena economy, synergy, placement, and pool helpers.", "Atur helper economy, synergy, placement, dan pool arena."},
    {"Refresh update check", "Refresh update check", "Refresh check update", "Start a manual GitHub release metadata refresh.", "Mulai refresh manual metadata rilis GitHub."},
    {"Save configuration", "Save configuration", "Simpan konfigurasi", "Write the current overlay settings to the config file.", "Tulis pengaturan overlay saat ini ke file config."},
    {"Load configuration", "Load configuration", "Load konfigurasi", "Read settings from the selected config file.", "Baca pengaturan dari file config yang dipilih."},
    {"Reset visuals", "Reset visuals", "Reset visual", "Restore default appearance and window settings.", "Kembalikan tampilan dan window ke default."},
    {"Capture current menu size", "Capture current menu size", "Ambil ukuran menu saat ini", "Store the current menu size in Settings.", "Simpan ukuran menu saat ini di Settings."},
    {"Capture current position", "Capture current position", "Ambil posisi saat ini", "Store the current menu position and enable fixed positioning.", "Simpan posisi menu saat ini dan aktifkan posisi tetap."},
    {"Reset feature state", "Reset feature state", "Reset state fitur", "Disable runtime assists and restore feature defaults.", "Matikan assist runtime dan kembalikan default fitur."},
    {"Clear shop hero targets", "Clear shop hero targets", "Bersihkan target hero shop", "Remove all selected shop hero targets.", "Hapus semua target hero shop yang dipilih."},
    {"Retry test bindings", "Retry test bindings", "Coba ulang binding test", "Request a new binding resolve pass and reference refresh.", "Minta ulang proses resolve binding dan refresh reference."},
    {"Use self", "Use self", "Pakai self", "Inspect the local account.", "Periksa account lokal."},
    {"Use opponent", "Use opponent", "Pakai opponent", "Inspect the current opponent account.", "Periksa account opponent saat ini."},
    {"Clear account", "Clear account", "Bersihkan account", "Clear the manual account override.", "Bersihkan override account manual."},
    {"Clear hero targets", "Clear hero targets", "Bersihkan target hero", "Deselect every tracked shop hero.", "Batalkan pilihan semua hero shop yang dilacak."},
    {"Spawn", "Spawn", "Spawn", "Create the selected hero with the configured star level.", "Buat hero yang dipilih dengan star level terkonfigurasi."},
    {"Grant", "Grant", "Berikan", "Grant the selected equipment.", "Berikan equipment yang dipilih."},
    {"Select", "Select", "Pilih", "Use this card slot for the selected Go Go Card.", "Gunakan slot card ini untuk Go Go Card yang dipilih."},
    {"Clear card 1", "Clear card 1", "Bersihkan card 1", "Clear the first forced Go Go Card slot.", "Bersihkan slot Go Go Card paksa pertama."},
    {"Clear card 2", "Clear card 2", "Bersihkan card 2", "Clear the second forced Go Go Card slot.", "Bersihkan slot Go Go Card paksa kedua."},
    {"Apply Skip Round now", "Apply Skip Round now", "Terapkan Skip Round sekarang", "Request an immediate round skip to the target round.", "Minta skip round langsung ke target round."},
    {"Reset time scale", "Reset time scale", "Reset time scale", "Disable SpeedHack and restore Unity time scale to 1.0x.", "Matikan SpeedHack dan kembalikan time scale Unity ke 1.0x."},
    {"Spawn all heroes with selected cost", "Spawn all heroes with selected cost", "Spawn semua hero dengan cost terpilih", "Spawn every loaded hero matching the cost filter.", "Spawn semua hero yang cocok dengan filter cost."},
    {"Grant 999999 gold", "Grant 999999 gold", "Berikan 999999 gold", "Grant a large amount of gold to the local player.", "Berikan gold besar ke player lokal."},
    {"Language", "Language", "Bahasa", "Select the overlay menu language.", "Pilih bahasa menu overlay."},
    {"Theme", "Theme", "Theme", "Select the overlay color theme.", "Pilih theme warna overlay."},
    {"Font", "Font", "Font", "Select the overlay font.", "Pilih font overlay."},
    {"Invisible Scout - hide spectate switching", "Invisible Scout - hide spectate switching", "Invisible Scout - sembunyikan perpindahan spectate", "Hide spectator switching while scouting.", "Sembunyikan perpindahan spectate saat scouting."},
    {"Force defend win", "Force defend win", "Paksa defend menang", "Force defensive fight resolution toward a win.", "Paksa resolusi fight defensif agar menang."},
    {"Prevent self HP loss", "Prevent self HP loss", "Cegah HP sendiri berkurang", "Block local HP loss from fight resolution.", "Blokir pengurangan HP lokal dari resolusi fight."},
    {"Boost self attack ratio", "Boost self attack ratio", "Boost attack ratio sendiri", "Apply the configured attack ratio to local combat.", "Terapkan attack ratio terkonfigurasi ke combat lokal."},
    {"Self attack ratio %", "Self attack ratio %", "Attack ratio sendiri %", "Set the local attack ratio percentage.", "Atur persentase attack ratio lokal."},
    {"Self fight value", "Self fight value", "Fight value sendiri", "Set the local fight value override.", "Atur override fight value lokal."},
    {"Cripple enemy boards", "Cripple enemy boards", "Lemahkan board enemy", "Apply low combat values to enemy boards.", "Terapkan nilai combat rendah ke board enemy."},
    {"Enemy attack ratio %", "Enemy attack ratio %", "Attack ratio enemy %", "Set the enemy attack ratio percentage.", "Atur persentase attack ratio enemy."},
    {"Menu width", "Menu width", "Lebar menu", "Set the overlay window width.", "Atur lebar window overlay."},
    {"Menu height", "Menu height", "Tinggi menu", "Set the overlay window height.", "Atur tinggi window overlay."},
    {"Use fixed menu position", "Use fixed menu position", "Gunakan posisi menu tetap", "Pin the overlay to saved coordinates.", "Kunci overlay ke koordinat tersimpan."},
    {"Menu position X", "Menu position X", "Posisi menu X", "Set the saved overlay X coordinate.", "Atur koordinat X overlay tersimpan."},
    {"Menu position Y", "Menu position Y", "Posisi menu Y", "Set the saved overlay Y coordinate.", "Atur koordinat Y overlay tersimpan."},
    {"Show next enemy HUD", "Show next enemy HUD", "Tampilkan HUD enemy berikutnya", "Draw the predicted next enemy near the lower screen edge.", "Gambar prediksi enemy berikutnya dekat bawah layar."},
    {"Move from title bar only", "Move from title bar only", "Pindah hanya dari title bar", "Restrict window dragging to the title bar.", "Batasi drag window hanya dari title bar."},
    {"Resize from edges", "Resize from edges", "Resize dari tepi", "Allow resizing by dragging window edges.", "Izinkan resize dengan menarik tepi window."},
    {"Font size scale", "Font size scale", "Skala ukuran font", "Scale overlay text size.", "Skalakan ukuran teks overlay."},
    {"Window opacity", "Window opacity", "Opacity window", "Set overall overlay opacity.", "Atur opacity keseluruhan overlay."},
    {"Window border", "Window border", "Border window", "Set window border thickness.", "Atur ketebalan border window."},
    {"Frame border", "Frame border", "Border frame", "Set input and button border thickness.", "Atur ketebalan border input dan tombol."},
    {"Scrollbar size", "Scrollbar size", "Ukuran scrollbar", "Set scrollbar thickness.", "Atur ketebalan scrollbar."},
    {"Window rounding", "Window rounding", "Rounding window", "Set window corner rounding.", "Atur rounding sudut window."},
    {"Child rounding", "Child rounding", "Rounding child", "Set child panel corner rounding.", "Atur rounding sudut child panel."},
    {"Frame rounding", "Frame rounding", "Rounding frame", "Set input and button corner rounding.", "Atur rounding sudut input dan tombol."},
    {"Popup rounding", "Popup rounding", "Rounding popup", "Set popup corner rounding.", "Atur rounding sudut popup."},
    {"Scrollbar rounding", "Scrollbar rounding", "Rounding scrollbar", "Set scrollbar corner rounding.", "Atur rounding sudut scrollbar."},
    {"Grab rounding", "Grab rounding", "Rounding grab", "Set slider grab corner rounding.", "Atur rounding grab slider."},
    {"Tab rounding", "Tab rounding", "Rounding tab", "Set tab corner rounding.", "Atur rounding sudut tab."},
    {"Frame padding X", "Frame padding X", "Padding frame X", "Set horizontal padding inside framed controls.", "Atur padding horizontal di dalam kontrol ber-frame."},
    {"Frame padding Y", "Frame padding Y", "Padding frame Y", "Set vertical padding inside framed controls.", "Atur padding vertikal di dalam kontrol ber-frame."},
    {"Item spacing X", "Item spacing X", "Spacing item X", "Set horizontal spacing between controls.", "Atur jarak horizontal antar kontrol."},
    {"Item spacing Y", "Item spacing Y", "Spacing item Y", "Set vertical spacing between controls.", "Atur jarak vertikal antar kontrol."},
    {"Indent spacing", "Indent spacing", "Spacing indent", "Set indentation width.", "Atur lebar indent."},
    {"Auto-buy free heroes", "Auto-buy free heroes", "Auto-buy hero gratis", "Buy free shop heroes when available.", "Beli hero shop gratis saat tersedia."},
    {"Auto-buy selected targets", "Auto-buy selected targets", "Auto-buy target terpilih", "Buy tracked heroes until their target counts are met.", "Beli hero terlacak sampai jumlah target terpenuhi."},
    {"Force Scavenger to Always Get Expensive Heroes", "Force Scavenger to Always Get Expensive Heroes", "Paksa Scavenger selalu dapat hero mahal", "After automatic refreshes, clear cheaper slots when Scavenger is active.", "Setelah refresh otomatis, bersihkan slot lebih murah saat Scavenger aktif."},
    {"Auto-buy recommendation heroes", "Auto-buy recommendation heroes", "Auto-buy hero rekomendasi", "Buy each visible Recommendation Lineup hero until its target count is met.", "Beli setiap hero Recommendation Lineup yang terlihat sampai jumlah targetnya terpenuhi."},
    {"Recommendation target count", "Recommendation target count", "Jumlah target rekomendasi", "Set the desired owned count for this Recommendation Lineup hero.", "Atur jumlah owned yang diinginkan untuk hero Recommendation Lineup ini."},
    {"Auto-refresh shop", "Auto-refresh shop", "Auto-refresh shop", "Refresh shop while selected or Recommendation Lineup targets are still missing.", "Refresh shop saat target terpilih atau Recommendation Lineup masih kurang."},
    {"Pause refresh when free hero appears", "Pause refresh when free hero appears", "Pause refresh saat hero gratis muncul", "Stop refreshing while a free hero is visible.", "Hentikan refresh saat hero gratis terlihat."},
    {"Pause refresh when selected target appears", "Pause refresh when selected target appears", "Pause refresh saat target terpilih muncul", "Stop refreshing while a tracked target is visible.", "Hentikan refresh saat target terlacak terlihat."},
    {"Pause refresh when recommendation hero appears", "Pause refresh when recommendation hero appears", "Pause refresh saat hero rekomendasi muncul", "Stop refreshing while a Recommendation Lineup hero that still needs copies is visible.", "Hentikan refresh saat hero Recommendation Lineup yang masih kurang terlihat."},
    {"Keep gold reserve", "Keep gold reserve", "Pertahankan reserve gold", "Do not buy or refresh below the configured reserve.", "Jangan beli atau refresh hingga di bawah reserve terkonfigurasi."},
    {"Show tracked heroes only", "Show tracked heroes only", "Tampilkan hanya hero terlacak", "Filter the hero table to selected shop targets.", "Filter tabel hero ke target shop yang dipilih."},
    {"Target Count", "Target Count", "Jumlah Target", "Set the desired owned count for this hero.", "Atur jumlah owned yang diinginkan untuk hero ini."},
    {"Track", "Track", "Lacak", "Track this hero for selected-target shop automation.", "Lacak hero ini untuk automasi shop target terpilih."},
    {"Spawn star level", "Spawn star level", "Star level spawn", "Set the star level used by hero spawn actions.", "Atur star level untuk aksi spawn hero."},
    {"Grant enhanced item", "Grant enhanced item", "Berikan item enhanced", "Grant enhanced equipment instead of the base item.", "Berikan equipment enhanced, bukan item dasar."},
    {"Force selected GogoCards", "Force selected GogoCards", "Paksa GogoCard terpilih", "Force the selected Go Go Card IDs when card data is available.", "Paksa ID Go Go Card terpilih saat data card tersedia."},
    {"Skip Round", "Skip Round", "Skip Round", "Enable automatic round skipping toward the target round.", "Aktifkan skip round otomatis menuju target round."},
    {"Target round", "Target round", "Target round", "Set the round that Skip Round should reach.", "Atur round yang harus dicapai Skip Round."},
    {"SpeedHack", "SpeedHack", "SpeedHack", "Enable Unity timeScale control for Arena only.", "Aktifkan kontrol timeScale Unity hanya untuk Arena."},
    {"Time scale", "Time scale", "Time scale", "Set the Unity time scale while SpeedHack is enabled.", "Atur time scale Unity saat SpeedHack aktif."},
    {"Force all synergies active", "Force all synergies active", "Paksa semua synergy aktif", "Treat all synergy checks as active while enabled.", "Anggap semua pengecekan synergy aktif saat dinyalakan."},
    {"Force level and population 99", "Force level and population 99", "Paksa level dan population 99", "Raise local level and population helpers to 99.", "Naikkan helper level dan population lokal ke 99."},
    {"Allow outside-map placement", "Allow outside-map placement", "Izinkan placement di luar map", "Relax placement checks for out-of-map positions.", "Longgarkan pengecekan placement untuk posisi luar map."},
    {"Set all enemy HP to 1", "Set all enemy HP to 1", "Set semua HP enemy ke 1", "Pressure enemy HP values down to 1.", "Tekan nilai HP enemy menjadi 1."},
    {"Force Complete Achievements Task", "Force Complete Achievements Task", "Paksa task achievement selesai", "Force achievement result and round counters while bindings are ready.", "Paksa result achievement dan counter round saat binding siap."},
    {"Hero cost filter", "Hero cost filter", "Filter cost hero", "Choose which hero cost to use for spawn-all.", "Pilih cost hero untuk spawn-all."},
    {"Ready", "Ready", "Siap", "", ""},
    {"Waiting", "Waiting", "Menunggu", "", ""},
    {"Unknown", "Unknown", "Tidak diketahui", "", ""},
    {"Waiting for network check", "Waiting for network check", "Menunggu check jaringan", "", ""},
    {"Up to date", "Up to date", "Sudah terbaru", "", ""},
    {"Update available", "Update available", "Update tersedia", "", ""},
    {"GitHub request failed", "GitHub request failed", "Request GitHub gagal", "", ""},
    {"Malformed release metadata", "Malformed release metadata", "Metadata rilis tidak valid", "", ""},
    {"Unknown local version", "Unknown local version", "Versi lokal tidak diketahui", "", ""},
    {"Library update status", "Library update status", "Status update library", "", ""},
    {"Updates / Changelog", "Updates / Changelog", "Update / Changelog", "Show GitHub release status and cached release notes.", "Tampilkan status rilis GitHub dan catatan rilis cache."},
    {"Field", "Field", "Field", "", ""},
    {"Value", "Value", "Nilai", "", ""},
    {"Runtime Status", "Runtime Status", "Status Runtime", "Show binding, cache, and feature readiness.", "Tampilkan kesiapan binding, cache, dan fitur."},
    {"Runtime", "Runtime", "Runtime", "", ""},
    {"State", "State", "State", "", ""},
    {"Signal", "Signal", "Sinyal", "", ""},
    {"Repository", "Repository", "Repository", "", ""},
    {"Current version", "Current version", "Versi saat ini", "", ""},
    {"Current commit", "Current commit", "Commit saat ini", "", ""},
    {"Current ref", "Current ref", "Ref saat ini", "", ""},
    {"Latest version", "Latest version", "Versi terbaru", "", ""},
    {"Release date", "Release date", "Tanggal rilis", "", ""},
    {"Last check", "Last check", "Check terakhir", "", ""},
    {"Status", "Status", "Status", "", ""},
    {"Summary", "Summary", "Ringkasan", "", ""},
    {"Failure", "Failure", "Kegagalan", "", ""},
    {"GGC", "GGC", "GGC", "", ""},
    {"Players", "Players", "Player", "", ""},
    {"Will fight", "Will fight", "Akan lawan", "", ""},
    {"Recent", "Recent", "Recent", "", ""},
    {"Hero", "Hero", "Hero", "", ""},
    {"Cost", "Cost", "Cost", "", ""},
    {"Action", "Action", "Aksi", "", ""},
    {"Item", "Item", "Item", "", ""},
    {"Card", "Card", "Card", "", ""},
    {"Card 1", "Card 1", "Card 1", "", ""},
    {"Card 2", "Card 2", "Card 2", "", ""},
    {"Quality", "Quality", "Kualitas", "", ""},
    {"Current enemy", "Current enemy", "Enemy saat ini", "", ""},
    {"Typography", "Typography", "Tipografi", "", ""},
    {"Rounding", "Rounding", "Rounding", "", ""},
    {"Spacing", "Spacing", "Spacing", "", ""},
    {"Saved state includes visual settings, window and HUD settings, and Combat, Shop, and Arena controls.", "Saved state includes visual settings, window and HUD settings, and Combat, Shop, and Arena controls.", "State tersimpan mencakup pengaturan visual, window dan HUD, serta kontrol Combat, Shop, dan Arena.", "", ""},
    {"Recommendation Lineup", "Recommendation Lineup", "Recommendation Lineup", "", ""},
    {"Current recommendation", "Current recommendation", "Rekomendasi saat ini", "", ""},
    {"Current recommendation: Waiting", "Current recommendation: Waiting", "Rekomendasi saat ini: Menunggu", "", ""},
    {"Recommended lineup heroes", "Recommended lineup heroes", "Hero lineup rekomendasi", "", ""},
    {"No recommendation lineup heroes detected", "No recommendation lineup heroes detected", "Tidak ada hero Recommendation Lineup terdeteksi", "", ""},
    {"Showing %d / %d heroes", "Showing %d / %d heroes", "Menampilkan %d / %d hero", "", ""},
    {"Showing %d / %d items", "Showing %d / %d items", "Menampilkan %d / %d item", "", ""},
    {"Showing %d / %d cards", "Showing %d / %d cards", "Menampilkan %d / %d card", "", ""},
    {"Card 1: %d  Card 2: %d", "Card 1: %d  Card 2: %d", "Card 1: %d  Card 2: %d", "", ""},
    {"Current round", "Current round", "Round saat ini", "", ""},
    {"Default", "Default", "Default", "", ""},
    {"Configuration file path", "Configuration file path", "Path file konfigurasi", "Edit the config file path used by save and load.", "Edit path file config yang dipakai save dan load."},
    {"Account ID to inspect (empty = self)", "Account ID to inspect (empty = self)", "Account ID untuk diperiksa (kosong = self)", "Enter an account ID for Test diagnostics.", "Masukkan account ID untuk diagnostik Test."},
    {"No release notes provided", "No release notes provided", "Tidak ada release notes", "", ""},
    {"Waiting for release metadata", "Waiting for release metadata", "Menunggu metadata rilis", "", ""},
    {"Waiting for IL2CPP runtime", "Waiting for IL2CPP runtime", "Menunggu runtime IL2CPP", "", ""},
    {"Waiting for GGC data", "Waiting for GGC data", "Menunggu data GGC", "", ""},
    {"No GGC qualities detected", "No GGC qualities detected", "Tidak ada kualitas GGC terdeteksi", "", ""},
    {"Waiting for battle data", "Waiting for battle data", "Menunggu data battle", "", ""},
    {"Waiting for player list", "Waiting for player list", "Menunggu daftar player", "", ""},
    {"No players found", "No players found", "Player tidak ditemukan", "", ""},
    {"Waiting for spectator hook", "Waiting for spectator hook", "Menunggu hook spectator", "", ""},
    {"Waiting for Noto Sans CJK font", "Waiting for Noto Sans CJK font", "Menunggu font Noto Sans CJK", "", ""},
    {"Waiting for shop automation bindings", "Waiting for shop automation bindings", "Menunggu binding automasi shop", "", ""},
    {"Waiting for shop refresh panel", "Waiting for shop refresh panel", "Menunggu panel refresh shop", "", ""},
    {"Waiting for recommendation lineup bindings", "Waiting for recommendation lineup bindings", "Menunggu binding Recommendation Lineup", "", ""},
    {"Waiting for Scavenger shop bindings", "Waiting for Scavenger shop bindings", "Menunggu binding shop Scavenger", "", ""},
    {"Waiting for hero table", "Waiting for hero table", "Menunggu tabel hero", "", ""},
    {"No tracked heroes selected", "No tracked heroes selected", "Tidak ada hero terlacak dipilih", "", ""},
    {"No heroes available", "No heroes available", "Tidak ada hero tersedia", "", ""},
    {"Waiting for arena hero bindings", "Waiting for arena hero bindings", "Menunggu binding hero arena", "", ""},
    {"Waiting for arena item binding", "Waiting for arena item binding", "Menunggu binding item arena", "", ""},
    {"Waiting for item table", "Waiting for item table", "Menunggu tabel item", "", ""},
    {"No items available", "No items available", "Tidak ada item tersedia", "", ""},
    {"Waiting for GogoCard binding", "Waiting for GogoCard binding", "Menunggu binding GogoCard", "", ""},
    {"Waiting for GogoCard table", "Waiting for GogoCard table", "Menunggu tabel GogoCard", "", ""},
    {"No GogoCards available", "No GogoCards available", "Tidak ada GogoCard tersedia", "", ""},
    {"Waiting for round skip bindings", "Waiting for round skip bindings", "Menunggu binding skip round", "", ""},
    {"Waiting for timeScale binding", "Waiting for timeScale binding", "Menunggu binding timeScale", "", ""},
    {"Waiting for synergy hooks", "Waiting for synergy hooks", "Menunggu hook synergy", "", ""},
    {"Waiting for player data bindings", "Waiting for player data bindings", "Menunggu binding data player", "", ""},
    {"Waiting for achievement bindings", "Waiting for achievement bindings", "Menunggu binding achievement", "", ""},
    {"Waiting for battle manager list", "Waiting for battle manager list", "Menunggu daftar battle manager", "", ""},
    {"Waiting for prediction refresh", "Waiting for prediction refresh", "Menunggu refresh prediksi", "", ""},
    {"No 8-round forecast yet", "No 8-round forecast yet", "Forecast 8 round belum tersedia", "", ""},
    {"Waiting for diagnostic budget", "Waiting for diagnostic budget", "Menunggu budget diagnostik", "", ""},
    {"Cycle", "Cycle", "Siklus", "", ""},
    {"Rotation", "Rotation", "Rotasi", "", ""},
    {"Learned", "Learned", "Belajar", "", ""},
    {"History", "History", "Histori", "", ""},
    {"Blue", "Blue", "Biru", "", ""},
    {"Purple", "Purple", "Ungu", "", ""},
    {"Gold", "Gold", "Gold", "", ""}
};

// Returns a localized menu entry for the currently selected language.
const MenuI18nEntry* FindMenuI18nEntry(const char* key) {
    if (!key || key[0] == '\0') {
        return nullptr;
    }

    for (const MenuI18nEntry& entry : kMenuI18nEntries) {
        if (strcmp(entry.key, key) == 0) {
            return &entry;
        }
    }

    return nullptr;
}

// Returns localized visible menu text with English fallback.
const char* MenuText(const char* key) {
    const MenuI18nEntry* entry = FindMenuI18nEntry(key);
    if (!entry) {
        return key ? key : "";
    }

    int languageIndex =
        std::clamp(UiState::LanguageIndex.load(), 0, kMenuLanguageCount - 1);
    return languageIndex == kLanguageIndonesian ? entry->id : entry->en;
}

// Builds an ImGui label that preserves hidden ID suffixes after localization.
std::string MenuLabel(const char* label) {
    if (!label) {
        return {};
    }

    const char* idSuffix = strstr(label, "##");
    if (!idSuffix) {
        return MenuText(label);
    }

    if (idSuffix == label) {
        return label;
    }

    std::string visible(label, static_cast<size_t>(idSuffix - label));
    std::string localized = MenuText(visible.c_str());
    localized += idSuffix;
    return localized;
}

// Returns localized tooltip text for an ImGui item source label.
const char* MenuTooltipText(const char* label) {
    if (!label || label[0] == '\0') {
        return nullptr;
    }

    const char* idSuffix = strstr(label, "##");
    std::string key;
    if (idSuffix && idSuffix != label) {
        key.assign(label, static_cast<size_t>(idSuffix - label));
    } else if (idSuffix == label) {
        return nullptr;
    } else {
        key = label;
    }

    const MenuI18nEntry* entry = FindMenuI18nEntry(key.c_str());
    if (!entry) {
        static thread_local std::string fallbackTooltip;
        fallbackTooltip = key;
        return fallbackTooltip.c_str();
    }

    int languageIndex =
        std::clamp(UiState::LanguageIndex.load(), 0, kMenuLanguageCount - 1);
    const char* tooltip =
        languageIndex == kLanguageIndonesian ? entry->tooltipId : entry->tooltipEn;

    if (tooltip && tooltip[0] != '\0') {
        return tooltip;
    }

    return languageIndex == kLanguageIndonesian ? entry->id : entry->en;
}

// Adds a localized tooltip to the last submitted ImGui menu item.
void DrawMenuTooltip(const char* label) {
    const char* tooltip = MenuTooltipText(label);
    if (tooltip && tooltip[0] != '\0') {
        ImGui::SetItemTooltip("%s", tooltip);
    }
}

// Draws a localized menu button and attaches the matching tooltip.
bool DrawMenuButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    std::string localized = MenuLabel(label);
    bool pressed = ImGui::Button(localized.c_str(), size);
    DrawMenuTooltip(label);
    return pressed;
}

// Draws a localized menu tab item and attaches the matching tooltip.
bool BeginMenuTabItem(const char* label, ImGuiTabItemFlags flags = 0) {
    std::string localized = MenuLabel(label);
    bool opened = ImGui::BeginTabItem(localized.c_str(), nullptr, flags);
    DrawMenuTooltip(label);
    return opened;
}

// Draws a localized collapsible header and attaches the matching tooltip.
bool DrawMenuCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) {
    std::string localized = MenuLabel(label);
    bool opened = ImGui::CollapsingHeader(localized.c_str(), flags);
    DrawMenuTooltip(label);
    return opened;
}

// Draws localized separator text without changing runtime state.
void DrawMenuSeparatorText(const char* label) {
    ImGui::SeparatorText(MenuText(label));
}

// Original function pointers resolved from IL2CPP metadata or hook trampolines.
namespace Originals {
    EGLBoolean (*EglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
    Touch (*Input_GetTouch)(int index);

    Il2CppString* (*MCLogicBattleData_ILOGIC_GetSelfChessPlayerName)(
        void* instance,
        uint64_t accID
    );
    MonoStructures::Dictionary<uint64_t, void*>* (*MCLogicBattleData_ILOGIC_GetAllBattleMgr)(
        void* instance
    );
    uint64_t (*MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID)(
        void* instance,
        uint64_t accID
    );
    int (*MCLogicBattleData_ILOGIC_GetCrystalQualityByRound)(
        void* instance,
        uint64_t accID,
        int roundId
    );
    uint32_t (*MCLogicBattleData_ILOGIC_GetGameRound)(void* instance);
    int (*MCLogicBattleData_ILOGIC_GetGamePhase)(void* instance);
    uint32_t (*MCLogicBattleData_ILOGIC_GetRoundRemainTime)(void* instance);
    uint64_t (*MCLogicBattleData_ILOGIC_GetRoundMaxRemainTime)(void* instance);
    bool (*MCLogicBattleData_ILOGIC_IsFightSection)(void* instance);
    bool (*MCLogicBattleData_ILOGIC_IsFightResultSection)(void* instance);
    bool (*MCLogicBattleData_ILOGIC_IsSelfFightOver)(void* instance);
    bool (*MCLogicBattleData_ILOGIC_GetIsMonsterRound)(void* instance);
    bool (*MCLogicBattleData_ILOGIC_IsRealPlayerMode)(void* instance);
    int (*MCLogicBattleData_ILOGIC_GetPlayerCoin)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetPlayerHP)(
        void* instance,
        uint64_t accountId
    );
    bool (*MCLogicBattleData_ILOGIC_GetBattleResultHistory)(
        void* instance,
        uint64_t accountId,
        int round
    );
    void* (*MCLogicBattleData_ILOGIC_GetPlayerData)(
        void* instance,
        uint64_t accountId
    );
    void* (*MCLogicBattleData_ILOGIC_GetStPlayerData)(
        void* instance,
        uint64_t accountId
    );
    MCLogicHeroShopItemData (*MCLogicBattleData_ILOGIC_GetShopItemData)(
        void* instance,
        uint64_t accountId,
        int slot
    );
    bool (*MCLogicBattleData_ILOGIC_IsCurrFreeBuy)(
        void* instance,
        uint64_t accountId,
        int slot,
        bool* needFx
    );
    int (*MCLogicBattleData_ILOGIC_GetRefreshCost)(
        void* instance,
        uint64_t accountId
    );
    bool (*MCLogicBattleData_ILOGIC_IsRefreshFree)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILogic_HeroOwnCount)(
        void* instance,
        uint64_t accountId,
        int heroId
    );
    int (*MCLogicBattleData_ILogic_HeroCountInPool)(
        void* instance,
        int heroId
    );
    int (*MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup)(void* instance);
    uint32_t (*MCLogicBattleData_ILOGIC_GetBuidByAccID)(void* instance, uint64_t accountId);
    uint32_t (*MCLogicBattleData_ILOGIC_GetGuidByAccID)(void* instance, uint64_t accountId);
    uint32_t (*MCLogicBattleData_ILOGIC_GetChessPlayerGuid)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetChessPlayerConfigID)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetChessSkinId)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetRank)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILGOIC_GetCampAliveCountByAccId)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetCampRankByAccId)(
        void* instance,
        uint64_t accountId
    );
    uint32_t (*MCLogicBattleData_ILOGIC_GetCup)(void* instance, uint64_t accountId);
    uint32_t (*MCLogicBattleData_ILOGIC_GetWarmValue)(void* instance, uint64_t accountId);
    uint32_t (*MCLogicBattleData_ILOGIC_GetRankLevel)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetCommanderLv)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetPlayerLevel)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetUpgradeCost)(void* instance, uint64_t accountId);
    bool (*MCLogicBattleData_ILOGIC_CanUpgrade)(
        void* instance,
        uint64_t accountId,
        int coin
    );
    bool (*MCLogicBattleData_ILOGIC_GetShopIsForbid)(void* instance, uint64_t accountId);
    AstarInt2 (*MCLogicBattleData_ILOGIC_GetShopStarLv)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetShopRuleBuyTimes)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_GetFreeFreshShopCount)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetCurRefreshShopLevel)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetHeroItemCount)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetHeroSlotDict_Count)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetBattleHeroNum)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetAllHeroNum)(void* instance, uint64_t accountId);
    int (*MCLogicBattleData_ILOGIC_GetBattleHeroTotalStart)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetBattleCount)(void* instance, uint64_t accountId);
    bool (*MCLogicBattleData_ILOGIC_IsCurrentLogic)(void* instance, uint64_t accountId);
    bool (*MCLogicBattleData_ILOGIC_IsPlayerCanPauseJudger)(
        void* instance,
        uint64_t accountId
    );
    int (*MCLogicBattleData_ILOGIC_GetSelfCamp)(void* instance);
    int (*MCLogicBattleData_ILOGIC_SelfTotalPopulation)(void* instance);
    int (*MCLogicBattleData_ILOGIC_SelfCurPopulation)(void* instance);
    int (*MCLogicBattleData_ILOGIC_GetSpareChessNum)(void* instance);
    int (*MCLogicBattleData_ILOGIC_GetHeroByStarUp)(void* instance);
    void* (*MCLogicBattleData_get_logicRoundMgr)(void* instance);
    void (*LogicRoundMgr_SetRound)(void* instance, uint32_t round);
    void (*LogicRoundMgr_NextRound)(void* instance, bool isAfterWelfare);
    void (*UnityEngine_Time_set_timeScale)(float value);

    void* (*MCComp_GetGamer)(uint64_t accountId);
    void* (*MCComp_GetGoGoCardComp)(uint64_t accountId);

    void* (*CData_MCHero_GetInstance)();
    MonoStructures::Dictionary<int, void*>* (*CData_MCHero_GetAll)(void* instance);
    void* (*CData_MCEquipBase_GetInstance)();
    MonoStructures::Dictionary<int, MonoStructures::Dictionary<int, void*>*>*
        (*CData_MCEquipBase_GetAll)(void* instance);
    void* (*CData_MCSuperCrystalKey_GetInstance)();
    MonoStructures::Dictionary<int, void*>* (*CData_MCSuperCrystalKey_GetAll)(void* instance);
    void* (*CData_RelationSkillTip_MC_GetInstance)();
    MonoStructures::Dictionary<int, void*>* (*CData_RelationSkillTip_MC_GetAll)(void* instance);
    Il2CppString* (*ShowMsgTool_GetDesc)(int id);
    bool (*LoadRes_IsCommander)(void* instance, int heroId);

    bool (*MCLogicBattleManager_BuyNormalHero)(
        void* instance,
        MCLogicHeroShopItemData* itemData,
        bool* ignoreExtraRule
    );
    bool (*MCLogicBattleManager_get_m_bDefendFaild)(void* instance);
    bool (*MCLogicBattleManager_get_IsHost)(void* instance);
    uint64_t (*MCLogicBattleManager_get_m_uAccountId)(void* instance);
    void* (*MCLogicBattleManager_GetCurrentOpponent)(void* instance);
    void (*MCLogicBattleManager_OnModifyPlayerBlood)(
        void* instance,
        int reduceHp,
        bool isFromCurse,
        int effectId
    );
    void (*MCLogicBattleManager_OnFightOver)(
        void* instance,
        bool failed,
        bool includeInvader,
        bool doubleFailed
    );
    bool (*MCLogicBattleManager_HasAliveFighter)(void* instance, int campType);
    void (*MCLogicBattleManager_GetAliveFighter)(
        void* instance,
        int* campACount,
        int* campBCount
    );
    void* (*MCBehaviorThreeApi_Get)(uint64_t accountId);
    int (*MCBehaviorThreeApi_GetCurrentBattleRoundResult)(void* instance);
    int (*MCBehaviorThreeApi_GetCurrentPhaseType)(void* instance);
    MonoStructures::Dictionary<uint64_t, uint64_t>* (*LogicInvasionMgr_GetCurPairDict)(
        void* instance
    );
    uint64_t (*LogicInvasionMgr_GetCurPair)(void* instance, uint64_t accountId);
    bool (*LogicInvasionMgr_IsRealPlayerMode)(void* instance);
    bool (*LogicInvasionMgr_IsMonsterRound)(void* instance, uint32_t roundIndex);
    void* (*MCEquipUtil_OnGetNewEquip)(
        uint64_t accountId,
        int equipId,
        uint32_t* guid,
        int equipUpgradeState
    );
    void (*UIPanelBattleHeroShop_KeyBoardRefreshShop)(void* instance);
    void (*UIPanelBattleHeroShop_KeyBoardShopSelect)(void* instance, int slot);
    void (*UIPanelBattleHeroShop_BuyHero)(void* instance, uint8_t slot, bool refreshSameHero);
    void (*UIPanelBattleHeroShop_HeroItemList_OnSelectHero)(void* instance, uint8_t slot);
    uint32_t (*UIPanelBattleHeroShop_get_lastOperationTime)(void* instance);
    bool (*UIPanelBattleHeroShop_IsDelayOpen)(void* instance);
    bool (*UIPanelBattleHeroShop_GetInfoAfterSpectate)(void* instance);
    bool (*UIPanelBattleHeroShop_CanOperate)(void* instance, bool onlyCheckState);
    bool (*MCBattleBridge_IsHeroInRecommendLineup)(void* instance, int heroId);
    bool (*MCBattleBridge_IsSuperCrystalShopOpen)(void* instance);
    bool (*MCBattleBridge_IsGoGoCardPanelOpen)(void* instance);
    bool (*MCBattleBridge_CheckEnableKeyBoard)(void* instance);
    void (*MCBattleBridge_OnRefreshShop)(
        void* instance,
        bool isAutoRefresh,
        void* dictHeroSlot,
        void* sameRefreshHero,
        bool isDelayOpenShop,
        bool onlyRefreshHero
    );
    int64_t (*MCBattleBridge_GetFreeMemory)(void* instance);
    uint32_t (*MCBattleBridge_GetPingTimes)(void* instance);
    float (*MCBattleBridge_GetStdevPing)(void* instance);
    float (*MCBattleBridge_GetStdevFps)(void* instance);
    void (*MCChessPlayerData_UpdateCoin)(void* instance, int addValue, int changeType);

    void (*MCShowSpectatorComp_SetSpectate)(void* instance, uint64_t accountId);
    bool (*MCBondUtil_CheckRelationActive_Config)(void* config, int curActiveCount, void* curBondDict);
    int (*MCBondUtil_GetBondActiveCount)(uint64_t accountId, int bondId, bool onlyActive);
    bool (*MCBondUtil_CheckRelationActive_Special)(
        void* specialCondition,
        int needCount,
        int curActiveCount,
        void* curBondDict
    );
    bool (*MCLogicAchievementRecordComp_AchievementDataBase_GetResult)(void* instance);
    bool (*MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData)(
        void* instance
    );
    bool (*MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation)(
        void* instance
    );
    bool (*MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition)(
        void* instance,
        void* players
    );
    bool (*MCLogicAchievementRecordComp_AchievementRoundData_GetResult)(void* instance);
    void (*MCLogicAchievementRecordComp_AchievementRoundData_RefreshData)(void* instance);
    AstarInt2 (*ShowBattleTouchMgr_ClampGridPos)(void* instance, AstarInt2 gridPos);
    bool (*AStarTileMap_ValidPos)(int x, int y);
    bool (*MCLogicEntityMap_CanWalkable)(void* instance, int x, int y);
    bool (*MCLogicEntityMap_IsWalkableAround)(void* instance, int x, int y);
}

// Checks whether the current process is the Unity target process.
bool IsUnityMoontonProcess() {
    FILE* fp = fopen("/proc/self/cmdline", "r");
    if (!fp) {
        return false;
    }

    char buffer[1024];
    size_t len = fread(buffer, 1, sizeof(buffer) - 1, fp);
    fclose(fp);

    if (len == 0) {
        return false;
    }

    buffer[len] = '\0';
    return strstr(buffer, ":UnityKillsMe") != nullptr;
}

// Returns true when needle appears in haystack without case sensitivity.
bool StringIncludesCaseInsensitive(
    const std::string& haystack,
    const std::string& needle
) {
    auto it = std::search(
        haystack.begin(),
        haystack.end(),
        needle.begin(),
        needle.end(),
        [](char ch1, char ch2) {
            return std::toupper(static_cast<unsigned char>(ch1)) ==
                   std::toupper(static_cast<unsigned char>(ch2));
        }
    );

    return it != haystack.end();
}

// Builds a stable key for caching resolved IL2CPP methods.
std::string GenerateCacheKey(
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    std::string key;
    key.reserve(
        (ns ? strlen(ns) : 0) +
        strlen(className) +
        strlen(methodName) +
        (paramTypes.size() * 16) +
        6
    );

    if (ns) {
        key += ns;
    }

    key += "::";
    key += className;
    key += "::";
    key += methodName;
    key += "(";

    for (size_t i = 0; i < paramTypes.size(); ++i) {
        key += paramTypes[i];

        if (i < paramTypes.size() - 1) {
            key += ",";
        }
    }

    key += ")";
    return key;
}

// Builds a stable key for caching resolved IL2CPP fields.
std::string GenerateFieldCacheKey(
    const char* ns,
    const char* className,
    const char* fieldName
) {
    std::string key;
    key.reserve(
        (ns ? strlen(ns) : 0) +
        strlen(className) +
        strlen(fieldName) +
        4
    );

    if (ns) {
        key += ns;
    }

    key += "::";
    key += className;
    key += "::";
    key += fieldName;
    return key;
}

bool HasIl2CppMethodScanApi();
bool HasIl2CppFieldScanApi();
bool IsIl2CppRuntimeReady();

// Resolves a class name, including nested class paths.
Il2CppClass* ResolveClassFromName(
    const Il2CppImage* image,
    const char* namespaze,
    const char* className
) {
    if (!image || !className || !il2cpp_class_from_name) {
        return nullptr;
    }

    Il2CppClass* klass = il2cpp_class_from_name(image, namespaze, className);
    if (klass) {
        return klass;
    }

    if (!il2cpp_class_get_nested_types || !il2cpp_class_get_name) {
        return nullptr;
    }

    char nameCopy[512]{0};
    strncpy(nameCopy, className, sizeof(nameCopy) - 1);

    char* ctx = nullptr;
    char* token = strtok_r(nameCopy, ".+/", &ctx);

    if (!token) {
        return nullptr;
    }

    Il2CppClass* current = il2cpp_class_from_name(image, namespaze, token);

    while (current && (token = strtok_r(nullptr, ".+/", &ctx))) {
        void* iter = nullptr;
        Il2CppClass* nested = nullptr;
        Il2CppClass* found = nullptr;

        while ((nested = il2cpp_class_get_nested_types(current, &iter))) {
            const char* nestedName = il2cpp_class_get_name(nested);

            if (nestedName && strcmp(nestedName, token) == 0) {
                found = nested;
                break;
            }
        }

        current = found;
    }

    return current;
}

// Searches a class and its parents for a field.
FieldInfo* FindFieldInClassHierarchy(Il2CppClass* klass, const char* fieldName) {
    if (!klass || !fieldName) {
        return nullptr;
    }

    if (!il2cpp_class_get_field_from_name &&
        (!il2cpp_class_get_fields || !il2cpp_field_get_name)) {
        return nullptr;
    }

    Il2CppClass* currentKlass = klass;

    while (currentKlass) {
        FieldInfo* field = nullptr;

        if (il2cpp_class_get_field_from_name) {
            field = il2cpp_class_get_field_from_name(currentKlass, fieldName);
            if (field) {
                return field;
            }
        }

        if (il2cpp_class_get_fields && il2cpp_field_get_name) {
            void* iter = nullptr;

            while ((field = il2cpp_class_get_fields(currentKlass, &iter))) {
                const char* currentFieldName = il2cpp_field_get_name(field);

                if (currentFieldName && strcmp(currentFieldName, fieldName) == 0) {
                    return field;
                }
            }
        }

        if (!il2cpp_class_get_parent) {
            break;
        }

        currentKlass = il2cpp_class_get_parent(currentKlass);
    }

    return nullptr;
}

// Resolves and caches IL2CPP field metadata by class and field name.
FieldInfo* GetFieldInfoFromName(
    const char* ns,
    const char* className,
    const char* fieldName
) {
    if (!className || !fieldName || !HasIl2CppFieldScanApi()) {
        return nullptr;
    }

    std::string cacheKey = GenerateFieldCacheKey(ns, className, fieldName);

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        auto cached = FieldCache.find(cacheKey);
        if (cached != FieldCache.end()) {
            if (cached->second) {
                return cached->second;
            }

            auto miss = FieldMissCache.find(cacheKey);
            if (miss != FieldMissCache.end() &&
                std::chrono::steady_clock::now() - miss->second <
                    std::chrono::milliseconds(RuntimeConfig::BindingRetryMs)) {
                return nullptr;
            }
        }
    }

    size_t size = 0;
    Il2CppDomain* domain = il2cpp_domain_get();

    if (!domain) {
        return nullptr;
    }

    const Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) {
        return nullptr;
    }

    for (size_t i = 0; i < size; ++i) {
        const Il2CppImage* image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) {
            continue;
        }

        Il2CppClass* klass = ResolveClassFromName(image, ns, className);
        if (!klass) {
            continue;
        }

        FieldInfo* field = FindFieldInClassHierarchy(klass, fieldName);
        if (field) {
            std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
            FieldCache[cacheKey] = field;
            FieldMissCache.erase(cacheKey);
            return field;
        }
    }

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        FieldCache[cacheKey] = nullptr;
        FieldMissCache[cacheKey] = std::chrono::steady_clock::now();
    }
    return nullptr;
}

// Resolves an instance field address from IL2CPP metadata and a bounded offset.
bool ResolveInstanceFieldAddress(Il2CppObject* instance, FieldInfo* field, void** address) {
    if (!instance || !field || !address || !il2cpp_field_get_offset) {
        return false;
    }

    size_t offset = il2cpp_field_get_offset(field);
    if (offset == static_cast<size_t>(-1) || offset > RuntimeConfig::MaxInstanceFieldOffsetBytes) {
        return false;
    }

    *address = reinterpret_cast<uint8_t*>(instance) + offset;
    return true;
}

// Reads an instance field by offset through a bounded direct copy.
bool ReadInstanceFieldByOffset(Il2CppObject* instance, FieldInfo* field, void* outValue, size_t valueSize) {
    if (!IsIl2CppRuntimeReady() || !outValue || valueSize == 0) {
        return false;
    }

    void* address = nullptr;
    if (!ResolveInstanceFieldAddress(instance, field, &address)) {
        return false;
    }

    memcpy(outValue, address, valueSize);
    return true;
}

// Writes an instance field by offset through direct memory when it is safe.
bool WriteInstanceFieldByOffset(Il2CppObject* instance, FieldInfo* field, const void* value, size_t valueSize) {
    if (!IsIl2CppRuntimeReady() || !value || valueSize == 0) {
        return false;
    }

    void* address = nullptr;
    if (!ResolveInstanceFieldAddress(instance, field, &address)) {
        return false;
    }

    memcpy(address, value, valueSize);
    return true;
}

// Reads an instance field by metadata into a caller-provided buffer through IL2CPP.
bool GetFieldRaw(Il2CppObject* instance, FieldInfo* field, void* outValue) {
    if (!IsIl2CppRuntimeReady() || !instance || !field || !outValue || !il2cpp_field_get_value) {
        return false;
    }

    il2cpp_field_get_value(instance, field, outValue);
    return true;
}

// Reads an instance field by name into a caller-provided buffer.
bool GetFieldRaw(
    Il2CppObject* instance,
    const char* ns,
    const char* className,
    const char* fieldName,
    void* outValue
) {
    return GetFieldRaw(
        instance,
        GetFieldInfoFromName(ns, className, fieldName),
        outValue
    );
}

// Reads a static field by metadata into a caller-provided buffer.
bool GetStaticFieldRaw(FieldInfo* field, void* outValue) {
    if (!IsIl2CppRuntimeReady() || !field || !outValue || !il2cpp_field_static_get_value) {
        return false;
    }

    il2cpp_field_static_get_value(field, outValue);
    return true;
}

// Reads a static field by name into a caller-provided buffer.
bool GetStaticFieldRaw(
    const char* ns,
    const char* className,
    const char* fieldName,
    void* outValue
) {
    return GetStaticFieldRaw(
        GetFieldInfoFromName(ns, className, fieldName),
        outValue
    );
}

// Writes an instance field by metadata from a caller-provided buffer.
bool SetFieldRaw(Il2CppObject* instance, FieldInfo* field, const void* value) {
    if (!IsIl2CppRuntimeReady() || !instance || !field || !value || !il2cpp_field_set_value) {
        return false;
    }

    il2cpp_field_set_value(instance, field, const_cast<void*>(value));
    return true;
}

// Writes an instance field by name from a caller-provided buffer.
bool SetFieldRaw(
    Il2CppObject* instance,
    const char* ns,
    const char* className,
    const char* fieldName,
    const void* value
) {
    return SetFieldRaw(
        instance,
        GetFieldInfoFromName(ns, className, fieldName),
        value
    );
}

// Writes a static field by metadata from a caller-provided buffer.
bool SetStaticFieldRaw(FieldInfo* field, const void* value) {
    if (!IsIl2CppRuntimeReady() || !field || !value || !il2cpp_field_static_set_value) {
        return false;
    }

    il2cpp_field_static_set_value(field, const_cast<void*>(value));
    return true;
}

// Writes a static field by name from a caller-provided buffer.
bool SetStaticFieldRaw(
    const char* ns,
    const char* className,
    const char* fieldName,
    const void* value
) {
    return SetStaticFieldRaw(
        GetFieldInfoFromName(ns, className, fieldName),
        value
    );
}

// Reads a typed instance field by metadata, preferring direct offset access.
template <typename T>
T GetField(Il2CppObject* instance, FieldInfo* field) {
    T value{};
    if (!ReadInstanceFieldByOffset(instance, field, &value, sizeof(T))) {
        GetFieldRaw(instance, field, &value);
    }
    return value;
}

// Reads a typed instance field by name.
template <typename T>
T GetField(
    Il2CppObject* instance,
    const char* ns,
    const char* className,
    const char* fieldName
) {
    T value{};
    FieldInfo* field = GetFieldInfoFromName(ns, className, fieldName);
    if (!ReadInstanceFieldByOffset(instance, field, &value, sizeof(T))) {
        GetFieldRaw(instance, field, &value);
    }
    return value;
}

// Reads a typed static field by metadata.
template <typename T>
T GetStaticField(FieldInfo* field) {
    T value{};
    GetStaticFieldRaw(field, &value);
    return value;
}

// Reads a typed static field by name.
template <typename T>
T GetStaticField(
    const char* ns,
    const char* className,
    const char* fieldName
) {
    T value{};
    GetStaticFieldRaw(ns, className, fieldName, &value);
    return value;
}

// Writes a typed instance field by metadata, preserving IL2CPP barriers for pointer fields.
template <typename T>
bool SetField(Il2CppObject* instance, FieldInfo* field, const T& value) {
    if constexpr (!std::is_pointer_v<T>) {
        if (WriteInstanceFieldByOffset(instance, field, &value, sizeof(T))) {
            return true;
        }
    }

    return SetFieldRaw(instance, field, &value);
}

// Writes a typed instance field by name.
template <typename T>
bool SetField(
    Il2CppObject* instance,
    const char* ns,
    const char* className,
    const char* fieldName,
    const T& value
) {
    return SetField(instance, GetFieldInfoFromName(ns, className, fieldName), value);
}

// Writes a typed static field by metadata.
template <typename T>
bool SetStaticField(FieldInfo* field, const T& value) {
    return SetStaticFieldRaw(field, &value);
}

// Writes a typed static field by name.
template <typename T>
bool SetStaticField(
    const char* ns,
    const char* className,
    const char* fieldName,
    const T& value
) {
    return SetStaticFieldRaw(ns, className, fieldName, &value);
}

// Finds all matching IL2CPP method metadata entries by class, method, and params.
std::vector<MethodInfo*> GetAllMethodInfosFromName(
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    std::vector<MethodInfo*> foundMethods;

    if (!className || !methodName || !HasIl2CppMethodScanApi()) {
        return foundMethods;
    }

    std::string cacheKey = GenerateCacheKey(ns, className, methodName, paramTypes);

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        auto cached = MultiMethodCache.find(cacheKey);
        if (cached != MultiMethodCache.end()) {
            if (!cached->second.empty()) {
                return cached->second;
            }

            auto miss = MethodMissCache.find(cacheKey);
            if (miss != MethodMissCache.end() &&
                std::chrono::steady_clock::now() - miss->second <
                    std::chrono::milliseconds(RuntimeConfig::MissingMethodRetryMs)) {
                return foundMethods;
            }
        }
    }

    size_t size = 0;
    Il2CppDomain* domain = il2cpp_domain_get();

    if (!domain) {
        return foundMethods;
    }

    const Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) {
        return foundMethods;
    }

    for (size_t i = 0; i < size; ++i) {
        const Il2CppImage* image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) {
            continue;
        }

        Il2CppClass* klass = ResolveClassFromName(image, ns, className);
        if (!klass) {
            continue;
        }

        Il2CppClass* currentKlass = klass;

        while (currentKlass) {
            void* iter = nullptr;
            MethodInfo* method = nullptr;

            while ((method = (MethodInfo*)il2cpp_class_get_methods(currentKlass, &iter))) {
                const char* currentMethodName = il2cpp_method_get_name(method);

                if (!currentMethodName || strcmp(currentMethodName, methodName) != 0) {
                    continue;
                }

                uint32_t paramCount = il2cpp_method_get_param_count(method);
                if (paramCount != paramTypes.size()) {
                    continue;
                }

                bool paramsMatch = true;

                for (uint32_t p = 0; p < paramCount; ++p) {
                    const Il2CppType* paramType = il2cpp_method_get_param(method, p);
                    Il2CppClass* paramClass = il2cpp_class_from_type(paramType);
                    const char* paramName = paramClass ? il2cpp_class_get_name(paramClass) : "";

                    if (!StringIncludesCaseInsensitive(paramName, paramTypes[p])) {
                        paramsMatch = false;
                        break;
                    }
                }

                if (paramsMatch) {
                    foundMethods.push_back(method);
                    break;
                }
            }

            if (!il2cpp_class_get_parent) {
                break;
            }

            currentKlass = il2cpp_class_get_parent(currentKlass);
        }

        if (!foundMethods.empty()) {
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        MultiMethodCache[cacheKey] = foundMethods;
        if (foundMethods.empty()) {
            MethodMissCache[cacheKey] = std::chrono::steady_clock::now();
        } else {
            MethodMissCache.erase(cacheKey);
        }
    }
    return foundMethods;
}

// Converts resolved IL2CPP method metadata entries into callable pointers.
std::vector<void*> GetAllMethodsFromName(
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    std::vector<MethodInfo*> infos =
        GetAllMethodInfosFromName(ns, className, methodName, paramTypes);

    std::vector<void*> pointers;

    for (MethodInfo* info : infos) {
        if (info && info->methodPointer) {
            pointers.push_back((void*)info->methodPointer);
        }
    }

    return pointers;
}

// Returns the first resolved method pointer for call-only bindings and hooks.
void* GetFirstMethodFromName(
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    std::vector<void*> methods = GetAllMethodsFromName(ns, className, methodName, paramTypes);
    return methods.empty() ? nullptr : methods[0];
}

// Resolves a managed method once and stores its callable pointer for later use.
template <typename T>
bool ResolveOriginal(
    T& target,
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        if (target) {
            return true;
        }
    }

    void* method = GetFirstMethodFromName(ns, className, methodName, paramTypes);

    std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
    if (method && !target) {
        target = reinterpret_cast<T>(method);
    }

    return target != nullptr;
}

// Resolves a managed method and installs a hook while preserving the original pointer.
template <typename T>
bool HookResolvedMethod(
    T& original,
    void* replacement,
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        if (original) {
            return true;
        }
    }

    void* method = GetFirstMethodFromName(ns, className, methodName, paramTypes);

    if (!method) {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        return original != nullptr;
    }

    T hookedOriginal = nullptr;

    if (DobbyHook(
            method,
            replacement,
            reinterpret_cast<void**>(&hookedOriginal)
        ) == RT_SUCCESS) {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        if (!original) {
            original = hookedOriginal;
        }
    }

    return original != nullptr;
}

// Advances a timestamp only when the requested interval has actually passed.
bool IntervalElapsed(
    std::chrono::steady_clock::time_point& lastRun,
    int intervalMs,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
) {
    if (lastRun.time_since_epoch().count() == 0 ||
        now - lastRun >= std::chrono::milliseconds(intervalMs)) {
        lastRun = now;
        return true;
    }

    return false;
}

// Checks a cooldown without mutating its timestamp.
bool CooldownElapsed(
    const std::chrono::steady_clock::time_point& lastRun,
    int intervalMs,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
) {
    return lastRun.time_since_epoch().count() == 0 ||
        now - lastRun >= std::chrono::milliseconds(intervalMs);
}

// Checks whether this render frame has spent its elapsed-time work budget.
bool FrameBudgetExceeded(
    const std::chrono::steady_clock::time_point& frameStart,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
) {
    return now - frameStart >= std::chrono::milliseconds(RuntimeConfig::FeatureFrameBudgetMs);
}

// Resets the render-thread managed-work budget for one overlay frame.
void BeginManagedWorkBudget(int unitLimit = RuntimeConfig::FeatureManagedWorkBudgetUnits) {
    RuntimeBudget::ManagedWorkUnits = 0;
    RuntimeBudget::ManagedWorkUnitLimit = std::max(unitLimit, 0);
}

// Accounts for bounded IL2CPP or game calls before a hot path continues.
bool TryConsumeManagedWorkUnits(int units = 1) {
    units = std::max(units, 0);
    if (units == 0 || RuntimeBudget::ManagedWorkUnitLimit <= 0) {
        return true;
    }

    if (RuntimeBudget::ManagedWorkUnits + units >
        RuntimeBudget::ManagedWorkUnitLimit) {
        RuntimeBudget::ManagedWorkUnits = RuntimeBudget::ManagedWorkUnitLimit;
        return false;
    }

    RuntimeBudget::ManagedWorkUnits += units;
    return true;
}

// Reports whether this frame already spent its managed-call budget.
bool ManagedWorkBudgetExceeded() {
    return RuntimeBudget::ManagedWorkUnitLimit > 0 &&
        RuntimeBudget::ManagedWorkUnits >= RuntimeBudget::ManagedWorkUnitLimit;
}

// Checks both elapsed frame time and bounded managed-call volume.
bool FeatureWorkBudgetExceeded(
    const std::chrono::steady_clock::time_point& frameStart
) {
    return FrameBudgetExceeded(frameStart) || ManagedWorkBudgetExceeded();
}

// Confirms the IL2CPP domain and thread APIs are available before managed calls.
bool HasIl2CppDomainApi() {
    return il2cpp_domain_get && il2cpp_thread_attach;
}

// Confirms the IL2CPP assembly APIs are available before metadata scans.
bool HasIl2CppAssemblyScanApi() {
    return il2cpp_domain_get &&
        il2cpp_domain_get_assemblies &&
        il2cpp_assembly_get_image &&
        il2cpp_class_from_name;
}

// Confirms the IL2CPP method APIs are available before method resolution.
bool HasIl2CppMethodScanApi() {
    return HasIl2CppAssemblyScanApi() &&
        il2cpp_class_get_methods &&
        il2cpp_method_get_name &&
        il2cpp_method_get_param_count &&
        il2cpp_method_get_param &&
        il2cpp_class_from_type &&
        il2cpp_class_get_name;
}

// Confirms the IL2CPP field APIs are available before field resolution.
bool HasIl2CppFieldScanApi() {
    return HasIl2CppAssemblyScanApi() &&
        (il2cpp_class_get_field_from_name ||
         (il2cpp_class_get_fields && il2cpp_field_get_name));
}

// Confirms setup has resolved IL2CPP and the runtime has a live domain.
bool IsIl2CppRuntimeReady() {
    if (!RuntimeState::Il2CppReady || !HasIl2CppDomainApi()) {
        return false;
    }

    return il2cpp_domain_get() != nullptr;
}

// Checks whether pinned managed-object handles can be created and released safely.
bool HasIl2CppGcHandleApi() {
    return IsIl2CppRuntimeReady() &&
        il2cpp_gchandle_new &&
        il2cpp_gchandle_free &&
        il2cpp_gchandle_get_target;
}

// Pins a managed object for the active match and keeps the handle until match end.
uint32_t PinManagedObjectForMatch(void* object) {
    if (!object || !HasIl2CppGcHandleApi()) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(RuntimeMutex::ManagedHandleMutex);
    auto found = FeatureState::ManagedObjectHandles.find(object);
    if (found != FeatureState::ManagedObjectHandles.end()) {
        return found->second;
    }

    uint32_t handle =
        il2cpp_gchandle_new(reinterpret_cast<Il2CppObject*>(object), true);
    if (handle == 0) {
        return 0;
    }

    FeatureState::ManagedObjectHandles[object] = handle;
    FeatureState::MatchManagedHandles.push_back(handle);
    return handle;
}

// Publishes a managed object cache only after the object has a pinned handle.
void PublishPinnedManagedReference(
    std::atomic<void*>& objectSlot,
    std::atomic<uint32_t>& handleSlot,
    void* object
) {
    uint32_t handle = PinManagedObjectForMatch(object);
    if (handle == 0) {
        objectSlot = nullptr;
        handleSlot = 0;
        return;
    }

    objectSlot = object;
    handleSlot = handle;
}

// Releases every pinned managed-object handle when the match has ended.
void ClearManagedObjectHandlesAfterMatch() {
    std::vector<uint32_t> handles;

    FeatureState::BattleBridge = nullptr;
    FeatureState::HeroShopPanel = nullptr;
    FeatureState::HeroShopItemList = nullptr;
    FeatureState::LoadResInstance = nullptr;
    FeatureState::BattleBridgeHandle = 0;
    FeatureState::HeroShopPanelHandle = 0;
    FeatureState::HeroShopItemListHandle = 0;
    FeatureState::LoadResInstanceHandle = 0;

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::ManagedHandleMutex);
        handles.swap(FeatureState::MatchManagedHandles);
        FeatureState::ManagedObjectHandles.clear();
    }

    if (!il2cpp_gchandle_free) {
        return;
    }

    for (uint32_t handle : handles) {
        if (handle != 0) {
            il2cpp_gchandle_free(handle);
        }
    }
}

// Checks whether a managed array pointer and length are safe for bounded native reads.
template <typename T>
bool IsManagedArrayValid(
    const MonoStructures::Array<T>* array,
    int maxItems = RuntimeConfig::MaxManagedListItems
) {
    if (!array || maxItems < 0) {
        return false;
    }

    il2cpp_array_size_t capacity = array->GetCapacity();
    return capacity <= static_cast<il2cpp_array_size_t>(maxItems);
}

// Returns validated managed-list data and size for safe native iteration.
template <typename T>
bool TryGetManagedListData(
    const MonoStructures::List<T>* list,
    const T** outData,
    int* outSize,
    int maxItems = RuntimeConfig::MaxManagedListItems
) {
    if (outData) {
        *outData = nullptr;
    }

    if (outSize) {
        *outSize = 0;
    }

    if (!list || !list->items || list->size < 0 || maxItems < 0) {
        return false;
    }

    if (!IsManagedArrayValid(list->items, maxItems)) {
        return false;
    }

    int capacity = list->items->getCapacity();
    if (list->size > capacity || list->size > maxItems) {
        return false;
    }

    const T* data = list->GetData();
    if (!data && list->size > 0) {
        return false;
    }

    if (outData) {
        *outData = data;
    }

    if (outSize) {
        *outSize = list->size;
    }

    return true;
}

// Returns validated managed-array data and size for safe native iteration.
template <typename T>
bool TryGetManagedArrayData(
    const MonoStructures::Array<T>* array,
    const T** outData,
    int* outSize,
    int maxItems = RuntimeConfig::MaxManagedListItems
) {
    if (outData) {
        *outData = nullptr;
    }

    if (outSize) {
        *outSize = 0;
    }

    if (!array || maxItems < 0 || !IsManagedArrayValid(array, maxItems)) {
        return false;
    }

    int capacity = array->getCapacity();
    if (capacity < 0 || capacity > maxItems) {
        return false;
    }

    const T* data = array->GetData();
    if (!data && capacity > 0) {
        return false;
    }

    if (outData) {
        *outData = data;
    }

    if (outSize) {
        *outSize = capacity;
    }

    return true;
}

// Returns validated dictionary entry storage and a bounded scan limit.
template <typename TKey, typename TValue>
bool TryGetDictionaryEntries(
    const MonoStructures::Dictionary<TKey, TValue>* dictionary,
    const typename MonoStructures::Dictionary<TKey, TValue>::Entry** outEntries,
    int* outLimit,
    int maxEntries = RuntimeConfig::MaxManagedDictionaryEntries
) {
    if (outEntries) {
        *outEntries = nullptr;
    }

    if (outLimit) {
        *outLimit = 0;
    }

    if (!dictionary || !dictionary->entries || maxEntries < 0) {
        return false;
    }

    if (dictionary->count < 0 ||
        dictionary->freeCount < 0 ||
        dictionary->freeCount > dictionary->count) {
        return false;
    }

    if (!IsManagedArrayValid(dictionary->entries, maxEntries)) {
        return false;
    }

    int capacity = dictionary->entries->getCapacity();
    if (capacity < 0 ||
        capacity > maxEntries ||
        dictionary->count > capacity) {
        return false;
    }

    const auto* entries = dictionary->entries->GetData();
    if (!entries && dictionary->count > 0) {
        return false;
    }

    if (outEntries) {
        *outEntries = entries;
    }

    if (outLimit) {
        *outLimit = std::min(dictionary->count, capacity);
    }

    return true;
}

// Copies active managed dictionary entries into a native vector for lock-free use.
template <typename TKey, typename TValue>
std::vector<std::pair<TKey, TValue>> CopyDictionaryEntries(
    const MonoStructures::Dictionary<TKey, TValue>* dictionary,
    int maxEntries = RuntimeConfig::MaxManagedDictionaryEntries
) {
    std::vector<std::pair<TKey, TValue>> output;
    const typename MonoStructures::Dictionary<TKey, TValue>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(dictionary, &entries, &entryLimit, maxEntries)) {
        return output;
    }

    output.reserve(static_cast<size_t>(std::max(entryLimit, 0)));

    for (int i = 0; entries && i < entryLimit; ++i) {
        const auto& entry = entries[i];

        if (entry.hashCode >= 0) {
            output.emplace_back(entry.key, entry.value);
        }
    }

    return output;
}

namespace Hooks {
    void MCBattleBridge_OnRefreshShop(
        void* instance,
        bool isAutoRefresh,
        void* dictHeroSlot,
        void* sameRefreshHero,
        bool isDelayOpenShop,
        bool onlyRefreshHero
    );
    void MCShowSpectatorComp_SetSpectate(void* instance, uint64_t accountId);
    bool MCLogicBattleData_ILOGIC_IsCurrFreeBuy(
        void* instance,
        uint64_t accountId,
        int slot,
        bool* needFx
    );
    int MCLogicBattleData_ILOGIC_GetRefreshCost(void* instance, uint64_t accountId);
    bool MCLogicBattleData_ILOGIC_IsRefreshFree(void* instance, uint64_t accountId);
    int MCLogicBattleData_ILogic_HeroCountInPool(void* instance, int heroId);
    bool MCLogicBattleData_ILOGIC_GetBattleResultHistory(
        void* instance,
        uint64_t accountId,
        int round
    );
    bool MCLogicBattleData_ILOGIC_CanUpgrade(
        void* instance,
        uint64_t accountId,
        int coin
    );
    bool MCLogicBattleData_ILOGIC_GetShopIsForbid(void* instance, uint64_t accountId);
    int MCLogicBattleData_ILOGIC_GetUpgradeCost(void* instance, uint64_t accountId);
    bool MCLogicBattleManager_get_m_bDefendFaild(void* instance);
    void MCLogicBattleManager_OnModifyPlayerBlood(
        void* instance,
        int reduceHp,
        bool isFromCurse,
        int effectId
    );
    void MCLogicBattleManager_OnFightOver(
        void* instance,
        bool failed,
        bool includeInvader,
        bool doubleFailed
    );
    bool MCBondUtil_CheckRelationActive_Config(
        void* config,
        int curActiveCount,
        void* curBondDict
    );
    bool MCBondUtil_CheckRelationActive_Special(
        void* specialCondition,
        int needCount,
        int curActiveCount,
        void* curBondDict
    );
    bool MCLogicAchievementRecordComp_AchievementDataBase_GetResult(void* instance);
    bool MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData(
        void* instance
    );
    bool MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation(
        void* instance
    );
    bool MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition(
        void* instance,
        void* players
    );
    bool MCLogicAchievementRecordComp_AchievementRoundData_GetResult(void* instance);
    void MCLogicAchievementRecordComp_AchievementRoundData_RefreshData(void* instance);
    AstarInt2 ShowBattleTouchMgr_ClampGridPos(void* instance, AstarInt2 gridPos);
    bool AStarTileMap_ValidPos(int x, int y);
    bool MCLogicEntityMap_CanWalkable(void* instance, int x, int y);
    bool MCLogicEntityMap_IsWalkableAround(void* instance, int x, int y);
}

// Resolves feature bindings. Missing entries are retried by the frame tick.
void ResolveFeatureBindings() {
    if (!IsIl2CppRuntimeReady() || !HasIl2CppMethodScanApi()) {
        return;
    }

    bool expected = false;
    if (!RuntimeState::BindingResolveInProgress.compare_exchange_strong(expected, true)) {
        return;
    }

    struct BindingResolveGuard {
        std::atomic<bool>& flag;
        // Releases the single-flight binding-resolution flag when this pass exits.
        ~BindingResolveGuard() {
            flag.store(false);
        }
    } bindingResolveGuard{RuntimeState::BindingResolveInProgress};

    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetSelfChessPlayerName",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetAllBattleMgr",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetCurrentOpponentAccountID",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetCrystalQualityByRound,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetCrystalQualityByRound",
        {"UInt64", "Int32"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetGameRound,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetGameRound",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetGamePhase,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetGamePhase",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetRoundRemainTime",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetRoundMaxRemainTime,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetRoundMaxRemainTime",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsFightSection,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsFightSection",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsFightResultSection,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsFightResultSection",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsSelfFightOver",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetIsMonsterRound,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetIsMonsterRound",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsRealPlayerMode,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsRealPlayerMode",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetPlayerCoin",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetPlayerHP",
        {"UInt64"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory,
        (void*)Hooks::MCLogicBattleData_ILOGIC_GetBattleResultHistory,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetBattleResultHistory",
        {"UInt64", "Int32"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetPlayerData,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetPlayerData",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetStPlayerData,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetStPlayerData",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetShopItemData,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetShopItemData",
        {"UInt64", "Int32"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy,
        (void*)Hooks::MCLogicBattleData_ILOGIC_IsCurrFreeBuy,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsCurrFreeBuy",
        {"UInt64", "Int32", "Boolean"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILOGIC_GetRefreshCost,
        (void*)Hooks::MCLogicBattleData_ILOGIC_GetRefreshCost,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetRefreshCost",
        {"UInt64"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILOGIC_IsRefreshFree,
        (void*)Hooks::MCLogicBattleData_ILOGIC_IsRefreshFree,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsRefreshFree",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILogic_HeroOwnCount,
        "",
        "MCLogicBattleData",
        "ILogic_HeroOwnCount",
        {"UInt64", "Int32"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILogic_HeroCountInPool,
        (void*)Hooks::MCLogicBattleData_ILogic_HeroCountInPool,
        "",
        "MCLogicBattleData",
        "ILogic_HeroCountInPool",
        {"Int32"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetHeroByRecommendLineup",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetBuidByAccID,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetBuidByAccID",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetGuidByAccID,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetGuidByAccID",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetChessPlayerGuid,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetChessPlayerGuid",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetChessPlayerConfigID,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetChessPlayerConfigID",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetChessSkinId,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetChessSkinId",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetRank,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetRank",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILGOIC_GetCampAliveCountByAccId,
        "",
        "MCLogicBattleData",
        "ILGOIC_GetCampAliveCountByAccId",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetCampRankByAccId,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetCampRankByAccId",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetCup,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetCup",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetWarmValue,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetWarmValue",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetRankLevel,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetRankLevel",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetCommanderLv,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetCommanderLv",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetPlayerLevel,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetPlayerLevel",
        {"UInt64"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILOGIC_GetUpgradeCost,
        (void*)Hooks::MCLogicBattleData_ILOGIC_GetUpgradeCost,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetUpgradeCost",
        {"UInt64"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILOGIC_CanUpgrade,
        (void*)Hooks::MCLogicBattleData_ILOGIC_CanUpgrade,
        "",
        "MCLogicBattleData",
        "ILOGIC_CanUpgrade",
        {"UInt64", "Int32"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleData_ILOGIC_GetShopIsForbid,
        (void*)Hooks::MCLogicBattleData_ILOGIC_GetShopIsForbid,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetShopIsForbid",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetShopStarLv,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetShopStarLv",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetShopRuleBuyTimes,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetShopRuleBuyTimes",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_GetFreeFreshShopCount,
        "",
        "MCLogicBattleData",
        "GetFreeFreshShopCount",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetCurRefreshShopLevel,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetCurRefreshShopLevel",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetHeroItemCount,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetHeroItemCount",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetHeroSlotDict_Count,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetHeroSlotDict_Count",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetBattleHeroNum,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetBattleHeroNum",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetAllHeroNum,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetAllHeroNum",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetBattleHeroTotalStart,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetBattleHeroTotalStart",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetBattleCount,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetBattleCount",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsCurrentLogic,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsCurrentLogic",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsPlayerCanPauseJudger,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsPlayerCanPauseJudger",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetSelfCamp,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetSelfCamp",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_SelfTotalPopulation,
        "",
        "MCLogicBattleData",
        "ILOGIC_SelfTotalPopulation",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_SelfCurPopulation,
        "",
        "MCLogicBattleData",
        "ILOGIC_SelfCurPopulation",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetSpareChessNum,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetSpareChessNum",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetHeroByStarUp,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetHeroByStarUp",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_get_logicRoundMgr,
        "",
        "MCLogicBattleData",
        "get_logicRoundMgr",
        {}
    );
    ResolveOriginal(
        Originals::LogicRoundMgr_SetRound,
        "",
        "LogicRoundMgr",
        "SetRound",
        {"UInt32"}
    );
    ResolveOriginal(
        Originals::LogicRoundMgr_NextRound,
        "",
        "LogicRoundMgr",
        "NextRound",
        {"Boolean"}
    );
    ResolveOriginal(
        Originals::UnityEngine_Time_set_timeScale,
        "UnityEngine",
        "Time",
        "set_timeScale",
        {"Single"}
    );
    ResolveOriginal(Originals::MCComp_GetGamer, "", "MCComp", "GetGamer", {"UInt64"});
    ResolveOriginal(
        Originals::MCComp_GetGoGoCardComp,
        "",
        "MCComp",
        "GetGoGoCardComp",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::CData_MCHero_GetInstance,
        "",
        "CData_MCHero",
        "GetInstance",
        {}
    );
    ResolveOriginal(Originals::CData_MCHero_GetAll, "", "CData_MCHero", "GetAll", {});
    ResolveOriginal(
        Originals::CData_MCEquipBase_GetInstance,
        "",
        "CData_MCEquipBase",
        "GetInstance",
        {}
    );
    ResolveOriginal(
        Originals::CData_MCEquipBase_GetAll,
        "",
        "CData_MCEquipBase",
        "GetAll",
        {}
    );
    ResolveOriginal(
        Originals::CData_MCSuperCrystalKey_GetInstance,
        "",
        "CData_MCSuperCrystalKey",
        "GetInstance",
        {}
    );
    ResolveOriginal(
        Originals::CData_MCSuperCrystalKey_GetAll,
        "",
        "CData_MCSuperCrystalKey",
        "GetAll",
        {}
    );
    ResolveOriginal(
        Originals::CData_RelationSkillTip_MC_GetInstance,
        "",
        "CData_RelationSkillTip_MC",
        "GetInstance",
        {}
    );
    ResolveOriginal(
        Originals::CData_RelationSkillTip_MC_GetAll,
        "",
        "CData_RelationSkillTip_MC",
        "GetAll",
        {}
    );
    ResolveOriginal(Originals::ShowMsgTool_GetDesc, "", "ShowMsgTool", "GetDesc", {"Int32"});
    ResolveOriginal(Originals::LoadRes_IsCommander, "", "LoadRes", "IsCommander", {"Int32"});
    ResolveOriginal(
        Originals::MCLogicBattleManager_BuyNormalHero,
        "",
        "MCLogicBattleManager",
        "BuyNormalHero",
        {"MCLogicHeroShopItemData", "Boolean"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleManager_get_m_bDefendFaild,
        (void*)Hooks::MCLogicBattleManager_get_m_bDefendFaild,
        "",
        "MCLogicBattleManager",
        "get_m_bDefendFaild",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_get_IsHost,
        "",
        "MCLogicBattleManager",
        "get_IsHost",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_get_m_uAccountId,
        "",
        "MCLogicBattleManager",
        "get_m_uAccountId",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_GetCurrentOpponent,
        "",
        "MCLogicBattleManager",
        "GetCurrentOpponent",
        {}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleManager_OnModifyPlayerBlood,
        (void*)Hooks::MCLogicBattleManager_OnModifyPlayerBlood,
        "",
        "MCLogicBattleManager",
        "OnModifyPlayerBlood",
        {"Int32", "Boolean", "Int32"}
    );
    HookResolvedMethod(
        Originals::MCLogicBattleManager_OnFightOver,
        (void*)Hooks::MCLogicBattleManager_OnFightOver,
        "",
        "MCLogicBattleManager",
        "OnFightOver",
        {"Boolean", "Boolean", "Boolean"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_HasAliveFighter,
        "",
        "MCLogicBattleManager",
        "HasAliveFighter",
        {"MCEntityCampType"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_GetAliveFighter,
        "",
        "MCLogicBattleManager",
        "GetAliveFighter",
        {"Int32", "Int32"}
    );
    ResolveOriginal(
        Originals::MCBehaviorThreeApi_Get,
        "",
        "MCBehaviorThreeApi",
        "Get",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCBehaviorThreeApi_GetCurrentBattleRoundResult,
        "",
        "MCBehaviorThreeApi",
        "GetCurrentBattleRoundResult",
        {}
    );
    ResolveOriginal(
        Originals::MCBehaviorThreeApi_GetCurrentPhaseType,
        "",
        "MCBehaviorThreeApi",
        "GetCurrentPhaseType",
        {}
    );
    ResolveOriginal(
        Originals::LogicInvasionMgr_GetCurPairDict,
        "",
        "LogicInvasionMgr",
        "GetCurPairDict",
        {}
    );
    ResolveOriginal(
        Originals::LogicInvasionMgr_GetCurPair,
        "",
        "LogicInvasionMgr",
        "GetCurPair",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::LogicInvasionMgr_IsRealPlayerMode,
        "",
        "LogicInvasionMgr",
        "IsRealPlayerMode",
        {}
    );
    ResolveOriginal(
        Originals::LogicInvasionMgr_IsMonsterRound,
        "",
        "LogicInvasionMgr",
        "IsMonsterRound",
        {"UInt32"}
    );
    ResolveOriginal(
        Originals::MCEquipUtil_OnGetNewEquip,
        "Battle",
        "MCEquipUtil",
        "OnGetNewEquip",
        {"UInt64", "Int32", "UInt32", "Int32"}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop,
        "",
        "UIPanelBattleHeroShop",
        "KeyBoardRefreshShop",
        {}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_KeyBoardShopSelect,
        "",
        "UIPanelBattleHeroShop",
        "KeyBoardShopSelect",
        {"Int32"}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_BuyHero,
        "",
        "UIPanelBattleHeroShop",
        "BuyHero",
        {"Byte", "Boolean"}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero,
        "",
        "UIPanelBattleHeroShop_HeroItemList",
        "OnSelectHero",
        {"Byte"}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_get_lastOperationTime,
        "",
        "UIPanelBattleHeroShop",
        "get_lastOperationTime",
        {}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_IsDelayOpen,
        "",
        "UIPanelBattleHeroShop",
        "IsDelayOpen",
        {}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_GetInfoAfterSpectate,
        "",
        "UIPanelBattleHeroShop",
        "GetInfoAfterSpectate",
        {}
    );
    ResolveOriginal(
        Originals::UIPanelBattleHeroShop_CanOperate,
        "",
        "UIPanelBattleHeroShop",
        "CanOperate",
        {"Boolean"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_IsHeroInRecommendLineup,
        "",
        "MCBattleBridge",
        "IsHeroInRecommendLineup",
        {"Int32"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_IsSuperCrystalShopOpen,
        "",
        "MCBattleBridge",
        "IsSuperCrystalShopOpen",
        {}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_IsGoGoCardPanelOpen,
        "",
        "MCBattleBridge",
        "IsGoGoCardPanelOpen",
        {}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_CheckEnableKeyBoard,
        "",
        "MCBattleBridge",
        "CheckEnableKeyBoard",
        {}
    );
    HookResolvedMethod(
        Originals::MCBattleBridge_OnRefreshShop,
        (void*)Hooks::MCBattleBridge_OnRefreshShop,
        "",
        "MCBattleBridge",
        "OnRefreshShop",
        {"Boolean", "Dictionary", "List", "Boolean", "Boolean"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_GetFreeMemory,
        "",
        "MCBattleBridge",
        "GetFreeMemory",
        {}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_GetPingTimes,
        "",
        "MCBattleBridge",
        "GetPingTimes",
        {}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_GetStdevPing,
        "",
        "MCBattleBridge",
        "GetStdevPing",
        {}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_GetStdevFps,
        "",
        "MCBattleBridge",
        "GetStdevFps",
        {}
    );
    ResolveOriginal(
        Originals::MCChessPlayerData_UpdateCoin,
        "",
        "MCChessPlayerData",
        "UpdateCoin",
        {"Int32", "CoinChangeType"}
    );

    HookResolvedMethod(
        Originals::MCShowSpectatorComp_SetSpectate,
        (void*)Hooks::MCShowSpectatorComp_SetSpectate,
        "Battle",
        "MCShowSpectatorComp",
        "SetSpectate",
        {"UInt64"}
    );
    HookResolvedMethod(
        Originals::MCBondUtil_CheckRelationActive_Config,
        (void*)Hooks::MCBondUtil_CheckRelationActive_Config,
        "Battle",
        "MCBondUtil",
        "CheckRelationActive",
        {"CData_RelationSkill_MC_Element", "Int32", "Dictionary"}
    );
    ResolveOriginal(
        Originals::MCBondUtil_GetBondActiveCount,
        "Battle",
        "MCBondUtil",
        "GetBondActiveCount",
        {"UInt64", "Int32", "Boolean"}
    );
    HookResolvedMethod(
        Originals::MCBondUtil_CheckRelationActive_Special,
        (void*)Hooks::MCBondUtil_CheckRelationActive_Special,
        "Battle",
        "MCBondUtil",
        "CheckRelationActive",
        {"Int32", "Int32", "Int32", "Dictionary"}
    );
    HookResolvedMethod(
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_GetResult,
        (void*)Hooks::MCLogicAchievementRecordComp_AchievementDataBase_GetResult,
        "Battle",
        "MCLogicAchievementRecordComp.AchievementDataBase",
        "GetResult",
        {}
    );
    HookResolvedMethod(
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData,
        (void*)Hooks::MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData,
        "Battle",
        "MCLogicAchievementRecordComp.AchievementDataBase",
        "canRecordAchievementData",
        {}
    );
    HookResolvedMethod(
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation,
        (void*)Hooks::MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation,
        "Battle",
        "MCLogicAchievementRecordComp.AchievementDataBase",
        "JudgeFinalRelation",
        {}
    );
    HookResolvedMethod(
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition,
        (void*)Hooks::MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition,
        "Battle",
        "MCLogicAchievementRecordComp.AchievementDataBase",
        "JudgeReachCondition",
        {"List"}
    );
    HookResolvedMethod(
        Originals::MCLogicAchievementRecordComp_AchievementRoundData_GetResult,
        (void*)Hooks::MCLogicAchievementRecordComp_AchievementRoundData_GetResult,
        "Battle",
        "MCLogicAchievementRecordComp.AchievementRoundData",
        "GetResult",
        {}
    );
    HookResolvedMethod(
        Originals::MCLogicAchievementRecordComp_AchievementRoundData_RefreshData,
        (void*)Hooks::MCLogicAchievementRecordComp_AchievementRoundData_RefreshData,
        "Battle",
        "MCLogicAchievementRecordComp.AchievementRoundData",
        "RefreshData",
        {}
    );
    HookResolvedMethod(
        Originals::ShowBattleTouchMgr_ClampGridPos,
        (void*)Hooks::ShowBattleTouchMgr_ClampGridPos,
        "",
        "ShowBattleTouchMgr",
        "ClampGridPos",
        {"int2"}
    );
    HookResolvedMethod(
        Originals::AStarTileMap_ValidPos,
        (void*)Hooks::AStarTileMap_ValidPos,
        "",
        "AStarTileMap",
        "ValidPos",
        {"Int32", "Int32"}
    );
    HookResolvedMethod(
        Originals::MCLogicEntityMap_CanWalkable,
        (void*)Hooks::MCLogicEntityMap_CanWalkable,
        "",
        "MCLogicEntityMap",
        "CanWalkable",
        {"Int32", "Int32"}
    );
    HookResolvedMethod(
        Originals::MCLogicEntityMap_IsWalkableAround,
        (void*)Hooks::MCLogicEntityMap_IsWalkableAround,
        "",
        "MCLogicEntityMap",
        "IsWalkableAround",
        {"Int32", "Int32"}
    );
}

// Retries unresolved feature bindings on the shared binding cadence.
void RetryFeatureBindingsIfNeeded() {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    if (RuntimeState::BindingRetryRequested ||
        IntervalElapsed(FeatureState::LastBindingRetry, RuntimeConfig::BindingRetryMs)) {
        RuntimeState::BindingRetryRequested = false;
        ResolveFeatureBindings();
    }
}

// Converts a managed IL2CPP string into a bounded native std::string.
std::string ManagedStringToStd(Il2CppString* value) {
    if (!value) {
        return {};
    }

    auto* managedString = reinterpret_cast<MonoStructures::String*>(value);
    int length = managedString->getLength();

    if (length <= 0 || length > RuntimeConfig::MaxManagedStringChars) {
        return {};
    }

    return managedString->str();
}

// Filters placeholder and non-hero table names from automation lists.
bool IsForbidHeroName(const std::string& name) {
    return name.empty() ||
        name == "Dijiang" ||
        name == "Johnny" ||
        name == "Bot" ||
        name == "Physical ATK" ||
        name == "Magic ATK";
}

// Checks whether a hero table row is a playable shop/arena hero, not a commander or placeholder entity.
bool IsPlayableHeroTableEntry(
    int heroId,
    const std::string& heroName,
    bool isCommander
) {
    return heroId > 0 &&
        heroId <= 10000000 &&
        !isCommander &&
        !IsForbidHeroName(heroName);
}

// Returns the cached or live self account id value used by runtime features.
uint64_t GetSelfAccountId() {
    static FieldInfo* selfAccountIdField = nullptr;

    if (!selfAccountIdField) {
        selfAccountIdField = GetFieldInfoFromName("", "MCLogicBattleData", "m_SelfAccID");
    }

    return GetStaticField<uint64_t>(selfAccountIdField);
}

// Returns the cached or live self logic battle manager value used by runtime features.
void* GetSelfLogicBattleManager() {
    static FieldInfo* selfBattleManagerField = nullptr;

    if (!selfBattleManagerField) {
        selfBattleManagerField =
            GetFieldInfoFromName("", "MCLogicBattleData", "m_SelfLogicBattleManager");
    }

    return GetStaticField<void*>(selfBattleManagerField);
}

// Returns the cached or live battle manager by account id value used by runtime features.
void* GetBattleManagerByAccountId(uint64_t accountId) {
    if (accountId == 0) {
        return nullptr;
    }

    uint64_t selfAccountId = GetSelfAccountId();
    if (selfAccountId != 0 && accountId == selfAccountId) {
        void* selfManager = GetSelfLogicBattleManager();
        if (selfManager) {
            return selfManager;
        }
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        return nullptr;
    }

    if (!TryConsumeManagedWorkUnits()) {
        return nullptr;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit)) {
        return nullptr;
    }

    for (int i = 0; entries && i < entryLimit; ++i) {
        const auto& entry = entries[i];

        if (entry.hashCode < 0 || entry.key != accountId) {
            continue;
        }

        return entry.value;
    }

    return nullptr;
}

// Checks whether an account ID belongs to the local player.
bool IsSelfAccount(uint64_t accountId) {
    uint64_t selfAccountId = GetSelfAccountId();
    return selfAccountId != 0 && accountId == selfAccountId;
}

// Checks whether a battle manager instance belongs to the local player.
bool IsSelfBattleManager(void* battleManager) {
    if (!battleManager) {
        return false;
    }

    if (battleManager == GetSelfLogicBattleManager()) {
        return true;
    }

    if (!Originals::MCLogicBattleManager_get_m_uAccountId) {
        return false;
    }

    return IsSelfAccount(Originals::MCLogicBattleManager_get_m_uAccountId(battleManager));
}

// Checks whether achievement forcing should satisfy managed achievement gates.
bool ShouldForceCompleteAchievements() {
    return FeatureState::ArenaForceCompleteAchievements.load() &&
        IsIl2CppRuntimeReady() &&
        GetSelfAccountId() != 0;
}

// Checks whether a managed object is the round achievement data subtype.
bool IsAchievementRoundDataObject(void* instance) {
    if (!instance || !il2cpp_object_get_class || !il2cpp_class_get_name) {
        return false;
    }

    Il2CppClass* klass =
        il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(instance));
    const char* className = klass ? il2cpp_class_get_name(klass) : nullptr;
    return className && strcmp(className, "AchievementRoundData") == 0;
}

// Marks round achievement counters complete without touching unrelated data objects.
void ForceAchievementRoundDataComplete(void* instance) {
    if (!IsAchievementRoundDataObject(instance)) {
        return;
    }

    static FieldInfo* roundAchievementCountField = nullptr;
    static FieldInfo* roundSuccessCountField = nullptr;

    if (!roundAchievementCountField) {
        roundAchievementCountField = GetFieldInfoFromName(
            "Battle",
            "MCLogicAchievementRecordComp.AchievementRoundData",
            "m_roundAchievementCount"
        );
    }

    if (!roundSuccessCountField) {
        roundSuccessCountField = GetFieldInfoFromName(
            "Battle",
            "MCLogicAchievementRecordComp.AchievementRoundData",
            "m_roundSuccessCount"
        );
    }

    int targetCount = std::max(
        GetField<int>(
            reinterpret_cast<Il2CppObject*>(instance),
            roundAchievementCountField
        ),
        1
    );

    SetField<int>(
        reinterpret_cast<Il2CppObject*>(instance),
        roundAchievementCountField,
        targetCount
    );
    SetField<int>(
        reinterpret_cast<Il2CppObject*>(instance),
        roundSuccessCountField,
        targetCount
    );
}

// Parses account id or default from user or config text with a safe fallback.
uint64_t ParseAccountIdOrDefault(const std::string& value, uint64_t fallback) {
    if (value.empty()) {
        return fallback;
    }

    char* end = nullptr;
    unsigned long long parsed = strtoull(value.c_str(), &end, 10);

    if (end == value.c_str()) {
        return fallback;
    }

    return static_cast<uint64_t>(parsed);
}

// Returns the cached or live logic manager from battle manager value used by runtime features.
void* GetLogicManagerFromBattleManager(void* battleManager) {
    static FieldInfo* logicManagerField = nullptr;

    if (!logicManagerField) {
        logicManagerField = GetFieldInfoFromName("", "MCLogicBattleManager", "m_LogicManager");
    }

    return battleManager && logicManagerField ?
        GetField<void*>(reinterpret_cast<Il2CppObject*>(battleManager), logicManagerField) :
        nullptr;
}

// Returns the cached or live logic invasion manager value used by runtime features.
void* GetLogicInvasionManager() {
    void* logicManager = GetLogicManagerFromBattleManager(GetSelfLogicBattleManager());

    if (!logicManager) {
        return nullptr;
    }

    static FieldInfo* invasionManagerField = nullptr;

    if (!invasionManagerField) {
        invasionManagerField =
            GetFieldInfoFromName("", "LogicChessManager", "m_LogicInvasionMgr");
    }

    return invasionManagerField ?
        GetField<void*>(reinterpret_cast<Il2CppObject*>(logicManager), invasionManagerField) :
        nullptr;
}

// Returns the cached or live battle manager account id value used by runtime features.
uint64_t GetBattleManagerAccountId(void* battleManager) {
    if (!battleManager) {
        return 0;
    }

    if (Originals::MCLogicBattleManager_get_m_uAccountId) {
        return Originals::MCLogicBattleManager_get_m_uAccountId(battleManager);
    }

    return 0;
}

// Returns the cached or live mirror origin account id value used by runtime features.
uint64_t GetMirrorOriginAccountId(void* maybeMirrorManager) {
    if (!maybeMirrorManager) {
        return 0;
    }

    void* logicManager = GetLogicManagerFromBattleManager(GetSelfLogicBattleManager());

    if (!logicManager) {
        return 0;
    }

    static FieldInfo* mirrorManagerField = nullptr;
    static FieldInfo* mirrorOriginAccountField = nullptr;

    if (!mirrorManagerField) {
        mirrorManagerField =
            GetFieldInfoFromName("", "LogicChessManager", "m_MirrorBattleManager");
    }

    if (!mirrorOriginAccountField) {
        mirrorOriginAccountField = GetFieldInfoFromName(
            "",
            "MCLogicMirrorBattleManager",
            "<originBattleManagerAccID>k__BackingField"
        );
    }

    void* mirrorManager = mirrorManagerField ?
        GetField<void*>(reinterpret_cast<Il2CppObject*>(logicManager), mirrorManagerField) :
        nullptr;

    if (!mirrorManager || mirrorManager != maybeMirrorManager || !mirrorOriginAccountField) {
        return 0;
    }

    return GetField<uint64_t>(
        reinterpret_cast<Il2CppObject*>(mirrorManager),
        mirrorOriginAccountField
    );
}

// Looks up an account pairing in a managed dictionary without exposing raw slots to callers.
uint64_t LookupPairInDictionary(
    MonoStructures::Dictionary<uint64_t, uint64_t>* pairDict,
    uint64_t accountId
) {
    const MonoStructures::Dictionary<uint64_t, uint64_t>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(pairDict, &entries, &entryLimit)) {
        return 0;
    }

    for (int i = 0; entries && i < entryLimit; ++i) {
        const auto& entry = entries[i];

        if (entry.hashCode >= 0 && entry.key == accountId) {
            return entry.value;
        }
    }

    return 0;
}

// Returns the cached or live current pair from invasion value used by runtime features.
uint64_t GetCurrentPairFromInvasion(void* invasionManager, uint64_t accountId) {
    if (!invasionManager || accountId == 0) {
        return 0;
    }

    if (Originals::LogicInvasionMgr_GetCurPair) {
        if (!TryConsumeManagedWorkUnits()) {
            return 0;
        }

        uint64_t pairId = Originals::LogicInvasionMgr_GetCurPair(
            invasionManager,
            accountId
        );

        if (pairId != 0) {
            return pairId;
        }
    }

    MonoStructures::Dictionary<uint64_t, uint64_t>* pairDict = nullptr;

    if (Originals::LogicInvasionMgr_GetCurPairDict) {
        if (!TryConsumeManagedWorkUnits()) {
            return 0;
        }

        pairDict = Originals::LogicInvasionMgr_GetCurPairDict(invasionManager);
    }

    if (!pairDict) {
        static FieldInfo* pairDictField = nullptr;

        if (!pairDictField) {
            pairDictField =
                GetFieldInfoFromName("", "LogicInvasionMgr", "m_CurPairDict");
        }

        pairDict = pairDictField ?
            GetField<MonoStructures::Dictionary<uint64_t, uint64_t>*>(
                reinterpret_cast<Il2CppObject*>(invasionManager),
                pairDictField
            ) :
            nullptr;
    }

    return LookupPairInDictionary(pairDict, accountId);
}

struct CurrentOpponentLookup {
    uint64_t accountId = 0;
    bool fromCurrentApi = false;
    bool fromInvasionPair = false;
    bool fromManager = false;
    bool mirror = false;
};

// Returns the cached or live current opponent from manager detailed value used by runtime features.
CurrentOpponentLookup GetCurrentOpponentFromManagerDetailed(void* battleManager) {
    CurrentOpponentLookup result{};

    void* currentOpponent = battleManager && Originals::MCLogicBattleManager_GetCurrentOpponent ?
        (TryConsumeManagedWorkUnits() ?
            Originals::MCLogicBattleManager_GetCurrentOpponent(battleManager) :
            nullptr) :
        nullptr;

    uint64_t accountId = GetBattleManagerAccountId(currentOpponent);
    if (accountId != 0) {
        result.accountId = accountId;
        result.fromManager = true;
        return result;
    }

    uint64_t mirrorOriginAccountId = GetMirrorOriginAccountId(currentOpponent);
    if (mirrorOriginAccountId != 0) {
        result.accountId = mirrorOriginAccountId;
        result.fromManager = true;
        result.mirror = true;
    }

    return result;
}

// Returns the cached or live current opponent from manager value used by runtime features.
uint64_t GetCurrentOpponentFromManager(void* battleManager) {
    return GetCurrentOpponentFromManagerDetailed(battleManager).accountId;
}

// Finds the best current-opponent signal for an account, trying exact APIs before fallbacks.
CurrentOpponentLookup GetCurrentOpponentForAccount(
    uint64_t accountId,
    void* battleManager,
    void* invasionManager
) {
    CurrentOpponentLookup result{};

    if (accountId == 0) {
        return result;
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID) {
        if (!TryConsumeManagedWorkUnits()) {
            return result;
        }

        uint64_t currentOpponent =
            Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                nullptr,
                accountId
            );

        if (currentOpponent != 0) {
            result.accountId = currentOpponent;
            result.fromCurrentApi = true;

            CurrentOpponentLookup managerOpponent =
                GetCurrentOpponentFromManagerDetailed(battleManager);
            if (managerOpponent.accountId == currentOpponent && managerOpponent.mirror) {
                result.mirror = true;
            }

            return result;
        }
    }

    uint64_t pairId = GetCurrentPairFromInvasion(invasionManager, accountId);
    if (pairId != 0) {
        result.accountId = pairId;
        result.fromInvasionPair = true;
        return result;
    }

    result = GetCurrentOpponentFromManagerDetailed(battleManager);
    return result;
}

// Returns the cached or live manager pointer account field value used by runtime features.
uint64_t GetManagerPointerAccountField(void* battleManager, FieldInfo* field) {
    if (battleManager && field && !TryConsumeManagedWorkUnits()) {
        return 0;
    }

    void* value = battleManager && field ?
        GetField<void*>(reinterpret_cast<Il2CppObject*>(battleManager), field) :
        nullptr;

    uint64_t accountId = GetBattleManagerAccountId(value);
    if (accountId != 0) {
        return accountId;
    }

    return GetMirrorOriginAccountId(value);
}

// Checks the current monster round condition before work proceeds.
bool IsCurrentMonsterRound(void* invasionManager) {
    if (Originals::MCLogicBattleData_ILOGIC_GetIsMonsterRound &&
        TryConsumeManagedWorkUnits() &&
        Originals::MCLogicBattleData_ILOGIC_GetIsMonsterRound(nullptr)) {
        return true;
    }

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
            TryConsumeManagedWorkUnits() ?
        Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr) :
        0;

    return invasionManager &&
        round > 0 &&
        Originals::LogicInvasionMgr_IsMonsterRound &&
        TryConsumeManagedWorkUnits() &&
        Originals::LogicInvasionMgr_IsMonsterRound(invasionManager, round);
}

// Checks the real player pairing mode condition before work proceeds.
bool IsRealPlayerPairingMode(void* invasionManager) {
    if (Originals::MCLogicBattleData_ILOGIC_IsRealPlayerMode) {
        return TryConsumeManagedWorkUnits() &&
            Originals::MCLogicBattleData_ILOGIC_IsRealPlayerMode(nullptr);
    }

    if (invasionManager && Originals::LogicInvasionMgr_IsRealPlayerMode) {
        return TryConsumeManagedWorkUnits() &&
            Originals::LogicInvasionMgr_IsRealPlayerMode(invasionManager);
    }

    return true;
}

// Builds a simple round-robin pairing guess when no live opponent data is available.
uint64_t PredictRoundRobinOpponent(
    std::vector<uint64_t> aliveAccounts,
    uint64_t selfAccountId,
    uint32_t round
) {
    if (aliveAccounts.size() < 2 || selfAccountId == 0) {
        return 0;
    }

    std::sort(aliveAccounts.begin(), aliveAccounts.end());

    if (std::find(aliveAccounts.begin(), aliveAccounts.end(), selfAccountId) ==
        aliveAccounts.end()) {
        return 0;
    }

    if (aliveAccounts.size() % 2 == 1) {
        aliveAccounts.push_back(0);
    }

    int playerCount = static_cast<int>(aliveAccounts.size());
    int rounds = std::max(playerCount - 1, 1);
    int rotation = round > 0 ? static_cast<int>((round - 1) % rounds) : 0;

    for (int r = 0; r < rotation; ++r) {
        uint64_t moved = aliveAccounts.back();
        for (int i = playerCount - 1; i > 1; --i) {
            aliveAccounts[i] = aliveAccounts[i - 1];
        }
        aliveAccounts[1] = moved;
    }

    for (int i = 0; i < playerCount / 2; ++i) {
        uint64_t left = aliveAccounts[i];
        uint64_t right = aliveAccounts[playerCount - 1 - i];

        if (left == selfAccountId) {
            return right;
        }

        if (right == selfAccountId) {
            return left;
        }
    }

    return 0;
}

// Reads the dump-backed invader order and filters it to currently alive accounts.
std::vector<uint64_t> GetInvaderAccountOrder(
    void* invasionManager,
    const std::vector<uint64_t>& aliveAccounts
) {
    std::vector<uint64_t> orderedAccounts;

    if (!invasionManager || aliveAccounts.empty()) {
        return orderedAccounts;
    }

    if (il2cpp_object_get_size &&
        il2cpp_object_get_size(reinterpret_cast<Il2CppObject*>(invasionManager)) <= 0x70) {
        return orderedAccounts;
    }

    static FieldInfo* lbmListField = nullptr;
    if (!lbmListField) {
        lbmListField = GetFieldInfoFromName("", "LogicRealPlayerInvader", "lbmList");
    }

    auto* lbmList = GetField<MonoStructures::List<void*>*>(
        reinterpret_cast<Il2CppObject*>(invasionManager),
        lbmListField
    );

    void* const* managers = nullptr;
    int managerCount = 0;

    if (!TryGetManagedListData(lbmList, &managers, &managerCount, 16)) {
        return orderedAccounts;
    }

    orderedAccounts.reserve(static_cast<size_t>(managerCount));

    for (int i = 0; managers && i < managerCount; ++i) {
        uint64_t accountId = GetBattleManagerAccountId(managers[i]);

        if (accountId == 0 ||
            std::find(aliveAccounts.begin(), aliveAccounts.end(), accountId) ==
                aliveAccounts.end() ||
            std::find(orderedAccounts.begin(), orderedAccounts.end(), accountId) !=
                orderedAccounts.end()) {
            continue;
        }

        orderedAccounts.push_back(accountId);
    }

    for (uint64_t accountId : aliveAccounts) {
        if (accountId != 0 &&
            std::find(orderedAccounts.begin(), orderedAccounts.end(), accountId) ==
                orderedAccounts.end()) {
            orderedAccounts.push_back(accountId);
        }
    }

    return orderedAccounts;
}

// Refreshes managed references on its throttled runtime cadence.
void RefreshManagedReferences(bool force = false) {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    if (!force &&
        !IntervalElapsed(
            FeatureState::LastReferenceRefresh,
            RuntimeConfig::ReferenceRefreshMs
        )) {
        return;
    }

    static FieldInfo* battleBridgeField = nullptr;
    static FieldInfo* heroShopPanelField = nullptr;
    static FieldInfo* heroShopItemListField = nullptr;
    static FieldInfo* loadResInstanceField = nullptr;

    if (!battleBridgeField) {
        battleBridgeField = GetFieldInfoFromName("", "MCBattleData", "m_BattleBridge");
    }

    if (!heroShopPanelField) {
        heroShopPanelField =
            GetFieldInfoFromName("", "MCBattleBridge", "uiPanelBattleHeroShop");
    }

    if (!heroShopItemListField) {
        heroShopItemListField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_heroList");
    }

    if (!loadResInstanceField) {
        loadResInstanceField = GetFieldInfoFromName("", "LoadRes", "s_instance");
    }

    if (!IsBattleActive(GetSelfAccountId())) {
        FeatureState::BattleBridge = nullptr;
        FeatureState::LoadResInstance = nullptr;
        FeatureState::HeroShopPanel = nullptr;
        FeatureState::HeroShopItemList = nullptr;
        FeatureState::BattleBridgeHandle = 0;
        FeatureState::LoadResInstanceHandle = 0;
        FeatureState::HeroShopPanelHandle = 0;
        FeatureState::HeroShopItemListHandle = 0;
        return;
    }

    void* battleBridge = GetStaticField<void*>(battleBridgeField);
    void* loadResInstance = GetStaticField<void*>(loadResInstanceField);
    void* heroShopPanel = nullptr;
    void* heroShopItemList = nullptr;

    if (battleBridge) {
        heroShopPanel = GetField<void*>(
            reinterpret_cast<Il2CppObject*>(battleBridge),
            heroShopPanelField
        );
    }

    if (heroShopPanel) {
        heroShopItemList = GetField<void*>(
            reinterpret_cast<Il2CppObject*>(heroShopPanel),
            heroShopItemListField
        );
    }

    PublishPinnedManagedReference(
        FeatureState::BattleBridge,
        FeatureState::BattleBridgeHandle,
        battleBridge
    );
    PublishPinnedManagedReference(
        FeatureState::LoadResInstance,
        FeatureState::LoadResInstanceHandle,
        loadResInstance
    );
    PublishPinnedManagedReference(
        FeatureState::HeroShopPanel,
        FeatureState::HeroShopPanelHandle,
        heroShopPanel
    );
    PublishPinnedManagedReference(
        FeatureState::HeroShopItemList,
        FeatureState::HeroShopItemListHandle,
        heroShopItemList
    );
}

// Returns the cached or live table cache counts value used by runtime features.
TableCacheCounts GetTableCacheCounts() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    return {
        static_cast<int>(FeatureState::Heroes.size()),
        static_cast<int>(FeatureState::Equips.size()),
        static_cast<int>(FeatureState::Cards.size()),
        static_cast<int>(FeatureState::Relations.size())
    };
}

// Attempts the get hero table entry action and reports whether it was safe to run.
bool TryGetHeroTableEntry(int heroId, HeroTableEntry* entry) {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    auto it = FeatureState::Heroes.find(heroId);

    if (it == FeatureState::Heroes.end()) {
        return false;
    }

    if (entry) {
        *entry = it->second;
    }

    return true;
}

// Attempts the get equip table entry action and reports whether it was safe to run.
bool TryGetEquipTableEntry(int equipId, EquipTableEntry* entry) {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    auto it = FeatureState::Equips.find(equipId);

    if (it == FeatureState::Equips.end()) {
        return false;
    }

    if (entry) {
        *entry = it->second;
    }

    return true;
}

// Attempts the get card table entry action and reports whether it was safe to run.
bool TryGetCardTableEntry(int cardId, CardTableEntry* entry) {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    auto it = FeatureState::Cards.find(cardId);

    if (it == FeatureState::Cards.end()) {
        return false;
    }

    if (entry) {
        *entry = it->second;
    }

    return true;
}

// Returns the cached or live shop hero targets snapshot value used by runtime features.
std::unordered_map<int, HeroAutomationState> GetShopHeroTargetsSnapshot() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    return FeatureState::ShopSelectedHeroes;
}

// Returns the cached or live selected shop hero targets snapshot value used by runtime features.
std::vector<std::pair<int, HeroAutomationState>> GetSelectedShopHeroTargetsSnapshot() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    std::vector<std::pair<int, HeroAutomationState>> targets;
    targets.reserve(FeatureState::ShopSelectedHeroes.size());

    for (const auto& item : FeatureState::ShopSelectedHeroes) {
        if (item.second.selected) {
            targets.push_back(item);
        }
    }

    return targets;
}

int ParseConfigInt(const std::string& value, int fallback);

// Clamps a shop automation target count to the supported owned-copy range.
int ClampShopTargetCount(int value) {
    return std::clamp(value, 1, 99);
}

// Returns the configured fallback count for new Recommendation Lineup heroes.
int GetRecommendLineupDefaultTargetCount() {
    int targetCount = ClampShopTargetCount(FeatureState::ShopRecommendTargetCount.load());
    FeatureState::ShopRecommendTargetCount = targetCount;
    return targetCount;
}

// Returns the cached per-hero Recommendation Lineup target count.
int GetRecommendLineupTargetCountForHero(int heroId) {
    if (heroId <= 0 || heroId > 10000000) {
        return GetRecommendLineupDefaultTargetCount();
    }

    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    auto it = FeatureState::ShopRecommendLineupTargetCounts.find(heroId);
    if (it == FeatureState::ShopRecommendLineupTargetCounts.end()) {
        return GetRecommendLineupDefaultTargetCount();
    }

    return ClampShopTargetCount(it->second);
}

// Updates a per-hero Recommendation Lineup target count from UI or policy.
void SetRecommendLineupTargetCount(int heroId, int targetCount) {
    if (heroId <= 0 || heroId > 10000000) {
        return;
    }

    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    FeatureState::ShopRecommendLineupTargetCounts[heroId] =
        ClampShopTargetCount(targetCount);
}

// Returns cached Recommendation Lineup hero ids paired with their target counts.
std::vector<std::pair<int, int>> GetRecommendLineupTargetsSnapshot() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    std::vector<std::pair<int, int>> targets;
    targets.reserve(FeatureState::CachedRecommendLineupHeroIds.size());
    int defaultTarget = GetRecommendLineupDefaultTargetCount();

    for (int heroId : FeatureState::CachedRecommendLineupHeroIds) {
        if (heroId <= 0 || heroId > 10000000) {
            continue;
        }

        auto it = FeatureState::ShopRecommendLineupTargetCounts.find(heroId);
        int targetCount = it != FeatureState::ShopRecommendLineupTargetCounts.end() ?
            ClampShopTargetCount(it->second) :
            defaultTarget;
        targets.emplace_back(heroId, targetCount);
    }

    return targets;
}

// Serializes per-hero Recommendation Lineup target counts for runtime config.
std::string FormatRecommendLineupTargetCounts() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    std::string value;

    for (const auto& item : FeatureState::ShopRecommendLineupTargetCounts) {
        if (item.first <= 0 || item.first > 10000000) {
            continue;
        }

        if (!value.empty()) {
            value += ",";
        }

        value += std::to_string(item.first);
        value += ":";
        value += std::to_string(ClampShopTargetCount(item.second));
    }

    return value;
}

// Loads per-hero Recommendation Lineup target counts from runtime config.
void LoadRecommendLineupTargetCounts(const std::string& value) {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    FeatureState::ShopRecommendLineupTargetCounts.clear();

    size_t cursor = 0;
    while (cursor < value.size()) {
        size_t comma = value.find(',', cursor);
        std::string token = value.substr(
            cursor,
            comma == std::string::npos ? std::string::npos : comma - cursor
        );
        size_t separator = token.find(':');

        int heroId = ParseConfigInt(
            separator == std::string::npos ? token : token.substr(0, separator),
            0
        );
        int targetCount = ParseConfigInt(
            separator == std::string::npos ? "9" : token.substr(separator + 1),
            9
        );

        if (heroId > 0 && heroId <= 10000000) {
            FeatureState::ShopRecommendLineupTargetCounts[heroId] =
                ClampShopTargetCount(targetCount);
        }

        if (comma == std::string::npos) {
            break;
        }

        cursor = comma + 1;
    }
}

// Updates shop hero target through the safest available runtime path.
void SetShopHeroTarget(int heroId, const HeroAutomationState& state) {
    if (heroId <= 0 || heroId > 10000000) {
        return;
    }

    HeroAutomationState clampedState = state;
    clampedState.targetCount = ClampShopTargetCount(clampedState.targetCount);

    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    FeatureState::ShopSelectedHeroes[heroId] = clampedState;
}

// Coordinates deselect shop hero targets for the overlay runtime.
void DeselectShopHeroTargets() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);

    for (auto& item : FeatureState::ShopSelectedHeroes) {
        item.second.selected = false;
    }
}

// Clears shop hero targets without touching unrelated feature state.
void ClearShopHeroTargets() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    FeatureState::ShopSelectedHeroes.clear();
}

// Returns the cached or live tracked shop hero target count value used by runtime features.
int GetTrackedShopHeroTargetCount() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    return static_cast<int>(FeatureState::ShopSelectedHeroes.size());
}

// Returns the cached or live sorted heroes value used by runtime features.
std::vector<HeroTableEntry> GetSortedHeroes(bool validOnly) {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    std::vector<HeroTableEntry> heroes;
    heroes.reserve(FeatureState::Heroes.size());

    for (const auto& pair : FeatureState::Heroes) {
        if (validOnly && !pair.second.valid) {
            continue;
        }

        heroes.push_back(pair.second);
    }

    std::sort(
        heroes.begin(),
        heroes.end(),
        [](const HeroTableEntry& left, const HeroTableEntry& right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }

            return left.id < right.id;
        }
    );

    return heroes;
}

// Returns the cached or live sorted equips value used by runtime features.
std::vector<EquipTableEntry> GetSortedEquips() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    std::vector<EquipTableEntry> equips;
    equips.reserve(FeatureState::Equips.size());

    for (const auto& pair : FeatureState::Equips) {
        equips.push_back(pair.second);
    }

    std::sort(
        equips.begin(),
        equips.end(),
        [](const EquipTableEntry& left, const EquipTableEntry& right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }

            return left.id < right.id;
        }
    );

    return equips;
}

// Returns the cached or live sorted cards value used by runtime features.
std::vector<CardTableEntry> GetSortedCards() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    std::vector<CardTableEntry> cards;
    cards.reserve(FeatureState::Cards.size());

    for (const auto& pair : FeatureState::Cards) {
        cards.push_back(pair.second);
    }

    std::sort(
        cards.begin(),
        cards.end(),
        [](const CardTableEntry& left, const CardTableEntry& right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }

            return left.id < right.id;
        }
    );

    return cards;
}

// Clears table data cache unlocked without touching unrelated feature state.
void ClearTableDataCacheUnlocked() {
    FeatureState::TableDataLoaded = false;
    FeatureState::Heroes.clear();
    FeatureState::Equips.clear();
    FeatureState::Cards.clear();
    FeatureState::Relations.clear();
    FeatureState::LastTableLoadAttempt = {};
    FeatureState::LastShopAction = {};
    FeatureState::LastShopBuyAttempt = {};
    FeatureState::LastShopRefreshAttempt = {};
    FeatureState::LastShopWorthCheck = {};
    FeatureState::LastRecommendLineupCheck = {};
    FeatureState::LastScavengerCheck = {};
    FeatureState::LastArenaSkipAttempt = {};
    FeatureState::ArenaLastSkipSourceRound = 0;
    FeatureState::ArenaLastSkipTargetRound = 0;
    FeatureState::CachedGameRound = 0;
    FeatureState::CachedShopHasWorthwhileTarget = false;
    FeatureState::CachedRecommendLineupHeroId = 0;
    FeatureState::CachedScavengerRelationId = 0;
    FeatureState::CachedScavengerActiveCount = -1;
    FeatureState::ShopScavengerAutoRefreshPending = false;
    FeatureState::ShopScavengerProcessing = false;
    FeatureState::LastShopBuyAccountId = 0;
    FeatureState::LastShopBuySlot = -1;
    FeatureState::LastShopBuyHeroId = 0;
    FeatureState::LastShopBuyPrice = 0;
    FeatureState::LastShopBuyOwnCount = -1;
    FeatureState::LastShopBuyWasFree = false;
    FeatureState::CachedRecommendLineupHeroIds.clear();
}

// Clears table data cache without touching unrelated feature state.
void ClearTableDataCache() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    ClearTableDataCacheUnlocked();
}

// Checks the load table data for frame condition before work proceeds.
bool ShouldLoadTableDataForFrame(int activeMainTab) {
    if (FeatureState::TableDataLoaded.load()) {
        return false;
    }

    return activeMainTab == MainTabShop ||
        activeMainTab == MainTabArena ||
        FeatureState::ShopForceScavengerExpensiveHero.load() ||
        FeatureState::ShopBuyRecommendLineup.load() ||
        FeatureState::ShopStopRefreshAtRecommendLineup.load();
}

// Checks the battle active condition before work proceeds.
bool IsBattleActive(uint64_t selfAccountId) {
    if (selfAccountId == 0) {
        return false;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        return true;
    }

    if (!TryConsumeManagedWorkUnits()) {
        return selfAccountId != 0;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit)) {
        return selfAccountId != 0;
    }

    for (int i = 0; entries && i < entryLimit; ++i) {
        if (entries[i].hashCode >= 0 && entries[i].key != 0) {
            return true;
        }
    }

    return false;
}

// Refreshes table data for match on its throttled runtime cadence.
void RefreshTableDataForMatch(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    if (!IntervalElapsed(
            FeatureState::LastMatchStateCheck,
            RuntimeConfig::MatchStateCheckMs
        )) {
        return;
    }

    bool battleActive = IsBattleActive(selfAccountId);
    bool endedMatch = !battleActive && FeatureState::WasInMatch.load();
    if (endedMatch) {
        ClearManagedObjectHandlesAfterMatch();
    }

    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    bool selfChanged =
        selfAccountId != 0 &&
        selfAccountId != FeatureState::LastSelfAccountId;

    if (battleActive && (!FeatureState::WasInMatch || selfChanged)) {
        ClearTableDataCacheUnlocked();
    }

    FeatureState::WasInMatch = battleActive;
    FeatureState::LastSelfAccountId = battleActive ? selfAccountId : 0;
}

// Ensures table data loaded exists before file or UI work continues.
void EnsureTableDataLoaded() {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    if (FeatureState::TableDataLoaded) {
        return;
    }

    if (!IntervalElapsed(
        FeatureState::LastTableLoadAttempt,
        RuntimeConfig::TableRetryMs
    )) {
        return;
    }

    struct ManagedWorkLimitGuard {
        int previousLimit;
        // Restores the frame budget after a bounded all-or-nothing table load.
        ~ManagedWorkLimitGuard() {
            RuntimeBudget::ManagedWorkUnitLimit = previousLimit;
        }
    } tableLoadBudgetGuard{RuntimeBudget::ManagedWorkUnitLimit};
    if (RuntimeBudget::ManagedWorkUnitLimit > 0) {
        RuntimeBudget::ManagedWorkUnitLimit = std::max(
            RuntimeBudget::ManagedWorkUnitLimit,
            RuntimeConfig::TableLoadManagedWorkBudgetUnits
        );
    }

    RefreshManagedReferences(true);

    std::unordered_map<int, HeroTableEntry> localHeroes;
    std::unordered_map<int, EquipTableEntry> localEquips;
    std::unordered_map<int, CardTableEntry> localCards;
    std::unordered_map<int, RelationTableEntry> localRelations;

    if (Originals::CData_MCHero_GetInstance && Originals::CData_MCHero_GetAll) {
        void* heroInstance = Originals::CData_MCHero_GetInstance();
        auto* heroDictionary =
            heroInstance ? Originals::CData_MCHero_GetAll(heroInstance) : nullptr;

        if (heroDictionary) {
            static FieldInfo* idField = nullptr;
            static FieldInfo* nameField = nullptr;
            static FieldInfo* qualityField = nullptr;
            static FieldInfo* tankField = nullptr;
            static FieldInfo* occupationField = nullptr;
            static FieldInfo* attackTypeField = nullptr;
            static FieldInfo* heroTypeField = nullptr;
            static FieldInfo* heroGroupField = nullptr;

            if (!idField) {
                idField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_ID");
            }

            if (!nameField) {
                nameField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_mName");
            }

            if (!qualityField) {
                qualityField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_Quality");
            }

            if (!tankField) {
                tankField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_IsTank");
            }

            if (!occupationField) {
                occupationField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_occupation");
            }

            if (!attackTypeField) {
                attackTypeField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_AttackType");
            }

            if (!heroTypeField) {
                heroTypeField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_HeroType");
            }

            if (!heroGroupField) {
                heroGroupField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_HeroGroup");
            }

            void* loadResInstance = FeatureState::LoadResInstance.load();
            if (!idField ||
                !nameField ||
                !qualityField ||
                !tankField ||
                !occupationField ||
                !attackTypeField ||
                !heroTypeField ||
                !heroGroupField ||
                !Originals::LoadRes_IsCommander ||
                !loadResInstance) {
                return;
            }

            for (const auto& item : CopyDictionaryEntries(heroDictionary)) {
                void* hero = item.second;

                if (!hero) {
                    continue;
                }

                if (!TryConsumeManagedWorkUnits(10)) {
                    return;
                }

                int heroId = GetField<int>(reinterpret_cast<Il2CppObject*>(hero), idField);
                std::string heroName = ManagedStringToStd(
                    GetField<Il2CppString*>(
                        reinterpret_cast<Il2CppObject*>(hero),
                        nameField
                    )
                );
                int quality = GetField<int>(reinterpret_cast<Il2CppObject*>(hero), qualityField);
                int isTank = GetField<int>(reinterpret_cast<Il2CppObject*>(hero), tankField);
                int occupation =
                    GetField<int>(reinterpret_cast<Il2CppObject*>(hero), occupationField);
                int attackType =
                    GetField<int>(reinterpret_cast<Il2CppObject*>(hero), attackTypeField);
                int heroType =
                    GetField<int>(reinterpret_cast<Il2CppObject*>(hero), heroTypeField);
                std::vector<int> groups;
                auto* groupArray = GetField<MonoStructures::Array<int>*>(
                    reinterpret_cast<Il2CppObject*>(hero),
                    heroGroupField
                );
                const int* groupData = nullptr;
                int groupCount = 0;

                if (TryGetManagedArrayData(groupArray, &groupData, &groupCount, 16)) {
                    for (int i = 0; groupData && i < groupCount; ++i) {
                        if (groupData[i] > 0) {
                            groups.push_back(groupData[i]);
                        }
                    }
                }

                bool isCommander = Originals::LoadRes_IsCommander(loadResInstance, heroId);
                if (!IsPlayableHeroTableEntry(
                        heroId,
                        heroName,
                        isCommander
                    )) {
                    continue;
                }

                localHeroes[heroId] = {
                    heroId,
                    heroName,
                    quality,
                    isTank,
                    occupation,
                    attackType,
                    heroType,
                    std::move(groups),
                    true
                };
            }
        }
    }

    if (Originals::CData_MCEquipBase_GetInstance && Originals::CData_MCEquipBase_GetAll) {
        void* equipInstance = Originals::CData_MCEquipBase_GetInstance();
        auto* equipDictionary =
            equipInstance ? Originals::CData_MCEquipBase_GetAll(equipInstance) : nullptr;

        if (equipDictionary) {
            static FieldInfo* idField = nullptr;
            static FieldInfo* nameField = nullptr;

            if (!idField) {
                idField =
                    GetFieldInfoFromName("", "CData_MCEquipBase_Element", "m_EuqipID");
            }

            if (!nameField) {
                nameField =
                    GetFieldInfoFromName("", "CData_MCEquipBase_Element", "m_mItemName");
            }

            for (const auto& outer : CopyDictionaryEntries(equipDictionary)) {
                auto* nestedDictionary = outer.second;

                if (!nestedDictionary) {
                    continue;
                }

                for (const auto& inner : CopyDictionaryEntries(nestedDictionary)) {
                    void* equip = inner.second;

                    if (!equip) {
                        continue;
                    }

                    if (!TryConsumeManagedWorkUnits(2)) {
                        return;
                    }

                    int equipId = GetField<int>(
                        reinterpret_cast<Il2CppObject*>(equip),
                        idField
                    );
                    std::string equipName = ManagedStringToStd(
                        GetField<Il2CppString*>(
                            reinterpret_cast<Il2CppObject*>(equip),
                            nameField
                        )
                    );

                    if (equipId > 0 && !equipName.empty()) {
                        localEquips[equipId] = {equipId, equipName};
                    }
                }
            }
        }
    }

    if (Originals::CData_MCSuperCrystalKey_GetInstance &&
        Originals::CData_MCSuperCrystalKey_GetAll) {
        void* cardInstance = Originals::CData_MCSuperCrystalKey_GetInstance();
        auto* cardDictionary =
            cardInstance ? Originals::CData_MCSuperCrystalKey_GetAll(cardInstance) : nullptr;

        if (cardDictionary) {
            static FieldInfo* idField = nullptr;
            static FieldInfo* skillNameField = nullptr;

            if (!idField) {
                idField =
                    GetFieldInfoFromName("", "CData_MCSuperCrystalKey_Element", "m_ID");
            }

            if (!skillNameField) {
                skillNameField =
                    GetFieldInfoFromName("", "CData_MCSuperCrystalKey_Element", "m_SkillName");
            }

            for (const auto& item : CopyDictionaryEntries(cardDictionary)) {
                void* card = item.second;

                if (!card) {
                    continue;
                }

                if (!TryConsumeManagedWorkUnits(3)) {
                    return;
                }

                int cardId = GetField<int>(reinterpret_cast<Il2CppObject*>(card), idField);
                int skillNameId = GetField<int>(
                    reinterpret_cast<Il2CppObject*>(card),
                    skillNameField
                );

                std::string cardName;

                if (Originals::ShowMsgTool_GetDesc) {
                    cardName = ManagedStringToStd(Originals::ShowMsgTool_GetDesc(skillNameId));
                }

                if (cardName.empty()) {
                    cardName = "Card " + std::to_string(cardId);
                }

                if (cardId > 0) {
                    localCards[cardId] = {cardId, cardName};
                }
            }
        }
    }

    if (Originals::CData_RelationSkillTip_MC_GetInstance &&
        Originals::CData_RelationSkillTip_MC_GetAll) {
        void* relationInstance = Originals::CData_RelationSkillTip_MC_GetInstance();
        auto* relationDictionary =
            relationInstance ? Originals::CData_RelationSkillTip_MC_GetAll(relationInstance) :
                nullptr;

        if (relationDictionary) {
            static FieldInfo* idField = nullptr;
            static FieldInfo* attachField = nullptr;
            static FieldInfo* nameField = nullptr;

            if (!idField) {
                idField =
                    GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_RelationId");
            }

            if (!attachField) {
                attachField = GetFieldInfoFromName(
                    "",
                    "CData_RelationSkillTip_MC_Element",
                    "m_mRelationAttach"
                );
            }

            if (!nameField) {
                nameField =
                    GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_mRelationName");
            }

            for (const auto& item : CopyDictionaryEntries(relationDictionary)) {
                void* relation = item.second;

                if (!relation) {
                    continue;
                }

                if (!TryConsumeManagedWorkUnits(3)) {
                    break;
                }

                int relationId =
                    GetField<int>(reinterpret_cast<Il2CppObject*>(relation), idField);

                if (relationId <= 0) {
                    continue;
                }

                std::string relationName = ManagedStringToStd(
                    GetField<Il2CppString*>(
                        reinterpret_cast<Il2CppObject*>(relation),
                        attachField
                    )
                );

                if (relationName.empty()) {
                    relationName = ManagedStringToStd(
                        GetField<Il2CppString*>(
                            reinterpret_cast<Il2CppObject*>(relation),
                            nameField
                        )
                    );
                }

                if (!relationName.empty()) {
                    localRelations[relationId] = {relationId, relationName};
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
        FeatureState::Heroes = std::move(localHeroes);
        FeatureState::Equips = std::move(localEquips);
        FeatureState::Cards = std::move(localCards);
        FeatureState::Relations = std::move(localRelations);
        FeatureState::TableDataLoaded =
            !FeatureState::Heroes.empty() &&
            !FeatureState::Equips.empty() &&
            !FeatureState::Cards.empty();
    }
}

// Checks the plausible hero id condition before work proceeds.
bool IsPlausibleHeroId(int heroId) {
    return heroId > 0 && heroId <= 10000000;
}

// Checks the known hero id or table pending condition before work proceeds.
bool IsKnownHeroIdOrTablePending(int heroId) {
    if (!IsPlausibleHeroId(heroId)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);

    return !FeatureState::TableDataLoaded.load() ||
        FeatureState::Heroes.empty() ||
        FeatureState::Heroes.find(heroId) != FeatureState::Heroes.end();
}

// Returns the cached or live recommend lineup target count value used by runtime features.
int GetRecommendLineupTargetCount() {
    return GetRecommendLineupDefaultTargetCount();
}

// Adds a hero id to a local Recommendation Lineup cache without duplicates.
void AppendUniqueRecommendHeroId(std::vector<int>& heroIds, int heroId) {
    if (!IsKnownHeroIdOrTablePending(heroId)) {
        return;
    }

    if (std::find(heroIds.begin(), heroIds.end(), heroId) == heroIds.end()) {
        heroIds.push_back(heroId);
    }
}

// Refreshes the cached Recommendation Lineup hero list on its throttled cadence.
int RefreshRecommendLineupHeroCache(std::chrono::steady_clock::time_point now) {
    bool hasSingleHeroBinding = Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup;
    bool hasMembershipBinding =
        FeatureState::BattleBridge.load() &&
        Originals::MCBattleBridge_IsHeroInRecommendLineup;

    if (!hasSingleHeroBinding && !hasMembershipBinding) {
        FeatureState::CachedRecommendLineupHeroId = 0;
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
        FeatureState::CachedRecommendLineupHeroIds.clear();
        return 0;
    }

    if (!IntervalElapsed(
            FeatureState::LastRecommendLineupCheck,
            RuntimeConfig::RecommendLineupCheckMs,
            now
        )) {
        return FeatureState::CachedRecommendLineupHeroId;
    }

    std::vector<int> lineupHeroIds;
    int primaryHeroId = 0;

    if (hasSingleHeroBinding) {
        if (!TryConsumeManagedWorkUnits()) {
            return FeatureState::CachedRecommendLineupHeroId;
        }

        primaryHeroId = Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup(nullptr);
        AppendUniqueRecommendHeroId(lineupHeroIds, primaryHeroId);
    }

    if (hasMembershipBinding && FeatureState::TableDataLoaded.load()) {
        std::vector<HeroTableEntry> heroes = GetSortedHeroes(true);
        void* battleBridge = FeatureState::BattleBridge.load();

        for (const HeroTableEntry& hero : heroes) {
            if (!TryConsumeManagedWorkUnits()) {
                FeatureState::CachedRecommendLineupHeroId =
                    primaryHeroId > 0 ? primaryHeroId : FeatureState::CachedRecommendLineupHeroId.load();
                return FeatureState::CachedRecommendLineupHeroId;
            }

            if (Originals::MCBattleBridge_IsHeroInRecommendLineup(battleBridge, hero.id)) {
                AppendUniqueRecommendHeroId(lineupHeroIds, hero.id);
            }
        }
    }

    if (primaryHeroId <= 0 && !lineupHeroIds.empty()) {
        primaryHeroId = lineupHeroIds.front();
    }

    FeatureState::CachedRecommendLineupHeroId =
        IsKnownHeroIdOrTablePending(primaryHeroId) ? primaryHeroId : 0;

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
        FeatureState::CachedRecommendLineupHeroIds = std::move(lineupHeroIds);
    }

    return FeatureState::CachedRecommendLineupHeroId;
}

// Returns the cached or live recommend lineup hero id value used by runtime features.
int GetRecommendLineupHeroId(std::chrono::steady_clock::time_point now) {
    return RefreshRecommendLineupHeroCache(now);
}

// Checks the recommend lineup hero condition before work proceeds.
bool IsRecommendLineupHero(int heroId, int recommendHeroId) {
    if (!IsPlausibleHeroId(heroId)) {
        return false;
    }

    if (recommendHeroId > 0 && heroId == recommendHeroId) {
        return true;
    }

    return FeatureState::BattleBridge &&
        Originals::MCBattleBridge_IsHeroInRecommendLineup &&
        TryConsumeManagedWorkUnits() &&
        Originals::MCBattleBridge_IsHeroInRecommendLineup(
            FeatureState::BattleBridge,
            heroId
        );
}

// Formats hero label for readable overlay output.
std::string FormatHeroLabel(int heroId) {
    if (!IsPlausibleHeroId(heroId)) {
        return "Waiting";
    }

    HeroTableEntry hero;
    if (TryGetHeroTableEntry(heroId, &hero) && !hero.name.empty()) {
        return hero.name + " (#" + std::to_string(heroId) + ")";
    }

    return "Hero #" + std::to_string(heroId);
}

// Checks the shop panel ready for automation condition before work proceeds.
bool IsShopPanelReadyForAutomation() {
    void* heroShopPanel = FeatureState::HeroShopPanel.load();
    if (!heroShopPanel) {
        return false;
    }

    if (!TryConsumeManagedWorkUnits(4)) {
        return false;
    }

    if (Originals::UIPanelBattleHeroShop_IsDelayOpen &&
        Originals::UIPanelBattleHeroShop_IsDelayOpen(heroShopPanel)) {
        return false;
    }

    if (Originals::UIPanelBattleHeroShop_GetInfoAfterSpectate &&
        Originals::UIPanelBattleHeroShop_GetInfoAfterSpectate(heroShopPanel)) {
        return false;
    }

    if (Originals::UIPanelBattleHeroShop_CanOperate &&
        !Originals::UIPanelBattleHeroShop_CanOperate(heroShopPanel, true)) {
        return false;
    }

    void* battleBridge = FeatureState::BattleBridge.load();
    if (battleBridge &&
        Originals::MCBattleBridge_CheckEnableKeyBoard &&
        !Originals::MCBattleBridge_CheckEnableKeyBoard(battleBridge)) {
        return false;
    }

    return true;
}

// Selects shop slot from the current safe runtime options.
bool SelectShopSlot(int slot) {
    if (slot < 0 || slot >= 5) {
        return false;
    }

    if (!IsShopPanelReadyForAutomation()) {
        return false;
    }

    void* heroShopItemList = FeatureState::HeroShopItemList.load();
    void* heroShopPanel = FeatureState::HeroShopPanel.load();

    if (heroShopItemList &&
        Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero) {
        Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero(
            heroShopItemList,
            static_cast<uint8_t>(slot)
        );
        return true;
    }

    if (heroShopPanel && Originals::UIPanelBattleHeroShop_KeyBoardShopSelect) {
        Originals::UIPanelBattleHeroShop_KeyBoardShopSelect(heroShopPanel, slot);
        return true;
    }

    if (heroShopPanel && Originals::UIPanelBattleHeroShop_BuyHero) {
        Originals::UIPanelBattleHeroShop_BuyHero(
            heroShopPanel,
            static_cast<uint8_t>(slot),
            false
        );
        return true;
    }

    return false;
}

// Checks the valid shop item data condition before work proceeds.
bool IsValidShopItemData(const MCLogicHeroShopItemData& shopData, int slot) {
    if (slot < 0 || slot >= 5) {
        return false;
    }

    if (!IsPlausibleHeroId(shopData.m_iHeroId) ||
        shopData.m_iPrice < 0 ||
        shopData.m_iPrice > 999999 ||
        shopData.m_iStarLv < 0 ||
        shopData.m_iStarLv > 3) {
        return false;
    }

    return true;
}

// Compares a shop buy request with the last request so repeat cooldowns can be enforced.
bool IsSameShopBuyAttempt(
    uint64_t accountId,
    int slot,
    const MCLogicHeroShopItemData& shopData,
    int ownCount,
    bool isFreeBuy
) {
    return FeatureState::LastShopBuyAccountId == accountId &&
        FeatureState::LastShopBuySlot == slot &&
        FeatureState::LastShopBuyHeroId == shopData.m_iHeroId &&
        FeatureState::LastShopBuyPrice == shopData.m_iPrice &&
        FeatureState::LastShopBuyOwnCount == ownCount &&
        FeatureState::LastShopBuyWasFree == isFreeBuy;
}

// Checks whether a shop purchase is outside both global and repeat-buy cooldowns.
bool CanAttemptShopBuy(
    uint64_t accountId,
    int slot,
    const MCLogicHeroShopItemData& shopData,
    int ownCount,
    bool isFreeBuy,
    std::chrono::steady_clock::time_point now
) {
    if (!CooldownElapsed(
            FeatureState::LastShopAction,
            RuntimeConfig::ShopActionCooldownMs,
            now
        )) {
        return false;
    }

    if (IsSameShopBuyAttempt(accountId, slot, shopData, ownCount, isFreeBuy) &&
        !CooldownElapsed(
            FeatureState::LastShopBuyAttempt,
            RuntimeConfig::ShopRepeatBuyCooldownMs,
            now
        )) {
        return false;
    }

    return true;
}

// Records a shop buy attempt so later ticks can throttle repeated requests.
void MarkShopBuyAttempt(
    uint64_t accountId,
    int slot,
    const MCLogicHeroShopItemData& shopData,
    int ownCount,
    bool isFreeBuy,
    std::chrono::steady_clock::time_point now
) {
    FeatureState::LastShopAction = now;
    FeatureState::LastShopBuyAttempt = now;
    FeatureState::LastShopBuyAccountId = accountId;
    FeatureState::LastShopBuySlot = slot;
    FeatureState::LastShopBuyHeroId = shopData.m_iHeroId;
    FeatureState::LastShopBuyPrice = shopData.m_iPrice;
    FeatureState::LastShopBuyOwnCount = ownCount;
    FeatureState::LastShopBuyWasFree = isFreeBuy;
    FeatureState::LastShopWorthCheck = {};
}

// Checks the attempt shop refresh condition before work proceeds.
bool CanAttemptShopRefresh(std::chrono::steady_clock::time_point now) {
    return CooldownElapsed(
            FeatureState::LastShopAction,
            RuntimeConfig::ShopActionCooldownMs,
            now
        ) &&
        CooldownElapsed(
            FeatureState::LastShopRefreshAttempt,
            RuntimeConfig::ShopRefreshCooldownMs,
            now
        );
}

// Coordinates mark shop refresh attempt for the overlay runtime.
void MarkShopRefreshAttempt(std::chrono::steady_clock::time_point now) {
    FeatureState::LastShopAction = now;
    FeatureState::LastShopRefreshAttempt = now;
}

// Scans visible shop slots for free, selected, or recommended heroes worth buying.
bool HasWorthwhileShopTarget(
    uint64_t selfAccountId,
    std::chrono::steady_clock::time_point now
) {
    if (!Originals::MCLogicBattleData_ILogic_HeroOwnCount) {
        FeatureState::CachedShopHasWorthwhileTarget = false;
        FeatureState::LastShopWorthCheck = now;
        return false;
    }

    if (!IntervalElapsed(
            FeatureState::LastShopWorthCheck,
            RuntimeConfig::ShopWorthCheckMs,
            now
        )) {
        return FeatureState::CachedShopHasWorthwhileTarget;
    }

    FeatureState::CachedShopHasWorthwhileTarget = false;

    int checkedTargets = 0;
    auto targetStillNeedsCopies = [&](int heroId, int targetCount) {
        if (!IsKnownHeroIdOrTablePending(heroId) || targetCount <= 0) {
            return false;
        }

        if (++checkedTargets > RuntimeConfig::MaxShopTargetChecks) {
            return false;
        }

        if (!TryConsumeManagedWorkUnits(2)) {
            return false;
        }

        int ownCount = Originals::MCLogicBattleData_ILogic_HeroOwnCount(
            nullptr,
            selfAccountId,
            heroId
        );
        int poolCount = Originals::MCLogicBattleData_ILogic_HeroCountInPool ?
            Originals::MCLogicBattleData_ILogic_HeroCountInPool(nullptr, heroId) :
            1;

        return ownCount >= 0 &&
            ownCount < std::clamp(targetCount, 1, 99) &&
            poolCount > 0;
    };

    for (const auto& item : GetSelectedShopHeroTargetsSnapshot()) {
        int heroId = item.first;
        const HeroAutomationState& state = item.second;

        if (targetStillNeedsCopies(heroId, state.targetCount)) {
            FeatureState::CachedShopHasWorthwhileTarget = true;
            break;
        }
    }

    if (!FeatureState::CachedShopHasWorthwhileTarget &&
        FeatureState::ShopBuyRecommendLineup) {
        GetRecommendLineupHeroId(now);

        for (const auto& item : GetRecommendLineupTargetsSnapshot()) {
            if (targetStillNeedsCopies(item.first, item.second)) {
                FeatureState::CachedShopHasWorthwhileTarget = true;
                break;
            }
        }
    }

    return FeatureState::CachedShopHasWorthwhileTarget;
}

// Checks relation text and dynamic markers for the Scavenger/Shadow Mercenary bond.
bool RelationLooksLikeScavenger(const std::string& text) {
    return StringIncludesCaseInsensitive(text, "scavenger") ||
        StringIncludesCaseInsensitive(text, "shadow mercenary") ||
        (StringIncludesCaseInsensitive(text, "shadow") &&
         StringIncludesCaseInsensitive(text, "mercenary")) ||
        StringIncludesCaseInsensitive(text, "pemulung");
}

// Reads a string field from a relation table row.
std::string ReadRelationStringField(void* relation, FieldInfo* field) {
    if (!relation || !field) {
        return {};
    }

    return ManagedStringToStd(
        GetField<Il2CppString*>(reinterpret_cast<Il2CppObject*>(relation), field)
    );
}

// Checks Scavenger dynamic-desc markers that survive localization.
bool RelationHasScavengerDynamicMarker(void* relation, FieldInfo* dynamicEnumField) {
    if (!relation || !dynamicEnumField) {
        return false;
    }

    auto* dynamicEnums = GetField<MonoStructures::Array<int>*>(
        reinterpret_cast<Il2CppObject*>(relation),
        dynamicEnumField
    );
    const int* values = nullptr;
    int valueCount = 0;

    if (!TryGetManagedArrayData(dynamicEnums, &values, &valueCount, 16)) {
        return false;
    }

    for (int i = 0; values && i < valueCount; ++i) {
        if (values[i] == RuntimeConfig::ScavengerDynamicEnumRefreshShopCost) {
            return true;
        }
    }

    return false;
}

// Checks Scavenger dynamic string params for code-name markers.
bool RelationHasScavengerDynamicParam(void* relation, FieldInfo* dynamicParamField) {
    if (!relation || !dynamicParamField) {
        return false;
    }

    auto* dynamicParams = GetField<MonoStructures::Array<Il2CppString*>*>(
        reinterpret_cast<Il2CppObject*>(relation),
        dynamicParamField
    );
    Il2CppString* const* values = nullptr;
    int valueCount = 0;

    if (!TryGetManagedArrayData(dynamicParams, &values, &valueCount, 16)) {
        return false;
    }

    for (int i = 0; values && i < valueCount; ++i) {
        if (RelationLooksLikeScavenger(ManagedStringToStd(values[i]))) {
            return true;
        }
    }

    return false;
}

// Resolves the localized Scavenger relation id from relation-tip table data.
int ResolveScavengerRelationId() {
    int cachedRelationId = FeatureState::CachedScavengerRelationId.load();
    if (cachedRelationId > 0) {
        return cachedRelationId;
    }

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
        for (const auto& item : FeatureState::Relations) {
            if (RelationLooksLikeScavenger(item.second.name)) {
                FeatureState::CachedScavengerRelationId = item.first;
                return item.first;
            }
        }
    }

    if (!Originals::CData_RelationSkillTip_MC_GetInstance ||
        !Originals::CData_RelationSkillTip_MC_GetAll) {
        return 0;
    }

    void* relationInstance = Originals::CData_RelationSkillTip_MC_GetInstance();
    auto* relationDictionary =
        relationInstance ? Originals::CData_RelationSkillTip_MC_GetAll(relationInstance) :
            nullptr;
    if (!relationDictionary) {
        return 0;
    }

    static FieldInfo* idField = nullptr;
    static FieldInfo* attachField = nullptr;
    static FieldInfo* nameField = nullptr;
    static FieldInfo* explainField = nullptr;
    static FieldInfo* dynamicTextField = nullptr;
    static FieldInfo* dynamicEnumField = nullptr;
    static FieldInfo* dynamicParamField = nullptr;

    if (!idField) {
        idField =
            GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_RelationId");
    }

    if (!attachField) {
        attachField =
            GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_mRelationAttach");
    }

    if (!nameField) {
        nameField =
            GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_mRelationName");
    }

    if (!explainField) {
        explainField =
            GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_mRelationExplain");
    }

    if (!dynamicTextField) {
        dynamicTextField =
            GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_mDynamicText");
    }

    if (!dynamicEnumField) {
        dynamicEnumField =
            GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_DynamicEnum");
    }

    if (!dynamicParamField) {
        dynamicParamField =
            GetFieldInfoFromName("", "CData_RelationSkillTip_MC_Element", "m_DynamicParam");
    }

    for (const auto& item : CopyDictionaryEntries(relationDictionary)) {
        void* relation = item.second;

        if (!relation) {
            continue;
        }

        if (!TryConsumeManagedWorkUnits(6)) {
            return 0;
        }

        int relationId = GetField<int>(reinterpret_cast<Il2CppObject*>(relation), idField);
        if (relationId <= 0) {
            continue;
        }

        std::string relationName = ReadRelationStringField(relation, attachField);
        if (relationName.empty()) {
            relationName = ReadRelationStringField(relation, nameField);
        }

        std::string relationText = relationName;
        relationText += " ";
        relationText += ReadRelationStringField(relation, explainField);
        relationText += " ";
        relationText += ReadRelationStringField(relation, dynamicTextField);

        if (RelationLooksLikeScavenger(relationText) ||
            RelationHasScavengerDynamicMarker(relation, dynamicEnumField) ||
            RelationHasScavengerDynamicParam(relation, dynamicParamField)) {
            FeatureState::CachedScavengerRelationId = relationId;

            if (!relationName.empty()) {
                std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
                FeatureState::Relations[relationId] = {relationId, relationName};
            }

            return relationId;
        }
    }

    return 0;
}

// Reads the current active Scavenger bond count for the local player.
int GetScavengerActiveCount(
    uint64_t selfAccountId,
    std::chrono::steady_clock::time_point now,
    bool force
) {
    if (selfAccountId == 0) {
        FeatureState::CachedScavengerActiveCount = -1;
        return -1;
    }

    if (!force &&
        !IntervalElapsed(FeatureState::LastScavengerCheck, RuntimeConfig::ScavengerCheckMs, now)) {
        return FeatureState::CachedScavengerActiveCount.load();
    }

    int relationId = ResolveScavengerRelationId();
    if (relationId <= 0 || !Originals::MCBondUtil_GetBondActiveCount) {
        FeatureState::CachedScavengerActiveCount = -1;
        FeatureState::LastScavengerCheck = now;
        return -1;
    }

    if (!TryConsumeManagedWorkUnits(2)) {
        return FeatureState::CachedScavengerActiveCount.load();
    }

    int activeCount =
        Originals::MCBondUtil_GetBondActiveCount(selfAccountId, relationId, true);
    FeatureState::CachedScavengerActiveCount = activeCount;
    FeatureState::LastScavengerCheck = now;
    return activeCount;
}

// Checks whether Scavenger is active at the minimum level needed for cleanup.
bool IsScavengerReadyForExpensiveHero(
    uint64_t selfAccountId,
    std::chrono::steady_clock::time_point now,
    bool force
) {
    return GetScavengerActiveCount(selfAccountId, now, force) >=
        RuntimeConfig::ScavengerMinimumActiveCount;
}

struct ScavengerShopCandidate {
    int slot = -1;
    MCLogicHeroShopItemData data{};
    bool isFreeBuy = false;
};

// Buys lower-priced shop heroes after an automatic shop refresh leaves Scavenger active.
bool RunScavengerShopCleanup(
    uint64_t selfAccountId,
    std::chrono::steady_clock::time_point now,
    bool forceScavengerRefresh
) {
    if (!FeatureState::ShopForceScavengerExpensiveHero.load()) {
        return true;
    }

    if (selfAccountId == 0 ||
        !Originals::MCLogicBattleData_ILOGIC_GetShopItemData ||
        !Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin ||
        !HasShopSelectBinding()) {
        return false;
    }

    if (!IsShopPanelReadyForAutomation()) {
        return false;
    }

    if (!IsScavengerReadyForExpensiveHero(selfAccountId, now, forceScavengerRefresh)) {
        return true;
    }

    if (FeatureState::ShopScavengerProcessing.exchange(true)) {
        return false;
    }

    struct ProcessingGuard {
        ~ProcessingGuard() {
            FeatureState::ShopScavengerProcessing = false;
        }
    } processingGuard;

    std::vector<ScavengerShopCandidate> candidates;
    candidates.reserve(5);
    int maxPrice = -1;

    for (int slot = 0; slot < 5; ++slot) {
        if (!TryConsumeManagedWorkUnits(4)) {
            return false;
        }

        bool needFx = false;
        bool isFreeBuy = false;

        if (Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy) {
            isFreeBuy = Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy(
                nullptr,
                selfAccountId,
                slot,
                &needFx
            );
        }

        MCLogicHeroShopItemData shopData =
            Originals::MCLogicBattleData_ILOGIC_GetShopItemData(
                nullptr,
                selfAccountId,
                slot
            );

        if (!IsValidShopItemData(shopData, slot)) {
            continue;
        }

        maxPrice = std::max(maxPrice, shopData.m_iPrice);
        candidates.push_back({slot, shopData, isFreeBuy});
    }

    if (candidates.empty() || maxPrice <= 0) {
        return true;
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const ScavengerShopCandidate& left, const ScavengerShopCandidate& right) {
            if (left.data.m_iPrice != right.data.m_iPrice) {
                return left.data.m_iPrice < right.data.m_iPrice;
            }

            return left.slot < right.slot;
        }
    );

    bool hasLowerPricedHero = false;
    for (const ScavengerShopCandidate& candidate : candidates) {
        if (candidate.data.m_iPrice < maxPrice) {
            hasLowerPricedHero = true;
            break;
        }
    }

    if (!hasLowerPricedHero) {
        return true;
    }

    int cachedCoin = -1;
    auto getCoin = [&]() {
        if (cachedCoin < 0) {
            if (!TryConsumeManagedWorkUnits()) {
                return -1;
            }

            cachedCoin =
                Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, selfAccountId);
        }

        return cachedCoin;
    };

    for (const ScavengerShopCandidate& candidate : candidates) {
        if (candidate.data.m_iPrice >= maxPrice) {
            continue;
        }

        int coin = getCoin();
        if (coin < 0) {
            return false;
        }

        if (!candidate.isFreeBuy) {
            if (coin < candidate.data.m_iPrice) {
                continue;
            }

            if (FeatureState::ShopKeepGold.load() &&
                coin - candidate.data.m_iPrice < FeatureState::ShopKeepGoldAt.load()) {
                continue;
            }
        }

        if (SelectShopSlot(candidate.slot)) {
            MarkShopBuyAttempt(
                selfAccountId,
                candidate.slot,
                candidate.data,
                -1,
                candidate.isFreeBuy,
                now
            );

            if (!candidate.isFreeBuy && cachedCoin >= 0) {
                cachedCoin -= candidate.data.m_iPrice;
            }
        }
    }

    return true;
}

// Grants hero through verified live-game bindings.
void GiveHero(int heroId, int star) {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();
    void* battleManager = nullptr;

    if (selfAccountId != 0 && Originals::MCComp_GetGamer) {
        battleManager = Originals::MCComp_GetGamer(selfAccountId);
    }

    if (!battleManager) {
        battleManager = GetSelfLogicBattleManager();
    }

    if (!battleManager || !Originals::MCLogicBattleManager_BuyNormalHero || heroId <= 0) {
        return;
    }

    star = std::clamp(star, 1, 3);
    MCLogicHeroShopItemData itemData{0, heroId, star, 0, 0, 0};
    bool ignoreExtraRule = false;
    Originals::MCLogicBattleManager_BuyNormalHero(
        battleManager,
        &itemData,
        &ignoreExtraRule
    );
}

// Grants equip through verified live-game bindings.
void GiveEquip(int equipId) {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();

    if (selfAccountId == 0 || !Originals::MCEquipUtil_OnGetNewEquip || equipId <= 0) {
        return;
    }

    uint32_t guid = 0;
    int upgradeState = FeatureState::ArenaItemEnhanced ? 1 : 0;
    Originals::MCEquipUtil_OnGetNewEquip(selfAccountId, equipId, &guid, upgradeState);
}

// Grants gold through verified live-game bindings.
void GiveGold() {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();

    if (selfAccountId == 0 ||
        !Originals::MCLogicBattleData_ILOGIC_GetPlayerData ||
        !Originals::MCChessPlayerData_UpdateCoin) {
        return;
    }

    void* playerData =
        Originals::MCLogicBattleData_ILOGIC_GetPlayerData(nullptr, selfAccountId);

    if (playerData) {
        Originals::MCChessPlayerData_UpdateCoin(playerData, 999999, 105);
    }
}

// Clamps arena time scale to the supported runtime range.
float ClampArenaTimeScale(float value) {
    return std::clamp(value, 0.1f, 20.0f);
}

// Applies arena speed hack to the live runtime when bindings are ready.
void ApplyArenaSpeedHack(uint64_t selfAccountId) {
    if (!Originals::UnityEngine_Time_set_timeScale) {
        return;
    }

    static float appliedTimeScale = 1.0f;
    static bool speedHackWasActive = false;
    bool active = FeatureState::ArenaSpeedHack.load() && selfAccountId != 0;
    float targetTimeScale =
        active ? ClampArenaTimeScale(FeatureState::ArenaTimeScale.load()) : 1.0f;
    FeatureState::ArenaTimeScale = ClampArenaTimeScale(FeatureState::ArenaTimeScale.load());

    if (appliedTimeScale > targetTimeScale - 0.001f &&
        appliedTimeScale < targetTimeScale + 0.001f &&
        speedHackWasActive == active) {
        return;
    }

    Originals::UnityEngine_Time_set_timeScale(targetTimeScale);
    appliedTimeScale = targetTimeScale;
    speedHackWasActive = active;
}

// Attempts the skip arena round to target action and reports whether it was safe to run.
bool TrySkipArenaRoundToTarget(bool force) {
    if (!IsIl2CppRuntimeReady() ||
        !Originals::MCLogicBattleData_ILOGIC_GetGameRound ||
        !Originals::MCLogicBattleData_get_logicRoundMgr ||
        !Originals::LogicRoundMgr_SetRound ||
        !Originals::LogicRoundMgr_NextRound) {
        return false;
    }

    int targetRound = std::clamp(FeatureState::ArenaSkipTargetRound.load(), 1, 99);
    FeatureState::ArenaSkipTargetRound = targetRound;

    if (!TryConsumeManagedWorkUnits(force ? 4 : 6)) {
        return false;
    }

    uint32_t currentRound = Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr);
    if (currentRound == 0 || currentRound >= static_cast<uint32_t>(targetRound)) {
        return false;
    }

    if (!force) {
        if (Originals::MCLogicBattleData_ILOGIC_IsFightSection &&
            Originals::MCLogicBattleData_ILOGIC_IsFightSection(nullptr)) {
            return false;
        }

        if (Originals::MCLogicBattleData_ILOGIC_IsFightResultSection &&
            Originals::MCLogicBattleData_ILOGIC_IsFightResultSection(nullptr)) {
            return false;
        }

        if (FeatureState::ArenaLastSkipSourceRound.load() == currentRound &&
            FeatureState::ArenaLastSkipTargetRound.load() == targetRound) {
            return false;
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (!force &&
        !CooldownElapsed(
            FeatureState::LastArenaSkipAttempt,
            RuntimeConfig::ArenaSkipCooldownMs,
            now
        )) {
        return false;
    }

    void* roundManager = Originals::MCLogicBattleData_get_logicRoundMgr(nullptr);
    if (!roundManager) {
        return false;
    }

    // Stage the manager on the previous round so NextRound performs normal phase setup.
    uint32_t stagedRound = static_cast<uint32_t>(std::max(targetRound - 1, 1));
    Originals::LogicRoundMgr_SetRound(roundManager, stagedRound);
    Originals::LogicRoundMgr_NextRound(roundManager, false);
    FeatureState::LastArenaSkipAttempt = now;
    FeatureState::ArenaLastSkipSourceRound = currentRound;
    FeatureState::ArenaLastSkipTargetRound = targetRound;
    return true;
}

// Checks the any combat power state condition before work proceeds.
bool HasAnyCombatPowerState() {
    return FeatureState::CombatForceWin ||
        FeatureState::CombatPreventHpLoss ||
        FeatureState::CombatBoostAttackRatio ||
        FeatureState::CombatCrippleEnemies;
}

// Applies player hp fields to the live runtime when bindings are ready.
void ApplyPlayerHpFields(void* playerData, int hpValue, bool useMaxHp) {
    if (!playerData) {
        return;
    }

    if (!TryConsumeManagedWorkUnits(useMaxHp ? 4 : 1)) {
        return;
    }

    static FieldInfo* currentHpField = nullptr;
    static FieldInfo* maxHpField = nullptr;
    static FieldInfo* lastReduceHpField = nullptr;
    static FieldInfo* failField = nullptr;

    if (!currentHpField) {
        currentHpField = GetFieldInfoFromName("", "MCChessPlayerData", "m_iCurrentHP");
    }

    if (!maxHpField) {
        maxHpField = GetFieldInfoFromName("", "MCChessPlayerData", "m_iMaxHP");
    }

    if (!lastReduceHpField) {
        lastReduceHpField = GetFieldInfoFromName("", "MCChessPlayerData", "m_iLastReduceHP");
    }

    if (!failField) {
        failField = GetFieldInfoFromName("", "MCChessPlayerData", "<m_bFail>k__BackingField");
    }

    int targetHp = hpValue;
    if (useMaxHp && maxHpField) {
        targetHp = std::max(GetField<int>(reinterpret_cast<Il2CppObject*>(playerData), maxHpField), 1);
    }

    SetField(reinterpret_cast<Il2CppObject*>(playerData), currentHpField, targetHp);

    if (useMaxHp) {
        SetField(reinterpret_cast<Il2CppObject*>(playerData), lastReduceHpField, 0);
        SetField(reinterpret_cast<Il2CppObject*>(playerData), failField, false);
    }
}

// Writes battle-manager power overrides for force-win and combat-value assists.
void ApplyBattleManagerPowerFields(
    void* battleManager,
    bool forceWin,
    bool boostAttack,
    double attackRatio,
    int fightValue
) {
    if (!battleManager) {
        return;
    }

    int requiredUnits = (boostAttack ? 3 : 0) + (forceWin ? 2 : 0);
    if (requiredUnits > 0 && !TryConsumeManagedWorkUnits(requiredUnits)) {
        return;
    }

    static FieldInfo* attackRatioField = nullptr;
    static FieldInfo* selfFightValueField = nullptr;
    static FieldInfo* killerFightValueField = nullptr;
    static FieldInfo* defendFailedField = nullptr;
    static FieldInfo* lastRoundWinField = nullptr;

    if (!attackRatioField) {
        attackRatioField = GetFieldInfoFromName("", "MCLogicBattleManager", "m_AtkRatio");
    }

    if (!selfFightValueField) {
        selfFightValueField = GetFieldInfoFromName("", "MCLogicBattleManager", "_selfFightValue");
    }

    if (!killerFightValueField) {
        killerFightValueField = GetFieldInfoFromName("", "MCLogicBattleManager", "killerFightValue");
    }

    if (!defendFailedField) {
        defendFailedField =
            GetFieldInfoFromName("", "MCLogicBattleManager", "<m_bDefendFaild>k__BackingField");
    }

    if (!lastRoundWinField) {
        lastRoundWinField = GetFieldInfoFromName("", "MCLogicBattleManager", "isLastRoundWin");
    }

    if (boostAttack) {
        SetField(reinterpret_cast<Il2CppObject*>(battleManager), attackRatioField, attackRatio);
        SetField(reinterpret_cast<Il2CppObject*>(battleManager), selfFightValueField, fightValue);
        SetField(reinterpret_cast<Il2CppObject*>(battleManager), killerFightValueField, fightValue);
    }

    if (forceWin) {
        SetField(reinterpret_cast<Il2CppObject*>(battleManager), defendFailedField, false);
        SetField(reinterpret_cast<Il2CppObject*>(battleManager), lastRoundWinField, true);
    }
}

// Applies combat state to the live runtime when bindings are ready.
void ApplyCombatState(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady() || selfAccountId == 0 || !HasAnyCombatPowerState()) {
        return;
    }

    bool forceWin = FeatureState::CombatForceWin.load();
    bool preventHpLoss = FeatureState::CombatPreventHpLoss.load();
    bool boostAttack = FeatureState::CombatBoostAttackRatio.load();
    bool crippleEnemies = FeatureState::CombatCrippleEnemies.load();
    double selfAttackRatio =
        static_cast<double>(std::clamp(FeatureState::CombatAttackRatioPercent.load(), 100, 100000)) /
        100.0;
    double enemyAttackRatio =
        static_cast<double>(std::clamp(FeatureState::CombatEnemyAttackRatioPercent.load(), 0, 100)) /
        100.0;
    int fightValue = std::clamp(FeatureState::CombatFightValue.load(), 0, 999999999);

    void* selfManager = GetSelfLogicBattleManager();
    ApplyBattleManagerPowerFields(selfManager, forceWin, boostAttack, selfAttackRatio, fightValue);

    if ((preventHpLoss || forceWin) && Originals::MCLogicBattleData_ILOGIC_GetPlayerData) {
        void* selfPlayerData =
            Originals::MCLogicBattleData_ILOGIC_GetPlayerData(nullptr, selfAccountId);
        ApplyPlayerHpFields(selfPlayerData, 1, true);
    }

    if (!crippleEnemies) {
        return;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr ||
        !Originals::MCLogicBattleData_ILOGIC_GetPlayerData) {
        return;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit)) {
        return;
    }

    for (int i = 0; entries && i < entryLimit; ++i) {
        const auto& entry = entries[i];

        if (entry.hashCode < 0 || entry.key == 0 || entry.key == selfAccountId) {
            continue;
        }

        if (ManagedWorkBudgetExceeded()) {
            break;
        }

        ApplyBattleManagerPowerFields(
            entry.value,
            false,
            true,
            enemyAttackRatio,
            0
        );

        void* enemyPlayerData =
            Originals::MCLogicBattleData_ILOGIC_GetPlayerData(nullptr, entry.key);
        ApplyPlayerHpFields(enemyPlayerData, 1, false);
    }
}

// Applies arena state to the live runtime when bindings are ready.
void ApplyArenaState(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    ApplyArenaSpeedHack(selfAccountId);

    if (selfAccountId == 0) {
        return;
    }

    if (FeatureState::ArenaSkipRound) {
        TrySkipArenaRoundToTarget(false);
    }

    if ((FeatureState::ArenaForceLevel99 ||
         FeatureState::ArenaAllEnemyHpOne) &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerData) {
        static FieldInfo* populationField = nullptr;
        static FieldInfo* slotPopulationField = nullptr;
        static FieldInfo* maxPopulationField = nullptr;
        static FieldInfo* levelField = nullptr;
        static FieldInfo* maxLevelField = nullptr;
        static FieldInfo* extraPopulationField = nullptr;
        static FieldInfo* hpField = nullptr;

        if (!populationField) {
            populationField =
                GetFieldInfoFromName("", "MCChessPlayerData", "m_iTotallPopulation");
        }

        if (!slotPopulationField) {
            slotPopulationField =
                GetFieldInfoFromName("", "MCChessPlayerData", "m_iSlotPopulation");
        }

        if (!maxPopulationField) {
            maxPopulationField =
                GetFieldInfoFromName("", "MCChessPlayerData", "<m_iMaxTotalPopulation>k__BackingField");
        }

        if (!levelField) {
            levelField = GetFieldInfoFromName("", "MCChessPlayerData", "m_iLevel");
        }

        if (!maxLevelField) {
            maxLevelField = GetFieldInfoFromName("", "MCChessPlayerData", "m_iMaxLevel");
        }

        if (!extraPopulationField) {
            extraPopulationField =
                GetFieldInfoFromName("", "MCChessPlayerData", "<m_iExtraPopulation>k__BackingField");
        }

        if (!hpField) {
            hpField = GetFieldInfoFromName("", "MCChessPlayerData", "m_iCurrentHP");
        }

        void* selfPlayerData =
            Originals::MCLogicBattleData_ILOGIC_GetPlayerData(nullptr, selfAccountId);

        if (FeatureState::ArenaForceLevel99 && selfPlayerData) {
            if (!TryConsumeManagedWorkUnits(6)) {
                return;
            }

            Il2CppObject* selfObject = reinterpret_cast<Il2CppObject*>(selfPlayerData);
            SetField(selfObject, levelField, 99);
            SetField(selfObject, maxLevelField, 99);
            SetField(selfObject, populationField, 99);
            SetField(selfObject, slotPopulationField, 99);
            SetField(selfObject, maxPopulationField, 99);
            SetField(selfObject, extraPopulationField, 99);
        }

        if (FeatureState::ArenaAllEnemyHpOne &&
            Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
            auto* battleManagers =
                Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
            const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
            int entryLimit = 0;

            if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit)) {
                return;
            }

            for (int i = 0; entries && i < entryLimit; ++i) {
                const auto& entry = entries[i];

                if (entry.hashCode < 0 || entry.key == 0 || entry.key == selfAccountId) {
                    continue;
                }

                if (!TryConsumeManagedWorkUnits(2)) {
                    break;
                }

                void* enemyPlayerData =
                    Originals::MCLogicBattleData_ILOGIC_GetPlayerData(nullptr, entry.key);

                if (enemyPlayerData) {
                    SetField(reinterpret_cast<Il2CppObject*>(enemyPlayerData), hpField, 1);
                }
            }
        }
    }

    if (FeatureState::ArenaGogoCardEnabled &&
        Originals::MCComp_GetGoGoCardComp &&
        (FeatureState::ArenaGogoCardSelected1 > 0 ||
         FeatureState::ArenaGogoCardSelected2 > 0)) {
        static FieldInfo* dictRoundField = nullptr;
        static FieldInfo* cardListField = nullptr;

        if (!dictRoundField) {
            dictRoundField =
                GetFieldInfoFromName("Battle", "MCLogicGoGoCardComp", "dictRound");
        }

        if (!cardListField) {
            cardListField =
                GetFieldInfoFromName("Battle", "MCLogicGoGoCardRoundData", "m_listCurrCard");
        }

        void* goGoCardComp = Originals::MCComp_GetGoGoCardComp(selfAccountId);

        auto* roundDictionary = goGoCardComp ?
            GetField<MonoStructures::Dictionary<int, void*>*>(
                reinterpret_cast<Il2CppObject*>(goGoCardComp),
                dictRoundField
            ) :
            nullptr;

        if (roundDictionary) {
            for (const auto& item : CopyDictionaryEntries(roundDictionary, 32)) {
                void* roundData = item.second;

                if (!roundData) {
                    continue;
                }

                if (!TryConsumeManagedWorkUnits(3)) {
                    break;
                }

                auto* cardList = GetField<MonoStructures::List<int>*>(
                    reinterpret_cast<Il2CppObject*>(roundData),
                    cardListField
                );

                if (!cardList) {
                    continue;
                }

                const int* cardData = nullptr;
                int cardCount = 0;

                if (!TryGetManagedListData(cardList, &cardData, &cardCount, 8)) {
                    continue;
                }

                if (FeatureState::ArenaGogoCardSelected1 > 0 && cardCount >= 1) {
                    cardList->set_Item(0, FeatureState::ArenaGogoCardSelected1);
                }

                if (FeatureState::ArenaGogoCardSelected2 > 0 && cardCount >= 2) {
                    cardList->set_Item(1, FeatureState::ArenaGogoCardSelected2);
                }
            }
        }
    }
}

// Advances features on its scheduled feature cadence.
void TickFeatures() {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    BeginManagedWorkBudget();

    auto frameStart = std::chrono::steady_clock::now();
    auto now = frameStart;
    int activeMainTab =
        std::clamp(UiState::MainTabIndex.load(), 0, static_cast<int>(MainTabSettings));

    RetryFeatureBindingsIfNeeded();
    // Let setup-thread binding scans finish before the render thread does managed work.
    if (RuntimeState::BindingResolveInProgress.load()) {
        return;
    }

    if (FeatureWorkBudgetExceeded(frameStart)) {
        return;
    }

    RefreshManagedReferences();
    if (FeatureWorkBudgetExceeded(frameStart)) {
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();
    RefreshTableDataForMatch(selfAccountId);
    if (FeatureWorkBudgetExceeded(frameStart)) {
        return;
    }

    if (ShouldLoadTableDataForFrame(activeMainTab)) {
        EnsureTableDataLoaded();
        if (FeatureWorkBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (activeMainTab == MainTabInfo) {
        RefreshGgcInfo(false);
        RefreshInfoPlayerRows(false);
    }

    if (activeMainTab == MainTabShop ||
        FeatureState::ShopBuyRecommendLineup.load() ||
        FeatureState::ShopStopRefreshAtRecommendLineup.load()) {
        GetRecommendLineupHeroId(now);
    }

    if (activeMainTab == MainTabShop ||
        FeatureState::ShopForceScavengerExpensiveHero.load()) {
        ResolveScavengerRelationId();
        if (FeatureState::ShopForceScavengerExpensiveHero.load()) {
            GetScavengerActiveCount(selfAccountId, now, false);
        }
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
        (activeMainTab == MainTabArena ||
         FeatureState::ArenaSkipRound.load())) {
        if (TryConsumeManagedWorkUnits()) {
            FeatureState::CachedGameRound =
                Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr);
        }
    } else if (selfAccountId == 0) {
        FeatureState::CachedGameRound = 0;
    }

    bool predictionRowsRebuilt = false;
    if (activeMainTab == MainTabInfo || UiState::ShowNextEnemyHud.load()) {
        predictionRowsRebuilt = RefreshCachedOpponentPredictionRows(selfAccountId, now);
        if (predictionRowsRebuilt) {
            FeatureState::LastOpponentPredictionTick = now;
        }

        if (FeatureWorkBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (UiState::ShowNextEnemyHud.load()) {
        RefreshNextEnemyHudText(selfAccountId);
        if (FeatureWorkBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (IntervalElapsed(FeatureState::LastArenaTick, RuntimeConfig::ArenaTickMs, now)) {
        ApplyArenaState(selfAccountId);
        if (FeatureWorkBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (IntervalElapsed(FeatureState::LastShopTick, RuntimeConfig::ShopTickMs, now)) {
        RunShopAutomation(selfAccountId);
        if (FeatureWorkBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (IntervalElapsed(FeatureState::LastCombatTick, RuntimeConfig::CombatTickMs, now)) {
        ApplyCombatState(selfAccountId);
    }

    if (IntervalElapsed(
            FeatureState::LastOpponentPredictionTick,
            RuntimeConfig::OpponentPredictionTickMs,
            now
        )) {
        if (!predictionRowsRebuilt) {
            TickOpponentPredictionHistory(selfAccountId);
        }
    }
}

// Coordinates ggc quality name for the overlay runtime.
const char* GgcQualityName(int quality) {
    switch (quality) {
        case 1:
            return "Blue";
        case 2:
            return "Purple";
        case 3:
            return "Gold";
        default:
            return "Unknown";
    }
}

// Coordinates ggc quality color for the overlay runtime.
ImVec4 GgcQualityColor(int quality) {
    switch (quality) {
        case 1:
            return ImVec4(0.25f, 0.55f, 1.0f, 1.0f);
        case 2:
            return ImVec4(0.70f, 0.35f, 1.0f, 1.0f);
        case 3:
            return ImVec4(1.0f, 0.78f, 0.20f, 1.0f);
        default:
            return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
    }
}

// Draws the status row overlay section without changing game state.
void DrawStatusRow(const char* label, bool ready) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(MenuText(label));
    ImGui::TableSetColumnIndex(1);
    ImGui::TextColored(
        ready ? ImVec4(0.40f, 0.90f, 0.45f, 1.0f) : ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
        "%s",
        ready ? MenuText("Ready") : MenuText("Waiting")
    );
}

// Draws the value row overlay section without changing game state.
void DrawValueRow(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(MenuText(label));
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(MenuText(value));
}

// Draws the value row overlay section without changing game state.
void DrawValueRow(const char* label, const std::string& value) {
    DrawValueRow(label, value.c_str());
}

bool IsKnownBuildMetadata(const std::string& value);
std::string ShortCommit(const std::string& commit);
const char* UpdateStatusLabel(UpdateCheckStatus status);
ImVec4 UpdateStatusColor(UpdateCheckStatus status);
bool MaybeStartUpdateCheck(bool manual);
UpdateCheckSnapshot GetUpdateCheckSnapshot();

// Draws a compact update status line for Settings without changing runtime features.
void DrawUpdateCompactStatus(const UpdateCheckSnapshot& snapshot) {
    ImGui::TextUnformatted(MenuText("Library update status"));
    ImGui::SameLine();
    ImGui::TextColored(
        UpdateStatusColor(snapshot.status),
        "%s",
        MenuText(UpdateStatusLabel(snapshot.status))
    );
}

// Draws scrollable release notes for cached GitHub release metadata.
void DrawUpdateChangelog(const std::vector<ReleaseInfo>& releases) {
    if (releases.empty()) {
        ImGui::TextUnformatted(MenuText("Waiting for release metadata"));
        return;
    }

    if (!ImGui::BeginChild(
            "##UpdateChangelogScroll",
            ImVec2(0.0f, 260.0f),
            true,
            ImGuiWindowFlags_HorizontalScrollbar
        )) {
        ImGui::EndChild();
        return;
    }

    for (const ReleaseInfo& release : releases) {
        std::string header = release.tag;
        if (!release.name.empty() && release.name != release.tag) {
            header += " - ";
            header += release.name;
        }

        ImGui::SeparatorText(header.c_str());
        if (!release.publishedAt.empty()) {
            ImGui::Text("%s: %s", MenuText("Release date"), release.publishedAt.c_str());
        }

        if (release.body.empty()) {
            ImGui::TextUnformatted(MenuText("No release notes provided"));
        } else {
            ImGui::TextWrapped("%s", release.body.c_str());
        }

        ImGui::Spacing();
    }

    ImGui::EndChild();
}

// Draws the Settings update notification and changelog section.
void DrawUpdateSettingsSection() {
    MaybeStartUpdateCheck(false);
    UpdateCheckSnapshot snapshot = GetUpdateCheckSnapshot();

    ImGui::Spacing();
    DrawUpdateCompactStatus(snapshot);

    if (!DrawMenuCollapsingHeader("Updates / Changelog")) {
        return;
    }

    if (ImGui::BeginTable(
        "##UpdateStatusTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        ImGui::TableSetupColumn(MenuText("Field"));
        ImGui::TableSetupColumn(MenuText("Value"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        DrawValueRow("Repository", snapshot.repository);
        DrawValueRow(
            "Current version",
            IsKnownBuildMetadata(snapshot.localVersion) ? snapshot.localVersion : MenuText("Unknown")
        );
        DrawValueRow("Current commit", ShortCommit(snapshot.localCommit));
        DrawValueRow(
            "Current ref",
            IsKnownBuildMetadata(snapshot.localRef) ? snapshot.localRef : MenuText("Unknown")
        );
        DrawValueRow(
            "Latest version",
            snapshot.latestVersion.empty() ? MenuText("Waiting") : snapshot.latestVersion
        );
        DrawValueRow(
            "Release date",
            snapshot.latestPublishedAt.empty() ? MenuText("Waiting") : snapshot.latestPublishedAt
        );
        DrawValueRow(
            "Last check",
            snapshot.lastCheckText.empty() ? MenuText("Waiting") : snapshot.lastCheckText
        );
        DrawValueRow("Status", UpdateStatusLabel(snapshot.status));
        DrawValueRow(
            "Summary",
            snapshot.latestSummary.empty() ? MenuText("Waiting") : snapshot.latestSummary
        );

        if (!snapshot.lastError.empty()) {
            DrawValueRow("Failure", snapshot.lastError);
        }

        ImGui::EndTable();
    }

    ImGui::BeginDisabled(snapshot.checkInProgress);
    if (DrawMenuButton("Refresh update check")) {
        MaybeStartUpdateCheck(true);
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    DrawUpdateChangelog(snapshot.releases);
}

// Formats bool for readable overlay output.
std::string FormatBool(bool value) {
    return value ? "true" : "false";
}

// Formats optional bool for readable overlay output.
std::string FormatOptionalBool(bool ready, bool value) {
    return ready ? FormatBool(value) : "Waiting";
}

// Formats pointer for readable overlay output.
std::string FormatPointer(const void* value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%p", value);
    return buffer;
}

// Formats u int64 for readable overlay output.
std::string FormatUInt64(uint64_t value) {
    return std::to_string(static_cast<unsigned long long>(value));
}

// Formats u int32 for readable overlay output.
std::string FormatUInt32(uint32_t value) {
    return std::to_string(static_cast<unsigned int>(value));
}

// Formats int64 for readable overlay output.
std::string FormatInt64(int64_t value) {
    return std::to_string(static_cast<long long>(value));
}

// Formats int for readable overlay output.
std::string FormatInt(int value) {
    return std::to_string(value);
}

// Formats float for readable overlay output.
std::string FormatFloat(float value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

// Coordinates hex color for the overlay runtime.
ImVec4 HexColor(unsigned int rgb, float alpha = 1.0f) {
    return ImVec4(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha
    );
}

// Coordinates trim string for the overlay runtime.
std::string TrimString(const std::string& value) {
    size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

// Coordinates lower string for the overlay runtime.
std::string LowerString(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );
    return value;
}

// Returns true when an embedded build metadata value is present and usable.
bool IsKnownBuildMetadata(const std::string& value) {
    std::string lower = LowerString(TrimString(value));
    return !lower.empty() &&
        lower != "unknown" &&
        lower != "local" &&
        lower != "(unknown)";
}

// Returns the current library build version compiled into the native module.
std::string GetLocalBuildVersion() {
    return TrimString(MCGG_BUILD_VERSION);
}

// Returns the current library commit compiled into the native module.
std::string GetLocalBuildCommit() {
    return TrimString(MCGG_BUILD_COMMIT);
}

// Returns the current library repository compiled into the native module.
std::string GetReleaseRepository() {
    std::string repository = TrimString(MCGG_BUILD_REPOSITORY);
    return repository.empty() ? "Yan-0001/MCGG" : repository;
}

// Formats the current system time for update check diagnostics.
std::string FormatCurrentSystemTime() {
    time_t now = time(nullptr);
    if (now <= 0) {
        return "Unknown";
    }

    struct tm tmValue{};
    if (!localtime_r(&now, &tmValue)) {
        return "Unknown";
    }

    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmValue) == 0) {
        return "Unknown";
    }

    return buffer;
}

// Converts update status into the short label used by Settings UI.
const char* UpdateStatusLabel(UpdateCheckStatus status) {
    switch (status) {
        case UpdateCheckStatus::UpToDate:
            return "Up to date";
        case UpdateCheckStatus::UpdateAvailable:
            return "Update available";
        case UpdateCheckStatus::Failed:
            return "GitHub request failed";
        case UpdateCheckStatus::Malformed:
            return "Malformed release metadata";
        case UpdateCheckStatus::UnknownLocalVersion:
            return "Unknown local version";
        case UpdateCheckStatus::Checking:
        case UpdateCheckStatus::Waiting:
        default:
            return "Waiting for network check";
    }
}

// Coordinates update status color for the overlay runtime.
ImVec4 UpdateStatusColor(UpdateCheckStatus status) {
    switch (status) {
        case UpdateCheckStatus::UpToDate:
            return ImVec4(0.40f, 0.90f, 0.45f, 1.0f);
        case UpdateCheckStatus::UpdateAvailable:
            return ImVec4(0.40f, 0.70f, 1.0f, 1.0f);
        case UpdateCheckStatus::Failed:
        case UpdateCheckStatus::Malformed:
        case UpdateCheckStatus::UnknownLocalVersion:
            return ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
        case UpdateCheckStatus::Checking:
        case UpdateCheckStatus::Waiting:
        default:
            return ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
    }
}

// Shortens a commit hash for compact build metadata display.
std::string ShortCommit(const std::string& commit) {
    if (!IsKnownBuildMetadata(commit)) {
        return "unknown";
    }

    return commit.substr(0, std::min<size_t>(commit.size(), 12));
}

// Checks whether two commit strings refer to the same known commit.
bool CommitMatches(const std::string& left, const std::string& right) {
    if (!IsKnownBuildMetadata(left) || !IsKnownBuildMetadata(right)) {
        return false;
    }

    if (left == right) {
        return true;
    }

    size_t sharedLength = std::min(left.size(), right.size());
    if (sharedLength < 7) {
        return false;
    }

    return left.compare(0, sharedLength, right, 0, sharedLength) == 0;
}

// Extracts numeric tag components for release-version comparison.
std::vector<int> ParseVersionNumbers(const std::string& version) {
    std::vector<int> numbers;
    long current = -1;

    for (char ch : version) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            if (current < 0) {
                current = 0;
            }

            current = std::min<long>(current * 10 + (ch - '0'), 999999999);
            continue;
        }

        if (current >= 0) {
            numbers.push_back(static_cast<int>(current));
            current = -1;
        }
    }

    if (current >= 0) {
        numbers.push_back(static_cast<int>(current));
    }

    return numbers;
}

// Compares local and remote release tags using their numeric components.
int CompareReleaseVersions(const std::string& localVersion, const std::string& latestVersion) {
    if (localVersion == latestVersion) {
        return 0;
    }

    std::vector<int> localNumbers = ParseVersionNumbers(localVersion);
    std::vector<int> latestNumbers = ParseVersionNumbers(latestVersion);
    if (localNumbers.empty() || latestNumbers.empty()) {
        return 0;
    }

    size_t count = std::max(localNumbers.size(), latestNumbers.size());
    for (size_t i = 0; i < count; ++i) {
        int localValue = i < localNumbers.size() ? localNumbers[i] : 0;
        int latestValue = i < latestNumbers.size() ? latestNumbers[i] : 0;

        if (localValue < latestValue) {
            return -1;
        }

        if (localValue > latestValue) {
            return 1;
        }
    }

    return 0;
}

// Extracts a JSON string field from a GitHub release object.
bool JsonStringField(
    const nlohmann::json& object,
    const char* key,
    std::string& output,
    bool allowNull = false
) {
    auto value = object.find(key);
    if (value == object.end()) {
        return false;
    }

    if (allowNull && value->is_null()) {
        output.clear();
        return true;
    }

    if (!value->is_string()) {
        return false;
    }

    output = value->get_ref<const std::string&>();
    return true;
}

// Extracts a JSON boolean field from a GitHub release object.
bool JsonBoolField(const nlohmann::json& object, const char* key, bool& output) {
    auto value = object.find(key);
    if (value == object.end() || !value->is_boolean()) {
        return false;
    }

    output = value->get<bool>();
    return true;
}

// Builds a compact first-line release summary for status display.
std::string BuildReleaseSummary(const ReleaseInfo& release) {
    size_t cursor = 0;
    while (cursor < release.body.size()) {
        size_t newline = release.body.find('\n', cursor);
        std::string line = TrimString(release.body.substr(
            cursor,
            newline == std::string::npos ? std::string::npos : newline - cursor
        ));

        while (!line.empty() && (line[0] == '#' || line[0] == '-' || line[0] == '*')) {
            line = TrimString(line.substr(1));
        }

        if (!line.empty()) {
            if (line.size() > 220) {
                line.resize(220);
                line += "...";
            }
            return line;
        }

        if (newline == std::string::npos) {
            break;
        }

        cursor = newline + 1;
    }

    return release.name.empty() ? "No release summary provided" : release.name;
}

// Parses GitHub release metadata needed by the update overlay.
bool ParseGitHubReleases(const std::string& jsonText, std::vector<ReleaseInfo>& releases) {
    nlohmann::json parsed = nlohmann::json::parse(jsonText, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array() || parsed.empty()) {
        return false;
    }

    releases.clear();
    releases.reserve(parsed.size());

    for (const nlohmann::json& object : parsed) {
        if (!object.is_object()) {
            continue;
        }

        ReleaseInfo release;
        if (!JsonStringField(object, "tag_name", release.tag)) {
            continue;
        }

        JsonStringField(object, "name", release.name, true);
        JsonStringField(object, "published_at", release.publishedAt, true);
        JsonStringField(object, "target_commitish", release.targetCommitish, true);
        JsonStringField(object, "body", release.body, true);
        JsonBoolField(object, "draft", release.draft);
        JsonBoolField(object, "prerelease", release.prerelease);

        if (release.body.size() > RuntimeConfig::MaxReleaseBodyPreviewChars) {
            release.body.resize(RuntimeConfig::MaxReleaseBodyPreviewChars);
            release.body += "\n\n[Release notes clipped in overlay]";
        }

        release.summary = BuildReleaseSummary(release);
        if (!release.draft && !release.prerelease && !release.tag.empty()) {
            releases.push_back(std::move(release));
        }
    }

    return !releases.empty();
}

// Stores bytes returned by libcurl with a strict in-memory response cap.
size_t CurlWriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t bytes = size * nmemb;
    auto* response = reinterpret_cast<std::string*>(userdata);

    if (!response ||
        response->size() + bytes > RuntimeConfig::MaxReleaseResponseBytes) {
        return 0;
    }

    response->append(ptr, bytes);
    return bytes;
}

// Ensures libcurl process-level state is initialized once for update checks.
CURLcode EnsureCurlInitialized() {
    static std::once_flag initOnce;
    static CURLcode initCode = CURLE_FAILED_INIT;

    std::call_once(initOnce, []() {
        initCode = curl_global_init(CURL_GLOBAL_DEFAULT);
    });

    return initCode;
}

// Fetches release metadata from GitHub without sending private runtime data.
bool FetchGitHubReleaseJson(std::string& response, long& httpCode, std::string& error) {
    CURLcode initCode = EnsureCurlInitialized();
    if (initCode != CURLE_OK) {
        error = curl_easy_strerror(initCode);
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    std::string repository = GetReleaseRepository();
    std::string url =
        "https://api.github.com/repos/" + repository + "/releases?per_page=20";
    char errorBuffer[CURL_ERROR_SIZE]{0};
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MCGG-update-check");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(result);
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        error = "GitHub returned HTTP " + std::to_string(httpCode);
        return false;
    }

    return true;
}

// Computes update availability from local build metadata and release metadata.
UpdateCheckStatus EvaluateUpdateStatus(const std::vector<ReleaseInfo>& releases) {
    if (releases.empty()) {
        return UpdateCheckStatus::Malformed;
    }

    const ReleaseInfo& latest = releases.front();
    std::string localVersion = GetLocalBuildVersion();
    std::string localCommit = GetLocalBuildCommit();

    if (CommitMatches(localCommit, latest.targetCommitish)) {
        return UpdateCheckStatus::UpToDate;
    }

    if (!IsKnownBuildMetadata(localVersion)) {
        return UpdateCheckStatus::UnknownLocalVersion;
    }

    int versionCompare = CompareReleaseVersions(localVersion, latest.tag);
    return versionCompare < 0 ?
        UpdateCheckStatus::UpdateAvailable :
        UpdateCheckStatus::UpToDate;
}

// Calculates bounded retry delay for failed update checks.
int UpdateRetryDelayMs(int failureCount) {
    int delay = RuntimeConfig::UpdateCheckRetryBaseMs;
    int steps = std::clamp(failureCount - 1, 0, 4);
    for (int i = 0; i < steps; ++i) {
        delay = std::min(delay * 2, RuntimeConfig::UpdateCheckRetryMaxMs);
    }
    return delay;
}

// Runs the GitHub release check on a detached worker thread.
void RunUpdateCheckWorker() {
    std::string response;
    std::string error;
    long httpCode = 0;
    bool requestOk = FetchGitHubReleaseJson(response, httpCode, error);
    std::vector<ReleaseInfo> releases;
    bool parsed = requestOk && ParseGitHubReleases(response, releases);
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(RuntimeMutex::UpdateMutex);
    UpdateState::CheckInProgress = false;
    UpdateState::LastCheckText = FormatCurrentSystemTime();
    UpdateState::LastCheckAttempt = now;

    if (!requestOk) {
        UpdateState::Status = UpdateCheckStatus::Failed;
        UpdateState::LastError = error.empty() ? "GitHub request failed" : error;
        UpdateState::FailureCount = std::min(UpdateState::FailureCount + 1, 8);
        UpdateState::NextAllowedCheck =
            now + std::chrono::milliseconds(UpdateRetryDelayMs(UpdateState::FailureCount));
        return;
    }

    if (!parsed) {
        UpdateState::Status = UpdateCheckStatus::Malformed;
        UpdateState::LastError = "GitHub release metadata did not include usable releases";
        UpdateState::FailureCount = std::min(UpdateState::FailureCount + 1, 8);
        UpdateState::NextAllowedCheck =
            now + std::chrono::milliseconds(UpdateRetryDelayMs(UpdateState::FailureCount));
        return;
    }

    UpdateState::FailureCount = 0;
    UpdateState::LastError.clear();
    UpdateState::Releases = std::move(releases);
    const ReleaseInfo& latest = UpdateState::Releases.front();
    UpdateState::LatestVersion = latest.tag;
    UpdateState::LatestName = latest.name;
    UpdateState::LatestPublishedAt = latest.publishedAt;
    UpdateState::LatestSummary = latest.summary;
    UpdateState::Status = EvaluateUpdateStatus(UpdateState::Releases);
    UpdateState::NextAllowedCheck =
        now + std::chrono::milliseconds(RuntimeConfig::UpdateCheckRefreshMs);
}

// Starts an update check when the cache or retry cadence allows it.
bool MaybeStartUpdateCheck(bool manual) {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::UpdateMutex);
        if (UpdateState::CheckInProgress) {
            return false;
        }

        if (!manual && UpdateState::NextAllowedCheck.time_since_epoch().count() != 0 &&
            now < UpdateState::NextAllowedCheck) {
            return false;
        }

        UpdateState::CheckInProgress = true;
        UpdateState::Status = UpdateCheckStatus::Checking;
    }

    std::thread(RunUpdateCheckWorker).detach();
    return true;
}

// Copies update state for UI rendering without holding the update mutex during ImGui calls.
UpdateCheckSnapshot GetUpdateCheckSnapshot() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::UpdateMutex);
    UpdateCheckSnapshot snapshot;
    snapshot.status = UpdateState::Status;
    snapshot.localVersion = GetLocalBuildVersion();
    snapshot.localCommit = GetLocalBuildCommit();
    snapshot.localRef = TrimString(MCGG_BUILD_REF);
    snapshot.repository = GetReleaseRepository();
    snapshot.latestVersion = UpdateState::LatestVersion;
    snapshot.latestName = UpdateState::LatestName;
    snapshot.latestPublishedAt = UpdateState::LatestPublishedAt;
    snapshot.latestSummary = UpdateState::LatestSummary;
    snapshot.lastCheckText = UpdateState::LastCheckText;
    snapshot.lastError = UpdateState::LastError;
    snapshot.failureCount = UpdateState::FailureCount;
    snapshot.checkInProgress = UpdateState::CheckInProgress;
    snapshot.releases = UpdateState::Releases;
    return snapshot;
}

// Parses config bool from user or config text with a safe fallback.
bool ParseConfigBool(const std::string& value, bool fallback) {
    std::string lower = LowerString(TrimString(value));

    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        return true;
    }

    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        return false;
    }

    return fallback;
}

// Parses config int from user or config text with a safe fallback.
int ParseConfigInt(const std::string& value, int fallback) {
    char* end = nullptr;
    long parsed = strtol(value.c_str(), &end, 10);

    if (end == value.c_str()) {
        return fallback;
    }

    return static_cast<int>(parsed);
}

// Parses config float from user or config text with a safe fallback.
float ParseConfigFloat(const std::string& value, float fallback) {
    char* end = nullptr;
    float parsed = strtof(value.c_str(), &end);

    if (end == value.c_str()) {
        return fallback;
    }

    return parsed;
}

// Clamps configurable state to the supported runtime range.
void ClampConfigurableState() {
    UiState::MainTabIndex =
        std::clamp(UiState::MainTabIndex.load(), 0, static_cast<int>(MainTabSettings));
    UiState::ThemeIndex =
        std::clamp(UiState::ThemeIndex.load(), 0, kAppearanceThemeCount - 1);
    UiState::FontIndex = std::clamp(UiState::FontIndex.load(), 0, 1);
    UiState::LanguageIndex =
        std::clamp(UiState::LanguageIndex.load(), 0, kMenuLanguageCount - 1);
    UiState::MenuWidth = std::clamp(UiState::MenuWidth.load(), 320.0f, 1600.0f);
    UiState::MenuHeight = std::clamp(UiState::MenuHeight.load(), 260.0f, 1200.0f);
    UiState::MenuPosX = std::clamp(UiState::MenuPosX.load(), -2000.0f, 4000.0f);
    UiState::MenuPosY = std::clamp(UiState::MenuPosY.load(), -2000.0f, 4000.0f);
    UiState::FontScale = std::clamp(UiState::FontScale.load(), 0.65f, 2.0f);
    UiState::WindowAlpha = std::clamp(UiState::WindowAlpha.load(), 0.35f, 1.0f);
    UiState::WindowRounding = std::clamp(UiState::WindowRounding.load(), 0.0f, 20.0f);
    UiState::ChildRounding = std::clamp(UiState::ChildRounding.load(), 0.0f, 20.0f);
    UiState::FrameRounding = std::clamp(UiState::FrameRounding.load(), 0.0f, 20.0f);
    UiState::PopupRounding = std::clamp(UiState::PopupRounding.load(), 0.0f, 20.0f);
    UiState::ScrollbarRounding = std::clamp(UiState::ScrollbarRounding.load(), 0.0f, 20.0f);
    UiState::GrabRounding = std::clamp(UiState::GrabRounding.load(), 0.0f, 20.0f);
    UiState::TabRounding = std::clamp(UiState::TabRounding.load(), 0.0f, 20.0f);
    UiState::ScrollbarSize = std::clamp(UiState::ScrollbarSize.load(), 8.0f, 32.0f);
    UiState::WindowBorderSize = std::clamp(UiState::WindowBorderSize.load(), 0.0f, 4.0f);
    UiState::FrameBorderSize = std::clamp(UiState::FrameBorderSize.load(), 0.0f, 4.0f);
    UiState::FramePaddingX = std::clamp(UiState::FramePaddingX.load(), 0.0f, 24.0f);
    UiState::FramePaddingY = std::clamp(UiState::FramePaddingY.load(), 0.0f, 24.0f);
    UiState::ItemSpacingX = std::clamp(UiState::ItemSpacingX.load(), 0.0f, 32.0f);
    UiState::ItemSpacingY = std::clamp(UiState::ItemSpacingY.load(), 0.0f, 32.0f);
    UiState::IndentSpacing = std::clamp(UiState::IndentSpacing.load(), 0.0f, 48.0f);
    FeatureState::CombatAttackRatioPercent =
        std::clamp(FeatureState::CombatAttackRatioPercent.load(), 100, 100000);
    FeatureState::CombatEnemyAttackRatioPercent =
        std::clamp(FeatureState::CombatEnemyAttackRatioPercent.load(), 0, 100);
    FeatureState::CombatFightValue =
        std::clamp(FeatureState::CombatFightValue.load(), 0, 999999999);
    FeatureState::ShopKeepGoldAt = std::clamp(FeatureState::ShopKeepGoldAt.load(), 0, 999999);
    FeatureState::ShopRecommendTargetCount =
        ClampShopTargetCount(FeatureState::ShopRecommendTargetCount.load());
    FeatureState::ArenaHeroStar = std::clamp(FeatureState::ArenaHeroStar.load(), 1, 3);
    FeatureState::ArenaPrice = std::clamp(FeatureState::ArenaPrice.load(), 0, 99);
    FeatureState::ArenaSkipTargetRound =
        std::clamp(FeatureState::ArenaSkipTargetRound.load(), 1, 99);
    FeatureState::ArenaTimeScale = ClampArenaTimeScale(FeatureState::ArenaTimeScale.load());
    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);

        for (auto& item : FeatureState::ShopSelectedHeroes) {
            item.second.targetCount = ClampShopTargetCount(item.second.targetCount);
        }

        for (auto& item : FeatureState::ShopRecommendLineupTargetCounts) {
            item.second = ClampShopTargetCount(item.second);
        }
    }
}

// Resets visual settings back to safe default values.
void ResetVisualSettings() {
    UiState::ThemeIndex = kDefaultThemeIndex;
    UiState::FontIndex = AppearanceState::NotoCjkFont ? 1 : 0;
    UiState::LanguageIndex = kLanguageEnglish;
    UiState::MoveFromTitleBarOnly = true;
    UiState::ResizeFromEdges = false;
    UiState::UseFixedMenuPosition = false;
    UiState::ShowNextEnemyHud = false;
    UiState::MenuWidth = 760.0f;
    UiState::MenuHeight = 560.0f;
    UiState::MenuPosX = 20.0f;
    UiState::MenuPosY = 20.0f;
    UiState::FontScale = 1.0f;
    UiState::WindowAlpha = 1.0f;
    UiState::WindowRounding = 7.0f;
    UiState::ChildRounding = 6.0f;
    UiState::FrameRounding = 5.0f;
    UiState::PopupRounding = 6.0f;
    UiState::ScrollbarRounding = 6.0f;
    UiState::GrabRounding = 5.0f;
    UiState::TabRounding = 5.0f;
    UiState::ScrollbarSize = 14.0f;
    UiState::WindowBorderSize = 1.0f;
    UiState::FrameBorderSize = 0.0f;
    UiState::FramePaddingX = 4.0f;
    UiState::FramePaddingY = 3.0f;
    UiState::ItemSpacingX = 8.0f;
    UiState::ItemSpacingY = 4.0f;
    UiState::IndentSpacing = 21.0f;
    AppearanceState::AppliedThemeIndex = -1;
}

// Resets feature settings back to safe default values.
void ResetFeatureSettings() {
    FeatureState::CombatInvisibleScout = false;
    FeatureState::CombatForceWin = false;
    FeatureState::CombatPreventHpLoss = false;
    FeatureState::CombatBoostAttackRatio = false;
    FeatureState::CombatCrippleEnemies = false;
    FeatureState::CombatAttackRatioPercent = 5000;
    FeatureState::CombatEnemyAttackRatioPercent = 1;
    FeatureState::CombatFightValue = 999999999;
    FeatureState::ShopBuyFreeHero = false;
    FeatureState::ShopBuySelectedHero = false;
    FeatureState::ShopBuyRecommendLineup = false;
    FeatureState::ShopForceScavengerExpensiveHero = false;
    FeatureState::ShopRefresh = false;
    FeatureState::ShopStopRefreshAtFreeHero = false;
    FeatureState::ShopStopRefreshAtSelectedHero = false;
    FeatureState::ShopStopRefreshAtRecommendLineup = false;
    FeatureState::ShopKeepGold = false;
    FeatureState::ShopKeepGoldAt = 20;
    FeatureState::ShopRecommendTargetCount = 9;
    FeatureState::CachedScavengerActiveCount = -1;
    FeatureState::ShopScavengerAutoRefreshPending = false;
    ClearShopHeroTargets();
    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
        FeatureState::ShopRecommendLineupTargetCounts.clear();
        FeatureState::CachedRecommendLineupHeroIds.clear();
    }
    FeatureState::ArenaHeroStar = 1;
    FeatureState::ArenaItemEnhanced = false;
    FeatureState::ArenaGogoCardEnabled = false;
    FeatureState::ArenaGogoCardSelected1 = -1;
    FeatureState::ArenaGogoCardSelected2 = -1;
    FeatureState::ArenaForceActiveSynergy = false;
    FeatureState::ArenaForceLevel99 = false;
    FeatureState::ArenaOutsideMapPlacement = false;
    FeatureState::ArenaAllEnemyHpOne = false;
    FeatureState::ArenaForceCompleteAchievements = false;
    FeatureState::ArenaPrice = 5;
    FeatureState::ArenaSkipRound = false;
    FeatureState::ArenaSkipTargetRound = 1;
    FeatureState::ArenaLastSkipSourceRound = 0;
    FeatureState::ArenaLastSkipTargetRound = 0;
    FeatureState::CachedGameRound = 0;
    FeatureState::ArenaSpeedHack = false;
    FeatureState::ArenaTimeScale = 2.0f;
    ApplyArenaSpeedHack(0);
}

// Ensures directory path exists before file or UI work continues.
bool EnsureDirectoryPath(const std::string& directory) {
    if (directory.empty()) {
        return true;
    }

    for (size_t i = 1; i <= directory.size(); ++i) {
        if (i < directory.size() && directory[i] != '/') {
            continue;
        }

        std::string partial = directory.substr(0, i);
        if (partial.empty()) {
            continue;
        }

        if (access(partial.c_str(), F_OK) == 0) {
            continue;
        }

        if (mkdir(partial.c_str(), 0775) != 0 && errno != EEXIST) {
            return false;
        }
    }

    return true;
}

// Ensures parent directory exists before file or UI work continues.
bool EnsureParentDirectory(const std::string& path) {
    size_t slash = path.find_last_of('/');

    if (slash == std::string::npos || slash == 0) {
        return true;
    }

    return EnsureDirectoryPath(path.substr(0, slash));
}

// Returns the cached or live current process name value used by runtime features.
std::string GetCurrentProcessName() {
    FILE* file = fopen("/proc/self/cmdline", "r");
    if (!file) {
        return {};
    }

    char buffer[1024];
    size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);

    if (length == 0) {
        return {};
    }

    buffer[length] = '\0';
    return std::string(buffer);
}

// Returns the cached or live game package name value used by runtime features.
std::string GetGamePackageName() {
    std::string processName = GetCurrentProcessName();
    size_t suffix = processName.find(':');

    if (suffix != std::string::npos) {
        processName.resize(suffix);
    }

    return processName;
}

// Returns the cached or live default config path value used by runtime features.
std::string GetDefaultConfigPath() {
    std::string packageName = GetGamePackageName();

    if (packageName.empty()) {
        return "mcgg_config.ini";
    }

    return "/data/data/" + packageName + "/files/mcgg_config.ini";
}

// Ensures config path initialized exists before file or UI work continues.
void EnsureConfigPathInitialized() {
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex::UiMutex);
    if (UiState::ConfigPath.empty()) {
        UiState::ConfigPath = GetDefaultConfigPath();
    }
}

// Updates config status through the safest available runtime path.
void SetConfigStatus(const char* prefix, const std::string& path, bool success) {
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex::UiMutex);
    UiState::ConfigStatus = prefix;
    UiState::ConfigStatus += success ? ": " : " failed: ";
    UiState::ConfigStatus += path;
}

// Formats shop selected heroes for readable overlay output.
std::string FormatShopSelectedHeroes() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    std::string value;

    for (const auto& item : FeatureState::ShopSelectedHeroes) {
        if (!item.second.selected) {
            continue;
        }

        if (!value.empty()) {
            value += ",";
        }

        value += std::to_string(item.first);
        value += ":";
        value += std::to_string(std::max(item.second.targetCount, 1));
    }

    return value;
}

// Loads shop selected heroes and falls back cleanly if unavailable.
void LoadShopSelectedHeroes(const std::string& value) {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    FeatureState::ShopSelectedHeroes.clear();

    size_t cursor = 0;
    while (cursor < value.size()) {
        size_t comma = value.find(',', cursor);
        std::string token = value.substr(
            cursor,
            comma == std::string::npos ? std::string::npos : comma - cursor
        );
        size_t separator = token.find(':');

        int heroId = ParseConfigInt(
            separator == std::string::npos ? token : token.substr(0, separator),
            0
        );
        int targetCount = ParseConfigInt(
            separator == std::string::npos ? "9" : token.substr(separator + 1),
            9
        );

        if (IsPlausibleHeroId(heroId)) {
            FeatureState::ShopSelectedHeroes[heroId] = {
                true,
                ClampShopTargetCount(targetCount)
            };
        }

        if (comma == std::string::npos) {
            break;
        }

        cursor = comma + 1;
    }
}

// Writes config bool in the existing config or managed runtime format.
void WriteConfigBool(FILE* file, const char* key, bool value) {
    fprintf(file, "%s=%d\n", key, value ? 1 : 0);
}

// Writes config int in the existing config or managed runtime format.
void WriteConfigInt(FILE* file, const char* key, int value) {
    fprintf(file, "%s=%d\n", key, value);
}

// Writes config float in the existing config or managed runtime format.
void WriteConfigFloat(FILE* file, const char* key, float value) {
    fprintf(file, "%s=%.3f\n", key, value);
}

// Writes config string in the existing config or managed runtime format.
void WriteConfigString(FILE* file, const char* key, const std::string& value) {
    fprintf(file, "%s=%s\n", key, value.c_str());
}

// Applies config value to the live runtime when bindings are ready.
void ApplyConfigValue(const std::string& key, const std::string& value) {
    if (key == "themeIndex") UiState::ThemeIndex = ParseConfigInt(value, UiState::ThemeIndex);
    else if (key == "fontIndex") UiState::FontIndex = ParseConfigInt(value, UiState::FontIndex);
    else if (key == "languageIndex") UiState::LanguageIndex = ParseConfigInt(value, UiState::LanguageIndex);
    else if (key == "shopShowSelectedOnly") UiState::ShopShowSelectedOnly = ParseConfigBool(value, UiState::ShopShowSelectedOnly);
    else if (key == "showNextEnemyHud") UiState::ShowNextEnemyHud = ParseConfigBool(value, UiState::ShowNextEnemyHud);
    else if (key == "moveFromTitleBarOnly") UiState::MoveFromTitleBarOnly = ParseConfigBool(value, UiState::MoveFromTitleBarOnly);
    else if (key == "resizeFromEdges") UiState::ResizeFromEdges = ParseConfigBool(value, UiState::ResizeFromEdges);
    else if (key == "useFixedMenuPosition") UiState::UseFixedMenuPosition = ParseConfigBool(value, UiState::UseFixedMenuPosition);
    else if (key == "menuWidth") UiState::MenuWidth = ParseConfigFloat(value, UiState::MenuWidth);
    else if (key == "menuHeight") UiState::MenuHeight = ParseConfigFloat(value, UiState::MenuHeight);
    else if (key == "menuPosX") UiState::MenuPosX = ParseConfigFloat(value, UiState::MenuPosX);
    else if (key == "menuPosY") UiState::MenuPosY = ParseConfigFloat(value, UiState::MenuPosY);
    else if (key == "fontScale") UiState::FontScale = ParseConfigFloat(value, UiState::FontScale);
    else if (key == "windowAlpha") UiState::WindowAlpha = ParseConfigFloat(value, UiState::WindowAlpha);
    else if (key == "windowRounding") UiState::WindowRounding = ParseConfigFloat(value, UiState::WindowRounding);
    else if (key == "childRounding") UiState::ChildRounding = ParseConfigFloat(value, UiState::ChildRounding);
    else if (key == "frameRounding") UiState::FrameRounding = ParseConfigFloat(value, UiState::FrameRounding);
    else if (key == "popupRounding") UiState::PopupRounding = ParseConfigFloat(value, UiState::PopupRounding);
    else if (key == "scrollbarRounding") UiState::ScrollbarRounding = ParseConfigFloat(value, UiState::ScrollbarRounding);
    else if (key == "grabRounding") UiState::GrabRounding = ParseConfigFloat(value, UiState::GrabRounding);
    else if (key == "tabRounding") UiState::TabRounding = ParseConfigFloat(value, UiState::TabRounding);
    else if (key == "scrollbarSize") UiState::ScrollbarSize = ParseConfigFloat(value, UiState::ScrollbarSize);
    else if (key == "windowBorderSize") UiState::WindowBorderSize = ParseConfigFloat(value, UiState::WindowBorderSize);
    else if (key == "frameBorderSize") UiState::FrameBorderSize = ParseConfigFloat(value, UiState::FrameBorderSize);
    else if (key == "framePaddingX") UiState::FramePaddingX = ParseConfigFloat(value, UiState::FramePaddingX);
    else if (key == "framePaddingY") UiState::FramePaddingY = ParseConfigFloat(value, UiState::FramePaddingY);
    else if (key == "itemSpacingX") UiState::ItemSpacingX = ParseConfigFloat(value, UiState::ItemSpacingX);
    else if (key == "itemSpacingY") UiState::ItemSpacingY = ParseConfigFloat(value, UiState::ItemSpacingY);
    else if (key == "indentSpacing") UiState::IndentSpacing = ParseConfigFloat(value, UiState::IndentSpacing);
    else if (key == "combatInvisibleScout") FeatureState::CombatInvisibleScout = ParseConfigBool(value, FeatureState::CombatInvisibleScout);
    else if (key == "shopBuyFreeHero") FeatureState::ShopBuyFreeHero = ParseConfigBool(value, FeatureState::ShopBuyFreeHero);
    else if (key == "shopBuySelectedHero") FeatureState::ShopBuySelectedHero = ParseConfigBool(value, FeatureState::ShopBuySelectedHero);
    else if (key == "shopBuyRecommendLineup") FeatureState::ShopBuyRecommendLineup = ParseConfigBool(value, FeatureState::ShopBuyRecommendLineup);
    else if (key == "shopForceScavengerExpensiveHero") FeatureState::ShopForceScavengerExpensiveHero = ParseConfigBool(value, FeatureState::ShopForceScavengerExpensiveHero);
    else if (key == "shopRefresh") FeatureState::ShopRefresh = ParseConfigBool(value, FeatureState::ShopRefresh);
    else if (key == "shopStopRefreshAtFreeHero") FeatureState::ShopStopRefreshAtFreeHero = ParseConfigBool(value, FeatureState::ShopStopRefreshAtFreeHero);
    else if (key == "shopStopRefreshAtSelectedHero") FeatureState::ShopStopRefreshAtSelectedHero = ParseConfigBool(value, FeatureState::ShopStopRefreshAtSelectedHero);
    else if (key == "shopStopRefreshAtRecommendLineup") FeatureState::ShopStopRefreshAtRecommendLineup = ParseConfigBool(value, FeatureState::ShopStopRefreshAtRecommendLineup);
    else if (key == "shopKeepGold") FeatureState::ShopKeepGold = ParseConfigBool(value, FeatureState::ShopKeepGold);
    else if (key == "shopKeepGoldAt") FeatureState::ShopKeepGoldAt = ParseConfigInt(value, FeatureState::ShopKeepGoldAt);
    else if (key == "shopRecommendTargetCount") FeatureState::ShopRecommendTargetCount = ParseConfigInt(value, FeatureState::ShopRecommendTargetCount);
    else if (key == "shopRecommendLineupTargets") LoadRecommendLineupTargetCounts(value);
    else if (key == "shopSelectedHeroes") LoadShopSelectedHeroes(value);
    else if (key == "arenaHeroStar") FeatureState::ArenaHeroStar = ParseConfigInt(value, FeatureState::ArenaHeroStar);
    else if (key == "arenaItemEnhanced") FeatureState::ArenaItemEnhanced = ParseConfigBool(value, FeatureState::ArenaItemEnhanced);
    else if (key == "arenaGogoCardEnabled") FeatureState::ArenaGogoCardEnabled = ParseConfigBool(value, FeatureState::ArenaGogoCardEnabled);
    else if (key == "arenaGogoCardSelected1") FeatureState::ArenaGogoCardSelected1 = ParseConfigInt(value, FeatureState::ArenaGogoCardSelected1);
    else if (key == "arenaGogoCardSelected2") FeatureState::ArenaGogoCardSelected2 = ParseConfigInt(value, FeatureState::ArenaGogoCardSelected2);
    else if (key == "arenaForceActiveSynergy") FeatureState::ArenaForceActiveSynergy = ParseConfigBool(value, FeatureState::ArenaForceActiveSynergy);
    else if (key == "arenaForceLevel99") FeatureState::ArenaForceLevel99 = ParseConfigBool(value, FeatureState::ArenaForceLevel99);
    else if (key == "arenaOutsideMapPlacement") FeatureState::ArenaOutsideMapPlacement = ParseConfigBool(value, FeatureState::ArenaOutsideMapPlacement);
    else if (key == "arenaAllEnemyHpOne") FeatureState::ArenaAllEnemyHpOne = ParseConfigBool(value, FeatureState::ArenaAllEnemyHpOne);
    else if (key == "arenaForceCompleteAchievements") FeatureState::ArenaForceCompleteAchievements = ParseConfigBool(value, FeatureState::ArenaForceCompleteAchievements);
    else if (key == "arenaPrice") FeatureState::ArenaPrice = ParseConfigInt(value, FeatureState::ArenaPrice);
    else if (key == "arenaSkipRound") FeatureState::ArenaSkipRound = ParseConfigBool(value, FeatureState::ArenaSkipRound);
    else if (key == "arenaSkipTargetRound") FeatureState::ArenaSkipTargetRound = ParseConfigInt(value, FeatureState::ArenaSkipTargetRound);
    else if (key == "arenaSpeedHack") FeatureState::ArenaSpeedHack = ParseConfigBool(value, FeatureState::ArenaSpeedHack);
    else if (key == "arenaTimeScale") FeatureState::ArenaTimeScale = ParseConfigFloat(value, FeatureState::ArenaTimeScale);
}

// Saves config to file using the project config format.
bool SaveConfigToFile(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex::UiMutex);
    EnsureConfigPathInitialized();

    if (!EnsureParentDirectory(path)) {
        SetConfigStatus("Create directory", path, false);
        return false;
    }

    FILE* file = fopen(path.c_str(), "w");
    if (!file) {
        UiState::ConfigStatus = "Save failed: ";
        UiState::ConfigStatus += path;
        UiState::ConfigStatus += " (";
        UiState::ConfigStatus += strerror(errno);
        UiState::ConfigStatus += ")";
        return false;
    }

    fprintf(file, "# MCGG runtime configuration\n");
    WriteConfigInt(file, "themeIndex", UiState::ThemeIndex);
    WriteConfigInt(file, "fontIndex", UiState::FontIndex);
    WriteConfigInt(file, "languageIndex", UiState::LanguageIndex);
    WriteConfigBool(file, "shopShowSelectedOnly", UiState::ShopShowSelectedOnly);
    WriteConfigBool(file, "showNextEnemyHud", UiState::ShowNextEnemyHud);
    WriteConfigBool(file, "moveFromTitleBarOnly", UiState::MoveFromTitleBarOnly);
    WriteConfigBool(file, "resizeFromEdges", UiState::ResizeFromEdges);
    WriteConfigBool(file, "useFixedMenuPosition", UiState::UseFixedMenuPosition);
    WriteConfigFloat(file, "menuWidth", UiState::MenuWidth);
    WriteConfigFloat(file, "menuHeight", UiState::MenuHeight);
    WriteConfigFloat(file, "menuPosX", UiState::MenuPosX);
    WriteConfigFloat(file, "menuPosY", UiState::MenuPosY);
    WriteConfigFloat(file, "fontScale", UiState::FontScale);
    WriteConfigFloat(file, "windowAlpha", UiState::WindowAlpha);
    WriteConfigFloat(file, "windowRounding", UiState::WindowRounding);
    WriteConfigFloat(file, "childRounding", UiState::ChildRounding);
    WriteConfigFloat(file, "frameRounding", UiState::FrameRounding);
    WriteConfigFloat(file, "popupRounding", UiState::PopupRounding);
    WriteConfigFloat(file, "scrollbarRounding", UiState::ScrollbarRounding);
    WriteConfigFloat(file, "grabRounding", UiState::GrabRounding);
    WriteConfigFloat(file, "tabRounding", UiState::TabRounding);
    WriteConfigFloat(file, "scrollbarSize", UiState::ScrollbarSize);
    WriteConfigFloat(file, "windowBorderSize", UiState::WindowBorderSize);
    WriteConfigFloat(file, "frameBorderSize", UiState::FrameBorderSize);
    WriteConfigFloat(file, "framePaddingX", UiState::FramePaddingX);
    WriteConfigFloat(file, "framePaddingY", UiState::FramePaddingY);
    WriteConfigFloat(file, "itemSpacingX", UiState::ItemSpacingX);
    WriteConfigFloat(file, "itemSpacingY", UiState::ItemSpacingY);
    WriteConfigFloat(file, "indentSpacing", UiState::IndentSpacing);
    WriteConfigBool(file, "combatInvisibleScout", FeatureState::CombatInvisibleScout);
    WriteConfigBool(file, "shopBuyFreeHero", FeatureState::ShopBuyFreeHero);
    WriteConfigBool(file, "shopBuySelectedHero", FeatureState::ShopBuySelectedHero);
    WriteConfigBool(file, "shopBuyRecommendLineup", FeatureState::ShopBuyRecommendLineup);
    WriteConfigBool(
        file,
        "shopForceScavengerExpensiveHero",
        FeatureState::ShopForceScavengerExpensiveHero
    );
    WriteConfigBool(file, "shopRefresh", FeatureState::ShopRefresh);
    WriteConfigBool(file, "shopStopRefreshAtFreeHero", FeatureState::ShopStopRefreshAtFreeHero);
    WriteConfigBool(file, "shopStopRefreshAtSelectedHero", FeatureState::ShopStopRefreshAtSelectedHero);
    WriteConfigBool(
        file,
        "shopStopRefreshAtRecommendLineup",
        FeatureState::ShopStopRefreshAtRecommendLineup
    );
    WriteConfigBool(file, "shopKeepGold", FeatureState::ShopKeepGold);
    WriteConfigInt(file, "shopKeepGoldAt", FeatureState::ShopKeepGoldAt);
    WriteConfigInt(file, "shopRecommendTargetCount", GetRecommendLineupTargetCount());
    WriteConfigString(file, "shopRecommendLineupTargets", FormatRecommendLineupTargetCounts());
    WriteConfigString(file, "shopSelectedHeroes", FormatShopSelectedHeroes());
    WriteConfigInt(file, "arenaHeroStar", FeatureState::ArenaHeroStar);
    WriteConfigBool(file, "arenaItemEnhanced", FeatureState::ArenaItemEnhanced);
    WriteConfigBool(file, "arenaGogoCardEnabled", FeatureState::ArenaGogoCardEnabled);
    WriteConfigInt(file, "arenaGogoCardSelected1", FeatureState::ArenaGogoCardSelected1);
    WriteConfigInt(file, "arenaGogoCardSelected2", FeatureState::ArenaGogoCardSelected2);
    WriteConfigBool(file, "arenaForceActiveSynergy", FeatureState::ArenaForceActiveSynergy);
    WriteConfigBool(file, "arenaForceLevel99", FeatureState::ArenaForceLevel99);
    WriteConfigBool(file, "arenaOutsideMapPlacement", FeatureState::ArenaOutsideMapPlacement);
    WriteConfigBool(file, "arenaAllEnemyHpOne", FeatureState::ArenaAllEnemyHpOne);
    WriteConfigBool(
        file,
        "arenaForceCompleteAchievements",
        FeatureState::ArenaForceCompleteAchievements
    );
    WriteConfigInt(file, "arenaPrice", FeatureState::ArenaPrice);
    WriteConfigBool(file, "arenaSkipRound", FeatureState::ArenaSkipRound);
    WriteConfigInt(file, "arenaSkipTargetRound", FeatureState::ArenaSkipTargetRound);
    WriteConfigBool(file, "arenaSpeedHack", FeatureState::ArenaSpeedHack);
    WriteConfigFloat(file, "arenaTimeScale", FeatureState::ArenaTimeScale);

    fclose(file);
    SetConfigStatus("Saved", path, true);
    return true;
}

// Loads config from file and falls back cleanly if unavailable.
bool LoadConfigFromFile(const std::string& path, bool updateStatus) {
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex::UiMutex);
    EnsureConfigPathInitialized();

    FILE* file = fopen(path.c_str(), "r");
    if (!file) {
        if (updateStatus) {
            UiState::ConfigStatus = "Load failed: ";
            UiState::ConfigStatus += path;
            UiState::ConfigStatus += " (";
            UiState::ConfigStatus += strerror(errno);
            UiState::ConfigStatus += ")";
        }
        return false;
    }

    char line[8192];
    while (fgets(line, sizeof(line), file)) {
        std::string row = TrimString(line);

        if (row.empty() || row[0] == '#') {
            continue;
        }

        size_t separator = row.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        std::string key = TrimString(row.substr(0, separator));
        std::string value = TrimString(row.substr(separator + 1));
        ApplyConfigValue(key, value);
    }

    fclose(file);
    ClampConfigurableState();
    AppearanceState::AppliedThemeIndex = -1;

    if (updateStatus) {
        SetConfigStatus("Loaded", path, true);
    }

    return true;
}

struct Issue707ThemePalette {
    unsigned int text;
    unsigned int disabledText;
    unsigned int windowBg;
    unsigned int panelBg;
    unsigned int panelHover;
    unsigned int accent;
    unsigned int accentHovered;
    unsigned int accentActive;
    unsigned int border;
    unsigned int checkMark;
    float windowAlpha;
    float panelAlpha;
    float popupAlpha;
};

// Applies issue707 palette theme to the live runtime when bindings are ready.
void ApplyIssue707PaletteTheme(int themeIndex) {
    static const Issue707ThemePalette palettes[] = {
        {0xE6E6E6, 0x999999, 0x171724, 0x33334D, 0x4D4D73, 0x6F80B3, 0x899CDD, 0xA8B8FF, 0x6E6E80, 0xA8B8FF, 1.00f, 0.72f, 0.94f},
        {0xFFFFFF, 0x666666, 0x0F0F0F, 0x294A7A, 0x3D659D, 0x4385D1, 0x5AA2F0, 0x72B7FF, 0xFFFFFF, 0x72B7FF, 0.94f, 0.54f, 0.94f},
        {0x4F403D, 0x8F807C, 0xF0F0F0, 0xE5E5F5, 0xD4D4F2, 0x7676C9, 0x5B5BB8, 0x4444A0, 0x808080, 0x333399, 1.00f, 0.75f, 0.98f},
        {0xBBBBBB, 0x585858, 0x3C3F41, 0x2B2B2B, 0x4E5254, 0x6897BB, 0x7FB6E0, 0xA3CCF5, 0x555555, 0x6A8759, 0.98f, 0.76f, 0.96f},
        {0xFFFFFF, 0x808080, 0x0F0F0F, 0x272727, 0x404040, 0x707070, 0x8A8A8A, 0xA0A0A0, 0x6E6E80, 0xFFB000, 0.94f, 0.74f, 0.94f},
        {0xDBEDE3, 0x91A096, 0x333845, 0x3B3345, 0x74324D, 0x801341, 0x9A1A52, 0xB82063, 0x74324D, 0xDBEDE3, 1.00f, 0.78f, 0.96f},
        {0x000000, 0x6B6B6B, 0xF3F6F0, 0xDFEBDD, 0xC6DEC5, 0x5CA66C, 0x67BF77, 0x4A8F58, 0xB5C5B1, 0x2F7D44, 1.00f, 0.88f, 0.98f},
        {0xFFFFFF, 0x8C8C8C, 0x232323, 0x333333, 0x454545, 0xD98B32, 0xF0A64D, 0xFFBE73, 0x555555, 0xF2A13B, 1.00f, 0.78f, 0.96f},
        {0xFFFFFF, 0x8C8C8C, 0x2F3136, 0x3F4147, 0x555861, 0xA0A7B5, 0xB7C0D0, 0xD0D7E5, 0x707070, 0xD0D7E5, 1.00f, 0.80f, 0.98f},
        {0xF2F5FA, 0x5C6B78, 0x1C262B, 0x263039, 0x30404C, 0x73C9E6, 0x8EE2FF, 0xB3ECFF, 0x2E3D47, 0x73C9E6, 1.00f, 0.82f, 0.96f},
        {0xFFFFFF, 0x808080, 0x4A5742, 0x3D4533, 0x68785A, 0xBFC9A8, 0xD6E3BE, 0xF0F7D8, 0xA0A090, 0xC9D4A8, 1.00f, 0.86f, 0.98f},
        {0xEBEBEB, 0x707070, 0x0F0F0F, 0x1F1A0B, 0x332A10, 0xC58D2A, 0xE0A63A, 0xF5C15A, 0x6B5A2A, 0xF5C15A, 1.00f, 0.74f, 0.96f},
        {0xF1F1F1, 0x6A6A6A, 0x252526, 0x333337, 0x525255, 0x007ACC, 0x1C97EA, 0x35A6F2, 0x3F3F46, 0x007ACC, 1.00f, 0.82f, 0.98f},
        {0xFFFFFF, 0x808080, 0x222426, 0x2A2D30, 0x393D42, 0x00A6ED, 0x20BAFF, 0x4DC8FF, 0x44484C, 0x00A6ED, 1.00f, 0.84f, 0.98f},
        {0xFFFFFF, 0x808080, 0x0F0F0F, 0x262626, 0x303A3F, 0x1EA896, 0x35D0B8, 0x65E8D5, 0x6E6E80, 0x1EA896, 0.94f, 0.74f, 0.94f},
        {0xBFBFBF, 0x595959, 0x000000, 0x1A1A1A, 0x352020, 0xC74949, 0xE05A5A, 0xFF7474, 0x2F2F35, 0xE05A5A, 0.94f, 0.74f, 0.94f},
        {0xFFFFFF, 0x808080, 0x1A1A1A, 0x242424, 0x333333, 0x4D77C9, 0x668FE0, 0x80A8F5, 0x333333, 0x5DADE2, 1.00f, 0.78f, 0.96f},
        {0xF8F8F2, 0x6272A4, 0x1A1A21, 0x282A36, 0x383A4A, 0xBD93F9, 0xD6ACFF, 0xFF79C6, 0x705E9C, 0x50FA7B, 1.00f, 0.82f, 0.98f},
        {0xE6E6E6, 0x999999, 0x120505, 0x241C1C, 0x5C3535, 0x8F2D2D, 0xB84444, 0xD95A5A, 0x808080, 0xD95A5A, 0.92f, 0.72f, 0.94f}
    };
    static_assert(
        kAppearanceThemeCount ==
            kIssue707ThemeOffset +
                static_cast<int>(sizeof(palettes) / sizeof(palettes[0]))
    );

    const int paletteIndex = themeIndex - kIssue707ThemeOffset;
    if (paletteIndex < 0 ||
        paletteIndex >= static_cast<int>(sizeof(palettes) / sizeof(palettes[0]))) {
        ImGui::StyleColorsDark();
        return;
    }

    const Issue707ThemePalette& palette = palettes[paletteIndex];
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = HexColor(palette.text);
    colors[ImGuiCol_TextDisabled] = HexColor(palette.disabledText);
    colors[ImGuiCol_WindowBg] = HexColor(palette.windowBg, palette.windowAlpha);
    colors[ImGuiCol_ChildBg] = HexColor(palette.panelBg, palette.panelAlpha * 0.55f);
    colors[ImGuiCol_PopupBg] = HexColor(palette.panelBg, palette.popupAlpha);
    colors[ImGuiCol_Border] = HexColor(palette.border, 0.62f);
    colors[ImGuiCol_BorderShadow] = HexColor(0x000000, 0.0f);
    colors[ImGuiCol_FrameBg] = HexColor(palette.panelBg, palette.panelAlpha);
    colors[ImGuiCol_FrameBgHovered] = HexColor(palette.panelHover, 0.88f);
    colors[ImGuiCol_FrameBgActive] = HexColor(palette.accent, 0.72f);
    colors[ImGuiCol_TitleBg] = HexColor(palette.windowBg, 1.0f);
    colors[ImGuiCol_TitleBgActive] = HexColor(palette.panelBg, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = HexColor(palette.windowBg, 0.82f);
    colors[ImGuiCol_MenuBarBg] = HexColor(palette.panelBg, 0.88f);
    colors[ImGuiCol_ScrollbarBg] = HexColor(palette.windowBg, 0.70f);
    colors[ImGuiCol_ScrollbarGrab] = HexColor(palette.panelHover, 0.72f);
    colors[ImGuiCol_ScrollbarGrabHovered] = HexColor(palette.accent, 0.68f);
    colors[ImGuiCol_ScrollbarGrabActive] = HexColor(palette.accentActive, 0.90f);
    colors[ImGuiCol_CheckMark] = HexColor(palette.checkMark);
    colors[ImGuiCol_SliderGrab] = HexColor(palette.accentHovered, 0.90f);
    colors[ImGuiCol_SliderGrabActive] = HexColor(palette.accentActive);
    colors[ImGuiCol_Button] = HexColor(palette.panelBg, 0.72f);
    colors[ImGuiCol_ButtonHovered] = HexColor(palette.accent, 0.48f);
    colors[ImGuiCol_ButtonActive] = HexColor(palette.accentActive, 0.74f);
    colors[ImGuiCol_Header] = HexColor(palette.accent, 0.30f);
    colors[ImGuiCol_HeaderHovered] = HexColor(palette.accentHovered, 0.46f);
    colors[ImGuiCol_HeaderActive] = HexColor(palette.accentActive, 0.62f);
    colors[ImGuiCol_Separator] = HexColor(palette.border, 0.70f);
    colors[ImGuiCol_SeparatorHovered] = HexColor(palette.accentHovered, 0.78f);
    colors[ImGuiCol_SeparatorActive] = HexColor(palette.accentActive);
    colors[ImGuiCol_ResizeGrip] = HexColor(palette.accent, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = HexColor(palette.accentHovered, 0.58f);
    colors[ImGuiCol_ResizeGripActive] = HexColor(palette.accentActive, 0.88f);
    colors[ImGuiCol_Tab] = HexColor(palette.panelBg, 0.72f);
    colors[ImGuiCol_TabHovered] = HexColor(palette.accent, 0.50f);
    colors[ImGuiCol_TabActive] = HexColor(palette.panelHover, 0.86f);
    colors[ImGuiCol_TabUnfocused] = HexColor(palette.windowBg, 0.78f);
    colors[ImGuiCol_TabUnfocusedActive] = HexColor(palette.panelBg, 0.78f);
    colors[ImGuiCol_PlotLines] = HexColor(palette.accentHovered);
    colors[ImGuiCol_PlotLinesHovered] = HexColor(palette.accentActive);
    colors[ImGuiCol_PlotHistogram] = HexColor(palette.accent);
    colors[ImGuiCol_PlotHistogramHovered] = HexColor(palette.accentActive);
    colors[ImGuiCol_TableHeaderBg] = HexColor(palette.panelBg, 0.92f);
    colors[ImGuiCol_TableBorderStrong] = HexColor(palette.border, 0.80f);
    colors[ImGuiCol_TableBorderLight] = HexColor(palette.border, 0.45f);
    colors[ImGuiCol_TableRowBg] = HexColor(0x000000, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = HexColor(palette.panelHover, 0.22f);
    colors[ImGuiCol_TextSelectedBg] = HexColor(palette.accent, 0.35f);
    colors[ImGuiCol_DragDropTarget] = HexColor(palette.accentActive);
    colors[ImGuiCol_NavHighlight] = HexColor(palette.accentHovered);
    colors[ImGuiCol_NavWindowingHighlight] = HexColor(palette.text, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = HexColor(0x000000, 0.55f);
    colors[ImGuiCol_ModalWindowDimBg] = HexColor(0x000000, 0.55f);

    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
}

// Applies catppuccin mocha theme to the live runtime when bindings are ready.
void ApplyCatppuccinMochaTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 rosewater = HexColor(0xF5E0DC);
    const ImVec4 flamingo = HexColor(0xF2CDCD);
    const ImVec4 mauve = HexColor(0xCBA6F7);
    const ImVec4 red = HexColor(0xF38BA8);
    const ImVec4 peach = HexColor(0xFAB387);
    const ImVec4 yellow = HexColor(0xF9E2AF);
    const ImVec4 green = HexColor(0xA6E3A1);
    const ImVec4 teal = HexColor(0x94E2D5);
    const ImVec4 blue = HexColor(0x89B4FA);
    const ImVec4 lavender = HexColor(0xB4BEFE);
    const ImVec4 text = HexColor(0xCDD6F4);
    const ImVec4 subtext = HexColor(0xBAC2DE);
    const ImVec4 overlay = HexColor(0x6C7086);
    const ImVec4 surface0 = HexColor(0x313244);
    const ImVec4 surface1 = HexColor(0x45475A);
    const ImVec4 surface2 = HexColor(0x585B70);
    const ImVec4 base = HexColor(0x1E1E2E);
    const ImVec4 mantle = HexColor(0x181825);
    const ImVec4 crust = HexColor(0x11111B);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = overlay;
    colors[ImGuiCol_WindowBg] = base;
    colors[ImGuiCol_ChildBg] = HexColor(0x000000, 0.0f);
    colors[ImGuiCol_PopupBg] = mantle;
    colors[ImGuiCol_Border] = surface1;
    colors[ImGuiCol_BorderShadow] = HexColor(0x000000, 0.0f);
    colors[ImGuiCol_FrameBg] = surface0;
    colors[ImGuiCol_FrameBgHovered] = surface1;
    colors[ImGuiCol_FrameBgActive] = surface2;
    colors[ImGuiCol_TitleBg] = crust;
    colors[ImGuiCol_TitleBgActive] = mantle;
    colors[ImGuiCol_TitleBgCollapsed] = crust;
    colors[ImGuiCol_MenuBarBg] = mantle;
    colors[ImGuiCol_ScrollbarBg] = mantle;
    colors[ImGuiCol_ScrollbarGrab] = surface1;
    colors[ImGuiCol_ScrollbarGrabHovered] = surface2;
    colors[ImGuiCol_ScrollbarGrabActive] = overlay;
    colors[ImGuiCol_CheckMark] = green;
    colors[ImGuiCol_SliderGrab] = blue;
    colors[ImGuiCol_SliderGrabActive] = lavender;
    colors[ImGuiCol_Button] = surface0;
    colors[ImGuiCol_ButtonHovered] = surface1;
    colors[ImGuiCol_ButtonActive] = surface2;
    colors[ImGuiCol_Header] = HexColor(0x89B4FA, 0.24f);
    colors[ImGuiCol_HeaderHovered] = HexColor(0x89B4FA, 0.36f);
    colors[ImGuiCol_HeaderActive] = HexColor(0x89B4FA, 0.48f);
    colors[ImGuiCol_Separator] = surface1;
    colors[ImGuiCol_SeparatorHovered] = blue;
    colors[ImGuiCol_SeparatorActive] = lavender;
    colors[ImGuiCol_ResizeGrip] = HexColor(0x89B4FA, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = HexColor(0x89B4FA, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = lavender;
    colors[ImGuiCol_Tab] = mantle;
    colors[ImGuiCol_TabHovered] = surface1;
    colors[ImGuiCol_TabActive] = surface0;
    colors[ImGuiCol_TabUnfocused] = crust;
    colors[ImGuiCol_TabUnfocusedActive] = mantle;
    colors[ImGuiCol_PlotLines] = blue;
    colors[ImGuiCol_PlotLinesHovered] = teal;
    colors[ImGuiCol_PlotHistogram] = peach;
    colors[ImGuiCol_PlotHistogramHovered] = yellow;
    colors[ImGuiCol_TableHeaderBg] = mantle;
    colors[ImGuiCol_TableBorderStrong] = surface2;
    colors[ImGuiCol_TableBorderLight] = surface0;
    colors[ImGuiCol_TableRowBg] = HexColor(0x000000, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = HexColor(0x313244, 0.35f);
    colors[ImGuiCol_TextSelectedBg] = HexColor(0x89B4FA, 0.35f);
    colors[ImGuiCol_DragDropTarget] = yellow;
    colors[ImGuiCol_NavHighlight] = blue;
    colors[ImGuiCol_NavWindowingHighlight] = rosewater;
    colors[ImGuiCol_NavWindowingDimBg] = HexColor(0x11111B, 0.65f);
    colors[ImGuiCol_ModalWindowDimBg] = HexColor(0x11111B, 0.65f);

    style.WindowRounding = 7.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);

    (void)flamingo;
    (void)mauve;
    (void)red;
    (void)subtext;
}

// Applies selected theme to the live runtime when bindings are ready.
void ApplySelectedTheme() {
    int themeIndex = std::clamp(UiState::ThemeIndex.load(), 0, kAppearanceThemeCount - 1);
    if (AppearanceState::AppliedThemeIndex == themeIndex) {
        return;
    }

    UiState::ThemeIndex = themeIndex;

    if (themeIndex == 1) {
        ApplyCatppuccinMochaTheme();
    } else if (themeIndex >= kIssue707ThemeOffset) {
        ApplyIssue707PaletteTheme(themeIndex);
    } else {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    }

    AppearanceState::AppliedThemeIndex = themeIndex;
}

// Applies user style settings to the live runtime when bindings are ready.
void ApplyUserStyleSettings() {
    ClampConfigurableState();

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = UiState::FontScale;
    io.ConfigWindowsMoveFromTitleBarOnly = UiState::MoveFromTitleBarOnly;
    io.ConfigWindowsResizeFromEdges = UiState::ResizeFromEdges;

    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = UiState::WindowAlpha;
    style.WindowRounding = UiState::WindowRounding;
    style.ChildRounding = UiState::ChildRounding;
    style.FrameRounding = UiState::FrameRounding;
    style.PopupRounding = UiState::PopupRounding;
    style.ScrollbarRounding = UiState::ScrollbarRounding;
    style.GrabRounding = UiState::GrabRounding;
    style.TabRounding = UiState::TabRounding;
    style.ScrollbarSize = UiState::ScrollbarSize;
    style.WindowBorderSize = UiState::WindowBorderSize;
    style.FrameBorderSize = UiState::FrameBorderSize;
    style.FramePadding = ImVec2(UiState::FramePaddingX, UiState::FramePaddingY);
    style.ItemSpacing = ImVec2(UiState::ItemSpacingX, UiState::ItemSpacingY);
    style.IndentSpacing = UiState::IndentSpacing;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
}

// Applies selected font to the live runtime when bindings are ready.
void ApplySelectedFont() {
    if (UiState::FontIndex == 1 && !AppearanceState::NotoCjkFont) {
        UiState::FontIndex = 0;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFont* selectedFont =
        UiState::FontIndex == 1 && AppearanceState::NotoCjkFont ?
            AppearanceState::NotoCjkFont :
            AppearanceState::DefaultFont;

    if (!selectedFont) {
        return;
    }

    io.FontDefault = selectedFont;
    AppearanceState::AppliedFontIndex = UiState::FontIndex;
}

// Loads appearance fonts and falls back cleanly if unavailable.
void LoadAppearanceFonts() {
    ImGuiIO& io = ImGui::GetIO();
    AppearanceState::DefaultFont = io.Fonts->AddFontDefault();

    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    fontConfig.PixelSnapH = true;

    static const ImWchar fullRanges[] = {
        0x0001, 0x10FFFF,
        0
    };

    static const unsigned char notoCjkFontData[] = {
        #embed "fonts/Noto Sans CJK Regular.otf"
    };

    AppearanceState::NotoCjkFont = io.Fonts->AddFontFromMemoryTTF(
        (void*)notoCjkFontData,
        sizeof(notoCjkFontData),
        20.0f,
        &fontConfig,
        fullRanges
    );

    if (!AppearanceState::NotoCjkFont) {
        UiState::FontIndex = 0;
    }

    io.Fonts->Build();
}

// Applies appearance to the live runtime when bindings are ready.
void ApplyAppearance() {
    ApplySelectedTheme();
    ApplySelectedFont();
    ApplyUserStyleSettings();
}

// Formats field bool for readable overlay output.
std::string FormatFieldBool(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return FormatBool(GetField<bool>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field int for readable overlay output.
std::string FormatFieldInt(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return FormatInt(GetField<int>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field u int32 for readable overlay output.
std::string FormatFieldUInt32(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return FormatUInt32(GetField<uint32_t>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field u int64 for readable overlay output.
std::string FormatFieldUInt64(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return FormatUInt64(GetField<uint64_t>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field float for readable overlay output.
std::string FormatFieldFloat(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return FormatFloat(GetField<float>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field pointer for readable overlay output.
std::string FormatFieldPointer(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return FormatPointer(GetField<void*>(reinterpret_cast<Il2CppObject*>(instance), field));
}

bool HasShopSelectBinding();
bool HasShopAutomationBindings();
bool HasShopRefreshBindings();
bool HasShopRecommendLineupBindings();
bool HasShopScavengerBindings();
bool HasShopDiagnosticBindings();
bool HasCombatPowerBindings();
bool HasArenaHeroBindings();
bool HasArenaItemBindings();
bool HasArenaGogoCardBindings();
bool HasArenaGoldBindings();
bool HasArenaRoundSkipBindings();
bool HasArenaSpeedHackBindings();
bool HasArenaAchievementBindings();
bool HasBattleTestBindings();
std::string GetBattlePlayerName(uint64_t accountId);

struct PlayerInfoRow {
    uint64_t accountId = 0;
    bool isSelf = false;
    bool isBot = false;
    std::string playerName;
    std::string sortName;
    std::string enemyName;
};

struct GgcQualityRow {
    int round = 0;
    int quality = -1;
};

namespace UiCache {
    std::vector<PlayerInfoRow> InfoPlayerRows;
    bool InfoPlayersReady = false;
    std::chrono::steady_clock::time_point LastInfoPlayerRefresh{};
    bool GgcInfoReady = false;
    std::vector<GgcQualityRow> GgcQualityRows;
    std::chrono::steady_clock::time_point LastGgcInfoRefresh{};
    std::string NextEnemyHudText;
    std::chrono::steady_clock::time_point LastNextEnemyHudRefresh{};
    ImVec2 MenuWindowPos{};
    ImVec2 MenuWindowSize{};
}

// Normalizes display name for stable sorting and comparison.
std::string NormalizeDisplayName(const std::string& value) {
    std::string output = value;
    std::transform(
        output.begin(),
        output.end(),
        output.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );
    return output;
}

// Reads the original RoomData bot flag for an account when battle player data is available.
bool IsBattlePlayerBot(uint64_t accountId) {
    if (!IsIl2CppRuntimeReady() ||
        accountId == 0 ||
        !Originals::MCLogicBattleData_ILOGIC_GetStPlayerData) {
        return false;
    }

    static FieldInfo* roomDataField = nullptr;
    static FieldInfo* roomDataBotField = nullptr;

    if (!roomDataField) {
        roomDataField = GetFieldInfoFromName("Battle", "MCFightPlayerData", "_RoomData");
    }

    if (!roomDataBotField) {
        roomDataBotField = GetFieldInfoFromName("SystemData", "RoomData", "bRobot");
    }

    if (!roomDataField || !roomDataBotField || !TryConsumeManagedWorkUnits()) {
        return false;
    }

    void* fightPlayerData =
        Originals::MCLogicBattleData_ILOGIC_GetStPlayerData(nullptr, accountId);
    void* roomData = fightPlayerData ?
        GetField<void*>(reinterpret_cast<Il2CppObject*>(fightPlayerData), roomDataField) :
        nullptr;

    return roomData &&
        GetField<bool>(reinterpret_cast<Il2CppObject*>(roomData), roomDataBotField);
}

// Builds the Info tab player label with local and bot suffixes.
std::string FormatInfoPlayerName(const PlayerInfoRow& row) {
    std::string playerDisplay = row.playerName.empty() ? "-" : row.playerName;

    if (row.isSelf) {
        playerDisplay += " (Self)";
    }

    if (row.isBot) {
        playerDisplay += " (Bot)";
    }

    return playerDisplay;
}

// Checks whether a GGC quality value is one shown by the overlay.
bool IsKnownGgcQuality(int quality) {
    return quality >= 1 && quality <= 3;
}

// Refreshes ggc info on its throttled runtime cadence.
void RefreshGgcInfo(bool force = false) {
    if (!IsIl2CppRuntimeReady() ||
        !Originals::MCLogicBattleData_ILOGIC_GetCrystalQualityByRound) {
        UiCache::GgcInfoReady = false;
        UiCache::GgcQualityRows.clear();
        return;
    }

    if (!force &&
        !IntervalElapsed(UiCache::LastGgcInfoRefresh, RuntimeConfig::GgcInfoRefreshMs)) {
        return;
    }

    uint64_t selfAccountId = FeatureState::LastSelfAccountId.load();
    if (selfAccountId == 0) {
        selfAccountId = GetSelfAccountId();
    }

    if (selfAccountId == 0) {
        UiCache::GgcInfoReady = false;
        UiCache::GgcQualityRows.clear();
        return;
    }

    std::vector<GgcQualityRow> rows;
    rows.reserve(8);

    for (int round = RuntimeConfig::GgcRoundScanStart;
         round <= RuntimeConfig::GgcRoundScanEnd;
         ++round) {
        if (!TryConsumeManagedWorkUnits()) {
            break;
        }

        int quality = Originals::MCLogicBattleData_ILOGIC_GetCrystalQualityByRound(
            nullptr,
            selfAccountId,
            round
        );

        if (IsKnownGgcQuality(quality)) {
            rows.push_back({round, quality});
        }
    }

    UiCache::GgcQualityRows = std::move(rows);
    UiCache::GgcInfoReady = true;
}

// Refreshes info player rows on its throttled runtime cadence.
void RefreshInfoPlayerRows(bool force = false) {
    if (!IsIl2CppRuntimeReady()) {
        UiCache::InfoPlayerRows.clear();
        UiCache::InfoPlayersReady = false;
        return;
    }

    if (!force &&
        !IntervalElapsed(UiCache::LastInfoPlayerRefresh, 500)) {
        return;
    }

    UiCache::InfoPlayerRows.clear();
    UiCache::InfoPlayersReady = false;

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr ||
        !Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ||
        !Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName) {
        return;
    }

    if (!TryConsumeManagedWorkUnits()) {
        return;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit)) {
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();
    std::unordered_map<uint64_t, std::string> playerNameCache;
    playerNameCache.reserve(static_cast<size_t>(std::max(entryLimit, 0) * 2));
    UiCache::InfoPlayerRows.reserve(static_cast<size_t>(std::max(entryLimit, 0)));

    auto getPlayerName = [&playerNameCache](uint64_t accountId) -> const std::string& {
        auto cached = playerNameCache.find(accountId);
        if (cached != playerNameCache.end()) {
            return cached->second;
        }

        std::string playerName;

        if (accountId != 0 && Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName) {
            playerName = ManagedStringToStd(
                Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName(nullptr, accountId)
            );
        }

        auto inserted = playerNameCache.emplace(accountId, std::move(playerName));
        return inserted.first->second;
    };

    for (int i = 0; entries && i < entryLimit; ++i) {
        const auto& entry = entries[i];

        if (entry.hashCode < 0 || entry.key == 0) {
            continue;
        }

        if (!TryConsumeManagedWorkUnits(3)) {
            break;
        }

        uint64_t accountId = entry.key;
        uint64_t enemyId = Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
            nullptr,
            accountId
        );
        std::string playerName = getPlayerName(accountId);
        std::string enemyName = enemyId != 0 ? getPlayerName(enemyId) : "";
        bool isBot = IsBattlePlayerBot(accountId);

        UiCache::InfoPlayerRows.push_back({
            accountId,
            selfAccountId != 0 && accountId == selfAccountId,
            isBot,
            playerName,
            NormalizeDisplayName(playerName),
            enemyName
        });
    }

    std::sort(
        UiCache::InfoPlayerRows.begin(),
        UiCache::InfoPlayerRows.end(),
        [](const PlayerInfoRow& left, const PlayerInfoRow& right) {
            if (left.isSelf != right.isSelf) {
                return left.isSelf;
            }

            if (left.sortName != right.sortName) {
                return left.sortName < right.sortName;
            }

            return left.accountId < right.accountId;
        }
    );

    UiCache::InfoPlayersReady = true;
}

// Draws the runtime status overlay section without changing game state.
void DrawRuntimeStatus() {
    if (!DrawMenuCollapsingHeader("Runtime Status", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    char selfIdText[32];
    uint64_t selfAccountId = FeatureState::LastSelfAccountId.load();
    snprintf(selfIdText, sizeof(selfIdText), "%llu", (unsigned long long)selfAccountId);

    char tableText[96];
    TableCacheCounts tableCounts = GetTableCacheCounts();
    snprintf(
        tableText,
        sizeof(tableText),
        "%d heroes / %d items / %d cards / %d synergies",
        tableCounts.heroes,
        tableCounts.equips,
        tableCounts.cards,
        tableCounts.relations
    );
    UpdateCheckSnapshot updateSnapshot = GetUpdateCheckSnapshot();

    if (ImGui::BeginTable(
        "##RuntimeStatusTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        ImGui::TableSetupColumn(MenuText("Runtime"));
        ImGui::TableSetupColumn(MenuText("State"), ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableHeadersRow();

        DrawValueRow("Self account", selfIdText);
        DrawValueRow("Table cache", tableText);
        DrawValueRow("Update check", UpdateStatusLabel(updateSnapshot.status));
        DrawStatusRow("IL2CPP", IsIl2CppRuntimeReady());
        DrawStatusRow(
            "Battle data",
            Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr &&
                Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID &&
                Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName
        );
        DrawStatusRow("GGC", Originals::MCLogicBattleData_ILOGIC_GetCrystalQualityByRound);
        DrawStatusRow("Shop select", HasShopSelectBinding());
        DrawStatusRow("Shop automation", HasShopAutomationBindings());
        DrawStatusRow("Recommend lineup", HasShopRecommendLineupBindings());
        DrawStatusRow("Shop Scavenger", HasShopScavengerBindings());
        DrawStatusRow("Shop refresh panel", HasShopRefreshBindings());
        DrawStatusRow("Shop diagnostics", HasShopDiagnosticBindings());
        DrawStatusRow("Battle power", HasCombatPowerBindings());
        DrawStatusRow("Arena heroes", HasArenaHeroBindings());
        DrawStatusRow("Arena items", HasArenaItemBindings());
        DrawStatusRow("Arena GogoCards", HasArenaGogoCardBindings());
        DrawStatusRow("Arena gold", HasArenaGoldBindings());
        DrawStatusRow("Arena round skip", HasArenaRoundSkipBindings());
        DrawStatusRow("Arena speedhack", HasArenaSpeedHackBindings());
        DrawStatusRow("Arena achievements", HasArenaAchievementBindings());
        DrawStatusRow("Battle tests", HasBattleTestBindings());
        DrawStatusRow("Spectator hook", Originals::MCShowSpectatorComp_SetSpectate);
        DrawStatusRow(
            "Synergy hooks",
            Originals::MCBondUtil_CheckRelationActive_Config &&
                Originals::MCBondUtil_CheckRelationActive_Special
        );
        DrawStatusRow(
            "Placement hooks",
            Originals::ShowBattleTouchMgr_ClampGridPos &&
                Originals::AStarTileMap_ValidPos &&
                Originals::MCLogicEntityMap_CanWalkable &&
                Originals::MCLogicEntityMap_IsWalkableAround
        );

        ImGui::EndTable();
    }
}

// Draws the waiting text overlay section without changing game state.
void DrawWaitingText(const char* message) {
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "%s", MenuText(message));
}

// Draws the atomic checkbox overlay section without changing game state.
bool DrawAtomicCheckbox(const char* label, std::atomic<bool>& value) {
    bool current = value.load();
    std::string localized = MenuLabel(label);

    if (!ImGui::Checkbox(localized.c_str(), &current)) {
        DrawMenuTooltip(label);
        return false;
    }

    DrawMenuTooltip(label);
    value = current;
    return true;
}

// Draws a combo box backed by an atomic integer without exposing unlocked UI state.
bool DrawAtomicCombo(
    const char* label,
    std::atomic<int>& value,
    const char* const items[],
    int itemsCount
) {
    int current = std::clamp(value.load(), 0, itemsCount > 0 ? itemsCount - 1 : 0);
    std::string localized = MenuLabel(label);
    bool changed = false;

    if (itemsCount <= 0) {
        return false;
    }

    bool opened = ImGui::BeginCombo(localized.c_str(), MenuText(items[current]));
    DrawMenuTooltip(label);
    if (!opened) {
        return false;
    }

    for (int i = 0; i < itemsCount; ++i) {
        bool selected = current == i;
        ImGui::PushID(i);
        if (ImGui::Selectable(MenuText(items[i]), selected)) {
            current = i;
            changed = true;
        }
        DrawMenuTooltip(items[i]);
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
        ImGui::PopID();
    }

    ImGui::EndCombo();

    if (!changed) {
        return false;
    }

    value = current;
    return true;
}

// Draws an integer input backed by an atomic value and publishes only committed edits.
bool DrawAtomicInputInt(
    const char* label,
    std::atomic<int>& value,
    int step = 1,
    int stepFast = 100,
    ImGuiInputTextFlags flags = 0
) {
    int current = value.load();
    std::string localized = MenuLabel(label);

    if (!ImGui::InputInt(localized.c_str(), &current, step, stepFast, flags)) {
        DrawMenuTooltip(label);
        return false;
    }

    DrawMenuTooltip(label);
    value = current;
    return true;
}

// Draws a float slider backed by an atomic value and publishes only committed edits.
bool DrawAtomicSliderFloat(
    const char* label,
    std::atomic<float>& value,
    float minValue,
    float maxValue,
    const char* format
) {
    float current = value.load();
    std::string localized = MenuLabel(label);

    if (!ImGui::SliderFloat(localized.c_str(), &current, minValue, maxValue, format)) {
        DrawMenuTooltip(label);
        return false;
    }

    DrawMenuTooltip(label);
    value = current;
    return true;
}

// Draws a float input backed by an atomic value and publishes only committed edits.
bool DrawAtomicInputFloat(
    const char* label,
    std::atomic<float>& value,
    float step,
    float stepFast,
    const char* format
) {
    float current = value.load();
    std::string localized = MenuLabel(label);

    if (!ImGui::InputFloat(localized.c_str(), &current, step, stepFast, format)) {
        DrawMenuTooltip(label);
        return false;
    }

    DrawMenuTooltip(label);
    value = current;
    return true;
}

// Checks the shop select binding condition before work proceeds.
bool HasShopSelectBinding() {
    return IsIl2CppRuntimeReady() &&
        ((FeatureState::HeroShopItemList &&
            Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero) ||
         (FeatureState::HeroShopPanel &&
         Originals::UIPanelBattleHeroShop_KeyBoardShopSelect) ||
         (FeatureState::HeroShopPanel &&
          Originals::UIPanelBattleHeroShop_BuyHero));
}

// Checks the shop automation bindings condition before work proceeds.
bool HasShopAutomationBindings() {
    bool needsHeroCount =
        FeatureState::ShopBuySelectedHero ||
        FeatureState::ShopStopRefreshAtSelectedHero ||
        FeatureState::ShopBuyRecommendLineup ||
        FeatureState::ShopStopRefreshAtRecommendLineup ||
        FeatureState::ShopRefresh;
    bool needsRecommendLineup =
        FeatureState::ShopBuyRecommendLineup ||
        FeatureState::ShopStopRefreshAtRecommendLineup;
    bool needsScavenger = FeatureState::ShopForceScavengerExpensiveHero;

    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetShopItemData &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin &&
        HasShopSelectBinding() &&
        (!needsHeroCount || Originals::MCLogicBattleData_ILogic_HeroOwnCount) &&
        (!needsRecommendLineup || HasShopRecommendLineupBindings()) &&
        (!needsScavenger || HasShopScavengerBindings());
}

// Checks the shop refresh bindings condition before work proceeds.
bool HasShopRefreshBindings() {
    return IsIl2CppRuntimeReady() &&
        FeatureState::HeroShopPanel &&
        Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop;
}

// Checks the shop recommend lineup bindings condition before work proceeds.
bool HasShopRecommendLineupBindings() {
    return IsIl2CppRuntimeReady() &&
        (Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup ||
         (FeatureState::BattleBridge &&
          Originals::MCBattleBridge_IsHeroInRecommendLineup));
}

// Checks the shop Scavenger bindings condition before work proceeds.
bool HasShopScavengerBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCBattleBridge_OnRefreshShop &&
        Originals::MCBondUtil_GetBondActiveCount &&
        Originals::CData_RelationSkillTip_MC_GetInstance &&
        Originals::CData_RelationSkillTip_MC_GetAll &&
        Originals::MCLogicBattleData_ILOGIC_GetShopItemData &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin &&
        HasShopSelectBinding();
}

// Checks the shop diagnostic bindings condition before work proceeds.
bool HasShopDiagnosticBindings() {
    return IsIl2CppRuntimeReady() &&
        (Originals::MCLogicBattleData_ILOGIC_GetShopIsForbid ||
         Originals::MCLogicBattleData_ILOGIC_IsRefreshFree ||
         Originals::MCLogicBattleData_ILOGIC_GetShopStarLv ||
         Originals::MCLogicBattleData_ILOGIC_GetShopRuleBuyTimes ||
         Originals::MCLogicBattleData_GetFreeFreshShopCount ||
         Originals::MCLogicBattleData_ILOGIC_GetCurRefreshShopLevel ||
         Originals::MCLogicBattleData_ILOGIC_GetHeroItemCount ||
         Originals::MCLogicBattleData_ILOGIC_GetHeroSlotDict_Count);
}

// Checks the combat power bindings condition before work proceeds.
bool HasCombatPowerBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleManager_get_m_bDefendFaild &&
        Originals::MCLogicBattleManager_OnModifyPlayerBlood &&
        Originals::MCLogicBattleManager_OnFightOver &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerData;
}

// Checks the arena hero bindings condition before work proceeds.
bool HasArenaHeroBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleManager_BuyNormalHero &&
        (Originals::MCComp_GetGamer || FeatureState::LastSelfAccountId.load() != 0);
}

// Checks the arena item bindings condition before work proceeds.
bool HasArenaItemBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCEquipUtil_OnGetNewEquip != nullptr;
}

// Checks the arena gogo card bindings condition before work proceeds.
bool HasArenaGogoCardBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCComp_GetGoGoCardComp != nullptr;
}

// Checks the arena gold bindings condition before work proceeds.
bool HasArenaGoldBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerData &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin &&
        Originals::MCChessPlayerData_UpdateCoin;
}

// Checks the arena round skip bindings condition before work proceeds.
bool HasArenaRoundSkipBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
        Originals::MCLogicBattleData_get_logicRoundMgr &&
        Originals::LogicRoundMgr_SetRound &&
        Originals::LogicRoundMgr_NextRound;
}

// Checks the arena speed hack bindings condition before work proceeds.
bool HasArenaSpeedHackBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::UnityEngine_Time_set_timeScale;
}

// Checks the arena achievement force-complete bindings condition before work proceeds.
bool HasArenaAchievementBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_GetResult &&
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData &&
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation &&
        Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition &&
        Originals::MCLogicAchievementRecordComp_AchievementRoundData_GetResult &&
        Originals::MCLogicAchievementRecordComp_AchievementRoundData_RefreshData;
}

// Checks the battle test bindings condition before work proceeds.
bool HasBattleTestBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime &&
        Originals::MCLogicBattleData_ILOGIC_IsFightSection &&
        Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP &&
        Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID &&
        Originals::MCLogicBattleManager_get_m_bDefendFaild;
}

// Draws the ggc info overlay section without changing game state.
void DrawGgcInfo() {
    DrawMenuSeparatorText("GGC");

    if (!UiCache::GgcInfoReady) {
        ImGui::TextUnformatted("Waiting for GGC data");
        return;
    }

    if (UiCache::GgcQualityRows.empty()) {
        ImGui::TextUnformatted("No GGC qualities detected");
        return;
    }

    if (!ImGui::BeginTable(
        "##GgcQualityTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 160.0f)
    )) {
        return;
    }

    ImGui::TableSetupColumn(MenuText("Round"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn(MenuText("Quality"));
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(UiCache::GgcQualityRows.size()));
    while (clipper.Step()) {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
            const GgcQualityRow& row = UiCache::GgcQualityRows[rowIndex];

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", row.round);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(
                GgcQualityColor(row.quality),
                "%s (%d)",
                MenuText(GgcQualityName(row.quality)),
                row.quality
            );
        }
    }

    ImGui::EndTable();
}

// Draws the info players table overlay section without changing game state.
void DrawInfoPlayersTable() {
    DrawMenuSeparatorText("Players");

    if (!IsIl2CppRuntimeReady()) {
        DrawWaitingText("Waiting for IL2CPP runtime");
        return;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr ||
        !Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ||
        !Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName) {
        DrawWaitingText("Waiting for battle data");
        return;
    }

    if (!UiCache::InfoPlayersReady) {
        DrawWaitingText("Waiting for player list");
        return;
    }

    if (UiCache::InfoPlayerRows.empty()) {
        ImGui::TextUnformatted("No players found");
        return;
    }

    if (!ImGui::BeginTable(
        "##InfoPlayersTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 260.0f)
    )) {
        return;
    }

    ImGui::TableSetupColumn(MenuText("Player"));
    ImGui::TableSetupColumn(MenuText("Current enemy"));
    ImGui::TableHeadersRow();

    for (const PlayerInfoRow& row : UiCache::InfoPlayerRows) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        std::string playerDisplay = FormatInfoPlayerName(row);
        ImGui::TextUnformatted(playerDisplay.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.enemyName.empty() ? "-" : row.enemyName.c_str());
    }

    ImGui::EndTable();
}

// Draws the info tab overlay section without changing game state.
void DrawInfoTab() {
    if (!IsIl2CppRuntimeReady()) {
        DrawWaitingText("Waiting for IL2CPP runtime");
        ImGui::Spacing();
    }

    DrawGgcInfo();
    ImGui::Spacing();
    DrawInfoPlayersTable();
    ImGui::Spacing();
    DrawOpponentPredictionSection(FeatureState::LastSelfAccountId.load());
}

// Draws the combat tab overlay section without changing game state.
void DrawCombatTab() {
    if (!Originals::MCShowSpectatorComp_SetSpectate) {
        DrawWaitingText("Waiting for spectator hook");
    }

    DrawAtomicCheckbox(
        "Invisible Scout - hide spectate switching",
        FeatureState::CombatInvisibleScout
    );
}

// Draws the appearance tab overlay section without changing game state.
void DrawAppearanceTab() {
    DrawMenuSeparatorText("Theme");

    ImGui::SetNextItemWidth(220.0f);
    if (DrawAtomicCombo(
            "Theme",
            UiState::ThemeIndex,
            kAppearanceThemes,
            kAppearanceThemeCount
        )) {
        UiState::ThemeIndex =
            std::clamp(UiState::ThemeIndex.load(), 0, kAppearanceThemeCount - 1);
        ApplyAppearance();
    }

    DrawMenuSeparatorText("Font");

    const char* fonts[] = {
        "Default",
        "Noto Sans CJK"
    };

    ImGui::SetNextItemWidth(220.0f);
    if (DrawAtomicCombo("Font", UiState::FontIndex, fonts, IM_ARRAYSIZE(fonts))) {
        if (UiState::FontIndex.load() == 1 && !AppearanceState::NotoCjkFont) {
            UiState::FontIndex = 0;
        }

        UiState::FontIndex =
            std::clamp(UiState::FontIndex.load(), 0, IM_ARRAYSIZE(fonts) - 1);
        ApplyAppearance();
    }

    if (!AppearanceState::NotoCjkFont) {
        DrawWaitingText("Waiting for Noto Sans CJK font");
    }

    DrawMenuSeparatorText("Language");

    ImGui::SetNextItemWidth(220.0f);
    if (DrawAtomicCombo(
            "Language",
            UiState::LanguageIndex,
            kMenuLanguages,
            kMenuLanguageCount
        )) {
        UiState::LanguageIndex =
            std::clamp(UiState::LanguageIndex.load(), 0, kMenuLanguageCount - 1);
    }
}

// Draws the settings tab overlay section without changing game state.
void DrawSettingsTab() {
    EnsureConfigPathInitialized();

    if (ImGui::BeginTabBar("##SettingsTabBar")) {
        if (BeginMenuTabItem("Config")) {
            ImGui::SetNextItemWidth(-1.0f);
            std::string configPathHint = MenuText("Configuration file path");
            ImGui::InputTextWithHint(
                "##ConfigPath",
                configPathHint.c_str(),
                &UiState::ConfigPath
            );
            DrawMenuTooltip("Configuration file path");

            if (DrawMenuButton("Save configuration")) {
                SaveConfigToFile(UiState::ConfigPath);
            }

            ImGui::SameLine();
            if (DrawMenuButton("Load configuration")) {
                if (LoadConfigFromFile(UiState::ConfigPath, true)) {
                    ApplyAppearance();
                }
            }

            ImGui::SameLine();
            if (DrawMenuButton("Reset visuals")) {
                ResetVisualSettings();
                ApplyAppearance();
                UiState::ConfigStatus = "Visual settings reset";
            }

            if (!UiState::ConfigStatus.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", UiState::ConfigStatus.c_str());
            }

            ImGui::Spacing();
            ImGui::TextUnformatted(
                MenuText(
                    "Saved state includes visual settings, window and HUD settings, and Combat, Shop, and Arena controls."
                )
            );

            DrawUpdateSettingsSection();

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Window")) {
            bool changed = false;

            changed |= DrawAtomicSliderFloat("Menu width", UiState::MenuWidth, 360.0f, 1600.0f, "%.0f");
            changed |= DrawAtomicSliderFloat("Menu height", UiState::MenuHeight, 280.0f, 1200.0f, "%.0f");
            changed |= DrawAtomicCheckbox("Use fixed menu position", UiState::UseFixedMenuPosition);

            ImGui::BeginDisabled(!UiState::UseFixedMenuPosition.load());
            changed |= DrawAtomicInputFloat("Menu position X", UiState::MenuPosX, 1.0f, 20.0f, "%.0f");
            changed |= DrawAtomicInputFloat("Menu position Y", UiState::MenuPosY, 1.0f, 20.0f, "%.0f");
            ImGui::EndDisabled();

            if (DrawMenuButton("Capture current menu size")) {
                ImVec2 size = UiCache::MenuWindowSize;
                UiState::MenuWidth = size.x;
                UiState::MenuHeight = size.y;
                changed = true;
            }

            ImGui::SameLine();
            if (DrawMenuButton("Capture current position")) {
                ImVec2 pos = UiCache::MenuWindowPos;
                UiState::MenuPosX = pos.x;
                UiState::MenuPosY = pos.y;
                UiState::UseFixedMenuPosition = true;
                changed = true;
            }

            DrawMenuSeparatorText("Behavior");
            changed |= DrawAtomicCheckbox("Show next enemy HUD", UiState::ShowNextEnemyHud);
            changed |= DrawAtomicCheckbox("Move from title bar only", UiState::MoveFromTitleBarOnly);
            changed |= DrawAtomicCheckbox("Resize from edges", UiState::ResizeFromEdges);

            if (changed) {
                ApplyAppearance();
            }

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Style")) {
            bool changed = false;

            DrawMenuSeparatorText("Typography");
            changed |= DrawAtomicSliderFloat("Font size scale", UiState::FontScale, 0.65f, 2.0f, "%.2fx");

            DrawMenuSeparatorText("Window");
            changed |= DrawAtomicSliderFloat("Window opacity", UiState::WindowAlpha, 0.35f, 1.0f, "%.2f");
            changed |= DrawAtomicSliderFloat("Window border", UiState::WindowBorderSize, 0.0f, 4.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Frame border", UiState::FrameBorderSize, 0.0f, 4.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Scrollbar size", UiState::ScrollbarSize, 8.0f, 32.0f, "%.0f");

            DrawMenuSeparatorText("Rounding");
            changed |= DrawAtomicSliderFloat("Window rounding", UiState::WindowRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Child rounding", UiState::ChildRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Frame rounding", UiState::FrameRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Popup rounding", UiState::PopupRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Scrollbar rounding", UiState::ScrollbarRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Grab rounding", UiState::GrabRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Tab rounding", UiState::TabRounding, 0.0f, 20.0f, "%.1f");

            DrawMenuSeparatorText("Spacing");
            changed |= DrawAtomicSliderFloat("Frame padding X", UiState::FramePaddingX, 0.0f, 24.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Frame padding Y", UiState::FramePaddingY, 0.0f, 24.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Item spacing X", UiState::ItemSpacingX, 0.0f, 32.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Item spacing Y", UiState::ItemSpacingY, 0.0f, 32.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Indent spacing", UiState::IndentSpacing, 0.0f, 48.0f, "%.1f");

            if (changed) {
                ApplyAppearance();
            }

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("State")) {
            if (DrawMenuButton("Reset feature state")) {
                ResetFeatureSettings();
                UiState::ConfigStatus = "Feature state reset";
            }

            ImGui::SameLine();
            if (DrawMenuButton("Clear shop hero targets")) {
                ClearShopHeroTargets();
                UiState::ConfigStatus = "Shop hero targets cleared";
            }

            ImGui::Spacing();
            ImGui::Text(
                "Tracked shop heroes: %d",
                GetTrackedShopHeroTargetCount()
            );
            ImGui::Text(
                "Selected GogoCards: %d / %d",
                FeatureState::ArenaGogoCardSelected1.load(),
                FeatureState::ArenaGogoCardSelected2.load()
            );

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

// Returns the cached or live battle player name value used by runtime features.
std::string GetBattlePlayerName(uint64_t accountId) {
    if (!IsIl2CppRuntimeReady() ||
        accountId == 0 ||
        !Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName) {
        return {};
    }

    if (!TryConsumeManagedWorkUnits()) {
        return {};
    }

    return ManagedStringToStd(
        Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName(nullptr, accountId)
    );
}

// Returns the cached or live round result data field value used by runtime features.
std::string GetRoundResultDataField(void* battleManager, const char* fieldName) {
    if (!battleManager) {
        return "Waiting";
    }

    static FieldInfo* roundResultDataField = nullptr;

    if (!roundResultDataField) {
        roundResultDataField =
            GetFieldInfoFromName("", "MCLogicBattleManager", "m_RoundResultData");
    }

    if (roundResultDataField && !TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    void* roundResultData = roundResultDataField ?
        GetField<void*>(
            reinterpret_cast<Il2CppObject*>(battleManager),
            roundResultDataField
        ) :
        nullptr;

    if (!roundResultData) {
        return "Waiting";
    }

    static std::unordered_map<std::string, FieldInfo*> valueFieldCache;
    static std::mutex valueFieldCacheMutex;
    FieldInfo* valueField = nullptr;

    {
        std::lock_guard<std::mutex> lock(valueFieldCacheMutex);
        auto it = valueFieldCache.find(fieldName);
        if (it != valueFieldCache.end()) {
            valueField = it->second;
        }
    }

    if (!valueField) {
        valueField = GetFieldInfoFromName("", "MCFightRoundResultData", fieldName);
        std::lock_guard<std::mutex> lock(valueFieldCacheMutex);
        valueFieldCache[fieldName] = valueField;
    }

    return FormatFieldInt(roundResultData, valueField);
}

// Draws the test binding rows overlay section without changing game state.
void DrawTestBindingRows() {
    if (!ImGui::BeginTable(
        "##TestBindingTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        return;
    }

    ImGui::TableSetupColumn("Binding");
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableHeadersRow();

    DrawStatusRow("Round/phase", Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime);
    DrawStatusRow("Round manager", HasArenaRoundSkipBindings());
    DrawStatusRow("Unity time scale", HasArenaSpeedHackBindings());
    DrawStatusRow("Fight section", Originals::MCLogicBattleData_ILOGIC_IsFightSection);
    DrawStatusRow("Self fight over", Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver);
    DrawStatusRow("Player HP", Originals::MCLogicBattleData_ILOGIC_GetPlayerHP);
    DrawStatusRow("Player identity", Originals::MCLogicBattleData_ILOGIC_GetBuidByAccID);
    DrawStatusRow("Player rank", Originals::MCLogicBattleData_ILOGIC_GetRank);
    DrawStatusRow("Player level", Originals::MCLogicBattleData_ILOGIC_GetPlayerLevel);
    DrawStatusRow("Population", Originals::MCLogicBattleData_ILOGIC_SelfCurPopulation);
    DrawStatusRow("Shop diagnostics", HasShopDiagnosticBindings());
    DrawStatusRow("Hero counts", Originals::MCLogicBattleData_ILOGIC_GetBattleHeroNum);
    DrawStatusRow("Result history", Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory);
    DrawStatusRow(
        "Recommend hero",
        Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup
    );
    DrawStatusRow("Star-up hero", Originals::MCLogicBattleData_ILOGIC_GetHeroByStarUp);
    DrawStatusRow("Recommend membership", Originals::MCBattleBridge_IsHeroInRecommendLineup);
    DrawStatusRow("Battle bridge UI", Originals::MCBattleBridge_CheckEnableKeyBoard);
    DrawStatusRow("Shop panel state", Originals::UIPanelBattleHeroShop_CanOperate);
    DrawStatusRow("Battle manager flags", Originals::MCLogicBattleManager_get_m_bDefendFaild);
    DrawStatusRow("Alive fighter counts", Originals::MCLogicBattleManager_GetAliveFighter);
    DrawStatusRow("Behavior API", Originals::MCBehaviorThreeApi_Get);

    ImGui::EndTable();
}

// Draws read-only round and phase diagnostics for the Test tab.
void DrawTestRoundRows(
    uint64_t selfAccountId,
    uint64_t targetAccountId,
    uint64_t opponentAccountId
) {
    if (!ImGui::BeginTable(
        "##TestRoundTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        return;
    }

    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableHeadersRow();

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
            TryConsumeManagedWorkUnits() ?
        Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr) :
        0;
    std::string targetName = GetBattlePlayerName(targetAccountId);
    std::string opponentName = GetBattlePlayerName(opponentAccountId);

    DrawValueRow("Self account", selfAccountId ? FormatUInt64(selfAccountId) : "Waiting");
    DrawValueRow("Inspect account", targetAccountId ? FormatUInt64(targetAccountId) : "Waiting");
    DrawValueRow("Inspect name", targetName.empty() ? "-" : targetName);
    DrawValueRow("Opponent account", opponentAccountId ? FormatUInt64(opponentAccountId) : "Waiting");
    DrawValueRow("Opponent name", opponentName.empty() ? "-" : opponentName);
    DrawValueRow(
        "Game round",
        Originals::MCLogicBattleData_ILOGIC_GetGameRound ? FormatInt(round) : "Waiting"
    );
    DrawValueRow(
        "Game phase",
        Originals::MCLogicBattleData_ILOGIC_GetGamePhase &&
                TryConsumeManagedWorkUnits() ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetGamePhase(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Remain time",
        Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime &&
                TryConsumeManagedWorkUnits() ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Max remain time",
        Originals::MCLogicBattleData_ILOGIC_GetRoundMaxRemainTime &&
                TryConsumeManagedWorkUnits() ?
            FormatUInt64(Originals::MCLogicBattleData_ILOGIC_GetRoundMaxRemainTime(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Is fight section",
        Originals::MCLogicBattleData_ILOGIC_IsFightSection &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleData_ILOGIC_IsFightSection(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Is result section",
        Originals::MCLogicBattleData_ILOGIC_IsFightResultSection &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleData_ILOGIC_IsFightResultSection(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Is self fight over",
        Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Inspect HP",
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP && targetAccountId &&
                TryConsumeManagedWorkUnits() ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetPlayerHP(nullptr, targetAccountId)) :
            "Waiting"
    );
    DrawValueRow(
        "Opponent HP",
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP && opponentAccountId &&
                TryConsumeManagedWorkUnits() ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetPlayerHP(nullptr, opponentAccountId)) :
            "Waiting"
    );
    DrawValueRow(
        "History fail flag",
        Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory &&
                targetAccountId &&
                round > 0 &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory(
                nullptr,
                targetAccountId,
                static_cast<int>(round)
            )) :
            "Waiting"
    );

    ImGui::EndTable();
}

// Formats an account-specific integer reader for Test tab diagnostics.
std::string FormatAccountInt(
    uint64_t accountId,
    int (*reader)(void* instance, uint64_t accountId)
) {
    if (accountId && reader && !TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return accountId && reader ? FormatInt(reader(nullptr, accountId)) : "Waiting";
}

// Formats an account-specific unsigned integer reader for Test tab diagnostics.
std::string FormatAccountUInt32(
    uint64_t accountId,
    uint32_t (*reader)(void* instance, uint64_t accountId)
) {
    if (accountId && reader && !TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return accountId && reader ? FormatUInt32(reader(nullptr, accountId)) : "Waiting";
}

// Formats an account-specific boolean reader for Test tab diagnostics.
std::string FormatAccountBool(
    uint64_t accountId,
    bool (*reader)(void* instance, uint64_t accountId)
) {
    if (accountId && reader && !TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return accountId && reader ? FormatBool(reader(nullptr, accountId)) : "Waiting";
}

// Formats global int for readable overlay output.
std::string FormatGlobalInt(int (*reader)(void* instance)) {
    if (reader && !TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    return reader ? FormatInt(reader(nullptr)) : "Waiting";
}

// Formats shop star level for readable overlay output.
std::string FormatShopStarLevel(uint64_t accountId) {
    if (!accountId || !Originals::MCLogicBattleData_ILOGIC_GetShopStarLv) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits()) {
        return "Waiting";
    }

    AstarInt2 shopStar = Originals::MCLogicBattleData_ILOGIC_GetShopStarLv(
        nullptr,
        accountId
    );
    return FormatInt(shopStar.x) + " / " + FormatInt(shopStar.y);
}

// Draws the test player rows overlay section without changing game state.
void DrawTestPlayerRows(uint64_t targetAccountId) {
    if (!ImGui::BeginTable(
        "##TestPlayerDataTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        return;
    }

    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableHeadersRow();

    int coin = 0;
    bool hasCoin = targetAccountId && Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin;

    if (hasCoin) {
        if (TryConsumeManagedWorkUnits()) {
            coin = Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, targetAccountId);
        } else {
            hasCoin = false;
        }
    }

    DrawValueRow("Account", targetAccountId ? FormatUInt64(targetAccountId) : "Waiting");
    DrawValueRow(
        "BUID",
        FormatAccountUInt32(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetBuidByAccID)
    );
    DrawValueRow(
        "GUID",
        FormatAccountUInt32(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetGuidByAccID)
    );
    DrawValueRow(
        "Chess player GUID",
        FormatAccountUInt32(
            targetAccountId,
            Originals::MCLogicBattleData_ILOGIC_GetChessPlayerGuid
        )
    );
    DrawValueRow(
        "Chess player config",
        FormatAccountInt(
            targetAccountId,
            Originals::MCLogicBattleData_ILOGIC_GetChessPlayerConfigID
        )
    );
    DrawValueRow(
        "Chess skin",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetChessSkinId)
    );
    DrawValueRow(
        "HP",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetPlayerHP)
    );
    DrawValueRow("Gold", hasCoin ? FormatInt(coin) : "Waiting");
    DrawValueRow(
        "Player level",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetPlayerLevel)
    );
    DrawValueRow(
        "Upgrade cost",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetUpgradeCost)
    );
    DrawValueRow(
        "Can upgrade now",
        targetAccountId && hasCoin && Originals::MCLogicBattleData_ILOGIC_CanUpgrade ?
            (TryConsumeManagedWorkUnits() ?
                FormatBool(Originals::MCLogicBattleData_ILOGIC_CanUpgrade(
                    nullptr,
                    targetAccountId,
                    coin
                )) :
                "Waiting") :
            "Waiting"
    );
    DrawValueRow(
        "Rank",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetRank)
    );
    DrawValueRow(
        "Camp rank",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetCampRankByAccId)
    );
    DrawValueRow(
        "Camp alive count",
        FormatAccountInt(
            targetAccountId,
            Originals::MCLogicBattleData_ILGOIC_GetCampAliveCountByAccId
        )
    );
    DrawValueRow(
        "Cup",
        FormatAccountUInt32(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetCup)
    );
    DrawValueRow(
        "Warm value",
        FormatAccountUInt32(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetWarmValue)
    );
    DrawValueRow(
        "Rank level",
        FormatAccountUInt32(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetRankLevel)
    );
    DrawValueRow(
        "Commander level",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetCommanderLv)
    );
    DrawValueRow(
        "Current logic",
        FormatAccountBool(targetAccountId, Originals::MCLogicBattleData_ILOGIC_IsCurrentLogic)
    );
    DrawValueRow(
        "Can pause judge",
        FormatAccountBool(
            targetAccountId,
            Originals::MCLogicBattleData_ILOGIC_IsPlayerCanPauseJudger
        )
    );
    DrawValueRow(
        "Shop forbidden",
        FormatAccountBool(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetShopIsForbid)
    );
    DrawValueRow(
        "Refresh free",
        FormatAccountBool(targetAccountId, Originals::MCLogicBattleData_ILOGIC_IsRefreshFree)
    );
    DrawValueRow("Shop star level", FormatShopStarLevel(targetAccountId));
    DrawValueRow(
        "Shop rule buy times",
        FormatAccountInt(
            targetAccountId,
            Originals::MCLogicBattleData_ILOGIC_GetShopRuleBuyTimes
        )
    );
    DrawValueRow(
        "Free refresh count",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_GetFreeFreshShopCount)
    );
    DrawValueRow(
        "Refresh shop level",
        FormatAccountInt(
            targetAccountId,
            Originals::MCLogicBattleData_ILOGIC_GetCurRefreshShopLevel
        )
    );
    DrawValueRow(
        "Hero item count",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetHeroItemCount)
    );
    DrawValueRow(
        "Hero slot count",
        FormatAccountInt(
            targetAccountId,
            Originals::MCLogicBattleData_ILOGIC_GetHeroSlotDict_Count
        )
    );
    DrawValueRow(
        "Battle hero count",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetBattleHeroNum)
    );
    DrawValueRow(
        "All hero count",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetAllHeroNum)
    );
    DrawValueRow(
        "Battle hero total star",
        FormatAccountInt(
            targetAccountId,
            Originals::MCLogicBattleData_ILOGIC_GetBattleHeroTotalStart
        )
    );
    DrawValueRow(
        "Battle count",
        FormatAccountInt(targetAccountId, Originals::MCLogicBattleData_ILOGIC_GetBattleCount)
    );
    DrawValueRow(
        "Self camp",
        Originals::MCLogicBattleData_ILOGIC_GetSelfCamp &&
                TryConsumeManagedWorkUnits() ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetSelfCamp(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Self population",
        Originals::MCLogicBattleData_ILOGIC_SelfCurPopulation &&
                Originals::MCLogicBattleData_ILOGIC_SelfTotalPopulation &&
                TryConsumeManagedWorkUnits(2) ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_SelfCurPopulation(nullptr)) +
                " / " +
                FormatInt(Originals::MCLogicBattleData_ILOGIC_SelfTotalPopulation(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Spare chess count",
        FormatGlobalInt(Originals::MCLogicBattleData_ILOGIC_GetSpareChessNum)
    );
    DrawValueRow(
        "Star-up recommendation",
        Originals::MCLogicBattleData_ILOGIC_GetHeroByStarUp &&
                TryConsumeManagedWorkUnits() ?
            FormatHeroLabel(Originals::MCLogicBattleData_ILOGIC_GetHeroByStarUp(nullptr)) :
            "Waiting"
    );

    ImGui::EndTable();
}

// Draws the test manager rows overlay section without changing game state.
void DrawTestManagerRows(void* battleManager) {
    static FieldInfo* fightOverField = nullptr;
    static FieldInfo* defendFailedField = nullptr;
    static FieldInfo* reduceHpOverField = nullptr;
    static FieldInfo* hasBattleField = nullptr;
    static FieldInfo* lastRoundWinField = nullptr;
    static FieldInfo* selfFightValueField = nullptr;
    static FieldInfo* killerFightValueField = nullptr;
    static FieldInfo* killSelfAccountField = nullptr;

    if (!fightOverField) {
        fightOverField = GetFieldInfoFromName("", "MCLogicBattleManager", "m_bFightOver");
    }

    if (!defendFailedField) {
        defendFailedField =
            GetFieldInfoFromName("", "MCLogicBattleManager", "<m_bDefendFaild>k__BackingField");
    }

    if (!reduceHpOverField) {
        reduceHpOverField =
            GetFieldInfoFromName("", "MCLogicBattleManager", "m_bReducePlayerHPOver");
    }

    if (!hasBattleField) {
        hasBattleField = GetFieldInfoFromName("", "MCLogicBattleManager", "_hasBattle");
    }

    if (!lastRoundWinField) {
        lastRoundWinField = GetFieldInfoFromName("", "MCLogicBattleManager", "isLastRoundWin");
    }

    if (!selfFightValueField) {
        selfFightValueField = GetFieldInfoFromName("", "MCLogicBattleManager", "_selfFightValue");
    }

    if (!killerFightValueField) {
        killerFightValueField = GetFieldInfoFromName("", "MCLogicBattleManager", "killerFightValue");
    }

    if (!killSelfAccountField) {
        killSelfAccountField =
            GetFieldInfoFromName("", "MCLogicBattleManager", "m_ulKillSelfAccId");
    }

    int campACount = 0;
    int campBCount = 0;
    bool hasAliveCounts = false;

    if (battleManager &&
        Originals::MCLogicBattleManager_GetAliveFighter &&
        TryConsumeManagedWorkUnits(2)) {
        Originals::MCLogicBattleManager_GetAliveFighter(
            battleManager,
            &campACount,
            &campBCount
        );
        hasAliveCounts = true;
    }

    void* currentOpponent = battleManager && Originals::MCLogicBattleManager_GetCurrentOpponent ?
        (TryConsumeManagedWorkUnits() ?
            Originals::MCLogicBattleManager_GetCurrentOpponent(battleManager) :
            nullptr) :
        nullptr;

    if (!ImGui::BeginTable(
        "##TestManagerTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        return;
    }

    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableHeadersRow();

    DrawValueRow("Manager pointer", FormatPointer(battleManager));
    DrawValueRow(
        "Manager account",
        battleManager && Originals::MCLogicBattleManager_get_m_uAccountId &&
                TryConsumeManagedWorkUnits() ?
            FormatUInt64(Originals::MCLogicBattleManager_get_m_uAccountId(battleManager)) :
            "Waiting"
    );
    DrawValueRow(
        "Is host",
        battleManager && Originals::MCLogicBattleManager_get_IsHost &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleManager_get_IsHost(battleManager)) :
            "Waiting"
    );
    DrawValueRow("hasBattle field", FormatFieldBool(battleManager, hasBattleField));
    DrawValueRow("fightOver field", FormatFieldBool(battleManager, fightOverField));
    DrawValueRow(
        "defendFailed getter",
        battleManager && Originals::MCLogicBattleManager_get_m_bDefendFaild &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleManager_get_m_bDefendFaild(battleManager)) :
            "Waiting"
    );
    DrawValueRow("defendFailed field", FormatFieldBool(battleManager, defendFailedField));
    DrawValueRow("reduce HP over", FormatFieldBool(battleManager, reduceHpOverField));
    DrawValueRow("last round win", FormatFieldBool(battleManager, lastRoundWinField));
    DrawValueRow("self fight value", FormatFieldInt(battleManager, selfFightValueField));
    DrawValueRow("killer fight value", FormatFieldInt(battleManager, killerFightValueField));
    DrawValueRow("kill self account", FormatFieldUInt64(battleManager, killSelfAccountField));
    DrawValueRow("round result", GetRoundResultDataField(battleManager, "result"));
    DrawValueRow("round start HP", GetRoundResultDataField(battleManager, "hpOnRoundStart"));
    DrawValueRow("current opponent ptr", FormatPointer(currentOpponent));
    DrawValueRow(
        "alive camp A/B",
        hasAliveCounts ?
            FormatInt(campACount) + " / " + FormatInt(campBCount) :
            "Waiting"
    );
    DrawValueRow(
        "has alive camp A",
        battleManager && Originals::MCLogicBattleManager_HasAliveFighter &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleManager_HasAliveFighter(battleManager, 1)) :
            "Waiting"
    );
    DrawValueRow(
        "has alive camp B",
        battleManager && Originals::MCLogicBattleManager_HasAliveFighter &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCLogicBattleManager_HasAliveFighter(battleManager, 2)) :
            "Waiting"
    );

    ImGui::EndTable();
}

// Draws the test behavior rows overlay section without changing game state.
void DrawTestBehaviorRows(uint64_t targetAccountId) {
    void* behaviorApi = targetAccountId && Originals::MCBehaviorThreeApi_Get ?
        (TryConsumeManagedWorkUnits() ?
            Originals::MCBehaviorThreeApi_Get(targetAccountId) :
            nullptr) :
        nullptr;

    if (!ImGui::BeginTable(
        "##TestBehaviorTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        return;
    }

    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableHeadersRow();

    DrawValueRow("Behavior API pointer", FormatPointer(behaviorApi));
    DrawValueRow(
        "Current battle result",
        behaviorApi && Originals::MCBehaviorThreeApi_GetCurrentBattleRoundResult &&
                TryConsumeManagedWorkUnits() ?
            FormatInt(Originals::MCBehaviorThreeApi_GetCurrentBattleRoundResult(behaviorApi)) :
            "Waiting"
    );
    DrawValueRow(
        "Current phase type",
        behaviorApi && Originals::MCBehaviorThreeApi_GetCurrentPhaseType &&
                TryConsumeManagedWorkUnits() ?
            FormatInt(Originals::MCBehaviorThreeApi_GetCurrentPhaseType(behaviorApi)) :
            "Waiting"
    );

    ImGui::EndTable();
}

// Formats list field count for readable overlay output.
std::string FormatListFieldCount(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits(2)) {
        return "Waiting";
    }

    void* listObject = GetField<void*>(reinterpret_cast<Il2CppObject*>(instance), field);
    if (!listObject) {
        return "Waiting";
    }

    auto* list = reinterpret_cast<MonoStructures::List<void*>*>(listObject);
    void* const* data = nullptr;
    int size = 0;

    return TryGetManagedListData(list, &data, &size) ? FormatInt(size) : "Unreadable";
}

// Formats int dictionary field count for readable overlay output.
std::string FormatIntDictionaryFieldCount(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    if (!TryConsumeManagedWorkUnits(2)) {
        return "Waiting";
    }

    void* dictionaryObject = GetField<void*>(reinterpret_cast<Il2CppObject*>(instance), field);
    if (!dictionaryObject) {
        return "Waiting";
    }

    auto* dictionary =
        reinterpret_cast<MonoStructures::Dictionary<int, int>*>(dictionaryObject);
    const MonoStructures::Dictionary<int, int>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(dictionary, &entries, &entryLimit)) {
        return "Unreadable";
    }

    int activeEntries = 0;
    for (int i = 0; entries && i < entryLimit; ++i) {
        if (entries[i].hashCode >= 0) {
            ++activeEntries;
        }
    }

    return FormatInt(activeEntries);
}

// Draws the test bridge rows overlay section without changing game state.
void DrawTestBridgeRows() {
    void* battleBridge = FeatureState::BattleBridge.load();

    static FieldInfo* debugInfoField = nullptr;
    static FieldInfo* startBattleField = nullptr;
    static FieldInfo* offlineField = nullptr;
    static FieldInfo* heroShopField = nullptr;
    static FieldInfo* recommendLineupField = nullptr;
    static FieldInfo* gogoCardPanelField = nullptr;
    static FieldInfo* selectHeroField = nullptr;
    static FieldInfo* autoOpenDpsField = nullptr;
    static FieldInfo* autoOpenRoundResultField = nullptr;
    static FieldInfo* showChangeEquipTipField = nullptr;
    static FieldInfo* hurtItemsField = nullptr;

    if (!debugInfoField) {
        debugInfoField = GetFieldInfoFromName("", "MCBattleBridge", "debugInfo");
    }

    if (!startBattleField) {
        startBattleField = GetFieldInfoFromName("", "MCBattleBridge", "bStartBattle");
    }

    if (!offlineField) {
        offlineField = GetFieldInfoFromName("", "MCBattleBridge", "bOffline");
    }

    if (!heroShopField) {
        heroShopField = GetFieldInfoFromName("", "MCBattleBridge", "uiPanelBattleHeroShop");
    }

    if (!recommendLineupField) {
        recommendLineupField =
            GetFieldInfoFromName("", "MCBattleBridge", "uiPanelBattleRecommendLineupNew");
    }

    if (!gogoCardPanelField) {
        gogoCardPanelField = GetFieldInfoFromName("", "MCBattleBridge", "uiGOGOCardPanel");
    }

    if (!selectHeroField) {
        selectHeroField = GetFieldInfoFromName("", "MCBattleBridge", "m_selectHero");
    }

    if (!autoOpenDpsField) {
        autoOpenDpsField =
            GetFieldInfoFromName("", "MCBattleBridge", "m_bAutoOpenBeginFightDPSPanel");
    }

    if (!autoOpenRoundResultField) {
        autoOpenRoundResultField =
            GetFieldInfoFromName("", "MCBattleBridge", "m_bAutoOpenRoundResltOverUI");
    }

    if (!showChangeEquipTipField) {
        showChangeEquipTipField =
            GetFieldInfoFromName("", "MCBattleBridge", "m_bShowChangeEquipTip");
    }

    if (!hurtItemsField) {
        hurtItemsField = GetFieldInfoFromName("", "MCBattleBridge", "m_HurtItems");
    }

    if (!ImGui::BeginTable(
        "##TestBridgeTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        return;
    }

    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableHeadersRow();

    DrawValueRow("Battle bridge pointer", FormatPointer(battleBridge));
    DrawValueRow("Debug info pointer", FormatFieldPointer(battleBridge, debugInfoField));
    DrawValueRow("Start battle", FormatFieldBool(battleBridge, startBattleField));
    DrawValueRow("Offline", FormatFieldBool(battleBridge, offlineField));
    DrawValueRow("Hero shop panel", FormatFieldPointer(battleBridge, heroShopField));
    DrawValueRow("Recommend lineup UI", FormatFieldPointer(battleBridge, recommendLineupField));
    DrawValueRow("GogoCard panel", FormatFieldPointer(battleBridge, gogoCardPanelField));
    DrawValueRow("Selected hero GUID", FormatFieldUInt32(battleBridge, selectHeroField));
    DrawValueRow("Auto-open DPS panel", FormatFieldBool(battleBridge, autoOpenDpsField));
    DrawValueRow(
        "Auto-open round result",
        FormatFieldBool(battleBridge, autoOpenRoundResultField)
    );
    DrawValueRow(
        "Show change equip tip",
        FormatFieldBool(battleBridge, showChangeEquipTipField)
    );
    DrawValueRow("Hurt items dictionary", FormatFieldPointer(battleBridge, hurtItemsField));
    DrawValueRow(
        "Super crystal shop open",
        battleBridge && Originals::MCBattleBridge_IsSuperCrystalShopOpen &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCBattleBridge_IsSuperCrystalShopOpen(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "GogoCard panel open",
        battleBridge && Originals::MCBattleBridge_IsGoGoCardPanelOpen &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCBattleBridge_IsGoGoCardPanelOpen(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Keyboard enabled",
        battleBridge && Originals::MCBattleBridge_CheckEnableKeyBoard &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::MCBattleBridge_CheckEnableKeyBoard(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Free memory",
        battleBridge && Originals::MCBattleBridge_GetFreeMemory &&
                TryConsumeManagedWorkUnits() ?
            FormatInt64(Originals::MCBattleBridge_GetFreeMemory(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Ping samples",
        battleBridge && Originals::MCBattleBridge_GetPingTimes &&
                TryConsumeManagedWorkUnits() ?
            FormatUInt32(Originals::MCBattleBridge_GetPingTimes(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Ping stdev",
        battleBridge && Originals::MCBattleBridge_GetStdevPing &&
                TryConsumeManagedWorkUnits() ?
            FormatFloat(Originals::MCBattleBridge_GetStdevPing(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "FPS stdev",
        battleBridge && Originals::MCBattleBridge_GetStdevFps &&
                TryConsumeManagedWorkUnits() ?
            FormatFloat(Originals::MCBattleBridge_GetStdevFps(battleBridge)) :
            "Waiting"
    );

    ImGui::EndTable();
}

// Draws the test shop panel rows overlay section without changing game state.
void DrawTestShopPanelRows() {
    void* heroShopPanel = FeatureState::HeroShopPanel.load();
    void* heroShopItemList = FeatureState::HeroShopItemList.load();

    static FieldInfo* needToShowField = nullptr;
    static FieldInfo* lastSelectHeroIndexField = nullptr;
    static FieldInfo* dictHeroSlotField = nullptr;
    static FieldInfo* sameRefreshHeroField = nullptr;
    static FieldInfo* operationCloseFlagField = nullptr;
    static FieldInfo* activeTimeField = nullptr;
    static FieldInfo* shopTypeField = nullptr;
    static FieldInfo* currentProbabilityLevelField = nullptr;
    static FieldInfo* tempHeroSlotField = nullptr;
    static FieldInfo* tempSameRefreshHeroField = nullptr;
    static FieldInfo* forbidRefreshHeroField = nullptr;
    static FieldInfo* shopStateField = nullptr;
    static FieldInfo* shopStateTimeField = nullptr;
    static FieldInfo* playingEffectField = nullptr;
    static FieldInfo* heroItemListField = nullptr;

    if (!needToShowField) {
        needToShowField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "needToShow");
    }

    if (!lastSelectHeroIndexField) {
        lastSelectHeroIndexField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_lastSelectHeroIndex");
    }

    if (!dictHeroSlotField) {
        dictHeroSlotField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "dictHeroSlot");
    }

    if (!sameRefreshHeroField) {
        sameRefreshHeroField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop", "sameRefreshHero");
    }

    if (!operationCloseFlagField) {
        operationCloseFlagField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_OperationCloseFlag");
    }

    if (!activeTimeField) {
        activeTimeField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_ActiveTime");
    }

    if (!shopTypeField) {
        shopTypeField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_eMCHeroShopType");
    }

    if (!currentProbabilityLevelField) {
        currentProbabilityLevelField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_curShowProLevel");
    }

    if (!tempHeroSlotField) {
        tempHeroSlotField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_TempHeroSlot");
    }

    if (!tempSameRefreshHeroField) {
        tempSameRefreshHeroField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_TempSameRefreshHero");
    }

    if (!forbidRefreshHeroField) {
        forbidRefreshHeroField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_bForbidRefershHero");
    }

    if (!shopStateField) {
        shopStateField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_eShopState");
    }

    if (!shopStateTimeField) {
        shopStateTimeField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_iStateTime");
    }

    if (!playingEffectField) {
        playingEffectField = GetFieldInfoFromName("", "UIPanelBattleHeroShop", "m_playingEffect");
    }

    if (!heroItemListField) {
        heroItemListField =
            GetFieldInfoFromName("", "UIPanelBattleHeroShop_HeroItemList", "heroItemList");
    }

    if (!ImGui::BeginTable(
        "##TestShopPanelTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        return;
    }

    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableHeadersRow();

    DrawValueRow("Shop panel pointer", FormatPointer(heroShopPanel));
    DrawValueRow("Hero item list pointer", FormatPointer(heroShopItemList));
    DrawValueRow("Need to show", FormatFieldBool(heroShopPanel, needToShowField));
    DrawValueRow("Operation close flag", FormatFieldBool(heroShopPanel, operationCloseFlagField));
    DrawValueRow("Active time", FormatFieldFloat(heroShopPanel, activeTimeField));
    DrawValueRow("Last selected index", FormatFieldInt(heroShopPanel, lastSelectHeroIndexField));
    DrawValueRow("Shop type", FormatFieldInt(heroShopPanel, shopTypeField));
    DrawValueRow("Probability level", FormatFieldInt(heroShopPanel, currentProbabilityLevelField));
    DrawValueRow("State", FormatFieldInt(heroShopPanel, shopStateField));
    DrawValueRow("State time", FormatFieldInt(heroShopPanel, shopStateTimeField));
    DrawValueRow("Forbid refresh", FormatFieldBool(heroShopPanel, forbidRefreshHeroField));
    DrawValueRow("Playing effect", FormatFieldBool(heroShopPanel, playingEffectField));
    DrawValueRow("Hero slot entries", FormatIntDictionaryFieldCount(heroShopPanel, dictHeroSlotField));
    DrawValueRow("Temp hero slot entries", FormatIntDictionaryFieldCount(heroShopPanel, tempHeroSlotField));
    DrawValueRow("Same refresh heroes", FormatListFieldCount(heroShopPanel, sameRefreshHeroField));
    DrawValueRow(
        "Temp same refresh heroes",
        FormatListFieldCount(heroShopPanel, tempSameRefreshHeroField)
    );
    DrawValueRow("Visible hero items", FormatListFieldCount(heroShopItemList, heroItemListField));
    DrawValueRow(
        "Last operation time",
        heroShopPanel && Originals::UIPanelBattleHeroShop_get_lastOperationTime &&
                TryConsumeManagedWorkUnits() ?
            FormatUInt32(Originals::UIPanelBattleHeroShop_get_lastOperationTime(heroShopPanel)) :
            "Waiting"
    );
    DrawValueRow(
        "Delay open",
        heroShopPanel && Originals::UIPanelBattleHeroShop_IsDelayOpen &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::UIPanelBattleHeroShop_IsDelayOpen(heroShopPanel)) :
            "Waiting"
    );
    DrawValueRow(
        "Info after spectate",
        heroShopPanel && Originals::UIPanelBattleHeroShop_GetInfoAfterSpectate &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::UIPanelBattleHeroShop_GetInfoAfterSpectate(heroShopPanel)) :
            "Waiting"
    );
    DrawValueRow(
        "Can operate",
        heroShopPanel && Originals::UIPanelBattleHeroShop_CanOperate &&
                TryConsumeManagedWorkUnits() ?
            FormatBool(Originals::UIPanelBattleHeroShop_CanOperate(heroShopPanel, true)) :
            "Waiting"
    );

    ImGui::EndTable();
}

struct PredictionPlayer {
    uint64_t accountId = 0;
    void* manager = nullptr;
    std::string name;
    int hp = 0;
    bool alive = false;
};

struct OpponentPredictionRow {
    uint64_t accountId = 0;
    std::string name;
    int percent = 0;
    double weight = 0.0;
    bool alive = false;
    bool mirror = false;
    bool lockedPercent = false;
    int recentMeetings = 0;
    std::string currentEnemyName;
};

struct OpponentForecastRow {
    int ahead = 0;
    uint32_t round = 0;
    uint64_t opponentId = 0;
    std::string name;
    int confidence = 0;
    std::string source;
};

struct CurrentOpponentObservation {
    uint64_t accountId = 0;
    uint64_t opponentId = 0;
    bool alive = false;
    bool mirror = false;
    bool fromCurrentApi = false;
    bool inferred = false;
};

struct OpponentHistoryEntry {
    uint32_t round = 0;
    uint64_t opponentId = 0;
    bool mirror = false;
    bool fromCurrentApi = false;
};

struct PredictionAccuracyEntry {
    int correct = 0;
    int wrong = 0;
    uint32_t lastRound = 0;
};

struct PendingOpponentForecast {
    uint32_t targetRound = 0;
    uint64_t predictedOpponentId = 0;
};

enum class OpponentCyclePattern {
    Unknown,
    Classic,
    Shifted
};

struct OpponentCyclePrediction {
    uint64_t opponentId = 0;
    OpponentCyclePattern pattern = OpponentCyclePattern::Unknown;
    bool tentative = false;
};

namespace PredictionCache {
    std::unordered_map<uint64_t, CurrentOpponentObservation> CurrentRoundOpponents;
    std::unordered_map<uint64_t, std::vector<OpponentHistoryEntry>> OpponentHistory;
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, PredictionAccuracyEntry>>
        PredictionAccuracy;
    std::unordered_map<uint64_t, std::vector<PendingOpponentForecast>> PendingForecasts;
    std::vector<OpponentPredictionRow> CachedRows;
    std::vector<OpponentForecastRow> CachedForecastRows;
    std::chrono::steady_clock::time_point LastRowsRefresh{};
    uint64_t HistorySelfAccountId = 0;
    uint64_t CachedRowsSelfAccountId = 0;
    bool CachedRowsReady = false;
}

// Collects prediction players with bounded managed reads.
std::vector<PredictionPlayer> CollectPredictionPlayers(uint64_t selfAccountId) {
    std::vector<PredictionPlayer> players;

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        return players;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit)) {
        return players;
    }

    players.reserve(static_cast<size_t>(std::max(entryLimit, 0)));

    for (int i = 0; entries && i < entryLimit; ++i) {
        const auto& entry = entries[i];

        if (entry.hashCode < 0 || entry.key == 0 || entry.key == selfAccountId) {
            continue;
        }

        if (!TryConsumeManagedWorkUnits(3)) {
            break;
        }

        int hp = Originals::MCLogicBattleData_ILOGIC_GetPlayerHP ?
            Originals::MCLogicBattleData_ILOGIC_GetPlayerHP(nullptr, entry.key) :
            1;

        players.push_back({
            entry.key,
            entry.value,
            GetBattlePlayerName(entry.key),
            hp,
            hp > 0
        });
    }

    return players;
}

// Resets opponent prediction history if needed back to safe default values.
void ResetOpponentPredictionHistoryIfNeeded(uint64_t selfAccountId) {
    if (selfAccountId == 0) {
        return;
    }

    if (PredictionCache::HistorySelfAccountId != 0 &&
        PredictionCache::HistorySelfAccountId != selfAccountId) {
        PredictionCache::OpponentHistory.clear();
        PredictionCache::PredictionAccuracy.clear();
        PredictionCache::PendingForecasts.clear();
        PredictionCache::CachedForecastRows.clear();
    }

    PredictionCache::HistorySelfAccountId = selfAccountId;
}

// Records whether a previously cached forecast matched the later live opponent.
void LearnFromForecastOutcome(uint64_t accountId, uint32_t round, uint64_t actualOpponentId) {
    if (accountId == 0 ||
        round == 0 ||
        actualOpponentId == 0 ||
        actualOpponentId == accountId) {
        return;
    }

    auto pendingIt = PredictionCache::PendingForecasts.find(accountId);
    if (pendingIt == PredictionCache::PendingForecasts.end()) {
        return;
    }

    std::vector<PendingOpponentForecast>& forecasts = pendingIt->second;
    bool learned = false;

    for (const PendingOpponentForecast& forecast : forecasts) {
        if (forecast.targetRound != round || forecast.predictedOpponentId == 0) {
            continue;
        }

        PredictionAccuracyEntry& predictedAccuracy =
            PredictionCache::PredictionAccuracy[accountId][forecast.predictedOpponentId];
        predictedAccuracy.lastRound = round;

        if (forecast.predictedOpponentId == actualOpponentId) {
            predictedAccuracy.correct = std::min(predictedAccuracy.correct + 1, 1000);
        } else {
            predictedAccuracy.wrong = std::min(predictedAccuracy.wrong + 1, 1000);
            PredictionAccuracyEntry& actualAccuracy =
                PredictionCache::PredictionAccuracy[accountId][actualOpponentId];
            actualAccuracy.correct = std::min(actualAccuracy.correct + 1, 1000);
            actualAccuracy.lastRound = round;
        }

        learned = true;
    }

    if (!learned) {
        return;
    }

    forecasts.erase(
        std::remove_if(
            forecasts.begin(),
            forecasts.end(),
            [round](const PendingOpponentForecast& forecast) {
                return forecast.targetRound <= round;
            }
        ),
        forecasts.end()
    );

    if (forecasts.empty()) {
        PredictionCache::PendingForecasts.erase(pendingIt);
    }
}

// Stores one predicted future matchup so later observations can adjust weights.
void RememberPendingForecast(
    uint64_t accountId,
    uint32_t targetRound,
    uint64_t predictedOpponentId
) {
    if (accountId == 0 ||
        targetRound == 0 ||
        predictedOpponentId == 0 ||
        predictedOpponentId == accountId) {
        return;
    }

    std::vector<PendingOpponentForecast>& forecasts =
        PredictionCache::PendingForecasts[accountId];

    for (PendingOpponentForecast& forecast : forecasts) {
        if (forecast.targetRound == targetRound) {
            forecast.predictedOpponentId = predictedOpponentId;
            return;
        }
    }

    forecasts.push_back({targetRound, predictedOpponentId});

    if (forecasts.size() > static_cast<size_t>(RuntimeConfig::MaxOpponentForecastRounds * 2)) {
        forecasts.erase(forecasts.begin());
    }
}

// Converts forecast accuracy into a bounded scoring multiplier for a candidate.
double GetPredictionAccuracyMultiplier(uint64_t accountId, uint64_t opponentId) {
    auto accountIt = PredictionCache::PredictionAccuracy.find(accountId);
    if (accountIt == PredictionCache::PredictionAccuracy.end()) {
        return 1.0;
    }

    auto opponentIt = accountIt->second.find(opponentId);
    if (opponentIt == accountIt->second.end()) {
        return 1.0;
    }

    const PredictionAccuracyEntry& accuracy = opponentIt->second;
    int total = accuracy.correct + accuracy.wrong;
    if (total <= 0) {
        return 1.0;
    }

    double smoothedAccuracy =
        (static_cast<double>(accuracy.correct) + 1.0) / (static_cast<double>(total) + 2.0);
    return std::clamp(0.65 + smoothedAccuracy * 0.75, 0.70, 1.35);
}

// Returns the remembered forecast misses for a candidate.
int GetPredictionMistakeCount(uint64_t accountId, uint64_t opponentId) {
    auto accountIt = PredictionCache::PredictionAccuracy.find(accountId);
    if (accountIt == PredictionCache::PredictionAccuracy.end()) {
        return 0;
    }

    auto opponentIt = accountIt->second.find(opponentId);
    if (opponentIt == accountIt->second.end()) {
        return 0;
    }

    return opponentIt->second.wrong;
}

// Stores one round of observed opponent data for prediction history.
void RememberOpponentObservation(
    const CurrentOpponentObservation& observation,
    uint32_t round
) {
    if (round == 0 || observation.accountId == 0 || observation.opponentId == 0) {
        return;
    }

    std::vector<OpponentHistoryEntry>& history =
        PredictionCache::OpponentHistory[observation.accountId];

    if (!history.empty() && history.back().round == round) {
        history.back().opponentId = observation.opponentId;
        history.back().mirror = observation.mirror;
        history.back().fromCurrentApi = observation.fromCurrentApi;
        LearnFromForecastOutcome(observation.accountId, round, observation.opponentId);
        return;
    }

    history.push_back({
        round,
        observation.opponentId,
        observation.mirror,
        observation.fromCurrentApi
    });

    if (history.size() > static_cast<size_t>(RuntimeConfig::MaxOpponentHistoryRounds)) {
        history.erase(history.begin());
    }

    LearnFromForecastOutcome(observation.accountId, round, observation.opponentId);
}

// Finds the latest cached current-opponent observation for one account.
const CurrentOpponentObservation* FindCurrentOpponentObservation(uint64_t accountId) {
    auto found = PredictionCache::CurrentRoundOpponents.find(accountId);
    return found != PredictionCache::CurrentRoundOpponents.end() ? &found->second : nullptr;
}

// Formats observed enemy name for readable overlay output.
std::string FormatObservedEnemyName(const CurrentOpponentObservation& observation) {
    if (observation.opponentId == 0) {
        return {};
    }

    std::string enemyName = GetBattlePlayerName(observation.opponentId);
    if (enemyName.empty()) {
        enemyName = FormatUInt64(observation.opponentId);
    }

    if (observation.mirror) {
        enemyName += " (Mirror)";
    }

    return enemyName;
}

// Counts recent opponent history from bounded cached history.
int CountRecentOpponentHistory(uint64_t accountId, uint64_t opponentId, int maxEntries) {
    if (accountId == 0 || opponentId == 0 || maxEntries <= 0) {
        return 0;
    }

    auto found = PredictionCache::OpponentHistory.find(accountId);
    if (found == PredictionCache::OpponentHistory.end()) {
        return 0;
    }

    const std::vector<OpponentHistoryEntry>& history = found->second;
    int count = 0;
    int inspected = 0;

    for (auto it = history.rbegin(); it != history.rend() && inspected < maxEntries; ++it) {
        ++inspected;
        if (it->opponentId == opponentId) {
            ++count;
        }
    }

    return count;
}

// Finds how many cached real-player meetings ago this opponent appeared.
int FindRecentOpponentDistance(uint64_t accountId, uint64_t opponentId, int maxEntries) {
    if (accountId == 0 || opponentId == 0 || maxEntries <= 0) {
        return -1;
    }

    auto found = PredictionCache::OpponentHistory.find(accountId);
    if (found == PredictionCache::OpponentHistory.end()) {
        return -1;
    }

    const std::vector<OpponentHistoryEntry>& history = found->second;
    int inspected = 0;

    for (auto it = history.rbegin(); it != history.rend() && inspected < maxEntries; ++it) {
        if (it->opponentId == opponentId && !it->mirror) {
            return inspected;
        }

        ++inspected;
    }

    return -1;
}

// Scores recent opponent history so repeated pairings can be deprioritized.
double ScoreRecentOpponentHistory(uint64_t accountId, uint64_t opponentId, int maxEntries) {
    if (accountId == 0 || opponentId == 0 || maxEntries <= 0) {
        return 0.0;
    }

    auto found = PredictionCache::OpponentHistory.find(accountId);
    if (found == PredictionCache::OpponentHistory.end()) {
        return 0.0;
    }

    const std::vector<OpponentHistoryEntry>& history = found->second;
    double score = 0.0;
    int inspected = 0;

    for (auto it = history.rbegin(); it != history.rend() && inspected < maxEntries; ++it) {
        if (it->opponentId == opponentId) {
            double recency = 1.0 - (static_cast<double>(inspected) / maxEntries);
            score += it->mirror ? recency * 0.6 : recency;
        }

        ++inspected;
    }

    return score;
}

// Scores both directions of recent opponent history and keeps the stronger signal.
double ScoreMutualRecentOpponentHistory(uint64_t accountId, uint64_t opponentId, int maxEntries) {
    return std::max(
        ScoreRecentOpponentHistory(accountId, opponentId, maxEntries),
        ScoreRecentOpponentHistory(opponentId, accountId, maxEntries)
    );
}

// Counts real player opponent history from bounded cached history.
int CountRealPlayerOpponentHistory(uint64_t accountId) {
    auto found = PredictionCache::OpponentHistory.find(accountId);
    if (found == PredictionCache::OpponentHistory.end()) {
        return 0;
    }

    int count = 0;
    for (const OpponentHistoryEntry& entry : found->second) {
        if (entry.opponentId != 0 && entry.opponentId != accountId && !entry.mirror) {
            ++count;
        }
    }

    return count;
}

// Builds the recent real-player cycle so the predictor can avoid stale repeats.
std::vector<uint64_t> BuildRecentOpponentCycle(uint64_t accountId, size_t maxOpponents) {
    std::vector<uint64_t> cycle;
    if (accountId == 0 || maxOpponents == 0) {
        return cycle;
    }

    auto found = PredictionCache::OpponentHistory.find(accountId);
    if (found == PredictionCache::OpponentHistory.end()) {
        return cycle;
    }

    cycle.reserve(maxOpponents);
    const std::vector<OpponentHistoryEntry>& history = found->second;

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        uint64_t opponentId = it->opponentId;
        if (opponentId == 0 || opponentId == accountId || it->mirror) {
            continue;
        }

        if (std::find(cycle.begin(), cycle.end(), opponentId) != cycle.end()) {
            continue;
        }

        cycle.push_back(opponentId);
        if (cycle.size() >= maxOpponents) {
            break;
        }
    }

    return cycle;
}

// Reports whether an account already appears in a bounded recent opponent set.
bool ContainsAccountId(const std::vector<uint64_t>& accounts, uint64_t accountId) {
    return std::find(accounts.begin(), accounts.end(), accountId) != accounts.end();
}

// Converts the game round into the seven-round opponent cycle index.
uint32_t GetOpponentCycleEffectiveRound(uint32_t round) {
    return round > 0 ? ((round - 1) % 7) + 1 : 0;
}

// Returns the absolute first round for the current seven-round opponent cycle.
uint32_t GetOpponentCycleStartRound(uint32_t round) {
    return round > 0 ? ((round - 1) / 7) * 7 + 1 : 0;
}

// Reads a completed, non-mirror opponent history entry for one absolute round.
uint64_t GetCompletedHistoryOpponentForRound(
    uint64_t accountId,
    uint32_t targetRound,
    uint32_t currentRound
) {
    if (accountId == 0 || targetRound == 0 || targetRound >= currentRound) {
        return 0;
    }

    auto found = PredictionCache::OpponentHistory.find(accountId);
    if (found == PredictionCache::OpponentHistory.end()) {
        return 0;
    }

    const std::vector<OpponentHistoryEntry>& history = found->second;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->round != targetRound ||
            it->mirror ||
            it->opponentId == 0 ||
            it->opponentId == accountId) {
            continue;
        }

        return it->opponentId;
    }

    return 0;
}

// Reads a completed opponent for an effective round in the active seven-round cycle.
uint64_t GetCompletedCycleOpponent(
    uint64_t accountId,
    uint32_t cycleStartRound,
    uint32_t effectiveRound,
    uint32_t currentRound
) {
    if (cycleStartRound == 0 || effectiveRound == 0 || effectiveRound > 7) {
        return 0;
    }

    return GetCompletedHistoryOpponentForRound(
        accountId,
        cycleStartRound + effectiveRound - 1,
        currentRound
    );
}

// Detects the active seven-round cycle pattern from completed local R1 and R4 data.
OpponentCyclePattern DetectOpponentCyclePattern(
    uint64_t selfAccountId,
    uint32_t currentRound
) {
    uint32_t cycleStartRound = GetOpponentCycleStartRound(currentRound);
    if (cycleStartRound == 0) {
        return OpponentCyclePattern::Unknown;
    }

    uint64_t roundOneOpponent =
        GetCompletedCycleOpponent(selfAccountId, cycleStartRound, 1, currentRound);
    uint64_t roundFourOpponent =
        GetCompletedCycleOpponent(selfAccountId, cycleStartRound, 4, currentRound);

    if (roundOneOpponent == 0 || roundFourOpponent == 0) {
        return OpponentCyclePattern::Unknown;
    }

    return roundFourOpponent == roundOneOpponent ?
        OpponentCyclePattern::Classic :
        OpponentCyclePattern::Shifted;
}

// Predicts the local opponent for a target round from the seven-round pattern.
OpponentCyclePrediction PredictCyclePatternOpponentForTargetRound(
    uint64_t selfAccountId,
    uint32_t currentRound,
    uint32_t targetRound
) {
    OpponentCyclePrediction prediction{};
    if (selfAccountId == 0 || currentRound == 0 || targetRound == 0) {
        return prediction;
    }

    uint32_t effectiveRound = GetOpponentCycleEffectiveRound(targetRound);
    uint32_t cycleStartRound = GetOpponentCycleStartRound(targetRound);
    uint64_t roundOneOpponent =
        GetCompletedCycleOpponent(selfAccountId, cycleStartRound, 1, currentRound);

    if (roundOneOpponent == 0) {
        return prediction;
    }

    OpponentCyclePattern pattern =
        DetectOpponentCyclePattern(selfAccountId, currentRound);
    prediction.pattern = pattern;

    if (pattern == OpponentCyclePattern::Unknown) {
        if (effectiveRound == 4) {
            prediction.opponentId = roundOneOpponent;
            prediction.tentative = true;
        }
        return prediction;
    }

    if (pattern == OpponentCyclePattern::Classic) {
        if (effectiveRound == 4) {
            prediction.opponentId = roundOneOpponent;
        } else if (effectiveRound == 5) {
            prediction.opponentId =
                GetCompletedCycleOpponent(selfAccountId, cycleStartRound, 3, currentRound);
        }
    } else if (pattern == OpponentCyclePattern::Shifted) {
        uint32_t keyEffectiveRound = 0;

        if (effectiveRound == 5) {
            keyEffectiveRound = 4;
        } else if (effectiveRound == 6) {
            keyEffectiveRound = 2;
        } else if (effectiveRound == 7) {
            keyEffectiveRound = 3;
        }

        if (keyEffectiveRound != 0) {
            prediction.opponentId = GetCompletedCycleOpponent(
                roundOneOpponent,
                cycleStartRound,
                keyEffectiveRound,
                currentRound
            );
        }
    }

    if (prediction.opponentId == selfAccountId) {
        prediction.opponentId = 0;
    }

    return prediction;
}

// Predicts the local opponent from the active seven-round pattern.
OpponentCyclePrediction PredictCyclePatternOpponent(
    uint64_t selfAccountId,
    uint32_t currentRound
) {
    return PredictCyclePatternOpponentForTargetRound(selfAccountId, currentRound, currentRound);
}

// Uses recent opponent cycles to guess who should be next in the pairing queue.
uint64_t PredictHistoryQueueOpponent(
    uint64_t selfAccountId,
    const std::vector<uint64_t>& orderedAliveAccounts
) {
    if (selfAccountId == 0 || orderedAliveAccounts.size() < 2) {
        return 0;
    }

    auto found = PredictionCache::OpponentHistory.find(selfAccountId);
    if (found == PredictionCache::OpponentHistory.end() || found->second.empty()) {
        return 0;
    }

    const std::vector<OpponentHistoryEntry>& history = found->second;
    int bestLastSeen = std::numeric_limits<int>::max();
    uint64_t bestAccountId = 0;
    uint64_t mostRecentOpponent = 0;

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->opponentId != 0 && it->opponentId != selfAccountId && !it->mirror) {
            mostRecentOpponent = it->opponentId;
            break;
        }
    }

    for (uint64_t candidate : orderedAliveAccounts) {
        if (candidate == 0 || candidate == selfAccountId) {
            continue;
        }

        int lastSeen = -1;

        for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
            const OpponentHistoryEntry& entry = history[static_cast<size_t>(i)];
            if (entry.opponentId == candidate && !entry.mirror) {
                lastSeen = i;
                break;
            }
        }

        if (candidate == mostRecentOpponent && orderedAliveAccounts.size() > 2) {
            lastSeen = static_cast<int>(history.size()) + 1;
        }

        if (lastSeen < bestLastSeen) {
            bestLastSeen = lastSeen;
            bestAccountId = candidate;
        }
    }

    return bestAccountId;
}

// Scores one future opponent candidate from history, rotation, cycle, and accuracy.
double ScoreForecastCandidate(
    uint64_t selfAccountId,
    uint64_t candidateId,
    uint64_t rotationOpponent,
    uint64_t cycleOpponent,
    const std::vector<uint64_t>& simulatedRecentOpponents,
    size_t aliveOpponentCount,
    int historyWindow
) {
    if (selfAccountId == 0 || candidateId == 0 || candidateId == selfAccountId) {
        return 0.0;
    }

    double weight = 100.0;

    if (rotationOpponent != 0) {
        weight *= candidateId == rotationOpponent ? 2.75 : 0.82;
    }

    if (cycleOpponent != 0) {
        weight *= candidateId == cycleOpponent ? 3.15 : 0.76;
    }

    int recentDistance = FindRecentOpponentDistance(selfAccountId, candidateId, historyWindow);
    if (recentDistance == 0) {
        weight *= 0.28;
    } else if (recentDistance == 1) {
        weight *= 0.48;
    } else if (recentDistance < 0) {
        weight *= 1.35;
    } else if (aliveOpponentCount > 1 &&
               recentDistance >= static_cast<int>(aliveOpponentCount) - 1) {
        weight *= 1.22;
    }

    if (ContainsAccountId(simulatedRecentOpponents, candidateId)) {
        weight *= 0.55;
    } else {
        weight *= 1.18;
    }

    double mutualScore = ScoreMutualRecentOpponentHistory(
        selfAccountId,
        candidateId,
        historyWindow
    );
    if (mutualScore >= 1.60) {
        weight *= 0.42;
    } else if (mutualScore >= 0.85) {
        weight *= 0.68;
    }

    const CurrentOpponentObservation* candidateObservation =
        FindCurrentOpponentObservation(candidateId);
    if (candidateObservation &&
        candidateObservation->opponentId != 0 &&
        candidateObservation->opponentId != selfAccountId &&
        !candidateObservation->mirror) {
        weight *= 0.92;
    }

    weight *= GetPredictionAccuracyMultiplier(selfAccountId, candidateId);
    return std::max(weight, 0.0);
}

// Names the strongest signal behind a forecast row.
std::string ForecastSourceLabel(
    uint64_t opponentId,
    uint64_t rotationOpponent,
    uint64_t cycleOpponent,
    int mistakeCount
) {
    if (opponentId != 0 && opponentId == cycleOpponent) {
        return "Cycle";
    }

    if (opponentId != 0 && opponentId == rotationOpponent) {
        return "Rotation";
    }

    if (mistakeCount > 0) {
        return "Learned";
    }

    return "History";
}

// Builds the next eight local opponent forecasts from cached match history.
std::vector<OpponentForecastRow> BuildOpponentForecastRows(
    uint64_t selfAccountId,
    uint32_t currentRound,
    const std::vector<uint64_t>& aliveAccounts,
    const std::vector<uint64_t>& orderedAccounts,
    int realPlayerHistoryCount
) {
    std::vector<OpponentForecastRow> forecastRows;
    if (selfAccountId == 0 || aliveAccounts.size() < 2 || orderedAccounts.size() < 2) {
        return forecastRows;
    }

    std::vector<uint64_t> candidates;
    candidates.reserve(aliveAccounts.size() - 1);
    for (uint64_t accountId : aliveAccounts) {
        if (accountId != 0 && accountId != selfAccountId) {
            candidates.push_back(accountId);
        }
    }

    if (candidates.empty()) {
        return forecastRows;
    }

    size_t aliveOpponentCount = candidates.size();
    std::vector<uint64_t> simulatedRecent =
        BuildRecentOpponentCycle(selfAccountId, aliveOpponentCount);
    forecastRows.reserve(RuntimeConfig::MaxOpponentForecastRounds);

    for (int ahead = 1; ahead <= RuntimeConfig::MaxOpponentForecastRounds; ++ahead) {
        uint32_t targetRound = currentRound > 0 ?
            currentRound + static_cast<uint32_t>(ahead) :
            0;
        uint64_t rotationOpponent = PredictRoundRobinOpponent(
            orderedAccounts,
            selfAccountId,
            static_cast<uint32_t>(std::max(realPlayerHistoryCount, 0) + ahead)
        );
        OpponentCyclePrediction cyclePrediction = targetRound > 0 ?
            PredictCyclePatternOpponentForTargetRound(
                selfAccountId,
                currentRound,
                targetRound
            ) :
            OpponentCyclePrediction{};
        uint64_t cycleOpponent = ContainsAccountId(candidates, cyclePrediction.opponentId) ?
            cyclePrediction.opponentId :
            0;
        double totalWeight = 0.0;
        double bestWeight = -1.0;
        uint64_t bestOpponent = 0;

        for (uint64_t candidateId : candidates) {
            double weight = ScoreForecastCandidate(
                selfAccountId,
                candidateId,
                rotationOpponent,
                cycleOpponent,
                simulatedRecent,
                aliveOpponentCount,
                RuntimeConfig::MaxOpponentHistoryRounds
            );
            totalWeight += weight;

            if (weight > bestWeight) {
                bestWeight = weight;
                bestOpponent = candidateId;
            }
        }

        if (bestOpponent == 0 || totalWeight <= 0.0) {
            break;
        }

        int confidence =
            static_cast<int>((bestWeight * 100.0 / totalWeight) + 0.5);
        confidence = std::clamp(confidence, 5, 95);
        int mistakeCount = GetPredictionMistakeCount(selfAccountId, bestOpponent);
        std::string opponentName = GetBattlePlayerName(bestOpponent);
        if (opponentName.empty()) {
            opponentName = FormatUInt64(bestOpponent);
        }

        forecastRows.push_back({
            ahead,
            targetRound,
            bestOpponent,
            opponentName,
            confidence,
            ForecastSourceLabel(bestOpponent, rotationOpponent, cycleOpponent, mistakeCount)
        });

        RememberPendingForecast(selfAccountId, targetRound, bestOpponent);
        simulatedRecent.insert(simulatedRecent.begin(), bestOpponent);
        if (simulatedRecent.size() > aliveOpponentCount) {
            simulatedRecent.pop_back();
        }
    }

    return forecastRows;
}

// Refreshes per-player current-opponent observations used by predictions and HUD text.
void RefreshPredictionOpponentCache(
    uint64_t selfAccountId,
    void* selfManager,
    void* invasionManager,
    const std::vector<PredictionPlayer>& players
) {
    ResetOpponentPredictionHistoryIfNeeded(selfAccountId);
    PredictionCache::CurrentRoundOpponents.clear();

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
            TryConsumeManagedWorkUnits() ?
        Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr) :
        0;

    auto addObservation = [invasionManager](uint64_t accountId, void* manager, bool alive) {
        if (accountId == 0) {
            return;
        }

        if (!TryConsumeManagedWorkUnits(3)) {
            return;
        }

        CurrentOpponentLookup lookup =
            GetCurrentOpponentForAccount(accountId, manager, invasionManager);
        PredictionCache::CurrentRoundOpponents[accountId] = {
            accountId,
            lookup.accountId,
            alive,
            lookup.mirror,
            lookup.fromCurrentApi,
            false
        };
    };

    addObservation(selfAccountId, selfManager, true);

    for (const PredictionPlayer& player : players) {
        addObservation(player.accountId, player.manager, player.alive);
    }

    std::vector<uint64_t> accounts;
    accounts.reserve(PredictionCache::CurrentRoundOpponents.size());

    for (const auto& observed : PredictionCache::CurrentRoundOpponents) {
        accounts.push_back(observed.first);
    }

    for (uint64_t accountId : accounts) {
        auto observed = PredictionCache::CurrentRoundOpponents.find(accountId);
        if (observed == PredictionCache::CurrentRoundOpponents.end()) {
            continue;
        }

        const CurrentOpponentObservation& observation = observed->second;
        if (observation.opponentId == 0 ||
            observation.opponentId == observation.accountId ||
            observation.mirror) {
            continue;
        }

        auto reverse = PredictionCache::CurrentRoundOpponents.find(observation.opponentId);
        if (reverse != PredictionCache::CurrentRoundOpponents.end() &&
            reverse->second.opponentId == 0) {
            reverse->second.opponentId = observation.accountId;
            reverse->second.inferred = true;
        }
    }

    for (const auto& observed : PredictionCache::CurrentRoundOpponents) {
        RememberOpponentObservation(observed.second, round);
    }
}

// Advances opponent prediction history on its scheduled feature cadence.
void TickOpponentPredictionHistory(uint64_t selfAccountId) {
    if (selfAccountId == 0 || !Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        return;
    }

    void* selfManager = GetBattleManagerByAccountId(selfAccountId);
    void* invasionManager = GetLogicInvasionManager();
    std::vector<PredictionPlayer> players = CollectPredictionPlayers(selfAccountId);
    RefreshPredictionOpponentCache(selfAccountId, selfManager, invasionManager, players);
}

// Returns the strongest exact next-opponent signal before weighted scoring runs.
uint64_t FindExactPredictedOpponent(
    uint64_t selfAccountId,
    void* selfManager,
    void* invasionManager,
    const std::vector<PredictionPlayer>& players
) {
    if (selfAccountId == 0) {
        return 0;
    }

    uint64_t exactOpponent = 0;

    if (Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID) {
        exactOpponent = Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
            nullptr,
            selfAccountId
        );
    }

    if (exactOpponent == 0) {
        exactOpponent = GetCurrentPairFromInvasion(invasionManager, selfAccountId);
    }

    if (exactOpponent == 0) {
        exactOpponent = GetCurrentOpponentFromManager(selfManager);
    }

    if (exactOpponent != 0 && exactOpponent != selfAccountId) {
        return exactOpponent;
    }

    for (const PredictionPlayer& player : players) {
        if (!player.alive) {
            continue;
        }

        uint64_t playerPair =
            GetCurrentPairFromInvasion(invasionManager, player.accountId);

        if (playerPair == selfAccountId) {
            return player.accountId;
        }

        if (Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID) {
            uint64_t playerOpponent =
                Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                    nullptr,
                    player.accountId
                );

            if (playerOpponent == selfAccountId) {
                return player.accountId;
            }
        }
    }

    return 0;
}

// Builds opponent predictions from current runtime evidence.
std::vector<OpponentPredictionRow> BuildOpponentPredictions(uint64_t selfAccountId) {
    std::vector<OpponentPredictionRow> rows;
    PredictionCache::CachedForecastRows.clear();
    std::vector<PredictionPlayer> players = CollectPredictionPlayers(selfAccountId);

    rows.reserve(players.size());

    for (const PredictionPlayer& player : players) {
        rows.push_back({
            player.accountId,
            player.name.empty() ? FormatUInt64(player.accountId) : player.name,
            0,
            0.0,
            player.alive,
            false,
            false,
            0,
            {}
        });
    }

    if (selfAccountId == 0 || rows.empty()) {
        return rows;
    }

    void* selfManager = GetBattleManagerByAccountId(selfAccountId);
    void* invasionManager = GetLogicInvasionManager();
    RefreshPredictionOpponentCache(selfAccountId, selfManager, invasionManager, players);

    for (OpponentPredictionRow& row : rows) {
        const CurrentOpponentObservation* observation =
            FindCurrentOpponentObservation(row.accountId);
        if (observation) {
            row.currentEnemyName = FormatObservedEnemyName(*observation);
        }
    }

    bool monsterRound = IsCurrentMonsterRound(invasionManager);
    bool realPlayerMode = IsRealPlayerPairingMode(invasionManager);
    uint64_t exactOpponent = 0;
    bool exactMirror = false;

    const CurrentOpponentObservation* selfObservation =
        FindCurrentOpponentObservation(selfAccountId);
    if (selfObservation && selfObservation->opponentId != 0) {
        exactOpponent = selfObservation->opponentId;
        exactMirror = selfObservation->mirror ||
            selfObservation->opponentId == selfAccountId;
    }

    if (exactOpponent == 0) {
        for (const auto& observed : PredictionCache::CurrentRoundOpponents) {
            const CurrentOpponentObservation& observation = observed.second;
            if (observation.accountId != selfAccountId &&
                observation.opponentId == selfAccountId &&
                observation.alive) {
                exactOpponent = observation.accountId;
                exactMirror = observation.mirror;
                break;
            }
        }
    }

    if (exactOpponent == 0) {
        exactOpponent = FindExactPredictedOpponent(
            selfAccountId,
            selfManager,
            invasionManager,
            players
        );
    }

    if (monsterRound) {
        return rows;
    }

    if (exactOpponent != 0 && (exactOpponent != selfAccountId || exactMirror)) {
        bool foundExactRow = false;

        for (OpponentPredictionRow& row : rows) {
            if (row.accountId == exactOpponent) {
                row.percent = 100;
                row.weight = 100.0;
                row.mirror = exactMirror;
                row.lockedPercent = true;
                foundExactRow = true;
                break;
            }
        }

        if (!foundExactRow) {
            std::string exactName = GetBattlePlayerName(exactOpponent);
            if (exactName.empty()) {
                exactName = FormatUInt64(exactOpponent);
            }

            rows.push_back({
                exactOpponent,
                exactName,
                100,
                100.0,
                true,
                exactMirror,
                true,
                0,
                {}
            });
        }
    }

    for (OpponentPredictionRow& row : rows) {
        row.recentMeetings = std::max(
            CountRecentOpponentHistory(selfAccountId, row.accountId, 8),
            CountRecentOpponentHistory(row.accountId, selfAccountId, 8)
        );
    }

    if (!realPlayerMode) {
        return rows;
    }

    std::vector<uint64_t> aliveAccounts;
    aliveAccounts.push_back(selfAccountId);

    for (const PredictionPlayer& player : players) {
        if (player.alive) {
            aliveAccounts.push_back(player.accountId);
        }
    }

    std::vector<uint64_t> invaderOrderedAccounts =
        GetInvaderAccountOrder(invasionManager, aliveAccounts);
    const std::vector<uint64_t>& patternAccounts =
        invaderOrderedAccounts.empty() ? aliveAccounts : invaderOrderedAccounts;
    size_t aliveOpponentCount =
        aliveAccounts.empty() ? 0 : static_cast<size_t>(aliveAccounts.size() - 1);
    std::vector<uint64_t> recentCycleOpponents =
        BuildRecentOpponentCycle(selfAccountId, aliveOpponentCount);
    int realPlayerHistoryCount = CountRealPlayerOpponentHistory(selfAccountId);
    uint64_t historyQueueOpponent =
        PredictHistoryQueueOpponent(selfAccountId, patternAccounts);
    uint64_t invaderOrderOpponent = realPlayerHistoryCount > 0 ?
        PredictRoundRobinOpponent(
            patternAccounts,
            selfAccountId,
            static_cast<uint32_t>(realPlayerHistoryCount + 1)
        ) :
        0;

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
            TryConsumeManagedWorkUnits() ?
        Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr) :
        0;
    OpponentCyclePrediction cyclePatternPrediction =
        PredictCyclePatternOpponent(selfAccountId, round);
    bool cyclePatternOpponentAlive =
        ContainsAccountId(aliveAccounts, cyclePatternPrediction.opponentId);
    uint64_t roundRobinOpponent = PredictRoundRobinOpponent(
        aliveAccounts,
        selfAccountId,
        round
    );
    PredictionCache::CachedForecastRows = BuildOpponentForecastRows(
        selfAccountId,
        round,
        aliveAccounts,
        patternAccounts,
        realPlayerHistoryCount
    );

    static FieldInfo* lastRoundEnemyField = nullptr;
    static FieldInfo* prevRealPlayerEnemyField = nullptr;

    if (!lastRoundEnemyField) {
        lastRoundEnemyField =
            GetFieldInfoFromName("", "MCLogicBattleManager", "lastRoundEnemy");
    }

    if (!prevRealPlayerEnemyField) {
        prevRealPlayerEnemyField =
            GetFieldInfoFromName("", "MCLogicBattleManager", "prevRealPlayerEnemy");
    }

    uint64_t lastRoundEnemyId =
        GetManagerPointerAccountField(selfManager, lastRoundEnemyField);
    uint64_t prevRealPlayerEnemyId =
        GetManagerPointerAccountField(selfManager, prevRealPlayerEnemyField);

    for (OpponentPredictionRow& row : rows) {
        const auto playerIt = std::find_if(
            players.begin(),
            players.end(),
            [&row](const PredictionPlayer& player) {
                return player.accountId == row.accountId;
            }
        );

        if (row.lockedPercent) {
            continue;
        }

        if (playerIt == players.end() || !playerIt->alive) {
            row.weight = 0.0;
            continue;
        }

        double weight = 100.0;
        int sourceVotes = 0;
        uint64_t candidatePair = GetCurrentPairFromInvasion(
            invasionManager,
            playerIt->accountId
        );

        if (candidatePair == selfAccountId) {
            weight *= 5.0;
            sourceVotes += 2;
        } else if (candidatePair != 0) {
            weight *= 0.03;
        }

        const CurrentOpponentObservation* candidateObservation =
            FindCurrentOpponentObservation(playerIt->accountId);
        uint64_t candidateCurrentOpponent = candidateObservation ?
            candidateObservation->opponentId :
            0;

        if (candidateCurrentOpponent == selfAccountId) {
            weight *= candidateObservation && candidateObservation->mirror ? 2.5 : 4.0;
            sourceVotes += 2;
        } else if (candidateCurrentOpponent != 0) {
            weight *= candidateObservation && candidateObservation->inferred ? 0.15 : 0.08;
        }

        if (row.accountId == lastRoundEnemyId) {
            weight *= 0.08;
        }

        if (row.accountId == prevRealPlayerEnemyId) {
            weight *= 0.35;
        }

        uint64_t candidateLastEnemy =
            GetManagerPointerAccountField(playerIt->manager, lastRoundEnemyField);
        uint64_t candidatePrevEnemy =
            GetManagerPointerAccountField(playerIt->manager, prevRealPlayerEnemyField);

        if (candidateLastEnemy == selfAccountId) {
            weight *= 0.20;
        }

        if (candidatePrevEnemy == selfAccountId) {
            weight *= 0.55;
        }

        if (historyQueueOpponent != 0) {
            weight *= row.accountId == historyQueueOpponent ? 4.2 : 0.68;
            if (row.accountId == historyQueueOpponent) {
                ++sourceVotes;
            }
        }

        if (invaderOrderOpponent != 0 &&
            invaderOrderOpponent != historyQueueOpponent) {
            weight *= row.accountId == invaderOrderOpponent ? 3.4 : 0.74;
            if (row.accountId == invaderOrderOpponent) {
                ++sourceVotes;
            }
        } else if (historyQueueOpponent == 0 && roundRobinOpponent != 0) {
            weight *= row.accountId == roundRobinOpponent ? 2.2 : 0.86;
            if (row.accountId == roundRobinOpponent) {
                ++sourceVotes;
            }
        }

        if (cyclePatternOpponentAlive) {
            bool cycleMatch = row.accountId == cyclePatternPrediction.opponentId;
            double cycleMatchBoost = cyclePatternPrediction.tentative ?
                1.9 :
                (cyclePatternPrediction.pattern == OpponentCyclePattern::Classic ? 3.1 : 3.4);
            double cycleMissPenalty = cyclePatternPrediction.tentative ? 0.91 : 0.76;

            weight *= cycleMatch ? cycleMatchBoost : cycleMissPenalty;
            if (cycleMatch && !cyclePatternPrediction.tentative) {
                ++sourceVotes;
            }
        }

        int recentSelfMeetings = std::max(
            CountRecentOpponentHistory(selfAccountId, row.accountId, 8),
            CountRecentOpponentHistory(row.accountId, selfAccountId, 8)
        );
        double recentSelfScore = ScoreMutualRecentOpponentHistory(
            selfAccountId,
            row.accountId,
            8
        );
        row.recentMeetings = recentSelfMeetings;

        int recentDistance = FindRecentOpponentDistance(selfAccountId, row.accountId, 12);
        if (recentDistance == 0) {
            weight *= 0.30;
        } else if (recentDistance == 1) {
            weight *= 0.52;
        } else if (recentDistance >= 2 &&
                   aliveOpponentCount > 1 &&
                   recentDistance >= static_cast<int>(aliveOpponentCount) - 1) {
            weight *= 1.28;
            ++sourceVotes;
        } else if (recentDistance < 0 && realPlayerHistoryCount >= 3) {
            weight *= 1.18;
        }

        bool inRecentCycle = ContainsAccountId(recentCycleOpponents, row.accountId);
        if (!recentCycleOpponents.empty() &&
            recentCycleOpponents.size() < aliveOpponentCount) {
            weight *= inRecentCycle ? 0.58 : 1.85;
            if (!inRecentCycle) {
                ++sourceVotes;
            }
        } else if (inRecentCycle && recentSelfMeetings > 0) {
            weight *= 0.82;
        }

        if (recentSelfScore >= 1.60) {
            weight *= 0.38;
        } else if (recentSelfScore >= 0.85) {
            weight *= 0.62;
        } else if (recentSelfMeetings == 1) {
            weight *= 0.78;
        } else {
            weight *= 1.15;
        }

        weight *= GetPredictionAccuracyMultiplier(selfAccountId, row.accountId);

        if (sourceVotes >= 2) {
            weight *= 1.0 + (std::min(sourceVotes, 5) * 0.16);
        }

        row.weight = std::max(weight, 0.0);
    }

    double totalWeight = 0.0;
    int strongestIndex = -1;

    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].lockedPercent) {
            continue;
        }

        totalWeight += rows[i].weight;

        if (strongestIndex < 0 || rows[i].weight > rows[static_cast<size_t>(strongestIndex)].weight) {
            strongestIndex = static_cast<int>(i);
        }
    }

    if (totalWeight <= 0.0) {
        return rows;
    }

    int totalPercent = 0;

    for (OpponentPredictionRow& row : rows) {
        if (row.lockedPercent) {
            continue;
        }

        row.percent = static_cast<int>((row.weight * 100.0 / totalWeight) + 0.5);
        row.percent = std::clamp(row.percent, 0, 100);
        totalPercent += row.percent;
    }

    if (strongestIndex >= 0 && totalPercent != 100) {
        OpponentPredictionRow& strongest = rows[static_cast<size_t>(strongestIndex)];
        strongest.percent = std::clamp(strongest.percent + (100 - totalPercent), 0, 100);
    }

    return rows;
}

// Keeps prediction display order stable while cached rows are reused across frames.
void SortOpponentPredictionRows(std::vector<OpponentPredictionRow>& rows) {
    std::sort(
        rows.begin(),
        rows.end(),
        [](const OpponentPredictionRow& left, const OpponentPredictionRow& right) {
            if (left.percent != right.percent) {
                return left.percent > right.percent;
            }

            if (left.name != right.name) {
                return left.name < right.name;
            }

            return left.accountId < right.accountId;
        }
    );
}

// Rebuilds the expensive prediction table only on the throttled feature tick.
bool RefreshCachedOpponentPredictionRows(
    uint64_t selfAccountId,
    std::chrono::steady_clock::time_point now,
    bool force
) {
    bool accountChanged = PredictionCache::CachedRowsSelfAccountId != selfAccountId;

    if (accountChanged) {
        PredictionCache::CachedRows.clear();
        PredictionCache::CachedForecastRows.clear();
        PredictionCache::CachedRowsReady = false;
        PredictionCache::CachedRowsSelfAccountId = selfAccountId;
        PredictionCache::LastRowsRefresh = {};
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        PredictionCache::CachedRows.clear();
        PredictionCache::CachedForecastRows.clear();
        PredictionCache::CachedRowsReady = false;
        PredictionCache::LastRowsRefresh = now;
        return false;
    }

    if (!force &&
        PredictionCache::CachedRowsReady &&
        now - PredictionCache::LastRowsRefresh <
            std::chrono::milliseconds(RuntimeConfig::OpponentPredictionTickMs)) {
        return false;
    }

    PredictionCache::CachedRows = BuildOpponentPredictions(selfAccountId);
    SortOpponentPredictionRows(PredictionCache::CachedRows);
    PredictionCache::CachedRowsReady = true;
    PredictionCache::CachedRowsSelfAccountId = selfAccountId;
    PredictionCache::LastRowsRefresh = now;
    return true;
}

// Returns the last tick-built prediction rows without touching managed state.
const std::vector<OpponentPredictionRow>& GetCachedOpponentPredictionRows() {
    return PredictionCache::CachedRows;
}

// Returns the last tick-built eight-round forecast without touching managed state.
const std::vector<OpponentForecastRow>& GetCachedOpponentForecastRows() {
    return PredictionCache::CachedForecastRows;
}

// Builds next enemy hud text from current runtime evidence.
std::string BuildNextEnemyHudText(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady() || selfAccountId == 0) {
        return "Next enemy: Waiting";
    }

    uint64_t enemyId = 0;

    if (Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID) {
        enemyId = Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
            nullptr,
            selfAccountId
        );
    }

    if (enemyId == 0) {
        void* invasionManager = GetLogicInvasionManager();
        enemyId = GetCurrentPairFromInvasion(invasionManager, selfAccountId);
    }

    if (enemyId == 0) {
        enemyId = GetCurrentOpponentFromManager(GetBattleManagerByAccountId(selfAccountId));
    }

    if (enemyId != 0 && enemyId != selfAccountId) {
        std::string enemyName = GetBattlePlayerName(enemyId);
        if (enemyName.empty()) {
            enemyName = FormatUInt64(enemyId);
        }
        return "Next enemy: " + enemyName;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        return "Next enemy: Waiting";
    }

    const std::vector<OpponentPredictionRow>& rows = GetCachedOpponentPredictionRows();
    auto best = std::max_element(
        rows.begin(),
        rows.end(),
        [](const OpponentPredictionRow& left, const OpponentPredictionRow& right) {
            return left.percent < right.percent;
        }
    );

    if (best == rows.end() || best->percent <= 0) {
        return "Next enemy: Waiting";
    }

    std::string enemyName = best->name.empty() ? FormatUInt64(best->accountId) : best->name;
    if (best->mirror) {
        enemyName += " (Mirror)";
    }

    return "Next enemy: " + enemyName + " (" + FormatInt(best->percent) + "%)";
}

// Refreshes next enemy hud text on its throttled runtime cadence.
void RefreshNextEnemyHudText(uint64_t selfAccountId) {
    if (!IntervalElapsed(UiCache::LastNextEnemyHudRefresh, 500)) {
        return;
    }

    UiCache::NextEnemyHudText = BuildNextEnemyHudText(selfAccountId);
}

// Draws the next enemy hud overlay section without changing game state.
void DrawNextEnemyHud() {
    if (!UiState::ShowNextEnemyHud.load()) {
        return;
    }

    if (UiCache::NextEnemyHudText.empty()) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
        return;
    }

    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize() * 1.15f;
    const char* text = UiCache::NextEnemyHudText.c_str();
    ImVec2 textSize = font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, text);
    float maxWidth = std::max(io.DisplaySize.x - 16.0f, 16.0f);

    if (textSize.x > maxWidth) {
        fontSize = std::max(fontSize * (maxWidth / textSize.x), 12.0f);
        textSize = font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, text);
    }

    float bottomOffset = std::clamp(io.DisplaySize.y * 0.12f, 72.0f, 150.0f);
    ImVec2 pos(
        std::max((io.DisplaySize.x - textSize.x) * 0.5f, 8.0f),
        std::max(io.DisplaySize.y - bottomOffset - textSize.y * 0.5f, 8.0f)
    );

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImU32 shadowColor = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.80f));
    ImU32 textColor = ImGui::GetColorU32(ImVec4(1.0f, 0.92f, 0.72f, 1.0f));

    drawList->AddText(font, fontSize, ImVec2(pos.x + 2.0f, pos.y + 2.0f), shadowColor, text);
    drawList->AddText(font, fontSize, ImVec2(pos.x - 1.0f, pos.y + 1.0f), shadowColor, text);
    drawList->AddText(font, fontSize, pos, textColor, text);
}

// Draws the opponent prediction table overlay section without changing game state.
void DrawOpponentPredictionTable(uint64_t selfAccountId) {
    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        DrawWaitingText("Waiting for battle manager list");
        return;
    }

    (void)selfAccountId;

    const std::vector<OpponentPredictionRow>& rows = GetCachedOpponentPredictionRows();
    if (!PredictionCache::CachedRowsReady) {
        DrawWaitingText("Waiting for prediction refresh");
        return;
    }

    if (rows.empty()) {
        ImGui::TextUnformatted("No players found");
        return;
    }

    if (!ImGui::BeginTable(
        "##OpponentPredictionTable",
        4,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 230.0f)
    )) {
        return;
    }

    ImGui::TableSetupColumn(MenuText("Player"));
    ImGui::TableSetupColumn(MenuText("Will fight"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn(MenuText("Recent"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn(MenuText("Current enemy"));
    ImGui::TableHeadersRow();

    for (const OpponentPredictionRow& row : rows) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        std::string playerName = row.name;
        if (row.mirror) {
            playerName += " (Mirror)";
        }
        ImGui::TextUnformatted(playerName.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d%%", row.percent);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d", row.recentMeetings);
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(row.currentEnemyName.empty() ? "-" : row.currentEnemyName.c_str());
    }

    ImGui::EndTable();
}

// Draws the eight-round opponent forecast table from cached prediction state.
void DrawOpponentForecastTable() {
    const std::vector<OpponentForecastRow>& rows = GetCachedOpponentForecastRows();

    DrawMenuSeparatorText("Forecast");

    if (!PredictionCache::CachedRowsReady) {
        DrawWaitingText("Waiting for prediction refresh");
        return;
    }

    if (rows.empty()) {
        ImGui::TextUnformatted(MenuText("No 8-round forecast yet"));
        return;
    }

    if (!ImGui::BeginTable(
        "##OpponentForecastTable",
        5,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 210.0f)
    )) {
        return;
    }

    ImGui::TableSetupColumn(MenuText("Ahead"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn(MenuText("Round"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn(MenuText("Player"));
    ImGui::TableSetupColumn(MenuText("Confidence"), ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn(MenuText("Source"), ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
            const OpponentForecastRow& row = rows[static_cast<size_t>(rowIndex)];

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("+%d", row.ahead);

            ImGui::TableSetColumnIndex(1);
            if (row.round > 0) {
                ImGui::Text("%u", row.round);
            } else {
                ImGui::TextUnformatted("-");
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row.name.empty() ? "-" : row.name.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d%%", row.confidence);

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(MenuText(row.source.c_str()));
        }
    }

    ImGui::EndTable();
}

// Draws the full Info-tab prediction section without doing managed reads.
void DrawOpponentPredictionSection(uint64_t selfAccountId) {
    DrawMenuSeparatorText("Prediction");
    DrawOpponentPredictionTable(selfAccountId);
    ImGui::Spacing();
    DrawOpponentForecastTable();
}

// Draws the test all managers table overlay section without changing game state.
void DrawTestAllManagersTable() {
    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        DrawWaitingText("Waiting for battle manager list");
        return;
    }

    if (!TryConsumeManagedWorkUnits()) {
        DrawWaitingText("Waiting for diagnostic budget");
        return;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit) || entryLimit <= 0) {
        ImGui::TextUnformatted("No battle managers found");
        return;
    }

    static FieldInfo* fightOverField = nullptr;

    if (!fightOverField) {
        fightOverField = GetFieldInfoFromName("", "MCLogicBattleManager", "m_bFightOver");
    }

    if (!ImGui::BeginTable(
        "##AllManagersTestTable",
        7,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 260.0f)
    )) {
        return;
    }

    ImGui::TableSetupColumn("Account", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Enemy", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Over", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Fail", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; entries && i < entryLimit; ++i) {
        const auto& entry = entries[i];

        if (entry.hashCode < 0 || entry.key == 0) {
            continue;
        }

        if (!TryConsumeManagedWorkUnits(6)) {
            break;
        }

        uint64_t accountId = entry.key;
        void* manager = entry.value;
        uint64_t enemyId =
            Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ?
                Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                    nullptr,
                    accountId
                ) :
                0;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%llu", (unsigned long long)accountId);
        ImGui::TableSetColumnIndex(1);
        std::string playerName = GetBattlePlayerName(accountId);
        ImGui::TextUnformatted(playerName.empty() ? "-" : playerName.c_str());
        ImGui::TableSetColumnIndex(2);
        if (enemyId != 0) {
            ImGui::Text("%llu", (unsigned long long)enemyId);
        } else {
            ImGui::TextUnformatted("-");
        }
        ImGui::TableSetColumnIndex(3);
        if (Originals::MCLogicBattleData_ILOGIC_GetPlayerHP) {
            ImGui::Text(
                "%d",
                Originals::MCLogicBattleData_ILOGIC_GetPlayerHP(nullptr, accountId)
            );
        } else {
            ImGui::TextUnformatted("-");
        }
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(FormatFieldBool(manager, fightOverField).c_str());
        ImGui::TableSetColumnIndex(5);
        if (manager && Originals::MCLogicBattleManager_get_m_bDefendFaild) {
            ImGui::TextUnformatted(
                FormatBool(Originals::MCLogicBattleManager_get_m_bDefendFaild(manager)).c_str()
            );
        } else {
            ImGui::TextUnformatted("-");
        }
        ImGui::TableSetColumnIndex(6);
        ImGui::TextUnformatted(GetRoundResultDataField(manager, "result").c_str());
    }

    ImGui::EndTable();
}

// Draws the test tab overlay section without changing game state.
void DrawTestTab() {
    DrawRuntimeStatus();
    ImGui::Spacing();

    if (!IsIl2CppRuntimeReady()) {
        DrawWaitingText("Waiting for IL2CPP runtime");
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();
    uint64_t defaultAccountId = selfAccountId;
    uint64_t selfOpponentId =
        selfAccountId && Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ?
            (TryConsumeManagedWorkUnits() ?
                Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                    nullptr,
                    selfAccountId
                ) :
                0) :
            0;

    if (DrawMenuButton("Retry test bindings")) {
        ResolveFeatureBindings();
        RefreshManagedReferences(true);
    }

    ImGui::SameLine();
    if (DrawMenuButton("Use self") && selfAccountId != 0) {
        UiState::TestAccountId = FormatUInt64(selfAccountId);
    }

    ImGui::SameLine();
    if (DrawMenuButton("Use opponent") && selfOpponentId != 0) {
        UiState::TestAccountId = FormatUInt64(selfOpponentId);
    }

    ImGui::SameLine();
    if (DrawMenuButton("Clear account")) {
        UiState::TestAccountId.clear();
    }

    ImGui::SetNextItemWidth(-1.0f);
    std::string testAccountHint = MenuText("Account ID to inspect (empty = self)");
    ImGui::InputTextWithHint(
        "##TestAccountId",
        testAccountHint.c_str(),
        &UiState::TestAccountId
    );
    DrawMenuTooltip("Account ID to inspect (empty = self)");

    uint64_t targetAccountId = ParseAccountIdOrDefault(
        UiState::TestAccountId,
        defaultAccountId
    );
    uint64_t opponentAccountId =
        targetAccountId && Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ?
            (TryConsumeManagedWorkUnits() ?
                Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                    nullptr,
                    targetAccountId
                ) :
                0) :
            0;
    void* targetManager =
        TryConsumeManagedWorkUnits(2) ? GetBattleManagerByAccountId(targetAccountId) : nullptr;

    if (ImGui::BeginTabBar("##TestTabBar", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (BeginMenuTabItem("Predict")) {
            DrawOpponentPredictionTable(selfAccountId);
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Bindings")) {
            DrawTestBindingRows();
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Round")) {
            DrawTestRoundRows(selfAccountId, targetAccountId, opponentAccountId);
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Player")) {
            DrawTestPlayerRows(targetAccountId);
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Manager")) {
            DrawTestManagerRows(targetManager);
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Bridge")) {
            DrawTestBridgeRows();
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Shop UI")) {
            DrawTestShopPanelRows();
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Behavior")) {
            DrawTestBehaviorRows(targetAccountId);
            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Managers")) {
            DrawTestAllManagersTable();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

// Draws the shop tab overlay section without changing game state.
void DrawShopTab() {
    if (ImGui::BeginTabBar("##ShopTabBar")) {
        if (BeginMenuTabItem("Automation")) {
            if (!HasShopAutomationBindings()) {
                DrawWaitingText("Waiting for shop automation bindings");
            }

            if (FeatureState::ShopRefresh.load() && !HasShopRefreshBindings()) {
                DrawWaitingText("Waiting for shop refresh panel");
            }

            if ((FeatureState::ShopBuyRecommendLineup.load() ||
                 FeatureState::ShopStopRefreshAtRecommendLineup.load()) &&
                !HasShopRecommendLineupBindings()) {
                DrawWaitingText("Waiting for recommendation lineup bindings");
            }

            DrawAtomicCheckbox("Auto-buy free heroes", FeatureState::ShopBuyFreeHero);
            DrawAtomicCheckbox("Auto-buy selected targets", FeatureState::ShopBuySelectedHero);
            DrawAtomicCheckbox(
                "Force Scavenger to Always Get Expensive Heroes",
                FeatureState::ShopForceScavengerExpensiveHero
            );

            if (FeatureState::ShopForceScavengerExpensiveHero.load()) {
                if (!HasShopScavengerBindings()) {
                    DrawWaitingText("Waiting for Scavenger shop bindings");
                }

                int scavengerRelationId = FeatureState::CachedScavengerRelationId.load();
                int scavengerActiveCount = FeatureState::CachedScavengerActiveCount.load();
                std::string scavengerState = scavengerRelationId > 0 ?
                    "Relation #" + std::to_string(scavengerRelationId) :
                    "Waiting";

                if (scavengerRelationId > 0) {
                    scavengerState += ", active count ";
                    scavengerState += (scavengerActiveCount >= 0) ?
                        std::to_string(scavengerActiveCount) :
                        std::string("Waiting");
                }

                ImGui::Text("Scavenger: %s", scavengerState.c_str());
            }

            ImGui::Separator();
            DrawMenuSeparatorText("Recommendation Lineup");
            DrawAtomicCheckbox(
                "Auto-buy recommendation heroes",
                FeatureState::ShopBuyRecommendLineup
            );

            if (HasShopRecommendLineupBindings()) {
                int recommendHeroId = FeatureState::CachedRecommendLineupHeroId.load();
                ImGui::Text(
                    "%s: %s",
                    MenuText("Current recommendation"),
                    FormatHeroLabel(recommendHeroId).c_str()
                );
            } else {
                ImGui::TextUnformatted(MenuText("Current recommendation: Waiting"));
            }

            std::vector<std::pair<int, int>> recommendTargets =
                GetRecommendLineupTargetsSnapshot();
            DrawMenuSeparatorText("Recommended lineup heroes");

            if (recommendTargets.empty()) {
                DrawWaitingText(
                    HasShopRecommendLineupBindings() ?
                        "No recommendation lineup heroes detected" :
                        "Waiting for recommendation lineup bindings"
                );
            } else if (ImGui::BeginTable(
                "##RecommendLineupTargetsTable",
                2,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 190.0f)
            )) {
                ImGui::TableSetupColumn(MenuText("Hero"));
                ImGui::TableSetupColumn(MenuText("Target Count"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(recommendTargets.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        int heroId = recommendTargets[row].first;
                        int targetCount = ClampShopTargetCount(recommendTargets[row].second);
                        int originalTargetCount = targetCount;

                        ImGui::PushID(heroId);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(FormatHeroLabel(heroId).c_str());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputInt("##recommendTarget", &targetCount);
                        DrawMenuTooltip("Recommendation target count");
                        targetCount = ClampShopTargetCount(targetCount);

                        if (targetCount != originalTargetCount) {
                            SetRecommendLineupTargetCount(heroId, targetCount);
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            ImGui::Separator();
            DrawAtomicCheckbox("Auto-refresh shop", FeatureState::ShopRefresh);
            DrawAtomicCheckbox(
                "Pause refresh when free hero appears",
                FeatureState::ShopStopRefreshAtFreeHero
            );
            DrawAtomicCheckbox(
                "Pause refresh when selected target appears",
                FeatureState::ShopStopRefreshAtSelectedHero
            );
            DrawAtomicCheckbox(
                "Pause refresh when recommendation hero appears",
                FeatureState::ShopStopRefreshAtRecommendLineup
            );
            ImGui::Separator();
            DrawAtomicCheckbox("Keep gold reserve", FeatureState::ShopKeepGold);
            ImGui::SetNextItemWidth(120.0f);
            DrawAtomicInputInt("Minimum reserve gold", FeatureState::ShopKeepGoldAt);
            FeatureState::ShopKeepGoldAt =
                std::clamp(FeatureState::ShopKeepGoldAt.load(), 0, 999999);

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Hero Targets")) {
            DrawAtomicCheckbox("Show tracked heroes only", UiState::ShopShowSelectedOnly);

            if (DrawMenuButton("Clear hero targets", ImVec2(-1.0f, 0.0f))) {
                DeselectShopHeroTargets();
            }

            ImGui::Spacing();
            std::vector<HeroTableEntry> heroes = GetSortedHeroes(true);
            std::unordered_map<int, HeroAutomationState> shopTargets =
                GetShopHeroTargetsSnapshot();
            int totalHeroCount = static_cast<int>(heroes.size());

            if (UiState::ShopShowSelectedOnly.load()) {
                heroes.erase(
                    std::remove_if(
                        heroes.begin(),
                        heroes.end(),
                        [&shopTargets](const HeroTableEntry& hero) {
                            auto it = shopTargets.find(hero.id);
                            return it == shopTargets.end() ||
                                !it->second.selected;
                        }
                    ),
                    heroes.end()
                );
            }

            ImGui::Text(
                MenuText("Showing %d / %d heroes"),
                static_cast<int>(heroes.size()),
                totalHeroCount
            );

            if (heroes.empty()) {
                if (totalHeroCount == 0) {
                    DrawWaitingText("Waiting for hero table");
                } else {
                    ImGui::TextUnformatted("No tracked heroes selected");
                }
            } else if (ImGui::BeginTable(
                "##ShopHeroListTable",
                4,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 340.0f)
            )) {
                ImGui::TableSetupColumn(MenuText("Hero"));
                ImGui::TableSetupColumn(MenuText("Cost"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(MenuText("Target Count"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn(MenuText("Track"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(heroes.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const HeroTableEntry& hero = heroes[row];
                        auto targetIt = shopTargets.find(hero.id);
                        HeroAutomationState state =
                            targetIt != shopTargets.end() ?
                                targetIt->second :
                                HeroAutomationState{};
                        state.targetCount = std::clamp(state.targetCount, 1, 99);
                        HeroAutomationState originalState = state;

                        ImGui::PushID(hero.id);
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(hero.name.c_str());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d", hero.quality);

                        ImGui::TableSetColumnIndex(2);
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputInt("##target", &state.targetCount);
                        DrawMenuTooltip("Target Count");
                        state.targetCount = std::clamp(state.targetCount, 1, 99);

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Checkbox("##selected", &state.selected);
                        DrawMenuTooltip("Track");
                        ImGui::PopID();

                        if (state.selected != originalState.selected ||
                            state.targetCount != originalState.targetCount) {
                            SetShopHeroTarget(hero.id, state);
                            shopTargets[hero.id] = state;
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

// Draws the arena tab overlay section without changing game state.
void DrawArenaTab() {
    if (ImGui::BeginTabBar("##ArenaTabBar")) {
        if (BeginMenuTabItem("Heroes")) {
            if (!HasArenaHeroBindings()) {
                DrawWaitingText("Waiting for arena hero bindings");
            }

            ImGui::SetNextItemWidth(120.0f);
            DrawAtomicInputInt("Spawn star level", FeatureState::ArenaHeroStar);
            FeatureState::ArenaHeroStar =
                std::clamp(FeatureState::ArenaHeroStar.load(), 1, 3);
            ImGui::Separator();

            std::vector<HeroTableEntry> heroes = GetSortedHeroes(true);
            int totalHeroCount = static_cast<int>(heroes.size());
            ImGui::Text(
                MenuText("Showing %d / %d heroes"),
                static_cast<int>(heroes.size()),
                totalHeroCount
            );

            if (heroes.empty()) {
                if (totalHeroCount == 0) {
                    DrawWaitingText("Waiting for hero table");
                } else {
                    ImGui::TextUnformatted("No heroes available");
                }
            } else if (ImGui::BeginTable(
                "##ArenaHeroListTable",
                3,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 340.0f)
            )) {
                ImGui::TableSetupColumn(MenuText("Hero"));
                ImGui::TableSetupColumn(MenuText("Cost"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(MenuText("Action"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(heroes.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const HeroTableEntry& hero = heroes[row];
                        ImGui::PushID(hero.id);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(hero.name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d", hero.quality);
                        ImGui::TableSetColumnIndex(2);

                        if (DrawMenuButton("Spawn", ImVec2(-1.0f, 0.0f))) {
                            GiveHero(hero.id, FeatureState::ArenaHeroStar.load());
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Items")) {
            if (!HasArenaItemBindings()) {
                DrawWaitingText("Waiting for arena item binding");
            }

            DrawAtomicCheckbox("Grant enhanced item", FeatureState::ArenaItemEnhanced);
            ImGui::Separator();

            std::vector<EquipTableEntry> equips = GetSortedEquips();
            int totalEquipCount = static_cast<int>(equips.size());
            ImGui::Text(
                MenuText("Showing %d / %d items"),
                static_cast<int>(equips.size()),
                totalEquipCount
            );

            if (equips.empty()) {
                if (totalEquipCount == 0) {
                    DrawWaitingText("Waiting for item table");
                } else {
                    ImGui::TextUnformatted("No items available");
                }
            } else if (ImGui::BeginTable(
                "##ArenaItemListTable",
                2,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 360.0f)
            )) {
                ImGui::TableSetupColumn(MenuText("Item"));
                ImGui::TableSetupColumn(MenuText("Action"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(equips.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const EquipTableEntry& equip = equips[row];
                        ImGui::PushID(equip.id);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(equip.name.c_str());
                        ImGui::TableSetColumnIndex(1);

                        if (DrawMenuButton("Grant", ImVec2(-1.0f, 0.0f))) {
                            GiveEquip(equip.id);
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("GogoCards")) {
            if (!HasArenaGogoCardBindings()) {
                DrawWaitingText("Waiting for GogoCard binding");
            }

            DrawAtomicCheckbox("Force selected GogoCards", FeatureState::ArenaGogoCardEnabled);
            ImGui::Text(
                MenuText("Card 1: %d  Card 2: %d"),
                FeatureState::ArenaGogoCardSelected1.load(),
                FeatureState::ArenaGogoCardSelected2.load()
            );
            if (DrawMenuButton("Clear card 1")) {
                FeatureState::ArenaGogoCardSelected1 = -1;
            }
            ImGui::SameLine();
            if (DrawMenuButton("Clear card 2")) {
                FeatureState::ArenaGogoCardSelected2 = -1;
            }
            ImGui::Separator();

            std::vector<CardTableEntry> cards = GetSortedCards();
            int totalCardCount = static_cast<int>(cards.size());
            ImGui::Text(
                MenuText("Showing %d / %d cards"),
                static_cast<int>(cards.size()),
                totalCardCount
            );

            if (cards.empty()) {
                if (totalCardCount == 0) {
                    DrawWaitingText("Waiting for GogoCard table");
                } else {
                    ImGui::TextUnformatted("No GogoCards available");
                }
            } else if (ImGui::BeginTable(
                "##ArenaGogoCardTable",
                3,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 340.0f)
            )) {
                ImGui::TableSetupColumn(MenuText("Card"));
                ImGui::TableSetupColumn(MenuText("Card 1"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn(MenuText("Card 2"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(cards.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const CardTableEntry& card = cards[row];
                        int selectedCard1 = FeatureState::ArenaGogoCardSelected1.load();
                        int selectedCard2 = FeatureState::ArenaGogoCardSelected2.load();

                        ImGui::PushID(card.id);
                        ImGui::TableNextRow();

                        if (card.id == selectedCard1 || card.id == selectedCard2) {
                            ImGui::TableSetBgColor(
                                ImGuiTableBgTarget_RowBg0,
                                ImGui::GetColorU32(ImVec4(0.25f, 0.55f, 0.25f, 0.35f))
                            );
                        }

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(card.name.c_str());
                        ImGui::TableSetColumnIndex(1);

                        if (DrawMenuButton("Select##card1", ImVec2(-1.0f, 0.0f))) {
                            FeatureState::ArenaGogoCardSelected1 = card.id;
                        }

                        ImGui::TableSetColumnIndex(2);

                        if (DrawMenuButton("Select##card2", ImVec2(-1.0f, 0.0f))) {
                            FeatureState::ArenaGogoCardSelected2 = card.id;
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Round")) {
            if (!HasArenaRoundSkipBindings()) {
                DrawWaitingText("Waiting for round skip bindings");
            }

            if (!HasArenaSpeedHackBindings()) {
                DrawWaitingText("Waiting for timeScale binding");
            }

            uint32_t currentRound = FeatureState::CachedGameRound.load();

            ImGui::Text(
                "%s: %s",
                MenuText("Current round"),
                currentRound > 0 ? FormatUInt32(currentRound).c_str() : MenuText("Waiting")
            );

            DrawAtomicCheckbox("Skip Round", FeatureState::ArenaSkipRound);
            ImGui::SetNextItemWidth(120.0f);
            DrawAtomicInputInt("Target round", FeatureState::ArenaSkipTargetRound);
            FeatureState::ArenaSkipTargetRound =
                std::clamp(FeatureState::ArenaSkipTargetRound.load(), 1, 99);

            if (DrawMenuButton("Apply Skip Round now", ImVec2(-1.0f, 0.0f))) {
                UiState::ConfigStatus =
                    TrySkipArenaRoundToTarget(true) ?
                        "Skip Round requested" :
                        "Skip Round unavailable or target reached";
            }

            ImGui::Separator();
            DrawAtomicCheckbox("SpeedHack", FeatureState::ArenaSpeedHack);
            ImGui::SetNextItemWidth(180.0f);
            DrawAtomicSliderFloat(
                "Time scale",
                FeatureState::ArenaTimeScale,
                0.1f,
                20.0f,
                "%.1fx"
            );
            FeatureState::ArenaTimeScale =
                ClampArenaTimeScale(FeatureState::ArenaTimeScale.load());

            if (DrawMenuButton("Reset time scale", ImVec2(-1.0f, 0.0f))) {
                FeatureState::ArenaSpeedHack = false;
                FeatureState::ArenaTimeScale = 1.0f;
                ApplyArenaSpeedHack(0);
            }

            ImGui::EndTabItem();
        }

        if (BeginMenuTabItem("Other")) {
            if (!Originals::MCBondUtil_CheckRelationActive_Config ||
                !Originals::MCBondUtil_CheckRelationActive_Special) {
                DrawWaitingText("Waiting for synergy hooks");
            }

            if (!HasArenaGoldBindings()) {
                DrawWaitingText("Waiting for player data bindings");
            }

            if (!HasArenaAchievementBindings()) {
                DrawWaitingText("Waiting for achievement bindings");
            }

            DrawAtomicCheckbox("Force all synergies active", FeatureState::ArenaForceActiveSynergy);
            DrawAtomicCheckbox("Force level and population 99", FeatureState::ArenaForceLevel99);
            DrawAtomicCheckbox("Allow outside-map placement", FeatureState::ArenaOutsideMapPlacement);
            DrawAtomicCheckbox("Set all enemy HP to 1", FeatureState::ArenaAllEnemyHpOne);
            DrawAtomicCheckbox(
                "Force Complete Achievements Task",
                FeatureState::ArenaForceCompleteAchievements
            );
            ImGui::Separator();
            ImGui::SetNextItemWidth(120.0f);
            DrawAtomicInputInt("Hero cost filter", FeatureState::ArenaPrice);
            FeatureState::ArenaPrice = std::clamp(FeatureState::ArenaPrice.load(), 0, 99);

            if (DrawMenuButton("Spawn all heroes with selected cost", ImVec2(-1.0f, 0.0f))) {
                int arenaPrice = FeatureState::ArenaPrice.load();
                int arenaHeroStar = FeatureState::ArenaHeroStar.load();

                for (const HeroTableEntry& hero : GetSortedHeroes(true)) {
                    if (hero.quality == arenaPrice) {
                        GiveHero(hero.id, arenaHeroStar);
                    }
                }
            }

            if (DrawMenuButton("Grant 999999 gold", ImVec2(-1.0f, 0.0f))) {
                GiveGold();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

struct MainMenuTab {
    const char* label;
    void (*draw)();
};

// Checks the compact display condition before work proceeds.
bool IsCompactDisplay() {
    ImGuiIO& io = ImGui::GetIO();
    return io.DisplaySize.x > 0.0f &&
        (io.DisplaySize.x < 700.0f || io.DisplaySize.y < 520.0f);
}

// Draws the main tab quick controls overlay section without changing game state.
void DrawMainTabQuickControls(const MainMenuTab* tabs, int tabCount) {
    if (!IsCompactDisplay() || !tabs || tabCount <= 0) {
        return;
    }

    int current = std::clamp(UiState::MainTabIndex.load(), 0, tabCount - 1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 10.0f));

    if (DrawMenuButton("Prev", ImVec2(92.0f, 0.0f))) {
        current = (current + tabCount - 1) % tabCount;
        UiState::MainTabIndex = current;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(MenuText(tabs[current].label));
    ImGui::SameLine();

    if (DrawMenuButton("Next", ImVec2(92.0f, 0.0f))) {
        current = (current + 1) % tabCount;
        UiState::MainTabIndex = current;
    }

    ImGui::PopStyleVar();
    ImGui::Spacing();
}

// Draws the menu tab button overlay section without changing game state.
void DrawMenuTabButton(const char* label, int index) {
    bool selected = UiState::MainTabIndex.load() == index;
    std::string localized = MenuLabel(label);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));

    if (ImGui::Selectable(localized.c_str(), selected, 0, ImVec2(-1.0f, 34.0f))) {
        UiState::MainTabIndex = index;
    }
    DrawMenuTooltip(label);

    ImGui::PopStyleVar(2);
}

// Returns the cached or live validated menu size value used by runtime features.
ImVec2 GetValidatedMenuSize() {
    ClampConfigurableState();

    ImGuiIO& io = ImGui::GetIO();
    const bool compactDisplay = IsCompactDisplay();
    float minWidth = compactDisplay ? 300.0f : 320.0f;
    float minHeight = compactDisplay ? 250.0f : 260.0f;
    float maxWidth = io.DisplaySize.x > 0.0f ?
        std::max(minWidth, io.DisplaySize.x - (compactDisplay ? 12.0f : 24.0f)) :
        1600.0f;
    float maxHeight = io.DisplaySize.y > 0.0f ?
        std::max(minHeight, io.DisplaySize.y - (compactDisplay ? 12.0f : 24.0f)) :
        1200.0f;

    return ImVec2(
        std::clamp(UiState::MenuWidth.load(), minWidth, maxWidth),
        std::clamp(UiState::MenuHeight.load(), minHeight, maxHeight)
    );
}

// Returns the cached or live validated menu position value used by runtime features.
ImVec2 GetValidatedMenuPosition(const ImVec2& menuSize) {
    ImGuiIO& io = ImGui::GetIO();
    float maxX = io.DisplaySize.x > 0.0f ? io.DisplaySize.x - 48.0f : 4000.0f;
    float maxY = io.DisplaySize.y > 0.0f ? io.DisplaySize.y - 48.0f : 4000.0f;
    float minX = menuSize.x > 0.0f ? 48.0f - menuSize.x : -2000.0f;
    float minY = menuSize.y > 0.0f ? 48.0f - menuSize.y : -2000.0f;

    return ImVec2(
        std::clamp(UiState::MenuPosX.load(), minX, maxX),
        std::clamp(UiState::MenuPosY.load(), minY, maxY)
    );
}

// Draws the main menu overlay section without changing game state.
void DrawMainMenu() {
    MaybeStartUpdateCheck(false);

    static const MainMenuTab tabs[] = {
        {"Info", DrawInfoTab},
        {"Combat", DrawCombatTab},
        {"Shop", DrawShopTab},
        {"Arena", DrawArenaTab},
        {"Appearance", DrawAppearanceTab},
        {"Settings", DrawSettingsTab}
    };

    constexpr int tabCount = static_cast<int>(IM_ARRAYSIZE(tabs));
    static_assert(tabCount > 0);

    UiState::MainTabIndex = std::clamp(UiState::MainTabIndex.load(), 0, tabCount - 1);

    const ImVec2 menuSize = GetValidatedMenuSize();
    const bool compactDisplay = IsCompactDisplay();

    if (UiState::UseFixedMenuPosition.load()) {
        ImGui::SetNextWindowPos(GetValidatedMenuPosition(menuSize), ImGuiCond_Always);
    } else if (compactDisplay) {
        ImGui::SetNextWindowPos(ImVec2(6.0f, 6.0f), ImGuiCond_Once);
    }

    ImGui::SetNextWindowSize(menuSize, compactDisplay ? ImGuiCond_Always : ImGuiCond_Once);

    if (!ImGui::Begin("MCGG", nullptr)) {
        ImGui::End();
        return;
    }

    UiCache::MenuWindowPos = ImGui::GetWindowPos();
    UiCache::MenuWindowSize = ImGui::GetWindowSize();

    DrawMainTabQuickControls(tabs, tabCount);

    static int lastDrawnTabIndex = -1;
    int requestedTabIndex = std::clamp(UiState::MainTabIndex.load(), 0, tabCount - 1);
    bool applyRequestedTab = requestedTabIndex != lastDrawnTabIndex;
    int activeTabIndex = -1;

    if (ImGui::BeginTabBar("##MainTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i = 0; i < tabCount; ++i) {
            ImGuiTabItemFlags itemFlags =
                applyRequestedTab && requestedTabIndex == i ?
                    ImGuiTabItemFlags_SetSelected :
                    0;

            if (BeginMenuTabItem(tabs[i].label, itemFlags)) {
                activeTabIndex = i;

                ImGui::Spacing();

                if (tabs[i].draw != nullptr) {
                    tabs[i].draw();
                }

                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    if (activeTabIndex >= 0) {
        UiState::MainTabIndex = activeTabIndex;
        lastDrawnTabIndex = activeTabIndex;
    }

    ImGui::End();
}

// Initializes overlay once the render context is valid.
bool InitializeOverlay() {
    if (ImGui::GetCurrentContext() != nullptr) {
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigWindowsResizeFromEdges = false;

    EnsureConfigPathInitialized();
    LoadConfigFromFile(UiState::ConfigPath, false);
    LoadAppearanceFonts();

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        AppearanceState::DefaultFont = nullptr;
        AppearanceState::NotoCjkFont = nullptr;
        ImGui::DestroyContext();
        return false;
    }

    ApplyAppearance();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.ScaleAllSizes(1.0f);

    return true;
}

// Attaches render il2 cpp thread only after IL2CPP is ready.
bool AttachRenderIl2CppThread(std::atomic<bool>& attached) {
    if (attached.load()) {
        return true;
    }

    if (!IsIl2CppRuntimeReady()) {
        return false;
    }

    Il2CppDomain* domain = il2cpp_domain_get();
    if (!domain || !il2cpp_thread_attach(domain)) {
        return false;
    }

    attached = true;
    return true;
}

namespace Hooks {
    // Tracks automatic regular shop refreshes so Scavenger cleanup can run before the passive resolves.
    void MCBattleBridge_OnRefreshShop(
        void* instance,
        bool isAutoRefresh,
        void* dictHeroSlot,
        void* sameRefreshHero,
        bool isDelayOpenShop,
        bool onlyRefreshHero
    ) {
        if (Originals::MCBattleBridge_OnRefreshShop) {
            Originals::MCBattleBridge_OnRefreshShop(
                instance,
                isAutoRefresh,
                dictHeroSlot,
                sameRefreshHero,
                isDelayOpenShop,
                onlyRefreshHero
            );
        }

        if (!isAutoRefresh || !FeatureState::ShopForceScavengerExpensiveHero.load()) {
            return;
        }

        FeatureState::ShopScavengerAutoRefreshPending = true;
        RefreshManagedReferences(true);

        uint64_t selfAccountId = GetSelfAccountId();
        if (selfAccountId == 0) {
            return;
        }

        if (RunScavengerShopCleanup(selfAccountId, std::chrono::steady_clock::now(), true)) {
            FeatureState::ShopScavengerAutoRefreshPending = false;
        }
    }

    // Hook wrapper for show spectator comp set spectate, applying feature overrides only when enabled.
    void MCShowSpectatorComp_SetSpectate(void* instance, uint64_t accountId) {
        if (FeatureState::CombatInvisibleScout) {
            return;
        }

        if (Originals::MCShowSpectatorComp_SetSpectate) {
            Originals::MCShowSpectatorComp_SetSpectate(instance, accountId);
        }
    }

    // Forwards free-buy checks through a hookable shop diagnostic path.
    bool MCLogicBattleData_ILOGIC_IsCurrFreeBuy(
        void* instance,
        uint64_t accountId,
        int slot,
        bool* needFx
    ) {
        return Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy ?
            Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy(instance, accountId, slot, needFx) :
            false;
    }

    // Hook wrapper for logic battle data ilogic get refresh cost.
    int MCLogicBattleData_ILOGIC_GetRefreshCost(void* instance, uint64_t accountId) {
        return Originals::MCLogicBattleData_ILOGIC_GetRefreshCost ?
            Originals::MCLogicBattleData_ILOGIC_GetRefreshCost(instance, accountId) :
            0;
    }

    // Hook wrapper for logic battle data ilogic is refresh free.
    bool MCLogicBattleData_ILOGIC_IsRefreshFree(void* instance, uint64_t accountId) {
        return Originals::MCLogicBattleData_ILOGIC_IsRefreshFree ?
            Originals::MCLogicBattleData_ILOGIC_IsRefreshFree(instance, accountId) :
            false;
    }

    // Hook wrapper for logic battle data i logic hero count in pool.
    int MCLogicBattleData_ILogic_HeroCountInPool(void* instance, int heroId) {
        return Originals::MCLogicBattleData_ILogic_HeroCountInPool ?
            Originals::MCLogicBattleData_ILogic_HeroCountInPool(instance, heroId) :
            0;
    }

    // Masks recorded self losses when HP-loss prevention is active.
    bool MCLogicBattleData_ILOGIC_GetBattleResultHistory(
        void* instance,
        uint64_t accountId,
        int round
    ) {
        if (FeatureState::CombatForceWin && IsSelfAccount(accountId)) {
            return true;
        }

        return Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory ?
            Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory(
                instance,
                accountId,
                round
            ) :
            false;
    }

    // Forwards upgrade checks through a hookable economy diagnostic path.
    bool MCLogicBattleData_ILOGIC_CanUpgrade(
        void* instance,
        uint64_t accountId,
        int coin
    ) {
        return Originals::MCLogicBattleData_ILOGIC_CanUpgrade ?
            Originals::MCLogicBattleData_ILOGIC_CanUpgrade(instance, accountId, coin) :
            false;
    }

    // Hook wrapper for logic battle data ilogic get shop is forbid.
    bool MCLogicBattleData_ILOGIC_GetShopIsForbid(void* instance, uint64_t accountId) {
        return Originals::MCLogicBattleData_ILOGIC_GetShopIsForbid ?
            Originals::MCLogicBattleData_ILOGIC_GetShopIsForbid(instance, accountId) :
            false;
    }

    // Hook wrapper for logic battle data ilogic get upgrade cost.
    int MCLogicBattleData_ILOGIC_GetUpgradeCost(void* instance, uint64_t accountId) {
        return Originals::MCLogicBattleData_ILOGIC_GetUpgradeCost ?
            Originals::MCLogicBattleData_ILOGIC_GetUpgradeCost(instance, accountId) :
            0;
    }

    // Hook wrapper for logic battle manager get m b defend faild, applying feature overrides only when enabled.
    bool MCLogicBattleManager_get_m_bDefendFaild(void* instance) {
        if (FeatureState::CombatForceWin && IsSelfBattleManager(instance)) {
            return false;
        }

        return Originals::MCLogicBattleManager_get_m_bDefendFaild ?
            Originals::MCLogicBattleManager_get_m_bDefendFaild(instance) :
            false;
    }

    // Suppresses self HP loss when prevention is enabled, then forwards safe changes.
    void MCLogicBattleManager_OnModifyPlayerBlood(
        void* instance,
        int reduceHp,
        bool isFromCurse,
        int effectId
    ) {
        if ((FeatureState::CombatPreventHpLoss || FeatureState::CombatForceWin) &&
            reduceHp > 0 &&
            IsSelfBattleManager(instance)) {
            return;
        }

        if (Originals::MCLogicBattleManager_OnModifyPlayerBlood) {
            Originals::MCLogicBattleManager_OnModifyPlayerBlood(
                instance,
                reduceHp,
                isFromCurse,
                effectId
            );
        }
    }

    // Forces selected self fight results to win before forwarding to the original method.
    void MCLogicBattleManager_OnFightOver(
        void* instance,
        bool failed,
        bool includeInvader,
        bool doubleFailed
    ) {
        if (FeatureState::CombatForceWin && IsSelfBattleManager(instance)) {
            failed = false;
            doubleFailed = false;
        }

        if (Originals::MCLogicBattleManager_OnFightOver) {
            Originals::MCLogicBattleManager_OnFightOver(
                instance,
                failed,
                includeInvader,
                doubleFailed
            );
        }
    }

    // Keeps regular active-synergy checks satisfied when synergy forcing is enabled.
    bool MCBondUtil_CheckRelationActive_Config(
        void* config,
        int curActiveCount,
        void* curBondDict
    ) {
        if (FeatureState::ArenaForceActiveSynergy) {
            return true;
        }

        return Originals::MCBondUtil_CheckRelationActive_Config ?
            Originals::MCBondUtil_CheckRelationActive_Config(
                config,
                curActiveCount,
                curBondDict
            ) :
            false;
    }

    // Keeps special-condition synergy checks satisfied when synergy forcing is enabled.
    bool MCBondUtil_CheckRelationActive_Special(
        void* specialCondition,
        int needCount,
        int curActiveCount,
        void* curBondDict
    ) {
        if (FeatureState::ArenaForceActiveSynergy) {
            return true;
        }

        return Originals::MCBondUtil_CheckRelationActive_Special ?
            Originals::MCBondUtil_CheckRelationActive_Special(
                specialCondition,
                needCount,
                curActiveCount,
                curBondDict
            ) :
            false;
    }

    // Forces generic achievement result checks to succeed when the Arena assist is enabled.
    bool MCLogicAchievementRecordComp_AchievementDataBase_GetResult(void* instance) {
        if (ShouldForceCompleteAchievements()) {
            ForceAchievementRoundDataComplete(instance);
            return true;
        }

        return Originals::MCLogicAchievementRecordComp_AchievementDataBase_GetResult ?
            Originals::MCLogicAchievementRecordComp_AchievementDataBase_GetResult(instance) :
            false;
    }

    // Allows achievement data to record progress when the Arena assist is enabled.
    bool MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData(
        void* instance
    ) {
        if (ShouldForceCompleteAchievements()) {
            return true;
        }

        return Originals::MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData ?
            Originals::MCLogicAchievementRecordComp_AchievementDataBase_canRecordAchievementData(
                instance
            ) :
            false;
    }

    // Forces final relation checks for achievement tasks when the Arena assist is enabled.
    bool MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation(
        void* instance
    ) {
        if (ShouldForceCompleteAchievements()) {
            return true;
        }

        return Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation ?
            Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeFinalRelation(
                instance
            ) :
            false;
    }

    // Forces achievement reach-condition checks when the Arena assist is enabled.
    bool MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition(
        void* instance,
        void* players
    ) {
        if (ShouldForceCompleteAchievements()) {
            ForceAchievementRoundDataComplete(instance);
            return true;
        }

        return Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition ?
            Originals::MCLogicAchievementRecordComp_AchievementDataBase_JudgeReachCondition(
                instance,
                players
            ) :
            false;
    }

    // Forces round achievement counters and result checks to completed when enabled.
    bool MCLogicAchievementRecordComp_AchievementRoundData_GetResult(void* instance) {
        if (ShouldForceCompleteAchievements()) {
            ForceAchievementRoundDataComplete(instance);
            return true;
        }

        return Originals::MCLogicAchievementRecordComp_AchievementRoundData_GetResult ?
            Originals::MCLogicAchievementRecordComp_AchievementRoundData_GetResult(instance) :
            false;
    }

    // Re-applies completed round counters after the game refreshes achievement data.
    void MCLogicAchievementRecordComp_AchievementRoundData_RefreshData(void* instance) {
        if (Originals::MCLogicAchievementRecordComp_AchievementRoundData_RefreshData) {
            Originals::MCLogicAchievementRecordComp_AchievementRoundData_RefreshData(instance);
        }

        if (ShouldForceCompleteAchievements()) {
            ForceAchievementRoundDataComplete(instance);
        }
    }

    // Hook wrapper for show battle touch mgr clamp grid pos, applying feature overrides only when enabled.
    AstarInt2 ShowBattleTouchMgr_ClampGridPos(void* instance, AstarInt2 gridPos) {
        AstarInt2 result = gridPos;

        if (Originals::ShowBattleTouchMgr_ClampGridPos) {
            result = Originals::ShowBattleTouchMgr_ClampGridPos(instance, gridPos);
        }

        if (FeatureState::ArenaOutsideMapPlacement) {
            result.y += 6;
        }

        return result;
    }

    // Hook wrapper for a star tile map valid pos, applying feature overrides only when enabled.
    bool AStarTileMap_ValidPos(int x, int y) {
        if (FeatureState::ArenaOutsideMapPlacement) {
            return true;
        }

        return Originals::AStarTileMap_ValidPos ?
            Originals::AStarTileMap_ValidPos(x, y) :
            false;
    }

    // Hook wrapper for logic entity map can walkable, applying feature overrides only when enabled.
    bool MCLogicEntityMap_CanWalkable(void* instance, int x, int y) {
        if (FeatureState::ArenaOutsideMapPlacement) {
            return true;
        }

        return Originals::MCLogicEntityMap_CanWalkable ?
            Originals::MCLogicEntityMap_CanWalkable(instance, x, y) :
            false;
    }

    // Hook wrapper for logic entity map is walkable around, applying feature overrides only when enabled.
    bool MCLogicEntityMap_IsWalkableAround(void* instance, int x, int y) {
        if (FeatureState::ArenaOutsideMapPlacement) {
            return true;
        }

        return Originals::MCLogicEntityMap_IsWalkableAround ?
            Originals::MCLogicEntityMap_IsWalkableAround(instance, x, y) :
            false;
    }

    // Renders the ImGui overlay before the frame is swapped.
    EGLBoolean EglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
        static std::atomic<bool> imguiReady{false};
        static std::atomic<bool> renderThreadAttached{false};

        if (!Originals::EglSwapBuffers) {
            return EGL_FALSE;
        }

        if (dpy == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) {
            return Originals::EglSwapBuffers(dpy, surface);
        }

        EGLint width = 0;
        EGLint height = 0;

        if (!eglQuerySurface(dpy, surface, EGL_WIDTH, &width) ||
            !eglQuerySurface(dpy, surface, EGL_HEIGHT, &height) ||
            width <= 0 ||
            height <= 0) {
            return Originals::EglSwapBuffers(dpy, surface);
        }

        GLWidth = width;
        GLHeight = height;

        if (!imguiReady) {
            imguiReady = InitializeOverlay();

            if (!imguiReady) {
                return Originals::EglSwapBuffers(dpy, surface);
            }
        }

        bool managedReady = AttachRenderIl2CppThread(renderThreadAttached);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)GLWidth, (float)GLHeight);

        ApplyAppearance();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        if (managedReady) {
            TickFeatures();
        }

        DrawMainMenu();
        DrawNextEnemyHud();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        return Originals::EglSwapBuffers(dpy, surface);
    }

    // Forwards Unity touch input into ImGui mouse input.
    Touch Input_GetTouch(int index) {
        Touch ret{};

        if (!Originals::Input_GetTouch) {
            return ret;
        }

        ret = Originals::Input_GetTouch(index);

        if (ImGui::GetCurrentContext() != nullptr && index == 0) {
            ImGuiIO& io = ImGui::GetIO();

            float x = ret.m_Position.x;
            float y = io.DisplaySize.y - ret.m_Position.y;

            if (io.DisplaySize.x <= 0.0f ||
                io.DisplaySize.y <= 0.0f ||
                !Unity::IsFinite(x) ||
                !Unity::IsFinite(y)) {
                return ret;
            }

            switch (ret.m_Phase) {
                case TouchPhase::Began:
                    io.AddMousePosEvent(x, y);
                    io.AddMouseButtonEvent(0, true);
                    break;

                case TouchPhase::Moved:
                case TouchPhase::Stationary:
                    io.AddMousePosEvent(x, y);
                    break;

                case TouchPhase::Ended:
                case TouchPhase::Canceled:
                    io.AddMousePosEvent(x, y);
                    io.AddMouseButtonEvent(0, false);
                    break;

                default:
                    break;
            }
        }

        return ret;
    }
}

// Waits for game libraries, resolves IL2CPP APIs, and installs hooks.
void SetupThread() {
    sleep(8);

    void* swapBuffers = nullptr;

    while (!swapBuffers) {
        swapBuffers = DobbySymbolResolver(nullptr, "eglSwapBuffers");

        if (!swapBuffers) {
            sleep(2);
        }
    }

    DobbyHook(
        swapBuffers,
        (void*)Hooks::EglSwapBuffers,
        (void**)&Originals::EglSwapBuffers
    );

    while (!handle.liblogic) {
        sleep(2);
        handle.liblogic = xdl_open("liblogic.so", XDL_DEFAULT);
    }

    sleep(2);

#define DO_API(ret, name, args) \
    name = reinterpret_cast<decltype(name)>(xdl_sym(handle.liblogic, #name, nullptr));

#include "Il2CppVersions/api/2019.4.33f1.h"

#undef DO_API

    if (!HasIl2CppDomainApi()) {
        return;
    }

    Il2CppDomain* domain = il2cpp_domain_get();
    if (!domain) {
        return;
    }

    if (!il2cpp_thread_attach(domain)) {
        return;
    }

    RuntimeState::Il2CppReady = true;
    ResolveFeatureBindings();
    RefreshManagedReferences(true);

    auto GetTouch_Methods =
        GetAllMethodsFromName("UnityEngine", "Input", "GetTouch", {"int"});

    if (!GetTouch_Methods.empty()) {
        DobbyHook(
            GetTouch_Methods[0],
            (void*)Hooks::Input_GetTouch,
            (void**)&Originals::Input_GetTouch
        );
    }

    RuntimeState::BindingRetryRequested = true;
}

// Starts hook setup when this shared library is loaded in the target process.
__attribute__((constructor))
void InitLibrary() {
    if (!IsUnityMoontonProcess()) {
        return;
    }

    std::thread(SetupThread).detach();
}

// Loads the original Unity library and forwards its JNI_OnLoad.
jboolean LoadOriginalLibrary(JNIEnv* env, jobject, jstring path) {
    if (!env || !path) {
        return JNI_FALSE;
    }

    const char* libraryPath = env->GetStringUTFChars(path, nullptr);
    if (!libraryPath) {
        return JNI_FALSE;
    }

    char fullPath[1024];
    int written = snprintf(fullPath, sizeof(fullPath), "%s/%s", libraryPath, "libunity.so");

    env->ReleaseStringUTFChars(path, libraryPath);

    if (written <= 0 || written >= static_cast<int>(sizeof(fullPath))) {
        return JNI_FALSE;
    }

    UnityLibraryHandle = dlopen(fullPath, RTLD_LAZY | RTLD_LOCAL);
    if (!UnityLibraryHandle) {
        return JNI_FALSE;
    }

    auto jniOnLoad =
        (jint (*)(JavaVM*, void*))dlsym(UnityLibraryHandle, "JNI_OnLoad");

    if (!jniOnLoad) {
        dlclose(UnityLibraryHandle);
        UnityLibraryHandle = nullptr;
        return JNI_FALSE;
    }

    JavaVM* vm = nullptr;

    if (env->GetJavaVM(&vm) != JNI_OK || !vm) {
        dlclose(UnityLibraryHandle);
        UnityLibraryHandle = nullptr;
        return JNI_FALSE;
    }

    jint result = jniOnLoad(vm, nullptr);

    if (result < JNI_VERSION_1_6) {
        dlclose(UnityLibraryHandle);
        UnityLibraryHandle = nullptr;
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

// Unloads the original Unity library handle if it is open.
jboolean UnloadOriginalLibrary(JNIEnv*, jclass) {
    if (UnityLibraryHandle) {
        dlclose(UnityLibraryHandle);
        UnityLibraryHandle = nullptr;
    }

    return JNI_TRUE;
}

// Registers the native loader bridge used by the Unity Java side.
extern "C" __attribute__((used, visibility("default")))
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* key) {
    if (!vm) {
        return JNI_VERSION_1_6;
    }

    JNIEnv* env = nullptr;

    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
        return JNI_VERSION_1_6;
    }

    jclass clazz = env->FindClass("com/unity3d/player/NativeLoader");
    if (!clazz) {
        return JNI_VERSION_1_6;
    }

    const JNINativeMethod methods[] = {
        {
            "load",
            "(Ljava/lang/String;)Z",
            (void*)LoadOriginalLibrary
        },
        {
            "unload",
            "()Z",
            (void*)UnloadOriginalLibrary
        }
    };

    if (env->RegisterNatives(
            clazz,
            methods,
            sizeof(methods) / sizeof(JNINativeMethod)
        ) != JNI_OK) {
        return JNI_VERSION_1_6;
    }

    return JNI_VERSION_1_6;
}
