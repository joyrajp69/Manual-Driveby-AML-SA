LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := ManualDrivebySA
LOCAL_SRC_FILES := main.cpp mod/config.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CXXFLAGS := -O2 -Wall -fvisibility=hidden -std=c++17
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
