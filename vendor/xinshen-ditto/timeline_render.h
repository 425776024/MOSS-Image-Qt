#pragma once

#include "avatar_runtime.h"

// Moss's fixed-track editor uses this small compositor, independent of the
// inference models and the VideoCut SDK.
void renderPageTimeline(const avatar::Json& request);
