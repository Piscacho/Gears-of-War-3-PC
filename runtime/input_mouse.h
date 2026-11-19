#pragma once
// GoW3 sesion 5: estado del raton (Raw Input en gpu_window.cpp, leido en xam.cpp)
#include <atomic>
extern std::atomic<int> g_mouseDX, g_mouseDY, g_mouseWheel;
extern std::atomic<int> g_mouseNativeDX, g_mouseNativeDY;   // hooks_gow3.cpp (raton nativo de UE3)
bool MouseNativeActive();
extern std::atomic<bool> g_mouseL, g_mouseR, g_mouseM, g_mouseCaptured;
