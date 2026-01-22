#pragma once

#include <vector>
#include "../core/Shape.h"
#include "../time/BoundedTimeline.h"
#include "../core/Phone.h"

namespace rhubarb_stream {

/**
 * Viseme output structure
 *
 * Represents a single mouth shape with absolute timing.
 */
struct Viseme {
	double start;   // Start time in seconds
	double end;     // End time in seconds
	Shape shape;    // Preston Blair shape (A-H, X)
};

/**
 * Converts phones to visemes for streaming output.
 *
 * This is a simplified animation pipeline optimized for streaming:
 * - Uses rhubarb's animationRules for phone-to-shape mapping
 * - Skips complex optimizations (timing, tweening) for lower latency
 * - Outputs visemes immediately for real-time processing
 *
 * Usage:
 *   SentenceAnimator animator;
 *   auto visemes = animator.animate(phones);
 *   for (const auto& viseme : visemes) {
 *       emit(viseme.start, viseme.end, viseme.shape);
 *   }
 */
class SentenceAnimator {
public:
	/**
	 * Create animator with optional target shape set
	 *
	 * @param targetShapes Set of shapes to use (default: all Preston Blair shapes)
	 */
	explicit SentenceAnimator(ShapeSet targetShapes = ShapeSet{});

	/**
	 * Convert phones to visemes
	 *
	 * @param phones Timeline of recognized phones with absolute timestamps
	 * @return Vector of visemes sorted by start time
	 */
	std::vector<Viseme> animate(const BoundedTimeline<Phone>& phones);

	/**
	 * Set target shape set (restricts output to specific shapes)
	 */
	void setTargetShapes(const ShapeSet& shapes);

private:
	/**
	 * Select best shape from a set based on previous shape
	 */
	Shape selectShape(const ShapeSet& shapeSet, Shape previousShape);

	/**
	 * Merge adjacent visemes with same shape
	 */
	std::vector<Viseme> mergeAdjacentVisemes(const std::vector<Viseme>& visemes);

	ShapeSet targetShapes_;
};

} // namespace rhubarb_stream
