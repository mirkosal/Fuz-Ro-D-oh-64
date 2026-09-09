#include "Hooks.h"

namespace
{
    struct HookAddresses
    {
        std::uintptr_t cachedResponseCtor{};
        std::uintptr_t queueDialogSubtitles{};
        std::uintptr_t displayQueuedNPCChatter{};
        std::uintptr_t queueNPCChatter{};
    };

    template <std::size_t N>
    std::optional<std::uintptr_t> FindUniqueTextPattern(
        std::string_view a_name,
        const std::array<int, N>& a_pattern)
    {
        const auto& module = REL::Module::get();
        const auto text = module.segment(REL::Segment::textx);
        if (!text.address() || text.size() < N) {
            logger::error("{}: Skyrim .text segment is unavailable", a_name);
            return std::nullopt;
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.address());
        std::uintptr_t found = 0;
        std::size_t matches = 0;

        for (std::size_t i = 0; i <= text.size() - N; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < N; ++j) {
                if (a_pattern[j] >= 0 && bytes[i + j] != static_cast<std::uint8_t>(a_pattern[j])) {
                    match = false;
                    break;
                }
            }

            if (match) {
                found = text.address() + i;
                ++matches;
                if (matches > 1) {
                    break;
                }
            }
        }

        if (matches != 1) {
            logger::error("{}: expected one signature match, found {}", a_name, matches);
            return std::nullopt;
        }

