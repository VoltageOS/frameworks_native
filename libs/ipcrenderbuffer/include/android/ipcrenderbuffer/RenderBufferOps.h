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

#include <SkRect.h>
#include <gui/RenderCommandBuffer.h>
#include <gui/RenderCommandBufferConsumer.h>

#include <SkAndroidFrameworkUtils.h>
#include <SkCanvas.h>
/*
#include <SkCanvasPriv.h>
*/
#include <SkCanvasVirtualEnforcer.h>
#include <SkColor.h>
#include <SkDrawable.h>
// #include <SkGainmapInfo.h>
#include <SkBitmap.h>
#include <SkImage.h>
#include <SkImageAndroid.h>
#include <SkNoDrawCanvas.h>
#include <SkPaint.h>
#include <SkPath.h>
#include <SkPixmap.h>
#include <SkRRect.h>
#include <SkRect.h>
#include <SkRegion.h>
#include <SkRuntimeEffect.h>
#include <SkSerialProcs.h>
#include <SkTextBlob.h>

#include <SkStream.h>
#include <sys/stat.h>
// #include <ports/SkFontMgr_android.h>
#include <SkColorSpace.h>
#include <SkData.h>
#include <SkFontArguments.h>
#include <SkFontMgr.h>
#include <SkFontMgr_android.h>
#include <SkFontMgr_android_ndk.h>
#include <SkFontMgr_empty.h>

#include <functional>
#include <map>

#include <android/ipcrenderbuffer/RenderBufferDebugUtils.h>
#include <android/ipcrenderbuffer/RenderBufferOpTypes.h>
#include <android/ipcrenderbuffer/RenderBufferShmemImageInfo.h>
#include <android/ipcrenderbuffer/RenderBufferShmemPaint.h>

#define IPCRENDERBUFFER_UNIMPLEMENTED_IS_FATAL 0
#ifdef IPCRENDERRBUFFER_UNIMPLEMENTED_IS_FATAL
#define IPCRENDERBUFFER_UNIMPLEMENTED LOG_ALWAYS_FATAL("Not implemented %s", __FUNCTION)
#else
#define IPCRENDERBUFFER_UNIMPLEMENTED ALOGE("Not implemented %s", __FUNCTION__)
#endif

namespace android {

// Derived from RecordingCanvas.cpp
struct SaveOp final : IPCRenderBufferOp {
    SaveOp() {
        size = sizeof(SaveOp);
        type = kType;
    }
    static const auto kType = TYPE_SAVE;
    void draw(SkCanvas* c, const SkMatrix&) { c->save(); }

    std::string toString() const { return std::string("SaveOp"); }
};

struct RestoreOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_RESTORE;
    RestoreOp() {
        size = sizeof(RestoreOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { c->restore(); }
    std::string toString() const { return std::string("RestoreOp"); }
};

struct SaveLayerOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVELAYER;
    SaveLayerOp() {
        size = sizeof(SaveLayerOp);
        type = kType;
        IPCRENDERBUFFER_UNIMPLEMENTED;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return std::string("SaveLayerOp"); }
};

struct SaveBehindOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVEBEHIND;
    SaveBehindOp(const SkRect& subset_) {
        type = kType;
        if (subset_.isEmpty()) {
            subset.setEmpty();
        } else {
            this->subset = subset_;
        }
        size = sizeof(SaveBehindOp);
    }

    SkRect subset;
    void draw(SkCanvas* c, const SkMatrix&) { SkAndroidFrameworkUtils::SaveBehind(c, &subset); }
    std::string toString() const {
        return std::string("SaveBehindOp subset: ") + rectToString(subset);
    }
};

struct ConcatOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CONCAT;
    ConcatOp(const SkM44& matrix) : matrix(matrix) {
        type = kType;
        size = sizeof(ConcatOp);
    }
    SkM44 matrix;
    void draw(SkCanvas* c, const SkMatrix&) { c->concat(matrix); }
    std::string toString() const {
        return std::string("ConcatOp matrix: ") + skmatrixToString(matrix);
    }
};

struct SetMatrixOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SETMATRIX;
    SetMatrixOp(const SkM44& matrix) : matrix(matrix) {
        type = kType;
        size = sizeof(SetMatrixOp);
    }
    SkM44 matrix;
    void draw(SkCanvas* c, const SkMatrix& original) { c->setMatrix(SkM44(original) * matrix); }
    std::string toString() const {
        return std::string("SetMatrixOp matrix: ") + skmatrixToString(matrix);
    }
};

