/*
 * Safe out-of-process CP/M format probing interface.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "plugin_config.h"
#include "libdsk.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class cpm_safe_probe_state {
    mounted,
    rejected,
    timeout,
    crashed,
    helper_error
};

struct cpm_safe_probe_candidate {
    std::string format_name;
    cpm_safe_probe_state state =
        cpm_safe_probe_state::helper_error;
    int score = 0;
    int physical_score = 0;
    std::size_t files = 0;
    std::size_t extents = 0;
    std::size_t used_blocks = 0;
    std::string summary;
};

struct cpm_safe_probe_report {
    bool helper_available = false;
    bool unique_match = false;
    bool container_layout_available = false;
    std::uint64_t logical_capacity = 0;
    std::size_t attempted_candidates = 0;
    std::string selected_format;
    std::string message;
    std::vector<cpm_safe_probe_candidate> candidates;

    const cpm_safe_probe_candidate* find(
        const char* format_name) const noexcept;
};

cpm_safe_probe_report cpm_run_safe_format_probes(
    const char* image_path,
    const char* diskdefs_path,
    const char* driver_name,
    const std::vector<cpm_disk_descr_t>& formats,
    std::uint64_t image_payload_size,
    const DSK_GEOMETRY& geometry,
    bool geometry_reliable,
    bool uppercase,
    unsigned int batch_timeout_ms = 1500,
    std::size_t batch_size = 7,
    std::size_t max_total_candidates = 42);

std::vector<cpm_disk_descr_t> cpm_rank_formats_by_probe(
    const std::vector<cpm_disk_descr_t>& formats,
    const cpm_safe_probe_report& report);
