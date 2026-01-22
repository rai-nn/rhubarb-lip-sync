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
	// TODO: Add more config options (pause threshold, speech speed, etc.)
};

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
					break;

				case FrameType::End:
					// TODO: Wait for worker pool to complete
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

		// TODO: Phase 3-4 implementation:
		// 1. Queue sentence for worker pool
		// 2. Worker extracts audio slice using start/end times
		// 3. Run PocketSphinx recognition with sentence text as hint
		// 4. Convert phones to visemes using animationRules
		// 5. Emit visemes with absolute timestamps

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

			writer_.emitDebug(fmt::format(
				"Config received: {} keys", config.size()));

		} catch (const nlohmann::json::parse_error& e) {
			writer_.emitError(fmt::format(
				"JSON parse error in CONFIG frame: {}", e.what()));
		}

		return true;
	}

	void processResetFrame() {
		audioBuffer_.clear();
		maxTime_ = 0.0;

		writer_.emitDebug("Audio buffer reset");
	}

	FrameReader& reader_;
	FrameWriter& writer_;
	AudioBuffer audioBuffer_;
	StreamConfig config_;
	double maxTime_ = 0.0;
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
