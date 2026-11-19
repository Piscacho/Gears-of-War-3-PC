#pragma once
// GoW3 sesion 3 (rendimiento): getenv con cache por hilo (el entorno no cambia; habia getenv en cada draw)
#include <unordered_map>
#include <cstdlib>
static inline const char* CachedGetenv(const char* n)
{
    thread_local std::unordered_map<const void*, const char*> c;
    auto it = c.find(n); if (it != c.end()) return it->second;
    const char* v = std::getenv(n); c.emplace(n, v); return v;
}
#define getenv(n) CachedGetenv(n)
