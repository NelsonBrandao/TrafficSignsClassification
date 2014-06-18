LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
OPENCV_LIB_TYPE:=STATIC
OPENCV_INSTALL_MODULES:=on
include ../../OpenCV-2.4.9-android-sdk/sdk/native/jni/OpenCV.mk

LOCAL_MODULE    := traffic_zigns
LOCAL_SRC_FILES := traffic_zigns.cpp
LOCAL_LDLIBS +=  -llog -ldl

include $(BUILD_SHARED_LIBRARY)
