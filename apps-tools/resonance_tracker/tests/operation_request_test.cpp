#include "instrument_state.hpp"
#include "operation_request.hpp"

#include <cassert>
#include <initializer_list>

namespace {
void ready(InstrumentStateMachine& state)
{
    assert(state.apply(InstrumentCommand::StartBaseline).accepted);
    assert(state.apply(InstrumentCommand::StartResonanceFinding).accepted);
    assert(state.apply(InstrumentCommand::CompleteResonanceFinding).accepted);
}

void manualRefresh(InstrumentState initial)
{
    InstrumentStateMachine state;
    ready(state);
    assert(state.apply(InstrumentCommand::StartTracking).accepted);
    if (initial != InstrumentState::Tracking)
        assert(state.apply(InstrumentCommand::TrackingPoor).accepted);
    if (initial == InstrumentState::Relocking)
        assert(state.apply(InstrumentCommand::BeginRelock).accepted);
    assert(state.state() == initial);

    OperationRequest request;
    request.request(RequestedOperation::Tracking);
    request.request(RequestedOperation::Baseline);
    const auto generation = request.generation;
    assert(request.continuation == BaselineContinuation::ManualTrackingRefresh);
    assert(state.apply(InstrumentCommand::StopTracking).accepted);
    ready(state);
    assert(request.completeBaseline(generation, true));
    assert(request.operation == RequestedOperation::Tracking);
    assert(request.generation > generation);
    assert(request.continuation == BaselineContinuation::None);
    assert(state.apply(InstrumentCommand::StartTracking).accepted);
}
}

int main()
{
    manualRefresh(InstrumentState::Tracking);
    manualRefresh(InstrumentState::Degraded);
    manualRefresh(InstrumentState::Relocking);

    OperationRequest request;
    request.request(RequestedOperation::Baseline);
    assert(request.continuation == BaselineContinuation::None);
    assert(request.completeBaseline(request.generation, true));
    assert(request.operation == RequestedOperation::Idle);

    // Stop/cancel, diagnostics, a new baseline, and a newer tracking request
    // all supersede the old completion, including its continuation intent.
    for (const auto next : {RequestedOperation::Idle, RequestedOperation::Diagnostics,
                            RequestedOperation::Baseline, RequestedOperation::Tracking}) {
        request.request(RequestedOperation::Tracking);
        request.request(RequestedOperation::Baseline);
        const auto stale = request.generation;
        request.request(next);
        assert(!request.completeBaseline(stale, true));
        assert(request.operation == next);
        assert(request.continuation == BaselineContinuation::None);
        if (next == RequestedOperation::Baseline) {
            assert(request.completeBaseline(request.generation, true));
            assert(request.operation == RequestedOperation::Idle);
        }
    }

    // Acquisition failure, invalid/no-resonance results, and early setup
    // failures must not leave continuation intent for a later baseline.
    for (bool recovery : {false, true}) {
        request.request(RequestedOperation::Tracking);
        if (recovery) request.requestRecoveryBaseline();
        else request.request(RequestedOperation::Baseline);
        assert(request.completeBaseline(request.generation, false));
        assert(request.operation == RequestedOperation::Idle);
        assert(request.continuation == BaselineContinuation::None);
        request.request(RequestedOperation::Tracking);
        request.requestRecoveryBaseline();
        assert(request.complete(RequestedOperation::Baseline, request.generation));
        assert(request.continuation == BaselineContinuation::None);
    }

    request.request(RequestedOperation::Tracking);
    request.requestRecoveryBaseline();
    assert(request.continuation == BaselineContinuation::Recovery);
    assert(request.completeBaseline(request.generation, true));
    assert(request.operation == RequestedOperation::Tracking);

    // Stop followed immediately by baseline must not inherit the worker's
    // possibly still-active tracker, even before it observes the stop.
    request.request(RequestedOperation::Idle);
    request.request(RequestedOperation::Baseline);
    assert(request.continuation == BaselineContinuation::None);
    assert(request.completeBaseline(request.generation, true));
    assert(request.operation == RequestedOperation::Idle);
}
