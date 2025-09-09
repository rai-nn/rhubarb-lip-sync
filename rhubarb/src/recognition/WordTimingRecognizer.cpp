#include "WordTimingRecognizer.h"
#include "pocketSphinxTools.h"
#include "time/ContinuousTimeline.h"
#include "audio/processing.h"
#include "audio/SampleRateConverter.h"
#include "languageModels.h"
#include "g2p.h"
#include "logging/logging.h"
#include <regex>
#include <gsl_util.h>
#include <algorithm>

extern "C" {
#include <state_align_search.h>
#include <pocketsphinx_internal.h>
}

using std::string;
using std::vector;
using std::unique_ptr;
using std::map;
using boost::optional;
using std::runtime_error;
using std::invalid_argument;
using std::filesystem::path;

// Helper functions in anonymous namespace to avoid symbol conflicts
namespace {
bool dictionaryContains(dict_t& dictionary, const string& word) {
	return dict_wordid(&dictionary, word.c_str()) != BAD_S3WID;
}

s3wid_t getWordId(const string& word, dict_t& dictionary) {
	const s3wid_t wordId = dict_wordid(&dictionary, word.c_str());
	if (wordId == BAD_S3WID) throw invalid_argument(fmt::format("Unknown word '{}'.", word));
	return wordId;
}

void addMissingDictionaryWords(const vector<string>& words, ps_decoder_t& decoder) {
	map<string, string> missingPronunciations;
	for (const string& word : words) {
		if (!dictionaryContains(*decoder.dict, word)) {
			// Convert to lowercase for g2p processing
			string lowerWord = word;
			std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
			
			// Handle compound words with hyphens by splitting them
			vector<string> wordParts;
			size_t start = 0;
			size_t pos = lowerWord.find('-');
			while (pos != string::npos) {
				if (pos > start) {
					wordParts.push_back(lowerWord.substr(start, pos - start));
				}
				start = pos + 1;
				pos = lowerWord.find('-', start);
			}
			if (start < lowerWord.length()) {
				wordParts.push_back(lowerWord.substr(start));
			}
			
			// If no hyphens found, treat as single word
			if (wordParts.empty()) {
				wordParts.push_back(lowerWord);
			}
			
			string pronunciation;
			for (const string& part : wordParts) {
				// Skip empty parts or parts with non-alphabetic characters
				if (part.empty() || !std::all_of(part.begin(), part.end(), 
					[](char c) { return isalpha(c) || c == '\''; })) {
					continue;
				}
				
				try {
					for (Phone phone : wordToPhones(part)) {
						if (pronunciation.length() > 0) pronunciation += " ";
						pronunciation += PhoneConverter::get().toString(phone);
					}
				} catch (const std::exception& e) {
					// If g2p fails for this part, skip the whole word
					logging::infoFormat("Skipping word '{}' - could not generate pronunciation for part '{}': {}", word, part, e.what());
					pronunciation.clear();
					break;
				}
			}
			
			if (!pronunciation.empty()) {
				missingPronunciations[word] = pronunciation;
			}
		}
	}
	for (auto it = missingPronunciations.begin(); it != missingPronunciations.end(); ++it) {
		const bool isLast = it == --missingPronunciations.end();
		logging::infoFormat("Unknown word '{}'. Guessing pronunciation '{}'.", it->first, it->second);
		ps_add_word(&decoder, it->first.c_str(), it->second.c_str(), isLast);
	}
}

lambda_unique_ptr<ngram_model_t> createDefaultLanguageModel(ps_decoder_t& decoder) {
	path modelPath = getSphinxModelDirectory() / "en-us.lm.bin";
	lambda_unique_ptr<ngram_model_t> result(
		ngram_model_read(decoder.config, modelPath.u8string().c_str(), NGRAM_AUTO, decoder.lmath),
		[](ngram_model_t* lm) { ngram_model_free(lm); });
	if (!result) {
		throw runtime_error(fmt::format("Error reading language model from {}.", modelPath.u8string()));
	}
	return result;
}

lambda_unique_ptr<ps_decoder_t> createDecoder(optional<string> dialog) {
	lambda_unique_ptr<cmd_ln_t> config(
		cmd_ln_init(
			nullptr, ps_args(), true,
			"-hmm", (getSphinxModelDirectory() / "acoustic-model").u8string().c_str(),
			"-dict", (getSphinxModelDirectory() / "cmudict-en-us.dict").u8string().c_str(),
			"-dither", "yes",
			"-remove_silence", "no",
			"-cmn", "batch",
			nullptr),
		[](cmd_ln_t* config) { cmd_ln_free_r(config); });
	if (!config) throw runtime_error("Error creating configuration.");

	lambda_unique_ptr<ps_decoder_t> decoder(
		ps_init(config.get()),
		[](ps_decoder_t* recognizer) { ps_free(recognizer); });
	if (!decoder) throw runtime_error("Error creating speech decoder.");

	// Set language model
	lambda_unique_ptr<ngram_model_t> languageModel = createDefaultLanguageModel(*decoder);
	ps_set_lm(decoder.get(), "lm", languageModel.get());
	ps_set_search(decoder.get(), "lm");

	return decoder;
}

optional<Timeline<Phone>> getPhoneAlignment(
	const vector<s3wid_t>& wordIds,
	const vector<int16_t>& audioBuffer,
	ps_decoder_t& decoder)
{
	if (wordIds.empty()) return boost::none;

	// Create alignment list
	lambda_unique_ptr<ps_alignment_t> alignment(
		ps_alignment_init(decoder.d2p),
		[](ps_alignment_t* alignment) { ps_alignment_free(alignment); });
	if (!alignment) throw runtime_error("Error creating alignment.");
	for (s3wid_t wordId : wordIds) {
		ps_alignment_add_word(alignment.get(), wordId, 0);
	}
	int error = ps_alignment_populate(alignment.get());
	if (error) throw runtime_error("Error populating alignment struct.");

	// Create search structure
	acmod_t* acousticModel = decoder.acmod;
	lambda_unique_ptr<ps_search_t> search(
		state_align_search_init("state_align", decoder.config, acousticModel, alignment.get()),
		[](ps_search_t* search) { ps_search_free(search); });
	if (!search) throw runtime_error("Error creating search.");

	// Start recognition
	error = acmod_start_utt(acousticModel);
	if (error) throw runtime_error("Error starting utterance processing for alignment.");

	{
		// Eventually end recognition
		auto endRecognition = gsl::finally([&]() { acmod_end_utt(acousticModel); });

		// Start search
		ps_search_start(search.get());

		// Process entire audio clip
		const int16* nextSample = audioBuffer.data();
		size_t remainingSamples = audioBuffer.size();
		const bool fullUtterance = true;
		while (acmod_process_raw(acousticModel, &nextSample, &remainingSamples, fullUtterance) > 0) {
			while (acousticModel->n_feat_frame > 0) {
				ps_search_step(search.get(), acousticModel->output_frame);
				acmod_advance(acousticModel);
			}
		}

		// End search
		error = ps_search_finish(search.get());
		if (error) return boost::none;
	}

	// Extract phones with timestamps
	char** phoneNames = decoder.dict->mdef->ciname;
	Timeline<Phone> result;
	for (
		ps_alignment_iter_t* it = ps_alignment_phones(alignment.get());
		it;
		it = ps_alignment_iter_next(it)
	) {
		// Get phone
		ps_alignment_entry_t* phoneEntry = ps_alignment_iter_get(it);
		const s3cipid_t phoneId = phoneEntry->id.pid.cipid;
		string phoneName = phoneNames[phoneId];

		// Handle silence phones
		Phone phone;
		if (phoneName == "SIL") {
			phone = Phone::Noise;  // Convert silence to noise (becomes X viseme)
		} else {
			phone = PhoneConverter::get().parse(phoneName);
		}

		// Add entry
		centiseconds start(phoneEntry->start);
		centiseconds duration(phoneEntry->duration);
		if (phone == Phone::AH && duration < 6_cs) {
			// Heuristic: < 6_cs is schwa. PocketSphinx doesn't differentiate.
			phone = Phone::Schwa;
		}
		const Timed<Phone> timedPhone(start, start + duration, phone);
		result.set(timedPhone);
	}
	return result;
}

string fixPronunciation(const string& word) {
	// Remove pronunciation indices like "(2)" from word
	return std::regex_replace(word, std::regex("\\(\\d+\\)"), "");
}

} // end anonymous namespace

