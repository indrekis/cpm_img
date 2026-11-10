/*
* Copyright (c) 2017-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* SPDX-License-Identifier: GPL-3.0-only
* This file is part of CPMimg and is licensed under GNU GPL v3; see LICENSE.txt.
*/
#ifndef PLUGIN_CONFIG_H_INCLUDED
#define PLUGIN_CONFIG_H_INCLUDED

#include "minimal_fixed_string.h"
#include "cpmtools/cpmfs.h"

#include <string>
#include <map>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>
#include "sysio_winapi.h"
#endif 
#include "wcxhead.h"


struct plugin_config_t {
	minimal_fixed_string_t<MAX_PATH> config_file_path;
	minimal_fixed_string_t<MAX_PATH> plugin_path;
	// Resolved paths, for opening files. The *_setting strings hold the text
	// exactly as written in the INI, and only those are saved, so that saving
	// cannot turn a %VAR% or relative setting into a machine-specific path.
	minimal_fixed_string_t<MAX_PATH> diskdefs_file_path;
	std::string diskdefs_file_setting = default_diskdefs_setting;
	uint32_t plugin_interface_version_lo = 0;
	uint32_t plugin_interface_version_hi = 0;
#if !defined NDEBUG
	bool allow_txt_log = true;
#else
	bool allow_txt_log = false;
#endif
	minimal_fixed_string_t<MAX_PATH> log_file_path;
	std::string log_file_setting = default_log_setting;
	file_handle_t log_file = file_handle_t();

	//! Enum is not convenient here because of I/O
	static constexpr int NO_DEBUG     = 0;
	static constexpr int DEBUGGER_MSG = 1;
	static constexpr int GUI_MSG      = 2;
	int debug_level = NO_DEBUG;
	//------------------------------------------------------------
	bool read_conf (const PackDefaultParamStruct* dps, bool reread);
	bool write_conf();
	void set_default_diskdefs_path();
	// Text for the user about settings that leave the plugin unable to mount
	// images or to log; empty when there is nothing to report. *cannot_mount
	// tells whether the problem stops every image operation.
	std::string describe_path_problems(bool* cannot_mount = nullptr) const;

	minimal_fixed_string_t<33> image_format{"osbexec1"};
	// Missing option in an older INI preserves the existing enabled behaviour.
	bool enable_format_probing = true;
	bool show_disk_info_file = true;
private:
	using options_map_t = std::map<std::string, std::string>;

	int file_to_options_map(std::FILE* cf);

	template<typename T>
	T get_option_from_map(const std::string& option_name) const;

	bool validate_conf();

	bool resolve_configured_path(
		const char* option_name,
		const std::string& setting,
		const char* base_dir,
		minimal_fixed_string_t<MAX_PATH>& resolved,
		std::string& problem);
	void open_log_file();
	void report_config_problem(const std::string& message, std::string& problem);

	options_map_t options_map;
	minimal_fixed_string_t<MAX_PATH> config_dir;
	bool log_open_attempted = false;
	bool log_open_failed = false;
	std::string opened_log_setting;
	std::string diskdefs_path_problem;
	std::string log_path_problem;
	std::string image_format_problem;

	constexpr static const char* inifilename = "\\cpmdiskimg.ini";
	// Relative on purpose: a newly created INI must stay valid when a portable
	// installation moves to another folder or drive letter.
	constexpr static const char* default_diskdefs_setting = "diskdefs";
	constexpr static const char* default_log_setting = "cpmimg.log";

public:
	template<typename... Args>
	void log_print(const char* format, Args&&... args) {
		if (allow_txt_log) {
			log_print_f(log_file, format, std::forward<Args>(args)...);
		}
	}

	template<typename... Args>
	void log_print_dbg(const char* format, Args&&... args) {
		debug_print(format, std::forward<Args>(args)...);
		log_print(format, std::forward<Args>(args)...);
	}

};

//! Using the cpmSuperBlock is totally failing -- WTF even push_back does not work, pushin garbage sometimes. 
//! So here our clone.

struct cpm_disk_descr_t
{
	int uppercase = 0;

	int secLength = 0;
	int tracks = 0;
	int sectrk = 0;
	int blksiz = 0;
	int maxdir = 0;
	int dirblks = 0;
	int skew = 0;
	int bootsec = 0;
	int boottrk = 0;
	off_t offset = 0;
	int type = 0;
	int size = 0;
	int extents = 0; /* logical extents per physical extent */
	int* skewtab = nullptr;
	char fmt_name[256] = { 0 };
};

std::vector<cpm_disk_descr_t> parse_diskdefs_c(const char* filename);

#endif 