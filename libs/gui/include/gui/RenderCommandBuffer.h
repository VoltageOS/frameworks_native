/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <log/log.h>
#include <string>

#define RENDER_COMMAND_BUFFER_DEFAULT_SIZE 1024 * 1024

// #define RENDER_COMMAND_BUFFER_VERBOSE 1

namespace android {

struct IPCRenderBufferOp {
    uint32_t type;
    size_t size;
};

class RenderCommandBuffer {
public:
    RenderCommandBuffer() {
        memset(mBytes, 0, sizeof(mBytes));
        mUsed = 0;
        mConsumeOffset = 0;
    }
    ~RenderCommandBuffer() {}

    bool record(const void* bytes, size_t size) {
        if (mUsed + size > RENDER_COMMAND_BUFFER_DEFAULT_SIZE) {
            ALOGE("RenderCommandBuffer overflow");
            return false;
        }
        ALOGE("Recording %d bytes", (int)size);
        memcpy(mBytes + mUsed, bytes, size);
        mUsed += size;
        return true;
    }

    void* reserve(size_t size) {
        if (mUsed + size > RENDER_COMMAND_BUFFER_DEFAULT_SIZE) {
            ALOGE("RenderCommandBuffer overflow");
            return nullptr;
        }
        void* addr = (char*)&(mBytes[0]) + mUsed;
        mUsed += size;
        return addr;
    }

    IPCRenderBufferOp* consume() {
        if (mConsumeOffset >= mUsed) {
#ifdef RENDER_COMMAND_BUFFER_VERBOSE
            ALOGE("RenderCommandBuffer::consume: No more ops to consume mConsumeOffset %d mUsed %d",
                  (int)mConsumeOffset, (int)mUsed);
#endif
            return nullptr;
        }
        IPCRenderBufferOp* op =
                reinterpret_cast<IPCRenderBufferOp*>(mBytes + mConsumeOffset);
        mConsumeOffset += op->size;
        return op;
    }

    void resetConsumeOffset() { mConsumeOffset = 0; }
    void resetProduceOffset() { mUsed = 0; }

    uint8_t* getBytes() { return mBytes; }

    bool dumpToFile(const char* filename) const;
    static RenderCommandBuffer* loadFromFile(const char* filename);

    void setFrameSize(int width, int height) {
        mWidth = width;
        mHeight = height;
    }

    void getFrameSize(int& width, int& height) {
        width = mWidth;
        height = mHeight;
    }

private:
    uint8_t mBytes[RENDER_COMMAND_BUFFER_DEFAULT_SIZE];
    size_t mUsed = 0;
    size_t mConsumeOffset = 0;
    // These are somewhat awkward, and used to achieve compatibility with the buffer based geometry
    // calculations. Effectively this is the size that the buffer would have been in the normal
    // rendering mode.
    int mWidth = 0;
    int mHeight = 0;
};
} // namespace android
