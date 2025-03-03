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

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include <android-base/thread_annotations.h>

namespace vulkan {
namespace driver {

struct PrivateDataSlot {
    // Private data slot implemented as a map.
    // Notes:
    // - This will only ever contain private data for *swapchain* objects. Everything else gets
    //   passed through to the underlying driver.
    // - There is provision in the spec for "more efficient" private data slots preallocated
    //   within the objects themselves. The number of such slots is specified at device creation time.
    //   For now we're not bothering to implement that -- all private data slots for swapchain behave
    //   as if not preallocated, and so have a bunch of locking. If this turns out to be a performance
    //   problem then we can do something more complex, but is probably OK as swapchains are a tiny
    //   minority of the vulkan objects.

    VkPrivateDataSlot driver_object;
    std::mutex mutex;   // callers are not required to externally synchronize access, so we do it.
    std::unordered_map<uint64_t, uint64_t> data GUARDED_BY(mutex);

    VkPrivateDataSlot as_handle() {
        return VkPrivateDataSlot(reinterpret_cast<uint64_t>(this));
    }

    static PrivateDataSlot* from_handle(VkPrivateDataSlot handle) {
        return reinterpret_cast<PrivateDataSlot*>(handle);
    }

    PrivateDataSlot(VkPrivateDataSlot driver_object) : driver_object(driver_object) {}

    void set(uint64_t object, uint64_t value) {
        std::lock_guard lock(mutex);
        data[object] = value;
    }

    uint64_t get(uint64_t object) {
        std::lock_guard lock(mutex);
        auto it = data.find(object);
        if (it != data.end())
            return it->second;

        return 0;
    }

    void erase(uint64_t object) {
        std::lock_guard lock(mutex);
        data.erase(object);
    }
};

// clang-format off
VKAPI_ATTR VkResult CreatePrivateDataSlotEXT(VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot);
VKAPI_ATTR void DestroyPrivateDataSlotEXT(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void GetPrivateDataEXT(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t* pData);
VKAPI_ATTR VkResult SetPrivateDataEXT(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data);

VKAPI_ATTR VkResult CreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot);
VKAPI_ATTR void DestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void GetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t* pData);
VKAPI_ATTR VkResult SetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data);
// clang-format on

}  // namespace driver
}  // namespace vulkan