struct ScaleOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SCALE;
    ScaleOp(SkScalar sx, SkScalar sy) : sx(sx), sy(sy) {
        type = kType;
        size = sizeof(ScaleOp);
    }
    SkScalar sx, sy;
    void draw(SkCanvas* c, const SkMatrix&) { c->scale(sx, sy); }
    std::string toString() const {
        return std::string("ScaleOp sx: ") + std::to_string(sx) + std::string(" sy: ") +
                std::to_string(sy);
    }
};

struct TranslateOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_TRANSLATE;
    TranslateOp(SkScalar dx, SkScalar dy) : dx(dx), dy(dy) {
        type = kType;
        size = sizeof(TranslateOp);
    }
    SkScalar dx, dy;
    void draw(SkCanvas* c, const SkMatrix&) { c->translate(dx, dy); }
    std::string toString() const {
        return std::string("TranslateOp dx: ") + std::to_string(dx) + std::string(" dy: ") +
                std::to_string(dy);
    }
};

struct ClipPathOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPPATH;
    ClipPathOp(const SkPath& path, SkClipOp op, bool aa) : op(op), aa(aa) {
        type = kType;
        size = sizeof(ClipPathOp);
        IPCRENDERBUFFER_UNIMPLEMENTED;
    }

    SkClipOp op;
    bool aa;
    void draw(SkCanvas* c, const SkMatrix&) {
        (void)c;
        IPCRENDERBUFFER_UNIMPLEMENTED;
    }
    std::string toString() const { return "ClipPathOp"; }
};

struct ClipRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPRECT;
    ClipRectOp(const SkRect& rect, SkClipOp op, bool aa) : rect(rect), op(op), aa(aa) {
        type = kType;
        size = sizeof(ClipRectOp);
    }
    SkRect rect;
    SkClipOp op;
    bool aa;
    void draw(SkCanvas* c, const SkMatrix&) { c->clipRect(rect, op, aa); }
    std::string toString() const {
        return std::string("ClipRectOp: rect: ") + rectToString(rect) + std::string(" op: ") +
                std::to_string((int)op) + std::string(" aa: ") + std::to_string(aa);
    }
};

struct ClipRRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPRRECT;
    ClipRRectOp(const SkRRect& rrect, SkClipOp op, bool aa) : rrect(rrect), op(op), aa(aa) {
        type = kType;
        size = sizeof(ClipRRectOp);
    }
    SkRRect rrect;
    SkClipOp op;
    bool aa;
    void draw(SkCanvas* c, const SkMatrix&) { c->clipRRect(rrect, op, aa); }
    std::string toString() const {
        std::string rectAsString = std::string(rrect.dumpToString(false).c_str());
        return std::string("ClipRRectOp: rrect: ") + rectAsString + std::string(" op: ") +
                std::to_string((int)op) + std::string(" aa: ") + std::to_string(aa);
    }
};

struct ClipRegionOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPREGION;
    ClipRegionOp(const SkRegion& region, SkClipOp op) : region(region), op(op) {
        type = kType;
        IPCRENDERBUFFER_UNIMPLEMENTED;
    }
    SkRegion region;
    SkClipOp op;

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "ClipRegionOp"; }
};

struct ClipShaderOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPSHADER;
    ClipShaderOp(const sk_sp<SkShader>& shader, SkClipOp op) : shader(shader), op(op) {
        type = kType;
        size = sizeof(ClipShaderOp);
        IPCRENDERBUFFER_UNIMPLEMENTED;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    sk_sp<SkShader> shader;
    SkClipOp op;
    std::string toString() const { return "ClipShaderOp"; }
};

struct ResetClipOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_RESETCLIP;
    ResetClipOp() {
        size = sizeof(ResetClipOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { SkAndroidFrameworkUtils::ResetClip(c); }
    std::string toString() const { return "ResetClipOp"; }
};

struct DrawPaintOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPAINT;
    DrawPaintOp(const SkPaint& p) {
        size = sizeof(DrawPaintOp);
        paint = toShmemPaint(p);
        type = kType;
    }
    ShmemPaint paint;
    void draw(SkCanvas* c, const SkMatrix&) { c->drawPaint(fromShmemPaint(paint)); }
    std::string toString() const { return "DrawPaintOp" + shmemPaintToString(paint); }
};

