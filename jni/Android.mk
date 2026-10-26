LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE     := ManualDriveby

# Include AML SDK source implementations alongside main.cpp
LOCAL_SRC_FILES  := main.cpp \
                    include/mod/logger.cpp \
                    include/mod/config.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_LDLIBS     := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
