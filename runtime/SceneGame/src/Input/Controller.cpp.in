#include "Input/Controller.hpp"
#include "Input/Buttons.hpp"
#include "prx/libScePad/libScePad.hpp"
#include "prx/libSceSysmodule/libSceSysmodule.hpp"
#include "prx/libSceUserService/libSceUserService.hpp"
#include <cmath>

namespace {

constexpr std::uint32_t UserServiceModule = 0x80000011;
constexpr std::uint32_t PadModule = 0x80000024;
constexpr float StickCenter = 128.0f;
constexpr float StickRange = 127.0f;
constexpr float StickDeadZone = 0.2f;

std::array<float, 2> normalized(float x, float y) {
    const float length = std::sqrt(x * x + y * y);
    if (length == 0.0f) return {0.0f, 0.0f};
    return {x / length, y / length};
}

std::array<float, 2> direction(const PadData& data) {
    const float dpadX = static_cast<float>((data.buttons & Buttons::DpadRight) != 0) - static_cast<float>((data.buttons & Buttons::DpadLeft) != 0);
    const float dpadY = static_cast<float>((data.buttons & Buttons::DpadDown) != 0) - static_cast<float>((data.buttons & Buttons::DpadUp) != 0);
    if (dpadX != 0.0f || dpadY != 0.0f) return normalized(dpadX, dpadY);

    const float stickX = (static_cast<float>(data.left_stick_x) - StickCenter) / StickRange;
    const float stickY = (static_cast<float>(data.left_stick_y) - StickCenter) / StickRange;
    if (stickX * stickX + stickY * stickY < StickDeadZone * StickDeadZone) return {0.0f, 0.0f};
    return normalized(stickX, stickY);
}

}

Controller::Controller() {
    if (sceSysmoduleLoadModuleInternal(UserServiceModule) < 0) return;
    if (sceSysmoduleLoadModuleInternal(PadModule) < 0) return;
    if (sceUserServiceInitialize(nullptr) < 0) return;
    int userId = 0;
    if (sceUserServiceGetInitialUser(&userId) < 0) return;
    if (scePadInit_nid_postfix() < 0) return;
    const int handle = scePadOpen_nid_postfix(userId, 0, 0, nullptr);
    if (handle >= 0) pad = handle;
}

Controller::Input Controller::Read() const {
    if (pad < 0) return {};
    PadData data{};
    if (scePadReadState(pad, &data) < 0 || !data.connected) return {};
    return {direction(data), data.buttons};
}
