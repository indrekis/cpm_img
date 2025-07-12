/*
* Copyright (c) 2017-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
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
	minimal_fixed_string_t<MAX_PATH> diskdefs_file_path;
	uint32_t plugin_interface_version_lo = 0;
	uint32_t plugin_interface_version_hi = 0;
#if !defined NDEBUG
	bool allow_dialogs = true;
	bool allow_txt_log = true;
#else 
	bool allow_dialogs = false;
	bool allow_txt_log = false;
#endif
	minimal_fixed_string_t<MAX_PATH> log_file_path;
	file_handle_t log_file = file_handle_t();

	//! Enum is not convenient here because of I/O
	static constexpr int NO_DEBUG     = 0;
	static constexpr int DEBUGGER_MSG = 1;
	static constexpr int GUI_MSG      = 2;
	int debug_level = NO_DEBUG;
	//------------------------------------------------------------
	bool read_conf (const PackDefaultParamStruct* dps, bool reread);
	bool write_conf();

	minimal_fixed_string_t<33> image_format{"osbexec1"};
private:
	using options_map_t = std::map<std::string, std::string>;

	int file_to_options_map(std::FILE* cf);

	template<typename T>
	T get_option_from_map(const std::string& option_name) const;

	bool validate_conf();

	options_map_t options_map;

	constexpr static const char* inifilename = "\\cpmdiskimg.ini";

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