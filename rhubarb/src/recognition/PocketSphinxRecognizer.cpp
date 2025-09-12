#include "PocketSphinxRecognizer.h"
#include <regex>
#include <gsl_util.h>
#include "audio/AudioSegment.h"
#include "audio/SampleRateConverter.h"
#include "languageModels.h"
#include "tokenization.h"
#include "g2p.h"
#include "time/ContinuousTimeline.h"
#include "audio/processing.h"
#include "time/timedLogging.h"
#include "../rhubarb/semanticEntries.h"
#include <chrono>
#include <format.h>

extern "C" {
#include <state_align_search.h>
}

using std::runtime_error;
using std::invalid_argument;
using std::unique_ptr;
using std::string;
using std::vector;
using std::map;
using std::filesystem::path;
using std::regex;
using std::regex_replace;
using boost::optional;
using std::array;

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
			string pronunciation;
			for (Phone phone : wordToPhones(word)) {
				if (pronunciation.length() > 0) pronunciation += " ";
				pronunciation += PhoneConverter::get().toString(phone);
			}
			missingPronunciations[word] = pronunciation;
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

lambda_unique_ptr<ngram_model_t> createDialogLanguageModel(
	ps_decoder_t& decoder,
	const string& dialog
) {
	// Split dialog into normalized words
	vector<string> words = tokenizeText(
		dialog,
		[&](const string& word) { return dictionaryContains(*decoder.dict, word); }
	);

	// Add dialog-specific words to the dictionary
	addMissingDictionaryWords(words, decoder);

	// Create dialog-specific language model
	words.insert(words.begin(), "<s>");
	words.emplace_back("</s>");
	return createLanguageModel(words, decoder);
}

lambda_unique_ptr<ngram_model_t> createBiasedLanguageModel(
	ps_decoder_t& decoder,
	const string& dialog
) {
	auto defaultLanguageModel = createDefaultLanguageModel(decoder);
	auto dialogLanguageModel = createDialogLanguageModel(decoder, dialog);
	constexpr int modelCount = 2;
	array<ngram_model_t*, modelCount> languageModels {
		defaultLanguageModel.get(),
		dialogLanguageModel.get()
	};
	array<const char*, modelCount> modelNames { "defaultLM", "dialogLM" };
	array<float, modelCount> modelWeights { 0.1f, 0.9f };
	lambda_unique_ptr<ngram_model_t> result(
		ngram_model_set_init(
			nullptr,
			languageModels.data(),
			const_cast<char**>(modelNames.data()),
			modelWeights.data(),
			modelCount
		),
		[](ngram_model_t* lm) { ngram_model_free(lm); });
	if (!result) {
		throw runtime_error("Error creating biased language model.");
	}

	return result;
}

static lambda_unique_ptr<ps_decoder_t> createDecoder(optional<std::string> dialog) {
	lambda_unique_ptr<cmd_ln_t> config(
		cmd_ln_init(
			nullptr, ps_args(), true,
			// Set acoustic model
			"-hmm", (getSphinxModelDirectory() / "acoustic-model").u8string().c_str(),
			// Set pronunciation dictionary
			"-dict", (getSphinxModelDirectory() / "cmudict-en-us.dict").u8string().c_str(),
			// Add noise against zero silence
			// (see http://cmusphinx.sourceforge.net/wiki/faq#qwhy_my_accuracy_is_poor)
			"-dither", "yes",
			// Disable VAD -- we're doing that ourselves
			"-remove_silence", "no",
			// Perform per-utterance cepstral mean normalization
			"-cmn", "batch",
			nullptr),
		[](cmd_ln_t* config) { cmd_ln_free_r(config); });
	if (!config) throw runtime_error("Error creating configuration.");

	lambda_unique_ptr<ps_decoder_t> decoder(
		ps_init(config.get()),
		[](ps_decoder_t* recognizer) { ps_free(recognizer); });
	if (!decoder) throw runtime_error("Error creating speech decoder.");

	// Set language model
	lambda_unique_ptr<ngram_model_t> languageModel(dialog
		? createBiasedLanguageModel(*decoder, *dialog)
		: createDefaultLanguageModel(*decoder));
	ps_set_lm(decoder.get(), "lm", languageModel.get());
	ps_set_search(decoder.get(), "lm");

	return decoder;
}

