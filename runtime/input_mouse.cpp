#include "input_mouse.h"
std::atomic<int> g_mouseDX{ 0 }, g_mouseDY{ 0 }, g_mouseWheel{ 0 };
std::atomic<bool> g_mouseL{ false }, g_mouseR{ false }, g_mouseM{ false }, g_mouseCaptured{ false };
std::atomic<int> g_mouseNativeDX{ 0 }, g_mouseNativeDY{ 0 };
