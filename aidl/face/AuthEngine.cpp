/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.biometrics.face-service.oneplus"

#include "AuthEngine.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <dlfcn.h>
#include <unistd.h>
#include <ziparchive/zip_archive.h>

#include <algorithm>

#include "AuthTee.h"

namespace aidl::android::hardware::biometrics::face {

using namespace st;

namespace {

constexpr const char* kResources = "/vendor/etc/faceunlock/resources.zip";
constexpr const char* kVendorLibraries[] = {"/vendor/lib64/libstfaceunlockocl.so",
                                            "/odm/lib64/libstfaceunlockocl.so"};
constexpr const char* kLibraryEntry = "lib/arm64-v8a/libstfaceunlockocl.so";
constexpr const char* kLicenses[] = {"res/raw/st_license_3_2.lic", "res/raw/st_license_3_1.lic"};
constexpr const char* kTemplateDir = "/data/vendor/face/st_auth";
constexpr const char* kTeeTemplateDir = "/data/vendor/misc/qsee/face";
constexpr const char* kFaceIdFile = "/data/vendor/face/st_auth_face_id";

constexpr int kPixelFormatNv21 = 3;
constexpr int kAuthConfig = ST_AUTH_CLOSE_EYE_BLOCK | ST_AUTH_CLOSE_MOUTH_BLOCK;

bool readEntry(const char* name, std::string* out) {
    ZipArchiveHandle archive = nullptr;
    ZipEntry64 entry;
    bool ok = OpenArchive(kResources, &archive) == 0 && FindEntry(archive, name, &entry) == 0;
    if (ok && out) {
        out->resize(entry.uncompressed_length);
        ok = ExtractToMemory(archive, &entry, reinterpret_cast<uint8_t*>(out->data()),
                             out->size()) == 0;
    }
    CloseArchive(archive);
    return ok;
}

std::optional<std::string> engineLibraryPath() {
    for (const char* lib : kVendorLibraries) {
        if (access(lib, F_OK) == 0) return std::string(lib);
    }
    if (readEntry(kLibraryEntry, nullptr)) return std::string(kResources) + "!/" + kLibraryEntry;
    return std::nullopt;
}

st_image_t toImage(const Frame& frame) {
    return {frame.nv21, kPixelFormatNv21, frame.width, frame.height, frame.width};
}

AcquiredInfo toAcquiredInfo(int status) {
    switch (status) {
        case ST_AUTH_OK:
        case ST_AUTH_ENROLL_KEEP:
        case ST_AUTH_ENROLL_OK:
        case ST_AUTH_UNLOCK_OK:
        case ST_AUTH_UNLOCK_FAILED:
            return AcquiredInfo::GOOD;
        case ST_AUTH_NO_FACE:
        case ST_AUTH_QUALITY_FACE_MISS:
            return AcquiredInfo::NOT_DETECTED;
        case ST_AUTH_EYE_CLOSE:
            return AcquiredInfo::POOR_GAZE;
        case ST_AUTH_OVER_EXPOSE:
        case ST_AUTH_QUALITY_BRIGHT_HIGH:
            return AcquiredInfo::TOO_BRIGHT;
        case ST_AUTH_QUALITY_BRIGHT_LOW:
            return AcquiredInfo::TOO_DARK;
        case ST_AUTH_EYE_BLOCKED:
        case ST_AUTH_MOUTH_BLOCKED:
        case ST_AUTH_QUALITY_MOUTH:
        case ST_AUTH_QUALITY_OCCLUSION:
            return AcquiredInfo::FACE_OBSCURED;
        case ST_AUTH_FACE_UP:
        case ST_AUTH_FACE_DOWN:
            return AcquiredInfo::TILT_TOO_EXTREME;
        case ST_AUTH_FACE_LEFT:
        case ST_AUTH_FACE_RIGHT:
            return AcquiredInfo::PAN_TOO_EXTREME;
        case ST_AUTH_QUALITY_SHARP:
            return AcquiredInfo::TOO_MUCH_MOTION;
        case ST_AUTH_QUALITY_DISTANCE:
        case ST_AUTH_QUALITY_FACE_SMALL:
            return AcquiredInfo::TOO_FAR;
        case ST_AUTH_QUALITY_FACE_BIG:
            return AcquiredInfo::TOO_CLOSE;
        default:
            return AcquiredInfo::INSUFFICIENT;
    }
}

}  // namespace

AuthEngine::AuthEngine()
    : mUseTee(::android::base::GetBoolProperty("persist.vendor.faceunlock.tee", true)) {}

AuthEngine::~AuthEngine() {
    deinit();
}

bool AuthEngine::available() {
    if (!mAvailable) mAvailable = engineLibraryPath().has_value();
    return *mAvailable;
}

std::string AuthEngine::name() const {
    return mUseTee ? "st_auth_tee" : "st_auth";
}

bool AuthEngine::loadSymbols() {
    if (mLib) return true;

    auto path = engineLibraryPath();
    if (!path) {
        LOG(ERROR) << "Engine library not found";
        return false;
    }
    void* lib = dlopen(path->c_str(), RTLD_NOW);
    if (!lib) {
        LOG(ERROR) << "Failed to load " << *path << ": " << dlerror();
        return false;
    }

    bool ok = true;
    auto load = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
        if (!fn) {
            LOG(ERROR) << "Missing symbol " << name;
            ok = false;
        }
    };
    load(initLicense, "st_face_init_license");
    load(setTeeCommunicationFunc, "st_auth_set_tee_communication_func");
    load(authInit, "st_auth_init");
    load(authDeinit, "st_auth_deinit");
    load(authReset, "st_auth_reset");
    load(setFaceOrientation, "st_auth_set_face_orientation");
    load(setImageNumber, "st_auth_set_faceid_image_number");
    load(authEnroll, "st_auth_enroll");
    load(authUnlock, "st_auth_unlock");
    load(deleteAllFaces, "st_auth_delete_all_face");
    if (ok) mLib = lib;
    return ok;
}

