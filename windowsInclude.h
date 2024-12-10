#pragma once

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#include <gdiplus.h>

#undef min
#undef max

#pragma comment(lib,"gdiplus.lib")
