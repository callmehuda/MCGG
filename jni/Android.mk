LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := dobby

LOCAL_SRC_FILES := $(LOCAL_PATH)/dobby/lib/$(TARGET_ARCH_ABI)/libdobby.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_CFLAGS := \
    -O0 \
    -DNDEBUG \
    -ffunction-sections \
    -fdata-sections \
    -fvisibility=hidden \
    -w \
    -Wno-everything \
    -Wno-error \
    -DIMGUI_DISABLE_DEMO_WINDOWS \
    -DIMGUI_DISABLE_DEBUG_TOOLS \
    -DIMGUI_USE_WCHAR32 \
    -DUNITY_VERSION_MAJOR=2019 \
    -DUNITY_VERSION_MINOR=4 \
    -DUNITY_VERSION_PATCH=22 \
    -DUNITY_VER=194

LOCAL_CPPFLAGS := \
    $(LOCAL_CFLAGS) \
    -std=c++26 \
    -fvisibility-inlines-hidden \
    -fno-exceptions \
    -fno-rtti

LOCAL_LDFLAGS := \
    -Wl,--gc-sections \
    -Wl,--strip-all \
    -Wl,--exclude-libs,ALL

LOCAL_LDLIBS := \
    -llog \
    -landroid \
    -lEGL \
    -lGLESv3

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/xDL/xdl/src/main/cpp/include/ \
    $(LOCAL_PATH)/xDL/xdl/src/main/cpp/ \
    $(LOCAL_PATH)/imgui/ \
    $(LOCAL_PATH)/imgui/backends/ \
    $(LOCAL_PATH)/imgui/misc/cpp/

LOCAL_SRC_FILES := \
    $(wildcard $(LOCAL_PATH)/xDL/xdl/src/main/cpp/*.c*) \
    $(LOCAL_PATH)/imgui/imgui.cpp \
    $(LOCAL_PATH)/imgui/imgui_draw.cpp \
    $(LOCAL_PATH)/imgui/imgui_tables.cpp \
    $(LOCAL_PATH)/imgui/imgui_widgets.cpp \
    $(LOCAL_PATH)/imgui/backends/imgui_impl_android.cpp \
    $(LOCAL_PATH)/imgui/backends/imgui_impl_opengl3.cpp \
    $(LOCAL_PATH)/imgui/misc/cpp/imgui_stdlib.cpp \
    $(wildcard $(LOCAL_PATH)/*.c*)

LOCAL_STATIC_LIBRARIES := \
    dobby

include $(BUILD_SHARED_LIBRARY)
