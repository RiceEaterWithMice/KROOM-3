/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2016-2017 Dustin Land

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#ifndef __VULKANCONTEXT_H__
#define __VULKANCONTEXT_H__

#include <SDL.h>
#include <SDL_vulkan.h>
#include "qvk.h"
#include "Allocator_VK.h"
#include "../RenderLog.h"
#include "../FrameData.h"

struct gpuInfo_t
{
	VkPhysicalDevice					device;
	VkPhysicalDeviceProperties			props;
	VkPhysicalDeviceMemoryProperties	memProps;
	VkSurfaceCapabilitiesKHR			surfaceCaps;
	idList< VkSurfaceFormatKHR >		surfaceFormats;
	idList< VkPresentModeKHR >			presentModes;
	idList< VkQueueFamilyProperties >	queueFamilyProps;
	idList< VkExtensionProperties >		extensionProps;
};

struct vulkanContext_t
{
	// Eric: If on linux, use this to pass SDL_Window pointer to the SDL_Vulkan_* methods not in sdl_vkimp.cpp file.
	// SRS - Generalized Vulkan SDL platform
	SDL_Window*						sdlWindow = nullptr;

	uint64							frameCounter;
	uint32							frameParity;

	vertCacheHandle_t				jointCacheHandle;

	VkInstance						instance;

	// selected physical device
	VkPhysicalDevice				physicalDevice;
	VkPhysicalDeviceFeatures		physicalDeviceFeatures;

	// logical device
	VkDevice						device;

	VkQueue							graphicsQueue;
	VkQueue							presentQueue;
	int								graphicsFamilyIdx;
	int								presentFamilyIdx;
	VkDebugReportCallbackEXT		callback;

	idList< const char* >			instanceExtensions;
	idList< const char* >			deviceExtensions;
	idList< const char* >			validationLayers;

	bool							debugMarkerSupportAvailable;
	bool							debugUtilsSupportAvailable;
	bool                            deviceProperties2Available;     // SRS - For getting device properties in support of gfxInfo

	// selected GPU
	gpuInfo_t* 						gpu;

	// all GPUs found on the system
	idList< gpuInfo_t >				gpus;

	VkCommandPool					commandPool;
	idArray< VkCommandBuffer, NUM_FRAME_DATA >	commandBuffer;
	idArray< VkFence, NUM_FRAME_DATA >			commandBufferFences;
	idArray< bool, NUM_FRAME_DATA >				commandBufferRecorded;

	VkSurfaceKHR					surface;
	VkPresentModeKHR				presentMode;
	VkFormat						depthFormat;
	VkRenderPass					renderPass;
	VkPipelineCache					pipelineCache;
	VkSampleCountFlagBits			sampleCount;
	bool							supersampling;

	int								fullscreen;
	VkSwapchainKHR					swapchain;
	VkFormat						swapchainFormat;
	VkExtent2D						swapchainExtent;
	uint32							currentSwapIndex;
	VkImage							msaaImage;
	VkImageView						msaaImageView;
	vulkanAllocation_t				msaaAllocation;
	idArray< VkImage, NUM_FRAME_DATA >			swapchainImages;
	idArray< VkImageView, NUM_FRAME_DATA >		swapchainViews;

	idArray< VkFramebuffer, NUM_FRAME_DATA >	frameBuffers;
	idArray< VkSemaphore, NUM_FRAME_DATA >		acquireSemaphores;
	idArray< VkSemaphore, NUM_FRAME_DATA >		renderCompleteSemaphores;

	int											currentImageParm;
	idArray< idImage*, MAX_IMAGE_PARMS >		imageParms;

	//typedef uint32 QueryTuple[2];

	// GPU timestamp queries
	idArray< uint32, NUM_FRAME_DATA >									queryIndex;
	idArray< idArray< uint32, MRB_TOTAL_QUERIES >, NUM_FRAME_DATA >		queryAssignedIndex;
	idArray< idArray< uint64, NUM_TIMESTAMP_QUERIES >, NUM_FRAME_DATA >	queryResults;
	idArray< VkQueryPool, NUM_FRAME_DATA >		queryPools;
};

extern vulkanContext_t vkcontext;

// per-frame Vulkan state owned by the render program manager
class idUniformBuffer;
struct vkRenderProgContext_t
{
	int					counter;
	int					currentData;
	int					currentDescSet;
	int					currentParmBufferOffset;
	VkDescriptorPool	descriptorPools[ NUM_FRAME_DATA ];
	VkDescriptorSet		descriptorSets[ NUM_FRAME_DATA ][ MAX_DESC_SETS ];

	idUniformBuffer*	parmBuffers[ NUM_FRAME_DATA ];
};

extern vkRenderProgContext_t vkRenderProgContext;
#endif /* !__VULKANCONTEXT_H__ */
