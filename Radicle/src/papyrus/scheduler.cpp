#include "fo4/papyrus/runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace fo4::papyrus {

SchedulerStepResult Runtime::runStep(std::int32_t budget) {
    SchedulerStepResult step;
    if (budget <= 0) budget = 1;
    if (!gate("vm.scheduler.instruction_budget")) {
        step.refused = static_cast<std::int32_t>(ready_.size());
        refusedEvents_ += step.refused;
        ready_.clear();
        step.waiting = static_cast<std::int32_t>(waiting_.size());
        return step;
    }

    for (auto it = waiting_.begin(); it != waiting_.end();) {
        Waiting& waiting = it->second;
        if (!waiting.completion.has_value()) {
            ++it;
            continue;
        }
        Instance* instance = findInstance(waiting.execution.instance);
        if (instance == nullptr || instance->detached) {
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
    for (std::size_t index = 0; index < count && spent < budget; ++index) {
        Execution execution = std::move(ready_.front());
        ready_.erase(ready_.begin());
        Instance* instance = findInstance(execution.instance);
        if (instance == nullptr || instance->detached) {
            ready_.push_back(std::move(execution));
            continue;
        }
        const std::size_t remaining = count - index;
        const std::int32_t left = budget - spent;
        const std::size_t share = std::max<std::size_t>(
            1, static_cast<std::size_t>(left) / std::max<std::size_t>(1, remaining));

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
                fo4::papyrus::objectValue(instance->id, instance->script, instance->state),
                execution.function, execution.arguments, share);
        } else {
            result = execution.interpreter->resumeSlice(share);
        }
        const std::size_t total = result.instructions;
        const std::size_t delta = total > execution.lastInstructions ? total - execution.lastInstructions : 0;
        execution.lastInstructions = total;
        spent += static_cast<std::int32_t>(delta);

        switch (result.status) {
            case Status::Latent: {
                const std::uint64_t token = result.latentToken;
                if (token == 0 || usedTokens_.count(token) != 0) {
                    step.refused += 1;
                    refusedEvents_ += 1;
                    break;
                }
                usedTokens_[token] = true;
                Waiting waiting;
                waiting.execution = std::move(execution);
                waiting_[token] = std::move(waiting);
                waitingEvents_ += 1;
                step.waiting = static_cast<std::int32_t>(waiting_.size());
                break;
            }
            case Status::Ok:
                step.completed += 1;
                completedEvents_ += 1;
                break;
            case Status::Paused: {
                step.paused += 1;
                pausedEvents_ += 1;
                execution.parks += 1;
                if (execution.parks >= maxParksPerEvent_) {
                    step.dropped += 1;
                    droppedEvents_ += 1;
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
                break;
        }
    }

    step.spent = spent;
    step.waiting = static_cast<std::int32_t>(waiting_.size());
    return step;
}

bool Runtime::completeLatent(std::uint64_t token, const Value& value) {
    const auto found = waiting_.find(token);
    if (found == waiting_.end() || found->second.completion.has_value()) {
        refusedCompletions_ += 1;
        return false;
    }
    found->second.completion = value;
    return true;
}

}  // namespace fo4::papyrus
