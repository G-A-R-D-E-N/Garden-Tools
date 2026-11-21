#include "fo4/papyrus/runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace fo4::papyrus {

namespace {

class SteppingScope {
public:
    explicit SteppingScope(bool& flag) : flag_(flag) { flag_ = true; }
    ~SteppingScope() { flag_ = false; }
    SteppingScope(const SteppingScope&) = delete;
    SteppingScope& operator=(const SteppingScope&) = delete;

private:
    bool& flag_;
};

}  // namespace

SchedulerStepResult Runtime::runStep(std::int32_t budget) {
    SchedulerStepResult step;
    if (stepping_) return step;
    const SteppingScope scope(stepping_);
    if (budget <= 0) budget = 1;
    if (!gate("vm.scheduler.instruction_budget")) {
        step.refused = static_cast<std::int32_t>(ready_.size());
        refusedEvents_ += step.refused;
        for (const Execution& execution : ready_) settleCall(execution.ticket, CallState::Failed, Status::GateRefused);
        ready_.clear();
        step.waiting = static_cast<std::int32_t>(waiting_.size());
        return step;
    }

    for (auto it = waiting_.begin(); it != waiting_.end();) {
        Waiting& waiting = it->second;
        Instance* instance = findInstance(waiting.execution.instance);
        if (instance == nullptr || instance->detached) {
            settleCall(waiting.execution.ticket, CallState::Dropped, Status::HostError);
            it = waiting_.erase(it);
            continue;
        }
        if (!waiting.completion.has_value()) {
            ++it;
            continue;
        }
        Execution execution = std::move(waiting.execution);
        execution.resumeToken = it->first;
        execution.resumeValue = std::move(waiting.completion);
        ready_.push_back(std::move(execution));
        it = waiting_.erase(it);
    }

    std::int32_t spent = 0;
    const std::size_t count = ready_.size();
    for (std::size_t index = 0; index < count && spent < budget && !ready_.empty(); ++index) {
        Execution execution = std::move(ready_.front());
        ready_.erase(ready_.begin());
        const Instance* instance = findInstance(execution.instance);
        if (instance == nullptr || instance->detached) {
            settleCall(execution.ticket, CallState::Dropped, Status::HostError);
            continue;
        }
        const std::size_t remaining = count - index;
        const std::int32_t left = budget - spent;
        const std::size_t share = std::max<std::size_t>(
            1, static_cast<std::size_t>(left) / std::max<std::size_t>(1, remaining));
        spent += runSlice(std::move(execution), *instance, share, step);
    }

    step.spent = spent;
    step.waiting = static_cast<std::int32_t>(waiting_.size());
    return step;
}

std::optional<SchedulerStepResult> Runtime::runCall(std::uint64_t ticket, std::int32_t budget) {
    if (ticket == 0 || stepping_) return std::nullopt;
    const SteppingScope scope(stepping_);
    const auto found = std::find_if(ready_.begin(), ready_.end(),
                                    [ticket](const Execution& execution) { return execution.ticket == ticket; });
    if (found == ready_.end()) return std::nullopt;
    if (budget <= 0) budget = 1;
    Execution execution = std::move(*found);
    ready_.erase(found);
    SchedulerStepResult step;
    const Instance* instance = findInstance(execution.instance);
    if (!gate("vm.scheduler.instruction_budget")) {
        step.refused = 1;
        refusedEvents_ += 1;
        settleCall(execution.ticket, CallState::Failed, Status::GateRefused);
    } else if (instance == nullptr || instance->detached) {
        settleCall(execution.ticket, CallState::Dropped, Status::HostError);
    } else {
        step.spent = runSlice(std::move(execution), *instance, static_cast<std::size_t>(budget), step);
    }
    step.waiting = static_cast<std::int32_t>(waiting_.size());
    return step;
}

std::int32_t Runtime::runSlice(Execution execution, const Instance& instance, std::size_t share,
                               SchedulerStepResult& step) {
    if (execution.interpreter == nullptr) {
        execution.interpreter = std::make_unique<Interpreter>(static_cast<Host&>(*this));
    }
    Result result;
    if (execution.resumeToken.has_value() && execution.resumeValue.has_value()) {
        result = execution.interpreter->completeLatent(*execution.resumeToken,
                                                       *execution.resumeValue, share);
        execution.resumeToken.reset();
        execution.resumeValue.reset();
    } else if (execution.lastInstructions == 0 && execution.interpreter->suspended()) {
        result = execution.interpreter->resumeSlice(share);
    } else if (execution.lastInstructions == 0) {
        result = execution.interpreter->startFunction(
            fo4::papyrus::objectValue(instance.id, instance.script, instance.state),
            execution.function, execution.arguments, share);
    } else {
        result = execution.interpreter->resumeSlice(share);
    }
    const std::size_t total = result.instructions;
    const std::size_t delta = total > execution.lastInstructions ? total - execution.lastInstructions : 0;
    execution.lastInstructions = total;

    switch (result.status) {
        case Status::Latent: {
            const std::uint64_t token = result.latentToken;
            issued_.erase(token);
            if (token == 0 || usedTokens_.count(token) != 0) {
                step.refused += 1;
                refusedEvents_ += 1;
                settleCall(execution.ticket, CallState::Failed, Status::HostError);
                early_.erase(token);
                break;
            }
            usedTokens_[token] = true;
            settleCall(execution.ticket, CallState::Waiting, Status::Ok);
            Waiting waiting;
            waiting.execution = std::move(execution);
            const auto early = early_.find(token);
            if (early != early_.end()) {
                waiting.completion = std::move(early->second);
                early_.erase(early);
            }
            waiting_[token] = std::move(waiting);
            waitingEvents_ += 1;
            break;
        }
        case Status::Ok:
            step.completed += 1;
            completedEvents_ += 1;
            settleCall(execution.ticket, CallState::Done, Status::Ok, std::move(result.value), total);
            break;
        case Status::Paused: {
            step.paused += 1;
            pausedEvents_ += 1;
            execution.parks += 1;
            if (execution.parks >= maxParksPerEvent_) {
                step.dropped += 1;
                droppedEvents_ += 1;
                settleCall(execution.ticket, CallState::Dropped, Status::Paused);
            } else {
                step.parked += 1;
                parkedEvents_ += 1;
                ready_.push_back(std::move(execution));
            }
            break;
        }
        default:
            step.refused += 1;
            refusedEvents_ += 1;
            settleCall(execution.ticket, CallState::Failed, result.status, Value{}, total);
            break;
    }
    return static_cast<std::int32_t>(delta);
}

bool Runtime::completeLatent(std::uint64_t token, const Value& value) {
    const auto found = waiting_.find(token);
    if (found != waiting_.end()) {
        if (found->second.completion.has_value()) {
            refusedCompletions_ += 1;
            return false;
        }
        found->second.completion = value;
        return true;
    }
    if (issued_.count(token) != 0 && early_.count(token) == 0) {
        early_.emplace(token, value);
        return true;
    }
    refusedCompletions_ += 1;
    return false;
}

}  // namespace fo4::papyrus
