/*
 * Copyright (C) 2026 The Android Open Source Project
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"

#include <com_android_graphics_libgui_flags.h>
#include <android/ipcrenderbuffer/IPCRecordingCanvas.h>
#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <SkColor.h>
#include "TransactionTestHarnesses.h"

#include <map>
#include <memory>

namespace android {

class ComparisonHarness {
public:
    ComparisonHarness(uint32_t layerType) : mLayerType(layerType) {}

    void SetUp() {
        mClient = sp<SurfaceComposerClient>::make();
        ASSERT_EQ(NO_ERROR, mClient->initCheck());

        const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
        ASSERT_FALSE(ids.empty());
        mDisplay = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
        ASSERT_FALSE(mDisplay == nullptr);

        ui::DisplayMode mode;
        ASSERT_EQ(NO_ERROR, SurfaceComposerClient::getActiveDisplayMode(mDisplay, &mode));

        SurfaceComposerClient::Transaction t;
        t.setDisplayLayerStack(mDisplay, ui::DEFAULT_LAYER_STACK);
        t.apply();
    }

    void TearDown() {
        mClient->dispose();
        mClient.clear();
    }

    sp<SurfaceControl> createLayer(const char* name, uint32_t width, uint32_t height,
                                   uint32_t flags = 0, SurfaceControl* parent = nullptr) {
        sp<IBinder> parentHandle = (parent) ? parent->getHandle() : nullptr;

        uint32_t type = mLayerType;
        uint32_t w = width;
        uint32_t h = height;
        if (mLayerType == LAYER_TYPE_RENDER_COMMAND_BUFFER) {
            type = ISurfaceComposerClient::eFXSurfaceEffect;
            w = 0;
            h = 0;
        }

        auto layer = mClient->createSurface(String8(name), w, h, PIXEL_FORMAT_RGBA_8888,
                                            flags | type, parentHandle);
        EXPECT_NE(nullptr, layer.get());

        SurfaceComposerClient::Transaction t;
        t.setLayerStack(layer, ui::DEFAULT_LAYER_STACK)
                .setLayer(layer, std::numeric_limits<int32_t>::max() - 256);

        if (mLayerType == LAYER_TYPE_RENDER_COMMAND_BUFFER) {
            auto cache = std::make_unique<IPCClientResourceCache>();
            auto canvas = std::make_shared<IPCRecordingCanvas>(*cache);
            t.setRenderCommandBuffer(layer, canvas->getRenderCommandBufferProducer());
            mRenderResourceCaches[layer.get()] = std::move(cache);
            mRenderCommandCanvases[layer.get()] = canvas;
            mRenderCommandFrameIds[layer.get()] = 0;
        }
        t.apply();
        return layer;
    }

    void fillLayerColor(const sp<SurfaceControl>& layer, const Color& color, uint32_t bufferWidth,
                        uint32_t bufferHeight) {
        if (mLayerType == LAYER_TYPE_RENDER_COMMAND_BUFFER) {
            auto it = mRenderCommandCanvases.find(layer.get());
            ASSERT_NE(it, mRenderCommandCanvases.end());
            auto canvas = it->second;
            auto& frameId = mRenderCommandFrameIds[layer.get()];
            frameId++;

            canvas->storeSize(bufferWidth, bufferHeight);
            canvas->startRecording();
            canvas->drawColor(SkColorSetARGB(color.a, color.r, color.g, color.b), SkBlendMode::kSrc);
            canvas->endRecording();

            SurfaceComposerClient::Transaction()
                    .setRenderCommandBufferFrameId(layer, frameId)
                    .setCrop(layer, Rect(bufferWidth, bufferHeight))
                    .apply(true);
        } else {
            using android::hardware::graphics::common::V1_1::BufferUsage;
            sp<GraphicBuffer> buffer =
                    sp<GraphicBuffer>::make(bufferWidth, bufferHeight, PIXEL_FORMAT_RGBA_8888, 1u,
                                            static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN |
                                                                  BufferUsage::CPU_WRITE_OFTEN |
                                                                  BufferUsage::COMPOSER_OVERLAY |
                                                                  BufferUsage::GPU_TEXTURE),
                                            "test");
            TransactionUtils::fillGraphicBufferColor(buffer, Rect(0, 0, bufferWidth, bufferHeight),
                                                     color);
            SurfaceComposerClient::Transaction().setBuffer(layer, buffer).apply();
        }
    }

    std::unique_ptr<ScreenCapture> screenshot() {
        std::unique_ptr<ScreenCapture> sc;
        ScreenCapture::captureScreen(&sc);
        return sc;
    }

private:
    uint32_t mLayerType;
    sp<SurfaceComposerClient> mClient;
    sp<IBinder> mDisplay;
    std::map<SurfaceControl*, std::unique_ptr<IPCClientResourceCache>> mRenderResourceCaches;
    std::map<SurfaceControl*, std::shared_ptr<IPCRecordingCanvas>> mRenderCommandCanvases;
    std::map<SurfaceControl*, uint64_t> mRenderCommandFrameIds;
};

class LayerRenderComparisonTest : public LayerTransactionTest {
protected:
    void runComparisonTest(std::function<void(ComparisonHarness&)> testLogic) {
        if (!com_android_graphics_libgui_flags_out_of_process_rendering()) {
            GTEST_SKIP();
        }

        std::unique_ptr<ScreenCapture> scBuffer;
        std::unique_ptr<ScreenCapture> scRender;

        {
            ComparisonHarness harness(ISurfaceComposerClient::eFXSurfaceBufferState);
            harness.SetUp();
            testLogic(harness);
            scBuffer = harness.screenshot();
            harness.TearDown();
        }

        {
            ComparisonHarness harness(LAYER_TYPE_RENDER_COMMAND_BUFFER);
            harness.SetUp();
            testLogic(harness);
            scRender = harness.screenshot();
            harness.TearDown();
        }

        scBuffer->expectBufferMatches(*scRender);
    }
};

TEST_F(LayerRenderComparisonTest, ChildLayerPositioning) {
    runComparisonTest([](ComparisonHarness& harness) {
        sp<SurfaceControl> parent = harness.createLayer("Parent", 64, 64);
        harness.fillLayerColor(parent, Color::RED, 64, 64);

        sp<SurfaceControl> child = harness.createLayer("Child", 32, 32, 0, parent.get());
        harness.fillLayerColor(child, Color::BLUE, 32, 32);

        SurfaceComposerClient::Transaction()
                .show(parent)
                .show(child)
                .setPosition(child, 16, 16)
                .apply();
    });
}

TEST_F(LayerRenderComparisonTest, ChildLayerCropping) {
    runComparisonTest([](ComparisonHarness& harness) {
        sp<SurfaceControl> parent = harness.createLayer("Parent", 64, 64);
        harness.fillLayerColor(parent, Color::RED, 64, 64);

        sp<SurfaceControl> child = harness.createLayer("Child", 32, 32, 0, parent.get());
        harness.fillLayerColor(child, Color::BLUE, 32, 32);

        SurfaceComposerClient::Transaction()
                .show(parent)
                .show(child)
                .setPosition(child, 0, 0)
                .setCrop(parent, Rect(0, 0, 16, 16))
                .apply();
    });
}

TEST_F(LayerRenderComparisonTest, ChildLayerScaling) {
    runComparisonTest([](ComparisonHarness& harness) {
        sp<SurfaceControl> parent = harness.createLayer("Parent", 64, 64);
        harness.fillLayerColor(parent, Color::RED, 64, 64);

        sp<SurfaceControl> child = harness.createLayer("Child", 32, 32, 0, parent.get());
        harness.fillLayerColor(child, Color::BLUE, 32, 32);

        SurfaceComposerClient::Transaction()
                .show(parent)
                .show(child)
                .setPosition(child, 0, 0)
                .setMatrix(child, 2.0f, 0.0f, 0.0f, 2.0f)
                .apply();
    });
}

TEST_F(LayerRenderComparisonTest, ChildLayerAlpha) {
    runComparisonTest([](ComparisonHarness& harness) {
        sp<SurfaceControl> parent = harness.createLayer("Parent", 64, 64);
        harness.fillLayerColor(parent, Color::RED, 64, 64);

        sp<SurfaceControl> child = harness.createLayer("Child", 32, 32, 0, parent.get());
        harness.fillLayerColor(child, Color::BLUE, 32, 32);

        SurfaceComposerClient::Transaction()
                .show(parent)
                .show(child)
                .setPosition(child, 16, 16)
                .setAlpha(child, 0.5f)
                .apply();
    });
}

} // namespace android


#pragma clang diagnostic pop
