#include "PhoneRecognizer.h"
#include <format.h>
#include <stdexcept>
#include <regex>
#include <map>
#include <fstream>
#include <sstream>
#include <cmath>

extern "C" {
#include <sphinxbase/err.h>
#include <pocketsphinx_internal.h>
#include <ngram_search.h>
#include <state_align_search.h>
}

namespace rhubarb_stream {

namespace {

constexpr int SPHINX_SAMPLE_RATE = 16000;

// Redirect PocketSphinx output to prevent console spam
void redirectPocketSphinxOutput() {
	static bool redirected = false;
	if (redirected) return;
	err_set_logfp(nullptr);
	redirected = true;
}

// Get sphinx model directory (relative to binary or working directory)
std::filesystem::path findSphinxModelDirectory() {
	// Try common locations
	std::vector<std::filesystem::path> candidates = {
		// Build directory structure
		"build/res/sphinx",
		"res/sphinx",
		"../res/sphinx",
		"../../res/sphinx",
		"../build/res/sphinx"
	};

	// Check for either flat structure (acoustic-model/) or nested (pocketsphinx-rev13216/)
	for (const auto& candidate : candidates) {
		// Check flat structure (what we expect)
		if (std::filesystem::exists(candidate / "acoustic-model")) {
			return std::filesystem::canonical(candidate);
		}
		// Check nested structure (what CMake creates)
		if (std::filesystem::exists(candidate / "pocketsphinx-rev13216" / "model" / "en-us" / "cmudict-en-us.dict")) {
			return std::filesystem::canonical(candidate);
		}
	}

	throw std::runtime_error("Cannot find sphinx model directory");
}

// Check if word exists in dictionary
bool dictionaryContains(dict_t& dictionary, const std::string& word) {
	return dict_wordid(&dictionary, word.c_str()) != BAD_S3WID;
}

// Get word ID from dictionary
s3wid_t getWordId(const std::string& word, dict_t& dictionary) {
	const s3wid_t wordId = dict_wordid(&dictionary, word.c_str());
	if (wordId == BAD_S3WID) {
		throw std::invalid_argument(fmt::format("Unknown word '{}'.", word));
	}
	return wordId;
}

// Simple tokenization: split on whitespace and punctuation
std::vector<std::string> tokenizeText(const std::string& text) {
	std::vector<std::string> words;
	std::regex wordRegex("[a-zA-Z']+");
	auto begin = std::sregex_iterator(text.begin(), text.end(), wordRegex);
	auto end = std::sregex_iterator();

	for (auto it = begin; it != end; ++it) {
		std::string word = it->str();
		// Convert to uppercase (CMU dictionary uses uppercase)
		std::transform(word.begin(), word.end(), word.begin(), ::toupper);
		if (!word.empty()) {
			words.push_back(word);
		}
	}

	return words;
}

// Create language model from word sequence (simplified)
lambda_unique_ptr<ngram_model_t> createLanguageModelFromWords(
	const std::vector<std::string>& words,
	ps_decoder_t& decoder
) {
	// Create a simple unigram model
	std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "rhubarb_stream_lm.arpa";
	std::ofstream file(tempPath);

	file << "\\data\\" << std::endl;
	file << "ngram 1=" << (words.size() + 2) << std::endl << std::endl; // +2 for <s> and </s>

	file << "\\1-grams:" << std::endl;
	file << std::fixed << std::setprecision(4);

	// Add sentence markers
	file << "-0.3010 <s>" << std::endl;
	file << "-0.3010 </s>" << std::endl;

	// Add words with equal probability
	double prob = -std::log10(static_cast<double>(words.size()));
	for (const auto& word : words) {
		file << prob << " " << word << std::endl;
	}

	file << std::endl << "\\end\\" << std::endl;
	file.close();

	auto lm = lambda_unique_ptr<ngram_model_t>(
		ngram_model_read(decoder.config, tempPath.string().c_str(), NGRAM_ARPA, decoder.lmath),
		[](ngram_model_t* lm) { ngram_model_free(lm); });

	std::filesystem::remove(tempPath);

	return lm;
}

// Fix known pronunciation issues
std::string fixPronunciation(const std::string& word) {
	static const std::map<std::string, std::string> replacements {
		{ "INTO(2)", "INTO" },
		{ "TO(2)", "TO" },
		{ "TO(3)", "TO" },
		{ "TODAY(2)", "TODAY" },
		{ "TOMORROW(2)", "TOMORROW" },
		{ "TONIGHT(2)", "TONIGHT" }
	};

	auto it = replacements.find(word);
	return it != replacements.end() ? it->second : word;
}

} // anonymous namespace

// Static member
const std::filesystem::path& PhoneRecognizer::getSphinxModelDirectory() {
	static std::filesystem::path dir = findSphinxModelDirectory();
	return dir;
}

PhoneRecognizer::PhoneRecognizer() {
	redirectPocketSphinxOutput();
}

PhoneRecognizer::~PhoneRecognizer() = default;

lambda_unique_ptr<ps_decoder_t> PhoneRecognizer::createDecoder(const std::string& dialogHint) {
	const auto& sphinxDir = getSphinxModelDirectory();

	// Determine paths based on directory structure
	std::filesystem::path acousticModelPath;
	std::filesystem::path dictPath;
	std::filesystem::path lmPath;

	// Check for nested structure (CMake copy creates this)
	auto nestedEnUs = sphinxDir / "pocketsphinx-rev13216" / "model" / "en-us";
	auto nestedAcoustic = sphinxDir / "acoustic-model" / "cmusphinx-en-us-5.2";

	if (std::filesystem::exists(nestedEnUs / "cmudict-en-us.dict")) {
		// Nested structure
		acousticModelPath = nestedAcoustic;
		dictPath = nestedEnUs / "cmudict-en-us.dict";
		lmPath = nestedEnUs / "en-us.lm.bin";
	} else {
		// Flat structure (expected by original rhubarb)
		acousticModelPath = sphinxDir / "acoustic-model";
		dictPath = sphinxDir / "cmudict-en-us.dict";
		lmPath = sphinxDir / "en-us.lm.bin";
	}

	lambda_unique_ptr<cmd_ln_t> config(
		cmd_ln_init(
			nullptr, ps_args(), true,
			// Set acoustic model
			"-hmm", acousticModelPath.string().c_str(),
			// Set pronunciation dictionary
			"-dict", dictPath.string().c_str(),
			// Add noise against zero silence
			"-dither", "yes",
			// Disable VAD -- we handle segmentation ourselves
			"-remove_silence", "no",
			// Per-utterance cepstral mean normalization
			"-cmn", "batch",
			// Beam search parameters for precision
			"-beam", "1e-40",
			"-pbeam", "1e-40",
			"-wbeam", "1e-20",
			"-maxhmmpf", "50000",
			"-maxwpf", "60",
			"-lw", "6.5",
			"-lpbeam", "1e-30",
			"-lponlybeam", "1e-30",
			nullptr),
		[](cmd_ln_t* config) { cmd_ln_free_r(config); });

	if (!config) {
		throw std::runtime_error("Error creating PocketSphinx configuration.");
	}

	lambda_unique_ptr<ps_decoder_t> decoder(
		ps_init(config.get()),
		[](ps_decoder_t* recognizer) { ps_free(recognizer); });

	if (!decoder) {
		throw std::runtime_error("Error creating speech decoder.");
	}

	// Set language model based on dialog hint
	if (!dialogHint.empty()) {
		auto words = tokenizeText(dialogHint);
		if (!words.empty()) {
			// Add missing words to dictionary
			addMissingDictionaryWords(words, *decoder);

			// Create dialog-based language model
			words.insert(words.begin(), "<S>");
			words.push_back("</S>");
			auto lm = createLanguageModelFromWords(words, *decoder);
			if (lm) {
				ps_set_lm(decoder.get(), "dialog", lm.get());
				ps_set_search(decoder.get(), "dialog");
			}
		}
	}

	// Fall back to default language model if no dialog or LM creation failed
	if (!ps_get_search(decoder.get())) {
		auto defaultLm = lambda_unique_ptr<ngram_model_t>(
			ngram_model_read(decoder->config, lmPath.string().c_str(), NGRAM_AUTO, decoder->lmath),
			[](ngram_model_t* lm) { ngram_model_free(lm); });
		if (defaultLm) {
			ps_set_lm(decoder.get(), "default", defaultLm.get());
			ps_set_search(decoder.get(), "default");
		}
	}

	return decoder;
}

ps_decoder_t* PhoneRecognizer::acquireDecoder(const std::string& dialogHint) {
	std::thread::id threadId = std::this_thread::get_id();

	// Check if thread already has a decoder
	{
		std::lock_guard<std::mutex> lock(decoderMapMutex_);
		auto it = threadDecoders_.find(threadId);
		if (it != threadDecoders_.end()) {
			// If dialog hint matches, reuse decoder
			if (it->second.lastDialogHint == dialogHint) {
				return it->second.decoder.get();
			}
			// Otherwise, we need a new decoder (dialog changed)
		}
	}

	// Create new decoder OUTSIDE mutex (parallel creation!)
	auto newDecoder = createDecoder(dialogHint);

	// Store in map (brief mutex hold)
	{
		std::lock_guard<std::mutex> lock(decoderMapMutex_);
		DecoderEntry entry;
		entry.decoder = std::move(newDecoder);
		entry.lastDialogHint = dialogHint;
		auto result = threadDecoders_.insert_or_assign(threadId, std::move(entry));
		return result.first->second.decoder.get();
	}
}

void PhoneRecognizer::addMissingDictionaryWords(
	const std::vector<std::string>& words,
	ps_decoder_t& decoder
) {
	std::map<std::string, std::string> missingPronunciations;

	for (const std::string& word : words) {
		if (!dictionaryContains(*decoder.dict, word)) {
			// Use simple G2P fallback
			auto phones = wordToPhones(word);
			std::string pronunciation;
			for (Phone phone : phones) {
				if (!pronunciation.empty()) pronunciation += " ";
				pronunciation += PhoneConverter::get().toString(phone);
			}
			missingPronunciations[word] = pronunciation;
		}
	}

	for (auto it = missingPronunciations.begin(); it != missingPronunciations.end(); ++it) {
		const bool isLast = it == --missingPronunciations.end();
		ps_add_word(&decoder, it->first.c_str(), it->second.c_str(), isLast);
	}
}

std::vector<Phone> PhoneRecognizer::wordToPhones(const std::string& word) {
	// Very simple G2P fallback for unknown words
	// Maps common letter patterns to phones
	std::vector<Phone> phones;

	std::string lower = word;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

	for (size_t i = 0; i < lower.length(); ++i) {
		char c = lower[i];
		switch (c) {
			case 'a': phones.push_back(Phone::AE); break;
			case 'e': phones.push_back(Phone::EH); break;
			case 'i': phones.push_back(Phone::IH); break;
			case 'o': phones.push_back(Phone::AO); break;
			case 'u': phones.push_back(Phone::UH); break;
			case 'b': phones.push_back(Phone::B); break;
			case 'c': phones.push_back(Phone::K); break;
			case 'd': phones.push_back(Phone::D); break;
			case 'f': phones.push_back(Phone::F); break;
			case 'g': phones.push_back(Phone::G); break;
			case 'h': phones.push_back(Phone::HH); break;
			case 'j': phones.push_back(Phone::JH); break;
			case 'k': phones.push_back(Phone::K); break;
			case 'l': phones.push_back(Phone::L); break;
			case 'm': phones.push_back(Phone::M); break;
			case 'n': phones.push_back(Phone::N); break;
			case 'p': phones.push_back(Phone::P); break;
			case 'q': phones.push_back(Phone::K); break;
			case 'r': phones.push_back(Phone::R); break;
			case 's': phones.push_back(Phone::S); break;
			case 't': phones.push_back(Phone::T); break;
			case 'v': phones.push_back(Phone::V); break;
			case 'w': phones.push_back(Phone::W); break;
			case 'x': phones.push_back(Phone::K); phones.push_back(Phone::S); break;
			case 'y': phones.push_back(Phone::Y); break;
			case 'z': phones.push_back(Phone::Z); break;
			default: break;
		}
	}

	if (phones.empty()) {
		phones.push_back(Phone::Noise);
	}

	return phones;
}

BoundedTimeline<std::string> PhoneRecognizer::recognizeWords(
	const std::vector<int16_t>& audioBuffer,
	ps_decoder_t& decoder
) {
	// Restart timing
	ps_start_stream(&decoder);

	// Start recognition
	int error = ps_start_utt(&decoder);
	if (error) {
		throw std::runtime_error("Error starting utterance processing.");
	}

	// Process audio
	const bool noRecognition = false;
	const bool fullUtterance = true;
	int searchedFrameCount = ps_process_raw(
		&decoder, audioBuffer.data(), audioBuffer.size(), noRecognition, fullUtterance);

	if (searchedFrameCount < 0) {
		throw std::runtime_error("Error analyzing raw audio data.");
	}

	// End recognition
	error = ps_end_utt(&decoder);
	if (error) {
		throw std::runtime_error("Error ending utterance processing.");
	}

	BoundedTimeline<std::string> result(
		TimeRange(0_cs, centiseconds(100 * audioBuffer.size() / SPHINX_SAMPLE_RATE))
	);

	// Check if any words were recognized
	const bool phonetic = cmd_ln_boolean_r(decoder.config, "-allphone_ci");
	if (!phonetic) {
		ngram_search_t* ngSearch = reinterpret_cast<ngram_search_t*>(decoder.search);
		if (ngSearch && ngSearch->bpidx == 0) {
			return result; // No words recognized
		}
	}

	// Collect words
	for (ps_seg_t* it = ps_seg_iter(&decoder); it; it = ps_seg_next(it)) {
		const char* word = ps_seg_word(it);
		int firstFrame, lastFrame;
		ps_seg_frames(it, &firstFrame, &lastFrame);
		result.set(centiseconds(firstFrame), centiseconds(lastFrame + 1), word);
	}

	return result;
}

std::optional<Timeline<Phone>> PhoneRecognizer::getPhoneAlignment(
	const std::vector<s3wid_t>& wordIds,
	const std::vector<int16_t>& audioBuffer,
	ps_decoder_t& decoder
) {
	if (wordIds.empty()) return std::nullopt;

	// Create alignment list
	lambda_unique_ptr<ps_alignment_t> alignment(
		ps_alignment_init(decoder.d2p),
		[](ps_alignment_t* alignment) { ps_alignment_free(alignment); });

	if (!alignment) {
		throw std::runtime_error("Error creating alignment.");
	}

	for (s3wid_t wordId : wordIds) {
		ps_alignment_add_word(alignment.get(), wordId, 0);
	}

	int error = ps_alignment_populate(alignment.get());
	if (error) {
		throw std::runtime_error("Error populating alignment struct.");
	}

	// Create search structure
	acmod_t* acousticModel = decoder.acmod;
	lambda_unique_ptr<ps_search_t> search(
		state_align_search_init("state_align", decoder.config, acousticModel, alignment.get()),
		[](ps_search_t* search) { ps_search_free(search); });

	if (!search) {
		throw std::runtime_error("Error creating search.");
	}

	// Start recognition
	error = acmod_start_utt(acousticModel);
	if (error) {
		throw std::runtime_error("Error starting utterance for alignment.");
	}

	// Process audio
	ps_search_start(search.get());

	const int16_t* nextSample = audioBuffer.data();
	size_t remainingSamples = audioBuffer.size();
	const bool fullUtterance = true;

	while (acmod_process_raw(acousticModel, &nextSample, &remainingSamples, fullUtterance) > 0) {
		while (acousticModel->n_feat_frame > 0) {
			ps_search_step(search.get(), acousticModel->output_frame);
			acmod_advance(acousticModel);
		}
	}

	// End recognition
	error = ps_search_finish(search.get());
	acmod_end_utt(acousticModel);

	if (error) return std::nullopt;

	// Extract phones with timestamps
	char** phoneNames = decoder.dict->mdef->ciname;
	Timeline<Phone> result;

	for (ps_alignment_iter_t* it = ps_alignment_phones(alignment.get());
		 it;
		 it = ps_alignment_iter_next(it))
	{
		ps_alignment_entry_t* phoneEntry = ps_alignment_iter_get(it);
		const s3cipid_t phoneId = phoneEntry->id.pid.cipid;
		std::string phoneName = phoneNames[phoneId];

		if (phoneName == "SIL") continue;

		centiseconds start(phoneEntry->start);
		centiseconds duration(phoneEntry->duration);
		Phone phone = PhoneConverter::get().parse(phoneName);

		// Heuristic: short AH is likely schwa
		if (phone == Phone::AH && duration < 6_cs) {
			phone = Phone::Schwa;
		}

		result.set(Timed<Phone>(start, start + duration, phone));
	}

	return result;
}

BoundedTimeline<Phone> PhoneRecognizer::recognizePhones(
	const std::vector<int16_t>& audioSlice,
	const std::string& sentenceText,
	double sliceStartSeconds
) {
	if (audioSlice.empty()) {
		return BoundedTimeline<Phone>(TimeRange(0_cs, 0_cs));
	}

	// Get or create decoder for this thread
	ps_decoder_t* decoder = acquireDecoder(sentenceText);

	// Recognize words
	BoundedTimeline<std::string> words = recognizeWords(audioSlice, *decoder);

	// Convert words to word IDs
	std::vector<s3wid_t> wordIds;
	for (const auto& timedWord : words) {
		std::string fixed = fixPronunciation(timedWord.getValue());
		// Skip sentence markers
		if (fixed == "<S>" || fixed == "</S>" || fixed == "<SIL>") continue;
		try {
			wordIds.push_back(getWordId(fixed, *decoder->dict));
		} catch (...) {
			// Skip unknown words
		}
	}

	// Align phones
	auto phoneAlignment = getPhoneAlignment(wordIds, audioSlice, *decoder);

	// Calculate time range
	double sliceDuration = static_cast<double>(audioSlice.size()) / SPHINX_SAMPLE_RATE;
	centiseconds offsetCs(static_cast<int>(sliceStartSeconds * 100));
	TimeRange sliceRange(0_cs, centiseconds(static_cast<int>(sliceDuration * 100)));

	BoundedTimeline<Phone> result(sliceRange);

	if (phoneAlignment) {
		// Shift phones to absolute timeline position
		for (const auto& timedPhone : *phoneAlignment) {
			Timed<Phone> shifted(
				timedPhone.getStart() + offsetCs,
				timedPhone.getEnd() + offsetCs,
				timedPhone.getValue()
			);
			result.set(shifted);
		}
	} else {
		// Fallback: treat entire slice as noise
		result.set(TimeRange(offsetCs, offsetCs + centiseconds(static_cast<int>(sliceDuration * 100))), Phone::Noise);
	}

	return result;
}

} // namespace rhubarb_stream
