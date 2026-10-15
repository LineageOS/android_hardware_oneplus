/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.biometrics.face-service.oneplus"

#include "Session.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android/native_window_aidl.h>
#include <util/CancellationSignal.h>

#include <random>

namespace aidl::android::hardware::biometrics::face {

using namespace std::chrono_literals;
using ::aidl::android::hardware::NativeWindow;

namespace {

constexpr const char* kEnrollmentFile = "/data/vendor/face/enrollment";
constexpr const char* kFeatureFile = "/data/vendor/face/feature";

// The faceapp trusted app stores the template in the TEE. When disabled, the template is kept in
// /data and matched in-process, mirroring the stock app's non-TEE mode.
bool useTee() {
    return ::android::base::GetBoolProperty("persist.vendor.faceunlock.tee", true);
}

constexpr auto kAuthTimeout = 5s;
constexpr auto kEnrollTimeout = 60s;
constexpr auto kLargeDetectorDelay = 500ms;

constexpr float kMatchThreshold = 0.8f;
constexpr float kSpoofThreshold = 0.95f;
constexpr int kEnrollFrames = 5;

constexpr int kLockoutTimedFailures = 5;
constexpr int kLockoutPermanentFailures = 20;
constexpr auto kLockoutDuration = 30s;

int64_t randomId() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int64_t> dist(1, INT64_MAX);
    return dist(rng);
}

int faceOrientation(int sensorOrientation) {
    switch (sensorOrientation) {
        case 0:
            return st::CV_FACE_UP;
        case 90:
            return st::CV_FACE_LEFT;
        case 180:
            return st::CV_FACE_DOWN;
        default:
            return st::CV_FACE_RIGHT;
    }
}

AuthenticationFrame authFrame(AcquiredInfo info) {
    AuthenticationFrame frame;
    frame.data.acquiredInfo = info;
    return frame;
}

EnrollmentFrame enrollFrame(AcquiredInfo info, EnrollmentStage stage) {
    EnrollmentFrame frame;
    frame.stage = stage;
    frame.data.acquiredInfo = info;
    return frame;
}

AcquiredInfo toAcquiredInfo(FaceEngine::Quality quality) {
    switch (quality) {
        case FaceEngine::Quality::GOOD:
            return AcquiredInfo::GOOD;
        case FaceEngine::Quality::TOO_DARK:
            return AcquiredInfo::TOO_DARK;
        case FaceEngine::Quality::TOO_BRIGHT:
            return AcquiredInfo::TOO_BRIGHT;
        case FaceEngine::Quality::POOR_GAZE:
            return AcquiredInfo::POOR_GAZE;
        default:
            return AcquiredInfo::INSUFFICIENT;
    }
}

}  // namespace

std::map<int32_t, LockoutTracker> Session::sLockouts;

Enrollment Enrollment::load() {
    Enrollment e;
    std::string content;
    if (!::android::base::ReadFileToString(kEnrollmentFile, &content)) return e;

    auto fields = ::android::base::Split(::android::base::Trim(content), " ");
    if (fields.size() != 4) return e;
    e.userId = std::stoi(fields[0]);
    e.enrollmentId = std::stoi(fields[1]);
    e.authenticatorId = std::stoll(fields[2]);
    e.requireAttention = fields[3] == "1";
    return e;
}

bool Enrollment::save() const {
    if (userId < 0) return unlink(kEnrollmentFile) == 0 || errno == ENOENT;
    std::string content = std::to_string(userId) + " " + std::to_string(enrollmentId) + " " +
                          std::to_string(authenticatorId) + " " + (requireAttention ? "1" : "0");
    return ::android::base::WriteStringToFile(content, kEnrollmentFile);
}

Session::Session(std::shared_ptr<Hardware> hw, int32_t userId, std::shared_ptr<ISessionCallback> cb)
    : mHw(std::move(hw)), mUserId(userId), mCb(std::move(cb)), mWorker(16) {}

