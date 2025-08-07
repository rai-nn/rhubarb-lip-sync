#pragma once

#include "core/Shape.h"
#include "time/ContinuousTimeline.h"
#include <vector>

// Simplify animation by limiting the number of visemes per time window
// This approximates word-based simplification without needing exact word boundaries
JoiningContinuousTimeline<Shape> simplifyByDensity(
	const JoiningContinuousTimeline<Shape>& animation,
	int maxVisemesPerWord
);