// Copyright 2016 Benjamin Glatzel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Precompiled header file
#include "stdafx_vulkan.h"
#include "stdafx.h"

namespace Intrinsic
{
namespace Renderer
{
namespace Vulkan
{
namespace Resources
{
void BufferManager::createResources(const BufferRefArray& p_Buffers)
{
  VkCommandBuffer copyCmd = RenderSystem::beginTemporaryCommandBuffer();

  _INTR_ARRAY(VkBuffer) stagingBuffersToDestroy;
  stagingBuffersToDestroy.reserve(p_Buffers.size());

  for (uint32_t i = 0u; i < p_Buffers.size(); ++i)
  {
    BufferRef bufferRef = p_Buffers[i];

    VkBufferCreateInfo bufferCreateInfo = {};
    {
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.pNext = nullptr;
      bufferCreateInfo.usage =
          Helper::mapBufferTypeToVkUsageFlagBits(_descBufferType(bufferRef)) |
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      bufferCreateInfo.size = _descSizeInBytes(bufferRef);
      bufferCreateInfo.queueFamilyIndexCount = 0;
      bufferCreateInfo.pQueueFamilyIndices = nullptr;
      bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      bufferCreateInfo.flags = 0u;
    }

    VkBuffer& buffer = _vkBuffer(bufferRef);
    _INTR_ASSERT(buffer == VK_NULL_HANDLE);

    VkResult result = vkCreateBuffer(RenderSystem::_vkDevice, &bufferCreateInfo,
                                     nullptr, &buffer);
    _INTR_VK_CHECK_RESULT(result);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(RenderSystem::_vkDevice, buffer, &memReqs);

    MemoryPoolType::Enum memoryPoolType = _descMemoryPoolType(bufferRef);
    MemoryAllocationInfo& memoryAllocationInfo =
        _memoryAllocationInfo(bufferRef);
    VkDeviceMemory deviceMemory =
        GpuMemoryManager::getDeviceMemory(memoryPoolType);

    bool needsAlloc = true;

    // Try to keep memory for static buffers
    if (memoryPoolType >= MemoryPoolType::kRangeStartStatic &&
        memoryPoolType <= MemoryPoolType::kRangeEndStatic)
    {
      if (memoryAllocationInfo.memoryPoolType == memoryPoolType &&
          memReqs.size <= memoryAllocationInfo.sizeInBytes &&
          memReqs.alignment == memoryAllocationInfo.alignmentInBytes &&
          deviceMemory == memoryAllocationInfo.vkDeviceMemory)
      {
        needsAlloc = false;
      }
    }

    if (needsAlloc)
    {
      memoryAllocationInfo.sizeInBytes = (uint32_t)memReqs.size;
      memoryAllocationInfo.alignmentInBytes = (uint32_t)memReqs.alignment;
      memoryAllocationInfo.memoryPoolType = memoryPoolType;
      memoryAllocationInfo.vkDeviceMemory = deviceMemory;
      memoryAllocationInfo.offsetInBytes = GpuMemoryManager::allocateOffset(
          memoryPoolType, (uint32_t)memReqs.size, (uint32_t)memReqs.alignment);
    }

    result = vkBindBufferMemory(RenderSystem::_vkDevice, buffer,
                                memoryAllocationInfo.vkDeviceMemory,
                                memoryAllocationInfo.offsetInBytes);
    _INTR_VK_CHECK_RESULT(result);

    void* initialData = _descInitialData(bufferRef);
    if (initialData)
    {
      VkBufferCreateInfo stagingBufferCreateInfo = bufferCreateInfo;
      {
        stagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      }

      VkBuffer stagingBuffer;
      result = vkCreateBuffer(RenderSystem::_vkDevice, &stagingBufferCreateInfo,
                              nullptr, &stagingBuffer);
      _INTR_VK_CHECK_RESULT(result);

      // We've run out of memory - flush the command buffer and reset the
      // allocator
      if (GpuMemoryManager::calcAvailableMemoryInBytes(
              MemoryPoolType::kVolatileStagingBuffers) < memReqs.size)
      {
        RenderSystem::flushTemporaryCommandBuffer();
        GpuMemoryManager::resetAllocator(
            MemoryPoolType::kVolatileStagingBuffers);
        copyCmd = RenderSystem::beginTemporaryCommandBuffer();
      }

      const uint32_t stagingMemOffset = GpuMemoryManager::allocateOffset(
          MemoryPoolType::kVolatileStagingBuffers, (uint32_t)memReqs.size,
          (uint32_t)memReqs.alignment);

      result = vkBindBufferMemory(RenderSystem::_vkDevice, stagingBuffer,
                                  GpuMemoryManager::getDeviceMemory(
                                      MemoryPoolType::kVolatileStagingBuffers),
                                  stagingMemOffset);
      _INTR_VK_CHECK_RESULT(result);

      // Copy initial data to staging memory
      {
        memcpy(
            GpuMemoryManager::getHostVisibleMemoryForOffset(stagingMemOffset),
            initialData, _descSizeInBytes(bufferRef));
      }

      VkBufferCopy bufferCopy = {};
      {
        bufferCopy.dstOffset = 0u;
        bufferCopy.srcOffset = 0u;
        bufferCopy.size = _descSizeInBytes(bufferRef);
      }

      // Finally copy from the staging buffer to the actual buffer
      vkCmdCopyBuffer(copyCmd, stagingBuffer, buffer, 1u, &bufferCopy);
    }
  }

  RenderSystem::flushTemporaryCommandBuffer();

  for (uint32_t i = 0u; i < stagingBuffersToDestroy.size(); ++i)
  {
    vkDestroyBuffer(RenderSystem::_vkDevice, stagingBuffersToDestroy[i],
                    nullptr);
  }

  GpuMemoryManager::resetAllocator(MemoryPoolType::kVolatileStagingBuffers);
}
}
}
}
}
