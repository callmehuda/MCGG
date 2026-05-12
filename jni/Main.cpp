#include <jni.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Il2CppVersions/headers/2019.4.22f1.h"
#include "unity/UnityStructures.hpp"
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

struct {
    void* libil2cpp = nullptr;
    void* liblogic = nullptr;
} handle;

struct PlayersData {
    int PlayerID = 0;
    std::string PlayerName = "";
    int EnemyID = 0;
    std::string EnemyName = "";
};

int GLWidth = 0;
int GLHeight = 0;

void* UnityLibraryHandle = nullptr;

std::unordered_map<std::string, std::vector<MethodInfo*>> MultiMethodCache;

namespace Originals {
    EGLBoolean (*EglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
    Touch (*Input_GetTouch)(int index);
}

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

std::string GenerateCacheKey(
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    std::string key =
        (ns ? std::string(ns) : "") +
        "::" +
        className +
        "::" +
        methodName +
        "(";

    for (size_t i = 0; i < paramTypes.size(); ++i) {
        key += paramTypes[i];

        if (i < paramTypes.size() - 1) {
            key += ",";
        }
    }

    key += ")";
    return key;
}

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

std::vector<MethodInfo*> GetAllMethodInfosFromName(
    const char* ns,
    const char* className,
    const char* methodName,
    const std::vector<const char*>& paramTypes
) {
    std::string cacheKey = GenerateCacheKey(ns, className, methodName, paramTypes);

    auto cached = MultiMethodCache.find(cacheKey);
    if (cached != MultiMethodCache.end()) {
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

namespace Hooks {
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

        ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_Once);

        if (ImGui::Begin("MCGG", nullptr)) {
            if (ImGui::BeginTabBar("##MainTabBar")) {
                if (ImGui::BeginTabItem("Info")) {
                    ImGui::SeparatorText("Players");
                    
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

void SetupThread() {
    while (!handle.libil2cpp) {
        sleep(2);
        handle.liblogic = xdl_open("libil2cpp", XDL_DEFAULT);
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
}

__attribute__((constructor))
void InitLibrary() {
    if (!IsUnityProcess()) {
        return;
    }

    std::thread(SetupThread).detach();
}

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

jboolean UnloadOriginalLibrary(JNIEnv*, jclass) {
    if (UnityLibraryHandle) {
        dlclose(UnityLibraryHandle);
        UnityLibraryHandle = nullptr;
    }

    return JNI_TRUE;
}

extern "C" __attribute__((used, visibility("default")))
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* key) {
    if (!vm) {
        return JNI_VERSION_1_6;
    }

    JNIEnv* env = nullptr;

    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
        return JNI_VERSION_1_6;
    }

    if (key == (void*)1337) {
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
