#include "SentenceQueue.h"

namespace rhubarb_stream {

void SentenceQueue::push(Sentence sentence) {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.push(std::move(sentence));
	}
	totalPushed_++;
	condition_.notify_one();
}

std::optional<Sentence> SentenceQueue::pop() {
	std::unique_lock<std::mutex> lock(mutex_);

	// Wait until queue has items OR we're finished
	condition_.wait(lock, [this] {
		return !queue_.empty() || finished_.load();
	});

	// If queue is empty and finished, return nullopt
	if (queue_.empty()) {
		return std::nullopt;
	}

	// Get the front sentence
	Sentence sentence = std::move(queue_.front());
	queue_.pop();
	totalPopped_++;

	return sentence;
}

void SentenceQueue::finish() {
	finished_.store(true);
	// Wake up all waiting workers
	condition_.notify_all();
}

size_t SentenceQueue::size() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return queue_.size();
}

bool SentenceQueue::empty() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return queue_.empty();
}

} // namespace rhubarb_stream
