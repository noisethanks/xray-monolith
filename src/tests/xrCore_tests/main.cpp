// xrCore_tests: unit tests for xrCore that need no game data, renderer or window.
// Exit code 0 when every check passes. Pass a substring to run matching tests only.
#include "test.h"
#include <cstring>

int main(int argc, char** argv)
{
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int ran = 0;
	for (const xrtest::Case& c : xrtest::registry())
	{
		if (filter && !std::strstr(c.name, filter))
			continue;
		const int before = xrtest::failures();
		std::printf("[ RUN  ] %s\n", c.name);
		std::fflush(stdout);
		c.fn();
		std::printf("[ %s ] %s\n", xrtest::failures() == before ? " OK " : "FAIL", c.name);
		std::fflush(stdout);
		++ran;
	}
	std::printf("\n%d tests, %lld checks, %d failures\n", ran, (long long)xrtest::checks(), (int)xrtest::failures());
	return ran > 0 && xrtest::failures() == 0 ? 0 : 1;
}
