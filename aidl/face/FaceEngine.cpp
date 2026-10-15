/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.biometrics.face-service.oneplus"

#include "FaceEngine.h"

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <ziparchive/zip_archive.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace aidl::android::hardware::biometrics::face {

using namespace st;

namespace {

constexpr const char* kCmdline = "com.oneplus.faceunlock";
constexpr const char* kResources = "/vendor/etc/faceunlock/resources.zip";

// In dependency order, so DT_NEEDED entries resolve to the already loaded libraries.
constexpr const char* kLibraries[] = {
        "libgnustl_shared.so",
        "libSNPE_G.so",
        "libstfaceunlocksnpe.so",
        "libstfaceunlock.so",
};

constexpr float kMinBrightness = 0.17f;
constexpr float kMaxBrightness = 0.83f;
constexpr float kMaxPitch = 20.0f;
constexpr float kMaxYaw = 25.0f;
constexpr float kEyeOpenThreshold = 9.5f;

constexpr cv_quality_filter_t kQualityFilters[] = {
        {CV_QUALITY_YAW, -20.0f, 20.0f},      {CV_QUALITY_PITCH, -25.0f, 25.0f},
        {CV_QUALITY_BRIGHTNESS, 0.2f, 0.87f}, {CV_QUALITY_LOCATION, 0.0f, 0.5f},
        {CV_QUALITY_MOUTH, 0.0f, 0.7f},       {CV_QUALITY_SHARPNESS, 0.8f, 1.0f},
        {CV_QUALITY_SIZE, 0.2f, 0.8f},        {CV_QUALITY_OCCLUSION, 0.0f, 0.5f},
};

constexpr int kOrientations[] = {CV_FACE_RIGHT, CV_FACE_LEFT, CV_FACE_UP, CV_FACE_DOWN};

// The SDK only takes model paths, so models are copied out of the archive into memfds.
class Models {
  public:
    ~Models() {
        if (mArchive) CloseArchive(mArchive);
    }

    bool open() {
        int32_t ret = OpenArchive(kResources, &mArchive);
        if (ret != 0) {
            LOG(ERROR) << "Failed to open " << kResources << ": " << ErrorCodeString(ret);
            mArchive = nullptr;
            return false;
        }
        return true;
    }

    bool read(const char* name, std::string* out) {
        ZipEntry64 entry;
        if (FindEntry(mArchive, name, &entry) != 0) {
            LOG(ERROR) << "Missing " << name;
            return false;
        }
        out->resize(entry.uncompressed_length);
        return ExtractToMemory(mArchive, &entry, reinterpret_cast<uint8_t*>(out->data()),
                               out->size()) == 0;
    }

    std::string path(const char* name) {
        ZipEntry64 entry;
        if (FindEntry(mArchive, name, &entry) != 0) {
            LOG(ERROR) << "Missing " << name;
            return {};
        }
        ::android::base::unique_fd fd(memfd_create(name, MFD_CLOEXEC));
        if (fd < 0 || ExtractEntryToFile(mArchive, &entry, fd.get()) != 0) {
            LOG(ERROR) << "Failed to extract " << name;
            return {};
        }
        std::string path = "/proc/self/fd/" + std::to_string(fd.get());
        mFds.push_back(std::move(fd));
        return path;
    }

  private:
    ZipArchiveHandle mArchive = nullptr;
    std::vector<::android::base::unique_fd> mFds;
};

float averageBrightness(const Frame& frame, const cv_rect_t& rect) {
    int left = std::clamp(rect.left, 0, frame.width);
    int right = std::clamp(rect.right, 0, frame.width);
    int top = std::clamp(rect.top, 0, frame.height);
    int bottom = std::clamp(rect.bottom, 0, frame.height);
    if (right <= left || bottom <= top) return 0.0f;

    uint64_t sum = 0;
    for (int y = top; y < bottom; y++) {
        const uint8_t* row = frame.nv21 + y * frame.width;
        for (int x = left; x < right; x++) sum += row[x];
    }
    return static_cast<float>(sum) / (255.0f * (right - left) * (bottom - top));
}

}  // namespace

char* FaceEngine::sCmdline = nullptr;
size_t FaceEngine::sCmdlineSize = 0;

void FaceEngine::setCmdline(char* cmdline, size_t size) {
    sCmdline = cmdline;
    sCmdlineSize = size;
}

cv_face_t DetectedFace::get() {
    cv_face_t f = face;
    f.landmarks.points = points.data();
    f.landmarks.occlusion = occlusion.data();
    f.landmarks.count = static_cast<int>(points.size());
    return f;
}

FaceEngine::~FaceEngine() {
    if (mDetector) destroyDetector(mDetector);
    if (mDetector640) destroyDetector(mDetector640);
    if (mOcular) destroyOcular(mOcular);
    if (mVerify) destroyVerify(mVerify);
    if (mHackness) destroyHackness(mHackness);
}

bool FaceEngine::loadSymbols() {
    for (const char* name : kLibraries) {
        std::string path = std::string(kResources) + "!/lib/arm64-v8a/" + name;
        void* lib = dlopen(path.c_str(), RTLD_NOW);
        if (!lib) {
            LOG(ERROR) << "Failed to load " << name << ": " << dlerror();
            return false;
        }
        if (!strcmp(name, "libstfaceunlocksnpe.so")) mSnpeLib = lib;
        if (!strcmp(name, "libstfaceunlock.so")) mLib = lib;
    }

    bool ok = true;
    auto load = [&](void* lib, auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
        if (!fn) {
            LOG(ERROR) << "Missing symbol " << name;
            ok = false;
        }
    };
    load(mLib, initLicense, "cv_face_init_license_config");
    load(mLib, createDetector, "cv_face_create_detector");
    load(mLib, destroyDetector, "cv_face_destroy_detector");
    load(mLib, detectFaces, "cv_face_detect");
    load(mLib, releaseFaces, "cv_face_release_detector_result");
    load(mLib, qualityDetect, "stmobile_faceunlock_quality_detect");
    load(mLib, createOcular, "cv_face_ocular_create");
    load(mLib, destroyOcular, "cv_face_ocular_destroy");
    load(mLib, detectOcular, "cv_face_ocular_detect_with_rect");
    load(mSnpeLib, initLicenseSnpe, "cv_face_init_license_config_snpe");
    load(mSnpeLib, createVerify, "cv_verify_create_handle_snpe");
    load(mSnpeLib, destroyVerify, "cv_verify_destroy_handle_snpe");
    load(mSnpeLib, getFeature, "cv_verify_get_feature_with_landmarks_snpe");
    load(mSnpeLib, serializeFeature, "cv_verify_serialize_feature_snpe");
    load(mSnpeLib, deserializeFeature, "cv_verify_deserialize_feature_snpe");
    load(mSnpeLib, compareFeature, "cv_verify_compare_feature_snpe");
    load(mSnpeLib, releaseFeature, "cv_verify_release_feature_snpe");
    load(mSnpeLib, createHackness, "cv_face_hackness_create_snpe");
    load(mSnpeLib, destroyHackness, "cv_face_hackness_destroy_snpe");
    load(mSnpeLib, resetHackness, "cv_face_hackness_reset_snpe");
    load(mSnpeLib, detectHackness, "cv_face_hackness_detect_with_landmarks_snpe");
    return ok;
}

bool FaceEngine::init() {
    if (mVerify) return true;
    if (!mLib && !loadSymbols()) return false;

    if (!sCmdline || sCmdlineSize <= strlen(kCmdline)) {
        LOG(ERROR) << "Unable to present cmdline";
        return false;
    }
    std::string saved(sCmdline, sCmdlineSize);
    memset(sCmdline, 0, sCmdlineSize);
    strcpy(sCmdline, kCmdline);
    bool ret = initLocked();
    memcpy(sCmdline, saved.data(), sCmdlineSize);
    return ret;
}

bool FaceEngine::initLocked() {
    Models models;
    std::string license;
    if (!models.open() || !models.read("res/raw/st_license_1_1.lic", &license)) return false;

    int ret = initLicense(license.c_str());
    int retSnpe = initLicenseSnpe(license.c_str());
    if (ret != 0 || retSnpe != 0) {
        LOG(ERROR) << "License rejected: " << ret << ", " << retSnpe;
        return false;
    }

    std::string detect = models.path("res/raw/st_detect_9_2_0.model");
    std::string align = models.path("res/raw/st_align_occlusion_1_6_4.model");
    std::string ocular = models.path("res/raw/st_ocular_2_9_2.model");
    std::string verify = models.path("res/raw/st_verify_snpe_3_100_4.model");
    std::string liveness = models.path("res/raw/st_liveness_snpe_7_93_0.model");
    if (detect.empty() || align.empty() || ocular.empty() || verify.empty() || liveness.empty()) {
        return false;
    }

    if ((ret = createDetector(&mDetector, detect.c_str(), align.c_str(),
                              CV_DETECT_RESIZE_427W | CV_DETECT_106_POINTS)) ||
        (ret = createDetector(&mDetector640, detect.c_str(), align.c_str(),
                              CV_DETECT_RESIZE_640W | CV_DETECT_106_POINTS)) ||
        (ret = createOcular(&mOcular, ocular.c_str(), align.c_str())) ||
        (ret = createVerify(&mVerify, verify.c_str())) ||
        (ret = createHackness(&mHackness, liveness.c_str()))) {
        LOG(ERROR) << "Failed to create ST handles: " << ret;
        return false;
    }
    return true;
}

bool FaceEngine::detect(const Frame& frame, int orientation, bool large, bool tryOtherOrientations,
                        DetectedFace* out) {
    cv_handle_t detector = large ? mDetector640 : mDetector;
    cv_face_t* faces = nullptr;
    int count = 0;

    auto run = [&](int o) {
        if (detectFaces(detector, frame.nv21, CV_PIX_FMT_NV21, frame.width, frame.height,
                        frame.width, o, &faces, &count, nullptr) != 0) {
            count = 0;
        }
        if (count == 0 && faces) {
            releaseFaces(faces, 0);
            faces = nullptr;
        }
        return count > 0;
    };

    bool found = run(orientation);
    if (!found && tryOtherOrientations) {
        for (int o : kOrientations) {
            if (o != orientation && run(o)) {
                orientation = o;
                found = true;
                break;
            }
        }
    }
    if (!found) return false;

    const cv_face_t* best = &faces[0];
    for (int i = 1; i < count; i++) {
        if (faces[i].rect.right - faces[i].rect.left > best->rect.right - best->rect.left) {
            best = &faces[i];
        }
    }

    out->face = *best;
    out->orientation = orientation;
    const cv_landmarks_t& lm = best->landmarks;
    out->points.assign(lm.points, lm.points + lm.count);
    if (lm.occlusion) {
        out->occlusion.assign(lm.occlusion, lm.occlusion + lm.count);
    } else {
        out->occlusion.assign(lm.count, 0);
    }
    memset(out->face.landmarks.reserved, 0, sizeof(out->face.landmarks.reserved));
    releaseFaces(faces, count);
    return true;
}

FaceEngine::Quality FaceEngine::checkQuality(const Frame& frame, DetectedFace& face) {
    cv_face_t f = face.get();
    float scores[std::size(kQualityFilters)] = {};
    int unused = 0;
    // The stock app also passes NV21 frames as NV12 here, only luma is inspected.
    if (qualityDetect(kQualityFilters, std::size(kQualityFilters), frame.nv21, CV_PIX_FMT_NV12,
                      frame.width, frame.height, frame.width, &f, scores, &unused) != 0) {
        return Quality::INSUFFICIENT;
    }

    float brightness = averageBrightness(frame, f.rect);
    if (brightness <= kMinBrightness) return Quality::TOO_DARK;
    if (brightness >= kMaxBrightness) return Quality::TOO_BRIGHT;

    if (std::fabs(f.pitch) >= kMaxPitch || std::fabs(f.yaw) >= kMaxYaw) {
        return Quality::POOR_GAZE;
    }
    return Quality::GOOD;
}

bool FaceEngine::eyesOpen(const Frame& frame, const DetectedFace& face) {
    float scores[2] = {};
    detectOcular(mOcular, frame.nv21, CV_PIX_FMT_NV21, frame.width, frame.height, frame.width,
                 face.face.rect, scores, face.orientation);
    return scores[0] > kEyeOpenThreshold || scores[1] > kEyeOpenThreshold;
}

float FaceEngine::spoofScore(const Frame& frame, DetectedFace& face) {
    cv_face_t f = face.get();
    float score = 0.0f;
    detectHackness(mHackness, frame.nv21, CV_PIX_FMT_NV21, frame.width, frame.height, frame.width,
                   &f.landmarks, &score, face.orientation);
    return score;
}

void FaceEngine::resetLiveness() {
    resetHackness(mHackness);
}

std::vector<uint8_t> FaceEngine::extractFeature(const Frame& frame, DetectedFace& face) {
    cv_face_t f = face.get();
    cv_feature_t* feature = nullptr;
    unsigned int unused = 0;
    int ret = getFeature(mVerify, frame.nv21, CV_PIX_FMT_NV21, frame.width, frame.height,
                         frame.width, &f.landmarks, &feature, &unused);
    if (ret != 0 || !feature) {
        LOG(ERROR) << "Failed to extract feature: " << ret;
        if (feature) releaseFeature(feature);
        return {};
    }

    // Serialized features are base64 encoded and NUL terminated.
    std::vector<uint8_t> out(((feature->length + 2) / 3) * 4 + 1);
    serializeFeature(feature, reinterpret_cast<char*>(out.data()));
    releaseFeature(feature);
    return out;
}

float FaceEngine::compareFeatures(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.empty() || b.empty()) return -1.0f;

    cv_feature_t* fa = deserializeFeature(reinterpret_cast<const char*>(a.data()));
    cv_feature_t* fb = deserializeFeature(reinterpret_cast<const char*>(b.data()));
    float score = -1.0f;
    if (fa && fb && compareFeature(mVerify, fa, fb, &score) != 0) score = -1.0f;
    if (fa) releaseFeature(fa);
    if (fb) releaseFeature(fb);
    return score;
}

}  // namespace aidl::android::hardware::biometrics::face
