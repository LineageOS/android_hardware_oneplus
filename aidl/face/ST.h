/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

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

// st_auth SDK (libstfaceunlockocl), which runs the whole pipeline and keeps the template itself.

struct st_image_t {
    const uint8_t* data;
    int format;
    int width;
    int height;
    int stride;
};
static_assert(sizeof(st_image_t) == 24);

struct st_rect_t {
    int left, top, right, bottom;
};

struct st_face_id_info_t {
    int face_id;
    int feature_count;
    uint8_t reserved[8];
};
static_assert(sizeof(st_face_id_info_t) == 16);

struct st_template_info_t {
    int count;
    int reserved;
    st_face_id_info_t* faces;
};

enum st_auth_config {
    ST_AUTH_CLOSE_EYE_STATUS = 1,
    ST_AUTH_CLOSE_GAZE = 2,
    ST_AUTH_CLOSE_MOUTH_BLOCK = 4,
    ST_AUTH_CLOSE_HACKNESS = 8,
    ST_AUTH_CLOSE_EYE_BLOCK = 16,
};

enum st_auth_status {
    ST_E_HANDLE = -2,
    ST_E_TEMPLATE_DAMAGED = -1001,
    ST_E_TEMPLATE_UPPER_LIMIT = -1008,
    ST_AUTH_OK = 0,
    ST_AUTH_NO_FACE = 1,
    ST_AUTH_HACKNESS_FAILED = 2,
    ST_AUTH_EYE_CLOSE = 3,
    ST_AUTH_SELECTOR_FAILED = 4,
    ST_AUTH_ENROLL_KEEP = 5,
    ST_AUTH_ENROLL_NOT_SAME_PERSON_OLD = 6,
    ST_AUTH_OVER_EXPOSE = 7,
    ST_AUTH_ENROLL_OK = 101,
    ST_AUTH_FRAME_SELECT_FAILED = 102,
    ST_AUTH_EYE_BLOCKED = 103,
    ST_AUTH_MOUTH_BLOCKED = 104,
    ST_AUTH_NOT_IN_ROI = 105,
    ST_AUTH_ENROLL_NOT_SAME_PERSON = 106,
    ST_AUTH_FACE_UP = 113,
    ST_AUTH_FACE_DOWN = 114,
    ST_AUTH_FACE_LEFT = 115,
    ST_AUTH_FACE_RIGHT = 116,
    ST_AUTH_QUALITY_SHARP = 117,
    ST_AUTH_QUALITY_MOUTH = 118,
    ST_AUTH_QUALITY_DISTANCE = 119,
    ST_AUTH_QUALITY_FACE_SMALL = 120,
    ST_AUTH_QUALITY_FACE_BIG = 121,
    ST_AUTH_QUALITY_OCCLUSION = 122,
    ST_AUTH_QUALITY_BRIGHT_LOW = 123,
    ST_AUTH_QUALITY_BRIGHT_HIGH = 124,
    ST_AUTH_QUALITY_FACE_MISS = 125,
    ST_AUTH_ENROLL_WRONG_ORIENTATION = 126,
    ST_AUTH_ENROLL_OUT_OF_FRAME = 127,
    ST_AUTH_UNLOCK_OK = 201,
    ST_AUTH_UNLOCK_FAILED = 202,
    ST_AUTH_TEMPLATE_NOT_EXIST = 203,
    ST_AUTH_TEMPLATE_NO_FACE = 204,
};

}  // namespace aidl::android::hardware::biometrics::face::st
