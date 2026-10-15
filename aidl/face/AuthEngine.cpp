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
#include <android-base/strings.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ziparchive/zip_archive.h>

#include <algorithm>
#include <filesystem>

#include "AuthTee.h"

namespace aidl::android::hardware::biometrics::face {

using namespace st;

namespace {

constexpr const char* kResources = "/vendor/etc/faceunlock/resources.zip";
constexpr const char* kLibrary = "lib/arm64-v8a/libstfaceunlockocl.so";
constexpr const char* kLicense = "res/raw/st_license_3_1.lic";

constexpr const char* kTemplateDir = "/data/vendor/face/st_auth";
// Where the stock service keeps the TEE protected template.
constexpr const char* kTeeTemplateDir = "/data/vendor/misc/qsee/face";
// The stock service matches against the face id returned by the enrollment.
constexpr const char* kFaceIdFile = "/data/vendor/face/st_auth_face_id";

constexpr int kPixelFormatNv21 = 3;
constexpr int kLogLevelError = 0;

// Matches the stock app, eye and mouth occlusion only gate enrollments.
constexpr int kAuthConfig = ST_AUTH_CLOSE_EYE_BLOCK | ST_AUTH_CLOSE_MOUTH_BLOCK;

st_image_t toImage(const Frame& frame) {
    return {
            .data = frame.nv21,
            .format = kPixelFormatNv21,
            .width = frame.width,
            .height = frame.height,
            .stride = frame.width,
    };
}

bool isTemplateError(int status) {
    return status == ST_E_TEMPLATE_DAMAGED || status == ST_AUTH_TEMPLATE_NOT_EXIST ||
           status == ST_AUTH_TEMPLATE_NO_FACE;
}

// Only for the template in /data, the TEE protected one is removed through the trusted app.
bool wipeTemplateDir() {
    std::error_code ec;
    std::filesystem::directory_iterator it(kTemplateDir, ec);
    if (ec) {
        if (ec == std::errc::no_such_file_or_directory) return true;
        LOG(ERROR) << "Failed to list " << kTemplateDir << ": " << ec.message();
        return false;
    }

    bool ok = true;
    for (const auto& entry : it) {
        std::error_code removeEc;
        std::filesystem::remove_all(entry.path(), removeEc);
        if (removeEc) {
            LOG(ERROR) << "Failed to remove " << entry.path() << ": " << removeEc.message();
            ok = false;
        }
    }
    return ok;
}

bool readEntry(const char* name, std::string* out) {
    ZipArchiveHandle archive = nullptr;
    int32_t ret = OpenArchive(kResources, &archive);
    ZipEntry64 entry;
    bool ok = ret == 0 && FindEntry(archive, name, &entry) == 0;
    if (ok && out) {
        out->resize(entry.uncompressed_length);
        ok = ExtractToMemory(archive, &entry, reinterpret_cast<uint8_t*>(out->data()),
                             out->size()) == 0;
    }
    // The handle has to be closed even if opening failed.
    CloseArchive(archive);
    return ok;
}

}  // namespace

AuthEngine::AuthEngine()
    : mUseTee(::android::base::GetBoolProperty("persist.vendor.faceunlock.tee", true)) {}

AuthEngine::~AuthEngine() {
    deinit();
}

bool AuthEngine::available() {
    if (!mAvailable) mAvailable = readEntry(kLibrary, nullptr);
    return *mAvailable;
}

std::string AuthEngine::name() const {
    return mUseTee ? "st_auth_tee" : "st_auth";
}

const char* AuthEngine::templateDir() const {
    return mUseTee ? kTeeTemplateDir : kTemplateDir;
}

AcquiredInfo AuthEngine::toAcquiredInfo(int status) {
    switch (status) {
        case ST_AUTH_OK:
        case ST_AUTH_ENROLL_OK:
        case ST_AUTH_ENROLL_KEEP:
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

bool AuthEngine::loadSymbols() {
    if (mSymbolsLoaded) return true;

    if (!mLib) {
        std::string path = std::string(kResources) + "!/" + kLibrary;
        mLib = dlopen(path.c_str(), RTLD_NOW);
        if (!mLib) {
            LOG(ERROR) << "Failed to load " << kLibrary << ": " << dlerror();
            return false;
        }
    }

    bool ok = true;
    auto load = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(mLib, name));
        if (!fn) {
            LOG(ERROR) << "Missing symbol " << name;
            ok = false;
        }
    };
    load(initLicense, "st_face_init_license");
    load(openLog, "st_face_open_log");
    load(getVersion, "st_auth_get_version");
    load(setTeeCommunicationFunc, "st_auth_set_tee_communication_func");
    load(authInit, "st_auth_init");
    load(authDeinit, "st_auth_deinit");
    load(authReset, "st_auth_reset");
    load(setRoi, "st_auth_set_roi");
    load(setFaceOrientation, "st_auth_set_face_orientation");
    load(setImageNumber, "st_auth_set_faceid_image_number");
    load(authEnroll, "st_auth_enroll");
    load(authUnlock, "st_auth_unlock");
    load(deleteFace, "st_auth_delete_face");
    load(deleteAllFaces, "st_auth_delete_all_face");
    load(getTemplateInfo, "st_auth_get_template_info");
    load(releaseTemplateInfo, "st_auth_release_template_info");
    mSymbolsLoaded = ok;
    return ok;
}

bool AuthEngine::init() {
    if (mInitialized) return true;
    if (!loadSymbols()) return false;

    if (!mUseTee && mkdir(kTemplateDir, 0700) != 0 && errno != EEXIST) {
        PLOG(ERROR) << "Failed to create " << kTemplateDir;
        return false;
    }

    if (!FaceEngine::withStockCmdline([this] { return initLocked(); })) {
        deinit();
        return false;
    }
    mInitialized = true;
    return true;
}

// Mirrors the stock service: license, trusted app, communication function, then the SDK.
bool AuthEngine::initLocked() {
    std::string license;
    if (!readEntry(kLicense, &license)) {
        LOG(ERROR) << "Missing " << kLicense;
        return false;
    }

    openLog(kLogLevelError);
    int ret = initLicense(license.c_str());
    if (ret != 0) {
        LOG(ERROR) << "License rejected: " << ret;
        return false;
    }

    if (mUseTee) {
        if (!AuthTee::open()) return false;
        if ((ret = setTeeCommunicationFunc(AuthTee::communicate)) != 0) {
            LOG(ERROR) << "Failed to set TEE communication: " << ret;
            return false;
        }
    }

    ret = authInit(templateDir(), 0);
    if (ret == ST_E_TEMPLATE_DAMAGED && !mUseTee) {
        // Nothing can be matched against a damaged template, start over.
        LOG(ERROR) << "Template damaged, removing it";
        authDeinit();
        if (!wipeTemplateDir()) return false;
        ret = authInit(templateDir(), 0);
    }
    if (ret != 0) {
        LOG(ERROR) << "Failed to initialize st_auth: " << ret;
        return false;
    }

    const char* version = getVersion();
    LOG(INFO) << "Initialized st_auth " << (version ? version : "unknown")
              << (mUseTee ? " with TEE" : "");
    return true;
}

void AuthEngine::deinit() {
    if (mSymbolsLoaded) authDeinit();
    if (mUseTee) AuthTee::close();
    mInitialized = false;
}

std::vector<int> AuthEngine::faceIds() {
    std::string stored;
    int id;
    if (::android::base::ReadFileToString(kFaceIdFile, &stored) &&
        ::android::base::ParseInt(::android::base::Trim(stored), &id, 0)) {
        return {id};
    }

    st_template_info_t* info = nullptr;
    int ret = getTemplateInfo(&info);
    std::vector<int> ids;
    if (ret == 0 && info && info->faces) {
        for (int i = 0; i < info->count; i++) ids.push_back(info->faces[i].face_id);
    } else if (ret != 0) {
        LOG(ERROR) << "Failed to get template info: " << ret;
    }
    if (info) releaseTemplateInfo(info);
    return ids;
}

std::optional<Error> AuthEngine::beginEnroll(int orientation) {
    mOrientation = orientation;
    mAccepted = 0;

    authReset();
    // Only one face is supported, drop anything left behind by an earlier enrollment.
    unlink(kFaceIdFile);
    int ret = deleteAllFaces();
    if (ret != 0 && !isTemplateError(ret)) LOG(WARNING) << "Failed to clear template: " << ret;

    if ((ret = setImageNumber(kEnrollFrames)) != 0 || (ret = setRoi(nullptr)) != 0 ||
        (ret = setFaceOrientation(mOrientation)) != 0) {
        LOG(ERROR) << "Failed to configure enrollment: " << ret;
        return Error::UNABLE_TO_PROCESS;
    }
    return std::nullopt;
}

AuthEngine::Result AuthEngine::enroll(const Frame& frame) {
    st_image_t image = toImage(frame);
    int faceId = -1;
    int status = authEnroll(&image, 0, &faceId);

    Result result;
    result.info = toAcquiredInfo(status);
    switch (status) {
        case ST_AUTH_OK:
        case ST_AUTH_ENROLL_OK:
            LOG(INFO) << "Enrolled face " << faceId;
            if (faceId >= 0 &&
                !::android::base::WriteStringToFile(std::to_string(faceId), kFaceIdFile)) {
                PLOG(ERROR) << "Failed to save face id";
                result.error = Error::UNABLE_TO_PROCESS;
                break;
            }
            result.remaining = 0;
            result.done = true;
            break;
        case ST_AUTH_ENROLL_KEEP:
            // The SDK collects a set of frames before it saves the template.
            mAccepted++;
            result.remaining = std::max(kEnrollFrames - mAccepted, 1);
            break;
        case ST_E_TEMPLATE_UPPER_LIMIT:
            LOG(ERROR) << "Template is full";
            result.error = Error::NO_SPACE;
            break;
        case ST_E_HANDLE:
        case ST_E_TEMPLATE_DAMAGED:
            LOG(ERROR) << "Enrollment failed: " << status;
            result.error = Error::UNABLE_TO_PROCESS;
            break;
        default:
            LOG(VERBOSE) << "Enrollment frame rejected: " << status;
            break;
    }
    return result;
}

std::optional<Error> AuthEngine::beginAuthenticate(int orientation, bool requireAttention) {
    mOrientation = orientation;
    mConfig = kAuthConfig | (requireAttention ? 0 : ST_AUTH_CLOSE_EYE_STATUS);

    // Drops tracking and liveness state left behind by earlier operations.
    authReset();
    mFaceIds = faceIds();
    if (mFaceIds.empty()) {
        LOG(ERROR) << "No faces in template";
        return Error::REENROLL_REQUIRED;
    }

    int ret;
    if ((ret = setRoi(nullptr)) != 0 || (ret = setFaceOrientation(mOrientation)) != 0) {
        LOG(ERROR) << "Failed to configure authentication: " << ret;
        return Error::UNABLE_TO_PROCESS;
    }
    return std::nullopt;
}

AuthEngine::Result AuthEngine::authenticate(const Frame& frame) {
    st_image_t image = toImage(frame);
    int status = authUnlock(&image, mConfig, mFaceIds.data(), static_cast<int>(mFaceIds.size()));

    Result result;
    result.info = toAcquiredInfo(status);
    switch (status) {
        case ST_AUTH_UNLOCK_OK:
            result.compared = true;
            result.done = true;
            break;
        case ST_AUTH_UNLOCK_FAILED:
        case ST_AUTH_HACKNESS_FAILED:
            // Spoofs count as failed attempts.
            result.compared = true;
            break;
        case ST_E_HANDLE:
            LOG(ERROR) << "Authentication failed: " << status;
            result.error = Error::HW_UNAVAILABLE;
            break;
        case ST_E_TEMPLATE_DAMAGED:
        case ST_AUTH_TEMPLATE_NOT_EXIST:
        case ST_AUTH_TEMPLATE_NO_FACE:
            LOG(ERROR) << "Template unusable: " << status;
            result.error = Error::REENROLL_REQUIRED;
            break;
        default:
            break;
    }
    return result;
}

bool AuthEngine::removeTemplate() {
    if (unlink(kFaceIdFile) != 0 && errno != ENOENT) PLOG(ERROR) << "Failed to remove face id";

    // The TEE protected template can only be removed through the trusted app.
    if (mUseTee && !init()) return false;

    if (mInitialized) {
        authReset();
        int ret = deleteAllFaces();
        // The stock app deletes faces with id -1 instead.
        if (ret != 0 && !isTemplateError(ret)) ret = deleteFace(-1);
        if (ret == 0 || isTemplateError(ret)) return true;
        LOG(ERROR) << "Failed to delete faces: " << ret;
        if (mUseTee) return false;
        // Fall back to removing the files, the SDK has to reload them afterwards.
        deinit();
    }
    return wipeTemplateDir();
}

}  // namespace aidl::android::hardware::biometrics::face
