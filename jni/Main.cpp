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

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <mutex>
#include <atomic>

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
#include "Il2CppVersions/api/2019.4.22f1.h"
#undef DO_API

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

// Cached table rows used by menu lists and automation.
struct HeroTableEntry {
    int id = 0;
    std::string name;
    int quality = 0;
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
std::unordered_map<std::string, FieldInfo*> FieldCache;

namespace RuntimeConfig {
    constexpr int BindingRetryMs = 2000;
    constexpr int ReferenceRefreshMs = 100;
    constexpr int TableRetryMs = 2000;
    constexpr int ArenaTickMs = 100;
    constexpr int ShopTickMs = 100;
    constexpr int LocalUiTickMs = 250;
    constexpr int ShopActionCooldownMs = 350;
    constexpr int ShopRepeatBuyCooldownMs = 1500;
    constexpr int ShopRefreshCooldownMs = 650;
    constexpr int ShopWorthCheckMs = 500;
    constexpr int RecommendLineupCheckMs = 500;
    constexpr int MaxShopTargetChecks = 256;
    constexpr int MaxManagedDictionaryEntries = 8192;
    constexpr int MaxManagedListItems = 2048;
    constexpr int MaxManagedStringChars = 4096;
}

namespace RuntimeMutex {
    std::mutex CacheMutex;
    std::mutex FeatureMutex;
    std::recursive_mutex UiMutex;
}

namespace RuntimeState {
    std::atomic<bool> Il2CppReady{false};
    std::atomic<bool> BindingRetryRequested{false};
}

// Feature toggles, cached managed references, and throttled runtime state.
namespace FeatureState {
    std::atomic<bool> CombatInvisibleScout{false};
    std::atomic<bool> CombatHideAllBloodBars{false};
    std::atomic<bool> CombatHideJoystick{false};
    std::atomic<bool> CombatHideSocialDragArea{false};
    std::atomic<bool> CombatBloodBarsHiddenApplied{false};
    std::atomic<bool> CombatJoystickHiddenApplied{false};
    std::atomic<bool> CombatSocialDragAreaHiddenApplied{false};

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
    std::atomic<bool> AutomationOpenShopDuringPrepare{false};
    std::atomic<bool> AutomationCloseShopDuringFight{false};
    std::atomic<bool> AutomationHideShopEntry{false};
    std::atomic<bool> AutomationKeepChatClosed{false};
    std::atomic<bool> AutomationShopEntryHiddenApplied{false};

    std::atomic<int> ArenaHeroStar{1};
    std::atomic<bool> ArenaItemEnhanced{false};
    std::atomic<bool> ArenaGogoCardEnabled{false};
    std::atomic<int> ArenaGogoCardSelected1{-1};
    std::atomic<int> ArenaGogoCardSelected2{-1};
    std::atomic<bool> ArenaForceActiveSynergy{false};
    std::atomic<bool> ArenaForceLevel99{false};
    std::atomic<bool> ArenaOutsideMapPlacement{false};
    std::atomic<bool> ArenaAllEnemyHpOne{false};
    std::atomic<int> ArenaPrice{5};

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
    std::chrono::steady_clock::time_point LastLocalUiTick{};
    std::chrono::steady_clock::time_point LastTableLoadAttempt{};
    std::chrono::steady_clock::time_point LastShopAction{};
    std::chrono::steady_clock::time_point LastShopBuyAttempt{};
    std::chrono::steady_clock::time_point LastShopRefreshAttempt{};
    std::chrono::steady_clock::time_point LastShopWorthCheck{};
    std::chrono::steady_clock::time_point LastRecommendLineupCheck{};
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

namespace AppearanceState {
    ImFont* DefaultFont = nullptr;
    ImFont* NotoCjkFont = nullptr;
    int AppliedThemeIndex = -1;
    int AppliedFontIndex = -1;
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

    void* (*MCComp_GetGamer)(uint64_t accountId);
    void* (*MCComp_GetGoGoCardComp)(uint64_t accountId);

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
    bool (*MCLogicBattleManager_get_m_bDefendFaild)(void* instance);
    bool (*MCLogicBattleManager_get_IsHost)(void* instance);
    uint64_t (*MCLogicBattleManager_get_m_uAccountId)(void* instance);
    void* (*MCLogicBattleManager_GetCurrentOpponent)(void* instance);
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
    bool (*MCBattleBridge_IsHeroInRecommendLineup)(void* instance, int heroId);
    void (*MCBattleBridge_OpenShop)(void* instance, bool open);
    void (*MCBattleBridge_CloseChatUI)(void* instance);
    bool (*MCBattleBridge_CheckEnableKeyBoard)(void* instance);
    void (*MCBattleBridge_SetJoyStickVisible)(void* instance, bool visible);
    void (*MCBattleBridge_SetSocialDragAreaVisible)(void* instance, bool visible);
    void (*MCBattleBridge_SetJoyStickUIShow)(void* instance, bool show);
    void (*MCBattleBridge_SetShopEnterActive)(void* instance, bool active);
    void (*MCBattleBridge_SetAllBloodVisible)(void* instance, bool visible);
    void (*MCChessPlayerData_UpdateCoin)(void* instance, int addValue, int changeType);

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
        if (cached != FieldCache.end() && cached->second) {
            return cached->second;
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
            return field;
        }
    }

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);
        FieldCache[cacheKey] = nullptr;
    }
    return nullptr;
}

// Reads an instance field by metadata into a caller-provided buffer.
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

