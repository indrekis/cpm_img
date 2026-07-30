/*
 * Safe out-of-process CP/M format probing client.
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "cpmimg_probe_client.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct physical_candidate {
    cpm_disk_descr_t format;
    int physical_score = 0;
    bool exact_size = false;
    int geometry_matches = 0;
};

struct running_probe {
    physical_candidate candidate;
    cpm_safe_probe_candidate result;
    PROCESS_INFORMATION process{};
    std::string output_path;
    bool running = false;
};

std::uint64_t expected_raw_size(const cpm_disk_descr_t& format) noexcept
{
    if (format.secLength <= 0 ||
        format.sectrk <= 0 ||
        format.tracks <= 0 ||
        format.offset < 0) {
        return 0;
    }

    return static_cast<std::uint64_t>(format.offset) +
        static_cast<std::uint64_t>(format.secLength) *
        static_cast<std::uint64_t>(format.sectrk) *
        static_cast<std::uint64_t>(format.tracks);
}

std::string quote_windows_argument(const std::string& argument)
{
    std::string quoted;
    quoted.push_back('"');

    std::size_t backslashes = 0;
    for (char ch : argument) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }

        if (ch == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(ch);
    }

    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

std::string own_module_directory()
{
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCSTR>(&cpm_run_safe_format_probes);

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            address,
            &module)) {
        return {};
    }

    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(
        module,
        path,
        static_cast<DWORD>(sizeof(path)));

    if (length == 0 || length >= sizeof(path))
        return {};

    std::string directory(path, length);
    const auto slash = directory.find_last_of("\\/");
    if (slash == std::string::npos)
        return {};

    directory.resize(slash);
    return directory;
}

std::string helper_path()
{
    const auto directory = own_module_directory();
    if (directory.empty())
        return {};

#ifdef _WIN64
    constexpr const char* helper_name = "cpmimg_probe64.exe";
#else
    constexpr const char* helper_name = "cpmimg_probe32.exe";
#endif

    return directory + "\\" + helper_name;
}

std::string make_temp_output_path()
{
    char temp_directory[MAX_PATH] = {};
    const DWORD length = GetTempPathA(
        static_cast<DWORD>(sizeof(temp_directory)),
        temp_directory);

    if (length == 0 || length >= sizeof(temp_directory))
        return {};

    char temp_file[MAX_PATH] = {};
    if (!GetTempFileNameA(temp_directory, "cpi", 0, temp_file))
        return {};

    DeleteFileA(temp_file);
    return temp_file;
}

std::vector<physical_candidate> select_physical_candidates(
    const std::vector<cpm_disk_descr_t>& formats,
    std::uint64_t image_payload_size,
    const DSK_GEOMETRY& geometry,
    bool geometry_reliable,
    std::size_t max_candidates)
{
    std::vector<physical_candidate> candidates;
    candidates.reserve(formats.size());

    bool any_exact_size = false;
    int best_geometry_matches = 0;

    for (const auto& format : formats) {
        physical_candidate candidate;
        candidate.format = format;

        const auto expected_size = expected_raw_size(format);
        candidate.exact_size =
            image_payload_size != 0 &&
            expected_size != 0 &&
            image_payload_size == expected_size;

        if (candidate.exact_size) {
            candidate.physical_score += 55;
            any_exact_size = true;
        }
        else if (image_payload_size != 0 && expected_size != 0) {
            candidate.physical_score -= 30;
        }

        if (geometry_reliable) {
            if (geometry.dg_secsize == format.secLength)
                ++candidate.geometry_matches;

            const int total_tracks =
                geometry.dg_cylinders * geometry.dg_heads;
            if (total_tracks == format.tracks)
                ++candidate.geometry_matches;

            if (geometry.dg_sectors == format.sectrk)
                ++candidate.geometry_matches;

            candidate.physical_score += candidate.geometry_matches * 12;
            best_geometry_matches = (std::max)(
                best_geometry_matches,
                candidate.geometry_matches);
        }

        candidates.push_back(std::move(candidate));
    }

    if (any_exact_size) {
        candidates.erase(
            std::remove_if(
                candidates.begin(),
                candidates.end(),
                [](const physical_candidate& candidate) {
                    return !candidate.exact_size;
                }),
            candidates.end());
    }
    else if (geometry_reliable && best_geometry_matches > 0) {
        candidates.erase(
            std::remove_if(
                candidates.begin(),
                candidates.end(),
                [best_geometry_matches](const physical_candidate& candidate) {
                    return candidate.geometry_matches < best_geometry_matches;
                }),
            candidates.end());
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const physical_candidate& left,
           const physical_candidate& right) {
            if (left.physical_score != right.physical_score)
                return left.physical_score > right.physical_score;
            return std::string(left.format.fmt_name) <
                std::string(right.format.fmt_name);
        });

    if (candidates.size() > max_candidates)
        candidates.resize(max_candidates);

    return candidates;
}

bool launch_probe(
    running_probe& probe,
    const std::string& helper,
    const char* image_path,
    const char* diskdefs_path_value,
    const char* driver_name,
    bool uppercase)
{
    probe.output_path = make_temp_output_path();
    if (probe.output_path.empty()) {
        probe.result.state = cpm_safe_probe_state::helper_error;
        probe.result.summary = "Could not create temporary output file";
        return false;
    }

    std::string command;
    command.reserve(1024);
    command += quote_windows_argument(helper);
    command += " --image ";
    command += quote_windows_argument(image_path ? image_path : "");
    command += " --diskdefs ";
    command += quote_windows_argument(
        diskdefs_path_value ? diskdefs_path_value : "");
    command += " --format ";
    command += quote_windows_argument(probe.candidate.format.fmt_name);
    command += " --driver ";
    command += quote_windows_argument(driver_name ? driver_name : "");
    command += " --uppercase ";
    command += uppercase ? "1" : "0";
    command += " --output ";
    command += quote_windows_argument(probe.output_path);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);

    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');

    if (!CreateProcessA(
            helper.c_str(),
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &probe.process)) {
        probe.result.state = cpm_safe_probe_state::helper_error;
        probe.result.summary =
            "CreateProcess failed: " + std::to_string(GetLastError());
        DeleteFileA(probe.output_path.c_str());
        probe.output_path.clear();
        return false;
    }

    probe.running = true;
    return true;
}

bool parse_probe_output(running_probe& probe)
{
    std::ifstream input(probe.output_path);
    std::string line;

    if (!std::getline(input, line)) {
        probe.result.state = cpm_safe_probe_state::crashed;
        probe.result.summary = "Helper returned no result";
        return false;
    }

    std::istringstream stream(line);
    std::string magic;
    std::string mounted_text;
    std::string files_text;
    std::string extents_text;
    std::string blocks_text;

    if (!std::getline(stream, magic, '\t') ||
        !std::getline(stream, mounted_text, '\t') ||
        !std::getline(stream, files_text, '\t') ||
        !std::getline(stream, extents_text, '\t') ||
        !std::getline(stream, blocks_text, '\t') ||
        magic != "CPMIMG_PROBE_V1") {
        probe.result.state = cpm_safe_probe_state::crashed;
        probe.result.summary = "Malformed helper result";
        return false;
    }

    try {
        const bool mounted = std::stoi(mounted_text) != 0;
        probe.result.files =
            static_cast<std::size_t>(std::stoull(files_text));
        probe.result.extents =
            static_cast<std::size_t>(std::stoull(extents_text));
        probe.result.used_blocks =
            static_cast<std::size_t>(std::stoull(blocks_text));

        probe.result.state = mounted
            ? cpm_safe_probe_state::mounted
            : cpm_safe_probe_state::rejected;

        if (mounted) {
            probe.result.score = std::clamp(
                55 + probe.result.physical_score +
                    static_cast<int>(
                        (std::min<std::size_t>)(probe.result.files, 10)),
                0,
                100);

            probe.result.summary =
                "Mount OK; " +
                std::to_string(probe.result.files) +
                " files, " +
                std::to_string(probe.result.extents) +
                " extents, " +
                std::to_string(probe.result.used_blocks) +
                " blocks";
        }
        else {
            probe.result.score = std::clamp(
                probe.result.physical_score / 3,
                0,
                40);
            probe.result.summary = "Mount rejected";
        }
    }
    catch (...) {
        probe.result.state = cpm_safe_probe_state::crashed;
        probe.result.summary = "Invalid numeric helper result";
        return false;
    }

    return true;
}

} // namespace

const cpm_safe_probe_candidate* cpm_safe_probe_report::find(
    const char* format_name) const noexcept
{
    if (!format_name)
        return nullptr;

    const auto found = std::find_if(
        candidates.begin(),
        candidates.end(),
        [format_name](const cpm_safe_probe_candidate& candidate) {
            return candidate.format_name == format_name;
        });

    return found == candidates.end() ? nullptr : &*found;
}

cpm_safe_probe_report cpm_run_safe_format_probes(
    const char* image_path,
    const char* diskdefs_path_value,
    const char* driver_name,
    const std::vector<cpm_disk_descr_t>& formats,
    std::uint64_t image_payload_size,
    const DSK_GEOMETRY& geometry,
    bool geometry_reliable,
    bool uppercase,
    unsigned int total_timeout_ms,
    std::size_t max_candidates)
{
    cpm_safe_probe_report report;

    const std::string helper = helper_path();
    if (helper.empty() ||
        GetFileAttributesA(helper.c_str()) == INVALID_FILE_ATTRIBUTES) {
        report.message =
            "Probe helper is unavailable; select the format manually.";
        return report;
    }

    report.helper_available = true;

    const auto selected = select_physical_candidates(
        formats,
        image_payload_size,
        geometry,
        geometry_reliable,
        max_candidates);

    if (selected.empty()) {
        report.message =
            "No physically plausible format was found; select manually.";
        return report;
    }

    std::vector<running_probe> probes;
    probes.reserve(selected.size());

    for (const auto& candidate : selected) {
        running_probe probe;
        probe.candidate = candidate;
        probe.result.format_name = candidate.format.fmt_name;
        probe.result.physical_score = candidate.physical_score;
        probe.result.score = std::clamp(candidate.physical_score, 0, 60);

        launch_probe(
            probe,
            helper,
            image_path,
            diskdefs_path_value,
            driver_name,
            uppercase);

        probes.push_back(std::move(probe));
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(total_timeout_ms);

    bool any_running = true;
    while (any_running && std::chrono::steady_clock::now() < deadline) {
        any_running = false;

        for (auto& probe : probes) {
            if (!probe.running)
                continue;

            const DWORD wait_result =
                WaitForSingleObject(probe.process.hProcess, 0);

            if (wait_result == WAIT_OBJECT_0)
                probe.running = false;
            else
                any_running = true;
        }

        if (any_running)
            Sleep(10);
    }

    for (auto& probe : probes) {
        if (probe.running) {
            TerminateProcess(probe.process.hProcess, 0x102);
            WaitForSingleObject(probe.process.hProcess, 250);
            probe.running = false;
            probe.result.state = cpm_safe_probe_state::timeout;
            probe.result.summary = "Probe timed out";
            probe.result.score = std::clamp(
                probe.result.physical_score / 4,
                0,
                30);
        }
        else if (probe.process.hProcess) {
            DWORD exit_code = 0;
            GetExitCodeProcess(probe.process.hProcess, &exit_code);

            if (exit_code == 0) {
                parse_probe_output(probe);
            }
            else {
                probe.result.state = cpm_safe_probe_state::crashed;
                probe.result.summary =
                    "Helper exit code " + std::to_string(exit_code);
                probe.result.score = std::clamp(
                    probe.result.physical_score / 4,
                    0,
                    30);
            }
        }

        if (probe.process.hThread)
            CloseHandle(probe.process.hThread);
        if (probe.process.hProcess)
            CloseHandle(probe.process.hProcess);
        if (!probe.output_path.empty())
            DeleteFileA(probe.output_path.c_str());

        report.candidates.push_back(std::move(probe.result));
    }

    std::stable_sort(
        report.candidates.begin(),
        report.candidates.end(),
        [](const cpm_safe_probe_candidate& left,
           const cpm_safe_probe_candidate& right) {
            const bool left_mounted =
                left.state == cpm_safe_probe_state::mounted;
            const bool right_mounted =
                right.state == cpm_safe_probe_state::mounted;

            if (left_mounted != right_mounted)
                return left_mounted > right_mounted;
            if (left.score != right.score)
                return left.score > right.score;
            return left.format_name < right.format_name;
        });

    std::vector<const cpm_safe_probe_candidate*> mounted;
    for (const auto& candidate : report.candidates) {
        if (candidate.state == cpm_safe_probe_state::mounted)
            mounted.push_back(&candidate);
    }

    if (mounted.empty()) {
        report.message =
            "No candidate passed the isolated mount test; "
            "select the format manually.";
        return report;
    }

    const int second_score =
        mounted.size() > 1 ? mounted[1]->score : -1;
    const bool clear_margin =
        mounted.size() == 1 || mounted[0]->score - second_score >= 20;

    if (mounted[0]->score >= 70 && clear_margin) {
        report.unique_match = true;
        report.selected_format = mounted[0]->format_name;
        report.message =
            "Detected in isolated helper: " +
            report.selected_format +
            " (" +
            std::to_string(mounted[0]->score) +
            "/100).";
    }
    else {
        report.message =
            "Several formats passed the isolated test; "
            "select the correct one.";
    }

    return report;
}

std::vector<cpm_disk_descr_t> cpm_rank_formats_by_probe(
    const std::vector<cpm_disk_descr_t>& formats,
    const cpm_safe_probe_report& report)
{
    std::vector<cpm_disk_descr_t> ranked = formats;

    std::stable_sort(
        ranked.begin(),
        ranked.end(),
        [&report](const cpm_disk_descr_t& left,
                  const cpm_disk_descr_t& right) {
            const auto* left_result = report.find(left.fmt_name);
            const auto* right_result = report.find(right.fmt_name);

            const int left_score = left_result ? left_result->score : -1;
            const int right_score = right_result ? right_result->score : -1;

            if (left_score != right_score)
                return left_score > right_score;

            return std::string(left.fmt_name) <
                std::string(right.fmt_name);
        });

    return ranked;
}
