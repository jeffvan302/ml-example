#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <urlmon.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <locale>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <cwctype>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "Advapi32.lib")

namespace fs = std::filesystem;

namespace {

constexpr UINT WMU_STATUS = WM_APP + 1;
constexpr UINT WMU_LOG = WM_APP + 2;
constexpr UINT WMU_FAILED = WM_APP + 3;
constexpr UINT WMU_LAUNCHED = WM_APP + 4;
constexpr int kProgressMax = 7;
constexpr wchar_t kWindowClassName[] = L"project_python_runtime_launcher";
constexpr int kControlClose = 1;
constexpr int kControlStart = 2;
constexpr int kControlPythonSourceCombo = 100;
constexpr int kControlEnvironmentDirect = 101;
constexpr int kControlEnvironmentNamed = 102;
constexpr int kControlEnvironmentNameEdit = 103;
constexpr int kControlTorchCuda = 120;
constexpr int kControlTorchRocm = 121;
constexpr int kControlTorchXpu = 122;
constexpr int kControlTorchCpu = 123;
constexpr int kControlLaunchScript = 140;
constexpr int kControlScriptCombo = 141;
constexpr int kControlLaunchCustom = 142;
constexpr int kControlCustomCommandEdit = 143;

struct UiMessage {
    int step = 0;
    std::wstring text;
};

struct LauncherConfig {
    std::wstring python_series = L"3.12";
    std::wstring fallback_python_version = L"3.12.10";
    std::wstring python_windows_release_url = L"https://www.python.org/getit/windows/";
    std::wstring python_installer_url_template = L"https://www.python.org/ftp/python/{version}/python-{version}-amd64.exe";
    std::wstring python_embeddable_url_template = L"https://www.python.org/ftp/python/{version}/python-{version}-embed-amd64.zip";
    std::wstring runtime_dir_name = L"runtime";
    std::wstring python_base_dir_name = L"python-base";
    std::wstring python_env_dir_name = L"python";
    std::wstring cache_dir_name = L"cache";
    std::wstring logs_dir_name = L"logs";
    std::wstring python_version_file_name = L"python-version.txt";
    std::wstring torch_target_file_name = L"torch-target.txt";
    std::wstring filtered_requirements_file_name = L"requirements.filtered.txt";
    std::wstring torch_cuda_index_url = L"https://download.pytorch.org/whl/cu129";
    std::wstring torch_xpu_index_url = L"https://download.pytorch.org/whl/xpu";
    std::wstring torch_rocm_index_url = L"https://download.pytorch.org/whl/rocm6.2.4";
};

struct ProjectLayout {
    fs::path launcher_dir;
    fs::path project_root;
    fs::path runtime_dir;
    fs::path cache_dir;
    fs::path python_layout_dir;
    fs::path logs_dir;
    fs::path log_file;
    fs::path python_base_dir;
    fs::path python_env_dir;
    fs::path python_base_exe;
    fs::path python_basew_exe;
    fs::path python_exe;
    fs::path pythonw_exe;
    fs::path python_version_file;
    fs::path torch_target_file;
    fs::path filtered_requirements_file;
    fs::path python_index_cache_file;
    fs::path python_installer_file;
    fs::path python_embeddable_file;
    fs::path python_installer_log_file;
    fs::path requirements_file;
    fs::path run_config_file;
    fs::path envs_dir;
    fs::path run_script;
    fs::path pip_cache_dir;
    fs::path active_python_exe;
    fs::path active_pythonw_exe;
    fs::path active_environment_dir;
};

enum class PythonSourceKind {
    Existing,
    InstallMachine,
    Portable,
};

enum class EnvironmentKind {
    Direct,
    NamedVenv,
    NamedConda,
    PortableRuntime,
};

enum class LaunchKind {
    Script,
    PythonArgs,
};

enum class AcceleratorKind {
    NvidiaCuda,
    IntelArcXpu,
    AmdRocm,
    Cpu,
};

struct AcceleratorChoice {
    AcceleratorKind kind = AcceleratorKind::Cpu;
    std::wstring display_name = L"CPU";
    std::wstring install_tag = L"cpu";
    std::optional<std::wstring> index_url;
    std::wstring note;
};

struct PythonSourceOption {
    PythonSourceKind kind = PythonSourceKind::Portable;
    std::wstring label;
    std::wstring description;
    std::wstring version;
    fs::path python_exe;
    fs::path pythonw_exe;
    bool is_conda = false;
    std::wstring conda_name;
};

struct RequirementsSnapshot {
    std::optional<fs::path> filtered_file;
    std::wstring hash;
};

struct RunConfig {
    int schema_version = 2;
    std::wstring python_version;
    PythonSourceKind python_source_kind = PythonSourceKind::Portable;
    std::wstring python_source_label;
    fs::path python_source_path;
    fs::path python_sourcew_path;
    EnvironmentKind environment_kind = EnvironmentKind::PortableRuntime;
    std::wstring environment_name;
    fs::path environment_path;
    AcceleratorKind accelerator_kind = AcceleratorKind::Cpu;
    LaunchKind launch_kind = LaunchKind::Script;
    std::wstring launch_target;
    std::wstring requirements_hash;
    std::wstring requirements_synced_at;
};

struct WindowState {
    HWND setup_intro = nullptr;
    HWND python_source_label = nullptr;
    HWND python_source_combo = nullptr;
    HWND python_source_note = nullptr;
    HWND environment_direct_radio = nullptr;
    HWND environment_named_radio = nullptr;
    HWND environment_name_label = nullptr;
    HWND environment_name_edit = nullptr;
    HWND torch_label = nullptr;
    HWND torch_cuda_radio = nullptr;
    HWND torch_rocm_radio = nullptr;
    HWND torch_xpu_radio = nullptr;
    HWND torch_cpu_radio = nullptr;
    HWND torch_note = nullptr;
    HWND launch_script_radio = nullptr;
    HWND script_combo = nullptr;
    HWND launch_custom_radio = nullptr;
    HWND custom_command_edit = nullptr;
    HWND launch_note = nullptr;
    HWND start_button = nullptr;
    HWND status_label = nullptr;
    HWND progress_bar = nullptr;
    HWND log_edit = nullptr;
    HWND close_button = nullptr;
    HWND detail_label = nullptr;
    bool can_close = true;
    bool setup_mode = true;
    bool run_started = false;
    LauncherConfig config;
    ProjectLayout layout;
    std::vector<PythonSourceOption> python_sources;
    std::vector<fs::path> script_options;
    std::optional<RunConfig> saved_config;
    std::vector<std::wstring> extra_args;
    fs::path log_file;
    fs::path conda_executable;
    bool conda_available = false;
};

struct WorkerContext {
    LauncherConfig config;
    ProjectLayout layout;
    RunConfig run_config;
    HWND hwnd = nullptr;
    std::vector<std::wstring> extra_args;
};

std::wstring trim_copy(std::wstring value);
std::wstring lower_copy(std::wstring value);
std::string utf8_from_wide(const std::wstring& value);
std::wstring wide_from_utf8(const std::string& value);
std::runtime_error make_error(const std::wstring& message);
void fail_if(bool condition, const std::wstring& message);
std::wstring last_error_message(DWORD error);
std::wstring format_hresult_hex(HRESULT hr);
std::wstring current_timestamp_local();
void ensure_parent_directory(const fs::path& path);
void write_text_file_utf8(const fs::path& path, const std::string& content);
std::wstring read_text_file_utf8(const fs::path& path);
std::wstring hash_text_utf8(const std::wstring& value);
std::vector<std::wstring> split_lines(const std::wstring& value);
std::vector<std::wstring> split_command_line(const std::wstring& value);
std::wstring sanitize_environment_name(const std::wstring& value);
std::wstring default_environment_name(const ProjectLayout& layout);
std::wstring xml_escape(const std::wstring& value);
std::wstring xml_unescape(std::wstring value);
std::optional<std::wstring> xml_element_attributes(const std::wstring& xml, const std::wstring& tag_name);
std::optional<std::wstring> xml_attribute_value(const std::wstring& attributes, const std::wstring& attribute_name);
bool path_exists_no_throw(const fs::path& path);
bool path_is_windows_apps_alias(const fs::path& path);
std::wstring normalized_path_key(const fs::path& path);
std::optional<fs::path> find_conda_executable();
std::vector<std::pair<std::wstring, fs::path>> discover_conda_environment_prefixes(const ProjectLayout& layout);
std::optional<fs::path> find_conda_environment_prefix(const ProjectLayout& layout, const std::wstring& environment_name);
void append_log_line(const fs::path& path, const std::wstring& message);
void post_message(HWND hwnd, UINT message_id, int step, const std::wstring& text);
void post_status(HWND hwnd, int step, const std::wstring& text);
void post_log(HWND hwnd, const std::wstring& text);
void post_failed(HWND hwnd, const std::wstring& text);
void post_launched(HWND hwnd, const std::wstring& text);
fs::path executable_path();
std::wstring quote_argument(const std::wstring& argument);
std::wstring build_command_line(const fs::path& executable, const std::vector<std::wstring>& arguments);
HANDLE open_log_handle_append(const fs::path& path);
int run_process(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::optional<fs::path>& working_dir,
    const fs::path& log_file,
    bool hidden = true
);
std::wstring run_process_capture(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::optional<fs::path>& working_dir,
    const ProjectLayout& layout,
    bool allow_non_zero_exit = false
);
void start_detached_process(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::optional<fs::path>& working_dir,
    bool hidden
);
std::optional<fs::path> find_executable_on_path(const std::wstring& executable_name);
std::wstring quote_powershell_literal(const std::wstring& value);
void download_file(const std::wstring& url, const fs::path& destination, const fs::path& log_file);
std::wstring replace_all(std::wstring text, std::wstring_view from, std::wstring_view to);
std::wstring read_remote_text(const std::wstring& url, const fs::path& cache_file, const fs::path& log_file);
std::vector<int> parse_version_numbers(const std::wstring& version);
bool version_less(const std::wstring& left, const std::wstring& right);
std::wstring resolve_latest_python_version(const LauncherConfig& config, const ProjectLayout& layout);
std::optional<std::wstring> read_registry_default_value(HKEY root, const std::wstring& subkey);
std::optional<std::wstring> read_registry_string_value(HKEY root, const std::wstring& subkey, const std::wstring& value_name);
std::optional<fs::path> find_registered_python_install_dir(const LauncherConfig& config);
std::optional<PythonSourceOption> probe_python_source(
    const fs::path& python_exe,
    const ProjectLayout& layout,
    const std::wstring& python_series,
    const std::wstring& label_prefix,
    PythonSourceKind kind
);
std::vector<PythonSourceOption> discover_python_sources(const LauncherConfig& config, const ProjectLayout& layout);
std::vector<fs::path> discover_script_options(const ProjectLayout& layout);
std::optional<RunConfig> load_run_config(const ProjectLayout& layout);
void write_run_config(const ProjectLayout& layout, const RunConfig& run_config);
bool run_config_is_usable(const RunConfig& run_config, const ProjectLayout& layout);
void configure_active_python(
    ProjectLayout& layout,
    const fs::path& python_exe,
    const fs::path& pythonw_exe,
    const std::optional<fs::path>& environment_dir
);
bool paths_equivalent_loose(const fs::path& left, const fs::path& right);
void cleanup_local_python_registration(const LauncherConfig& config, const ProjectLayout& layout);
bool command_exists_on_path(const std::wstring& executable_name);
std::vector<std::wstring> enumerate_display_adapter_names();
bool contains_case_insensitive(const std::wstring& text, const std::wstring& needle);
std::optional<std::wstring> env_value(const std::wstring& name);
AcceleratorChoice detect_accelerator(const LauncherConfig& config, const ProjectLayout& layout);
AcceleratorChoice accelerator_choice_for_kind(const LauncherConfig& config, AcceleratorKind kind);
std::wstring python_installer_url_for_version(const LauncherConfig& config, const std::wstring& version);
std::wstring python_embeddable_url_for_version(const LauncherConfig& config, const std::wstring& version);
fs::path windows_system_executable(const std::wstring& executable_name);
void extract_zip_archive(const fs::path& archive_file, const fs::path& destination_dir, const fs::path& log_file);
void download_python_layout(const LauncherConfig& config, const ProjectLayout& layout);
std::optional<std::wstring> python_package_cache_version(const fs::path& candidate_path, const LauncherConfig& config);
std::optional<fs::path> locate_python_runtime_msi(
    const LauncherConfig& config,
    const ProjectLayout& layout,
    const std::wstring& file_name
);
void extract_python_runtime_msi(const ProjectLayout& layout, const fs::path& msi_path);
void write_embeddable_runtime_pth(const LauncherConfig& config, const ProjectLayout& layout);
void set_child_environment(const ProjectLayout& layout);
bool python_supports_tkinter(const fs::path& python_exe, const fs::path& log_file);
bool python_supports_launcher_runtime(const fs::path& python_exe, const fs::path& log_file);
bool python_has_importable_torch_stack(const fs::path& python_exe, const fs::path& log_file);
void create_virtual_environment(const fs::path& base_python_exe, const fs::path& environment_dir, const ProjectLayout& layout);
void create_local_venv(const fs::path& base_python_exe, const ProjectLayout& layout);
void create_or_update_conda_environment(const ProjectLayout& layout, const std::wstring& environment_name, const std::wstring& python_version);
void repair_base_python(const ProjectLayout& layout);
void install_base_python(const LauncherConfig& config, const ProjectLayout& layout, const std::wstring& python_version);
void install_machine_python(const LauncherConfig& config, const ProjectLayout& layout, const std::wstring& python_version);
void ensure_python_runtime(const LauncherConfig& config, const ProjectLayout& layout, const std::wstring& python_version);
void run_python(const ProjectLayout& layout, const std::vector<std::wstring>& arguments, const std::optional<fs::path>& cwd);
void ensure_packaging_tools(const ProjectLayout& layout);
std::wstring canonical_requirement_name(const std::wstring& requirement_line);
std::optional<fs::path> prepare_filtered_requirements(const ProjectLayout& layout);
RequirementsSnapshot collect_requirements_snapshot(const ProjectLayout& layout);
std::vector<std::wstring> pip_install_arguments(
    const std::vector<std::wstring>& packages,
    const std::optional<std::wstring>& index_url
);
void uninstall_torch_packages(const ProjectLayout& layout);
void install_torch_stack(const ProjectLayout& layout, const AcceleratorChoice& accelerator);
void install_project_requirements(const ProjectLayout& layout);
void sync_project_requirements(const ProjectLayout& layout, RunConfig& run_config);
void launch_selected_target(const ProjectLayout& layout, const RunConfig& run_config, const std::vector<std::wstring>& extra_args);
ProjectLayout resolve_project_layout(const LauncherConfig& config);
RunConfig collect_run_config_from_controls(HWND hwnd, const WindowState& state);
void update_setup_controls(HWND hwnd);
void set_window_mode(HWND hwnd, bool setup_mode);
void start_launcher_run(HWND hwnd, const RunConfig& run_config, bool confirm_direct_install = true);
void bootstrap_worker(WorkerContext context);
void append_log_to_edit(HWND edit_control, const std::wstring& text);
LRESULT CALLBACK launcher_window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
HWND create_launcher_window(HINSTANCE instance, const fs::path& log_file);
std::vector<std::wstring> parse_extra_arguments();

std::wstring trim_copy(std::wstring value) {
    const auto is_space = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    while (!value.empty() && is_space(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(value.back())) {
        value.pop_back();
    }
    return value;
}

std::wstring lower_copy(std::wstring value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); }
    );
    return value;
}

std::string utf8_from_wide(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        throw std::runtime_error("Failed to convert UTF-16 to UTF-8.");
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr);
    if (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    return result;
}

std::wstring wide_from_utf8(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error("Failed to convert UTF-8 to UTF-16.");
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), length);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

