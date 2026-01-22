#pragma once

#include <vector>
#include <cstdint>
#include "../protocol/FrameTypes.h"

namespace rhubarb_stream {

/**
 * Extracts audio segments from a full audio buffer using timestamps.
 *
 * Given:
 * - Full audio buffer (PCM 16-bit, 16kHz, mono)
 * - Start and end times in seconds
 *
 * Returns:
 * - A new vector containing just the samples in that time range
 *
 * Padding: Adds configurable padding before/after the segment
 * to give PocketSphinx acoustic context.
 */
class AudioSlicer {
public:
	/**
	 * Create an audio slicer with the given audio buffer
	 *
	 * @param audioData Pointer to audio samples (must remain valid)
	 * @param sampleCount Number of samples in buffer
	 * @param paddingMs Padding in milliseconds to add before/after (default: 30ms)
	 */
	AudioSlicer(
		const int16_t* audioData,
		size_t sampleCount,
		int paddingMs = 30
	);

	/**
	 * Extract audio segment for given time range
	 *
	 * @param startSeconds Start time in seconds
	 * @param endSeconds End time in seconds
	 * @return Vector of samples for the segment (with padding)
	 */
	std::vector<int16_t> slice(double startSeconds, double endSeconds) const;

	/**
	 * Get the actual start time of the slice (including padding)
	 *
	 * @param startSeconds Requested start time
	 * @return Actual start time (may be earlier due to padding, clamped to 0)
	 */
	double getActualStart(double startSeconds) const;

	/**
	 * Get the actual end time of the slice (including padding)
	 *
	 * @param endSeconds Requested end time
	 * @return Actual end time (may be later due to padding, clamped to buffer end)
	 */
	double getActualEnd(double endSeconds) const;

	/**
	 * Get total duration of audio buffer in seconds
	 */
	double totalDuration() const;

private:
	const int16_t* audioData_;
	size_t sampleCount_;
	int paddingSamples_;
};

} // namespace rhubarb_stream
