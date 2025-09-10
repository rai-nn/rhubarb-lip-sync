#include "mouthAnimation.h"
#include "time/timedLogging.h"
#include "ShapeRule.h"
#include "roughAnimation.h"
#include "pauseAnimation.h"
#include "tweening.h"
#include "timingOptimization.h"
#include "targetShapeSet.h"
#include "staticSegments.h"
#include "wordSimplification.h"
#include "../rhubarb/semanticEntries.h"
#include <chrono>

JoiningContinuousTimeline<Shape> animate(
	const BoundedTimeline<Phone>& phones,
	const ShapeSet& targetShapeSet,
	bool noTweening,
	bool skipTimingOptimization,
	int maxVisemesPerWord
) {
	// Start animation phase
	auto animationStart = std::chrono::steady_clock::now();
	logging::log(PhaseStartEntry("AnimationGeneration"));
	
	// Create timeline of shape rules
	ContinuousTimeline<ShapeRule> shapeRules = getShapeRules(phones);

	// Modify shape rules to only contain allowed shapes -- plus X, which is needed for pauses and
	// will be replaced later
	ShapeSet targetShapeSetPlusX = targetShapeSet;
	targetShapeSetPlusX.insert(Shape::X);
	shapeRules = convertToTargetShapeSet(shapeRules, targetShapeSetPlusX);

	// Animate in multiple steps
	const auto performMainAnimationSteps = [&targetShapeSet, noTweening, skipTimingOptimization, maxVisemesPerWord](const auto& shapeRules) {
		JoiningContinuousTimeline<Shape> animation = animateRough(shapeRules);
		
		// Skip timing optimization if requested
		if (!skipTimingOptimization) {
			animation = optimizeTiming(animation);
		}
		
		animation = animatePauses(animation);
		
		// Apply tweening unless disabled
		if (!noTweening) {
			animation = insertTweens(animation);
		}
		
		// Apply word-based simplification if requested
		if (maxVisemesPerWord > 0) {
			animation = simplifyByDensity(animation, maxVisemesPerWord);
		}
		
		animation = convertToTargetShapeSet(animation, targetShapeSet);
		return animation;
	};
	const JoiningContinuousTimeline<Shape> result =
		avoidStaticSegments(shapeRules, performMainAnimationSteps);

	for (const auto& timedShape : result) {
		logTimedEvent("shape", timedShape);
	}

	// End animation phase
	auto animationEnd = std::chrono::steady_clock::now();
	double duration = std::chrono::duration<double>(animationEnd - animationStart).count();
	logging::log(PhaseEndEntry("AnimationGeneration", duration));

	return result;
}
