/*
* CP/M floppy disk images plugin for the Total Commander.
* Copyright (c) 2022-2026, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
*/
#pragma once 
#ifndef CPMIMG_PLUGIN_GUI_INC
#define CPMIMG_PLUGIN_GUI_INC

#ifndef GUI_PLUGIN_IMPORT
#define DLLEXPORTGUI __declspec(dllexport)
#else
#define DLLEXPORTGUI __declspec(dllimport)
#endif 

#ifdef _WIN32
#define STDCALL __stdcall
#else 
#define STDCALL 
#endif 

#include "cpmtools/config.h"
#include "cpmtools/cpmfs.h"
#include "cpmtools/getopt_.h"

#include "sysio_winapi.h"
#include "minimal_fixed_string.h"
#include "plugin_config.h"
#include "cpmimg_probe_client.h"

//! TODO: Provision for the Linux GUIs

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>

#include "cpmimg_plugin_gui_resources.h"


extern HINSTANCE g_GUI_dlg_hInstance;

inline std::uint64_t cpm_disk_expected_raw_size(
    const cpm_disk_descr_t& dsk) noexcept
{
    if (dsk.secLength <= 0 || dsk.sectrk <= 0 || dsk.tracks <= 0 ||
        dsk.offset < 0) {
        return 0;
    }

    return static_cast<std::uint64_t>(dsk.offset) +
        static_cast<std::uint64_t>(dsk.secLength) *
        static_cast<std::uint64_t>(dsk.sectrk) *
        static_cast<std::uint64_t>(dsk.tracks);
}

enum class format_dialog_action_t {
    cancel,
    mount_selected,
    probe_now
};

class img_type_sel_GUI_t {
private:
    HWND hDlg;
    HWND hComboFormat;
    HWND hEditSecLength;
    HWND hEditTracks;
    HWND hEditSectrk;
    HWND hEditBlksiz;
    HWND hEditMaxdir;
    HWND hEditDirblks;
    HWND hEditBoottrk;
    HWND hEditProbability;
    HWND hCheckSaveType;
    HWND hCheckSaveTypeCur;
    HWND hButtonProbeNow;
    HWND hCheckEnableProbing;
    HWND hCheckSaveProbing;

    HBRUSH hDialogBgBrush = nullptr;

    const std::vector<cpm_disk_descr_t>& possible_fmts;
    DSK_GEOMETRY img_geom;
    bool geometry_reliable = false;
    size_t image_payload_size = 0;
    bool ui_retry = false;
    minimal_fixed_string_t<33> image_type;
    bool show_probab = false;
    bool save_disk_type_res = false; 
    bool save_disk_type_for_cur_res = false;
    const cpm_safe_probe_report* safe_probe_report = nullptr;
    bool automatic_probing_initial = true;
    bool automatic_probing_res = true;
    bool save_probing_preference_res = false;
    format_dialog_action_t action_res = format_dialog_action_t::cancel;

    static HFONT hBoldFont;
    static HFONT hNormalFont;
    
    const cpm_safe_probe_candidate* probe_for(
        const char* format_name) const noexcept
    {
        return safe_probe_report
            ? safe_probe_report->find(format_name)
            : nullptr;
    }

    static const char* probe_state_text(
        cpm_safe_probe_state state) noexcept
    {
        switch (state) {
        case cpm_safe_probe_state::mounted:
            return "mount OK";
        case cpm_safe_probe_state::rejected:
            return "rejected";
        case cpm_safe_probe_state::timeout:
            return "timeout";
        case cpm_safe_probe_state::crashed:
            return "crashed";
        case cpm_safe_probe_state::helper_error:
        default:
            return "helper error";
        }
    }

