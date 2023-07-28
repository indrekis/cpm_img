// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* CP/M floppy disk images plugin for the Total Commander.
* Copyright (c) 2022-2023, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
*/


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "cpmtools/config.h"
#include "cpmtools/cpmfs.h"
#include "cpmtools/getopt_.h"

#include "sysio_winapi.h"
#include "minimal_fixed_string.h"
#include "plugin_config.h"

#include "wcxhead.h"
#include <new>
#include <memory>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <optional>
#include <map>
#include <cassert>

// #define FLTK_ENABLED_EXPERIMENTAL  // Here for the quick tests -- should be defined by the build system

#ifdef FLTK_ENABLED_EXPERIMENTAL
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/fl_ask.H>
#endif

using std::nothrow, std::uint8_t;

#ifdef _WIN32
#define WCX_PLUGIN_EXPORTS
#endif 

#ifdef WCX_PLUGIN_EXPORTS
#define DLLEXPORT __declspec(dllexport)
#define STDCALL __stdcall
//! Not enough for the Win32 -- exports would be decorated by: _name@XX.
//! This can help but reverting to the def-file is simpler:
//! #pragma comment(linker, "/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__) 
#else
#define WCX_API
#define STDCALL
#endif 

// For cpmfs.c
char const cmd[] = "cpmimg_wcx";

plugin_config_t plugin_config;

// The DLL entry point
BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	// For some reason is called many-many times. 
	auto rdconf = plugin_config.read_conf(nullptr, true);
	return TRUE;
}

bool set_file_attributes_ex(const char* filename, uint32_t attribute) {
	// TODO: Fix
	/*
	DWORD winattr = FILE_ATTRIBUTE_NORMAL;
	if (attribute.is_readonly()) winattr |= FILE_ATTRIBUTE_READONLY;
	if (attribute.is_archive() ) winattr |= FILE_ATTRIBUTE_ARCHIVE;
	if (attribute.is_hidden()  ) winattr |= FILE_ATTRIBUTE_HIDDEN;
	if (attribute.is_system()  ) winattr |= FILE_ATTRIBUTE_SYSTEM;
	*/
	return set_file_attributes(filename, attribute); // Codes are equal
}

struct whole_disk_t {
	minimal_fixed_string_t<MAX_PATH> archname; // Should be saved for the TCmd API
	file_handle_t hArchFile = file_handle_t(); //opened file handles
	int openmode_m = PK_OM_LIST;
	size_t image_file_size = 0;	

	tChangeVolProc   pLocChangeVol = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	cpmSuperBlock super;
	cpmInode root;
	std::string format{FORMAT}; //  osb1sssd, osbexec1
	// struct cpmInode root;
	std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
	bool use_uppercase = true;

	//! TODO: rename 
	char starlit[2] = "*";
	char* const star[1] = { starlit };
	char** gargv = nullptr;
	int gargc;

	uint32_t curren_file_counter = 0;

	whole_disk_t(const char* archname_in, size_t vol_size, file_handle_t fh, int openmode):
		hArchFile{ fh }, openmode_m(openmode), image_file_size(vol_size)
	{
		archname.push_back(archname_in);
	}

	uint32_t users_counter = 0;

	//! TODO: How to support F1-F4 attributes
	static int cpm_attr_to_tcmd_attr(cpm_attr_t attr) {
		int res = 0;
		if (attr & CPM_ATTR_RO)
			res |= 0x01;
		if (attr & CPM_ATTR_SYS)
			res |= 0x04;
		if (attr & CPM_ATTR_ARCV)
			res |= 0x20;
		return res;
	}

	~whole_disk_t() {
		if (gargv) {
			cpmglobfree(gargv, gargc);
			// TODO: fix call of cpmUmount
			cpmUmount(&super);
//			Device_close(&super.dev);
		}
		if (hArchFile)
			close_file(hArchFile);
	}

};
//------- whole_disk_t implementation --------------------------
using archive_HANDLE = whole_disk_t*;

//-----------------------=[ DLL exports ]=--------------------

