#include <jni.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
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

struct HeroAutomationState {
    bool selected = false;
    int targetCount = 9;
};

struct {
    void* libil2cpp = nullptr;
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
}

// Feature toggles, cached managed references, and throttled runtime state.
namespace FeatureState {
    bool CombatInvisibleScout = false;

    bool ShopBuyFreeHero = false;
    bool ShopBuySelectedHero = false;
    bool ShopRefresh = false;
    bool ShopStopRefreshAtFreeHero = false;
    bool ShopStopRefreshAtSelectedHero = false;
    bool ShopKeepGold = false;
    int ShopKeepGoldAt = 20;
    std::unordered_map<int, HeroAutomationState> ShopSelectedHeroes;

    int ArenaHeroStar = 1;
    bool ArenaItemEnhanced = false;
    bool ArenaGogoCardEnabled = false;
    int ArenaGogoCardSelected1 = -1;
    int ArenaGogoCardSelected2 = -1;
    bool ArenaForceActiveSynergy = false;
    bool ArenaForceLevel99 = false;
    bool ArenaOutsideMapPlacement = false;
    bool ArenaAllEnemyHpOne = false;
    int ArenaPrice = 5;

    void* BattleBridge = nullptr;
    void* HeroShopPanel = nullptr;
    void* HeroShopItemList = nullptr;
    void* LoadResInstance = nullptr;

    bool TableDataLoaded = false;
    bool WasInMatch = false;
    uint64_t LastSelfAccountId = 0;
    std::unordered_map<int, HeroTableEntry> Heroes;
    std::unordered_map<int, EquipTableEntry> Equips;
    std::unordered_map<int, CardTableEntry> Cards;

