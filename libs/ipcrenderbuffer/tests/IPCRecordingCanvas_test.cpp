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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <gui/RenderCommandBuffer.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <gui/RenderCommandBufferProducer.h>

#include <android/ipcrenderbuffer/IPCRecordingCanvas.h>
#include <android/ipcrenderbuffer/RenderBufferHelpers.h>

#include <gtest/gtest.h>

#include <android-base/file.h>
#include <android/bitmap.h>
#include <android/data_space.h>
#include <filesystem>
#include <fstream>

#include <SkCanvas.h>
#include <SkData.h>
#include <SkEncodedImageFormat.h>
#include <SkFont.h>
#include <SkFontMgr.h>
#include <SkFontScanner.h>
#include <SkFontScanner_FreeType.h>
#include <SkImage.h>
#include <SkPngEncoder.h>
#include <SkStream.h>
#include <SkSurface.h>
#include <SkTextBlob.h>
#include <SkTypeface.h>

#include "src/ports/SkFontMgr_custom.h"

namespace android {

#define CANVAS_TEST_SIZE 512

static const std::string kScreenshotPath("/data/local/tmp/libipcrenderbuffer_test_screenshots/");

static void writePng(const std::filesystem::path& path, const void* pixels, uint32_t width,
                     uint32_t height, uint32_t stride) {
    AndroidBitmapInfo info{
            .width = width,
            .height = height,
            .stride = stride,
            .format = ANDROID_BITMAP_FORMAT_RGBA_8888,
            .flags = ANDROID_BITMAP_FLAGS_ALPHA_OPAQUE,
    };

    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    auto writeFunc = [](void* filePtr, const void* data, size_t size) -> bool {
        auto file = reinterpret_cast<std::ofstream*>(filePtr);
        file->write(reinterpret_cast<const char*>(data), size);
        return file->good();
    };

    int compressResult = AndroidBitmap_compress(&info, ADATASPACE_SRGB, pixels,
                                                ANDROID_BITMAP_COMPRESS_FORMAT_PNG,
                                                /*(ignored) quality=*/100, &file, writeFunc);
    ASSERT_EQ(compressResult, ANDROID_BITMAP_RESULT_SUCCESS);
    file.close();
}

struct SoftwareSurfaceAndCanvas {
    SoftwareSurfaceAndCanvas(int width, int height) {
        SkImageInfo info = SkImageInfo::MakeN32Premul(width, height); // Example size
        const size_t minRowBytes = info.minRowBytes();
        const size_t size = info.computeMinByteSize();
        pixels = new SkPMColor[size];

        // Create SkSurface using SkSurfaces::WrapPixels
        surface = SkSurfaces::WrapPixels(info, pixels, minRowBytes);
        if (!surface) {
            ALOGE("Failed to create SkSurface with SkSurfaces::WrapPixels");
            delete[] pixels;
            canvas = nullptr;
            surface = nullptr;
            return;
        }
        canvas = surface->getCanvas(); // Get Canvas from the Surface
    }
    ~SoftwareSurfaceAndCanvas() { delete[] pixels; }
    sk_sp<SkSurface> surface;
    SkCanvas* canvas;
    SkPMColor* pixels;
};

bool compareData(const sk_sp<SkData>& a, const sk_sp<SkData>& b) {
    if (a->size() != b->size()) {
        return false;
    }
    return (memcmp(a->data(), b->data(), a->size()) == 0);
}

sk_sp<SkData> surfaceToPNGData(const sk_sp<SkSurface>& surface) {
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        ALOGE("Failed to create image snapshot");
        return nullptr;
    }

    SkPngEncoder::Options options;
    return SkPngEncoder::Encode(nullptr, image.get(), options);
}

bool compareSurfaces(const sk_sp<SkSurface>& a, const sk_sp<SkSurface>& b, const char* testName) {
    auto da = surfaceToPNGData(a);
    auto db = surfaceToPNGData(b);

    SkPixmap pixmapA;
    a->peekPixels(&pixmapA);
    writePng(kScreenshotPath + testName + "_direct.png", pixmapA.addr(), a->width(), a->height(),
             pixmapA.rowBytes());

    SkPixmap pixmapB;
    b->peekPixels(&pixmapB);
    writePng(kScreenshotPath + testName + "_ipc.png", pixmapB.addr(), b->width(), b->height(),
             pixmapB.rowBytes());

    return compareData(da, db);
}

class TestFontLoader : public SkFontMgr_Custom::SystemFontLoader {
public:
    TestFontLoader() {}
    void loadSystemFonts(const SkFontScanner* scanner,
                         SkFontMgr_Custom::Families* families) const override {
        std::string fontPath =
                android::base::GetExecutableDirectory() + "/testdata/Roboto-Regular.ttf";

        auto fontData = SkStreamAsset::MakeFromFile(fontPath.c_str());

        auto typeface = SkTypeface_FreeType::MakeFromStream(std::move(fontData), SkFontArguments());

        SkString familyName;
        typeface->getFamilyName(&familyName);
        SkFontStyleSet_Custom* family = new SkFontStyleSet_Custom(familyName);
        families->push_back().reset(family);
        family->appendTypeface(typeface);
    }
};