struct DrawBehindOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWBEHIND;
    DrawBehindOp(const SkPaint& p) {
        size = sizeof(DrawBehindOp);
        paint = toShmemPaint(p);
        type = kType;
        IPCRENDERBUFFER_UNIMPLEMENTED;
    }
    ShmemPaint paint;
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawBehindOp"; }
};

// TODO(b/448196665): Generic flexible design for variable size ops.
struct DrawPathOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPATH;
    DrawPathOp(uint8_t* blob, size_t bs, const SkPaint& p) {
        size = sizeof(DrawPathOp) + bs;
        paint = toShmemPaint(p);
        type = kType;
        blobSize = bs;

        memcpy(&(blobStorage[0]), blob, bs);
    }
    SkPath* path;
    ShmemPaint paint;
    bool hasDeserializedPath = false;
    size_t blobSize;
    void draw(SkCanvas* c, const SkMatrix&) {
        if (hasDeserializedPath == false) {
            // TODO: Fix leak
            path = new SkPath();
            path->readFromMemory((void*)&(blobStorage[0]), blobSize);
            hasDeserializedPath = true;
        }
        c->drawPath(*(path), fromShmemPaint(paint));
    }
    std::string toString() const { return "DrawPathOp"; }
    void resetForReplay() { hasDeserializedPath = false; }

    // TODO: Fix size
    uint8_t blobStorage[1024]; // Must be last member
};

struct DrawRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWRECT;
    DrawRectOp(const SkRect& r, const SkPaint& p) {
        size = sizeof(DrawRectOp);
        rect = r;
        paint = toShmemPaint(p);
        type = kType;
    }
    SkRect rect;
    ShmemPaint paint;
    void draw(SkCanvas* c, const SkMatrix&) { c->drawRect(rect, fromShmemPaint(paint)); }
    std::string toString() const {
        return std::string("DrawRectOp rect(") + rectToString(rect) + ") " +
                std::string(" paint: ") + shmemPaintToString(paint);
    }
};

struct DrawRegionOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWREGION;
    DrawRegionOp(const SkRegion& r, const SkPaint& p) {
        size = sizeof(DrawRegionOp);
        region = r;
        paint = toShmemPaint(p);
        type = kType;
    }
    SkRegion region;
    ShmemPaint paint;
    void draw(SkCanvas* c, const SkMatrix&) { c->drawRegion(region, fromShmemPaint(paint)); }
    std::string toString() const { return "DrawRegionOp"; }
};

struct DrawOvalOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWOVAL;
    DrawOvalOp(const SkRect& o, const SkPaint& p) {
        size = sizeof(DrawOvalOp);
        oval = o;
        paint = toShmemPaint(p);
        type = kType;
    }
    SkRect oval;
    ShmemPaint paint;
    void draw(SkCanvas* c, const SkMatrix&) { c->drawOval(oval, fromShmemPaint(paint)); }
    std::string toString() const { return std::string("DrawOvalOp") + rectToString(oval); }
};

struct DrawArcOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWARC;
    DrawArcOp(const SkRect& oval_, SkScalar startAngle_, SkScalar sweepAngle_, bool useCenter_,
              const SkPaint& paint_) {
        size = sizeof(DrawArcOp);
        type = kType;
        paint = toShmemPaint(paint_);
        oval = oval_;
        startAngle = startAngle_;
        sweepAngle = sweepAngle_;
        useCenter = useCenter_;
    }
    SkRect oval;
    SkScalar startAngle;
    SkScalar sweepAngle;
    bool useCenter;
    ShmemPaint paint;
    void draw(SkCanvas* c, const SkMatrix&) {
        c->drawArc(oval, startAngle, sweepAngle, useCenter, fromShmemPaint(paint));
    }
    std::string toString() const {
        return std::string("DrawArcOp") + rectToString(oval) + std::string(" startAngle: ") +
                std::to_string(startAngle) + std::string(" sweepAngle: ") +
                std::to_string(sweepAngle) + std::string(" useCenter: ") +
                std::to_string(useCenter);
    }
};

struct DrawRRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWRRECT;
    DrawRRectOp(const SkRRect& rr, const SkPaint& p) {
        size = sizeof(DrawRRectOp);
        paint = toShmemPaint(p);
        rrect = rr;
        type = kType;
    }
    SkRRect rrect;
    ShmemPaint paint;
    void draw(SkCanvas* c, const SkMatrix&) { c->drawRRect(rrect, fromShmemPaint(paint)); }
    std::string toString() const {
        return std::string("DrawRRectOp") + std::string(rrect.dumpToString(false).c_str());
    }
};

