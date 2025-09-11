#include "DetailedStderrSink.h"
#include "semanticEntries.h"
#include "tools/parallel.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <format.h>

using std::string;
using std::chrono::steady_clock;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

DetailedStderrSink::DetailedStderrSink(logging::Level minLevel, bool includeThreadTimeline, bool verbose, int threadLimit, bool threadLimitExplicit) :
	minLevel(minLevel),
	includeThreadTimeline(includeThreadTimeline),
	verbose(verbose),
	startTime(steady_clock::now()),
	machineCpuCount(getProcessorCoreCount()),
	threadLimit(threadLimit),
	threadLimitExplicit(threadLimitExplicit)
{
}

void DetailedStderrSink::receive(const logging::Entry& entry) {
	// Check for our semantic entries
	if (const auto* startEntry = dynamic_cast<const StartEntry*>(&entry)) {
		startPhase(PipelinePhase::AudioLoading, "Processing audio file...");
	}
	else if (const auto* phaseStartEntry = dynamic_cast<const PhaseStartEntry*>(&entry)) {
		// Handle explicit phase starts
		const string& phaseName = phaseStartEntry->getPhaseName();
		if (phaseName == "VoiceActivityDetection") {
			endPhase(currentPhase);
			startPhase(PipelinePhase::VoiceActivityDetection, "Detecting voice activity...");
		} else if (phaseName == "SpeechRecognition") {
			endPhase(currentPhase);
			totalUtterances = phaseStartEntry->getTotalItems();
			completedUtterances = 0;
			startPhase(PipelinePhase::SpeechRecognition, "Recognizing speech...");
		} else if (phaseName == "AnimationGeneration") {
			// Only start animation phase if we've completed speech recognition
			// or if we're coming from a valid previous phase.
			// This prevents the duplicate "Generating animation..." message that happens
			// early during initialization
			if (currentPhase == PipelinePhase::SpeechRecognition || 
			    phaseDurations.count(PipelinePhase::SpeechRecognition) > 0) {
				endPhase(currentPhase);
				startPhase(PipelinePhase::AnimationGeneration, "Generating animation...");
			}
		} else if (phaseName == "Export") {
			endPhase(currentPhase);
			startPhase(PipelinePhase::Export, "Exporting results...");
		}
	}
	else if (const auto* phaseEndEntry = dynamic_cast<const PhaseEndEntry*>(&entry)) {
		// Handle explicit phase ends
		endPhase(currentPhase);
	}
	else if (const auto* utteranceEntry = dynamic_cast<const UtteranceEntry*>(&entry)) {
		// Track utterance processing (old format - still supported)
		PipelineEvent event;
		event.timestamp = steady_clock::now();
		event.threadId = std::this_thread::get_id();
		event.phase = PipelinePhase::SpeechRecognition;
		event.isUtterance = true;
		event.utteranceIndex = utteranceEntry->getIndex();
		event.utteranceStart = utteranceEntry->getStartTime();
		event.utteranceEnd = utteranceEntry->getEndTime();
		event.utteranceText = utteranceEntry->getText();
		event.description = fmt::format("Utterance {}/{}", utteranceEntry->getIndex(), utteranceEntry->getTotal());
		
		completedUtterances = utteranceEntry->getIndex();
		totalUtterances = utteranceEntry->getTotal();
		
		addEvent(event);
	}
	else if (const auto* utteranceStartEntry = dynamic_cast<const UtteranceStartEntry*>(&entry)) {
		// Track utterance processing start
		PipelineEvent event;
		event.timestamp = steady_clock::now();
		event.threadId = std::this_thread::get_id();
		event.phase = PipelinePhase::SpeechRecognition;
		event.isUtterance = true;
		event.isUtteranceStart = true;
		event.utteranceIndex = utteranceStartEntry->getIndex();
		event.utteranceStart = utteranceStartEntry->getStartTime();
		event.utteranceEnd = utteranceStartEntry->getEndTime();
		event.utteranceText = utteranceStartEntry->getText();
		event.description = fmt::format("Utterance {}/{} starting", utteranceStartEntry->getIndex(), utteranceStartEntry->getTotal());
		
		totalUtterances = utteranceStartEntry->getTotal();
		
		addEvent(event);
	}
	else if (const auto* decoderCreationEntry = dynamic_cast<const DecoderCreationEntry*>(&entry)) {
		// Track decoder creation
		PipelineEvent event;
		event.timestamp = steady_clock::now();
		event.threadId = std::this_thread::get_id();
		event.phase = PipelinePhase::SpeechRecognition;
		event.description = fmt::format("Decoder #{} created ({:.2f}s)", 
			decoderCreationEntry->getDecoderNumber(), 
			decoderCreationEntry->getCreationTime());
		
		addEvent(event);
	}
	else if (const auto* utteranceEndEntry = dynamic_cast<const UtteranceEndEntry*>(&entry)) {
		// Track utterance processing end
		PipelineEvent event;
		event.timestamp = steady_clock::now();
		event.threadId = std::this_thread::get_id();
		event.phase = PipelinePhase::SpeechRecognition;
		event.isUtterance = true;
		event.isUtteranceEnd = true;
		event.utteranceIndex = utteranceEndEntry->getIndex();
		event.utteranceStart = utteranceEndEntry->getStartTime();
		event.utteranceEnd = utteranceEndEntry->getEndTime();
		event.utteranceText = utteranceEndEntry->getText();
		event.processingDuration = utteranceEndEntry->getProcessingDuration();
		event.description = fmt::format("Utterance {}/{} completed", utteranceEndEntry->getIndex(), utteranceEndEntry->getTotal());
		
		completedUtterances = utteranceEndEntry->getIndex();
		totalUtterances = utteranceEndEntry->getTotal();
		
		addEvent(event);
	}
	else if (const auto* audioMetadataEntry = dynamic_cast<const AudioMetadataEntry*>(&entry)) {
		// Track audio metadata
		audioDuration = audioMetadataEntry->getDuration();
		sampleRate = audioMetadataEntry->getSampleRate();
	}
	else if (const auto* voiceActivityEntry = dynamic_cast<const VoiceActivityEntry*>(&entry)) {
		// Track voice activity segment statistics
		speechSegmentCount = voiceActivityEntry->getSpeechSegments();
		silenceSegmentCount = voiceActivityEntry->getSilenceSegments();
	}
	else if (const auto* progressEntry = dynamic_cast<const ProgressEntry*>(&entry)) {
		// We'll track progress but not display it during execution
	}
	else if (dynamic_cast<const SuccessEntry*>(&entry)) {
		// Pipeline complete - print the summary
		if (!summaryPrinted) {
			endPhase(currentPhase);
			currentPhase = PipelinePhase::Complete;
			printSummary();
			summaryPrinted = true;
		}
	}
	else {
		// Handle regular log entries
		// Look for specific log messages to track pipeline phases
		const string& message = entry.message;
		
		// Note: utterance text is now provided directly in UtteranceEndEntry,
		// so we no longer need to parse ##utterance messages for text matching
		
		// Process log messages only if they meet the minimum level
		if (entry.level >= minLevel) {
			// Track phase transitions
			// Note: VAD phase timing is now handled by explicit PhaseStartEntry/PhaseEndEntry
			// so we don't need string-based detection for VAD anymore
			if (message.find("Speech recognition using") != string::npos) {
				// Extract thread count (but don't manually manage phases - let PhaseStartEntry/PhaseEndEntry handle it)
				size_t threadPos = message.find("using ");
				if (threadPos != string::npos) {
					threadPos += 6;
					size_t spacePos = message.find(' ', threadPos);
					if (spacePos != string::npos) {
						string threadStr = message.substr(threadPos, spacePos - threadPos);
						maxThreadsUsed = std::stoi(threadStr);
					}
				}
			}
			// Commented out string-based detection since we have proper semantic entries now
			// This prevents duplicate "Generating animation..." messages
			// else if (message.find("Speech recognition using") != string::npos && message.find("end") != string::npos) {
			// 	endPhase(PipelinePhase::SpeechRecognition);
			// 	startPhase(PipelinePhase::AnimationGeneration, "Generating animation...");
			// }
			// else if (message.find("Starting animation") != string::npos) {
			// 	if (currentPhase == PipelinePhase::VoiceActivityDetection) {
			// 		endPhase(PipelinePhase::VoiceActivityDetection);
			// 	}
			// 	startPhase(PipelinePhase::AnimationGeneration, "Generating animation...");
			// }
			else if (message.find("Starting export") != string::npos || message.find("Done exporting") != string::npos) {
				if (currentPhase == PipelinePhase::AnimationGeneration) {
					endPhase(PipelinePhase::AnimationGeneration);
				}
				if (message.find("Starting export") != string::npos) {
					startPhase(PipelinePhase::Export, "Exporting results...");
				} else {
					endPhase(PipelinePhase::Export);
				}
			}
			
			// Track word count
			if (message.find("##word[") == 0 && message.find("<s>") == string::npos && message.find("</s>") == string::npos) {
				totalWords++;
			}
			// Track phoneme count
			else if (message.find("##phone[") == 0 && message.find("Noise") == string::npos) {
				totalPhonemes++;
			}
			// Track shape/viseme count
			else if (message.find("##shape[") == 0) {
				totalVisemes++;
			}
		}
	}
}