// Reads a typed instance field by metadata.
template <typename T>
T GetField(Il2CppObject* instance, FieldInfo* field) {
    T value{};
    GetFieldRaw(instance, field, &value);
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
    GetFieldRaw(instance, ns, className, fieldName, &value);
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

// Writes a typed instance field by metadata.
template <typename T>
bool SetField(Il2CppObject* instance, FieldInfo* field, const T& value) {
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
    return SetFieldRaw(instance, ns, className, fieldName, &value);
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
        if (cached != MultiMethodCache.end() && !cached->second.empty()) {
            return cached->second;
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
    std::lock_guard<std::mutex> lock(RuntimeMutex::CacheMutex);

    if (!original) {
        if (DobbyHook(
            method,
            replacement,
            reinterpret_cast<void**>(&hookedOriginal)
        ) == RT_SUCCESS) {
            original = hookedOriginal;
        }
    }

    return original != nullptr;
}

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

bool CooldownElapsed(
    const std::chrono::steady_clock::time_point& lastRun,
    int intervalMs,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
) {
    return lastRun.time_since_epoch().count() == 0 ||
        now - lastRun >= std::chrono::milliseconds(intervalMs);
}

bool HasIl2CppDomainApi() {
    return il2cpp_domain_get && il2cpp_thread_attach;
}

bool HasIl2CppAssemblyScanApi() {
    return il2cpp_domain_get &&
        il2cpp_domain_get_assemblies &&
        il2cpp_assembly_get_image &&
        il2cpp_class_from_name;
}

bool HasIl2CppMethodScanApi() {
    return HasIl2CppAssemblyScanApi() &&
        il2cpp_class_get_methods &&
        il2cpp_method_get_name &&
        il2cpp_method_get_param_count &&
        il2cpp_method_get_param &&
        il2cpp_class_from_type &&
        il2cpp_class_get_name;
}

bool HasIl2CppFieldScanApi() {
    return HasIl2CppAssemblyScanApi() &&
        (il2cpp_class_get_field_from_name ||
         (il2cpp_class_get_fields && il2cpp_field_get_name));
}

bool IsIl2CppRuntimeReady() {
    if (!RuntimeState::Il2CppReady || !HasIl2CppDomainApi()) {
        return false;
    }

    return il2cpp_domain_get() != nullptr;
}

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
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory,
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
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsCurrFreeBuy,
        "",
        "MCLogicBattleData",
        "ILOGIC_IsCurrFreeBuy",
        {"UInt64", "Int32", "Boolean"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_GetRefreshCost,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetRefreshCost",
        {"UInt64"}
    );
    ResolveOriginal(
        Originals::MCLogicBattleData_ILOGIC_IsRefreshFree,
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
    ResolveOriginal(
        Originals::MCLogicBattleData_ILogic_HeroCountInPool,
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
        Originals::MCLogicBattleManager_get_m_bDefendFaild,
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
        Originals::MCBattleBridge_IsHeroInRecommendLineup,
        "",
        "MCBattleBridge",
        "IsHeroInRecommendLineup",
        {"Int32"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_OpenShop,
        "",
        "MCBattleBridge",
        "OpenShop",
        {"Boolean"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_CloseChatUI,
        "",
        "MCBattleBridge",
        "CloseChatUI",
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
        Originals::MCBattleBridge_SetJoyStickVisible,
        "",
        "MCBattleBridge",
        "SetJoyStickVisible",
        {"Boolean"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_SetSocialDragAreaVisible,
        "",
        "MCBattleBridge",
        "SetSocialDragAreaVisible",
        {"Boolean"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_SetJoyStickUIShow,
        "",
        "MCBattleBridge",
        "SetJoyStickUIShow",
        {"Boolean"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_SetShopEnterActive,
        "",
        "MCBattleBridge",
        "SetShopEnterActive",
        {"Boolean"}
    );
    ResolveOriginal(
        Originals::MCBattleBridge_SetAllBloodVisible,
        "",
        "MCBattleBridge",
        "SetAllBloodVisible",
        {"Boolean"}
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

bool IsForbidHeroName(const std::string& name) {
    return name.empty() ||
        name == "Dijiang" ||
        name == "Johnny" ||
        name == "Bot" ||
        name == "Physical ATK" ||
        name == "Magic ATK";
}

uint64_t GetSelfAccountId() {
    static FieldInfo* selfAccountIdField = nullptr;

    if (!selfAccountIdField) {
        selfAccountIdField = GetFieldInfoFromName("", "MCLogicBattleData", "m_SelfAccID");
    }

    return GetStaticField<uint64_t>(selfAccountIdField);
}

void* GetSelfLogicBattleManager() {
    static FieldInfo* selfBattleManagerField = nullptr;

    if (!selfBattleManagerField) {
        selfBattleManagerField =
            GetFieldInfoFromName("", "MCLogicBattleData", "m_SelfLogicBattleManager");
    }

    return GetStaticField<void*>(selfBattleManagerField);
}

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

void* GetLogicManagerFromBattleManager(void* battleManager) {
    static FieldInfo* logicManagerField = nullptr;

    if (!logicManagerField) {
        logicManagerField = GetFieldInfoFromName("", "MCLogicBattleManager", "m_LogicManager");
    }

    return battleManager && logicManagerField ?
        GetField<void*>(reinterpret_cast<Il2CppObject*>(battleManager), logicManagerField) :
        nullptr;
}

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

uint64_t GetBattleManagerAccountId(void* battleManager) {
    if (!battleManager) {
        return 0;
    }

    if (Originals::MCLogicBattleManager_get_m_uAccountId) {
        return Originals::MCLogicBattleManager_get_m_uAccountId(battleManager);
    }

    return 0;
}

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

uint64_t GetCurrentOpponentFromManager(void* battleManager) {
    void* currentOpponent = battleManager && Originals::MCLogicBattleManager_GetCurrentOpponent ?
        Originals::MCLogicBattleManager_GetCurrentOpponent(battleManager) :
        nullptr;

    uint64_t accountId = GetBattleManagerAccountId(currentOpponent);
    if (accountId != 0) {
        return accountId;
    }

    return GetMirrorOriginAccountId(currentOpponent);
}

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

bool IsRealPlayerPairingMode(void* invasionManager) {
    if (Originals::MCLogicBattleData_ILOGIC_IsRealPlayerMode) {
        return Originals::MCLogicBattleData_ILOGIC_IsRealPlayerMode(nullptr);
    }

    if (invasionManager && Originals::LogicInvasionMgr_IsRealPlayerMode) {
        return Originals::LogicInvasionMgr_IsRealPlayerMode(invasionManager);
    }

    return true;
}

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

TableCacheCounts GetTableCacheCounts() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    return {
        static_cast<int>(FeatureState::Heroes.size()),
        static_cast<int>(FeatureState::Equips.size()),
        static_cast<int>(FeatureState::Cards.size())
    };
}

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

std::unordered_map<int, HeroAutomationState> GetShopHeroTargetsSnapshot() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    return FeatureState::ShopSelectedHeroes;
}

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

void SetShopHeroTarget(int heroId, const HeroAutomationState& state) {
    if (heroId <= 0 || heroId > 10000000) {
        return;
    }

    HeroAutomationState clampedState = state;
    clampedState.targetCount = std::clamp(clampedState.targetCount, 1, 99);

    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    FeatureState::ShopSelectedHeroes[heroId] = clampedState;
}

void DeselectShopHeroTargets() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);

    for (auto& item : FeatureState::ShopSelectedHeroes) {
        item.second.selected = false;
    }
}

void ClearShopHeroTargets() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    FeatureState::ShopSelectedHeroes.clear();
}

int GetTrackedShopHeroTargetCount() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    return static_cast<int>(FeatureState::ShopSelectedHeroes.size());
}

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
    FeatureState::CachedShopHasWorthwhileTarget = false;
    FeatureState::CachedRecommendLineupHeroId = 0;
    FeatureState::LastShopBuyAccountId = 0;
    FeatureState::LastShopBuySlot = -1;
    FeatureState::LastShopBuyHeroId = 0;
    FeatureState::LastShopBuyPrice = 0;
    FeatureState::LastShopBuyOwnCount = -1;
    FeatureState::LastShopBuyWasFree = false;
}

void ClearTableDataCache() {
    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);
    ClearTableDataCacheUnlocked();
}

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

void RefreshTableDataForMatch(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady()) {
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

            if (!idField) {
                idField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_ID");
            }

            if (!nameField) {
                nameField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_mName");
            }

            if (!qualityField) {
                qualityField = GetFieldInfoFromName("", "CData_MCHero_Element", "m_Quality");
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
                localHeroes[heroId] = {heroId, heroName, quality, true};
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

bool IsPlausibleHeroId(int heroId) {
    return heroId > 0 && heroId <= 10000000;
}

bool IsKnownHeroIdOrTablePending(int heroId) {
    if (!IsPlausibleHeroId(heroId)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);

    return !FeatureState::TableDataLoaded.load() ||
        FeatureState::Heroes.empty() ||
        FeatureState::Heroes.find(heroId) != FeatureState::Heroes.end();
}

int GetRecommendLineupTargetCount() {
    return std::clamp(FeatureState::ShopRecommendTargetCount.load(), 1, 99);
}

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

bool SelectShopSlot(int slot) {
    if (slot < 0 || slot >= 5) {
        return false;
    }

    if (FeatureState::HeroShopItemList &&
        Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero) {
        Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero(
            FeatureState::HeroShopItemList,
            static_cast<uint8_t>(slot)
        );
        return true;
    }

    if (FeatureState::HeroShopPanel && Originals::UIPanelBattleHeroShop_KeyBoardShopSelect) {
        Originals::UIPanelBattleHeroShop_KeyBoardShopSelect(FeatureState::HeroShopPanel, slot);
        return true;
    }

    if (FeatureState::HeroShopPanel && Originals::UIPanelBattleHeroShop_BuyHero) {
        Originals::UIPanelBattleHeroShop_BuyHero(
            FeatureState::HeroShopPanel,
            static_cast<uint8_t>(slot),
            false
        );
        return true;
    }

    return false;
}

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

void MarkShopRefreshAttempt(std::chrono::steady_clock::time_point now) {
    FeatureState::LastShopAction = now;
    FeatureState::LastShopRefreshAttempt = now;
}

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

void ApplyArenaState(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady() || selfAccountId == 0) {
        return;
    }

    if ((FeatureState::ArenaForceLevel99 || FeatureState::ArenaAllEnemyHpOne) &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerData) {
        static FieldInfo* populationField = nullptr;
        static FieldInfo* hpField = nullptr;

        if (!populationField) {
            populationField =
                GetFieldInfoFromName("", "MCChessPlayerData", "m_iTotallPopulation");
        }

        if (!hpField) {
            hpField = GetFieldInfoFromName("", "MCChessPlayerData", "m_iCurrentHP");
        }

        if (FeatureState::ArenaForceLevel99) {
            void* selfPlayerData =
                Originals::MCLogicBattleData_ILOGIC_GetPlayerData(nullptr, selfAccountId);

            if (selfPlayerData) {
                SetField(
                    reinterpret_cast<Il2CppObject*>(selfPlayerData),
                    populationField,
                    99
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
        !FeatureState::HeroShopPanel ||
        !Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop) {
        return;
    }

    if (!HasWorthwhileShopTarget(selfAccountId, now) ||
        !CanAttemptShopRefresh(now)) {
        return;
    }

    int refreshCost = Originals::MCLogicBattleData_ILOGIC_GetRefreshCost ?
        Originals::MCLogicBattleData_ILOGIC_GetRefreshCost(nullptr, selfAccountId) :
        0;
    bool isFreeRefresh = Originals::MCLogicBattleData_ILOGIC_IsRefreshFree &&
        Originals::MCLogicBattleData_ILOGIC_IsRefreshFree(nullptr, selfAccountId);
    int coin = getCoin();
    bool canRefresh =
        isFreeRefresh ||
        (coin >= refreshCost &&
         (!FeatureState::ShopKeepGold ||
          coin - refreshCost >= FeatureState::ShopKeepGoldAt));

    if (canRefresh) {
        Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop(FeatureState::HeroShopPanel);
        MarkShopRefreshAttempt(now);
    }
}

bool HasAnyLocalUiFeatureState() {
    return FeatureState::CombatHideAllBloodBars ||
        FeatureState::CombatHideJoystick ||
        FeatureState::CombatHideSocialDragArea ||
        FeatureState::CombatBloodBarsHiddenApplied ||
        FeatureState::CombatJoystickHiddenApplied ||
        FeatureState::CombatSocialDragAreaHiddenApplied ||
        FeatureState::AutomationOpenShopDuringPrepare ||
        FeatureState::AutomationCloseShopDuringFight ||
        FeatureState::AutomationHideShopEntry ||
        FeatureState::AutomationKeepChatClosed ||
        FeatureState::AutomationShopEntryHiddenApplied;
}

void ApplyLocalUiFeatures(uint64_t selfAccountId) {
    if (!IsIl2CppRuntimeReady() || !HasAnyLocalUiFeatureState()) {
        return;
    }

    void* battleBridge = FeatureState::BattleBridge.load();

    if (!battleBridge) {
        return;
    }

    // MCBattleBridge UI calls are local presentation changes; they do not
    // request battle state changes from the game server.
    bool hideBloodBars = FeatureState::CombatHideAllBloodBars.load();
    if (Originals::MCBattleBridge_SetAllBloodVisible &&
        (hideBloodBars || FeatureState::CombatBloodBarsHiddenApplied.load())) {
        Originals::MCBattleBridge_SetAllBloodVisible(battleBridge, !hideBloodBars);
        FeatureState::CombatBloodBarsHiddenApplied = hideBloodBars;
    }

    bool hideJoystick = FeatureState::CombatHideJoystick.load();
    if (hideJoystick || FeatureState::CombatJoystickHiddenApplied.load()) {
        if (Originals::MCBattleBridge_SetJoyStickVisible) {
            Originals::MCBattleBridge_SetJoyStickVisible(battleBridge, !hideJoystick);
        }

        if (Originals::MCBattleBridge_SetJoyStickUIShow) {
            Originals::MCBattleBridge_SetJoyStickUIShow(battleBridge, !hideJoystick);
        }

        FeatureState::CombatJoystickHiddenApplied = hideJoystick;
    }

    bool hideSocialDragArea = FeatureState::CombatHideSocialDragArea.load();
    if (Originals::MCBattleBridge_SetSocialDragAreaVisible &&
        (hideSocialDragArea || FeatureState::CombatSocialDragAreaHiddenApplied.load())) {
        Originals::MCBattleBridge_SetSocialDragAreaVisible(
            battleBridge,
            !hideSocialDragArea
        );
        FeatureState::CombatSocialDragAreaHiddenApplied = hideSocialDragArea;
    }

    bool hideShopEntry = FeatureState::AutomationHideShopEntry.load();
    if (Originals::MCBattleBridge_SetShopEnterActive &&
        (hideShopEntry || FeatureState::AutomationShopEntryHiddenApplied.load())) {
        Originals::MCBattleBridge_SetShopEnterActive(battleBridge, !hideShopEntry);
        FeatureState::AutomationShopEntryHiddenApplied = hideShopEntry;
    }

    if (FeatureState::AutomationKeepChatClosed &&
        Originals::MCBattleBridge_CloseChatUI) {
        Originals::MCBattleBridge_CloseChatUI(battleBridge);
    }

    bool shouldOpenShop = FeatureState::AutomationOpenShopDuringPrepare.load();
    bool shouldCloseShop = FeatureState::AutomationCloseShopDuringFight.load();

    if ((shouldOpenShop || shouldCloseShop) &&
        selfAccountId != 0 &&
        FeatureState::WasInMatch &&
        Originals::MCBattleBridge_OpenShop &&
        Originals::MCLogicBattleData_ILOGIC_IsFightSection &&
        Originals::MCLogicBattleData_ILOGIC_IsFightResultSection) {
        bool isFightSection =
            Originals::MCLogicBattleData_ILOGIC_IsFightSection(nullptr);
        bool isFightResultSection =
            Originals::MCLogicBattleData_ILOGIC_IsFightResultSection(nullptr);

        if (shouldCloseShop && (isFightSection || isFightResultSection)) {
            Originals::MCBattleBridge_OpenShop(battleBridge, false);
        } else if (shouldOpenShop && !isFightSection && !isFightResultSection) {
            Originals::MCBattleBridge_OpenShop(battleBridge, true);
        }
    }
}

void TickFeatures() {
    if (!IsIl2CppRuntimeReady()) {
        return;
    }

    RetryFeatureBindingsIfNeeded();
    RefreshManagedReferences();

    uint64_t selfAccountId = GetSelfAccountId();
    RefreshTableDataForMatch(selfAccountId);
    EnsureTableDataLoaded();
    auto now = std::chrono::steady_clock::now();

    if (IntervalElapsed(FeatureState::LastArenaTick, RuntimeConfig::ArenaTickMs, now)) {
        ApplyArenaState(selfAccountId);
    }

    if (IntervalElapsed(FeatureState::LastShopTick, RuntimeConfig::ShopTickMs, now)) {
        RunShopAutomation(selfAccountId);
    }

    if (IntervalElapsed(FeatureState::LastLocalUiTick, RuntimeConfig::LocalUiTickMs, now)) {
        ApplyLocalUiFeatures(selfAccountId);
    }
}

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

void DrawValueRow(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
}

void DrawValueRow(const char* label, const std::string& value) {
    DrawValueRow(label, value.c_str());
}

std::string FormatBool(bool value) {
    return value ? "true" : "false";
}

std::string FormatOptionalBool(bool ready, bool value) {
    return ready ? FormatBool(value) : "Waiting";
}

std::string FormatPointer(const void* value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%p", value);
    return buffer;
}

std::string FormatUInt64(uint64_t value) {
    return std::to_string(static_cast<unsigned long long>(value));
}

std::string FormatInt(int value) {
    return std::to_string(value);
}

ImVec4 HexColor(unsigned int rgb, float alpha = 1.0f) {
    return ImVec4(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha
    );
}

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

int ParseConfigInt(const std::string& value, int fallback) {
    char* end = nullptr;
    long parsed = strtol(value.c_str(), &end, 10);

    if (end == value.c_str()) {
        return fallback;
    }

    return static_cast<int>(parsed);
}

float ParseConfigFloat(const std::string& value, float fallback) {
    char* end = nullptr;
    float parsed = strtof(value.c_str(), &end);

    if (end == value.c_str()) {
        return fallback;
    }

    return parsed;
}

void ClampConfigurableState() {
    UiState::MainTabIndex = std::clamp(UiState::MainTabIndex.load(), 0, 6);
    UiState::ThemeIndex = std::clamp(UiState::ThemeIndex.load(), 0, 1);
    UiState::FontIndex = std::clamp(UiState::FontIndex.load(), 0, 1);
    UiState::MenuWidth = std::clamp(UiState::MenuWidth.load(), 360.0f, 1600.0f);
    UiState::MenuHeight = std::clamp(UiState::MenuHeight.load(), 280.0f, 1200.0f);
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
    FeatureState::ShopKeepGoldAt = std::clamp(FeatureState::ShopKeepGoldAt.load(), 0, 999999);
    FeatureState::ShopRecommendTargetCount =
        std::clamp(FeatureState::ShopRecommendTargetCount.load(), 1, 99);
    FeatureState::ArenaHeroStar = std::clamp(FeatureState::ArenaHeroStar.load(), 1, 3);
    FeatureState::ArenaPrice = std::clamp(FeatureState::ArenaPrice.load(), 0, 99);

    {
        std::lock_guard<std::mutex> lock(RuntimeMutex::FeatureMutex);

        for (auto& item : FeatureState::ShopSelectedHeroes) {
            item.second.targetCount = std::clamp(item.second.targetCount, 1, 99);
        }
    }
}

void ResetVisualSettings() {
    UiState::ThemeIndex = 1;
    UiState::FontIndex = AppearanceState::NotoCjkFont ? 1 : 0;
    UiState::MoveFromTitleBarOnly = true;
    UiState::ResizeFromEdges = false;
    UiState::UseFixedMenuPosition = false;
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

void ResetFeatureSettings() {
    FeatureState::CombatInvisibleScout = false;
    FeatureState::CombatHideAllBloodBars = false;
    FeatureState::CombatHideJoystick = false;
    FeatureState::CombatHideSocialDragArea = false;
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
    FeatureState::AutomationOpenShopDuringPrepare = false;
    FeatureState::AutomationCloseShopDuringFight = false;
    FeatureState::AutomationHideShopEntry = false;
    FeatureState::AutomationKeepChatClosed = false;
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
    FeatureState::ArenaPrice = 5;
}

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

bool EnsureParentDirectory(const std::string& path) {
    size_t slash = path.find_last_of('/');

    if (slash == std::string::npos || slash == 0) {
        return true;
    }

    return EnsureDirectoryPath(path.substr(0, slash));
}

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

std::string GetGamePackageName() {
    std::string processName = GetCurrentProcessName();
    size_t suffix = processName.find(':');

    if (suffix != std::string::npos) {
        processName.resize(suffix);
    }

    return processName;
}

std::string GetDefaultConfigPath() {
    std::string packageName = GetGamePackageName();

    if (packageName.empty()) {
        return "mcgg_config.ini";
    }

    return "/data/data/" + packageName + "/files/mcgg_config.ini";
}

void EnsureConfigPathInitialized() {
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex::UiMutex);
    if (UiState::ConfigPath.empty()) {
        UiState::ConfigPath = GetDefaultConfigPath();
    }
}

void SetConfigStatus(const char* prefix, const std::string& path, bool success) {
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex::UiMutex);
    UiState::ConfigStatus = prefix;
    UiState::ConfigStatus += success ? ": " : " failed: ";
    UiState::ConfigStatus += path;
}

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

void WriteConfigBool(FILE* file, const char* key, bool value) {
    fprintf(file, "%s=%d\n", key, value ? 1 : 0);
}

void WriteConfigInt(FILE* file, const char* key, int value) {
    fprintf(file, "%s=%d\n", key, value);
}

void WriteConfigFloat(FILE* file, const char* key, float value) {
    fprintf(file, "%s=%.3f\n", key, value);
}

void WriteConfigString(FILE* file, const char* key, const std::string& value) {
    fprintf(file, "%s=%s\n", key, value.c_str());
}

void ApplyConfigValue(const std::string& key, const std::string& value) {
    if (key == "themeIndex") UiState::ThemeIndex = ParseConfigInt(value, UiState::ThemeIndex);
    else if (key == "fontIndex") UiState::FontIndex = ParseConfigInt(value, UiState::FontIndex);
    else if (key == "shopShowSelectedOnly") UiState::ShopShowSelectedOnly = ParseConfigBool(value, UiState::ShopShowSelectedOnly);
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
    else if (key == "combatHideAllBloodBars") FeatureState::CombatHideAllBloodBars = ParseConfigBool(value, FeatureState::CombatHideAllBloodBars);
    else if (key == "combatHideJoystick") FeatureState::CombatHideJoystick = ParseConfigBool(value, FeatureState::CombatHideJoystick);
    else if (key == "combatHideSocialDragArea") FeatureState::CombatHideSocialDragArea = ParseConfigBool(value, FeatureState::CombatHideSocialDragArea);
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
    else if (key == "automationOpenShopDuringPrepare") FeatureState::AutomationOpenShopDuringPrepare = ParseConfigBool(value, FeatureState::AutomationOpenShopDuringPrepare);
    else if (key == "automationCloseShopDuringFight") FeatureState::AutomationCloseShopDuringFight = ParseConfigBool(value, FeatureState::AutomationCloseShopDuringFight);
    else if (key == "automationHideShopEntry") FeatureState::AutomationHideShopEntry = ParseConfigBool(value, FeatureState::AutomationHideShopEntry);
    else if (key == "automationKeepChatClosed") FeatureState::AutomationKeepChatClosed = ParseConfigBool(value, FeatureState::AutomationKeepChatClosed);
    else if (key == "arenaHeroStar") FeatureState::ArenaHeroStar = ParseConfigInt(value, FeatureState::ArenaHeroStar);
    else if (key == "arenaItemEnhanced") FeatureState::ArenaItemEnhanced = ParseConfigBool(value, FeatureState::ArenaItemEnhanced);
    else if (key == "arenaGogoCardEnabled") FeatureState::ArenaGogoCardEnabled = ParseConfigBool(value, FeatureState::ArenaGogoCardEnabled);
    else if (key == "arenaGogoCardSelected1") FeatureState::ArenaGogoCardSelected1 = ParseConfigInt(value, FeatureState::ArenaGogoCardSelected1);
    else if (key == "arenaGogoCardSelected2") FeatureState::ArenaGogoCardSelected2 = ParseConfigInt(value, FeatureState::ArenaGogoCardSelected2);
    else if (key == "arenaForceActiveSynergy") FeatureState::ArenaForceActiveSynergy = ParseConfigBool(value, FeatureState::ArenaForceActiveSynergy);
    else if (key == "arenaForceLevel99") FeatureState::ArenaForceLevel99 = ParseConfigBool(value, FeatureState::ArenaForceLevel99);
    else if (key == "arenaOutsideMapPlacement") FeatureState::ArenaOutsideMapPlacement = ParseConfigBool(value, FeatureState::ArenaOutsideMapPlacement);
    else if (key == "arenaAllEnemyHpOne") FeatureState::ArenaAllEnemyHpOne = ParseConfigBool(value, FeatureState::ArenaAllEnemyHpOne);
    else if (key == "arenaPrice") FeatureState::ArenaPrice = ParseConfigInt(value, FeatureState::ArenaPrice);
}

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
    WriteConfigBool(file, "combatHideAllBloodBars", FeatureState::CombatHideAllBloodBars);
    WriteConfigBool(file, "combatHideJoystick", FeatureState::CombatHideJoystick);
    WriteConfigBool(
        file,
        "combatHideSocialDragArea",
        FeatureState::CombatHideSocialDragArea
    );
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
    WriteConfigBool(
        file,
        "automationOpenShopDuringPrepare",
        FeatureState::AutomationOpenShopDuringPrepare
    );
    WriteConfigBool(
        file,
        "automationCloseShopDuringFight",
        FeatureState::AutomationCloseShopDuringFight
    );
    WriteConfigBool(file, "automationHideShopEntry", FeatureState::AutomationHideShopEntry);
    WriteConfigBool(file, "automationKeepChatClosed", FeatureState::AutomationKeepChatClosed);
    WriteConfigInt(file, "arenaHeroStar", FeatureState::ArenaHeroStar);
    WriteConfigBool(file, "arenaItemEnhanced", FeatureState::ArenaItemEnhanced);
    WriteConfigBool(file, "arenaGogoCardEnabled", FeatureState::ArenaGogoCardEnabled);
    WriteConfigInt(file, "arenaGogoCardSelected1", FeatureState::ArenaGogoCardSelected1);
    WriteConfigInt(file, "arenaGogoCardSelected2", FeatureState::ArenaGogoCardSelected2);
    WriteConfigBool(file, "arenaForceActiveSynergy", FeatureState::ArenaForceActiveSynergy);
    WriteConfigBool(file, "arenaForceLevel99", FeatureState::ArenaForceLevel99);
    WriteConfigBool(file, "arenaOutsideMapPlacement", FeatureState::ArenaOutsideMapPlacement);
    WriteConfigBool(file, "arenaAllEnemyHpOne", FeatureState::ArenaAllEnemyHpOne);
    WriteConfigInt(file, "arenaPrice", FeatureState::ArenaPrice);

    fclose(file);
    SetConfigStatus("Saved", path, true);
    return true;
}

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

void ApplySelectedTheme() {
    if (AppearanceState::AppliedThemeIndex == UiState::ThemeIndex) {
        return;
    }

    if (UiState::ThemeIndex == 1) {
        ApplyCatppuccinMochaTheme();
    } else {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    }

    AppearanceState::AppliedThemeIndex = UiState::ThemeIndex;
}

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

void ApplyAppearance() {
    ApplySelectedTheme();
    ApplySelectedFont();
    ApplyUserStyleSettings();
}

std::string FormatFieldBool(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatBool(GetField<bool>(reinterpret_cast<Il2CppObject*>(instance), field));
}

std::string FormatFieldInt(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatInt(GetField<int>(reinterpret_cast<Il2CppObject*>(instance), field));
}

std::string FormatFieldUInt64(void* instance, FieldInfo* field) {
    if (!instance || !field) {
        return "Waiting";
    }

    return FormatUInt64(GetField<uint64_t>(reinterpret_cast<Il2CppObject*>(instance), field));
}

bool HasShopSelectBinding();
bool HasShopAutomationBindings();
bool HasShopRefreshBindings();
bool HasShopRecommendLineupBindings();
bool HasCombatLocalUiBindings();
bool HasClientUiAutomationBindings();
bool HasArenaHeroBindings();
bool HasArenaItemBindings();
bool HasArenaGogoCardBindings();
bool HasArenaGoldBindings();
bool HasBattleTestBindings();
std::string GetBattlePlayerName(uint64_t accountId);

struct PlayerInfoRow {
    uint64_t accountId = 0;
    bool isSelf = false;
    std::string playerName;
    std::string sortName;
    std::string enemyName;
};

namespace UiCache {
    std::vector<PlayerInfoRow> InfoPlayerRows;
    bool InfoPlayersReady = false;
    std::chrono::steady_clock::time_point LastInfoPlayerRefresh{};
    ImVec2 MenuWindowPos{};
    ImVec2 MenuWindowSize{};
}

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

void DrawRuntimeStatus() {
    if (!ImGui::CollapsingHeader("Runtime Status", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    char selfIdText[32];
    uint64_t selfAccountId = IsIl2CppRuntimeReady() ? GetSelfAccountId() : 0;
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

    if (ImGui::BeginTable(
        "##RuntimeStatusTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp
    )) {
        ImGui::TableSetupColumn("Runtime");
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        DrawValueRow("Self account", selfIdText);
        DrawValueRow("Table cache", tableText);
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
        DrawStatusRow("Combat local UI", HasCombatLocalUiBindings());
        DrawStatusRow("Client UI automation", HasClientUiAutomationBindings());
        DrawStatusRow("Arena heroes", HasArenaHeroBindings());
        DrawStatusRow("Arena items", HasArenaItemBindings());
        DrawStatusRow("Arena GogoCards", HasArenaGogoCardBindings());
        DrawStatusRow("Arena gold", HasArenaGoldBindings());
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

void DrawWaitingText(const char* message) {
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "%s", message);
}

bool DrawAtomicCheckbox(const char* label, std::atomic<bool>& value) {
    bool current = value.load();

    if (!ImGui::Checkbox(label, &current)) {
        return false;
    }

    value = current;
    return true;
}

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

bool HasShopSelectBinding() {
    return IsIl2CppRuntimeReady() &&
        ((FeatureState::HeroShopItemList &&
            Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero) ||
         (FeatureState::HeroShopPanel &&
         Originals::UIPanelBattleHeroShop_KeyBoardShopSelect) ||
         (FeatureState::HeroShopPanel &&
          Originals::UIPanelBattleHeroShop_BuyHero));
}

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

bool HasShopRefreshBindings() {
    return IsIl2CppRuntimeReady() &&
        FeatureState::HeroShopPanel &&
        Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop;
}

bool HasShopRecommendLineupBindings() {
    return IsIl2CppRuntimeReady() &&
        (Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup ||
         (FeatureState::BattleBridge &&
          Originals::MCBattleBridge_IsHeroInRecommendLineup));
}

bool HasCombatLocalUiBindings() {
    return IsIl2CppRuntimeReady() &&
        FeatureState::BattleBridge &&
        Originals::MCBattleBridge_SetAllBloodVisible &&
        Originals::MCBattleBridge_SetJoyStickVisible &&
        Originals::MCBattleBridge_SetJoyStickUIShow &&
        Originals::MCBattleBridge_SetSocialDragAreaVisible;
}

bool HasClientUiAutomationBindings() {
    return IsIl2CppRuntimeReady() &&
        FeatureState::BattleBridge &&
        Originals::MCBattleBridge_OpenShop &&
        Originals::MCBattleBridge_CloseChatUI &&
        Originals::MCBattleBridge_SetShopEnterActive &&
        Originals::MCLogicBattleData_ILOGIC_IsFightSection &&
        Originals::MCLogicBattleData_ILOGIC_IsFightResultSection;
}

bool HasArenaHeroBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleManager_BuyNormalHero &&
        (Originals::MCComp_GetGamer || GetSelfLogicBattleManager());
}

bool HasArenaItemBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCEquipUtil_OnGetNewEquip != nullptr;
}

bool HasArenaGogoCardBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCComp_GetGoGoCardComp != nullptr;
}

bool HasArenaGoldBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerData &&
        Originals::MCChessPlayerData_UpdateCoin;
}

bool HasBattleTestBindings() {
    return IsIl2CppRuntimeReady() &&
        Originals::MCLogicBattleData_ILOGIC_GetRoundRemainTime &&
        Originals::MCLogicBattleData_ILOGIC_IsFightSection &&
        Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerHP &&
        Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID &&
        Originals::MCLogicBattleManager_get_m_bDefendFaild;
}

void DrawGgcInfo() {
    ImGui::SeparatorText("GGC");

    if (!IsIl2CppRuntimeReady()) {
        ImGui::TextUnformatted("Waiting for GGC data");
        return;
    }

    uint64_t selfAccountId = GetSelfAccountId();

    if (!Originals::MCLogicBattleData_ILOGIC_GetCrystalQualityByRound ||
        selfAccountId == 0) {
        ImGui::TextUnformatted("Waiting for GGC data");
        return;
    }

    int round7Quality =
        Originals::MCLogicBattleData_ILOGIC_GetCrystalQualityByRound(
            nullptr,
            selfAccountId,
            7
        );
    int round13Quality =
        Originals::MCLogicBattleData_ILOGIC_GetCrystalQualityByRound(
            nullptr,
            selfAccountId,
            13
        );

    ImGui::TextUnformatted("Round 7");
    ImGui::SameLine(120.0f);
    ImGui::TextColored(
        GgcQualityColor(round7Quality),
        "%s (%d)",
        GgcQualityName(round7Quality),
        round7Quality
    );

    ImGui::TextUnformatted("Round 13");
    ImGui::SameLine(120.0f);
    ImGui::TextColored(
        GgcQualityColor(round13Quality),
        "%s (%d)",
        GgcQualityName(round13Quality),
        round13Quality
    );
}

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

    RefreshInfoPlayerRows();

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

void DrawInfoTab() {
    if (!IsIl2CppRuntimeReady()) {
        DrawWaitingText("Waiting for IL2CPP runtime");
        ImGui::Spacing();
    }

    DrawRuntimeStatus();
    ImGui::Spacing();
    DrawGgcInfo();
    ImGui::Spacing();
    DrawInfoPlayersTable();
}

void DrawCombatTab() {
    ImGui::SeparatorText("Lineup");

    if (!Originals::MCShowSpectatorComp_SetSpectate) {
        DrawWaitingText("Waiting for spectator hook");
    }

    DrawAtomicCheckbox(
        "Invisible Scout - hide spectate switching",
        FeatureState::CombatInvisibleScout
    );

    ImGui::SeparatorText("Client UI");

    if (!HasCombatLocalUiBindings()) {
        DrawWaitingText("Waiting for combat local UI bindings");
    }

    void* battleBridge = FeatureState::BattleBridge.load();
    if (battleBridge && Originals::MCBattleBridge_CheckEnableKeyBoard) {
        ImGui::Text(
            "Keyboard enabled: %s",
            FormatBool(Originals::MCBattleBridge_CheckEnableKeyBoard(battleBridge)).c_str()
        );
    } else {
        ImGui::TextUnformatted("Keyboard enabled: Waiting");
    }

    DrawAtomicCheckbox("Hide all blood bars", FeatureState::CombatHideAllBloodBars);
    DrawAtomicCheckbox("Hide joystick", FeatureState::CombatHideJoystick);
    DrawAtomicCheckbox("Hide social drag area", FeatureState::CombatHideSocialDragArea);
}

void DrawAppearanceTab() {
    ImGui::SeparatorText("Theme");

    const char* themes[] = {
        "ImGui Dark",
        "Catppuccin Mocha"
    };

    ImGui::SetNextItemWidth(220.0f);
    if (DrawAtomicCombo("Theme", UiState::ThemeIndex, themes, IM_ARRAYSIZE(themes))) {
        UiState::ThemeIndex =
            std::clamp(UiState::ThemeIndex.load(), 0, IM_ARRAYSIZE(themes) - 1);
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
            ImGui::TextUnformatted("Saved state includes visual settings, window settings, Combat, Shop, and Arena state.");

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
    DrawStatusRow("Fight section", Originals::MCLogicBattleData_ILOGIC_IsFightSection);
    DrawStatusRow("Self fight over", Originals::MCLogicBattleData_ILOGIC_IsSelfFightOver);
    DrawStatusRow("Player HP", Originals::MCLogicBattleData_ILOGIC_GetPlayerHP);
    DrawStatusRow("Result history", Originals::MCLogicBattleData_ILOGIC_GetBattleResultHistory);
    DrawStatusRow(
        "Recommend hero",
        Originals::MCLogicBattleData_ILOGIC_GetHeroByRecommendLineup
    );
    DrawStatusRow("Recommend membership", Originals::MCBattleBridge_IsHeroInRecommendLineup);
    DrawStatusRow("Battle manager flags", Originals::MCLogicBattleManager_get_m_bDefendFaild);
    DrawStatusRow("Alive fighter counts", Originals::MCLogicBattleManager_GetAliveFighter);
    DrawStatusRow("Behavior API", Originals::MCBehaviorThreeApi_Get);

    ImGui::EndTable();
}

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
};

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
            player.alive
        });
    }

    if (selfAccountId == 0 || rows.empty()) {
        return rows;
    }

    void* selfManager = GetBattleManagerByAccountId(selfAccountId);
    void* invasionManager = GetLogicInvasionManager();
    bool monsterRound = IsCurrentMonsterRound(invasionManager);
    bool realPlayerMode = IsRealPlayerPairingMode(invasionManager);
    uint64_t exactOpponent = FindExactPredictedOpponent(
        selfAccountId,
        selfManager,
        invasionManager,
        players
    );

    if (monsterRound) {
        return rows;
    }

    if (exactOpponent != 0) {
        for (OpponentPredictionRow& row : rows) {
            if (row.accountId == exactOpponent && row.alive) {
                row.percent = 100;
                row.weight = 100.0;
                break;
            }
        }

        return rows;
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

    uint32_t round = Originals::MCLogicBattleData_ILOGIC_GetGameRound ?
        Originals::MCLogicBattleData_ILOGIC_GetGameRound(nullptr) :
        0;
    uint64_t roundRobinOpponent = PredictRoundRobinOpponent(
        aliveAccounts,
        selfAccountId,
        round
    );

    if (roundRobinOpponent == 0 && aliveAccounts.size() % 2 == 1) {
        return rows;
    }

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

        if (playerIt == players.end() || !playerIt->alive) {
            row.weight = 0.0;
            continue;
        }

        double weight = 100.0;
        uint64_t candidatePair = GetCurrentPairFromInvasion(
            invasionManager,
            playerIt->accountId
        );

        if (candidatePair == selfAccountId) {
            weight *= 5.0;
        } else if (candidatePair != 0) {
            weight *= 0.03;
        }

        uint64_t candidateCurrentOpponent =
            Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ?
                Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                    nullptr,
                    playerIt->accountId
                ) :
                0;

        if (candidateCurrentOpponent == selfAccountId) {
            weight *= 4.0;
        } else if (candidateCurrentOpponent != 0) {
            weight *= 0.08;
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

        if (roundRobinOpponent != 0) {
            weight *= row.accountId == roundRobinOpponent ? 4.5 : 0.55;
        }

        row.weight = std::max(weight, 0.0);
    }

    double totalWeight = 0.0;
    int strongestIndex = -1;

    for (size_t i = 0; i < rows.size(); ++i) {
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

void DrawOpponentPredictionTable(uint64_t selfAccountId) {
    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        DrawWaitingText("Waiting for battle manager list");
        return;
    }

    std::vector<OpponentPredictionRow> rows = BuildOpponentPredictions(selfAccountId);

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

    if (rows.empty()) {
        ImGui::TextUnformatted("No players found");
        return;
    }

    if (!ImGui::BeginTable(
        "##OpponentPredictionTable",
        2,
        ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 230.0f)
    )) {
        return;
    }

    ImGui::TableSetupColumn("Player");
    ImGui::TableSetupColumn("Will fight", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableHeadersRow();

    for (const OpponentPredictionRow& row : rows) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(row.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d%%", row.percent);
    }

    ImGui::EndTable();
}

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

