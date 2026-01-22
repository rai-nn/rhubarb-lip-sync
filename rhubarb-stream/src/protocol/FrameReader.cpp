#include "FrameReader.h"

namespace rhubarb_stream {

FrameReader::FrameReader(std::istream& input)
	: input_(input)
{
}

bool FrameReader::readBytes(void* buffer, size_t count) {
	input_.read(reinterpret_cast<char*>(buffer), count);

	// Check that we read exactly the requested number of bytes
	if (input_.gcount() != static_cast<std::streamsize>(count)) {
		return false;  // Partial read or EOF
	}

	// Track bytes read for statistics
	bytesRead_ += count;

	// Accept if stream is good OR if we hit EOF exactly at the boundary
	return input_.good() || input_.eof();
}

ReadResult FrameReader::readFrame(Frame& frame) {
	// Read frame type (1 byte)
	uint8_t frameTypeByte;
	if (!readBytes(&frameTypeByte, 1)) {
		// EOF at frame boundary is clean end of stream
		if (input_.eof() && input_.gcount() == 0) {
			return ReadResult::EndOfStream;
		}
		return ReadResult::ReadError;
	}

	// Validate frame type (known values only)
	FrameType frameType;
	switch (frameTypeByte) {
		case static_cast<uint8_t>(FrameType::Audio):
			frameType = FrameType::Audio;
			break;
		case static_cast<uint8_t>(FrameType::Sentence):
			frameType = FrameType::Sentence;
			break;
		case static_cast<uint8_t>(FrameType::Config):
			frameType = FrameType::Config;
			break;
		case static_cast<uint8_t>(FrameType::Reset):
			frameType = FrameType::Reset;
			break;
		case static_cast<uint8_t>(FrameType::End):
			frameType = FrameType::End;
			break;
		default:
			return ReadResult::InvalidFrameType;
	}

	// Read payload length (4 bytes, little-endian)
	// Manual byte-order conversion for portability across architectures
	uint8_t lengthBytes[4];
	if (!readBytes(lengthBytes, 4)) {
		return ReadResult::ReadError;
	}
	uint32_t payloadLength =
		static_cast<uint32_t>(lengthBytes[0]) |
		(static_cast<uint32_t>(lengthBytes[1]) << 8) |
		(static_cast<uint32_t>(lengthBytes[2]) << 16) |
		(static_cast<uint32_t>(lengthBytes[3]) << 24);

	// Validate payload size
	if (payloadLength > Limits::MAX_PAYLOAD_BYTES) {
		return ReadResult::PayloadTooLarge;
	}

	// Populate frame header
	frame.header.type = frameType;
	frame.header.payloadLength = payloadLength;

	// Read payload
	frame.payload.resize(payloadLength);
	if (payloadLength > 0) {
		if (!readBytes(frame.payload.data(), payloadLength)) {
			return ReadResult::ReadError;
		}
	}

	// Successfully read a frame
	framesRead_++;
	return ReadResult::Success;
}

} // namespace rhubarb_stream