bool AuthEngine::init() {
    if (mInitialized) return true;
    if (!loadSymbols()) return false;

    mInitialized = FaceEngine::withStockCmdline([this] { return initLocked(); });
    if (!mInitialized) deinit();
    return mInitialized;
}

bool AuthEngine::initLocked() {
    int ret = -1;
    std::string license;
    for (const char* entry : kLicenses) {
        if (!readEntry(entry, &license)) continue;
        ret = initLicense(license.c_str());
        if (ret == 0) break;
        LOG(WARNING) << "License " << entry << " rejected: " << ret;
    }
    if (ret != 0) {
        LOG(ERROR) << "No usable st_auth license";
        return false;
    }

    if (mUseTee &&
        (!AuthTee::open() || (ret = setTeeCommunicationFunc(AuthTee::communicate)) != 0)) {
        LOG(ERROR) << "Failed to set up TEE: " << ret;
        return false;
    }

    ret = authInit(mUseTee ? kTeeTemplateDir : kTemplateDir, 0);
    if (ret != 0) {
        LOG(ERROR) << "Failed to initialize st_auth: " << ret;
        return false;
    }
    return true;
}

void AuthEngine::deinit() {
    if (mLib) authDeinit();
    if (mUseTee) AuthTee::close();
    mInitialized = false;
}

std::optional<Error> AuthEngine::beginEnroll(int orientation) {
    mAccepted = 0;
    authReset();
    deleteAllFaces();
    unlink(kFaceIdFile);

    int ret;
    if ((ret = setImageNumber(kEnrollFrames)) != 0 ||
        (ret = setFaceOrientation(orientation)) != 0) {
        LOG(ERROR) << "Failed to configure enrollment: " << ret;
        return Error::UNABLE_TO_PROCESS;
    }
    return std::nullopt;
}

AuthEngine::Result AuthEngine::enroll(const Frame& frame) {
    st_image_t image = toImage(frame);
    int faceId = -1;
    int status = authEnroll(&image, 0, &faceId);

    Result result{.info = toAcquiredInfo(status)};
    switch (status) {
        case ST_AUTH_OK:
        case ST_AUTH_ENROLL_OK:
            // The stock service matches against the face id returned here.
            if (!::android::base::WriteStringToFile(std::to_string(faceId), kFaceIdFile)) {
                PLOG(ERROR) << "Failed to save face id";
                result.error = Error::UNABLE_TO_PROCESS;
                break;
            }
            result.remaining = 0;
            result.done = true;
            break;
        case ST_AUTH_ENROLL_KEEP:
            result.remaining = std::max(kEnrollFrames - ++mAccepted, 1);
            break;
        case ST_E_TEMPLATE_UPPER_LIMIT:
            result.error = Error::NO_SPACE;
            break;
        case ST_E_HANDLE:
        case ST_E_TEMPLATE_DAMAGED:
            LOG(ERROR) << "Enrollment failed: " << status;
            result.error = Error::UNABLE_TO_PROCESS;
            break;
    }
    return result;
}

std::optional<Error> AuthEngine::beginAuthenticate(int orientation, bool requireAttention) {
    std::string faceId;
    if (!::android::base::ReadFileToString(kFaceIdFile, &faceId) ||
        !::android::base::ParseInt(faceId, &mFaceId, 0)) {
        LOG(ERROR) << "Missing face id";
        return Error::REENROLL_REQUIRED;
    }

    mConfig = kAuthConfig | (requireAttention ? 0 : ST_AUTH_CLOSE_EYE_STATUS);
    authReset();
    if (int ret = setFaceOrientation(orientation); ret != 0) {
        LOG(ERROR) << "Failed to configure authentication: " << ret;
        return Error::UNABLE_TO_PROCESS;
    }
    return std::nullopt;
}

AuthEngine::Result AuthEngine::authenticate(const Frame& frame) {
    st_image_t image = toImage(frame);
    int status = authUnlock(&image, mConfig, &mFaceId, 1);

    Result result{.info = toAcquiredInfo(status)};
    switch (status) {
        case ST_AUTH_UNLOCK_OK:
            result.done = true;
            [[fallthrough]];
        case ST_AUTH_UNLOCK_FAILED:
        case ST_AUTH_HACKNESS_FAILED:
            result.compared = true;
            break;
        case ST_E_HANDLE:
            result.error = Error::HW_UNAVAILABLE;
            break;
        case ST_E_TEMPLATE_DAMAGED:
        case ST_AUTH_TEMPLATE_NOT_EXIST:
        case ST_AUTH_TEMPLATE_NO_FACE:
            LOG(ERROR) << "Template unusable: " << status;
            result.error = Error::REENROLL_REQUIRED;
            break;
    }
    return result;
}

bool AuthEngine::removeTemplate() {
    unlink(kFaceIdFile);
    if (!init()) return false;

    authReset();
    int ret = deleteAllFaces();
    if (ret != 0 && ret != ST_AUTH_TEMPLATE_NOT_EXIST && ret != ST_AUTH_TEMPLATE_NO_FACE) {
        LOG(ERROR) << "Failed to delete faces: " << ret;
        return false;
    }
    return true;
}

}  // namespace aidl::android::hardware::biometrics::face
