// Contract tests for xrMemory (xr_malloc, xr_realloc, xr_free, operator new).
//
// In every shipping configuration xrCore is built with PURE_ALLOC, so xrMemory is a
// thin layer over _aligned_malloc/_aligned_realloc/_aligned_free that adds 16 byte
// alignment and zero fill. Engine code may rely on both. Any replacement allocator
// must keep these tests passing. Of these, the realloc tail zeroing is the easiest
// to lose: it uses _aligned_msize to find the old size, and an allocator that reports
// a rounded up usable size there leaves stale bytes in the grown region.
#include "../../xrCore/xrCore.h"
#include "test.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <thread>

namespace
{
const size_t kAlign = 16;

bool aligned(const void* p) { return (reinterpret_cast<uintptr_t>(p) % kAlign) == 0; }

bool all_bytes(const void* p, size_t n, u8 value)
{
	const u8* b = static_cast<const u8*>(p);
	for (size_t i = 0; i < n; ++i)
		if (b[i] != value)
			return false;
	return true;
}

const size_t kSizes[] = {1, 2, 3, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 127, 128, 255, 256, 257,
                         1000, 1023, 1024, 4095, 4096, 4097, 16384, 65535, 65536, 65537, 1u << 20, 16u << 20};
} // namespace

// Freed blocks are dirtied before release, so the second pass usually gets reused,
// dirty memory back and the zero check means something.
XRTEST(xr_malloc_is_aligned_and_zeroed)
{
	for (int pass = 0; pass < 2; ++pass)
		for (size_t n : kSizes)
		{
			void* p = xr_malloc(n);
			CHECK(p != nullptr);
			if (!p)
				continue;
			CHECK(aligned(p));
			CHECK(all_bytes(p, n, 0));
			std::memset(p, 0xCD, n);
			xr_free(p);
			CHECK(p == nullptr);
		}
}

XRTEST(xr_alloc_typed_is_aligned_and_zeroed)
{
	for (int pass = 0; pass < 2; ++pass)
	{
		u32* p = xr_alloc<u32>(1000);
		CHECK(p != nullptr && aligned(p));
		CHECK(p && all_bytes(p, 1000 * sizeof(u32), 0));
		if (p)
			std::memset(p, 0xCD, 1000 * sizeof(u32));
		xr_free(p);
	}
}

// operator new is replaced in xrMemory.h, so plain new of a POD is zero filled.
// GSC era code may depend on this for members a constructor never sets.
struct Pod
{
	u32 a;
	float b;
	void* c;
	u8 tail[61];
};

XRTEST(operator_new_is_zeroed)
{
	for (int pass = 0; pass < 2; ++pass)
	{
		Pod* p = new Pod;
		CHECK(aligned(p));
		CHECK(all_bytes(p, sizeof(Pod), 0));
		std::memset(p, 0xCD, sizeof(Pod));
		delete p;

		u8* arr = new u8[5000];
		CHECK(aligned(arr));
		CHECK(all_bytes(arr, 5000, 0));
		std::memset(arr, 0xCD, 5000);
		delete[] arr;
	}
}

XRTEST(operator_new_zero_size_is_unique_and_freeable)
{
	u8* a = new u8[0];
	u8* b = new u8[0];
	CHECK(a != nullptr && b != nullptr);
	CHECK(a != b);
	delete[] a;
	delete[] b;
}

// Records current behaviour. xr_malloc(0) does not round up the way operator new does.
XRTEST(xr_malloc_zero_size_behaviour)
{
	void* p = xr_malloc(0);
	INFO("xr_malloc(0) returned %s\n", p ? "a pointer" : "nullptr");
	if (p)
		CHECK(aligned(p));
	xr_free(p);
}

XRTEST(xr_free_null_is_noop)
{
	void* p = nullptr;
	xr_free(p);
	CHECK(p == nullptr);
	Memory.mem_free(nullptr);
}

XRTEST(xr_realloc_null_behaves_like_malloc)
{
	void* p = xr_realloc(nullptr, 300);
	CHECK(p != nullptr && aligned(p));
	CHECK(p && all_bytes(p, 300, 0));
	xr_free(p);
}

XRTEST(xr_realloc_grow_preserves_and_zeroes_tail)
{
	for (size_t from : {1, 15, 16, 17, 100, 4096})
		for (size_t to : {from + 1, from * 2, from * 10 + 3, size_t(1) << 20})
		{
			u8* p = static_cast<u8*>(xr_malloc(from));
			std::memset(p, 0xAB, from);
			p = static_cast<u8*>(xr_realloc(p, to));
			CHECK(p != nullptr && aligned(p));
			if (!p)
				continue;
			CHECK(all_bytes(p, from, 0xAB));
			CHECK(all_bytes(p + from, to - from, 0));
			xr_free(p);
		}
}

// Shrinking usually stays in place, so the dirty bytes past the new size are still
// there when the block grows again. They must come back zeroed.
XRTEST(xr_realloc_shrink_then_grow_zeroes_tail)
{
	for (size_t big : {64, 1000, 4096, 100000})
		for (size_t small : {1, 3, 10, 17, 63})
		{
			u8* p = static_cast<u8*>(xr_malloc(big));
			std::memset(p, 0xAB, big);
			p = static_cast<u8*>(xr_realloc(p, small));
			CHECK(p != nullptr && all_bytes(p, small, 0xAB));
			p = static_cast<u8*>(xr_realloc(p, big));
			CHECK(p != nullptr && aligned(p));
			if (!p)
				continue;
			CHECK(all_bytes(p, small, 0xAB));
			CHECK(all_bytes(p + small, big - small, 0));
			if (!all_bytes(p + small, big - small, 0))
			{
				size_t first = small, dirty = 0;
				while (first < big && p[first] == 0)
					++first;
				for (size_t i = small; i < big; ++i)
					dirty += p[i] != 0;
				INFO("big %zu small %zu: first stale byte at %zu, %zu stale bytes\n", big, small, first, dirty);
			}
			xr_free(p);
		}
}

