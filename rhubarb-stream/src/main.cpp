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
#include <mutex>
#include <format.h>
#include <json.hpp>

// Windows binary mode for stdin/stdout
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "core/Phone.h"
#include "core/Shape.h"
#include "time/centiseconds.h"
#include "animation/animationRules.h"

using json = nlohmann::json;

// Frame types
enum class FrameType : uint8_t {
	Audio = 0x01,
	Sentence = 0x02,
	Config = 0x03,
	Reset = 0x04,
	End = 0xFF
};

// Constants
constexpr size_t MAX_AUDIO_BYTES = 100 * 1024 * 1024;  // 100MB max audio buffer
constexpr size_t MAX_PAYLOAD_BYTES = 50 * 1024 * 1024; // 50MB max single frame payload
constexpr int SAMPLE_RATE = 16000;                      // 16kHz expected sample rate

// Thread-safe output mutex
static std::mutex outputMutex;

// Read exactly n bytes from stdin, handling partial reads
bool readBytes(void* buffer, size_t count) {
	std::cin.read(reinterpret_cast<char*>(buffer), count);
	// Check that we read exactly the requested number of bytes
	if (std::cin.gcount() != static_cast<std::streamsize>(count)) {
		return false;  // Partial read or EOF
	}
	// Accept if stream is good OR if we hit EOF exactly at the boundary
	return std::cin.good() || std::cin.eof();
}

// Write a JSON line to stdout (thread-safe)
void writeJsonLine(const std::string& jsonStr) {
	std::lock_guard<std::mutex> lock(outputMutex);
	std::cout << jsonStr << "\n" << std::flush;
}

// Output a viseme
void emitViseme(double start, double end, Shape shape) {
	writeJsonLine(fmt::format(
		R"({{"type":"viseme","start":{:.3f},"end":{:.3f},"value":"{}"}})",
		start, end, ShapeConverter::get().toString(shape)
	));
}

// Output the final end message
void emitEnd(int totalVisemes, double duration) {
	writeJsonLine(fmt::format(
		R"({{"type":"end","total_visemes":{},"duration":{:.3f}}})",
		totalVisemes, duration
	));
}

// Output an error message
void emitError(const std::string& message) {
	writeJsonLine(fmt::format(
		R"({{"type":"error","message":"{}"}})",
		message
	));
}

// Output a ready message
void emitReady() {
	writeJsonLine(R"({"type":"ready","version":"1.0.0"})");
}

// Convert centiseconds to seconds
inline double toSeconds(centiseconds cs) {
	return cs.count() / 100.0;
}

int main() {
	// Set binary mode for stdin/stdout on Windows
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	// Disable stdio synchronization for better performance
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(nullptr);

	// Signal ready
	emitReady();

	// Audio buffer accumulates chunks across frames
	std::vector<int16_t> audioBuffer;
	int totalVisemes = 0;
	double maxTime = 0.0;

	// Main frame processing loop
	while (true) {
		// Read frame header: type (1 byte) + length (4 bytes LE)
		uint8_t frameType;
		uint32_t payloadLength;

		if (!readBytes(&frameType, 1)) {
			// EOF or error - exit gracefully
			break;
		}

		if (!readBytes(&payloadLength, 4)) {
			emitError("Failed to read frame length");
			return 1;
		}

		// Validate payload size
		if (payloadLength > MAX_PAYLOAD_BYTES) {
			emitError(fmt::format("Payload too large: {} bytes (max {})",
				payloadLength, MAX_PAYLOAD_BYTES));
			return 1;
		}

		// Read payload
		std::vector<uint8_t> payload(payloadLength);
		if (payloadLength > 0 && !readBytes(payload.data(), payloadLength)) {
			emitError("Failed to read frame payload");
			return 1;
		}

		// Process frame based on type
		switch (static_cast<FrameType>(frameType)) {
			case FrameType::Audio: {
				// Validate alignment (must be even for 16-bit samples)
				if (payloadLength % 2 != 0) {
					emitError(fmt::format("Audio payload must be even bytes, got {}", payloadLength));
					return 1;
				}

				// Check accumulated buffer size limit
				size_t newSampleCount = payloadLength / 2;
				size_t totalBytes = (audioBuffer.size() + newSampleCount) * 2;
				if (totalBytes > MAX_AUDIO_BYTES) {
					emitError(fmt::format("Audio buffer would exceed {} bytes limit", MAX_AUDIO_BYTES));
					return 1;
				}

				// Accumulate audio chunk (append, not replace)
				size_t oldSize = audioBuffer.size();
				audioBuffer.resize(oldSize + newSampleCount);
				std::memcpy(audioBuffer.data() + oldSize, payload.data(), payloadLength);

				// Debug: acknowledge audio received
				writeJsonLine(fmt::format(
					R"JSON({{"type":"debug","message":"Audio chunk: +{} samples, total: {} ({:.2f}s)"}})JSON",
					newSampleCount,
					audioBuffer.size(),
					static_cast<double>(audioBuffer.size()) / SAMPLE_RATE
				));
				break;
			}

			case FrameType::Sentence: {
				// Parse JSON sentence
				std::string jsonStr(payload.begin(), payload.end());

				try {
					json sentence = json::parse(jsonStr);

					// Extract sentence data
					std::string text = sentence.value("text", "");
					double start = sentence.value("start", 0.0);
					double end = sentence.value("end", 0.0);

					writeJsonLine(fmt::format(
						R"JSON({{"type":"debug","message":"Sentence: '{}' ({:.2f}s - {:.2f}s)"}})JSON",
						text.substr(0, 50), start, end
					));

					// TODO: Phase 3-4 implementation:
					// 1. Extract audio slice from audioBuffer using start/end times
					// 2. Run PocketSphinx recognition with sentence text as hint
					// 3. Convert phones to visemes using animationRules
					// 4. Emit visemes with absolute timestamps

					// Track max time for final duration
					if (end > maxTime) {
						maxTime = end;
					}

				} catch (const json::parse_error& e) {
					emitError(fmt::format("JSON parse error in SENTENCE frame: {}", e.what()));
				}
				break;
			}

			case FrameType::Config: {
				// Parse configuration JSON
				std::string jsonStr(payload.begin(), payload.end());

				try {
					json config = json::parse(jsonStr);

					// TODO: Apply configuration settings
					// - sample_rate (verify matches expected)
					// - pause_threshold
					// - speech_speed (slow/normal/fast)

					writeJsonLine(fmt::format(
						R"({{"type":"debug","message":"Config received: {} keys"}})",
						config.size()
					));

				} catch (const json::parse_error& e) {
					emitError(fmt::format("JSON parse error in CONFIG frame: {}", e.what()));
				}
				break;
			}

			case FrameType::Reset: {
				// Clear audio buffer for new stream
				audioBuffer.clear();
				audioBuffer.shrink_to_fit();
				totalVisemes = 0;
				maxTime = 0.0;

				writeJsonLine(R"({"type":"debug","message":"Audio buffer reset"})");
				break;
			}

			case FrameType::End: {
				// Finalize: wait for all workers, emit end message
				// TODO: Wait for worker pool to complete
				emitEnd(totalVisemes, maxTime);
				return 0;
			}

			default: {
				emitError(fmt::format("Unknown frame type: 0x{:02X}", frameType));
				break;
			}
		}
	}

	// If we get here, stdin closed unexpectedly
	emitEnd(totalVisemes, maxTime);
	return 0;
}
