from pathlib import Path

path = Path("src/Hooks.cpp")
text = path.read_text(encoding="utf-8")

old_resolve = '''        const auto cached = FindUniqueTextPattern("CachedResponseData ctor", cachedPattern);\n        const auto queueDialogSig = FindUniqueTextPattern("UIUtils::QueueDialogSubtitles signature", queueDialogPattern);\n        const auto displayQueued = FindUniqueTextPattern("ASCM::DisplayQueuedNPCChatterData", displayQueuedPattern);\n        const auto queueNPC = FindUniqueTextPattern("ASCM::QueueNPCChatterData", queueNPCPattern);\n\n        if (!cached || !queueDialogSig || !displayQueued || !queueNPC) {\n            return std::nullopt;\n        }\n\n        // CommonLib/Address Library has a stable ID for QueueDialogSubtitles.\n        // Require the 1.7.104 Address Library result and shadeMe's signature to\n        // agree before we trust the historical in-function offsets.\n        REL::Relocation<std::uintptr_t> queueDialogByID{ REL::ID(52854) };\n        const auto queueDialog = queueDialogByID.address();\n        logger::info("UIUtils::QueueDialogSubtitles Address Library ID 52854 -> RVA 0x{:X}",\n            queueDialog - REL::Module::get().base());\n\n        if (queueDialog != *queueDialogSig) {\n            logger::critical(\n                "QueueDialogSubtitles signature/Address-Library disagreement (sig RVA 0x{:X}, ID RVA 0x{:X}); refusing unsafe hooks",\n                *queueDialogSig - REL::Module::get().base(),\n                queueDialog - REL::Module::get().base());\n            return std::nullopt;\n        }\n\n        return HookAddresses{ *cached, queueDialog, *displayQueued, *queueNPC };\n'''

new_resolve = '''        // The upstream 1.6.1170 RVAs reverse-map to these Address Library IDs:\n        //   CachedResponseData ctor         0x5DE460 -> 35249\n        //   QueueDialogSubtitles            0x976E60 -> 52854\n        //   DisplayQueuedNPCChatterData     0x96D1B0 -> 52637\n        //   QueueNPCChatterData             0x96CB00 -> 52626\n        // Resolve the actual 1.7.104 function bases from Address Library v13.\n        const auto cachedSig = FindUniqueTextPattern("CachedResponseData ctor", cachedPattern);\n        const auto queueDialogSig = FindUniqueTextPattern("UIUtils::QueueDialogSubtitles signature", queueDialogPattern);\n        const auto displayQueuedSig = FindUniqueTextPattern("ASCM::DisplayQueuedNPCChatterData", displayQueuedPattern);\n        const auto queueNPCSig = FindUniqueTextPattern("ASCM::QueueNPCChatterData", queueNPCPattern);\n\n        REL::Relocation<std::uintptr_t> cachedByID{ REL::ID(35249) };\n        REL::Relocation<std::uintptr_t> queueDialogByID{ REL::ID(52854) };\n        REL::Relocation<std::uintptr_t> displayQueuedByID{ REL::ID(52637) };\n        REL::Relocation<std::uintptr_t> queueNPCByID{ REL::ID(52626) };\n\n        const auto cached = cachedByID.address();\n        const auto queueDialog = queueDialogByID.address();\n        const auto displayQueued = displayQueuedByID.address();\n        const auto queueNPC = queueNPCByID.address();\n        const auto base = REL::Module::get().base();\n\n        logger::info("CachedResponseData Address Library ID 35249 -> RVA 0x{:X}", cached - base);\n        logger::info("UIUtils::QueueDialogSubtitles Address Library ID 52854 -> RVA 0x{:X}", queueDialog - base);\n        logger::info("ASCM::DisplayQueuedNPCChatterData Address Library ID 52637 -> RVA 0x{:X}", displayQueued - base);\n        logger::info("ASCM::QueueNPCChatterData Address Library ID 52626 -> RVA 0x{:X}", queueNPC - base);\n\n        // Signatures are now diagnostics only.  They can move inside a function\n        // when Bethesda changes code generation; the Address Library ID is the\n        // authoritative function base for this probe.\n        if (cachedSig) {\n            logger::info("CachedResponseData signature delta from ID base: {:+#x}",\n                static_cast<std::intptr_t>(*cachedSig) - static_cast<std::intptr_t>(cached));\n        }\n        if (queueDialogSig) {\n            logger::info("QueueDialogSubtitles signature delta from ID base: {:+#x}",\n                static_cast<std::intptr_t>(*queueDialogSig) - static_cast<std::intptr_t>(queueDialog));\n        }\n        if (displayQueuedSig) {\n            logger::info("DisplayQueuedNPCChatterData signature delta from ID base: {:+#x}",\n                static_cast<std::intptr_t>(*displayQueuedSig) - static_cast<std::intptr_t>(displayQueued));\n        }\n        if (queueNPCSig) {\n            logger::info("QueueNPCChatterData signature delta from ID base: {:+#x}",\n                static_cast<std::intptr_t>(*queueNPCSig) - static_cast<std::intptr_t>(queueNPC));\n        }\n\n        return HookAddresses{ cached, queueDialog, displayQueued, queueNPC };\n'''

