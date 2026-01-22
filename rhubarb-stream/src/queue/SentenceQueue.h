#pragma once

#include "Sentence.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>

namespace rhubarb_stream {

/**
 * Thread-safe queue for sentences awaiting processing
 *
 * Producer: Main thread parses SENTENCE frames and enqueues
 * Consumers: Worker threads dequeue and process sentences
 *
 * Usage:
 *   SentenceQueue queue;
 *
 *   // Producer (main thread)
 *   queue.push(sentence);
 *   queue.finish();  // Signal no more sentences
 *
 *   // Consumer (worker thread)
 *   while (auto sentence = queue.pop()) {
 *       process(*sentence);
 *   }
 */
class SentenceQueue {
public:
	SentenceQueue() = default;

	// Non-copyable, non-movable
	SentenceQueue(const SentenceQueue&) = delete;
	SentenceQueue& operator=(const SentenceQueue&) = delete;

	/**
	 * Add a sentence to the queue (producer)
	 *
	 * Thread-safe. Can be called from any thread.
	 * Must not be called after finish().
	 */
	void push(Sentence sentence);

	/**
	 * Remove and return the next sentence (consumer)
	 *
	 * Blocks until a sentence is available or the queue is finished.
	 * Returns nullopt when queue is empty AND finished.
	 *
	 * Thread-safe. Multiple workers can call concurrently.
	 */
	std::optional<Sentence> pop();

	/**
	 * Signal that no more sentences will be added
	 *
	 * After calling finish(), all waiting pop() calls will wake up
	 * and return nullopt once the queue is empty.
	 */
	void finish();

	/**
	 * Check if the queue has been marked as finished
	 */
	bool isFinished() const { return finished_.load(); }

	/**
	 * Get current queue size (approximate, may change immediately)
	 */
	size_t size() const;

	/**
	 * Check if queue is empty (approximate)
	 */
	bool empty() const;

	/**
	 * Get total number of sentences pushed
	 */
	size_t totalPushed() const { return totalPushed_.load(); }

	/**
	 * Get total number of sentences popped
	 */
	size_t totalPopped() const { return totalPopped_.load(); }

private:
	mutable std::mutex mutex_;
	std::condition_variable condition_;
	std::queue<Sentence> queue_;
	std::atomic<bool> finished_{false};
	std::atomic<size_t> totalPushed_{0};
	std::atomic<size_t> totalPopped_{0};
};

} // namespace rhubarb_stream