std::runtime_error make_error(const std::wstring& message) {
    return std::runtime_error(utf8_from_wide(message));
}

void fail_if(bool condition, const std::wstring& message) {
    if (condition) {
        throw make_error(message);
    }
}

std::wstring last_error_message(DWORD error) {
    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );
    std::wstring message = size && buffer ? buffer : L"Unknown Windows error.";
    if (buffer) {
        LocalFree(buffer);
    }
    return trim_copy(message);
}

std::wstring format_hresult_hex(HRESULT hr) {
    wchar_t buffer[11]{};
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%08lX", static_cast<unsigned long>(hr));
    return buffer;
}

std::wstring current_timestamp_local() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u",
        static_cast<unsigned>(st.wYear),
        static_cast<unsigned>(st.wMonth),
        static_cast<unsigned>(st.wDay),
        static_cast<unsigned>(st.wHour),
        static_cast<unsigned>(st.wMinute),
        static_cast<unsigned>(st.wSecond)
    );
    return buffer;
}

void ensure_parent_directory(const fs::path& path) {
    if (!path.empty()) {
        fs::create_directories(path.parent_path());
    }
}

void write_text_file_utf8(const fs::path& path, const std::string& content) {
    ensure_parent_directory(path);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw make_error(L"Could not open " + path.wstring() + L" for writing.");
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::wstring read_text_file_utf8(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return L"";
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return wide_from_utf8(buffer.str());
}

std::wstring hash_text_utf8(const std::wstring& value) {
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    const std::string encoded = utf8_from_wide(value);
    for (const unsigned char byte : encoded) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kPrime;
    }
    wchar_t buffer[17]{};
    swprintf_s(buffer, L"%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::vector<std::wstring> split_lines(const std::wstring& value) {
    std::vector<std::wstring> lines;
    std::wstringstream stream(value);
    std::wstring line;
    while (std::getline(stream, line)) {
        lines.push_back(trim_copy(line));
    }
    return lines;
}

std::vector<std::wstring> split_command_line(const std::wstring& value) {
    const std::wstring trimmed = trim_copy(value);
    if (trimmed.empty()) {
        return {};
    }
    const std::wstring command_line = L"launcher.exe " + trimmed;
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(command_line.c_str(), &argument_count);
    fail_if(arguments == nullptr, L"Could not parse the custom Python command.");
    std::vector<std::wstring> result;
    for (int index = 1; index < argument_count; ++index) {
        result.emplace_back(arguments[index]);
    }
    LocalFree(arguments);
    return result;
}

std::wstring sanitize_environment_name(const std::wstring& value) {
    std::wstring sanitized;
    sanitized.reserve(value.size());
    for (const wchar_t ch : value) {
        if (std::iswalnum(ch) || ch == L'-' || ch == L'_') {
            sanitized += ch;
        } else if (ch == L' ' || ch == L'.') {
            sanitized += L'-';
        }
    }
    while (!sanitized.empty() && (sanitized.front() == L'-' || sanitized.front() == L'_')) {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && (sanitized.back() == L'-' || sanitized.back() == L'_')) {
        sanitized.pop_back();
    }
    return sanitized;
}

std::wstring default_environment_name(const ProjectLayout& layout) {
    const std::wstring base_name = sanitize_environment_name(layout.project_root.filename().wstring());
    if (!base_name.empty()) {
        return base_name + L"-312";
    }
    return L"project-312";
}

std::wstring xml_escape(const std::wstring& value) {
    std::wstring escaped = value;
    escaped = replace_all(std::move(escaped), L"&", L"&amp;");
    escaped = replace_all(std::move(escaped), L"\"", L"&quot;");
    escaped = replace_all(std::move(escaped), L"'", L"&apos;");
    escaped = replace_all(std::move(escaped), L"<", L"&lt;");
    escaped = replace_all(std::move(escaped), L">", L"&gt;");
    return escaped;
}

std::wstring xml_unescape(std::wstring value) {
    value = replace_all(std::move(value), L"&quot;", L"\"");
    value = replace_all(std::move(value), L"&apos;", L"'");
    value = replace_all(std::move(value), L"&lt;", L"<");
    value = replace_all(std::move(value), L"&gt;", L">");
    value = replace_all(std::move(value), L"&amp;", L"&");
    return value;
}

std::optional<std::wstring> xml_element_attributes(const std::wstring& xml, const std::wstring& tag_name) {
    const std::wstring marker = L"<" + tag_name + L" ";
    const size_t start = xml.find(marker);
    if (start == std::wstring::npos) {
        return std::nullopt;
    }
    const size_t end = xml.find(L"/>", start);
    if (end == std::wstring::npos) {
        return std::nullopt;
    }
    return xml.substr(start + marker.size(), end - (start + marker.size()));
}

std::optional<std::wstring> xml_attribute_value(const std::wstring& attributes, const std::wstring& attribute_name) {
    const std::wstring marker = attribute_name + L"=\"";
    const size_t start = attributes.find(marker);
    if (start == std::wstring::npos) {
        return std::nullopt;
    }
    const size_t value_start = start + marker.size();
    const size_t value_end = attributes.find(L"\"", value_start);
    if (value_end == std::wstring::npos) {
        return std::nullopt;
    }
    return xml_unescape(attributes.substr(value_start, value_end - value_start));
}

bool path_exists_no_throw(const fs::path& path) {
    std::error_code error;
    const bool exists = fs::exists(path, error);
    return !error && exists;
}

bool path_is_windows_apps_alias(const fs::path& path) {
    const std::wstring lowered = lower_copy(path.wstring());
    return lowered.find(L"\\appdata\\local\\microsoft\\windowsapps\\") != std::wstring::npos;
}

std::wstring normalized_path_key(const fs::path& path) {
    std::wstring text = lower_copy(path.lexically_normal().wstring());
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) {
        text.pop_back();
    }
    return text;
}

