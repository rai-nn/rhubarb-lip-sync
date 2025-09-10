#include "VoiceActivityExporter.h"
#include "exporterTools.h"
#include "tools/stringTools.h"
#include <iomanip>
#include <sstream>

using std::string;

void VoiceActivityExporter::exportVoiceActivity(const VoiceActivityExporterInput& input, std::ostream& outputStream) {
	// Calculate statistics
	const auto range = input.voiceActivity.getRange();
	const double totalDuration = range.getDuration().count() / 100.0; // Convert from centiseconds to seconds
	
	double speechDuration = 0.0;
	std::vector<std::pair<double, double>> segments;
	
	// Create segments list
	if (input.voiceActivity.empty()) {
		// All silence
		segments.push_back({0.0, totalDuration});
	} else {
		double lastEnd = 0.0;
		
		for (const auto& voiceSegment : input.voiceActivity) {
			double segmentStart = voiceSegment.getStart().count() / 100.0;
			double segmentEnd = voiceSegment.getEnd().count() / 100.0;
			
			// Add silence segment if there's a gap
			if (segmentStart > lastEnd) {
				segments.push_back({lastEnd, segmentStart});
			}
			
			// Add speech segment
			segments.push_back({segmentStart, segmentEnd});
			speechDuration += (segmentEnd - segmentStart);
			lastEnd = segmentEnd;
		}
		
		// Add final silence segment if needed
		if (lastEnd < totalDuration) {
			segments.push_back({lastEnd, totalDuration});
		}
	}
	
	double silenceDuration = totalDuration - speechDuration;
	double speechRatio = totalDuration > 0 ? speechDuration / totalDuration : 0.0;
	
	// Export as JSON
	outputStream << "{\n";
	
	// Metadata section
	outputStream << "  \"metadata\": {\n";
	outputStream << "    \"soundFile\": \"" << escapeJsonString(absolute(input.inputFilePath).u8string()) << "\",\n";
	outputStream << "    \"duration\": " << std::fixed << std::setprecision(2) << totalDuration << ",\n";
	outputStream << "    \"vadSettings\": {\n";
	outputStream << "      \"aggressiveness\": 2,\n"; // WebRTC VAD aggressiveness is hardcoded to 2 in the implementation
	outputStream << "      \"maxGap\": " << (input.vadConfig.vadMaxGap.count() * 10) << ",\n"; // Convert cs to ms
	outputStream << "      \"minSegment\": " << (input.vadConfig.vadMinSegmentLength.count() * 10) << "\n"; // Convert cs to ms
	outputStream << "    }\n";
	outputStream << "  },\n";
	
	// Voice segments section
	outputStream << "  \"voiceSegments\": [\n";
	bool isFirst = true;
	bool isSpeech = false;
	
	// Output segments with alternating speech/silence types
	for (const auto& segment : segments) {
		if (!isFirst) outputStream << ",\n";
		isFirst = false;
		
		// Determine if this is a speech or silence segment
		bool isCurrentSegmentSpeech = false;
		for (const auto& voiceSegment : input.voiceActivity) {
			double voiceStart = voiceSegment.getStart().count() / 100.0;
			double voiceEnd = voiceSegment.getEnd().count() / 100.0;
			if (std::abs(segment.first - voiceStart) < 0.001 && std::abs(segment.second - voiceEnd) < 0.001) {
				isCurrentSegmentSpeech = true;
				break;
			}
		}
		
		outputStream << "    {\"start\": " << std::fixed << std::setprecision(2) << segment.first
					 << ", \"end\": " << std::fixed << std::setprecision(2) << segment.second
					 << ", \"type\": \"" << (isCurrentSegmentSpeech ? "speech" : "silence") << "\"}";
	}
	outputStream << "\n";
	outputStream << "  ],\n";
	
	// Statistics section
	outputStream << "  \"statistics\": {\n";
	outputStream << "    \"totalDuration\": " << std::fixed << std::setprecision(2) << totalDuration << ",\n";
	outputStream << "    \"speechDuration\": " << std::fixed << std::setprecision(2) << speechDuration << ",\n";
	outputStream << "    \"silenceDuration\": " << std::fixed << std::setprecision(2) << silenceDuration << ",\n";
	outputStream << "    \"speechRatio\": " << std::fixed << std::setprecision(3) << speechRatio << "\n";
	outputStream << "  }\n";
	
	outputStream << "}\n";
}