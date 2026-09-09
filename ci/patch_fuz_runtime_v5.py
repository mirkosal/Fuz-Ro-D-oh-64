from pathlib import Path

path = Path("src/Hooks.cpp")
text = path.read_text(encoding="utf-8")

# Resolve the four enclosing functions only through Address Library v13 IDs.
# The old byte signatures are no longer authoritative on Skyrim 1.7.104.
resolve_start = text.index("    std::optional<HookAddresses> ResolveHookAddresses()\n")
resolve_end = text.index("    std::size_t CountResponseWords", resolve_start)

new_resolve = r'''    std::optional<HookAddresses> ResolveHookAddresses()
    {
        // Upstream Skyrim 1.6.1170 RVAs reverse-mapped to stable Address Library IDs:
        //   CachedResponseData ctor         -> 35249
        //   UIUtils::QueueDialogSubtitles   -> 52854
        //   ASCM::DisplayQueuedNPCChatterData -> 52637
        //   ASCM::QueueNPCChatterData       -> 52626
        // Address Library v13 supplies the corresponding 1.7.104 function bases.
        REL::Relocation<std::uintptr_t> cachedByID{ REL::ID(35249) };
        REL::Relocation<std::uintptr_t> queueDialogByID{ REL::ID(52854) };
        REL::Relocation<std::uintptr_t> displayQueuedByID{ REL::ID(52637) };
        REL::Relocation<std::uintptr_t> queueNPCByID{ REL::ID(52626) };

        const HookAddresses addresses{
            cachedByID.address(),
            queueDialogByID.address(),
            displayQueuedByID.address(),
            queueNPCByID.address()
        };

        const auto base = REL::Module::get().base();
        logger::info("CachedResponseData Address Library ID 35249 -> RVA 0x{:X}", addresses.cachedResponseCtor - base);
        logger::info("UIUtils::QueueDialogSubtitles Address Library ID 52854 -> RVA 0x{:X}", addresses.queueDialogSubtitles - base);
        logger::info("ASCM::DisplayQueuedNPCChatterData Address Library ID 52637 -> RVA 0x{:X}", addresses.displayQueuedNPCChatter - base);
        logger::info("ASCM::QueueNPCChatterData Address Library ID 52626 -> RVA 0x{:X}", addresses.queueNPCChatter - base);

        if (!IsInsideText(addresses.cachedResponseCtor) ||
            !IsInsideText(addresses.queueDialogSubtitles) ||
            !IsInsideText(addresses.displayQueuedNPCChatter) ||
            !IsInsideText(addresses.queueNPCChatter)) {
            logger::critical("One or more Address Library IDs resolved outside Skyrim .text; refusing hooks");
            return std::nullopt;
        }

        return addresses;
    }

'''
text = text[:resolve_start] + new_resolve + text[resolve_end:]

# Restore the Win64 call ABI protection used by the original plugin.  These
# inline hooks call normal C++ functions and therefore must provide 32 bytes of
# shadow space while preserving volatile argument registers.
old_macros = "#define PUSH_VOLATILE push(rcx); push(rdx); push(r8)\n#define POP_VOLATILE pop(r8); pop(rdx); pop(rcx)"
new_macros = "#define PUSH_VOLATILE push(rcx); push(rdx); push(r8); sub(rsp, 0x20)\n#define POP_VOLATILE add(rsp, 0x20); pop(r8); pop(rdx); pop(rcx)"
if old_macros not in text:
    raise SystemExit("Expected volatile-register macros not found")
text = text.replace(old_macros, new_macros, 1)

# Replace the old weak validation with validation against the exact instruction
# structures observed on the user's Skyrim 1.7.104 runtime.  The control-flow
# destinations are checked too, so a future executable change fails closed.
validate_start = text.index("    bool ValidateSites(const HookAddresses& a)\n")
validate_end_marker = "    }\n}\n\nbool InstallHooks()"
validate_end = text.index(validate_end_marker, validate_start)

