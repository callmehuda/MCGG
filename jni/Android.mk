LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := dobby

LOCAL_SRC_FILES := $(LOCAL_PATH)/dobby/lib/$(TARGET_ARCH_ABI)/libdobby.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := crypto

LOCAL_SRC_FILES := $(LOCAL_PATH)/../obj/openssl-install/lib/libcrypto.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := ssl

LOCAL_SRC_FILES := $(LOCAL_PATH)/../obj/openssl-install/lib/libssl.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := psl

LOCAL_SRC_FILES := $(LOCAL_PATH)/../obj/libpsl-install/lib/libpsl.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := curl

LOCAL_SRC_FILES := $(LOCAL_PATH)/../obj/curl-install/lib/libcurl.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := main

MCGG_BUILD_REPOSITORY ?= Yan-0001/MCGG
MCGG_BUILD_VERSION ?= $(shell git -C $(LOCAL_PATH)/.. describe --tags --abbrev=0 --match 'v*' 2>/dev/null || echo unknown)
MCGG_BUILD_COMMIT ?= $(shell git -C $(LOCAL_PATH)/.. rev-parse HEAD 2>/dev/null || echo unknown)
MCGG_BUILD_REF ?= $(shell git -C $(LOCAL_PATH)/.. rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)

LOCAL_CFLAGS := \
    -Oz \
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
    -DCURL_STATICLIB \
    -DMCGG_BUILD_REPOSITORY=\"$(MCGG_BUILD_REPOSITORY)\" \
    -DMCGG_BUILD_VERSION=\"$(MCGG_BUILD_VERSION)\" \
    -DMCGG_BUILD_COMMIT=\"$(MCGG_BUILD_COMMIT)\" \
    -DMCGG_BUILD_REF=\"$(MCGG_BUILD_REF)\" \
    -DUNITY_VERSION_MAJOR=2019 \
    -DUNITY_VERSION_MINOR=4 \
    -DUNITY_VERSION_PATCH=33 \
    -DUNITY_VER=194

ifeq ($(NDK_DEBUG),1)
LOCAL_CFLAGS += -O0
endif

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
    -ldl \
    -lz \
    -landroid \
    -lEGL \
    -lGLESv3

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../obj/curl-install/include/ \
    $(LOCAL_PATH)/json/single_include/ \
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
    curl \
    psl \
    ssl \
    crypto \
    dobby

include $(BUILD_SHARED_LIBRARY)
