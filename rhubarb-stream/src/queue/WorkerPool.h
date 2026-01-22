#pragma once

#include "SentenceQueue.h"
#include "../protocol/FrameWriter.h"
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <memory>

namespace rhubarb_stream {

/**
 * Callback type for processing a sentence
 *
 * Parameters:
 *   - sentence: The sentence to process
 *   - audioData: Pointer to full audio buffer
 *   - audioSampleCount: Number of samples in audio buffer
 *   - writer: Thread-safe output writer
 *
 * The callback should:
 *   1. Extract audio slice for sentence time range
 *   2. Run speech recognition
 *   3. Convert phones to visemes
 *   4. Emit visemes via writer
 */
using SentenceProcessor = std::function<void(
	const Sentence& sentence,
	const int16_t* audioData,
	size_t audioSampleCount,
	FrameWriter& writer
)>;

/**
 * Pool of worker threads for parallel sentence processing
 *
 * Based on RAI-739 pattern:
 * - Workers pull sentences from queue
 * - Thread-local decoder storage (created on first use, reused)
 * - Parallel decoder creation (no mutex during creation)
 * - Graceful shutdown when queue is finished
 *
 * Thread count calculation:
 *   min(audio_duration/5, cpu_cores, sentence_count)
 *
 * Usage:
 *   WorkerPool pool(queue, writer, processor, audioData, audioSize);
 *   pool.start(threadCount);
 *   // ... push sentences to queue ...
 *   queue.finish();
 *   pool.waitForCompletion();
 */
class WorkerPool {
public:
	/**
	 * Create a worker pool
	 *
	 * @param queue Sentence queue to pull from
	 * @param writer Thread-safe output writer
	 * @param processor Callback to process each sentence
	 * @param audioData Pointer to audio buffer (must remain valid until waitForCompletion)
	 * @param audioSampleCount Number of samples in audio buffer
	 */
	WorkerPool(
		SentenceQueue& queue,
		FrameWriter& writer,
		SentenceProcessor processor,
		const int16_t* audioData,
		size_t audioSampleCount
	);

	~WorkerPool();

	// Non-copyable, non-movable
	WorkerPool(const WorkerPool&) = delete;
	WorkerPool& operator=(const WorkerPool&) = delete;

	/**
	 * Start worker threads
	 *
	 * @param threadCount Number of worker threads to spawn
	 */
	void start(int threadCount);

	/**
	 * Wait for all workers to complete
	 *
	 * Call this after queue.finish() to block until all sentences are processed.
	 */
	void waitForCompletion();

	/**
	 * Check if all workers have completed
	 */
	bool isComplete() const { return completed_.load(); }

	/**
	 * Get number of sentences processed
	 */
	size_t sentencesProcessed() const { return sentencesProcessed_.load(); }

	/**
	 * Get number of active workers
	 */
	int activeWorkers() const { return activeWorkers_.load(); }

	/**
	 * Calculate optimal thread count based on workload
	 *
	 * @param audioDurationSeconds Total audio duration in seconds
	 * @param sentenceCount Number of sentences to process
	 * @return Recommended thread count
	 */
	static int calculateThreadCount(double audioDurationSeconds, int sentenceCount);

private:
	/**
	 * Worker thread main function
	 */
	void workerMain(int workerId);

	SentenceQueue& queue_;
	FrameWriter& writer_;
	SentenceProcessor processor_;
	const int16_t* audioData_;
	size_t audioSampleCount_;

	std::vector<std::thread> workers_;
	std::atomic<bool> started_{false};
	std::atomic<bool> completed_{false};
	std::atomic<size_t> sentencesProcessed_{0};
	std::atomic<int> activeWorkers_{0};
};

} // namespace rhubarb_stream
