#pragma once

#include "Recognizer.h"
#include "../lib/characterTiming.h"
#include <vector>
#include <string>

class WordTimingRecognizer : public Recognizer {
public:
	WordTimingRecognizer(
		const std::vector<rhubarb::WordTiming>& wordTimings,
		const std::string& audioPath,
		const std::vector<rhubarb::CharacterTiming>& allCharacterTimings
	);
	
	BoundedTimeline<Phone> recognizePhones(
		const AudioClip& audioClip,
		boost::optional<std::string> dialog,
		int maxThreadCount,
		ProgressSink& progressSink
	) const override;

private:
	std::vector<rhubarb::WordTiming> wordTimings_;
	std::string audioPath_;
	std::vector<rhubarb::CharacterTiming> allCharacterTimings_;
	
	// Convert space characters to silence in the phone timeline
	void convertSpacesToSilence(Timeline<Phone>& phoneTimeline) const;
};