// Diagnostic: what _aligned_msize reports after an in place shrink. xrMemory's
// realloc zeroes from this value, so anything above the requested size leaves stale
// bytes in the next growth.
XRTEST(diag_aligned_msize_after_shrink)
{
	for (size_t big : {64, 1000, 4096, 100000})
		for (size_t small : {1, 3, 10, 17, 63})
		{
			void* p = _aligned_malloc(big, 16);
			void* q = _aligned_realloc(p, small, 16);
			INFO("big %zu small %zu: moved %d, _aligned_msize %zu\n", big, small, int(p != q), _aligned_msize(q, 16, 0));
			_aligned_free(q);
		}
}

XRTEST(xr_strdup_roundtrip)
{
	char* s = xr_strdup("stalker");
	CHECK(s != nullptr && aligned(s));
	CHECK(s && std::strcmp(s, "stalker") == 0);
	xr_free(s);
	char* e = xr_strdup("");
	CHECK(e != nullptr && e[0] == 0);
	xr_free(e);
}

XRTEST(large_allocation_is_zeroed)
{
	const size_t n = size_t(256) << 20;
	u8* p = static_cast<u8*>(xr_malloc(n));
	CHECK(p != nullptr && aligned(p));
	if (!p)
		return;
	bool zero = true;
	for (size_t i = 0; i < n; i += 4096)
		zero &= p[i] == 0 && p[i + 4095] == 0;
	CHECK(zero);
	xr_free(p);
}

namespace
{
struct Block
{
	u8* p = nullptr;
	size_t n = 0;
	u8 tag = 0;
};

// Each live block is filled with its tag. Corruption from another block or thread
// shows up as a wrong byte, a missed zero fill as a non tag byte in a fresh region.
void churn(unsigned seed, int ops, int slots, std::atomic<int>* errors)
{
	std::mt19937 rng(seed);
	std::vector<Block> live(slots);
	auto size = [&] {
		const unsigned r = rng() % 100;
		return r < 70 ? 1 + rng() % 256 : r < 95 ? 257 + rng() % 8192 : 8193 + rng() % (1 << 20);
	};
	for (int i = 0; i < ops; ++i)
	{
		Block& b = live[rng() % slots];
		const unsigned op = rng() % 3;
		if (b.p && !all_bytes(b.p, b.n, b.tag))
			++*errors;
		if (!b.p || op == 0)
		{
			xr_free(b.p);
			b.n = size();
			b.p = static_cast<u8*>(xr_malloc(b.n));
			b.tag = u8(1 + rng() % 255);
			if (!b.p || !aligned(b.p) || !all_bytes(b.p, b.n, 0))
				++*errors;
			if (b.p)
				std::memset(b.p, b.tag, b.n);
		}
		else if (op == 1)
		{
			const size_t n = size();
			u8* p = static_cast<u8*>(xr_realloc(b.p, n));
			if (!p || !aligned(p) || !all_bytes(p, n < b.n ? n : b.n, b.tag) || (n > b.n && !all_bytes(p + b.n, n - b.n, 0)))
				++*errors;
			b.p = p;
			b.n = n;
			if (p)
				std::memset(p, b.tag, n);
		}
		else
		{
			xr_free(b.p);
			b.n = 0;
		}
	}
	for (Block& b : live)
	{
		if (b.p && !all_bytes(b.p, b.n, b.tag))
			++*errors;
		xr_free(b.p);
	}
}
} // namespace

XRTEST(single_thread_churn)
{
	std::atomic<int> errors{0};
	churn(1234, 200000, 512, &errors);
	CHECK(errors == 0);
	if (errors)
		INFO("%d bad blocks\n", (int)errors);
}

XRTEST(multi_thread_churn)
{
	std::atomic<int> errors{0};
	const unsigned hw = std::thread::hardware_concurrency();
	const unsigned threads = hw < 4 ? 4 : hw > 16 ? 16 : hw;
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < threads; ++t)
		pool.emplace_back(churn, 1000 + t, 100000, 256, &errors);
	for (std::thread& t : pool)
		t.join();
	INFO("%u threads\n", threads);
	CHECK(errors == 0);
	if (errors)
		INFO("%d bad blocks\n", (int)errors);
}

// Memory freed by one thread and allocated by another, the pattern of loader and
// worker threads handing buffers to the main thread.
XRTEST(cross_thread_free)
{
	const int n = 20000;
	std::vector<u8*> blocks(n);
	std::thread producer([&] {
		for (int i = 0; i < n; ++i)
		{
			blocks[i] = static_cast<u8*>(xr_malloc(16 + i % 2000));
			std::memset(blocks[i], u8(i), 16 + i % 2000);
		}
	});
	producer.join();
	int bad = 0;
	std::thread consumer([&] {
		for (int i = 0; i < n; ++i)
		{
			bad += !all_bytes(blocks[i], 16 + i % 2000, u8(i));
			xr_free(blocks[i]);
		}
	});
	consumer.join();
	CHECK(bad == 0);
}
