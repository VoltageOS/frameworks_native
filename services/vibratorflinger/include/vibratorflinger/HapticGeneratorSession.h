/*
 * Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may not use a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <aidl/android/hardware/vibrator/HapticGeneratorCommand.h>
#include <aidl/android/hardware/vibrator/HapticGeneratorConfig.h>
#include <aidl/android/hardware/vibrator/HapticGeneratorQueues.h>
#include <aidl/android/hardware/vibrator/HapticGeneratorReply.h>
#include <aidl/android/hardware/vibrator/HapticGeneratorSession.h>
#include <aidl/android/hardware/vibrator/VibrationEffect.h>
#include <android-base/thread_annotations.h>
#include <fmq/AidlMessageQueue.h>

#include <map>
#include <mutex>
#include <vector>

#include <vibratorflinger/HapticGeneratorStream.h>
#include <vibratorflinger/VibratorQueues.h>

namespace android::vibrator {

class HapticGeneratorSession {
public:
    /**
     * Constructs a session from the HAL's session object.
     *
     * This constructor takes ownership of the HAL session and is responsible
     * for creating and managing the message queues.
     *
     * @param halSession The session object returned by the HAL.
     */
    explicit HapticGeneratorSession(
            aidl::android::hardware::vibrator::HapticGeneratorSession&& halSession);
    /**
     * Constructs a session for testing purposes.
     *
     * This constructor bypasses the HAL and takes ownership of pre-constructed
     * message queues, allowing for isolated unit testing.
     *
     * @param queues A map of vibrator IDs to their corresponding message queues,
     *               which are moved into the session.
     */
    explicit HapticGeneratorSession(std::map<int32_t, VibratorQueues>&& queues);

    ~HapticGeneratorSession();

    /**
     * Starts a haptic stream for a single vibrator within this session.
     *
     * If a stream for the given vibratorId already exists, it will be replaced.
     *
     * @param vibratorId The ID of the vibrator to use for the stream.
     * @param effect The complete vibration effect to be converted to PCM data.
     *
     * @return OK on success, or an error code if the stream could not be started.
     */
    status_t startStream(
            int32_t vibratorId,
            const std::vector<aidl::android::hardware::vibrator::VibrationEffectContent>& effect);

    /**
     * Reads the next chunk of generated PCM data from an active stream.
     *
     * This is a blocking call that will internally handle the protocol of sending
     * effect data and requesting PCM data from the HAL until data is available,
     * the stream ends, or an error occurs.
     *
     * @param vibratorId The ID of the vibrator associated with the stream.
     * @param bufferSize The maximum number of bytes to write into the buffer.
     * @param buffer A pointer to the buffer where the PCM data will be written.
     * @param bytesRead A pointer to a size_t that will be filled with the number of bytes
     *                  actually written to the buffer.
     *
     * @return OK on success or a negative error code on failure.
     */
    status_t readStream(int32_t vibratorId, size_t bufferSize, int8_t* buffer, size_t* bytesRead);

    /**
     * Stops an active haptic stream for a single vibrator.
     *
     * This sends a CANCEL command to the HAL and removes the stream from the session.
     *
     * @param vibratorId The ID of the vibrator associated with the stream to stop.
     *
     * @return OK on success, or an error code if the stream could not be stopped.
     */
    status_t stopStream(int32_t vibratorId);

    /**
     * Terminates the entire HAL session and releases all associated resources.
     *
     * @return OK on success, or an error code.
     */
    status_t close();

private:
    /** Sends a command to the HAL and waits for a reply. */
    void sendCommandAndReceiveReplyLocked(
            int32_t vibratorId,
            const aidl::android::hardware::vibrator::HapticGeneratorCommand& command,
            aidl::android::hardware::vibrator::HapticGeneratorReply* reply);

    void closeStreamLocked(int32_t vibratorId);

    std::mutex mMutex;
    // Store queues for all vibrators, mapped by their vibratorId
    std::map<int32_t, VibratorQueues> mQueues GUARDED_BY(mMutex);
    // Store active streams, one per vibratorId.
    std::map<int32_t, std::unique_ptr<HapticGeneratorStream>> mStreams GUARDED_BY(mMutex);
    bool mIsClosed GUARDED_BY(mMutex) = false;
};

} // namespace android::vibrator