std::optional<fs::path> find_conda_executable() {
    if (const auto on_path = find_executable_on_path(L"conda.exe")) {
        return on_path;
    }

    std::vector<fs::path> roots;
    if (const auto user_profile = env_value(L"USERPROFILE")) {
        roots.push_back(*user_profile);
    }
    if (const auto local_app_data = env_value(L"LOCALAPPDATA")) {
        roots.push_back(*local_app_data);
    }
    if (const auto program_data = env_value(L"PROGRAMDATA")) {
        roots.push_back(*program_data);
    }

    const std::vector<std::wstring> install_names = {
        L"miniconda3",
        L"anaconda3",
        L"miniforge3",
        L"mambaforge",
        L"micromamba"
    };

    for (const auto& root : roots) {
        for (const auto& install_name : install_names) {
            const fs::path candidate = root / install_name / "Scripts" / "conda.exe";
            if (path_exists_no_throw(candidate)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::pair<std::wstring, fs::path>> discover_conda_environment_prefixes(const ProjectLayout& layout) {
    std::vector<std::pair<std::wstring, fs::path>> environments;
    const auto conda_exe = find_conda_executable();
    if (!conda_exe.has_value()) {
        return environments;
    }

    auto append_unique = [&](std::wstring name, fs::path prefix) {
        if (name.empty() || prefix.empty() || !path_exists_no_throw(prefix / "python.exe")) {
            return;
        }
        for (const auto& existing : environments) {
            if (paths_equivalent_loose(existing.second, prefix)) {
                return;
            }
        }
        environments.emplace_back(std::move(name), std::move(prefix));
    };

    try {
        const std::wstring listing = run_process_capture(*conda_exe, {L"env", L"list"}, std::nullopt, layout, true);
        for (const auto& raw_line : split_lines(listing)) {
            const std::wstring line = trim_copy(raw_line);
            if (line.empty() || line.front() == L'#') {
                continue;
            }
            std::wsmatch match;
            const std::wregex path_pattern(LR"(([A-Za-z]:\\.+))", std::regex::icase);
            if (!std::regex_search(line, match, path_pattern) || match.size() < 2) {
                continue;
            }

            const fs::path prefix = trim_copy(match[1].str());
            std::wstring name = trim_copy(line.substr(0, static_cast<size_t>(match.position(1))));
            name = replace_all(name, L"*", L"");
            name = trim_copy(name);
            if (name.empty()) {
                name = prefix.filename().wstring();
            }
            if (name.empty()) {
                name = L"base";
            }
            append_unique(name, prefix);
        }
    } catch (const std::exception& exc) {
        append_log_line(layout.log_file, L"Could not enumerate conda environments: " + wide_from_utf8(exc.what()));
    }

    const fs::path conda_root = conda_exe->parent_path().parent_path();
    append_unique(L"base", conda_root);

    const std::vector<fs::path> env_roots = {
        conda_root / "envs",
        env_value(L"USERPROFILE").has_value() ? fs::path(*env_value(L"USERPROFILE")) / ".conda" / "envs" : fs::path()
    };

    for (const auto& env_root : env_roots) {
        if (env_root.empty() || !path_exists_no_throw(env_root)) {
            continue;
        }
        std::error_code iter_error;
        for (fs::directory_iterator it(env_root, fs::directory_options::skip_permission_denied, iter_error);
             it != fs::directory_iterator();
             it.increment(iter_error)) {
            if (iter_error) {
                iter_error.clear();
                continue;
            }
            if (!it->is_directory(iter_error)) {
                if (iter_error) {
                    iter_error.clear();
                }
                continue;
            }
            append_unique(it->path().filename().wstring(), it->path());
        }
    }

    std::sort(
        environments.begin(),
        environments.end(),
        [](const auto& left, const auto& right) {
            return lower_copy(left.first) < lower_copy(right.first);
        }
    );
    return environments;
}

std::optional<fs::path> find_conda_environment_prefix(const ProjectLayout& layout, const std::wstring& environment_name) {
    const std::wstring target = lower_copy(trim_copy(environment_name));
    for (const auto& environment : discover_conda_environment_prefixes(layout)) {
        if (lower_copy(environment.first) == target) {
            return environment.second;
        }
    }
    return std::nullopt;
}

void append_log_line(const fs::path& path, const std::wstring& message) {
    ensure_parent_directory(path);
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream) {
        return;
    }
    const std::string encoded = utf8_from_wide(L"[" + current_timestamp_local() + L"] " + message + L"\r\n");
    stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
}

void post_message(HWND hwnd, UINT message_id, int step, const std::wstring& text) {
    auto* payload = new UiMessage{step, text};
    PostMessageW(hwnd, message_id, 0, reinterpret_cast<LPARAM>(payload));
}

void post_status(HWND hwnd, int step, const std::wstring& text) {
    post_message(hwnd, WMU_STATUS, step, text);
}

void post_log(HWND hwnd, const std::wstring& text) {
    post_message(hwnd, WMU_LOG, 0, text);
}

void post_failed(HWND hwnd, const std::wstring& text) {
    post_message(hwnd, WMU_FAILED, kProgressMax, text);
}

void post_launched(HWND hwnd, const std::wstring& text) {
    post_message(hwnd, WMU_LAUNCHED, kProgressMax, text);
}

fs::path executable_path() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        fail_if(written == 0, L"Could not determine the launcher executable path: " + last_error_message(GetLastError()));
        if (written < buffer.size() - 1) {
            buffer.resize(written);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring quote_argument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    const bool needs_quotes = argument.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needs_quotes) {
        return argument;
    }
    std::wstring quoted = L"\"";
    for (size_t index = 0; index < argument.size(); ++index) {
        const wchar_t ch = argument[index];
        if (ch == L'\\') {
            size_t slash_count = 0;
            while (index < argument.size() && argument[index] == L'\\') {
                ++slash_count;
                ++index;
            }
            if (index == argument.size()) {
                quoted.append(slash_count * 2, L'\\');
                break;
            }
            if (argument[index] == L'"') {
                quoted.append(slash_count * 2 + 1, L'\\');
                quoted += L'"';
            } else {
                quoted.append(slash_count, L'\\');
                quoted += argument[index];
            }
        } else if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += L"\"";
    return quoted;
}

std::wstring build_command_line(const fs::path& executable, const std::vector<std::wstring>& arguments) {
    std::wstring command_line = quote_argument(executable.wstring());
    for (const auto& argument : arguments) {
        command_line += L" ";
        command_line += quote_argument(argument);
    }
    return command_line;
}

HANDLE open_log_handle_append(const fs::path& path) {
    ensure_parent_directory(path);
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE handle = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &security,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    fail_if(handle == INVALID_HANDLE_VALUE, L"Could not open " + path.wstring() + L" for logging: " + last_error_message(GetLastError()));
    SetFilePointer(handle, 0, nullptr, FILE_END);
    return handle;
}

int run_process(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::optional<fs::path>& working_dir,
    const fs::path& log_file,
    bool hidden
) {
    fail_if(!fs::exists(executable), L"Executable not found: " + executable.wstring());

    std::wstring command_line = build_command_line(executable, arguments);
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    HANDLE log_handle = open_log_handle_append(log_file);
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (stdin_handle == nullptr || stdin_handle == INVALID_HANDLE_VALUE) {
        stdin_handle = nullptr;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_handle;
    startup.hStdOutput = log_handle;
    startup.hStdError = log_handle;
    if (hidden) {
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }

    PROCESS_INFORMATION process{};
    const DWORD flags = hidden ? CREATE_NO_WINDOW : 0;
    const std::wstring cwd = working_dir ? working_dir->wstring() : L"";
    const BOOL created = CreateProcessW(
        executable.c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        flags,
        nullptr,
        working_dir ? cwd.c_str() : nullptr,
        &startup,
        &process
    );
    CloseHandle(log_handle);

    fail_if(!created, L"Could not start " + executable.wstring() + L": " + last_error_message(GetLastError()));

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}

std::wstring run_process_capture(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::optional<fs::path>& working_dir,
    const ProjectLayout& layout,
    bool allow_non_zero_exit
) {
    const DWORD tick_count = GetTickCount();
    const fs::path capture_file = layout.cache_dir / (L"capture-" + wide_from_utf8(std::to_string(tick_count)) + L".log");
    const int exit_code = run_process(executable, arguments, working_dir, capture_file, true);
    const std::wstring output = read_text_file_utf8(capture_file);
    std::error_code fs_error;
    fs::remove(capture_file, fs_error);
    if (exit_code != 0 && !allow_non_zero_exit) {
        throw make_error(
            L"Command failed with exit code "
            + wide_from_utf8(std::to_string(exit_code))
            + L" while probing "
            + executable.wstring()
        );
    }
    return output;
}

void start_detached_process(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::optional<fs::path>& working_dir,
    bool hidden
) {
    fail_if(!fs::exists(executable), L"Executable not found: " + executable.wstring());

    std::wstring command_line = build_command_line(executable, arguments);
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hidden) {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }

    PROCESS_INFORMATION process{};
    const DWORD flags = hidden ? CREATE_NO_WINDOW : 0;
    const std::wstring cwd = working_dir ? working_dir->wstring() : L"";
    const BOOL created = CreateProcessW(
        executable.c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        FALSE,
        flags,
        nullptr,
        working_dir ? cwd.c_str() : nullptr,
        &startup,
        &process
    );
    fail_if(!created, L"Could not launch " + executable.wstring() + L": " + last_error_message(GetLastError()));

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

std::optional<fs::path> find_executable_on_path(const std::wstring& executable_name) {
    const DWORD required = SearchPathW(nullptr, executable_name.c_str(), nullptr, 0, nullptr, nullptr);
    if (required == 0) {
        return std::nullopt;
    }
    std::wstring buffer(required, L'\0');
    const DWORD written = SearchPathW(nullptr, executable_name.c_str(), nullptr, required, buffer.data(), nullptr);
    if (written == 0 || written >= required) {
        return std::nullopt;
    }
    buffer.resize(written);
    return fs::path(buffer);
}

std::wstring quote_powershell_literal(const std::wstring& value) {
    std::wstring quoted = L"'";
    for (const wchar_t ch : value) {
        if (ch == L'\'') {
            quoted += L"''";
        } else {
            quoted += ch;
        }
    }
    quoted += L"'";
    return quoted;
}

void download_file(const std::wstring& url, const fs::path& destination, const fs::path& log_file) {
    ensure_parent_directory(destination);
    const fs::path temp_destination = destination.wstring() + L".part";
    if (fs::exists(temp_destination)) {
        std::error_code ignore;
        fs::remove(temp_destination, ignore);
    }

    append_log_line(log_file, L"Downloading " + url + L" -> " + destination.wstring());

    const auto finalize_download = [&]() {
        fail_if(!fs::exists(temp_destination), L"Download finished, but the temporary file is missing: " + temp_destination.wstring());
        if (fs::exists(destination)) {
            std::error_code ignore;
            fs::remove(destination, ignore);
        }
        fs::rename(temp_destination, destination);
    };

    const HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), temp_destination.c_str(), 0, nullptr);
    if (SUCCEEDED(hr)) {
        finalize_download();
        append_log_line(log_file, L"Download completed with URLMON.");
        return;
    }

    append_log_line(log_file, L"URLMON download failed with HRESULT 0x" + format_hresult_hex(hr) + L". Trying fallback download methods.");
    if (fs::exists(temp_destination)) {
        std::error_code ignore;
        fs::remove(temp_destination, ignore);
    }

    if (const auto curl_path = find_executable_on_path(L"curl.exe")) {
        append_log_line(log_file, L"Trying curl fallback with " + curl_path->wstring());
        const int curl_exit = run_process(
            *curl_path,
            {
                L"-L",
                L"--fail",
                L"--retry", L"3",
                L"--retry-delay", L"2",
                L"--output", temp_destination.wstring(),
                url
            },
            std::nullopt,
            log_file,
            true
        );
        if (curl_exit == 0 && fs::exists(temp_destination)) {
            finalize_download();
            append_log_line(log_file, L"Download completed with curl.");
            return;
        }
        append_log_line(log_file, L"curl fallback failed with exit code " + wide_from_utf8(std::to_string(curl_exit)) + L".");
        if (fs::exists(temp_destination)) {
            std::error_code ignore;
            fs::remove(temp_destination, ignore);
        }
    } else {
        append_log_line(log_file, L"curl.exe was not found on PATH.");
    }

    if (const auto powershell_path = find_executable_on_path(L"powershell.exe")) {
        append_log_line(log_file, L"Trying PowerShell Invoke-WebRequest fallback with " + powershell_path->wstring());
        const std::wstring command =
            L"$ProgressPreference='SilentlyContinue'; "
            L"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
            L"Invoke-WebRequest -Uri " + quote_powershell_literal(url)
            + L" -OutFile " + quote_powershell_literal(temp_destination.wstring())
            + L" -UseBasicParsing";
        const int powershell_exit = run_process(
            *powershell_path,
            {
                L"-NoProfile",
                L"-ExecutionPolicy", L"Bypass",
                L"-Command", command
            },
            std::nullopt,
            log_file,
            true
        );
        if (powershell_exit == 0 && fs::exists(temp_destination)) {
            finalize_download();
            append_log_line(log_file, L"Download completed with PowerShell.");
            return;
        }
        append_log_line(log_file, L"PowerShell fallback failed with exit code " + wide_from_utf8(std::to_string(powershell_exit)) + L".");
        if (fs::exists(temp_destination)) {
            std::error_code ignore;
            fs::remove(temp_destination, ignore);
        }
    } else {
        append_log_line(log_file, L"powershell.exe was not found on PATH.");
    }

    throw make_error(
        L"Download failed for "
        + url
        + L". URLMON returned HRESULT 0x"
        + format_hresult_hex(hr)
        + L", and the fallback download methods did not succeed either."
    );
}

std::wstring replace_all(std::wstring text, std::wstring_view from, std::wstring_view to) {
    size_t position = 0;
    while ((position = text.find(from, position)) != std::wstring::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::wstring read_remote_text(const std::wstring& url, const fs::path& cache_file, const fs::path& log_file) {
    try {
        download_file(url, cache_file, log_file);
        return read_text_file_utf8(cache_file);
    } catch (...) {
        if (fs::exists(cache_file)) {
            append_log_line(log_file, L"Falling back to cached response at " + cache_file.wstring());
            return read_text_file_utf8(cache_file);
        }
        throw;
    }
}

std::vector<int> parse_version_numbers(const std::wstring& version) {
    std::vector<int> parts;
    std::wstring current;
    for (const wchar_t ch : version) {
        if (ch == L'.') {
            if (!current.empty()) {
                parts.push_back(std::stoi(current));
                current.clear();
            }
            continue;
        }
        if (!std::iswdigit(ch)) {
            break;
        }
        current += ch;
    }
    if (!current.empty()) {
        parts.push_back(std::stoi(current));
    }
    return parts;
}

bool version_less(const std::wstring& left, const std::wstring& right) {
    const auto left_parts = parse_version_numbers(left);
    const auto right_parts = parse_version_numbers(right);
    const size_t count = std::max(left_parts.size(), right_parts.size());
    for (size_t index = 0; index < count; ++index) {
        const int left_value = index < left_parts.size() ? left_parts[index] : 0;
        const int right_value = index < right_parts.size() ? right_parts[index] : 0;
        if (left_value < right_value) {
            return true;
        }
        if (left_value > right_value) {
            return false;
        }
    }
    return false;
}

std::wstring resolve_latest_python_version(const LauncherConfig& config, const ProjectLayout& layout) {
    append_log_line(
        layout.log_file,
        L"Resolving the latest Windows-installable Python "
        + config.python_series
        + L" release from "
        + config.python_windows_release_url
    );
    std::wstring latest = config.fallback_python_version;
    try {
        const std::wstring listing = read_remote_text(config.python_windows_release_url, layout.python_index_cache_file, layout.log_file);
        const std::wregex pattern(L"Python (" + config.python_series + L"(\\.\\d+))");
        for (std::wsregex_iterator it(listing.begin(), listing.end(), pattern), end; it != end; ++it) {
            const std::wstring candidate = (*it)[1].str();
            if (version_less(latest, candidate)) {
                latest = candidate;
            }
        }
    } catch (const std::exception& exc) {
        append_log_line(layout.log_file, L"Could not refresh the Windows release listing. Using fallback " + latest + L".");
        append_log_line(layout.log_file, wide_from_utf8(exc.what()));
    }
    append_log_line(layout.log_file, L"Selected Windows-installable Python runtime version " + latest);
    return latest;
}

std::optional<std::wstring> read_registry_default_value(HKEY root, const std::wstring& subkey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LONG size_result = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &size);
    if (size_result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    const LONG read_result = RegQueryValueExW(
        key,
        nullptr,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer.data()),
        &size
    );
    RegCloseKey(key);
    if (read_result != ERROR_SUCCESS) {
        return std::nullopt;
    }
    if (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return trim_copy(buffer);
}

std::optional<std::wstring> read_registry_string_value(HKEY root, const std::wstring& subkey, const std::wstring& value_name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LONG size_result = RegQueryValueExW(key, value_name.c_str(), nullptr, &type, nullptr, &size);
    if (size_result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    const LONG read_result = RegQueryValueExW(
        key,
        value_name.c_str(),
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer.data()),
        &size
    );
    RegCloseKey(key);
    if (read_result != ERROR_SUCCESS) {
        return std::nullopt;
    }
    if (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return trim_copy(buffer);
}

std::optional<fs::path> find_registered_python_install_dir(const LauncherConfig& config) {
    const std::wstring subkey = L"Software\\Python\\PythonCore\\" + config.python_series + L"\\InstallPath";
    if (const auto value = read_registry_default_value(HKEY_CURRENT_USER, subkey)) {
        return fs::path(*value);
    }
    if (const auto value = read_registry_default_value(HKEY_LOCAL_MACHINE, subkey)) {
        return fs::path(*value);
    }
    const std::wstring wow_subkey = L"Software\\WOW6432Node\\Python\\PythonCore\\" + config.python_series + L"\\InstallPath";
    if (const auto value = read_registry_default_value(HKEY_LOCAL_MACHINE, wow_subkey)) {
        return fs::path(*value);
    }
    return std::nullopt;
}

std::optional<PythonSourceOption> probe_python_source(
    const fs::path& python_exe,
    const ProjectLayout& layout,
    const std::wstring& python_series,
    const std::wstring& label_prefix,
    PythonSourceKind kind
) {
    if (python_exe.empty()) {
        return std::nullopt;
    }
    if (path_is_windows_apps_alias(python_exe)) {
        append_log_line(layout.log_file, L"Skipping Windows App Execution Alias stub: " + python_exe.wstring());
        return std::nullopt;
    }
    if (!path_exists_no_throw(python_exe)) {
        return std::nullopt;
    }

    std::wstring output;
    try {
        output = run_process_capture(
            python_exe,
            {
                L"-c",
                L"import sys; print(sys.version.split()[0]); print(sys.executable)"
            },
            std::nullopt,
            layout
        );
    } catch (const std::exception& exc) {
        append_log_line(layout.log_file, L"Skipping Python probe for " + python_exe.wstring() + L": " + wide_from_utf8(exc.what()));
        return std::nullopt;
    }

    const auto lines = split_lines(output);
    if (lines.size() < 2) {
        append_log_line(layout.log_file, L"Skipping Python probe with incomplete output: " + python_exe.wstring());
        return std::nullopt;
    }

    const std::wstring version = trim_copy(lines[0]);
    if (version.rfind(python_series, 0) != 0) {
        return std::nullopt;
    }

    const fs::path resolved_python = fs::path(trim_copy(lines[1]));
    PythonSourceOption option{};
    option.kind = kind;
    option.version = version;
    option.python_exe = resolved_python;
    option.pythonw_exe = resolved_python.parent_path() / "pythonw.exe";
    option.description = resolved_python.wstring();
    option.label = label_prefix + version + L"  (" + resolved_python.wstring() + L")";
    return option;
}

std::vector<PythonSourceOption> discover_python_sources(const LauncherConfig& config, const ProjectLayout& layout) {
    std::vector<PythonSourceOption> results;
    auto append_unique = [&](const std::optional<PythonSourceOption>& candidate) {
        if (!candidate.has_value()) {
            return;
        }
        const std::wstring candidate_key = normalized_path_key(candidate->python_exe);
        for (const auto& existing : results) {
            if (normalized_path_key(existing.python_exe) == candidate_key) {
                return;
            }
        }
        results.push_back(*candidate);
    };

    for (const auto& conda_environment : discover_conda_environment_prefixes(layout)) {
        auto option = probe_python_source(
            conda_environment.second / "python.exe",
            layout,
            config.python_series,
            L"Conda env " + conda_environment.first + L" - Python ",
            PythonSourceKind::Existing
        );
        if (option.has_value()) {
            option->is_conda = true;
            option->conda_name = conda_environment.first;
            option->description = L"Conda environment at " + conda_environment.second.wstring();
            append_unique(option);
        }
    }

    if (const auto py_launcher = find_executable_on_path(L"py.exe")) {
        try {
            const std::wstring listing = run_process_capture(*py_launcher, {L"-0p"}, std::nullopt, layout, true);
            for (const auto& line : split_lines(listing)) {
                std::wsmatch match;
                const std::wregex path_pattern(LR"(([A-Za-z]:\\[^:\r\n]*python(?:w)?\.exe))", std::regex::icase);
                if (std::regex_search(line, match, path_pattern) && match.size() > 1) {
                    append_unique(
                        probe_python_source(
                            fs::path(match[1].str()),
                            layout,
                            config.python_series,
                            L"Detected Python ",
                            PythonSourceKind::Existing
                        )
                    );
                }
            }
        } catch (const std::exception& exc) {
            append_log_line(layout.log_file, L"Could not enumerate py.exe runtimes: " + wide_from_utf8(exc.what()));
        }
    }

    if (const auto local_app_data = env_value(L"LOCALAPPDATA")) {
        append_unique(
            probe_python_source(
                fs::path(*local_app_data) / "Programs" / "Python" / "Python312" / "python.exe",
                layout,
                config.python_series,
                L"Detected Python ",
                PythonSourceKind::Existing
            )
        );
    }
    append_unique(
        probe_python_source(
            fs::path(LR"(C:\Program Files)") / L"Python312\\python.exe",
            layout,
            config.python_series,
            L"Detected Python ",
            PythonSourceKind::Existing
        )
    );
    if (const auto registry_install = find_registered_python_install_dir(config)) {
        append_unique(probe_python_source(*registry_install / "python.exe", layout, config.python_series, L"Detected Python ", PythonSourceKind::Existing));
    }
    if (const auto path_python = find_executable_on_path(L"python.exe")) {
        append_unique(probe_python_source(*path_python, layout, config.python_series, L"Detected Python ", PythonSourceKind::Existing));
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const PythonSourceOption& left, const PythonSourceOption& right) {
            if (left.version != right.version) {
                return version_less(right.version, left.version);
            }
            return lower_copy(left.python_exe.wstring()) < lower_copy(right.python_exe.wstring());
        }
    );

    PythonSourceOption machine_install{};
    machine_install.kind = PythonSourceKind::InstallMachine;
    if (find_conda_executable().has_value()) {
        machine_install.label = L"Create a new Python 3.12.x conda environment";
        machine_install.description = L"Conda is installed, so the launcher will create or update a named conda environment.";
    } else {
        machine_install.label = L"Install Python 3.12.x on this machine";
        machine_install.description = L"Downloads the official installer and registers Python for this user.";
    }
    results.push_back(machine_install);

    PythonSourceOption portable{};
    portable.kind = PythonSourceKind::Portable;
    portable.label = L"Use the portable runtime inside the runtime folder";
    portable.description = L"Keeps the launcher self-contained and does not depend on a machine-wide Python install.";
    results.push_back(portable);

    return results;
}

std::vector<fs::path> discover_script_options(const ProjectLayout& layout) {
    std::vector<fs::path> scripts;
    std::error_code iter_error;
    for (fs::directory_iterator it(layout.project_root, fs::directory_options::skip_permission_denied, iter_error);
         it != fs::directory_iterator();
         it.increment(iter_error)) {
        if (iter_error) {
            iter_error.clear();
            continue;
        }
        if (!it->is_regular_file(iter_error)) {
            if (iter_error) {
                iter_error.clear();
            }
            continue;
        }
        const fs::path path = it->path();
        if (lower_copy(path.extension().wstring()) == L".py") {
            scripts.push_back(path.filename());
        }
    }

    std::sort(
        scripts.begin(),
        scripts.end(),
        [](const fs::path& left, const fs::path& right) {
            if (lower_copy(left.filename().wstring()) == L"run.py") {
                return true;
            }
            if (lower_copy(right.filename().wstring()) == L"run.py") {
                return false;
            }
            return lower_copy(left.filename().wstring()) < lower_copy(right.filename().wstring());
        }
    );
    return scripts;
}

std::optional<RunConfig> load_run_config(const ProjectLayout& layout) {
    if (!fs::exists(layout.run_config_file)) {
        return std::nullopt;
    }
    const std::wstring xml = read_text_file_utf8(layout.run_config_file);
    const auto launcher_attributes = xml_element_attributes(xml, L"launcher");
    const auto python_attributes = xml_element_attributes(xml, L"python");
    const auto torch_attributes = xml_element_attributes(xml, L"torch");
    const auto launch_attributes = xml_element_attributes(xml, L"launch");
    const auto requirements_attributes = xml_element_attributes(xml, L"requirements");
    if (!launcher_attributes.has_value() || !python_attributes.has_value() || !torch_attributes.has_value()
        || !launch_attributes.has_value() || !requirements_attributes.has_value()) {
        return std::nullopt;
    }

    RunConfig run_config{};
    try {
        run_config.schema_version = std::stoi(xml_attribute_value(*launcher_attributes, L"schema").value_or(L"0"));
    } catch (...) {
        return std::nullopt;
    }
    if (run_config.schema_version != 2) {
        return std::nullopt;
    }

    const std::wstring source_kind = xml_attribute_value(*python_attributes, L"source_kind").value_or(L"");
    if (source_kind == L"existing") {
        run_config.python_source_kind = PythonSourceKind::Existing;
    } else if (source_kind == L"install-machine") {
        run_config.python_source_kind = PythonSourceKind::InstallMachine;
    } else if (source_kind == L"portable") {
        run_config.python_source_kind = PythonSourceKind::Portable;
    } else {
        return std::nullopt;
    }

    const std::wstring environment_kind = xml_attribute_value(*python_attributes, L"environment_kind").value_or(L"");
    if (environment_kind == L"direct") {
        run_config.environment_kind = EnvironmentKind::Direct;
    } else if (environment_kind == L"named-venv") {
        run_config.environment_kind = EnvironmentKind::NamedVenv;
    } else if (environment_kind == L"named-conda") {
        run_config.environment_kind = EnvironmentKind::NamedConda;
    } else if (environment_kind == L"portable") {
        run_config.environment_kind = EnvironmentKind::PortableRuntime;
    } else {
        return std::nullopt;
    }

    const std::wstring accelerator_kind = xml_attribute_value(*torch_attributes, L"kind").value_or(L"");
    if (accelerator_kind == L"cuda") {
        run_config.accelerator_kind = AcceleratorKind::NvidiaCuda;
    } else if (accelerator_kind == L"rocm") {
        run_config.accelerator_kind = AcceleratorKind::AmdRocm;
    } else if (accelerator_kind == L"xpu") {
        run_config.accelerator_kind = AcceleratorKind::IntelArcXpu;
    } else if (accelerator_kind == L"cpu") {
        run_config.accelerator_kind = AcceleratorKind::Cpu;
    } else {
        return std::nullopt;
    }

    const std::wstring launch_kind = xml_attribute_value(*launch_attributes, L"kind").value_or(L"");
    if (launch_kind == L"script") {
        run_config.launch_kind = LaunchKind::Script;
    } else if (launch_kind == L"python-args") {
        run_config.launch_kind = LaunchKind::PythonArgs;
    } else {
        return std::nullopt;
    }

    run_config.python_version = xml_attribute_value(*python_attributes, L"version").value_or(L"");
    run_config.python_source_label = xml_attribute_value(*python_attributes, L"source_label").value_or(L"");
    run_config.python_source_path = xml_attribute_value(*python_attributes, L"source_path").value_or(L"");
    run_config.python_sourcew_path = xml_attribute_value(*python_attributes, L"sourcew_path").value_or(L"");
    run_config.environment_name = xml_attribute_value(*python_attributes, L"environment_name").value_or(L"");
    run_config.environment_path = xml_attribute_value(*python_attributes, L"environment_path").value_or(L"");
    run_config.launch_target = xml_attribute_value(*launch_attributes, L"target").value_or(L"");
    run_config.requirements_hash = xml_attribute_value(*requirements_attributes, L"hash").value_or(L"");
    run_config.requirements_synced_at = xml_attribute_value(*requirements_attributes, L"synced_at").value_or(L"");
    return run_config;
}

void write_run_config(const ProjectLayout& layout, const RunConfig& run_config) {
    const auto python_source_kind = [&]() {
        switch (run_config.python_source_kind) {
        case PythonSourceKind::Existing:
            return L"existing";
        case PythonSourceKind::InstallMachine:
            return L"install-machine";
        case PythonSourceKind::Portable:
            return L"portable";
        }
        return L"portable";
    }();
    const auto environment_kind = [&]() {
        switch (run_config.environment_kind) {
        case EnvironmentKind::Direct:
            return L"direct";
        case EnvironmentKind::NamedVenv:
            return L"named-venv";
        case EnvironmentKind::NamedConda:
            return L"named-conda";
        case EnvironmentKind::PortableRuntime:
            return L"portable";
        }
        return L"portable";
    }();
    const auto accelerator_kind = [&]() {
        switch (run_config.accelerator_kind) {
        case AcceleratorKind::NvidiaCuda:
            return L"cuda";
        case AcceleratorKind::IntelArcXpu:
            return L"xpu";
        case AcceleratorKind::AmdRocm:
            return L"rocm";
        case AcceleratorKind::Cpu:
            return L"cpu";
        }
        return L"cpu";
    }();
    const auto launch_kind = run_config.launch_kind == LaunchKind::Script ? L"script" : L"python-args";

    const std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        L"<launcher-config>\r\n"
        L"  <launcher schema=\"2\"/>\r\n"
        L"  <python"
        L" source_kind=\"" + xml_escape(python_source_kind) + L"\""
        L" source_label=\"" + xml_escape(run_config.python_source_label) + L"\""
        L" source_path=\"" + xml_escape(run_config.python_source_path.wstring()) + L"\""
        L" sourcew_path=\"" + xml_escape(run_config.python_sourcew_path.wstring()) + L"\""
        L" version=\"" + xml_escape(run_config.python_version) + L"\""
        L" environment_kind=\"" + xml_escape(environment_kind) + L"\""
        L" environment_name=\"" + xml_escape(run_config.environment_name) + L"\""
        L" environment_path=\"" + xml_escape(run_config.environment_path.wstring()) + L"\""
        L"/>\r\n"
        L"  <torch kind=\"" + xml_escape(accelerator_kind) + L"\"/>\r\n"
        L"  <launch kind=\"" + xml_escape(launch_kind) + L"\" target=\"" + xml_escape(run_config.launch_target) + L"\"/>\r\n"
        L"  <requirements hash=\"" + xml_escape(run_config.requirements_hash) + L"\" synced_at=\"" + xml_escape(run_config.requirements_synced_at) + L"\"/>\r\n"
        L"</launcher-config>\r\n";
    write_text_file_utf8(layout.run_config_file, utf8_from_wide(xml));
}

bool run_config_is_usable(const RunConfig& run_config, const ProjectLayout& layout) {
    if (run_config.launch_kind == LaunchKind::Script) {
        if (run_config.launch_target.empty() || !path_exists_no_throw(layout.project_root / run_config.launch_target)) {
            return false;
        }
    } else if (trim_copy(run_config.launch_target).empty()) {
        return false;
    }

    if (run_config.python_source_kind == PythonSourceKind::Existing
        && (path_is_windows_apps_alias(run_config.python_source_path) || !path_exists_no_throw(run_config.python_source_path))) {
        return false;
    }
    if ((run_config.environment_kind == EnvironmentKind::NamedVenv
            || run_config.environment_kind == EnvironmentKind::NamedConda)
        && trim_copy(run_config.environment_name).empty()) {
        return false;
    }
    return true;
}

void configure_active_python(
    ProjectLayout& layout,
    const fs::path& python_exe,
    const fs::path& pythonw_exe,
    const std::optional<fs::path>& environment_dir
) {
    layout.active_python_exe = python_exe;
    layout.active_pythonw_exe = pythonw_exe;
    layout.active_environment_dir = environment_dir.value_or(fs::path());
}

bool paths_equivalent_loose(const fs::path& left, const fs::path& right) {
    std::error_code exists_error;
    if (fs::exists(left, exists_error) && fs::exists(right, exists_error)) {
        std::error_code equivalent_error;
        if (fs::equivalent(left, right, equivalent_error) && !equivalent_error) {
            return true;
        }
    }
    auto normalize = [](fs::path value) {
        std::wstring text = lower_copy(value.lexically_normal().wstring());
        while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) {
            text.pop_back();
        }
        return text;
    };
    return normalize(left) == normalize(right);
}

void cleanup_local_python_registration(const LauncherConfig& config, const ProjectLayout& layout) {
    const std::wstring python_core_key = L"Software\\Python\\PythonCore\\" + config.python_series;
    const std::wstring install_path_key = python_core_key + L"\\InstallPath";
    const auto registered_install_dir = read_registry_default_value(HKEY_CURRENT_USER, install_path_key);
    const bool local_python_registered = registered_install_dir.has_value()
        && paths_equivalent_loose(fs::path(*registered_install_dir), layout.python_base_dir);

    if (!local_python_registered) {
        return;
    }

    append_log_line(
        layout.log_file,
        L"Removing Windows registration for the project-local Python runtime at " + layout.python_base_dir.wstring()
    );

    const std::wstring uninstall_root = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    std::vector<std::wstring> uninstall_subkeys;
    std::vector<fs::path> bundle_cache_dirs;

    HKEY uninstall_key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, uninstall_root.c_str(), 0, KEY_READ, &uninstall_key) == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t subkey_name[256];
        DWORD subkey_size = _countof(subkey_name);
        while (true) {
            subkey_size = _countof(subkey_name);
            const LONG result = RegEnumKeyExW(
                uninstall_key,
                index,
                subkey_name,
                &subkey_size,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            );
            if (result == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (result == ERROR_SUCCESS) {
                const std::wstring child = uninstall_root + L"\\" + subkey_name;
                const auto provider_key = read_registry_string_value(HKEY_CURRENT_USER, child, L"BundleProviderKey");
                const auto display_name = read_registry_string_value(HKEY_CURRENT_USER, child, L"DisplayName");
                const auto publisher = read_registry_string_value(HKEY_CURRENT_USER, child, L"Publisher");
                if (provider_key.has_value()
                    && display_name.has_value()
                    && publisher.has_value()
                    && *provider_key == L"CPython-" + config.python_series
                    && lower_copy(*publisher) == lower_copy(L"Python Software Foundation")
                    && lower_copy(*display_name).rfind(lower_copy(L"Python " + config.python_series), 0) == 0) {
                    uninstall_subkeys.push_back(child);
                    if (const auto bundle_cache = read_registry_string_value(HKEY_CURRENT_USER, child, L"BundleCachePath")) {
                        bundle_cache_dirs.push_back(fs::path(*bundle_cache).parent_path());
                    }
                }
            }
            ++index;
        }
        RegCloseKey(uninstall_key);
    }

    std::error_code fs_error;
    for (const auto& subkey : uninstall_subkeys) {
        const LONG delete_result = RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        if (delete_result == ERROR_SUCCESS || delete_result == ERROR_FILE_NOT_FOUND) {
            append_log_line(layout.log_file, L"Removed uninstall registration key " + subkey);
        } else {
            append_log_line(
                layout.log_file,
                L"Could not remove uninstall registration key " + subkey + L" (error " + wide_from_utf8(std::to_string(delete_result)) + L")."
            );
        }
    }

    const LONG python_core_delete = RegDeleteTreeW(HKEY_CURRENT_USER, python_core_key.c_str());
    if (python_core_delete == ERROR_SUCCESS || python_core_delete == ERROR_FILE_NOT_FOUND) {
        append_log_line(layout.log_file, L"Removed PythonCore registration key " + python_core_key);
    } else {
        append_log_line(
            layout.log_file,
            L"Could not remove PythonCore registration key " + python_core_key + L" (error " + wide_from_utf8(std::to_string(python_core_delete)) + L")."
        );
    }

    for (const auto& cache_dir : bundle_cache_dirs) {
        if (cache_dir.empty()) {
            continue;
        }
        if (fs::exists(cache_dir, fs_error)) {
            fs::remove_all(cache_dir, fs_error);
            if (!fs_error) {
                append_log_line(layout.log_file, L"Removed cached installer bundle directory " + cache_dir.wstring());
            } else {
                append_log_line(layout.log_file, L"Could not remove cached installer bundle directory " + cache_dir.wstring());
                fs_error.clear();
            }
        }
    }
}

bool command_exists_on_path(const std::wstring& executable_name) {
    const DWORD required = SearchPathW(nullptr, executable_name.c_str(), nullptr, 0, nullptr, nullptr);
    return required > 0;
}

std::vector<std::wstring> enumerate_display_adapter_names() {
    std::vector<std::wstring> names;
    for (DWORD adapter_index = 0;; ++adapter_index) {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, adapter_index, &adapter, 0)) {
            break;
        }
        std::wstring name = trim_copy(adapter.DeviceString);
        if (!name.empty()) {
            names.push_back(name);
        }
    }
    return names;
}

