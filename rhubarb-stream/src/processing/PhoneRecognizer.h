#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <filesystem>
#include "../core/Phone.h"
#include "../time/Timeline.h"
#include "../time/BoundedTimeline.h"
#include "../time/centiseconds.h"
#include "../tools/tools.h"

extern "C" {
#include <pocketsphinx.h>
#include <pocketsphinx_internal.h>
}

namespace rhubarb_stream {

/**
 * PocketSphinx-based phone recognizer for sentence processing.
 *
 * This is a simplified version of rhubarb's PocketSphinxRecognizer,
 * optimized for sentence-level processing in the streaming pipeline.
 *
 * Key features:
 * - Creates decoder with sentence text as dialog hint (better accuracy)
 * - Thread-local decoder storage for parallel sentence processing
 * - Decoder reuse within same thread (RAI-739 pattern)
 * - Forced alignment using known word sequence
 *
 * Usage:
 *   PhoneRecognizer recognizer;
 *   auto phones = recognizer.recognizePhones(
 *       audioSlice,
 *       "Hello world",  // sentence text as dialog hint
 *       0.0, 1.5        // sentence time range for offset
 *   );
 */
class PhoneRecognizer {
public:
	/**
	 * Create a phone recognizer
	 */
	PhoneRecognizer();
	~PhoneRecognizer();

	// Non-copyable, non-movable
	PhoneRecognizer(const PhoneRecognizer&) = delete;
	PhoneRecognizer& operator=(const PhoneRecognizer&) = delete;

	/**
	 * Recognize phones in an audio segment
	 *
	 * @param audioSlice PCM samples (16-bit, 16kHz, mono)
	 * @param sentenceText The sentence text (used as dialog hint)
	 * @param sliceStartSeconds Start time of the slice relative to full audio
	 * @return Timeline of recognized phones with absolute timestamps
	 */
	BoundedTimeline<Phone> recognizePhones(
		const std::vector<int16_t>& audioSlice,
		const std::string& sentenceText,
		double sliceStartSeconds
	);

	/**
	 * Get the path to sphinx model directory
	 */
	static const std::filesystem::path& getSphinxModelDirectory();

private:
	/**
	 * Decoder entry for thread-local storage
	 */
	struct DecoderEntry {
		lambda_unique_ptr<ps_decoder_t> decoder;
		std::string lastDialogHint;
	};

	/**
	 * Get or create decoder for current thread
	 */
	ps_decoder_t* acquireDecoder(const std::string& dialogHint);

	/**
	 * Create a new decoder with optional dialog hint
	 */
	lambda_unique_ptr<ps_decoder_t> createDecoder(const std::string& dialogHint);

	/**
	 * Recognize words from audio using decoder
	 */
	BoundedTimeline<std::string> recognizeWords(
		const std::vector<int16_t>& audioBuffer,
		ps_decoder_t& decoder
	);

	/**
	 * Align words to phones using forced alignment
	 */
	std::optional<Timeline<Phone>> getPhoneAlignment(
		const std::vector<s3wid_t>& wordIds,
		const std::vector<int16_t>& audioBuffer,
		ps_decoder_t& decoder
	);

	/**
	 * Convert word to phone sequence using dictionary or G2P
	 */
	std::vector<Phone> wordToPhones(const std::string& word);

	/**
	 * Add unknown words to dictionary using G2P
	 */
	void addMissingDictionaryWords(
		const std::vector<std::string>& words,
		ps_decoder_t& decoder
	);

	// Thread-local decoder storage (RAI-739 pattern)
	std::unordered_map<std::thread::id, DecoderEntry> threadDecoders_;
	std::mutex decoderMapMutex_;
};

} // namespace rhubarb_stream
