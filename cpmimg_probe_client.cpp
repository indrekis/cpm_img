/*
 * Safe out-of-process CP/M format probing client.
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cpmimg_probe_client.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t imd_max_file_size =
    64u * 1024u * 1024u;
constexpr std::size_t imd_max_comment_size =
    1024u * 1024u;

struct imd_layout_summary {
    bool detected = false;
    bool valid = false;
    std::uint64_t logical_capacity = 0;
    std::uint64_t recoverable_capacity = 0;
    std::size_t track_records = 0;
    std::size_t sector_records = 0;
    std::size_t unavailable_sectors = 0;
    unsigned int max_cylinder = 0;
    unsigned int heads_mask = 0;
    unsigned int dominant_sector_size = 0;
    unsigned int dominant_sectors_per_track = 0;
    bool uniform_sector_size = false;
    bool uniform_sectors_per_track = false;
    std::map<unsigned int, std::size_t>
        sector_size_counts;
    std::map<unsigned int, std::size_t>
        sectors_per_track_counts;
    std::string error;
};

struct physical_candidate {
    cpm_disk_descr_t format{};
    int physical_score = 0;
    int priority_tier = 99;
    int geometry_matches = 0;
    bool exact_size = false;
    bool track_match = false;
    bool sector_size_match = false;
    bool sectors_per_track_match = false;
    std::string hint;
};

struct running_probe {
    physical_candidate candidate;
    cpm_safe_probe_candidate result;
    PROCESS_INFORMATION process{};
    std::string output_path;
    bool running = false;
};

bool checked_add(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (right >
        (std::numeric_limits<std::uint64_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_mul(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (left != 0 &&
        right >
            (std::numeric_limits<std::uint64_t>::max)() /
                left) {
        return false;
    }
    result = left * right;
    return true;
}

bool range_available(
    std::size_t position,
    std::size_t count,
    std::size_t total) noexcept
{
    return position <= total && count <= total - position;
}

unsigned int dominant_value(
    const std::map<unsigned int, std::size_t>& counts)
{
    unsigned int value = 0;
    std::size_t best_count = 0;

    for (const auto& entry : counts) {
        if (entry.second > best_count) {
            value = entry.first;
            best_count = entry.second;
        }
    }
    return value;
}

bool imd_mode_supported(unsigned int mode) noexcept
{
    switch (mode) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6: // LibDsk extension: ED FM
    case 9: // LibDsk extension: ED MFM
        return true;
    default:
        return false;
    }
}

imd_layout_summary parse_imd_layout(
    const char* image_path)
{
    imd_layout_summary summary;
    if (!image_path || !*image_path)
        return summary;

    std::ifstream input(
        image_path,
        std::ios::binary | std::ios::ate);
    if (!input)
        return summary;

    const auto end_position = input.tellg();
    if (end_position <= 0)
        return summary;

    const auto file_size_u64 =
        static_cast<std::uint64_t>(end_position);
    if (file_size_u64 > imd_max_file_size)
        return summary;

    const auto file_size =
        static_cast<std::size_t>(file_size_u64);
    std::vector<unsigned char> bytes(file_size);

    input.seekg(0, std::ios::beg);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input)
        return summary;

    if (bytes.size() < 4 ||
        bytes[0] != 'I' ||
        bytes[1] != 'M' ||
        bytes[2] != 'D' ||
        bytes[3] != ' ') {
        return summary;
    }

    summary.detected = true;

    const auto comment_limit =
        (std::min)(bytes.size(), imd_max_comment_size);
    std::size_t position = 0;
    while (position < comment_limit &&
           bytes[position] != 0x1a) {
        ++position;
    }

    if (position >= comment_limit ||
        bytes[position] != 0x1a) {
        summary.error = "IMD comment terminator not found";
        return summary;
    }
    ++position;

    while (position < bytes.size()) {
        if (!range_available(position, 5, bytes.size())) {
            summary.error = "truncated IMD track header";
            return summary;
        }

        const unsigned int mode = bytes[position++];
        const unsigned int cylinder = bytes[position++];
        const unsigned int head_flags = bytes[position++];
        const unsigned int sector_count = bytes[position++];
        const unsigned int size_code = bytes[position++];

        if (!imd_mode_supported(mode)) {
            summary.error = "unsupported IMD recording mode";
            return summary;
        }
        if (sector_count == 0) {
            summary.error = "IMD track has zero sectors";
            return summary;
        }

        if (!range_available(
                position,
                sector_count,
                bytes.size())) {
            summary.error = "truncated IMD sector ID map";
            return summary;
        }
        position += sector_count;

        if (head_flags & 0x80u) {
            if (!range_available(
                    position,
                    sector_count,
                    bytes.size())) {
                summary.error =
                    "truncated IMD sector cylinder map";
                return summary;
            }
            position += sector_count;
        }

        if (head_flags & 0x40u) {
            if (!range_available(
                    position,
                    sector_count,
                    bytes.size())) {
                summary.error =
                    "truncated IMD sector head map";
                return summary;
            }
            position += sector_count;
        }

        std::vector<unsigned int> sector_lengths(
            sector_count,
            0);

        if (size_code == 0xffu) {
            const std::size_t length_map_size =
                static_cast<std::size_t>(sector_count) * 2u;
            if (!range_available(
                    position,
                    length_map_size,
                    bytes.size())) {
                summary.error =
                    "truncated IMD sector length map";
                return summary;
            }

            for (unsigned int index = 0;
                 index < sector_count;
                 ++index) {
                const unsigned int low = bytes[position++];
                const unsigned int high = bytes[position++];
                sector_lengths[index] = low | (high << 8u);
                if (sector_lengths[index] == 0) {
                    summary.error =
                        "IMD sector has zero length";
                    return summary;
                }
            }
        }
        else {
            if (size_code > 7u) {
                summary.error =
                    "unsupported IMD sector size code";
                return summary;
            }
            const unsigned int sector_length =
                128u << size_code;
            std::fill(
                sector_lengths.begin(),
                sector_lengths.end(),
                sector_length);
        }

        ++summary.track_records;
        summary.max_cylinder =
            (std::max)(summary.max_cylinder, cylinder);

        const unsigned int head = head_flags & 0x3fu;
        if (head < 32u)
            summary.heads_mask |= 1u << head;

        ++summary.sectors_per_track_counts[sector_count];

        for (unsigned int index = 0;
             index < sector_count;
             ++index) {
            const unsigned int sector_length =
                sector_lengths[index];

            if (!range_available(position, 1, bytes.size())) {
                summary.error =
                    "truncated IMD sector status";
                return summary;
            }

            const unsigned int status = bytes[position++];
            if (status > 8u) {
                summary.error =
                    "unsupported IMD sector status";
                return summary;
            }

            std::uint64_t new_capacity = 0;
            if (!checked_add(
                    summary.logical_capacity,
                    sector_length,
                    new_capacity)) {
                summary.error =
                    "IMD logical capacity overflow";
                return summary;
            }
            summary.logical_capacity = new_capacity;

            if (status == 0u) {
                ++summary.unavailable_sectors;
            }
            else {
                if (!checked_add(
                        summary.recoverable_capacity,
                        sector_length,
                        new_capacity)) {
                    summary.error =
                        "IMD recoverable capacity overflow";
                    return summary;
                }
                summary.recoverable_capacity = new_capacity;
            }

            ++summary.sector_records;
            ++summary.sector_size_counts[sector_length];

            const bool compressed =
                status == 2u || status == 4u ||
                status == 6u || status == 8u;
            const bool uncompressed =
                status == 1u || status == 3u ||
                status == 5u || status == 7u;

            if (compressed) {
                if (!range_available(position, 1, bytes.size())) {
                    summary.error =
                        "truncated compressed IMD sector";
                    return summary;
                }
                ++position;
            }
            else if (uncompressed) {
                if (!range_available(
                        position,
                        sector_length,
                        bytes.size())) {
                    summary.error =
                        "truncated uncompressed IMD sector";
                    return summary;
                }
                position += sector_length;
            }
        }
    }

    if (summary.track_records == 0 ||
        summary.sector_records == 0) {
        summary.error = "IMD contains no track data";
        return summary;
    }

    summary.dominant_sector_size =
        dominant_value(summary.sector_size_counts);
    summary.dominant_sectors_per_track =
        dominant_value(summary.sectors_per_track_counts);
    summary.uniform_sector_size =
        summary.sector_size_counts.size() == 1;
    summary.uniform_sectors_per_track =
        summary.sectors_per_track_counts.size() == 1;
    summary.valid = true;
    return summary;
}

std::uint64_t expected_data_size(
    const cpm_disk_descr_t& format) noexcept
{
    if (format.secLength <= 0 ||
        format.sectrk <= 0 ||
        format.tracks <= 0) {
        return 0;
    }

    std::uint64_t sectors = 0;
    std::uint64_t bytes = 0;
    if (!checked_mul(
            static_cast<std::uint64_t>(format.sectrk),
            static_cast<std::uint64_t>(format.tracks),
            sectors) ||
        !checked_mul(
            sectors,
            static_cast<std::uint64_t>(format.secLength),
            bytes)) {
        return 0;
    }
    return bytes;
}

std::uint64_t expected_raw_size(
    const cpm_disk_descr_t& format) noexcept
{
    const auto data_size = expected_data_size(format);
    if (data_size == 0 || format.offset < 0)
        return 0;

    std::uint64_t result = 0;
    if (!checked_add(
            data_size,
            static_cast<std::uint64_t>(format.offset),
            result)) {
        return 0;
    }
    return result;
}

std::vector<cpm_disk_descr_t> deduplicate_formats(
    const std::vector<cpm_disk_descr_t>& formats)
{
    std::vector<cpm_disk_descr_t> unique;
    unique.reserve(formats.size());
    std::set<std::string> seen_names;

    for (const auto& format : formats) {
        const std::string name = format.fmt_name;
        if (name.empty())
            continue;
        if (seen_names.insert(name).second)
            unique.push_back(format);
    }
    return unique;
}

int geometry_match_count(
    const cpm_disk_descr_t& format,
    const DSK_GEOMETRY& geometry,
    bool geometry_reliable) noexcept
{
    if (!geometry_reliable)
        return 0;

    int matches = 0;
    if (geometry.dg_secsize == format.secLength)
        ++matches;

    const int total_tracks =
        geometry.dg_cylinders * geometry.dg_heads;
    if (total_tracks == format.tracks)
        ++matches;

    if (geometry.dg_sectors == format.sectrk)
        ++matches;

    return matches;
}

std::string join_hints(
    const std::vector<std::string>& hints)
{
    std::string result;
    for (const auto& hint : hints) {
        if (!result.empty())
            result += ", ";
        result += hint;
    }
    return result;
}

physical_candidate make_candidate(
    const cpm_disk_descr_t& format,
    std::uint64_t image_payload_size,
    const DSK_GEOMETRY& geometry,
    bool geometry_reliable,
    const imd_layout_summary& imd)
{
    physical_candidate candidate;
    candidate.format = format;
    candidate.geometry_matches =
        geometry_match_count(
            format,
            geometry,
            geometry_reliable);

    const auto data_size = expected_data_size(format);
    const auto raw_size = expected_raw_size(format);
    std::vector<std::string> hints;

    if (imd.valid) {
        candidate.exact_size =
            imd.logical_capacity != 0 &&
            (imd.logical_capacity == data_size ||
             imd.logical_capacity == raw_size);
        candidate.track_match =
            imd.track_records ==
            static_cast<std::size_t>(format.tracks);

        const bool sector_size_present =
            imd.sector_size_counts.count(
                static_cast<unsigned int>(format.secLength)) != 0;
        candidate.sector_size_match =
            imd.dominant_sector_size ==
            static_cast<unsigned int>(format.secLength);

        const bool sectors_per_track_present =
            imd.sectors_per_track_counts.count(
                static_cast<unsigned int>(format.sectrk)) != 0;
        candidate.sectors_per_track_match =
            imd.dominant_sectors_per_track ==
            static_cast<unsigned int>(format.sectrk);

        if (candidate.exact_size) {
            candidate.physical_score += 60;
            hints.emplace_back("IMD logical size");
        }
        if (candidate.track_match) {
            candidate.physical_score += 18;
            hints.emplace_back("track count");
        }
        if (candidate.sector_size_match) {
            candidate.physical_score += 12;
            hints.emplace_back("dominant sector size");
        }
        else if (sector_size_present) {
            candidate.physical_score += 7;
            hints.emplace_back("sector size present");
        }
        if (candidate.sectors_per_track_match) {
            candidate.physical_score += 12;
            hints.emplace_back("dominant sectors/track");
        }
        else if (sectors_per_track_present) {
            candidate.physical_score += 7;
            hints.emplace_back("sectors/track present");
        }

        candidate.physical_score +=
            candidate.geometry_matches * 3;
        if (candidate.geometry_matches > 0) {
            hints.emplace_back(
                "LibDsk geometry " +
                std::to_string(candidate.geometry_matches) +
                "/3");
        }

        const int layout_matches =
            (candidate.track_match ? 1 : 0) +
            (candidate.sector_size_match ? 1 : 0) +
            (candidate.sectors_per_track_match ? 1 : 0);

        if (candidate.exact_size && layout_matches == 3)
            candidate.priority_tier = 0;
        else if (candidate.exact_size && layout_matches >= 1)
            candidate.priority_tier = 1;
        else if (candidate.exact_size)
            candidate.priority_tier = 2;
        else if (layout_matches == 3)
            candidate.priority_tier = 3;
        else if (layout_matches >= 2 ||
                 candidate.geometry_matches >= 2)
            candidate.priority_tier = 4;
        else if (layout_matches >= 1 ||
                 candidate.geometry_matches >= 1)
            candidate.priority_tier = 5;
        else
            candidate.priority_tier = 6;
    }
    else {
        candidate.exact_size =
            image_payload_size != 0 &&
            raw_size != 0 &&
            image_payload_size == raw_size;

        if (candidate.exact_size) {
            candidate.physical_score += 55;
            hints.emplace_back("raw size");
        }
        else if (image_payload_size != 0 && raw_size != 0) {
            candidate.physical_score -= 25;
        }

        candidate.physical_score +=
            candidate.geometry_matches * 12;
        if (candidate.geometry_matches > 0) {
            hints.emplace_back(
                "LibDsk geometry " +
                std::to_string(candidate.geometry_matches) +
                "/3");
        }

        if (candidate.exact_size &&
            candidate.geometry_matches == 3)
            candidate.priority_tier = 0;
        else if (candidate.exact_size)
            candidate.priority_tier = 1;
        else if (candidate.geometry_matches == 3)
            candidate.priority_tier = 2;
        else if (candidate.geometry_matches == 2)
            candidate.priority_tier = 3;
        else if (candidate.geometry_matches == 1)
            candidate.priority_tier = 4;
        else
            candidate.priority_tier = 5;
    }

    candidate.physical_score = std::clamp(
        candidate.physical_score,
        -50,
        100);
    candidate.hint = join_hints(hints);
    if (candidate.hint.empty())
        candidate.hint = "no physical hint";
    return candidate;
}

std::vector<physical_candidate> rank_candidates(
    const std::vector<cpm_disk_descr_t>& formats,
    std::uint64_t image_payload_size,
    const DSK_GEOMETRY& geometry,
    bool geometry_reliable,
    const imd_layout_summary& imd)
{
    const auto unique_formats = deduplicate_formats(formats);
    std::vector<physical_candidate> candidates;
    candidates.reserve(unique_formats.size());

    for (const auto& format : unique_formats) {
        candidates.push_back(
            make_candidate(
                format,
                image_payload_size,
                geometry,
                geometry_reliable,
                imd));
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const physical_candidate& left,
           const physical_candidate& right) {
            if (left.priority_tier != right.priority_tier)
                return left.priority_tier < right.priority_tier;
            if (left.physical_score != right.physical_score)
                return left.physical_score > right.physical_score;
            return std::string(left.format.fmt_name) <
                std::string(right.format.fmt_name);
        });

    return candidates;
}

std::string quote_windows_argument(
    const std::string& argument)
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
    const auto address = reinterpret_cast<LPCSTR>(
        &cpm_run_safe_format_probes);

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
    constexpr const char* helper_name =
        "cpmimg_probe64.exe";
#else
    constexpr const char* helper_name =
        "cpmimg_probe32.exe";
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
    if (!GetTempFileNameA(
            temp_directory,
            "cpi",
            0,
            temp_file)) {
        return {};
    }

    DeleteFileA(temp_file);
    return temp_file;
}

void append_hint(
    cpm_safe_probe_candidate& result,
    const physical_candidate& candidate)
{
    if (!candidate.hint.empty()) {
        result.summary += "; priority " +
            std::to_string(candidate.priority_tier) +
            ": " + candidate.hint;
    }
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
        probe.result.state =
            cpm_safe_probe_state::helper_error;
        probe.result.summary =
            "Could not create temporary output file";
        append_hint(probe.result, probe.candidate);
        return false;
    }

    std::string command;
    command.reserve(1024);
    command += quote_windows_argument(helper);
    command += " --image ";
    command += quote_windows_argument(
        image_path ? image_path : "");
    command += " --diskdefs ";
    command += quote_windows_argument(
        diskdefs_path_value ? diskdefs_path_value : "");
    command += " --format ";
    command += quote_windows_argument(
        probe.candidate.format.fmt_name);
    command += " --driver ";
    command += quote_windows_argument(
        driver_name ? driver_name : "");
    command += " --uppercase ";
    command += uppercase ? "1" : "0";
    command += " --output ";
    command += quote_windows_argument(probe.output_path);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);

    std::vector<char> mutable_command(
        command.begin(),
        command.end());
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
        probe.result.state =
            cpm_safe_probe_state::helper_error;
        probe.result.summary =
            "CreateProcess failed: " +
            std::to_string(GetLastError());
        append_hint(probe.result, probe.candidate);
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
        probe.result.state =
            cpm_safe_probe_state::crashed;
        probe.result.summary =
            "Helper returned no result";
        append_hint(probe.result, probe.candidate);
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
        probe.result.state =
            cpm_safe_probe_state::crashed;
        probe.result.summary =
            "Malformed helper result";
        append_hint(probe.result, probe.candidate);
        return false;
    }

    try {
        const bool mounted =
            std::stoi(mounted_text) != 0;
        probe.result.files =
            static_cast<std::size_t>(
                std::stoull(files_text));
        probe.result.extents =
            static_cast<std::size_t>(
                std::stoull(extents_text));
        probe.result.used_blocks =
            static_cast<std::size_t>(
                std::stoull(blocks_text));

        probe.result.state = mounted
            ? cpm_safe_probe_state::mounted
            : cpm_safe_probe_state::rejected;

        if (mounted) {
            probe.result.score = std::clamp(
                55 + probe.result.physical_score +
                    static_cast<int>(
                        (std::min<std::size_t>)(
                            probe.result.files,
                            10)),
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
                probe.result.physical_score / 2,
                0,
                50);
            probe.result.summary = "Mount rejected";
        }
        append_hint(probe.result, probe.candidate);
    }
    catch (...) {
        probe.result.state =
            cpm_safe_probe_state::crashed;
        probe.result.summary =
            "Invalid numeric helper result";
        append_hint(probe.result, probe.candidate);
        return false;
    }

    return true;
}

std::vector<cpm_safe_probe_candidate> run_probe_batch(
    const std::vector<physical_candidate>& candidates,
    std::size_t begin,
    std::size_t end,
    const std::string& helper,
    const char* image_path,
    const char* diskdefs_path_value,
    const char* driver_name,
    bool uppercase,
    unsigned int batch_timeout_ms)
{
    std::vector<running_probe> probes;
    probes.reserve(end - begin);

    for (std::size_t index = begin; index < end; ++index) {
        running_probe probe;
        probe.candidate = candidates[index];
        probe.result.format_name =
            probe.candidate.format.fmt_name;
        probe.result.physical_score =
            probe.candidate.physical_score;
        probe.result.score = std::clamp(
            probe.candidate.physical_score,
            0,
            70);

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
        std::chrono::milliseconds(batch_timeout_ms);

    bool any_running = true;
    while (any_running &&
           std::chrono::steady_clock::now() < deadline) {
        any_running = false;

        for (auto& probe : probes) {
            if (!probe.running)
                continue;

            const DWORD wait_result =
                WaitForSingleObject(
                    probe.process.hProcess,
                    0);

            if (wait_result == WAIT_OBJECT_0)
                probe.running = false;
            else
                any_running = true;
        }

        if (any_running)
            Sleep(10);
    }

    std::vector<cpm_safe_probe_candidate> results;
    results.reserve(probes.size());

    for (auto& probe : probes) {
        if (probe.running) {
            TerminateProcess(
                probe.process.hProcess,
                0x102);
            WaitForSingleObject(
                probe.process.hProcess,
                250);
            probe.running = false;
            probe.result.state =
                cpm_safe_probe_state::timeout;
            probe.result.summary =
                "Probe timed out";
            probe.result.score = std::clamp(
                probe.result.physical_score / 4,
                0,
                30);
            append_hint(
                probe.result,
                probe.candidate);
        }
        else if (probe.process.hProcess) {
            DWORD exit_code = 0;
            GetExitCodeProcess(
                probe.process.hProcess,
                &exit_code);

            if (exit_code == 0) {
                parse_probe_output(probe);
            }
            else {
                probe.result.state =
                    cpm_safe_probe_state::crashed;
                probe.result.summary =
                    "Helper exit code " +
                    std::to_string(exit_code);
                probe.result.score = std::clamp(
                    probe.result.physical_score / 4,
                    0,
                    30);
                append_hint(
                    probe.result,
                    probe.candidate);
            }
        }

        if (probe.process.hThread)
            CloseHandle(probe.process.hThread);
        if (probe.process.hProcess)
            CloseHandle(probe.process.hProcess);
        if (!probe.output_path.empty())
            DeleteFileA(probe.output_path.c_str());

        results.push_back(std::move(probe.result));
    }

    return results;
}

bool has_mounted_candidate(
    const std::vector<cpm_safe_probe_candidate>& results)
{
    return std::any_of(
        results.begin(),
        results.end(),
        [](const cpm_safe_probe_candidate& candidate) {
            return candidate.state ==
                cpm_safe_probe_state::mounted;
        });
}

std::string imd_report_prefix(
    const imd_layout_summary& imd)
{
    if (!imd.detected)
        return {};

    if (!imd.valid) {
        return "IMD layout parse failed" +
            (imd.error.empty()
                ? std::string("; ")
                : std::string(": ") + imd.error + "; ");
    }

    return "IMD logical size " +
        std::to_string(imd.logical_capacity) +
        " bytes, " +
        std::to_string(imd.track_records) +
        " track records; ";
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
        [format_name](
            const cpm_safe_probe_candidate& candidate) {
            return candidate.format_name == format_name;
        });

    return found == candidates.end()
        ? nullptr
        : &*found;
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
    unsigned int batch_timeout_ms,
    std::size_t batch_size,
    std::size_t max_total_candidates)
{
    cpm_safe_probe_report report;

    const std::string helper = helper_path();
    if (helper.empty() ||
        GetFileAttributesA(helper.c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
        report.message =
            "Probe helper is unavailable; "
            "select the format manually.";
        return report;
    }
    report.helper_available = true;

    if (batch_size == 0)
        batch_size = 1;
    if (max_total_candidates == 0)
        max_total_candidates = batch_size;

    const auto imd = parse_imd_layout(image_path);
    report.container_layout_available = imd.valid;
    report.logical_capacity = imd.logical_capacity;

    const auto ranked = rank_candidates(
        formats,
        image_payload_size,
        geometry,
        geometry_reliable,
        imd);

    if (ranked.empty()) {
        report.message =
            imd_report_prefix(imd) +
            "No disk definitions are available; "
            "select the format manually.";
        return report;
    }

    std::size_t tier_begin = 0;
    bool mounted_in_best_successful_tier = false;

    while (tier_begin < ranked.size() &&
           report.attempted_candidates <
               max_total_candidates) {
        const int tier = ranked[tier_begin].priority_tier;
        std::size_t tier_end = tier_begin;
        while (tier_end < ranked.size() &&
               ranked[tier_end].priority_tier == tier) {
            ++tier_end;
        }

        bool tier_mounted = false;
        std::size_t batch_begin = tier_begin;

        while (batch_begin < tier_end &&
               report.attempted_candidates <
                   max_total_candidates) {
            const std::size_t remaining_budget =
                max_total_candidates -
                report.attempted_candidates;
            const std::size_t batch_end =
                (std::min)(
                    tier_end,
                    batch_begin +
                        (std::min)(
                            batch_size,
                            remaining_budget));

            auto batch_results = run_probe_batch(
                ranked,
                batch_begin,
                batch_end,
                helper,
                image_path,
                diskdefs_path_value,
                driver_name,
                uppercase,
                batch_timeout_ms);

            tier_mounted = tier_mounted ||
                has_mounted_candidate(batch_results);
            report.attempted_candidates +=
                batch_results.size();

            for (auto& result : batch_results)
                report.candidates.push_back(
                    std::move(result));

            batch_begin = batch_end;
        }

        if (tier_mounted) {
            mounted_in_best_successful_tier = true;
            break;
        }

        tier_begin = tier_end;
    }

    std::stable_sort(
        report.candidates.begin(),
        report.candidates.end(),
        [](const cpm_safe_probe_candidate& left,
           const cpm_safe_probe_candidate& right) {
            const bool left_mounted =
                left.state ==
                cpm_safe_probe_state::mounted;
            const bool right_mounted =
                right.state ==
                cpm_safe_probe_state::mounted;

            if (left_mounted != right_mounted)
                return left_mounted > right_mounted;
            if (left.score != right.score)
                return left.score > right.score;
            return left.format_name < right.format_name;
        });

    std::vector<const cpm_safe_probe_candidate*> mounted;
    for (const auto& candidate : report.candidates) {
        if (candidate.state ==
            cpm_safe_probe_state::mounted) {
            mounted.push_back(&candidate);
        }
    }

    const auto prefix = imd_report_prefix(imd);

    if (mounted.empty()) {
        const bool limited =
            report.attempted_candidates < ranked.size();
        report.message =
            prefix +
            "No candidate passed the isolated mount test (" +
            std::to_string(report.attempted_candidates) +
            " prioritized formats tested" +
            (limited ? ", search limit reached" : "") +
            "); select the format manually.";
        return report;
    }

    const int second_score =
        mounted.size() > 1
            ? mounted[1]->score
            : -1;
    const bool clear_margin =
        mounted.size() == 1 ||
        mounted[0]->score - second_score >= 20;

    if (mounted_in_best_successful_tier &&
        mounted[0]->score >= 70 &&
        clear_margin) {
        report.unique_match = true;
        report.selected_format =
            mounted[0]->format_name;
        report.message =
            prefix +
            "Detected in isolated helper: " +
            report.selected_format +
            " (" +
            std::to_string(mounted[0]->score) +
            "/100; " +
            std::to_string(report.attempted_candidates) +
            " prioritized formats tested).";
    }
    else {
        report.message =
            prefix +
            "Several formats passed the isolated test; "
            "select the correct one.";
    }

    return report;
}

std::vector<cpm_disk_descr_t> cpm_rank_formats_by_probe(
    const std::vector<cpm_disk_descr_t>& formats,
    const cpm_safe_probe_report& report)
{
    auto ranked = deduplicate_formats(formats);

    std::stable_sort(
        ranked.begin(),
        ranked.end(),
        [&report](const cpm_disk_descr_t& left,
                  const cpm_disk_descr_t& right) {
            const auto* left_result =
                report.find(left.fmt_name);
            const auto* right_result =
                report.find(right.fmt_name);

            const int left_score =
                left_result ? left_result->score : -1;
            const int right_score =
                right_result ? right_result->score : -1;

            if (left_score != right_score)
                return left_score > right_score;

            return std::string(left.fmt_name) <
                std::string(right.fmt_name);
        });

    return ranked;
}
