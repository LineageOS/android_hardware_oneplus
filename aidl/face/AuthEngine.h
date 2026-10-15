/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/biometrics/face/AcquiredInfo.h>
#include <aidl/android/hardware/biometrics/face/Error.h>

#include <optional>
#include <string>
#include <vector>

#include "FaceEngine.h"
#include "ST.h"

namespace aidl::android::hardware::biometrics::face {

// The st_auth SDK (libstfaceunlockocl) shipped by newer stock apps runs detection, quality
// checks, liveness and matching on the GPU, and keeps the template itself. With the TEE enabled,
// the template is protected by the faceapp trusted app like the stock
// vendor.oneplus.faceunlock.hal service does, otherwise it is kept in /data.
class AuthEngine {
  public:
    static constexpr int kEnrollFrames = 5;

    struct Result {
        AcquiredInfo info = AcquiredInfo::UNKNOWN;
        // Enrollment: frames still needed, or -1 if unchanged.
        int remaining = -1;
        // Enrollment: the template was saved. Authentication: the face matched.
        bool done = false;
        // Authentication: the frame was matched against the template.
        bool compared = false;
        std::optional<Error> error;
    };

    AuthEngine();
    ~AuthEngine();

    // Whether the resources ship this SDK rather than the one FaceEngine drives.
    bool available();

    // Identifies the template format, as templates made in one mode can't be used in the other.
    std::string name() const;

    bool init();

    std::optional<Error> beginEnroll(int orientation);
    Result enroll(const Frame& frame);

    std::optional<Error> beginAuthenticate(int orientation, bool requireAttention);
    Result authenticate(const Frame& frame);

    bool removeTemplate();

  private:
    static AcquiredInfo toAcquiredInfo(int status);

    bool loadSymbols();
    bool initLocked();
    void deinit();
    std::vector<int> faceIds();
    const char* templateDir() const;

    const bool mUseTee;
    std::optional<bool> mAvailable;
    bool mSymbolsLoaded = false;
    bool mInitialized = false;
    void* mLib = nullptr;

    int mOrientation = st::CV_FACE_RIGHT;
    int mConfig = 0;
    int mAccepted = 0;
    std::vector<int> mFaceIds;

#define ST_FN(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr
    ST_FN(int, initLicense, const char*);
    ST_FN(void, openLog, int);
    ST_FN(const char*, getVersion);
    ST_FN(int, setTeeCommunicationFunc, int (*)(void*, int, void*, int));
    ST_FN(int, authInit, const char*, int);
    ST_FN(void, authDeinit);
    ST_FN(int, authReset);
    ST_FN(int, setRoi, const st::st_rect_t*);
    ST_FN(int, setFaceOrientation, int);
    ST_FN(int, setImageNumber, int);
    ST_FN(int, authEnroll, const st::st_image_t*, int, int*);
    ST_FN(int, authUnlock, const st::st_image_t*, int, const int*, int);
    ST_FN(int, deleteFace, int);
    ST_FN(int, deleteAllFaces);
    ST_FN(int, getTemplateInfo, st::st_template_info_t**);
    ST_FN(void, releaseTemplateInfo, st::st_template_info_t*);
#undef ST_FN
};

}  // namespace aidl::android::hardware::biometrics::face