bool contains_case_insensitive(const std::wstring& text, const std::wstring& needle) {
    return lower_copy(text).find(lower_copy(needle)) != std::wstring::npos;
}

std::optional<std::wstring> env_value(const std::wstring& name) {
    const DWORD required = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }
    std::wstring buffer(required, L'\0');
    GetEnvironmentVariableW(name.c_str(), buffer.data(), required);
    if (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return trim_copy(buffer);
}

AcceleratorChoice detect_accelerator(const LauncherConfig& config, const ProjectLayout& layout) {
    const auto adapter_names = enumerate_display_adapter_names();
    for (const auto& name : adapter_names) {
        append_log_line(layout.log_file, L"Detected display adapter: " + name);
    }

    const bool has_nvidia_adapter = std::any_of(
        adapter_names.begin(),
        adapter_names.end(),
        [](const std::wstring& name) { return contains_case_insensitive(name, L"nvidia"); }
    );
    const bool has_nvidia_driver = has_nvidia_adapter || command_exists_on_path(L"nvidia-smi.exe")
        || fs::exists(LR"(C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe)");
    if (has_nvidia_driver) {
        AcceleratorChoice choice = accelerator_choice_for_kind(config, AcceleratorKind::NvidiaCuda);
        choice.note = L"NVIDIA driver detected. Preselecting the CUDA wheel set.";
        return choice;
    }

    const bool has_intel_arc = std::any_of(
        adapter_names.begin(),
        adapter_names.end(),
        [](const std::wstring& name) {
            return contains_case_insensitive(name, L"intel") && contains_case_insensitive(name, L"arc");
        }
    );
    if (has_intel_arc) {
        AcceleratorChoice choice = accelerator_choice_for_kind(config, AcceleratorKind::IntelArcXpu);
        choice.note = L"Intel Arc adapter detected. Preselecting the official PyTorch XPU wheels.";
        return choice;
    }

    const bool has_amd_adapter = std::any_of(
        adapter_names.begin(),
        adapter_names.end(),
        [](const std::wstring& name) {
            return contains_case_insensitive(name, L"amd") || contains_case_insensitive(name, L"radeon");
        }
    );
    const bool has_rocm_markers = env_value(L"ROCM_PATH").has_value()
        || env_value(L"HIP_PATH").has_value()
        || command_exists_on_path(L"rocminfo.exe")
        || command_exists_on_path(L"hipinfo.exe")
        || fs::exists(LR"(C:\Program Files\AMD\ROCm)")
        || fs::exists(LR"(C:\Program Files\AMD\HIP)");
    if (has_amd_adapter && has_rocm_markers) {
        AcceleratorChoice choice = accelerator_choice_for_kind(config, AcceleratorKind::AmdRocm);
        choice.note = L"AMD ROCm markers were detected. Preselecting the ROCm wheel index.";
        return choice;
    }

    AcceleratorChoice choice = accelerator_choice_for_kind(config, AcceleratorKind::Cpu);
    choice.note = L"No supported accelerator driver was detected. Installing the regular CPU wheels.";
    return choice;
}

AcceleratorChoice accelerator_choice_for_kind(const LauncherConfig& config, AcceleratorKind kind) {
    AcceleratorChoice choice{};
    choice.kind = kind;
    switch (kind) {
    case AcceleratorKind::NvidiaCuda:
        choice.display_name = L"NVIDIA CUDA";
        choice.install_tag = L"nvidia-cu129";
        choice.index_url = config.torch_cuda_index_url;
        choice.note = L"Installing the CUDA wheel set from the configured cu129 index.";
        break;
    case AcceleratorKind::IntelArcXpu:
        choice.display_name = L"Intel XPU";
        choice.install_tag = L"intel-xpu";
        choice.index_url = config.torch_xpu_index_url;
        choice.note = L"Installing the official XPU wheels.";
        break;
    case AcceleratorKind::AmdRocm:
        choice.display_name = L"AMD ROCm";
        choice.install_tag = L"amd-rocm";
        choice.index_url = config.torch_rocm_index_url;
        choice.note = L"Installing the ROCm wheels from the configured ROCm index.";
        break;
    case AcceleratorKind::Cpu:
        choice.display_name = L"CPU";
        choice.install_tag = L"cpu";
        choice.note = L"Installing the regular CPU wheels.";
        break;
    }
    return choice;
}

