#include "FrameWriter.h"
#include <format.h>
#include <sstream>

namespace rhubarb_stream {

FrameWriter::FrameWriter(std::ostream& output)
	: output_(output)
{
}

void FrameWriter::writeLine(const std::string& json) {
	std::lock_guard<std::mutex> lock(mutex_);
	output_ << json << "\n" << std::flush;
}

std::string FrameWriter::escapeJson(const std::string& str) {
	std::ostringstream escaped;
	for (char c : str) {
		switch (c) {
			case '"':  escaped << "\\\""; break;
			case '\\': escaped << "\\\\"; break;
			case '\b': escaped << "\\b";  break;
			case '\f': escaped << "\\f";  break;
			case '\n': escaped << "\\n";  break;
			case '\r': escaped << "\\r";  break;
			case '\t': escaped << "\\t";  break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					// Control character - encode as \uXXXX
					escaped << fmt::format("\\u{:04x}", static_cast<unsigned char>(c));
				} else {
					escaped << c;
				}
				break;
		}
	}
	return escaped.str();
}

void FrameWriter::emitReady() {
	writeLine(fmt::format(
		R"({{"type":"ready","version":"{}"}})",
		VERSION
	));
}

void FrameWriter::emitViseme(double start, double end, Shape shape) {
	// Track statistics (atomic increment)
	visemeCount_++;

	// Format JSON outside lock for better concurrency
	std::string json = fmt::format(
		R"({{"type":"viseme","start":{:.3f},"end":{:.3f},"value":"{}"}})",
		start, end, ShapeConverter::get().toString(shape)
	);

	// Thread-safe output and timestamp update
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (end > maxTimestamp_) {
			maxTimestamp_ = end;
		}
		output_ << json << "\n" << std::flush;
	}
}

void FrameWriter::emitFlush(double timestamp) {
	writeLine(fmt::format(
		R"({{"type":"flush","timestamp":{:.3f}}})",
		timestamp
	));
}

void FrameWriter::emitEnd(double duration) {
	writeLine(fmt::format(
		R"({{"type":"end","total_visemes":{},"duration":{:.3f}}})",
		visemeCount_.load(),
		duration
	));
}

void FrameWriter::emitError(const std::string& message) {
	writeLine(fmt::format(
		R"({{"type":"error","message":"{}"}})",
		escapeJson(message)
	));
}

void FrameWriter::emitDebug(const std::string& message) {
	if (!debugEnabled_) return;

	writeLine(fmt::format(
		R"({{"type":"debug","message":"{}"}})",
		escapeJson(message)
	));
}

} // namespace rhubarb_stream
