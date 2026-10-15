/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "ST.h"

namespace aidl::android::hardware::biometrics::face {

struct DetectedFace {
    st::cv_face_t face;
    std::vector<st::cv_point3f_t> points;
    std::vector<uint8_t> occlusion;
    int orientation;

    // Returns the face with its landmark pointers referring to this object.
    st::cv_face_t get();
};

struct Frame {
    const uint8_t* nv21;
    int width;
    int height;
};

class FaceEngine {
  public:
    enum class Quality {
        GOOD,
        INSUFFICIENT,
        TOO_DARK,
        TOO_BRIGHT,
        POOR_GAZE,
    };

    ~FaceEngine();

    // The ST license is bound to the stock app, which the SDK identifies by the process cmdline.
    static void setCmdline(char* cmdline, size_t size);
    // Runs fn while the stock app's cmdline is presented.
    static bool withStockCmdline(const std::function<bool()>& fn);

    bool init();

    // Detects the largest face. The 640w detector finds faces further away at a higher cost.
    bool detect(const Frame& frame, int orientation, bool large, bool tryOtherOrientations,
                DetectedFace* out);
    Quality checkQuality(const Frame& frame, DetectedFace& face);
    bool eyesOpen(const Frame& frame, const DetectedFace& face);
    float spoofScore(const Frame& frame, DetectedFace& face);
    void resetLiveness();
    std::vector<uint8_t> extractFeature(const Frame& frame, DetectedFace& face);

    // Compares two serialized features and returns their similarity in [0, 1], or -1 on error.
    float compareFeatures(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

  private:
    bool loadSymbols();
    bool initLocked();

    static char* sCmdline;
    static size_t sCmdlineSize;

    void* mLib = nullptr;
    void* mSnpeLib = nullptr;
    st::cv_handle_t mDetector = nullptr;
    st::cv_handle_t mDetector640 = nullptr;
    st::cv_handle_t mVerify = nullptr;
    st::cv_handle_t mHackness = nullptr;
    st::cv_handle_t mOcular = nullptr;

#define ST_FN(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr
    ST_FN(int, initLicense, const char*);
    ST_FN(int, initLicenseSnpe, const char*);
    ST_FN(int, createDetector, st::cv_handle_t*, const char*, const char*, unsigned int);
    ST_FN(void, destroyDetector, st::cv_handle_t);
    ST_FN(int, detectFaces, st::cv_handle_t, const uint8_t*, int, int, int, int, int,
          st::cv_face_t**, int*, void*);
    ST_FN(void, releaseFaces, st::cv_face_t*, int);
    ST_FN(int, qualityDetect, const st::cv_quality_filter_t*, int, const uint8_t*, int, int, int,
          int, const st::cv_face_t*, float*, int*);
    ST_FN(int, createOcular, st::cv_handle_t*, const char*, const char*);
    ST_FN(void, destroyOcular, st::cv_handle_t);
    ST_FN(int, detectOcular, st::cv_handle_t, const uint8_t*, int, int, int, int, st::cv_rect_t,
          float*, int);
    ST_FN(int, createVerify, st::cv_handle_t*, const char*);
    ST_FN(void, destroyVerify, st::cv_handle_t);
    ST_FN(int, getFeature, st::cv_handle_t, const uint8_t*, int, int, int, int,
          const st::cv_landmarks_t*, st::cv_feature_t**, unsigned int*);
    ST_FN(int, serializeFeature, const st::cv_feature_t*, char*);
    ST_FN(st::cv_feature_t*, deserializeFeature, const char*);
    ST_FN(int, compareFeature, st::cv_handle_t, st::cv_feature_t*, st::cv_feature_t*, float*);
    ST_FN(void, releaseFeature, st::cv_feature_t*);
    ST_FN(int, createHackness, st::cv_handle_t*, const char*);
    ST_FN(void, destroyHackness, st::cv_handle_t);
    ST_FN(int, resetHackness, st::cv_handle_t);
    ST_FN(int, detectHackness, st::cv_handle_t, const uint8_t*, int, int, int, int,
          const st::cv_landmarks_t*, float*, int);
#undef ST_FN
};

}  // namespace aidl::android::hardware::biometrics::face
