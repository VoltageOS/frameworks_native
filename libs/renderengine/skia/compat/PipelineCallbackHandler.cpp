/*
 * Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "PipelineCallbackHandler.h"

#include <android-base/stringprintf.h>
#include <common/trace.h>
#include "Base64.h"

#include <vector>

namespace android::renderengine::skia {

namespace {

void traceSerializedKey(sk_sp<SkData> data) {
    if (!data->size()) {
        SFTRACE_FORMAT("re_skia_serialized_key:invalid_key_empty");
        return;
    }

    std::string str;
    str.resize(Base64::EncodedSize(data->size()));
    Base64::Encode(data->data(), data->size(), str.data());

    SFTRACE_FORMAT("re_skia_serialized_key:%s", str.c_str());
}

} // anonymous namespace

PipelineCallbackHandler::PipelineCallbackHandler(bool isProtected, bool storeSerializedKeys)
      : mStartTime(std::chrono::steady_clock::now()),
        mIsProtected(isProtected),
        mStoreSerializedKeys(storeSerializedKeys) {
    mLastEpochUpdateTime = mStartTime;
    // TODO(482036727): initialize 'mCurrentEpoch' and 'mEpochOfLastSave' from the cache file
}

void PipelineCallbackHandler::beginWarmup() {
    std::lock_guard<std::mutex> guard(mMutex);
    mInWarmup = true;
}
void PipelineCallbackHandler::endWarmup() {
    std::lock_guard<std::mutex> guard(mMutex);
    mInWarmup = false;
}

void PipelineCallbackHandler::updateEpoch() {
    std::chrono::time_point<std::chrono::steady_clock> curTime = std::chrono::steady_clock::now();

    std::chrono::seconds deltaSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(curTime - mLastEpochUpdateTime);
    uint32_t tensOfSeconds = static_cast<uint32_t>(deltaSeconds.count() / 10);

    mCurrentEpoch += tensOfSeconds;
    mLastEpochUpdateTime = curTime;
}

void PipelineCallbackHandler::add(skgpu::graphite::ContextOptions::PipelineCacheOp op,
                                  const std::string& label, uint32_t uniqueKeyHash,
                                  bool fromPrecompile, sk_sp<SkData> serializedKey) {
    std::lock_guard<std::mutex> guard(mMutex);

    this->updateEpoch();

    auto iter = mMap.find(PipelineKey(&label, uniqueKeyHash));
    if (iter != mMap.end()) {
        // Pre-existing Pipeline - just update its usage(s)
        iter->second->mUses++;
        iter->second->mLastUsageEpoch = mCurrentEpoch;
    } else {
        SkASSERT(op == skgpu::graphite::ContextOptions::PipelineCacheOp::kAddingPipeline);

        mPipelineAddedSinceLastSave = true;

        if (serializedKey) {
            traceSerializedKey(serializedKey);
            if (!mStoreSerializedKeys) {
                serializedKey.reset();
            }
        }

        std::chrono::milliseconds creationTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - mStartTime);

        std::unique_ptr<PipelineData> newData =
                std::make_unique<PipelineData>(label, creationTime, std::move(serializedKey),
                                               mCurrentEpoch, fromPrecompile, mInWarmup);

        mMap.emplace(
                std::make_pair(PipelineKey(&newData->mLabel, uniqueKeyHash), std::move(newData)));
    }

    this->maybeSaveCache();
}

void PipelineCallbackHandler::report(const char* label, std::string& result) {
    // The assumption is that we're just doing this very infrequently so we just lock for the
    // entire method.
    std::lock_guard<std::mutex> guard(mMutex);

    std::vector<const PipelineData*> tmp;
    int precompileCount = 0, warmupCount = 0, normalCount = 0;

    tmp.reserve(mMap.size());
    for (const auto& [_, value] : mMap) {
        tmp.push_back(value.get());
        if (value->mFromPrecompile) {
            ++precompileCount;
        } else if (value->mFromWarmup) {
            ++warmupCount;
        } else {
            ++normalCount;
        }
    }

    std::sort(tmp.begin(), tmp.end(), [](const PipelineData* a, const PipelineData* b) {
        if (a->mUses != b->mUses) {
            return a->mUses > b->mUses;
        }
        return a->mLabel < b->mLabel;
    });

    base::StringAppendF(&result,
                        "%zu %s Pipelines (%d Warmup/%d Normal/%d Precompile) ----------\n",
                        tmp.size(), label, warmupCount, normalCount, precompileCount);

    for (const PipelineData* data : tmp) {
        base::StringAppendF(&result, "%c %d %.3fs %s %u %zuB\n",
                            data->mFromPrecompile ? 'P' : (data->mFromWarmup ? 'W' : 'N'),
                            data->mUses, data->mCreationTime.count() / 1000.0f,
                            data->mLabel.c_str(), data->mLastUsageEpoch,
                            data->mSerializedKey ? data->mSerializedKey->size() : 0);
    }
}

// This is a stub implementation - just enough to demonstrate its interaction with epochs and
// saving.
bool PipelineCallbackHandler::maybeSaveCache() {
    uint32_t epochDelta = mCurrentEpoch - mEpochOfLastSave;

    // TODO: we also need to ensure that no save occur during warmup and or precompile.
    bool saveForNew = mPipelineAddedSinceLastSave && epochDelta > kNumEpochsBetweenNewPipelineSaves;
    bool saveForUses = epochDelta > kNumEpochsBetweenUsesSaves;

    if (!saveForNew && !saveForUses) {
        // In this case we don't need to create the blob.
        return false;
    }

    mPipelineAddedSinceLastSave = false;
    mEpochOfLastSave = mCurrentEpoch;

    // TODO(482036727): add actual creation of the data blob
    return true;
}

} // namespace android::renderengine::skia
