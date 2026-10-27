#include "PCH.h"

#include <algorithm>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

void SetupLog()
{
    auto logsFolder = logger::log_directory();
    if (!logsFolder) {
        util::report_and_fail("SKSE log_directory not provided, logs can't be written");
    }

    const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    const auto logName = plugin ? std::string{ plugin->GetName() } + ".log" : "Plugin.log";
    auto logPath = *logsFolder / logName;

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
    std::vector<spdlog::sink_ptr> sinks{ fileSink };
    if (IsDebuggerPresent()) {
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    }

    auto spdlogger = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
    spdlog::set_default_logger(std::move(spdlogger));
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] [%s:%#] %v");
#ifdef NDEBUG
    spdlog::set_level(spdlog::level::info);
#else
    spdlog::set_level(spdlog::level::trace);
#endif
    spdlog::flush_on(spdlog::level::info);
}

// Simple settings: read at Skyrim startup from the INI next to the DLL.
struct DisplaySettings
{
    bool showEditorID{ true };
    bool showFormID{ true };
    bool showStage{ false };
    int positionX{ 505 };
    int positionY{ 0 };
    int fontSize{ 13 };
    bool enableCopy{ true };
    int copyEditorKey{ 'C' };
    int copyFormKey{ 'F' };
    // Step 15: inspector panel position, dimensions, and typography.
    int inspectorX{ 250 };
    int inspectorY{ 150 };
    int inspectorWidth{ 500 };
    int inspectorHeight{ 330 };
    int inspectorFontSize{ 15 };
    bool inspectorCompactPages{ true };
    int inspectorToggleKey{ 'I' };
    int inspectorNextKey{ 'N' };
    int inspectorPrevKey{ 'P' };
    // Step 17: navigate the inspector without moving the journal quest selection.
    int inspectorSelectNextKey{ 'J' };
    int inspectorSelectPrevKey{ 'K' };
    // Step 18: toggle list and detail view (read-only).
    int inspectorDetailsKey{ 'D' };
    // Step 21: copy a stage number or console command (never execute it).
    int inspectorCopyStageKey{ 'S' };
    int inspectorCopyCommandKey{ 'G' };
    // Step 22: separate request and confirmation. DISABLED by default for safety.
    bool enableStageExecution{ false };
    int inspectorRequestExecutionKey{ 'V' };
    int inspectorConfirmExecutionKey{ 'R' };
};

inline DisplaySettings g_displaySettings{};

// Step 23: the Papyrus bridge only writes to the INI.
// Active settings are reloaded on the journal UI thread to avoid data races.
inline std::atomic_bool g_qijMcmSettingsDirty{ false };


// Allow A-Z letters and 0-9 digits as primary hotkeys.
// Ctrl+Shift modifiers remain fixed in this version.
int ReadCopyKey(const std::filesystem::path& a_iniPath,
                const wchar_t* a_setting,
                const wchar_t* a_default,
                int a_fallback)
{
    wchar_t buffer[32]{};
    GetPrivateProfileStringW(L"Hotkeys", a_setting, a_default,
                             buffer, 32, a_iniPath.c_str());

    if (buffer[0] == L'\0' || buffer[1] != L'\0') {
        logger::warn("INI: invalid hotkey; using default");
        return a_fallback;
    }

    wchar_t key = buffer[0];
    if (key >= L'a' && key <= L'z') {
        key -= (L'a' - L'A');
    }

    if ((key >= L'A' && key <= L'Z') ||
        (key >= L'0' && key <= L'9')) {
        return static_cast<int>(key);
    }

    logger::warn("INI: unsupported hotkey; using default");
    return a_fallback;
}

void LoadDisplaySettings()
{
    // Find our loaded DLL and look for its INI in the same directory.
    const auto module = GetModuleHandleW(L"questidsinjournal.dll");
    if (!module) {
        logger::warn("INI: plugin DLL not found; using defaults");
        return;
    }

    wchar_t modulePath[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        logger::warn("INI: unable to resolve DLL path; using defaults");
        return;
    }

    std::filesystem::path iniPath(modulePath);
    iniPath.replace_extension(L".ini");

    std::error_code ec;
    if (!std::filesystem::exists(iniPath, ec)) {
        logger::warn("INI: file not found next to DLL; using default settings");
        return;
    }

    // Values are 1 (show) and 0 (hide).
    g_displaySettings.showEditorID =
        GetPrivateProfileIntW(L"Display", L"ShowEditorID", 1, iniPath.c_str()) != 0;
    g_displaySettings.showFormID =
        GetPrivateProfileIntW(L"Display", L"ShowFormID", 1, iniPath.c_str()) != 0;

    g_displaySettings.showStage =
        GetPrivateProfileIntW(L"Display", L"ShowStage", 0, iniPath.c_str()) != 0;

    // QuestsPage-local coordinates: clamp to sensible ranges.
    // Use integer INI values to avoid locale-dependent decimal formatting.
    g_displaySettings.positionX = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Display", L"PositionX", 505, iniPath.c_str())),
        0, 650);
    g_displaySettings.positionY = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Display", L"PositionY", 0, iniPath.c_str())),
        0, 150);
    g_displaySettings.fontSize = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Display", L"FontSize", 13, iniPath.c_str())),
        10, 16);

    // Configure primary keys for Ctrl+Shift+key hotkeys.
    g_displaySettings.enableCopy =
        GetPrivateProfileIntW(L"Hotkeys", L"EnableCopy", 1, iniPath.c_str()) != 0;
    g_displaySettings.copyEditorKey = ReadCopyKey(iniPath, L"EditorKey", L"C", 'C');
    g_displaySettings.copyFormKey = ReadCopyKey(iniPath, L"FormKey", L"F", 'F');

    if (g_displaySettings.copyEditorKey == g_displaySettings.copyFormKey) {
        logger::warn("INI: EditorKey and FormKey conflict; restoring C/F");
        g_displaySettings.copyEditorKey = 'C';
        g_displaySettings.copyFormKey = 'F';
    }

    // Optional inspector settings: these do not modify save-game data.
    // Coordinates are relative to QuestsPage, not screen pixels.
    g_displaySettings.inspectorX = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Inspector", L"PositionX", 250, iniPath.c_str())),
        0, 800);
    g_displaySettings.inspectorY = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Inspector", L"PositionY", 150, iniPath.c_str())),
        0, 550);
    g_displaySettings.inspectorWidth = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Inspector", L"Width", 500, iniPath.c_str())),
        260, 750);
    g_displaySettings.inspectorHeight = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Inspector", L"Height", 330, iniPath.c_str())),
        300, 600);
    g_displaySettings.inspectorFontSize = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Inspector", L"FontSize", 15, iniPath.c_str())),
        11, 20);
    g_displaySettings.inspectorCompactPages =
        GetPrivateProfileIntW(L"Inspector", L"CompactPages", 1, iniPath.c_str()) != 0;

    g_displaySettings.inspectorToggleKey = ReadCopyKey(iniPath, L"InspectorToggleKey", L"I", 'I');
    g_displaySettings.inspectorNextKey = ReadCopyKey(iniPath, L"InspectorNextKey", L"N", 'N');
    g_displaySettings.inspectorPrevKey = ReadCopyKey(iniPath, L"InspectorPrevKey", L"P", 'P');
    g_displaySettings.inspectorSelectNextKey = ReadCopyKey(iniPath, L"InspectorSelectNextKey", L"J", 'J');
    g_displaySettings.inspectorSelectPrevKey = ReadCopyKey(iniPath, L"InspectorSelectPrevKey", L"K", 'K');
    g_displaySettings.inspectorDetailsKey = ReadCopyKey(iniPath, L"InspectorDetailsKey", L"D", 'D');
    g_displaySettings.inspectorCopyStageKey = ReadCopyKey(iniPath, L"InspectorCopyStageKey", L"S", 'S');
    g_displaySettings.inspectorCopyCommandKey = ReadCopyKey(iniPath, L"InspectorCopyCommandKey", L"G", 'G');
    g_displaySettings.inspectorRequestExecutionKey = ReadCopyKey(
        iniPath, L"InspectorRequestExecutionKey", L"V", 'V');
    g_displaySettings.inspectorConfirmExecutionKey = ReadCopyKey(
        iniPath, L"InspectorConfirmExecutionKey", L"R", 'R');
    g_displaySettings.enableStageExecution =
        GetPrivateProfileIntW(L"Execution", L"EnableStageExecution", 0, iniPath.c_str()) != 0;

    // Prevent inspector hotkeys from accidentally triggering a second command.
    const std::array<int, 12> keys{
        g_displaySettings.copyEditorKey, g_displaySettings.copyFormKey,
        g_displaySettings.inspectorToggleKey, g_displaySettings.inspectorNextKey,
        g_displaySettings.inspectorPrevKey, g_displaySettings.inspectorSelectNextKey,
        g_displaySettings.inspectorSelectPrevKey, g_displaySettings.inspectorDetailsKey,
        g_displaySettings.inspectorCopyStageKey, g_displaySettings.inspectorCopyCommandKey,
        g_displaySettings.inspectorRequestExecutionKey, g_displaySettings.inspectorConfirmExecutionKey
    };
    bool conflict = false;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            if (keys[i] == keys[j]) { conflict = true; }
        }
    }
    if (conflict) {
        logger::warn("INI: duplicate Ctrl+Shift hotkeys; restoring C/F/I/N/P/J/K/D/S/G/V/R");
        g_displaySettings.copyEditorKey = 'C';
        g_displaySettings.copyFormKey = 'F';
        g_displaySettings.inspectorToggleKey = 'I';
        g_displaySettings.inspectorNextKey = 'N';
        g_displaySettings.inspectorPrevKey = 'P';
        g_displaySettings.inspectorSelectNextKey = 'J';
        g_displaySettings.inspectorSelectPrevKey = 'K';
        g_displaySettings.inspectorDetailsKey = 'D';
        g_displaySettings.inspectorCopyStageKey = 'S';
        g_displaySettings.inspectorCopyCommandKey = 'G';
        g_displaySettings.inspectorRequestExecutionKey = 'V';
        g_displaySettings.inspectorConfirmExecutionKey = 'R';
    }
    logger::info("STEP15 INI inspector: X={} | Y={} | Width={} | Height={} | FontSize={} | CompactPages={}",
        g_displaySettings.inspectorX, g_displaySettings.inspectorY,
        g_displaySettings.inspectorWidth, g_displaySettings.inspectorHeight,
        g_displaySettings.inspectorFontSize, g_displaySettings.inspectorCompactPages ? 1 : 0);
    logger::info("STEP17 INI inspector: Ctrl+Shift+{} toggle | {}/{} pages | {}/{} select", 
        static_cast<char>(g_displaySettings.inspectorToggleKey),
        static_cast<char>(g_displaySettings.inspectorNextKey),
        static_cast<char>(g_displaySettings.inspectorPrevKey),
        static_cast<char>(g_displaySettings.inspectorSelectNextKey),
        static_cast<char>(g_displaySettings.inspectorSelectPrevKey));

    logger::info("STEP18 INI: Ctrl+Shift+{} toggles selected stage details",
        static_cast<char>(g_displaySettings.inspectorDetailsKey));
    logger::info("STEP21 INI: Ctrl+Shift+{} copies stage | Ctrl+Shift+{} copies setstage command | EnableCopy={}",
        static_cast<char>(g_displaySettings.inspectorCopyStageKey),
        static_cast<char>(g_displaySettings.inspectorCopyCommandKey),
        g_displaySettings.enableCopy ? 1 : 0);

    logger::warn("STEP22.1 EXECUTION: EnableStageExecution={} | request Ctrl+Shift+{} | confirm Ctrl+Shift+{} | warnings at 10/30s, NEVER unlock on timeout",
        g_displaySettings.enableStageExecution ? 1 : 0,
        static_cast<char>(g_displaySettings.inspectorRequestExecutionKey),
        static_cast<char>(g_displaySettings.inspectorConfirmExecutionKey));

    logger::info("INI: ShowEditorID={} | ShowFormID={} | ShowStage={} | PositionX={} | PositionY={} | FontSize={} | EnableCopy={}",
        g_displaySettings.showEditorID ? 1 : 0,
        g_displaySettings.showFormID ? 1 : 0,
        g_displaySettings.showStage ? 1 : 0,
        g_displaySettings.positionX,
        g_displaySettings.positionY,
        g_displaySettings.fontSize,
        g_displaySettings.enableCopy ? 1 : 0);
    logger::info("INI: hotkeys Ctrl+Shift+{} (EditorID) and Ctrl+Shift+{} (FormID)",
        static_cast<char>(g_displaySettings.copyEditorKey),
        static_cast<char>(g_displaySettings.copyFormKey));
}

