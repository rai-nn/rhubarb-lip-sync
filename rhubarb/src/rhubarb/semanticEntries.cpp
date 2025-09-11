#include "semanticEntries.h"

using logging::Level;
using std::string;

SemanticEntry::SemanticEntry(Level level, const string& message) :
	Entry(level, message)
{}

StartEntry::StartEntry(const std::filesystem::path& inputFilePath) :
	SemanticEntry(Level::Info, fmt::format("Application startup. Input file: {}.", inputFilePath.u8string())),
	inputFilePath(inputFilePath)
{}

std::filesystem::path StartEntry::getInputFilePath() const {
	return inputFilePath;
}

ProgressEntry::ProgressEntry(double progress) :
	SemanticEntry(Level::Trace, fmt::format("Progress: {}%", static_cast<int>(progress * 100))),
	progress(progress)
{}

double ProgressEntry::getProgress() const {
	return progress;
}

SuccessEntry::SuccessEntry() :
	SemanticEntry(Level::Info, "Application terminating normally.")
{}

FailureEntry::FailureEntry(const string& reason) :
	SemanticEntry(Level::Fatal, fmt::format("Application terminating with error: {}", reason)),
	reason(reason)
{}

string FailureEntry::getReason() const {
	return reason;
}

PhaseStartEntry::PhaseStartEntry(const string& phaseName, int totalItems) :
	SemanticEntry(Level::Info, fmt::format("Phase started: {}", phaseName)),
	phaseName(phaseName),
	totalItems(totalItems)
{}

string PhaseStartEntry::getPhaseName() const {
	return phaseName;
}

int PhaseStartEntry::getTotalItems() const {
	return totalItems;
}

PhaseEndEntry::PhaseEndEntry(const string& phaseName, double duration) :
	SemanticEntry(Level::Info, fmt::format("Phase completed: {} ({:.2f}s)", phaseName, duration)),
	phaseName(phaseName),
	duration(duration)
{}

string PhaseEndEntry::getPhaseName() const {
	return phaseName;
}

double PhaseEndEntry::getDuration() const {
	return duration;
}

DecoderCreationEntry::DecoderCreationEntry(int decoderNumber, double creationTime) :
	SemanticEntry(Level::Info, fmt::format("Decoder #{} created in {:.2f}s", decoderNumber, creationTime)),
	decoderNumber(decoderNumber),
	creationTime(creationTime)
{}

int DecoderCreationEntry::getDecoderNumber() const {
	return decoderNumber;
}

double DecoderCreationEntry::getCreationTime() const {
	return creationTime;
}

UtteranceEntry::UtteranceEntry(int index, int total, double startTime, double endTime, const string& text) :
	SemanticEntry(Level::Debug, fmt::format("Utterance {}/{} ({:.2f}-{:.2f}s): {}", index, total, startTime, endTime, text)),
	index(index),
	total(total),
	startTime(startTime),
	endTime(endTime),
	text(text)
{}

int UtteranceEntry::getIndex() const {
	return index;
}

int UtteranceEntry::getTotal() const {
	return total;
}

double UtteranceEntry::getStartTime() const {
	return startTime;
}

double UtteranceEntry::getEndTime() const {
	return endTime;
}

string UtteranceEntry::getText() const {
	return text;
}

AudioMetadataEntry::AudioMetadataEntry(double duration, int sampleRate, int channelCount) :
	SemanticEntry(Level::Debug, fmt::format("Audio metadata: {:.2f}s, {}Hz, {} channel(s)", duration, sampleRate, channelCount)),
	duration(duration),
	sampleRate(sampleRate),
	channelCount(channelCount)
{}

double AudioMetadataEntry::getDuration() const {
	return duration;
}

int AudioMetadataEntry::getSampleRate() const {
	return sampleRate;
}

int AudioMetadataEntry::getChannelCount() const {
	return channelCount;
}

VoiceActivityEntry::VoiceActivityEntry(int speechSegments, int silenceSegments) :
	SemanticEntry(Level::Debug, fmt::format("Voice activity segments: {} speech, {} silence", speechSegments, silenceSegments)),
	speechSegments(speechSegments),
	silenceSegments(silenceSegments)
{}

