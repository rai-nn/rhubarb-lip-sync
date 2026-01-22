#include "SentenceAnimator.h"
#include "animationRules.h"
#include "ShapeRule.h"

namespace rhubarb_stream {

SentenceAnimator::SentenceAnimator(ShapeSet targetShapes)
	: targetShapes_(std::move(targetShapes))
{
	// If no target shapes specified, use all shapes
	if (targetShapes_.empty()) {
		targetShapes_ = {Shape::A, Shape::B, Shape::C, Shape::D, Shape::E, Shape::F, Shape::G, Shape::H, Shape::X};
	}
}

void SentenceAnimator::setTargetShapes(const ShapeSet& shapes) {
	targetShapes_ = shapes;
	if (targetShapes_.empty()) {
		targetShapes_ = {Shape::A, Shape::B, Shape::C, Shape::D, Shape::E, Shape::F, Shape::G, Shape::H, Shape::X};
	}
}

Shape SentenceAnimator::selectShape(const ShapeSet& shapeSet, Shape previousShape) {
	if (shapeSet.empty()) {
		return Shape::X; // Fallback to idle
	}

	// Find intersection of shape set with target shapes
	ShapeSet validShapes;
	for (Shape shape : shapeSet) {
		if (targetShapes_.find(shape) != targetShapes_.end()) {
			validShapes.insert(shape);
		}
	}

	// If no valid shapes, find closest target shape to any in the set
	if (validShapes.empty()) {
		// Use the first shape from the set and find its closest match in target shapes
		Shape referenceShape = *shapeSet.begin();
		return getClosestShape(referenceShape, targetShapes_);
	}

	// Use getClosestShape to pick the best match based on previous shape
	return getClosestShape(previousShape, validShapes);
}

std::vector<Viseme> SentenceAnimator::mergeAdjacentVisemes(const std::vector<Viseme>& visemes) {
	if (visemes.empty()) {
		return {};
	}

	std::vector<Viseme> merged;
	merged.reserve(visemes.size());

	Viseme current = visemes[0];

	for (size_t i = 1; i < visemes.size(); ++i) {
		const Viseme& next = visemes[i];

		// Check if shapes are the same and times are adjacent (within 1cs tolerance)
		const double tolerance = 0.01; // 10ms
		if (next.shape == current.shape && (next.start - current.end) < tolerance) {
			// Extend current viseme
			current.end = next.end;
		} else {
			// Save current and start new
			merged.push_back(current);
			current = next;
		}
	}

	// Don't forget the last one
	merged.push_back(current);

	return merged;
}

std::vector<Viseme> SentenceAnimator::animate(const BoundedTimeline<Phone>& phones) {
	if (phones.empty()) {
		return {};
	}

	// Convert phones to shape rules
	ContinuousTimeline<ShapeRule> shapeRules = getShapeRules(phones);

	// Convert shape rules to visemes
	std::vector<Viseme> visemes;
	visemes.reserve(shapeRules.size());

	Shape previousShape = Shape::X; // Start with idle

	for (const auto& timedRule : shapeRules) {
		const ShapeRule& rule = timedRule.getValue();

		// Skip invalid rules
		if (rule.shapeSet.empty()) {
			continue;
		}

		// Select best shape
		Shape shape = selectShape(rule.shapeSet, previousShape);

		// Convert centiseconds to seconds
		double start = timedRule.getStart().count() / 100.0;
		double end = timedRule.getEnd().count() / 100.0;

		// Create viseme
		Viseme viseme;
		viseme.start = start;
		viseme.end = end;
		viseme.shape = shape;

		visemes.push_back(viseme);
		previousShape = shape;
	}

	// Merge adjacent visemes with same shape (reduces output noise)
	return mergeAdjacentVisemes(visemes);
}

} // namespace rhubarb_stream