class IPCRecordingCanvasTest : public ::testing::Test {
public:
    IPCRecordingCanvasTest()
          : mDirectCanvas(CANVAS_TEST_SIZE, CANVAS_TEST_SIZE),
            mIPCCanvasBackend(CANVAS_TEST_SIZE, CANVAS_TEST_SIZE) {}

protected:
    void SetUp() override {
        Parcel p;
        mCache.fontManager = sk_make_sp<SkFontMgr_Custom>(TestFontLoader());

        mIPCRecordingCanvas.getRenderCommandBufferProducer()->writeToParcel(&p);
        p.setDataPosition(0);
        RenderCommandBufferConsumer::readFromParcel(p, &mRenderCommandBufferConsumer);
    }
    void TearDown() override {}

public:
    void renderWithIPCCanvas(const std::function<void(SkCanvas*)>& doDraw) {
        mIPCRecordingCanvas.startRecording();
        doDraw(&mIPCRecordingCanvas);
        mIPCRecordingCanvas.endRecording();
        renderCommandBufferToCanvas(&mCache, &mRenderCommandBufferConsumer,
                                    mIPCCanvasBackend.canvas, [&](int) {});
    }

    bool compareRendering(const std::function<void(SkCanvas*)>& doDraw, const char* testName) {
        doDraw(mDirectCanvas.canvas);
        renderWithIPCCanvas(doDraw);
        return compareSurfaces(mDirectCanvas.surface, mIPCCanvasBackend.surface, testName);
    }

    SoftwareSurfaceAndCanvas mDirectCanvas;
    SoftwareSurfaceAndCanvas mIPCCanvasBackend;
    IPCRecordingCanvas mIPCRecordingCanvas;
    RenderCommandBufferConsumer mRenderCommandBufferConsumer;
    IPCResourceCache mCache;
};

TEST_F(IPCRecordingCanvasTest, ClearToRed) {
    auto clearToRed = [&](SkCanvas* c) { c->clear(SK_ColorRED); };
    ASSERT_TRUE(compareRendering(clearToRed, "ClearToRed"));
}

TEST_F(IPCRecordingCanvasTest, DrawRect) {
    auto drawRect = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorBLUE);
        c->drawRect(SkRect::MakeWH(100, 100), paint);
    };
    ASSERT_TRUE(compareRendering(drawRect, "DrawRect"));
}

TEST_F(IPCRecordingCanvasTest, DrawOval) {
    auto drawOval = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorGREEN);
        c->drawOval(SkRect::MakeWH(100, 150), paint);
    };
    ASSERT_TRUE(compareRendering(drawOval, "DrawOval"));
}

TEST_F(IPCRecordingCanvasTest, Translate) {
    auto translateAndDraw = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorYELLOW);
        c->save();
        c->translate(50, 50);
        c->drawRect(SkRect::MakeWH(100, 100), paint);
        c->restore();
    };
    ASSERT_TRUE(compareRendering(translateAndDraw, "Translate"));
}

TEST_F(IPCRecordingCanvasTest, Scale) {
    auto scaleAndDraw = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorMAGENTA);
        c->save();
        c->scale(2.0f, 0.5f);
        c->drawRect(SkRect::MakeWH(100, 100), paint);
        c->restore();
    };
    ASSERT_TRUE(compareRendering(scaleAndDraw, "Scale"));
}

TEST_F(IPCRecordingCanvasTest, ClipRect) {
    auto clipAndDraw = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorCYAN);
        c->save();
        c->clipRect(SkRect::MakeLTRB(20, 20, 80, 80));
        c->drawRect(SkRect::MakeWH(100, 100), paint);
        c->restore();
    };
    ASSERT_TRUE(compareRendering(clipAndDraw, "ClipRect"));
}

TEST_F(IPCRecordingCanvasTest, DrawPaint) {
    auto drawPaint = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorBLACK);
        c->drawPaint(paint);
    };
    ASSERT_TRUE(compareRendering(drawPaint, "DrawPaint"));
}

TEST_F(IPCRecordingCanvasTest, DrawTextBlob) {
    auto drawTextBlob = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorDKGRAY);
        SkFont font;
        font.setSize(64);
        font.setTypeface(mCache.fontManager->matchFamilyStyle("Roboto", SkFontStyle()));
        auto blob = SkTextBlob::MakeFromString("Skia", font);
        c->drawTextBlob(blob, 50, 100, paint);
    };
    ASSERT_TRUE(compareRendering(drawTextBlob, "DrawTextBlob"));
}

} // namespace android