        logger::info("{} resolved at Skyrim RVA 0x{:X}", a_name, found - module.base());
        return found;
    }

    bool IsInsideText(std::uintptr_t a_address, std::size_t a_length = 1)
    {
        const auto text = REL::Module::get().segment(REL::Segment::textx);
        const auto begin = text.address();
        const auto end = begin + text.size();
        return a_address >= begin && a_address <= end && a_length <= end - a_address;
    }

    std::string HexBytes(std::uintptr_t a_address, std::size_t a_count)
    {
        std::string out;
        out.reserve(a_count * 3);
        const auto* p = reinterpret_cast<const std::uint8_t*>(a_address);
        for (std::size_t i = 0; i < a_count; ++i) {
            fmt::format_to(std::back_inserter(out), "{:02X}{}", p[i], i + 1 == a_count ? "" : " ");
        }
        return out;
    }

    std::optional<HookAddresses> ResolveHookAddresses()
    {
        // These are the signatures shadeMe used when moving Fuz Ro D'oh from
        // Skyrim 1.6.1130 to 1.6.1170.  We resolve the enclosing functions at
        // runtime instead of carrying their old RVAs into 1.7.104.
        constexpr std::array cachedPattern{
            0xE8, -1, -1, -1, -1, 0x48, 0x8B, 0xF8, 0xEB, 0x02, 0x33, 0xFF, 0x48, 0x85, 0xFF
        };
        constexpr std::array queueDialogPattern{
            0xE8, -1, -1, -1, -1, 0x8B, 0x06, 0xEB, 0x09
        };
        constexpr std::array displayQueuedPattern{
            0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x75, 0x42, 0x48, 0x8B, 0x35, -1, -1, -1, -1
        };
        constexpr std::array queueNPCPattern{
            0xE8, -1, -1, -1, -1, 0xF3, 0x0F, 0x10, 0x35, -1, -1, -1, -1, 0x48, 0x8D, 0x4E, 0x28
        };

        const auto cached = FindUniqueTextPattern("CachedResponseData ctor", cachedPattern);
        const auto queueDialogSig = FindUniqueTextPattern("UIUtils::QueueDialogSubtitles signature", queueDialogPattern);
        const auto displayQueued = FindUniqueTextPattern("ASCM::DisplayQueuedNPCChatterData", displayQueuedPattern);
        const auto queueNPC = FindUniqueTextPattern("ASCM::QueueNPCChatterData", queueNPCPattern);

        if (!cached || !queueDialogSig || !displayQueued || !queueNPC) {
            return std::nullopt;
        }

        // CommonLib/Address Library has a stable ID for QueueDialogSubtitles.
        // Require the 1.7.104 Address Library result and shadeMe's signature to
        // agree before we trust the historical in-function offsets.
        REL::Relocation<std::uintptr_t> queueDialogByID{ REL::ID(52854) };
        const auto queueDialog = queueDialogByID.address();
        logger::info("UIUtils::QueueDialogSubtitles Address Library ID 52854 -> RVA 0x{:X}",
            queueDialog - REL::Module::get().base());

        if (queueDialog != *queueDialogSig) {
            logger::critical(
                "QueueDialogSubtitles signature/Address-Library disagreement (sig RVA 0x{:X}, ID RVA 0x{:X}); refusing unsafe hooks",
                *queueDialogSig - REL::Module::get().base(),
                queueDialog - REL::Module::get().base());
            return std::nullopt;
        }

        return HookAddresses{ *cached, queueDialog, *displayQueued, *queueNPC };
    }

    std::size_t CountResponseWords(std::string_view a_text)
    {
        std::size_t words = 0;
        bool inWord = false;
        std::size_t nonAsciiCodePoints = 0;

        for (std::size_t i = 0; i < a_text.size(); ++i) {
            const auto ch = static_cast<unsigned char>(a_text[i]);
            if (ch >= 0x80) {
                if ((ch & 0xC0) != 0x80) {
                    ++nonAsciiCodePoints;
                }
                inWord = false;
                continue;
            }

            const bool separator = std::isspace(ch) != 0;
            if (!separator && !inWord) {
                ++words;
            }
            inWord = !separator;
        }

        if (nonAsciiCodePoints > 0) {
            const auto widePerWord = static_cast<std::size_t>(std::max(1, GetFuzSettings().wideCharactersPerWord));
            words += (nonAsciiCodePoints + widePerWord - 1) / widePerWord;
        }
        return words;
    }

    void SneakAttackVoicePath(CachedResponseData* a_data, char* a_voicePathBuffer)
    {
        if (!a_data || !a_voicePathBuffer) {
            return;
        }

        // Instruction replaced by the original hook.
        a_data->voiceFilePath = a_voicePathBuffer;

        if (std::strlen(a_voicePathBuffer) < 17) {
            return;
        }

        std::string fuzPath(a_voicePathBuffer);
        std::string wavPath(a_voicePathBuffer);
        std::string xwmPath(a_voicePathBuffer);

        if (wavPath.size() >= 5) {
            wavPath.erase(0, 5);  // BSResource paths are relative to Data.
        }
        if (fuzPath.size() >= 8) {
            fuzPath.erase(0, 5);
            fuzPath.replace(fuzPath.size() - 3, 3, "fuz");
        }
        if (xwmPath.size() >= 8) {
            xwmPath.erase(0, 5);
            xwmPath.replace(xwmPath.size() - 3, 3, "xwm");
        }

        RE::BSResourceNiBinaryStream wavStream(wavPath.c_str());
        RE::BSResourceNiBinaryStream fuzStream(fuzPath.c_str());
        RE::BSResourceNiBinaryStream xwmStream(xwmPath.c_str());

        if (wavStream.good() || fuzStream.good() || xwmStream.good()) {
            return;
        }

        const std::string responseText(a_data->responseText.c_str());
        int secondsOfSilence = 2;

        if (responseText.size() > 4 && responseText.rfind("<ID=", 0) != 0) {
            const auto wordCount = CountResponseWords(responseText);
            const auto wordsPerSecond = std::max(1, GetFuzSettings().wordsPerSecondSilence);
            secondsOfSilence = static_cast<int>(wordCount / static_cast<std::size_t>(wordsPerSecond)) + 1;
            secondsOfSilence = std::clamp(secondsOfSilence, 2, 10);
            SubtitleHasher::Instance.Add(a_data->responseText.c_str());
        }

        if (responseText.size() > 1 ||
            (responseText.size() == 1 && responseText[0] == ' ' && !GetFuzSettings().skipEmptyResponses)) {
            const auto shim = fmt::format("Data\\Sound\\Voice\\Fuz Ro Doh\\Stock_{}.xwm", secondsOfSilence);
            a_data->voiceFilePath = shim.c_str();
            logger::debug("Missing voice asset; substituting {}", shim);
        }
    }

    bool ShouldForceSubs(NPCChatterData* a_chatterData, std::uint32_t a_forceRegardless, const char* a_subtitle)
    {
        if (a_subtitle && SubtitleHasher::Instance.HasMatch(a_subtitle)) {
            return true;
        }
        if (a_forceRegardless || (a_chatterData && a_chatterData->forceSubtitles)) {
            return true;
        }

        auto* manager = RE::MenuTopicManager::GetSingleton();
        if (!manager) {
            return false;
        }

        RE::TESTopicInfo* topicInfo = nullptr;
        if (manager->selectedResponseNode && manager->selectedResponseNode->item) {
            topicInfo = manager->selectedResponseNode->item->parentTopicInfo;
        } else if (manager->lastSelectedDialogue) {
            topicInfo = manager->lastSelectedDialogue->parentTopicInfo;
        }

        if (!topicInfo) {
            topicInfo = manager->rootTopicInfo;
        }
        if (!topicInfo) {
            topicInfo = manager->currentTopicInfo;
        }
        if (!topicInfo) {
            return false;
        }

        std::uint16_t rawFlags = 0;
        static_assert(sizeof(topicInfo->data.flags) == sizeof(rawFlags));
        std::memcpy(&rawFlags, std::addressof(topicInfo->data.flags), sizeof(rawFlags));
        return (rawFlags & (1u << 9)) != 0;  // TOPIC_INFO_DATA::kForceSubtitle
    }