std::wstring python_installer_url_for_version(const LauncherConfig& config, const std::wstring& version) {
    return replace_all(config.python_installer_url_template, L"{version}", version);
}

std::wstring python_embeddable_url_for_version(const LauncherConfig& config, const std::wstring& version) {
    return replace_all(config.python_embeddable_url_template, L"{version}", version);
}

fs::path windows_system_executable(const std::wstring& executable_name) {
    std::wstring buffer(MAX_PATH, L'\0');
    const UINT written = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    fail_if(written == 0 || written >= buffer.size(), L"Could not determine the Windows system directory.");
    buffer.resize(written);
    return fs::path(buffer) / executable_name;
}

void extract_zip_archive(const fs::path& archive_file, const fs::path& destination_dir, const fs::path& log_file) {
    fail_if(!fs::exists(archive_file), L"ZIP archive not found: " + archive_file.wstring());
    fs::create_directories(destination_dir);

    const fs::path tar_path = windows_system_executable(L"tar.exe");
    if (fs::exists(tar_path)) {
        const int tar_exit = run_process(
            tar_path,
            {L"-xf", archive_file.wstring(), L"-C", destination_dir.wstring()},
            std::nullopt,
            log_file,
            true
        );
        if (tar_exit == 0) {
            return;
        }
        append_log_line(
            log_file,
            L"tar.exe failed to extract "
            + archive_file.wstring()
            + L" with exit code "
            + wide_from_utf8(std::to_string(tar_exit))
            + L". Falling back to PowerShell Expand-Archive."
        );
    }

    const auto powershell_path = find_executable_on_path(L"powershell.exe");
    fail_if(!powershell_path.has_value(), L"Neither tar.exe nor powershell.exe is available to unpack " + archive_file.wstring());
    const std::wstring command =
        L"Expand-Archive -LiteralPath "
        + quote_powershell_literal(archive_file.wstring())
        + L" -DestinationPath "
        + quote_powershell_literal(destination_dir.wstring())
        + L" -Force";
    const int powershell_exit = run_process(
        *powershell_path,
        {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-Command", command},
        std::nullopt,
        log_file,
        true
    );
    fail_if(
        powershell_exit != 0,
        L"Could not extract "
        + archive_file.wstring()
        + L" into "
        + destination_dir.wstring()
        + L" (PowerShell exit code "
        + wide_from_utf8(std::to_string(powershell_exit))
        + L")."
    );
}

void download_python_layout(const LauncherConfig& config, const ProjectLayout& layout) {
    std::error_code fs_error;
    fs::remove_all(layout.python_layout_dir, fs_error);
    fs::create_directories(layout.python_layout_dir);

    append_log_line(
        layout.log_file,
        L"Preparing standalone Python package layout in " + layout.python_layout_dir.wstring()
    );

    const std::vector<std::wstring> arguments = {
        L"/layout", layout.python_layout_dir.wstring(),
        L"/quiet",
        L"/log", layout.python_installer_log_file.wstring(),
        L"Include_core=1",
        L"Include_exe=1",
        L"Include_lib=1",
        L"Include_tcltk=1",
        L"Include_pip=0",
        L"Include_dev=0",
        L"Include_doc=0",
        L"Include_test=0",
        L"Include_symbols=0",
        L"Include_debug=0",
        L"Include_launcher=0",
        L"InstallLauncherAllUsers=0"
    };

    const int exit_code = run_process(
        layout.python_installer_file,
        arguments,
        std::nullopt,
        layout.log_file,
        true
    );
    if (exit_code != 0) {
        append_log_line(
            layout.log_file,
            L"Python layout download exited with code "
            + wide_from_utf8(std::to_string(exit_code))
            + L". Continuing to search any already cached package payloads."
        );
    }
}

std::optional<std::wstring> python_package_cache_version(const fs::path& candidate_path, const LauncherConfig& config) {
    static const std::wregex version_pattern(LR"(v(\d+\.\d+\.\d+\.\d+))", std::regex::icase);
    for (fs::path cursor = candidate_path.parent_path(); !cursor.empty(); cursor = cursor.parent_path()) {
        const std::wstring name = cursor.filename().wstring();
        std::wsmatch match;
        if (std::regex_search(name, match, version_pattern) && match.size() > 1) {
            const std::wstring version = match[1].str();
            if (version.rfind(config.python_series + L".", 0) == 0) {
                return version;
            }
        }
        if (cursor == cursor.root_path()) {
            break;
        }
    }
    return std::nullopt;
}

std::optional<fs::path> locate_python_runtime_msi(
    const LauncherConfig& config,
    const ProjectLayout& layout,
    const std::wstring& file_name
) {
    const fs::path direct_layout_candidate = layout.python_layout_dir / file_name;
    if (fs::exists(direct_layout_candidate)) {
        return direct_layout_candidate;
    }

    std::vector<fs::path> search_roots;
    if (const auto local_app_data = env_value(L"LOCALAPPDATA")) {
        search_roots.push_back(fs::path(*local_app_data) / "Package Cache");
    }
    if (const auto program_data = env_value(L"PROGRAMDATA")) {
        search_roots.push_back(fs::path(*program_data) / "Package Cache");
    }

    const std::wstring target_name = lower_copy(file_name);
    std::optional<fs::path> best_path;
    std::wstring best_version;

    for (const auto& root : search_roots) {
        if (!fs::exists(root)) {
            continue;
        }
        std::error_code iter_error;
        for (fs::recursive_directory_iterator it(
                 root,
                 fs::directory_options::skip_permission_denied,
                 iter_error);
             it != fs::recursive_directory_iterator();
             it.increment(iter_error)) {
            if (iter_error) {
                iter_error.clear();
                continue;
            }
            if (!it->is_regular_file(iter_error)) {
                if (iter_error) {
                    iter_error.clear();
                }
                continue;
            }
            const fs::path candidate = it->path();
            if (lower_copy(candidate.filename().wstring()) != target_name) {
                continue;
            }
            const auto version = python_package_cache_version(candidate, config);
            if (!version.has_value()) {
                continue;
            }
            if (!best_path.has_value() || version_less(best_version, *version)) {
                best_path = candidate;
                best_version = *version;
            }
        }
    }

    return best_path;
}

void extract_python_runtime_msi(const ProjectLayout& layout, const fs::path& msi_path) {
    const fs::path msiexec_path = windows_system_executable(L"msiexec.exe");
    append_log_line(
        layout.log_file,
        L"Extracting " + msi_path.filename().wstring() + L" into " + layout.python_base_dir.wstring()
    );
    const int exit_code = run_process(
        msiexec_path,
        {
            L"/a",
            msi_path.wstring(),
            L"/qn",
            L"TARGETDIR=" + layout.python_base_dir.wstring()
        },
        std::nullopt,
        layout.log_file,
        true
    );
    if (exit_code != 0) {
        throw make_error(
            L"Could not extract "
            + msi_path.filename().wstring()
            + L" into "
            + layout.python_base_dir.wstring()
            + L" (exit code "
            + wide_from_utf8(std::to_string(exit_code))
            + L")."
        );
    }
}

void write_embeddable_runtime_pth(const LauncherConfig& config, const ProjectLayout& layout) {
    std::wstring series_no_dot;
    series_no_dot.reserve(config.python_series.size());
    for (const wchar_t ch : config.python_series) {
        if (ch != L'.') {
            series_no_dot += ch;
        }
    }
    std::wstring project_root_entry;
    std::error_code relative_error;
    const fs::path relative_project_root = fs::relative(layout.project_root, layout.python_base_dir, relative_error);
    if (!relative_error && !relative_project_root.empty()) {
        project_root_entry = relative_project_root.wstring();
    } else {
        project_root_entry = layout.project_root.wstring();
    }
    const fs::path pth_file = layout.python_base_dir / (L"python" + series_no_dot + L"._pth");
    const std::wstring content_wide =
        std::wstring(L"python") + series_no_dot + L".zip\r\n"
        + L".\r\n"
        + L"DLLs\r\n"
        + L"Lib\r\n"
        + L"Lib\\site-packages\r\n"
        + project_root_entry + L"\r\n"
        + L"\r\n"
        + L"import site\r\n";
    const std::string content = utf8_from_wide(content_wide);
    write_text_file_utf8(pth_file, content);
}

void set_child_environment(const ProjectLayout& layout) {
    std::wstring path_prefix;
    std::vector<fs::path> path_entries;
    if (!layout.active_python_exe.empty()) {
        const fs::path python_dir = layout.active_python_exe.parent_path();
        if (lower_copy(python_dir.filename().wstring()) == L"scripts") {
            path_entries.push_back(python_dir);
            const fs::path env_root = python_dir.parent_path();
            if (!env_root.empty() && !paths_equivalent_loose(env_root, python_dir)) {
                path_entries.push_back(env_root);
            }
        } else {
            path_entries.push_back(python_dir);
            const fs::path scripts_dir = python_dir / "Scripts";
            if (path_exists_no_throw(scripts_dir) && !paths_equivalent_loose(scripts_dir, python_dir)) {
                path_entries.push_back(scripts_dir);
            }
        }
    }
    for (const auto& entry : path_entries) {
        if (entry.empty()) {
            continue;
        }
        if (!path_prefix.empty()) {
            path_prefix += L";";
        }
        path_prefix += entry.wstring();
    }
    const auto current_path = env_value(L"PATH").value_or(L"");
    if (!current_path.empty()) {
        if (!path_prefix.empty()) {
            path_prefix += L";";
        }
        path_prefix += current_path;
    }
    SetEnvironmentVariableW(L"PATH", path_prefix.c_str());
    SetEnvironmentVariableW(L"PYTHONNOUSERSITE", L"1");
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    SetEnvironmentVariableW(L"PIP_DISABLE_PIP_VERSION_CHECK", L"1");
    SetEnvironmentVariableW(L"PIP_CACHE_DIR", layout.pip_cache_dir.wstring().c_str());
}

bool python_supports_tkinter(const fs::path& python_exe, const fs::path& log_file) {
    if (!fs::exists(python_exe)) {
        return false;
    }
    const int exit_code = run_process(
        python_exe,
        {L"-c", L"import tkinter; import sys; sys.stdout.write('tk-ok')"},
        std::nullopt,
        log_file,
        true
    );
    return exit_code == 0;
}

bool python_supports_launcher_runtime(const fs::path& python_exe, const fs::path& log_file) {
    if (!fs::exists(python_exe)) {
        return false;
    }
    const int exit_code = run_process(
        python_exe,
        {L"-c", L"import ensurepip, venv, sys; sys.stdout.write('launcher-runtime-ok')"},
        std::nullopt,
        log_file,
        true
    );
    return exit_code == 0;
}

bool python_has_importable_torch_stack(const fs::path& python_exe, const fs::path& log_file) {
    if (!fs::exists(python_exe)) {
        return false;
    }
    const int exit_code = run_process(
        python_exe,
        {L"-c", L"import torch, torchvision, torchaudio, sys; sys.stdout.write('torch-stack-ok')"},
        std::nullopt,
        log_file,
        true
    );
    return exit_code == 0;
}

void create_virtual_environment(const fs::path& base_python_exe, const fs::path& environment_dir, const ProjectLayout& layout) {
    if (fs::exists(environment_dir)) {
        fs::remove_all(environment_dir);
    }
    append_log_line(layout.log_file, L"Creating project environment in " + environment_dir.wstring());
    const int exit_code = run_process(
        base_python_exe,
        {L"-m", L"venv", environment_dir.wstring(), L"--clear"},
        layout.project_root,
        layout.log_file,
        true
    );
    if (exit_code != 0) {
        throw make_error(
            L"Could not create the project Python environment (exit code "
            + wide_from_utf8(std::to_string(exit_code))
            + L")."
        );
    }

    const fs::path python_exe = environment_dir / "Scripts" / "python.exe";
    const fs::path pythonw_exe = environment_dir / "Scripts" / "pythonw.exe";
    fail_if(!fs::exists(python_exe), L"Virtual environment was created, but python.exe is missing in " + environment_dir.wstring());
    if (!fs::exists(pythonw_exe)) {
        append_log_line(layout.log_file, L"pythonw.exe is not present in " + environment_dir.wstring() + L". Falling back to python.exe for launches.");
    }
}

void create_local_venv(const fs::path& base_python_exe, const ProjectLayout& layout) {
    create_virtual_environment(base_python_exe, layout.python_env_dir, layout);
}

void create_or_update_conda_environment(const ProjectLayout& layout, const std::wstring& environment_name, const std::wstring& python_version) {
    const auto conda_exe = find_conda_executable();
    fail_if(!conda_exe.has_value(), L"Conda is required for this environment selection, but conda.exe was not found.");

    const std::wstring sanitized_name = sanitize_environment_name(environment_name);
    fail_if(sanitized_name.empty(), L"Provide a valid conda environment name.");

    const auto existing_prefix = find_conda_environment_prefix(layout, sanitized_name);
    const bool exists = existing_prefix.has_value();
    append_log_line(
        layout.log_file,
        (exists ? L"Updating conda environment " : L"Creating conda environment ")
        + sanitized_name
        + L" with Python "
        + python_version
    );

    std::vector<std::wstring> arguments = exists
        ? std::vector<std::wstring>{L"install", L"--yes", L"--name", sanitized_name, L"python=" + python_version, L"pip"}
        : std::vector<std::wstring>{L"create", L"--yes", L"--name", sanitized_name, L"python=" + python_version, L"pip"};
    const int exit_code = run_process(*conda_exe, arguments, std::nullopt, layout.log_file, true);
    fail_if(
        exit_code != 0,
        L"Conda failed while preparing environment "
        + sanitized_name
        + L" (exit code "
        + wide_from_utf8(std::to_string(exit_code))
        + L")."
    );
}

void repair_base_python(const ProjectLayout& layout) {
    append_log_line(layout.log_file, L"Attempting to repair the registered local Python install in " + layout.python_base_dir.wstring());
    const int exit_code = run_process(
        layout.python_installer_file,
        {
            L"/repair",
            L"/quiet",
            L"/log", layout.python_installer_log_file.wstring(),
            L"InstallAllUsers=0",
            L"InstallLauncherAllUsers=0",
            L"AssociateFiles=0",
            L"Include_launcher=0",
            L"PrependPath=0",
            L"Shortcuts=0"
        },
        std::nullopt,
        layout.log_file,
        true
    );
    if (exit_code != 0) {
        throw make_error(
            L"Python repair failed with exit code "
            + wide_from_utf8(std::to_string(exit_code))
            + L". See "
            + layout.python_installer_log_file.wstring()
        );
    }
}

void install_base_python(const LauncherConfig& config, const ProjectLayout& layout, const std::wstring& python_version) {
    if (fs::exists(layout.python_base_dir)) {
        fs::remove_all(layout.python_base_dir);
    }
    fs::create_directories(layout.cache_dir);
    fs::create_directories(layout.logs_dir);
    fs::create_directories(layout.python_base_dir);

    const std::wstring embeddable_url = python_embeddable_url_for_version(config, python_version);
    download_file(embeddable_url, layout.python_embeddable_file, layout.log_file);

    const std::wstring installer_url = python_installer_url_for_version(config, python_version);
    download_file(installer_url, layout.python_installer_file, layout.log_file);

    if (fs::exists(layout.python_installer_log_file)) {
        fs::remove(layout.python_installer_log_file);
    }

    append_log_line(
        layout.log_file,
        L"Building the project-local Python " + python_version + L" runtime from the embeddable package."
    );

    try {
        extract_zip_archive(layout.python_embeddable_file, layout.python_base_dir, layout.log_file);
        download_python_layout(config, layout);

        const std::vector<std::wstring> required_msi_names = {
            L"lib.msi",
            L"tcltk.msi"
        };
        for (const auto& msi_name : required_msi_names) {
            const auto located_msi = locate_python_runtime_msi(config, layout, msi_name);
            if (!located_msi.has_value()) {
                throw make_error(
                    L"Could not locate the required embeddable support payload "
                    + msi_name
                    + L" for Python "
                    + config.python_series
                    + L"."
                );
            }
            append_log_line(layout.log_file, L"Using " + msi_name + L" from " + located_msi->wstring());
            extract_python_runtime_msi(layout, *located_msi);
        }

        std::error_code fs_error;
        fs::remove(layout.python_base_dir / "lib.msi", fs_error);
        fs_error.clear();
        fs::remove(layout.python_base_dir / "tcltk.msi", fs_error);
        fs_error.clear();

        write_embeddable_runtime_pth(config, layout);

        fail_if(
            !fs::exists(layout.python_base_exe) || !fs::exists(layout.python_basew_exe),
            L"Embedded Python extraction finished, but python.exe or pythonw.exe is missing in " + layout.python_base_dir.wstring()
        );
        fail_if(
            !python_supports_launcher_runtime(layout.python_base_exe, layout.log_file),
            L"The embeddable Python runtime is missing one or more required modules (ensurepip or venv)."
        );
        append_log_line(layout.log_file, L"Embeddable Python runtime is ready.");
    } catch (const std::exception& exc) {
        append_log_line(
            layout.log_file,
            L"Embeddable runtime setup failed. No full-installer fallback is enabled."
        );
        append_log_line(layout.log_file, wide_from_utf8(exc.what()));
        throw;
    }
}

void install_machine_python(const LauncherConfig& config, const ProjectLayout& layout, const std::wstring& python_version) {
    fs::create_directories(layout.cache_dir);
    fs::create_directories(layout.logs_dir);

    const std::wstring installer_url = python_installer_url_for_version(config, python_version);
    download_file(installer_url, layout.python_installer_file, layout.log_file);
    if (fs::exists(layout.python_installer_log_file)) {
        fs::remove(layout.python_installer_log_file);
    }

    append_log_line(layout.log_file, L"Installing Python " + python_version + L" for the current Windows user.");
    const int exit_code = run_process(
        layout.python_installer_file,
        {
            L"/quiet",
            L"/log", layout.python_installer_log_file.wstring(),
            L"InstallAllUsers=0",
            L"InstallLauncherAllUsers=0",
            L"AssociateFiles=0",
            L"Include_launcher=1",
            L"Include_pip=1",
            L"Include_tcltk=1",
            L"Include_test=0",
            L"Include_doc=0",
            L"Include_dev=0",
            L"Include_symbols=0",
            L"Include_debug=0",
            L"PrependPath=0",
            L"Shortcuts=0"
        },
        std::nullopt,
        layout.log_file,
        true
    );
    fail_if(
        exit_code != 0 && exit_code != 3010,
        L"Python machine install failed with exit code "
        + wide_from_utf8(std::to_string(exit_code))
        + L". See "
        + layout.python_installer_log_file.wstring()
    );
    if (exit_code == 3010) {
        append_log_line(layout.log_file, L"Python installer reported that a reboot may be required, but the install completed.");
    }
}

void ensure_python_runtime(const LauncherConfig& config, const ProjectLayout& layout, const std::wstring& python_version) {
    const std::wstring installed_version = trim_copy(read_text_file_utf8(layout.python_version_file));
    const bool env_present = fs::exists(layout.python_exe) && fs::exists(layout.pythonw_exe);
    const bool base_present = fs::exists(layout.python_base_exe) && fs::exists(layout.python_basew_exe);
    std::wstring series_no_dot;
    for (const wchar_t ch : config.python_series) {
        if (ch != L'.') {
            series_no_dot += ch;
        }
    }
    const fs::path embeddable_pth_file = layout.python_base_dir / (L"python" + series_no_dot + L"._pth");
    if (base_present && fs::exists(embeddable_pth_file)) {
        write_embeddable_runtime_pth(config, layout);
    }
    const bool base_valid = base_present && python_supports_launcher_runtime(layout.python_base_exe, layout.log_file);
    if (env_present
        && base_valid
        && installed_version == python_version
        && python_supports_launcher_runtime(layout.python_exe, layout.log_file)) {
        append_log_line(layout.log_file, L"Local Python runtime " + python_version + L" is already present.");
        cleanup_local_python_registration(config, layout);
        return;
    }

    cleanup_local_python_registration(config, layout);

    if (!base_valid || installed_version != python_version) {
        install_base_python(config, layout, python_version);
    } else {
        append_log_line(layout.log_file, L"Reusing existing Python base install at " + layout.python_base_dir.wstring());
    }

    fail_if(
        !fs::exists(layout.python_base_exe) || !fs::exists(layout.python_basew_exe),
        L"Python installed, but python.exe was not found in " + layout.python_base_dir.wstring()
    );
    create_local_venv(layout.python_base_exe, layout);
    fail_if(
        !python_supports_launcher_runtime(layout.python_exe, layout.log_file),
        L"The local Python environment was created, but one or more required modules (tkinter, ensurepip, or venv) are unavailable."
    );
    write_text_file_utf8(layout.python_version_file, utf8_from_wide(python_version + L"\n"));
    cleanup_local_python_registration(config, layout);
}

void run_python(const ProjectLayout& layout, const std::vector<std::wstring>& arguments, const std::optional<fs::path>& cwd) {
    const fs::path python_exe = !layout.active_python_exe.empty() ? layout.active_python_exe : layout.python_exe;
    const int exit_code = run_process(python_exe, arguments, cwd, layout.log_file, true);
    if (exit_code != 0) {
        throw make_error(L"Python command failed with exit code " + wide_from_utf8(std::to_string(exit_code)) + L". See " + layout.log_file.wstring());
    }
}

void ensure_packaging_tools(const ProjectLayout& layout) {
    append_log_line(layout.log_file, L"Bootstrapping pip, setuptools, and wheel.");
    const fs::path python_exe = !layout.active_python_exe.empty() ? layout.active_python_exe : layout.python_exe;
    const int pip_check_exit = run_process(
        python_exe,
        {L"-m", L"pip", L"--version"},
        std::nullopt,
        layout.log_file,
        true
    );
    if (pip_check_exit != 0) {
        append_log_line(layout.log_file, L"pip is not ready yet. Attempting ensurepip.");
        run_python(layout, {L"-m", L"ensurepip", L"--upgrade"}, std::nullopt);
    }
    run_python(
        layout,
        {
            L"-m", L"pip", L"install",
            L"--upgrade",
            L"--disable-pip-version-check",
            L"--no-warn-script-location",
            L"pip",
            L"setuptools",
            L"wheel"
        },
        std::nullopt
    );
}

std::wstring canonical_requirement_name(const std::wstring& requirement_line) {
    const std::wstring trimmed = trim_copy(requirement_line);
    if (trimmed.empty() || trimmed.front() == L'#' || trimmed.front() == L'-') {
        return L"";
    }
    size_t end = 0;
    while (end < trimmed.size()) {
        const wchar_t ch = trimmed[end];
        if (ch == L' ' || ch == L'\t' || ch == L'[' || ch == L';' || ch == L'@' || ch == L'=' || ch == L'<' || ch == L'>' || ch == L'!' || ch == L'~') {
            break;
        }
        ++end;
    }
    std::wstring name = lower_copy(trimmed.substr(0, end));
    std::wstring normalized;
    normalized.reserve(name.size());
    for (const wchar_t ch : name) {
        if (ch != L'-' && ch != L'_' && ch != L'.') {
            normalized += ch;
        }
    }
    return normalized;
}

std::optional<fs::path> prepare_filtered_requirements(const ProjectLayout& layout) {
    if (!fs::exists(layout.requirements_file)) {
        append_log_line(layout.log_file, L"No requirements.txt was found. Skipping application requirements sync.");
        return std::nullopt;
    }

    std::wifstream input(layout.requirements_file);
    if (!input) {
        throw make_error(L"Could not open " + layout.requirements_file.wstring() + L" for reading.");
    }
    input.imbue(std::locale::classic());

    std::wstring output;
    bool has_package_lines = false;
    std::wstring line;
    while (std::getline(input, line)) {
        const std::wstring package_name = canonical_requirement_name(line);
        if (package_name == L"torch" || package_name == L"torchvision" || package_name == L"torchaudio") {
            append_log_line(layout.log_file, L"Skipping torch-related requirement line: " + trim_copy(line));
            continue;
        }
        const std::wstring trimmed = trim_copy(line);
        if (!trimmed.empty() && trimmed.front() != L'#') {
            has_package_lines = true;
        }
        output += line;
        output += L"\r\n";
    }

    write_text_file_utf8(layout.filtered_requirements_file, utf8_from_wide(output));
    if (!has_package_lines) {
        append_log_line(layout.log_file, L"requirements.txt contains no non-torch packages after filtering.");
        return std::nullopt;
    }
    return layout.filtered_requirements_file;
}

std::vector<std::wstring> pip_install_arguments(
    const std::vector<std::wstring>& packages,
    const std::optional<std::wstring>& index_url
) {
    std::vector<std::wstring> arguments = {
        L"-m", L"pip", L"install",
        L"--upgrade",
        L"--disable-pip-version-check",
        L"--no-warn-script-location",
        L"--upgrade-strategy", L"only-if-needed"
    };
    if (index_url.has_value()) {
        arguments.push_back(L"--index-url");
        arguments.push_back(*index_url);
    }
    arguments.insert(arguments.end(), packages.begin(), packages.end());
    return arguments;
}

void uninstall_torch_packages(const ProjectLayout& layout) {
    append_log_line(layout.log_file, L"Removing previously installed torch packages so the runtime can switch variants cleanly.");
    const fs::path python_exe = !layout.active_python_exe.empty() ? layout.active_python_exe : layout.python_exe;
    const int exit_code = run_process(
        python_exe,
        {
            L"-m", L"pip", L"uninstall",
            L"-y",
            L"torch",
            L"torchvision",
            L"torchaudio"
        },
        std::nullopt,
        layout.log_file,
        true
    );
    if (exit_code != 0) {
        append_log_line(layout.log_file, L"pip uninstall returned exit code " + wide_from_utf8(std::to_string(exit_code)) + L". Continuing.");
    }
}

void install_torch_stack(const ProjectLayout& layout, const AcceleratorChoice& accelerator) {
    const std::wstring previous_target = trim_copy(read_text_file_utf8(layout.torch_target_file));
    const fs::path python_exe = !layout.active_python_exe.empty() ? layout.active_python_exe : layout.python_exe;
    if (previous_target == accelerator.install_tag && python_has_importable_torch_stack(python_exe, layout.log_file)) {
        append_log_line(
            layout.log_file,
            L"Torch stack for " + accelerator.display_name + L" is already installed and importable. Skipping reinstall."
        );
        return;
    }
    if (!previous_target.empty() && previous_target != accelerator.install_tag) {
        uninstall_torch_packages(layout);
    }

    const auto install_packages = [&](const std::optional<std::wstring>& index_url, const std::wstring& description) {
        append_log_line(layout.log_file, description);
        run_python(
            layout,
            pip_install_arguments({L"torch", L"torchvision", L"torchaudio"}, index_url),
            std::nullopt
        );
    };

    append_log_line(layout.log_file, accelerator.note);
    if (accelerator.index_url.has_value()) {
        install_packages(accelerator.index_url, L"Installing the " + accelerator.display_name + L" torch stack.");
    } else {
        install_packages(std::nullopt, L"Installing the regular CPU torch stack.");
    }
    fail_if(
        !python_has_importable_torch_stack(python_exe, layout.log_file),
        L"The selected torch stack did not import successfully after installation."
    );
    write_text_file_utf8(layout.torch_target_file, utf8_from_wide(accelerator.install_tag + L"\n"));
}

void install_project_requirements(const ProjectLayout& layout) {
    const auto filtered = prepare_filtered_requirements(layout);
    if (!filtered.has_value()) {
        return;
    }
    append_log_line(layout.log_file, L"Installing filtered application requirements from " + filtered->wstring());
    run_python(
        layout,
        {
            L"-m", L"pip", L"install",
            L"--upgrade",
            L"--disable-pip-version-check",
            L"--no-warn-script-location",
            L"--upgrade-strategy", L"only-if-needed",
            L"-r", filtered->wstring()
        },
        layout.project_root
    );
}

RequirementsSnapshot collect_requirements_snapshot(const ProjectLayout& layout) {
    RequirementsSnapshot snapshot{};
    snapshot.filtered_file = prepare_filtered_requirements(layout);
    if (snapshot.filtered_file.has_value()) {
        snapshot.hash = hash_text_utf8(read_text_file_utf8(*snapshot.filtered_file));
    }
    return snapshot;
}

void sync_project_requirements(const ProjectLayout& layout, RunConfig& run_config) {
    const RequirementsSnapshot snapshot = collect_requirements_snapshot(layout);
    if (snapshot.hash == run_config.requirements_hash) {
        append_log_line(layout.log_file, L"requirements.txt has not changed since the last successful sync.");
        return;
    }
    if (!snapshot.filtered_file.has_value()) {
        run_config.requirements_hash.clear();
        run_config.requirements_synced_at = current_timestamp_local();
        append_log_line(layout.log_file, L"No non-torch requirements need syncing.");
        return;
    }

    install_project_requirements(layout);
    run_config.requirements_hash = snapshot.hash;
    run_config.requirements_synced_at = current_timestamp_local();
}

void launch_selected_target(const ProjectLayout& layout, const RunConfig& run_config, const std::vector<std::wstring>& extra_args) {
    std::vector<std::wstring> arguments;
    if (run_config.launch_kind == LaunchKind::Script) {
        arguments.push_back((layout.project_root / run_config.launch_target).wstring());
    } else {
        arguments = split_command_line(run_config.launch_target);
        fail_if(arguments.empty(), L"The saved custom Python command is empty.");
    }
    arguments.insert(arguments.end(), extra_args.begin(), extra_args.end());

    const fs::path preferred_python = fs::exists(layout.active_pythonw_exe) ? layout.active_pythonw_exe : layout.active_python_exe;
    fail_if(preferred_python.empty(), L"The selected Python environment is not ready.");
    append_log_line(
        layout.log_file,
        L"Launching "
        + (run_config.launch_kind == LaunchKind::Script ? run_config.launch_target : run_config.launch_target)
        + L" with "
        + preferred_python.wstring()
    );
    start_detached_process(preferred_python, arguments, layout.project_root, true);
}

ProjectLayout resolve_project_layout(const LauncherConfig& config) {
    ProjectLayout layout{};
    const fs::path exe_path = executable_path();
    layout.launcher_dir = exe_path.parent_path();
    layout.project_root = layout.launcher_dir;

    layout.runtime_dir = layout.project_root / config.runtime_dir_name;
    layout.cache_dir = layout.runtime_dir / config.cache_dir_name;
    layout.python_layout_dir = layout.cache_dir / "python-layout";
    layout.logs_dir = layout.runtime_dir / config.logs_dir_name;
    layout.log_file = layout.logs_dir / "launcher.log";
    layout.python_base_dir = layout.runtime_dir / config.python_base_dir_name;
    layout.python_env_dir = layout.runtime_dir / config.python_env_dir_name;
    layout.python_base_exe = layout.python_base_dir / "python.exe";
    layout.python_basew_exe = layout.python_base_dir / "pythonw.exe";
    layout.python_exe = layout.python_env_dir / "Scripts" / "python.exe";
    layout.pythonw_exe = layout.python_env_dir / "Scripts" / "pythonw.exe";
    layout.python_version_file = layout.runtime_dir / config.python_version_file_name;
    layout.torch_target_file = layout.runtime_dir / config.torch_target_file_name;
    layout.filtered_requirements_file = layout.cache_dir / config.filtered_requirements_file_name;
    layout.python_index_cache_file = layout.cache_dir / "python-ftp-index.html";
    layout.python_installer_file = layout.cache_dir / "python-runtime-installer.exe";
    layout.python_embeddable_file = layout.cache_dir / "python-runtime-embed.zip";
    layout.python_installer_log_file = layout.logs_dir / "python-installer.log";
    layout.requirements_file = layout.project_root / "requirements.txt";
    layout.run_config_file = layout.project_root / "run.cfg";
    layout.envs_dir = layout.runtime_dir / "envs";
    layout.run_script = layout.project_root / "run.py";
    layout.pip_cache_dir = layout.cache_dir / "pip";
    layout.active_python_exe = layout.python_exe;
    layout.active_pythonw_exe = layout.pythonw_exe;
    layout.active_environment_dir = layout.python_env_dir;

    fs::create_directories(layout.runtime_dir);
    fs::create_directories(layout.cache_dir);
    fs::create_directories(layout.logs_dir);
    fs::create_directories(layout.pip_cache_dir);
    fs::create_directories(layout.envs_dir);
    return layout;
}

std::wstring window_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return L"";
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    if (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    return trim_copy(text);
}

RunConfig collect_run_config_from_controls(HWND hwnd, const WindowState& state) {
    const LRESULT selected_index = SendMessageW(state.python_source_combo, CB_GETCURSEL, 0, 0);
    fail_if(selected_index == CB_ERR, L"Select a Python 3.12.x source before launching.");
    fail_if(selected_index < 0 || static_cast<size_t>(selected_index) >= state.python_sources.size(), L"Invalid Python source selection.");

    const PythonSourceOption& selected_source = state.python_sources[static_cast<size_t>(selected_index)];
    RunConfig run_config{};
    run_config.python_source_kind = selected_source.kind;
    run_config.python_source_label = selected_source.label;
    run_config.python_source_path = selected_source.python_exe;
    run_config.python_sourcew_path = selected_source.pythonw_exe;
    run_config.python_version = !selected_source.version.empty() ? selected_source.version : state.config.fallback_python_version;

    if (selected_source.kind == PythonSourceKind::Portable) {
        run_config.environment_kind = EnvironmentKind::PortableRuntime;
        run_config.environment_name = state.config.python_env_dir_name;
        run_config.environment_path = state.layout.python_env_dir;
    } else if (selected_source.kind == PythonSourceKind::InstallMachine && state.conda_available) {
        run_config.environment_kind = EnvironmentKind::NamedConda;
        run_config.environment_name = window_text(state.environment_name_edit);
        if (run_config.environment_name.empty()) {
            run_config.environment_name = default_environment_name(state.layout);
        }
    } else if (IsDlgButtonChecked(hwnd, kControlEnvironmentDirect) == BST_CHECKED) {
        run_config.environment_kind = EnvironmentKind::Direct;
    } else {
        run_config.environment_kind = state.conda_available ? EnvironmentKind::NamedConda : EnvironmentKind::NamedVenv;
        run_config.environment_name = window_text(state.environment_name_edit);
        if (run_config.environment_name.empty()) {
            run_config.environment_name = default_environment_name(state.layout);
        }
    }

    if (IsDlgButtonChecked(hwnd, kControlTorchCuda) == BST_CHECKED) {
        run_config.accelerator_kind = AcceleratorKind::NvidiaCuda;
    } else if (IsDlgButtonChecked(hwnd, kControlTorchRocm) == BST_CHECKED) {
        run_config.accelerator_kind = AcceleratorKind::AmdRocm;
    } else if (IsDlgButtonChecked(hwnd, kControlTorchXpu) == BST_CHECKED) {
        run_config.accelerator_kind = AcceleratorKind::IntelArcXpu;
    } else {
        run_config.accelerator_kind = AcceleratorKind::Cpu;
    }

    const bool use_custom_command = state.script_options.empty() || IsDlgButtonChecked(hwnd, kControlLaunchCustom) == BST_CHECKED;
    if (use_custom_command) {
        run_config.launch_kind = LaunchKind::PythonArgs;
        run_config.launch_target = window_text(state.custom_command_edit);
        fail_if(run_config.launch_target.empty(), L"Type a Python command or module to execute.");
    } else {
        const LRESULT script_index = SendMessageW(state.script_combo, CB_GETCURSEL, 0, 0);
        fail_if(script_index == CB_ERR, L"Select a Python script to launch.");
        fail_if(script_index < 0 || static_cast<size_t>(script_index) >= state.script_options.size(), L"Invalid script selection.");
        run_config.launch_kind = LaunchKind::Script;
        run_config.launch_target = state.script_options[static_cast<size_t>(script_index)].wstring();
    }

    return run_config;
}

void update_setup_controls(HWND hwnd) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) {
        return;
    }

    LRESULT selected_index = SendMessageW(state->python_source_combo, CB_GETCURSEL, 0, 0);
    if (selected_index == CB_ERR && !state->python_sources.empty()) {
        selected_index = 0;
        SendMessageW(state->python_source_combo, CB_SETCURSEL, 0, 0);
    }

    if (!state->python_sources.empty() && selected_index != CB_ERR) {
        const PythonSourceOption& source = state->python_sources[static_cast<size_t>(selected_index)];
        std::wstring note = source.description;
        if (source.kind == PythonSourceKind::Portable) {
            note += L"\r\nThe launcher will manage the portable runtime in the runtime folder automatically.";
            ShowWindow(state->environment_direct_radio, SW_HIDE);
            ShowWindow(state->environment_named_radio, SW_HIDE);
            ShowWindow(state->environment_name_label, SW_HIDE);
            ShowWindow(state->environment_name_edit, SW_HIDE);
        } else if (source.kind == PythonSourceKind::InstallMachine && state->conda_available) {
            if (window_text(state->environment_name_edit).empty()) {
                SetWindowTextW(state->environment_name_edit, default_environment_name(state->layout).c_str());
            }
            ShowWindow(state->environment_direct_radio, SW_HIDE);
            ShowWindow(state->environment_named_radio, SW_HIDE);
            ShowWindow(state->environment_name_label, SW_SHOW);
            ShowWindow(state->environment_name_edit, SW_SHOW);
            note += L"\r\nConda is installed, so this option will create or update the named conda environment below.";
        } else {
            if (IsDlgButtonChecked(hwnd, kControlEnvironmentDirect) != BST_CHECKED
                && IsDlgButtonChecked(hwnd, kControlEnvironmentNamed) != BST_CHECKED) {
                CheckRadioButton(hwnd, kControlEnvironmentDirect, kControlEnvironmentNamed, kControlEnvironmentNamed);
            }
            if (window_text(state->environment_name_edit).empty()) {
                SetWindowTextW(state->environment_name_edit, default_environment_name(state->layout).c_str());
            }
            ShowWindow(state->environment_direct_radio, SW_SHOW);
            ShowWindow(state->environment_named_radio, SW_SHOW);
            ShowWindow(state->environment_name_label, SW_SHOW);
            ShowWindow(state->environment_name_edit, SW_SHOW);
            if (IsDlgButtonChecked(hwnd, kControlEnvironmentDirect) == BST_CHECKED) {
                note += L"\r\nDirect use installs torch and requirements into the selected Python installation.";
            } else {
                note += state->conda_available
                    ? L"\r\nA named project environment will be created with conda so dependencies stay isolated."
                    : L"\r\nA named project environment keeps dependencies isolated from the base Python install.";
            }
        }
        SetWindowTextW(state->python_source_note, note.c_str());
    }

    if (state->script_options.empty()) {
        EnableWindow(state->launch_script_radio, FALSE);
        EnableWindow(state->script_combo, FALSE);
        CheckRadioButton(hwnd, kControlLaunchScript, kControlLaunchCustom, kControlLaunchCustom);
        EnableWindow(state->custom_command_edit, TRUE);
        SetWindowTextW(
            state->launch_note,
            L"No .py files were found in this folder. Type Python arguments such as -m mypackage or tool.py --flag."
        );
    } else {
        EnableWindow(state->launch_script_radio, TRUE);
        EnableWindow(state->script_combo, IsDlgButtonChecked(hwnd, kControlLaunchScript) == BST_CHECKED);
        EnableWindow(state->custom_command_edit, IsDlgButtonChecked(hwnd, kControlLaunchCustom) == BST_CHECKED);
        SetWindowTextW(
            state->launch_note,
            L"Script selections are remembered in run.cfg. Torch pins in requirements.txt are ignored so your launcher choice always wins."
        );
    }
}

