#pragma once

#include <cstdlib>

// Portable environment mutation for tests: MSVC has no setenv/unsetenv, and
// _putenv_s with an empty value removes the variable.
namespace dw::test {

inline void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

inline void unsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

} // namespace dw::test
