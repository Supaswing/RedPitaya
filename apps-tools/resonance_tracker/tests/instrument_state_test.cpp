#include "instrument_state.hpp"

#include <cassert>

int main()
{
    InstrumentStateMachine machine;
    assert(machine.state() == InstrumentState::Stopped);

    assert(machine.apply(InstrumentCommand::StartRawIq).accepted);
    assert(machine.state() == InstrumentState::RawIq);
    assert(machine.apply(InstrumentCommand::StartBaseline).accepted);
    assert(machine.state() == InstrumentState::BaselineAcquiring);
    assert(!machine.baselineValid());

    assert(machine.apply(InstrumentCommand::StartResonanceFinding).accepted);
    assert(!machine.baselineValid());
    assert(machine.apply(InstrumentCommand::CompleteResonanceFinding).accepted);
    assert(machine.state() == InstrumentState::BaselineReady);
    assert(machine.baselineValid());
    assert(!machine.apply(InstrumentCommand::CompleteResonanceFinding).accepted);

    assert(machine.apply(InstrumentCommand::StartResonanceFinding).accepted);
    assert(machine.apply(InstrumentCommand::CancelBaseline).accepted);
    assert(machine.state() == InstrumentState::Stopped);
    assert(!machine.apply(InstrumentCommand::CompleteResonanceFinding).accepted);
    assert(machine.state() == InstrumentState::Stopped);

    assert(machine.apply(InstrumentCommand::StartDiagnostics).accepted);
    assert(machine.apply(InstrumentCommand::CompleteDiagnostics).accepted);
    assert(machine.state() == InstrumentState::BaselineReady);

    assert(machine.apply(InstrumentCommand::StartTracking).accepted);
    assert(machine.state() == InstrumentState::Tracking);
    assert(machine.apply(InstrumentCommand::TrackingPoor).accepted);
    assert(machine.state() == InstrumentState::Degraded);
    assert(machine.apply(InstrumentCommand::BeginRelock).accepted);
    assert(machine.state() == InstrumentState::Relocking);
    assert(!machine.apply(InstrumentCommand::TrackingGood).accepted);
    assert(machine.apply(InstrumentCommand::RelockSucceeded).accepted);
    assert(machine.state() == InstrumentState::Tracking);
    assert(!machine.apply(InstrumentCommand::RelockSucceeded).accepted);
    assert(machine.apply(InstrumentCommand::TrackingPoor).accepted);
    assert(machine.apply(InstrumentCommand::BeginRelock).accepted);
    assert(machine.apply(InstrumentCommand::RelockFallback).accepted);
    assert(machine.state() == InstrumentState::Searching);
    assert(machine.apply(InstrumentCommand::StopTracking).accepted);
    assert(machine.state() == InstrumentState::BaselineReady);
    assert(machine.apply(InstrumentCommand::StartBaseline).accepted);
    assert(machine.apply(InstrumentCommand::StartResonanceFinding).accepted);
    assert(machine.apply(InstrumentCommand::CompleteResonanceFinding).accepted);
    assert(machine.apply(InstrumentCommand::StartTracking).accepted);
    assert(machine.apply(InstrumentCommand::TrackingGood).accepted);
    assert(machine.state() == InstrumentState::Tracking);
    assert(machine.apply(InstrumentCommand::StopTracking).accepted);
    assert(machine.state() == InstrumentState::BaselineReady);

    assert(machine.apply(InstrumentCommand::StartTracking).accepted);
    assert(machine.apply(InstrumentCommand::TrackingPoor).accepted);
    assert(machine.apply(InstrumentCommand::BeginRelock).accepted);
    assert(machine.apply(InstrumentCommand::RelockFallback).accepted);
    assert(machine.apply(InstrumentCommand::StopTracking).accepted);
    assert(machine.apply(InstrumentCommand::StartBaseline).accepted);
    assert(machine.apply(InstrumentCommand::Fail, "recovery baseline has no acceptable resonance").accepted);
    assert(machine.state() == InstrumentState::Error);
    assert(!machine.baselineValid());

    assert(machine.apply(InstrumentCommand::Fail, "test failure").accepted);
    assert(machine.state() == InstrumentState::Error);
    assert(machine.error() == "test failure");
    assert(machine.apply(InstrumentCommand::StartRawIq).accepted);
    assert(machine.error().empty());
    assert(machine.apply(InstrumentCommand::Stop).accepted);
    assert(machine.state() == InstrumentState::Stopped);
    return 0;
}
