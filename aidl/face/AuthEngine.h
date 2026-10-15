/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/biometrics/face/AcquiredInfo.h>
#include <aidl/android/hardware/biometrics/face/Error.h>

#include <optional>
#include <string>

#include "FaceEngine.h"
#include "ST.h"

namespace aidl::android::hardware::biometrics::face {

// Newer stock apps ship the st_auth SDK, which runs the whole pipeline and keeps the template.
class AuthEngine {
  public:
    static constexpr int kEnrollFrames = 5;

    struct Result {
        AcquiredInfo info = AcquiredInfo::UNKNOWN;
        int remaining = -1;
        bool done = false;
        bool compared = false;
        std::optional<Error> error;
    };

    AuthEngine();
    ~AuthEngine();

    bool available();
    std::string name() const;
    bool init();

    std::optional<Error> beginEnroll(int orientation);
    Result enroll(const Frame& frame);
    std::optional<Error> beginAuthenticate(int orientation, bool requireAttention);
    Result authenticate(const Frame& frame);
    bool removeTemplate();

  private:
    bool loadSymbols();
    bool initLocked();
    void deinit();

    const bool mUseTee;
    std::optional<bool> mAvailable;
    bool mInitialized = false;
    void* mLib = nullptr;
    int mConfig = 0;
    int mAccepted = 0;
    int mFaceId = -1;

#define ST_FN(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr
    ST_FN(int, initLicense, const char*);
    ST_FN(int, setTeeCommunicationFunc, int (*)(void*, int, void*, int));
    ST_FN(int, authInit, const char*, int);
    ST_FN(void, authDeinit);
    ST_FN(int, authReset);
    ST_FN(int, setFaceOrientation, int);
    ST_FN(int, setImageNumber, int);
    ST_FN(int, authEnroll, const st::st_image_t*, int, int*);
    ST_FN(int, authUnlock, const st::st_image_t*, int, const int*, int);
    ST_FN(int, deleteAllFaces);
#undef ST_FN
};

}  // namespace aidl::android::hardware::biometrics::face
