#pragma once

#include <string>

enum class InstrumentState {
    Stopped = 0,
    RawIq = 1,
    BaselineAcquiring = 2,
    BaselineReady = 3,
    ResonanceFinding = 4,
    Diagnostics = 5,
    Searching = 6,
    Tracking = 7,
    Degraded = 8,
    Relocking = 9,
    Error = 10,
};

enum class InstrumentCommand {
    Stop,
    StartRawIq,
    StartBaseline,
    CancelBaseline,
    StartResonanceFinding,
    CompleteResonanceFinding,
    StartDiagnostics,
    CancelDiagnostics,
    CompleteDiagnostics,
    StartTracking,
    TrackingGood,
    TrackingPoor,
    BeginRelock,
    RelockSucceeded,
    RelockFallback,
    StopTracking,
    Fail,
};

struct StateTransitionResult {
    bool accepted = false;
    InstrumentState previous = InstrumentState::Stopped;
    InstrumentState current = InstrumentState::Stopped;
    std::string error;
};

class InstrumentStateMachine {
public:
    void reset();
    InstrumentState state() const;
    bool baselineValid() const;
    const std::string& error() const;
    StateTransitionResult apply(InstrumentCommand command, const std::string& detail = "");

private:
    StateTransitionResult accept(InstrumentState next);
    StateTransitionResult reject(const std::string& reason) const;

    InstrumentState state_ = InstrumentState::Stopped;
    bool baseline_valid_ = false;
    std::string error_;
};

const char* instrumentStateName(InstrumentState state);
