from pathlib import Path

path = Path("src/Hooks.cpp")
text = path.read_text(encoding="utf-8")

old_decl = """        std::uintptr_t found = 0;\n        std::size_t matches = 0;\n"""
new_decl = """        std::vector<std::uintptr_t> candidates;\n"""

old_match = """            if (match) {\n                found = text.address() + i;\n                ++matches;\n                if (matches > 1) {\n                    break;\n                }\n            }\n"""
new_match = """            if (match) {\n                candidates.push_back(text.address() + i);\n            }\n"""

old_result = """        if (matches != 1) {\n            logger::error(\"{}: expected one signature match, found {}\", a_name, matches);\n            return std::nullopt;\n        }\n\n        logger::info(\"{} resolved at Skyrim RVA 0x{:X}\", a_name, found - module.base());\n        return found;\n"""
new_result = """        if (candidates.size() == 1) {\n            const auto found = candidates.front();\n            logger::info(\"{} resolved at Skyrim RVA 0x{:X}\", a_name, found - module.base());\n            return found;\n        }\n\n        // Skyrim 1.7.104 contains a second byte-pattern match for the historical\n        // CachedResponseData signature.  Keep the original hook invariant as an\n        // additional discriminator: Fuz Ro D'oh has always replaced a 5-byte CALL\n        // at function +0xEC.  We only accept a candidate when that invariant makes\n        // the result unique; otherwise we still refuse to patch anything.\n        if (a_name == \"CachedResponseData ctor\" && !candidates.empty()) {\n            std::vector<std::uintptr_t> valid;\n            const auto textBegin = text.address();\n            const auto textEnd = textBegin + text.size();\n\n            for (const auto candidate : candidates) {\n                const auto hookSite = candidate + 0xEC;\n                const bool inText = hookSite >= textBegin && hookSite < textEnd;\n                const int opcode = inText ? *reinterpret_cast<const std::uint8_t*>(hookSite) : -1;\n\n                if (opcode >= 0) {\n                    logger::info(\n                        \"{} candidate RVA 0x{:X}: +0xEC opcode 0x{:02X}\",\n                        a_name, candidate - module.base(), static_cast<unsigned>(opcode));\n                } else {\n                    logger::info(\"{} candidate RVA 0x{:X}: +0xEC outside .text\",\n                        a_name, candidate - module.base());\n                }\n\n                if (opcode == 0xE8) {\n                    valid.push_back(candidate);\n                }\n            }\n\n            if (valid.size() == 1) {\n                const auto found = valid.front();\n                logger::info(\n                    \"{} disambiguated by +0xEC CALL invariant -> Skyrim RVA 0x{:X}\",\n                    a_name, found - module.base());\n                return found;\n            }\n\n            logger::error(\n                \"{}: {} signature matches, but {} satisfy the +0xEC CALL invariant\",\n                a_name, candidates.size(), valid.size());\n            return std::nullopt;\n        }\n\n        logger::error(\"{}: expected one signature match, found {}\", a_name, candidates.size());\n        return std::nullopt;\n"""

for old, new, name in [
    (old_decl, new_decl, "declaration"),
    (old_match, new_match, "match block"),
    (old_result, new_result, "result block"),
]:
    if old not in text:
        raise SystemExit(f"Expected {name} not found in src/Hooks.cpp")
    text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8")
print("Patched CachedResponseData signature disambiguation for Skyrim 1.7.104")
