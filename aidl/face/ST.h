/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

// Subset of the ST face SDK ABI shipped with OPFaceUnlock.

namespace aidl::android::hardware::biometrics::face::st {

typedef void* cv_handle_t;

enum cv_pixel_format { CV_PIX_FMT_NV12 = 2, CV_PIX_FMT_NV21 = 3 };

enum cv_face_orientation {
    CV_FACE_UP = 1,
    CV_FACE_LEFT = 2,
    CV_FACE_DOWN = 4,
    CV_FACE_RIGHT = 8,
};

enum {
    CV_DETECT_RESIZE_640W = 4,
    CV_DETECT_RESIZE_427W = 16,
    CV_DETECT_106_POINTS = 512,
};

enum cv_quality_type {
    CV_QUALITY_YAW = 1,
    CV_QUALITY_PITCH = 2,
    CV_QUALITY_LOCATION = 4,
    CV_QUALITY_SIZE = 8,
    CV_QUALITY_OCCLUSION = 16,
    CV_QUALITY_BRIGHTNESS = 32,
    CV_QUALITY_SHARPNESS = 64,
    CV_QUALITY_MOUTH = 128,
};

struct cv_rect_t {
    int left, top, right, bottom;
};

struct cv_point3f_t {
    float x, y, z;
};

struct cv_landmarks_t {
    cv_point3f_t* points;
    uint8_t* occlusion;
    int count;
    uint8_t reserved[36];
};
static_assert(sizeof(cv_landmarks_t) == 56);

struct cv_face_t {
    cv_rect_t rect;
    float score;
    float yaw;
    float pitch;
    float roll;
    float eye_dist;
    int id;
    cv_landmarks_t landmarks;
};
static_assert(sizeof(cv_face_t) == 96);

struct cv_quality_filter_t {
    int type;
    float min;
    float max;
};

struct cv_feature_t {
    uint64_t reserved;
    int length;
};

}  // namespace aidl::android::hardware::biometrics::face::st
