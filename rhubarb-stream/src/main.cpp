/**
 * rhubarb-stream - Real-time streaming lip-sync viseme generation
 *
 * Input: Binary frames via stdin
 *   [Type: 1 byte][Length: 4 bytes LE][Payload: N bytes]
 *
 *   Types:
 *   - 0x01 AUDIO:    PCM chunk (16-bit, 16kHz, mono, little-endian) - accumulated
 *   - 0x02 SENTENCE: JSON with text and word timestamps
 *   - 0x03 CONFIG:   JSON configuration (optional)
 *   - 0x04 RESET:    Clear audio buffer (start new stream)
 *   - 0xFF END:      Finalize and exit
 *
 * Output: JSON Lines via stdout
 *   {"type":"viseme","start":0.15,"end":0.25,"value":"B"}
 *   {"type":"end","total_visemes":42,"duration":3.25}
 */

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <format.h>
#include <json.hpp>

// Windows binary mode for stdin/stdout
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "protocol/FrameTypes.h"
#include "protocol/FrameReader.h"
#include "protocol/FrameWriter.h"
#include "queue/Sentence.h"
#include "queue/SentenceQueue.h"
#include "queue/WorkerPool.h"
#include "processing/AudioSlicer.h"
#include "processing/PhoneRecognizer.h"
#include "animation/SentenceAnimator.h"

using namespace rhubarb_stream;

/**
 * Audio buffer manager
 *
 * Accumulates PCM audio chunks and provides access for sentence processing.
 */
class AudioBuffer {
public:
	void append(const int16_t* samples, size_t count) {
		size_t oldSize = samples_.size();
		samples_.resize(oldSize + count);
		std::memcpy(samples_.data() + oldSize, samples, count * sizeof(int16_t));
	}

	void clear() {
		samples_.clear();
		samples_.shrink_to_fit();
	}

	size_t sampleCount() const { return samples_.size(); }
	size_t byteCount() const { return samples_.size() * sizeof(int16_t); }
	double durationSeconds() const {
		return static_cast<double>(samples_.size()) / Limits::SAMPLE_RATE;
	}

	const int16_t* data() const { return samples_.data(); }
	bool empty() const { return samples_.empty(); }

private:
	std::vector<int16_t> samples_;
};

/**
 * Configuration for rhubarb-stream processing
 */
struct StreamConfig {
	int sampleRate = Limits::SAMPLE_RATE;
	bool debugEnabled = true;
	int threadCount = 0;  // 0 = auto-calculate
};

/**
 * Shared processing components (thread-safe)
 *
 * PhoneRecognizer handles thread-local decoder storage internally.
 * SentenceAnimator is stateless and safe to share.
 */
static PhoneRecognizer g_phoneRecognizer;
static SentenceAnimator g_sentenceAnimator;

/**
 * Real sentence processor for Phase 4
 *
 * Pipeline:
 * 1. AudioSlicer extracts audio segment for sentence
 * 2. PhoneRecognizer runs PocketSphinx with sentence text as dialog hint
 * 3. SentenceAnimator converts phones to visemes
 * 4. Visemes emitted via FrameWriter
 */
void realSentenceProcessor(
	const Sentence& sentence,
	const int16_t* audioData,
	size_t audioSampleCount,
	FrameWriter& writer
) {
	// Create audio slicer
	AudioSlicer slicer(audioData, audioSampleCount);

	// Extract audio slice with padding
	auto audioSlice = slicer.slice(sentence.start, sentence.end);
	double actualStart = slicer.getActualStart(sentence.start);

	writer.emitDebug(fmt::format(
		"Processing sentence #{}: '{}' (audio: {} samples, {:.2f}s)",
		sentence.index,
		sentence.text.substr(0, 40),
		audioSlice.size(),
		static_cast<double>(audioSlice.size()) / Limits::SAMPLE_RATE
	));

	if (audioSlice.empty()) {
		writer.emitDebug(fmt::format(
			"Sentence #{}: No audio data available",
			sentence.index
		));
		return;
	}

	try {
		// Recognize phones using PocketSphinx
		auto phones = g_phoneRecognizer.recognizePhones(
			audioSlice,
			sentence.text,
			actualStart
		);

		writer.emitDebug(fmt::format(
			"Sentence #{}: Recognized {} phones",
			sentence.index,
			phones.size()
		));

		// Convert phones to visemes
		auto visemes = g_sentenceAnimator.animate(phones);

		writer.emitDebug(fmt::format(
			"Sentence #{}: Generated {} visemes",
			sentence.index,
			visemes.size()
		));

		// Emit visemes
		for (const auto& viseme : visemes) {
			writer.emitViseme(viseme.start, viseme.end, viseme.shape);
		}

	} catch (const std::exception& e) {
		writer.emitError(fmt::format(
			"Sentence #{} processing error: {}",
			sentence.index,
			e.what()
		));
	}
}