// ============================================================================
// STEP 23 - SKYUI MCM (QIJ_MCMBridge.psc)
// These functions NEVER execute stages: they only read/write validated
// INI settings. Execution still requires the two-step request/confirmation
// with configurable hotkeys and the pending-operation lock from Step 22.1.
// ============================================================================

struct QIJIniSetting
{
    int id;
    const wchar_t* section;
    const wchar_t* key;
    int defaultValue;
    int minimum;
    int maximum;
};

// No arbitrary keys from Papyrus: whitelist setting IDs and value ranges.
constexpr std::array<QIJIniSetting, 14> kQIJMcmSettings{{
    { 1,  L"Display",   L"ShowEditorID",         1,   0,   1 },
    { 2,  L"Display",   L"ShowFormID",           1,   0,   1 },
    { 3,  L"Display",   L"ShowStage",            1,   0,   1 },
    { 4,  L"Display",   L"PositionX",          505,   0, 650 },
    { 5,  L"Display",   L"PositionY",            0,   0, 150 },
    { 6,  L"Display",   L"FontSize",            13,  10,  16 },
    { 7,  L"Inspector", L"PositionX",          250,   0, 800 },
    { 8,  L"Inspector", L"PositionY",          150,   0, 550 },
    { 9,  L"Inspector", L"Width",              500, 260, 750 },
    { 10, L"Inspector", L"Height",             330, 300, 600 },
    { 11, L"Inspector", L"FontSize",            15,  11,  20 },
    { 12, L"Inspector", L"CompactPages",         1,   0,   1 },
    { 13, L"Hotkeys",   L"EnableCopy",           1,   0,   1 },
    { 14, L"Execution", L"EnableStageExecution", 0,   0,   1 }
}};

// STEP 24: SkyUI returns DirectInput key codes (DIK), while GetAsyncKeyState
// uses Windows virtual-key codes (VK). Conversion is REQUIRED.
// Only A-Z and 0-9 are supported; Ctrl+Shift is fixed.
struct QIJHotkeySetting
{
    int id;
    const wchar_t* key;
    wchar_t defaultVK;
};
constexpr std::array<QIJHotkeySetting, 12> kQIJHotkeys{{
    { 1,  L"EditorKey",                  L'C' },
    { 2,  L"FormKey",                    L'F' },
    { 3,  L"InspectorToggleKey",         L'I' },
    { 4,  L"InspectorNextKey",           L'N' },
    { 5,  L"InspectorPrevKey",           L'P' },
    { 6,  L"InspectorSelectNextKey",     L'J' },
    { 7,  L"InspectorSelectPrevKey",     L'K' },
    { 8,  L"InspectorDetailsKey",        L'D' },
    { 9,  L"InspectorCopyStageKey",      L'S' },
    { 10, L"InspectorCopyCommandKey",    L'G' },
    { 11, L"InspectorRequestExecutionKey", L'V' },
    { 12, L"InspectorConfirmExecutionKey", L'R' }
}};

// Physical DIK codes for QWERTY keyboard keys, as used by SkyUI.
constexpr std::array<std::pair<int, int>, 36> kQIJDIKVirtualKeys{{
    { 30, 'A' }, { 48, 'B' }, { 46, 'C' }, { 32, 'D' }, { 18, 'E' },
    { 33, 'F' }, { 34, 'G' }, { 35, 'H' }, { 23, 'I' }, { 36, 'J' },
    { 37, 'K' }, { 38, 'L' }, { 50, 'M' }, { 49, 'N' }, { 24, 'O' },
    { 25, 'P' }, { 16, 'Q' }, { 19, 'R' }, { 31, 'S' }, { 20, 'T' },
    { 22, 'U' }, { 47, 'V' }, { 17, 'W' }, { 45, 'X' }, { 21, 'Y' },
    { 44, 'Z' },
    { 11, '0' }, { 2, '1' }, { 3, '2' }, { 4, '3' }, { 5, '4' },
    { 6, '5' }, { 7, '6' }, { 8, '7' }, { 9, '8' }, { 10, '9' }
}};

int QIJDIKToVK(int a_dik)
{
    for (const auto& [dik, vk] : kQIJDIKVirtualKeys) {
        if (dik == a_dik) { return vk; }
    }
    return 0;
}

int QIJVKToDIK(int a_vk)
{
    for (const auto& [dik, vk] : kQIJDIKVirtualKeys) {
        if (vk == a_vk) { return dik; }
    }
    return 0;
}

const QIJHotkeySetting* QIJHotkeyLookup(int a_id)
{
    for (const auto& setting : kQIJHotkeys) {
        if (setting.id == a_id) { return std::addressof(setting); }
    }
    return nullptr;
}

// Return uppercase VK; accept lowercase letters from older INI versions.
int QIJReadHotkeyVK(const std::filesystem::path& a_path, const QIJHotkeySetting& a_setting)
{
    wchar_t fallback[2]{ a_setting.defaultVK, 0 };
    wchar_t text[16]{};
    GetPrivateProfileStringW(L"Hotkeys", a_setting.key, fallback,
                             text, static_cast<DWORD>(std::size(text)), a_path.c_str());
    if (!text[0] || text[1]) { return a_setting.defaultVK; }
    wchar_t vk = text[0];
    if (vk >= L'a' && vk <= L'z') { vk = static_cast<wchar_t>(vk - L'a' + L'A'); }
    return QIJVKToDIK(static_cast<int>(vk)) != 0 ? vk : a_setting.defaultVK;
}

std::filesystem::path QIJMcmIniPath()
{
    const auto module = GetModuleHandleW(L"questidsinjournal.dll");
    if (!module) { return {}; }
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, modulePath, MAX_PATH);
    if (!length || length >= MAX_PATH) { return {}; }
    std::filesystem::path path(modulePath);
    path.replace_extension(L".ini");
    return path;
}

const QIJIniSetting* QIJMcmLookup(int a_id)
{
    for (const auto& setting : kQIJMcmSettings) {
        if (setting.id == a_id) { return std::addressof(setting); }
    }
    return nullptr;
}

std::int32_t QIJMcmGetValue(RE::StaticFunctionTag*, std::int32_t a_id)
{
    const auto* setting = QIJMcmLookup(a_id);
    if (!setting) { return 0; }
    const auto path = QIJMcmIniPath();
    if (path.empty()) { return setting->defaultValue; }
    const auto stored = static_cast<int>(GetPrivateProfileIntW(
        setting->section, setting->key, setting->defaultValue, path.c_str()));
    return std::clamp(stored, setting->minimum, setting->maximum);
}

bool QIJMcmSetValue(RE::StaticFunctionTag*, std::int32_t a_id, std::int32_t a_value)
{
    const auto* setting = QIJMcmLookup(a_id);
    if (!setting || a_value < setting->minimum || a_value > setting->maximum) {
        logger::warn("STEP23 MCM: invalid ID or value: id={} value={}", a_id, a_value);
        return false;
    }
    const auto path = QIJMcmIniPath();
    if (path.empty()) {
        logger::error("STEP23 MCM: unable to find INI directory");
        return false;
    }
    const auto value = std::to_wstring(a_value);
    if (!WritePrivateProfileStringW(setting->section, setting->key,
                                    value.c_str(), path.c_str())) {
        logger::error("STEP23 MCM: failed writing setting id={} to INI", a_id);
        return false;
    }
    g_qijMcmSettingsDirty.store(true, std::memory_order_release);
    logger::info("STEP23 MCM CHANGE: id={} value={} saved to INI", a_id, a_value);
    return true;
}

// Global native functions for MCM. The INI remains the single source
// of truth, reloaded during JournalMenu::AdvanceMovie.
std::int32_t QIJMcmGetHotkey(RE::StaticFunctionTag*, std::int32_t a_id)
{
    const auto* setting = QIJHotkeyLookup(a_id);
    if (!setting) { return 0; }
    const auto path = QIJMcmIniPath();
    const auto vk = path.empty() ? setting->defaultVK : QIJReadHotkeyVK(path, *setting);
    return QIJVKToDIK(vk);
}

bool QIJMcmSetHotkey(RE::StaticFunctionTag*, std::int32_t a_id, std::int32_t a_dik)
{
    const auto* setting = QIJHotkeyLookup(a_id);
    const auto vk = QIJDIKToVK(a_dik);
    if (!setting || !vk) {
        logger::warn("STEP24 MCM HOTKEY REJECTED: id={} DIK={} (A-Z / 0-9 only)", a_id, a_dik);
        return false;
    }
    const auto path = QIJMcmIniPath();
    if (path.empty()) { return false; }
    for (const auto& other : kQIJHotkeys) {
        if (other.id == a_id) { continue; }
        if (QIJReadHotkeyVK(path, other) == vk) {
            logger::warn("STEP24 MCM DUPLICATE HOTKEY: id={} conflictsWith={} key={}",
                         a_id, other.id, static_cast<char>(vk));
            return false;
        }
    }
    const wchar_t value[2]{ static_cast<wchar_t>(vk), 0 };
    if (!WritePrivateProfileStringW(L"Hotkeys", setting->key, value, path.c_str())) {
        logger::error("STEP24 MCM HOTKEY: unable to write INI");
        return false;
    }
    g_qijMcmSettingsDirty.store(true, std::memory_order_release);
    logger::info("STEP24 MCM HOTKEY: id={} DIK={} key={} | Ctrl+Shift+{} saved to INI",
                 a_id, a_dik, static_cast<char>(vk), static_cast<char>(vk));
    return true;
}

bool QIJMcmIsReady(RE::StaticFunctionTag*)
{
    return !QIJMcmIniPath().empty();
}

bool RegisterQIJMcmPapyrus(RE::BSScript::IVirtualMachine* a_vm)
{
    if (!a_vm) { return false; }
    a_vm->RegisterFunction("GetValue", "QIJ_MCMBridge", QIJMcmGetValue);
    a_vm->RegisterFunction("SetValue", "QIJ_MCMBridge", QIJMcmSetValue);
    a_vm->RegisterFunction("IsReady", "QIJ_MCMBridge", QIJMcmIsReady);
    a_vm->RegisterFunction("GetHotkey", "QIJ_MCMBridge", QIJMcmGetHotkey);
    a_vm->RegisterFunction("SetHotkey", "QIJ_MCMBridge", QIJMcmSetHotkey);
    logger::info("STEP23 MCM: native Papyrus functions registered");
    return true;
}

// Copy UTF-8 text to the Windows clipboard as Unicode.
// Does not change quest state or save-game data.
bool CopyToClipboard(std::string_view a_text)
{
    if (a_text.empty()) {
        return false;
    }

    const auto* bytes = a_text.data();
    const auto byteCount = static_cast<int>(a_text.size());
    const int charCount = MultiByteToWideChar(CP_UTF8, 0, bytes, byteCount, nullptr, 0);
    if (charCount <= 0) {
        return false;
    }

    std::wstring wideText(static_cast<std::size_t>(charCount) + 1, L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, bytes, byteCount, wideText.data(), charCount) != charCount) {
        return false;
    }

    const auto memoryBytes = wideText.size() * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, memoryBytes);
    if (!memory) {
        return false;
    }

    void* dest = GlobalLock(memory);
    if (!dest) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(dest, wideText.data(), memoryBytes);
    GlobalUnlock(memory);

    HWND foreground = GetForegroundWindow();
    DWORD processID = 0;
    if (foreground) {
        GetWindowThreadProcessId(foreground, &processID);
    }
    // Never modify the clipboard while another application is focused.
    if (!foreground || processID != GetCurrentProcessId()) {
        GlobalFree(memory);
        return false;
    }

    if (!OpenClipboard(foreground)) {
        GlobalFree(memory);
        return false;
    }

    bool success = false;
    if (EmptyClipboard()) {
        if (SetClipboardData(CF_UNICODETEXT, memory)) {
            // Windows takes ownership of the memory block after SetClipboardData.
            memory = nullptr;
            success = true;
        }
    }
    CloseClipboard();
    if (memory) {
        GlobalFree(memory);
    }
    return success;
}

