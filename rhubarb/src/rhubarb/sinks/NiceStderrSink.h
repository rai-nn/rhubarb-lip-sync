#pragma once

#include "logging/Entry.h"
#include "logging/Sink.h"

// Prints nicely formatted progress to stderr.
// Non-semantic entries are only printed if their log level at least matches the specified minimum level.
class NiceStderrSink : public logging::Sink {
public:
	NiceStderrSink(logging::Level minLevel);
	void receive(const logging::Entry& entry) override;
private:
	logging::Level minLevel;
	std::shared_ptr<Sink> innerSink;
};
