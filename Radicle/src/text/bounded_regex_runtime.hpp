#pragma once

#include <cstdint>

struct RadicleRegexRuntimeV1 {
    std::uint32_t version;
    int (*spawn)(const char*, int, std::uint64_t*);
    int (*status)(std::uint64_t, int*);
    void (*release)(std::uint64_t);
};

using RadicleRegexRuntimeEntry = const RadicleRegexRuntimeV1* (*)();