// Step 14: initial on-demand, read-only inspector.
// Papyrus callbacks can arrive asynchronously: guard shared state
// and never manipulate Scaleform outside the menu thread.
enum class StageQueryStatus : std::uint8_t { kPending, kExecuted, kNotExecuted, kFailed };
struct InspectorStageEntry
{
    std::uint16_t index{ 0 };
    StageQueryStatus status{ StageQueryStatus::kPending };
};
// QOBJ objectives belong to the QUEST; they are not tied to a specific stage.
struct InspectorQuestObjective
{
    std::uint16_t index{ 0 };
    std::string text;
};
struct InspectorQueryState
{
    std::uint32_t formID{ 0 };
    std::string editorID;
    std::size_t objectiveCount{ 0 };
    std::vector<InspectorQuestObjective> objectivePreview;
    std::uint16_t currentStage{ 0 };
    std::vector<InspectorStageEntry> stages;
    std::size_t received{ 0 };
    bool truncated{ false };
    bool vmUnavailable{ false };
    std::uint64_t generation{ 0 };
    std::uint64_t revision{ 0 };
};
inline std::mutex g_inspectorMutex;
inline InspectorQueryState g_inspectorState{};
inline std::uint64_t g_inspectorGeneration{ 0 }; // Protected by g_inspectorMutex.
inline std::uint64_t g_inspectorRevision{ 0 };   // Protected by g_inspectorMutex.
inline bool g_inspectorVisible{ false };         // Menu thread only.
inline std::size_t g_inspectorPage{ 0 };         // Menu thread only.
inline std::size_t g_inspectorSelectedPos{ 0 };  // Step 17: selected stage position. Menu thread only.
inline bool g_inspectorDetailsView{ false };       // Step 18: detail/list view. Menu thread only.
// Step 21: temporary feedback state, menu thread only.
inline std::string g_inspectorCopyFeedback{};
inline bool g_inspectorCopyFeedbackSuccess{ false };
inline std::chrono::steady_clock::time_point g_inspectorCopyFeedbackExpires{};
inline std::uint64_t g_inspectorUiRevision{ 0 };

// Menu-thread-only state. Confirmation never persists between journal opens.
// The time window requires two separate key presses and revalidates at dispatch.
struct StageExecutionArm
{
    bool active{ false };
    std::uint32_t formID{ 0 };
    std::uint16_t stage{ 0 };
    std::uint16_t originalCurrentStage{ 0 };
    std::uint64_t queryGeneration{ 0 };
    std::chrono::steady_clock::time_point expires{};
};
inline StageExecutionArm g_stageExecutionArm{};

// STEP 22.1: SetCurrentStageID is LATENT. The requested stage may apply
// before Papyrus fragments complete or before the callback arrives.
// Never unlock an execution due to a timeout or observed stage progress.
// requestID prevents accepting responses from another invocation.
struct StageExecutionResult
{
    bool inFlight{ false };
    bool resultReady{ false };
    bool returnedBool{ false };
    bool success{ false };
    bool slowWarningLogged{ false };
    bool prolongedWarningLogged{ false };
    bool stageProgressObserved{ false };
    bool questCompletedObserved{ false };
    std::uint32_t formID{ 0 };
    std::uint16_t stage{ 0 };
    std::uint64_t requestID{ 0 };
    std::chrono::steady_clock::time_point startedAt{};
};
inline std::mutex g_stageExecutionMutex;
inline StageExecutionResult g_stageExecutionResult{};
inline std::uint64_t g_nextStageExecutionRequestID{ 0 }; // Protected by g_stageExecutionMutex.

class QIJStageExecutionCallback final : public RE::BSScript::IStackCallbackFunctor
{
public:
    QIJStageExecutionCallback(std::uint32_t a_formID, std::uint16_t a_stage,
        std::uint64_t a_requestID) :
        formID(a_formID), stage(a_stage), requestID(a_requestID) {}

    void operator()(RE::BSScript::Variable a_result) override
    {
        const bool isBool = a_result.IsBool();
        const bool ok = isBool && a_result.GetBool();
        std::int64_t elapsedSeconds = 0;
        {
            std::lock_guard lock(g_stageExecutionMutex);
            if (!g_stageExecutionResult.inFlight ||
                g_stageExecutionResult.requestID != requestID ||
                g_stageExecutionResult.formID != formID ||
                g_stageExecutionResult.stage != stage ||
                g_stageExecutionResult.resultReady) {
                return;
            }
            elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - g_stageExecutionResult.startedAt).count();
            g_stageExecutionResult.returnedBool = isBool;
            g_stageExecutionResult.success = ok;
            g_stageExecutionResult.resultReady = true;
        }
        logger::warn("STEP22.1 PAPYRUS RESULT: Request={} | FormID={:08X} | Stage={} | IsBool={} | OK={} | Seconds={}",
            requestID, formID, stage, isBool ? 1 : 0, ok ? 1 : 0, elapsedSeconds);
    }

    void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

private:
    std::uint32_t formID;
    std::uint16_t stage;
    std::uint64_t requestID;
};

void SetInspectorCopyFeedback(std::string a_text, bool a_success)
{
    g_inspectorCopyFeedback = std::move(a_text);
    g_inspectorCopyFeedbackSuccess = a_success;
    g_inspectorCopyFeedbackExpires = std::chrono::steady_clock::now() + std::chrono::milliseconds(1800);
    ++g_inspectorUiRevision;
}
inline bool g_inspectorRefreshPending{ false };
inline std::uint32_t g_inspectorPendingFormID{ 0 };
inline std::chrono::steady_clock::time_point g_inspectorPendingSince{};

void ScheduleInspectorRefresh(std::uint32_t a_formID)
{
    if (!g_inspectorVisible) { return; }
    // Hovering over another quest invalidates pending confirmation.
    if (g_stageExecutionArm.active) {
        g_stageExecutionArm = {};
        SetInspectorCopyFeedback("Execution canceled: quest selection changed", false);
    }
    g_inspectorPendingFormID = a_formID;
    g_inspectorRefreshPending = true;
    g_inspectorPendingSince = std::chrono::steady_clock::now();
}

void ClearInspectorQuery()
{
    std::lock_guard lock(g_inspectorMutex);
    ++g_inspectorGeneration;
    g_inspectorState = {};
    g_inspectorState.generation = g_inspectorGeneration;
    g_inspectorState.revision = ++g_inspectorRevision;
}

class QIJInspectorCallback final : public RE::BSScript::IStackCallbackFunctor
{
public:
    QIJInspectorCallback(std::uint32_t a_formID, std::uint16_t a_stage,
                        std::uint64_t a_generation) :
        formID(a_formID), stage(a_stage), generation(a_generation) {}

    void operator()(RE::BSScript::Variable a_result) override
    {
        // Update only the result of the query that is still current.
        // Ignore responses for previous quests or a closed inspector.
        {
            std::lock_guard lock(g_inspectorMutex);
            if (g_inspectorState.generation != generation ||
                g_inspectorState.formID != formID) {
                return;
            }
            for (auto& entry : g_inspectorState.stages) {
                if (entry.index == stage && entry.status == StageQueryStatus::kPending) {
                    entry.status = !a_result.IsBool() ? StageQueryStatus::kFailed :
                        (a_result.GetBool() ? StageQueryStatus::kExecuted : StageQueryStatus::kNotExecuted);
                    ++g_inspectorState.received;
                    g_inspectorState.revision = ++g_inspectorRevision;
                    break;
                }
            }
        }
        if (a_result.IsBool()) {
            logger::info("STEP14 IsStageDone: FormID={:08X} | Stage={} | Done={}",
                formID, stage, a_result.GetBool() ? 1 : 0);
        } else {
            logger::warn("STEP14 IsStageDone: FormID={:08X} | Stage={} | non-boolean response",
                formID, stage);
        }
    }

    void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

private:
    std::uint32_t formID;
    std::uint16_t stage;
    std::uint64_t generation;
};

