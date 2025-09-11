#include "voiceActivityDetection.h"
#include "DcOffset.h"
#include "SampleRateConverter.h"
#include "logging/logging.h"
#include "tools/pairs.h"
#include <boost/range/adaptor/transformed.hpp>
#include <webrtc/common_audio/vad/include/webrtc_vad.h>
#include "processing.h"
#include <gsl_util.h>
#include "tools/parallel.h"
#include <webrtc/common_audio/vad/vad_core.h>
#include "../rhubarb/PauseDetectionConfig.h"
#include "../rhubarb/semanticEntries.h"
#include <chrono>

using std::vector;
using boost::adaptors::transformed;
using fmt::format;
using std::runtime_error;
using std::unique_ptr;

JoiningBoundedTimeline<void> detectVoiceActivity(
	const AudioClip& inputAudioClip,
	ProgressSink& progressSink
) {
	// Start phase tracking
	auto phaseStart = std::chrono::steady_clock::now();
	logging::log(PhaseStartEntry("VoiceActivityDetection"));
	
	// Sub-phase 2.1: Audio Preparation
	auto subPhaseStart = std::chrono::steady_clock::now();
	
	// Prepare audio for VAD
	constexpr int webRtcSamplingRate = 8000;
	const unique_ptr<AudioClip> audioClip = inputAudioClip.clone()
		| resample(webRtcSamplingRate)
		| removeDcOffset();
	
	// Log sub-phase 2.1 timing
	auto subPhaseEnd = std::chrono::steady_clock::now();
	double subPhaseDuration = std::chrono::duration<double>(subPhaseEnd - subPhaseStart).count();
	logging::log(SubPhaseTimingEntry("VoiceActivityDetection", "2.1: Audio Preparation", subPhaseDuration));

	VadInst* vadHandle = WebRtcVad_Create();
	if (!vadHandle) throw runtime_error("Error creating WebRTC VAD handle.");

	auto freeHandle = gsl::finally([&]() { WebRtcVad_Free(vadHandle); });

	int error = WebRtcVad_Init(vadHandle);
	if (error) throw runtime_error("Error initializing WebRTC VAD.");

	// Get aggressiveness from config
	const auto& config = PauseDetectionConfig::getInstance();
	const int aggressiveness = config.vadAggressiveness; // 0..3. The higher, the more is cut off.
	error = WebRtcVad_set_mode(vadHandle, aggressiveness);
	if (error) throw runtime_error("Error setting WebRTC VAD aggressiveness.");

	// Sub-phase 2.2: WebRTC VAD Processing
	subPhaseStart = std::chrono::steady_clock::now();
	
	// Detect activity
	JoiningBoundedTimeline<void> activity(audioClip->getTruncatedRange());
	centiseconds time = 0_cs;
	const size_t frameSize = webRtcSamplingRate / 100;
	const auto processBuffer = [&](const vector<int16_t>& buffer) {
		// WebRTC is picky regarding buffer size
		if (buffer.size() < frameSize) return;

		const int result = WebRtcVad_Process(
			vadHandle,
			webRtcSamplingRate,
			buffer.data(),
			buffer.size()
		);
		if (result == -1) throw runtime_error("Error processing audio buffer using WebRTC VAD.");

		// Ignore the result of WebRtcVad_Process, instead directly interpret the internal VAD flag.
		// The result of WebRtcVad_Process stays 1 for a number of frames after the last detected
		// activity.
		const bool isActive = reinterpret_cast<VadInstT*>(vadHandle)->vad == 1;

		if (isActive) {
			activity.set(time, time + 1_cs);
		}

		time += 1_cs;
	};
	process16bitAudioClip(*audioClip, processBuffer, frameSize, progressSink);
	
	// Log sub-phase 2.2 timing
	subPhaseEnd = std::chrono::steady_clock::now();
	subPhaseDuration = std::chrono::duration<double>(subPhaseEnd - subPhaseStart).count();
	logging::log(SubPhaseTimingEntry("VoiceActivityDetection", "2.2: WebRTC VAD Processing", subPhaseDuration));
	
	// Sub-phase 2.3: Activity Refinement
	subPhaseStart = std::chrono::steady_clock::now();

	// Fill small gaps in activity
	// Config already retrieved above for aggressiveness
	const centiseconds maxGap = config.vadMaxGap;
	for (const auto& pair : getPairs(activity)) {
		if (pair.second.getStart() - pair.first.getEnd() <= maxGap) {
			activity.set(pair.first.getEnd(), pair.second.getStart());
		}
	}

	// Discard very short segments of activity
	// Use configurable minimum segment length
	const centiseconds minSegmentLength = config.vadMinSegmentLength;
	for (const auto& segment : Timeline<void>(activity)) {
		if (segment.getDuration() < minSegmentLength) {
			activity.clear(segment.getTimeRange());
		}
	}
	
	// Log sub-phase 2.3 timing
	subPhaseEnd = std::chrono::steady_clock::now();
	subPhaseDuration = std::chrono::duration<double>(subPhaseEnd - subPhaseStart).count();
	logging::log(SubPhaseTimingEntry("VoiceActivityDetection", "2.3: Activity Refinement", subPhaseDuration));
	
	// Sub-phase 2.4: Timeline Generation
	subPhaseStart = std::chrono::steady_clock::now();

	logging::debugFormat(
		"Found {} sections of voice activity: {}",
		activity.size(),
		join(activity | transformed([](const Timed<void>& t) {
			return format("{0}-{1}", t.getStart(), t.getEnd());
		}), ", ")
	);

	// Calculate segment statistics
	int speechSegments = activity.size();
	int silenceSegments = 0;
	
	// Count silence segments: gaps between speech segments, plus potential leading/trailing silences
	if (speechSegments > 0) {
		// Check for leading silence
		auto firstSegment = activity.begin();
		if (firstSegment->getStart() > inputAudioClip.getTruncatedRange().getStart()) {
			silenceSegments++;
		}
		
		// Count gaps between speech segments
		auto prev = activity.begin();
		for (auto it = std::next(activity.begin()); it != activity.end(); ++it, ++prev) {
			if (it->getStart() > prev->getEnd()) {
				silenceSegments++;
			}
		}
		
		// Check for trailing silence
		auto lastSegment = std::prev(activity.end());
		if (lastSegment->getEnd() < inputAudioClip.getTruncatedRange().getEnd()) {
			silenceSegments++;
		}
	} else {
		// No speech segments means the entire audio is silence
		silenceSegments = 1;
	}
	
	// Log the segment statistics
	logging::log(VoiceActivityEntry(speechSegments, silenceSegments));
	
	// Log sub-phase 2.4 timing
	subPhaseEnd = std::chrono::steady_clock::now();
	subPhaseDuration = std::chrono::duration<double>(subPhaseEnd - subPhaseStart).count();
	logging::log(SubPhaseTimingEntry("VoiceActivityDetection", "2.4: Timeline Generation", subPhaseDuration));

	// Calculate voice activity timeline output
	double totalDuration = inputAudioClip.getTruncatedRange().getDuration().count() / 100.0; // Convert from centiseconds to seconds
	double speechTime = 0.0;
	std::vector<std::pair<double, double>> speechSegmentPairs;
	
	for (const auto& segment : activity) {
		double segmentStart = segment.getStart().count() / 100.0;
		double segmentEnd = segment.getEnd().count() / 100.0;
		speechTime += (segmentEnd - segmentStart);
		speechSegmentPairs.push_back({segmentStart, segmentEnd});
	}
	
	double silenceTime = totalDuration - speechTime;
	
	// Log the voice activity timeline output
	logging::log(VoiceActivityTimelineEntry(totalDuration, speechTime, silenceTime, speechSegmentPairs));

	// End phase tracking
	auto phaseEnd = std::chrono::steady_clock::now();
	double duration = std::chrono::duration<double>(phaseEnd - phaseStart).count();
	logging::log(PhaseEndEntry("VoiceActivityDetection", duration));

	return activity;
}