int VoiceActivityEntry::getSpeechSegments() const {
	return speechSegments;
}

int VoiceActivityEntry::getSilenceSegments() const {
	return silenceSegments;
}

UtteranceStartEntry::UtteranceStartEntry(int index, int total, double startTime, double endTime, const string& text) :
	SemanticEntry(Level::Debug, fmt::format("Utterance {}/{} starting ({:.2f}-{:.2f}s): {}", index, total, startTime, endTime, text)),
	index(index),
	total(total),
	startTime(startTime),
	endTime(endTime),
	text(text)
{}

int UtteranceStartEntry::getIndex() const {
	return index;
}

int UtteranceStartEntry::getTotal() const {
	return total;
}

double UtteranceStartEntry::getStartTime() const {
	return startTime;
}

double UtteranceStartEntry::getEndTime() const {
	return endTime;
}

string UtteranceStartEntry::getText() const {
	return text;
}

UtteranceEndEntry::UtteranceEndEntry(int index, int total, double startTime, double endTime, const string& text, double processingDuration) :
	SemanticEntry(Level::Debug, fmt::format("Utterance {}/{} completed ({:.2f}-{:.2f}s, processed in {:.2f}s): {}", index, total, startTime, endTime, processingDuration, text)),
	index(index),
	total(total),
	startTime(startTime),
	endTime(endTime),
	text(text),
	processingDuration(processingDuration)
{}

int UtteranceEndEntry::getIndex() const {
	return index;
}

int UtteranceEndEntry::getTotal() const {
	return total;
}

double UtteranceEndEntry::getStartTime() const {
	return startTime;
}

double UtteranceEndEntry::getEndTime() const {
	return endTime;
}

string UtteranceEndEntry::getText() const {
	return text;
}

double UtteranceEndEntry::getProcessingDuration() const {
	return processingDuration;
}

SubPhaseTimingEntry::SubPhaseTimingEntry(const string& phaseName, const string& subPhaseName, double duration) :
	SemanticEntry(logging::Level::Debug, fmt::format("Sub-phase timing: {}/{} - {:.2f}s", phaseName, subPhaseName, duration)),
	phaseName(phaseName),
	subPhaseName(subPhaseName),
	duration(duration)
{}

string SubPhaseTimingEntry::getPhaseName() const {
	return phaseName;
}

string SubPhaseTimingEntry::getSubPhaseName() const {
	return subPhaseName;
}

double SubPhaseTimingEntry::getDuration() const {
	return duration;
}

VoiceActivityTimelineEntry::VoiceActivityTimelineEntry(
	double totalDuration,
	double speechTime,
	double silenceTime,
	const std::vector<std::pair<double, double>>& speechSegments
) :
	SemanticEntry(Level::Debug, fmt::format("Voice activity timeline: {:.2f}s total, {:.2f}s speech ({:.1f}%), {:.2f}s silence ({:.1f}%)", 
		totalDuration, speechTime, (speechTime/totalDuration)*100, silenceTime, (silenceTime/totalDuration)*100)),
	totalDuration(totalDuration),
	speechTime(speechTime),
	silenceTime(silenceTime),
	speechSegments(speechSegments)
{}

double VoiceActivityTimelineEntry::getTotalDuration() const {
	return totalDuration;
}

double VoiceActivityTimelineEntry::getSpeechTime() const {
	return speechTime;
}

double VoiceActivityTimelineEntry::getSilenceTime() const {
	return silenceTime;
}

double VoiceActivityTimelineEntry::getSpeechPercentage() const {
	return totalDuration > 0 ? (speechTime / totalDuration) * 100 : 0;
}

double VoiceActivityTimelineEntry::getSilencePercentage() const {
	return totalDuration > 0 ? (silenceTime / totalDuration) * 100 : 0;
}

const std::vector<std::pair<double, double>>& VoiceActivityTimelineEntry::getSpeechSegments() const {
	return speechSegments;
}
