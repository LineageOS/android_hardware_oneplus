/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.biometrics.face-service.oneplus"

#include "FaceTee.h"

#include <android-base/logging.h>
#include <dlfcn.h>
#include <unistd.h>

#include <cstring>

namespace aidl::android::hardware::biometrics::face {

namespace {

// faceapp lives in /vendor/etc/faceunlock on sm8150/sm8250/sm8350, or the firmware partition on
// sdm845.
constexpr const char* kAppPaths[] = {"/vendor/etc/faceunlock", "/vendor/firmware_mnt/image"};
constexpr const char* kAppName = "faceapp";
constexpr uint32_t kSharedBufferSize = 0xc8000;

enum Command : uint32_t {
    SAVE_FEATURE = 1,
    COMPARE_FEATURE = 2,
    REMOVE_FEATURE = 3,
    SAVE_FRAME = 4,
    REMOVE_FRAME = 6,
};

struct Request {
    uint32_t cmd;
    uint32_t size;
    uint32_t index;
    uint8_t flags;
    uint8_t reserved[3];
    float thresholds[8];
    uint8_t data[];
};
static_assert(sizeof(Request) == 48);

struct Response {
    float score;
    uint32_t result;
};

// Maps the raw similarity of feature version 31004 onto [0, 1].
constexpr float kThresholds[8] = {-1.0f, 0.172f, 0.268f, 0.342f, 0.405f, 0.49f, 0.55f, 1.0f};

constexpr size_t kHeaderSize = 64;

// Responses follow the request payload, both are 64 byte aligned.
constexpr size_t requestSize(size_t payload) {
    return (payload + sizeof(Request) + kHeaderSize) & ~(kHeaderSize - 1);
}

}  // namespace

FaceTee::~FaceTee() {
    if (mFeatureHandle) mShutdownApp(&mFeatureHandle);
    if (mFrameHandle) mShutdownApp(&mFrameHandle);
}

bool FaceTee::load() {
    if (mLib) return true;

    mLib = dlopen("libQSEEComAPI.so", RTLD_NOW);
    if (!mLib) {
        LOG(ERROR) << "Failed to load libQSEEComAPI: " << dlerror();
        return false;
    }
    mStartApp = reinterpret_cast<decltype(mStartApp)>(dlsym(mLib, "QSEECom_start_app"));
    mShutdownApp = reinterpret_cast<decltype(mShutdownApp)>(dlsym(mLib, "QSEECom_shutdown_app"));
    mSendCmd = reinterpret_cast<decltype(mSendCmd)>(dlsym(mLib, "QSEECom_send_cmd"));
    if (!mStartApp || !mShutdownApp || !mSendCmd) {
        LOG(ERROR) << "Missing QSEECom symbols";
        dlclose(mLib);
        mLib = nullptr;
        return false;
    }
    return true;
}

bool FaceTee::start(QSEECom_handle** handle) {
    if (*handle) return true;

    // Loading the trusted app occasionally fails with a transient scm_call error when the secure
    // app region is briefly contended, and the TA lives in a different directory depending on the
    // device, so retry across both candidate paths before giving up.
    constexpr int kMaxAttempts = 5;
    for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
        for (const char* path : kAppPaths) {
            int ret = mStartApp(handle, path, kAppName, kSharedBufferSize);
            if (ret == 0 && *handle && (*handle)->ion_sbuffer) return true;

            LOG(WARNING) << "Failed to start " << kAppName << " from " << path << " (attempt "
                         << attempt << "): " << ret;
            if (*handle) {
                mShutdownApp(handle);
                *handle = nullptr;
            }
        }
        if (attempt < kMaxAttempts) usleep(50000);
    }
    LOG(ERROR) << "Giving up starting " << kAppName;
    return false;
}

bool FaceTee::open() {
    return load() && start(&mFeatureHandle) && start(&mFrameHandle);
}

uint32_t FaceTee::send(QSEECom_handle* handle, size_t reqSize, size_t rspSize) {
    uint8_t* buf = handle->ion_sbuffer;
    auto* rsp = reinterpret_cast<Response*>(buf + reqSize);
    rsp->result = 9;

    uint32_t cmd = reinterpret_cast<Request*>(buf)->cmd;
    int ret = mSendCmd(handle, buf, reqSize, rsp, rspSize);
    if (ret != 0) {
        LOG(ERROR) << "Command " << cmd << " failed to send: " << ret;
        return ret;
    }
    // Remove commands return non-zero when the slot is empty, which callers treat as benign.
    if (rsp->result != 0) LOG(DEBUG) << "Command " << cmd << " returned " << rsp->result;
    return rsp->result;
}

bool FaceTee::saveFeature(const uint8_t* feature, size_t size) {
    if (!open()) return false;

    auto* req = reinterpret_cast<Request*>(mFeatureHandle->ion_sbuffer);
    memset(req, 0, sizeof(*req));
    req->flags = 1;  // select the /data/vendor/misc/qsee/face storage paths
    req->cmd = SAVE_FEATURE;
    req->size = size;
    memcpy(req->data, feature, size);
    return send(mFeatureHandle, requestSize(size), kHeaderSize) == 0;
}

bool FaceTee::removeFeature() {
    if (!open()) return false;

    auto* req = reinterpret_cast<Request*>(mFeatureHandle->ion_sbuffer);
    memset(req, 0, sizeof(*req));
    req->flags = 1;  // select the /data/vendor/misc/qsee/face storage paths
    req->cmd = REMOVE_FEATURE;
    return send(mFeatureHandle, kHeaderSize, kHeaderSize) == 0;
}

std::optional<float> FaceTee::compare(const uint8_t* feature, size_t size) {
    if (!open()) return std::nullopt;

    auto* req = reinterpret_cast<Request*>(mFeatureHandle->ion_sbuffer);
    memset(req, 0, sizeof(*req));
    req->flags = 1;  // select the /data/vendor/misc/qsee/face storage paths
    req->cmd = COMPARE_FEATURE;
    req->size = size;
    memcpy(req->thresholds, kThresholds, sizeof(kThresholds));
    memcpy(req->data, feature, size);

    size_t reqSize = requestSize(size);
    if (send(mFeatureHandle, reqSize, kHeaderSize) != 0) return std::nullopt;
    return reinterpret_cast<Response*>(mFeatureHandle->ion_sbuffer + reqSize)->score;
}

bool FaceTee::saveFrame(int index, const uint8_t* nv21) {
    if (!open()) return false;

    auto* req = reinterpret_cast<Request*>(mFrameHandle->ion_sbuffer);
    memset(req, 0, sizeof(*req));
    req->flags = 1;  // select the /data/vendor/misc/qsee/face storage paths
    req->cmd = SAVE_FRAME;
    req->size = kFrameSize;
    req->index = index;
    memcpy(req->data, nv21, kFrameSize);
    return send(mFrameHandle, requestSize(kFrameSize), kHeaderSize) == 0;
}

bool FaceTee::removeFrame(int index) {
    if (!open()) return false;

    auto* req = reinterpret_cast<Request*>(mFrameHandle->ion_sbuffer);
    memset(req, 0, sizeof(*req));
    req->flags = 1;  // select the /data/vendor/misc/qsee/face storage paths
    req->cmd = REMOVE_FRAME;
    req->index = index;
    return send(mFrameHandle, kHeaderSize, kHeaderSize) == 0;
}

}  // namespace aidl::android::hardware::biometrics::face
