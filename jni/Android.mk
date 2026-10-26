LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := ManualDrivebySA

# Automatically include mod/logger.cpp and mod/config.cpp if present in jni/mod/
MOD_CPP_FILES := $(wildcard $(LOCAL_PATH)/mod/*.cpp)
LOCAL_SRC_FILES := main.cpp $(MOD_CPP_FILES:$(LOCAL_PATH)/%=%)

LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/mod
LOCAL_CXXFLAGS := -O2 -Wall -Wno-unused-function -Wno-unused-variable -fvisibility=hidden -std=c++17 -fexceptions
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