void DrawTestTab() {
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

    ImGui::SeparatorText("Fight Prediction");
    DrawOpponentPredictionTable(selfAccountId);

    ImGui::SeparatorText("Bindings");
    DrawTestBindingRows();

    ImGui::SeparatorText("Round State");
    DrawTestRoundRows(selfAccountId, targetAccountId, opponentAccountId);

    ImGui::SeparatorText("Battle Manager");
    DrawTestManagerRows(targetManager);

    ImGui::SeparatorText("Behavior API");
    DrawTestBehaviorRows(targetAccountId);

    ImGui::SeparatorText("All Managers");
    DrawTestAllManagersTable();
}

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
                int recommendHeroId =
                    GetRecommendLineupHeroId(std::chrono::steady_clock::now());
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

            ImGui::Separator();
            ImGui::SeparatorText("Client UI");

            if (!HasClientUiAutomationBindings()) {
                DrawWaitingText("Waiting for client UI automation bindings");
            }

            DrawAtomicCheckbox(
                "Open shop during prepare",
                FeatureState::AutomationOpenShopDuringPrepare
            );
            DrawAtomicCheckbox(
                "Close shop during fight/result",
                FeatureState::AutomationCloseShopDuringFight
            );
            DrawAtomicCheckbox(
                "Hide shop entry button",
                FeatureState::AutomationHideShopEntry
            );
            DrawAtomicCheckbox(
                "Keep chat closed",
                FeatureState::AutomationKeepChatClosed
            );
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

                for (const HeroTableEntry& hero : heroes) {
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

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

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

                for (const HeroTableEntry& hero : heroes) {
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

                for (const EquipTableEntry& equip : equips) {
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

                for (const CardTableEntry& card : cards) {
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

                ImGui::EndTable();
            }

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
            DrawAtomicCheckbox("Force player level 99", FeatureState::ArenaForceLevel99);
            DrawAtomicCheckbox("Allow outside-map placement", FeatureState::ArenaOutsideMapPlacement);
            DrawAtomicCheckbox("Set all enemy HP to 1", FeatureState::ArenaAllEnemyHpOne);
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

void DrawMenuTabButton(const char* label, int index) {
    bool selected = UiState::MainTabIndex.load() == index;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));

    if (ImGui::Selectable(label, selected, 0, ImVec2(-1.0f, 34.0f))) {
        UiState::MainTabIndex = index;
    }

    ImGui::PopStyleVar(2);
}

ImVec2 GetValidatedMenuSize() {
    ClampConfigurableState();

    ImGuiIO& io = ImGui::GetIO();
    float maxWidth = io.DisplaySize.x > 0.0f ?
        std::max(360.0f, io.DisplaySize.x - 24.0f) :
        1600.0f;
    float maxHeight = io.DisplaySize.y > 0.0f ?
        std::max(280.0f, io.DisplaySize.y - 24.0f) :
        1200.0f;

    return ImVec2(
        std::clamp(UiState::MenuWidth.load(), 360.0f, maxWidth),
        std::clamp(UiState::MenuHeight.load(), 280.0f, maxHeight)
    );
}

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

void DrawMainMenu() {
    static const MainMenuTab tabs[] = {
        {"Info", DrawInfoTab},
        {"Combat", DrawCombatTab},
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

    if (UiState::UseFixedMenuPosition.load()) {
        ImGui::SetNextWindowPos(GetValidatedMenuPosition(menuSize), ImGuiCond_Always);
    }

    ImGui::SetNextWindowSize(menuSize, ImGuiCond_Once);

    if (!ImGui::Begin("MCGG", nullptr)) {
        ImGui::End();
        return;
    }

    UiCache::MenuWindowPos = ImGui::GetWindowPos();
    UiCache::MenuWindowSize = ImGui::GetWindowSize();

    if (ImGui::BeginTabBar("##MainTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i = 0; i < tabCount; ++i) {
            if (ImGui::BeginTabItem(tabs[i].label)) {
                UiState::MainTabIndex = i;

                ImGui::Spacing();

                if (tabs[i].draw != nullptr) {
                    tabs[i].draw();
                }

                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

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
    void MCShowSpectatorComp_SetSpectate(void* instance, uint64_t accountId) {
        if (FeatureState::CombatInvisibleScout) {
            return;
        }

        if (Originals::MCShowSpectatorComp_SetSpectate) {
            Originals::MCShowSpectatorComp_SetSpectate(instance, accountId);
        }
    }

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

    bool AStarTileMap_ValidPos(int x, int y) {
        if (FeatureState::ArenaOutsideMapPlacement) {
            return true;
        }

        return Originals::AStarTileMap_ValidPos ?
            Originals::AStarTileMap_ValidPos(x, y) :
            false;
    }

    bool MCLogicEntityMap_CanWalkable(void* instance, int x, int y) {
        if (FeatureState::ArenaOutsideMapPlacement) {
            return true;
        }

        return Originals::MCLogicEntityMap_CanWalkable ?
            Originals::MCLogicEntityMap_CanWalkable(instance, x, y) :
            false;
    }

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
    sleep(5);

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

#include "Il2CppVersions/api/2019.4.22f1.h"

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

    auto GetTouch_Methods =
        GetAllMethodsFromName("UnityEngine", "Input", "GetTouch", {"int"});

    if (!GetTouch_Methods.empty()) {
        DobbyHook(
            GetTouch_Methods[0],
            (void*)Hooks::Input_GetTouch,
            (void**)&Originals::Input_GetTouch
        );
    }

    RuntimeState::Il2CppReady = true;
    RuntimeState::BindingRetryRequested = true;
}

// Starts hook setup when this shared library is loaded in the target process.
__attribute__((constructor))
void InitLibrary() {
    sleep(1);

    if (!IsUnityMoontonProcess()) {
        return;
    }

    sleep(2);

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