std::shared_ptr<common::ICancellationSignal> Session::schedule(
        std::function<void(const std::future<void>&)> op) {
    std::promise<void> promise;
    std::future<void> future = promise.get_future();
    mWorker.schedule(Callable::from([this, op = std::move(op), future = std::move(future)] {
        std::lock_guard lock(mHw->lock);
        op(future);
    }));
    return ndk::SharedRefBase::make<CancellationSignal>(std::move(promise));
}

bool Session::prepare() {
    if (!mHw->engine.init() || (useTee() && !mHw->tee.open())) {
        mCb->onError(Error::HW_UNAVAILABLE, 0);
        return false;
    }
    return true;
}

bool Session::storeFeature(const std::vector<uint8_t>& feature, const std::vector<uint8_t>& nv21) {
    removeFeature();
    if (!useTee()) {
        if (!::android::base::WriteStringToFile(std::string(feature.begin(), feature.end()),
                                                kFeatureFile)) {
            PLOG(ERROR) << "Failed to write feature";
            return false;
        }
        return true;
    }

    FaceTee& tee = mHw->tee;
    if (!tee.saveFeature(feature.data(), feature.size())) {
        LOG(ERROR) << "Failed to save feature of " << feature.size() << " bytes";
        return false;
    }
    // Kept so the feature can be regenerated if the algorithm ever changes.
    tee.saveFrame(0, nv21.data());
    return true;
}

bool Session::removeFeature() {
    if (!useTee()) return unlink(kFeatureFile) == 0 || errno == ENOENT;

    FaceTee& tee = mHw->tee;
    if (!tee.open()) return false;
    bool ok = tee.removeFeature();
    for (int i = 0; i < FaceTee::kMaxFrames; i++) tee.removeFrame(i);
    return ok;
}

std::optional<float> Session::matchFeature(const std::vector<uint8_t>& probe) {
    if (useTee()) return mHw->tee.compare(probe.data(), probe.size());

    std::string stored;
    if (!::android::base::ReadFileToString(kFeatureFile, &stored)) {
        PLOG(ERROR) << "Failed to read feature";
        return std::nullopt;
    }
    float score =
            mHw->engine.compareFeatures(probe, std::vector<uint8_t>(stored.begin(), stored.end()));
    if (score < 0.0f) return std::nullopt;
    return score;
}