struct DrawAnnotationOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWANNOTATION;
    DrawAnnotationOp(const SkRect& rect, const char* text, SkData* data) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawAnnotationOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawAnnotationOp"; }
};

struct DrawDrawableOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWDRAWABLE;
    DrawDrawableOp(SkDrawable* drawable, const SkMatrix* matrix) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawDrawableOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawDrawableOp"; }
};

struct DrawPictureOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPICTURE;
    DrawPictureOp(const SkPicture* picture, const SkMatrix* matrix, const SkPaint* paint) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawPictureOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawPictureOp"; }
};

struct DrawImageOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWIMAGE;
    DrawImageOp(const SkImage* image, SkScalar x, SkScalar y, const SkSamplingOptions& sampling,
                const SkPaint* paint) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawImageOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawImageOp"; }
};

// TODO(b/448196792): Implement hardware bitmap support
struct DrawImageRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWIMAGERECT;
    int offset;
    SkRect srcRect;
    SkRect dstRect;
    SkSamplingOptions sampling;
    ShmemPaint paint;
    int constraint;
    bool hasPaint;
    int uniqueId;
    bool isHardware;

    DrawImageRectOp() = default;

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    virtual std::string toString() const { return "DrawImageRectOp"; }
};

// TODO(b/448197462): Move over implementation from prototype.
struct DrawTextBlobOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWTEXTBLOB;
    ShmemPaint paint;
    size_t blobSize;
    SkScalar x;
    SkScalar y;
    sk_sp<SkTypeface> typeface = nullptr;

    DrawTextBlobOp(uint8_t* blob, size_t bs, SkScalar x_in, SkScalar y_in, const SkPaint& p) {
        size = sizeof(DrawTextBlobOp) + bs;
        blobSize = bs;
        type = kType;
        paint = toShmemPaint(p);
        memcpy(&(blobStorage[0]), blob, bs);

        x = x_in;
        y = y_in;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawTextBlobOp"; }

    void resetForReplay() { typeface = nullptr; }

    // TODO(b/448196665): Generic/safer system for flexible storage.
    uint8_t blobStorage[1]; // Must be last member
};

struct DrawPatchOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPATCH;
    DrawPatchOp(const SkPoint points[12], const SkColor colors[4], const SkPoint texCoords[4],
                SkBlendMode mode, const SkPaint& paint) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawPatchOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawPatchOp"; }
};

struct DrawPointsOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPOINTS;
    DrawPointsOp(SkCanvas::PointMode mode, size_t count, const SkPoint* points,
                 const SkPaint& paint) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawPointsOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawPointsOp"; }
};

struct DrawVerticesOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWVERTICES;
    DrawVerticesOp(const SkVertices* vertices, SkBlendMode mode, const SkPaint& paint) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawVerticesOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawVerticesOp"; }
};

/*struct DrawMeshOp final : IPCRenderBufferOp {
  static const auto kType = TYPE_DRAWMESH;
  DrawMeshOp(const Mesh& mesh, const SkPaint& paint) {
    ALOGE("Not implemented %s", __FUNCTION__);
  }
};*/

struct DrawSkMeshOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWMESH;
    DrawSkMeshOp(const SkMesh& mesh, sk_sp<SkBlender> blender, const SkPaint& paint) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawSkMeshOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawSkMeshOp"; }
};

struct DrawAtlasOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWATLAS;
    DrawAtlasOp(const SkImage* atlas, const SkRSXform* xform, const SkRect* tex,
                const SkColor* colors, int count, SkBlendMode mode,
                const SkSamplingOptions& sampling, const SkRect* cull, const SkPaint* paint) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        size = sizeof(DrawAtlasOp);
        type = kType;
    }
    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawAtlasOp"; }
};

struct DrawProxySurfaceControlOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPROXYSURFACECONTROL;
    DrawProxySurfaceControlOp(int id) {
        size = sizeof(DrawProxySurfaceControlOp);
        type = kType;
        proxyId = id;
    }
    int proxyId;

    void draw(SkCanvas* c, const SkMatrix&) {
        LOG_ALWAYS_FATAL_IF("DrawProxySurfaceControlOp::draw unexpected");
    }

    std::string toString() const { return "DrawProxySurfaceControlOp"; }
};
} // namespace android
