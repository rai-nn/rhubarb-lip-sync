#pragma once

#include "core/Phone.h"
#include "time/BoundedTimeline.h"
#include <string>

// Result from speech recognition containing both phones and word boundaries
struct RecognitionResult {
	BoundedTimeline<Phone> phones;
	BoundedTimeline<std::string> words;
	
	RecognitionResult() = default;
	RecognitionResult(
		BoundedTimeline<Phone> phones,
		BoundedTimeline<std::string> words = BoundedTimeline<std::string>()
	) : phones(std::move(phones)), words(std::move(words)) {}
};