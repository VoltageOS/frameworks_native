/*
 * Copyright 2024 The Android Open Source Project
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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "KawaseBlurDualFilter.h"
#include <SkAlphaType.h>
#include <SkBlendMode.h>
#include <SkCanvas.h>
#include <SkData.h>
#include <SkPaint.h>
#include <SkRRect.h>
#include <SkRuntimeEffect.h>
#include <SkShader.h>
#include <SkSize.h>
#include <SkString.h>
#include <SkSurface.h>
#include <SkTileMode.h>
#include <include/gpu/GpuTypes.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <log/log.h>
#include <utils/Trace.h>

#include "RuntimeEffectManager.h"

namespace android {
namespace renderengine {
namespace skia {

// A shader to sample each vertex of a square, plus the original fragment coordinate,
// using a total of 5 samples.
const SkString kEffectSource_KawaseBlurDualFilter_LowSampleBlurEffect(R"(
    uniform shader child;
    uniform float in_blurOffset;
    uniform float in_crossFade;
    uniform float in_weightedCrossFade;
    uniform float in_saturation;

    const float2 STEP_0 = float2( 0.707106781, 0.707106781);
    const float2 STEP_1 = float2( 0.707106781, -0.707106781);
    const float2 STEP_2 = float2(-0.707106781, -0.707106781);
    const float2 STEP_3 = float2(-0.707106781, 0.707106781);

    half4 main(float2 xy) {
        half3 c = child.eval(xy).rgb;

        c += child.eval(xy + STEP_0 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_1 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_2 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_3 * in_blurOffset).rgb;

        half lum = dot(c, half3(0.2126, 0.7152, 0.0722));
        c = mix(half3(lum), c, in_saturation);

        return half4(c * in_weightedCrossFade, in_crossFade);
    }
)");

// A shader to sample each vertex of a unit regular heptagon, plus the original fragment
// coordinate, using a total of 8 samples.
const SkString kEffectSource_KawaseBlurDualFilter_HighSampleBlurEffect(R"(
    uniform shader child;
    uniform float in_blurOffset;

    const float2 STEP_0 = float2( 1.0, 0.0);
    const float2 STEP_1 = float2( 0.623489802,  0.781831482);
    const float2 STEP_2 = float2(-0.222520934,  0.974927912);
    const float2 STEP_3 = float2(-0.900968868,  0.433883739);
    const float2 STEP_4 = float2( 0.900968868, -0.433883739);
    const float2 STEP_5 = float2(-0.222520934, -0.974927912);
    const float2 STEP_6 = float2(-0.623489802, -0.781831482);

    half4 main(float2 xy) {
        half3 c = child.eval(xy).rgb;

        c += child.eval(xy + STEP_0 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_1 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_2 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_3 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_4 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_5 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_6 * in_blurOffset).rgb;

        return half4(c * 0.125, 1.0);
    }
)");

KawaseBlurDualFilter::KawaseBlurDualFilter(RuntimeEffectManager& effectManager)
      : BlurFilter(effectManager) {
    mLowSampleBlurEffect = effectManager.mKnownEffects[kKawaseBlurDualFilter_LowSampleBlurEffect];
    mHighSampleBlurEffect = effectManager.mKnownEffects[kKawaseBlurDualFilter_HighSampleBlurEffect];
}

void KawaseBlurDualFilter::blurInto(const sk_sp<SkSurface>& drawSurface,
                                    const sk_sp<SkImage>& readImage, const float radius,
                                    const float alpha, const float saturation,
                                    const sk_sp<SkRuntimeEffect>& blurEffect) const {
    const float scale = static_cast<float>(drawSurface->width()) / readImage->width();
    SkMatrix blurMatrix = SkMatrix::Scale(scale, scale);
    blurInto(drawSurface,
             readImage->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                   SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone),
                                   blurMatrix),
             radius, alpha, saturation, blurEffect);
}

void KawaseBlurDualFilter::blurInto(const sk_sp<SkSurface>& drawSurface, sk_sp<SkShader> input,
                                    const float radius, const float alpha, const float saturation,
                                    const sk_sp<SkRuntimeEffect>& blurEffect) const {
    SkPaint paint;
    if (radius == 0) {
        paint.setShader(std::move(input));
        paint.setAlphaf(alpha);
    } else {
        SkRuntimeShaderBuilder blurBuilder(blurEffect);
        blurBuilder.child("child") = std::move(input);
        if (blurEffect == mLowSampleBlurEffect) {
            blurBuilder.uniform("in_crossFade") = alpha;
            blurBuilder.uniform("in_weightedCrossFade") = alpha * 0.2f;
            blurBuilder.uniform("in_saturation") = saturation;
        }
        blurBuilder.uniform("in_blurOffset") = radius;
        paint.setShader(blurBuilder.makeShader(nullptr));
    }
    paint.setBlendMode(alpha == 1.0f ? SkBlendMode::kSrc : SkBlendMode::kSrcOver);
    drawSurface->getCanvas()->drawPaint(paint);
}

sk_sp<SkImage> KawaseBlurDualFilter::generate(SkiaGpuContext* context, const uint32_t blurRadius,
                                              const sk_sp<SkImage> input,
                                              const SkRect& blurRect) const {

    if (mCachedBlurredImage && mCachedInputUniqueID == input->uniqueID() &&
        mCachedBlurRadius == blurRadius && mCachedBlurRect == blurRect) {
        return mCachedBlurredImage;
    }

    // Apply a conversion factor of (1 / sqrt(3)) to match Skia's built-in blur as used by
    // RenderEffect. See the comment in SkBlurMask.cpp for reasoning behind this.
    const float radius = blurRadius * 0.57735f * 8.0f;

    // Use a variable number of blur passes depending on the radius. The non-integer part of this
    // calculation is used to mix the final pass into the second-last with an alpha blend.
    constexpr int kMaxSurfaces = 6;
    const float filterDepth = std::min(kMaxSurfaces - 1.0f, radius * kInputScale / 2.5f);
    const int filterPasses = std::min(kMaxSurfaces - 1, static_cast<int>(ceil(filterDepth)));

    auto makeSurface = [&](float scale) -> sk_sp<SkSurface> {
        const auto newW = ceil(static_cast<float>(blurRect.width() / scale));
        const auto newH = ceil(static_cast<float>(blurRect.height() / scale));
        sk_sp<SkSurface> surface =
                context->createRenderTarget(input->imageInfo().makeWH(newW, newH));
        LOG_ALWAYS_FATAL_IF(!surface, "%s: Failed to create surface for blurring!", __func__);
        return surface;
    };

    // Render into surfaces downscaled by 1x, 2x, and 4x from the initial downscale.
    sk_sp<SkSurface> surfaces[kMaxSurfaces] =
            {filterPasses >= 0 ? makeSurface(1 * kInverseInputScale) : nullptr,
             filterPasses >= 1 ? makeSurface(2 * kInverseInputScale) : nullptr,
             filterPasses >= 2 ? makeSurface(4 * kInverseInputScale) : nullptr,
             filterPasses >= 3 ? makeSurface(8 * kInverseInputScale) : nullptr,
             filterPasses >= 4 ? makeSurface(16 * kInverseInputScale) : nullptr,
             filterPasses >= 5 ? makeSurface(32 * kInverseInputScale) : nullptr};

    static const float kDownWeights[6] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f };
    static const float kUpWeights[6]   = { 3.0f, 2.5f, 2.0f, 1.5f, 1.0f, 0.5f };

    // Kawase is an approximation of Gaussian, but behaves differently because it is made up of many
    // simpler blurs. A transformation is required to approximate the same effect as Gaussian.
    float sumSquaredR = powf(kDownWeights[0], 2.0f);
    for (int i = 0; i < filterPasses; i++) {
        float alpha = (i == filterPasses - 1) ? filterDepth - i : 1.0f;
        
        // Add downscale weight
        sumSquaredR += powf(powf(2.0f, i) * alpha * kDownWeights[1 + i] / kInputScale, 2.0f);
        
        // Add upscale weight
        int upIndex = (filterPasses - 1) - i;
        sumSquaredR += powf(powf(2.0f, i + 1) * alpha * kUpWeights[upIndex] / kInputScale, 2.0f);
    }
    const float step = radius / sqrtf(sumSquaredR);

    // Start by downscaling and doing the first blur pass.
    {
        // For sampling Skia's API expects the inverse of what logically seems appropriate. In this
        // case one may expect Translate(blurRect.fLeft, blurRect.fTop) * Scale(kInverseInputScale)
        // but instead we must do the inverse.
        SkMatrix blurMatrix = SkMatrix::Translate(-blurRect.fLeft, -blurRect.fTop);
        blurMatrix.postScale(kInputScale, kInputScale);
        const auto sourceShader =
                input->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                  SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone),
                                  blurMatrix);
        blurInto(surfaces[0], std::move(sourceShader), kDownWeights[0] * step, 1.0f,
                 1.0f, mLowSampleBlurEffect);
    }
    // Next the remaining downscale blur passes.
    for (int i = 0; i < filterPasses; i++) {
        // Blur with the higher sample effect into the smaller buffers, for better visual quality.
        blurInto(surfaces[i + 1], surfaces[i]->makeTemporaryImage(), kDownWeights[1 + i] * step, 1.0f,
                 1.0f, i == 0 ? mLowSampleBlurEffect : mHighSampleBlurEffect);
    }
    // Finally blur+upscale back to our original size.
    for (int i = filterPasses - 1; i >= 0; i--) {
        int upIndex = (filterPasses - 1) - i;
        float sat = (i == 0) ? 1.8f : 1.0f;
        blurInto(surfaces[i], surfaces[i + 1]->makeTemporaryImage(), kUpWeights[upIndex] * step,
                 std::min(1.0f, filterDepth - i), sat, mLowSampleBlurEffect);
    }
    mCachedInputUniqueID = input->uniqueID();
    mCachedBlurRadius = blurRadius;
    mCachedBlurRect = blurRect;
    mCachedBlurredImage = surfaces[0]->makeTemporaryImage();

    return mCachedBlurredImage;
}

} // namespace skia
} // namespace renderengine
} // namespace android
