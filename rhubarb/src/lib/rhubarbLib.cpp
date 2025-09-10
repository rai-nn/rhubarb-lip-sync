#include "rhubarbLib.h"
#include "core/Phone.h"
#include "tools/textFiles.h"
#include "animation/mouthAnimation.h"
#include "audio/audioFileReading.h"
#include "characterTiming.h"
#include "../rhubarb/semanticEntries.h"
#include "logging/logging.h"

using boost::optional;
using std::string;
using std::filesystem::path;

JoiningContinuousTimeline<Shape> animateAudioClip(
	const AudioClip& audioClip,
	const optional<string>& dialog,
	const Recognizer& recognizer,
	const ShapeSet& targetShapeSet,
	int maxThreadCount,
	ProgressSink& progressSink,
	bool noTweening,
	bool skipTimingOptimization,
	int maxVisemesPerWord)
{
	// Log audio metadata
	double duration = audioClip.getTruncatedRange().getDuration().count() / 100.0; // Convert from centiseconds to seconds
	logging::log(AudioMetadataEntry(duration, audioClip.getSampleRate(), 1)); // AudioClip is always mono
	
	const BoundedTimeline<Phone> phones =
		recognizer.recognizePhones(audioClip, dialog, maxThreadCount, progressSink);
	// The animation functions will use PauseDetectionConfig::getInstance() internally
	JoiningContinuousTimeline<Shape> result = animate(phones, targetShapeSet, noTweening, skipTimingOptimization, maxVisemesPerWord);
	return result;
}

JoiningContinuousTimeline<Shape> animateWaveFile(
	path filePath,
	const optional<string>& dialog,
	const Recognizer& recognizer,
	const ShapeSet& targetShapeSet,
	int maxThreadCount,
	ProgressSink& progressSink,
	bool noTweening,
	bool skipTimingOptimization,
	int maxVisemesPerWord)
{
	const auto audioClip = createAudioFileClip(filePath);
	return animateAudioClip(*audioClip, dialog, recognizer, targetShapeSet, maxThreadCount, progressSink, noTweening, skipTimingOptimization, maxVisemesPerWord);
}

