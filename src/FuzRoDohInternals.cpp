#include "FuzRoDohInternals.h"

SubtitleHasher SubtitleHasher::Instance;

namespace
{
    FuzSettings g_settings;
    constexpr auto kIniPath = "Data\\SKSE\\Plugins\\Fuz Ro D'oh.ini";
}

FuzSettings& GetFuzSettings()
{
    return g_settings;
}

void FuzSettings::Load()
{
    CSimpleIniA ini;
    ini.SetUnicode();

    const auto loadResult = ini.LoadFile(kIniPath);
    if (loadResult < 0) {
        logger::info("INI not found; creating {} with defaults", kIniPath);
        ini.SetLongValue("General", "WordsPerSecondSilence", wordsPerSecondSilence,
            "Number of words a second of silent voice can hold");
        ini.SetLongValue("General", "WideCharacterPerWord", wideCharactersPerWord,
            "For CJK and other wide-character languages, how many characters are regarded as one word");
        ini.SetBoolValue("General", "SkipEmptyResponses", skipEmptyResponses,
            "Do not play silent dialogue for empty responses");

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(kIniPath).parent_path(), ec);
        if (const auto saveResult = ini.SaveFile(kIniPath); saveResult < 0) {
            logger::warn("Could not create {} (SimpleIni error {})", kIniPath, saveResult);
        }
        return;
    }

    wordsPerSecondSilence = static_cast<int>(ini.GetLongValue("General", "WordsPerSecondSilence", 2));
    wideCharactersPerWord = static_cast<int>(ini.GetLongValue("General", "WideCharacterPerWord", 3));
    skipEmptyResponses = ini.GetBoolValue("General", "SkipEmptyResponses", true);

    if (wordsPerSecondSilence <= 0) {
        wordsPerSecondSilence = 2;
    }
    if (wideCharactersPerWord <= 0) {
        wideCharactersPerWord = 3;
    }

    logger::info("Settings: WordsPerSecondSilence={}, WideCharacterPerWord={}, SkipEmptyResponses={}",
        wordsPerSecondSilence, wideCharactersPerWord, skipEmptyResponses);
}

std::string MakeSillyName()
{
    return "Fuz Ro D'oh";
}

bool CanShowDialogSubtitles()
{
    const auto* setting = RE::GetINISetting("bDialogueSubtitles:Interface");
    return setting && setting->data.b;
}

bool CanShowGeneralSubtitles()
{
    const auto* setting = RE::GetINISetting("bGeneralSubtitles:Interface");
    return setting && setting->data.b;
}

SubtitleHasher::HashT SubtitleHasher::CalculateHash(const char* a_string)
{
    if (!a_string) {
        return 0;
    }

    HashT hash = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*>(a_string); *p; ++p) {
        hash = ((hash << 5) + hash) + *p;  // djb2
    }
    return hash;
}

void SubtitleHasher::Add(const char* a_subtitle)
{
    if (!a_subtitle || std::strlen(a_subtitle) <= 1) {
        return;
    }

    std::scoped_lock lock(_lock);
    _store.insert(CalculateHash(a_subtitle));
}

bool SubtitleHasher::HasMatch(const char* a_subtitle) const
{
    if (!a_subtitle || !*a_subtitle) {
        return false;
    }

    std::scoped_lock lock(_lock);
    return _store.contains(CalculateHash(a_subtitle));
}

void SubtitleHasher::Tick()
{
    const auto now = std::chrono::steady_clock::now();
    if (now < _nextPurge) {
        return;
    }

    std::scoped_lock lock(_lock);
    if (now >= _nextPurge) {
        _store.clear();
        _nextPurge = now + std::chrono::minutes(1);
        logger::debug("Subtitle hash cache purged");
    }
}