    std::chrono::steady_clock::time_point LastBindingRetry{};
    std::chrono::steady_clock::time_point LastReferenceRefresh{};
    std::chrono::steady_clock::time_point LastArenaTick{};
    std::chrono::steady_clock::time_point LastShopTick{};
    std::chrono::steady_clock::time_point LastTableLoadAttempt{};
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
    int (*MCLogicBattleData_ILOGIC_GetPlayerCoin)(
        void* instance,
        uint64_t accountId
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
bool IsUnityProcess() {
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

// Resolves a class name, including nested class paths.
Il2CppClass* ResolveClassFromName(
    const Il2CppImage* image,
    const char* namespaze,
    const char* className
) {
    Il2CppClass* klass = il2cpp_class_from_name(image, namespaze, className);
    if (klass) {
        return klass;
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
    if (!className || !fieldName || !il2cpp_domain_get || !il2cpp_domain_get_assemblies) {
        return nullptr;
    }

    std::string cacheKey = GenerateFieldCacheKey(ns, className, fieldName);

    auto cached = FieldCache.find(cacheKey);
    if (cached != FieldCache.end() && cached->second) {
        return cached->second;
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
            FieldCache[cacheKey] = field;
            return field;
        }
    }

    FieldCache[cacheKey] = nullptr;
    return nullptr;
}

// Reads an instance field by metadata into a caller-provided buffer.
bool GetFieldRaw(Il2CppObject* instance, FieldInfo* field, void* outValue) {
    if (!instance || !field || !outValue || !il2cpp_field_get_value) {
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
    if (!field || !outValue || !il2cpp_field_static_get_value) {
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
    if (!instance || !field || !value || !il2cpp_field_set_value) {
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
    if (!field || !value || !il2cpp_field_static_set_value) {
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
    std::string cacheKey = GenerateCacheKey(ns, className, methodName, paramTypes);

    auto cached = MultiMethodCache.find(cacheKey);
    if (cached != MultiMethodCache.end() && !cached->second.empty()) {
        return cached->second;
    }

    std::vector<MethodInfo*> foundMethods;

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

            currentKlass = il2cpp_class_get_parent(currentKlass);
        }

        if (!foundMethods.empty()) {
            break;
        }
    }

    MultiMethodCache[cacheKey] = foundMethods;
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
    if (target) {
        return true;
    }

    void* method = GetFirstMethodFromName(ns, className, methodName, paramTypes);

    if (method) {
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
    if (original) {
        return true;
    }

    void* method = GetFirstMethodFromName(ns, className, methodName, paramTypes);

    if (method) {
        DobbyHook(method, replacement, reinterpret_cast<void**>(&original));
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
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin,
        "",
        "MCLogicBattleData",
        "ILOGIC_GetPlayerCoin",
        {"UInt64"}
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
    if (IntervalElapsed(FeatureState::LastBindingRetry, RuntimeConfig::BindingRetryMs)) {
        ResolveFeatureBindings();
    }
}

std::string ManagedStringToStd(Il2CppString* value) {
    if (!value) {
        return {};
    }

    return reinterpret_cast<MonoStructures::String*>(value)->str();
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

void RefreshManagedReferences(bool force = false) {
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

    FeatureState::BattleBridge = GetStaticField<void*>(battleBridgeField);
    FeatureState::LoadResInstance = GetStaticField<void*>(loadResInstanceField);
    FeatureState::HeroShopPanel = nullptr;
    FeatureState::HeroShopItemList = nullptr;

    if (FeatureState::BattleBridge) {
        FeatureState::HeroShopPanel = GetField<void*>(
            reinterpret_cast<Il2CppObject*>(FeatureState::BattleBridge),
            heroShopPanelField
        );
    }

    if (FeatureState::HeroShopPanel) {
        FeatureState::HeroShopItemList = GetField<void*>(
            reinterpret_cast<Il2CppObject*>(FeatureState::HeroShopPanel),
            heroShopItemListField
        );
    }
}

std::vector<HeroTableEntry> GetSortedHeroes(bool validOnly) {
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

void ClearTableDataCache() {
    FeatureState::TableDataLoaded = false;
    FeatureState::Heroes.clear();
    FeatureState::Equips.clear();
    FeatureState::Cards.clear();
    FeatureState::LastTableLoadAttempt = {};
}

bool IsBattleActive(uint64_t selfAccountId) {
    if (selfAccountId == 0) {
        return false;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr) {
        return true;
    }

    auto* battleManagers = Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);
    return battleManagers && !battleManagers->empty();
}

void RefreshTableDataForMatch(uint64_t selfAccountId) {
    bool battleActive = IsBattleActive(selfAccountId);
    bool selfChanged =
        selfAccountId != 0 &&
        selfAccountId != FeatureState::LastSelfAccountId;

    if (battleActive && (!FeatureState::WasInMatch || selfChanged)) {
        ClearTableDataCache();
    }

    FeatureState::WasInMatch = battleActive;
    FeatureState::LastSelfAccountId = battleActive ? selfAccountId : 0;
}

void EnsureTableDataLoaded() {
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

            for (const auto& item : heroDictionary->ToVector()) {
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
                FeatureState::Heroes[heroId] = {heroId, heroName, quality, true};
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

            for (const auto& outer : equipDictionary->ToVector()) {
                auto* nestedDictionary = outer.second;

                if (!nestedDictionary) {
                    continue;
                }

                for (const auto& inner : nestedDictionary->ToVector()) {
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
                        FeatureState::Equips[equipId] = {equipId, equipName};
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

            for (const auto& item : cardDictionary->ToVector()) {
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
                    FeatureState::Cards[cardId] = {cardId, cardName};
                }
            }
        }
    }

    FeatureState::TableDataLoaded =
        !FeatureState::Heroes.empty() &&
        !FeatureState::Equips.empty() &&
        !FeatureState::Cards.empty();
}

bool SelectShopSlot(int slot) {
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

void GiveHero(int heroId, int star) {
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
    uint64_t selfAccountId = GetSelfAccountId();

    if (selfAccountId == 0 || !Originals::MCEquipUtil_OnGetNewEquip || equipId <= 0) {
        return;
    }

    uint32_t guid = 0;
    int upgradeState = FeatureState::ArenaItemEnhanced ? 1 : 0;
    Originals::MCEquipUtil_OnGetNewEquip(selfAccountId, equipId, &guid, upgradeState);
}

void GiveGold() {
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
    if (selfAccountId == 0) {
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
            auto* entries = battleManagers && battleManagers->entries ?
                battleManagers->entries->GetData() :
                nullptr;
            int entryLimit = battleManagers && battleManagers->entries ?
                std::min(battleManagers->count, battleManagers->entries->getCapacity()) :
                0;

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
            for (const auto& item : roundDictionary->ToVector()) {
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

                if (FeatureState::ArenaGogoCardSelected1 > 0 && cardList->GetSize() >= 1) {
                    cardList->set_Item(0, FeatureState::ArenaGogoCardSelected1);
                }

                if (FeatureState::ArenaGogoCardSelected2 > 0 && cardList->GetSize() >= 2) {
                    cardList->set_Item(1, FeatureState::ArenaGogoCardSelected2);
                }
            }
        }
    }
}

void RunShopAutomation(uint64_t selfAccountId) {
    bool anyShopAutomation =
        FeatureState::ShopBuyFreeHero ||
        FeatureState::ShopBuySelectedHero ||
        FeatureState::ShopRefresh ||
        FeatureState::ShopStopRefreshAtFreeHero ||
        FeatureState::ShopStopRefreshAtSelectedHero;

    if (!anyShopAutomation || selfAccountId == 0) {
        return;
    }

    if (!Originals::MCLogicBattleData_ILOGIC_GetShopItemData ||
        !Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin) {
        return;
    }

    bool needRefreshShop = true;

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
        int heroId = shopData.m_iHeroId;

        if (heroId <= 0) {
            continue;
        }

        auto selectedIt = FeatureState::ShopSelectedHeroes.find(heroId);
        bool isSelected =
            selectedIt != FeatureState::ShopSelectedHeroes.end() &&
            selectedIt->second.selected;
        int targetCount =
            selectedIt != FeatureState::ShopSelectedHeroes.end() ?
                std::max(selectedIt->second.targetCount, 1) :
                0;
        int ownCount = -1;

        if (Originals::MCLogicBattleData_ILogic_HeroOwnCount) {
            ownCount = Originals::MCLogicBattleData_ILogic_HeroOwnCount(
                nullptr,
                selfAccountId,
                heroId
            );
        }

        if (FeatureState::ShopStopRefreshAtSelectedHero &&
            isSelected &&
            ownCount >= 0 &&
            ownCount < targetCount) {
            needRefreshShop = false;
        }

        if (FeatureState::ShopStopRefreshAtFreeHero && isFreeBuy) {
            needRefreshShop = false;
        }

        if (FeatureState::ShopBuyFreeHero && isFreeBuy) {
            SelectShopSlot(slot);
            continue;
        }

        if (!FeatureState::ShopBuySelectedHero || !isSelected) {
            continue;
        }

        if (ownCount >= 0 && ownCount >= targetCount) {
            continue;
        }

        int coin =
            Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, selfAccountId);

        if (coin < shopData.m_iPrice) {
            continue;
        }

        if (FeatureState::ShopKeepGold &&
            coin - shopData.m_iPrice < FeatureState::ShopKeepGoldAt) {
            continue;
        }

        SelectShopSlot(slot);
    }

    if (!FeatureState::ShopRefresh ||
        !needRefreshShop ||
        !FeatureState::HeroShopPanel ||
        !Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop) {
        return;
    }

    bool anyHeroWorth = false;

    for (const auto& item : FeatureState::ShopSelectedHeroes) {
        int heroId = item.first;
        const HeroAutomationState& state = item.second;

        if (!state.selected || !Originals::MCLogicBattleData_ILogic_HeroOwnCount) {
            continue;
        }

        int ownCount = Originals::MCLogicBattleData_ILogic_HeroOwnCount(
            nullptr,
            selfAccountId,
            heroId
        );
        int poolCount = Originals::MCLogicBattleData_ILogic_HeroCountInPool ?
            Originals::MCLogicBattleData_ILogic_HeroCountInPool(nullptr, heroId) :
            1;

        if (ownCount < std::max(state.targetCount, 1) && poolCount > 0) {
            anyHeroWorth = true;
            break;
        }
    }

    if (!anyHeroWorth) {
        return;
    }

    int refreshCost = Originals::MCLogicBattleData_ILOGIC_GetRefreshCost ?
        Originals::MCLogicBattleData_ILOGIC_GetRefreshCost(nullptr, selfAccountId) :
        0;
    bool isFreeRefresh = Originals::MCLogicBattleData_ILOGIC_IsRefreshFree &&
        Originals::MCLogicBattleData_ILOGIC_IsRefreshFree(nullptr, selfAccountId);
    int coin = Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin(nullptr, selfAccountId);
    bool canRefresh =
        isFreeRefresh ||
        (coin >= refreshCost &&
         (!FeatureState::ShopKeepGold ||
          coin - refreshCost >= FeatureState::ShopKeepGoldAt));

    if (canRefresh) {
        Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop(FeatureState::HeroShopPanel);
    }
}

void TickFeatures() {
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

void DrawWaitingText(const char* message) {
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "%s", message);
}

bool HasShopSelectBinding() {
    return (FeatureState::HeroShopItemList &&
            Originals::UIPanelBattleHeroShop_HeroItemList_OnSelectHero) ||
        (FeatureState::HeroShopPanel &&
         Originals::UIPanelBattleHeroShop_KeyBoardShopSelect) ||
        (FeatureState::HeroShopPanel &&
         Originals::UIPanelBattleHeroShop_BuyHero);
}

bool HasShopAutomationBindings() {
    return Originals::MCLogicBattleData_ILOGIC_GetShopItemData &&
        Originals::MCLogicBattleData_ILOGIC_GetPlayerCoin &&
        HasShopSelectBinding();
}

bool HasShopRefreshBindings() {
    return FeatureState::HeroShopPanel &&
        Originals::UIPanelBattleHeroShop_KeyBoardRefreshShop;
}

bool HasArenaHeroBindings() {
    return Originals::MCLogicBattleManager_BuyNormalHero &&
        (Originals::MCComp_GetGamer || GetSelfLogicBattleManager());
}

bool HasArenaItemBindings() {
    return Originals::MCEquipUtil_OnGetNewEquip != nullptr;
}

bool HasArenaGogoCardBindings() {
    return Originals::MCComp_GetGoGoCardComp != nullptr;
}

bool HasArenaGoldBindings() {
    return Originals::MCLogicBattleData_ILOGIC_GetPlayerData &&
        Originals::MCChessPlayerData_UpdateCoin;
}

void DrawGgcInfo() {
    ImGui::SeparatorText("GGC");

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

void DrawCombatTab() {
    ImGui::SeparatorText("Lineup");

    if (!Originals::MCShowSpectatorComp_SetSpectate) {
        DrawWaitingText("Waiting for spectator hook");
    }

    ImGui::Checkbox(
        "Invisible Scout - hide spectate switching",
        &FeatureState::CombatInvisibleScout
    );
}

void DrawShopTab() {
    if (ImGui::BeginTabBar("##ShopTabBar")) {
        if (ImGui::BeginTabItem("Automation")) {
            if (!HasShopAutomationBindings()) {
                DrawWaitingText("Waiting for shop automation bindings");
            }

            if (FeatureState::ShopRefresh && !HasShopRefreshBindings()) {
                DrawWaitingText("Waiting for shop refresh panel");
            }

            ImGui::Checkbox("Auto-buy free heroes", &FeatureState::ShopBuyFreeHero);
            ImGui::Checkbox("Auto-buy selected targets", &FeatureState::ShopBuySelectedHero);
            ImGui::Separator();
            ImGui::Checkbox("Auto-refresh shop", &FeatureState::ShopRefresh);
            ImGui::Checkbox(
                "Pause refresh when free hero appears",
                &FeatureState::ShopStopRefreshAtFreeHero
            );
            ImGui::Checkbox(
                "Pause refresh when selected target appears",
                &FeatureState::ShopStopRefreshAtSelectedHero
            );
            ImGui::Separator();
            ImGui::Checkbox("Keep gold reserve", &FeatureState::ShopKeepGold);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Minimum reserve gold", &FeatureState::ShopKeepGoldAt);
            FeatureState::ShopKeepGoldAt = std::max(0, FeatureState::ShopKeepGoldAt);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Hero Targets")) {
            if (ImGui::Button("Clear hero targets", ImVec2(-1.0f, 0.0f))) {
                for (auto& item : FeatureState::ShopSelectedHeroes) {
                    item.second.selected = false;
                }
            }

            ImGui::Spacing();
            std::vector<HeroTableEntry> heroes = GetSortedHeroes(true);

            if (heroes.empty()) {
                DrawWaitingText("Waiting for hero table");
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
                    HeroAutomationState& state = FeatureState::ShopSelectedHeroes[hero.id];
                    state.targetCount = std::max(1, state.targetCount);

                    ImGui::PushID(hero.id);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(hero.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", hero.quality);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputInt("##target", &state.targetCount);
                    state.targetCount = std::max(1, state.targetCount);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Checkbox("##selected", &state.selected);
                    ImGui::PopID();
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
            ImGui::InputInt("Spawn star level", &FeatureState::ArenaHeroStar);
            FeatureState::ArenaHeroStar = std::clamp(FeatureState::ArenaHeroStar, 1, 3);
            ImGui::Separator();

            std::vector<HeroTableEntry> heroes = GetSortedHeroes(true);

            if (heroes.empty()) {
                DrawWaitingText("Waiting for hero table");
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
                        GiveHero(hero.id, FeatureState::ArenaHeroStar);
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

            ImGui::Checkbox("Grant enhanced item", &FeatureState::ArenaItemEnhanced);
            ImGui::Separator();

            std::vector<EquipTableEntry> equips = GetSortedEquips();

            if (equips.empty()) {
                DrawWaitingText("Waiting for item table");
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

            ImGui::Checkbox("Force selected GogoCards", &FeatureState::ArenaGogoCardEnabled);
            ImGui::Text(
                "Card 1: %d  Card 2: %d",
                FeatureState::ArenaGogoCardSelected1,
                FeatureState::ArenaGogoCardSelected2
            );
            ImGui::Separator();

            std::vector<CardTableEntry> cards = GetSortedCards();

            if (cards.empty()) {
                DrawWaitingText("Waiting for GogoCard table");
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
                    ImGui::PushID(card.id);
                    ImGui::TableNextRow();

                    if (card.id == FeatureState::ArenaGogoCardSelected1 ||
                        card.id == FeatureState::ArenaGogoCardSelected2) {
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

            ImGui::Checkbox("Force all synergies active", &FeatureState::ArenaForceActiveSynergy);
            ImGui::Checkbox("Force player level 99", &FeatureState::ArenaForceLevel99);
            ImGui::Checkbox("Allow outside-map placement", &FeatureState::ArenaOutsideMapPlacement);
            ImGui::Checkbox("Set all enemy HP to 1", &FeatureState::ArenaAllEnemyHpOne);
            ImGui::Separator();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Hero cost filter", &FeatureState::ArenaPrice);

            if (ImGui::Button("Spawn all heroes with selected cost", ImVec2(-1.0f, 0.0f))) {
                for (const HeroTableEntry& hero : GetSortedHeroes(true)) {
                    if (hero.quality == FeatureState::ArenaPrice) {
                        GiveHero(hero.id, FeatureState::ArenaHeroStar);
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
        static bool ImGui_Init = false;

        eglQuerySurface(dpy, surface, EGL_WIDTH, &GLWidth);
        eglQuerySurface(dpy, surface, EGL_HEIGHT, &GLHeight);

        if (!ImGui_Init) {
            IMGUI_CHECKVERSION();

            ImGui::CreateContext();

            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            io.ConfigWindowsResizeFromEdges = false;

            ImGui_ImplOpenGL3_Init("#version 300 es");
            ImGui::StyleColorsDark();

            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.ScaleAllSizes(1.0f);

            il2cpp_thread_attach(il2cpp_domain_get());

            ImGui_Init = true;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)GLWidth, (float)GLHeight);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        TickFeatures();

        ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_Once);

        if (ImGui::Begin("MCGG", nullptr)) {
            if (ImGui::BeginTabBar("##MainTabBar")) {
                if (ImGui::BeginTabItem("Info")) {
                    DrawGgcInfo();
                    ImGui::Spacing();
                    ImGui::SeparatorText("Players");

                    if (!Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr ||
                        !Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID ||
                        !Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName) {
                        ImGui::TextUnformatted("Waiting for battle data");
                    } else {
                        MonoStructures::Dictionary<uint64_t, void*>* battleManagers =
                            Originals::MCLogicBattleData_ILOGIC_GetAllBattleMgr(nullptr);

                        auto* battleManagerEntries = battleManagers ? battleManagers->entries : nullptr;

                        if (!battleManagers || battleManagers->empty() || !battleManagerEntries) {
                            ImGui::TextUnformatted("No players found");
                        } else if (ImGui::BeginTable(
                            "##EnemyPredictorTable",
                            2,
                            ImGuiTableFlags_Borders |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp |
                                ImGuiTableFlags_ScrollY,
                            ImVec2(0.0f, 300.0f)
                        )) {
                            ImGui::TableSetupColumn("Player Name");
                            ImGui::TableSetupColumn("Enemy Name");
                            ImGui::TableHeadersRow();

                            static FieldInfo* selfAccountIdField = nullptr;

                            if (!selfAccountIdField) {
                                selfAccountIdField =
                                    GetFieldInfoFromName("", "MCLogicBattleData", "m_SelfAccID");
                            }

                            uint64_t selfAccountId = GetStaticField<uint64_t>(selfAccountIdField);

                            struct PlayerInfoRow {
                                uint64_t accountId;
                                bool isSelf;
                                std::string playerName;
                                std::string sortName;
                                std::string enemyName;
                            };

                            auto normalizeName = [](const std::string& value) {
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
                            };

                            std::unordered_map<uint64_t, std::string> playerNameCache;
                            std::vector<PlayerInfoRow> playerRows;

                            int entryLimit =
                                std::min(battleManagers->count, battleManagerEntries->getCapacity());

                            playerNameCache.reserve(static_cast<size_t>(battleManagers->GetSize() * 2));
                            playerRows.reserve(static_cast<size_t>(battleManagers->GetSize()));

                            auto getPlayerName = [&playerNameCache](uint64_t accountId) -> const std::string& {
                                auto cached = playerNameCache.find(accountId);
                                if (cached != playerNameCache.end()) {
                                    return cached->second;
                                }

                                std::string playerName;

                                if (accountId != 0) {
                                    Il2CppString* managedPlayerName =
                                        Originals::MCLogicBattleData_ILOGIC_GetSelfChessPlayerName(
                                            nullptr,
                                            accountId
                                        );

                                    if (managedPlayerName) {
                                        playerName =
                                            reinterpret_cast<MonoStructures::String*>(managedPlayerName)->str();
                                    }
                                }

                                auto inserted = playerNameCache.emplace(accountId, std::move(playerName));
                                return inserted.first->second;
                            };

                            const auto* entries = battleManagerEntries->GetData();

                            for (int i = 0; entries && i < entryLimit; ++i) {
                                const auto& entry = entries[i];

                                if (entry.hashCode < 0) {
                                    continue;
                                }

                                uint64_t accountId = entry.key;

                                if (accountId == 0) {
                                    continue;
                                }

                                uint64_t enemyId = 0;
                                enemyId = Originals::MCLogicBattleData_ILOGIC_GetCurrentOpponentAccountID(
                                    nullptr,
                                    accountId
                                );

                                std::string playerName = getPlayerName(accountId);
                                std::string enemyName;

                                if (enemyId != 0) {
                                    enemyName = getPlayerName(enemyId);
                                }

                                playerRows.push_back({
                                    accountId,
                                    selfAccountId != 0 && accountId == selfAccountId,
                                    playerName,
                                    normalizeName(playerName),
                                    enemyName
                                });
                            }

                            std::sort(
                                playerRows.begin(),
                                playerRows.end(),
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

                            for (const PlayerInfoRow& row : playerRows) {
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                if (row.isSelf) {
                                    std::string playerDisplay =
                                        row.playerName.empty() ? "-" : row.playerName;
                                    playerDisplay += " (Self)";
                                    ImGui::TextUnformatted(playerDisplay.c_str());
                                } else {
                                    ImGui::TextUnformatted(
                                        row.playerName.empty() ? "-" : row.playerName.c_str()
                                    );
                                }

                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextUnformatted(row.enemyName.empty() ? "-" : row.enemyName.c_str());

                            }

                            ImGui::EndTable();
                        }
                    }
                    
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Combat")) {
                    DrawCombatTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Shop")) {
                    DrawShopTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Arena")) {
                    DrawArenaTab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        return Originals::EglSwapBuffers(dpy, surface);
    }

    // Forwards Unity touch input into ImGui mouse input.
    Touch Input_GetTouch(int index) {
        Touch ret = Originals::Input_GetTouch(index);

        if (ImGui::GetCurrentContext() != nullptr && index == 0) {
            ImGuiIO& io = ImGui::GetIO();

            float x = ret.m_Position.x;
            float y = io.DisplaySize.y - ret.m_Position.y;

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
    while (!handle.libil2cpp) {
        sleep(2);
        handle.libil2cpp = xdl_open("libil2cpp.so", XDL_DEFAULT);
    }

    while (!handle.liblogic) {
        sleep(2);
        handle.liblogic = xdl_open("liblogic.so", XDL_DEFAULT);
    }

    sleep(2);

#define DO_API(ret, name, args) \
    name = reinterpret_cast<decltype(name)>(xdl_sym(handle.libil2cpp, #name, nullptr));

#include "Il2CppVersions/api/2019.4.22f1.h"

#undef DO_API

    if (!il2cpp_domain_get || !il2cpp_thread_attach) {
        return;
    }

    Il2CppDomain* domain = il2cpp_domain_get();
    if (!domain) {
        return;
    }

    il2cpp_thread_attach(domain);

    DobbyHook(
        DobbySymbolResolver(nullptr, "eglSwapBuffers"),
        (void*)Hooks::EglSwapBuffers,
        (void**)&Originals::EglSwapBuffers
    );

    auto GetTouch_Methods =
        GetAllMethodsFromName("UnityEngine", "Input", "GetTouch", {"int"});

    if (!GetTouch_Methods.empty()) {
        DobbyHook(
            GetTouch_Methods[0],
            (void*)Hooks::Input_GetTouch,
            (void**)&Originals::Input_GetTouch
        );
    }

    ResolveFeatureBindings();
}

// Starts hook setup when this shared library is loaded in the target process.
__attribute__((constructor))
void InitLibrary() {
    if (!IsUnityProcess()) {
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
    snprintf(fullPath, sizeof(fullPath), "%s/%s", libraryPath, "libunity.so");

    env->ReleaseStringUTFChars(path, libraryPath);

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