#define PUSH_VOLATILE push(rcx); push(rdx); push(r8)
#define POP_VOLATILE pop(r8); pop(rdx); pop(rcx)

    bool ValidateSites(const HookAddresses& a)
    {
        struct Site
        {
            const char* name;
            std::uintptr_t address;
        };

        const std::array sites{
            Site{ "CachedResponseData+EC", a.cachedResponseCtor + 0xEC },
            Site{ "QueueDialogSubtitles+4D", a.queueDialogSubtitles + 0x4D },
            Site{ "DisplayQueuedNPCChatter+99", a.displayQueuedNPCChatter + 0x99 },
            Site{ "DisplayQueuedNPCChatter+1CA", a.displayQueuedNPCChatter + 0x1CA },
            Site{ "QueueNPCChatter+85", a.queueNPCChatter + 0x85 }
        };

        for (const auto& site : sites) {
            if (!IsInsideText(site.address, 16)) {
                logger::critical("{} is outside Skyrim .text", site.name);
                return false;
            }
            logger::info("{} RVA 0x{:X}: {}", site.name,
                site.address - REL::Module::get().base(), HexBytes(site.address, 16));
        }

        // The cached-response hook replaces a five-byte call in all upstream
        // supported runtimes.  Refuse to patch if that invariant changed.
        if (*reinterpret_cast<const std::uint8_t*>(a.cachedResponseCtor + 0xEC) != 0xE8) {
            logger::critical("CachedResponseData+0xEC is no longer a CALL; refusing unsafe port");
            return false;
        }

        return true;
    }
}

