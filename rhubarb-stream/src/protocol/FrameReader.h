#pragma once

#include "FrameTypes.h"
#include <istream>
#include <memory>

namespace rhubarb_stream {

/**
 * Binary frame reader for stdin protocol
 *
 * Reads length-prefixed binary frames from an input stream.
 * Format: [Type: 1 byte][Length: 4 bytes LE][Payload: N bytes]
 *
 * Usage:
 *   FrameReader reader(std::cin);
 *   Frame frame;
 *   while (reader.readFrame(frame) == ReadResult::Success) {
 *       // Process frame
 *   }
 */
class FrameReader {
public:
	/**
	 * Construct a frame reader for the given input stream
	 *
	 * @param input Input stream (typically std::cin in binary mode)
	 */
	explicit FrameReader(std::istream& input);

	// Non-copyable, non-movable (holds reference to stream)
	FrameReader(const FrameReader&) = delete;
	FrameReader& operator=(const FrameReader&) = delete;
	FrameReader(FrameReader&&) = delete;
	FrameReader& operator=(FrameReader&&) = delete;

	/**
	 * Read the next frame from the input stream
	 *
	 * @param frame Output parameter filled with the parsed frame
	 * @return ReadResult indicating success or type of failure
	 */
	ReadResult readFrame(Frame& frame);

	/**
	 * Get the total number of frames read successfully
	 */
	size_t framesRead() const { return framesRead_; }

	/**
	 * Get the total bytes read from the stream
	 */
	size_t bytesRead() const { return bytesRead_; }

private:
	/**
	 * Read exactly n bytes from the input stream
	 *
	 * @param buffer Destination buffer
	 * @param count Number of bytes to read
	 * @return true if exactly count bytes were read, false otherwise
	 */
	bool readBytes(void* buffer, size_t count);

	std::istream& input_;
	size_t framesRead_ = 0;
	size_t bytesRead_ = 0;
};

} // namespace rhubarb_stream
