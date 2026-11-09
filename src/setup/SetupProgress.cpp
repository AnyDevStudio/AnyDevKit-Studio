#include "SetupProgress.h"

#include <array>

namespace Studio {

namespace {

constexpr std::array<SetupStep, 4> AllSteps{SetupStep::Tools, SetupStep::Download, SetupStep::Unpack, SetupStep::Dependencies};

}

QString setupStepId(SetupStep step) {
    switch (step) {
    case SetupStep::Tools: return "tools";
    case SetupStep::Download: return "download";
    case SetupStep::Unpack: return "unpack";
    case SetupStep::Dependencies: return "dependencies";
    }
    return {};
}

QString setupStepTitle(SetupStep step) {
    switch (step) {
    case SetupStep::Tools: return "Check your computer's tools";
    case SetupStep::Download: return "Download AnyDevKit";
    case SetupStep::Unpack: return "Unpack AnyDevKit";
    case SetupStep::Dependencies: return "Install the PS5 libraries";
    }
    return {};
}

QString setupStepStateName(SetupStepState state) {
    switch (state) {
    case SetupStepState::Waiting: return "waiting";
    case SetupStepState::Running: return "running";
    case SetupStepState::Done: return "done";
    case SetupStepState::Failed: return "failed";
    }
    return {};
}

SetupProgress::SetupProgress() {
    restart();
}

const QList<SetupStepStatus>& SetupProgress::steps() const { return entries; }

const SetupStepStatus& SetupProgress::status(SetupStep step) const {
    return entries.at(static_cast<int>(step));
}

bool SetupProgress::begin(SetupStep step, const QString& detail) {
    const auto index = static_cast<int>(step);
    if (running() || failed()) return false;
    for (int earlier = 0; earlier < index; ++earlier) {
        if (entries[earlier].state != SetupStepState::Done) return false;
    }
    if (entries[index].state == SetupStepState::Done) return false;
    entries[index].state = SetupStepState::Running;
    entries[index].detail = detail;
    entries[index].progress = -1;
    return true;
}

void SetupProgress::report(SetupStep step, const QString& detail, double progress) {
    auto& entry = entries[static_cast<int>(step)];
    if (entry.state != SetupStepState::Running) return;
    entry.detail = detail;
    entry.progress = progress;
}

void SetupProgress::complete(SetupStep step, const QString& detail) {
    auto& entry = entries[static_cast<int>(step)];
    entry.state = SetupStepState::Done;
    entry.detail = detail;
    entry.progress = 1;
}

void SetupProgress::fail(SetupStep step, const QString& detail) {
    auto& entry = entries[static_cast<int>(step)];
    entry.state = SetupStepState::Failed;
    entry.detail = detail;
    entry.progress = -1;
}

void SetupProgress::pause(const QString& detail) {
    for (auto& entry : entries) {
        if (entry.state != SetupStepState::Running) continue;
        entry.state = SetupStepState::Waiting;
        entry.detail = detail;
        entry.progress = -1;
    }
}

void SetupProgress::clearFailure() {
    for (auto& entry : entries) {
        if (entry.state != SetupStepState::Failed) continue;
        entry.state = SetupStepState::Waiting;
        entry.detail.clear();
    }
}

void SetupProgress::reopen(SetupStep step) {
    for (int index = static_cast<int>(step); index < entries.size(); ++index) {
        entries[index].state = SetupStepState::Waiting;
        entries[index].detail.clear();
        entries[index].progress = -1;
    }
}

void SetupProgress::restart() {
    entries.clear();
    for (const auto step : AllSteps) entries.append({step, SetupStepState::Waiting, {}, -1});
}

std::optional<SetupStep> SetupProgress::running() const {
    for (const auto& entry : entries) {
        if (entry.state == SetupStepState::Running) return entry.step;
    }
    return std::nullopt;
}

std::optional<SetupStep> SetupProgress::failed() const {
    for (const auto& entry : entries) {
        if (entry.state == SetupStepState::Failed) return entry.step;
    }
    return std::nullopt;
}

bool SetupProgress::allDone() const {
    for (const auto& entry : entries) {
        if (entry.state != SetupStepState::Done) return false;
    }
    return true;
}

}