bool InstallHooks()
{
    if (!REL::Module::IsAE() || REL::Module::get().version() != REL::Version{ 1, 7, 104, 0 }) {
        logger::critical("InstallHooks called for an unsupported runtime");
        return false;
    }

    const auto addresses = ResolveHookAddresses();
    if (!addresses || !ValidateSites(*addresses)) {
        return false;
    }

    SKSE::AllocTrampoline(32 * 1024);
    auto& trampoline = SKSE::GetTrampoline();

    {
        struct Code : Xbyak::CodeGenerator
        {
            Code(void* a_buffer, std::uintptr_t a_return) : Xbyak::CodeGenerator(4096, a_buffer)
            {
                Xbyak::Label returnLabel;
                push(rcx);
                push(rdx);
                mov(rcx, rbx);
                mov(rax, reinterpret_cast<std::uintptr_t>(SneakAttackVoicePath));
                call(rax);
                pop(rdx);
                pop(rcx);
                jmp(ptr[rip + returnLabel]);
                L(returnLabel);
                dq(a_return);
            }
        };

        auto* buffer = trampoline.allocate(4096);
        Code code(buffer, addresses->cachedResponseCtor + 0xF1);
        code.ready();
        trampoline.write_branch<5>(addresses->cachedResponseCtor + 0xEC,
            reinterpret_cast<std::uintptr_t>(code.getCode()), true);
    }

    {
        struct Code : Xbyak::CodeGenerator
        {
            Code(void* a_buffer, std::uintptr_t a_show, std::uintptr_t a_exit) : Xbyak::CodeGenerator(4096, a_buffer)
            {
                Xbyak::Label showLabel;
                mov(rax, reinterpret_cast<std::uintptr_t>(CanShowDialogSubtitles));
                PUSH_VOLATILE;
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                PUSH_VOLATILE;
                xor_(rcx, rcx);
                xor_(rdx, rdx);
                mov(r8, r14);
                mov(rax, reinterpret_cast<std::uintptr_t>(ShouldForceSubs));
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                mov(rax, a_exit);
                jmp(rax);
                L(showLabel);
                mov(rax, a_show);
                jmp(rax);
            }
        };

        auto* buffer = trampoline.allocate(4096);
        Code code(buffer, addresses->queueDialogSubtitles + 0x5A, addresses->queueDialogSubtitles + 0x103);
        code.ready();
        trampoline.write_branch<5>(addresses->queueDialogSubtitles + 0x4D,
            reinterpret_cast<std::uintptr_t>(code.getCode()), true);
    }

    {
        struct Code : Xbyak::CodeGenerator
        {
            Code(void* a_buffer, std::uintptr_t a_show, std::uintptr_t a_exit) : Xbyak::CodeGenerator(4096, a_buffer)
            {
                Xbyak::Label showLabel;
                mov(rax, reinterpret_cast<std::uintptr_t>(CanShowDialogSubtitles));
                PUSH_VOLATILE;
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                PUSH_VOLATILE;
                mov(rcx, rsi);
                xor_(rdx, rdx);
                mov(r8, ptr[rsi + 0x8]);
                mov(rax, reinterpret_cast<std::uintptr_t>(ShouldForceSubs));
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                mov(rax, a_exit);
                jmp(rax);
                L(showLabel);
                mov(rax, a_show);
                jmp(rax);
            }
        };

        auto* buffer = trampoline.allocate(4096);
        Code code(buffer,
            addresses->displayQueuedNPCChatter + 0x1D3,
            addresses->displayQueuedNPCChatter + 0x1FD);
        code.ready();
        trampoline.write_branch<5>(addresses->displayQueuedNPCChatter + 0x1CA,
            reinterpret_cast<std::uintptr_t>(code.getCode()), true);
    }

    {
        struct Code : Xbyak::CodeGenerator
        {
            Code(void* a_buffer, std::uintptr_t a_show, std::uintptr_t a_exit) : Xbyak::CodeGenerator(4096, a_buffer)
            {
                Xbyak::Label showLabel;
                mov(rax, reinterpret_cast<std::uintptr_t>(CanShowGeneralSubtitles));
                PUSH_VOLATILE;
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                PUSH_VOLATILE;
                mov(rcx, rsi);
                xor_(rdx, rdx);
                mov(r8, ptr[rsi + 0x8]);
                mov(rax, reinterpret_cast<std::uintptr_t>(ShouldForceSubs));
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                mov(rax, a_exit);
                jmp(rax);
                L(showLabel);
                mov(rax, a_show);
                jmp(rax);
            }
        };

        auto* buffer = trampoline.allocate(4096);
        Code code(buffer,
            addresses->displayQueuedNPCChatter + 0xA6,
            addresses->displayQueuedNPCChatter + 0x1CA);
        code.ready();
        trampoline.write_branch<5>(addresses->displayQueuedNPCChatter + 0x99,
            reinterpret_cast<std::uintptr_t>(code.getCode()), true);
    }

    {
        struct Code : Xbyak::CodeGenerator
        {
            Code(void* a_buffer, std::uintptr_t a_show, std::uintptr_t a_exit) : Xbyak::CodeGenerator(4096, a_buffer)
            {
                Xbyak::Label showLabel;
                mov(rax, reinterpret_cast<std::uintptr_t>(CanShowDialogSubtitles));
                PUSH_VOLATILE;
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                PUSH_VOLATILE;
                xor_(rcx, rcx);
                mov(edx, r15d);
                mov(r8, rbp);
                mov(rax, reinterpret_cast<std::uintptr_t>(ShouldForceSubs));
                call(rax);
                POP_VOLATILE;
                test(al, al);
                jnz(showLabel);

                mov(rax, a_exit);
                jmp(rax);
                L(showLabel);
                mov(rax, a_show);
                jmp(rax);
            }
        };

        auto* buffer = trampoline.allocate(4096);
        Code code(buffer,
            addresses->queueNPCChatter + 0x92,
            addresses->queueNPCChatter + 0xCA);
        code.ready();
        trampoline.write_branch<5>(addresses->queueNPCChatter + 0x85,
            reinterpret_cast<std::uintptr_t>(code.getCode()), true);
    }

    logger::info("All five Fuz Ro D'oh hooks installed for Skyrim 1.7.104");
    return true;
}