new_validate = r'''    bool ValidateSites(const HookAddresses& a)
    {
        const auto base = REL::Module::get().base();

        const auto readRel32Target = [](std::uintptr_t instruction, std::size_t displacementOffset, std::size_t instructionLength) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement,
                reinterpret_cast<const void*>(instruction + displacementOffset),
                sizeof(displacement));
            return static_cast<std::uintptr_t>(
                static_cast<std::intptr_t>(instruction + instructionLength) +
                static_cast<std::intptr_t>(displacement));
        };

        const auto cached = a.cachedResponseCtor + 0xEC;
        const auto queueDialog = a.queueDialogSubtitles + 0x4D;
        const auto displayGeneral = a.displayQueuedNPCChatter + 0x99;
        const auto displayDialog = a.displayQueuedNPCChatter + 0x1CA;
        const auto queueNPC = a.queueNPCChatter + 0x85;

        const std::array sites{
            std::pair{ "CachedResponseData+EC", cached },
            std::pair{ "QueueDialogSubtitles+4D", queueDialog },
            std::pair{ "DisplayQueuedNPCChatter+99", displayGeneral },
            std::pair{ "DisplayQueuedNPCChatter+1CA", displayDialog },
            std::pair{ "QueueNPCChatter+85", queueNPC }
        };

        for (const auto& [name, address] : sites) {
            if (!IsInsideText(address - 4, 24)) {
                logger::critical("{} is outside Skyrim .text", name);
                return false;
            }
            logger::info("{} RVA 0x{:X}: {}", name, address - base, HexBytes(address - 4, 24));
        }

        // CachedResponseData: LEA RCX,[RBX+18h] / CALL StringCache::Ref::Set.
        const auto* cachedBytes = reinterpret_cast<const std::uint8_t*>(cached);
        const auto* cachedPre = reinterpret_cast<const std::uint8_t*>(cached - 4);
        if (!(cachedPre[0] == 0x48 && cachedPre[1] == 0x8D && cachedPre[2] == 0x4B && cachedPre[3] == 0x18 &&
              cachedBytes[0] == 0xE8)) {
            logger::critical("CachedResponseData +0xEC no longer matches LEA RCX,[RBX+18h] + CALL");
            return false;
        }

        // QueueDialogSubtitles: CALL; TEST AL,AL; JE exit.
        const auto* qd = reinterpret_cast<const std::uint8_t*>(queueDialog);
        if (!(qd[0] == 0xE8 && qd[5] == 0x84 && qd[6] == 0xC0 && qd[7] == 0x0F && qd[8] == 0x84)) {
            logger::critical("QueueDialogSubtitles +0x4D instruction structure changed");
            return false;
        }
        const auto queueDialogExit = readRel32Target(queueDialog + 7, 2, 6);
        if (queueDialog + 13 != a.queueDialogSubtitles + 0x5A ||
            queueDialogExit != a.queueDialogSubtitles + 0x103) {
            logger::critical("QueueDialogSubtitles control-flow destinations changed (show RVA 0x{:X}, exit RVA 0x{:X})",
                queueDialog + 13 - base, queueDialogExit - base);
            return false;
        }

        // DisplayQueuedNPCChatter general subtitles:
        // CMP byte ptr [rip+disp32],0; JE dialog-subtitle gate.
        const auto* dg = reinterpret_cast<const std::uint8_t*>(displayGeneral);
        if (!(dg[0] == 0x80 && dg[1] == 0x3D && dg[6] == 0x00 && dg[7] == 0x0F && dg[8] == 0x84)) {
            logger::critical("DisplayQueuedNPCChatter +0x99 instruction structure changed");
            return false;
        }
        const auto displayGeneralExit = readRel32Target(displayGeneral + 7, 2, 6);
        if (displayGeneral + 13 != a.displayQueuedNPCChatter + 0xA6 ||
            displayGeneralExit != a.displayQueuedNPCChatter + 0x1CA) {
            logger::critical("DisplayQueuedNPCChatter general control-flow destinations changed");
            return false;
        }

        // DisplayQueuedNPCChatter dialog subtitles:
        // CMP byte ptr [rip+disp32],0; JE exit (short branch).
        const auto* dd = reinterpret_cast<const std::uint8_t*>(displayDialog);
        if (!(dd[0] == 0x80 && dd[1] == 0x3D && dd[6] == 0x00 && dd[7] == 0x74)) {
            logger::critical("DisplayQueuedNPCChatter +0x1CA instruction structure changed");
            return false;
        }
        const auto displayDialogExit = static_cast<std::uintptr_t>(
            static_cast<std::intptr_t>(displayDialog + 9) +
            static_cast<std::intptr_t>(static_cast<std::int8_t>(dd[8])));
        if (displayDialog + 9 != a.displayQueuedNPCChatter + 0x1D3 ||
            displayDialogExit != a.displayQueuedNPCChatter + 0x1FD) {
            logger::critical("DisplayQueuedNPCChatter dialog control-flow destinations changed");
            return false;
        }

        // QueueNPCChatterData on 1.7.104 retained the +0x85 gate and +0x92 show
        // path, but Bethesda enlarged the skip path.  The JE target moved from
        // upstream +0xCA to +0x186.  Validate the exact gate before patching.
        const auto* qn = reinterpret_cast<const std::uint8_t*>(queueNPC);
        if (!(qn[0] == 0x44 && qn[1] == 0x38 && qn[2] == 0x2D && qn[7] == 0x0F && qn[8] == 0x84)) {
            logger::critical("QueueNPCChatterData +0x85 instruction structure changed");
            return false;
        }
        const auto queueNPCExit = readRel32Target(queueNPC + 7, 2, 6);
        if (queueNPC + 13 != a.queueNPCChatter + 0x92 ||
            queueNPCExit != a.queueNPCChatter + 0x186) {
            logger::critical("QueueNPCChatterData control-flow destinations changed (show RVA 0x{:X}, exit RVA 0x{:X})",
                queueNPC + 13 - base, queueNPCExit - base);
            return false;
        }

        logger::info("All five Skyrim 1.7.104 Fuz hook sites passed structural validation");
        return true;
    }
'''