optional<Timeline<Phone>> getPhoneAlignment(
	const vector<s3wid_t>& wordIds,
	const vector<int16_t>& audioBuffer,
	ps_decoder_t& decoder,
	int utteranceIndex)
{
	if (wordIds.empty()) return boost::none;

	// Create alignment list
	auto alignmentSetupStart = std::chrono::steady_clock::now();
	lambda_unique_ptr<ps_alignment_t> alignment(
		ps_alignment_init(decoder.d2p),
		[](ps_alignment_t* alignment) { ps_alignment_free(alignment); });
	if (!alignment) throw runtime_error("Error creating alignment.");
	for (s3wid_t wordId : wordIds) {
		// Add word. Initial value for duration is ignored.
		ps_alignment_add_word(alignment.get(), wordId, 0);
	}
	int error = ps_alignment_populate(alignment.get());
	if (error) throw runtime_error("Error populating alignment struct.");
	auto alignmentSetupEnd = std::chrono::steady_clock::now();
	double alignmentSetupDuration = std::chrono::duration<double>(alignmentSetupEnd - alignmentSetupStart).count();
	if (utteranceIndex >= 0 && alignmentSetupDuration > 0.0001) {
		logging::log(UtteranceSubStepEntry(utteranceIndex, "Phone Alignment: Alignment Setup", alignmentSetupDuration));
	}

	// Create search structure
	auto searchInitStart = std::chrono::steady_clock::now();
	acmod_t* acousticModel = decoder.acmod;
	lambda_unique_ptr<ps_search_t> search(
		state_align_search_init("state_align", decoder.config, acousticModel, alignment.get()),
		[](ps_search_t* search) { ps_search_free(search); });
	if (!search) throw runtime_error("Error creating search.");
	auto searchInitEnd = std::chrono::steady_clock::now();
	double searchInitDuration = std::chrono::duration<double>(searchInitEnd - searchInitStart).count();
	if (utteranceIndex >= 0 && searchInitDuration > 0.0001) {
		logging::log(UtteranceSubStepEntry(utteranceIndex, "Phone Alignment: Search Init", searchInitDuration));
	}

	// Start recognition
	auto acmodStartStart = std::chrono::steady_clock::now();
	error = acmod_start_utt(acousticModel);
	if (error) throw runtime_error("Error starting utterance processing for alignment.");
	auto acmodStartEnd = std::chrono::steady_clock::now();
	double acmodStartDuration = std::chrono::duration<double>(acmodStartEnd - acmodStartStart).count();
	if (utteranceIndex >= 0 && acmodStartDuration > 0.0001) {
		logging::log(UtteranceSubStepEntry(utteranceIndex, "Phone Alignment: Acoustic Model Start", acmodStartDuration));
	}

	{
		// Eventually end recognition
		auto endRecognition = gsl::finally([&]() { acmod_end_utt(acousticModel); });

		// Start search
		ps_search_start(search.get());

		// Process entire audio clip
		auto featureExtractionStart = std::chrono::steady_clock::now();
		const int16* nextSample = audioBuffer.data();
		size_t remainingSamples = audioBuffer.size();
		const bool fullUtterance = true;
		while (acmod_process_raw(acousticModel, &nextSample, &remainingSamples, fullUtterance) > 0) {
			while (acousticModel->n_feat_frame > 0) {
				ps_search_step(search.get(), acousticModel->output_frame);
				acmod_advance(acousticModel);
			}
		}
		auto featureExtractionEnd = std::chrono::steady_clock::now();
		double featureExtractionDuration = std::chrono::duration<double>(featureExtractionEnd - featureExtractionStart).count();
		if (utteranceIndex >= 0) {
			logging::log(UtteranceSubStepEntry(utteranceIndex, "Phone Alignment: Feature Extraction", featureExtractionDuration));
		}

		// End search
		auto searchFinishStart = std::chrono::steady_clock::now();
		error = ps_search_finish(search.get());
		if (error) return boost::none;
		auto searchFinishEnd = std::chrono::steady_clock::now();
		double searchFinishDuration = std::chrono::duration<double>(searchFinishEnd - searchFinishStart).count();
		if (utteranceIndex >= 0 && searchFinishDuration > 0.0001) {
			logging::log(UtteranceSubStepEntry(utteranceIndex, "Phone Alignment: Search Finish", searchFinishDuration));
		}
	}

	// Extract phones with timestamps
	auto phoneExtractionStart = std::chrono::steady_clock::now();
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

		if (phoneName == "SIL") continue;

		// Add entry
		centiseconds start(phoneEntry->start);
		centiseconds duration(phoneEntry->duration);
		Phone phone = PhoneConverter::get().parse(phoneName);
		if (phone == Phone::AH && duration < 6_cs) {
			// Heuristic: < 6_cs is schwa. PocketSphinx doesn't differentiate.
			phone = Phone::Schwa;
		}
		const Timed<Phone> timedPhone(start, start + duration, phone);
		result.set(timedPhone);
	}
	auto phoneExtractionEnd = std::chrono::steady_clock::now();
	double phoneExtractionDuration = std::chrono::duration<double>(phoneExtractionEnd - phoneExtractionStart).count();
	if (utteranceIndex >= 0 && phoneExtractionDuration > 0.0001) {
		logging::log(UtteranceSubStepEntry(utteranceIndex, "Phone Alignment: Phone Extraction", phoneExtractionDuration));
	}
	
	return result;
}

