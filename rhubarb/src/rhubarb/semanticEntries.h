#pragma once
#include "logging/Entry.h"
#include <filesystem>

// Marker class for semantic entries
class SemanticEntry : public logging::Entry {
public:
	SemanticEntry(logging::Level level, const std::string& message);
};

class StartEntry : public SemanticEntry {
public:
	StartEntry(const std::filesystem::path& inputFilePath);
	std::filesystem::path getInputFilePath() const;
private:
	std::filesystem::path inputFilePath;
};

class ProgressEntry : public SemanticEntry {
public:
	ProgressEntry(double progress);
	double getProgress() const;
private:
	double progress;
};

class SuccessEntry : public SemanticEntry {
public:
	SuccessEntry();
};

class FailureEntry : public SemanticEntry {
public:
	FailureEntry(const std::string& reason);
	std::string getReason() const;
private:
	std::string reason;
};

// Pipeline phase tracking entries
class PhaseStartEntry : public SemanticEntry {
public:
	PhaseStartEntry(const std::string& phaseName, int totalItems = 0);
	std::string getPhaseName() const;
	int getTotalItems() const;
private:
	std::string phaseName;
	int totalItems;
};

class PhaseEndEntry : public SemanticEntry {
public:
	PhaseEndEntry(const std::string& phaseName, double duration);
	std::string getPhaseName() const;
	double getDuration() const;
private:
	std::string phaseName;
	double duration;
};

class DecoderCreationEntry : public SemanticEntry {
public:
	DecoderCreationEntry(int decoderNumber, double creationTime);
	int getDecoderNumber() const;
	double getCreationTime() const;
private:
	int decoderNumber;
	double creationTime;
};

class UtteranceEntry : public SemanticEntry {
public:
	UtteranceEntry(int index, int total, double startTime, double endTime, const std::string& text);
	int getIndex() const;
	int getTotal() const;
	double getStartTime() const;
	double getEndTime() const;
	std::string getText() const;
private:
	int index;
	int total;
	double startTime;
	double endTime;
	std::string text;
};

class AudioMetadataEntry : public SemanticEntry {
public:
	AudioMetadataEntry(double duration, int sampleRate, int channelCount);
	double getDuration() const;
	int getSampleRate() const;
	int getChannelCount() const;
private:
	double duration;
	int sampleRate;
	int channelCount;
};

class VoiceActivityEntry : public SemanticEntry {
public:
	VoiceActivityEntry(int speechSegments, int silenceSegments);
	int getSpeechSegments() const;
	int getSilenceSegments() const;
private:
	int speechSegments;
	int silenceSegments;
};

class UtteranceStartEntry : public SemanticEntry {
public:
	UtteranceStartEntry(int index, int total, double startTime, double endTime, const std::string& text);
	int getIndex() const;
	int getTotal() const;
	double getStartTime() const;
	double getEndTime() const;
	std::string getText() const;
private:
	int index;
	int total;
	double startTime;
	double endTime;
	std::string text;
};

class UtteranceEndEntry : public SemanticEntry {
public:
	UtteranceEndEntry(int index, int total, double startTime, double endTime, const std::string& text, double processingDuration);
	int getIndex() const;
	int getTotal() const;
	double getStartTime() const;
	double getEndTime() const;
	std::string getText() const;
	double getProcessingDuration() const;
private:
	int index;
	int total;
	double startTime;
	double endTime;
	std::string text;
	double processingDuration;
};
