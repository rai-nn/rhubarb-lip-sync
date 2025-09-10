#pragma once

#include "Exporter.h"
#include "time/BoundedTimeline.h"
#include "rhubarb/PauseDetectionConfig.h"
#include <filesystem>

class VoiceActivityExporterInput {
public:
	VoiceActivityExporterInput(
		const std::filesystem::path& inputFilePath,
		const JoiningBoundedTimeline<void>& voiceActivity,
		const PauseDetectionConfig& vadConfig) :
		inputFilePath(inputFilePath),
		voiceActivity(voiceActivity),
		vadConfig(vadConfig) {}

	std::filesystem::path inputFilePath;
	JoiningBoundedTimeline<void> voiceActivity;
	PauseDetectionConfig vadConfig;
};

class VoiceActivityExporter {
public:
	void exportVoiceActivity(const VoiceActivityExporterInput& input, std::ostream& outputStream);
};