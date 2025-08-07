#pragma once

#include "time/centiseconds.h"
#include <string>
#include <memory>

// Configuration for pause detection and voice activity detection
struct PauseDetectionConfig {
	// Voice Activity Detection parameters
	centiseconds vadMaxGap = 6_cs;           // Maximum gap to fill between voice segments
	centiseconds vadMinSegmentLength = 3_cs; // Minimum segment length to keep
	
	// Pause animation thresholds
	centiseconds microPauseThreshold = 6_cs;     // Threshold for micro-pauses
	centiseconds veryShortPauseThreshold = 10_cs; // Threshold for very short pauses
	centiseconds shortPauseThreshold = 25_cs;     // Threshold for short pauses
	
	// Sentence boundary detection
	centiseconds sentenceBoundaryThreshold = 10_cs; // Max duration for sentence boundary detection
	centiseconds microPauseDuration = 3_cs;         // Duration of inserted micro-pauses
	
	// Factory method for preset configurations
	static PauseDetectionConfig forSpeechSpeed(const std::string& speed) {
		PauseDetectionConfig config;
		
		if (speed == "slow") {
			// Conservative settings for slow speech (original values)
			config.vadMaxGap = 10_cs;
			config.vadMinSegmentLength = 5_cs;
			config.microPauseThreshold = 12_cs;
			config.veryShortPauseThreshold = 12_cs;
			config.shortPauseThreshold = 35_cs;
			config.sentenceBoundaryThreshold = 15_cs;
			config.microPauseDuration = 5_cs;
		} else if (speed == "fast") {
			// Aggressive settings for fast speech
			config.vadMaxGap = 4_cs;
			config.vadMinSegmentLength = 2_cs;
			config.microPauseThreshold = 4_cs;
			config.veryShortPauseThreshold = 8_cs;
			config.shortPauseThreshold = 20_cs;
			config.sentenceBoundaryThreshold = 8_cs;
			config.microPauseDuration = 2_cs;
		}
		// else use default "normal" values
		
		return config;
	}
	
	// Convert milliseconds to centiseconds for CLI input
	static centiseconds millisecondsToCs(int ms) {
		return centiseconds(ms / 10);
	}
	
	// Global instance for access across the codebase
	static PauseDetectionConfig& getInstance() {
		static PauseDetectionConfig instance;
		return instance;
	}
	
	// Set the global instance
	static void setInstance(const PauseDetectionConfig& config) {
		getInstance() = config;
	}
};