void set_window_mode(HWND hwnd, bool setup_mode) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) {
        return;
    }
    state->setup_mode = setup_mode;

    const std::vector<HWND> setup_controls = {
        state->setup_intro,
        state->python_source_label,
        state->python_source_combo,
        state->python_source_note,
        state->environment_direct_radio,
        state->environment_named_radio,
        state->environment_name_label,
        state->environment_name_edit,
        state->torch_label,
        state->torch_cuda_radio,
        state->torch_rocm_radio,
        state->torch_xpu_radio,
        state->torch_cpu_radio,
        state->torch_note,
        state->launch_script_radio,
        state->script_combo,
        state->launch_custom_radio,
        state->custom_command_edit,
        state->launch_note,
        state->start_button
    };
    const std::vector<HWND> progress_controls = {
        state->status_label,
        state->progress_bar,
        state->detail_label,
        state->log_edit
    };

    for (HWND control : setup_controls) {
        if (control) {
            ShowWindow(control, setup_mode ? SW_SHOW : SW_HIDE);
        }
    }
    for (HWND control : progress_controls) {
        if (control) {
            ShowWindow(control, setup_mode ? SW_HIDE : SW_SHOW);
        }
    }
    update_setup_controls(hwnd);
}

void start_launcher_run(HWND hwnd, const RunConfig& run_config, bool confirm_direct_install) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state || state->run_started) {
        return;
    }

    if (confirm_direct_install
        && run_config.environment_kind == EnvironmentKind::Direct
        && run_config.python_source_kind != PythonSourceKind::Portable) {
        const int response = MessageBoxW(
            hwnd,
            L"This will install torch and the requirements.txt packages into the selected Python 3.12.x installation.\r\n\r\nContinue?",
            L"Use selected Python directly?",
            MB_YESNO | MB_ICONWARNING
        );
        if (response != IDYES) {
            return;
        }
    }

    state->run_started = true;
    state->can_close = false;
    EnableWindow(state->close_button, FALSE);
    EnableWindow(state->start_button, FALSE);
    SetWindowTextW(state->status_label, L"Starting launcher...");
    SendMessageW(state->progress_bar, PBM_SETPOS, 0, 0);
    SetWindowTextW(state->log_edit, L"");
    set_window_mode(hwnd, false);

    WorkerContext context{};
    context.config = state->config;
    context.layout = state->layout;
    context.run_config = run_config;
    context.hwnd = hwnd;
    context.extra_args = state->extra_args;

    std::thread worker([context]() mutable { bootstrap_worker(std::move(context)); });
    worker.detach();
}

