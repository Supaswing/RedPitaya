#pragma once

#include <cstdint>

enum class RequestedOperation {
    Idle,
    Baseline,
    Diagnostics,
    Tracking,
};

enum class BaselineContinuation {
    None,
    ManualTrackingRefresh,
    Recovery,
};

// The application owns synchronization: every access uses operation_mutex.
// Continuation belongs to one request generation, never to the worker lifetime.
struct OperationRequest {
    RequestedOperation operation = RequestedOperation::Idle;
    std::uint64_t generation = 0;
    BaselineContinuation continuation = BaselineContinuation::None;

    void request(RequestedOperation next)
    {
        continuation = next == RequestedOperation::Baseline && operation == RequestedOperation::Tracking ?
            BaselineContinuation::ManualTrackingRefresh : BaselineContinuation::None;
        operation = next;
        ++generation;
    }

    void requestRecoveryBaseline()
    {
        request(RequestedOperation::Baseline);
        continuation = BaselineContinuation::Recovery;
    }

    bool isCurrent(RequestedOperation expected, std::uint64_t expected_generation) const
    {
        return operation == expected && generation == expected_generation;
    }

    bool complete(RequestedOperation expected, std::uint64_t expected_generation)
    {
        if (!isCurrent(expected, expected_generation)) return false;
        operation = RequestedOperation::Idle;
        continuation = BaselineContinuation::None;
        return true;
    }

    bool completeBaseline(std::uint64_t expected_generation, bool valid)
    {
        if (!isCurrent(RequestedOperation::Baseline, expected_generation)) return false;
        const bool resume = valid && continuation != BaselineContinuation::None;
        if (resume) request(RequestedOperation::Tracking);
        else complete(RequestedOperation::Baseline, expected_generation);
        return true;
    }
};
