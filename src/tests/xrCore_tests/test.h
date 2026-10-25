// Minimal test harness. The engine builds without C++ exceptions, so checks
// record failures and keep going instead of throwing.
#pragma once
#include <atomic>
#include <cstdio>
#include <vector>

namespace xrtest
{
struct Case
{
	const char* name;
	void (*fn)();
};

inline std::vector<Case>& registry()
{
	static std::vector<Case> cases;
	return cases;
}

inline std::atomic<int>& failures()
{
	static std::atomic<int> n{0};
	return n;
}

inline std::atomic<long long>& checks()
{
	static std::atomic<long long> n{0};
	return n;
}

struct Registrar
{
	Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};
} // namespace xrtest

#define XRTEST(name)                                                    \
	static void name();                                                 \
	static xrtest::Registrar name##_registrar(#name, &name);            \
	static void name()

// Thread safe. Prints at most 20 failures per check site to keep logs readable.
#define CHECK(cond)                                                                    \
	do                                                                                 \
	{                                                                                  \
		++xrtest::checks();                                                            \
		if (!(cond))                                                                   \
		{                                                                              \
			static std::atomic<int> site_failures{0};                                  \
			++xrtest::failures();                                                      \
			if (site_failures++ < 20)                                                  \
				std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                              \
	} while (0)

#define INFO(...) std::printf("    info: " __VA_ARGS__)
