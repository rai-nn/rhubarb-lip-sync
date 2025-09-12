#include "pocketSphinxTools.h"

#include "tools/platformTools.h"
#include <regex>
#include "audio/DcOffset.h"
#include "audio/voiceActivityDetection.h"
#include "tools/parallel.h"
#include "tools/ObjectPool.h"
#include "time/timedLogging.h"
#include "../rhubarb/semanticEntries.h"
#include <chrono>
#include <atomic>
#include <sstream>
#include <thread>

extern "C" {
#include <sphinxbase/err.h>
#include <pocketsphinx_internal.h>
#include <ngram_search.h>
}

using std::runtime_error;
using std::invalid_argument;
using std::unique_ptr;
using std::string;
using std::vector;
using std::filesystem::path;
using std::regex;
using boost::optional;
using std::chrono::duration_cast;
	
logging::Level convertSphinxErrorLevel(err_lvl_t errorLevel) {
	switch (errorLevel) {
		case ERR_DEBUG:
		case ERR_INFO:
		case ERR_INFOCONT:
			return logging::Level::Trace;
		case ERR_WARN:
			return logging::Level::Warn;
		case ERR_ERROR:
			return logging::Level::Error;
		case ERR_FATAL:
			return logging::Level::Fatal;
		default:
			throw invalid_argument("Unknown log level.");
	}
}

void sphinxLogCallback(void* user_data, err_lvl_t errorLevel, const char* format, ...) {
	UNUSED(user_data);

	// Create varArgs list
	va_list args;
	va_start(args, format);
	auto _ = gsl::finally([&args]() { va_end(args); });

	// Format message
	const int initialSize = 256;
	vector<char> chars(initialSize);
	bool success = false;
	while (!success) {
		const int charsWritten = vsnprintf(chars.data(), chars.size(), format, args);
		if (charsWritten < 0) throw runtime_error("Error formatting PocketSphinx log message.");

		success = charsWritten < static_cast<int>(chars.size());
		if (!success) chars.resize(chars.size() * 2);
	}
	const regex waste("^(DEBUG|INFO|INFOCONT|WARN|ERROR|FATAL): ");
	string message =
		std::regex_replace(chars.data(), waste, "", std::regex_constants::format_first_only);
	boost::algorithm::trim(message);

	const logging::Level logLevel = convertSphinxErrorLevel(errorLevel);
	logging::log(logLevel, message);
}

void redirectPocketSphinxOutput() {
	static bool redirected = false;
	if (redirected) return;

	// Discard PocketSphinx output
	err_set_logfp(nullptr);

	// Redirect PocketSphinx output to log
	err_set_callback(sphinxLogCallback, nullptr);

	redirected = true;
}

