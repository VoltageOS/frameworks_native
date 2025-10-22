/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *            http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <aidl/android/hardware/vibrator/HapticGeneratorCommand.h>
#include <aidl/android/hardware/vibrator/HapticGeneratorReply.h>
#include <aidl/android/hardware/vibrator/PredefinedEffect.h>
#include <gtest/gtest.h>
#include <vibratorflinger/HapticGeneratorSession.h>
#include <vibratorflinger/HapticGeneratorStream.h>
#include "FakeVibratorHal.h"
#include "HapticGeneratorTestUtils.h"

#include <future>
#include <map>
#include <thread>
#include <vector>

using namespace android;
using namespace android::vibrator;
using namespace std::chrono_literals;
namespace fmq = aidl::android::hardware::common::fmq;
using aidl::android::hardware::vibrator::Effect;
using aidl::android::hardware::vibrator::HapticGeneratorCommand;
using aidl::android::hardware::vibrator::HapticGeneratorReply;
using aidl::android::hardware::vibrator::PredefinedEffect;
using aidl::android::hardware::vibrator::VibrationEffectContent;

const std::vector<Effect> kEffects{ndk::enum_range<Effect>().begin(),
                                   ndk::enum_range<Effect>().end()};
static constexpr int32_t VIBRATOR_ID_1 = 1;
static constexpr int32_t VIBRATOR_ID_2 = 2;

class HapticGeneratorTest : public ::testing::Test {
protected:
    void setupTestEnvironment(const std::map<int32_t, FakeVibratorHal::Config>& halConfigs) {
        std::map<int32_t, VibratorQueues> sessionQueues;

        for (const auto& [id, config] : halConfigs) {
            auto commandMQ = std::make_shared<FakeVibratorHal::CommandQueue>(1024, true);
            auto replyMQ = std::make_shared<FakeVibratorHal::ReplyQueue>(1024, true);
            // Use a small effect queue (size=1) to test streaming of multi-segment effects
            auto effectMQ = std::make_shared<FakeVibratorHal::EffectQueue>(1, true);
            auto pcmMQ = std::make_shared<FakeVibratorHal::PcmQueue>(1024, true);

            ASSERT_TRUE(commandMQ && commandMQ->isValid());
            ASSERT_TRUE(replyMQ && replyMQ->isValid());
            ASSERT_TRUE(effectMQ && effectMQ->isValid());
            ASSERT_TRUE(pcmMQ && pcmMQ->isValid());

            FakeVibratorHal::Config halConfig = config;
            halConfig.vibratorId = id;

            auto hal = std::make_unique<FakeVibratorHal>(commandMQ, replyMQ, effectMQ, pcmMQ,
                                                         halConfig);
            hal->start();

            sessionQueues.emplace(id,
                                  VibratorQueues{.command = commandMQ,
                                                 .reply = replyMQ,
                                                 .effect = effectMQ,
                                                 .pcm = pcmMQ});
            mFakeHals[id] = std::move(hal);
        }
        mSession = std::make_unique<HapticGeneratorSession>(std::move(sessionQueues));
    }

    void setupTestEnvironment(int32_t vibratorId, const FakeVibratorHal::Config& config = {}) {
        FakeVibratorHal::Config singleConfig = config;
        singleConfig.vibratorId = vibratorId;
        setupTestEnvironment({{vibratorId, singleConfig}});
    }

    void TearDown() override {
        if (mSession) {
            mSession->close();
        }
        mFakeHals.clear();
    }

    // Map to hold the fake HAL instances
    std::map<int32_t, std::unique_ptr<FakeVibratorHal>> mFakeHals;
    std::unique_ptr<HapticGeneratorSession> mSession;
};

TEST_F(HapticGeneratorTest, StartStream_StopStream_Success) {
    setupTestEnvironment(VIBRATOR_ID_1);
    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};

    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));
    mSession->stopStream(VIBRATOR_ID_1);

    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCancelCmd});
}

TEST_F(HapticGeneratorTest, StartStream_ReplacesExistingStream) {
    setupTestEnvironment(VIBRATOR_ID_1);
    auto effect1 = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    auto effect2 = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[1]);
    std::vector<VibrationEffectContent> effects1 = {effect1};
    std::vector<VibrationEffectContent> effects2 = {effect2};

    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects1));
    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects2));

    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCancelCmd,
                                                     HapticGeneratorTestUtils::kStartCmd});
}