void DetailedStderrSink::startPhase(PipelinePhase phase, const string& description) {
	currentPhase = phase;
	currentPhaseStart = steady_clock::now();
	printMinimalProgress(description);
	
	PipelineEvent event;
	event.timestamp = currentPhaseStart;
	event.phase = phase;
	event.description = "Phase started: " + description;
	addEvent(event);
}

void DetailedStderrSink::endPhase(PipelinePhase phase) {
	auto now = steady_clock::now();
	double duration = duration_cast<milliseconds>(now - currentPhaseStart).count() / 1000.0;
	phaseDurations[phase] = duration;
	
	PipelineEvent event;
	event.timestamp = now;
	event.phase = phase;
	event.description = "Phase completed";
	event.metrics["duration"] = duration;
	addEvent(event);
}

void DetailedStderrSink::addEvent(const PipelineEvent& event) {
	std::lock_guard<std::mutex> lock(eventMutex);
	events.push_back(event);
	
	// Track thread IDs
	if (threadIdToNumber.find(event.threadId) == threadIdToNumber.end()) {
		threadIdToNumber[event.threadId] = nextThreadNumber++;
	}
}

void DetailedStderrSink::printMinimalProgress(const string& message) {
	std::cerr << message << std::endl;
}

void DetailedStderrSink::printSummary() {
	std::cerr << "\nDone.\n" << std::endl;
	
	// Calculate total duration
	auto endTime = steady_clock::now();
	double totalDuration = duration_cast<milliseconds>(endTime - startTime).count() / 1000.0;
	
	// Print detailed summary
	std::cerr << "============================================\n";
	std::cerr << "RHUBARB PIPELINE EXECUTION SUMMARY\n";
	std::cerr << "============================================\n\n";
	
	// Phase summaries
	// Always show Phase 1 even if duration is not tracked properly
	if (phaseDurations.count(PipelinePhase::AudioLoading) || audioDuration > 0) {
		std::cerr << "PHASE 1: Entry Point and Initialization\n";
		if (phaseDurations.count(PipelinePhase::AudioLoading)) {
			std::cerr << "└─ Duration: " << formatDuration(phaseDurations[PipelinePhase::AudioLoading]) << "\n";
		} else {
			std::cerr << "└─ Duration: <1ms\n";
		}
		if (audioDuration > 0) {
			std::cerr << "└─ Audio loaded: " << formatDuration(audioDuration);
			if (sampleRate > 0) {
				std::cerr << ", " << sampleRate << "Hz";
			}
			std::cerr << "\n";
		}
		std::cerr << "\n";
	}
	
	if (phaseDurations.count(PipelinePhase::VoiceActivityDetection)) {
		std::cerr << "PHASE 2: Audio Processing (Voice Activity Detection)\n";
		std::cerr << "└─ Duration: " << formatDuration(phaseDurations[PipelinePhase::VoiceActivityDetection]) << "\n";
		if (speechSegmentCount > 0 || silenceSegmentCount > 0) {
			std::cerr << "└─ Speech segments: " << speechSegmentCount << "\n";
			std::cerr << "└─ Silence segments: " << silenceSegmentCount << "\n";
		}
		std::cerr << "\n";
	}
	
	if (phaseDurations.count(PipelinePhase::SpeechRecognition)) {
		std::cerr << "PHASE 3: Speech Recognition";
		if (maxThreadsUsed > 1) {
			std::cerr << " (" << maxThreadsUsed << " threads)";
		}
		std::cerr << "\n";
		std::cerr << "└─ Total Duration: " << formatDuration(phaseDurations[PipelinePhase::SpeechRecognition]) << "\n";
		if (completedUtterances > 0) {
			std::cerr << "└─ Utterances processed: " << completedUtterances << "\n";
		}
		if (totalWords > 0) {
			std::cerr << "└─ Words recognized: " << totalWords << "\n";
		}
		if (totalPhonemes > 0) {
			std::cerr << "└─ Phonemes detected: " << totalPhonemes << "\n";
		}
		
		// Sub-phases breakdown
		std::cerr << "\nSub-phases:\n";
		std::cerr << "└─ 3.1: Decoder Initialization (see timeline below)\n";
		std::cerr << "└─ 3.2: Parallel Utterance Processing (see timeline below)\n";
		std::cerr << "└─ 3.3: Utterance-Level Processing (see timeline below)\n";
		std::cerr << "└─ 3.4: Timeline Assembly (automatic during processing)\n";
		
		// Thread execution timeline (if enabled and multi-threaded)
		if (includeThreadTimeline && maxThreadsUsed > 1) {
			std::cerr << "\nThread execution timeline:\n";
			
			// Collect indices of relevant events
			std::vector<size_t> timelineEventIndices;
			for (size_t i = 0; i < events.size(); ++i) {
				const auto& event = events[i];
				// Include utterance events
				if (event.isUtterance && event.phase == PipelinePhase::SpeechRecognition) {
					// Prefer new format (end events with processing duration)
					if (event.isUtteranceEnd) {
						timelineEventIndices.push_back(i);
					}
					// Fallback to old format for backward compatibility
					else if (!event.isUtteranceStart && !event.isUtteranceEnd) {
						timelineEventIndices.push_back(i);
					}
				}
				// Include decoder creation events
				else if (event.phase == PipelinePhase::SpeechRecognition && 
				         event.description.find("Decoder #") != std::string::npos) {
					timelineEventIndices.push_back(i);
				}
			}
			
			// Sort by event timestamp
			std::sort(timelineEventIndices.begin(), timelineEventIndices.end(), 
				[this](size_t a, size_t b) {
					// For utterances, sort by start time; for others, by timestamp
					if (events[a].isUtterance && events[b].isUtterance) {
						return events[a].utteranceStart < events[b].utteranceStart;
					}
					return events[a].timestamp < events[b].timestamp;
				});
			
			// Display sorted events with thread information
			for (size_t idx : timelineEventIndices) {
				const auto& event = events[idx];
				int threadNum = threadIdToNumber[event.threadId];
				
				// Handle decoder creation events
				if (event.description.find("Decoder #") != std::string::npos) {
					double relativeTime = duration_cast<milliseconds>(event.timestamp - startTime).count() / 1000.0;
					std::cerr << fmt::format("[T{}] {:.2f}s: {}",
						threadNum,
						relativeTime,
						event.description);
					std::cerr << "\n";
				}
				// Handle utterance events
				else if (event.isUtterance) {
					if (event.processingDuration > 0.0) {
						// Show processing duration for new format
						std::cerr << fmt::format("[T{}] {:.2f}s: Utterance {} ({:.2f}-{:.2f}s)",
							threadNum,
							event.processingDuration,
							event.utteranceIndex,
							event.utteranceStart,
							event.utteranceEnd);
					} else {
						// Fallback to old format (completion timestamp)
						double relativeTime = duration_cast<milliseconds>(event.timestamp - startTime).count() / 1000.0;
						std::cerr << fmt::format("[T{}] {:.2f}s: Utterance {} ({:.2f}-{:.2f}s)",
							threadNum,
							relativeTime,
							event.utteranceIndex,
							event.utteranceStart,
							event.utteranceEnd);
					}
					
					if (!event.utteranceText.empty() && event.utteranceText != " ") {
						std::cerr << " \"" << event.utteranceText << "\"";
					}
					std::cerr << "\n";
				}
			}
		}
		std::cerr << "\n";
	}
	
	if (phaseDurations.count(PipelinePhase::AnimationGeneration)) {
		std::cerr << "PHASE 4: Animation Generation\n";
		std::cerr << "└─ Total Duration: " << formatDuration(phaseDurations[PipelinePhase::AnimationGeneration]) << "\n";
		if (totalVisemes > 0) {
			std::cerr << "└─ Visemes generated: " << totalVisemes << "\n";
		}
		std::cerr << "\n";
	}
	
	if (phaseDurations.count(PipelinePhase::Export)) {
		std::cerr << "PHASE 5: Export\n";
		std::cerr << "└─ Duration: " << formatDuration(phaseDurations[PipelinePhase::Export]) << "\n";
		std::cerr << "└─ Format: JSON\n";
		std::cerr << "\n";
	}
	
	// Overall statistics
	std::cerr << "============================================\n";
	std::cerr << "OVERALL STATISTICS\n";
	std::cerr << "============================================\n";
	std::cerr << "Total processing time: " << formatDuration(totalDuration) << "\n";
	
	if (audioDuration > 0) {
		double speed = audioDuration / totalDuration;
		std::cerr << "Audio duration: " << formatDuration(audioDuration) << "\n";
		std::cerr << fmt::format("Processing speed: {:.2f}x realtime\n", speed);
	}
	
	if (totalVisemes > 0) {
		std::cerr << "Total visemes: " << totalVisemes << "\n";
		if (audioDuration > 0) {
			double rate = totalVisemes / audioDuration;
			std::cerr << fmt::format("Viseme rate: {:.1f} per second\n", rate);
		}
	}
	
	// Enhanced thread information
	std::cerr << "Machine CPUs: " << machineCpuCount << "\n";
	if (threadLimitExplicit) {
		std::cerr << "Thread limit: " << threadLimit << "\n";
	} else {
		std::cerr << "Thread limit: default\n";
	}
	std::cerr << "Threads utilized: " << maxThreadsUsed << "\n";
	
	std::cerr << "============================================\n";
}

string DetailedStderrSink::phaseToString(PipelinePhase phase) const {
	switch (phase) {
		case PipelinePhase::Initialization: return "Initialization";
		case PipelinePhase::AudioLoading: return "Audio Loading";
		case PipelinePhase::VoiceActivityDetection: return "Voice Activity Detection";
		case PipelinePhase::SpeechRecognition: return "Speech Recognition";
		case PipelinePhase::PhonemeMapping: return "Phoneme Mapping";
		case PipelinePhase::AnimationGeneration: return "Animation Generation";
		case PipelinePhase::Export: return "Export";
		case PipelinePhase::Complete: return "Complete";
		default: return "Unknown";
	}
}

string DetailedStderrSink::formatDuration(double seconds) const {
	if (seconds < 1.0) {
		return fmt::format("{:.0f}ms", seconds * 1000);
	} else {
		return fmt::format("{:.2f}s", seconds);
	}
}

string DetailedStderrSink::formatTimestamp(const steady_clock::time_point& tp) const {
	double seconds = duration_cast<milliseconds>(tp - startTime).count() / 1000.0;
	return fmt::format("{:.3f}s", seconds);
}