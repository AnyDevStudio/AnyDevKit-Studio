#include "Application.hpp"
#include "Audio/AudioEngine.hpp"
#include "Graphics/Display.hpp"
#include "Graphics/Renderer.hpp"
#include "Input/Controller.hpp"
#include "Save/SaveService.hpp"
#include "Scene/SceneState.hpp"
#include "Error.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include "prx/libkernel/include/Api.hpp"
#include "prx/libSceAgc/libSceAgc.hpp"

void Application::Run() {
    constexpr int Width = 1920;
    constexpr int Height = 1080;

    Display display(Width, Height);
    Renderer renderer(Width, Height);
    Controller controller;
    AudioEngine audio;
    SaveService saves;
    SceneState scene(Width, Height, audio.Mixer(), saves.Mail());
    auto previous = sceKernelGetProcessTime();

    for (;;) {
        const auto now = sceKernelGetProcessTime();
        const auto input = controller.Read();
        scene.Update(static_cast<double>(now - previous) / 1000000.0, input.Direction, input.Buttons);
        previous = now;
        renderer.Draw(display, scene);
        const auto beforeSuspend = sceKernelGetProcessTime();
        Check(sceAgcSuspendPoint(), "sceAgcSuspendPoint");
        previous += sceKernelGetProcessTime() - beforeSuspend;
    }
}