TEST_F(HapticGeneratorTest, ReadStream_Success) {
    setupTestEnvironment(VIBRATOR_ID_1, {.pcmToProduce = 10});

    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};
    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));

    std::vector<int8_t> buffer(1024);
    size_t bytesRead = 0;
    status_t status = mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead);

    ASSERT_EQ(OK, status);
    ASSERT_EQ(static_cast<size_t>(10), bytesRead);

    // We expect 2 bursts to try reading the full buffer size.
    // The second burst returns 0 to indicate the stream is complete.
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});
}

TEST_F(HapticGeneratorTest, ReadStream_ReadsUntilEnd) {
    setupTestEnvironment(VIBRATOR_ID_1);
    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};
    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));

    std::vector<int8_t> buffer(1024);
    size_t bytesRead = 0;

    // First read
    ASSERT_EQ(OK, mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead));
    ASSERT_EQ(static_cast<size_t>(10), bytesRead);

    // Second read (end of stream)
    ASSERT_EQ(OK, mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead));
    ASSERT_EQ(static_cast<size_t>(0), bytesRead);

    // Third read (still end of stream)
    ASSERT_EQ(OK, mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead));
    ASSERT_EQ(static_cast<size_t>(0), bytesRead);

    // We expect 2 bursts to try reading the full buffer size.
    // The second burst returns 0 to indicate the stream is complete.
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});
}

TEST_F(HapticGeneratorTest, ReadStream_SingleCallFillsBufferWithMultipleBursts) {
    setupTestEnvironment(VIBRATOR_ID_1, {.pcmToProduce = 10});

    // Create an effect with enough segments to trigger multiple reads.
    auto effect1 = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    auto effect2 = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[1]);
    std::vector<VibrationEffectContent> effects = {effect1, effect1, effect2};

    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));

    std::vector<int8_t> buffer(35);
    size_t bytesRead = 0;
    status_t status = mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead);

    ASSERT_EQ(OK, status);
    ASSERT_EQ(static_cast<size_t>(30), bytesRead); // Should read 3 * 10 bytes.

    // We expect 4 burst commands: 3 to get data, 1 to check if there's any more left.
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});
}

TEST_F(HapticGeneratorTest, ReadStream_NonExistent_Fail) {
    // Create the session, but don't start a stream for VIBRATOR_ID_1.
    setupTestEnvironment(VIBRATOR_ID_1);

    // Attempting to read from a non-existent stream must fail
    std::vector<int8_t> buffer(1024);
    size_t bytesRead = 0;
    status_t status = mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead);

    ASSERT_EQ(NAME_NOT_FOUND, status);
    ASSERT_EQ(static_cast<size_t>(0), bytesRead);

    // Ensure no commands were sent to the HAL for this vibrator ID
    ASSERT_TRUE(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands().empty());
}

TEST_F(HapticGeneratorTest, ReadStream_MultiVibrator_Success) {
    setupTestEnvironment({{VIBRATOR_ID_1, {}}, {VIBRATOR_ID_2, {}}});

    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};

    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));
    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_2, effects));

    std::vector<int8_t> buffer(1024);
    size_t bytesRead1 = 0;
    size_t bytesRead2 = 0;

    // Read from the first vibrator and check its data
    status_t status1 =
            mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead1);

    ASSERT_EQ(OK, status1);
    ASSERT_EQ(static_cast<size_t>(10), bytesRead1); // Default pcmToProduce
    for (size_t i = 0; i < bytesRead1; i++) {
        ASSERT_EQ(VIBRATOR_ID_1, buffer[i]) << "Mismatch in buffer data at index " << i;
    }

    // Read from the second vibrator and check its data
    status_t status2 =
            mSession->readStream(VIBRATOR_ID_2, buffer.size(), buffer.data(), &bytesRead2);

    ASSERT_EQ(OK, status2);
    ASSERT_EQ(static_cast<size_t>(10), bytesRead2); // Default pcmToProduce
    for (size_t i = 0; i < bytesRead2; i++) {
        ASSERT_EQ(VIBRATOR_ID_2, buffer[i]) << "Mismatch in buffer data at index " << i;
    }

    // Commands for VIBRATOR 1
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});

    // Commands for VIBRATOR 2
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_2]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});
}

