#include <iostream>
#include <format.h>
#include <tclap/CmdLine.h>
#include "core/appInfo.h"
#include "tools/NiceCmdLineOutput.h"
#include "logging/logging.h"
#include "logging/sinks.h"
#include "logging/formatters.h"
#include <gsl_util.h>
#include "exporters/Exporter.h"
#include "time/ContinuousTimeline.h"
#include "tools/stringTools.h"
#include <boost/range/adaptor/transformed.hpp>
#include <fstream>
#include "tools/parallel.h"
#include "tools/exceptions.h"
#include "tools/textFiles.h"
#include "lib/rhubarbLib.h"
#include "ExportFormat.h"
#include "exporters/DatExporter.h"
#include "exporters/TsvExporter.h"
#include "exporters/XmlExporter.h"
#include "exporters/JsonExporter.h"
#include "exporters/VoiceActivityExporter.h"
#include "animation/targetShapeSet.h"
#include "audio/voiceActivityDetection.h"
#include "audio/audioFileReading.h"
#include <boost/utility/in_place_factory.hpp>
#include "tools/platformTools.h"
#include "sinks/MachineReadableStderrSink.h"
#include "sinks/NiceStderrSink.h"
#include "sinks/QuietStderrSink.h"
#include "semanticEntries.h"
#include "RecognizerType.h"
#include "recognition/PocketSphinxRecognizer.h"
#include "recognition/PhoneticRecognizer.h"
#include "recognition/WordTimingRecognizer.h"
#include "PauseDetectionConfig.h"

using std::exception;
using std::string;
using std::string;
using std::vector;
using std::unique_ptr;
using std::make_unique;
using std::shared_ptr;
using std::make_shared;
using std::filesystem::path;
using std::filesystem::u8path;
using boost::adaptors::transformed;
using boost::optional;

namespace tclap = TCLAP;

// Tell TCLAP how to handle our types
namespace TCLAP {
	template<>
	struct ArgTraits<logging::Level> {
		typedef ValueLike ValueCategory;
	};

	template<>
	struct ArgTraits<ExportFormat> {
		typedef ValueLike ValueCategory;
	};

	template<>
	struct ArgTraits<RecognizerType> {
		typedef ValueLike ValueCategory;
	};
}

shared_ptr<logging::Sink> createFileSink(const path& path, logging::Level minLevel) {
	auto file = make_shared<std::ofstream>();
	file->exceptions(std::ifstream::failbit | std::ifstream::badbit);
	file->open(path);
	auto FileSink =
		make_shared<logging::StreamSink>(file, make_shared<logging::SimpleFileFormatter>());
	return make_shared<logging::LevelFilter>(FileSink, minLevel);
}

unique_ptr<Recognizer> createRecognizer(
	RecognizerType recognizerType,
	const optional<string>& characterTimingPath = optional<string>(),
	const optional<string>& text = optional<string>(),
	const optional<string>& audioPath = optional<string>()
) {
	switch (recognizerType) {
		case RecognizerType::PocketSphinx:
			return make_unique<PocketSphinxRecognizer>();
		case RecognizerType::Phonetic:
			return make_unique<PhoneticRecognizer>();
		case RecognizerType::WordTiming: {
			if (!characterTimingPath || !text || !audioPath) {
				throw std::runtime_error("WordTiming recognizer requires character timing file, text, and audio file");
			}
			// Parse character timing and group into words
			auto characterTimings = rhubarb::parseCharacterTimingJson(*characterTimingPath);
			auto wordTimings = rhubarb::groupCharactersIntoWords(*text, characterTimings);
			return make_unique<WordTimingRecognizer>(wordTimings, *audioPath, characterTimings);
		}
		default:
			throw std::runtime_error("Unknown recognizer.");
	}
}

