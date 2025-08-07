#include "pauseAnimation.h"
#include "animationRules.h"
#include "../rhubarb/PauseDetectionConfig.h"

Shape getPauseShape(Shape previous, Shape next, centiseconds duration) {
	const auto& config = PauseDetectionConfig::getInstance();
	
	// For micro-pauses (fast speech sentence boundaries): Slight relaxation
	if (duration < config.microPauseThreshold) {
		// Very brief pause - apply subtle relaxation to indicate separation
		Shape relaxed = relax(previous);
		// If already relaxed, consider a brief closure for better visual separation
		if (relaxed == previous && duration >= 4_cs) {
			return (previous == Shape::B || previous == Shape::C) ? Shape::A : relaxed;
		}
		return relaxed;
	}

	// For very short pauses: More noticeable relaxation
	if (duration < config.veryShortPauseThreshold) {
		// Apply stronger relaxation or partial closure
		Shape relaxed = relax(previous);
		// For B shape, prefer moving to A for clearer pause indication
		if (relaxed == Shape::B) {
			return Shape::A;
		}
		return relaxed;
	}

	// For short pauses: Relax the mouth significantly
	if (duration <= config.shortPauseThreshold) {
		// It looks odd if the pause shape is identical to the next shape.
		// Make sure we find a relaxed shape that's different from the next one.
		for (Shape currentRelaxedShape = previous;;) {
			const Shape nextRelaxedShape = relax(currentRelaxedShape);
			if (nextRelaxedShape != next) {
				return nextRelaxedShape;
			}
			if (nextRelaxedShape == currentRelaxedShape) {
				// We're going in circles
				break;
			}
			currentRelaxedShape = nextRelaxedShape;
		}
	}

	// For longer pauses: Close the mouth
	return Shape::X;
}

JoiningContinuousTimeline<Shape> animatePauses(const JoiningContinuousTimeline<Shape>& animation) {
	JoiningContinuousTimeline<Shape> result(animation);
	
	for_each_adjacent(
		animation.begin(),
		animation.end(),
		[&](const Timed<Shape>& previous, const Timed<Shape>& pause, const Timed<Shape>& next) {
			if (pause.getValue() != Shape::X) return;

			result.set(
				pause.getTimeRange(),
				getPauseShape(previous.getValue(), next.getValue(), pause.getDuration())
			);
		}
	);

	return result;
}
