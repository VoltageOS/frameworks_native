/*
 * Copyright 2026 The Android Open Source Project
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

#include "vulkan/vulkan_core.h"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <log/log.h>
#include <utils/Trace.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "driver.h"

namespace vulkan {
namespace driver {

// Note: two versions of each of these entrypoints, and we need to
// forward to the matching driver function for anything we're not
// handling ourselves.

VKAPI_ATTR VkResult CreatePrivateDataSlotEXT(
        VkDevice device,
        const VkPrivateDataSlotCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkPrivateDataSlot* pPrivateDataSlot) {

    const auto& dispatch = GetData(device).driver;
    return dispatch.CreatePrivateDataSlotEXT(
            device, pCreateInfo, pAllocator, pPrivateDataSlot);
}

VKAPI_ATTR VkResult CreatePrivateDataSlot(
        VkDevice device,
        const VkPrivateDataSlotCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkPrivateDataSlot* pPrivateDataSlot) {

    const auto& dispatch = GetData(device).driver;
    return dispatch.CreatePrivateDataSlot(
            device, pCreateInfo, pAllocator, pPrivateDataSlot);
}

VKAPI_ATTR void DestroyPrivateDataSlotEXT(
        VkDevice device,
        VkPrivateDataSlot privateDataSlot,
        const VkAllocationCallbacks* pAllocator) {

    const auto& dispatch = GetData(device).driver;
    dispatch.DestroyPrivateDataSlotEXT(
            device, privateDataSlot, pAllocator);
}

VKAPI_ATTR void DestroyPrivateDataSlot(
        VkDevice device,
        VkPrivateDataSlot privateDataSlot,
        const VkAllocationCallbacks* pAllocator) {

    const auto& dispatch = GetData(device).driver;
    dispatch.DestroyPrivateDataSlot(
            device, privateDataSlot, pAllocator);
}

VKAPI_ATTR void GetPrivateDataEXT(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t* pData) {

    const auto& dispatch = GetData(device).driver;
    dispatch.GetPrivateDataEXT(
            device, objectType, objectHandle, privateDataSlot, pData);
}

VKAPI_ATTR void GetPrivateData(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t* pData) {

    const auto& dispatch = GetData(device).driver;
    dispatch.GetPrivateData(
            device, objectType, objectHandle, privateDataSlot, pData);
}

VKAPI_ATTR VkResult SetPrivateDataEXT(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t data) {

    const auto& dispatch = GetData(device).driver;
    return dispatch.SetPrivateDataEXT(
            device, objectType, objectHandle, privateDataSlot, data);
}

VKAPI_ATTR VkResult SetPrivateData(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t data) {

    const auto& dispatch = GetData(device).driver;
    return dispatch.SetPrivateData(
            device, objectType, objectHandle, privateDataSlot, data);
}


}  // namespace driver
}  // namespace vulkan

