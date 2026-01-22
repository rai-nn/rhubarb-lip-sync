#include "AudioSlicer.h"
#include <algorithm>
#include <cmath>

namespace rhubarb_stream {

AudioSlicer::AudioSlicer(
	const int16_t* audioData,
	size_t sampleCount,
	int paddingMs
)
	: audioData_(audioData)
	, sampleCount_(sampleCount)
	, paddingSamples_(static_cast<int>(paddingMs * Limits::SAMPLE_RATE / 1000))
{
}

std::vector<int16_t> AudioSlicer::slice(double startSeconds, double endSeconds) const {
	// Calculate sample indices with padding
	int startSample = static_cast<int>(startSeconds * Limits::SAMPLE_RATE) - paddingSamples_;
	int endSample = static_cast<int>(endSeconds * Limits::SAMPLE_RATE) + paddingSamples_;

	// Clamp to buffer bounds
	startSample = std::max(0, startSample);
	endSample = std::min(static_cast<int>(sampleCount_), endSample);

	// Handle edge cases
	if (startSample >= endSample || startSample >= static_cast<int>(sampleCount_)) {
		return std::vector<int16_t>();
	}

	// Copy the slice
	size_t sliceSize = static_cast<size_t>(endSample - startSample);
	std::vector<int16_t> result(sliceSize);
	std::copy(
		audioData_ + startSample,
		audioData_ + endSample,
		result.begin()
	);

	return result;
}

double AudioSlicer::getActualStart(double startSeconds) const {
	double paddingSeconds = static_cast<double>(paddingSamples_) / Limits::SAMPLE_RATE;
	double actualStart = startSeconds - paddingSeconds;
	return std::max(0.0, actualStart);
}

double AudioSlicer::getActualEnd(double endSeconds) const {
	double paddingSeconds = static_cast<double>(paddingSamples_) / Limits::SAMPLE_RATE;
	double actualEnd = endSeconds + paddingSeconds;
	return std::min(totalDuration(), actualEnd);
}

double AudioSlicer::totalDuration() const {
	return static_cast<double>(sampleCount_) / Limits::SAMPLE_RATE;
}

} // namespace rhubarb_stream