/**
 * Main frame processor
 *
 * Handles the frame processing loop and orchestrates components.
 */
class FrameProcessor {
public:
	FrameProcessor(FrameReader& reader, FrameWriter& writer)
		: reader_(reader), writer_(writer) {}

	int run() {
		// Signal ready
		writer_.emitReady();

		Frame frame;
		size_t sentenceIndex = 0;

		while (true) {
			ReadResult result = reader_.readFrame(frame);

			if (result == ReadResult::EndOfStream) {
				// Clean EOF - finalize
				finalizeProcessing();
				writer_.emitEnd(audioBuffer_.durationSeconds());
				return 0;
			}

			if (result == ReadResult::ReadError) {
				writer_.emitError("Failed to read frame from stdin");
				return 1;
			}

			if (result == ReadResult::PayloadTooLarge) {
				writer_.emitError("Frame payload exceeds maximum size");
				return 1;
			}

			if (result == ReadResult::InvalidFrameType) {
				writer_.emitError("Invalid frame type received");
				return 1;
			}

			// Process frame based on type
			switch (frame.type()) {
				case FrameType::Audio:
					if (!processAudioFrame(frame)) {
						return 1;
					}
					break;

				case FrameType::Sentence:
					if (!processSentenceFrame(frame, sentenceIndex++)) {
						return 1;
					}
					break;

				case FrameType::Config:
					if (!processConfigFrame(frame)) {
						return 1;
					}
					break;

				case FrameType::Reset:
					processResetFrame();
					sentenceIndex = 0;
					break;

				case FrameType::End:
					finalizeProcessing();
					writer_.emitEnd(maxTime_);
					return 0;

				default:
					writer_.emitError(fmt::format("Unknown frame type: 0x{:02X}",
						static_cast<uint8_t>(frame.type())));
					break;
			}
		}
	}

private:
	bool processAudioFrame(const Frame& frame) {
		// Skip zero-length audio frames (malformed but non-fatal)
		if (frame.payloadSize() == 0) {
			writer_.emitDebug("Ignoring zero-length audio frame");
			return true;
		}

		// Validate alignment (must be even for 16-bit samples)
		if (frame.payloadSize() % Limits::BYTES_PER_SAMPLE != 0) {
			writer_.emitError(fmt::format(
				"Audio payload must be even bytes, got {}", frame.payloadSize()));
			return false;
		}

		// Check accumulated buffer size limit
		size_t newSampleCount = frame.sampleCount();
		size_t totalBytes = audioBuffer_.byteCount() + frame.payloadSize();
		if (totalBytes > Limits::MAX_AUDIO_BYTES) {
			writer_.emitError(fmt::format(
				"Audio buffer size {} bytes exceeds maximum {} bytes",
				totalBytes, Limits::MAX_AUDIO_BYTES));
			return false;
		}

		// Accumulate audio chunk
		audioBuffer_.append(frame.payloadAsSamples(), newSampleCount);

		writer_.emitDebug(fmt::format(
			"Audio chunk: +{} samples, total: {} ({:.2f}s)",
			newSampleCount,
			audioBuffer_.sampleCount(),
			audioBuffer_.durationSeconds()
		));

		return true;
	}