TEST_F(HapticGeneratorTest, ReadStream_PartialRead_WhenBufferIsSmall) {
    // Configure the fake HAL to produce more data than the read buffer size
    setupTestEnvironment(VIBRATOR_ID_1, {.pcmToProduce = 100});

    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};

    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));

    // Read into a buffer smaller than what the HAL will produce
    std::vector<int8_t> smallBuffer(10); // Can only fit 10 bytes
    size_t bytesRead = 0;
    status_t status =
            mSession->readStream(VIBRATOR_ID_1, smallBuffer.size(), smallBuffer.data(), &bytesRead);

    ASSERT_EQ(OK, status);
    ASSERT_EQ(smallBuffer.size(), bytesRead); // Should only read 10 bytes

    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});
}

TEST_F(HapticGeneratorTest, ReadStream_ReturnsErrorOnContinuousNotEnoughData) {
    // Setup: Configure the fake HAL to always reply with NOT_ENOUGH_DATA.
    setupTestEnvironment(VIBRATOR_ID_1, {.alwaysReplyNotEnoughData = true});

    auto effect1 = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect1};

    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));

    std::vector<int8_t> buffer(1024);
    size_t bytesRead = 0;

    // The first call to readStream should trigger an error. The entire effect will be
    // written, immediately followed by a COMPLETE command, setting `mIsEffectComplete=true`.
    // The fake HAL is configured to misbehave and reply NOT_ENOUGH_DATA to the BURST command.
    // The stream should detect this invalid state and return UNKNOWN_ERROR.
    status_t status = mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead);

    ASSERT_EQ(UNKNOWN_ERROR, status);
    ASSERT_EQ(static_cast<size_t>(0), bytesRead);

    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});
}

TEST_F(HapticGeneratorTest, ReadStream_TimesOutWhenHalDoesNotReply) {
    // Setup: Configure the fake HAL to be unresponsive to burst commands.
    setupTestEnvironment(VIBRATOR_ID_1, {.shouldNotReplyToBurst = true});

    // Create and start a simple stream.
    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};
    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));

    std::vector<int8_t> buffer(1024);
    size_t bytesRead = 0;
    // Attempt to read from the stream. The read stream method should time out because its
    // internal call to read will time out waiting for a reply from the unresponsive HAL.
    status_t status = mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead);

    ASSERT_EQ(TIMED_OUT, status);
    ASSERT_EQ(static_cast<size_t>(0), bytesRead);

    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCompleteCmd,
                                                     HapticGeneratorTestUtils::kDefaultBurstCmd});
}

TEST_F(HapticGeneratorTest, CloseSession_ClosesActiveStream) {
    setupTestEnvironment({{VIBRATOR_ID_1, {}}, {VIBRATOR_ID_2, {}}});

    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};

    ASSERT_EQ(OK, mSession->startStream(VIBRATOR_ID_1, effects));

    ASSERT_EQ(OK, mSession->close());

    // Check commands sent to VIBRATOR 1
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kStartCmd,
                                                     HapticGeneratorTestUtils::kCancelCmd,
                                                     HapticGeneratorTestUtils::kSessionCloseCmd});

    // Check commands sent to VIBRATOR 2 is only SESSION_CLOSE
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_2]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kSessionCloseCmd});
}

TEST_F(HapticGeneratorTest, CloseSession_NewOperations_Fail) {
    setupTestEnvironment(VIBRATOR_ID_1);

    ASSERT_EQ(OK, mSession->close());

    // Check session close command was sent
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kSessionCloseCmd});

    // Attempt to create a new stream on the closed session
    auto effect = HapticGeneratorTestUtils::createPredefinedEffect(kEffects[0]);
    std::vector<VibrationEffectContent> effects = {effect};
    ASSERT_EQ(INVALID_OPERATION, mSession->startStream(VIBRATOR_ID_1, effects));

    // Attempt to read from the closed session
    std::vector<int8_t> buffer(1024);
    size_t bytesRead = 0;
    ASSERT_EQ(INVALID_OPERATION,
              mSession->readStream(VIBRATOR_ID_1, buffer.size(), buffer.data(), &bytesRead));

    // The command list should still only contain the SESSION_CLOSE command.
    HapticGeneratorTestUtils::assertCommandSequence(mFakeHals[VIBRATOR_ID_1]->getRecordedCommands(),
                                                    {HapticGeneratorTestUtils::kSessionCloseCmd});
}
