#pragma once

#include "../core/Shape.h"
#include <ostream>
#include <mutex>
#include <string>
#include <atomic>

namespace rhubarb_stream {

/**
 * Thread-safe JSON line writer for stdout protocol
 *
 * All output methods are thread-safe and can be called from worker threads.
 * Messages are written as newline-delimited JSON (JSON Lines format).
 *
 * Output message types:
 *   - ready:  {"type":"ready","version":"1.0.0"}
 *   - viseme: {"type":"viseme","start":0.15,"end":0.25,"value":"B"}
 *   - flush:  {"type":"flush","timestamp":0.50}
 *   - end:    {"type":"end","total_visemes":42,"duration":3.25}
 *   - error:  {"type":"error","message":"..."}
 *   - debug:  {"type":"debug","message":"..."}
 */
class FrameWriter {
public:
	static constexpr const char* VERSION = "1.0.0";

	/**
	 * Construct a frame writer for the given output stream
	 *
	 * @param output Output stream (typically std::cout)
	 */
	explicit FrameWriter(std::ostream& output);

	// Non-copyable (holds mutex and reference to stream)
	FrameWriter(const FrameWriter&) = delete;
	FrameWriter& operator=(const FrameWriter&) = delete;

	/**
	 * Write the ready message (call once at startup)
	 */
	void emitReady();

	/**
	 * Write a viseme message
	 *
	 * @param start Start time in seconds
	 * @param end End time in seconds
	 * @param shape Viseme shape (A-H, X)
	 */
	void emitViseme(double start, double end, Shape shape);

	/**
	 * Write a flush message (indicates safe point for client to render)
	 *
	 * @param timestamp Current processing timestamp in seconds
	 */
	void emitFlush(double timestamp);

	/**
	 * Write the end message (call once when processing completes)
	 *
	 * @param duration Total audio duration in seconds
	 */
	void emitEnd(double duration);

	/**
	 * Write an error message
	 *
	 * @param message Error description
	 */
	void emitError(const std::string& message);

	/**
	 * Write a debug message (disabled in release builds by default)
	 *
	 * @param message Debug information
	 */
	void emitDebug(const std::string& message);

	/**
	 * Enable or disable debug messages
	 */
	void setDebugEnabled(bool enabled) { debugEnabled_ = enabled; }
	bool isDebugEnabled() const { return debugEnabled_; }

	/**
	 * Get the total number of visemes emitted
	 */
	int visemeCount() const { return visemeCount_.load(); }

	/**
	 * Get the maximum timestamp seen across all visemes
	 */
	double maxTimestamp() const { return maxTimestamp_; }

private:
	/**
	 * Write a JSON line to output (thread-safe)
	 */
	void writeLine(const std::string& json);

	/**
	 * Escape a string for JSON output
	 */
	static std::string escapeJson(const std::string& str);

	std::ostream& output_;
	std::mutex mutex_;
	std::atomic<int> visemeCount_{0};
	double maxTimestamp_ = 0.0;
	bool debugEnabled_ = true;  // Enable debug by default for development
};

} // namespace rhubarb_stream
