#include "wordSimplification.h"
#include <algorithm>
#include <vector>

using std::vector;

// Helper to calculate viseme prominence score
static double calculateProminence(const Timed<Shape>& viseme, bool isVowel, bool isStart, bool isEnd) {
	double score = 0.0;
	
	// Duration is most important factor
	const double duration = viseme.getDuration().count() / 100.0; // Convert to 100ms units
	score += duration * 100.0;
	
	// Vowels are more prominent than consonants
	if (isVowel) {
		score += 50.0;
	}
	
	// Position in window matters - start and end are more important
	if (isStart) {
		score += 30.0;
	}
	if (isEnd) {
		score += 20.0;
	}
	
	return score;
}

// Check if a shape represents a vowel-like sound
static bool isVowelShape(Shape shape) {
	switch (shape) {
		case Shape::A:  // Open vowel
		case Shape::C:  // EE sound
		case Shape::D:  // AA/AH sound
		case Shape::E:  // EH/AE sound
		case Shape::F:  // OO/UH sound
		case Shape::G:  // OW sound
			return true;
		default:
			return false;
	}
}

JoiningContinuousTimeline<Shape> simplifyByDensity(
	const JoiningContinuousTimeline<Shape>& animation,
	int maxVisemesPerWord
) {
	// If disabled or no limit, return original
	if (maxVisemesPerWord <= 0) {
		return animation;
	}
	
	// Create result timeline
	JoiningContinuousTimeline<Shape> result(animation.getRange(), Shape::X);
	
	// Average word duration assumption: ~300-500ms per word
	// We'll use sliding windows of 400ms (40cs)
	const centiseconds windowSize(40);
	const centiseconds stepSize(20); // 50% overlap for smoother transitions
	
	// Collect all visemes into a vector for easier processing
	vector<Timed<Shape>> allVisemes;
	for (const auto& viseme : animation) {
		// Skip silence shapes
		if (viseme.getValue() != Shape::X) {
			allVisemes.push_back(viseme);
		}
	}
	
	// Process the animation in sliding windows
	const centiseconds animationEnd = animation.getRange().getEnd();
	for (centiseconds windowStart(0); windowStart < animationEnd; windowStart += stepSize) {
		const centiseconds windowEnd = std::min(windowStart + windowSize, animationEnd);
		const TimeRange window(windowStart, windowEnd);
		
		// Collect visemes in this window
		vector<Timed<Shape>> windowVisemes;
		for (const auto& viseme : allVisemes) {
			// Check if viseme overlaps with window
			const TimeRange& visemeRange = viseme.getTimeRange();
			if (visemeRange.getStart() < window.getEnd() && visemeRange.getEnd() > window.getStart()) {
				windowVisemes.push_back(viseme);
			}
		}
		
		// If window has fewer visemes than limit, keep all
		if (static_cast<int>(windowVisemes.size()) <= maxVisemesPerWord) {
			for (const auto& viseme : windowVisemes) {
				result.set(viseme.getTimeRange(), viseme.getValue());
			}
			continue;
		}
		
		// Calculate prominence scores for each viseme
		struct ScoredViseme {
			Timed<Shape> viseme;
			double score;
			size_t originalIndex;
		};
		vector<ScoredViseme> scoredVisemes;
		
		for (size_t i = 0; i < windowVisemes.size(); ++i) {
			const auto& viseme = windowVisemes[i];
			const bool isStart = (i == 0);
			const bool isEnd = (i == windowVisemes.size() - 1);
			const bool isVowel = isVowelShape(viseme.getValue());
			
			const double score = calculateProminence(viseme, isVowel, isStart, isEnd);
			scoredVisemes.push_back({viseme, score, i});
		}
		
		// Sort by score (descending)
		std::sort(scoredVisemes.begin(), scoredVisemes.end(),
			[](const ScoredViseme& a, const ScoredViseme& b) {
				return a.score > b.score;
			});
		
		// Select top N visemes
		int visemesToKeep = std::min(maxVisemesPerWord, static_cast<int>(scoredVisemes.size()));
		for (int i = 0; i < visemesToKeep; ++i) {
			const auto& viseme = scoredVisemes[i].viseme;
			// Only set if this viseme hasn't been set yet or if it has a better score
			result.set(viseme.getTimeRange(), viseme.getValue());
		}
	}
	
	// Fill any remaining gaps with silence
	for (const auto& viseme : animation) {
		if (viseme.getValue() == Shape::X) {
			// Preserve silence regions
			result.set(viseme.getTimeRange(), Shape::X);
		}
	}
	
	return result;
}