// Some words have multiple pronunciations, one of which results in better animation than the others.
// This function returns the optimal pronunciation for a select set of these words.
string fixPronunciation(const string& word) {
	const static map<string, string> replacements {
		{ "into(2)", "into" },
		{ "to(2)", "to" },
		{ "to(3)", "to" },
		{ "today(2)", "today" },
		{ "tomorrow(2)", "tomorrow" },
		{ "tonight(2)", "tonight" }
	};

	const auto pair = replacements.find(word);
	return pair != replacements.end() ? pair->second : word;
}

static UtteranceResult utteranceToPhones(
	const AudioClip& audioClip,
	TimeRange utteranceTimeRange,
	ps_decoder_t& decoder,
	ProgressSink& utteranceProgressSink,
	int utteranceIndex
) {
	
	ProgressMerger utteranceProgressMerger(utteranceProgressSink);
	ProgressSink& wordRecognitionProgressSink =
		utteranceProgressMerger.addSource("word recognition (PocketSphinx recognizer)", 1.0);
	ProgressSink& alignmentProgressSink =
		utteranceProgressMerger.addSource("alignment (PocketSphinx recognizer)", 0.5);

	// Start timing audio preparation
	auto audioPrepStart = std::chrono::steady_clock::now();
	
	// Pad time range to give PocketSphinx some breathing room
	TimeRange paddedTimeRange = utteranceTimeRange;
	const centiseconds padding(3);
	paddedTimeRange.grow(padding);
	paddedTimeRange.trim(audioClip.getTruncatedRange());

	const unique_ptr<AudioClip> clipSegment = audioClip.clone()
		| segment(paddedTimeRange)
		| resample(sphinxSampleRate);
	const auto audioBuffer = copyTo16bitBuffer(*clipSegment);
	
	auto audioPrepEnd = std::chrono::steady_clock::now();
	double audioPrepDuration = std::chrono::duration<double>(audioPrepEnd - audioPrepStart).count();
	logging::log(UtteranceSubStepEntry(utteranceIndex, "Audio Preparation", audioPrepDuration));

	// Get words
	auto wordRecognitionStart = std::chrono::steady_clock::now();
	BoundedTimeline<string> words = recognizeWords(audioBuffer, decoder, utteranceIndex);
	auto wordRecognitionEnd = std::chrono::steady_clock::now();
	double wordRecognitionDuration = std::chrono::duration<double>(wordRecognitionEnd - wordRecognitionStart).count();
	// Don't log parent "Word Recognition" since we log detailed sub-steps
	// logging::log(UtteranceSubStepEntry(utteranceIndex, "Word Recognition", wordRecognitionDuration));
	wordRecognitionProgressSink.reportProgress(1.0);

	// Log utterance text
	auto textExtractionStart = std::chrono::steady_clock::now();
	string text;
	for (auto& timedWord : words) {
		string word = timedWord.getValue();
		// Skip details
		if (word == "<s>" || word == "</s>" || word == "<sil>") {
			continue;
		}
		word = regex_replace(word, regex("\\(\\d\\)"), "");
		if (!text.empty()) {
			text += " ";
		}
		text += word;
	}
	auto textExtractionEnd = std::chrono::steady_clock::now();
	double textExtractionDuration = std::chrono::duration<double>(textExtractionEnd - textExtractionStart).count();
	logging::log(UtteranceSubStepEntry(utteranceIndex, "Text Extraction", textExtractionDuration));
	logTimedEvent("utterance", utteranceTimeRange, text);

	// Log words
	auto loggingStart = std::chrono::steady_clock::now();
	for (Timed<string> timedWord : words) {
		timedWord.getTimeRange().shift(paddedTimeRange.getStart());
		logTimedEvent("word", timedWord);
	}
	auto loggingMid1 = std::chrono::steady_clock::now();

	// Convert word strings to word IDs using dictionary
	vector<s3wid_t> wordIds;
	for (const auto& timedWord : words) {
		const string fixedWord = fixPronunciation(timedWord.getValue());
		wordIds.push_back(getWordId(fixedWord, *decoder.dict));
	}

	// Align the words' phones with speech
	auto alignmentStart = std::chrono::steady_clock::now();
#if BOOST_VERSION < 105600 // Support legacy syntax
#define value_or get_value_or
#endif
	Timeline<Phone> utterancePhones = getPhoneAlignment(wordIds, audioBuffer, decoder, utteranceIndex)
		.value_or(ContinuousTimeline<Phone>(clipSegment->getTruncatedRange(), Phone::Noise));
	auto alignmentEnd = std::chrono::steady_clock::now();
	double alignmentDuration = std::chrono::duration<double>(alignmentEnd - alignmentStart).count();
	// Don't log parent "Phone Alignment" since we log detailed sub-steps
	// logging::log(UtteranceSubStepEntry(utteranceIndex, "Phone Alignment", alignmentDuration));
	alignmentProgressSink.reportProgress(1.0);
	utterancePhones.shift(paddedTimeRange.getStart());

	// Log raw phones
	auto loggingMid2 = std::chrono::steady_clock::now();
	for (const auto& timedPhone : utterancePhones) {
		logTimedEvent("rawPhone", timedPhone);
	}
	auto loggingMid3 = std::chrono::steady_clock::now();

	// Guess positions of noise sounds
	auto noiseDetectionStart = std::chrono::steady_clock::now();
	JoiningTimeline<void> noiseSounds = getNoiseSounds(utteranceTimeRange, utterancePhones);
	for (const auto& noiseSound : noiseSounds) {
		utterancePhones.set(noiseSound.getTimeRange(), Phone::Noise);
	}
	auto noiseDetectionEnd = std::chrono::steady_clock::now();
	double noiseDetectionDuration = std::chrono::duration<double>(noiseDetectionEnd - noiseDetectionStart).count();
	logging::log(UtteranceSubStepEntry(utteranceIndex, "Noise Detection", noiseDetectionDuration));

	// Log phones
	for (const auto& timedPhone : utterancePhones) {
		logTimedEvent("phone", timedPhone);
	}
	auto loggingEnd = std::chrono::steady_clock::now();
	
	// Calculate total logging time
	double wordLoggingTime = std::chrono::duration<double>(loggingMid1 - loggingStart).count();
	double rawPhoneLoggingTime = std::chrono::duration<double>(loggingMid3 - loggingMid2).count();
	double phoneLoggingTime = std::chrono::duration<double>(loggingEnd - noiseDetectionEnd).count();
	double totalLoggingTime = wordLoggingTime + rawPhoneLoggingTime + phoneLoggingTime;
	
	// Log the logging overhead
	if (totalLoggingTime > 0.001) { // Only log if significant
		std::string loggingDetails = fmt::format("words: {:.3f}s, raw phones: {:.3f}s, phones: {:.3f}s",
			wordLoggingTime, rawPhoneLoggingTime, phoneLoggingTime);
		logging::log(UtteranceSubStepEntry(utteranceIndex, "Debug Logging", totalLoggingTime, loggingDetails));
	}

	return UtteranceResult{ utterancePhones, text };
}

BoundedTimeline<Phone> PocketSphinxRecognizer::recognizePhones(
	const AudioClip& inputAudioClip,
	optional<std::string> dialog,
	int maxThreadCount,
	ProgressSink& progressSink
) const {
	return ::recognizePhones(
		inputAudioClip, dialog, &createDecoder, &utteranceToPhones, maxThreadCount, progressSink);
}