void bootstrap_worker(WorkerContext context) {
    try {
        RunConfig& run_config = context.run_config;
        append_log_line(context.layout.log_file, L"Launcher starting in " + context.layout.project_root.wstring());
        post_status(context.hwnd, 1, L"Resolving the selected Python runtime...");
        post_log(context.hwnd, L"Project root: " + context.layout.project_root.wstring());

        if (run_config.python_source_kind == PythonSourceKind::Portable) {
            if (run_config.python_version.empty()) {
                run_config.python_version = resolve_latest_python_version(context.config, context.layout);
            }
            post_log(context.hwnd, L"Using portable Python " + run_config.python_version);
            post_status(context.hwnd, 2, L"Preparing the portable runtime...");
            ensure_python_runtime(context.config, context.layout, run_config.python_version);
            configure_active_python(
                context.layout,
                context.layout.python_exe,
                path_exists_no_throw(context.layout.pythonw_exe) ? context.layout.pythonw_exe : context.layout.python_exe,
                context.layout.python_env_dir
            );
            run_config.environment_kind = EnvironmentKind::PortableRuntime;
            run_config.environment_name = context.config.python_env_dir_name;
            run_config.environment_path = context.layout.python_env_dir;
            run_config.python_source_label = L"Portable runtime in the runtime folder";
            run_config.python_source_path = context.layout.python_base_exe;
            run_config.python_sourcew_path = context.layout.python_basew_exe;
        } else {
            std::optional<PythonSourceOption> source_option;
            if (run_config.python_source_kind == PythonSourceKind::InstallMachine) {
                if (run_config.environment_kind == EnvironmentKind::NamedConda) {
                    if (run_config.python_version.empty()) {
                        run_config.python_version = context.config.python_series;
                    }
                    run_config.environment_name = sanitize_environment_name(run_config.environment_name);
                    if (run_config.environment_name.empty()) {
                        run_config.environment_name = default_environment_name(context.layout);
                    }
                    post_log(context.hwnd, L"Preparing conda environment " + run_config.environment_name + L" with Python " + context.config.python_series);
                } else {
                    source_option = probe_python_source(run_config.python_source_path, context.layout, context.config.python_series, L"Installed Python ", PythonSourceKind::Existing);
                    if (!source_option.has_value()) {
                        const std::wstring target_version =
                            !run_config.python_version.empty() ? run_config.python_version : resolve_latest_python_version(context.config, context.layout);
                        post_log(context.hwnd, L"Installing Python " + target_version + L" on this machine.");
                        install_machine_python(context.config, context.layout, target_version);
                        const auto registered_dir = find_registered_python_install_dir(context.config);
                        fail_if(
                            !registered_dir.has_value(),
                            L"Python installed successfully, but the launcher could not locate the registered Python 3.12.x installation."
                        );
                        source_option = probe_python_source(*registered_dir / "python.exe", context.layout, context.config.python_series, L"Installed Python ", PythonSourceKind::Existing);
                    }
                }
            } else {
                source_option = probe_python_source(run_config.python_source_path, context.layout, context.config.python_series, L"Selected Python ", PythonSourceKind::Existing);
            }

            fail_if(
                !source_option.has_value() && run_config.environment_kind != EnvironmentKind::NamedConda,
                L"The configured Python 3.12.x interpreter could not be used. Delete run.cfg or start the launcher again to reconfigure."
            );

            if (source_option.has_value()) {
                run_config.python_version = source_option->version;
                run_config.python_source_label = source_option->label;
                run_config.python_source_path = source_option->python_exe;
                run_config.python_sourcew_path = source_option->pythonw_exe;
                post_log(context.hwnd, L"Using base Python: " + run_config.python_source_path.wstring());
                post_status(context.hwnd, 2, L"Preparing the selected environment...");
            }

            if (run_config.environment_kind == EnvironmentKind::Direct) {
                configure_active_python(
                    context.layout,
                    run_config.python_source_path,
                    path_exists_no_throw(run_config.python_sourcew_path) ? run_config.python_sourcew_path : run_config.python_source_path,
                    std::nullopt
                );
                run_config.environment_name.clear();
                run_config.environment_path.clear();
            } else if (run_config.environment_kind == EnvironmentKind::NamedConda) {
                run_config.environment_name = sanitize_environment_name(run_config.environment_name);
                if (run_config.environment_name.empty()) {
                    run_config.environment_name = default_environment_name(context.layout);
                }
                create_or_update_conda_environment(context.layout, run_config.environment_name, context.config.python_series);
                const auto conda_prefix = find_conda_environment_prefix(context.layout, run_config.environment_name);
                fail_if(!conda_prefix.has_value(), L"The conda environment could not be located after creation.");
                run_config.environment_path = *conda_prefix;
                const fs::path environment_python = *conda_prefix / "python.exe";
                const fs::path environment_pythonw = *conda_prefix / "pythonw.exe";
                const auto conda_option = probe_python_source(
                    environment_python,
                    context.layout,
                    context.config.python_series,
                    L"Conda env " + run_config.environment_name + L" - Python ",
                    PythonSourceKind::Existing
                );
                fail_if(!conda_option.has_value(), L"The conda environment does not expose a usable Python 3.12.x interpreter.");
                run_config.python_version = conda_option->version;
                run_config.python_source_label = conda_option->label;
                run_config.python_source_path = environment_python;
                run_config.python_sourcew_path = environment_pythonw;
                configure_active_python(
                    context.layout,
                    environment_python,
                    path_exists_no_throw(environment_pythonw) ? environment_pythonw : environment_python,
                    *conda_prefix
                );
                post_log(context.hwnd, L"Using conda environment: " + conda_prefix->wstring());
            } else {
                run_config.environment_kind = EnvironmentKind::NamedVenv;
                run_config.environment_name = sanitize_environment_name(run_config.environment_name);
                if (run_config.environment_name.empty()) {
                    run_config.environment_name = default_environment_name(context.layout);
                }
                run_config.environment_path = context.layout.envs_dir / run_config.environment_name;
                const fs::path environment_python = run_config.environment_path / "Scripts" / "python.exe";
                const fs::path environment_pythonw = run_config.environment_path / "Scripts" / "pythonw.exe";
                if (!path_exists_no_throw(environment_python) || !python_supports_launcher_runtime(environment_python, context.layout.log_file)) {
                    create_virtual_environment(run_config.python_source_path, run_config.environment_path, context.layout);
                }
                configure_active_python(
                    context.layout,
                    environment_python,
                    path_exists_no_throw(environment_pythonw) ? environment_pythonw : environment_python,
                    run_config.environment_path
                );
            }
        }

        post_log(context.hwnd, L"Active Python: " + context.layout.active_python_exe.wstring());
        set_child_environment(context.layout);

        post_status(context.hwnd, 3, L"Bootstrapping pip, setuptools, and wheel...");
        ensure_packaging_tools(context.layout);

        post_status(context.hwnd, 4, L"Installing the selected torch stack...");
        const AcceleratorChoice accelerator = accelerator_choice_for_kind(context.config, run_config.accelerator_kind);
        post_log(context.hwnd, accelerator.note);
        install_torch_stack(context.layout, accelerator);

        post_status(context.hwnd, 5, L"Synchronizing requirements.txt...");
        sync_project_requirements(context.layout, run_config);

        post_status(context.hwnd, 6, L"Writing run.cfg...");
        write_run_config(context.layout, run_config);

        post_status(context.hwnd, 7, L"Launching the selected target...");
        launch_selected_target(context.layout, run_config, context.extra_args);
        post_launched(
            context.hwnd,
            (run_config.launch_kind == LaunchKind::Script ? run_config.launch_target : L"Custom Python command")
            + L" started successfully. Closing launcher..."
        );
    } catch (const std::exception& exc) {
        const std::wstring message = wide_from_utf8(exc.what());
        append_log_line(context.layout.log_file, L"Bootstrap failed: " + message);
        post_failed(context.hwnd, message + L"\r\n\r\nSee the log at:\r\n" + context.layout.log_file.wstring());
    }
}