unique_ptr<Exporter> createExporter(
	ExportFormat exportFormat,
	const ShapeSet& targetShapeSet,
	double datFrameRate,
	bool datUsePrestonBlair
) {
	switch (exportFormat) {
		case ExportFormat::Dat:
			return make_unique<DatExporter>(targetShapeSet, datFrameRate, datUsePrestonBlair);
		case ExportFormat::Tsv:
			return make_unique<TsvExporter>();
		case ExportFormat::Xml:
			return make_unique<XmlExporter>();
		case ExportFormat::Json:
			return make_unique<JsonExporter>();
		default:
			throw std::runtime_error("Unknown export format.");
	}
}

ShapeSet getTargetShapeSet(const string& extendedShapesString) {
	// All basic shapes are mandatory
	ShapeSet result(ShapeConverter::get().getBasicShapes());

	// Add any extended shapes
	for (char ch : extendedShapesString) {
		Shape shape = ShapeConverter::get().parse(string(1, ch));
		result.insert(shape);
	}
	return result;
}

int main(int platformArgc, char* platformArgv[]) {
	// Set up default logging so early errors are printed to stdout
	const logging::Level defaultMinStderrLevel = logging::Level::Error;
	shared_ptr<logging::Sink> defaultSink = make_shared<NiceStderrSink>(defaultMinStderrLevel);
	logging::addSink(defaultSink);

	// Make sure the console uses UTF-8 on all platforms including Windows
	useUtf8ForConsole();

	// Convert command-line arguments to UTF-8
	const vector<string> args = argsToUtf8(platformArgc, platformArgv);

	// Define command-line parameters
	const char argumentValueSeparator = ' ';
	tclap::CmdLine cmd(appName, argumentValueSeparator, appVersion);
	cmd.setExceptionHandling(false);
	cmd.setOutput(new NiceCmdLineOutput());

	tclap::ValueArg<string> outputFileName(
		"o", "output", "The output file path.",
		false, string(), "string", cmd
	);

	auto logLevels = vector<logging::Level>(logging::LevelConverter::get().getValues());
	tclap::ValuesConstraint<logging::Level> logLevelConstraint(logLevels);
	tclap::ValueArg<logging::Level> logLevel(
		"", "logLevel", "The minimum log level that will be written to the log file",
		false, logging::Level::Debug, &logLevelConstraint, cmd
	);

	tclap::ValueArg<string> logFileName(
		"", "logFile", "The log file path.",
		false, string(), "string", cmd
	);
	tclap::ValueArg<logging::Level> consoleLevel(
		"", "consoleLevel", "The minimum log level that will be printed on the console (stderr)",
		false, defaultMinStderrLevel, &logLevelConstraint, cmd
	);

	tclap::SwitchArg machineReadableMode(
		"", "machineReadable", "Formats all output to stderr in a structured JSON format.",
		cmd, false
	);

	tclap::SwitchArg quietMode(
		"q", "quiet", "Suppresses all output to stderr except for warnings and error messages.",
		cmd, false
	);

	tclap::ValueArg<int> maxThreadCount(
		"", "threads", "The maximum number of worker threads to use.",
		false, getProcessorCoreCount(), "number", cmd
	);

	tclap::ValueArg<string> extendedShapes(
		"", "extendedShapes", "All extended, optional shapes to use.",
		false, "GHX", "string", cmd
	);

	tclap::ValueArg<string> dialogFile(
		"d", "dialogFile", "A file containing the text of the dialog.",
		false, string(), "string", cmd
	);

	tclap::SwitchArg datUsePrestonBlair(
		"", "datUsePrestonBlair", "Only for dat exporter: uses the Preston Blair mouth shape names.",
		cmd, false
	);

	tclap::ValueArg<double> datFrameRate(
		"", "datFrameRate", "Only for dat exporter: the desired frame rate.",
		false, 24.0, "number", cmd
	);

	auto exportFormats = vector<ExportFormat>(ExportFormatConverter::get().getValues());
	tclap::ValuesConstraint<ExportFormat> exportFormatConstraint(exportFormats);
	tclap::ValueArg<ExportFormat> exportFormat(
		"f", "exportFormat", "The export format.",
		false, ExportFormat::Tsv, &exportFormatConstraint, cmd
	);

	auto recognizerTypes = vector<RecognizerType>(RecognizerTypeConverter::get().getValues());
	tclap::ValuesConstraint<RecognizerType> recognizerConstraint(recognizerTypes);
	tclap::ValueArg<RecognizerType> recognizerType(
		"r", "recognizer", "The dialog recognizer.",
		false, RecognizerType::PocketSphinx, &recognizerConstraint, cmd
	);

	// Pause detection configuration arguments
	vector<string> allowedSpeechSpeeds = {"slow", "normal", "fast"};
	tclap::ValuesConstraint<string> speechSpeedConstraint(allowedSpeechSpeeds);
	tclap::ValueArg<string> speechSpeed(
		"", "speechSpeed",
		"Preset for speech speed detection: slow, normal, or fast. "
		"slow: Conservative pause detection for slow speech. "
		"normal: Balanced pause detection (default). "
		"fast: Aggressive pause detection for fast speech.",
		false, "normal", &speechSpeedConstraint, cmd
	);

	tclap::ValueArg<int> vadMaxGap(
		"", "vadMaxGap",
		"Maximum gap to fill in voice activity detection (milliseconds). "
		"Lower values preserve more pauses in fast speech. Default: 60ms",
		false, 60, "number", cmd
	);

	tclap::ValueArg<int> vadMinSegment(
		"", "vadMinSegment",
		"Minimum segment length to keep (milliseconds). "
		"Lower values preserve shorter speech segments. Default: 30ms",
		false, 30, "number", cmd
	);
	
	tclap::ValueArg<int> vadAggressiveness(
		"", "vadAggressiveness",
		"WebRTC VAD aggressiveness level (0-3). "
		"0=Quality (least aggressive), 1=Low bitrate, 2=Aggressive (default), 3=Very aggressive. "
		"Higher values detect less as speech.",
		false, 2, "number", cmd
	);

	tclap::ValueArg<int> microPauseThreshold(
		"", "microPauseThreshold",
		"Threshold for micro-pause detection (milliseconds). "
		"Pauses shorter than this create subtle mouth relaxation. Default: 60ms",
		false, 60, "number", cmd
	);

	tclap::SwitchArg noTweening(
		"", "noTweening",
		"Disable tweening between mouth shapes. "
		"By default, Rhubarb inserts transition shapes for smoother animation. "
		"Use this flag to disable that behavior.",
		cmd, false
	);

	tclap::SwitchArg skipTimingOptimization(
		"", "skipTimingOptimization",
		"Skip the timing optimization step that consolidates short visemes. "
		"This preserves all original phoneme-to-viseme mappings, even very short ones. "
		"Useful when your animation system handles timing and interpolation.",
		cmd, false
	);

	tclap::ValueArg<int> maxVisemesPerWord(
		"", "maxVisemesPerWord",
		"Maximum number of visemes per word. "
		"Reduces animation complexity by keeping only the most prominent mouth shapes per word. "
		"Recommended: 2 for simple animation, 3-4 for more detail. "
		"0 = disabled (default).",
		false, 0, "number", cmd
	);

	tclap::ValueArg<string> characterTimingFile(
		"", "characterTiming",
		"JSON file with character-level timing data from TTS (uses word boundaries with forced alignment)",
		false, string(), "string", cmd
	);

	tclap::ValueArg<string> textFile(
		"", "text",
		"Text file containing the transcript (required with --characterTiming)",
		false, string(), "string", cmd
	);

	tclap::UnlabeledValueArg<string> inputFileName(
		"inputFile", "The input file. Must be a sound file in WAVE format.",
		false, "", "string", cmd
	);

	try {
		// Parse command line
		{
			// TCLAP mutates the function argument! Pass a copy.
			vector<string> argsCopy(args);
			cmd.parse(argsCopy);
		}

		// Set up logging
		// ... to stderr
		if (quietMode.getValue()) {
			logging::addSink(make_shared<QuietStderrSink>(consoleLevel.getValue()));
		} else if (machineReadableMode.getValue()) {
			logging::addSink(make_shared<MachineReadableStderrSink>(consoleLevel.getValue()));
		} else {
			logging::addSink(make_shared<NiceStderrSink>(consoleLevel.getValue()));
		}
		logging::removeSink(defaultSink);
		// ... to log file
		if (logFileName.isSet()) {
			auto fileSink = createFileSink(u8path(logFileName.getValue()), logLevel.getValue());
			logging::addSink(fileSink);
		}

		// Validate and transform command line arguments
		if (maxThreadCount.getValue() < 1) {
			throw std::runtime_error("Thread count must be 1 or higher.");
		}
		
		// Validate character timing arguments
		if (characterTimingFile.isSet()) {
			if (!textFile.isSet() && !dialogFile.isSet()) {
				throw std::runtime_error(
					"When using --characterTiming, you must provide either --text or -d"
				);
			}
			if (!inputFileName.isSet()) {
				throw std::runtime_error(
					"When using --characterTiming, you must still provide an audio file for forced alignment"
				);
			}
		} else if (!inputFileName.isSet()) {
			throw std::runtime_error(
				"Either provide an input audio file or use --characterTiming with --text"
			);
		}
		
		path inputFilePath = inputFileName.isSet() ? u8path(inputFileName.getValue()) : path();
		ShapeSet targetShapeSet = getTargetShapeSet(extendedShapes.getValue());

		// Create and set pause detection configuration
		PauseDetectionConfig pauseConfig = PauseDetectionConfig::forSpeechSpeed(speechSpeed.getValue());
		
		// Override with specific values if provided
		if (vadMaxGap.isSet()) {
			pauseConfig.vadMaxGap = PauseDetectionConfig::millisecondsToCs(vadMaxGap.getValue());
		}
		if (vadMinSegment.isSet()) {
			pauseConfig.vadMinSegmentLength = PauseDetectionConfig::millisecondsToCs(vadMinSegment.getValue());
		}
		if (vadAggressiveness.isSet()) {
			int aggressivenessValue = vadAggressiveness.getValue();
			if (aggressivenessValue < 0 || aggressivenessValue > 3) {
				throw std::runtime_error("VAD aggressiveness must be between 0 and 3");
			}
			pauseConfig.vadAggressiveness = aggressivenessValue;
		}
		if (microPauseThreshold.isSet()) {
			pauseConfig.microPauseThreshold = PauseDetectionConfig::millisecondsToCs(microPauseThreshold.getValue());
		}
		
		// Set the global instance for use throughout the application
		PauseDetectionConfig::setInstance(pauseConfig);

		unique_ptr<Exporter> exporter;
		if (exportFormat.getValue() != ExportFormat::VoiceActivity) {
			exporter = createExporter(
				exportFormat.getValue(),
				targetShapeSet,
				datFrameRate.getValue(),
				datUsePrestonBlair.getValue()
			);
		}

		// Log the input file or character timing mode
		if (characterTimingFile.isSet()) {
			logging::log(StartEntry(u8path(characterTimingFile.getValue())));
		} else {
			logging::log(StartEntry(inputFilePath));
		}
		logging::debugFormat("Command line: {}",
			join(args | transformed([](string arg) { return fmt::format("\"{}\"", arg); }), " "));

		try {
			// On progress change: Create log message
			ProgressForwarder progressSink([](double progress) {
				logging::log(ProgressEntry(progress));
			});

			// Check if we're in voice-detection-only mode
			if (exportFormat.getValue() == ExportFormat::VoiceActivity) {
				// Voice detection only mode
				logging::info("Starting voice activity detection.");
				
				// Load audio file
				const auto audioClip = createAudioFileClip(inputFilePath);
				
				// Perform voice activity detection
				JoiningBoundedTimeline<void> voiceActivity = detectVoiceActivity(*audioClip, progressSink);
				logging::info("Done detecting voice activity.");
				
				// Export voice activity
				optional<std::ofstream> outputFile;
				if (outputFileName.isSet()) {
					outputFile = boost::in_place(u8path(outputFileName.getValue()));
					outputFile->exceptions(std::ifstream::failbit | std::ifstream::badbit);
				}
				
				VoiceActivityExporterInput vadExporterInput(inputFilePath, voiceActivity, pauseConfig);
				VoiceActivityExporter vadExporter;
				logging::info("Starting export.");
				vadExporter.exportVoiceActivity(vadExporterInput, outputFile ? *outputFile : std::cout);
				logging::info("Done exporting.");
			} else {
				// Normal animation mode
				logging::info("Starting animation.");
				
				// Use a function to create the animation to avoid uninitialized timeline
				auto createAnimation = [&]() -> JoiningContinuousTimeline<Shape> {
					if (characterTimingFile.isSet()) {
						// Use WordTiming recognizer with character timing data
						string text = dialogFile.isSet() 
							? readUtf8File(u8path(dialogFile.getValue()))
							: readUtf8File(u8path(textFile.getValue()));
						
						// Create WordTiming recognizer
						auto recognizer = createRecognizer(
							RecognizerType::WordTiming,
							characterTimingFile.getValue(),
							text,
							inputFilePath.u8string()
						);
						
						return animateWaveFile(
							inputFilePath,
							text,
							*recognizer,
							targetShapeSet,
							maxThreadCount.getValue(),
							progressSink,
							noTweening.getValue(),
							skipTimingOptimization.getValue(),
							maxVisemesPerWord.getValue());
					} else {
						// Use traditional audio-based processing
						return animateWaveFile(
							inputFilePath,
							dialogFile.isSet()
								? readUtf8File(u8path(dialogFile.getValue()))
								: boost::optional<string>(),
							*createRecognizer(recognizerType.getValue()),
							targetShapeSet,
							maxThreadCount.getValue(),
							progressSink,
							noTweening.getValue(),
							skipTimingOptimization.getValue(),
							maxVisemesPerWord.getValue());
					}
				};
				
				JoiningContinuousTimeline<Shape> animation = createAnimation();
				logging::info("Done animating.");

				// Export animation
				optional<std::ofstream> outputFile;
				if (outputFileName.isSet()) {
					outputFile = boost::in_place(u8path(outputFileName.getValue()));
					outputFile->exceptions(std::ifstream::failbit | std::ifstream::badbit);
				}
				// Use character timing file path if no audio input, otherwise use audio file path
				path exportInputPath = characterTimingFile.isSet() 
					? u8path(characterTimingFile.getValue()) 
					: inputFilePath;
				ExporterInput exporterInput = ExporterInput(exportInputPath, animation, targetShapeSet);
				logging::info("Starting export.");
				exporter->exportAnimation(exporterInput, outputFile ? *outputFile : std::cout);
				logging::info("Done exporting.");
			}

			logging::log(SuccessEntry());
		} catch (...) {
			std::throw_with_nested(
				std::runtime_error(fmt::format("Error processing file {}.", inputFilePath.u8string()))
			);
		}

		return 0;
	} catch (tclap::ArgException& e) {
		// Error parsing command-line args.
		cmd.getOutput()->failure(cmd, e);
		logging::log(FailureEntry("Invalid command line."));
		return 1;
	} catch (tclap::ExitException&) {
		// A built-in TCLAP command (like --help) has finished. Exit application.
		logging::info("Exiting application after help-like command.");
		return 0;
	} catch (const exception& e) {
		// Generic error
		string message = getMessage(e);
		logging::log(FailureEntry(message));
		return 1;
	}
}
