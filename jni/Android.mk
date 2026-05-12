LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := dobby

LOCAL_SRC_FILES := $(LOCAL_PATH)/dobby/lib/$(TARGET_ARCH_ABI)/libdobby.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_CFLAGS := \
    -O0 \
    -g0 \
    -w \
    -Wno-everything \
    -Wno-error \
    -Wformat \

LOCAL_CPPFLAGS := \
    $(LOCAL_CFLAGS) \
    -std=c++26

LOCAL_LDFLAGS :=

LOCAL_LDLIBS := \
    -llog \
    -landroid \
    -lEGL \
    -lGLESv3 \
    -lz

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/xDL/xdl/src/main/cpp/include/ \
    $(LOCAL_PATH)/xDL/xdl/src/main/cpp/ \
    $(LOCAL_PATH)/imgui/ \
    $(LOCAL_PATH)/imgui/backends/ \
    $(LOCAL_PATH)/imgui/misc/cpp/

LOCAL_SRC_FILES := \
    $(wildcard $(LOCAL_PATH)/xDL/xdl/src/main/cpp/*.c*) \
    $(wildcard $(LOCAL_PATH)/imgui/*.c*) \
    $(LOCAL_PATH)/imgui/backends/imgui_impl_android.cpp \
    $(LOCAL_PATH)/imgui/backends/imgui_impl_opengl3.cpp \
    $(LOCAL_PATH)/imgui/misc/cpp/imgui_stdlib.cpp \
    $(wildcard $(LOCAL_PATH)/*.c*)

LOCAL_STATIC_LIBRARIES := \
    dobby

include $(BUILD_SHARED_LIBRARY)