text = text[:validate_start] + new_validate + "}\n\nbool InstallHooks()" + text[validate_end + len(validate_end_marker):]

# The only confirmed control-flow offset that changed relative to 1.6.1170 is
# QueueNPCChatterData's non-display exit: +0xCA -> +0x186.
old_exit = "            addresses->queueNPCChatter + 0xCA);"
new_exit = "            addresses->queueNPCChatter + 0x186);"
if old_exit not in text:
    raise SystemExit("Expected QueueNPCChatterData old exit not found")
text = text.replace(old_exit, new_exit, 1)

# Give the CachedResponseData helper call its own Win64 shadow space so the
# saved RCX/RDX values cannot be used as home slots by the C++ callee.
old_cached_call = '''                push(rcx);\n                push(rdx);\n                mov(rcx, rbx);\n                mov(rax, reinterpret_cast<std::uintptr_t>(SneakAttackVoicePath));\n                call(rax);\n                pop(rdx);\n                pop(rcx);'''
new_cached_call = '''                push(rcx);\n                push(rdx);\n                sub(rsp, 0x20);\n                mov(rcx, rbx);\n                mov(rax, reinterpret_cast<std::uintptr_t>(SneakAttackVoicePath));\n                call(rax);\n                add(rsp, 0x20);\n                pop(rdx);\n                pop(rcx);'''
if old_cached_call not in text:
    raise SystemExit("Expected CachedResponseData helper call not found")
text = text.replace(old_cached_call, new_cached_call, 1)

path.write_text(text, encoding="utf-8")
print("Applied validated functional Fuz Ro D'oh port for Skyrim 1.7.104")