WordTimingRecognizer::WordTimingRecognizer(
	const vector<rhubarb::WordTiming>& wordTimings,
	const string& audioPath,
	const vector<rhubarb::CharacterTiming>& allCharacterTimings
) : wordTimings_(wordTimings), audioPath_(audioPath), allCharacterTimings_(allCharacterTimings) {
	logging::infoFormat("WordTimingRecognizer initialized with {} words and {} character timings", 
		wordTimings_.size(), allCharacterTimings_.size());
	
	// Direct debug output to ensure we see it
	fprintf(stderr, "DEBUG: WordTimingRecognizer created with %zu character timings\n", allCharacterTimings_.size());
}

BoundedTimeline<Phone> WordTimingRecognizer::recognizePhones(
	const AudioClip& audioClip,
	optional<string> dialog,
	int maxThreadCount,
	ProgressSink& progressSink
) const {
	// Initialize progress tracking
	progressSink.reportProgress(0.0);
	
	try {
		// Create decoder (this sets up dictionary and acoustic models)
		auto decoder = createDecoder(dialog);
		
		// Prepare audio in PocketSphinx format
		const unique_ptr<AudioClip> resampledAudio = audioClip.clone() | resample(sphinxSampleRate);
		const auto audioBuffer = copyTo16bitBuffer(*resampledAudio);
		
		progressSink.reportProgress(0.1);
		
		// Extract word strings from timing data and prepare for dictionary lookup
		vector<string> wordStrings;
		for (const auto& wordTiming : wordTimings_) {
			if (!wordTiming.word.empty()) {
				wordStrings.push_back(wordTiming.word);
			}
		}
		
		// Add any missing words to dictionary using g2p
		addMissingDictionaryWords(wordStrings, *decoder);
		
		progressSink.reportProgress(0.2);
		
		// Convert words to word IDs
		vector<s3wid_t> wordIds;
		for (const string& word : wordStrings) {
			const string cleanWord = fixPronunciation(word);
			try {
				wordIds.push_back(getWordId(cleanWord, *decoder->dict));
			} catch (const invalid_argument& e) {
				// If word still not found, skip it
				continue;
			}
		}
		
		if (wordIds.empty()) {
			throw runtime_error("No valid words found for alignment");
		}
		
		progressSink.reportProgress(0.4);
		
		// Perform forced alignment using PocketSphinx
		optional<Timeline<Phone>> alignment = getPhoneAlignment(wordIds, audioBuffer, *decoder);
		
		progressSink.reportProgress(0.8);
		
		Timeline<Phone> result;
		if (alignment) {
			result = *alignment;
			// Convert space characters to silence (X visemes)
			convertSpacesToSilence(result);
		} else {
			// If alignment failed, create a simple fallback
			result = ContinuousTimeline<Phone>(audioClip.getTruncatedRange(), Phone::Noise);
		}
		
		progressSink.reportProgress(1.0);
		
		// Return as BoundedTimeline
		return BoundedTimeline<Phone>(
			audioClip.getTruncatedRange(),
			result
		);
		
	} catch (const std::exception& e) {
		throw runtime_error("WordTimingRecognizer error: " + string(e.what()));
	}
}