    void update_info_fields(int idx) {
        if (idx < 0 || idx >= static_cast<int>(possible_fmts.size()))
            return;

        const auto& dsk = possible_fmts[idx];
        int match_score = 0;
        const auto expected_size = cpm_disk_expected_raw_size(dsk);
        const bool size_match =
            image_payload_size != 0 &&
            expected_size != 0 &&
            expected_size == static_cast<std::uint64_t>(image_payload_size);

        if (show_probab) {
            SendMessage(hEditSecLength, WM_SETFONT,
                (WPARAM)hNormalFont, TRUE);
            SendMessage(hEditTracks, WM_SETFONT,
                (WPARAM)hNormalFont, TRUE);
            SendMessage(hEditSectrk, WM_SETFONT,
                (WPARAM)hNormalFont, TRUE);

            if (geometry_reliable) {
                if (img_geom.dg_secsize == dsk.secLength) {
                    SendMessage(hEditSecLength, WM_SETFONT,
                        (WPARAM)hBoldFont, TRUE);
                    ++match_score;
                }

                const int geom_total_tracks =
                    img_geom.dg_cylinders * img_geom.dg_heads;
                if (geom_total_tracks == dsk.tracks) {
                    SendMessage(hEditTracks, WM_SETFONT,
                        (WPARAM)hBoldFont, TRUE);
                    ++match_score;
                }

                if (img_geom.dg_sectors == dsk.sectrk) {
                    SendMessage(hEditSectrk, WM_SETFONT,
                        (WPARAM)hBoldFont, TRUE);
                    ++match_score;
                }
            }
        }

        char buf[128];
        sprintf(buf, "%d", dsk.secLength); SetWindowText(hEditSecLength, buf);
        sprintf(buf, "%d", dsk.tracks); SetWindowText(hEditTracks, buf);
        sprintf(buf, "%d", dsk.sectrk); SetWindowText(hEditSectrk, buf);
        sprintf(buf, "%d", dsk.blksiz); SetWindowText(hEditBlksiz, buf);
        sprintf(buf, "%d", dsk.maxdir); SetWindowText(hEditMaxdir, buf);
        sprintf(buf, "%d", dsk.dirblks); SetWindowText(hEditDirblks, buf);
        sprintf(buf, "%d", dsk.boottrk); SetWindowText(hEditBoottrk, buf);

        if (show_probab) {
            const auto* probe =
                probe_for(dsk.fmt_name);
            if (probe) {
                SendMessage(
                    hEditProbability,
                    WM_SETFONT,
                    (WPARAM)(
                        probe->state ==
                            cpm_safe_probe_state::mounted
                            ? hBoldFont
                            : hNormalFont),
                    TRUE);

                SetWindowText(
                    GetDlgItem(
                        hDlg,
                        IDC_STATIC_PROBABILITY),
                    "Probe score:");

                char probe_buf[96] = {};
                sprintf(
                    probe_buf,
                    "%d/100 %s",
                    probe->score,
                    probe_state_text(probe->state));
                SetWindowText(
                    hEditProbability,
                    probe_buf);
                return;
            }

            SendMessage(hEditProbability, WM_SETFONT,
                (WPARAM)hBoldFont, TRUE);

                        if (!geometry_reliable) {
                SetWindowText(
                    GetDlgItem(hDlg, IDC_STATIC_PROBABILITY),
                    size_match ? "Geometry (size OK):" : "Geometry:"
                );
                SetWindowText(hEditProbability, "Unreliable");
                return;
            }

            SetWindowText(
                GetDlgItem(hDlg, IDC_STATIC_PROBABILITY),
                "Geometry match:"
            );

            if (match_score == 0 && size_match) {
                SetWindowText(hEditProbability, "Size match");
                return;
            }

            switch (match_score) {
            case 0:
                SetWindowText(hEditProbability, "No");
                break;
            case 1:
                SetWindowText(hEditProbability, "Low probability");
                break;
            case 2:
                SetWindowText(hEditProbability, "Could be");
                break;
            case 3:
                SetWindowText(hEditProbability, "Yes");
                break;
            default:
                SetWindowText(hEditProbability, "Unknown");
            }
        }
    }

    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
        img_type_sel_GUI_t* pThis = reinterpret_cast<img_type_sel_GUI_t*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));

        switch (message) {
        case WM_INITDIALOG:
        {
            pThis = reinterpret_cast<img_type_sel_GUI_t*>(lParam);
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pThis->hDlg = hDlg;

            pThis->hDialogBgBrush = CreateSolidBrush(RGB(240, 240, 255));

            return pThis->OnInitDialog();
        }

        case WM_COMMAND:
            if (pThis) {
                return pThis->OnCommand(LOWORD(wParam), HIWORD(wParam));
            }
            break;

        case WM_KEYDOWN:
            if (pThis && GetFocus() == pThis->hComboFormat) {
                return pThis->OnComboKeyDown(wParam);
            }
            break;

        case WM_CTLCOLORDLG:
            return (INT_PTR)GetStockObject(LTGRAY_BRUSH);
            // return (INT_PTR)(pThis->hDialogBgBrush);

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)GetStockObject(LTGRAY_BRUSH);
        }

        case WM_CLOSE:
            if (pThis) {
                pThis->action_res = format_dialog_action_t::cancel;
                pThis->ui_retry = false;
                if (pThis->hDialogBgBrush) {
                    DeleteObject(pThis->hDialogBgBrush);
                    pThis->hDialogBgBrush = nullptr;
                }
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }



        return FALSE;
    }

    BOOL OnInitDialog() {
        // Center the dialog on screen
        RECT rcDlg, rcDesktop;
        GetWindowRect(hDlg, &rcDlg);
        GetWindowRect(GetDesktopWindow(), &rcDesktop);

        int x = (rcDesktop.right - rcDesktop.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = (rcDesktop.bottom - rcDesktop.top - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        // Get control handles
        hComboFormat = GetDlgItem(hDlg, IDC_COMBO_FORMAT);
        hEditSecLength = GetDlgItem(hDlg, IDC_EDIT_SECLENGTH);
        hEditTracks = GetDlgItem(hDlg, IDC_EDIT_TRACKS);
        hEditSectrk = GetDlgItem(hDlg, IDC_EDIT_SECTRK);
        hEditBlksiz = GetDlgItem(hDlg, IDC_EDIT_BLKSIZ);
        hEditMaxdir = GetDlgItem(hDlg, IDC_EDIT_MAXDIR);
        hEditDirblks = GetDlgItem(hDlg, IDC_EDIT_DIRBLKS);
        hEditBoottrk = GetDlgItem(hDlg, IDC_EDIT_BOOTTRK);
        hEditProbability = GetDlgItem(hDlg, IDC_EDIT_PROBABILITY);
        hCheckSaveType = GetDlgItem(hDlg, IDC_CHECK_SAVE_TYPE);
        hCheckSaveTypeCur = GetDlgItem(hDlg, IDC_CHECK_SAVE_TYPE_CUR);
        hButtonProbeNow = GetDlgItem(hDlg, IDC_BUTTON_PROBE_NOW);
        hCheckEnableProbing = GetDlgItem(hDlg, IDC_CHECK_ENABLE_PROBING);
        hCheckSaveProbing = GetDlgItem(hDlg, IDC_CHECK_SAVE_PROBING);
        auto hStaticText = GetDlgItem(hDlg, IDC_STATIC_TITLE);

        if (safe_probe_report &&
            !safe_probe_report->message.empty()) {
            SetWindowText(
                hStaticText,
                safe_probe_report->message.c_str());
        }
        else if (show_probab) {
            SetWindowText(
                hStaticText,
                automatic_probing_initial
                    ? "Automatic probing is enabled."
                    : "Automatic probing is disabled.");
        }
        else {
            SetWindowText(
                hStaticText,
                "Configure CP/M image handling");
        }

        // Debug: Check if controls were found
        if (!hComboFormat) {
            MessageBox(hDlg, "ComboBox control not found!", "Error", MB_OK);
            return FALSE;
        }

        // Create fonts
        if (!hBoldFont) {
            LOGFONT lf;
            GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(LOGFONT), &lf);
            lf.lfWeight = FW_BOLD;
            lf.lfHeight = -16; // Approx. 12pt on 96 DPI
            hBoldFont = CreateFontIndirect(&lf);
            GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(LOGFONT), &lf);
            lf.lfHeight = -16; // Approx. 12pt on 96 DPI
			hNormalFont = CreateFontIndirect(&lf);
        }

        // Populate combo box
        for (const auto& dsk : possible_fmts) {
            ComboBox_AddString(hComboFormat, dsk.fmt_name);
        }
        ComboBox_SetCurSel(hComboFormat, 0);

        Button_SetCheck(
            hCheckEnableProbing,
            automatic_probing_initial
                ? BST_CHECKED
                : BST_UNCHECKED);
        Button_SetCheck(
            hCheckSaveProbing,
            BST_UNCHECKED);

        if (!show_probab) {
        // Set checkbox state
            Button_SetCheck(hCheckSaveType, BST_CHECKED);
			Button_SetCheck(hCheckSaveTypeCur, BST_CHECKED);

        // Hide probability field if not needed
            ShowWindow(hEditProbability, SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PROBABILITY), SW_HIDE);
            ShowWindow(hButtonProbeNow, SW_HIDE);
        }

        SendMessage(hStaticText, WM_SETFONT, (WPARAM)hBoldFont, TRUE);

        // Update info fields
        update_info_fields(0);

        return TRUE;
    }

    BOOL OnCommand(WORD wID, WORD wNotifyCode) {
        switch (wID) {
        case IDC_COMBO_FORMAT:
            if (wNotifyCode == CBN_SELCHANGE) {
                int idx = ComboBox_GetCurSel(hComboFormat);
                update_info_fields(idx);
            }
            break;

        case IDC_BUTTON_PROBE_NOW:
            automatic_probing_res =
                Button_GetCheck(
                    hCheckEnableProbing) ==
                BST_CHECKED;
            save_probing_preference_res =
                Button_GetCheck(
                    hCheckSaveProbing) ==
                BST_CHECKED;
            action_res =
                format_dialog_action_t::probe_now;
            ui_retry = false;
            EndDialog(
                hDlg,
                IDC_BUTTON_PROBE_NOW);
            return TRUE;

        case IDOK:
        {
            int idx = ComboBox_GetCurSel(hComboFormat);
            if (idx >= 0 && idx < static_cast<int>(possible_fmts.size())) {
                image_type = possible_fmts[idx].fmt_name;
                ui_retry = true;
                save_disk_type_for_cur_res = (Button_GetCheck(hCheckSaveTypeCur) == BST_CHECKED);
                save_disk_type_res = (Button_GetCheck(hCheckSaveType) == BST_CHECKED);
                automatic_probing_res =
                    Button_GetCheck(
                        hCheckEnableProbing) ==
                    BST_CHECKED;
                save_probing_preference_res =
                    Button_GetCheck(
                        hCheckSaveProbing) ==
                    BST_CHECKED;
                action_res =
                    format_dialog_action_t::
                        mount_selected;
                EndDialog(hDlg, IDOK);
            }
        }
        break;

        case IDCANCEL:
            action_res = format_dialog_action_t::cancel;
            ui_retry = false;
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return FALSE;
    }

    BOOL OnComboKeyDown(WPARAM wParam) {
        int current = ComboBox_GetCurSel(hComboFormat);
        int count = ComboBox_GetCount(hComboFormat);

        if (wParam == VK_UP && current > 0) {
            ComboBox_SetCurSel(hComboFormat, current - 1);
            update_info_fields(current - 1);
            return TRUE;
        }
        else if (wParam == VK_DOWN && current < count - 1) {
            ComboBox_SetCurSel(hComboFormat, current + 1);
            update_info_fields(current + 1);
            return TRUE;
        }

        return FALSE;
    }

public:

    static bool  TestResourceLoading() {
        HRSRC hResource = FindResource(g_GUI_dlg_hInstance, MAKEINTRESOURCE(IDD_FORMAT_SELECT), RT_DIALOG);

        if (!hResource) {
            DWORD error = GetLastError();
            char errorMsg[256];
            sprintf(errorMsg, "Dialog resource not found! Error: %lu", error);
            MessageBox(nullptr, errorMsg, "Error", MB_OK | MB_ICONERROR); // TODO: Check if dialogs are allowed
            return false;
        }
        return true; 
    }


    img_type_sel_GUI_t(const std::vector<cpm_disk_descr_t>& possible_fmts_in,
        const DSK_GEOMETRY& geom_in,
        bool show_probab_in,
        bool geometry_reliable_in = false,
        size_t image_payload_size_in = 0,
        const cpm_safe_probe_report* safe_probe_report_in = nullptr,
        bool automatic_probing_enabled_in = true) :
        possible_fmts(possible_fmts_in),
        img_geom(geom_in),
        geometry_reliable(geometry_reliable_in),
        image_payload_size(image_payload_size_in),
        show_probab(show_probab_in),
        safe_probe_report(safe_probe_report_in),
        automatic_probing_initial(automatic_probing_enabled_in),
        automatic_probing_res(automatic_probing_enabled_in),
        hDlg(nullptr)
    {
        auto can_load_res = TestResourceLoading();

        HWND hParent = GetActiveWindow(); // TODO: check. For Options dialog -- use parent sent by TCmd

        // Show modal dialog
        INT_PTR result = DialogBoxParam(g_GUI_dlg_hInstance,  // GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDD_FORMAT_SELECT),
            hParent, // TODO: set to parent sent by TCmd 
            DlgProc,
            reinterpret_cast<LPARAM>(this));

        // Debug: Check if dialog creation failed
        if (result == -1) {
            DWORD error = GetLastError();
            char errorMsg[256];
            sprintf(errorMsg, "DialogBoxParam failed with error: %lu", error);
            MessageBox(nullptr, errorMsg, "Error", MB_OK | MB_ICONERROR); // TODO: Check if dialogs are allowed
            ui_retry = false; // Should already be false
        }

    }

    bool attempt_new_read() {
        return ui_retry;
    }

    bool save_disk_type() const {
        return save_disk_type_res;
    }

    bool save_disk_type_for_cur() const {
        return save_disk_type_for_cur_res;
    }
    format_dialog_action_t action() const noexcept {
        return action_res;
    }

    bool automatic_probing_enabled() const noexcept {
        return automatic_probing_res;
    }

    bool save_probing_preference() const noexcept {
        return save_probing_preference_res;
    }

    auto get_image_type() const {
        return image_type;
    }
};



#endif