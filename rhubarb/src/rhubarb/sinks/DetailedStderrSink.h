#pragma once

#include "logging/Sink.h"
#include "logging/Entry.h"
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <variant>
#include <thread>

// Forward declaration
struct UtteranceOutputInfo;

// Pipeline phases for tracking
enum class PipelinePhase {
	Initialization,
	AudioLoading,
	VoiceActivityDetection,
	SpeechRecognition,
	PhonemeMapping,
	AnimationGeneration,
	Export,
	Complete
};

// Event structure to store pipeline timing information
struct PipelineEvent {
	std::chrono::steady_clock::time_point timestamp;
	std::thread::id threadId;
	PipelinePhase phase;
	std::string description;
	std::map<std::string, std::variant<int, double, std::string>> metrics;
	
	// For utterance-specific events
	bool isUtterance = false;
	int utteranceIndex = -1;
	double utteranceStart = 0.0;
	double utteranceEnd = 0.0;
	std::string utteranceText;
	
	// For tracking processing duration
	bool isUtteranceStart = false;
	bool isUtteranceEnd = false;
	double processingDuration = 0.0; // Only used for end events
};

// Thread execution info for the summary
struct ThreadExecutionInfo {
	std::thread::id threadId;
	int threadNumber;
	std::vector<PipelineEvent> events;
	double totalActiveTime = 0.0;
};

class DetailedStderrSink : public logging::Sink {
public:
	DetailedStderrSink(logging::Level minLevel, bool includeThreadTimeline = true, bool verbose = false, int threadLimit = -1, bool threadLimitExplicit = false);
	void receive(const logging::Entry& entry) override;
	logging::Level getMinLevel() const { return minLevel; }
	
private:
	void startPhase(PipelinePhase phase, const std::string& description);
	void endPhase(PipelinePhase phase);
	void addEvent(const PipelineEvent& event);
	void printSummary();
	void printMinimalProgress(const std::string& message);
	
	std::string phaseToString(PipelinePhase phase) const;
	std::string formatDuration(double seconds) const;
	std::string formatTimestamp(const std::chrono::steady_clock::time_point& tp) const;
	
	logging::Level minLevel;
	bool includeThreadTimeline;
	bool verbose;
	
	// Thread-safe event storage
	std::mutex eventMutex;
	std::vector<PipelineEvent> events;
	
	// Phase timing
	std::chrono::steady_clock::time_point startTime;
	std::chrono::steady_clock::time_point currentPhaseStart;
	PipelinePhase currentPhase = PipelinePhase::Initialization;
	std::map<PipelinePhase, double> phaseDurations;
	
	// Recognition phase specifics
	int totalUtterances = 0;
	int completedUtterances = 0;
	int totalWords = 0;
	int totalPhonemes = 0;
	std::vector<std::pair<double, double>> speechSegments;
	std::vector<UtteranceOutputInfo> utteranceResults;
	
	// Voice activity detection specifics
	int speechSegmentCount = 0;
	int silenceSegmentCount = 0;
	double voiceTimelineTotalDuration = 0.0;
	double voiceTimelineSpeechTime = 0.0;
	double voiceTimelineSilenceTime = 0.0;
	std::vector<std::pair<double, double>> voiceTimelineSegments;
	
	// Animation phase specifics
	int totalVisemes = 0;
	int consolidatedSegments = 0;
	int insertedTweens = 0;
	int addedPauses = 0;
	
	// Audio info
	double audioDuration = 0.0;
	int sampleRate = 0;
	
	// Thread tracking
	std::map<std::thread::id, int> threadIdToNumber;
	int nextThreadNumber = 1;
	int maxThreadsUsed = 0;
	
	// CPU and thread configuration
	int machineCpuCount = 0;
	int threadLimit = -1;
	bool threadLimitExplicit = false;
	
	// Whether we've already printed the summary
	bool summaryPrinted = false;
	
	// Sub-phase timings
	std::map<std::string, double> subPhaseTimings;
	
	// Utterance sub-step timings
	struct UtteranceSubStep {
		std::string name;
		double duration;
		std::string details;
	};
	std::map<int, std::vector<UtteranceSubStep>> utteranceSubSteps;
};