#include "NiceStderrSink.h"
#include "logging/sinks.h"
#include "logging/formatters.h"
#include "semanticEntries.h"
#include <iostream>

using std::make_shared;
using logging::Level;
using logging::StdErrSink;
using logging::SimpleConsoleFormatter;

NiceStderrSink::NiceStderrSink(Level minLevel) :
	minLevel(minLevel),
	innerSink(make_shared<StdErrSink>(make_shared<SimpleConsoleFormatter>()))
{
}

void NiceStderrSink::receive(const logging::Entry& entry) {
	// For selected semantic entries, print a user-friendly message instead of
	// the technical log message.
	if (const auto* startEntry = dynamic_cast<const StartEntry*>(&entry)) {
		std::cerr
			<< fmt::format("Generating lip sync data for {}.", startEntry->getInputFilePath().u8string())
			<< std::endl;
	}
	else if (const auto* progressEntry = dynamic_cast<const ProgressEntry*>(&entry)) {
		// Progress entries are ignored - no progress bar at all
	}
	else if (dynamic_cast<const SuccessEntry*>(&entry)) {
		std::cerr << "Done." << std::endl;
	}
	else {
		// Treat the entry as a normal log message
		if (entry.level >= minLevel) {
			innerSink->receive(entry);
		}
	}
}

