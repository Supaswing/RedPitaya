#include "instrument_state.hpp"

void InstrumentStateMachine::reset()
{
    state_ = InstrumentState::Stopped;
    baseline_valid_ = false;
    error_.clear();
}

InstrumentState InstrumentStateMachine::state() const
{
    return state_;
}

bool InstrumentStateMachine::baselineValid() const
{
    return baseline_valid_;
}

const std::string& InstrumentStateMachine::error() const
{
    return error_;
}

StateTransitionResult InstrumentStateMachine::accept(InstrumentState next)
{
    StateTransitionResult result;
    result.accepted = true;
    result.previous = state_;
    state_ = next;
    result.current = state_;
    if (next != InstrumentState::Error) error_.clear();
    return result;
}

StateTransitionResult InstrumentStateMachine::reject(const std::string& reason) const
{
    StateTransitionResult result;
    result.previous = state_;
    result.current = state_;
    result.error = reason;
    return result;
}

StateTransitionResult InstrumentStateMachine::apply(InstrumentCommand command, const std::string& detail)
{
    switch (command) {
        case InstrumentCommand::Stop:
            return accept(InstrumentState::Stopped);
        case InstrumentCommand::StartRawIq:
            if (state_ == InstrumentState::Stopped || state_ == InstrumentState::BaselineReady ||
                state_ == InstrumentState::Error)
                return accept(InstrumentState::RawIq);
            return reject("raw-IQ acquisition cannot start from " + std::string(instrumentStateName(state_)));
        case InstrumentCommand::StartBaseline:
            if (state_ == InstrumentState::Stopped || state_ == InstrumentState::RawIq ||
                state_ == InstrumentState::BaselineReady || state_ == InstrumentState::Error) {
                baseline_valid_ = false;
                return accept(InstrumentState::BaselineAcquiring);
            }
            return reject("baseline acquisition cannot start from " + std::string(instrumentStateName(state_)));
        case InstrumentCommand::CancelBaseline:
            if (state_ == InstrumentState::BaselineAcquiring || state_ == InstrumentState::ResonanceFinding)
                return accept(InstrumentState::Stopped);
            return reject("there is no baseline operation to cancel");
        case InstrumentCommand::StartResonanceFinding:
            if (state_ == InstrumentState::BaselineAcquiring ||
                (state_ == InstrumentState::BaselineReady && baseline_valid_))
                return accept(InstrumentState::ResonanceFinding);
            return reject("resonance finding requires the current baseline scan");
        case InstrumentCommand::CompleteResonanceFinding:
            if (state_ == InstrumentState::ResonanceFinding) {
                baseline_valid_ = true;
                return accept(InstrumentState::BaselineReady);
            }
            return reject("stale resonance-finding completion rejected");
        case InstrumentCommand::StartDiagnostics:
            if (state_ == InstrumentState::Stopped || state_ == InstrumentState::RawIq ||
                state_ == InstrumentState::BaselineReady)
                return accept(InstrumentState::Diagnostics);
            return reject("diagnostics cannot start from " + std::string(instrumentStateName(state_)));
        case InstrumentCommand::CancelDiagnostics:
        case InstrumentCommand::CompleteDiagnostics:
            if (state_ == InstrumentState::Diagnostics)
                return accept(baseline_valid_ ? InstrumentState::BaselineReady : InstrumentState::Stopped);
            return reject("there is no diagnostics acquisition to complete or cancel");
        case InstrumentCommand::StartTracking:
            if (state_ == InstrumentState::BaselineReady && baseline_valid_)
                return accept(InstrumentState::Tracking);
            return reject("tracking requires a valid completed baseline");
        case InstrumentCommand::TrackingGood:
            if (state_ == InstrumentState::Tracking || state_ == InstrumentState::Degraded)
                return accept(InstrumentState::Tracking);
            return reject("tracking update cannot complete from " + std::string(instrumentStateName(state_)));
        case InstrumentCommand::TrackingPoor:
            if (state_ == InstrumentState::Tracking || state_ == InstrumentState::Degraded)
                return accept(InstrumentState::Degraded);
            return reject("degraded update cannot complete from " + std::string(instrumentStateName(state_)));
        case InstrumentCommand::StopTracking:
            if (state_ == InstrumentState::Searching || state_ == InstrumentState::Tracking ||
                state_ == InstrumentState::Degraded || state_ == InstrumentState::Relocking)
                return accept(baseline_valid_ ? InstrumentState::BaselineReady : InstrumentState::Stopped);
            return reject("tracking is not active");
        case InstrumentCommand::Fail: {
            StateTransitionResult result = accept(InstrumentState::Error);
            error_ = detail.empty() ? "unspecified instrument error" : detail;
            return result;
        }
    }
    return reject("unknown instrument command");
}

const char* instrumentStateName(InstrumentState state)
{
    switch (state) {
        case InstrumentState::Stopped: return "STOPPED";
        case InstrumentState::RawIq: return "RAW_IQ";
        case InstrumentState::BaselineAcquiring: return "BASELINE_ACQUIRING";
        case InstrumentState::BaselineReady: return "BASELINE_READY";
        case InstrumentState::ResonanceFinding: return "RESONANCE_FINDING";
        case InstrumentState::Diagnostics: return "DIAGNOSTICS";
        case InstrumentState::Searching: return "SEARCHING";
        case InstrumentState::Tracking: return "TRACKING";
        case InstrumentState::Degraded: return "DEGRADED";
        case InstrumentState::Relocking: return "RELOCKING";
        case InstrumentState::Error: return "ERROR";
    }
    return "UNKNOWN";
}
