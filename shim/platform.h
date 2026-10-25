// The shims' platform layer: the one header for the platform smith is built for (knowledge/platform-layer.md).
#pragma once

#ifdef __APPLE__
#include "platform_macos.h"
#else
#include "platform_linux.h"
#endif