void append_log_to_edit(HWND edit_control, const std::wstring& text) {
    const int current_length = GetWindowTextLengthW(edit_control);
    SendMessageW(edit_control, EM_SETSEL, current_length, current_length);
    std::wstring line = text + L"\r\n";
    SendMessageW(edit_control, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(edit_control, EM_SCROLLCARET, 0, 0);
}

LRESULT CALLBACK launcher_window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        auto* new_state = new WindowState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new_state));
        state = new_state;

        state->setup_intro = CreateWindowExW(
            0,
            WC_STATICW,
            L"Choose the Python environment, torch variant, and launch target for this folder. The launcher will remember everything in run.cfg after the first successful setup.",
            WS_CHILD | WS_VISIBLE,
            18,
            18,
            780,
            36,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->python_source_label = CreateWindowExW(
            0,
            WC_STATICW,
            L"Python 3.12.x source",
            WS_CHILD | WS_VISIBLE,
            18,
            70,
            220,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->python_source_combo = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            18,
            92,
            780,
            220,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlPythonSourceCombo)),
            nullptr,
            nullptr
        );
        state->python_source_note = CreateWindowExW(
            0,
            WC_STATICW,
            nullptr,
            WS_CHILD | WS_VISIBLE,
            18,
            126,
            780,
            44,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->environment_direct_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Use the selected Python directly",
            WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON,
            18,
            176,
            300,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlEnvironmentDirect)),
            nullptr,
            nullptr
        );
        state->environment_named_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Create a named project environment (recommended)",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            18,
            204,
            360,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlEnvironmentNamed)),
            nullptr,
            nullptr
        );
        state->environment_name_label = CreateWindowExW(
            0,
            WC_STATICW,
            L"Environment name",
            WS_CHILD | WS_VISIBLE,
            42,
            234,
            120,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->environment_name_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_EDITW,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            170,
            230,
            250,
            24,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlEnvironmentNameEdit)),
            nullptr,
            nullptr
        );
        state->torch_label = CreateWindowExW(
            0,
            WC_STATICW,
            L"PyTorch stack",
            WS_CHILD | WS_VISIBLE,
            18,
            270,
            220,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->torch_cuda_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"CUDA",
            WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON,
            18,
            294,
            120,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlTorchCuda)),
            nullptr,
            nullptr
        );
        state->torch_rocm_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"ROCm",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            160,
            294,
            120,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlTorchRocm)),
            nullptr,
            nullptr
        );
        state->torch_xpu_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Intel XPU",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            300,
            294,
            140,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlTorchXpu)),
            nullptr,
            nullptr
        );
        state->torch_cpu_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"CPU",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            460,
            294,
            120,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlTorchCpu)),
            nullptr,
            nullptr
        );
        state->torch_note = CreateWindowExW(
            0,
            WC_STATICW,
            L"Your torch choice overrides any torch, torchvision, or torchaudio pins in requirements.txt.",
            WS_CHILD | WS_VISIBLE,
            18,
            322,
            780,
            28,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->launch_script_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Run a Python script from this folder",
            WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON,
            18,
            366,
            320,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlLaunchScript)),
            nullptr,
            nullptr
        );
        state->script_combo = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            42,
            392,
            756,
            220,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlScriptCombo)),
            nullptr,
            nullptr
        );
        state->launch_custom_radio = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Run a custom Python command or module",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            18,
            426,
            340,
            20,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlLaunchCustom)),
            nullptr,
            nullptr
        );
        state->custom_command_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_EDITW,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            42,
            452,
            756,
            24,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlCustomCommandEdit)),
            nullptr,
            nullptr
        );
        state->launch_note = CreateWindowExW(
            0,
            WC_STATICW,
            L"Examples: -m mypackage, tool.py --flag, or -m uvicorn app:app --reload",
            WS_CHILD | WS_VISIBLE,
            18,
            484,
            780,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->start_button = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Save && Launch",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            590,
            518,
            110,
            30,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlStart)),
            nullptr,
            nullptr
        );
        state->close_button = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            708,
            518,
            90,
            30,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlClose)),
            nullptr,
            nullptr
        );

        state->status_label = CreateWindowExW(
            0,
            WC_STATICW,
            L"Preparing launcher...",
            WS_CHILD,
            18,
            18,
            780,
            26,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->progress_bar = CreateWindowExW(
            0,
            PROGRESS_CLASSW,
            nullptr,
            WS_CHILD,
            18,
            54,
            780,
            22,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        SendMessageW(state->progress_bar, PBM_SETRANGE32, 0, kProgressMax);
        SendMessageW(state->progress_bar, PBM_SETPOS, 0, 0);
        state->detail_label = CreateWindowExW(
            0,
            WC_STATICW,
            L"Logs will appear below.",
            WS_CHILD,
            18,
            84,
            780,
            18,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        state->log_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_EDITW,
            nullptr,
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            18,
            112,
            780,
            390,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );
        return 0;
    }
    case WM_COMMAND:
        if (!state) {
            break;
        }
        switch (LOWORD(w_param)) {
        case kControlClose:
            if (state->can_close) {
                DestroyWindow(hwnd);
            } else {
                MessageBeep(MB_ICONINFORMATION);
            }
            return 0;
        case kControlStart:
            try {
                start_launcher_run(hwnd, collect_run_config_from_controls(hwnd, *state));
            } catch (const std::exception& exc) {
                MessageBoxW(hwnd, wide_from_utf8(exc.what()).c_str(), L"Setup incomplete", MB_OK | MB_ICONWARNING);
            }
            return 0;
        case kControlPythonSourceCombo:
            if (HIWORD(w_param) == CBN_SELCHANGE) {
                const LRESULT selected_index = SendMessageW(state->python_source_combo, CB_GETCURSEL, 0, 0);
                if (selected_index != CB_ERR
                    && selected_index >= 0
                    && static_cast<size_t>(selected_index) < state->python_sources.size()
                    && state->python_sources[static_cast<size_t>(selected_index)].kind == PythonSourceKind::Existing) {
                    CheckRadioButton(hwnd, kControlEnvironmentDirect, kControlEnvironmentNamed, kControlEnvironmentDirect);
                }
                update_setup_controls(hwnd);
            }
            return 0;
        case kControlEnvironmentDirect:
        case kControlEnvironmentNamed:
        case kControlLaunchScript:
        case kControlLaunchCustom:
            if (HIWORD(w_param) == BN_CLICKED) {
                update_setup_controls(hwnd);
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        if (state && !state->can_close) {
            MessageBeep(MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        delete state;
        PostQuitMessage(0);
        return 0;
    case WMU_STATUS:
    case WMU_LOG:
    case WMU_FAILED:
    case WMU_LAUNCHED: {
        std::unique_ptr<UiMessage> payload(reinterpret_cast<UiMessage*>(l_param));
        if (!state || !payload) {
            return 0;
        }

        if (message == WMU_STATUS) {
            SetWindowTextW(state->status_label, payload->text.c_str());
            SendMessageW(state->progress_bar, PBM_SETPOS, payload->step, 0);
        } else if (message == WMU_LOG) {
            append_log_to_edit(state->log_edit, payload->text);
        } else if (message == WMU_FAILED) {
            SetWindowTextW(state->status_label, L"Launcher failed.");
            SendMessageW(state->progress_bar, PBM_SETPOS, kProgressMax, 0);
            append_log_to_edit(state->log_edit, payload->text);
            state->can_close = true;
            state->run_started = false;
            EnableWindow(state->close_button, TRUE);
        } else if (message == WMU_LAUNCHED) {
            SetWindowTextW(state->status_label, payload->text.c_str());
            SendMessageW(state->progress_bar, PBM_SETPOS, kProgressMax, 0);
            append_log_to_edit(state->log_edit, payload->text);
            state->can_close = true;
            state->run_started = false;
            EnableWindow(state->close_button, TRUE);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

HWND create_launcher_window(HINSTANCE instance, const fs::path& log_file) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = launcher_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&window_class);

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        L"Python Launch Helper",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        836,
        620,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    fail_if(hwnd == nullptr, L"Could not create the launcher window: " + last_error_message(GetLastError()));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    if (auto* window_state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        window_state->log_file = log_file;
        SetWindowTextW(window_state->detail_label, log_file.wstring().c_str());
        set_window_mode(hwnd, true);
    }
    return hwnd;
}

std::vector<std::wstring> parse_extra_arguments() {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (!arguments) {
        return {};
    }
    std::vector<std::wstring> result;
    for (int index = 1; index < argument_count; ++index) {
        result.emplace_back(arguments[index]);
    }
    LocalFree(arguments);
    return result;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        LauncherConfig config{};
        ProjectLayout layout = resolve_project_layout(config);
        const HWND hwnd = create_launcher_window(instance, layout.log_file);
        auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        fail_if(state == nullptr, L"Could not initialize the launcher window state.");
        state->config = config;
        state->layout = layout;
        state->extra_args = parse_extra_arguments();
        state->conda_executable = find_conda_executable().value_or(fs::path());
        state->conda_available = !state->conda_executable.empty();

        append_log_line(layout.log_file, L"Launcher executable: " + executable_path().wstring());
        append_log_line(layout.log_file, L"Project root: " + layout.project_root.wstring());
        if (state->conda_available) {
            append_log_line(layout.log_file, L"Conda detected at " + state->conda_executable.wstring());
        } else {
            append_log_line(layout.log_file, L"Conda was not detected.");
        }
        if (fs::exists(layout.requirements_file)) {
            append_log_line(layout.log_file, L"Using requirements file: " + layout.requirements_file.wstring());
        } else {
            append_log_line(layout.log_file, L"No requirements.txt was found next to launch.exe.");
        }

        state->python_sources = discover_python_sources(config, layout);
        state->script_options = discover_script_options(layout);
        append_log_line(
            layout.log_file,
            L"Discovered " + wide_from_utf8(std::to_string(state->python_sources.size())) + L" Python source option(s)."
        );
        for (const auto& source : state->python_sources) {
            append_log_line(layout.log_file, L"Python source option: " + source.label);
        }
        for (const auto& source : state->python_sources) {
            SendMessageW(state->python_source_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(source.label.c_str()));
        }
        for (const auto& script : state->script_options) {
            SendMessageW(state->script_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(script.wstring().c_str()));
        }

        const int default_python_index = state->python_sources.size() > 2 ? 0 : static_cast<int>(state->python_sources.size()) - 1;
        if (!state->python_sources.empty() && default_python_index >= 0) {
            SendMessageW(state->python_source_combo, CB_SETCURSEL, default_python_index, 0);
        }
        if (!state->script_options.empty()) {
            SendMessageW(state->script_combo, CB_SETCURSEL, 0, 0);
            CheckRadioButton(hwnd, kControlLaunchScript, kControlLaunchCustom, kControlLaunchScript);
        } else {
            CheckRadioButton(hwnd, kControlLaunchScript, kControlLaunchCustom, kControlLaunchCustom);
        }
        CheckRadioButton(hwnd, kControlEnvironmentDirect, kControlEnvironmentNamed, kControlEnvironmentNamed);

        const AcceleratorChoice recommended = detect_accelerator(config, layout);
        switch (recommended.kind) {
        case AcceleratorKind::NvidiaCuda:
            CheckRadioButton(hwnd, kControlTorchCuda, kControlTorchCpu, kControlTorchCuda);
            break;
        case AcceleratorKind::AmdRocm:
            CheckRadioButton(hwnd, kControlTorchCuda, kControlTorchCpu, kControlTorchRocm);
            break;
        case AcceleratorKind::IntelArcXpu:
            CheckRadioButton(hwnd, kControlTorchCuda, kControlTorchCpu, kControlTorchXpu);
            break;
        case AcceleratorKind::Cpu:
            CheckRadioButton(hwnd, kControlTorchCuda, kControlTorchCpu, kControlTorchCpu);
            break;
        }
        SetWindowTextW(state->torch_note, recommended.note.c_str());
        SetWindowTextW(state->environment_name_edit, default_environment_name(layout).c_str());
        update_setup_controls(hwnd);

        try {
            state->saved_config = load_run_config(layout);
        } catch (const std::exception& exc) {
            append_log_line(layout.log_file, L"Could not read run.cfg: " + wide_from_utf8(exc.what()));
            state->saved_config.reset();
        }

        if (state->saved_config.has_value()) {
            if (run_config_is_usable(*state->saved_config, layout)) {
                start_launcher_run(hwnd, *state->saved_config, false);
            } else {
                SetWindowTextW(
                    state->setup_intro,
                    L"The saved run.cfg could not be reused. Review the selections below, then save and launch again."
                );
            }
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return 0;
    } catch (const std::exception& exc) {
        MessageBoxW(
            nullptr,
            wide_from_utf8(exc.what()).c_str(),
            L"Launcher startup failed",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }
}
