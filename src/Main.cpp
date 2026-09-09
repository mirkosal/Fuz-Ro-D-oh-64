#include "FuzRoDohInternals.h"
#include "Hooks.h"

namespace
{
    void InitializeLog()
    {
        auto path = logger::log_directory();
        if (!path) {
            return;
        }

        *path /= "Fuz Ro D'oh.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message || a_message->type != SKSE::MessagingInterface::kInputLoaded) {
            return;
        }

        static std::once_flag cleanupThreadOnce;
        std::call_once(cleanupThreadOnce, []() {
            std::thread([]() {
                for (;;) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    SubtitleHasher::Instance.Tick();
                }
            }).detach();
            logger::info("Scheduled subtitle hash cleanup thread");
        });
    }
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({ Version::MAJOR, Version::MINOR, Version::PATCH });
    v.PluginName("Fuz Ro D'oh");
    v.AuthorName("shadeMe; Skyrim 1.7.104 CommonLib port");
    v.UsesAddressLibrary();
    v.CompatibleVersions({ REL::Version{ 1, 7, 104, 0 } });
    return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    InitializeLog();

    if (!a_skse) {
        return false;
    }

    const REL::Version targetRuntime{ 1, 7, 104, 0 };
    if (a_skse->RuntimeVersion() != targetRuntime) {
        logger::critical("Unsupported Skyrim runtime {}; this test build only supports {}",
            a_skse->RuntimeVersion().string(), targetRuntime.string());
        return false;
    }

    SKSE::Init(a_skse, false);

    logger::info("Fuz Ro D'oh {} loading on Skyrim {}", Version::NAME, targetRuntime.string());
    GetFuzSettings().Load();

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || messaging->Version() < SKSE::MessagingInterface::kVersion) {
        logger::critical("SKSE messaging interface is unavailable or too old");
        return false;
    }

    if (!InstallHooks()) {
        logger::critical("Hook validation/installation failed; refusing to load the plugin");
        return false;
    }

    if (!messaging->RegisterListener(MessageHandler)) {
        logger::critical("Could not register the SKSE message listener");
        return false;
    }

    logger::info("Fuz Ro D'oh initialized successfully");
    return true;
}