extern "C" {
	// OpenArchive should perform all necessary operations when an archive is to be opened
	DLLEXPORT archive_HANDLE STDCALL OpenArchive(tOpenArchiveData* ArchiveData)
	{
		auto rdconf = plugin_config.read_conf(nullptr, true); // Reread confuguration
		plugin_config.log_print("\n\nInfo# Opening file: %s", ArchiveData->ArcName);

		std::unique_ptr<whole_disk_t> arch; // TCmd API expects HANDLE/raw pointer,
										// so smart pointer is used to manage cleanup on errors 
										// only inside this function
		//! Not used by TCmd yet.
		ArchiveData->CmtBuf = 0;
		ArchiveData->CmtBufSize = 0;
		ArchiveData->CmtSize = 0;
		ArchiveData->CmtState = 0;

		auto hArchFile = open_file_shared_read(ArchiveData->ArcName);
		if (hArchFile == file_open_error_v)
		{
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}
		try {
			arch = std::make_unique<whole_disk_t>(ArchiveData->ArcName, 0,
				hArchFile, ArchiveData->OpenMode);
		}
		catch (std::bad_alloc&) {
			ArchiveData->OpenResult = E_NO_MEMORY;
			return nullptr;
		}

		const char* errs = Device_open(&(arch->super.dev), ArchiveData->ArcName, O_RDONLY,
			arch->driver_name.empty() ? nullptr : arch->driver_name.c_str());

		if (errs) // Pointer to error string 
		{
			plugin_config.log_print("\n\nError# Failed opening file: %s", errs);
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}
		int erri = cpmReadSuper(&arch->super, &arch->root, 
			arch->format.empty() ? nullptr : arch->format.c_str(), 
			arch->use_uppercase);
		if (erri == -1) 
		{
			plugin_config.log_print("\n\nError# Failed reading superblock.");
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}

		cpmglob(0, 1, arch->star, &arch->root, &arch->gargc, &arch->gargv);
		for (int i = 0; i < arch->gargc; ++i) {
			if ( arch->gargv[i][0] != '.' && 
				(arch->gargv[i][0] != '0' || arch->gargv[i][1] != '0') 
			   ) {
				++arch->users_counter;
			}
		}

		return arch.release(); // Returns raw ptr and releases ownership 
	}

	// TCmd calls ReadHeader to find out what files are in the archive
	DLLEXPORT int STDCALL ReadHeader(archive_HANDLE hArcData, tHeaderData* HeaderData)
	{
		if (hArcData->curren_file_counter < hArcData->gargc)
		{
			auto root_ino = &hArcData->root;
			cpmInode file_ino;
			auto dirent_raw_ptr = hArcData->gargv[hArcData->curren_file_counter];

			if (strcmp(dirent_raw_ptr, "..") == 0) { // Skip
				++hArcData->curren_file_counter;
				if( hArcData->curren_file_counter < hArcData->gargc )
					dirent_raw_ptr = hArcData->gargv[hArcData->curren_file_counter];
				else 
					return E_END_ARCHIVE;
			}

			cpmNamei(root_ino, dirent_raw_ptr, &file_ino);

			strcpy(HeaderData->ArcName, hArcData->archname.data());

			if (hArcData->users_counter == 0)
			{
				
				strcpy(HeaderData->FileName, dirent_raw_ptr + 2);
			}
			else {
				minimal_fixed_string_t<MAX_PATH> ts{dirent_raw_ptr, 2};
				ts.push_back("\\");
				ts.push_back(dirent_raw_ptr + 2);
				strcpy(HeaderData->FileName, ts.data());
			}
			HeaderData->FileAttr = hArcData->cpm_attr_to_tcmd_attr(file_ino.attr);
			// TODO: check and fix time
			HeaderData->FileTime = file_ino.mtime;
			HeaderData->PackSize = file_ino.size; // (statbuf.size+127)/128
			HeaderData->UnpSize = HeaderData->PackSize;
			HeaderData->CmtBuf = 0;
			HeaderData->CmtBufSize = 0;
			HeaderData->CmtSize = 0;
			HeaderData->CmtState = 0;
			HeaderData->UnpVer = 0;
			HeaderData->Method = 0;
			HeaderData->FileCRC = 0;
			++hArcData->curren_file_counter;
			return 0;
		}
		
		hArcData->curren_file_counter = 0;
		return E_END_ARCHIVE;
	}

	// ProcessFile should unpack the specified file or test the integrity of the archive
	DLLEXPORT int STDCALL ProcessFile(archive_HANDLE hArcData, int Operation, char* DestPath, char* DestName) //-V2009
	{
		char dest[MAX_PATH] = "";
		file_handle_t hUnpFile;

		if (Operation == PK_SKIP) return 0;

		if (hArcData->curren_file_counter > hArcData->gargc)
			return E_NO_MEMORY; // Logic error

		if ( hArcData->curren_file_counter == 0 )
			return E_END_ARCHIVE;
		// if (newentry->FileAttr & ATTR_DIRECTORY) return 0;

		if (Operation == PK_TEST) {
			auto res = get_temp_filename(dest, "FIM");
			if (!res) {
				return E_ECREATE;
			}
		}
		else {
			if (DestPath) strcpy(dest, DestPath);
			if (DestName) strcat(dest, DestName);
		}

		struct cpmFile file;
		auto root_ino = &hArcData->root;
		cpmInode file_ino;
		auto dirent_raw_ptr = hArcData->gargv[hArcData->curren_file_counter - 1];
		cpmNamei(root_ino, dirent_raw_ptr, &file_ino);

		std::unique_ptr<char[]> buf{ new char[file_ino.size] };
		cpmOpen(&file_ino, &file, O_RDONLY);
		auto rres = cpmRead(&file, buf.get(), file_ino.size);
		if (Operation == PK_TEST) {
			hUnpFile = open_file_overwrite(dest);
		}
		else {
			hUnpFile = open_file_write(dest);
		}
		if (hUnpFile == file_open_error_v)
			return E_ECREATE;
		write_file(hUnpFile, buf.get(), file_ino.size);

		if(file_ino.mtime != 0 )
			set_file_datetime(hUnpFile, file_ino.mtime); // TODO: Check and fix
		close_file(hUnpFile);
		set_file_attributes_ex(dest, hArcData->cpm_attr_to_tcmd_attr(file_ino.attr) );

		if (Operation == PK_TEST) {
			delete_file(dest);
		}
		return 0;
	}

	// CloseArchive should perform all necessary operations when an archive is about to be closed
	DLLEXPORT int STDCALL CloseArchive(archive_HANDLE hArcData)
	{
		delete hArcData;
		hArcData = nullptr;
		return 0; // OK
	}

	// This function allows you to notify user about changing a volume when packing files
	DLLEXPORT void STDCALL SetChangeVolProc(archive_HANDLE hArcData, tChangeVolProc pChangeVolProc)
	{
		if( hArcData != reinterpret_cast<archive_HANDLE>(static_cast<size_t>(-1)) )
			hArcData->pLocChangeVol = pChangeVolProc;
	}

	// This function allows you to notify user about the progress when you un/pack files
	DLLEXPORT void STDCALL SetProcessDataProc(archive_HANDLE hArcData, tProcessDataProc pProcessDataProc)
	{
		if ( hArcData != reinterpret_cast<archive_HANDLE>( static_cast<size_t>(- 1)) )
			hArcData->pLocProcessData = pProcessDataProc;
	}

	// PackSetDefaultParams is called immediately after loading the DLL, before any other function. 
	// This function is new in version 2.1. 
	// It requires Total Commander >=5.51, but is ignored by older versions.
	DLLEXPORT void STDCALL PackSetDefaultParams(PackDefaultParamStruct* dps) { //-V2009
		auto res = plugin_config.read_conf(dps, false);
		if (!res) { // Create default configuration if conf file is absent
			plugin_config.write_conf();
		}
	}

	// GetBackgroundFlags is called to determine whether a plugin supports background packing or unpacking.
	// BACKGROUND_UNPACK == 1 Calls to OpenArchive, ReadHeader(Ex), ProcessFile and CloseArchive are thread-safe 
#ifdef _WIN64
	DLLEXPORT int STDCALL GetBackgroundFlags(PackDefaultParamStruct* dps) {
		return BACKGROUND_UNPACK;
	}
#endif 
	DLLEXPORT int STDCALL CanYouHandleThisFile(char* FileName) { // BOOL == int 
		auto hArchFile = open_file_shared_read(FileName);
		if (hArchFile == file_open_error_v)
		{
			return 0;
		} else {
			close_file(hArchFile);
		}

		// Caching results here would complicate code too much as for now
		cpmSuperBlock super;
		cpmInode root;
		//! TODO: Read from config
		std::string format{FORMAT}; //  osb1sssd, osbexec1
		// struct cpmInode root;
		std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
		//! TODO: parse extension and use it as a possible driver name.
		bool use_uppercase = true;

		const char* errs = Device_open(&(super.dev), FileName, O_RDONLY,
			driver_name.empty() ? nullptr : driver_name.c_str());

		if (errs) // Pointer to error string 
		{
			plugin_config.log_print("\n\nError# Failed opening file: %s in CanYouHandleThisFile\n", errs);
			return 0;
		}
		int erri = cpmReadSuper(&super, &root,
			format.empty() ? nullptr : format.c_str(),
			use_uppercase);
		if (erri == -1)
		{
			plugin_config.log_print("\n\nError# Failed reading superblock of %s in CanYouHandleThisFile.", FileName);
			return 0;
		}
		cpmUmount(&super);

		return 1; 
	}

	DLLEXPORT int STDCALL DeleteFiles(char* PackedFile, char* DeleteList) {
		cpmSuperBlock super;
		cpmInode root;
		//! TODO: Read from config
		std::string format{FORMAT}; //  osb1sssd, osbexec1
		// struct cpmInode root;
		std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
		//! TODO: parse extension and use it as a possible driver name.
		bool use_uppercase = true;

		const char* errs = Device_open(&(super.dev), PackedFile, O_RDONLY,
			driver_name.empty() ? nullptr : driver_name.c_str());

		if (errs) // Pointer to error string 
		{
			plugin_config.log_print("\n\nError# Failed opening file: %s in DeleteFiles\n", errs);
			return E_EOPEN;
		}
		int erri = cpmReadSuper(&super, &root,
			format.empty() ? nullptr : format.c_str(),
			use_uppercase);
		if (erri == -1)
		{
			plugin_config.log_print("\n\nError# Failed reading superblock of %s in DeleteFiles.", PackedFile);
			return E_EOPEN;
		}

		std::vector<const char*> file_list;
		const char* cur_ptr = DeleteList;
		size_t sl = strlen(cur_ptr);
		while( true ){
			// Erase
			sl = strlen(cur_ptr);
			if (sl == 0)
				break;
			std::string ps{cur_ptr};
			auto user_pos = ps.find('\\');
			if (user_pos == 2) {
				ps.erase(2, 1);
			}
			else if (user_pos == std::string::npos) {
				ps = "00" + ps;
			}
			else {
				plugin_config.log_print("\n\nError# Wrong file name %s for archive %s in DeleteFiles.", 
					cur_ptr, PackedFile);
			}
			if (cpmUnlink(&root, ps.c_str()) == -1)
			{
				plugin_config.log_print("\n\nError# Failed deleting file %s in archive %s with error: %s.", 
					ps.c_str(), PackedFile, boo); // �����! ������� �������...
				return E_NOT_SUPPORTED;
			}
			cur_ptr += sl + 1;
		}
		cpmUmount(&super);
		return 0;
	}

	DLLEXPORT int STDCALL PackFiles(char* PackedFile, char* SubPath, char* SrcPath, char* AddList, int Flags) {
		// PK_PACK_MOVE_FILES         1 Delete original after packing
		// PK_PACK_SAVE_PATHS         2 Save path names of files
		// PK_PACK_ENCRYPT            4 Ask user for password, then encrypt file with that password
		cpmSuperBlock super;
		cpmInode root;
		//! TODO: Read from config
		std::string format{FORMAT}; //  osb1sssd, osbexec1
		// struct cpmInode root;
		std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
		//! TODO: parse extension and use it as a possible driver name.
		bool use_uppercase = true;

		std::string SubPathS{SubPath ? SubPath : ""};
		if (!SubPathS.empty() && SubPathS.size() != 2) {
			// Impossible path
			plugin_config.log_print("\n\nError# Impossible in-image path: %s for %s archive in PackFiles\n", 
				SubPath, PackedFile);
			return E_EOPEN;
		}

		std::string SrcPathS{SrcPath ? SrcPath : ""};

		const char* errs = Device_open(&(super.dev), PackedFile, O_RDONLY,
			driver_name.empty() ? nullptr : driver_name.c_str());

		if (errs) // Pointer to error string 
		{
			plugin_config.log_print("\n\nError# Failed opening file: %s in DeleteFiles\n", errs);
			return E_EOPEN;
		}
		int erri = cpmReadSuper(&super, &root,
			format.empty() ? nullptr : format.c_str(),
			use_uppercase);
		if (erri == -1)
		{
			plugin_config.log_print("\n\nError# Failed reading superblock of %s in DeleteFiles.", PackedFile);
			return E_EOPEN;
		}

		std::vector<const char*> file_list;
		const char* cur_ptr = AddList;
		size_t sl = strlen(cur_ptr);
		while (true) {
			// Erase
			sl = strlen(cur_ptr);
			if (sl == 0)
				break;
			std::string ps{cur_ptr};
			auto user_pos = ps.find('\\');
			if ( user_pos == ps.size() - 1 ) {
				cur_ptr += sl + 1;
				continue; // We do not creat folders by themself 
			}
			if (user_pos == 2 && SubPathS.empty() ) {
				ps.erase(2, 1);
			}
			else if (user_pos == std::string::npos) {
				if(SubPathS.empty())
					ps = "00" + ps;
				else 
					ps = SubPathS + ps;
			}
			else {
				plugin_config.log_print("\n\nError# Wrong file name %s for archive %s in DeleteFiles.",
					cur_ptr, PackedFile);
			}
			// TODO: Process text files?
			auto src_file_path = SrcPathS + cur_ptr; // cur_ptr -- original name
			auto filehdr = open_file_shared_read(src_file_path.c_str());
			if (filehdr == file_open_error_v)
			{
				plugin_config.log_print("\n\nError# Failed opening file %s", src_file_path.c_str());
				return E_EOPEN;
			}
			
			auto file_size = get_file_size(filehdr);
			
			char* buf = new char[ file_size+128 ];

			auto read_size = read_file(filehdr, buf, file_size);
			// TODO: check errors in read_file
			// TODO: check correctness 
			// The end of an ASCII file is denoted by a CTRL-Z character (1AH) 
			// or a real end-of-file returned by the CP/M read operation. 
			// CTRL-Z characters embedded within machine code files (for example, 
			// COM files) are ignored and the end-of-file condition returned 
			// by CP/M is used to terminate read operations.
			// http://www.gaby.de/cpm/manuals/archive/cpm22htm/ch5.htm
			if (file_size % 128 != 0) {
				auto cur_idx = file_size;
				buf[cur_idx++] = 0x1A;
				while (cur_idx % 128) {
					buf[cur_idx++] = 0x1A;
				}
			}

			struct cpmFile file;
			struct cpmInode ino;
			if (cpmCreat(&root, ps.c_str(), &ino, 0666) == -1)
			{
				plugin_config.log_print("\n\nError# Failed creating file %s in archive %s with error: %s.",
					ps.c_str(), PackedFile, boo);
			}

			cpmOpen(&ino, &file, O_WRONLY);

			auto written_size = cpmWrite(&file, buf, ((file_size + 127)/128)*128);

			if (cpmClose(&file) != 0)
			{
				plugin_config.log_print("\n\nError# Failed closing file %s in archive %s with error: %s.",
					ps.c_str(), PackedFile, boo);
			}

			delete[] buf;
			close_file(filehdr);
			

			if (false)
			{
				plugin_config.log_print("\n\nError# Failed deleting file %s in archive %s with error: %s.",
					ps.c_str(), PackedFile, boo); // �����! ������� �������...
				return E_NOT_SUPPORTED;
			}
			cur_ptr += sl + 1;
		}
		cpmUmount(&super);
		return 0;
	}

	DLLEXPORT int STDCALL GetPackerCaps() {
		return PK_CAPS_BY_CONTENT | PK_CAPS_SEARCHTEXT | PK_CAPS_DELETE | PK_CAPS_MODIFY | PK_CAPS_NEW | PK_CAPS_MULTIPLE;
		// PK_CAPS_DELETE // PK_CAPS_MODIFY //PK_CAPS_ENCRYPT // PK_CAPS_NEW 
	}
}

#if 0 // FLTK Dialogs:
Fl_Window* w = new Fl_Window(400, 300);
w->set_modal();
w->show();
while (w->shown()) Fl::wait();

https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent -- 
Changes the parent window of the specified child window.
HWND SetParent(
	[in]           HWND hWndChild,
	[in, optional] HWND hWndNewParent
);


FLTK windows as children of system windows: "replaces WS_POPUP with WS_CHILD flags"
https ://fltk.easysw.narkive.com/FJwMvdmO/windows-as-children-of-system-windows
HWND hWnd = (HWND)fl_xid(dlgWnd_);
SetWindowLong(hWnd, GWL_STYLE, (GetWindowLong(hWnd, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
SetParent(hWnd, parentWindow);

https://www.fltk.org/doc-1.3/osissues.html#osissues_win32
See "Handling Other WIN32 Messages"

Fl_Window* fl_find(HWND xid)
Returns the Fl_Window that corresponds to the given window handle, or NULL if not found.
This function uses a cache so it is slightly faster than iterating through the windows yourself.
#endif
