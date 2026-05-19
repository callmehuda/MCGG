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
#include <array>
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
    constexpr int AutoPlayTickMs = 250;
    constexpr int OpponentPredictionTickMs = 500;
    constexpr int GgcInfoRefreshMs = 500;
    constexpr int GgcRoundScanStart = 1;
    constexpr int GgcRoundScanEnd = 99;
    constexpr int FeatureFrameBudgetMs = 12;
    constexpr int ShopActionCooldownMs = 350;
    constexpr int ShopRepeatBuyCooldownMs = 1500;
    constexpr int ShopRefreshCooldownMs = 650;
    constexpr int ShopWorthCheckMs = 500;
    constexpr int RecommendLineupCheckMs = 500;
    constexpr int ArenaSkipCooldownMs = 750;
    constexpr int AutoPlayAiStartCooldownMs = 2000;
    constexpr int AutoPlayAiRefreshMs = 8000;
    constexpr int AutoPlayDeployCooldownMs = 750;
    constexpr int AutoPlayFormationCooldownMs = 1000;
    constexpr int AutoPlayLevelCooldownMs = 900;
    constexpr int AutoPlayAuctionCooldownMs = 750;
    constexpr int UpdateCheckRefreshMs = 6 * 60 * 60 * 1000;
    constexpr int UpdateCheckRetryBaseMs = 5 * 60 * 1000;
    constexpr int UpdateCheckRetryMaxMs = 60 * 60 * 1000;
    constexpr int MaxShopTargetChecks = 256;
    constexpr int MaxAutoPlayFallbackEnemyManagers = 4;
    constexpr int MaxManagedDictionaryEntries = 8192;
    constexpr int MaxManagedListItems = 2048;
    constexpr int MaxManagedStringChars = 4096;
    constexpr int MaxOpponentHistoryRounds = 32;
    constexpr size_t MaxReleaseResponseBytes = 512 * 1024;
    constexpr size_t MaxReleaseBodyPreviewChars = 12000;
    constexpr size_t MaxInstanceFieldOffsetBytes = 1024 * 1024;
}

namespace RuntimeMutex {
    std::mutex CacheMutex;
    std::mutex FeatureMutex;
    std::mutex UpdateMutex;
    std::recursive_mutex UiMutex;
}

namespace RuntimeState {
    std::atomic<bool> Il2CppReady{false};
    std::atomic<bool> BindingRetryRequested{false};
    std::atomic<bool> BindingResolveInProgress{false};
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

// Feature toggles, cached managed references, and throttled runtime state.
namespace FeatureState {
    std::atomic<bool> AutoPlayEnabled{false};
    std::atomic<bool> AutoPlayAdaptive{true};
    std::atomic<bool> AutoPlayUseBuiltInAI{false};
    std::atomic<bool> AutoPlayUseShop{true};
    std::atomic<bool> AutoPlayUseEconomy{true};
    std::atomic<bool> AutoPlayUseCombat{true};
    std::atomic<bool> AutoPlayUseArenaAssist{true};
    std::atomic<bool> AutoPlayUseFormation{true};
    std::atomic<bool> AutoPlayUseAuction{true};
    std::atomic<bool> AutoPlayUseGoGoCards{true};
    std::atomic<int> AutoPlayAiDifficulty{3};
    std::atomic<int> AutoPlayMinReserveGold{20};
    std::atomic<int> AutoPlayTargetLevel{9};
    std::atomic<int> AutoPlayPressure{25};
    std::atomic<int> AutoPlayStrategy{1};
    std::atomic<int> AutoPlayLearnedRounds{0};
    std::atomic<int> AutoPlayStrategyChanges{0};
    std::atomic<int> AutoPlayLastRound{0};
    std::atomic<int> AutoPlayLastHp{-1};
    std::atomic<bool> AutoPlayWasRunning{false};
    std::atomic<bool> AutoPlayBuiltInAiRunning{false};
    std::atomic<uint64_t> AutoPlayLastAiStartAccountId{0};
    std::atomic<int> AutoPlayFocusGroup{0};
    std::atomic<int> AutoPlayTargetHeroId{0};
    std::atomic<int> AutoPlayBestCardId{0};
    std::atomic<int> AutoPlayBestAuctionIndex{-1};
    std::atomic<int> AutoPlayBestAuctionScore{0};
    std::atomic<int> AutoPlayBoardSelfUnits{0};
    std::atomic<int> AutoPlayBoardEnemyUnits{0};
    std::atomic<int> AutoPlayBoardMoves{0};
    std::atomic<int> AutoPlayLastMoveHeroId{0};
    std::atomic<int> AutoPlayLastMoveGain{0};
    std::atomic<int> AutoPlayLastMoveX{-1};
    std::atomic<int> AutoPlayLastMoveY{-1};
    std::atomic<int> AutoPlayOpponentCount{0};
    std::atomic<int> AutoPlayContestedTargets{0};
    std::atomic<int> AutoPlayStrongestOpponentFightValue{0};
    std::atomic<uint64_t> AutoPlayCurrentOpponentId{0};
    std::atomic<int> AutoPlayInterestTier{0};
    std::atomic<int> AutoPlayInterestNextGold{10};
    std::atomic<int> AutoPlayInterestReserveGold{20};
    std::atomic<int> AutoPlaySpendBudget{0};
    std::atomic<bool> AutoPlayHoldInterest{false};
    std::atomic<bool> AutoPlaySnapshotReady{false};
    std::atomic<uint64_t> AutoPlaySnapshotAccountId{0};
    std::atomic<int> AutoPlaySnapshotRound{0};
    std::atomic<int> AutoPlaySnapshotPhase{-1};
    std::atomic<int> AutoPlaySnapshotHp{-1};
    std::atomic<int> AutoPlaySnapshotCoin{-1};
    std::atomic<int> AutoPlaySnapshotLevel{-1};
    std::atomic<int> AutoPlaySnapshotCurrentPopulation{-1};
    std::atomic<int> AutoPlaySnapshotTotalPopulation{-1};
    std::atomic<int> AutoPlaySnapshotLineupWorth{-1};
    std::atomic<int> AutoPlaySnapshotFightValue{-1};
    std::atomic<int> AutoPlaySnapshotRecommendHeroId{0};
    std::atomic<int> AutoPlaySnapshotStarUpHeroId{0};
    std::atomic<int> AutoPlaySnapshotCurrentOpponentFightValue{-1};
    std::atomic<bool> AutoPlaySnapshotFightSection{false};
    std::atomic<bool> AutoPlaySnapshotMonsterRound{false};

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
    std::atomic<bool> ShopRefresh{false};
    std::atomic<bool> ShopStopRefreshAtFreeHero{false};
    std::atomic<bool> ShopStopRefreshAtSelectedHero{false};
    std::atomic<bool> ShopStopRefreshAtRecommendLineup{false};
    std::atomic<bool> ShopKeepGold{false};
    std::atomic<int> ShopKeepGoldAt{20};
    std::atomic<int> ShopRecommendTargetCount{9};
    std::unordered_map<int, HeroAutomationState> ShopSelectedHeroes;

    std::atomic<int> ArenaHeroStar{1};
    std::atomic<bool> ArenaItemEnhanced{false};
    std::atomic<bool> ArenaGogoCardEnabled{false};
    std::atomic<int> ArenaGogoCardSelected1{-1};
    std::atomic<int> ArenaGogoCardSelected2{-1};
    std::atomic<bool> ArenaForceActiveSynergy{false};
    std::atomic<bool> ArenaForceLevel99{false};
    std::atomic<bool> ArenaOutsideMapPlacement{false};
    std::atomic<bool> ArenaAllEnemyHpOne{false};
    std::atomic<bool> ArenaPassiveGold{false};
    std::atomic<int> ArenaGoldTarget{999999};
    std::atomic<bool> ArenaFreeEconomy{false};
    std::atomic<bool> ArenaUnlimitedHeroPool{false};
    std::atomic<bool> ArenaNoShopLock{false};
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

    std::atomic<bool> TableDataLoaded{false};
    std::atomic<bool> WasInMatch{false};
    std::atomic<uint64_t> LastSelfAccountId{0};
    std::unordered_map<int, HeroTableEntry> Heroes;
    std::unordered_map<int, EquipTableEntry> Equips;
    std::unordered_map<int, CardTableEntry> Cards;

    std::chrono::steady_clock::time_point LastBindingRetry{};
    std::chrono::steady_clock::time_point LastReferenceRefresh{};
    std::chrono::steady_clock::time_point LastArenaTick{};
    std::chrono::steady_clock::time_point LastShopTick{};
    std::chrono::steady_clock::time_point LastCombatTick{};
    std::chrono::steady_clock::time_point LastAutoPlayTick{};
    std::chrono::steady_clock::time_point LastOpponentPredictionTick{};
    std::chrono::steady_clock::time_point LastMatchStateCheck{};
    std::chrono::steady_clock::time_point LastTableLoadAttempt{};
    std::chrono::steady_clock::time_point LastShopAction{};
    std::chrono::steady_clock::time_point LastShopBuyAttempt{};
    std::chrono::steady_clock::time_point LastShopRefreshAttempt{};
    std::chrono::steady_clock::time_point LastShopWorthCheck{};
    std::chrono::steady_clock::time_point LastRecommendLineupCheck{};
    std::chrono::steady_clock::time_point LastArenaSkipAttempt{};
    std::chrono::steady_clock::time_point LastAutoPlayAiStartAttempt{};
    std::chrono::steady_clock::time_point LastAutoPlayDeployAttempt{};
    std::chrono::steady_clock::time_point LastAutoPlayFormationAttempt{};
    std::chrono::steady_clock::time_point LastAutoPlayLevelAttempt{};
    std::chrono::steady_clock::time_point LastAutoPlayAuctionAttempt{};
    std::atomic<bool> CachedShopHasWorthwhileTarget{false};
    std::atomic<int> CachedRecommendLineupHeroId{0};
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
    MainTabAutoPlay = 2,
    MainTabShop = 3,
    MainTabArena = 4,
    MainTabAppearance = 5,
    MainTabSettings = 6,
    MainTabTest = 7
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
    void* (*LogicRoundMgr_get_m_AuctionComp)(void* instance);
    void (*UnityEngine_Time_set_timeScale)(float value);

    void* (*MCComp_GetGamer)(uint64_t accountId);
    void* (*MCComp_GetGoGoCardComp)(uint64_t accountId);
    void* (*MCLogicGoGoCardComp_get_m_CurrData)(void* instance);

    void* (*CData_MCHero_GetInstance)();
    MonoStructures::Dictionary<int, void*>* (*CData_MCHero_GetAll)(void* instance);
    void* (*CData_MCEquipBase_GetInstance)();
    MonoStructures::Dictionary<int, MonoStructures::Dictionary<int, void*>*>*
        (*CData_MCEquipBase_GetAll)(void* instance);
    void* (*CData_MCSuperCrystalKey_GetInstance)();
    MonoStructures::Dictionary<int, void*>* (*CData_MCSuperCrystalKey_GetAll)(void* instance);
    Il2CppString* (*ShowMsgTool_GetDesc)(int id);
    bool (*LoadRes_IsCommander)(void* instance, int heroId);

