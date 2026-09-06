// Host regression tests include the queue implementation extracted from vkraster.cpp.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int32 = int32_t;
using bool32 = int;
using VkDeviceSize = uint64_t;
using VkFormat = uint32_t;
using VkBuffer = uint64_t;
using VkDeviceMemory = uint64_t;
using VkCommandBuffer = uint64_t;
using VkImageView = uint64_t;
#define nil nullptr
#define VKLOG(...) ((void)0)
enum {
	MEMDUR_GLOBAL = 0, ID_IMAGE = 0, VK_NULL_HANDLE = 0, VK_SUCCESS = 0,
	VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 1,
	VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 2, VK_IMAGE_ASPECT_COLOR_BIT = 1,
	VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 1,
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL = 2,
	VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 1, VK_IMAGE_VIEW_TYPE_2D = 1,
	NUM_FRAME_CONTEXTS = 2
};
struct VulkanRaster {
	uint64 image = 1, view = 1;
	uint32 layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	uint32 samplerKey[NUM_FRAME_CONTEXTS] = {};
};
struct {
	bool32 generateMipmaps = 0, inFrame = 0;
	uint64 device = 1;
} gvk;
struct VkBufferImageCopy {
	VkDeviceSize bufferOffset;
	struct { uint32 aspectMask, mipLevel, layerCount; } imageSubresource;
	struct { uint32 width, height, depth; } imageExtent;
};
struct VkImageViewCreateInfo {
	uint32 sType, viewType;
	uint64 image;
	VkFormat format;
	struct { uint32 aspectMask, levelCount, layerCount; } subresourceRange;
};

static std::atomic<int> allocations(0), builds(0);
static std::atomic<uint64> gMipMicroseconds(0);
static uint32 gMipLanded;
static uint8 *gMipScratch[2], *gMipMask;
static size_t gMipScratchBytes, gMipMaskBytes;
static std::mutex buildMutex;
static std::condition_variable buildSignal;
static bool blockBuild = false, buildSuccess = true;
static uint32 maps, copies, submits, retired;
static bool mapSuccess = true;
static std::vector<uint8> uploadStorage;

static void *rwMalloc(size_t bytes, int) {
	void *p = std::malloc(bytes);
	if(p) allocations.fetch_add(1);
	return p;
}
static void rwFree(void *p) {
	if(p) allocations.fetch_sub(1);
	std::free(p);
}
static bool renderDiagnosticsEnabled() { return false; }
static uint64 microseconds() {
	return (uint64)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}
static uint32 levelDimension(uint32 base, int32 level) {
	const uint32 value = base >> level;
	return value ? value : 1;
}
static VkDeviceSize alignLevel(VkDeviceSize bytes) { return (bytes+3)&~3ull; }
static VkDeviceSize bcLevelBytes(uint32 w, uint32 h, VkFormat) {
	return (VkDeviceSize)((w+3)/4)*((h+3)/4)*8;
}
static bool32 buildCompressedMips(const uint8 *, uint8 *, uint32, uint32,
	VkFormat, VkFormat, int32) {
	builds.fetch_add(1);
	std::unique_lock<std::mutex> lock(buildMutex);
	buildSignal.wait(lock, [] { return !blockBuild; });
	return buildSuccess;
}
static bool createBuffer(VkDeviceSize bytes, uint32, uint32,
	VkBuffer *buffer, VkDeviceMemory *memory) {
	uploadStorage.resize((size_t)bytes);
	*buffer = *memory = 1;
	return true;
}
static int vkMapMemory(uint64, VkDeviceMemory, uint64, VkDeviceSize,
	uint32, void **mapped) {
	++maps;
	if(!mapSuccess) return -1;
	*mapped = uploadStorage.data();
	return VK_SUCCESS;
}
static void vkUnmapMemory(uint64, VkDeviceMemory) {}
static void retireBuffer(VkBuffer, VkDeviceMemory) { ++retired; }
static VkCommandBuffer beginOneShot() { return 1; }
static void endOneShot(VkCommandBuffer) { ++submits; }
static void transitionImageLayout(VkCommandBuffer, uint64, uint32, uint32,
	uint32, uint32) {}
static void vkCmdCopyBufferToImage(VkCommandBuffer, VkBuffer, uint64,
	uint32, uint32, const VkBufferImageCopy *) { ++copies; }
static void retireImage(uint64, uint64, uint64) {}
static int vkCreateImageView(uint64, const VkImageViewCreateInfo *,
	const void *, VkImageView *view) { *view = 2; return VK_SUCCESS; }

#include "mip-worker-extracted.inc"

