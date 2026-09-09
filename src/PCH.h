#pragma once

#define NOMMNOSOUND
#define NOMINMAX

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <SimpleIni.h>
#include <fmt/format.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <xbyak/xbyak.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace logger = SKSE::log;
using namespace std::literals;

#define DLLEXPORT __declspec(dllexport)

#include "Version.h"