    bool (*MCLogicBattleManager_BuyNormalHero)(
        void* instance,
        MCLogicHeroShopItemData* itemData,
        bool* ignoreExtraRule
    );
    void (*MCLogicBattleManager_StartAI)(void* instance, int difficulty);
    void (*MCLogicBattleManager_StopAI)(void* instance);
    void (*MCLogicBattleManager_TryAutoDeploy)(void* instance);
    bool (*MCLogicBattleManager_OnPlayerLvlUp)(void* instance);
    int (*MCLogicBattleManager_GetLineupWorth)(void* instance);
    int (*MCLogicBattleManager_CalcCurrentFightValue)(void* instance);
    bool (*MCLogicBattleManager_MoveHeroToBattleField_ById)(
        void* instance,
        uint32_t instanceId,
        uint8_t gridX,
        uint8_t gridY,
        bool byAI,
        bool bIsAIApi
    );
    void (*MCLogicBattleManager_MoveHeroInBattleField)(
        void* instance,
        uint32_t instanceId,
        uint8_t gridX,
        uint8_t gridY,
        bool bIsAi
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
    int64_t (*MCBattleBridge_GetFreeMemory)(void* instance);
    uint32_t (*MCBattleBridge_GetPingTimes)(void* instance);
    float (*MCBattleBridge_GetStdevPing)(void* instance);
    float (*MCBattleBridge_GetStdevFps)(void* instance);
    void (*MCChessPlayerData_UpdateCoin)(void* instance, int addValue, int changeType);
    int (*MCLogicAuctionComp_get_m_CurrPhase)(void* instance);
    bool (*MCLogicAuctionComp_Bid)(
        void* instance,
        void* slotInfo,
        uint64_t accountId,
        uint32_t bidPrice
    );
    void (*MCLogicAuctionComp_FixedPrice)(void* instance, void* slotInfo, uint64_t accountId);
    int (*MCLogicAuctionComp_GetSelectItemIndexByAccId)(void* instance, uint64_t accountId);

    void (*MCShowSpectatorComp_SetSpectate)(void* instance, uint64_t accountId);
    bool (*MCBondUtil_CheckRelationActive_Config)(void* config, int curActiveCount, void* curBondDict);
    bool (*MCBondUtil_CheckRelationActive_Special)(
        void* specialCondition,
        int needCount,
        int curActiveCount,
        void* curBondDict
    );
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

// Checks whether this render frame has spent its managed-work budget.
bool FrameBudgetExceeded(
    const std::chrono::steady_clock::time_point& frameStart,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
) {
    return now - frameStart >= std::chrono::milliseconds(RuntimeConfig::FeatureFrameBudgetMs);
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
        Originals::LogicRoundMgr_get_m_AuctionComp,
        "",
        "LogicRoundMgr",
        "get_m_AuctionComp",
        {}
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
        Originals::MCLogicGoGoCardComp_get_m_CurrData,
        "Battle",
        "MCLogicGoGoCardComp",
        "get_m_CurrData",
        {}
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
    ResolveOriginal(Originals::ShowMsgTool_GetDesc, "", "ShowMsgTool", "GetDesc", {"Int32"});
    ResolveOriginal(Originals::LoadRes_IsCommander, "", "LoadRes", "IsCommander", {"Int32"});
    ResolveOriginal(
        Originals::MCLogicBattleManager_BuyNormalHero,
        "",
        "MCLogicBattleManager",
        "BuyNormalHero",
        {"MCLogicHeroShopItemData", "Boolean"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_StartAI,
        "",
        "MCLogicBattleManager",
        "StartAI",
        {"Int32"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_StopAI,
        "",
        "MCLogicBattleManager",
        "StopAI",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_TryAutoDeploy,
        "",
        "MCLogicBattleManager",
        "TryAutoDeploy",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_OnPlayerLvlUp,
        "",
        "MCLogicBattleManager",
        "OnPlayerLvlUp",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_GetLineupWorth,
        "",
        "MCLogicBattleManager",
        "GetLineupWorth",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_CalcCurrentFightValue,
        "",
        "MCLogicBattleManager",
        "CalcCurrentFightValue",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_MoveHeroToBattleField_ById,
        "",
        "MCLogicBattleManager",
        "MoveHeroToBattleField",
        {"UInt32", "Byte", "Byte", "Boolean", "Boolean"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleManager_MoveHeroInBattleField,
        "",
        "MCLogicBattleManager",
        "MoveHeroInBattleField",
        {"UInt32", "Byte", "Byte", "Boolean"}
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
    ResolveOriginal(
        Originals::MCLogicAuctionComp_get_m_CurrPhase,
        "Battle",
        "MCLogicAuctionComp",
        "get_m_CurrPhase",
        {}
    );
    ResolveOriginal(
        Originals::MCLogicAuctionComp_Bid,
        "Battle",
        "MCLogicAuctionComp",
        "Bid",
        {"MCLogicAuctionSlotInfo", "UInt64", "UInt32"}
    );
    ResolveOriginal(
        Originals::MCLogicAuctionComp_FixedPrice,
        "Battle",
        "MCLogicAuctionComp",
        "FixedPrice",
        {"MCLogicAuctionSlotInfo", "UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicAuctionComp_GetSelectItemIndexByAccId,
        "Battle",
        "MCLogicAuctionComp",
        "GetSelectItemIndexByAccId",
        {"UInt64"}
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
    HookResolvedMethod(
        Originals::MCBondUtil_CheckRelationActive_Special,
        (void*)Hooks::MCBondUtil_CheckRelationActive_Special,
        "Battle",
        "MCBondUtil",
        "CheckRelationActive",
        {"Int32", "Int32", "Int32", "Dictionary"}
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
        Originals::MCLogicBattleManager_GetCurrentOpponent(battleManager) :
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
        Originals::MCLogicBattleData_ILOGIC_GetIsMonsterRound(nullptr)) {
        return true;
    }

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound ?
        Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr) :
        0;

    return invasionManager &&
        round > 0 &&
        Originals::LogicInvasionMgr_IsMonsterRound &&
        Originals::LogicInvasionMgr_IsMonsterRound(invasionManager, round);
}

// Checks the real player pairing mode condition before work proceeds.
bool IsRealPlayerPairingMode(void* invasionManager) {
    if (Originals::MCLogicBattleData_ILOGIC_IsRealPlayerMode) {
        return Originals::MCLogicBattleData_ILOGIC_IsRealPlayerMode(nullptr);
    }

    if (invasionManager && Originals::LogicInvasionMgr_IsRealPlayerMode) {
        return Originals::LogicInvasionMgr_IsRealPlayerMode(invasionManager);
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

    FeatureState::BattleBridge = battleBridge;
    FeatureState::LoadResInstance = loadResInstance;
    FeatureState::HeroShopPanel = heroShopPanel;
    FeatureState::HeroShopItemList = heroShopItemList;
}

// Returns the cached or live table cache counts value used by runtime features.
TableCacheCounts GetTableCacheCounts() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    return {
        static_cast<int>(FeatureState::Heroes.size()),
        static_cast<int>(FeatureState::Equips.size()),
        static_cast<int>(FeatureState::Cards.size())
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

// Updates shop hero target through the safest available runtime path.
void SetShopHeroTarget(int heroId, const HeroAutomationState& state) {
    if (heroId <= 0 || heroId > 10000000) {
        return;
    }

    HeroAutomationState clampedState = state;
    clampedState.targetCount = std::clamp(clampedState.targetCount, 1, 99);

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
    FeatureState::LastTableLoadAttempt = {};
    FeatureState::LastShopAction = {};
    FeatureState::LastShopBuyAttempt = {};
    FeatureState::LastShopRefreshAttempt = {};
    FeatureState::LastShopWorthCheck = {};
    FeatureState::LastRecommendLineupCheck = {};
    FeatureState::LastArenaSkipAttempt = {};
    FeatureState::LastAutoPlayAiStartAttempt = {};
    FeatureState::LastAutoPlayDeployAttempt = {};
    FeatureState::LastAutoPlayFormationAttempt = {};
    FeatureState::LastAutoPlayLevelAttempt = {};
    FeatureState::LastAutoPlayAuctionAttempt = {};
    FeatureState::ArenaLastSkipSourceRound = 0;
    FeatureState::ArenaLastSkipTargetRound = 0;
    FeatureState::CachedGameRound = 0;
    FeatureState::CachedShopHasWorthwhileTarget = false;
    FeatureState::CachedRecommendLineupHeroId = 0;
    FeatureState::LastShopBuyAccountId = 0;
    FeatureState::LastShopBuySlot = -1;
    FeatureState::LastShopBuyHeroId = 0;
    FeatureState::LastShopBuyPrice = 0;
    FeatureState::LastShopBuyOwnCount = -1;
    FeatureState::LastShopBuyWasFree = false;
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

    return activeMainTab == MainTabAutoPlay ||
        activeMainTab == MainTabShop ||
        activeMainTab == MainTabArena ||
        activeMainTab == MainTabTest ||
        FeatureState::AutoPlayEnabled.load();
}

// Checks the battle active condition before work proceeds.
bool IsBattleActive(uint64_t selfAccountId) {
    if (selfAccountId == 0) {
        return false;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        return true;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    const MonoStructures::Dictionary<uint64_t, void*>::Entry* entries = nullptr;
    int entryLimit = 0;

    if (!TryGetDictionaryEntries(battleManagers, &entries, &entryLimit)) {
        return false;
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

    RefreshManagedReferences(true);

    std::unordered_map<int, HeroTableEntry> localHeroes;
    std::unordered_map<int, EquipTableEntry> localEquips;
    std::unordered_map<int, CardTableEntry> localCards;

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

            for (const auto& item : CopyDictionaryEntries(heroDictionary)) {
                void* hero = item.second;

                if (!hero) {
                    continue;
                }

                int heroId = GetField<int>(reinterpret_cast<Il2CppObject*>(hero), idField);

                if (heroId <= 0 ||
                    (FeatureState::LoadResInstance &&
                     Originals::LoadRes_IsCommander &&
                     Originals::LoadRes_IsCommander(FeatureState::LoadResInstance, heroId))) {
                    continue;
                }

                std::string heroName = ManagedStringToStd(
                    GetField<Il2CppString*>(
                        reinterpret_cast<Il2CppObject*>(hero),
                        nameField
                    )
                );

                if (IsForbidHeroName(heroName)) {
                    continue;
                }

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

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
        FeatureState::Heroes = std::move(localHeroes);
        FeatureState::Equips = std::move(localEquips);
        FeatureState::Cards = std::move(localCards);
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
    return std::clamp(FeatureState::ShopRecommendTargetCount.load(), 1, 99);
}

// Returns the cached or live recommend lineup hero id value used by runtime features.
int GetRecommendLineupHeroId(std::chrono::steady_clock::time_point now) {
    if (!Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup) {
        FeatureState::CachedRecommendLineupHeroId = 0;
        return 0;
    }

    if (!IntervalElapsed(
            FeatureState::LastRecommendLineupCheck,
            RuntimeConfig::RecommendLineupCheckMs,
            now
        )) {
        return FeatureState::CachedRecommendLineupHeroId;
    }

    int heroId = Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup(nullptr);
    FeatureState::CachedRecommendLineupHeroId =
        IsKnownHeroIdOrTablePending(heroId) ? heroId : 0;
    return FeatureState::CachedRecommendLineupHeroId;
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
        int recommendHeroId = GetRecommendLineupHeroId(now);

        if (targetStillNeedsCopies(recommendHeroId, GetRecommendLineupTargetCount())) {
            FeatureState::CachedShopHasWorthwhileTarget = true;
        }
    }

    return FeatureState::CachedShopHasWorthwhileTarget;
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
         FeatureState::ArenaAllEnemyHpOne ||
         FeatureState::ArenaPassiveGold) &&
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
            Il2CppObject* selfObject = reinterpret_cast<Il2CppObject*>(selfPlayerData);
            SetField(selfObject, levelField, 99);
            SetField(selfObject, maxLevelField, 99);
            SetField(selfObject, populationField, 99);
            SetField(selfObject, slotPopulationField, 99);
            SetField(selfObject, maxPopulationField, 99);
            SetField(selfObject, extraPopulationField, 99);
        }

        if (FeatureState::ArenaPassiveGold &&
            selfPlayerData &&
            Originals::MCChessPlayerData_UpdateCoin &&
            Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin) {
            int targetGold = std::clamp(FeatureState::ArenaGoldTarget.load(), 0, 999999999);
            int currentGold =
                Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, selfAccountId);

            if (currentGold < targetGold) {
                Originals::MCChessPlayerData_UpdateCoin(
                    selfPlayerData,
                    targetGold - currentGold,
                    105
                );
            }
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

enum AutoPlayStrategyKind {
    AutoPlayStrategyEconomy = 0,
    AutoPlayStrategyBalanced = 1,
    AutoPlayStrategyAggressive = 2
};

enum GamePhaseKind {
    GamePhasePrepare = 1,
    GamePhaseWelfare = 5,
    GamePhaseForecast = 6,
    GamePhaseRegionPick = 7,
    GamePhaseAuction = 8
};

struct AutoPlaySnapshot {
    uint64_t accountId = 0;
    int round = 0;
    int phase = -1;
    int hp = -1;
    int coin = -1;
    int level = -1;
    int currentPopulation = -1;
    int totalPopulation = -1;
    int spareChess = -1;
    int battleHeroCount = -1;
    int allHeroCount = -1;
    int shopLevel = -1;
    int refreshCost = -1;
    int recommendHeroId = 0;
    int starUpHeroId = 0;
    int lineupWorth = -1;
    int fightValue = -1;
    int opponentCount = 0;
    int currentOpponentFightValue = -1;
    int strongestOpponentFightValue = -1;
    int contestedTargetOwners = 0;
    uint64_t currentOpponentId = 0;
    bool fightSection = false;
    bool fightResultSection = false;
    bool monsterRound = false;
};

struct AutoPlayGoldPlan {
    int interestTier = 0;
    int nextInterestGold = 10;
    int reserveGold = 20;
    int levelReserveGold = 20;
    int spendBudget = 0;
    int passiveTargetGold = 80;
    int recommendationTarget = 6;
    int maxAuctionBid = 0;
    bool holdForInterest = false;
    bool useFreeEconomy = false;
    bool forceLevel = false;
};

// Publishes auto play snapshot telemetry into atomic UI telemetry.
void PublishAutoPlaySnapshotTelemetry(const AutoPlaySnapshot& snapshot, bool ready) {
    FeatureState::AutoPlaySnapshotReady = ready;
    FeatureState::AutoPlaySnapshotAccountId = ready ? snapshot.accountId : 0;
    FeatureState::AutoPlaySnapshotRound = ready ? snapshot.round : 0;
    FeatureState::AutoPlaySnapshotPhase = ready ? snapshot.phase : -1;
    FeatureState::AutoPlaySnapshotHp = ready ? snapshot.hp : -1;
    FeatureState::AutoPlaySnapshotCoin = ready ? snapshot.coin : -1;
    FeatureState::AutoPlaySnapshotLevel = ready ? snapshot.level : -1;
    FeatureState::AutoPlaySnapshotCurrentPopulation =
        ready ? snapshot.currentPopulation : -1;
    FeatureState::AutoPlaySnapshotTotalPopulation =
        ready ? snapshot.totalPopulation : -1;
    FeatureState::AutoPlaySnapshotLineupWorth = ready ? snapshot.lineupWorth : -1;
    FeatureState::AutoPlaySnapshotFightValue = ready ? snapshot.fightValue : -1;
    FeatureState::AutoPlaySnapshotRecommendHeroId =
        ready ? snapshot.recommendHeroId : 0;
    FeatureState::AutoPlaySnapshotStarUpHeroId =
        ready ? snapshot.starUpHeroId : 0;
    FeatureState::AutoPlaySnapshotCurrentOpponentFightValue =
        ready ? snapshot.currentOpponentFightValue : -1;
    FeatureState::AutoPlaySnapshotFightSection = ready && snapshot.fightSection;
    FeatureState::AutoPlaySnapshotMonsterRound = ready && snapshot.monsterRound;

    if (!ready) {
        FeatureState::AutoPlayOpponentCount = 0;
        FeatureState::AutoPlayCurrentOpponentId = 0;
        FeatureState::AutoPlayStrongestOpponentFightValue = 0;
        FeatureState::AutoPlayContestedTargets = 0;
    }
}

// Returns the cached or live cached auto play snapshot value used by runtime features.
AutoPlaySnapshot GetCachedAutoPlaySnapshot() {
    AutoPlaySnapshot snapshot{};
    if (!FeatureState::AutoPlaySnapshotReady.load()) {
        return snapshot;
    }

    snapshot.accountId = FeatureState::AutoPlaySnapshotAccountId.load();
    snapshot.round = FeatureState::AutoPlaySnapshotRound.load();
    snapshot.phase = FeatureState::AutoPlaySnapshotPhase.load();
    snapshot.hp = FeatureState::AutoPlaySnapshotHp.load();
    snapshot.coin = FeatureState::AutoPlaySnapshotCoin.load();
    snapshot.level = FeatureState::AutoPlaySnapshotLevel.load();
    snapshot.currentPopulation = FeatureState::AutoPlaySnapshotCurrentPopulation.load();
    snapshot.totalPopulation = FeatureState::AutoPlaySnapshotTotalPopulation.load();
    snapshot.recommendHeroId = FeatureState::AutoPlaySnapshotRecommendHeroId.load();
    snapshot.starUpHeroId = FeatureState::AutoPlaySnapshotStarUpHeroId.load();
    snapshot.lineupWorth = FeatureState::AutoPlaySnapshotLineupWorth.load();
    snapshot.fightValue = FeatureState::AutoPlaySnapshotFightValue.load();
    snapshot.opponentCount = FeatureState::AutoPlayOpponentCount.load();
    snapshot.currentOpponentFightValue =
        FeatureState::AutoPlaySnapshotCurrentOpponentFightValue.load();
    snapshot.strongestOpponentFightValue =
        FeatureState::AutoPlayStrongestOpponentFightValue.load();
    snapshot.contestedTargetOwners = FeatureState::AutoPlayContestedTargets.load();
    snapshot.currentOpponentId = FeatureState::AutoPlayCurrentOpponentId.load();
    snapshot.fightSection = FeatureState::AutoPlaySnapshotFightSection.load();
    snapshot.monsterRound = FeatureState::AutoPlaySnapshotMonsterRound.load();
    return snapshot;
}

// Returns the cached or live cached auto play gold plan value used by runtime features.
AutoPlayGoldPlan GetCachedAutoPlayGoldPlan() {
    AutoPlayGoldPlan plan{};
    plan.interestTier = FeatureState::AutoPlayInterestTier.load();
    plan.nextInterestGold = FeatureState::AutoPlayInterestNextGold.load();
    plan.reserveGold = FeatureState::AutoPlayInterestReserveGold.load();
    plan.spendBudget = FeatureState::AutoPlaySpendBudget.load();
    plan.holdForInterest = FeatureState::AutoPlayHoldInterest.load();
    return plan;
}

struct AutoPlayPolicyBackup {
    bool captured = false;
    bool shopBuyFreeHero = false;
    bool shopBuySelectedHero = false;
    bool shopBuyRecommendLineup = false;
    bool shopRefresh = false;
    bool shopStopRefreshAtFreeHero = false;
    bool shopStopRefreshAtSelectedHero = false;
    bool shopStopRefreshAtRecommendLineup = false;
    bool shopKeepGold = false;
    int shopKeepGoldAt = 20;
    int shopRecommendTargetCount = 9;
    bool arenaForceActiveSynergy = false;
    bool arenaNoShopLock = false;
    bool arenaUnlimitedHeroPool = false;
    bool arenaPassiveGold = false;
    int arenaGoldTarget = 999999;
    bool arenaFreeEconomy = false;
    bool arenaForceLevel99 = false;
    bool arenaAllEnemyHpOne = false;
    bool combatPreventHpLoss = false;
    bool combatBoostAttackRatio = false;
    int combatAttackRatioPercent = 5000;
    int combatFightValue = 999999999;
    bool combatForceWin = false;
    bool combatCrippleEnemies = false;
    int combatEnemyAttackRatioPercent = 1;
};

namespace RuntimePolicy {
    AutoPlayPolicyBackup AutoPlayBackup;
}

// Coordinates capture auto play policy backup for the overlay runtime.
void CaptureAutoPlayPolicyBackup() {
    if (RuntimePolicy::AutoPlayBackup.captured) {
        return;
    }

    AutoPlayPolicyBackup backup{};
    backup.captured = true;
    backup.shopBuyFreeHero = FeatureState::ShopBuyFreeHero.load();
    backup.shopBuySelectedHero = FeatureState::ShopBuySelectedHero.load();
    backup.shopBuyRecommendLineup = FeatureState::ShopBuyRecommendLineup.load();
    backup.shopRefresh = FeatureState::ShopRefresh.load();
    backup.shopStopRefreshAtFreeHero = FeatureState::ShopStopRefreshAtFreeHero.load();
    backup.shopStopRefreshAtSelectedHero = FeatureState::ShopStopRefreshAtSelectedHero.load();
    backup.shopStopRefreshAtRecommendLineup =
        FeatureState::ShopStopRefreshAtRecommendLineup.load();
    backup.shopKeepGold = FeatureState::ShopKeepGold.load();
    backup.shopKeepGoldAt = FeatureState::ShopKeepGoldAt.load();
    backup.shopRecommendTargetCount = FeatureState::ShopRecommendTargetCount.load();
    backup.arenaForceActiveSynergy = FeatureState::ArenaForceActiveSynergy.load();
    backup.arenaNoShopLock = FeatureState::ArenaNoShopLock.load();
    backup.arenaUnlimitedHeroPool = FeatureState::ArenaUnlimitedHeroPool.load();
    backup.arenaPassiveGold = FeatureState::ArenaPassiveGold.load();
    backup.arenaGoldTarget = FeatureState::ArenaGoldTarget.load();
    backup.arenaFreeEconomy = FeatureState::ArenaFreeEconomy.load();
    backup.arenaForceLevel99 = FeatureState::ArenaForceLevel99.load();
    backup.arenaAllEnemyHpOne = FeatureState::ArenaAllEnemyHpOne.load();
    backup.combatPreventHpLoss = FeatureState::CombatPreventHpLoss.load();
    backup.combatBoostAttackRatio = FeatureState::CombatBoostAttackRatio.load();
    backup.combatAttackRatioPercent = FeatureState::CombatAttackRatioPercent.load();
    backup.combatFightValue = FeatureState::CombatFightValue.load();
    backup.combatForceWin = FeatureState::CombatForceWin.load();
    backup.combatCrippleEnemies = FeatureState::CombatCrippleEnemies.load();
    backup.combatEnemyAttackRatioPercent = FeatureState::CombatEnemyAttackRatioPercent.load();
    RuntimePolicy::AutoPlayBackup = backup;
}

// Coordinates restore auto play policy backup for the overlay runtime.
void RestoreAutoPlayPolicyBackup() {
    if (!RuntimePolicy::AutoPlayBackup.captured) {
        return;
    }

    const AutoPlayPolicyBackup backup = RuntimePolicy::AutoPlayBackup;
    FeatureState::ShopBuyFreeHero = backup.shopBuyFreeHero;
    FeatureState::ShopBuySelectedHero = backup.shopBuySelectedHero;
    FeatureState::ShopBuyRecommendLineup = backup.shopBuyRecommendLineup;
    FeatureState::ShopRefresh = backup.shopRefresh;
    FeatureState::ShopStopRefreshAtFreeHero = backup.shopStopRefreshAtFreeHero;
    FeatureState::ShopStopRefreshAtSelectedHero = backup.shopStopRefreshAtSelectedHero;
    FeatureState::ShopStopRefreshAtRecommendLineup =
        backup.shopStopRefreshAtRecommendLineup;
    FeatureState::ShopKeepGold = backup.shopKeepGold;
    FeatureState::ShopKeepGoldAt = backup.shopKeepGoldAt;
    FeatureState::ShopRecommendTargetCount = backup.shopRecommendTargetCount;
    FeatureState::ArenaForceActiveSynergy = backup.arenaForceActiveSynergy;
    FeatureState::ArenaNoShopLock = backup.arenaNoShopLock;
    FeatureState::ArenaUnlimitedHeroPool = backup.arenaUnlimitedHeroPool;
    FeatureState::ArenaPassiveGold = backup.arenaPassiveGold;
    FeatureState::ArenaGoldTarget = backup.arenaGoldTarget;
    FeatureState::ArenaFreeEconomy = backup.arenaFreeEconomy;
    FeatureState::ArenaForceLevel99 = backup.arenaForceLevel99;
    FeatureState::ArenaAllEnemyHpOne = backup.arenaAllEnemyHpOne;
    FeatureState::CombatPreventHpLoss = backup.combatPreventHpLoss;
    FeatureState::CombatBoostAttackRatio = backup.combatBoostAttackRatio;
    FeatureState::CombatAttackRatioPercent = backup.combatAttackRatioPercent;
    FeatureState::CombatFightValue = backup.combatFightValue;
    FeatureState::CombatForceWin = backup.combatForceWin;
    FeatureState::CombatCrippleEnemies = backup.combatCrippleEnemies;
    FeatureState::CombatEnemyAttackRatioPercent = backup.combatEnemyAttackRatioPercent;
    RuntimePolicy::AutoPlayBackup = AutoPlayPolicyBackup{};
}

struct AutoPlayBoardUnit {
    void* instance = nullptr;
    uint32_t guid = 0;
    int heroId = 0;
    int star = 1;
    int quality = 0;
    int location = 0;
    int camp = 0;
    AstarInt2 grid{0, 0};
    bool reserve = false;
    bool chessPlayer = false;
};

struct AutoPlayBoardPlan {
    int focusGroup = 0;
    int targetHeroId = 0;
    int selfUnits = 0;
    int enemyUnits = 0;
    int contestedTargets = 0;
    int boardScore = 0;
};

struct AutoPlayFormationStats {
    std::array<int, 8> enemyColumnThreat{};
    std::array<int, 8> allyColumnCount{};
    std::array<int, 8> allyFrontlineCount{};
    std::array<int, 8> allyBacklineCount{};
    float enemyCenterX = -1.0f;
    float enemyCenterY = -1.0f;
    int enemyPositionCount = 0;
};

// Computes auto play strategy name data for the Auto-Play controller.
const char* AutoPlayStrategyName(int strategy) {
    switch (strategy) {
        case AutoPlayStrategyEconomy:
            return "Economy";
        case AutoPlayStrategyAggressive:
            return "Aggressive";
        default:
            return "Balanced";
    }
}

// Clamps auto play strategy to the supported runtime range.
int ClampAutoPlayStrategy(int strategy) {
    return std::clamp(
        strategy,
        static_cast<int>(AutoPlayStrategyEconomy),
        static_cast<int>(AutoPlayStrategyAggressive)
    );
}

// Clamps auto play gold to the supported runtime range.
int ClampAutoPlayGold(int gold, int maxGold = 999999) {
    return std::clamp(gold, 0, maxGold);
}

// Computes auto play interest tier for gold data for the Auto-Play controller.
int AutoPlayInterestTierForGold(int gold) {
    return std::clamp(ClampAutoPlayGold(gold) / 10, 0, 5);
}

// Computes auto play interest floor for tier data for the Auto-Play controller.
int AutoPlayInterestFloorForTier(int tier) {
    return std::clamp(tier, 0, 5) * 10;
}

// Computes auto play next interest gold for gold data for the Auto-Play controller.
int AutoPlayNextInterestGoldForGold(int gold) {
    int tier = AutoPlayInterestTierForGold(gold);
    return tier >= 5 ? 50 : (tier + 1) * 10;
}

// Coordinates hero has group for the overlay runtime.
bool HeroHasGroup(const HeroTableEntry& hero, int groupId) {
    if (groupId <= 0) {
        return false;
    }

    return std::find(hero.groups.begin(), hero.groups.end(), groupId) != hero.groups.end();
}

// Scores a hero candidate for Auto-Play using table metadata and current targets.
int ScoreHeroForAutoPlay(
    int heroId,
    int star,
    int focusGroup,
    int recommendHeroId,
    int starUpHeroId
) {
    HeroTableEntry hero;
    int quality = 1;
    int score = std::max(star, 1) * std::max(star, 1) * 120;

    if (TryGetHeroTableEntry(heroId, &hero)) {
        quality = std::max(hero.quality, 1);
        score += quality * 75;

        if (hero.isTank > 0) {
            score += 70;
        }

        if (HeroHasGroup(hero, focusGroup)) {
            score += 180;
        }
    }

    if (heroId > 0 && heroId == recommendHeroId) {
        score += 260;
    }

    if (heroId > 0 && heroId == starUpHeroId) {
        score += 220;
    }

    return score + quality;
}

// Checks the frontline hero condition before work proceeds.
bool IsFrontlineHero(int heroId) {
    HeroTableEntry hero;
    if (!TryGetHeroTableEntry(heroId, &hero)) {
        return false;
    }

    return hero.isTank > 0 ||
        hero.heroType == 1 ||
        hero.attackType == 1 ||
        StringIncludesCaseInsensitive(hero.name, "tank");
}

// Checks the auto play frontline unit condition before work proceeds.
bool IsAutoPlayFrontlineUnit(const AutoPlayBoardUnit& unit) {
    return IsFrontlineHero(unit.heroId) || unit.star >= 3 || unit.quality >= 4;
}

// Checks the auto play carry unit condition before work proceeds.
bool IsAutoPlayCarryUnit(const AutoPlayBoardUnit& unit) {
    return !IsAutoPlayFrontlineUnit(unit) && (unit.star >= 2 || unit.quality >= 3);
}

// Collects auto play board units with bounded managed reads.
std::vector<AutoPlayBoardUnit> CollectAutoPlayBoardUnits(void* battleManager) {
    std::vector<AutoPlayBoardUnit> units;

    if (!battleManager) {
        return units;
    }

    static FieldInfo* chessListField = nullptr;
    static FieldInfo* guidField = nullptr;
    static FieldInfo* idField = nullptr;
    static FieldInfo* locationField = nullptr;
    static FieldInfo* campField = nullptr;
    static FieldInfo* gridField = nullptr;
    static FieldInfo* starField = nullptr;
    static FieldInfo* reserveField = nullptr;
    static FieldInfo* chessPlayerField = nullptr;

    if (!chessListField) {
        chessListField = GetFieldInfoFromName("", "LogicHeroContainer", "m_ChessList");
    }

    if (!guidField) {
        guidField = GetFieldInfoFromName("Battle", "MCEntityBase", "m_uGuid");
    }

    if (!idField) {
        idField = GetFieldInfoFromName("Battle", "MCEntityBase", "m_ID");
    }

    if (!locationField) {
        locationField =
            GetFieldInfoFromName("Battle", "MCEntityBase", "m_eEntityLocatoinType");
    }

    if (!campField) {
        campField =
            GetFieldInfoFromName("Battle", "MCEntityBase", "<m_EntityCampType>k__BackingField");
    }

    if (!gridField) {
        gridField = GetFieldInfoFromName("Battle", "MCLogicFighter", "_gridPos");
    }

    if (!starField) {
        starField = GetFieldInfoFromName("Battle", "MCLogicFighter", "_iStarLevel");
    }

    if (!reserveField) {
        reserveField = GetFieldInfoFromName("Battle", "MCLogicFighter", "m_updateAtReserve");
    }

    if (!chessPlayerField) {
        chessPlayerField = GetFieldInfoFromName("Battle", "MCEntityBase", "IsChessPlayer");
    }

    auto* chessList = GetField<MonoStructures::List<void*>*>(
        reinterpret_cast<Il2CppObject*>(battleManager),
        chessListField
    );
    void* const* fighters = nullptr;
    int fighterCount = 0;

    if (!TryGetManagedListData(chessList, &fighters, &fighterCount, 96)) {
        return units;
    }

    units.reserve(static_cast<size_t>(std::max(fighterCount, 0)));

    for (int i = 0; fighters && i < fighterCount; ++i) {
        void* fighter = fighters[i];
        if (!fighter) {
            continue;
        }

        AutoPlayBoardUnit unit{};
        unit.instance = fighter;
        unit.guid = GetField<uint32_t>(reinterpret_cast<Il2CppObject*>(fighter), guidField);
        unit.heroId = GetField<int>(reinterpret_cast<Il2CppObject*>(fighter), idField);
        unit.location = GetField<int>(reinterpret_cast<Il2CppObject*>(fighter), locationField);
        unit.camp = GetField<int>(reinterpret_cast<Il2CppObject*>(fighter), campField);
        unit.grid = GetField<AstarInt2>(reinterpret_cast<Il2CppObject*>(fighter), gridField);
        unit.star = std::clamp(
            GetField<int>(reinterpret_cast<Il2CppObject*>(fighter), starField),
            1,
            5
        );
        unit.reserve = GetField<bool>(reinterpret_cast<Il2CppObject*>(fighter), reserveField) ||
            unit.location == 2 ||
            unit.location == 5;
        unit.chessPlayer =
            GetField<bool>(reinterpret_cast<Il2CppObject*>(fighter), chessPlayerField) ||
            unit.location == 3;

        HeroTableEntry hero;
        if (TryGetHeroTableEntry(unit.heroId, &hero)) {
            unit.quality = hero.quality;
        }

        if (!unit.chessPlayer && IsPlausibleHeroId(unit.heroId) && unit.guid != 0) {
            units.push_back(unit);
        }
    }

    return units;
}

// Checks whether another live unit already occupies a board cell.
bool IsBoardCellOccupied(
    const std::vector<AutoPlayBoardUnit>& units,
    int x,
    int y,
    uint32_t ignoreGuid = 0
) {
    for (const AutoPlayBoardUnit& unit : units) {
        if (!unit.reserve &&
            unit.guid != ignoreGuid &&
            unit.grid.x == x &&
            unit.grid.y == y) {
            return true;
        }
    }

    return false;
}

// Checks the valid auto play board cell condition before work proceeds.
bool IsValidAutoPlayBoardCell(int x, int y) {
    return x >= 0 && x <= 7 && y >= 0 && y <= 3;
}

// Computes auto play column value data for the Auto-Play controller.
int AutoPlayColumnValue(const std::array<int, 8>& values, int x) {
    if (x < 0 || x >= static_cast<int>(values.size())) {
        return 0;
    }

    return values[static_cast<size_t>(x)];
}

// Computes auto play nearby column value data for the Auto-Play controller.
int AutoPlayNearbyColumnValue(const std::array<int, 8>& values, int x) {
    return AutoPlayColumnValue(values, x - 1) +
        AutoPlayColumnValue(values, x) +
        AutoPlayColumnValue(values, x + 1);
}

// Summarizes allied and enemy board positions used by formation scoring.
AutoPlayFormationStats BuildAutoPlayFormationStats(
    const std::vector<AutoPlayBoardUnit>& selfUnits,
    const std::vector<AutoPlayBoardUnit>& enemyUnits
) {
    AutoPlayFormationStats stats{};

    for (const AutoPlayBoardUnit& unit : selfUnits) {
        if (unit.reserve || unit.chessPlayer || !IsValidAutoPlayBoardCell(unit.grid.x, unit.grid.y)) {
            continue;
        }

        int x = unit.grid.x;
        stats.allyColumnCount[static_cast<size_t>(x)]++;
        if (IsAutoPlayFrontlineUnit(unit) || unit.grid.y <= 1) {
            stats.allyFrontlineCount[static_cast<size_t>(x)]++;
        } else {
            stats.allyBacklineCount[static_cast<size_t>(x)]++;
        }
    }

    for (const AutoPlayBoardUnit& enemy : enemyUnits) {
        if (enemy.reserve || !IsValidAutoPlayBoardCell(enemy.grid.x, enemy.grid.y)) {
            continue;
        }

        int x = enemy.grid.x;
        int threat = 8 + (std::max(enemy.star, 1) * 3) + (std::max(enemy.quality, 0) * 2);
        if (IsAutoPlayFrontlineUnit(enemy)) {
            threat += 6;
        }

        stats.enemyColumnThreat[static_cast<size_t>(x)] += threat;
        if (x > 0) {
            stats.enemyColumnThreat[static_cast<size_t>(x - 1)] += threat / 2;
        }
        if (x < 7) {
            stats.enemyColumnThreat[static_cast<size_t>(x + 1)] += threat / 2;
        }

        stats.enemyCenterX += enemy.grid.x;
        stats.enemyCenterY += enemy.grid.y;
        stats.enemyPositionCount++;
    }

    if (stats.enemyPositionCount > 0) {
        stats.enemyCenterX =
            (stats.enemyCenterX + 1.0f) / static_cast<float>(stats.enemyPositionCount);
        stats.enemyCenterY =
            (stats.enemyCenterY + 1.0f) / static_cast<float>(stats.enemyPositionCount);
    }

    return stats;
}

// Rates one destination cell for a unit using role, cover, and enemy pressure.
int ScoreBoardCellForUnit(
    const AutoPlayBoardUnit& unit,
    int x,
    int y,
    const AutoPlayFormationStats& stats,
    const AutoPlaySnapshot& snapshot,
    const AutoPlayBoardPlan& plan
) {
    bool frontline = IsAutoPlayFrontlineUnit(unit);
    bool carry = IsAutoPlayCarryUnit(unit);
    int desiredY = frontline ? 0 : (carry ? 3 : 2);
    int score = 140 - (std::abs(y - desiredY) * (frontline ? 28 : 24));

    int columnThreat = AutoPlayColumnValue(stats.enemyColumnThreat, x);
    int nearbyThreat = AutoPlayNearbyColumnValue(stats.enemyColumnThreat, x);
    int allyColumnCount = AutoPlayColumnValue(stats.allyColumnCount, x);
    if (!unit.reserve && IsValidAutoPlayBoardCell(unit.grid.x, unit.grid.y) && unit.grid.x == x) {
        allyColumnCount = std::max(allyColumnCount - 1, 0);
    }
    int postMoveColumnCount = allyColumnCount + 1;

    if (stats.enemyCenterX >= 0.0f) {
        float distanceFromEnemyCenter = std::abs(static_cast<float>(x) - stats.enemyCenterX);
        if (frontline) {
            score -= static_cast<int>(distanceFromEnemyCenter * 4.0f);
            score += columnThreat * 2;
            score += nearbyThreat;
        } else {
            score += static_cast<int>(distanceFromEnemyCenter * (carry ? 4.0f : 2.0f));
            score -= columnThreat * (carry ? 3 : 2);
            score -= nearbyThreat;
        }
    }

    if (frontline && stats.enemyCenterY >= 0.0f) {
        score -= static_cast<int>(std::abs(static_cast<float>(y) - stats.enemyCenterY) * 3.0f);
    }

    if (frontline) {
        int protectedBackline = AutoPlayNearbyColumnValue(stats.allyBacklineCount, x);
        score += protectedBackline * 9;
        score -= std::max(postMoveColumnCount - 2, 0) * 12;
    } else {
        int nearbyCover = AutoPlayNearbyColumnValue(stats.allyFrontlineCount, x);
        score += nearbyCover * (carry ? 12 : 8);
        score -= std::max(postMoveColumnCount - 1, 0) * (carry ? 16 : 10);

        if (nearbyCover == 0 && stats.enemyPositionCount > 0) {
            score -= carry ? 28 : 16;
        }
    }

    score += ScoreHeroForAutoPlay(
        unit.heroId,
        unit.star,
        plan.focusGroup,
        snapshot.recommendHeroId,
        snapshot.starUpHeroId
    ) / 18;

    if (unit.heroId == plan.targetHeroId) {
        score += carry ? 24 : 12;
    }

    if (snapshot.currentOpponentFightValue > snapshot.fightValue && frontline) {
        score += 18;
    }

    return score;
}

// Chooses the best bounded formation move and strategic focus for the current board.
AutoPlayBoardPlan BuildAutoPlayBoardPlan(
    const AutoPlaySnapshot& snapshot,
    const std::vector<AutoPlayBoardUnit>& selfUnits,
    const std::vector<AutoPlayBoardUnit>& enemyUnits
) {
    AutoPlayBoardPlan plan{};
    plan.selfUnits = static_cast<int>(selfUnits.size());
    plan.enemyUnits = static_cast<int>(enemyUnits.size());

    std::unordered_map<int, int> groupScore;

    for (const AutoPlayBoardUnit& unit : selfUnits) {
        HeroTableEntry hero;
        if (!TryGetHeroTableEntry(unit.heroId, &hero)) {
            continue;
        }

        int heroScore = ScoreHeroForAutoPlay(
            unit.heroId,
            unit.star,
            0,
            snapshot.recommendHeroId,
            snapshot.starUpHeroId
        );

        for (int groupId : hero.groups) {
            groupScore[groupId] += heroScore;
        }
    }

    if (snapshot.recommendHeroId > 0) {
        HeroTableEntry hero;
        if (TryGetHeroTableEntry(snapshot.recommendHeroId, &hero)) {
            for (int groupId : hero.groups) {
                groupScore[groupId] += 220;
            }
        }
    }

    std::unordered_map<int, int> enemyGroupCount;
    for (const AutoPlayBoardUnit& unit : enemyUnits) {
        HeroTableEntry hero;
        if (!TryGetHeroTableEntry(unit.heroId, &hero)) {
            continue;
        }

        for (int groupId : hero.groups) {
            enemyGroupCount[groupId] += 1;
        }
    }

    int bestGroup = 0;
    int bestScore = -999999;
    for (const auto& item : groupScore) {
        int groupId = item.first;
        int adjusted = item.second - (enemyGroupCount[groupId] * 45);

        if (adjusted > bestScore) {
            bestGroup = groupId;
            bestScore = adjusted;
        }
    }

    plan.focusGroup = bestGroup;

    int bestHero = snapshot.starUpHeroId > 0 ? snapshot.starUpHeroId : snapshot.recommendHeroId;
    int bestHeroScore = bestHero > 0 ?
        ScoreHeroForAutoPlay(bestHero, 1, bestGroup, snapshot.recommendHeroId, snapshot.starUpHeroId) :
        -1;

    for (const AutoPlayBoardUnit& unit : selfUnits) {
        int score = ScoreHeroForAutoPlay(
            unit.heroId,
            unit.star,
            bestGroup,
            snapshot.recommendHeroId,
            snapshot.starUpHeroId
        );
        plan.boardScore += score;

        if (score > bestHeroScore) {
            bestHeroScore = score;
            bestHero = unit.heroId;
        }
    }

    plan.targetHeroId = bestHero;
    return plan;
}

// Publishes auto play board plan into atomic UI telemetry.
void PublishAutoPlayBoardPlan(const AutoPlayBoardPlan& plan) {
    FeatureState::AutoPlayFocusGroup = plan.focusGroup;
    FeatureState::AutoPlayTargetHeroId = plan.targetHeroId;
    FeatureState::AutoPlayBoardSelfUnits = plan.selfUnits;
    FeatureState::AutoPlayBoardEnemyUnits = plan.enemyUnits;
    FeatureState::AutoPlayContestedTargets = plan.contestedTargets;
}

// Applies auto play shop targets to the live runtime when bindings are ready.
void ApplyAutoPlayShopTargets(const AutoPlayBoardPlan& plan, const AutoPlaySnapshot& snapshot) {
    if (!FeatureState::AutoPlayUseShop.load()) {
        return;
    }

    int targetHero = plan.targetHeroId > 0 ? plan.targetHeroId : snapshot.recommendHeroId;
    if (!IsPlausibleHeroId(targetHero)) {
        return;
    }

    HeroAutomationState state{};
    state.selected = true;
    state.targetCount =
        FeatureState::AutoPlayStrategy.load() == AutoPlayStrategyAggressive ? 9 : 6;
    SetShopHeroTarget(targetHero, state);
}

// Verifies a board cell is valid and not occupied before attempting a move.
bool IsLikelyFreeBoardCell(
    const std::vector<AutoPlayBoardUnit>& selfUnits,
    int x,
    int y,
    uint32_t ignoreGuid
) {
    if (x < 0 || x > 7 || y < 0 || y > 3) {
        return false;
    }

    if (IsBoardCellOccupied(selfUnits, x, y, ignoreGuid)) {
        return false;
    }

    if (Originals::AStarTileMap_ValidPos && !Originals::AStarTileMap_ValidPos(x, y)) {
        return false;
    }

    return true;
}

// Attempts one high-value Auto-Play formation move when the cooldown allows it.
bool TryAutoPlaySmartFormation(
    const AutoPlaySnapshot& snapshot,
    const AutoPlayBoardPlan& plan,
    const std::vector<AutoPlayBoardUnit>& selfUnits,
    const std::vector<AutoPlayBoardUnit>& enemyUnits,
    std::chrono::steady_clock::time_point now
) {
    if (!FeatureState::AutoPlayUseFormation.load() ||
        snapshot.fightSection ||
        !Originals::MCLogicBattleManager_MoveHeroInBattleField ||
        !CooldownElapsed(
            FeatureState::LastAutoPlayFormationAttempt,
            RuntimeConfig::AutoPlayFormationCooldownMs,
            now
        )) {
        return false;
    }

    void* selfManager = GetSelfLogicBattleManager();
    if (!selfManager || selfUnits.empty()) {
        return false;
    }

    AutoPlayFormationStats formationStats = BuildAutoPlayFormationStats(selfUnits, enemyUnits);
    const AutoPlayBoardUnit* bestUnit = nullptr;
    AstarInt2 bestTarget{0, 0};
    int bestGain = 0;
    int bestScore = -999999;

    for (const AutoPlayBoardUnit& unit : selfUnits) {
        if (unit.reserve || unit.chessPlayer || !IsValidAutoPlayBoardCell(unit.grid.x, unit.grid.y)) {
            continue;
        }

        int currentScore = ScoreBoardCellForUnit(
            unit,
            unit.grid.x,
            unit.grid.y,
            formationStats,
            snapshot,
            plan
        );

        for (int y = 0; y <= 3; ++y) {
            for (int x = 0; x <= 7; ++x) {
                if (!IsLikelyFreeBoardCell(selfUnits, x, y, unit.guid)) {
                    continue;
                }

                int score = ScoreBoardCellForUnit(
                    unit,
                    x,
                    y,
                    formationStats,
                    snapshot,
                    plan
                );
                int gain = score - currentScore;

                if (gain > bestGain || (gain == bestGain && score > bestScore)) {
                    bestGain = gain;
                    bestScore = score;
                    bestUnit = &unit;
                    bestTarget = {x, y};
                }
            }
        }
    }

    if (!bestUnit || bestGain < 18) {
        return false;
    }

    Originals::MCLogicBattleManager_MoveHeroInBattleField(
        selfManager,
        bestUnit->guid,
        static_cast<uint8_t>(bestTarget.x),
        static_cast<uint8_t>(bestTarget.y),
        true
    );
    FeatureState::LastAutoPlayFormationAttempt = now;
    FeatureState::AutoPlayBoardMoves =
        std::min(FeatureState::AutoPlayBoardMoves.load() + 1, 999999);
    FeatureState::AutoPlayLastMoveHeroId = bestUnit->heroId;
    FeatureState::AutoPlayLastMoveGain = bestGain;
    FeatureState::AutoPlayLastMoveX = bestTarget.x;
    FeatureState::AutoPlayLastMoveY = bestTarget.y;
    return true;
}

// Scores auto play card so automation can choose one best action.
int ScoreAutoPlayCard(int cardId, const AutoPlayBoardPlan& plan, const AutoPlaySnapshot& snapshot) {
    if (cardId <= 0) {
        return -999999;
    }

    CardTableEntry card;
    int score = cardId % 97;

    if (TryGetCardTableEntry(cardId, &card) && !card.name.empty()) {
        if (StringIncludesCaseInsensitive(card.name, "gold") ||
            StringIncludesCaseInsensitive(card.name, "coin")) {
            score += snapshot.round < 10 ? 180 : 60;
        }

        if (StringIncludesCaseInsensitive(card.name, "hero") ||
            StringIncludesCaseInsensitive(card.name, "shop")) {
            score += 130;
        }

        if (StringIncludesCaseInsensitive(card.name, "star") ||
            StringIncludesCaseInsensitive(card.name, "upgrade")) {
            score += 150;
        }

        if (StringIncludesCaseInsensitive(card.name, "bond") ||
            StringIncludesCaseInsensitive(card.name, "relation") ||
            StringIncludesCaseInsensitive(card.name, "synergy")) {
            score += plan.focusGroup > 0 ? 170 : 90;
        }

        if (StringIncludesCaseInsensitive(card.name, "fight") ||
            StringIncludesCaseInsensitive(card.name, "attack") ||
            StringIncludesCaseInsensitive(card.name, "damage")) {
            score += snapshot.round >= 10 ? 150 : 80;
        }
    }

    if (snapshot.hp >= 0 && snapshot.hp <= 35) {
        score += 120;
    }

    if (snapshot.currentOpponentFightValue > snapshot.fightValue) {
        score += 80;
    }

    return score;
}

// Publishes the best Go Go Card choice for the current Auto-Play plan.
void ApplyAutoPlayGoGoCardChoice(
    const AutoPlaySnapshot& snapshot,
    const AutoPlayBoardPlan& plan
) {
    if (!FeatureState::AutoPlayUseGoGoCards.load() ||
        !Originals::MCComp_GetGoGoCardComp ||
        !Originals::MCLogicGoGoCardComp_get_m_CurrData) {
        return;
    }

    void* goGoCardComp = Originals::MCComp_GetGoGoCardComp(snapshot.accountId);
    if (!goGoCardComp) {
        return;
    }

    static FieldInfo* cardListField = nullptr;

    if (!cardListField) {
        cardListField =
            GetFieldInfoFromName("Battle", "MCLogicGoGoCardRoundData", "m_listCurrCard");
    }

    void* currentData = Originals::MCLogicGoGoCardComp_get_m_CurrData(goGoCardComp);
    auto* cardList = currentData ?
        GetField<MonoStructures::List<int>*>(
            reinterpret_cast<Il2CppObject*>(currentData),
            cardListField
        ) :
        nullptr;
    const int* cards = nullptr;
    int cardCount = 0;

    if (!TryGetManagedListData(cardList, &cards, &cardCount, 16) || cardCount <= 0) {
        return;
    }

    std::array<std::pair<int, int>, 16> scored{};
    int scoredCount = 0;
    for (int i = 0; cards && i < cardCount && scoredCount < static_cast<int>(scored.size()); ++i) {
        int cardId = cards[i];
        scored[scoredCount++] = {ScoreAutoPlayCard(cardId, plan, snapshot), cardId};
    }

    std::sort(scored.begin(), scored.begin() + scoredCount, [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });

    if (scoredCount > 0 && scored[0].second > 0) {
        FeatureState::ArenaGogoCardEnabled = true;
        FeatureState::ArenaGogoCardSelected1 = scored[0].second;
        FeatureState::AutoPlayBestCardId = scored[0].second;
    }

    if (scoredCount > 1 && scored[1].second > 0) {
        FeatureState::ArenaGogoCardSelected2 = scored[1].second;
    }
}

// Scores auction reward item so automation can choose one best action.
int ScoreAuctionRewardItem(void* rewardItem, const AutoPlayBoardPlan& plan, const AutoPlaySnapshot& snapshot) {
    if (!rewardItem) {
        return 0;
    }

    static FieldInfo* itemConfField = nullptr;
    static FieldInfo* biddingTypeField = nullptr;
    static FieldInfo* biddingListField = nullptr;
    static FieldInfo* biddingChangeField = nullptr;

    if (!itemConfField) {
        itemConfField =
            GetFieldInfoFromName("Battle", "MCLogicAuctionItemBase", "<m_ItemConf>k__BackingField");
    }

    if (!biddingTypeField) {
        biddingTypeField = GetFieldInfoFromName("", "CData_AuctionItem_Element", "m_BiddingType");
    }

    if (!biddingListField) {
        biddingListField = GetFieldInfoFromName("", "CData_AuctionItem_Element", "m_BiddingList");
    }

    if (!biddingChangeField) {
        biddingChangeField =
            GetFieldInfoFromName("", "CData_AuctionItem_Element", "m_BiddingChange");
    }

    void* itemConf = GetField<void*>(reinterpret_cast<Il2CppObject*>(rewardItem), itemConfField);
    if (!itemConf) {
        return 0;
    }

    int type = GetField<int>(reinterpret_cast<Il2CppObject*>(itemConf), biddingTypeField);
    auto* biddingList = GetField<MonoStructures::Array<int>*>(
        reinterpret_cast<Il2CppObject*>(itemConf),
        biddingListField
    );
    auto* biddingChange = GetField<MonoStructures::Array<int>*>(
        reinterpret_cast<Il2CppObject*>(itemConf),
        biddingChangeField
    );
    const int* values = nullptr;
    int valueCount = 0;
    int score = type * 35;

    if (TryGetManagedArrayData(biddingList, &values, &valueCount, 16)) {
        for (int i = 0; values && i < valueCount; ++i) {
            int value = values[i];

            if (type == 2 && IsPlausibleHeroId(value)) {
                score += ScoreHeroForAutoPlay(
                    value,
                    1,
                    plan.focusGroup,
                    snapshot.recommendHeroId,
                    snapshot.starUpHeroId
                );
            } else if (type == 1) {
                EquipTableEntry equip;
                score += TryGetEquipTableEntry(value, &equip) ? 150 : 80;
            } else {
                score += 90;
            }
        }
    }

    const int* changes = nullptr;
    int changeCount = 0;
    if (TryGetManagedArrayData(biddingChange, &changes, &changeCount, 16)) {
        for (int i = 0; changes && i < changeCount; ++i) {
            if (changes[i] == 1 || changes[i] == 2) {
                score += plan.targetHeroId > 0 ? 220 : 120;
            } else if (changes[i] == 3 || changes[i] == 4) {
                score += 140;
            }
        }
    }

    return score;
}

// Evaluates auction slots and bids only when the gold plan allows spending.
bool TryAutoPlayAuction(
    const AutoPlaySnapshot& snapshot,
    const AutoPlayBoardPlan& plan,
    const AutoPlayGoldPlan& goldPlan,
    std::chrono::steady_clock::time_point now
) {
    if (!FeatureState::AutoPlayUseAuction.load() ||
        snapshot.accountId == 0 ||
        !Originals::MCLogicBattleData_get_logicRoundMgr ||
        !Originals::LogicRoundMgr_get_m_AuctionComp ||
        !Originals::MCLogicAuctionComp_get_m_CurrPhase ||
        !Originals::MCLogicAuctionComp_Bid ||
        !Originals::MCLogicAuctionComp_GetSelectItemIndexByAccId ||
        !CooldownElapsed(
            FeatureState::LastAutoPlayAuctionAttempt,
            RuntimeConfig::AutoPlayAuctionCooldownMs,
            now
        )) {
        return false;
    }

    void* roundMgr = Originals::MCLogicBattleData_get_logicRoundMgr(nullptr);
    void* auctionComp = roundMgr ? Originals::LogicRoundMgr_get_m_AuctionComp(roundMgr) : nullptr;
    if (!auctionComp) {
        return false;
    }

    int phase = Originals::MCLogicAuctionComp_get_m_CurrPhase(auctionComp);
    if (phase != 3) {
        return false;
    }

    if (Originals::MCLogicAuctionComp_GetSelectItemIndexByAccId(
            auctionComp,
            snapshot.accountId
        ) >= 0) {
        return false;
    }

    static FieldInfo* auctionItemListField = nullptr;
    static FieldInfo* indexField = nullptr;
    static FieldInfo* nextBidField = nullptr;
    static FieldInfo* activeField = nullptr;
    static FieldInfo* rewardListField = nullptr;

    if (!auctionItemListField) {
        auctionItemListField =
            GetFieldInfoFromName("Battle", "MCLogicAuctionComp", "auctionItemList");
    }

    if (!indexField) {
        indexField =
            GetFieldInfoFromName("Battle", "MCLogicAuctionSlotInfo", "<m_ItemIndex>k__BackingField");
    }

    if (!nextBidField) {
        nextBidField =
            GetFieldInfoFromName("Battle", "MCLogicAuctionSlotInfo", "<m_iBidPrice>k__BackingField");
    }

    if (!activeField) {
        activeField =
            GetFieldInfoFromName("Battle", "MCLogicAuctionSlotInfo", "<active>k__BackingField");
    }

    if (!rewardListField) {
        rewardListField = GetFieldInfoFromName(
            "Battle",
            "MCLogicAuctionSlotInfo",
            "m_RewardItemList"
        );
    }

    auto* auctionItems = GetField<MonoStructures::List<void*>*>(
        reinterpret_cast<Il2CppObject*>(auctionComp),
        auctionItemListField
    );
    void* const* slots = nullptr;
    int slotCount = 0;

    if (!TryGetManagedListData(auctionItems, &slots, &slotCount, 16)) {
        return false;
    }

    void* bestSlot = nullptr;
    int bestIndex = -1;
    int bestBid = 0;
    int bestScore = -999999;

    for (int i = 0; slots && i < slotCount; ++i) {
        void* slot = slots[i];
        if (!slot || !GetField<bool>(reinterpret_cast<Il2CppObject*>(slot), activeField)) {
            continue;
        }

        int bidPrice = GetField<int>(reinterpret_cast<Il2CppObject*>(slot), nextBidField);
        if (snapshot.coin >= 0 && bidPrice > std::max(snapshot.coin, 0)) {
            continue;
        }

        if (bidPrice > goldPlan.maxAuctionBid) {
            continue;
        }

        int score = 0;
        auto* rewardList = GetField<MonoStructures::List<void*>*>(
            reinterpret_cast<Il2CppObject*>(slot),
            rewardListField
        );
        void* const* rewards = nullptr;
        int rewardCount = 0;

        if (TryGetManagedListData(rewardList, &rewards, &rewardCount, 8)) {
            for (int rewardIndex = 0; rewards && rewardIndex < rewardCount; ++rewardIndex) {
                score += ScoreAuctionRewardItem(rewards[rewardIndex], plan, snapshot);
            }
        }

        score -= bidPrice * (snapshot.round < 10 ? 4 : 2);
        if (snapshot.hp >= 0 && snapshot.hp <= 40) {
            score += 100;
        }

        if (score > bestScore) {
            bestScore = score;
            bestSlot = slot;
            bestIndex = GetField<int>(reinterpret_cast<Il2CppObject*>(slot), indexField);
            bestBid = bidPrice;
        }
    }

    if (!bestSlot || bestScore < 120) {
        return false;
    }

    bool bidAccepted = Originals::MCLogicAuctionComp_Bid(
        auctionComp,
        bestSlot,
        snapshot.accountId,
        static_cast<uint32_t>(std::max(bestBid, 0))
    );
    FeatureState::LastAutoPlayAuctionAttempt = now;
    if (!bidAccepted) {
        return false;
    }

    FeatureState::AutoPlayBestAuctionIndex = bestIndex;
    FeatureState::AutoPlayBestAuctionScore = bestScore;
    return true;
}

// Reads auto play snapshot into a bounded native snapshot.
AutoPlaySnapshot ReadAutoPlaySnapshot(uint64_t selfAccountId) {
    AutoPlaySnapshot snapshot{};
    snapshot.accountId = selfAccountId;

    if (!IsIl2CppRuntimeReady() || selfAccountId == 0) {
        PublishAutoPlaySnapshotTelemetry(snapshot, false);
        return snapshot;
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetGameRound) {
        snapshot.round =
            static_cast<int>(Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr));
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetGamePhase) {
        snapshot.phase = Originals::MCLogicBattleData_ILOGIC_GetGamePhase(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetPlayerHP) {
        snapshot.hp = Originals::MCLogicBattleData_ILOGIC_GetPlayerHP(nullptr, selfAccountId);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin) {
        snapshot.coin =
            Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, selfAccountId);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetPlayerLevel) {
        snapshot.level =
            Originals::MCLogicBattleData_ILOGIC_GetPlayerLevel(nullptr, selfAccountId);
    }

    if (Originals::MCLogicBattleData_ILOGIC_SelfCurPopulation) {
        snapshot.currentPopulation =
            Originals::MCLogicBattleData_ILOGIC_SelfCurPopulation(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_SelfTotalPopulation) {
        snapshot.totalPopulation =
            Originals::MCLogicBattleData_ILOGIC_SelfTotalPopulation(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetSpareChessNum) {
        snapshot.spareChess =
            Originals::MCLogicBattleData_ILOGIC_GetSpareChessNum(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetBattleHeroNum) {
        snapshot.battleHeroCount =
            Originals::MCLogicBattleData_ILOGIC_GetBattleHeroNum(nullptr, selfAccountId);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetAllHeroNum) {
        snapshot.allHeroCount =
            Originals::MCLogicBattleData_ILOGIC_GetAllHeroNum(nullptr, selfAccountId);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetCurRefreshShopLevel) {
        snapshot.shopLevel =
            Originals::MCLogicBattleData_ILOGIC_GetCurRefreshShopLevel(nullptr, selfAccountId);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetRefreshCost) {
        snapshot.refreshCost =
            Originals::MCLogicBattleData_ILOGIC_GetRefreshCost(nullptr, selfAccountId);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup) {
        snapshot.recommendHeroId =
            Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetHeroByStarUp) {
        snapshot.starUpHeroId =
            Originals::MCLogicBattleData_ILOGIC_GetHeroByStarUp(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_IsFightSection) {
        snapshot.fightSection =
            Originals::MCLogicBattleData_ILOGIC_IsFightSection(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_IsFightResultSection) {
        snapshot.fightResultSection =
            Originals::MCLogicBattleData_ILOGIC_IsFightResultSection(nullptr);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetIsMonsterRound) {
        snapshot.monsterRound =
            Originals::MCLogicBattleData_ILOGIC_GetIsMonsterRound(nullptr);
    }

    void* selfManager = GetSelfLogicBattleManager();
    if (selfManager && Originals::MCLogicBattleManager_GetLineupWorth) {
        snapshot.lineupWorth = Originals::MCLogicBattleManager_GetLineupWorth(selfManager);
    }

    if (selfManager && Originals::MCLogicBattleManager_CalcCurrentFightValue) {
        snapshot.fightValue =
            Originals::MCLogicBattleManager_CalcCurrentFightValue(selfManager);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID) {
        snapshot.currentOpponentId =
            Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                nullptr,
                selfAccountId
            );
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);

        for (const auto& item : CopyDictionaryEntries(battleManagers, 16)) {
            uint64_t accountId = item.first;
            void* manager = item.second;

            if (accountId == 0 || accountId == selfAccountId || !manager) {
                continue;
            }

            snapshot.opponentCount++;

            int opponentFightValue = Originals::MCLogicBattleManager_CalcCurrentFightValue ?
                Originals::MCLogicBattleManager_CalcCurrentFightValue(manager) :
                -1;

            if (opponentFightValue > snapshot.strongestOpponentFightValue) {
                snapshot.strongestOpponentFightValue = opponentFightValue;
            }

            if (accountId == snapshot.currentOpponentId) {
                snapshot.currentOpponentFightValue = opponentFightValue;
            }

            if (Originals::MCLogicBattleData_ILogic_HeroOwnCount) {
                bool ownsTarget = false;

                if (snapshot.recommendHeroId > 0 &&
                    Originals::MCLogicBattleData_ILogic_HeroOwnCount(
                        nullptr,
                        accountId,
                        snapshot.recommendHeroId
                    ) > 0) {
                    ownsTarget = true;
                }

                if (snapshot.starUpHeroId > 0 &&
                    snapshot.starUpHeroId != snapshot.recommendHeroId &&
                    Originals::MCLogicBattleData_ILogic_HeroOwnCount(
                        nullptr,
                        accountId,
                        snapshot.starUpHeroId
                    ) > 0) {
                    ownsTarget = true;
                }

                if (ownsTarget) {
                    snapshot.contestedTargetOwners++;
                }
            }
        }
    }

    FeatureState::AutoPlayOpponentCount = snapshot.opponentCount;
    FeatureState::AutoPlayCurrentOpponentId = snapshot.currentOpponentId;
    FeatureState::AutoPlayStrongestOpponentFightValue =
        std::max(snapshot.strongestOpponentFightValue, 0);
    FeatureState::AutoPlayContestedTargets = snapshot.contestedTargetOwners;
    PublishAutoPlaySnapshotTelemetry(snapshot, snapshot.round > 0);

    return snapshot;
}

// Coordinates update auto play learning for the overlay runtime.
int UpdateAutoPlayLearning(const AutoPlaySnapshot& snapshot) {
    int pressure = std::clamp(FeatureState::AutoPlayPressure.load(), 0, 100);

    if (snapshot.accountId == 0 || snapshot.round <= 0) {
        FeatureState::AutoPlayPressure = pressure;
        return pressure;
    }

    int lastRound = FeatureState::AutoPlayLastRound.load();
    int lastHp = FeatureState::AutoPlayLastHp.load();

    if (lastRound != snapshot.round) {
        if (lastRound > 0 && lastHp >= 0 && snapshot.hp >= 0) {
            int hpDelta = lastHp - snapshot.hp;
            pressure += hpDelta > 0 ? std::clamp(10 + hpDelta, 10, 35) : -4;
        }

        if (snapshot.hp >= 0 && snapshot.hp <= 30) {
            pressure += 20;
        }

        if (snapshot.round >= 15) {
            pressure += 8;
        }

        if (snapshot.currentOpponentFightValue > 0 &&
            snapshot.fightValue > 0 &&
            snapshot.currentOpponentFightValue > snapshot.fightValue) {
            pressure += 10;
        }

        if (snapshot.strongestOpponentFightValue > 0 &&
            snapshot.fightValue > 0 &&
            snapshot.strongestOpponentFightValue > snapshot.fightValue + 1000) {
            pressure += 8;
        }

        if (snapshot.coin >= 50 && snapshot.hp >= 50 && snapshot.round < 12) {
            pressure -= 8;
        }

        FeatureState::AutoPlayLastRound = snapshot.round;
        FeatureState::AutoPlayLastHp = snapshot.hp;
        FeatureState::AutoPlayLearnedRounds =
            std::min(FeatureState::AutoPlayLearnedRounds.load() + 1, 999999);
    } else if (snapshot.hp >= 0) {
        FeatureState::AutoPlayLastHp = snapshot.hp;
    }

    pressure = std::clamp(pressure, 0, 100);
    FeatureState::AutoPlayPressure = pressure;
    return pressure;
}

// Selects auto play strategy from the current safe runtime options.
int SelectAutoPlayStrategy(const AutoPlaySnapshot& snapshot, int pressure) {
    int strategy = ClampAutoPlayStrategy(FeatureState::AutoPlayStrategy.load());

    if (FeatureState::AutoPlayAdaptive.load()) {
        if (pressure >= 55 ||
            (snapshot.hp >= 0 && snapshot.hp <= 35) ||
            (snapshot.currentOpponentFightValue > 0 &&
             snapshot.fightValue > 0 &&
             snapshot.currentOpponentFightValue > snapshot.fightValue) ||
            snapshot.round >= 16) {
            strategy = AutoPlayStrategyAggressive;
        } else if (pressure <= 20 &&
                   snapshot.round > 0 &&
                   snapshot.round < 10 &&
                   snapshot.coin >= 50 &&
                   (snapshot.hp < 0 || snapshot.hp >= 60)) {
            strategy = AutoPlayStrategyEconomy;
        } else {
            strategy = AutoPlayStrategyBalanced;
        }
    }

    int previousStrategy = ClampAutoPlayStrategy(FeatureState::AutoPlayStrategy.load());
    if (strategy != previousStrategy) {
        FeatureState::AutoPlayStrategyChanges =
            std::min(FeatureState::AutoPlayStrategyChanges.load() + 1, 999999);
    }

    FeatureState::AutoPlayStrategy = strategy;
    return strategy;
}

// Converts combat pressure into economy pressure so gold plans can protect
// interest tiers until survival or population needs justify spending down.
int EstimateAutoPlayGoldThreat(const AutoPlaySnapshot& snapshot, int pressure) {
    int threat = std::clamp(pressure, 0, 100);

    if (snapshot.hp >= 0) {
        if (snapshot.hp <= 25) {
            threat += 35;
        } else if (snapshot.hp <= 40) {
            threat += 22;
        } else if (snapshot.hp <= 55) {
            threat += 10;
        }
    }

    int opponentValue = std::max(
        snapshot.currentOpponentFightValue,
        snapshot.strongestOpponentFightValue
    );
    if (opponentValue > 0 && snapshot.fightValue > 0 && opponentValue > snapshot.fightValue) {
        int deficit = std::min(opponentValue - snapshot.fightValue, 50000);
        threat += std::clamp(deficit / 700, 6, 28);
    }

    if (snapshot.round >= 18) {
        threat += 22;
    } else if (snapshot.round >= 14) {
        threat += 12;
    } else if (snapshot.round > 0 && snapshot.round <= 8) {
        threat -= 8;
    }

    return std::clamp(threat, 0, 100);
}

// Computes auto play needs population gold data for the Auto-Play controller.
bool AutoPlayNeedsPopulationGold(const AutoPlaySnapshot& snapshot) {
    if (snapshot.currentPopulation < 0 || snapshot.totalPopulation <= 0) {
        return false;
    }

    if (snapshot.currentPopulation >= snapshot.totalPopulation) {
        return true;
    }

    return snapshot.spareChess > 2 &&
        snapshot.currentPopulation + 1 >= snapshot.totalPopulation;
}

// Builds one bounded economy plan for shop, auction, arena assists, and level-up
// actions so they do not fight over gold interest breakpoints independently.
AutoPlayGoldPlan BuildAutoPlayGoldPlan(
    const AutoPlaySnapshot& snapshot,
    int strategy,
    int pressure
) {
    AutoPlayGoldPlan plan{};
    strategy = ClampAutoPlayStrategy(strategy);

    int configuredReserve =
        std::clamp(FeatureState::AutoPlayMinReserveGold.load(), 0, 999999);
    bool hasCoin = snapshot.coin >= 0;
    int coin = hasCoin ? ClampAutoPlayGold(snapshot.coin, 999999999) : configuredReserve;
    int threat = EstimateAutoPlayGoldThreat(snapshot, pressure);
    int tier = AutoPlayInterestTierForGold(coin);
    int currentInterestFloor = AutoPlayInterestFloorForTier(tier);
    int nextInterestGold = AutoPlayNextInterestGoldForGold(coin);
    int nextInterestGap = hasCoin ? nextInterestGold - coin : 999999;
    bool earlyOrMidGame = snapshot.round <= 0 || snapshot.round <= 14;
    bool healthyEnough = snapshot.hp < 0 || snapshot.hp >= 45;
    bool canBank =
        strategy != AutoPlayStrategyAggressive &&
        earlyOrMidGame &&
        healthyEnough &&
        threat < (strategy == AutoPlayStrategyEconomy ? 72 : 54);
    int reserve = configuredReserve;
    int levelReserve = configuredReserve;

    if (strategy == AutoPlayStrategyEconomy) {
        reserve = std::max(reserve, 40);
        if (canBank) {
            reserve = std::max(reserve, 50);
        }
    } else if (strategy == AutoPlayStrategyAggressive) {
        reserve = std::min(reserve, threat >= 70 ? 0 : 8);
    } else {
        reserve = std::max(reserve, snapshot.round >= 12 ? 10 : 20);
    }

    if (canBank) {
        int chaseWindow = strategy == AutoPlayStrategyEconomy ? 8 : 4;
        if (tier < 5 && nextInterestGap > 0 && nextInterestGap <= chaseWindow) {
            reserve = std::max(reserve, nextInterestGold);
            plan.holdForInterest = true;
        } else if (tier > 0) {
            reserve = std::max(reserve, currentInterestFloor);
        }
    }

    if (threat >= 80) {
        reserve = std::min(reserve, configuredReserve);
    } else if (threat >= 65) {
        reserve = std::min(reserve, std::max(configuredReserve, currentInterestFloor - 10));
    } else if (threat >= 50 && strategy != AutoPlayStrategyEconomy) {
        reserve = std::min(reserve, std::max(configuredReserve, currentInterestFloor));
    }

    reserve = std::clamp(reserve, 0, 999999);
    if (hasCoin && coin < reserve) {
        plan.holdForInterest = true;
    }

    bool populationPressure = AutoPlayNeedsPopulationGold(snapshot);
    levelReserve = reserve;
    if (populationPressure || snapshot.round >= 12) {
        levelReserve = std::min(levelReserve, currentInterestFloor);
    }
    if (strategy == AutoPlayStrategyAggressive || threat >= 75) {
        levelReserve = 0;
    }
    levelReserve = std::clamp(levelReserve, 0, 999999);

    int auctionReserve = reserve;
    if (snapshot.round >= 14 || threat >= 65) {
        auctionReserve = std::min(auctionReserve, levelReserve);
    }
    if (strategy == AutoPlayStrategyAggressive || threat >= 80) {
        auctionReserve = 0;
    }

    int spendBudget = hasCoin ? std::max(coin - reserve, 0) : 0;
    int maxAuctionBid = hasCoin ? std::max(coin - auctionReserve, 0) : 999999;
    int passiveTarget = 80;
    int recommendationTarget = 6;
    bool critical = threat >= 75 || (snapshot.hp >= 0 && snapshot.hp <= 35);

    if (strategy == AutoPlayStrategyEconomy) {
        passiveTarget = snapshot.round >= 12 ? 80 : std::max(50, nextInterestGold);
        recommendationTarget = 6;
    } else if (strategy == AutoPlayStrategyAggressive) {
        passiveTarget = 999999;
        recommendationTarget = 9;
    } else {
        passiveTarget = (critical || snapshot.round >= 12) ? 120 : std::max(60, reserve + 20);
        recommendationTarget = (critical || snapshot.round >= 10) ? 9 : 6;
    }

    plan.interestTier = tier;
    plan.nextInterestGold = nextInterestGold;
    plan.reserveGold = reserve;
    plan.levelReserveGold = levelReserve;
    plan.spendBudget = spendBudget;
    plan.passiveTargetGold = std::clamp(passiveTarget, 0, 999999999);
    plan.recommendationTarget = std::clamp(recommendationTarget, 1, 99);
    plan.maxAuctionBid = std::clamp(maxAuctionBid, 0, 999999999);
    plan.useFreeEconomy = strategy == AutoPlayStrategyAggressive || critical;
    plan.forceLevel =
        strategy == AutoPlayStrategyAggressive ||
        critical ||
        populationPressure ||
        snapshot.round >= 12;

    return plan;
}

// Publishes auto play gold plan into atomic UI telemetry.
void PublishAutoPlayGoldPlan(const AutoPlayGoldPlan& plan) {
    FeatureState::AutoPlayInterestTier = plan.interestTier;
    FeatureState::AutoPlayInterestNextGold = plan.nextInterestGold;
    FeatureState::AutoPlayInterestReserveGold = plan.reserveGold;
    FeatureState::AutoPlaySpendBudget = plan.spendBudget;
    FeatureState::AutoPlayHoldInterest = plan.holdForInterest;
}

// Applies Auto-Play ownership of Shop, Combat, and Arena assist toggles.
void ApplyAutoPlayPolicy(
    const AutoPlaySnapshot& snapshot,
    int strategy,
    const AutoPlayGoldPlan& goldPlan
) {
    CaptureAutoPlayPolicyBackup();

    int reserve = goldPlan.reserveGold;
    int targetGold = goldPlan.passiveTargetGold;
    int recommendationTarget = goldPlan.recommendationTarget;

    if (FeatureState::AutoPlayUseShop.load()) {
        FeatureState::ShopBuyFreeHero = true;
        FeatureState::ShopBuySelectedHero = true;
        FeatureState::ShopBuyRecommendLineup = true;
        FeatureState::ShopRefresh = true;
        FeatureState::ShopStopRefreshAtFreeHero = true;
        FeatureState::ShopStopRefreshAtSelectedHero = true;
        FeatureState::ShopStopRefreshAtRecommendLineup = true;
        FeatureState::ShopKeepGold = true;
        FeatureState::ShopKeepGoldAt = reserve;
        FeatureState::ShopRecommendTargetCount = recommendationTarget;
    }

    if (FeatureState::AutoPlayUseArenaAssist.load()) {
        FeatureState::ArenaForceActiveSynergy = true;
        FeatureState::ArenaNoShopLock = true;
        FeatureState::ArenaUnlimitedHeroPool = true;
        FeatureState::ArenaPassiveGold = FeatureState::AutoPlayUseEconomy.load();
        FeatureState::ArenaGoldTarget = targetGold;
        FeatureState::ArenaFreeEconomy =
            FeatureState::AutoPlayUseEconomy.load() &&
            goldPlan.useFreeEconomy;
        FeatureState::ArenaForceLevel99 =
            FeatureState::AutoPlayUseEconomy.load() &&
            goldPlan.forceLevel;
        FeatureState::ArenaAllEnemyHpOne =
            strategy == AutoPlayStrategyAggressive ||
            (snapshot.hp >= 0 && snapshot.hp <= 35);
    }

    if (FeatureState::AutoPlayUseCombat.load()) {
        FeatureState::CombatPreventHpLoss = true;
        FeatureState::CombatBoostAttackRatio = true;
        FeatureState::CombatAttackRatioPercent =
            strategy == AutoPlayStrategyAggressive ? 10000 : 5000;
        FeatureState::CombatFightValue =
            strategy == AutoPlayStrategyAggressive ? 999999999 : 500000000;
        FeatureState::CombatForceWin =
            strategy == AutoPlayStrategyAggressive ||
            snapshot.round >= 8 ||
            (snapshot.hp >= 0 && snapshot.hp <= 60);
        FeatureState::CombatCrippleEnemies =
            strategy == AutoPlayStrategyAggressive ||
            (snapshot.hp >= 0 && snapshot.hp <= 45);
        FeatureState::CombatEnemyAttackRatioPercent =
            strategy == AutoPlayStrategyAggressive ? 0 : 1;
    }
}

// Stops built in auto play ai and restores owned runtime state.
void StopBuiltInAutoPlayAI(uint64_t selfAccountId) {
    void* selfManager =
        selfAccountId != 0 ? GetBattleManagerByAccountId(selfAccountId) : GetSelfLogicBattleManager();

    if (FeatureState::AutoPlayBuiltInAiRunning.load() &&
        selfManager &&
        Originals::MCLogicBattleManager_StopAI) {
        Originals::MCLogicBattleManager_StopAI(selfManager);
    }

    FeatureState::AutoPlayBuiltInAiRunning = false;
    FeatureState::AutoPlayLastAiStartAccountId = 0;
}

// Stops auto play runtime and restores owned runtime state.
void StopAutoPlayRuntime(uint64_t selfAccountId) {
    StopBuiltInAutoPlayAI(selfAccountId);
    RestoreAutoPlayPolicyBackup();
    FeatureState::AutoPlayWasRunning = false;
}

// Checks whether it is safe to call the game's built-in AI entry points.
bool IsAutoPlayBuiltInAiSafePhase(const AutoPlaySnapshot& snapshot) {
    if (snapshot.fightSection || snapshot.fightResultSection || snapshot.monsterRound) {
        return false;
    }

    if (snapshot.phase < 0) {
        return true;
    }

    return snapshot.phase == GamePhasePrepare ||
        snapshot.phase == GamePhaseWelfare ||
        snapshot.phase == GamePhaseForecast ||
        snapshot.phase == GamePhaseRegionPick ||
        snapshot.phase == GamePhaseAuction;
}

// Starts or refreshes the built-in AI on cooldown without replaying it every tick.
void RunBuiltInAutoPlayAI(
    uint64_t selfAccountId,
    const AutoPlaySnapshot& snapshot,
    std::chrono::steady_clock::time_point now
) {
    if (!FeatureState::AutoPlayUseBuiltInAI.load()) {
        StopBuiltInAutoPlayAI(selfAccountId);
        return;
    }

    void* selfManager = GetSelfLogicBattleManager();
    if (!selfManager) {
        return;
    }

    if (!IsAutoPlayBuiltInAiSafePhase(snapshot)) {
        return;
    }

    uint64_t lastAiAccountId = FeatureState::AutoPlayLastAiStartAccountId.load();
    bool aiRunningForAccount =
        FeatureState::AutoPlayBuiltInAiRunning.load() &&
        lastAiAccountId == selfAccountId;
    bool needsStart = !aiRunningForAccount;
    bool startCooldownElapsed = CooldownElapsed(
        FeatureState::LastAutoPlayAiStartAttempt,
        RuntimeConfig::AutoPlayAiStartCooldownMs,
        now
    );
    bool refreshCooldownElapsed = CooldownElapsed(
        FeatureState::LastAutoPlayAiStartAttempt,
        RuntimeConfig::AutoPlayAiRefreshMs,
        now
    );

    if (Originals::MCLogicBattleManager_StartAI &&
        ((needsStart && startCooldownElapsed) ||
         (!needsStart && refreshCooldownElapsed))) {
        int difficulty = std::clamp(FeatureState::AutoPlayAiDifficulty.load(), 1, 10);
        FeatureState::AutoPlayAiDifficulty = difficulty;
        Originals::MCLogicBattleManager_StartAI(selfManager, difficulty);
        FeatureState::AutoPlayBuiltInAiRunning = true;
        FeatureState::AutoPlayLastAiStartAccountId = selfAccountId;
        FeatureState::LastAutoPlayAiStartAttempt = now;
    }

    if (!snapshot.fightSection &&
        Originals::MCLogicBattleManager_TryAutoDeploy &&
        CooldownElapsed(
            FeatureState::LastAutoPlayDeployAttempt,
            RuntimeConfig::AutoPlayDeployCooldownMs,
            now
        )) {
        Originals::MCLogicBattleManager_TryAutoDeploy(selfManager);
        FeatureState::LastAutoPlayDeployAttempt = now;
    }
}

// Triggers a level-up only when population pressure, budget, and cooldown all allow it.
void TryAutoPlayLevelUp(
    const AutoPlaySnapshot& snapshot,
    const AutoPlayGoldPlan& goldPlan,
    std::chrono::steady_clock::time_point now
) {
    if (!FeatureState::AutoPlayUseEconomy.load() ||
        FeatureState::ArenaForceLevel99.load() ||
        snapshot.fightSection ||
        snapshot.accountId == 0 ||
        snapshot.level <= 0 ||
        snapshot.coin < 0 ||
        !Originals::MCLogicBattleManager_OnPlayerLvlUp ||
        !Originals::MCLogicBattleData_ILOGIC_GetUpgradeCost ||
        !Originals::MCLogicBattleData_ILOGIC_CanUpgrade ||
        !CooldownElapsed(
            FeatureState::LastAutoPlayLevelAttempt,
            RuntimeConfig::AutoPlayLevelCooldownMs,
            now
        )) {
        return;
    }

    int targetLevel = std::clamp(FeatureState::AutoPlayTargetLevel.load(), 1, 99);
    FeatureState::AutoPlayTargetLevel = targetLevel;

    if (snapshot.level >= targetLevel) {
        return;
    }

    int upgradeCost =
        Originals::MCLogicBattleData_ILOGIC_GetUpgradeCost(nullptr, snapshot.accountId);

    if (upgradeCost < 0 || snapshot.coin < upgradeCost) {
        return;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_CanUpgrade(
            nullptr,
            snapshot.accountId,
            snapshot.coin
        )) {
        return;
    }

    if (snapshot.coin - upgradeCost < goldPlan.levelReserveGold) {
        return;
    }

    void* selfManager = GetSelfLogicBattleManager();
    if (!selfManager) {
        return;
    }

    Originals::MCLogicBattleManager_OnPlayerLvlUp(selfManager);
    FeatureState::LastAutoPlayLevelAttempt = now;
}

// Checks whether the Auto-Play snapshot has enough live battle data to act on.
bool IsAutoPlaySnapshotActionable(const AutoPlaySnapshot& snapshot) {
    return snapshot.accountId != 0 &&
        snapshot.round > 0 &&
        snapshot.hp > 0 &&
        snapshot.coin >= 0 &&
        snapshot.level > 0;
}

// Runs one Auto-Play controller tick and defers lower-priority work on busy frames.
void RunAutoPlay(
    uint64_t selfAccountId,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point frameStart = {}
) {
    if (!FeatureState::AutoPlayEnabled.load()) {
        if (FeatureState::AutoPlayWasRunning.load()) {
            StopAutoPlayRuntime(selfAccountId);
        }
        return;
    }

    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    if (selfAccountId == 0) {
        if (FeatureState::AutoPlayWasRunning.load()) {
            StopAutoPlayRuntime(0);
        }
        return;
    }

    auto budgetExceeded = [&frameStart]() {
        return frameStart.time_since_epoch().count() != 0 &&
            FrameBudgetExceeded(frameStart);
    };

    if (budgetExceeded()) {
        return;
    }

    AutoPlaySnapshot snapshot = ReadAutoPlaySnapshot(selfAccountId);
    if (!IsAutoPlaySnapshotActionable(snapshot)) {
        StopAutoPlayRuntime(selfAccountId);
        return;
    }

    if (budgetExceeded()) {
        return;
    }

    int pressure = UpdateAutoPlayLearning(snapshot);
    int strategy = SelectAutoPlayStrategy(snapshot, pressure);
    AutoPlayGoldPlan goldPlan = BuildAutoPlayGoldPlan(snapshot, strategy, pressure);
    std::vector<AutoPlayBoardUnit> selfUnits = CollectAutoPlayBoardUnits(GetSelfLogicBattleManager());
    std::vector<AutoPlayBoardUnit> enemyUnits;

    if (budgetExceeded()) {
        return;
    }

    if (snapshot.currentOpponentId != 0) {
        enemyUnits = CollectAutoPlayBoardUnits(
            GetBattleManagerByAccountId(snapshot.currentOpponentId)
        );
    }

    if (budgetExceeded()) {
        return;
    }

    if (enemyUnits.empty() && Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
        int scannedFallbackManagers = 0;
        for (const auto& item : CopyDictionaryEntries(battleManagers, 16)) {
            if (item.first == 0 || item.first == selfAccountId || !item.second) {
                continue;
            }

            std::vector<AutoPlayBoardUnit> units = CollectAutoPlayBoardUnits(item.second);
            enemyUnits.insert(enemyUnits.end(), units.begin(), units.end());
            ++scannedFallbackManagers;

            if (scannedFallbackManagers >= RuntimeConfig::MaxAutoPlayFallbackEnemyManagers ||
                budgetExceeded()) {
                break;
            }
        }
    }

    if (budgetExceeded()) {
        return;
    }

    AutoPlayBoardPlan plan = BuildAutoPlayBoardPlan(snapshot, selfUnits, enemyUnits);
    plan.contestedTargets = snapshot.contestedTargetOwners;
    PublishAutoPlayBoardPlan(plan);
    PublishAutoPlayGoldPlan(goldPlan);

    ApplyAutoPlayPolicy(snapshot, strategy, goldPlan);
    FeatureState::AutoPlayWasRunning = true;

    ApplyAutoPlayShopTargets(plan, snapshot);
    if (budgetExceeded()) {
        return;
    }

    ApplyAutoPlayGoGoCardChoice(snapshot, plan);
    if (budgetExceeded()) {
        return;
    }

    TryAutoPlayAuction(snapshot, plan, goldPlan, now);
    if (budgetExceeded()) {
        return;
    }

    RunBuiltInAutoPlayAI(selfAccountId, snapshot, now);
    if (budgetExceeded()) {
        return;
    }

    TryAutoPlaySmartFormation(snapshot, plan, selfUnits, enemyUnits, now);
    if (budgetExceeded()) {
        return;
    }

    TryAutoPlayLevelUp(snapshot, goldPlan, now);
}

// Runs shop automation for one bounded feature tick.
void RunShopAutomation(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    bool anyShopAutomation =
        FeatureState::ShopBuyFreeHero ||
        FeatureState::ShopBuySelectedHero ||
        FeatureState::ShopBuyRecommendLineup ||
        FeatureState::ShopRefresh ||
        FeatureState::ShopStopRefreshAtFreeHero ||
        FeatureState::ShopStopRefreshAtSelectedHero ||
        FeatureState::ShopStopRefreshAtRecommendLineup;

    if (!anyShopAutomation || selfAccountId == 0) {
        return;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetShopItemData ||
        !Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin) {
        return;
    }

    auto now = std::chrono::steady_clock::now();

    if (!CooldownElapsed(
            FeatureState::LastShopAction,
            RuntimeConfig::ShopActionCooldownMs,
            now
        )) {
        return;
    }

    bool hasBuyAction =
        FeatureState::ShopBuyFreeHero ||
        FeatureState::ShopBuySelectedHero ||
        FeatureState::ShopBuyRecommendLineup;
    bool hasRefreshAction = FeatureState::ShopRefresh.load();
    if ((hasBuyAction || hasRefreshAction) && !IsShopPanelReadyForAutomation()) {
        return;
    }

    bool needRefreshShop = true;
    int cachedCoin = -1;
    bool useSelectedTargets =
        FeatureState::ShopBuySelectedHero ||
        FeatureState::ShopStopRefreshAtSelectedHero;
    bool useRecommendLineup =
        FeatureState::ShopBuyRecommendLineup ||
        FeatureState::ShopStopRefreshAtRecommendLineup;
    int recommendHeroId = useRecommendLineup ? GetRecommendLineupHeroId(now) : 0;
    std::unordered_map<int, HeroAutomationState> shopTargets =
        useSelectedTargets ?
            GetShopHeroTargetsSnapshot() :
            std::unordered_map<int, HeroAutomationState>{};

    auto getCoin = [&]() {
        if (cachedCoin < 0) {
            cachedCoin =
                Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, selfAccountId);
        }

        return cachedCoin;
    };

    for (int slot = 0; slot < 5; ++slot) {
        bool needFx = false;
        bool isFreeBuy = false;

        if (Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy &&
            (FeatureState::ShopBuyFreeHero || FeatureState::ShopStopRefreshAtFreeHero)) {
            isFreeBuy = Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy(
                nullptr,
                selfAccountId,
                slot,
                &needFx
            );
        }

        if (FeatureState::ArenaFreeEconomy) {
            isFreeBuy = true;
            needFx = false;
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

        int heroId = shopData.m_iHeroId;
        auto selectedIt = shopTargets.find(heroId);
        bool isSelected =
            useSelectedTargets &&
            selectedIt != shopTargets.end() &&
            selectedIt->second.selected;
        bool isRecommend =
            useRecommendLineup &&
            IsRecommendLineupHero(heroId, recommendHeroId);
        int selectedTargetCount =
            isSelected ?
                std::clamp(selectedIt->second.targetCount, 1, 99) :
                0;
        int recommendTargetCount = isRecommend ? GetRecommendLineupTargetCount() : 0;
        int ownCount = -1;
        bool needsOwnCount =
            (isSelected &&
             (FeatureState::ShopStopRefreshAtSelectedHero ||
              FeatureState::ShopBuySelectedHero)) ||
            (isRecommend &&
             (FeatureState::ShopStopRefreshAtRecommendLineup ||
              FeatureState::ShopBuyRecommendLineup));

        if (needsOwnCount && Originals::MCLogicBattleData_ILogic_HeroOwnCount) {
            ownCount = Originals::MCLogicBattleData_ILogic_HeroOwnCount(
                nullptr,
                selfAccountId,
                heroId
            );
        }

        if (FeatureState::ShopStopRefreshAtSelectedHero &&
            isSelected &&
            ownCount >= 0 &&
            ownCount < selectedTargetCount) {
            needRefreshShop = false;
        }

        if (FeatureState::ShopStopRefreshAtRecommendLineup &&
            isRecommend &&
            ownCount >= 0 &&
            ownCount < recommendTargetCount) {
            needRefreshShop = false;
        }

        if (FeatureState::ShopStopRefreshAtFreeHero && isFreeBuy) {
            needRefreshShop = false;
        }

        if (FeatureState::ShopBuyFreeHero && isFreeBuy) {
            if (!CanAttemptShopBuy(selfAccountId, slot, shopData, ownCount, true, now)) {
                continue;
            }

            if (SelectShopSlot(slot)) {
                MarkShopBuyAttempt(selfAccountId, slot, shopData, ownCount, true, now);
                return;
            }
        }

        bool canBuySelected = FeatureState::ShopBuySelectedHero && isSelected;
        bool canBuyRecommend = FeatureState::ShopBuyRecommendLineup && isRecommend;

        if (!canBuySelected && !canBuyRecommend) {
            continue;
        }

        int targetCount = std::max(
            canBuySelected ? selectedTargetCount : 0,
            canBuyRecommend ? recommendTargetCount : 0
        );

        if (targetCount <= 0 || ownCount < 0) {
            continue;
        }

        if (ownCount >= 0 && ownCount >= targetCount) {
            continue;
        }

        int coin = getCoin();

        if (coin < shopData.m_iPrice) {
            continue;
        }

        if (FeatureState::ShopKeepGold &&
            coin - shopData.m_iPrice < FeatureState::ShopKeepGoldAt) {
            continue;
        }

        if (!CanAttemptShopBuy(selfAccountId, slot, shopData, ownCount, false, now)) {
            continue;
        }

        if (SelectShopSlot(slot)) {
            MarkShopBuyAttempt(selfAccountId, slot, shopData, ownCount, false, now);
            return;
        }
    }

    if (!FeatureState::ShopRefresh ||
        !needRefreshShop ||
        !Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop) {
        return;
    }

    void* heroShopPanel = FeatureState::HeroShopPanel.load();
    if (!heroShopPanel || !IsShopPanelReadyForAutomation()) {
        return;
    }

    if (!HasWorthwhileShopTarget(selfAccountId, now) ||
        !CanAttemptShopRefresh(now)) {
        return;
    }

    int refreshCost = FeatureState::ArenaFreeEconomy ? 0 :
        Originals::MCLogicBattleData_ILOGIC_GetRefreshCost ?
        Originals::MCLogicBattleData_ILOGIC_GetRefreshCost(nullptr, selfAccountId) :
        0;
    bool isFreeRefresh = FeatureState::ArenaFreeEconomy ||
        (Originals::MCLogicBattleData_ILOGIC_IsRefreshFree &&
         Originals::MCLogicBattleData_ILOGIC_IsRefreshFree(nullptr, selfAccountId));
    int coin = getCoin();
    bool canRefresh =
        isFreeRefresh ||
        (coin >= refreshCost &&
         (!FeatureState::ShopKeepGold ||
          coin - refreshCost >= FeatureState::ShopKeepGoldAt));

    if (canRefresh) {
        Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop(heroShopPanel);
        MarkShopRefreshAttempt(now);
    }
}

// Advances features on its scheduled feature cadence.
void TickFeatures() {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    auto frameStart = std::chrono::steady_clock::now();
    auto now = frameStart;
    int activeMainTab =
        std::clamp(UiState::MainTabIndex.load(), 0, static_cast<int>(MainTabTest));

    RetryFeatureBindingsIfNeeded();
    // Let setup-thread binding scans finish before the render thread does managed work.
    if (RuntimeState::BindingResolveInProgress.load()) {
        return;
    }

    if (FrameBudgetExceeded(frameStart)) {
        return;
    }

    RefreshManagedReferences();
    if (FrameBudgetExceeded(frameStart)) {
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();
    RefreshTableDataForMatch(selfAccountId);
    if (FrameBudgetExceeded(frameStart)) {
        return;
    }

    if (ShouldLoadTableDataForFrame(activeMainTab)) {
        EnsureTableDataLoaded();
        if (FrameBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (selfAccountId == 0) {
        PublishAutoPlaySnapshotTelemetry(AutoPlaySnapshot{}, false);
    }

    if (activeMainTab == MainTabInfo) {
        RefreshGgcInfo(false);
        RefreshInfoPlayerRows(false);
    }

    if (activeMainTab == MainTabShop ||
        FeatureState::ShopBuyRecommendLineup.load() ||
        FeatureState::ShopStopRefreshAtRecommendLineup.load() ||
        (FeatureState::AutoPlayEnabled.load() && FeatureState::AutoPlayUseShop.load())) {
        GetRecommendLineupHeroId(now);
    }

    if (Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
        (activeMainTab == MainTabArena ||
         FeatureState::ArenaSkipRound.load() ||
         FeatureState::AutoPlayEnabled.load())) {
        FeatureState::CachedGameRound =
            Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr);
    } else if (selfAccountId == 0) {
        FeatureState::CachedGameRound = 0;
    }

    bool predictionRowsRebuilt = false;
    if (activeMainTab == MainTabTest || UiState::ShowNextEnemyHud.load()) {
        predictionRowsRebuilt = RefreshCachedOpponentPredictionRows(selfAccountId, now);
        if (predictionRowsRebuilt) {
            FeatureState::LastOpponentPredictionTick = now;
        }

        if (FrameBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (UiState::ShowNextEnemyHud.load()) {
        RefreshNextEnemyHudText(selfAccountId);
        if (FrameBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (IntervalElapsed(FeatureState::LastAutoPlayTick, RuntimeConfig::AutoPlayTickMs, now)) {
        RunAutoPlay(selfAccountId, now, frameStart);
        if (FrameBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (IntervalElapsed(FeatureState::LastArenaTick, RuntimeConfig::ArenaTickMs, now)) {
        ApplyArenaState(selfAccountId);
        if (FrameBudgetExceeded(frameStart)) {
            return;
        }
    }

    if (IntervalElapsed(FeatureState::LastShopTick, RuntimeConfig::ShopTickMs, now)) {
        RunShopAutomation(selfAccountId);
        if (FrameBudgetExceeded(frameStart)) {
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
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextColored(
        ready ? ImVec4(0.40f, 0.90f, 0.45f, 1.0f) : ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
        "%s",
        ready ? "Ready" : "Waiting"
    );
}

// Draws the value row overlay section without changing game state.
void DrawValueRow(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
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
    ImGui::TextUnformatted("Library update status");
    ImGui::SameLine();
    ImGui::TextColored(
        UpdateStatusColor(snapshot.status),
        "%s",
        UpdateStatusLabel(snapshot.status)
    );
}

// Draws scrollable release notes for cached GitHub release metadata.
void DrawUpdateChangelog(const std::vector<ReleaseInfo>& releases) {
    if (releases.empty()) {
        ImGui::TextUnformatted("Waiting for release metadata");
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
            ImGui::Text("Released: %s", release.publishedAt.c_str());
        }

        if (release.body.empty()) {
            ImGui::TextUnformatted("No release notes provided");
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

    if (!ImGui::CollapsingHeader("Updates / Changelog")) {
        return;
    }

    if (ImGui::BeginTable(
        "##UpdateStatusTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        ImGui::TableSetupColumn("Field");
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        DrawValueRow("Repository", snapshot.repository);
        DrawValueRow(
            "Current version",
            IsKnownBuildMetadata(snapshot.localVersion) ? snapshot.localVersion : "Unknown"
        );
        DrawValueRow("Current commit", ShortCommit(snapshot.localCommit));
        DrawValueRow(
            "Current ref",
            IsKnownBuildMetadata(snapshot.localRef) ? snapshot.localRef : "unknown"
        );
        DrawValueRow(
            "Latest version",
            snapshot.latestVersion.empty() ? "Waiting" : snapshot.latestVersion
        );
        DrawValueRow(
            "Release date",
            snapshot.latestPublishedAt.empty() ? "Waiting" : snapshot.latestPublishedAt
        );
        DrawValueRow(
            "Last check",
            snapshot.lastCheckText.empty() ? "Waiting" : snapshot.lastCheckText
        );
        DrawValueRow("Status", UpdateStatusLabel(snapshot.status));
        DrawValueRow(
            "Summary",
            snapshot.latestSummary.empty() ? "Waiting" : snapshot.latestSummary
        );

        if (!snapshot.lastError.empty()) {
            DrawValueRow("Failure", snapshot.lastError);
        }

        ImGui::EndTable();
    }

    ImGui::BeginDisabled(snapshot.checkInProgress);
    if (ImGui::Button("Refresh update check")) {
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

// Converts update status into the short label used by Settings and Test UI.
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
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAPATH, "/system/etc/security/cacerts");
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
    UiState::MainTabIndex = std::clamp(UiState::MainTabIndex.load(), 0, 7);
    UiState::ThemeIndex =
        std::clamp(UiState::ThemeIndex.load(), 0, kAppearanceThemeCount - 1);
    UiState::FontIndex = std::clamp(UiState::FontIndex.load(), 0, 1);
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
        std::clamp(FeatureState::ShopRecommendTargetCount.load(), 1, 99);
    FeatureState::ArenaHeroStar = std::clamp(FeatureState::ArenaHeroStar.load(), 1, 3);
    FeatureState::ArenaGoldTarget =
        std::clamp(FeatureState::ArenaGoldTarget.load(), 0, 999999999);
    FeatureState::ArenaPrice = std::clamp(FeatureState::ArenaPrice.load(), 0, 99);
    FeatureState::ArenaSkipTargetRound =
        std::clamp(FeatureState::ArenaSkipTargetRound.load(), 1, 99);
    FeatureState::ArenaTimeScale = ClampArenaTimeScale(FeatureState::ArenaTimeScale.load());
    FeatureState::AutoPlayAiDifficulty =
        std::clamp(FeatureState::AutoPlayAiDifficulty.load(), 1, 10);
    FeatureState::AutoPlayMinReserveGold =
        std::clamp(FeatureState::AutoPlayMinReserveGold.load(), 0, 999999);
    FeatureState::AutoPlayTargetLevel =
        std::clamp(FeatureState::AutoPlayTargetLevel.load(), 1, 99);
    FeatureState::AutoPlayPressure =
        std::clamp(FeatureState::AutoPlayPressure.load(), 0, 100);
    FeatureState::AutoPlayStrategy =
        ClampAutoPlayStrategy(FeatureState::AutoPlayStrategy.load());

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);

        for (auto& item : FeatureState::ShopSelectedHeroes) {
            item.second.targetCount = std::clamp(item.second.targetCount, 1, 99);
        }
    }
}

// Resets visual settings back to safe default values.
void ResetVisualSettings() {
    UiState::ThemeIndex = kDefaultThemeIndex;
    UiState::FontIndex = AppearanceState::NotoCjkFont ? 1 : 0;
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
    uint64_t selfAccountId = IsIl2CppRuntimeReady() ? GetSelfAccountId() : 0;
    StopAutoPlayRuntime(selfAccountId);
    FeatureState::AutoPlayEnabled = false;
    FeatureState::AutoPlayAdaptive = true;
    FeatureState::AutoPlayUseBuiltInAI = false;
    FeatureState::AutoPlayUseShop = true;
    FeatureState::AutoPlayUseEconomy = true;
    FeatureState::AutoPlayUseCombat = true;
    FeatureState::AutoPlayUseArenaAssist = true;
    FeatureState::AutoPlayUseFormation = true;
    FeatureState::AutoPlayUseAuction = true;
    FeatureState::AutoPlayUseGoGoCards = true;
    FeatureState::AutoPlayAiDifficulty = 3;
    FeatureState::AutoPlayMinReserveGold = 20;
    FeatureState::AutoPlayTargetLevel = 9;
    FeatureState::AutoPlayPressure = 25;
    FeatureState::AutoPlayStrategy = AutoPlayStrategyBalanced;
    FeatureState::AutoPlayLearnedRounds = 0;
    FeatureState::AutoPlayStrategyChanges = 0;
    FeatureState::AutoPlayLastRound = 0;
    FeatureState::AutoPlayLastHp = -1;
    FeatureState::AutoPlayBuiltInAiRunning = false;
    FeatureState::AutoPlayLastAiStartAccountId = 0;
    FeatureState::LastAutoPlayAiStartAttempt = {};
    FeatureState::LastAutoPlayDeployAttempt = {};
    FeatureState::LastAutoPlayFormationAttempt = {};
    FeatureState::LastAutoPlayLevelAttempt = {};
    FeatureState::LastAutoPlayAuctionAttempt = {};
    FeatureState::AutoPlayFocusGroup = 0;
    FeatureState::AutoPlayTargetHeroId = 0;
    FeatureState::AutoPlayBestCardId = 0;
    FeatureState::AutoPlayBestAuctionIndex = -1;
    FeatureState::AutoPlayBestAuctionScore = 0;
    FeatureState::AutoPlayBoardSelfUnits = 0;
    FeatureState::AutoPlayBoardEnemyUnits = 0;
    FeatureState::AutoPlayBoardMoves = 0;
    FeatureState::AutoPlayLastMoveHeroId = 0;
    FeatureState::AutoPlayLastMoveGain = 0;
    FeatureState::AutoPlayLastMoveX = -1;
    FeatureState::AutoPlayLastMoveY = -1;
    FeatureState::AutoPlayOpponentCount = 0;
    FeatureState::AutoPlayContestedTargets = 0;
    FeatureState::AutoPlayStrongestOpponentFightValue = 0;
    FeatureState::AutoPlayCurrentOpponentId = 0;
    FeatureState::AutoPlayInterestTier = 0;
    FeatureState::AutoPlayInterestNextGold = 10;
    FeatureState::AutoPlayInterestReserveGold = 20;
    FeatureState::AutoPlaySpendBudget = 0;
    FeatureState::AutoPlayHoldInterest = false;
    FeatureState::AutoPlaySnapshotReady = false;
    FeatureState::AutoPlaySnapshotAccountId = 0;
    FeatureState::AutoPlaySnapshotRound = 0;
    FeatureState::AutoPlaySnapshotPhase = -1;
    FeatureState::AutoPlaySnapshotHp = -1;
    FeatureState::AutoPlaySnapshotCoin = -1;
    FeatureState::AutoPlaySnapshotLevel = -1;
    FeatureState::AutoPlaySnapshotCurrentPopulation = -1;
    FeatureState::AutoPlaySnapshotTotalPopulation = -1;
    FeatureState::AutoPlaySnapshotLineupWorth = -1;
    FeatureState::AutoPlaySnapshotFightValue = -1;
    FeatureState::AutoPlaySnapshotRecommendHeroId = 0;
    FeatureState::AutoPlaySnapshotStarUpHeroId = 0;
    FeatureState::AutoPlaySnapshotCurrentOpponentFightValue = -1;
    FeatureState::AutoPlaySnapshotFightSection = false;
    FeatureState::AutoPlaySnapshotMonsterRound = false;
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
    FeatureState::ShopRefresh = false;
    FeatureState::ShopStopRefreshAtFreeHero = false;
    FeatureState::ShopStopRefreshAtSelectedHero = false;
    FeatureState::ShopStopRefreshAtRecommendLineup = false;
    FeatureState::ShopKeepGold = false;
    FeatureState::ShopKeepGoldAt = 20;
    FeatureState::ShopRecommendTargetCount = 9;
    ClearShopHeroTargets();
    FeatureState::ArenaHeroStar = 1;
    FeatureState::ArenaItemEnhanced = false;
    FeatureState::ArenaGogoCardEnabled = false;
    FeatureState::ArenaGogoCardSelected1 = -1;
    FeatureState::ArenaGogoCardSelected2 = -1;
    FeatureState::ArenaForceActiveSynergy = false;
    FeatureState::ArenaForceLevel99 = false;
    FeatureState::ArenaOutsideMapPlacement = false;
    FeatureState::ArenaAllEnemyHpOne = false;
    FeatureState::ArenaPassiveGold = false;
    FeatureState::ArenaGoldTarget = 999999;
    FeatureState::ArenaFreeEconomy = false;
    FeatureState::ArenaUnlimitedHeroPool = false;
    FeatureState::ArenaNoShopLock = false;
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
                std::max(targetCount, 1)
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
    else if (key == "autoPlayEnabled") FeatureState::AutoPlayEnabled = ParseConfigBool(value, FeatureState::AutoPlayEnabled);
    else if (key == "autoPlayAdaptive") FeatureState::AutoPlayAdaptive = ParseConfigBool(value, FeatureState::AutoPlayAdaptive);
    else if (key == "autoPlayUseBuiltInAI") FeatureState::AutoPlayUseBuiltInAI = ParseConfigBool(value, FeatureState::AutoPlayUseBuiltInAI);
    else if (key == "autoPlayUseShop") FeatureState::AutoPlayUseShop = ParseConfigBool(value, FeatureState::AutoPlayUseShop);
    else if (key == "autoPlayUseEconomy") FeatureState::AutoPlayUseEconomy = ParseConfigBool(value, FeatureState::AutoPlayUseEconomy);
    else if (key == "autoPlayUseCombat") FeatureState::AutoPlayUseCombat = ParseConfigBool(value, FeatureState::AutoPlayUseCombat);
    else if (key == "autoPlayUseArenaAssist") FeatureState::AutoPlayUseArenaAssist = ParseConfigBool(value, FeatureState::AutoPlayUseArenaAssist);
    else if (key == "autoPlayUseFormation") FeatureState::AutoPlayUseFormation = ParseConfigBool(value, FeatureState::AutoPlayUseFormation);
    else if (key == "autoPlayUseAuction") FeatureState::AutoPlayUseAuction = ParseConfigBool(value, FeatureState::AutoPlayUseAuction);
    else if (key == "autoPlayUseGoGoCards") FeatureState::AutoPlayUseGoGoCards = ParseConfigBool(value, FeatureState::AutoPlayUseGoGoCards);
    else if (key == "autoPlayAiDifficulty") FeatureState::AutoPlayAiDifficulty = ParseConfigInt(value, FeatureState::AutoPlayAiDifficulty);
    else if (key == "autoPlayMinReserveGold") FeatureState::AutoPlayMinReserveGold = ParseConfigInt(value, FeatureState::AutoPlayMinReserveGold);
    else if (key == "autoPlayTargetLevel") FeatureState::AutoPlayTargetLevel = ParseConfigInt(value, FeatureState::AutoPlayTargetLevel);
    else if (key == "autoPlayPressure") FeatureState::AutoPlayPressure = ParseConfigInt(value, FeatureState::AutoPlayPressure);
    else if (key == "autoPlayStrategy") FeatureState::AutoPlayStrategy = ParseConfigInt(value, FeatureState::AutoPlayStrategy);
    else if (key == "combatInvisibleScout") FeatureState::CombatInvisibleScout = ParseConfigBool(value, FeatureState::CombatInvisibleScout);
    else if (key == "combatForceWin") FeatureState::CombatForceWin = ParseConfigBool(value, FeatureState::CombatForceWin);
    else if (key == "combatPreventHpLoss") FeatureState::CombatPreventHpLoss = ParseConfigBool(value, FeatureState::CombatPreventHpLoss);
    else if (key == "combatBoostAttackRatio") FeatureState::CombatBoostAttackRatio = ParseConfigBool(value, FeatureState::CombatBoostAttackRatio);
    else if (key == "combatCrippleEnemies") FeatureState::CombatCrippleEnemies = ParseConfigBool(value, FeatureState::CombatCrippleEnemies);
    else if (key == "combatAttackRatioPercent") FeatureState::CombatAttackRatioPercent = ParseConfigInt(value, FeatureState::CombatAttackRatioPercent);
    else if (key == "combatEnemyAttackRatioPercent") FeatureState::CombatEnemyAttackRatioPercent = ParseConfigInt(value, FeatureState::CombatEnemyAttackRatioPercent);
    else if (key == "combatFightValue") FeatureState::CombatFightValue = ParseConfigInt(value, FeatureState::CombatFightValue);
    else if (key == "shopBuyFreeHero") FeatureState::ShopBuyFreeHero = ParseConfigBool(value, FeatureState::ShopBuyFreeHero);
    else if (key == "shopBuySelectedHero") FeatureState::ShopBuySelectedHero = ParseConfigBool(value, FeatureState::ShopBuySelectedHero);
    else if (key == "shopBuyRecommendLineup") FeatureState::ShopBuyRecommendLineup = ParseConfigBool(value, FeatureState::ShopBuyRecommendLineup);
    else if (key == "shopRefresh") FeatureState::ShopRefresh = ParseConfigBool(value, FeatureState::ShopRefresh);
    else if (key == "shopStopRefreshAtFreeHero") FeatureState::ShopStopRefreshAtFreeHero = ParseConfigBool(value, FeatureState::ShopStopRefreshAtFreeHero);
    else if (key == "shopStopRefreshAtSelectedHero") FeatureState::ShopStopRefreshAtSelectedHero = ParseConfigBool(value, FeatureState::ShopStopRefreshAtSelectedHero);
    else if (key == "shopStopRefreshAtRecommendLineup") FeatureState::ShopStopRefreshAtRecommendLineup = ParseConfigBool(value, FeatureState::ShopStopRefreshAtRecommendLineup);
    else if (key == "shopKeepGold") FeatureState::ShopKeepGold = ParseConfigBool(value, FeatureState::ShopKeepGold);
    else if (key == "shopKeepGoldAt") FeatureState::ShopKeepGoldAt = ParseConfigInt(value, FeatureState::ShopKeepGoldAt);
    else if (key == "shopRecommendTargetCount") FeatureState::ShopRecommendTargetCount = ParseConfigInt(value, FeatureState::ShopRecommendTargetCount);
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
    else if (key == "arenaPassiveGold") FeatureState::ArenaPassiveGold = ParseConfigBool(value, FeatureState::ArenaPassiveGold);
    else if (key == "arenaGoldTarget") FeatureState::ArenaGoldTarget = ParseConfigInt(value, FeatureState::ArenaGoldTarget);
    else if (key == "arenaFreeEconomy") FeatureState::ArenaFreeEconomy = ParseConfigBool(value, FeatureState::ArenaFreeEconomy);
    else if (key == "arenaUnlimitedHeroPool") FeatureState::ArenaUnlimitedHeroPool = ParseConfigBool(value, FeatureState::ArenaUnlimitedHeroPool);
    else if (key == "arenaNoShopLock") FeatureState::ArenaNoShopLock = ParseConfigBool(value, FeatureState::ArenaNoShopLock);
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
    WriteConfigBool(file, "autoPlayEnabled", FeatureState::AutoPlayEnabled);
    WriteConfigBool(file, "autoPlayAdaptive", FeatureState::AutoPlayAdaptive);
    WriteConfigBool(file, "autoPlayUseBuiltInAI", FeatureState::AutoPlayUseBuiltInAI);
    WriteConfigBool(file, "autoPlayUseShop", FeatureState::AutoPlayUseShop);
    WriteConfigBool(file, "autoPlayUseEconomy", FeatureState::AutoPlayUseEconomy);
    WriteConfigBool(file, "autoPlayUseCombat", FeatureState::AutoPlayUseCombat);
    WriteConfigBool(file, "autoPlayUseArenaAssist", FeatureState::AutoPlayUseArenaAssist);
    WriteConfigBool(file, "autoPlayUseFormation", FeatureState::AutoPlayUseFormation);
    WriteConfigBool(file, "autoPlayUseAuction", FeatureState::AutoPlayUseAuction);
    WriteConfigBool(file, "autoPlayUseGoGoCards", FeatureState::AutoPlayUseGoGoCards);
    WriteConfigInt(file, "autoPlayAiDifficulty", FeatureState::AutoPlayAiDifficulty);
    WriteConfigInt(file, "autoPlayMinReserveGold", FeatureState::AutoPlayMinReserveGold);
    WriteConfigInt(file, "autoPlayTargetLevel", FeatureState::AutoPlayTargetLevel);
    WriteConfigInt(file, "autoPlayPressure", FeatureState::AutoPlayPressure);
    WriteConfigInt(file, "autoPlayStrategy", FeatureState::AutoPlayStrategy);
    WriteConfigBool(file, "combatInvisibleScout", FeatureState::CombatInvisibleScout);
    WriteConfigBool(file, "combatForceWin", FeatureState::CombatForceWin);
    WriteConfigBool(file, "combatPreventHpLoss", FeatureState::CombatPreventHpLoss);
    WriteConfigBool(file, "combatBoostAttackRatio", FeatureState::CombatBoostAttackRatio);
    WriteConfigBool(file, "combatCrippleEnemies", FeatureState::CombatCrippleEnemies);
    WriteConfigInt(file, "combatAttackRatioPercent", FeatureState::CombatAttackRatioPercent);
    WriteConfigInt(
        file,
        "combatEnemyAttackRatioPercent",
        FeatureState::CombatEnemyAttackRatioPercent
    );
    WriteConfigInt(file, "combatFightValue", FeatureState::CombatFightValue);
    WriteConfigBool(file, "shopBuyFreeHero", FeatureState::ShopBuyFreeHero);
    WriteConfigBool(file, "shopBuySelectedHero", FeatureState::ShopBuySelectedHero);
    WriteConfigBool(file, "shopBuyRecommendLineup", FeatureState::ShopBuyRecommendLineup);
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
    WriteConfigBool(file, "arenaPassiveGold", FeatureState::ArenaPassiveGold);
    WriteConfigInt(file, "arenaGoldTarget", FeatureState::ArenaGoldTarget);
    WriteConfigBool(file, "arenaFreeEconomy", FeatureState::ArenaFreeEconomy);
    WriteConfigBool(file, "arenaUnlimitedHeroPool", FeatureState::ArenaUnlimitedHeroPool);
    WriteConfigBool(file, "arenaNoShopLock", FeatureState::ArenaNoShopLock);
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

    return FormatBool(GetField<bool>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field int for readable overlay output.
std::string FormatFieldInt(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatInt(GetField<int>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field u int32 for readable overlay output.
std::string FormatFieldUInt32(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatUInt32(GetField<uint32_t>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field u int64 for readable overlay output.
std::string FormatFieldUInt64(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatUInt64(GetField<uint64_t>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field float for readable overlay output.
std::string FormatFieldFloat(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatFloat(GetField<float>(reinterpret_cast<Il2CppObject*>(instance), field));
}

// Formats field pointer for readable overlay output.
std::string FormatFieldPointer(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatPointer(GetField<void*>(reinterpret_cast<Il2CppObject*>(instance), field));
}

bool HasShopSelectBinding();
bool HasShopAutomationBindings();
bool HasShopRefreshBindings();
bool HasShopRecommendLineupBindings();
bool HasShopDiagnosticBindings();
bool HasCombatPowerBindings();
bool HasArenaHeroBindings();
bool HasArenaItemBindings();
bool HasArenaGogoCardBindings();
bool HasArenaGoldBindings();
bool HasArenaRoundSkipBindings();
bool HasArenaSpeedHackBindings();
bool HasBattleTestBindings();
bool HasAutoPlayBindings();
std::string GetBattlePlayerName(uint64_t accountId);

struct PlayerInfoRow {
    uint64_t accountId = 0;
    bool isSelf = false;
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

        uint64_t accountId = entry.key;
        uint64_t enemyId = Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
            nullptr,
            accountId
        );
        std::string playerName = getPlayerName(accountId);
        std::string enemyName = enemyId != 0 ? getPlayerName(enemyId) : "";

        UiCache::InfoPlayerRows.push_back({
            accountId,
            selfAccountId != 0 && accountId == selfAccountId,
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
    if (!ImGui::CollapsingHeader("Runtime Status", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        "%d heroes / %d items / %d cards",
        tableCounts.heroes,
        tableCounts.equips,
        tableCounts.cards
    );
    UpdateCheckSnapshot updateSnapshot = GetUpdateCheckSnapshot();

    if (ImGui::BeginTable(
        "##RuntimeStatusTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        ImGui::TableSetupColumn("Runtime");
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 170.0f);
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
        DrawStatusRow("Shop refresh panel", HasShopRefreshBindings());
        DrawStatusRow("Shop diagnostics", HasShopDiagnosticBindings());
        DrawStatusRow("Battle power", HasCombatPowerBindings());
        DrawStatusRow("Arena heroes", HasArenaHeroBindings());
        DrawStatusRow("Arena items", HasArenaItemBindings());
        DrawStatusRow("Arena GogoCards", HasArenaGogoCardBindings());
        DrawStatusRow("Arena gold", HasArenaGoldBindings());
        DrawStatusRow("Arena round skip", HasArenaRoundSkipBindings());
        DrawStatusRow("Arena speedhack", HasArenaSpeedHackBindings());
        DrawStatusRow("Auto-play controller", HasAutoPlayBindings());
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
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "%s", message);
}

// Draws the atomic checkbox overlay section without changing game state.
bool DrawAtomicCheckbox(const char* label, std::atomic<bool>& value) {
    bool current = value.load();

    if (!ImGui::Checkbox(label, &current)) {
        return false;
    }

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
    int current = value.load();

    if (!ImGui::Combo(label, &current, items, itemsCount)) {
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

    if (!ImGui::InputInt(label, &current, step, stepFast, flags)) {
        return false;
    }

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

    if (!ImGui::SliderFloat(label, &current, minValue, maxValue, format)) {
        return false;
    }

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

    if (!ImGui::InputFloat(label, &current, step, stepFast, format)) {
        return false;
    }

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

    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetShopItemData &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin &&
        HasShopSelectBinding() &&
        (!needsHeroCount || Originals::MCLogicBattleData_ILogic_HeroOwnCount) &&
        (!needsRecommendLineup || HasShopRecommendLineupBindings());
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

// Checks the auto play bindings condition before work proceeds.
bool HasAutoPlayBindings() {
    bool builtInAiReady =
        !FeatureState::AutoPlayUseBuiltInAI.load() ||
        (Originals::MCLogicBattleManager_StartAI &&
         Originals::MCLogicBattleManager_TryAutoDeploy);

    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetGameRound &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerLevel &&
        builtInAiReady &&
        Originals::MCLogicBattleManager_OnPlayerLvlUp &&
        Originals::MCLogicBattleManager_CalcCurrentFightValue &&
        Originals::MCLogicBattleManager_MoveHeroInBattleField &&
        Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr &&
        Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID &&
        Originals::MCLogicBattleData_ILOGIC_GetUpgradeCost &&
        Originals::MCLogicBattleData_ILOGIC_CanUpgrade &&
        HasArenaGoldBindings() &&
        HasCombatPowerBindings() &&
        Originals::MCLogicBattleData_ILOGIC_GetShopItemData &&
        Originals::MCLogicBattleData_ILogic_HeroOwnCount &&
        HasShopSelectBinding() &&
        HasShopRecommendLineupBindings();
}

// Draws the ggc info overlay section without changing game state.
void DrawGgcInfo() {
    ImGui::SeparatorText("GGC");

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

    ImGui::TableSetupColumn("Round", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Quality");
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
                GgcQualityName(row.quality),
                row.quality
            );
        }
    }

    ImGui::EndTable();
}

// Draws the info players table overlay section without changing game state.
void DrawInfoPlayersTable() {
    ImGui::SeparatorText("Players");

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

    ImGui::TableSetupColumn("Player");
    ImGui::TableSetupColumn("Current enemy");
    ImGui::TableHeadersRow();

    for (const PlayerInfoRow& row : UiCache::InfoPlayerRows) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (row.isSelf) {
            std::string playerDisplay = row.playerName.empty() ? "-" : row.playerName;
            playerDisplay += " (Self)";
            ImGui::TextUnformatted(playerDisplay.c_str());
        } else {
            ImGui::TextUnformatted(row.playerName.empty() ? "-" : row.playerName.c_str());
        }

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

// Draws the auto play tab overlay section without changing game state.
void DrawAutoPlayTab() {
    if (!HasAutoPlayBindings()) {
        DrawWaitingText("Waiting for auto-play controller bindings");
    }

    DrawAtomicCheckbox("Auto-play", FeatureState::AutoPlayEnabled);
    DrawAtomicCheckbox("Adaptive strategy", FeatureState::AutoPlayAdaptive);
    DrawAtomicCheckbox("Use built-in battle AI", FeatureState::AutoPlayUseBuiltInAI);
    DrawAtomicCheckbox("Manage shop", FeatureState::AutoPlayUseShop);
    DrawAtomicCheckbox("Manage economy and level", FeatureState::AutoPlayUseEconomy);
    DrawAtomicCheckbox("Manage combat power", FeatureState::AutoPlayUseCombat);
    DrawAtomicCheckbox("Use arena assists", FeatureState::AutoPlayUseArenaAssist);
    DrawAtomicCheckbox("Use smart formation", FeatureState::AutoPlayUseFormation);
    DrawAtomicCheckbox("Use auction scoring", FeatureState::AutoPlayUseAuction);
    DrawAtomicCheckbox("Use GogoCard scoring", FeatureState::AutoPlayUseGoGoCards);

    ImGui::SeparatorText("Policy");
    ImGui::SetNextItemWidth(120.0f);
    DrawAtomicInputInt("AI difficulty", FeatureState::AutoPlayAiDifficulty);
    FeatureState::AutoPlayAiDifficulty =
        std::clamp(FeatureState::AutoPlayAiDifficulty.load(), 1, 10);

    ImGui::SetNextItemWidth(120.0f);
    DrawAtomicInputInt("Minimum reserve gold", FeatureState::AutoPlayMinReserveGold);
    FeatureState::AutoPlayMinReserveGold =
        std::clamp(FeatureState::AutoPlayMinReserveGold.load(), 0, 999999);

    ImGui::SetNextItemWidth(120.0f);
    DrawAtomicInputInt("Target level", FeatureState::AutoPlayTargetLevel);
    FeatureState::AutoPlayTargetLevel =
        std::clamp(FeatureState::AutoPlayTargetLevel.load(), 1, 99);

    const char* strategies[] = {
        "Economy",
        "Balanced",
        "Aggressive"
    };

    ImGui::BeginDisabled(FeatureState::AutoPlayAdaptive.load());
    ImGui::SetNextItemWidth(180.0f);
    if (DrawAtomicCombo(
            "Manual strategy",
            FeatureState::AutoPlayStrategy,
            strategies,
            IM_ARRAYSIZE(strategies)
        )) {
        FeatureState::AutoPlayStrategy =
            ClampAutoPlayStrategy(FeatureState::AutoPlayStrategy.load());
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Runtime");
    bool telemetryReady = FeatureState::AutoPlaySnapshotReady.load();
    AutoPlaySnapshot snapshot = GetCachedAutoPlaySnapshot();
    int pressureValue = std::clamp(FeatureState::AutoPlayPressure.load(), 0, 100);
    float pressure = static_cast<float>(pressureValue) / 100.0f;
    AutoPlayGoldPlan goldPlan = GetCachedAutoPlayGoldPlan();

    ImGui::Text("Strategy: %s", AutoPlayStrategyName(FeatureState::AutoPlayStrategy.load()));
    ImGui::Text(
        "Learned rounds: %d  Strategy changes: %d",
        FeatureState::AutoPlayLearnedRounds.load(),
        FeatureState::AutoPlayStrategyChanges.load()
    );
    ImGui::ProgressBar(pressure, ImVec2(-1.0f, 0.0f));

    if (ImGui::BeginTable(
        "##AutoPlaySnapshotTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        ImGui::TableSetupColumn("Signal");
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();
        DrawValueRow("Round", telemetryReady && snapshot.round > 0 ? FormatInt(snapshot.round) : "Waiting");
        DrawValueRow("Phase", telemetryReady && snapshot.phase >= 0 ? FormatInt(snapshot.phase) : "Waiting");
        DrawValueRow("HP", telemetryReady && snapshot.hp >= 0 ? FormatInt(snapshot.hp) : "Waiting");
        DrawValueRow("Gold", telemetryReady && snapshot.coin >= 0 ? FormatInt(snapshot.coin) : "Waiting");
        DrawValueRow(
            "Interest tier",
            telemetryReady && snapshot.coin >= 0 ? FormatInt(goldPlan.interestTier) + "/5" : "Waiting"
        );
        DrawValueRow(
            "Next interest",
            telemetryReady && snapshot.coin >= 0 ? FormatInt(goldPlan.nextInterestGold) : "Waiting"
        );
        DrawValueRow(
            "Gold reserve",
            telemetryReady && snapshot.coin >= 0 ? FormatInt(goldPlan.reserveGold) : "Waiting"
        );
        DrawValueRow(
            "Spend budget",
            telemetryReady && snapshot.coin >= 0 ? FormatInt(goldPlan.spendBudget) : "Waiting"
        );
        DrawValueRow(
            "Banking interest",
            telemetryReady && snapshot.coin >= 0 ? FormatBool(goldPlan.holdForInterest) : "Waiting"
        );
        DrawValueRow("Level", telemetryReady && snapshot.level >= 0 ? FormatInt(snapshot.level) : "Waiting");
        DrawValueRow(
            "Population",
            telemetryReady && snapshot.currentPopulation >= 0 && snapshot.totalPopulation >= 0 ?
                FormatInt(snapshot.currentPopulation) + "/" + FormatInt(snapshot.totalPopulation) :
                "Waiting"
        );
        DrawValueRow(
            "Lineup worth",
            telemetryReady && snapshot.lineupWorth >= 0 ? FormatInt(snapshot.lineupWorth) : "Waiting"
        );
        DrawValueRow(
            "Fight value",
            telemetryReady && snapshot.fightValue >= 0 ? FormatInt(snapshot.fightValue) : "Waiting"
        );
        DrawValueRow("Recommendation", telemetryReady ? FormatHeroLabel(snapshot.recommendHeroId) : "Waiting");
        DrawValueRow("Star-up", telemetryReady ? FormatHeroLabel(snapshot.starUpHeroId) : "Waiting");
        DrawValueRow(
            "Current opponent",
            telemetryReady && snapshot.currentOpponentId != 0 ?
                FormatUInt64(snapshot.currentOpponentId) :
                "Waiting"
        );
        DrawValueRow(
            "Opponent fight value",
            telemetryReady && snapshot.currentOpponentFightValue >= 0 ?
                FormatInt(snapshot.currentOpponentFightValue) :
                "Waiting"
        );
        DrawValueRow(
            "Strongest opponent",
            telemetryReady && snapshot.strongestOpponentFightValue >= 0 ?
                FormatInt(snapshot.strongestOpponentFightValue) :
                "Waiting"
        );
        DrawValueRow(
            "Opponent count",
            telemetryReady ? FormatInt(FeatureState::AutoPlayOpponentCount.load()) : "Waiting"
        );
        DrawValueRow(
            "Contested targets",
            telemetryReady ? FormatInt(FeatureState::AutoPlayContestedTargets.load()) : "Waiting"
        );
        DrawValueRow("Focus synergy", FormatInt(FeatureState::AutoPlayFocusGroup.load()));
        DrawValueRow("Target hero", FormatHeroLabel(FeatureState::AutoPlayTargetHeroId.load()));
        DrawValueRow("Best GogoCard", FormatInt(FeatureState::AutoPlayBestCardId.load()));
        DrawValueRow("Auction index", FormatInt(FeatureState::AutoPlayBestAuctionIndex.load()));
        DrawValueRow("Auction score", FormatInt(FeatureState::AutoPlayBestAuctionScore.load()));
        DrawValueRow(
            "Board units",
            FormatInt(FeatureState::AutoPlayBoardSelfUnits.load()) +
                "/" +
                FormatInt(FeatureState::AutoPlayBoardEnemyUnits.load())
        );
        DrawValueRow("Board moves", FormatInt(FeatureState::AutoPlayBoardMoves.load()));
        DrawValueRow("Last moved hero", FormatHeroLabel(FeatureState::AutoPlayLastMoveHeroId.load()));
        DrawValueRow("Last move gain", FormatInt(FeatureState::AutoPlayLastMoveGain.load()));
        DrawValueRow(
            "Last move cell",
            FeatureState::AutoPlayLastMoveX.load() >= 0 &&
                    FeatureState::AutoPlayLastMoveY.load() >= 0 ?
                FormatInt(FeatureState::AutoPlayLastMoveX.load()) +
                    "," +
                    FormatInt(FeatureState::AutoPlayLastMoveY.load()) :
                "Waiting"
        );
        ImGui::EndTable();
    }
}

// Draws the arena battle power controls overlay section without changing game state.
void DrawArenaBattlePowerControls() {
    ImGui::SeparatorText("Battle Power");

    if (!HasCombatPowerBindings()) {
        DrawWaitingText("Waiting for battle power bindings");
    }

    DrawAtomicCheckbox("Force defend win", FeatureState::CombatForceWin);
    DrawAtomicCheckbox("Prevent self HP loss", FeatureState::CombatPreventHpLoss);
    DrawAtomicCheckbox("Boost self attack ratio", FeatureState::CombatBoostAttackRatio);
    ImGui::SetNextItemWidth(150.0f);
    DrawAtomicInputInt("Self attack ratio %", FeatureState::CombatAttackRatioPercent);
    FeatureState::CombatAttackRatioPercent =
        std::clamp(FeatureState::CombatAttackRatioPercent.load(), 100, 100000);
    ImGui::SetNextItemWidth(150.0f);
    DrawAtomicInputInt("Self fight value", FeatureState::CombatFightValue);
    FeatureState::CombatFightValue =
        std::clamp(FeatureState::CombatFightValue.load(), 0, 999999999);

    ImGui::Separator();
    DrawAtomicCheckbox("Cripple enemy boards", FeatureState::CombatCrippleEnemies);
    ImGui::SetNextItemWidth(150.0f);
    DrawAtomicInputInt("Enemy attack ratio %", FeatureState::CombatEnemyAttackRatioPercent);
    FeatureState::CombatEnemyAttackRatioPercent =
        std::clamp(FeatureState::CombatEnemyAttackRatioPercent.load(), 0, 100);
}

// Draws the appearance tab overlay section without changing game state.
void DrawAppearanceTab() {
    ImGui::SeparatorText("Theme");

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

    ImGui::SeparatorText("Font");

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
}

// Draws the settings tab overlay section without changing game state.
void DrawSettingsTab() {
    EnsureConfigPathInitialized();

    if (ImGui::BeginTabBar("##SettingsTabBar")) {
        if (ImGui::BeginTabItem("Config")) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##ConfigPath",
                "Configuration file path",
                &UiState::ConfigPath
            );

            if (ImGui::Button("Save configuration")) {
                SaveConfigToFile(UiState::ConfigPath);
            }

            ImGui::SameLine();
            if (ImGui::Button("Load configuration")) {
                if (LoadConfigFromFile(UiState::ConfigPath, true)) {
                    ApplyAppearance();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset visuals")) {
                ResetVisualSettings();
                ApplyAppearance();
                UiState::ConfigStatus = "Visual settings reset";
            }

            if (!UiState::ConfigStatus.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", UiState::ConfigStatus.c_str());
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Saved state includes visual settings, window and HUD settings, and Auto-Play, Combat, Shop, and Arena controls.");

            DrawUpdateSettingsSection();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Window")) {
            bool changed = false;

            changed |= DrawAtomicSliderFloat("Menu width", UiState::MenuWidth, 360.0f, 1600.0f, "%.0f");
            changed |= DrawAtomicSliderFloat("Menu height", UiState::MenuHeight, 280.0f, 1200.0f, "%.0f");
            changed |= DrawAtomicCheckbox("Use fixed menu position", UiState::UseFixedMenuPosition);

            ImGui::BeginDisabled(!UiState::UseFixedMenuPosition.load());
            changed |= DrawAtomicInputFloat("Menu position X", UiState::MenuPosX, 1.0f, 20.0f, "%.0f");
            changed |= DrawAtomicInputFloat("Menu position Y", UiState::MenuPosY, 1.0f, 20.0f, "%.0f");
            ImGui::EndDisabled();

            if (ImGui::Button("Capture current menu size")) {
                ImVec2 size = UiCache::MenuWindowSize;
                UiState::MenuWidth = size.x;
                UiState::MenuHeight = size.y;
                changed = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Capture current position")) {
                ImVec2 pos = UiCache::MenuWindowPos;
                UiState::MenuPosX = pos.x;
                UiState::MenuPosY = pos.y;
                UiState::UseFixedMenuPosition = true;
                changed = true;
            }

            ImGui::SeparatorText("Behavior");
            changed |= DrawAtomicCheckbox("Show next enemy HUD", UiState::ShowNextEnemyHud);
            changed |= DrawAtomicCheckbox("Move from title bar only", UiState::MoveFromTitleBarOnly);
            changed |= DrawAtomicCheckbox("Resize from edges", UiState::ResizeFromEdges);

            if (changed) {
                ApplyAppearance();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Style")) {
            bool changed = false;

            ImGui::SeparatorText("Typography");
            changed |= DrawAtomicSliderFloat("Font size scale", UiState::FontScale, 0.65f, 2.0f, "%.2fx");

            ImGui::SeparatorText("Window");
            changed |= DrawAtomicSliderFloat("Window opacity", UiState::WindowAlpha, 0.35f, 1.0f, "%.2f");
            changed |= DrawAtomicSliderFloat("Window border", UiState::WindowBorderSize, 0.0f, 4.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Frame border", UiState::FrameBorderSize, 0.0f, 4.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Scrollbar size", UiState::ScrollbarSize, 8.0f, 32.0f, "%.0f");

            ImGui::SeparatorText("Rounding");
            changed |= DrawAtomicSliderFloat("Window rounding", UiState::WindowRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Child rounding", UiState::ChildRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Frame rounding", UiState::FrameRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Popup rounding", UiState::PopupRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Scrollbar rounding", UiState::ScrollbarRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Grab rounding", UiState::GrabRounding, 0.0f, 20.0f, "%.1f");
            changed |= DrawAtomicSliderFloat("Tab rounding", UiState::TabRounding, 0.0f, 20.0f, "%.1f");

            ImGui::SeparatorText("Spacing");
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

        if (ImGui::BeginTabItem("State")) {
            if (ImGui::Button("Reset feature state")) {
                ResetFeatureSettings();
                UiState::ConfigStatus = "Feature state reset";
            }

            ImGui::SameLine();
            if (ImGui::Button("Clear shop hero targets")) {
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
    DrawStatusRow("Auto-play AI", Originals::MCLogicBattleManager_StartAI);
    DrawStatusRow("Auto deploy", Originals::MCLogicBattleManager_TryAutoDeploy);
    DrawStatusRow("Auto level up", Originals::MCLogicBattleManager_OnPlayerLvlUp);
    DrawStatusRow("Lineup scoring", Originals::MCLogicBattleManager_GetLineupWorth);
    DrawStatusRow("Smart formation", Originals::MCLogicBattleManager_MoveHeroInBattleField);
    DrawStatusRow("Auction scoring", Originals::MCLogicAuctionComp_Bid);
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

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound ?
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
        Originals::MCLogicBattleData_ILOGIC_GetGamePhase ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetGamePhase(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Remain time",
        Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Max remain time",
        Originals::MCLogicBattleData_ILOGIC_GetRoundMaxRemainTime ?
            FormatUInt64(Originals::MCLogicBattleData_ILOGIC_GetRoundMaxRemainTime(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Is fight section",
        Originals::MCLogicBattleData_ILOGIC_IsFightSection ?
            FormatBool(Originals::MCLogicBattleData_ILOGIC_IsFightSection(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Is result section",
        Originals::MCLogicBattleData_ILOGIC_IsFightResultSection ?
            FormatBool(Originals::MCLogicBattleData_ILOGIC_IsFightResultSection(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Is self fight over",
        Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver ?
            FormatBool(Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Inspect HP",
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP && targetAccountId ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetPlayerHP(nullptr, targetAccountId)) :
            "Waiting"
    );
    DrawValueRow(
        "Opponent HP",
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP && opponentAccountId ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetPlayerHP(nullptr, opponentAccountId)) :
            "Waiting"
    );
    DrawValueRow(
        "History fail flag",
        Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory &&
                targetAccountId &&
                round > 0 ?
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
    return accountId && reader ? FormatInt(reader(nullptr, accountId)) : "Waiting";
}

// Formats an account-specific unsigned integer reader for Test tab diagnostics.
std::string FormatAccountUInt32(
    uint64_t accountId,
    uint32_t (*reader)(void* instance, uint64_t accountId)
) {
    return accountId && reader ? FormatUInt32(reader(nullptr, accountId)) : "Waiting";
}

// Formats an account-specific boolean reader for Test tab diagnostics.
std::string FormatAccountBool(
    uint64_t accountId,
    bool (*reader)(void* instance, uint64_t accountId)
) {
    return accountId && reader ? FormatBool(reader(nullptr, accountId)) : "Waiting";
}

// Formats global int for readable overlay output.
std::string FormatGlobalInt(int (*reader)(void* instance)) {
    return reader ? FormatInt(reader(nullptr)) : "Waiting";
}

// Formats shop star level for readable overlay output.
std::string FormatShopStarLevel(uint64_t accountId) {
    if (!accountId || !Originals::MCLogicBattleData_ILOGIC_GetShopStarLv) {
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
        coin = Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, targetAccountId);
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
            FormatBool(Originals::MCLogicBattleData_ILOGIC_CanUpgrade(
                nullptr,
                targetAccountId,
                coin
            )) :
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
        Originals::MCLogicBattleData_ILOGIC_GetSelfCamp ?
            FormatInt(Originals::MCLogicBattleData_ILOGIC_GetSelfCamp(nullptr)) :
            "Waiting"
    );
    DrawValueRow(
        "Self population",
        Originals::MCLogicBattleData_ILOGIC_SelfCurPopulation &&
                Originals::MCLogicBattleData_ILOGIC_SelfTotalPopulation ?
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
        Originals::MCLogicBattleData_ILOGIC_GetHeroByStarUp ?
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

    if (battleManager && Originals::MCLogicBattleManager_GetAliveFighter) {
        Originals::MCLogicBattleManager_GetAliveFighter(
            battleManager,
            &campACount,
            &campBCount
        );
        hasAliveCounts = true;
    }

    void* currentOpponent = battleManager && Originals::MCLogicBattleManager_GetCurrentOpponent ?
        Originals::MCLogicBattleManager_GetCurrentOpponent(battleManager) :
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
        battleManager && Originals::MCLogicBattleManager_get_m_uAccountId ?
            FormatUInt64(Originals::MCLogicBattleManager_get_m_uAccountId(battleManager)) :
            "Waiting"
    );
    DrawValueRow(
        "Is host",
        battleManager && Originals::MCLogicBattleManager_get_IsHost ?
            FormatBool(Originals::MCLogicBattleManager_get_IsHost(battleManager)) :
            "Waiting"
    );
    DrawValueRow("hasBattle field", FormatFieldBool(battleManager, hasBattleField));
    DrawValueRow("fightOver field", FormatFieldBool(battleManager, fightOverField));
    DrawValueRow(
        "defendFailed getter",
        battleManager && Originals::MCLogicBattleManager_get_m_bDefendFaild ?
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
        battleManager && Originals::MCLogicBattleManager_HasAliveFighter ?
            FormatBool(Originals::MCLogicBattleManager_HasAliveFighter(battleManager, 1)) :
            "Waiting"
    );
    DrawValueRow(
        "has alive camp B",
        battleManager && Originals::MCLogicBattleManager_HasAliveFighter ?
            FormatBool(Originals::MCLogicBattleManager_HasAliveFighter(battleManager, 2)) :
            "Waiting"
    );

    ImGui::EndTable();
}

// Draws the test behavior rows overlay section without changing game state.
void DrawTestBehaviorRows(uint64_t targetAccountId) {
    void* behaviorApi = targetAccountId && Originals::MCBehaviorThreeApi_Get ?
        Originals::MCBehaviorThreeApi_Get(targetAccountId) :
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
        behaviorApi && Originals::MCBehaviorThreeApi_GetCurrentBattleRoundResult ?
            FormatInt(Originals::MCBehaviorThreeApi_GetCurrentBattleRoundResult(behaviorApi)) :
            "Waiting"
    );
    DrawValueRow(
        "Current phase type",
        behaviorApi && Originals::MCBehaviorThreeApi_GetCurrentPhaseType ?
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
        battleBridge && Originals::MCBattleBridge_IsSuperCrystalShopOpen ?
            FormatBool(Originals::MCBattleBridge_IsSuperCrystalShopOpen(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "GogoCard panel open",
        battleBridge && Originals::MCBattleBridge_IsGoGoCardPanelOpen ?
            FormatBool(Originals::MCBattleBridge_IsGoGoCardPanelOpen(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Keyboard enabled",
        battleBridge && Originals::MCBattleBridge_CheckEnableKeyBoard ?
            FormatBool(Originals::MCBattleBridge_CheckEnableKeyBoard(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Free memory",
        battleBridge && Originals::MCBattleBridge_GetFreeMemory ?
            FormatInt64(Originals::MCBattleBridge_GetFreeMemory(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Ping samples",
        battleBridge && Originals::MCBattleBridge_GetPingTimes ?
            FormatUInt32(Originals::MCBattleBridge_GetPingTimes(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "Ping stdev",
        battleBridge && Originals::MCBattleBridge_GetStdevPing ?
            FormatFloat(Originals::MCBattleBridge_GetStdevPing(battleBridge)) :
            "Waiting"
    );
    DrawValueRow(
        "FPS stdev",
        battleBridge && Originals::MCBattleBridge_GetStdevFps ?
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
        heroShopPanel && Originals::UIPanelBattleHeroShop_get_lastOperationTime ?
            FormatUInt32(Originals::UIPanelBattleHeroShop_get_lastOperationTime(heroShopPanel)) :
            "Waiting"
    );
    DrawValueRow(
        "Delay open",
        heroShopPanel && Originals::UIPanelBattleHeroShop_IsDelayOpen ?
            FormatBool(Originals::UIPanelBattleHeroShop_IsDelayOpen(heroShopPanel)) :
            "Waiting"
    );
    DrawValueRow(
        "Info after spectate",
        heroShopPanel && Originals::UIPanelBattleHeroShop_GetInfoAfterSpectate ?
            FormatBool(Originals::UIPanelBattleHeroShop_GetInfoAfterSpectate(heroShopPanel)) :
            "Waiting"
    );
    DrawValueRow(
        "Can operate",
        heroShopPanel && Originals::UIPanelBattleHeroShop_CanOperate ?
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
    std::vector<OpponentPredictionRow> CachedRows;
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
    }

    PredictionCache::HistorySelfAccountId = selfAccountId;
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

// Predicts the local opponent from the seven-round pattern learned from MCGG_Predictor.
OpponentCyclePrediction PredictCyclePatternOpponent(
    uint64_t selfAccountId,
    uint32_t currentRound
) {
    OpponentCyclePrediction prediction{};
    if (selfAccountId == 0 || currentRound == 0) {
        return prediction;
    }

    uint32_t effectiveRound = GetOpponentCycleEffectiveRound(currentRound);
    uint32_t cycleStartRound = GetOpponentCycleStartRound(currentRound);
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

// Refreshes per-player current-opponent observations used by predictions and HUD text.
void RefreshPredictionOpponentCache(
    uint64_t selfAccountId,
    void* selfManager,
    void* invasionManager,
    const std::vector<PredictionPlayer>& players
) {
    ResetOpponentPredictionHistoryIfNeeded(selfAccountId);
    PredictionCache::CurrentRoundOpponents.clear();

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound ?
        Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr) :
        0;

    auto addObservation = [invasionManager](uint64_t accountId, void* manager, bool alive) {
        if (accountId == 0) {
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

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound ?
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
        PredictionCache::CachedRowsReady = false;
        PredictionCache::CachedRowsSelfAccountId = selfAccountId;
        PredictionCache::LastRowsRefresh = {};
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        PredictionCache::CachedRows.clear();
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

    ImGui::TableSetupColumn("Player");
    ImGui::TableSetupColumn("Will fight", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Recent", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Current enemy");
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

// Draws the test all managers table overlay section without changing game state.
void DrawTestAllManagersTable() {
    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        DrawWaitingText("Waiting for battle manager list");
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
            Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                nullptr,
                selfAccountId
            ) :
            0;

    if (ImGui::Button("Retry test bindings")) {
        ResolveFeatureBindings();
        RefreshManagedReferences(true);
    }

    ImGui::SameLine();
    if (ImGui::Button("Use self") && selfAccountId != 0) {
        UiState::TestAccountId = FormatUInt64(selfAccountId);
    }

    ImGui::SameLine();
    if (ImGui::Button("Use opponent") && selfOpponentId != 0) {
        UiState::TestAccountId = FormatUInt64(selfOpponentId);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear account")) {
        UiState::TestAccountId.clear();
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint(
        "##TestAccountId",
        "Account ID to inspect (empty = self)",
        &UiState::TestAccountId
    );

    uint64_t targetAccountId = ParseAccountIdOrDefault(
        UiState::TestAccountId,
        defaultAccountId
    );
    uint64_t opponentAccountId =
        targetAccountId && Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ?
            Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                nullptr,
                targetAccountId
            ) :
            0;
    void* targetManager = GetBattleManagerByAccountId(targetAccountId);

    if (ImGui::BeginTabBar("##TestTabBar", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Predict")) {
            DrawOpponentPredictionTable(selfAccountId);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Bindings")) {
            DrawTestBindingRows();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Round")) {
            DrawTestRoundRows(selfAccountId, targetAccountId, opponentAccountId);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Player")) {
            DrawTestPlayerRows(targetAccountId);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Manager")) {
            DrawTestManagerRows(targetManager);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Bridge")) {
            DrawTestBridgeRows();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shop UI")) {
            DrawTestShopPanelRows();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Behavior")) {
            DrawTestBehaviorRows(targetAccountId);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Managers")) {
            DrawTestAllManagersTable();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

// Draws the shop tab overlay section without changing game state.
void DrawShopTab() {
    if (ImGui::BeginTabBar("##ShopTabBar")) {
        if (ImGui::BeginTabItem("Automation")) {
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
            ImGui::Separator();
            ImGui::SeparatorText("Recommendation Lineup");
            DrawAtomicCheckbox(
                "Auto-buy recommendation heroes",
                FeatureState::ShopBuyRecommendLineup
            );
            ImGui::SetNextItemWidth(120.0f);
            DrawAtomicInputInt(
                "Recommendation target count",
                FeatureState::ShopRecommendTargetCount
            );
            FeatureState::ShopRecommendTargetCount = GetRecommendLineupTargetCount();

            if (HasShopRecommendLineupBindings()) {
                int recommendHeroId = FeatureState::CachedRecommendLineupHeroId.load();
                ImGui::Text(
                    "Current recommendation: %s",
                    FormatHeroLabel(recommendHeroId).c_str()
                );
            } else {
                ImGui::TextUnformatted("Current recommendation: Waiting");
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

        if (ImGui::BeginTabItem("Hero Targets")) {
            DrawAtomicCheckbox("Show tracked heroes only", UiState::ShopShowSelectedOnly);

            if (ImGui::Button("Clear hero targets", ImVec2(-1.0f, 0.0f))) {
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
                "Showing %d / %d heroes",
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
                ImGui::TableSetupColumn("Hero");
                ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Target Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 80.0f);
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
                        state.targetCount = std::clamp(state.targetCount, 1, 99);

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Checkbox("##selected", &state.selected);
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
        if (ImGui::BeginTabItem("Heroes")) {
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
                "Showing %d / %d heroes",
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
                ImGui::TableSetupColumn("Hero");
                ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 90.0f);
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

                        if (ImGui::Button("Spawn", ImVec2(-1.0f, 0.0f))) {
                            GiveHero(hero.id, FeatureState::ArenaHeroStar.load());
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Items")) {
            if (!HasArenaItemBindings()) {
                DrawWaitingText("Waiting for arena item binding");
            }

            DrawAtomicCheckbox("Grant enhanced item", FeatureState::ArenaItemEnhanced);
            ImGui::Separator();

            std::vector<EquipTableEntry> equips = GetSortedEquips();
            int totalEquipCount = static_cast<int>(equips.size());
            ImGui::Text(
                "Showing %d / %d items",
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
                ImGui::TableSetupColumn("Item");
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 90.0f);
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

                        if (ImGui::Button("Grant", ImVec2(-1.0f, 0.0f))) {
                            GiveEquip(equip.id);
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("GogoCards")) {
            if (!HasArenaGogoCardBindings()) {
                DrawWaitingText("Waiting for GogoCard binding");
            }

            DrawAtomicCheckbox("Force selected GogoCards", FeatureState::ArenaGogoCardEnabled);
            ImGui::Text(
                "Card 1: %d  Card 2: %d",
                FeatureState::ArenaGogoCardSelected1.load(),
                FeatureState::ArenaGogoCardSelected2.load()
            );
            if (ImGui::Button("Clear card 1")) {
                FeatureState::ArenaGogoCardSelected1 = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear card 2")) {
                FeatureState::ArenaGogoCardSelected2 = -1;
            }
            ImGui::Separator();

            std::vector<CardTableEntry> cards = GetSortedCards();
            int totalCardCount = static_cast<int>(cards.size());
            ImGui::Text(
                "Showing %d / %d cards",
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
                ImGui::TableSetupColumn("Card");
                ImGui::TableSetupColumn("Card 1", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Card 2", ImGuiTableColumnFlags_WidthFixed, 90.0f);
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

                        if (ImGui::Button("Select##card1", ImVec2(-1.0f, 0.0f))) {
                            FeatureState::ArenaGogoCardSelected1 = card.id;
                        }

                        ImGui::TableSetColumnIndex(2);

                        if (ImGui::Button("Select##card2", ImVec2(-1.0f, 0.0f))) {
                            FeatureState::ArenaGogoCardSelected2 = card.id;
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Round")) {
            if (!HasArenaRoundSkipBindings()) {
                DrawWaitingText("Waiting for round skip bindings");
            }

            if (!HasArenaSpeedHackBindings()) {
                DrawWaitingText("Waiting for timeScale binding");
            }

            uint32_t currentRound = FeatureState::CachedGameRound.load();

            ImGui::Text(
                "Current round: %s",
                currentRound > 0 ? FormatUInt32(currentRound).c_str() : "Waiting"
            );

            DrawAtomicCheckbox("Skip Round", FeatureState::ArenaSkipRound);
            ImGui::SetNextItemWidth(120.0f);
            DrawAtomicInputInt("Target round", FeatureState::ArenaSkipTargetRound);
            FeatureState::ArenaSkipTargetRound =
                std::clamp(FeatureState::ArenaSkipTargetRound.load(), 1, 99);

            if (ImGui::Button("Apply Skip Round now", ImVec2(-1.0f, 0.0f))) {
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

            if (ImGui::Button("Reset time scale", ImVec2(-1.0f, 0.0f))) {
                FeatureState::ArenaSpeedHack = false;
                FeatureState::ArenaTimeScale = 1.0f;
                ApplyArenaSpeedHack(0);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Battle Power")) {
            DrawArenaBattlePowerControls();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Other")) {
            if (!Originals::MCBondUtil_CheckRelationActive_Config ||
                !Originals::MCBondUtil_CheckRelationActive_Special) {
                DrawWaitingText("Waiting for synergy hooks");
            }

            if (!HasArenaGoldBindings()) {
                DrawWaitingText("Waiting for player data bindings");
            }

            DrawAtomicCheckbox("Force all synergies active", FeatureState::ArenaForceActiveSynergy);
            DrawAtomicCheckbox("Force level and population 99", FeatureState::ArenaForceLevel99);
            DrawAtomicCheckbox("Allow outside-map placement", FeatureState::ArenaOutsideMapPlacement);
            DrawAtomicCheckbox("Set all enemy HP to 1", FeatureState::ArenaAllEnemyHpOne);
            DrawAtomicCheckbox("Maintain target gold", FeatureState::ArenaPassiveGold);
            ImGui::SetNextItemWidth(150.0f);
            DrawAtomicInputInt("Target gold", FeatureState::ArenaGoldTarget);
            FeatureState::ArenaGoldTarget =
                std::clamp(FeatureState::ArenaGoldTarget.load(), 0, 999999999);
            DrawAtomicCheckbox("Free shop and upgrades", FeatureState::ArenaFreeEconomy);
            DrawAtomicCheckbox("Unlimited hero pool", FeatureState::ArenaUnlimitedHeroPool);
            DrawAtomicCheckbox("Disable shop lock", FeatureState::ArenaNoShopLock);
            ImGui::Separator();
            ImGui::SetNextItemWidth(120.0f);
            DrawAtomicInputInt("Hero cost filter", FeatureState::ArenaPrice);
            FeatureState::ArenaPrice = std::clamp(FeatureState::ArenaPrice.load(), 0, 99);

            if (ImGui::Button("Spawn all heroes with selected cost", ImVec2(-1.0f, 0.0f))) {
                int arenaPrice = FeatureState::ArenaPrice.load();
                int arenaHeroStar = FeatureState::ArenaHeroStar.load();

                for (const HeroTableEntry& hero : GetSortedHeroes(true)) {
                    if (hero.quality == arenaPrice) {
                        GiveHero(hero.id, arenaHeroStar);
                    }
                }
            }

            if (ImGui::Button("Grant 999999 gold", ImVec2(-1.0f, 0.0f))) {
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

    if (ImGui::Button("Prev", ImVec2(92.0f, 0.0f))) {
        current = (current + tabCount - 1) % tabCount;
        UiState::MainTabIndex = current;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(tabs[current].label);
    ImGui::SameLine();

    if (ImGui::Button("Next", ImVec2(92.0f, 0.0f))) {
        current = (current + 1) % tabCount;
        UiState::MainTabIndex = current;
    }

    ImGui::PopStyleVar();
    ImGui::Spacing();
}

// Draws the menu tab button overlay section without changing game state.
void DrawMenuTabButton(const char* label, int index) {
    bool selected = UiState::MainTabIndex.load() == index;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));

    if (ImGui::Selectable(label, selected, 0, ImVec2(-1.0f, 34.0f))) {
        UiState::MainTabIndex = index;
    }

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
        {"Auto-Play", DrawAutoPlayTab},
        {"Shop", DrawShopTab},
        {"Arena", DrawArenaTab},
        {"Appearance", DrawAppearanceTab},
        {"Settings", DrawSettingsTab},
        {"Test", DrawTestTab}
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

            if (ImGui::BeginTabItem(tabs[i].label, nullptr, itemFlags)) {
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
    // Hook wrapper for show spectator comp set spectate, applying feature overrides only when enabled.
    void MCShowSpectatorComp_SetSpectate(void* instance, uint64_t accountId) {
        if (FeatureState::CombatInvisibleScout) {
            return;
        }

        if (Originals::MCShowSpectatorComp_SetSpectate) {
            Originals::MCShowSpectatorComp_SetSpectate(instance, accountId);
        }
    }

    // Forces free-buy checks to succeed when free economy assistance is enabled.
    bool MCLogicBattleData_ILOGIC_IsCurrFreeBuy(
        void* instance,
        uint64_t accountId,
        int slot,
        bool* needFx
    ) {
        if (FeatureState::ArenaFreeEconomy && IsSelfAccount(accountId)) {
            if (needFx) {
                *needFx = false;
            }
            return true;
        }

        return Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy ?
            Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy(instance, accountId, slot, needFx) :
            false;
    }

    // Hook wrapper for logic battle data ilogic get refresh cost, applying feature overrides only when enabled.
    int MCLogicBattleData_ILOGIC_GetRefreshCost(void* instance, uint64_t accountId) {
        if (FeatureState::ArenaFreeEconomy && IsSelfAccount(accountId)) {
            return 0;
        }

        return Originals::MCLogicBattleData_ILOGIC_GetRefreshCost ?
            Originals::MCLogicBattleData_ILOGIC_GetRefreshCost(instance, accountId) :
            0;
    }

    // Hook wrapper for logic battle data ilogic is refresh free, applying feature overrides only when enabled.
    bool MCLogicBattleData_ILOGIC_IsRefreshFree(void* instance, uint64_t accountId) {
        if (FeatureState::ArenaFreeEconomy && IsSelfAccount(accountId)) {
            return true;
        }

        return Originals::MCLogicBattleData_ILOGIC_IsRefreshFree ?
            Originals::MCLogicBattleData_ILOGIC_IsRefreshFree(instance, accountId) :
            false;
    }

    // Hook wrapper for logic battle data i logic hero count in pool, applying feature overrides only when enabled.
    int MCLogicBattleData_ILogic_HeroCountInPool(void* instance, int heroId) {
        if (FeatureState::ArenaUnlimitedHeroPool) {
            return 99;
        }

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

    // Allows upgrade checks to pass when free economy assistance is active.
    bool MCLogicBattleData_ILOGIC_CanUpgrade(
        void* instance,
        uint64_t accountId,
        int coin
    ) {
        if (FeatureState::ArenaFreeEconomy && IsSelfAccount(accountId)) {
            return true;
        }

        return Originals::MCLogicBattleData_ILOGIC_CanUpgrade ?
            Originals::MCLogicBattleData_ILOGIC_CanUpgrade(instance, accountId, coin) :
            false;
    }

    // Hook wrapper for logic battle data ilogic get shop is forbid, applying feature overrides only when enabled.
    bool MCLogicBattleData_ILOGIC_GetShopIsForbid(void* instance, uint64_t accountId) {
        if ((FeatureState::ArenaNoShopLock || FeatureState::ArenaFreeEconomy) &&
            IsSelfAccount(accountId)) {
            return false;
        }

        return Originals::MCLogicBattleData_ILOGIC_GetShopIsForbid ?
            Originals::MCLogicBattleData_ILOGIC_GetShopIsForbid(instance, accountId) :
            false;
    }

    // Hook wrapper for logic battle data ilogic get upgrade cost, applying feature overrides only when enabled.
    int MCLogicBattleData_ILOGIC_GetUpgradeCost(void* instance, uint64_t accountId) {
        if (FeatureState::ArenaFreeEconomy && IsSelfAccount(accountId)) {
            return 0;
        }

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