static void check(bool okay, const char *message) {
	if(!okay) { std::fprintf(stderr, "FAIL: %s\n", message); std::_Exit(1); }
}
template<class Predicate> static void waitUntil(Predicate predicate) {
	const uint64 deadline = microseconds()+3000000;
	while(!predicate()) {
		check(microseconds() < deadline, "worker timeout");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
static size_t pendingCount() {
	std::lock_guard<std::mutex> lock(gMipMutex);
	return gMipPending.size();
}
static size_t finishedCount() {
	std::lock_guard<std::mutex> lock(gMipMutex);
	return gMipFinished.size();
}
static void releaseBuild() {
	{ std::lock_guard<std::mutex> lock(buildMutex); blockBuild = false; }
	buildSignal.notify_all();
}
static void queue(VulkanRaster &native) {
	const uint8 source[128] = {};
	queueGeneratedMips(&native, 1, 1, 16, 16, 2, source, sizeof(source));
}
static void reset(bool blocked = false, bool successful = true) {
	stopGeneratedMips();
	check(allocations.load() == 0, "job allocation leak");
	builds = 0;
	{ std::lock_guard<std::mutex> lock(buildMutex);
		blockBuild = blocked; buildSuccess = successful; }
	gvk.generateMipmaps = 1;
	gvk.inFrame = 1;
	maps = copies = submits = retired = 0;
	mapSuccess = true;
}
static void addFinished(VulkanRaster &native, VkDeviceSize bytes) {
	auto *job = (GeneratedMipJob *)rwMalloc(sizeof(GeneratedMipJob), 0);
	std::memset(job, 0, sizeof(*job));
	job->native = &native;
	job->width = job->height = 16;
	job->numLevels = 2;
	job->resultBytes = bytes;
	job->result = (uint8 *)rwMalloc((size_t)bytes, 0);
	std::memset(job->result, 0, (size_t)bytes);
	std::lock_guard<std::mutex> lock(gMipMutex);
	gMipFinished.push_back(job);
}

int main() {
	VulkanRaster first, second;
	reset();
	setGenerateMipmaps(0);
	queue(first);
	check(allocations == 0 && !gMipWorkerStarted, "OFF queues no jobs");
	std::puts("PASS: OFF admission");

	reset(true);
	queue(first);
	waitUntil([] { return builds.load() == 1; });
	for(int i = 0; i < 20; ++i) queue(second);
	check(pendingCount() == 20, "backlog fixture");
	setGenerateMipmaps(0);
	check(pendingCount() == 0 && finishedCount() == 0 && allocations == 3,
		"OFF immediately frees backlog, retaining only in-flight job");
	releaseBuild();
	waitUntil([] { return allocations.load() == 0; });
	check(builds == 1 && finishedCount() == 0, "OFF discards in-flight output");
	std::puts("PASS: OFF releases queues and rejects in-flight result");

	reset(true);
	queue(first);
	waitUntil([] { return builds.load() == 1; });
	queue(second);
	cancelGeneratedMips(&second);
	check(pendingCount() == 0 && allocations == 3, "evicted pending job freed");
	cancelGeneratedMips(&first);
	releaseBuild();
	waitUntil([] { return allocations.load() == 0; });
	check(finishedCount() == 0, "evicted in-flight job cannot return");
	std::puts("PASS: raster eviction releases pending and in-flight work");

	reset();
	queue(first);
	waitUntil([] { return finishedCount() == 1; });
	cancelGeneratedMips(&first);
	check(allocations == 0 && finishedCount() == 0, "finished job freed on eviction");
	std::puts("PASS: completed raster eviction");

	reset(false, false);
	queue(first);
	waitUntil([] { return builds.load() == 1 && allocations.load() == 0; });
	check(finishedCount() == 0, "failed build never reaches GPU upload queue");
	std::puts("PASS: failed generation is not uploaded");

	reset(true);
	queue(first);
	waitUntil([] { return builds.load() == 1; });
	for(int i = 0; i < 20; ++i) queue(second);
	std::thread stopper([] { stopGeneratedMips(); });
	waitUntil([] { return pendingCount() == 0; });
	releaseBuild();
	stopper.join();
	check(builds == 1 && allocations == 0, "shutdown must not drain backlog");
	std::puts("PASS: shutdown cancels backlog instead of filtering it");

	reset();
	addFinished(first, 32);
	gvk.inFrame = 0;
	uploadFinishedMips();
	check(maps == 0 && copies == 0 && finishedCount() == 1, "no-frame upload is deferred");
	gvk.inFrame = 1;
	mapSuccess = false;
	uploadFinishedMips();
	check(maps == 1 && copies == 0 && submits == 0 && allocations == 0 && retired == 1,
		"failed map retires staging without copy or submit");
	std::puts("PASS: frame and map-failure upload guards");

	reset();
	addFinished(first, 1500000);
	addFinished(second, 1500000);
	uploadFinishedMips();
	check(submits == 1 && finishedCount() == 1, "byte budget defers second chain");
	uploadFinishedMips();
	check(submits == 2 && finishedCount() == 0 && allocations == 0,
		"deferred chain completes next frame");
	addFinished(first, 3000000);
	addFinished(second, 32);
	uploadFinishedMips();
	check(submits == 3 && finishedCount() == 1, "oversize chain progresses alone");
	uploadFinishedMips();
	check(submits == 4 && allocations == 0, "small chain following oversize is retained");
	std::puts("PASS: bounded uploads retain every chain, including oversized chains");
	reset();
	std::puts("All mip worker regression tests passed.");
}
