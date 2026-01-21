#include "progress.h"

#include <mutex>

using std::string;

ProgressForwarder::ProgressForwarder(std::function<void(double progress)> callback) :
	callback(callback)
{}

void ProgressForwarder::reportProgress(double value) {
	callback(value);
}

ProgressMerger::ProgressMerger(ProgressSink& sink) :
	sink(sink)
{}

ProgressMerger::~ProgressMerger() {
	// Debug logging removed for streaming version
}

ProgressSink& ProgressMerger::addSource(const std::string& description, double weight) {
	std::lock_guard<std::mutex> lock(mutex);

	totalWeight += weight;

	const int sourceIndex = static_cast<int>(sources.size());
	sources.push_back({
		description,
		weight,
		std::make_unique<ProgressForwarder>(
			[sourceIndex, this](double progress) {
				sources[sourceIndex].progress = progress;
				report();
			}
		),
		0.0
	});
	return *sources[sourceIndex].forwarder;
}

void ProgressMerger::report() {
	std::lock_guard<std::mutex> lock(mutex);

	if (totalWeight != 0) {
		double weightedSum = 0;
		for (const auto& source : sources) {
			weightedSum += source.weight * source.progress;
		}
		const double progress = weightedSum / totalWeight;
		sink.reportProgress(progress);
	} else {
		sink.reportProgress(0);
	}
}
