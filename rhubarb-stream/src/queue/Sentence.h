#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json.hpp>

namespace rhubarb_stream {

/**
 * A single word with timing information
 *
 * From ElevenLabs committed_transcript_with_timestamps:
 *   {"text": "Hello", "start": 0.079, "end": 0.399, "type": "word"}
 */
struct Word {
	std::string text;   // Word text (may include punctuation)
	double start;       // Start time in seconds
	double end;         // End time in seconds

	Word() : start(0.0), end(0.0) {}
	Word(std::string text, double start, double end)
		: text(std::move(text)), start(start), end(end) {}

	// Duration in seconds
	double duration() const { return end - start; }
};

/**
 * A sentence with word-level timing information
 *
 * Sentences are the unit of parallel processing in rhubarb-stream.
 * Each sentence is processed by a worker thread independently.
 *
 * JSON format (SENTENCE frame payload):
 *   {
 *     "text": "Hello world.",
 *     "start": 0.079,
 *     "end": 1.234,
 *     "words": [
 *       {"text": "Hello", "start": 0.079, "end": 0.399},
 *       {"text": "world.", "start": 0.599, "end": 1.234}
 *     ]
 *   }
 */
struct Sentence {
	std::string text;       // Full sentence text
	double start;           // Sentence start time (first word start)
	double end;             // Sentence end time (last word end)
	std::vector<Word> words; // Individual words with timings
	size_t index;           // Sentence index (0-based, for ordering output)

	Sentence() : start(0.0), end(0.0), index(0) {}

	// Duration in seconds
	double duration() const { return end - start; }

	// Word count
	size_t wordCount() const { return words.size(); }

	/**
	 * Parse a Sentence from JSON string
	 *
	 * @param json JSON string from SENTENCE frame payload
	 * @param sentenceIndex Index of this sentence (for ordering)
	 * @return Parsed Sentence, or nullopt if parsing fails
	 */
	static std::optional<Sentence> fromJson(const std::string& json, size_t sentenceIndex);

	/**
	 * Parse a Sentence from JSON object
	 */
	static std::optional<Sentence> fromJson(const nlohmann::json& j, size_t sentenceIndex);
};

/**
 * Parse a Word from JSON object
 */
inline std::optional<Word> parseWord(const nlohmann::json& j) {
	try {
		Word word;
		word.text = j.value("text", "");
		word.start = j.value("start", 0.0);
		word.end = j.value("end", 0.0);

		// Validate: end must be >= start
		if (word.end < word.start) {
			return std::nullopt;
		}

		return word;
	} catch (...) {
		return std::nullopt;
	}
}

inline std::optional<Sentence> Sentence::fromJson(const std::string& json, size_t sentenceIndex) {
	try {
		auto j = nlohmann::json::parse(json);
		return fromJson(j, sentenceIndex);
	} catch (const nlohmann::json::parse_error&) {
		return std::nullopt;
	}
}

inline std::optional<Sentence> Sentence::fromJson(const nlohmann::json& j, size_t sentenceIndex) {
	try {
		Sentence sentence;
		sentence.index = sentenceIndex;
		sentence.text = j.value("text", "");
		sentence.start = j.value("start", 0.0);
		sentence.end = j.value("end", 0.0);

		// Parse words array if present
		if (j.contains("words") && j["words"].is_array()) {
			for (const auto& wordJson : j["words"]) {
				auto word = parseWord(wordJson);
				if (word) {
					sentence.words.push_back(std::move(*word));
				}
			}
		}

		// Validate: end must be >= start
		if (sentence.end < sentence.start) {
			return std::nullopt;
		}

		// If no explicit start/end but we have words, derive from words
		if (sentence.start == 0.0 && sentence.end == 0.0 && !sentence.words.empty()) {
			sentence.start = sentence.words.front().start;
			sentence.end = sentence.words.back().end;
		}

		return sentence;
	} catch (...) {
		return std::nullopt;
	}
}

} // namespace rhubarb_stream