if old_resolve not in text:
    raise SystemExit("Expected ResolveHookAddresses block not found")
text = text.replace(old_resolve, new_resolve, 1)

start = text.index("    bool ValidateSites(const HookAddresses& a)\n")
end_marker = "    }\n}\n\nbool InstallHooks()"
end = text.index(end_marker, start)

new_validate = r'''    bool ValidateSites(const HookAddresses& a)
    {
        const auto& module = REL::Module::get();
        const auto base = module.base();

        struct HistoricalSite
        {
            const char* name;
            std::uintptr_t address;
        };

        const std::array historicalSites{
            HistoricalSite{ "CachedResponseData historical +0xEC", a.cachedResponseCtor + 0xEC },
            HistoricalSite{ "QueueDialogSubtitles historical +0x4D", a.queueDialogSubtitles + 0x4D },
            HistoricalSite{ "DisplayQueuedNPCChatter historical +0x99", a.displayQueuedNPCChatter + 0x99 },
            HistoricalSite{ "DisplayQueuedNPCChatter historical +0x1CA", a.displayQueuedNPCChatter + 0x1CA },
            HistoricalSite{ "QueueNPCChatter historical +0x85", a.queueNPCChatter + 0x85 }
        };

        logger::info("--- Skyrim 1.7.104 hook-site diagnostic ---");
        for (const auto& site : historicalSites) {
            if (!IsInsideText(site.address, 32)) {
                logger::warning("{} is outside Skyrim .text", site.name);
                continue;
            }
            const auto dumpStart = site.address >= base + 16 ? site.address - 16 : site.address;
            logger::info("{} RVA 0x{:X}; bytes[-16..+31]: {}",
                site.name, site.address - base, HexBytes(dumpStart, 48));
        }

        // The original +0xEC hook replaced StringCache::Ref::Set on
        // CachedResponseData::voiceFilePath (offset 0x18).  The function grew in
        // 1.7.104, so search the Address-Library-resolved function body for CALLs
        // preceded by LEA RCX,[RBX+18h].  This is diagnostic-only: no candidate is
        // patched until a unique, structurally valid site has been confirmed.
        std::vector<std::uintptr_t> leaCallCandidates;
        constexpr std::size_t scanSize = 0x300;
        const auto scanBegin = a.cachedResponseCtor;
        const auto scanEnd = scanBegin + scanSize;

        if (IsInsideText(scanBegin, scanSize)) {
            for (auto p = scanBegin + 4; p + 5 <= scanEnd; ++p) {
                if (*reinterpret_cast<const std::uint8_t*>(p) != 0xE8) {
                    continue;
                }

                std::int32_t rel = 0;
                std::memcpy(&rel, reinterpret_cast<const void*>(p + 1), sizeof(rel));
                const auto target = static_cast<std::uintptr_t>(
                    static_cast<std::intptr_t>(p + 5) + static_cast<std::intptr_t>(rel));

                const auto* pre = reinterpret_cast<const std::uint8_t*>(p - 4);
                const bool leaRcxRbx18 = pre[0] == 0x48 && pre[1] == 0x8D && pre[2] == 0x4B && pre[3] == 0x18;

                if (leaRcxRbx18 || (p >= scanBegin + 0xA0 && p <= scanBegin + 0x180)) {
                    logger::info(
                        "CachedResponseData CALL +0x{:X} -> target RVA 0x{:X}; lea_rcx_rbx_18={}; context={}",
                        p - scanBegin,
                        target >= base ? target - base : target,
                        leaRcxRbx18,
                        HexBytes(p - 12, 28));
                }

                if (leaRcxRbx18) {
                    leaCallCandidates.push_back(p);
                }
            }
        } else {
            logger::critical("CachedResponseData ID base + 0x300 leaves Skyrim .text");
        }

        logger::info("CachedResponseData LEA RCX,[RBX+18]+CALL candidates: {}", leaCallCandidates.size());
        for (const auto p : leaCallCandidates) {
            logger::info("  candidate hook RVA 0x{:X} (function +0x{:X})", p - base, p - scanBegin);
        }

        logger::critical(
            "Diagnostic build: function bases were resolved by Address Library IDs, but in-function 1.7.104 hook offsets are not yet trusted; refusing to install hooks");
        return false;
    }
'''

text = text[:start] + new_validate + "}\n\nbool InstallHooks()" + text[end + len(end_marker):]
path.write_text(text, encoding="utf-8")
print("Applied Address Library ID resolution and 1.7.104 hook-site diagnostics")
