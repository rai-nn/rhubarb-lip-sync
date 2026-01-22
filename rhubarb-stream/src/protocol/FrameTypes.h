#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <optional>

namespace rhubarb_stream {

/**
 * Binary frame types for stdin protocol
 *
 * Frame format: [Type: 1 byte][Length: 4 bytes LE][Payload: N bytes]
 */
enum class FrameType : uint8_t {
	Audio    = 0x01,  // PCM chunk (16-bit, 16kHz, mono, little-endian)
	Sentence = 0x02,  // JSON with text and word timestamps
	Config   = 0x03,  // JSON configuration (optional)
	Reset    = 0x04,  // Clear audio buffer (start new stream)
	End      = 0xFF   // Finalize and exit
};

/**
 * Constants for protocol limits
 */
namespace Limits {
	constexpr size_t MAX_AUDIO_BYTES = 100 * 1024 * 1024;   // 100MB max audio buffer
	constexpr size_t MAX_PAYLOAD_BYTES = 50 * 1024 * 1024;  // 50MB max single frame
	constexpr int SAMPLE_RATE = 16000;                       // Expected sample rate (16kHz)
	constexpr int BYTES_PER_SAMPLE = 2;                      // 16-bit PCM
}

/**
 * Frame header structure (5 bytes total)
 */
struct FrameHeader {
	FrameType type;
	uint32_t payloadLength;
};

/**
 * Parsed frame with header and payload
 */
struct Frame {
	FrameHeader header;
	std::vector<uint8_t> payload;

	// Convenience accessors
	FrameType type() const { return header.type; }
	size_t payloadSize() const { return payload.size(); }

	// Get payload as string (for JSON frames)
	std::string payloadAsString() const {
		return std::string(payload.begin(), payload.end());
	}

	// Get payload as PCM samples (for audio frames)
	// Note: Caller must verify payloadSize() is even
	const int16_t* payloadAsSamples() const {
		return reinterpret_cast<const int16_t*>(payload.data());
	}

	size_t sampleCount() const {
		return payload.size() / Limits::BYTES_PER_SAMPLE;
	}
};

/**
 * Result type for frame reading operations
 */
enum class ReadResult {
	Success,         // Frame read successfully
	EndOfStream,     // Clean EOF (no more frames)
	ReadError,       // I/O error during read
	PayloadTooLarge, // Payload exceeds MAX_PAYLOAD_BYTES
	InvalidFrameType // Unknown frame type byte
};

/**
 * Convert FrameType to human-readable string
 */
inline const char* frameTypeName(FrameType type) {
	switch (type) {
		case FrameType::Audio:    return "AUDIO";
		case FrameType::Sentence: return "SENTENCE";
		case FrameType::Config:   return "CONFIG";
		case FrameType::Reset:    return "RESET";
		case FrameType::End:      return "END";
		default:                  return "UNKNOWN";
	}
}

} // namespace rhubarb_stream
