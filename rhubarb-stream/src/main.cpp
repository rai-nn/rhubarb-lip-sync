/**
 * rhubarb-stream - Real-time streaming lip-sync viseme generation
 *
 * Input: Binary frames via stdin
 *   [Type: 1 byte][Length: 4 bytes LE][Payload: N bytes]
 *
 *   Types:
 *   - 0x01 AUDIO:    Full PCM buffer (16-bit, 16kHz, mono, little-endian)
 *   - 0x02 SENTENCE: JSON with text and word timestamps
 *   - 0x03 CONFIG:   JSON configuration (optional)
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

#include "core/Phone.h"
#include "core/Shape.h"
#include "time/centiseconds.h"
#include "animation/animationRules.h"

// Frame types
enum class FrameType : uint8_t {
	Audio = 0x01,
	Sentence = 0x02,
	Config = 0x03,
	End = 0xFF
};

// Read exactly n bytes from stdin
bool readBytes(void* buffer, size_t count) {
	return std::cin.read(reinterpret_cast<char*>(buffer), count).good();
}

// Write a JSON line to stdout (thread-safe in future)
void writeJsonLine(const std::string& json) {
	std::cout << json << "\n" << std::flush;
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

int main() {
	// Use binary mode for stdin
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(nullptr);

	// Signal ready
	emitReady();

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

		// Read payload
		std::vector<uint8_t> payload(payloadLength);
		if (payloadLength > 0 && !readBytes(payload.data(), payloadLength)) {
			emitError("Failed to read frame payload");
			return 1;
		}

		// Process frame based on type
		switch (static_cast<FrameType>(frameType)) {
			case FrameType::Audio: {
				// Store audio buffer (PCM 16-bit samples)
				size_t sampleCount = payloadLength / 2;
				audioBuffer.resize(sampleCount);
				std::memcpy(audioBuffer.data(), payload.data(), payloadLength);

				// Debug: acknowledge audio received
				writeJsonLine(fmt::format(
					"{{\"type\":\"debug\",\"message\":\"Audio received: {} samples ({:.2f}s)\"}}",
					sampleCount, static_cast<double>(sampleCount) / 16000.0
				));
				break;
			}

			case FrameType::Sentence: {
				// TODO: Parse JSON and process sentence
				// For now, just acknowledge receipt
				std::string jsonStr(payload.begin(), payload.end());
				writeJsonLine(fmt::format(
					"{{\"type\":\"debug\",\"message\":\"Sentence received: {} bytes\"}}",
					payloadLength
				));

				// TODO: Phase 3-4 implementation:
				// 1. Parse sentence JSON (text, start, end, words[])
				// 2. Extract audio slice from audioBuffer
				// 3. Run PocketSphinx recognition with sentence text as hint
				// 4. Convert phones to visemes
				// 5. Emit visemes with absolute timestamps

				break;
			}

			case FrameType::Config: {
				// TODO: Parse configuration JSON
				std::string jsonStr(payload.begin(), payload.end());
				writeJsonLine(fmt::format(
					"{{\"type\":\"debug\",\"message\":\"Config received: {} bytes\"}}",
					payloadLength
				));
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