BoundedTimeline<Phone> recognizePhones(
	const AudioClip& inputAudioClip,
	optional<std::string> dialog,
	decoderFactory createDecoder,
	utteranceToPhonesFunction utteranceToPhones,
	int maxThreadCount,
	ProgressSink& progressSink
) {
	ProgressMerger totalProgressMerger(progressSink);
	ProgressSink& voiceActivationProgressSink =
		totalProgressMerger.addSource("VAD (PocketSphinx tools)", 1.0);
	ProgressSink& dialogProgressSink =
		totalProgressMerger.addSource("recognition (PocketSphinx tools)", 15.0);

	// Make sure audio stream has no DC offset
	const unique_ptr<AudioClip> audioClip = inputAudioClip.clone() | removeDcOffset();

	// Split audio into utterances
	JoiningBoundedTimeline<void> utterances;
	try {
		utterances = detectVoiceActivity(*audioClip, voiceActivationProgressSink);
	} catch (...) {
		std::throw_with_nested(runtime_error("Error detecting segments of speech."));
	}

	redirectPocketSphinxOutput();

	// Start speech recognition phase
	auto recognitionStart = std::chrono::steady_clock::now();
	logging::log(PhaseStartEntry("SpeechRecognition", utterances.size()));
	
	// Prepare pool of decoders with timing wrapper
	std::atomic<int> decoderCount(0);
	ObjectPool<ps_decoder_t, lambda_unique_ptr<ps_decoder_t>> decoderPool(
		[&] { 
			auto start = std::chrono::steady_clock::now();
			int currentCount = decoderCount.load();
			logging::debugFormat("Creating decoder #{} (pool was empty)...", currentCount + 1);
			
			auto decoder = createDecoder(dialog);
			
			auto end = std::chrono::steady_clock::now();
			double duration = std::chrono::duration<double>(end - start).count();
			decoderCount++;
			logging::debugFormat("Decoder #{} created in {:.2f}s", currentCount + 1, duration);
			
			// Log using semantic entry for detailed mode
			logging::log(DecoderCreationEntry(currentCount + 1, duration));
			
			return decoder;
		});

	BoundedTimeline<Phone> phones(audioClip->getTruncatedRange());
	std::mutex resultMutex;
	std::atomic<int> utteranceCounter(0);
	const int totalUtterances = utterances.size();
	
	// Collect utterance results for output logging
	std::vector<UtteranceOutputInfo> collectedUtteranceResults;
	collectedUtteranceResults.reserve(totalUtterances);
	
	const auto processUtterance = [&](Timed<void> timedUtterance, ProgressSink& utteranceProgressSink) {
		// Get utterance index and track processing start time
		int utteranceIndex = utteranceCounter.fetch_add(1) + 1;
		auto processingStart = std::chrono::steady_clock::now();
		
		// Log utterance start (text not available yet)
		logging::log(UtteranceStartEntry(
			utteranceIndex, 
			totalUtterances,
			timedUtterance.getTimeRange().getStart().count() / 100.0, // Convert centiseconds to seconds
			timedUtterance.getTimeRange().getEnd().count() / 100.0, // Convert centiseconds to seconds
			"" // Text not available at start
		));
		
		// Log which thread is about to acquire decoder
		std::stringstream ss;
		ss << std::this_thread::get_id();
		logging::debugFormat("Thread {} requesting decoder for utterance {} ({:.2f}-{:.2f}s)...", 
		                    ss.str(), utteranceIndex,
		                    timedUtterance.getTimeRange().getStart().count() / 100.0,
		                    timedUtterance.getTimeRange().getEnd().count() / 100.0);
		
		// Detect phones for utterance
		auto beforeAcquire = std::chrono::steady_clock::now();
		const auto decoder = decoderPool.acquire();
		
		auto decoderAcquired = std::chrono::steady_clock::now();
		double acquireTime = std::chrono::duration<double>(decoderAcquired - beforeAcquire).count();
		logging::debugFormat("Thread {} acquired decoder in {:.2f}s (total acquire time including any waiting), starting utterance {} processing", 
		                    ss.str(), acquireTime, utteranceIndex);
		
		// Log decoder acquisition as a sub-step
		// Heuristic: if acquisition took > 0.1s, a new decoder was likely created
		std::string acquireDetails = (acquireTime > 0.1) ? "new decoder created" : "from pool";
		logging::log(UtteranceSubStepEntry(utteranceIndex, "Decoder Acquisition", acquireTime, acquireDetails));
		
		UtteranceResult utteranceResult = utteranceToPhones(
			*audioClip,
			timedUtterance.getTimeRange(),
			*decoder,
			utteranceProgressSink,
			utteranceIndex
		);
		
		// Measure post-processing time (result collection)
		auto postProcessingStart = std::chrono::steady_clock::now();

		// Copy phones to result timeline and collect data for output logging
		auto mutexAcquireStart = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> lock(resultMutex);
		auto mutexAcquireEnd = std::chrono::steady_clock::now();
		double mutexWaitTime = std::chrono::duration<double>(mutexAcquireEnd - mutexAcquireStart).count();
		
		// Measure timeline update time
		auto timelineUpdateStart = std::chrono::steady_clock::now();
		for (const auto& timedPhone : utteranceResult.phones) {
			phones.set(timedPhone);
		}
		auto timelineUpdateEnd = std::chrono::steady_clock::now();
		double timelineUpdateTime = std::chrono::duration<double>(timelineUpdateEnd - timelineUpdateStart).count();
		
		// Store utterance result for final output logging
		// Note: We're storing with utteranceIndex-1 because we incremented it earlier
		if (collectedUtteranceResults.size() < static_cast<size_t>(utteranceIndex)) {
			collectedUtteranceResults.resize(utteranceIndex);
		}
		
		// Create an UtteranceOutputInfo structure for the semantic entry
		UtteranceOutputInfo& storedResult = collectedUtteranceResults[utteranceIndex - 1];
		storedResult.index = utteranceIndex;
		storedResult.startTime = timedUtterance.getTimeRange().getStart().count() / 100.0;
		storedResult.endTime = timedUtterance.getTimeRange().getEnd().count() / 100.0;
		storedResult.text = utteranceResult.text;
		
		// Extract words from the text (simple space-based tokenization)
		if (!utteranceResult.text.empty() && utteranceResult.text != "[BREATH]" && utteranceResult.text != "[NOISE]") {
			std::istringstream iss(utteranceResult.text);
			std::string word;
			while (iss >> word) {
				// Remove [BREATH] markers from word counting
				if (word != "[BREATH]" && word != "[NOISE]") {
					storedResult.words.push_back(word);
				}
			}
		}
		
		// Extract phonemes
		for (const auto& timedPhone : utteranceResult.phones) {
			string phoneName = PhoneConverter::get().toString(timedPhone.getValue());
			storedResult.phonemes.push_back(phoneName);
		}
		
		// Calculate total post-processing time
		auto postProcessingEnd = std::chrono::steady_clock::now();
		double postProcessingDuration = std::chrono::duration<double>(postProcessingEnd - postProcessingStart).count();
		
		// Log post-processing as a sub-step with details
		std::string postDetails = fmt::format("mutex wait: {:.3f}s, timeline: {:.3f}s", 
			mutexWaitTime, timelineUpdateTime);
		logging::log(UtteranceSubStepEntry(utteranceIndex, "Result Collection", postProcessingDuration, postDetails));
		
		// Calculate overall processing duration
		auto processingEnd = std::chrono::steady_clock::now();
		double processingDuration = std::chrono::duration<double>(processingEnd - processingStart).count();
		
		// Log utterance end with the actual recognized text
		logging::log(UtteranceEndEntry(
			utteranceIndex, 
			totalUtterances,
			timedUtterance.getTimeRange().getStart().count() / 100.0, // Convert centiseconds to seconds
			timedUtterance.getTimeRange().getEnd().count() / 100.0, // Convert centiseconds to seconds
			utteranceResult.text, // Now we have the actual text
			processingDuration
		));
		
	};

	const auto getUtteranceProgressWeight = [](const Timed<void> timedUtterance) {
		return timedUtterance.getDuration().count();
	};

	// Perform speech recognition
	try {
		// Calculate all thread constraints for debugging
		int utteranceCount = static_cast<int>(utterances.size());
		double audioDurationSeconds = duration_cast<std::chrono::seconds>(audioClip->getTruncatedRange().getDuration()).count();
		
		// Determine the maximum thread count
		int maxThreads;
		if (maxThreadCount == 0) {
			// Automatic mode: calculate based on audio duration
			// Use the formula: audio_duration_seconds / 5
			// This assumes 5 seconds of audio per thread is optimal
			int durationBasedLimit = static_cast<int>(audioDurationSeconds / 5);
			if (durationBasedLimit < 1) {
				durationBasedLimit = 1;
			}
			
			// Also limit by available CPU cores (use 4 as reasonable default)
			int coreCount = std::thread::hardware_concurrency();
			if (coreCount == 0) {
				coreCount = 4;
			}
			
			// Use the minimum of duration-based limit and core count
			maxThreads = std::min(durationBasedLimit, coreCount);
			
			logging::debugFormat("Automatic thread calculation: duration={}s, durationBasedLimit={}, coreCount={}, selected={}", 
				audioDurationSeconds, durationBasedLimit, coreCount, maxThreads);
		} else {
			// Explicit thread count specified
			maxThreads = maxThreadCount;
		}
		
		// Determine how many parallel threads to use
		int threadCount = std::min({
			maxThreads,
			// Don't use more threads than there are utterances to be processed
			utteranceCount
		});
		if (threadCount < 1) {
			threadCount = 1;
		}
		
		// Debug logging to understand thread allocation
		logging::debugFormat("Thread allocation constraints: maxThreads={}, utteranceCount={}, audioDuration={}s, finalThreadCount={}", 
			maxThreads, utteranceCount, audioDurationSeconds, threadCount);
		logging::debugFormat("Speech recognition using {} threads -- start", threadCount);
		
		// Log pool state before processing
		logging::debugFormat("Starting parallel processing with {} threads for {} utterances", 
		                    threadCount, utterances.size());
		logging::debugFormat("Decoder pool initially empty: {}", decoderPool.empty());
		
		runParallel(
			"speech recognition (PocketSphinx tools)",
			processUtterance,
			utterances,
			threadCount,
			dialogProgressSink,
			getUtteranceProgressWeight
		);
		
		// Log pool state after processing
		logging::debugFormat("Processing complete. Final decoder pool size: {}", decoderPool.size());
		
		logging::debug("Speech recognition -- end");
		
		// Calculate totals for speech recognition output
		int totalWords = 0;
		int totalPhonemesCount = 0;
		for (const auto& result : collectedUtteranceResults) {
			totalWords += result.words.size();
			totalPhonemesCount += result.phonemes.size();
		}
		
		// Log the speech recognition output
		logging::log(SpeechRecognitionOutputEntry(
			collectedUtteranceResults.size(),
			totalWords,
			totalPhonemesCount,
			collectedUtteranceResults
		));
		
		// End speech recognition phase
		auto recognitionEnd = std::chrono::steady_clock::now();
		double duration = std::chrono::duration<double>(recognitionEnd - recognitionStart).count();
		logging::log(PhaseEndEntry("SpeechRecognition", duration));
	} catch (...) {
		std::throw_with_nested(runtime_error("Error performing speech recognition via PocketSphinx tools."));
	}

	return phones;
}