std::vector<std::uint16_t> CollectQuestStages(RE::TESQuest* a_quest, bool& a_truncated)
{
    a_truncated = false;
    std::vector<std::uint16_t> indices;
    if (!a_quest) {
        return indices;
    }
    constexpr std::size_t kMaxListEntries = 512;
    if (a_quest->executedStages) {
        std::size_t n = 0;
        for (const auto& stage : *a_quest->executedStages) {
            if (n++ >= kMaxListEntries) { a_truncated = true; break; }
            indices.push_back(stage.data.index);
        }
    }
    if (a_quest->waitingStages) {
        std::size_t n = 0;
        for (const auto* stage : *a_quest->waitingStages) {
            if (n++ >= kMaxListEntries) { a_truncated = true; break; }
            if (stage) { indices.push_back(stage->data.index); }
        }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

// Step 18: summarize objectives stored in the quest QUST record.
// Objectives are NOT known to belong to the selected stage.
// Copy text when building the query; never retain game pointers
// in the state accessed by asynchronous callbacks.
void CollectQuestObjectivePreview(RE::TESQuest* a_quest,
    std::size_t& a_count, std::vector<InspectorQuestObjective>& a_preview)
{
    a_count = 0;
    a_preview.clear();
    if (!a_quest) { return; }
    constexpr std::size_t kMaxScan = 256;
    for (auto* obj : a_quest->objectives) {
        if (a_count >= kMaxScan) { break; }
        if (!obj) { continue; }
        ++a_count;
        if (a_preview.size() >= 2) { continue; }
        InspectorQuestObjective brief;
        brief.index = obj->index;
        const char* raw = obj->displayText.c_str();
        if (raw) {
            // The current range formatting uses ASCII indices; avoid UTF-8
            // character-length mismatches until full Unicode support is implemented.
            for (std::size_t i = 0; raw[i] != '\0' && i < 52; ++i) {
                const unsigned char c = static_cast<unsigned char>(raw[i]);
                brief.text.push_back(c >= 32 && c <= 126 ? static_cast<char>(c) : ' ');
            }
        }
        if (brief.text.empty()) { brief.text = "(no text)"; }
        a_preview.push_back(std::move(brief));
    }
}

void BeginInspectorQuery(RE::TESQuest* a_quest)
{
    if (!g_inspectorVisible) {
        return;
    }
    g_stageExecutionArm = {};
    g_inspectorPage = 0;
    g_inspectorSelectedPos = 0;
    bool truncated = false;
    auto indices = CollectQuestStages(a_quest, truncated);
    // If the current stage is listed, navigate directly to its page.
    // Otherwise start at the first available stage.
    constexpr std::size_t kInspectorPageSize = 10;
    if (a_quest && !indices.empty()) {
        auto current = std::find(indices.begin(), indices.end(), a_quest->GetCurrentStageID());
        if (current != indices.end()) {
            g_inspectorSelectedPos = static_cast<std::size_t>(std::distance(indices.begin(), current));
            g_inspectorPage = g_inspectorSelectedPos / kInspectorPageSize;
        }
    }

    constexpr std::size_t kMaxStages = 96; // Limited batch during prototype development.
    if (indices.size() > kMaxStages) {
        truncated = true;
    }

    std::uint64_t generation = 0;
    {
        std::lock_guard lock(g_inspectorMutex);
        ++g_inspectorGeneration;
        generation = g_inspectorGeneration;
        g_inspectorState = {};
        g_inspectorState.generation = generation;
        g_inspectorState.revision = ++g_inspectorRevision;
        if (a_quest) {
            g_inspectorState.formID = a_quest->GetFormID();
            const char* edid = a_quest->GetFormEditorID();
            g_inspectorState.editorID = edid ? edid : "(no EditorID)";
            CollectQuestObjectivePreview(a_quest,
                g_inspectorState.objectiveCount, g_inspectorState.objectivePreview);
            g_inspectorState.currentStage = a_quest->GetCurrentStageID();
            g_inspectorState.truncated = truncated;
            if (!truncated) {
                for (auto stage : indices) {
                    g_inspectorState.stages.push_back({ stage, StageQueryStatus::kPending });
                }
            }
        }
    }
    if (!a_quest || truncated || indices.empty()) {
        if (truncated && a_quest) {
            logger::warn("STEP14: quest {:08X} exceeds {} stages or stage lists were truncated; skipping query",
                a_quest->GetFormID(), kMaxStages);
        }
        return;
    }
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    auto* handles = vm ? vm->GetObjectHandlePolicy() : nullptr;
    if (!handles) {
        std::lock_guard lock(g_inspectorMutex);
        if (g_inspectorState.generation == generation) {
            g_inspectorState.vmUnavailable = true;
            g_inspectorState.revision = ++g_inspectorRevision;
        }
        logger::warn("STEP14: VM/object handle policy unavailable");
        return;
    }

    const auto handle = handles->GetHandleForObject(
        static_cast<RE::VMTypeID>(a_quest->GetFormType()), a_quest);
    std::size_t accepted = 0;
    for (const auto index : indices) {
        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(index));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{
            new QIJInspectorCallback(a_quest->GetFormID(), index, generation)
        };
        if (vm->DispatchMethodCall(handle, RE::BSFixedString("Quest"),
                RE::BSFixedString("IsStageDone"), args, callback)) {
            ++accepted;
        } else {
            std::lock_guard lock(g_inspectorMutex);
            if (g_inspectorState.generation == generation) {
                for (auto& entry : g_inspectorState.stages) {
                    if (entry.index == index && entry.status == StageQueryStatus::kPending) {
                        entry.status = StageQueryStatus::kFailed;
                        ++g_inspectorState.received;
                        g_inspectorState.revision = ++g_inspectorRevision;
                        break;
                    }
                }
            }
            logger::warn("STEP14: query rejected FormID={:08X} Stage={}",
                a_quest->GetFormID(), index);
        }
    }
    logger::info("STEP14: FormID={:08X} | stages={} | accepted queries={}/{}",
        a_quest->GetFormID(), indices.size(), accepted, indices.size());
}

// Step 11: enumerate defined stages for the selected quest.
// Executed/not executed status CANNOT be inferred from the source list.
// The actual state requires an IsStageDone/SQS-equivalent query.
void LogDefinedQuestStages(RE::TESQuest* a_quest)
{
    if (!a_quest) {
        return;
    }

    // Log each quest once per session, not continuously
    // when hovering across the same entries.
    static std::vector<std::uint32_t> loggedQuests;
    const auto formID = a_quest->GetFormID();
    if (std::find(loggedQuests.begin(), loggedQuests.end(), formID) != loggedQuests.end()) {
        return;
    }
    constexpr std::size_t kMaxQuests = 24;
    if (loggedQuests.size() >= kMaxQuests) {
        return;
    }
    loggedQuests.push_back(formID);

    constexpr std::size_t kMaxEntriesPerList = 512;
    std::vector<std::uint16_t> indices;
    bool truncated = false;

    if (a_quest->executedStages) {
        std::size_t count = 0;
        for (const auto& stage : *a_quest->executedStages) {
            if (count++ >= kMaxEntriesPerList) {
                truncated = true;
                break;
            }
            indices.push_back(stage.data.index);
        }
    }

    if (a_quest->waitingStages) {
        std::size_t count = 0;
        for (const auto* stage : *a_quest->waitingStages) {
            if (count++ >= kMaxEntriesPerList) {
                truncated = true;
                break;
            }
            if (stage) {
                indices.push_back(stage->data.index);
            }
        }
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    std::string formatted;
    for (const auto index : indices) {
        if (!formatted.empty()) {
            formatted += ", ";
        }
        formatted += std::to_string(index);
    }

    const auto* editorID = a_quest->GetFormEditorID();
    logger::info("STEP11 DEFINED STAGES: EditorID={} | FormID={:08X} | Total={} | Indices={}{}",
        editorID ? editorID : "(no EditorID)", formID,
        indices.size(), formatted.empty() ? "(none)" : formatted,
        truncated ? " [WARNING: truncated stage lists]" : "");
    logger::info("STEP14: defined stages enumerated; IsStageDone is queried when the inspector opens");
}

// Keep the last selection only while the journal is open.
inline std::atomic<std::uint32_t> g_lastSelectedFormID{ 0xFFFFFFFFu };

// Temporary notification confirming a clipboard copy without SWF replacement.
// Updated exclusively from the journal hook.
enum class CopyNoticeType { kNone, kEditorID, kFormID };
inline CopyNoticeType g_copyNotice = CopyNoticeType::kNone;
inline std::chrono::steady_clock::time_point g_copyNoticeExpires{};
inline bool g_copyNoticeNeedsDraw = false;


// Listen for menu opening/closing; do not read Scaleform here.
class JournalMenuListener final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (!a_event || std::string_view(a_event->menuName.c_str()) != RE::JournalMenu::MENU_NAME) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Force an initial selection read when reopening the journal.
        g_lastSelectedFormID.store(0xFFFFFFFFu, std::memory_order_relaxed);

        if (a_event->opening) {
            logger::info("Player OPENED the Skyrim journal");
        } else {
            // The TextField is part of the SWF and is destroyed when the journal closes.
            g_copyNotice = CopyNoticeType::kNone;
            g_copyNoticeNeedsDraw = false;
            g_inspectorVisible = false;
            g_inspectorRefreshPending = false;
            g_inspectorPage = 0;
            g_inspectorSelectedPos = 0;
            g_inspectorCopyFeedback.clear();
            g_stageExecutionArm = {};
            ++g_inspectorUiRevision;
            ClearInspectorQuery();
            logger::info("Player CLOSED the Skyrim journal");
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

// Read the selected quest and draw an unobtrusive label.
// Call the original function before reading the selection.
struct JournalAdvanceHook
{
    // Two-line label above the quest title on the right.
    // Created in memory: does NOT replace quest_journal.swf or the game title.
    static bool ShowQuestID(RE::GFxMovieView* a_movie, RE::GFxValue& a_page,
                            const char* a_editorID, std::uint32_t a_formID, int a_stage)
    {
        constexpr const char* labelName = "QIJ_QuestIDs";
        // Option B: top-right strip above the quest title.
        // Coordinates are local to QuestsPage and originally tuned
        // for the Apostasy UI shown during development.
        // They can be adjusted without changing quest detection.
        const double labelX = static_cast<double>(g_displaySettings.positionX);
        const double labelY = static_cast<double>(g_displaySettings.positionY);
        constexpr double labelWidth = 300.0;
        constexpr double labelHeight = 42.0;
        const double fontSize = static_cast<double>(g_displaySettings.fontSize);
        constexpr std::uint32_t fontColor = 0xB8B8B8; // Light gray

        if (!a_movie) {
            return false;
        }

        // Keep the copy notification visible for its 1.4-second duration.
        // Changing quests must not recreate it or spam logs.
        if (g_copyNotice != CopyNoticeType::kNone) {
            return true;
        }

        // Stage display is independent of EditorID and FormID settings.
        if (!g_displaySettings.showEditorID &&
            !g_displaySettings.showFormID &&
            !g_displaySettings.showStage) {
            RE::GFxValue existing;
            if (a_page.GetMember(labelName, &existing) && existing.IsObject()) {
                RE::GFxValue empty;
                a_movie->CreateString(&empty, "");
                existing.SetMember("text", empty);
            }
            return true;
        }

        RE::GFxValue label;
        if (!a_page.GetMember(labelName, &label) || !label.IsObject()) {
            RE::GFxValue depth;
            if (!a_page.Invoke("getNextHighestDepth", &depth) || !depth.IsNumber()) {
                logger::warn("UI: getNextHighestDepth unavailable");
                return false;
            }

            std::array<RE::GFxValue, 6> args{};
            args[0].SetString(labelName);
            args[1].SetNumber(depth.GetNumber());
            args[2].SetNumber(labelX);
            args[3].SetNumber(labelY);
            args[4].SetNumber(labelWidth);
            args[5].SetNumber(labelHeight);

            RE::GFxValue newField;
            if (!a_page.Invoke("createTextField", &newField, args.data(), args.size())) {
                logger::warn("UI: failed to create TextField");
                return false;
            }
            if (!a_page.GetMember(labelName, &label) || !label.IsObject()) {
                logger::warn("UI: TextField created but inaccessible");
                return false;
            }

            RE::GFxValue selectable;
            selectable.SetBoolean(false);
            label.SetMember("selectable", selectable);

            RE::GFxValue multiline;
            multiline.SetBoolean(true);
            label.SetMember("multiline", multiline);

            RE::GFxValue wrapping;
            wrapping.SetBoolean(false); // Prevent a third line from overlapping the quest title.
            label.SetMember("wordWrap", wrapping);

            RE::GFxValue autoSize;
            autoSize.SetString("none"); // Keep a fixed height in the top-right strip.
            label.SetMember("autoSize", autoSize);

            RE::GFxValue embedded;
            embedded.SetBoolean(true);
            label.SetMember("embedFonts", embedded);

            logger::info("UI: top-right label created");
        }

        // Futura CondensedLight has been tested in the development environment.
        RE::GFxValue textFormat;
        a_movie->CreateObject(&textFormat, "TextFormat");
        if (!textFormat.IsObject()) {
            logger::warn("UI: failed to create TextFormat");
            return false;
        }

        RE::GFxValue fontName;
        fontName.SetString("Futura CondensedLight");
        textFormat.SetMember("font", fontName);

        RE::GFxValue size;
        size.SetNumber(fontSize);
        textFormat.SetMember("size", size);

        RE::GFxValue color;
        color.SetNumber(fontColor);
        textFormat.SetMember("color", color);

        RE::GFxValue align;
        align.SetString("right"); // Right-aligned with the edge of the panel.
        textFormat.SetMember("align", align);

        RE::GFxValue leading;
        leading.SetNumber(1.0);
        textFormat.SetMember("leading", leading);

        if (!label.Invoke("setNewTextFormat", nullptr, &textFormat, 1)) {
            logger::warn("UI: setNewTextFormat unavailable");
        }

        std::string line;
        if (g_displaySettings.showEditorID) {
            line = "EditorID: ";
            line += a_formID == 0 ? "(miscellaneous)" :
                (a_editorID ? a_editorID : "(not available)");
        }
        if (g_displaySettings.showFormID) {
            if (!line.empty()) {
                line += "\n";
            }
            line += "FormID: ";
            if (a_formID == 0) {
                line += "(not applicable)";
            } else {
                char hexID[16]{};
                std::snprintf(hexID, sizeof(hexID), "%08X", a_formID);
                line += hexID;
            }
        }

        // Show Stage next to FormID on line 2 so all three fields fit
        // into two lines and do not overlap the current quest title
        // in the Apostasy journal UI.
        if (g_displaySettings.showStage) {
            if (g_displaySettings.showFormID) {
                line += "  |  ";
            } else if (!line.empty()) {
                line += "\n";
            }
            line += "Stage: ";
            if (a_stage >= 0) {
                line += std::to_string(a_stage);
            } else {
                line += "N/A";
            }
        }

        RE::GFxValue text;
        a_movie->CreateString(&text, line.c_str());
        if (!label.SetMember("text", text)) {
            logger::warn("UI: failed to update label text");
            return false;
        }

        // Also format existing text after each quest selection change.
        if (!label.Invoke("setTextFormat", nullptr, &textFormat, 1)) {
            logger::warn("UI: setTextFormat unavailable");
        }

        logger::info("UI: selected quest data updated (top-right)");
        return true;
    }

    static void ReadSelectedQuest(RE::JournalMenu* a_menu)
    {
        if (!a_menu || !a_menu->uiMovie) {
            return;
        }

        RE::GFxValue questPage;
        constexpr const char* path =
            "_root.QuestJournalFader.Menu_mc.QuestsFader.Page_mc";

        if (!a_menu->uiMovie->GetVariable(&questPage, path) || !questPage.IsObject()) {
            return; // The user may be in another tab, or the UI may still be loading.
        }

        // QuestsPage updates the visible title using TitleList.selectedEntry.
        // Read that SAME entry to avoid using another quest's ID.
        // Fall back if the UI does not expose selectedEntry.
        RE::GFxValue selectedID;
        RE::GFxValue titleList;
        RE::GFxValue selectedEntry;
        bool hasID = false;

        if (questPage.GetMember("TitleList", &titleList) && titleList.IsObject() &&
            titleList.GetMember("selectedEntry", &selectedEntry) && selectedEntry.IsObject() &&
            selectedEntry.GetMember("formID", &selectedID) && selectedID.IsNumber()) {
            hasID = true;
        } else if (questPage.GetMember("selectedQuestID", &selectedID) && selectedID.IsNumber()) {
            hasID = true; // Compatibility fallback for UI mods without selectedEntry.
        }

        if (!hasID) {
            return; // No selected quest yet.
        }

        const auto formID = static_cast<std::uint32_t>(selectedID.GetNumber());
        if (formID == g_lastSelectedFormID.load(std::memory_order_relaxed)) {
            return; // Avoid per-frame updates and logs.
        }
        g_lastSelectedFormID.store(formID, std::memory_order_relaxed);

        if (formID == 0) {
            logger::info("Current selection: Miscellaneous (FormID 00000000)");
            ScheduleInspectorRefresh(formID);
            ShowQuestID(a_menu->uiMovie.get(), questPage, nullptr, formID, -1);
            return;
        }

        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(formID);
        if (!quest) {
            logger::warn("Selection changed: quest FormID {:08X} not found", formID);
            ScheduleInspectorRefresh(formID);
            ShowQuestID(a_menu->uiMovie.get(), questPage, "(not found)", formID, -1);
            return;
        }

        const char* editorID = quest->GetFormEditorID();
        logger::info("QUEST CHANGED: EditorID={} | FormID={:08X}",
            editorID ? editorID : "(unavailable)", formID);

        // Read only the selected quest's current stage.
        // This read is safe; optional execution follows a separate workflow.
        const std::uint16_t currentStage = quest->GetCurrentStageID();
        logger::info("CURRENT STAGE: EditorID={} | FormID={:08X} | Stage={}",
            editorID ? editorID : "(unavailable)", formID, currentStage);

        // Step 11 diagnostics: enumerate stages (do not infer their status).
        LogDefinedQuestStages(quest);
        if (g_inspectorVisible) {
            ScheduleInspectorRefresh(formID);
        }

        ShowQuestID(a_menu->uiMovie.get(), questPage, editorID, formID, static_cast<int>(currentStage));
    }

    // Primary hotkeys are configurable; Ctrl+Shift is fixed.
    // Poll keys only in the journal hook, never globally in gameplay.
    static void CheckCopyHotkeys()
    {
        static bool previousEditorChord = false;
        static bool previousFormChord = false;

        const auto down = [](int a_key) {
            return (GetAsyncKeyState(a_key) & 0x8000) != 0;
        };
        const bool modifiers = down(VK_CONTROL) && down(VK_SHIFT);
        const bool editorChord = modifiers && down(g_displaySettings.copyEditorKey);
        const bool formChord = modifiers && down(g_displaySettings.copyFormKey);
        const bool editorPressed = editorChord && !previousEditorChord;
        const bool formPressed = formChord && !previousFormChord;
        previousEditorChord = editorChord;
        previousFormChord = formChord;

        if (!g_displaySettings.enableCopy || (!editorPressed && !formPressed)) {
            return;
        }

        HWND foreground = GetForegroundWindow();
        DWORD processID = 0;
        if (foreground) {
            GetWindowThreadProcessId(foreground, &processID);
        }
        if (processID != GetCurrentProcessId()) {
            return;
        }

        const auto formID = g_lastSelectedFormID.load(std::memory_order_relaxed);
        if (formID == 0 || formID == 0xFFFFFFFFu) {
            logger::warn("Copy: no individual quest is selected");
            return;
        }

        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(formID);
        if (!quest) {
            logger::warn("Copy: quest {:08X} not found", formID);
            return;
        }

        if (editorPressed) {
            const char* editorID = quest->GetFormEditorID();
            if (!editorID || !editorID[0]) {
                logger::warn("Copy: selected quest has no EditorID");
                return;
            }
            const bool copied = CopyToClipboard(editorID);
            if (copied) {
                logger::info("COPIED EditorID: {}", editorID);
                g_copyNotice = CopyNoticeType::kEditorID;
                g_copyNoticeExpires = std::chrono::steady_clock::now() + std::chrono::milliseconds(1400);
                g_copyNoticeNeedsDraw = true;
            } else {
                logger::warn("Copy: failed to copy EditorID");
            }
        } else if (formPressed) {
            char hexID[16]{};
            std::snprintf(hexID, sizeof(hexID), "%08X", formID);
            const bool copied = CopyToClipboard(hexID);
            if (copied) {
                logger::info("COPIED FormID: {}", hexID);
                g_copyNotice = CopyNoticeType::kFormID;
                g_copyNoticeExpires = std::chrono::steady_clock::now() + std::chrono::milliseconds(1400);
                g_copyNoticeNeedsDraw = true;
            } else {
                logger::warn("Copy: failed to copy FormID");
            }
        }
    }

    // Temporarily replace the IDs with a copy notification.
    // Do not create extra fields or move the user-approved label.
    static void UpdateCopyNotice(RE::JournalMenu* a_menu)
    {
        if (g_copyNotice == CopyNoticeType::kNone || !a_menu || !a_menu->uiMovie) {
            return;
        }

        if (std::chrono::steady_clock::now() >= g_copyNoticeExpires) {
            g_copyNotice = CopyNoticeType::kNone;
            g_copyNoticeNeedsDraw = false;

            // Restore the original content once when the notice expires.
            // If selection changed, read the newly selected quest.
            g_lastSelectedFormID.store(0xFFFFFFFFu, std::memory_order_relaxed);
            ReadSelectedQuest(a_menu);
            return;
        }

        if (!g_copyNoticeNeedsDraw) {
            return;
        }

        RE::GFxValue questPage;
        constexpr const char* path =
            "_root.QuestJournalFader.Menu_mc.QuestsFader.Page_mc";
        if (!a_menu->uiMovie->GetVariable(&questPage, path) || !questPage.IsObject()) {
            return;
        }

        RE::GFxValue label;
        if (!questPage.GetMember("QIJ_QuestIDs", &label) || !label.IsObject()) {
            // When all displayed fields are disabled, no label exists.
            // Copying still works and is logged.
            return;
        }

        const char* message = g_copyNotice == CopyNoticeType::kEditorID
            ? "EditorID copied!" : "FormID copied!";

        RE::GFxValue text;
        a_menu->uiMovie->CreateString(&text, message);
        if (!label.SetMember("text", text)) {
            logger::warn("UI: failed to display copy notification");
            g_copyNoticeNeedsDraw = false;
            return;
        }

        // Use an embedded font to prevent missing-glyph squares.
        RE::GFxValue fmt;
        a_menu->uiMovie->CreateObject(&fmt, "TextFormat");
        if (fmt.IsObject()) {
            RE::GFxValue font;
            font.SetString("Futura CondensedLight");
            fmt.SetMember("font", font);

            RE::GFxValue size;
            size.SetNumber(static_cast<double>(g_displaySettings.fontSize));
            fmt.SetMember("size", size);

            RE::GFxValue color;
            color.SetNumber(0xD6CCAF); // Soft beige for visible confirmation.
            fmt.SetMember("color", color);

            RE::GFxValue align;
            align.SetString("right");
            fmt.SetMember("align", align);
            label.Invoke("setNewTextFormat", nullptr, &fmt, 1);
            label.Invoke("setTextFormat", nullptr, &fmt, 1);
        }

        g_copyNoticeNeedsDraw = false;
        logger::info("UI: copy notification displayed");
    }

    // Read-only inspector as an additional Scaleform TextField.
    // Ctrl+Shift+configurable key; defaults I/N/P.
    static void CheckInspectorHotkeys()
    {
        static bool oldToggle = false;
        static bool oldNext = false;
        static bool oldPrev = false;
        static bool oldSelectNext = false;
        static bool oldSelectPrev = false;
        static bool oldDetails = false;
        static bool oldCopyStage = false;
        static bool oldCopyCommand = false;
        static bool oldRequestExecution = false;
        static bool oldConfirmExecution = false;
        const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
        const bool modifiers = down(VK_CONTROL) && down(VK_SHIFT);
        const bool toggle = modifiers && down(g_displaySettings.inspectorToggleKey);
        const bool next = modifiers && down(g_displaySettings.inspectorNextKey);
        const bool prev = modifiers && down(g_displaySettings.inspectorPrevKey);
        const bool selectNext = modifiers && down(g_displaySettings.inspectorSelectNextKey);
        const bool selectPrev = modifiers && down(g_displaySettings.inspectorSelectPrevKey);
        const bool details = modifiers && down(g_displaySettings.inspectorDetailsKey);
        const bool copyStage = modifiers && down(g_displaySettings.inspectorCopyStageKey);
        const bool copyCommand = modifiers && down(g_displaySettings.inspectorCopyCommandKey);
        const bool requestExecution = modifiers && down(g_displaySettings.inspectorRequestExecutionKey);
        const bool confirmExecution = modifiers && down(g_displaySettings.inspectorConfirmExecutionKey);
        const bool togglePressed = toggle && !oldToggle;
        const bool nextPressed = next && !oldNext;
        const bool prevPressed = prev && !oldPrev;
        const bool selectNextPressed = selectNext && !oldSelectNext;
        const bool selectPrevPressed = selectPrev && !oldSelectPrev;
        const bool detailsPressed = details && !oldDetails;
        const bool copyStagePressed = copyStage && !oldCopyStage;
        const bool copyCommandPressed = copyCommand && !oldCopyCommand;
        const bool requestExecutionPressed = requestExecution && !oldRequestExecution;
        const bool confirmExecutionPressed = confirmExecution && !oldConfirmExecution;
        oldToggle = toggle;
        oldNext = next;
        oldPrev = prev;
        oldSelectNext = selectNext;
        oldSelectPrev = selectPrev;
        oldDetails = details;
        oldCopyStage = copyStage;
        oldCopyCommand = copyCommand;
        oldRequestExecution = requestExecution;
        oldConfirmExecution = confirmExecution;
        if (!togglePressed && !nextPressed && !prevPressed &&
            !selectNextPressed && !selectPrevPressed && !detailsPressed &&
            !copyStagePressed && !copyCommandPressed &&
            !requestExecutionPressed && !confirmExecutionPressed) {
            return;
        }
        HWND foreground = GetForegroundWindow();
        DWORD pid = 0;
        if (foreground) { GetWindowThreadProcessId(foreground, &pid); }
        if (pid != GetCurrentProcessId()) { return; }

        if (togglePressed) {
            g_stageExecutionArm = {};
            g_inspectorVisible = !g_inspectorVisible;
            if (g_inspectorVisible) {
                g_inspectorDetailsView = false;
                g_inspectorCopyFeedback.clear();
                ++g_inspectorUiRevision;
                g_inspectorRefreshPending = false;
                const auto formID = g_lastSelectedFormID.load(std::memory_order_relaxed);
                auto* quest = formID != 0 && formID != 0xFFFFFFFFu ?
                    RE::TESForm::LookupByID<RE::TESQuest>(formID) : nullptr;
                BeginInspectorQuery(quest);
                logger::info("STEP16: inspector OPENED (Ctrl+Shift+{})",
                    static_cast<char>(g_displaySettings.inspectorToggleKey));
            } else {
                g_inspectorRefreshPending = false;
                g_inspectorSelectedPos = 0;
                g_inspectorDetailsView = false;
                g_inspectorCopyFeedback.clear();
                ++g_inspectorUiRevision;
                ClearInspectorQuery();
                logger::info("STEP16: inspector CLOSED (Ctrl+Shift+{})",
                    static_cast<char>(g_displaySettings.inspectorToggleKey));
            }
            return;
        }
        if (!g_inspectorVisible) { return; }
        if (g_stageExecutionArm.active &&
            (nextPressed || prevPressed || selectNextPressed || selectPrevPressed || detailsPressed)) {
            g_stageExecutionArm = {};
            SetInspectorCopyFeedback("Execution canceled: selection changed", false);
        }
        if (detailsPressed) {
            g_inspectorDetailsView = !g_inspectorDetailsView;
            logger::info("STEP18: {} view (read-only)",
                g_inspectorDetailsView ? "DETAILS" : "LIST");
        }
        constexpr std::size_t kPageSize = 10;
        std::size_t count = 0;
        {
            std::lock_guard lock(g_inspectorMutex);
            count = g_inspectorState.stages.size();
        }
        const std::size_t pageCount = std::max<std::size_t>(1, (count + kPageSize - 1) / kPageSize);
        if (count == 0) { return; }
        // Keep selected row visible while navigating pages.
        // N/P move selection to the first stage on the new page.
        if (nextPressed && g_inspectorPage + 1 < pageCount) {
            ++g_inspectorPage;
            g_inspectorSelectedPos = g_inspectorPage * kPageSize;
        }
        if (prevPressed && g_inspectorPage > 0) {
            --g_inspectorPage;
            g_inspectorSelectedPos = g_inspectorPage * kPageSize;
        }
        // J/K select the next/previous stage WITHOUT executing it.
        // Automatically change pages at list boundaries.
        if (selectNextPressed && g_inspectorSelectedPos + 1 < count) {
            ++g_inspectorSelectedPos;
            g_inspectorPage = g_inspectorSelectedPos / kPageSize;
        }
        if (selectPrevPressed && g_inspectorSelectedPos > 0) {
            --g_inspectorSelectedPos;
            g_inspectorPage = g_inspectorSelectedPos / kPageSize;
        }

        // Step 22: deliberately separate request and confirmation.
        // NEVER execute on request alone or repeated key holds.
        if (requestExecutionPressed || confirmExecutionPressed) {
            if (!g_displaySettings.enableStageExecution) {
                SetInspectorCopyFeedback("Stage execution is DISABLED in [Execution]", false);
                return;
            }
            if (g_inspectorRefreshPending) {
                g_stageExecutionArm = {};
                SetInspectorCopyFeedback("Wait for the quest selection to update", false);
                return;
            }
            {
                std::lock_guard lock(g_stageExecutionMutex);
                if (g_stageExecutionResult.inFlight) {
                    SetInspectorCopyFeedback(
                        g_stageExecutionResult.slowWarningLogged ?
                        "Papyrus is not responding: LOCKED. Do not retry" :
                        "A Papyrus stage execution is already pending", false);
                    return;
                }
            }
            std::uint32_t targetFormID = 0;
            std::uint16_t targetStage = 0;
            std::uint16_t queriedCurrentStage = 0;
            std::uint64_t generation = 0;
            StageQueryStatus status = StageQueryStatus::kPending;
            bool invalid = false;
            {
                std::lock_guard lock(g_inspectorMutex);
                invalid = g_inspectorState.truncated || g_inspectorState.vmUnavailable ||
                    g_inspectorSelectedPos >= g_inspectorState.stages.size();
                if (!invalid) {
                    targetFormID = g_inspectorState.formID;
                    const auto& selected = g_inspectorState.stages[g_inspectorSelectedPos];
                    targetStage = selected.index;
                    status = selected.status;
                    queriedCurrentStage = g_inspectorState.currentStage;
                    generation = g_inspectorState.generation;
                }
            }
            if (invalid || !targetFormID ||
                targetFormID != g_lastSelectedFormID.load(std::memory_order_relaxed)) {
                g_stageExecutionArm = {};
                SetInspectorCopyFeedback("Selection is not ready or has changed", false);
                return;
            }
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(targetFormID);
            if (!quest || !quest->IsRunning() || quest->IsCompleted() ||
                quest->IsStopping() || quest->IsStarting()) {
                g_stageExecutionArm = {};
                SetInspectorCopyFeedback("Blocked: quest is inactive or completed", false);
                return;
            }
            // Never allow backward, current, or already-completed stages
            // from the inspector. Never call Reset() automatically.
            if (status != StageQueryStatus::kNotExecuted ||
                targetStage <= queriedCurrentStage ||
                quest->GetCurrentStageID() != queriedCurrentStage) {
                g_stageExecutionArm = {};
                SetInspectorCopyFeedback("Blocked: stage is already done, earlier, or outdated", false);
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (requestExecutionPressed) {
                g_stageExecutionArm = { true, targetFormID, targetStage,
                    queriedCurrentStage, generation, now + std::chrono::seconds(8) };
                SetInspectorCopyFeedback("REQUESTED " + std::to_string(targetStage) +
                    ": Ctrl+Shift+" + std::string(1, static_cast<char>(g_displaySettings.inspectorConfirmExecutionKey)) +
                    " within 8s - CANNOT be undone", false);
                logger::warn("STEP22 REQUESTED: FormID={:08X} Stage={} | current={} | armed only",
                    targetFormID, targetStage, queriedCurrentStage);
                return;
            }
            // Confirmation requires the same FormID, stage, query generation,
            // and current stage within eight seconds of request.
            if (!g_stageExecutionArm.active || now >= g_stageExecutionArm.expires ||
                g_stageExecutionArm.formID != targetFormID ||
                g_stageExecutionArm.stage != targetStage ||
                g_stageExecutionArm.queryGeneration != generation ||
                g_stageExecutionArm.originalCurrentStage != queriedCurrentStage) {
                g_stageExecutionArm = {};
                SetInspectorCopyFeedback("Confirm only after requesting this exact stage", false);
                return;
            }
            g_stageExecutionArm = {};
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* handles = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!vm || !handles) {
                SetInspectorCopyFeedback("Papyrus VM unavailable", false);
                return;
            }
            // Previous status comes from the VM IsStageDone query.
            // Execution calls the native latent Papyrus method so Skyrim can
            // process fragments, objectives, and other events.
            const auto handle = handles->GetHandleForObject(
                static_cast<RE::VMTypeID>(quest->GetFormType()), quest);
            // Record the operation BEFORE dispatch because Papyrus callbacks
            // can arrive on another thread. Do not reuse IDs during this session.
            std::uint64_t requestID = 0;
            {
                std::lock_guard lock(g_stageExecutionMutex);
                requestID = ++g_nextStageExecutionRequestID;
                g_stageExecutionResult = {};
                g_stageExecutionResult.inFlight = true;
                g_stageExecutionResult.formID = targetFormID;
                g_stageExecutionResult.stage = targetStage;
                g_stageExecutionResult.requestID = requestID;
                g_stageExecutionResult.startedAt = std::chrono::steady_clock::now();
            }
            auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(targetStage));
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{
                new QIJStageExecutionCallback(targetFormID, targetStage, requestID)
            };
            ++g_inspectorUiRevision;
            logger::warn("STEP22.1 CONFIRMED: Request={} | DISPATCH SetCurrentStageID | FormID={:08X} | Stage={} | previous={}",
                requestID, targetFormID, targetStage, queriedCurrentStage);
            if (vm->DispatchMethodCall(handle, RE::BSFixedString("Quest"),
                    RE::BSFixedString("SetCurrentStageID"), args, callback)) {
                SetInspectorCopyFeedback("Papyrus accepted the request; processing...", true);
            } else {
                {
                    std::lock_guard lock(g_stageExecutionMutex);
                    g_stageExecutionResult = {};
                }
                SetInspectorCopyFeedback("VM rejected SetCurrentStageID: NOT confirmed", false);
                logger::warn("STEP22 REJECTED by VM: FormID={:08X} Stage={}",
                    targetFormID, targetStage);
            }
            return;
        }

        // Step 21: copy data; never execute SetStage or open the console.
        if (!copyStagePressed && !copyCommandPressed) { return; }
        if (!g_displaySettings.enableCopy) {
            SetInspectorCopyFeedback("Copying is disabled (EnableCopy=0)", false);
            return;
        }
        if (g_inspectorRefreshPending) {
            SetInspectorCopyFeedback("Wait for the quest to update", false);
            return;
        }

        std::uint32_t formID = 0;
        std::uint16_t selectedStage = 0;
        {
            // Papyrus callbacks update query results from another thread.
            std::lock_guard lock(g_inspectorMutex);
            if (g_inspectorSelectedPos >= g_inspectorState.stages.size()) {
                SetInspectorCopyFeedback("No stage selected", false);
                return;
            }
            formID = g_inspectorState.formID;
            selectedStage = g_inspectorState.stages[g_inspectorSelectedPos].index;
        }
        const auto currentFormID = g_lastSelectedFormID.load(std::memory_order_relaxed);
        if (formID == 0 || formID != currentFormID) {
            SetInspectorCopyFeedback("Selection changed: please wait", false);
            return;
        }
        // Never trust stored strings from another quest or a prior hover selection.
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(formID);
        if (!quest) {
            SetInspectorCopyFeedback("Selected quest not found", false);
            return;
        }

        std::string toCopy;
        if (copyStagePressed) {
            toCopy = std::to_string(selectedStage);
        } else {
            const char* editorID = quest->GetFormEditorID();
            if (!editorID || editorID[0] == '\0') {
                SetInspectorCopyFeedback("No EditorID available: cannot build command", false);
                return;
            }
            toCopy = "setstage ";
            toCopy += editorID;
            toCopy += ' ';
            toCopy += std::to_string(selectedStage);
        }
        if (CopyToClipboard(toCopy)) {
            SetInspectorCopyFeedback(copyStagePressed ?
                "Stage " + std::to_string(selectedStage) + " copied" :
                "setstage command copied (NOT executed)", true);
            logger::info("STEP21 COPIED {}: FormID={:08X} | Stage={} | Text={}",
                copyStagePressed ? "stage" : "command", formID, selectedStage, toCopy);
        } else {
            SetInspectorCopyFeedback("Failed to copy to clipboard", false);
            logger::warn("STEP21: clipboard operation failed for FormID={:08X} Stage={}",
                formID, selectedStage);
        }
    }

    static void ProcessInspectorSelection()
    {
        if (!g_inspectorVisible || !g_inspectorRefreshPending) { return; }
        // Avoid 20-30 Papyrus calls for every quest briefly hovered by the mouse.
        if (std::chrono::steady_clock::now() - g_inspectorPendingSince <
            std::chrono::milliseconds(250)) { return; }
        g_inspectorRefreshPending = false;
        auto* quest = g_inspectorPendingFormID ?
            RE::TESForm::LookupByID<RE::TESQuest>(g_inspectorPendingFormID) : nullptr;
        BeginInspectorQuery(quest);
    }

    static void ProcessStageExecutionResult()
    {
        StageExecutionResult result;
        bool hasCallback = false;
        {
            std::lock_guard lock(g_stageExecutionMutex);
            if (!g_stageExecutionResult.inFlight) { return; }
            result = g_stageExecutionResult;
            if (g_stageExecutionResult.resultReady) {
                g_stageExecutionResult = {}; // ONLY the callback may unlock an operation.
                hasCallback = true;
            }
        }
        if (hasCallback) {
            ++g_inspectorUiRevision;
            const auto message = !result.returnedBool ? "Papyrus returned a non-boolean result" :
                result.success ? "SetStage succeeded: review the quest before saving" :
                                 "SetStage returned FALSE: check the quest";
            logger::info("STEP22.1 UNLOCKED: Request={} | Stage={} | final response received | OK={}",
                result.requestID, result.stage, result.success ? 1 : 0);
            if (auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(result.formID)) {
                logger::info("STEP22.1 FINAL STATUS: Request={} | FormID={:08X} | CurrentStage={} | QuestCompleted={}",
                    result.requestID, result.formID, quest->GetCurrentStageID(),
                    quest->IsCompleted() ? 1 : 0);
            } else {
                logger::warn("STEP22.1 FINAL STATUS: Request={} | FormID={:08X} unavailable for verification",
                    result.requestID, result.formID);
            }
            if (!g_inspectorVisible) { return; }
            SetInspectorCopyFeedback(message, result.success);
            if (g_lastSelectedFormID.load(std::memory_order_relaxed) == result.formID) {
                g_lastSelectedFormID.store(0xFFFFFFFFu, std::memory_order_relaxed);
            }
            return;
        }

        // Observe without touching Papyrus: a stage can be recorded before
        // all fragments finish and the latent function returns.
        // These values are OBSERVATIONS, not callback confirmation.
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(result.formID);
        const bool stageAdvanced = quest && quest->GetCurrentStageID() >= result.stage;
        const bool questCompleted = quest && quest->IsCompleted();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - result.startedAt).count();
        bool newStageObservation = false;
        bool newCompletionObservation = false;
        bool newSlowWarning = false;
        bool newProlongedWarning = false;
        {
            std::lock_guard lock(g_stageExecutionMutex);
            auto& pending = g_stageExecutionResult;
            if (!pending.inFlight || pending.resultReady ||
                pending.requestID != result.requestID) { return; }
            if (stageAdvanced && !pending.stageProgressObserved) {
                pending.stageProgressObserved = true;
                newStageObservation = true;
            }
            if (questCompleted && !pending.questCompletedObserved) {
                pending.questCompletedObserved = true;
                newCompletionObservation = true;
            }
            if (elapsed >= 10 && !pending.slowWarningLogged) {
                pending.slowWarningLogged = true;
                newSlowWarning = true;
            }
            if (elapsed >= 30 && !pending.prolongedWarningLogged) {
                pending.prolongedWarningLogged = true;
                newProlongedWarning = true;
            }
        }
        if (newStageObservation) {
            logger::warn("STEP22.1 PROGRESS OBSERVED: Request={} | FormID={:08X} | TargetStage={} | CurrentStage={} | callback PENDING",
                result.requestID, result.formID, result.stage, quest->GetCurrentStageID());
        }
        if (newCompletionObservation) {
            logger::warn("STEP22.1 QUEST COMPLETED: Request={} | FormID={:08X} | callback PENDING",
                result.requestID, result.formID);
        }
        if (newSlowWarning) {
            logger::warn("STEP22.1 WAITING: Request={} | {} s without callback | TargetStage={} | progressObserved={} | completed={} | LOCK REMAINS",
                result.requestID, elapsed, result.stage, stageAdvanced ? 1 : 0, questCompleted ? 1 : 0);
        }
        if (newProlongedWarning) {
            logger::error("STEP22.1 RESPONSE PENDING: Request={} | {} s without callback | DO NOT retry or execute another stage; locked until callback or restart",
                result.requestID, elapsed);
        }
        if (newStageObservation || newCompletionObservation || newSlowWarning || newProlongedWarning) {
            ++g_inspectorUiRevision; // Refresh the persistent status message.
        }
    }

    // Step 17: plain-text inspector with per-range color formatting.
    // Text labels remain for every state; color is only supplemental.
    // The main label remains a single TextField; no SWF is replaced.
    static void UpdateInspectorVisual(RE::JournalMenu* a_menu)
    {
        if (!a_menu || !a_menu->uiMovie) { return; }
        RE::GFxValue page;
        constexpr const char* kPagePath = "_root.QuestJournalFader.Menu_mc.QuestsFader.Page_mc";
        if (!a_menu->uiMovie->GetVariable(&page, kPagePath) || !page.IsObject()) {
            return;
        }

        constexpr const char* kName = "QIJ_StageInspector";
        constexpr const char* kAccentName = "QIJ_StageInspectorAccent";
        RE::GFxValue label;
        RE::GFxValue accent;
        const bool exists = page.GetMember(kName, &label) && label.IsObject();
        const bool accentExists = page.GetMember(kAccentName, &accent) && accent.IsObject();
        if (!g_inspectorVisible) {
            RE::GFxValue hidden;
            hidden.SetBoolean(false);
            if (exists) { label.SetMember("_visible", hidden); }
            if (accentExists) { accent.SetMember("_visible", hidden); }
            return;
        }

        bool created = false;
        if (!exists) {
            RE::GFxValue depth;
            if (!page.Invoke("getNextHighestDepth", &depth) || !depth.IsNumber()) {
                logger::warn("STEP16 UI: getNextHighestDepth unavailable");
                return;
            }
            // Local QuestsPage coordinates; do not alter the original menu.
            std::array<RE::GFxValue, 6> args{};
            args[0].SetString(kName);
            args[1].SetNumber(depth.GetNumber());
            args[2].SetNumber(static_cast<double>(g_displaySettings.inspectorX));
            args[3].SetNumber(static_cast<double>(g_displaySettings.inspectorY));
            args[4].SetNumber(static_cast<double>(g_displaySettings.inspectorWidth));
            args[5].SetNumber(static_cast<double>(g_displaySettings.inspectorHeight));
            RE::GFxValue newField;
            if (!page.Invoke("createTextField", &newField, args.data(), args.size()) ||
                !page.GetMember(kName, &label) || !label.IsObject()) {
                logger::warn("STEP16 UI: failed to create main TextField");
                return;
            }
            created = true;
            auto setBool = [&](const char* key, bool value) {
                RE::GFxValue v;
                v.SetBoolean(value);
                label.SetMember(key, v);
            };
            setBool("selectable", false);
            setBool("multiline", true);
            setBool("wordWrap", false);
            setBool("embedFonts", true);
            setBool("background", true);
            setBool("border", true);
            RE::GFxValue bg;
            bg.SetNumber(0x101014);
            label.SetMember("backgroundColor", bg);
            RE::GFxValue border;
            border.SetNumber(0x776B58);
            label.SetMember("borderColor", border);
            RE::GFxValue fixed;
            fixed.SetString("none");
            label.SetMember("autoSize", fixed);
            logger::info("STEP17 UI: panel created with stage selection");
        }
        RE::GFxValue shown;
        shown.SetBoolean(true);
        label.SetMember("_visible", shown);

        // Discreet top accent matching Skyrim's gold UI palette.
        // The inspector must still work if the accent creation fails.
        if (!accentExists) {
            RE::GFxValue depth;
            if (page.Invoke("getNextHighestDepth", &depth) && depth.IsNumber()) {
                std::array<RE::GFxValue, 6> args{};
                args[0].SetString(kAccentName);
                args[1].SetNumber(depth.GetNumber());
                args[2].SetNumber(static_cast<double>(g_displaySettings.inspectorX + 1));
                args[3].SetNumber(static_cast<double>(g_displaySettings.inspectorY + 1));
                args[4].SetNumber(static_cast<double>(g_displaySettings.inspectorWidth - 2));
                args[5].SetNumber(3.0);
                RE::GFxValue newField;
                if (page.Invoke("createTextField", &newField, args.data(), args.size()) &&
                    page.GetMember(kAccentName, &accent) && accent.IsObject()) {
                    RE::GFxValue value;
                    value.SetBoolean(false);
                    accent.SetMember("selectable", value);
                    accent.SetMember("border", value);
                    accent.SetMember("multiline", value);
                    value.SetBoolean(true);
                    accent.SetMember("background", value);
                    RE::GFxValue gold;
                    gold.SetNumber(0xBDA577);
                    accent.SetMember("backgroundColor", gold);
                }
            }
        }
        if (accent.IsObject()) { accent.SetMember("_visible", shown); }

        // Redrawing must not extend the confirmation time window.
        if (g_stageExecutionArm.active &&
            std::chrono::steady_clock::now() >= g_stageExecutionArm.expires) {
            g_stageExecutionArm = {};
            SetInspectorCopyFeedback("Request expired: no stage executed", false);
        }
        // Expire feedback and redraw only once.
        if (!g_inspectorCopyFeedback.empty() &&
            std::chrono::steady_clock::now() >= g_inspectorCopyFeedbackExpires) {
            g_inspectorCopyFeedback.clear();
            ++g_inspectorUiRevision;
        }

        // Copy query state to avoid holding the mutex during Scaleform calls.
        InspectorQueryState snapshot;
        {
            std::lock_guard lock(g_inspectorMutex);
            snapshot = g_inspectorState;
        }
        constexpr std::size_t kPageSize = 10;
        const std::size_t pageCount = (std::max)(std::size_t{ 1 },
            (snapshot.stages.size() + kPageSize - 1) / kPageSize);
        g_inspectorPage = (std::min)(g_inspectorPage, pageCount - 1);
        if (!snapshot.stages.empty()) {
            g_inspectorSelectedPos = (std::min)(g_inspectorSelectedPos, snapshot.stages.size() - 1);
        } else {
            g_inspectorSelectedPos = 0;
        }

        // Compact pages: adjust height to visible rows.
        // Maintain minimum room for headers, messages, and controls.
        const std::size_t first = g_inspectorPage * kPageSize;
        const std::size_t last = (std::min)(first + kPageSize, snapshot.stages.size());
        const std::size_t visibleRows = last - first;
        double actualHeight = static_cast<double>(g_displaySettings.inspectorHeight);
        if (g_displaySettings.inspectorCompactPages && !g_inspectorDetailsView) {
            const double perRow = static_cast<double>(g_displaySettings.inspectorFontSize + 5);
            actualHeight = (std::max)(190.0,
                actualHeight - static_cast<double>(kPageSize - visibleRows) * perRow);
        }
        // The detail view requires its own minimum height to prevent clipping;
        // list view preserves the behavior introduced in Step 17.
        if (g_inspectorDetailsView) {
            actualHeight = (std::max)(actualHeight, 390.0);
        } else {
            actualHeight += static_cast<double>(2 * (g_displaySettings.inspectorFontSize + 5));
        }
        RE::GFxValue newHeight;
        newHeight.SetNumber(actualHeight);
        label.SetMember("_height", newHeight);

        static std::uint64_t lastRevision = 0;
        static std::size_t lastPage = static_cast<std::size_t>(-1);
        static std::size_t lastSelected = static_cast<std::size_t>(-1);
        static bool lastDetailsView = false;
        static std::uint64_t lastUiRevision = static_cast<std::uint64_t>(-1);
        if (!created && lastRevision == snapshot.revision &&
            lastPage == g_inspectorPage && lastSelected == g_inspectorSelectedPos &&
            lastDetailsView == g_inspectorDetailsView &&
            lastUiRevision == g_inspectorUiRevision) {
            return;
        }
        lastRevision = snapshot.revision;
        lastPage = g_inspectorPage;
        lastSelected = g_inspectorSelectedPos;
        lastDetailsView = g_inspectorDetailsView;
        lastUiRevision = g_inspectorUiRevision;

        // Step 16 fix: NEVER use htmlText. In Skyrim Scaleform,
        // setting htmlText may succeed but draw the tags literally.
        // Step 15 reliably used TextField.text.
        // Apply colors with setTextFormat(start, end, format);
        // if formatting fails, plain text remains visible.
        RE::GFxValue htmlDisabled;
        htmlDisabled.SetBoolean(false);
        label.SetMember("html", htmlDisabled);

        RE::GFxValue fmt;
        a_menu->uiMovie->CreateObject(&fmt, "TextFormat");
        if (fmt.IsObject()) {
            RE::GFxValue font;
            font.SetString("Futura CondensedLight");
            fmt.SetMember("font", font);
            RE::GFxValue size;
            size.SetNumber(static_cast<double>(g_displaySettings.inspectorFontSize));
            fmt.SetMember("size", size);
            RE::GFxValue color;
            color.SetNumber(0xE6DED0);
            fmt.SetMember("color", color);
            RE::GFxValue align;
            align.SetString("left");
            fmt.SetMember("align", align);
            RE::GFxValue leading;
            leading.SetNumber(2.0);
            fmt.SetMember("leading", leading);
            label.Invoke("setNewTextFormat", nullptr, &fmt, 1);
        }

        constexpr std::uint32_t kGold = 0xDCC89F;
        constexpr std::uint32_t kGreen = 0xAED0B4;
        constexpr std::uint32_t kGray = 0xB1B0AD;
        constexpr std::uint32_t kDim = 0x898985;
        constexpr std::uint32_t kRed = 0xD9A19B;
        constexpr std::uint32_t kCyan = 0xB0C9D1;

        struct ColorSpan {
            std::size_t first;
            std::size_t last;
            std::uint32_t color;
        };
        std::vector<ColorSpan> spans;
        spans.reserve(20);
        std::string plain;
        plain.reserve(1600);
        auto appendLine = [&](std::string_view content, std::uint32_t color) {
            const std::size_t firstChar = plain.size();
            plain.append(content);
            spans.push_back({ firstChar, plain.size(), color });
            plain.push_back('\n');
        };

        appendLine(g_displaySettings.enableStageExecution ?
            "  QUEST STAGE INSPECTOR     [EXECUTION ENABLED]" :
            "  QUEST STAGE INSPECTOR     [READ ONLY]", kGold);
        if (snapshot.formID == 0) {
            appendLine("  Select an individual quest.", kGray);
        } else {
            std::string editor = snapshot.editorID;
            if (editor.size() > 45) { editor.resize(42); editor += "..."; }
            appendLine("  " + editor + "   |   CURRENT STAGE: " +
                std::to_string(snapshot.currentStage), kGray);
            if (snapshot.truncated) {
                appendLine("  Quest exceeds 96 stages (partial queries disabled).", kRed);
            } else if (snapshot.vmUnavailable) {
                appendLine("  Papyrus VM query failed.", kRed);
            } else if (snapshot.stages.empty()) {
                appendLine("  No defined stages found.", kDim);
            } else {
                std::size_t done = 0;
                std::size_t unresolved = 0;
                std::size_t failed = 0;
                for (const auto& entry : snapshot.stages) {
                    if (entry.status == StageQueryStatus::kExecuted) { ++done; }
                    if (entry.status == StageQueryStatus::kPending) { ++unresolved; }
                    if (entry.status == StageQueryStatus::kFailed) { ++failed; }
                }
                std::string summary = "  COMPLETED " + std::to_string(done) + "/" +
                    std::to_string(snapshot.stages.size()) + "    QUERIES " +
                    std::to_string(snapshot.received) + "/" +
                    std::to_string(snapshot.stages.size());
                if (unresolved || failed) {
                    summary += "    (pending " + std::to_string(unresolved) +
                        " / failed " + std::to_string(failed) + ")";
                }
                appendLine(summary, kGold);
                appendLine("  --------------------------------------------", kDim);
                if (g_inspectorDetailsView) {
                    // Selected stage details do not change quest progress, scripts, or objectives.
                    const auto& chosen = snapshot.stages[g_inspectorSelectedPos];
                    const bool actual = chosen.index == snapshot.currentStage;
                    appendLine("  STAGE DETAILS " + std::to_string(chosen.index), kGold);
                    char formBuf[9]{};
                    std::snprintf(formBuf, sizeof(formBuf), "%08X", snapshot.formID);
                    appendLine(std::string("  FormID ") + formBuf + " | Position " +
                        std::to_string(g_inspectorSelectedPos + 1) + "/" +
                        std::to_string(snapshot.stages.size()), kGray);
                    appendLine("  Current in-game stage: " +
                        std::to_string(snapshot.currentStage), kGray);
                    appendLine(std::string("  Is current stage: ") +
                        (actual ? "YES" : "NO"), actual ? kGold : kCyan);
                    const char* stateText = "QUERYING";
                    std::uint32_t stateColor = kCyan;
                    switch (chosen.status) {
                    case StageQueryStatus::kExecuted: stateText = "COMPLETED"; stateColor = kGreen; break;
                    case StageQueryStatus::kNotExecuted: stateText = "NOT COMPLETED"; stateColor = kGray; break;
                    case StageQueryStatus::kFailed: stateText = "QUERY FAILED"; stateColor = kRed; break;
                    case StageQueryStatus::kPending: break;
                    }
                    appendLine(std::string("  Status: ") + stateText, stateColor);
                    appendLine("  Quest log entry text: not available", kDim);
                    appendLine("  --------------------------------------------", kDim);
                    appendLine("  QUEST OBJECTIVES (not stage-specific)", kGold);
                    appendLine("  Objectives defined in QUST: " +
                        std::to_string(snapshot.objectiveCount), kGray);
                    for (const auto& obj : snapshot.objectivePreview) {
                        appendLine("  #" + std::to_string(obj.index) + ": " + obj.text, kGray);
                    }
                    appendLine("  --------------------------------------------", kDim);
                    appendLine("  Ctrl+Shift+" +
                        std::string(1, static_cast<char>(g_displaySettings.inspectorDetailsKey)) +
                        " return to list", kGold);
                } else {
                for (std::size_t i = first; i < last; ++i) {
                    const auto& item = snapshot.stages[i];
                    const bool current = item.index == snapshot.currentStage;
                    const auto number = std::to_string(item.index);
                    const bool selected = i == g_inspectorSelectedPos;
                    std::string row = selected ? ">> " : (current ? " > " : "   ");
                    row += "STAGE " + number;
                    row += std::string(number.size() < 4 ? 4 - number.size() : 1, ' ');
                    std::uint32_t color = kDim;
                    switch (item.status) {
                    case StageQueryStatus::kExecuted:
                        row += "[OK] DONE";
                        color = kGreen;
                        break;
                    case StageQueryStatus::kNotExecuted:
                        row += "[--] NOT DONE";
                        color = kGray;
                        break;
                    case StageQueryStatus::kFailed:
                        row += "[!] ERROR";
                        color = kRed;
                        break;
                    case StageQueryStatus::kPending:
                        row += "[...] QUERYING";
                        color = kCyan;
                        break;
                    }
                    if (current) {
                        row += "  < CURRENT >";
                        color = kGold;
                    } else if (selected) {
                        color = kCyan; // Selected is not necessarily current; use distinct highlights.
                    }
                    appendLine(row, color);
                }
                appendLine("  --------------------------------------------", kDim);
                const auto& selected = snapshot.stages[g_inspectorSelectedPos];
                std::string details = "  SELECTED " + std::to_string(selected.index) + " | ";
                switch (selected.status) {
                case StageQueryStatus::kExecuted: details += "DONE"; break;
                case StageQueryStatus::kNotExecuted: details += "NOT DONE"; break;
                case StageQueryStatus::kFailed: details += "QUERY FAILED"; break;
                case StageQueryStatus::kPending: details += "QUERYING"; break;
                }
                const bool selectionIsCurrent = selected.index == snapshot.currentStage;
                if (selectionIsCurrent) { details += " | CURRENT"; }
                appendLine(details, selectionIsCurrent ? kGold : kCyan);
                appendLine("  PAGE " + std::to_string(g_inspectorPage + 1) +
                    "/" + std::to_string(pageCount), kGold);
                appendLine("  Ctrl+Shift+" +
                    std::string(1, static_cast<char>(g_displaySettings.inspectorDetailsKey)) +
                    " view selected stage details", kCyan);
                } // end of list view
            }
        }
        appendLine("  Ctrl+Shift+" +
            std::string(1, static_cast<char>(g_displaySettings.inspectorToggleKey)) +
            " close  |  " +
            std::string(1, static_cast<char>(g_displaySettings.inspectorNextKey)) +
            "/" +
            std::string(1, static_cast<char>(g_displaySettings.inspectorPrevKey)) +
            " pages", kDim);
        StageExecutionResult executionSnapshot;
        {
            std::lock_guard lock(g_stageExecutionMutex);
            executionSnapshot = g_stageExecutionResult;
        }
        if (g_stageExecutionArm.active) {
            appendLine("  WARNING: confirm stage " +
                std::to_string(g_stageExecutionArm.stage) + " Ctrl+Shift+" +
                std::string(1, static_cast<char>(g_displaySettings.inspectorConfirmExecutionKey)) +
                " (8s)", kRed);
        } else if (executionSnapshot.inFlight && !executionSnapshot.resultReady) {
            std::string status = "  PAPYRUS: stage " + std::to_string(executionSnapshot.stage);
            if (executionSnapshot.prolongedWarningLogged) {
                status += " | NO RESPONSE: DO NOT RETRY";
            } else if (executionSnapshot.slowWarningLogged) {
                status += " | WAITING FOR SCRIPTS; NO CALLBACK";
            } else {
                status += " | PROCESSING...";
            }
            appendLine(status, executionSnapshot.slowWarningLogged ? kRed : kGold);
        } else if (!g_inspectorCopyFeedback.empty()) {
            appendLine("  " + g_inspectorCopyFeedback,
                g_inspectorCopyFeedbackSuccess ? kGreen : kRed);
        } else {
            appendLine("  Ctrl+Shift+" +
                std::string(1, static_cast<char>(g_displaySettings.inspectorSelectPrevKey)) +
                "/" +
                std::string(1, static_cast<char>(g_displaySettings.inspectorSelectNextKey)) +
                " select | " +
                std::string(1, static_cast<char>(g_displaySettings.inspectorCopyStageKey)) +
                " stage | " +
                std::string(1, static_cast<char>(g_displaySettings.inspectorCopyCommandKey)) +
                " setstage", kDim);
        }

        RE::GFxValue text;
        a_menu->uiMovie->CreateString(&text, plain.c_str());
        if (!label.SetMember("text", text)) {
            logger::warn("STEP16 FIX: failed to update panel text");
            return;
        }
        // Apply base formatting AFTER writing the text, as in Step 15.
        if (fmt.IsObject()) {
            label.Invoke("setTextFormat", nullptr, &fmt, 1);
        }

        // AS2: TextField.setTextFormat(beginIndex, endIndex, TextFormat).
        // Displayed labels are ASCII so the indices match
        // Flash character positions.
        bool colorsOk = true;
        for (const auto& span : spans) {
            if (span.first == span.last) { continue; }
            RE::GFxValue lineFmt;
            a_menu->uiMovie->CreateObject(&lineFmt, "TextFormat");
            if (!lineFmt.IsObject()) {
                colorsOk = false;
                break;
            }
            RE::GFxValue lineColor;
            lineColor.SetNumber(static_cast<double>(span.color));
            lineFmt.SetMember("color", lineColor);
            std::array<RE::GFxValue, 3> args{};
            args[0].SetNumber(static_cast<double>(span.first));
            args[1].SetNumber(static_cast<double>(span.last));
            args[2] = lineFmt;
            if (!label.Invoke("setTextFormat", nullptr, args.data(), args.size())) {
                colorsOk = false;
                break;
            }
        }
        if (!colorsOk) {
            logger::warn("STEP16 FIX: text is visible; line-by-line formatting unavailable");
        }
    }

    static void thunk(RE::JournalMenu* a_menu, float a_interval, std::uint32_t a_currentTime)
    {
        // Always invoke the game's original menu method.
        func(a_menu, a_interval, a_currentTime);
        // The MCM may write the INI from the Papyrus thread. Reload
        // ONLY here on the journal UI/AdvanceMovie thread.
        if (g_qijMcmSettingsDirty.exchange(false, std::memory_order_acq_rel)) {
            LoadDisplaySettings();
            // A configuration change cancels any armed confirmation,
            // never an execution already dispatched to Papyrus.
            g_stageExecutionArm = {};
            g_lastSelectedFormID.store(0xFFFFFFFFu, std::memory_order_relaxed);
            logger::info("STEP23 MCM: settings reloaded in JournalMenu");
        }
        ReadSelectedQuest(a_menu);
        CheckCopyHotkeys();
        UpdateCopyNotice(a_menu);
        CheckInspectorHotkeys();
        ProcessInspectorSelection();
        ProcessStageExecutionResult();
        UpdateInspectorVisual(a_menu);
    }

    static inline REL::Relocation<decltype(&thunk)> func;

    static void Install()
    {
        // Slot 0x05 is JournalMenu::AdvanceMovie on tested Skyrim SE.
        // Install the hook once after DataLoaded. Verify slot on other runtimes.
        REL::Relocation<std::uintptr_t> vtable{ RE::JournalMenu::VTABLE[0] };
        func = vtable.write_vfunc(0x05, thunk);
        logger::info("JournalMenu::AdvanceMovie hook installed");
    }
};

void OnDataLoaded()
{
    logger::info("OnDataLoaded hook fired");
    LoadDisplaySettings();

    static JournalMenuListener journalListener;
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&journalListener);
        logger::info("Journal event listener registered");
    } else {
        logger::error("Unable to access UI system");
    }

    JournalAdvanceHook::Install();

    // Simple sanity-check quest used during earlier testing.
    auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("MQ103");
    if (quest) {
        const char* editorID = quest->GetFormEditorID();
        logger::info("Sanity-check quest: EditorID={} | FormID={:08X}",
            editorID ? editorID : "(no EditorID)", quest->GetFormID());
    } else {
        logger::warn("Quest MQ103 not found");
    }

    const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    const auto name = plugin ? plugin->GetName() : "Plugin";
    if (auto* console = RE::ConsoleLog::GetSingleton()) {
        console->Print("[%s] Loaded successfully!", std::string(name).c_str());
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SetupLog();

    const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    if (!plugin) {
        logger::error("Failed to get plugin declaration");
        return false;
    }
    logger::info("{} v{} loaded", plugin->GetName(), plugin->GetVersion());

    const auto* papyrusInterface = SKSE::GetPapyrusInterface();
    if (!papyrusInterface || !papyrusInterface->Register(RegisterQIJMcmPapyrus)) {
        logger::error("STEP23 MCM: failed to register Papyrus bridge");
        return false;
    }

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("Failed to get SKSE messaging interface");
        return false;
    }

    if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
            switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                OnDataLoaded();
                break;
            default:
                break;
            }
        })) {
        logger::error("Failed to register messaging listener");
        return false;
    }

    return true;
}