ndk::ScopedAStatus Session::generateChallenge() {
    int64_t challenge = randomId();
    {
        std::lock_guard lock(mChallengeLock);
        mChallenges.insert(challenge);
    }
    mCb->onChallengeGenerated(challenge);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::revokeChallenge(int64_t challenge) {
    {
        std::lock_guard lock(mChallengeLock);
        mChallenges.erase(challenge);
    }
    mCb->onChallengeRevoked(challenge);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getEnrollmentConfig(EnrollmentType,
                                                std::vector<EnrollmentStageConfig>* return_val) {
    *return_val = {};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enroll(const keymaster::HardwareAuthToken& hat, EnrollmentType,
                                   const std::vector<Feature>&,
                                   const std::optional<NativeHandle>& /*previewSurface*/,
                                   std::shared_ptr<common::ICancellationSignal>* return_val) {
    *return_val = scheduleEnroll(hat, nullptr);
    return ndk::ScopedAStatus::ok();
}

std::shared_ptr<common::ICancellationSignal> Session::scheduleEnroll(
        const keymaster::HardwareAuthToken& hat, ANativeWindow* preview) {
    auto window = std::make_shared<NativeWindow>(preview);
    return schedule([this, hat, window](const std::future<void>& cancel) {
        doEnroll(hat, window->get(), cancel);
    });
}

void Session::doEnroll(const keymaster::HardwareAuthToken& hat, ANativeWindow* preview,
                       const std::future<void>& cancel) {
    {
        std::lock_guard lock(mChallengeLock);
        if (!mChallenges.count(hat.challenge)) {
            LOG(ERROR) << "Enrollment requested with unknown challenge";
            mCb->onError(Error::UNABLE_TO_PROCESS, 0);
            return;
        }
    }

    Enrollment enrollment = Enrollment::load();
    if (enrollment.userId >= 0) {
        LOG(ERROR) << "A face is already enrolled for user " << enrollment.userId;
        mCb->onError(Error::NO_SPACE, 0);
        return;
    }
    if (!prepare()) return;

    CameraSource& camera = mHw->camera;
    if (!camera.start(preview)) {
        mCb->onError(Error::HW_UNAVAILABLE, 0);
        return;
    }

    FaceEngine& engine = mHw->engine;
    int orientation = faceOrientation(camera.sensorOrientation());
    int32_t enrollmentId = static_cast<int32_t>(randomId() & INT32_MAX);
    auto deadline = std::chrono::steady_clock::now() + kEnrollTimeout;
    auto cancelled = [&] { return shouldCancel(cancel); };

    std::vector<uint8_t> nv21;
    std::vector<uint8_t> feature;
    int lastFaceId = -1;
    int goodFrames = 0;
    AcquiredInfo lastInfo = AcquiredInfo::UNKNOWN;
    bool first = true;

    engine.resetLiveness();
    while (camera.nextFrame(&nv21, deadline, cancelled)) {
        if (first) {
            mCb->onEnrollmentFrame(enrollFrame(AcquiredInfo::FIRST_FRAME_RECEIVED,
                                               EnrollmentStage::FIRST_FRAME_RECEIVED));
            first = false;
        }

        Frame frame = {nv21.data(), CameraSource::kWidth, CameraSource::kHeight};
        DetectedFace face;
        AcquiredInfo info = AcquiredInfo::GOOD;
        if (!engine.detect(frame, orientation, false, false, &face)) {
            info = AcquiredInfo::NOT_DETECTED;
        } else if (auto quality = engine.checkQuality(frame, face);
                   quality != FaceEngine::Quality::GOOD) {
            info = toAcquiredInfo(quality);
        } else if (!engine.eyesOpen(frame, face)) {
            info = AcquiredInfo::POOR_GAZE;
        } else if (engine.spoofScore(frame, face) > kSpoofThreshold) {
            info = AcquiredInfo::INSUFFICIENT;
        }

        if (info != lastInfo) {
            mCb->onEnrollmentFrame(enrollFrame(info, EnrollmentStage::HOLD_STILL_IN_CENTER));
            lastInfo = info;
        }
        if (info != AcquiredInfo::GOOD) {
            goodFrames = 0;
            continue;
        }

        if (face.face.id != lastFaceId) {
            lastFaceId = face.face.id;
            goodFrames = 0;
        }
        if (++goodFrames < kEnrollFrames) {
            mCb->onEnrollmentProgress(enrollmentId, kEnrollFrames - goodFrames);
            continue;
        }

        feature = engine.extractFeature(frame, face);
        if (!feature.empty()) break;
        goodFrames = 0;
    }
    camera.stop();

    if (feature.empty()) {
        mCb->onError(cancelled() ? Error::CANCELED : Error::TIMEOUT, 0);
        return;
    }

    if (!storeFeature(feature, nv21)) {
        mCb->onError(Error::UNABLE_TO_PROCESS, 0);
        return;
    }

    enrollment.userId = mUserId;
    enrollment.enrollmentId = enrollmentId;
    enrollment.authenticatorId = randomId();
    if (!enrollment.save()) {
        PLOG(ERROR) << "Failed to save enrollment";
        removeFeature();
        mCb->onError(Error::UNABLE_TO_PROCESS, 0);
        return;
    }

    mCb->onEnrollmentFrame(enrollFrame(AcquiredInfo::GOOD, EnrollmentStage::ENROLLMENT_FINISHED));
    mCb->onEnrollmentProgress(enrollmentId, 0);
}

ndk::ScopedAStatus Session::authenticate(int64_t,
                                         std::shared_ptr<common::ICancellationSignal>* return_val) {
    *return_val = schedule([this](const std::future<void>& cancel) { doAuthenticate(cancel); });
    return ndk::ScopedAStatus::ok();
}

bool Session::checkLockoutLocked() {
    LockoutTracker& lockout = sLockouts[mUserId];
    if (lockout.failures >= kLockoutPermanentFailures) {
        mCb->onLockoutPermanent();
        return true;
    }
    auto now = std::chrono::steady_clock::now();
    if (lockout.lockedUntil > now) {
        mCb->onLockoutTimed(
                std::chrono::duration_cast<std::chrono::milliseconds>(lockout.lockedUntil - now)
                        .count());
        return true;
    }
    return false;
}

void Session::onAuthenticationFailedLocked() {
    mCb->onAuthenticationFailed();

    LockoutTracker& lockout = sLockouts[mUserId];
    lockout.failures++;
    if (lockout.failures >= kLockoutPermanentFailures) {
        mCb->onLockoutPermanent();
    } else if (lockout.failures % kLockoutTimedFailures == 0) {
        lockout.lockedUntil = std::chrono::steady_clock::now() + kLockoutDuration;
        mCb->onLockoutTimed(std::chrono::milliseconds(kLockoutDuration).count());
    }
}

void Session::doAuthenticate(const std::future<void>& cancel) {
    Enrollment enrollment = Enrollment::load();
    if (enrollment.userId != mUserId) {
        mCb->onError(Error::UNABLE_TO_PROCESS, 0);
        return;
    }
    if (checkLockoutLocked() || !prepare()) return;

    CameraSource& camera = mHw->camera;
    if (!camera.start(nullptr)) {
        mCb->onError(Error::HW_UNAVAILABLE, 0);
        return;
    }

    FaceEngine& engine = mHw->engine;
    int orientation = faceOrientation(camera.sensorOrientation());
    auto start = std::chrono::steady_clock::now();
    auto lastFace = start;
    auto cancelled = [&] { return shouldCancel(cancel); };

    std::vector<uint8_t> nv21;
    bool large = false;
    bool compared = false;
    bool matched = false;
    AcquiredInfo lastInfo = AcquiredInfo::UNKNOWN;

    engine.resetLiveness();
    while (camera.nextFrame(&nv21, start + kAuthTimeout, cancelled)) {
        Frame frame = {nv21.data(), CameraSource::kWidth, CameraSource::kHeight};
        DetectedFace face;
        AcquiredInfo info = AcquiredInfo::GOOD;
        if (!engine.detect(frame, orientation, large, true, &face)) {
            info = AcquiredInfo::NOT_DETECTED;
            if (std::chrono::steady_clock::now() - lastFace > kLargeDetectorDelay) large = true;
        } else {
            lastFace = std::chrono::steady_clock::now();
            if (enrollment.requireAttention && !engine.eyesOpen(frame, face)) {
                info = AcquiredInfo::POOR_GAZE;
            }
        }

        if (info != lastInfo) {
            mCb->onAuthenticationFrame(authFrame(info));
            lastInfo = info;
        }
        if (info != AcquiredInfo::GOOD) continue;

        std::vector<uint8_t> feature = engine.extractFeature(frame, face);
        if (feature.empty()) continue;
        float spoof = engine.spoofScore(frame, face);
        std::optional<float> score = matchFeature(feature);
        if (!score) continue;
        compared = true;

        LOG(DEBUG) << "Match score " << *score << ", spoof score " << spoof;
        if (*score >= kMatchThreshold && spoof <= kSpoofThreshold) {
            matched = true;
            break;
        }
    }
    camera.stop();

    if (matched) {
        sLockouts.erase(mUserId);
        // Convenience sensors must not mint auth tokens.
        mCb->onAuthenticationSucceeded(enrollment.enrollmentId, keymaster::HardwareAuthToken());
    } else if (cancelled()) {
        mCb->onError(Error::CANCELED, 0);
    } else if (compared) {
        onAuthenticationFailedLocked();
    } else {
        mCb->onError(Error::TIMEOUT, 0);
    }
}

ndk::ScopedAStatus Session::detectInteraction(
        std::shared_ptr<common::ICancellationSignal>* return_val) {
    *return_val = schedule(
            [this](const std::future<void>&) { mCb->onError(Error::UNABLE_TO_PROCESS, 0); });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enumerateEnrollments() {
    schedule([this](const std::future<void>&) {
        Enrollment enrollment = Enrollment::load();
        if (enrollment.userId == mUserId) {
            mCb->onEnrollmentsEnumerated({enrollment.enrollmentId});
        } else {
            mCb->onEnrollmentsEnumerated({});
        }
    });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::removeEnrollments(const std::vector<int32_t>& enrollmentIds) {
    schedule([this, enrollmentIds](const std::future<void>&) {
        Enrollment enrollment = Enrollment::load();
        if (enrollment.userId != mUserId ||
            std::find(enrollmentIds.begin(), enrollmentIds.end(), enrollment.enrollmentId) ==
                    enrollmentIds.end()) {
            mCb->onEnrollmentsRemoved(enrollmentIds);
            return;
        }

        if (!removeFeature()) {
            mCb->onError(Error::UNABLE_TO_REMOVE, 0);
            return;
        }
        Enrollment().save();
        mCb->onEnrollmentsRemoved(enrollmentIds);
    });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getFeatures() {
    schedule([this](const std::future<void>&) {
        Enrollment enrollment = Enrollment::load();
        if (enrollment.userId != mUserId) {
            mCb->onError(Error::UNABLE_TO_PROCESS, 0);
            return;
        }
        std::vector<Feature> features;
        if (enrollment.requireAttention) features.push_back(Feature::REQUIRE_ATTENTION);
        mCb->onFeaturesRetrieved(features);
    });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::setFeature(const keymaster::HardwareAuthToken&, Feature feature,
                                       bool enabled) {
    schedule([this, feature, enabled](const std::future<void>&) {
        Enrollment enrollment = Enrollment::load();
        if (enrollment.userId != mUserId || feature != Feature::REQUIRE_ATTENTION) {
            mCb->onError(Error::UNABLE_TO_PROCESS, 0);
            return;
        }
        enrollment.requireAttention = enabled;
        if (!enrollment.save()) {
            mCb->onError(Error::UNABLE_TO_PROCESS, 0);
            return;
        }
        mCb->onFeatureSet(feature);
    });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getAuthenticatorId() {
    schedule([this](const std::future<void>&) {
        Enrollment enrollment = Enrollment::load();
        mCb->onAuthenticatorIdRetrieved(enrollment.userId == mUserId ? enrollment.authenticatorId
                                                                     : 0);
    });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::invalidateAuthenticatorId() {
    schedule([this](const std::future<void>&) {
        Enrollment enrollment = Enrollment::load();
        if (enrollment.userId != mUserId) {
            mCb->onAuthenticatorIdInvalidated(0);
            return;
        }
        enrollment.authenticatorId = randomId();
        enrollment.save();
        mCb->onAuthenticatorIdInvalidated(enrollment.authenticatorId);
    });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::resetLockout(const keymaster::HardwareAuthToken&) {
    schedule([this](const std::future<void>&) {
        sLockouts.erase(mUserId);
        mCb->onLockoutCleared();
    });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::close() {
    mCb->onSessionClosed();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticateWithContext(
        int64_t operationId, const common::OperationContext&,
        std::shared_ptr<common::ICancellationSignal>* return_val) {
    return authenticate(operationId, return_val);
}

ndk::ScopedAStatus Session::enrollWithContext(
        const keymaster::HardwareAuthToken& hat, EnrollmentType enrollmentType,
        const std::vector<Feature>& features, const std::optional<NativeHandle>& previewSurface,
        const common::OperationContext&, std::shared_ptr<common::ICancellationSignal>* return_val) {
    return enroll(hat, enrollmentType, features, previewSurface, return_val);
}

ndk::ScopedAStatus Session::detectInteractionWithContext(
        const common::OperationContext&, std::shared_ptr<common::ICancellationSignal>* return_val) {
    return detectInteraction(return_val);
}

ndk::ScopedAStatus Session::onContextChanged(const common::OperationContext&) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enrollWithOptions(
        const FaceEnrollOptions& options,
        std::shared_ptr<common::ICancellationSignal>* return_val) {
    *return_val = scheduleEnroll(options.hardwareAuthToken,
                                 options.surfacePreview ? options.surfacePreview->get() : nullptr);
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::biometrics::face
