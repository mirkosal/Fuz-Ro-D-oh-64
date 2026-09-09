#pragma once

#include "PCH.h"

struct FuzSettings
{
    int wordsPerSecondSilence{ 2 };
    int wideCharactersPerWord{ 3 };
    bool skipEmptyResponses{ true };

    void Load();
};

FuzSettings& GetFuzSettings();

class SubtitleHasher
{
public:
    using HashT = std::uint32_t;

    void Add(const char* a_subtitle);
    [[nodiscard]] bool HasMatch(const char* a_subtitle) const;
    void Tick();

    static SubtitleHasher Instance;

private:
    static HashT CalculateHash(const char* a_string);

    mutable std::mutex _lock;
    std::unordered_set<HashT> _store;
    std::chrono::steady_clock::time_point _nextPurge{ std::chrono::steady_clock::now() + std::chrono::minutes(1) };
};

// Runtime view used by the dialogue-response hook.  The layout matches the
// 1.6.1170 object used by upstream and is validated before any hook is applied
// on the 1.7.104 test port.
struct CachedResponseData
{
    RE::BSString responseText;               // 00
    std::uint32_t emotionType;               // 10
    std::uint32_t emotionLevel;              // 14
    RE::BSFixedString voiceFilePath;          // 18
    std::uint64_t unk20;                     // 20
    std::uint64_t unk28;                     // 28
    std::uint64_t unk30;                     // 30
    std::uint8_t useEmotionAnim;             // 38
    std::uint8_t hasLipFile;                 // 39
    std::uint8_t pad3A[6];                   // 3A
};
static_assert(sizeof(CachedResponseData) == 0x40);

struct NPCChatterData
{
    std::uint32_t speaker;                   // 00
    std::uint32_t pad04;                     // 04
    RE::BSString title;                      // 08
    float subtitleDistance;                  // 18
    bool forceSubtitles;                     // 1C
    std::uint8_t pad1D[3];                   // 1D
};
static_assert(sizeof(NPCChatterData) == 0x20);

[[nodiscard]] std::string MakeSillyName();
[[nodiscard]] bool CanShowDialogSubtitles();
[[nodiscard]] bool CanShowGeneralSubtitles();