const path& getSphinxModelDirectory() {
	static path sphinxModelDirectory(getBinDirectory() / "res" / "sphinx");
	return sphinxModelDirectory;
}

JoiningTimeline<void> getNoiseSounds(TimeRange utteranceTimeRange, const Timeline<Phone>& phones) {
	JoiningTimeline<void> noiseSounds;

	// Find utterance parts without recognized phones
	noiseSounds.set(utteranceTimeRange);
	for (const auto& timedPhone : phones) {
		noiseSounds.clear(timedPhone.getTimeRange());
	}

	// Remove undesired elements
	const centiseconds minSoundDuration = 12_cs;
	for (const auto& unknownSound : JoiningTimeline<void>(noiseSounds)) {
		const bool startsAtZero = unknownSound.getStart() == 0_cs;
		const bool tooShort = unknownSound.getDuration() < minSoundDuration;
		if (startsAtZero || tooShort) {
			noiseSounds.clear(unknownSound.getTimeRange());
		}
	}

	return noiseSounds;
}

BoundedTimeline<string> recognizeWords(const vector<int16_t>& audioBuffer, ps_decoder_t& decoder) {
	// Restart timing at 0
	ps_start_stream(&decoder);

	// Start recognition
	int error = ps_start_utt(&decoder);
	if (error) throw runtime_error("Error starting utterance processing for word recognition.");

	// Process entire audio clip
	const bool noRecognition = false;
	const bool fullUtterance = true;
	const int searchedFrameCount =
		ps_process_raw(&decoder, audioBuffer.data(), audioBuffer.size(), noRecognition, fullUtterance);
	if (searchedFrameCount < 0) {
		throw runtime_error("Error analyzing raw audio data for word recognition.");
	}

	// End recognition
	error = ps_end_utt(&decoder);
	if (error) throw runtime_error("Error ending utterance processing for word recognition.");

	BoundedTimeline<string> result(
		TimeRange(0_cs, centiseconds(100 * audioBuffer.size() / sphinxSampleRate))
	);
	const bool phonetic = cmd_ln_boolean_r(decoder.config, "-allphone_ci");
	if (!phonetic) {
		// If the decoder is in word mode (as opposed to phonetic recognition), it expects each
		// utterance to contain speech. If it doesn't, ps_seg_word() logs the annoying error
		// "Couldn't find <s> in first frame".
		// Not every utterance does contain speech, however. In this case, we exit early to prevent
		// the log output.
		// We *don't* to that in phonetic mode because here, the same code would omit valid phones.
		const bool noWordsRecognized = reinterpret_cast<ngram_search_t*>(decoder.search)->bpidx == 0;
		if (noWordsRecognized) {
			return result;
		}
	}

	// Collect words
	for (ps_seg_t* it = ps_seg_iter(&decoder); it; it = ps_seg_next(it)) {
		const char* word = ps_seg_word(it);
		int firstFrame, lastFrame;
		ps_seg_frames(it, &firstFrame, &lastFrame);
		result.set(centiseconds(firstFrame), centiseconds(lastFrame + 1), word);
	}

	return result;
}