	bool processSentenceFrame(const Frame& frame, size_t index) {
		auto sentence = Sentence::fromJson(frame.payloadAsString(), index);
		if (!sentence) {
			writer_.emitError(fmt::format(
				"Failed to parse SENTENCE frame #{} - sentence will be skipped", index));
			return true;  // Non-fatal, continue processing (but client is notified)
		}

		writer_.emitDebug(fmt::format(
			"Sentence #{}: '{}' ({:.2f}s - {:.2f}s, {} words)",
			index,
			sentence->text.substr(0, 50),
			sentence->start,
			sentence->end,
			sentence->wordCount()
		));

		// Track max time for final duration
		if (sentence->end > maxTime_) {
			maxTime_ = sentence->end;
		}

		// Ensure worker pool is started
		ensureWorkerPoolStarted();

		// Queue sentence for processing
		sentenceQueue_.push(std::move(*sentence));

		return true;
	}

	bool processConfigFrame(const Frame& frame) {
		try {
			auto config = nlohmann::json::parse(frame.payloadAsString());

			// Apply configuration settings
			if (config.contains("sample_rate")) {
				config_.sampleRate = config["sample_rate"].get<int>();
			}
			if (config.contains("debug")) {
				config_.debugEnabled = config["debug"].get<bool>();
				writer_.setDebugEnabled(config_.debugEnabled);
			}
			if (config.contains("thread_count")) {
				config_.threadCount = config["thread_count"].get<int>();
			}

			writer_.emitDebug(fmt::format(
				"Config received: {} keys", config.size()));

		} catch (const nlohmann::json::parse_error& e) {
			writer_.emitError(fmt::format(
				"JSON parse error in CONFIG frame: {}", e.what()));
		}

		return true;
	}

	void processResetFrame() {
		// Finalize any in-progress work
		finalizeProcessing();

		// Clear state
		audioBuffer_.clear();
		maxTime_ = 0.0;

		// Reset worker pool (will be recreated on next sentence)
		workerPool_.reset();

		writer_.emitDebug("Audio buffer and worker pool reset");
	}

	void ensureWorkerPoolStarted() {
		if (workerPool_) {
			return;  // Already started
		}

		// Calculate thread count
		int threadCount = config_.threadCount;
		if (threadCount <= 0) {
			// Auto-calculate: will be refined as more sentences arrive
			// For now, use a reasonable default based on cores
			int coreCount = static_cast<int>(std::thread::hardware_concurrency());
			if (coreCount == 0) coreCount = 4;
			threadCount = std::min(coreCount, 4);  // Cap at 4 for streaming
		}

		writer_.emitDebug(fmt::format(
			"Creating worker pool with {} threads",
			threadCount
		));

		// Create worker pool with real sentence processor
		workerPool_ = std::make_unique<WorkerPool>(
			sentenceQueue_,
			writer_,
			realSentenceProcessor,
			audioBuffer_.data(),
			audioBuffer_.sampleCount()
		);

		workerPool_->start(threadCount);
	}

	void finalizeProcessing() {
		if (!workerPool_) {
			return;  // No workers to wait for
		}

		writer_.emitDebug("Finalizing: waiting for workers to complete...");

		// Signal no more sentences
		sentenceQueue_.finish();

		// Wait for all workers
		workerPool_->waitForCompletion();

		writer_.emitDebug(fmt::format(
			"Workers completed: {} sentences processed",
			workerPool_->sentencesProcessed()
		));
	}

	FrameReader& reader_;
	FrameWriter& writer_;
	AudioBuffer audioBuffer_;
	StreamConfig config_;
	double maxTime_ = 0.0;

	// Parallel processing
	SentenceQueue sentenceQueue_;
	std::unique_ptr<WorkerPool> workerPool_;
};

int main() {
	// Set binary mode for stdin/stdout on Windows
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	// Disable stdio synchronization for better performance
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(nullptr);

	// Create protocol components
	FrameReader reader(std::cin);
	FrameWriter writer(std::cout);

	// Run the frame processor
	FrameProcessor processor(reader, writer);
	return processor.run();
}
