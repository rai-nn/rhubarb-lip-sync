#include "WorkerPool.h"
#include <algorithm>
#include <sstream>

namespace rhubarb_stream {

WorkerPool::WorkerPool(
	SentenceQueue& queue,
	FrameWriter& writer,
	SentenceProcessor processor,
	const int16_t* audioData,
	size_t audioSampleCount
)
	: queue_(queue)
	, writer_(writer)
	, processor_(std::move(processor))
	, audioData_(audioData)
	, audioSampleCount_(audioSampleCount)
{
}

WorkerPool::~WorkerPool() {
	// Ensure all workers are joined
	waitForCompletion();
}

void WorkerPool::start(int threadCount) {
	if (started_.exchange(true)) {
		// Already started
		return;
	}

	if (threadCount < 1) {
		threadCount = 1;
	}

	writer_.emitDebug("Starting " + std::to_string(threadCount) + " worker threads");

	workers_.reserve(threadCount);
	for (int i = 0; i < threadCount; ++i) {
		workers_.emplace_back(&WorkerPool::workerMain, this, i);
	}
}

void WorkerPool::waitForCompletion() {
	// Join all worker threads
	for (auto& worker : workers_) {
		if (worker.joinable()) {
			worker.join();
		}
	}

	completed_.store(true);
}

void WorkerPool::workerMain(int workerId) {
	activeWorkers_++;

	// Get thread ID for logging
	std::thread::id threadId = std::this_thread::get_id();
	std::ostringstream ss;
	ss << threadId;
	std::string threadIdStr = ss.str();

	writer_.emitDebug("Worker " + std::to_string(workerId) + " started (thread " + threadIdStr + ")");

	// Process sentences until queue is empty and finished
	while (auto sentence = queue_.pop()) {
		// Process this sentence
		if (processor_) {
			try {
				processor_(*sentence, audioData_, audioSampleCount_, writer_);
			} catch (const std::exception& e) {
				writer_.emitError("Worker " + std::to_string(workerId) +
					" error processing sentence #" + std::to_string(sentence->index) +
					": " + e.what());
			} catch (...) {
				writer_.emitError("Worker " + std::to_string(workerId) +
					" unknown error processing sentence #" + std::to_string(sentence->index));
			}
		}

		sentencesProcessed_++;
	}

	writer_.emitDebug("Worker " + std::to_string(workerId) + " finished");
	activeWorkers_--;
}

int WorkerPool::calculateThreadCount(double audioDurationSeconds, int sentenceCount) {
	// Based on RAI-739 pattern:
	// - 5 seconds of audio per thread is optimal
	// - Don't exceed CPU core count
	// - Don't exceed sentence count

	// Duration-based limit (minimum 1)
	int durationBasedLimit = std::max(1, static_cast<int>(audioDurationSeconds / 5.0));

	// CPU core count (default to 4 if unknown)
	int coreCount = static_cast<int>(std::thread::hardware_concurrency());
	if (coreCount == 0) {
		coreCount = 4;
	}

	// Don't use more threads than sentences
	int sentenceLimit = std::max(1, sentenceCount);

	// Use minimum of all constraints
	return std::min({durationBasedLimit, coreCount, sentenceLimit});
}

} // namespace rhubarb_stream