void WordTimingRecognizer::convertSpacesToSilence(Timeline<Phone>& phoneTimeline) const {
	// Check if we have character timings
	if (allCharacterTimings_.empty()) {
		logging::warn("No character timings available for space conversion!");
		return;
	}
	
	// Debug: Count spaces
	int spaceCount = 0;
	int gapsCreated = 0;
	
	logging::infoFormat("Processing {} character timings for space conversion", allCharacterTimings_.size());
	fprintf(stderr, "DEBUG: Converting spaces - have %zu character timings\n", allCharacterTimings_.size());
	
	// Iterate through all character timings to find spaces
	for (const auto& charTiming : allCharacterTimings_) {
		if (charTiming.character == " ") {
			spaceCount++;
			
			// Convert space time range to centiseconds
			centiseconds start(static_cast<int>(charTiming.startTime * 100));
			centiseconds end(static_cast<int>(charTiming.endTime * 100));
			
			// Debug log
			logging::debugFormat("Creating gap at {}-{} for space", start.count(), end.count());
			
			// Clear this time range to create a gap (which becomes Shape::X in animation)
			// The Timeline class will handle creating the gap
			phoneTimeline.clear(start, end);
			gapsCreated++;
		}
	}
	
	logging::infoFormat("Space conversion: {} spaces found, {} gaps created", 
		spaceCount, gapsCreated);
	fprintf(stderr, "DEBUG: Space conversion complete - %d spaces found, %d gaps created\n", spaceCount, gapsCreated);
}