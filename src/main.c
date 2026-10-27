// Mem Reduct
// Copyright (c) 2011-2026 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"

STATIC_DATA config = {0};

ULONG limits_arr[13] = {0};
ULONG intervals_arr[13] = {0};

// exit code reported to the console caller, only meaningful for command line mode
INT cmdline_exit_code = ERROR_APP_INIT_FAILURE;

// this file is ordered by definition, and the tray timer outlives the window code that first calls the updater
_Success_ (return)
BOOLEAN _app_update_check (
	_In_ HWND hwnd,
	_In_ BOOLEAN is_silent
);

INT WINAPIV compare_numbers (
	_In_opt_ PVOID context,
	_In_ LPCVOID ptr1,
	_In_ LPCVOID ptr2
)
{
	ULONG val1, val2;

	val1 = *(const PULONG)ptr1;
	val2 = *(const PULONG)ptr2;

	if (val1 < val2)
		return -1;

	if (val1 > val2)
		return 1;

	return 0;
}

VOID _app_generate_array (
	_Out_ _Writable_elements_ (count) PULONG integers,
	_In_ ULONG_PTR count,
	_In_ ULONG value,
	_In_ ULONG max_value
)
{
	PR_HASHTABLE hashtable;
	ULONG_PTR enum_key = 0;
	ULONG_PTR hash_code = 0;
	ULONG index = 0;

	RtlSecureZeroMemory (integers, sizeof (ULONG) * count);

	hashtable = _r_obj_createhashtable (sizeof (BOOLEAN), 0x10, NULL);

	for (index = 1; index < 9; index++)
	{
		_r_obj_addhashtableitem (hashtable, index * 10, NULL);
	}

	for (index = (value > 2) ? (value - 2) : 1; index <= (value + 2); index++)
	{
		if (index >= 5)
			_r_obj_addhashtableitem (hashtable, index, NULL);
	}

	index = 0;

	while (_r_obj_enumhashtable (hashtable, NULL, &hash_code, &enum_key))
	{
		if (hash_code <= max_value)
			integers[index] = (ULONG)hash_code;

		if (++index >= count)
			break;
	}

	qsort_s (integers, count, sizeof (ULONG), &compare_numbers, NULL);

	_r_obj_dereference (hashtable);
}

VOID _app_generate_menu (
	_In_ HMENU hsubmenu,
	_In_ ULONG menu_idx,
	_Out_ _Writable_elements_ (count) PULONG integers,
	_In_ ULONG_PTR count,
	_In_opt_ LPCWSTR unit,
	_In_ ULONG value,
	_In_ ULONG max_value,
	_In_ BOOLEAN is_enabled
)
{
	WCHAR buffer[0x40];
	ULONG menu_value;
	ULONG menu_id;
	BOOLEAN is_checked = FALSE;

	_r_menu_setitemtext (hsubmenu, 0, TRUE, _r_locale_getstring (IDS_TRAY_DISABLE));

	_app_generate_array (integers, count, value, max_value);

	for (ULONG i = 0; i < count; i++)
	{
		menu_value = integers[i];

		if (!menu_value)
			continue;

		menu_id = (menu_idx + i);

		// a locale string is only ever passed as an argument, never as the format
		if (unit)
			_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%" TEXT (PR_ULONG) L" %s", menu_value, unit);
		else
			_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%" TEXT (PR_ULONG) L"%%", menu_value);

		_r_menu_additem (hsubmenu, menu_id, buffer);

		if (!_r_sys_iselevated ())
			_r_menu_enableitem (hsubmenu, menu_id, FALSE, FALSE);

		if (value == menu_value)
		{
			_r_menu_checkitem (hsubmenu, menu_id, 0, MF_BYCOMMAND, TRUE);

			is_checked = TRUE;
		}
	}

	if (!is_enabled || !is_checked)
		_r_menu_checkitem (hsubmenu, 0, 0, MF_BYPOSITION, TRUE);
}

ULONG _app_getlimitvalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"AutoreductValue", DEFAULT_AUTOREDUCT_VAL, NULL), 0, 100);
}

ULONG _app_getintervalvalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"AutoreductIntervalValue", DEFAULT_AUTOREDUCTINTERVAL_VAL, NULL), 1, 1440);
}

ULONG _app_getdangervalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"TrayLevelDanger", DEFAULT_DANGER_LEVEL, NULL), 0, 100);
}

ULONG _app_getwarningvalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"TrayLevelWarning", DEFAULT_WARNING_LEVEL, NULL), 0, 100);
}

FORCEINLINE BOOLEAN _app_update_isenabled ()
{
	return _r_config_getlong_ex (L"CheckUpdatesPeriod", UPDATE_CHECK_PERIOD, NULL) > 0;
}

VOID _app_update_enable (
	_In_ BOOLEAN is_enable
)
{
	_r_config_setlong_ex (L"CheckUpdatesPeriod", is_enable ? UPDATE_CHECK_PERIOD : 0, NULL);
}

BOOLEAN _app_update_isready ()
{
	LONG period;

	period = _r_config_getlong_ex (L"CheckUpdatesPeriod", UPDATE_CHECK_PERIOD, NULL);

	if (period <= 0)
		return FALSE;

	return (_r_unixtime_now () - _r_config_getlong64_ex (L"CheckUpdatesLast", 0, NULL)) > _r_calc_hours2seconds (period);
}

// spin buddies do not sync their text into the up-down position, so typed values must be read from the edit control
LONG _app_edit_getnumber (
	_In_ HWND hwnd,
	_In_ INT ctrl_id,
	_In_ LONG def_value,
	_In_ LONG min_value,
	_In_ LONG max_value
)
{
	BOOL is_success = FALSE;
	LONG value;

	value = GetDlgItemInt (hwnd, ctrl_id, &is_success, TRUE);

	if (!is_success)
		return def_value;

	if (value < min_value)
		value = min_value;

	if (value > max_value)
		value = max_value;

	return value;
}

VOID _app_fixmemorygroup (
	_Inout_ PR_MEMORY_OBJECT group
)
{
	// the page file can be switched off, and the sdk computes the percent by dividing through its zero size
	if (!group->total_bytes)
	{
		group->percent = 0;
		group->percent_f = 0.0;
	}
}

ULONG64 _app_getmemoryinfo (
	_Out_ PR_MEMORY_INFO mem_info
)
{
	_r_sys_getmemoryinfo (mem_info);

	_app_fixmemorygroup (&mem_info->physical_memory);
	_app_fixmemorygroup (&mem_info->page_file);
	_app_fixmemorygroup (&mem_info->system_cache);

	return mem_info->physical_memory.used_bytes;
}

FORCEINLINE LPCWSTR _app_getcleanupreason (
	_In_ CLEANUP_SOURCE_ENUM src
)
{
	switch (src)
	{
		case SOURCE_AUTO:
		{
			return L"Cleanup (Auto)";
		}

		case SOURCE_MANUAL:
		{
			return L"Cleanup (Manual)";
		}

		case SOURCE_HOTKEY:
		{
			return L"Cleanup (Hotkey)";
		}

		case SOURCE_CMDLINE:
		{
			return L"Cleanup (Command-line)";
		}

		case SOURCE_MANUAL_REGION:
		{
			return L"Cleanup (Manual)";
		}

		default:
		{
			return L"Unknown";
		}
	}
}

VOID _app_log_rotate ()
{
	PR_STRING log_path;
	LONG64 file_size = 0;
	HANDLE hfile;
	NTSTATUS status;

	log_path = _r_app_getlogpath ();

	status = _r_fs_getsize2 (&log_path->sr, NULL, &file_size);

	if (status != STATUS_SUCCESS || file_size <= LOG_MAXIMUM_SIZE)
		return;

	// routine only appends to the debug log, so empty it once in a while to stop a long run filling the disk
	status = _r_fs_createfile (
		&log_path->sr,
		FILE_OVERWRITE,
		GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SEQUENTIAL_ONLY,
		FALSE,
		NULL,
		&hfile
	);

	if (status == STATUS_SUCCESS)
		NtClose (hfile);
}

BOOLEAN _app_issafeformat (
	_In_ LPCWSTR format,
	_In_ ULONG count
)
{
	SIZE_T index = 0;
	ULONG found = 0;

	while (format[index] != UNICODE_NULL)
	{
		if (format[index] == L'%')
		{
			// a lone '%' or anything else in the conversion reads arguments we never pass
			if (format[index + 1] != L's')
				return FALSE;

			found += 1;

			index += 2;

			continue;
		}

		index += 1;
	}

	return (found == count);
}

BOOLEAN _app_config_invertboolean (
	_In_ LPCWSTR key_name,
	_In_ BOOLEAN def_value
)
{
	BOOLEAN value;

	value = !_r_config_getboolean_ex (key_name, def_value, NULL);

	_r_config_setboolean_ex (key_name, value, NULL);

	return value;
}

_Success_ (NT_SUCCESS (return))
NTSTATUS _app_flushvolumecache ()
{
	WCHAR volume_name[0x100];
	R_STRINGREF path_ref;
	ULONG_PTR length;
	HANDLE hvolume;
	HANDLE hfind;
	NTSTATUS last_status = STATUS_NOT_FOUND;
	NTSTATUS status;
	BOOLEAN is_flushed = FALSE;

	hfind = FindFirstVolumeW (volume_name, RTL_NUMBER_OF (volume_name));

	if (hfind == INVALID_HANDLE_VALUE)
		return STATUS_NOT_FOUND;

	do
	{
		// strip the trailing backslash, so we open the volume device and not its root directory
		length = _r_str_getlength (volume_name);

		if (length != 0 && volume_name[length - 1] == L'\\')
			volume_name[length - 1] = UNICODE_NULL;

		_r_obj_initializestringref (&path_ref, volume_name);

		status = _r_fs_createfile (&path_ref, FILE_OPEN, FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, 0, FALSE, NULL, &hvolume);

		if (!NT_SUCCESS (status))
		{
			last_status = status;

			_r_log (LOG_LEVEL_ERROR, NULL, L"_app_flushvolumecache", status, volume_name);

			continue;
		}

		status = _r_fs_flushfile (hvolume);

		NtClose (hvolume);

		if (!NT_SUCCESS (status))
		{
			last_status = status;

			_r_log (LOG_LEVEL_ERROR, NULL, L"_app_flushvolumecache", status, volume_name);
		}
		else
		{
			is_flushed = TRUE;
		}
	}
	while (FindNextVolumeW (hfind, volume_name, RTL_NUMBER_OF (volume_name)));

	FindVolumeClose (hfind);

	// read-only or locked volumes are normal on a running system, so flushing one volume is enough to succeed
	return is_flushed ? STATUS_SUCCESS : last_status;
}

VOID _app_cleanpending_set (
	_In_ ULONG mask
)
{
	// the elevated copy restarts with the same command line, and a click has no argument to carry the request across
	if (!mask)
		mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

	_r_config_setulong_ex (L"CleanPendingMask", mask & REDUCT_MASK_ALL, NULL);
}

VOID _app_memoryclean (
	_In_opt_ HWND hwnd,
	_In_ CLEANUP_SOURCE_ENUM src,
	_In_opt_ ULONG mask
)
{
	WCHAR buffer_cleaned_result[0x100] = {0}, buffer_region[0x100] = {0}, buffer_status[0x100] = {0}, buffer_result[0x100] = {0};
	MEMORY_COMBINE_INFORMATION_EX combine_info_ex = {0};
	SYSTEM_FILECACHE_INFORMATION sfci = {0};
	SYSTEM_MEMORY_LIST_COMMAND command;
	R_MEMORY_INFO mem_info;
	ULONG64 reduct_after, reduct_before;
	ULONG flags = NIIF_WARNING;
	LPCWSTR cleaned_message;
	BOOLEAN is_failed = FALSE;
	NTSTATUS status;

	if (!_r_config_getboolean_ex (L"IsNotificationsSound", TRUE, NULL))
		flags |= NIIF_NOSOUND;

	if (!_r_sys_iselevated ())
	{
		// the command line clean carries its own argument, every other source has to wait for the elevated copy
		if (src != SOURCE_CMDLINE)
			_app_cleanpending_set (mask);

		if (_r_app_runasadmin ())
		{
			// the elevated copy inherited the command line and does the cleaning, so report the handoff as done
			if (src == SOURCE_CMDLINE)
				cmdline_exit_code = ERROR_SUCCESS;

			if (hwnd)
				DestroyWindow (hwnd);
		}
		else
		{
			if (src != SOURCE_CMDLINE)
				_r_config_setulong_ex (L"CleanPendingMask", 0, NULL);

			if (src == SOURCE_CMDLINE)
			{
				_r_show_message (hwnd, MB_OK | MB_ICONSTOP, _r_app_getname (), _r_locale_getstring (IDS_STATUS_NOPRIVILEGES));

				cmdline_exit_code = 1;
			}
			else
			{
				if (hwnd)
					_r_tray_popup (hwnd, &GUID_TrayIcon, flags, _r_app_getname (), _r_locale_getstring (IDS_STATUS_NOPRIVILEGES));
			}

			if (!hwnd)
				_r_log (LOG_LEVEL_ERROR, NULL, L"_app_memoryclean", STATUS_PRIVILEGE_NOT_HELD, _r_locale_getstring (IDS_STATUS_NOPRIVILEGES));
		}

		return;
	}

	if (mask == 0)
		mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

	mask &= REDUCT_MASK_ALL;

	if (src == SOURCE_AUTO)
	{
		if (!_r_config_getboolean_ex (L"IsAllowStandbyListCleanup", FALSE, NULL))
			mask &= ~(REDUCT_MASK_FREEZES | REDUCT_STANDBYPRIORITY0LIST); // exclude the purges that stall disk activity from autoclean
	}

	if ((mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_WORKINGSET));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"\r\n");
	}

	if ((mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"\r\n");
	}

	if ((mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"\r\n");
	}

	if ((mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"*\r\n");
	}

	if ((mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"*\r\n");
	}

	if ((mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"\r\n");
	}

	if ((mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"\r\n");
	}

	if ((mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS)
	{
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"- ");
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS));
		_r_str_append (buffer_region, RTL_NUMBER_OF (buffer_region), L"\r\n");
	}

	StrTrimW (buffer_region, L"\r\n");

	static BOOLEAN is_running = FALSE;

	if (is_running)
		return;

	is_running = TRUE;

	// purging a single region stutters the system as much as cleaning everything, so ask before either
	if (src == SOURCE_MANUAL || src == SOURCE_MANUAL_REGION)
	{
		if (!_r_show_confirmmessage (hwnd, _r_locale_getstring (IDS_QUESTION), buffer_region, L"IsShowReductConfirmation", FALSE))
		{
			is_running = FALSE;

			return;
		}
	}

	SetCursor (LoadCursorW (NULL, IDC_WAIT));

	// difference (before)
	reduct_before = _app_getmemoryinfo (&mem_info);

	// Working set (vista+)
	if ((mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET)
	{
		command = MemoryEmptyWorkingSets;

		status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryEmptyWorkingSets");
			is_failed = TRUE;
		}
	}

	// System file cache
	if ((mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE)
	{
		sfci.MinimumWorkingSet = MAXSIZE_T;
		sfci.MaximumWorkingSet = MAXSIZE_T;

		status = NtSetSystemInformation (SystemFileCacheInformationEx, &sfci, sizeof (SYSTEM_FILECACHE_INFORMATION));

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"SystemFileCacheInformation");
			is_failed = TRUE;
		}
	}

	// Flush volume cache
	if ((mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE)
	{
		status = _app_flushvolumecache ();

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"_app_flushvolumecache", status, L"FlushFileBuffers");
			is_failed = TRUE;
		}
	}

	// Modified page list (vista+)
	if ((mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST)
	{
		command = MemoryFlushModifiedList;

		status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryFlushModifiedList");
			is_failed = TRUE;
		}
	}

	// Standby list (vista+)
	if ((mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST)
	{
		command = MemoryPurgeStandbyList;

		status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryPurgeStandbyList");
			is_failed = TRUE;
		}
	}

	// Standby priority-0 list (vista+)
	if ((mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST)
	{
		command = MemoryPurgeLowPriorityStandbyList;

		status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryPurgeLowPriorityStandbyList");
			is_failed = TRUE;
		}
	}

	// Flush registry cache
	if ((mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE)
	{
		status = NtSetSystemInformation (SystemRegistryReconciliationInformation, NULL, 0);

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"SystemRegistryReconciliationInformation");
			is_failed = TRUE;
		}
	}

	// Combine memory lists
	if ((mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS)
	{
		status = NtSetSystemInformation (SystemCombinePhysicalMemoryInformation, &combine_info_ex, sizeof (MEMORY_COMBINE_INFORMATION_EX));

		if (!NT_SUCCESS (status))
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"SystemCombinePhysicalMemoryInformation");
			is_failed = TRUE;
		}
	}

	SetCursor (LoadCursorW (NULL, IDC_ARROW));

	// difference (after)
	reduct_after = _app_getmemoryinfo (&mem_info);

	reduct_after = (reduct_after < reduct_before) ? (reduct_before - reduct_after) : 0;

	// time of last cleaning
	_r_config_setlong64_ex (L"StatisticLastReduct", _r_unixtime_now (), NULL);

	_r_format_bytesize64 (buffer_cleaned_result, RTL_NUMBER_OF (buffer_cleaned_result), reduct_after);

	// a region that failed is still listed among the cleaned ones, so the notification must not look like a plain success
	if (is_failed)
		flags = (flags & NIIF_NOSOUND) | NIIF_ERROR;

	cleaned_message = _r_locale_getstring (IDS_STATUS_CLEANED);

	// the message comes from a locale pack which the user can edit, so only print through it when it is well formed
	if (_app_issafeformat (cleaned_message, 1))
		_r_str_printf (buffer_status, RTL_NUMBER_OF (buffer_status), cleaned_message, buffer_cleaned_result);
	else
		_r_str_printf (buffer_status, RTL_NUMBER_OF (buffer_status), L"%s %s", cleaned_message, buffer_cleaned_result);
	_r_str_printf (buffer_result, RTL_NUMBER_OF (buffer_result), L"%s\r\n\r\n%s:\r\n%s", buffer_status, _r_locale_getstring (IDS_TITLE_3), buffer_region);

	if (src == SOURCE_CMDLINE)
	{
		if (_r_config_getboolean_ex (L"BalloonCleanResults", TRUE, NULL) && hwnd)
			_r_tray_popup (hwnd, &GUID_TrayIcon, flags, _r_app_getname (), buffer_result);
		else
			_r_show_message (hwnd, MB_OK | (is_failed ? MB_ICONERROR : MB_ICONINFORMATION), _r_app_getname (), buffer_result);
	}
	else
	{
		if (hwnd && _r_config_getboolean_ex (L"BalloonCleanResults", TRUE, NULL))
			_r_tray_popup (hwnd, &GUID_TrayIcon, flags, _r_app_getname (), buffer_result);
	}

	if (_r_config_getboolean_ex (L"LogCleanResults", FALSE, NULL))
	{
		_app_log_rotate ();

		_r_log_v (LOG_LEVEL_INFO, NULL, _app_getcleanupreason (src), 0, L"%s", buffer_cleaned_result);
	}

	if (src == SOURCE_CMDLINE)
		cmdline_exit_code = is_failed ? 1 : ERROR_SUCCESS;

	is_running = FALSE;
}

VOID _app_fontinit (
	_Out_ PLOGFONT logfont,
	_In_ LONG dpi_value
)
{
	RtlZeroMemory (logfont, sizeof (LOGFONT));

	_r_str_copy (logfont->lfFaceName, LF_FACESIZE, L"Segoe UI");

	logfont->lfHeight = _r_dc_fontsizetoheight (8, dpi_value);
	logfont->lfWeight = FW_NORMAL;

	_r_config_getfont_ex (L"TrayFont", logfont, dpi_value, NULL);

	logfont->lfCharSet = DEFAULT_CHARSET;
	logfont->lfQuality = _r_config_getboolean_ex (L"TrayUseAntialiasing", TRUE, NULL) ? CLEARTYPE_QUALITY : NONANTIALIASED_QUALITY;
}

VOID _app_drawbackground (
	_In_ HDC hdc,
	_In_ COLORREF bg_clr,
	_In_ COLORREF pen_clr,
	_In_ COLORREF brush_clr,
	_In_ LPCRECT rect,
	_In_ BOOLEAN is_round
)
{
	HGDIOBJ prev_brush, prev_pen;
	COLORREF prev_clr;

	prev_brush = SelectObject (hdc, GetStockObject (DC_BRUSH));
	prev_pen = SelectObject (hdc, GetStockObject (DC_PEN));

	prev_clr = SetBkColor (hdc, bg_clr);

	SetDCPenColor (hdc, pen_clr);
	SetDCBrushColor (hdc, brush_clr);

	_r_dc_fillrect (hdc, rect, bg_clr);

	if (is_round)
	{
		LONG radius_x;
		LONG radius_y;

		radius_x = (rect->right - rect->left) / 3;
		radius_y = (rect->bottom - rect->top) / 3;

		RoundRect (hdc, rect->left, rect->top, rect->right, rect->bottom, radius_x < 1 ? 1 : radius_x, radius_y < 1 ? 1 : radius_y);
	}
	else
	{
		Rectangle (hdc, rect->left, rect->top, rect->right, rect->bottom);
	}

	SelectObject (hdc, prev_brush);
	SelectObject (hdc, prev_pen);

	SetBkColor (hdc, prev_clr);
}

FORCEINLINE LONG _app_ui_scale (
	_In_ LONG value,
	_In_ LONG dpi_value
)
{
	return _r_calc_multipledivide (value, dpi_value, USER_DEFAULT_SCREEN_DPI);
}

typedef struct _APP_UI_PALETTE
{
	COLORREF window;
	COLORREF card;
	COLORREF text;
	COLORREF textmuted;
	COLORREF track;
	COLORREF accent;
	COLORREF accenthover;
	COLORREF accentpressed;
	COLORREF accenttext;
	COLORREF chipbg;
	COLORREF chiptext;
} APP_UI_PALETTE, *PAPP_UI_PALETTE;

VOID _app_ui_getpalette (
	_Out_ PAPP_UI_PALETTE palette,
	_In_ BOOLEAN is_dark,
	_In_ BOOLEAN is_elevated
)
{
	if (is_dark)
	{
		palette->window = RGB (0x20, 0x20, 0x20);
		palette->card = RGB (0x2B, 0x2B, 0x2B);
		palette->text = RGB (0xFF, 0xFF, 0xFF);
		palette->textmuted = RGB (0xBE, 0xBE, 0xBE);
		palette->track = RGB (0x3D, 0x3D, 0x3D);
		palette->accent = RGB (0x60, 0xCD, 0xFF);
		palette->accenthover = RGB (0x7A, 0xD4, 0xFF);
		palette->accentpressed = RGB (0x4F, 0xC3, 0xF7);
		palette->accenttext = RGB (0x00, 0x1E, 0x2B);
		palette->chipbg = is_elevated ? RGB (0x28, 0x38, 0x3F) : RGB (0x3E, 0x37, 0x2A);
		palette->chiptext = is_elevated ? RGB (0x60, 0xCD, 0xFF) : RGB (0xF5, 0xC2, 0x6B);
	}
	else
	{
		palette->window = RGB (0xF3, 0xF3, 0xF3);
		palette->card = RGB (0xFF, 0xFF, 0xFF);
		palette->text = RGB (0x1B, 0x1B, 0x1B);
		palette->textmuted = RGB (0x5C, 0x5C, 0x5C);
		palette->track = RGB (0xE4, 0xE4, 0xE4);
		palette->accent = RGB (0x0F, 0x6C, 0xBD);
		palette->accenthover = RGB (0x11, 0x5E, 0xA3);
		palette->accentpressed = RGB (0x0C, 0x4F, 0x8A);
		palette->accenttext = RGB (0xFF, 0xFF, 0xFF);
		palette->chipbg = is_elevated ? RGB (0xE8, 0xF2, 0xFB) : RGB (0xFF, 0xF3, 0xDE);
		palette->chiptext = is_elevated ? RGB (0x0F, 0x6C, 0xBD) : RGB (0x8A, 0x4B, 0x00);
	}
}

VOID _app_ui_fillroundrect (
	_In_ HDC hdc,
	_In_ LPCRECT rect,
	_In_ LONG radius,
	_In_ COLORREF clr
)
{
	HGDIOBJ prev_brush, prev_pen;

	prev_brush = SelectObject (hdc, GetStockObject (DC_BRUSH));
	prev_pen = SelectObject (hdc, GetStockObject (DC_PEN));

	SetDCBrushColor (hdc, clr);
	SetDCPenColor (hdc, clr);

	if (radius < 1)
		radius = 1;

	RoundRect (hdc, rect->left, rect->top, rect->right, rect->bottom, radius * 2, radius * 2);

	SelectObject (hdc, prev_brush);
	SelectObject (hdc, prev_pen);
}

VOID _app_ui_drawfocus (
	_In_ HDC hdc,
	_In_ LPCRECT rect,
	_In_ LONG radius,
	_In_ COLORREF clr
)
{
	HGDIOBJ prev_brush, prev_pen;

	prev_brush = SelectObject (hdc, GetStockObject (NULL_BRUSH));
	prev_pen = SelectObject (hdc, GetStockObject (DC_PEN));

	SetDCPenColor (hdc, clr);

	if (radius < 1)
		radius = 1;

	RoundRect (hdc, rect->left, rect->top, rect->right, rect->bottom, radius * 2, radius * 2);

	SelectObject (hdc, prev_brush);
	SelectObject (hdc, prev_pen);
}

VOID _app_ui_drawtext (
	_In_ HDC hdc,
	_In_ LPCWSTR text,
	_In_ LPCRECT rect,
	_In_ HFONT hfont,
	_In_ COLORREF clr,
	_In_ UINT flags
)
{
	HGDIOBJ hfont_prev;

	hfont_prev = SelectObject (hdc, hfont);

	SetTextColor (hdc, clr);
	SetBkMode (hdc, TRANSPARENT);

	DrawTextW (hdc, text, -1, (LPRECT)rect, flags | DT_NOPREFIX);

	SelectObject (hdc, hfont_prev);
}

LONG _app_ui_gettextwidth (
	_In_ HDC hdc,
	_In_ HFONT hfont,
	_In_ LPCWSTR text
)
{
	SIZE size = {0};
	HGDIOBJ hfont_prev;

	hfont_prev = SelectObject (hdc, hfont);

	GetTextExtentPoint32W (hdc, text, (INT)_r_str_getlength (text), &size);

	SelectObject (hdc, hfont_prev);

	return size.cx;
}

VOID _app_ui_drawbar (
	_In_ HDC hdc,
	_In_ LPCRECT rect,
	_In_ COLORREF clr_track,
	_In_ COLORREF clr_fill,
	_In_ ULONG percent
)
{
	RECT fill;
	LONG radius;
	LONG minimum;

	if (percent > 100)
		percent = 100;

	radius = (rect->bottom - rect->top) / 2;

	_app_ui_fillroundrect (hdc, rect, radius, clr_track);

	if (percent == 0)
		return;

	fill = *rect;

	fill.right = fill.left + (LONG)(((ULONG)(rect->right - rect->left) * percent) / 100);

	// a short bar must still show its rounded cap
	minimum = rect->left + (radius * 2);

	if (fill.right < minimum)
		fill.right = minimum;

	if (fill.right > rect->right)
		fill.right = rect->right;

	_app_ui_fillroundrect (hdc, &fill, radius, clr_fill);
}

COLORREF _app_ui_getlevelcolor (
	_In_ PAPP_UI_PALETTE palette,
	_In_ ULONG percent
)
{
	if (percent >= _app_getdangervalue ())
		return _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL);

	if (percent >= _app_getwarningvalue ())
		return _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL);

	return palette->accent;
}

VOID _app_ui_drawshield (
	_In_ HDC hdc,
	_In_ LONG x,
	_In_ LONG y,
	_In_ LONG height,
	_In_ COLORREF clr
)
{
	POINT points[6];
	HGDIOBJ prev_brush, prev_pen;
	LONG width;

	width = (height * 5) / 6;

	points[0].x = x + (width / 2);
	points[0].y = y;

	points[1].x = x + width;
	points[1].y = y + ((height * 2) / 10);

	points[2].x = x + width;
	points[2].y = y + ((height * 6) / 10);

	points[3].x = x + (width / 2);
	points[3].y = y + height;

	points[4].x = x;
	points[4].y = y + ((height * 6) / 10);

	points[5].x = x;
	points[5].y = y + ((height * 2) / 10);

	prev_brush = SelectObject (hdc, GetStockObject (DC_BRUSH));
	prev_pen = SelectObject (hdc, GetStockObject (DC_PEN));

	SetDCBrushColor (hdc, clr);
	SetDCPenColor (hdc, clr);

	Polygon (hdc, points, RTL_NUMBER_OF (points));

	SelectObject (hdc, prev_brush);
	SelectObject (hdc, prev_pen);
}

VOID _app_ui_drawchevron (
	_In_ HDC hdc,
	_In_ LONG center_x,
	_In_ LONG center_y,
	_In_ LONG size,
	_In_ LONG stroke,
	_In_ COLORREF clr
)
{
	POINT points[3];
	HPEN hpen, hpen_prev;

	points[0].x = center_x - size;
	points[0].y = center_y - (size / 2);

	points[1].x = center_x;
	points[1].y = center_y + (size / 2);

	points[2].x = center_x + size;
	points[2].y = center_y - (size / 2);

	hpen = CreatePen (PS_SOLID, stroke, clr);

	if (!hpen)
		return;

	hpen_prev = SelectObject (hdc, hpen);

	Polyline (hdc, points, RTL_NUMBER_OF (points));

	SelectObject (hdc, hpen_prev);

	DeleteObject (hpen);
}

VOID _app_ui_drawdots (
	_In_ HDC hdc,
	_In_ LONG center_x,
	_In_ LONG center_y,
	_In_ LONG radius,
	_In_ LONG gap,
	_In_ COLORREF clr
)
{
	HGDIOBJ prev_brush, prev_pen;
	LONG x;

	prev_brush = SelectObject (hdc, GetStockObject (DC_BRUSH));
	prev_pen = SelectObject (hdc, GetStockObject (NULL_PEN));

	SetDCBrushColor (hdc, clr);

	for (INT i = -1; i <= 1; i++)
	{
		x = center_x + (i * gap);

		Ellipse (hdc, x - radius, center_y - radius, x + radius, center_y + radius);
	}

	SelectObject (hdc, prev_brush);
	SelectObject (hdc, prev_pen);
}

HFONT _app_ui_fontcreate (
	_In_ LONG size,
	_In_ LONG weight,
	_In_ LONG dpi_value
)
{
	LOGFONT logfont = {0};

	_r_str_copy (logfont.lfFaceName, LF_FACESIZE, L"Segoe UI");

	logfont.lfHeight = _r_dc_fontsizetoheight (size, dpi_value);
	logfont.lfWeight = weight;
	logfont.lfCharSet = DEFAULT_CHARSET;
	logfont.lfQuality = CLEARTYPE_QUALITY;

	return CreateFontIndirectW (&logfont);
}

VOID _app_ui_fontsfree ()
{
	SAFE_DELETE_OBJECT (config.hfont_ui_hero);
	SAFE_DELETE_OBJECT (config.hfont_ui_title);
	SAFE_DELETE_OBJECT (config.hfont_ui_body);
	SAFE_DELETE_OBJECT (config.hfont_ui_bodybold);
	SAFE_DELETE_OBJECT (config.hfont_ui_small);
	SAFE_DELETE_OBJECT (config.hfont_ui_smallbold);
}

VOID _app_ui_fontscreate (
	_In_ LONG dpi_value
)
{
	_app_ui_fontsfree ();

	config.hfont_ui_hero = _app_ui_fontcreate (30, FW_SEMIBOLD, dpi_value);
	config.hfont_ui_title = _app_ui_fontcreate (13, FW_SEMIBOLD, dpi_value);
	config.hfont_ui_body = _app_ui_fontcreate (10, FW_NORMAL, dpi_value);
	config.hfont_ui_bodybold = _app_ui_fontcreate (10, FW_SEMIBOLD, dpi_value);
	config.hfont_ui_small = _app_ui_fontcreate (9, FW_NORMAL, dpi_value);
	config.hfont_ui_smallbold = _app_ui_fontcreate (9, FW_SEMIBOLD, dpi_value);
}

VOID _app_ui_localize (
	_In_ HWND hwnd
)
{
	// the labels are drawn by the owner draw code, the window text only feeds screen readers
	_r_ctrl_setstring (hwnd, IDC_CLEAN, _r_locale_getstring (IDS_CLEAN));
	_r_ctrl_setstring (hwnd, IDC_CLEANMENU, _r_locale_getstring (IDS_CLEAN));
	_r_ctrl_setstring (hwnd, IDC_MAINMENU, _r_locale_getstring (IDS_UI_MENU));
	_r_ctrl_setstring (hwnd, IDC_SETTINGS, _r_locale_getstring (IDS_SETTINGS));
}

BOOLEAN _app_ui_updatemetrics (
	_In_opt_ PR_MEMORY_INFO info
)
{
	R_MEMORY_INFO mem_info_local;
	PR_MEMORY_INFO mem_info = info;
	PR_MEMORY_OBJECT object;
	WCHAR hero_percent[0x08];
	WCHAR hero_used[0x20];
	WCHAR hero_free[0x20];
	WCHAR metric_percent[0x02][0x08];
	WCHAR metric_meta[0x02][0x40];
	WCHAR buffer[0x40];
	WCHAR buffer_line[0x40];
	ULONG percent[3];

	if (!mem_info)
	{
		_app_getmemoryinfo (&mem_info_local);

		mem_info = &mem_info_local;
	}

	percent[0] = mem_info->physical_memory.percent;
	percent[1] = mem_info->page_file.percent;
	percent[2] = mem_info->system_cache.percent;

	_r_str_printf (hero_percent, RTL_NUMBER_OF (hero_percent), L"%" TEXT (PR_ULONG) L"%%", percent[0]);

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info->physical_memory.used_bytes);
	_r_str_printf (hero_used, RTL_NUMBER_OF (hero_used), L"%s %s", buffer, _r_locale_getstring (IDS_UI_INUSE));

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info->physical_memory.free_bytes);
	_r_str_printf (hero_free, RTL_NUMBER_OF (hero_free), L"%s %s", buffer, _r_locale_getstring (IDS_UI_FREE));

	for (INT i = 0; i < 2; i++)
	{
		object = (i == 0) ? &mem_info->page_file : &mem_info->system_cache;

		_r_str_printf (metric_percent[i], RTL_NUMBER_OF (metric_percent[i]), L"%" TEXT (PR_ULONG) L"%%", percent[i + 1]);

		_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), object->used_bytes);
		_r_format_bytesize64 (buffer_line, RTL_NUMBER_OF (buffer_line), object->total_bytes);

		_r_str_printf (metric_meta[i], RTL_NUMBER_OF (metric_meta[i]), L"%s / %s", buffer, buffer_line);
	}

	// the timer runs every second: the strings are still formatted every time, but a repaint is only requested
	// when one of them changed - an idle machine would otherwise flicker for a value that is not even different
	if (config.ui_hasdata &&
		_r_str_compare (hero_percent, config.ui_hero_percent, FALSE) == 0 &&
		_r_str_compare (hero_used, config.ui_hero_used, FALSE) == 0 &&
		_r_str_compare (hero_free, config.ui_hero_free, FALSE) == 0 &&
		_r_str_compare (metric_percent[0], config.ui_metric_percent[0], FALSE) == 0 &&
		_r_str_compare (metric_percent[1], config.ui_metric_percent[1], FALSE) == 0 &&
		_r_str_compare (metric_meta[0], config.ui_metric_meta[0], FALSE) == 0 &&
		_r_str_compare (metric_meta[1], config.ui_metric_meta[1], FALSE) == 0)
	{
		return FALSE;
	}

	config.ui_hasdata = TRUE;

	config.ui_percent[0] = percent[0];
	config.ui_percent[1] = percent[1];
	config.ui_percent[2] = percent[2];

	_r_str_copy (config.ui_hero_percent, RTL_NUMBER_OF (config.ui_hero_percent), hero_percent);
	_r_str_copy (config.ui_hero_used, RTL_NUMBER_OF (config.ui_hero_used), hero_used);
	_r_str_copy (config.ui_hero_free, RTL_NUMBER_OF (config.ui_hero_free), hero_free);

	_r_str_copy (config.ui_metric_percent[0], RTL_NUMBER_OF (config.ui_metric_percent[0]), metric_percent[0]);
	_r_str_copy (config.ui_metric_percent[1], RTL_NUMBER_OF (config.ui_metric_percent[1]), metric_percent[1]);
	_r_str_copy (config.ui_metric_meta[0], RTL_NUMBER_OF (config.ui_metric_meta[0]), metric_meta[0]);
	_r_str_copy (config.ui_metric_meta[1], RTL_NUMBER_OF (config.ui_metric_meta[1]), metric_meta[1]);

	return TRUE;
}

VOID _app_ui_invalidate (
	_In_ HWND hwnd
)
{
	INT controls[] = {
		IDC_CLEAN,
		IDC_CLEANMENU,
		IDC_MAINMENU,
		IDC_SETTINGS,
	};

	HWND hctrl;

	InvalidateRect (hwnd, NULL, FALSE);

	for (ULONG_PTR i = 0; i < RTL_NUMBER_OF (controls); i++)
	{
		hctrl = GetDlgItem (hwnd, controls[i]);

		if (hctrl)
			InvalidateRect (hctrl, NULL, FALSE);
	}
}

VOID _app_ui_setsize (
	_In_ HWND hwnd,
	_In_ LONG dpi_value
)
{
	RECT rect = {0};

	rect.right = _app_ui_scale (UI_WIDTH, dpi_value);
	rect.bottom = _app_ui_scale (UI_HEIGHT, dpi_value);

	// the dialog template is sized in dialog units, the client area must be pinned in pixels
	_r_dc_adjustwindowrect (
		&rect,
		WS_POPUPWINDOW | WS_MINIMIZEBOX | WS_CAPTION | WS_CLIPCHILDREN | DS_MODALFRAME,
		WS_EX_DLGMODALFRAME,
		dpi_value,
		FALSE
	);

	SetWindowPos (hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

VOID _app_ui_layoutcontrols (
	_In_ HWND hwnd,
	_In_ LONG dpi_value
)
{
	HRGN hrgn;
	HWND hctrl;
	LONG pad;
	LONG width;
	LONG actions_top;
	LONG actions_height;
	LONG split_width;
	LONG radius;
	LONG x, y, w, h;

	pad = _app_ui_scale (UI_PAD, dpi_value);
	width = _app_ui_scale (UI_WIDTH, dpi_value);
	actions_top = _app_ui_scale (UI_ACTIONS_TOP, dpi_value);
	actions_height = _app_ui_scale (UI_ACTIONS_HEIGHT, dpi_value);
	split_width = _app_ui_scale (UI_SPLIT_WIDTH, dpi_value);
	radius = _app_ui_scale (UI_CONTROL_RADIUS, dpi_value);

	// the two halves share one rounded outline: the main half runs under the caret, so only its outer corners show
	hctrl = GetDlgItem (hwnd, IDC_CLEAN);

	if (hctrl)
	{
		w = width - (pad * 2) - split_width;

		MoveWindow (hctrl, pad, actions_top, w + radius, actions_height, TRUE);

		hrgn = CreateRoundRectRgn (0, 0, w + radius + 1, actions_height + 1, radius * 2, radius * 2);

		if (hrgn)
			SetWindowRgn (hctrl, hrgn, TRUE);
	}

	hctrl = GetDlgItem (hwnd, IDC_CLEANMENU);

	if (hctrl)
	{
		x = width - pad - split_width;

		MoveWindow (hctrl, x, actions_top, split_width, actions_height, TRUE);

		hrgn = CreateRoundRectRgn (-radius, 0, split_width + 1, actions_height + 1, radius * 2, radius * 2);

		if (hrgn)
			SetWindowRgn (hctrl, hrgn, TRUE);
	}

	hctrl = GetDlgItem (hwnd, IDC_MAINMENU);

	if (hctrl)
	{
		w = _app_ui_scale (UI_OVERFLOW_SIZE, dpi_value);
		h = w;

		y = _app_ui_scale (UI_HEADER_TOP, dpi_value) + ((_app_ui_scale (UI_HEADER_HEIGHT, dpi_value) - h) / 2);

		MoveWindow (hctrl, width - pad - w, y, w, h, TRUE);
	}

	hctrl = GetDlgItem (hwnd, IDC_SETTINGS);

	if (hctrl)
	{
		w = _app_ui_scale (80, dpi_value);
		h = _app_ui_scale (UI_FOOTER_HEIGHT, dpi_value);

		MoveWindow (hctrl, width - pad - w, _app_ui_scale (UI_FOOTER_TOP, dpi_value), w, h, TRUE);
	}
}

VOID _app_ui_paintheader (
	_In_ HDC hdc,
	_In_ LONG dpi_value,
	_In_ PAPP_UI_PALETTE palette
)
{
	RECT rect;
	RECT chip;
	LONG pad;
	LONG header_top;
	LONG header_height;
	LONG chip_height;
	LONG chip_padding;
	LONG text_width;
	LPCWSTR text;

	pad = _app_ui_scale (UI_PAD, dpi_value);
	header_top = _app_ui_scale (UI_HEADER_TOP, dpi_value);
	header_height = _app_ui_scale (UI_HEADER_HEIGHT, dpi_value);
	chip_height = _app_ui_scale (20, dpi_value);
	chip_padding = _app_ui_scale (10, dpi_value);

	rect.left = pad;
	rect.top = header_top;
	rect.right = _app_ui_scale (UI_WIDTH, dpi_value) - pad - _app_ui_scale (UI_OVERFLOW_SIZE + 8, dpi_value);
	rect.bottom = header_top + header_height;

	text = _r_locale_getstring (IDS_GROUP_1);

	_app_ui_drawtext (hdc, text, &rect, config.hfont_ui_title, palette->text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	text_width = _app_ui_gettextwidth (hdc, config.hfont_ui_title, text);

	text = _r_locale_getstring (_r_sys_iselevated () ? IDS_UI_ADMIN : IDS_UI_GUEST);

	chip.left = rect.left + text_width + _app_ui_scale (8, dpi_value);
	chip.top = header_top + ((header_height - chip_height) / 2);
	chip.right = chip.left + _app_ui_gettextwidth (hdc, config.hfont_ui_smallbold, text) + (chip_padding * 2);
	chip.bottom = chip.top + chip_height;

	if (chip.right > rect.right)
		chip.right = rect.right;

	_app_ui_fillroundrect (hdc, &chip, chip_height / 2, palette->chipbg);

	_app_ui_drawtext (hdc, text, &chip, config.hfont_ui_smallbold, palette->chiptext, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

VOID _app_ui_paintcard (
	_In_ HDC hdc,
	_In_ LONG dpi_value,
	_In_ PAPP_UI_PALETTE palette
)
{
	RECT card;
	RECT rect;
	RECT bar;
	RECT separator;
	LONG pad;
	LONG inner;
	LONG width;
	LONG radius;
	LONG bar_height;
	LONG row_height;
	LONG row_top;
	LONG thickness;

	pad = _app_ui_scale (UI_PAD, dpi_value);
	width = _app_ui_scale (UI_WIDTH, dpi_value);
	inner = _app_ui_scale (16, dpi_value);
	radius = _app_ui_scale (UI_CARD_RADIUS, dpi_value);
	bar_height = _app_ui_scale (UI_BAR_HEIGHT, dpi_value);
	row_height = _app_ui_scale (UI_ROW_HEIGHT, dpi_value);

	card.left = pad;
	card.top = _app_ui_scale (UI_CARD_TOP, dpi_value);
	card.right = width - pad;
	card.bottom = _app_ui_scale (UI_CARD_BOTTOM, dpi_value);

	_app_ui_fillroundrect (hdc, &card, radius, palette->card);

	// hero
	rect.left = card.left + inner;
	rect.top = card.top + _app_ui_scale (10, dpi_value);
	rect.right = card.right - inner;
	rect.bottom = rect.top + _app_ui_scale (46, dpi_value);

	_app_ui_drawtext (hdc, config.ui_hero_percent, &rect, config.hfont_ui_hero, palette->text, DT_LEFT | DT_TOP | DT_SINGLELINE);

	// used and free share the line under the hero
	rect.top = rect.bottom;
	rect.bottom = rect.top + _app_ui_scale (18, dpi_value);

	_app_ui_drawtext (hdc, config.ui_hero_used, &rect, config.hfont_ui_body, palette->textmuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	_app_ui_drawtext (hdc, config.ui_hero_free, &rect, config.hfont_ui_body, palette->textmuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

	// separator
	separator.left = card.left + inner;
	separator.right = card.right - inner;
	separator.top = rect.bottom + _app_ui_scale (10, dpi_value);

	thickness = _app_ui_scale (1, dpi_value);

	separator.bottom = separator.top + (thickness < 1 ? 1 : thickness);

	_r_dc_fillrect (hdc, &separator, palette->track);

	for (INT i = 0; i < 2; i++)
	{
		row_top = separator.bottom + _app_ui_scale (8, dpi_value) + (i * row_height);

		rect.left = card.left + inner;
		rect.top = row_top;
		rect.right = card.right - inner;
		rect.bottom = row_top + _app_ui_scale (16, dpi_value);

		_app_ui_drawtext (hdc, _r_locale_getstring (IDS_GROUP_2 + i), &rect, config.hfont_ui_body, palette->text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		_app_ui_drawtext (hdc, config.ui_metric_percent[i], &rect, config.hfont_ui_bodybold, palette->text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

		bar.left = rect.left;
		bar.right = rect.right;
		bar.top = rect.bottom + _app_ui_scale (6, dpi_value);
		bar.bottom = bar.top + bar_height;

		_app_ui_drawbar (hdc, &bar, palette->track, _app_ui_getlevelcolor (palette, config.ui_percent[i + 1]), config.ui_percent[i + 1]);

		rect.top = bar.bottom + _app_ui_scale (4, dpi_value);
		rect.bottom = rect.top + _app_ui_scale (14, dpi_value);

		_app_ui_drawtext (hdc, config.ui_metric_meta[i], &rect, config.hfont_ui_small, palette->textmuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

VOID _app_ui_paintfooter (
	_In_ HDC hdc,
	_In_ LONG dpi_value,
	_In_ PAPP_UI_PALETTE palette
)
{
	RECT rect;
	LONG pad;

	pad = _app_ui_scale (UI_PAD, dpi_value);

	rect.left = pad;
	rect.top = _app_ui_scale (UI_FOOTER_TOP, dpi_value);
	rect.right = _app_ui_scale (UI_WIDTH, dpi_value) - pad - _app_ui_scale (88, dpi_value);
	rect.bottom = rect.top + _app_ui_scale (UI_FOOTER_HEIGHT, dpi_value);

	_app_ui_drawtext (
		hdc,
		_r_locale_getstring (_r_sys_iselevated () ? IDS_UI_ADMINNOTE : IDS_UI_GUESTNOTE),
		&rect,
		config.hfont_ui_small,
		palette->textmuted,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
	);
}

VOID _app_ui_paint (
	_In_ HWND hwnd,
	_In_ HDC hdc_window
)
{
	APP_UI_PALETTE palette;
	RECT client;
	HDC hdc;
	HBITMAP hbitmap;
	HGDIOBJ hbitmap_prev;
	LONG dpi_value;

	if (!GetClientRect (hwnd, &client))
		return;

	hdc = CreateCompatibleDC (hdc_window);

	if (!hdc)
		return;

	hbitmap = CreateCompatibleBitmap (hdc_window, client.right, client.bottom);

	if (!hbitmap)
	{
		DeleteDC (hdc);

		return;
	}

	hbitmap_prev = SelectObject (hdc, hbitmap);

	dpi_value = _r_dc_getwindowdpi (hwnd);

	_app_ui_getpalette (&palette, _r_theme_isenabled (), _r_sys_iselevated ());

	SetBkMode (hdc, TRANSPARENT);

	_r_dc_fillrect (hdc, &client, palette.window);

	_app_ui_paintheader (hdc, dpi_value, &palette);
	_app_ui_paintcard (hdc, dpi_value, &palette);
	_app_ui_paintfooter (hdc, dpi_value, &palette);

	BitBlt (hdc_window, 0, 0, client.right, client.bottom, hdc, 0, 0, SRCCOPY);

	SelectObject (hdc, hbitmap_prev);

	DeleteObject (hbitmap);
	DeleteDC (hdc);
}

VOID _app_ui_drawbutton (
	_In_ LPDRAWITEMSTRUCT dis
)
{
	APP_UI_PALETTE palette;
	RECT rect;
	RECT focus;
	RECT textrect;
	RECT divider;
	LONG dpi_value;
	LONG radius;
	LONG width;
	LONG height;
	LONG center_x;
	LONG center_y;
	LONG text_width;
	LONG shield_width;
	LONG shield_height;
	LONG left;
	UINT flags;
	BOOLEAN is_elevated;
	BOOLEAN is_hover;
	BOOLEAN is_pressed;
	LPCWSTR text;

	dpi_value = _r_dc_getwindowdpi (dis->hwndItem);

	is_elevated = _r_sys_iselevated ();
	is_hover = (config.hwnd_ui_hover == dis->hwndItem);
	is_pressed = !!(dis->itemState & ODS_SELECTED);

	_app_ui_getpalette (&palette, _r_theme_isenabled (), is_elevated);

	rect = dis->rcItem;

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	radius = _app_ui_scale (UI_CONTROL_RADIUS, dpi_value);

	center_x = (rect.left + rect.right) / 2;
	center_y = (rect.top + rect.bottom) / 2;

	// the child window erases nothing, every pixel of the region is painted here
	_r_dc_fillrect (dis->hDC, &rect, palette.window);

	switch (dis->CtlID)
	{
		case IDC_CLEAN:
		case IDC_CLEANMENU:
		{
			COLORREF clr;
			RECT shape;

			clr = is_pressed ? palette.accentpressed : (is_hover ? palette.accenthover : palette.accent);

			shape = rect;

			// the caret covers the rounded right end of the pair, so its own left side has to stay square
			if (dis->CtlID == IDC_CLEANMENU)
				shape.left -= radius;

			_app_ui_fillroundrect (dis->hDC, &shape, radius, clr);

			if (dis->CtlID == IDC_CLEAN)
			{
				// the last radius pixels run under the caret, so the divider and the label stay left of them
				divider.left = rect.right - radius - _app_ui_scale (1, dpi_value);
				divider.right = rect.right - radius;
				divider.top = rect.top + (height / 4);
				divider.bottom = rect.bottom - (height / 4);

				_r_dc_fillrect (dis->hDC, &divider, palette.accentpressed);

				text = _r_locale_getstring (IDS_CLEAN);

				text_width = _app_ui_gettextwidth (dis->hDC, config.hfont_ui_bodybold, text);
				shield_height = is_elevated ? 0 : _app_ui_scale (14, dpi_value);
				shield_width = is_elevated ? 0 : ((shield_height * 5) / 6);

				left = rect.left + (((width - radius) - (text_width + shield_width + (shield_width ? _app_ui_scale (6, dpi_value) : 0))) / 2);

				if (!is_elevated)
				{
					_app_ui_drawshield (dis->hDC, left, rect.top + ((height - shield_height) / 2), shield_height, palette.accenttext);

					left += shield_width + _app_ui_scale (6, dpi_value);
				}

				textrect.left = left;
				textrect.top = rect.top;
				textrect.right = rect.right - radius;
				textrect.bottom = rect.bottom;

				_app_ui_drawtext (dis->hDC, text, &textrect, config.hfont_ui_bodybold, palette.accenttext, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}
			else
			{
				_app_ui_drawchevron (dis->hDC, center_x, center_y, _app_ui_scale (5, dpi_value), _app_ui_scale (2, dpi_value), palette.accenttext);
			}

			if (GetFocus () == dis->hwndItem)
			{
				focus = rect;

				if (dis->CtlID == IDC_CLEAN)
					focus.right -= radius;

				InsetRect (&focus, _app_ui_scale (3, dpi_value), _app_ui_scale (3, dpi_value));

				_app_ui_drawfocus (dis->hDC, &focus, radius - _app_ui_scale (2, dpi_value), palette.accenttext);
			}

			break;
		}

		case IDC_MAINMENU:
		{
			if (is_hover || is_pressed)
				_app_ui_fillroundrect (dis->hDC, &rect, radius, palette.track);

			_app_ui_drawdots (dis->hDC, center_x, center_y, _app_ui_scale (2, dpi_value), _app_ui_scale (7, dpi_value), palette.text);

			if (GetFocus () == dis->hwndItem)
			{
				focus = rect;

				InsetRect (&focus, _app_ui_scale (2, dpi_value), _app_ui_scale (2, dpi_value));

				_app_ui_drawfocus (dis->hDC, &focus, radius, palette.text);
			}

			break;
		}

		case IDC_SETTINGS:
		{
			flags = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;

			if (is_hover)
				flags |= DT_UNDERLINE;

			_app_ui_drawtext (
				dis->hDC,
				_r_locale_getstring (IDS_SETTINGS),
				&rect,
				config.hfont_ui_body,
				is_pressed ? palette.accentpressed : (is_hover ? palette.accenthover : palette.accent),
				flags
			);

			if (GetFocus () == dis->hwndItem)
			{
				focus = rect;

				InsetRect (&focus, _app_ui_scale (2, dpi_value), _app_ui_scale (2, dpi_value));

				_app_ui_drawfocus (dis->hDC, &focus, radius, palette.text);
			}

			break;
		}
	}
}

LRESULT CALLBACK _app_ui_buttonproc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	WNDPROC wnd_proc;
	TRACKMOUSEEVENT tme = {0};

	wnd_proc = (WNDPROC)_r_wnd_getcontext (hwnd, LONG_MAX);

	if (!wnd_proc)
		return 0;

	switch (msg)
	{
		case WM_MOUSEMOVE:
		{
			if (config.hwnd_ui_hover != hwnd)
			{
				if (config.hwnd_ui_hover)
					InvalidateRect (config.hwnd_ui_hover, NULL, FALSE);

				config.hwnd_ui_hover = hwnd;

				InvalidateRect (hwnd, NULL, FALSE);

				tme.cbSize = sizeof (tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;

				TrackMouseEvent (&tme);
			}

			break;
		}

		case WM_MOUSELEAVE:
		{
			if (config.hwnd_ui_hover == hwnd)
				config.hwnd_ui_hover = NULL;

			InvalidateRect (hwnd, NULL, FALSE);

			break;
		}

		case WM_ERASEBKGND:
		{
			return TRUE;
		}

		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		{
			InvalidateRect (hwnd, NULL, FALSE);

			break;
		}

		case WM_NCDESTROY:
		{
			if (config.hwnd_ui_hover == hwnd)
				config.hwnd_ui_hover = NULL;

			_r_wnd_removecontext (hwnd, LONG_MAX);

			break;
		}
	}

	return CallWindowProcW (wnd_proc, hwnd, msg, wparam, lparam);
}

VOID _app_ui_initialize (
	_In_ HWND hwnd
)
{
	INT controls[] = {
		IDC_CLEAN,
		IDC_CLEANMENU,
		IDC_MAINMENU,
		IDC_SETTINGS,
	};

	HWND hctrl;

	_app_ui_fontscreate (_r_dc_getwindowdpi (hwnd));

	_app_ui_localize (hwnd);

	// the buttons paint themselves, the subclass only tracks hover and focus
	for (ULONG_PTR i = 0; i < RTL_NUMBER_OF (controls); i++)
	{
		hctrl = GetDlgItem (hwnd, controls[i]);

		if (hctrl)
			_r_wnd_setsubclass (hctrl, GWLP_WNDPROC, &_app_ui_buttonproc);
	}
}

VOID _app_ui_showcleanmenu (
	_In_ HWND hwnd
)
{
	WCHAR buffer[0x100];
	RECT rect;
	POINT point;
	HMENU hmenu;
	HWND hctrl;

	hmenu = CreatePopupMenu ();

	if (!hmenu)
		return;

	_r_menu_additem (hmenu, IDM_CLEAN_WORKINGSET, _r_locale_getstring (IDS_MEMREGION_WORKINGSET));
	_r_menu_additem (hmenu, IDM_CLEAN_SYSTEMFILECACHE, _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE));
	_r_menu_additem (hmenu, IDM_CLEAN_MODIFIEDFILECACHE, _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE));

	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST));
	_r_menu_additem (hmenu, IDM_CLEAN_MODIFIEDLIST, buffer);

	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));
	_r_menu_additem (hmenu, IDM_CLEAN_STANDBYLIST, buffer);

	_r_menu_additem (hmenu, IDM_CLEAN_STANDBYLISTPRIORITY0, _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0));
	_r_menu_additem (hmenu, IDM_CLEAN_REGISTRYCACHE, _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE));
	_r_menu_additem (hmenu, IDM_CLEAN_COMBINEMEMORYLISTS, _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS));

	hctrl = GetDlgItem (hwnd, IDC_CLEANMENU);

	if (hctrl && GetWindowRect (hctrl, &rect))
	{
		point.x = rect.left;
		point.y = rect.bottom;

		_r_menu_popup (hmenu, hwnd, &point, TRUE);
	}

	DestroyMenu (hmenu);
}

VOID _app_ui_showmainmenu (
	_In_ HWND hwnd,
	_In_opt_ PPOINT point_at
)
{
	HMENU hmenu, hsubmenu;
	RECT rect;
	POINT point;
	INT command_id;
	HWND hctrl;

	hmenu = LoadMenuW (_r_sys_getimagebase (), MAKEINTRESOURCEW (IDM_MAIN));

	if (!hmenu)
		return;

	hsubmenu = GetSubMenu (hmenu, 0);

	if (!hsubmenu)
	{
		DestroyMenu (hmenu);

		return;
	}

	_r_menu_setitemtextformat (hsubmenu, IDM_SETTINGS, FALSE, L"%s...\tF2", _r_locale_getstring (IDS_SETTINGS));
	_r_menu_setitemtext (hsubmenu, IDM_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));
	_r_menu_setitemtext (hsubmenu, IDM_ALWAYSONTOP_CHK, FALSE, _r_locale_getstring (IDS_ALWAYSONTOP_CHK));
	_r_menu_setitemtext (hsubmenu, IDM_LOADONSTARTUP_CHK, FALSE, _r_locale_getstring (IDS_LOADONSTARTUP_CHK));
	_r_menu_setitemtext (hsubmenu, IDM_USEDARKTHEME, FALSE, _r_locale_getstring (IDS_USEDARKTHEME));
	_r_menu_setitemtext (hsubmenu, IDM_STARTMINIMIZED_CHK, FALSE, _r_locale_getstring (IDS_STARTMINIMIZED_CHK));
	_r_menu_setitemtext (hsubmenu, IDM_REDUCTCONFIRMATION_CHK, FALSE, _r_locale_getstring (IDS_REDUCTCONFIRMATION_CHK));
	_r_menu_setitemtext (hsubmenu, IDM_SKIPUACWARNING_CHK, FALSE, _r_locale_getstring (IDS_SKIPUACWARNING_CHK));
	_r_menu_setitemtext (hsubmenu, IDM_CHECKUPDATES_CHK, FALSE, _r_locale_getstring (IDS_CHECKUPDATES_CHK));
	_r_menu_setitemtext (hsubmenu, LANG_MENU, TRUE, _r_locale_getstring (IDS_LANGUAGE));
	_r_menu_setitemtext (hsubmenu, IDM_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
	_r_menu_setitemtext (hsubmenu, IDM_CHECKUPDATES, FALSE, _r_locale_getstring (IDS_CHECKUPDATES));
	_r_menu_setitemtextformat (hsubmenu, IDM_ABOUT, FALSE, L"%s\tF1", _r_locale_getstring (IDS_ABOUT));

	_r_menu_checkitem (hsubmenu, IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"AlwaysOnTop", FALSE, NULL));
	_r_menu_checkitem (hsubmenu, IDM_USEDARKTHEME, 0, MF_BYCOMMAND, _r_theme_isenabled ());
	_r_menu_checkitem (hsubmenu, IDM_LOADONSTARTUP_CHK, 0, MF_BYCOMMAND, _r_autorun_isenabled ());
	_r_menu_checkitem (hsubmenu, IDM_STARTMINIMIZED_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"IsStartMinimized", FALSE, NULL));
	_r_menu_checkitem (hsubmenu, IDM_REDUCTCONFIRMATION_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"IsShowReductConfirmation", TRUE, NULL));
	_r_menu_checkitem (hsubmenu, IDM_SKIPUACWARNING_CHK, 0, MF_BYCOMMAND, _r_skipuac_isenabled ());
	_r_menu_checkitem (hsubmenu, IDM_CHECKUPDATES_CHK, 0, MF_BYCOMMAND, _app_update_isenabled ());

	if (!_r_sys_iselevated ())
		_r_menu_enableitem (hsubmenu, IDM_SKIPUACWARNING_CHK, FALSE, FALSE);

	// the language submenu is rebuilt with the available locales every time the menu is opened
	_r_locale_enum (hsubmenu, LANG_MENU, IDX_LANGUAGE);

	if (point_at)
	{
		point = *point_at;
	}
	else
	{
		hctrl = GetDlgItem (hwnd, IDC_MAINMENU);

		if (!hctrl || !GetWindowRect (hctrl, &rect))
		{
			DestroyMenu (hmenu);

			return;
		}

		point.x = rect.right;
		point.y = rect.bottom;
	}

	command_id = TrackPopupMenuEx (
		hsubmenu,
		TPM_RIGHTALIGN | TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTBUTTON,
		point.x,
		point.y,
		hwnd,
		NULL
	);

	if (command_id)
		_r_ctrl_sendcommand (hwnd, command_id, 0);

	DestroyMenu (hmenu);
}

HICON _app_iconcreate (
	_In_opt_ ULONG percent
)
{
	R_MEMORY_INFO mem_info;
	WCHAR icon_text[0x08];
	ICONINFO ii = {0};
	LOGFONT logfont;
	R_STRINGREF sr;
	HGDIOBJ prev_bmp, prev_font;
	HICON hicon_new;
	HFONT hfont_fit = NULL;
	COLORREF bg_color, text_color;
	INT prev_mode;
	LONG max_width;
	LONG text_width;
	BOOLEAN is_border, is_round, is_transparent, has_danger;

	if (!config.hdc || !config.hdc_mask || !config.hbitmap || !config.hbitmap_mask || !config.hfont)
		return config.hicon;

	text_color = _r_config_getulong_ex (L"TrayColorText", TRAY_COLOR_TEXT, NULL);
	bg_color = _r_config_getulong_ex (L"TrayColorBg", TRAY_COLOR_BG, NULL);

	is_transparent = _r_config_getboolean_ex (L"TrayUseTransparency", FALSE, NULL);
	is_border = _r_config_getboolean_ex (L"TrayShowBorder", FALSE, NULL);
	is_round = _r_config_getboolean_ex (L"TrayRoundCorners", FALSE, NULL);

	if (percent == 0)
	{
		_app_getmemoryinfo (&mem_info);

		percent = mem_info.physical_memory.percent;
	}

	has_danger = (percent >= _app_getdangervalue ());

	if (has_danger || percent >= _app_getwarningvalue ())
	{
		if (_r_config_getboolean_ex (L"TrayChangeBg", TRUE, NULL))
		{
			bg_color = has_danger ? _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL) : _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL);

			is_transparent = FALSE;

			// warning and danger colours are user provided, so pick whichever digit colour stays readable on them
			text_color = _r_dc_getcolorbrightness (bg_color);
		}
		else
		{
			text_color = has_danger ? _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL) : _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL);
		}
	}

	// set tray text
	_r_str_fromulong (icon_text, RTL_NUMBER_OF (icon_text), percent);

	_r_obj_initializestringref (&sr, icon_text);

	// draw main device context
	prev_bmp = SelectObject (config.hdc, config.hbitmap);
	prev_font = SelectObject (config.hdc, config.hfont);
	prev_mode = SetBkMode (config.hdc, TRANSPARENT);

	// the badge is a dozen pixels wide, so shrink the digits until they really fit inside it
	max_width = config.icon_size.right - 2;
	text_width = _r_dc_getfontwidth (config.hdc, &sr, NULL);

	if (max_width > 0 && text_width > max_width && GetObjectW (config.hfont, sizeof (LOGFONT), &logfont))
	{
		logfont.lfHeight = MulDiv (logfont.lfHeight, max_width, text_width);
		logfont.lfWidth = 0;

		// a strongly shrunk badge rounds the height away to nothing and the font mapper then silently restores the old size
		if (!logfont.lfHeight)
			logfont.lfHeight = -1;

		hfont_fit = CreateFontIndirectW (&logfont);

		if (hfont_fit)
			SelectObject (config.hdc, hfont_fit);
	}

	_app_drawbackground (config.hdc, bg_color, is_border ? text_color : bg_color, is_transparent ? text_color : bg_color, &config.icon_size, is_round);

	_r_dc_drawtext (NULL, config.hdc, &sr, &config.icon_size, 0, 0, DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX, text_color);

	SetBkMode (config.hdc, prev_mode);

	SelectObject (config.hdc, prev_font);
	SelectObject (config.hdc, prev_bmp);

	// draw mask device context
	prev_bmp = SelectObject (config.hdc_mask, config.hbitmap_mask);
	prev_font = SelectObject (config.hdc_mask, hfont_fit ? hfont_fit : config.hfont);
	prev_mode = SetBkMode (config.hdc_mask, TRANSPARENT);

	_app_drawbackground (config.hdc_mask, TRAY_COLOR_WHITE, is_border ? TRAY_COLOR_BLACK : TRAY_COLOR_WHITE, is_transparent ? TRAY_COLOR_WHITE : TRAY_COLOR_BLACK, &config.icon_size, is_round);

	_r_dc_drawtext (NULL, config.hdc_mask, &sr, &config.icon_size, 0, 0, DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX, TRAY_COLOR_BLACK);

	SetBkMode (config.hdc_mask, prev_mode);

	SelectObject (config.hdc_mask, prev_bmp);
	SelectObject (config.hdc_mask, prev_font);

	// create icon
	ii.hbmMask = config.hbitmap_mask;
	ii.hbmColor = config.hbitmap;
	ii.fIcon = TRUE;

	hicon_new = CreateIconIndirect (&ii);

	if (hfont_fit)
		DeleteObject (hfont_fit);

	if (!hicon_new)
		return config.hicon;

	// the previous icon is released by the caller after the tray have taken the new one
	config.hicon = hicon_new;

	return hicon_new;
}

VOID CALLBACK _app_timercallback (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ UINT_PTR id_event,
	_In_ ULONG time
)
{
	R_MEMORY_INFO mem_info;
	WCHAR tooltip[0x80];
	HICON hicon = NULL;
	HICON hicon_prev = NULL;
	LONG64 timestamp;
	ULONG limit_value;
	ULONG pending_mask;
	BOOLEAN is_clean = FALSE;

	if (id_event != UID)
		return;

	_app_getmemoryinfo (&mem_info);

	// a clean clicked without administrator rights is armed in the config, because the elevated copy starts empty-handed
	if (msg == WM_TIMER && _r_sys_iselevated ())
	{
		pending_mask = _r_config_getulong_ex (L"CleanPendingMask", 0, NULL);

		if (pending_mask)
		{
			_r_config_setulong_ex (L"CleanPendingMask", 0, NULL);

			_app_memoryclean (hwnd, SOURCE_MANUAL, pending_mask);
		}
	}

	// autocleanup functional (settings and dpi changes reuse this routine, they must not trigger a clean)
	if (msg == WM_TIMER && _r_sys_iselevated ())
	{
		if (_r_config_getboolean_ex (L"AutoreductEnable", FALSE, NULL))
		{
			limit_value = _app_getlimitvalue ();

			// 0 is the "Disable" entry of the tray submenu, not a threshold to compare against
			if (limit_value != 0 && mem_info.physical_memory.percent >= limit_value)
			{
				// cooldown prevents cleaning loop when memory usage stays above the limit (issue #191)
				timestamp = _r_unixtime_now () - _r_config_getlong64_ex (L"StatisticLastReduct", 0, NULL);

				if (timestamp >= AUTOREDUCT_COOLDOWN)
					is_clean = TRUE;
			}
		}

		if (!is_clean && _r_config_getboolean_ex (L"AutoreductIntervalEnable", FALSE, NULL))
		{
			timestamp = _r_unixtime_now () - _r_config_getlong64_ex (L"StatisticLastReduct", 0, NULL);

			if (timestamp >= (_app_getintervalvalue () * 60))
				is_clean = TRUE;
		}

		if (is_clean)
			_app_memoryclean (hwnd, SOURCE_AUTO, 0);
	}

	// the app lives in the tray for weeks, so a period set only at startup would never fire again
	if (msg == WM_TIMER && _app_update_isready ())
		_app_update_check (hwnd, TRUE);

	// check previous percent to prevent icon redraw
	if (!config.ms_prev || config.ms_prev != mem_info.physical_memory.percent)
	{
		config.ms_prev = mem_info.physical_memory.percent; // store last percentage value (required!)

		hicon_prev = config.hicon;

		hicon = _app_iconcreate (config.ms_prev);
	}
	else
	{
		// the icon did not change, so only the tooltip is sent to the tray
		hicon = NULL;
	}

	_r_str_printf (
		tooltip,
		RTL_NUMBER_OF (tooltip),
		L"%s: %" TEXT (PR_DOUBLE) L"%%\r\n%s: %" TEXT (PR_DOUBLE) L"%%\r\n%s: %" TEXT (PR_DOUBLE) L"%%",
		_r_locale_getstring (IDS_GROUP_1),
		mem_info.physical_memory.percent_f,
		_r_locale_getstring (IDS_GROUP_2),
		mem_info.page_file.percent_f,
		_r_locale_getstring (IDS_GROUP_3),
		mem_info.system_cache.percent_f
	);

	// NIM_MODIFY makes the shell repaint the tray band, so send it only when the icon or the text changed
	if (hicon || _r_str_compare (tooltip, config.tooltip_prev, FALSE) != 0)
	{
		_r_str_copy (config.tooltip_prev, RTL_NUMBER_OF (config.tooltip_prev), tooltip);

		_r_tray_setinfo (hwnd, &GUID_TrayIcon, hicon, config.tooltip_prev);
	}

	// the tray have taken the new icon - the previous one is not required anymore
	if (hicon_prev && hicon_prev != config.hicon)
		DestroyIcon (hicon_prev);

	if (!_r_wnd_isvisible (hwnd, FALSE))
		return;

	// an idle machine keeps producing the same numbers, so the card is repainted only when one changed
	if (_app_ui_updatemetrics (&mem_info))
		InvalidateRect (hwnd, NULL, FALSE);
}

VOID _app_iconredraw (
	_In_opt_ HWND hwnd
)
{
	config.ms_prev = 0;

	if (hwnd)
		_app_timercallback (hwnd, 0, UID, 0);
}

VOID _app_iconinit (
	_In_ LONG dpi_value
)
{
	LOGFONT logfont;
	PVOID bits;
	HDC hdc;

	SAFE_DELETE_OBJECT (config.hbitmap_mask);
	SAFE_DELETE_OBJECT (config.hbitmap);
	SAFE_DELETE_OBJECT (config.hfont);

	SAFE_DELETE_DC (config.hdc_mask);
	SAFE_DELETE_DC (config.hdc);

	// init font
	_app_fontinit (&logfont, dpi_value);

	config.hfont = CreateFontIndirectW (&logfont);

	// init rect
	SetRect (&config.icon_size, 0, 0, _r_dc_getsystemmetrics (SM_CXSMICON, dpi_value), _r_dc_getsystemmetrics (SM_CYSMICON, dpi_value));

	// init dc
	hdc = GetDC (NULL);

	if (!hdc)
		return;

	config.hdc = CreateCompatibleDC (hdc);
	config.hdc_mask = CreateCompatibleDC (hdc);

	// init bitmap
	config.hbitmap = _r_dc_createbitmap (hdc, config.icon_size.right, config.icon_size.bottom, &bits);
	config.hbitmap_mask = CreateBitmap (config.icon_size.right, config.icon_size.bottom, 1, 1, NULL);

	ReleaseDC (NULL, hdc);

	if (!config.hdc || !config.hdc_mask || !config.hbitmap || !config.hbitmap_mask || !config.hfont)
	{
		SAFE_DELETE_OBJECT (config.hbitmap_mask);
		SAFE_DELETE_OBJECT (config.hbitmap);
		SAFE_DELETE_OBJECT (config.hfont);

		SAFE_DELETE_DC (config.hdc_mask);
		SAFE_DELETE_DC (config.hdc);

		_r_log (LOG_LEVEL_ERROR, NULL, L"_app_iconinit", STATUS_NO_MEMORY, L"Could not create tray icon drawing surfaces");
	}
}

VOID _app_trayiconrefresh (
	_In_ HWND hwnd
)
{
	HICON hicon_prev;

	hicon_prev = config.hicon;

	_app_iconinit (_r_dc_gettaskbardpi ());

	_r_tray_create (hwnd, &GUID_TrayIcon, RM_TRAYICON, _app_iconcreate (0), _r_app_getname (), FALSE);

	// the tray has taken the new icon, the one built for the previous dpi is not required anymore
	if (hicon_prev && hicon_prev != config.hicon)
		DestroyIcon (hicon_prev);

	_app_iconredraw (hwnd);
}

VOID _app_hotkeyinit (
	_In_ HWND hwnd
)
{
	LONG hotkey;

	UnregisterHotKey (hwnd, UID);

	if (!_r_config_getboolean_ex (L"HotkeyCleanEnable", FALSE, NULL))
		return;

	hotkey = _r_config_getlong_ex (L"HotkeyClean", MAKEWORD (VK_F1, HOTKEYF_CONTROL), NULL);

	if (!hotkey)
		return;

	if (!RegisterHotKey (hwnd, UID, (HIBYTE (hotkey) & 2) | ((HIBYTE (hotkey) & 4) >> 2) | ((HIBYTE (hotkey) & 1) << 2) | MOD_NOREPEAT, LOBYTE (hotkey)))
		_r_show_errormessage (hwnd, L"Could not register hotkey!", NtLastError (), NULL, ET_WINDOWS);
}

FORCEINLINE VOID _app_setfontcontrol (
	_In_ HWND hwnd,
	_In_ INT ctrl_id,
	_In_ PLOGFONT logfont,
	_In_ LONG dpi_value
)
{
	_r_ctrl_setstringformat (hwnd, ctrl_id, L"%s, %" TEXT (PR_LONG) L"px, %" TEXT (PR_LONG), logfont->lfFaceName, _r_dc_fontheighttosize (logfont->lfHeight, dpi_value), logfont->lfWeight);
}

INT_PTR CALLBACK SettingsProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	switch (msg)
	{
		case RM_INITIALIZE:
		{
			INT dialog_id = (INT)wparam;

			switch (dialog_id)
			{
				case IDD_SETTINGS_GENERAL:
				{
					_r_ctrl_checkbutton (hwnd, IDC_ALWAYSONTOP_CHK, _r_config_getboolean_ex (L"AlwaysOnTop", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_LOADONSTARTUP_CHK, _r_autorun_isenabled ());
					_r_ctrl_checkbutton (hwnd, IDC_STARTMINIMIZED_CHK, _r_config_getboolean_ex (L"IsStartMinimized", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_REDUCTCONFIRMATION_CHK, _r_config_getboolean_ex (L"IsShowReductConfirmation", TRUE, NULL));

					if (!_r_sys_iselevated ())
						_r_ctrl_enable (hwnd, IDC_SKIPUACWARNING_CHK, FALSE);

					_r_ctrl_checkbutton (hwnd, IDC_SKIPUACWARNING_CHK, _r_skipuac_isenabled ());
					_r_ctrl_checkbutton (hwnd, IDC_CHECKUPDATES_CHK, _app_update_isenabled ());

					_r_locale_enum (hwnd, IDC_LANGUAGE, 0);

					break;
				}

				case IDD_SETTINGS_MEMORY:
				{
					WCHAR buffer[0x100];
					ULONG mask;

					_r_listview_setstyle (hwnd, IDC_REGIONS, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER, FALSE);

					_r_wnd_setcontext (hwnd, IDC_REGIONS, INVALID_HANDLE_VALUE);

					// fix by @XakerTwo
					_r_listview_deleteallcolumns (hwnd, IDC_REGIONS);
					_r_listview_deleteallitems (hwnd, IDC_REGIONS);

					_r_listview_addcolumn (hwnd, IDC_REGIONS, 0, NULL, 10, LVCFMT_LEFT);

					_r_listview_additem (hwnd, IDC_REGIONS, 0, _r_locale_getstring (IDS_MEMREGION_WORKINGSET), I_DEFAULT, I_DEFAULT, REDUCT_WORKINGSET);
					_r_listview_additem (hwnd, IDC_REGIONS, 1, _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE), I_DEFAULT, I_DEFAULT, REDUCT_SYSTEMFILECACHE);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST));
					_r_listview_additem (hwnd, IDC_REGIONS, 2, buffer, I_DEFAULT, I_DEFAULT, REDUCT_MODIFIEDLIST);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));
					_r_listview_additem (hwnd, IDC_REGIONS, 3, buffer, I_DEFAULT, I_DEFAULT, REDUCT_STANDBYLIST);

					_r_listview_additem (hwnd, IDC_REGIONS, 4, _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0), I_DEFAULT, I_DEFAULT, REDUCT_STANDBYPRIORITY0LIST);
					_r_listview_additem (hwnd, IDC_REGIONS, 5, _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE), I_DEFAULT, I_DEFAULT, REDUCT_MODIFIEDFILECACHE);

					_r_listview_additem (hwnd, IDC_REGIONS, 6, _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE), I_DEFAULT, I_DEFAULT, REDUCT_REGISTRYCACHE);

					_r_listview_additem (hwnd, IDC_REGIONS, 7, _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS), I_DEFAULT, I_DEFAULT, REDUCT_COMBINEMEMORYLISTS);

					_r_listview_setcolumn (hwnd, IDC_REGIONS, 0, NULL, -100);

					mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 0, (mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 1, (mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 2, (mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 3, (mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 4, (mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 5, (mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 6, (mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 7, (mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS);

					_r_wnd_removecontext (hwnd, IDC_REGIONS);

					if (!_r_sys_iselevated ())
					{
						_r_ctrl_enable (hwnd, IDC_REGIONS, FALSE);
						_r_ctrl_enable (hwnd, IDC_AUTOREDUCTENABLE_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN, FALSE);
					}

					_r_ctrl_checkbutton (hwnd, IDC_AUTOREDUCTENABLE_CHK, _r_config_getboolean_ex (L"AutoreductEnable", FALSE, NULL));

					_r_updown_setrange (hwnd, IDC_AUTOREDUCTVALUE, 0, 100);

					_r_updown_setvalue (hwnd, IDC_AUTOREDUCTVALUE, _app_getlimitvalue ());

					_r_ctrl_checkbutton (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, _r_config_getboolean_ex (L"AutoreductIntervalEnable", FALSE, NULL));

					_r_updown_setrange (hwnd, IDC_AUTOREDUCTINTERVALVALUE, 1, 1440);

					_r_updown_setvalue (hwnd, IDC_AUTOREDUCTINTERVALVALUE, _app_getintervalvalue ());

					_r_ctrl_checkbutton (hwnd, IDC_HOTKEY_CLEAN_CHK, _r_config_getboolean_ex (L"HotkeyCleanEnable", FALSE, NULL));

					if (!_r_ctrl_isbuttonchecked (hwnd, IDC_HOTKEY_CLEAN_CHK))
						_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN, FALSE);

					_r_hotkey_set (hwnd, IDC_HOTKEY_CLEAN, _r_config_getlong_ex (L"HotkeyClean", MAKEWORD (VK_F1, HOTKEYF_CONTROL), NULL));

					_r_ctrl_sendcommand (hwnd, IDC_AUTOREDUCTENABLE_CHK, 0);
					_r_ctrl_sendcommand (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, 0);
					_r_ctrl_sendcommand (hwnd, IDC_HOTKEY_CLEAN_CHK, 0);

					break;
				}

				case IDD_SETTINGS_APPEARANCE:
				{
					LOGFONT logfont;
					LONG dpi_value;

					_r_ctrl_checkbutton (hwnd, IDC_TRAYUSETRANSPARENCY_CHK, _r_config_getboolean_ex (L"TrayUseTransparency", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYSHOWBORDER_CHK, _r_config_getboolean_ex (L"TrayShowBorder", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYROUNDCORNERS_CHK, _r_config_getboolean_ex (L"TrayRoundCorners", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYCHANGEBG_CHK, _r_config_getboolean_ex (L"TrayChangeBg", TRUE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYUSEANTIALIASING_CHK, _r_config_getboolean_ex (L"TrayUseAntialiasing", TRUE, NULL));

					dpi_value = _r_dc_gettaskbardpi ();

					_app_fontinit (&logfont, dpi_value);
					_app_setfontcontrol (hwnd, IDC_FONT, &logfont, dpi_value);

					_r_listview_setstyle (hwnd, IDC_COLORS, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP, FALSE);

					// fix by @XakerTwo
					_r_listview_deleteallcolumns (hwnd, IDC_COLORS);
					_r_listview_deleteallitems (hwnd, IDC_COLORS);

					_r_listview_addcolumn (hwnd, IDC_COLORS, 0, NULL, -100, LVCFMT_LEFT);

					_r_listview_additem (hwnd, IDC_COLORS, 0, _r_locale_getstring (IDS_COLOR_TEXT_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorText", TRAY_COLOR_TEXT, NULL));
					_r_listview_additem (hwnd, IDC_COLORS, 1, _r_locale_getstring (IDS_COLOR_BACKGROUND_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorBg", TRAY_COLOR_BG, NULL));
					_r_listview_additem (hwnd, IDC_COLORS, 2, _r_locale_getstring (IDS_COLOR_WARNING_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL));
					_r_listview_additem (hwnd, IDC_COLORS, 3, _r_locale_getstring (IDS_COLOR_DANGER_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL));

					break;
				}

				case IDD_SETTINGS_TRAY:
				{
					_r_updown_setrange (hwnd, IDC_TRAYLEVELWARNING, 0, 100);
					_r_updown_setvalue (hwnd, IDC_TRAYLEVELWARNING, _app_getwarningvalue ());

					_r_updown_setrange (hwnd, IDC_TRAYLEVELDANGER, 0, 100);
					_r_updown_setvalue (hwnd, IDC_TRAYLEVELDANGER, _app_getdangervalue ());

					_r_ctrl_checkbutton (hwnd, IDC_SHOW_CLEAN_RESULT_CHK, _r_config_getboolean_ex (L"BalloonCleanResults", TRUE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_NOTIFICATIONSOUND_CHK, _r_config_getboolean_ex (L"IsNotificationsSound", TRUE, NULL));

					break;
				}

				case IDD_SETTINGS_ADVANCED:
				{
					_r_ctrl_checkbutton (hwnd, IDC_ALLOWSTANDBYLISTCLEANUP_CHK, _r_config_getboolean_ex (L"IsAllowStandbyListCleanup", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_LOGRESULTS_CHK, _r_config_getboolean_ex (L"LogCleanResults", FALSE, NULL));

					break;
				}
			}

			break;
		}

		case RM_LOCALIZE:
		{
			INT dialog_id = (INT)wparam;

			// localize titles
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_1, L"%s:", _r_locale_getstring (IDS_TITLE_1));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_2, L"%s:", _r_locale_getstring (IDS_TITLE_2));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_3, L"%s:", _r_locale_getstring (IDS_TITLE_3));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_4, L"%s:", _r_locale_getstring (IDS_TITLE_4));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_5, L"%s:", _r_locale_getstring (IDS_TITLE_5));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_6, L"%s:", _r_locale_getstring (IDS_TITLE_6));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_7, L"%s:", _r_locale_getstring (IDS_TITLE_7));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_8, L"%s:", _r_locale_getstring (IDS_TITLE_8));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_9, L"%s:", _r_locale_getstring (IDS_TITLE_9));

			switch (dialog_id)
			{
				case IDD_SETTINGS_GENERAL:
				{
					_r_ctrl_setstring (hwnd, IDC_ALWAYSONTOP_CHK, _r_locale_getstring (IDS_ALWAYSONTOP_CHK));
					_r_ctrl_setstring (hwnd, IDC_LOADONSTARTUP_CHK, _r_locale_getstring (IDS_LOADONSTARTUP_CHK));
					_r_ctrl_setstring (hwnd, IDC_STARTMINIMIZED_CHK, _r_locale_getstring (IDS_STARTMINIMIZED_CHK));
					_r_ctrl_setstring (hwnd, IDC_REDUCTCONFIRMATION_CHK, _r_locale_getstring (IDS_REDUCTCONFIRMATION_CHK));
					_r_ctrl_setstring (hwnd, IDC_SKIPUACWARNING_CHK, _r_locale_getstring (IDS_SKIPUACWARNING_CHK));
					_r_ctrl_setstring (hwnd, IDC_CHECKUPDATES_CHK, _r_locale_getstring (IDS_CHECKUPDATES_CHK));
					_r_ctrl_setstring (hwnd, IDC_LANGUAGE_HINT, _r_locale_getstring (IDS_LANGUAGE_HINT));

					break;
				}

				case IDD_SETTINGS_MEMORY:
				{
					_r_ctrl_setstring (hwnd, IDC_AUTOREDUCTENABLE_CHK, _r_locale_getstring (IDS_AUTOREDUCTENABLE_CHK));
					_r_ctrl_setstring (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, _r_locale_getstring (IDS_AUTOREDUCTINTERVALENABLE_CHK));
					_r_ctrl_setstring (hwnd, IDC_HOTKEY_CLEAN_CHK, _r_locale_getstring (IDS_HOTKEY_CLEAN_CHK));

					break;
				}

				case IDD_SETTINGS_APPEARANCE:
				{
					_r_ctrl_setstring (hwnd, IDC_TRAYUSETRANSPARENCY_CHK, _r_locale_getstring (IDS_TRAYUSETRANSPARENCY_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYSHOWBORDER_CHK, _r_locale_getstring (IDS_TRAYSHOWBORDER_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYROUNDCORNERS_CHK, _r_locale_getstring (IDS_TRAYROUNDCORNERS_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYCHANGEBG_CHK, _r_locale_getstring (IDS_TRAYCHANGEBG_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYUSEANTIALIASING_CHK, _r_locale_getstring (IDS_TRAYUSEANTIALIASING_CHK));
					_r_ctrl_setstring (hwnd, IDC_FONT_HINT, _r_locale_getstring (IDS_FONT_HINT));

					_r_listview_setitem (hwnd, IDC_COLORS, 0, 0, _r_locale_getstring (IDS_COLOR_TEXT_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);
					_r_listview_setitem (hwnd, IDC_COLORS, 1, 0, _r_locale_getstring (IDS_COLOR_BACKGROUND_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);
					_r_listview_setitem (hwnd, IDC_COLORS, 2, 0, _r_locale_getstring (IDS_COLOR_WARNING_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);
					_r_listview_setitem (hwnd, IDC_COLORS, 3, 0, _r_locale_getstring (IDS_COLOR_DANGER_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);

					break;
				}

				case IDD_SETTINGS_TRAY:
				{
					LPCWSTR string;

					_r_ctrl_setstring (hwnd, IDC_TRAYLEVELWARNING_HINT, _r_locale_getstring (IDS_TRAYLEVELWARNING_HINT));
					_r_ctrl_setstring (hwnd, IDC_TRAYLEVELDANGER_HINT, _r_locale_getstring (IDS_TRAYLEVELDANGER_HINT));

					_r_ctrl_setstring (hwnd, IDC_TRAYACTIONSC_HINT, _r_locale_getstring (IDS_TRAYACTIONSC_HINT));
					_r_ctrl_setstring (hwnd, IDC_TRAYACTIONMC_HINT, _r_locale_getstring (IDS_TRAYACTIONMC_HINT));

					_r_combobox_clear (hwnd, IDC_TRAYACTIONSC);
					_r_combobox_clear (hwnd, IDC_TRAYACTIONMC);

					for (INT i = 0; i < 3; i++)
					{
						string = _r_locale_getstring (IDS_TRAY_ACTION_1 + i);

						_r_combobox_insertitem (hwnd, IDC_TRAYACTIONSC, i, string, i);
						_r_combobox_insertitem (hwnd, IDC_TRAYACTIONMC, i, string, i);
					}

					_r_combobox_setcurrentitembylparam (hwnd, IDC_TRAYACTIONSC, _r_config_getlong_ex (L"TrayActionDc", 0, NULL));
					_r_combobox_setcurrentitembylparam (hwnd, IDC_TRAYACTIONMC, _r_config_getlong_ex (L"TrayActionMc", 1, NULL));

					_r_ctrl_setstring (hwnd, IDC_SHOW_CLEAN_RESULT_CHK, _r_locale_getstring (IDS_SHOW_CLEAN_RESULT_CHK));
					_r_ctrl_setstring (hwnd, IDC_NOTIFICATIONSOUND_CHK, _r_locale_getstring (IDS_NOTIFICATIONSOUND_CHK));

					break;
				}

				case IDD_SETTINGS_ADVANCED:
				{
					// the page shares IDC_TITLE_1 with the general page, so its caption must be overwritten after the common pass
					_r_ctrl_setstringformat (hwnd, IDC_TITLE_1, L"%s:", _r_locale_getstring (IDS_TITLE_ADVANCED));

					_r_ctrl_setstring (hwnd, IDC_ALLOWSTANDBYLISTCLEANUP_CHK, _r_locale_getstring (IDS_ALLOWSTANDBYLISTCLEANUP_CHK));
					_r_ctrl_setstring (hwnd, IDC_LOGRESULTS_CHK, _r_locale_getstring (IDS_LOGRESULTS_CHK));

					break;
				}
			}

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_CUSTOMDRAW:
				{
					LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;
					COLORREF clr;

					switch (lpnmlv->nmcd.dwDrawStage)
					{
						case CDDS_PREPAINT:
						{
							SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);

							return CDRF_NOTIFYITEMDRAW;
						}

						case CDDS_ITEMPREPAINT:
						{
							LONG_PTR value = CDRF_DODEFAULT;

							if (lpnmlv->nmcd.hdr.idFrom == IDC_COLORS)
							{
								clr = (COLORREF)lpnmlv->nmcd.lItemlParam;

								lpnmlv->clrText = _r_dc_getcolorbrightness (clr);
								lpnmlv->clrTextBk = clr;

								_r_dc_fillrect (lpnmlv->nmcd.hdc, &lpnmlv->nmcd.rc, clr);

								value = CDRF_NEWFONT;
							}

							SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, value);

							return value;
						}
					}

					break;
				}

				case NM_DBLCLK:
				{
					LPNMITEMACTIVATE lpnmlv = (LPNMITEMACTIVATE)lparam;
					CHOOSECOLOR cc = {0};
					COLORREF clr, cust[0x10] = {TRAY_COLOR_DANGER, TRAY_COLOR_WARNING, TRAY_COLOR_BG, TRAY_COLOR_TEXT};

					if (lpnmlv->hdr.idFrom != IDC_COLORS || lpnmlv->iItem == INT_ERROR)
						break;

					clr = (COLORREF)_r_listview_getitemlparam (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom, lpnmlv->iItem);

					cc.lStructSize = sizeof (CHOOSECOLOR);
					cc.Flags = CC_RGBINIT | CC_FULLOPEN;
					cc.hwndOwner = hwnd;
					cc.lpCustColors = cust;
					cc.rgbResult = clr;

					if (ChooseColorW (&cc))
					{
						if (lpnmlv->iItem == 0)
						{
							_r_config_setulong_ex (L"TrayColorText", cc.rgbResult, NULL);
						}
						else if (lpnmlv->iItem == 1)
						{
							_r_config_setulong_ex (L"TrayColorBg", cc.rgbResult, NULL);
						}
						else if (lpnmlv->iItem == 2)
						{
							_r_config_setulong_ex (L"TrayColorWarning", cc.rgbResult, NULL);
						}
						else if (lpnmlv->iItem == 3)
						{
							_r_config_setulong_ex (L"TrayColorDanger", cc.rgbResult, NULL);
						}

						_r_listview_setitem (hwnd, IDC_COLORS, lpnmlv->iItem, lpnmlv->iSubItem, NULL, I_DEFAULT, I_DEFAULT, cc.rgbResult);

						_r_listview_redraw (hwnd, IDC_COLORS);

						_app_iconinit (_r_dc_gettaskbardpi ());
						_app_iconredraw (_r_app_gethwnd ());
					}

					break;
				}

				case LVN_ITEMCHANGED:
				{
					LPNMLISTVIEW lpnmlv = (LPNMLISTVIEW)lparam;
					ULONG mask, value;

					if (lpnmlv->hdr.idFrom != IDC_REGIONS)
						break;

					if (_r_wnd_getcontext (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom))
						break;

					if ((lpnmlv->uChanged & LVIF_STATE) == 0)
						break;

					if ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (1) || ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (2)))
					{
						value = (ULONG)lpnmlv->lParam;

						mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

						if ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (2))
						{
							if ((value & REDUCT_MASK_FREEZES) != 0)
							{
								if (!_r_show_confirmmessage (hwnd, _r_app_getname (), _r_locale_getstring (IDS_QUESTION_WARNING), L"IsShowWarningConfirmation", FALSE))
								{
									_r_listview_setitemcheck (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom, lpnmlv->iItem, FALSE);

									return FALSE;
								}
							}
						}

						if ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (2))
						{
							mask |= value;
						}
						else
						{
							mask &= ~value;
						}

						_r_config_setulong_ex (L"ReductMask2", mask, NULL);
					}

					break;
				}
			}

			break;
		}

		case WM_VSCROLL:
		case WM_HSCROLL:
		{
			LONG value;
			INT ctrl_id;
			BOOLEAN is_stylechanged = FALSE;

			ctrl_id = GetDlgCtrlID ((HWND)lparam);

			if (ctrl_id == IDC_AUTOREDUCTVALUE)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"AutoreductValue", value, NULL);
			}
			else if (ctrl_id == IDC_AUTOREDUCTINTERVALVALUE)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"AutoreductIntervalValue", value, NULL);
			}
			else if (ctrl_id == IDC_TRAYLEVELWARNING)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"TrayLevelWarning", value, NULL);

				is_stylechanged = TRUE;
			}
			else if (ctrl_id == IDC_TRAYLEVELDANGER)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"TrayLevelDanger", value, NULL);

				is_stylechanged = TRUE;
			}

			if (is_stylechanged)
				_app_iconredraw (_r_app_gethwnd ());

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);
			INT notify_code = HIWORD (wparam);

			switch (ctrl_id)
			{
				case IDC_AUTOREDUCTVALUE_CTRL:
				{
					LONG value;

					if (notify_code == EN_CHANGE)
					{
						value = _app_edit_getnumber (hwnd, ctrl_id, _app_getlimitvalue (), 0, 100);

						_r_config_setlong_ex (L"AutoreductValue", value, NULL);

						if (_r_updown_getvalue (hwnd, IDC_AUTOREDUCTVALUE) != value)
							_r_updown_setvalue (hwnd, IDC_AUTOREDUCTVALUE, value);
					}

					break;
				}

				case IDC_AUTOREDUCTINTERVALVALUE_CTRL:
				{
					LONG value;

					if (notify_code == EN_CHANGE)
					{
						value = _app_edit_getnumber (hwnd, ctrl_id, _app_getintervalvalue (), 1, 1440);

						_r_config_setlong_ex (L"AutoreductIntervalValue", value, NULL);

						if (_r_updown_getvalue (hwnd, IDC_AUTOREDUCTINTERVALVALUE) != value)
							_r_updown_setvalue (hwnd, IDC_AUTOREDUCTINTERVALVALUE, value);
					}

					break;
				}

				case IDC_TRAYLEVELWARNING_CTRL:
				case IDC_TRAYLEVELDANGER_CTRL:
				{
					if (notify_code == EN_CHANGE)
					{
						LONG value;

						if (ctrl_id == IDC_TRAYLEVELWARNING_CTRL)
						{
							value = _app_edit_getnumber (hwnd, ctrl_id, _app_getwarningvalue (), 0, 100);

							_r_config_setlong_ex (L"TrayLevelWarning", value, NULL);

							if (_r_updown_getvalue (hwnd, IDC_TRAYLEVELWARNING) != value)
								_r_updown_setvalue (hwnd, IDC_TRAYLEVELWARNING, value);
						}
						else
						{
							value = _app_edit_getnumber (hwnd, ctrl_id, _app_getdangervalue (), 0, 100);

							_r_config_setlong_ex (L"TrayLevelDanger", value, NULL);

							if (_r_updown_getvalue (hwnd, IDC_TRAYLEVELDANGER) != value)
								_r_updown_setvalue (hwnd, IDC_TRAYLEVELDANGER, value);
						}

						_app_iconredraw (_r_app_gethwnd ());
					}

					break;
				}

				case IDC_ALWAYSONTOP_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"AlwaysOnTop", is_enable, NULL);

					break;
				}

				case IDC_LOADONSTARTUP_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_autorun_enable (hwnd, is_enable);

					is_enable = _r_autorun_isenabled ();

					_r_ctrl_checkbutton (hwnd, ctrl_id, is_enable);

					break;
				}

				case IDC_STARTMINIMIZED_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"IsStartMinimized", is_enable, NULL);

					break;
				}

				case IDC_REDUCTCONFIRMATION_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"IsShowReductConfirmation", is_enable, NULL);

					break;
				}

				case IDC_SKIPUACWARNING_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_skipuac_enable (hwnd, is_enable);

					is_enable = _r_skipuac_isenabled ();

					_r_ctrl_checkbutton (hwnd, ctrl_id, is_enable);

					break;
				}

				case IDC_CHECKUPDATES_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_app_update_enable (is_enable);

					break;
				}

				case IDC_LANGUAGE:
				{
					if (notify_code == CBN_SELCHANGE)
						_r_locale_apply (hwnd, ctrl_id, 0);

					break;
				}

				case IDC_AUTOREDUCTENABLE_CHK:
				{
					HWND hbuddy;
					BOOLEAN is_checked = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);
					// the box is only greyed out when the app is not elevated, so being enabled does not mean being checked
					BOOLEAN is_active = is_checked && _r_ctrl_isenabled (hwnd, ctrl_id);

					hbuddy = _r_updown_getbuddy (hwnd, IDC_AUTOREDUCTVALUE);

					if (hbuddy)
						_r_ctrl_enable (hbuddy, 0, is_active);

					// the arrows of the up-down are not the buddy, so they must be switched off together with the edit
					_r_ctrl_enable (hwnd, IDC_AUTOREDUCTVALUE, is_active);

					if (_r_ctrl_isenabled (hwnd, ctrl_id))
						_r_config_setboolean_ex (L"AutoreductEnable", is_checked, NULL);

					break;
				}

				case IDC_AUTOREDUCTINTERVALENABLE_CHK:
				{
					HWND hbuddy;
					BOOLEAN is_checked = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);
					BOOLEAN is_active = is_checked && _r_ctrl_isenabled (hwnd, ctrl_id);

					hbuddy = _r_updown_getbuddy (hwnd, IDC_AUTOREDUCTINTERVALVALUE);

					if (hbuddy)
						_r_ctrl_enable (hbuddy, 0, is_active);

					_r_ctrl_enable (hwnd, IDC_AUTOREDUCTINTERVALVALUE, is_active);

					if (_r_ctrl_isenabled (hwnd, ctrl_id))
						_r_config_setboolean_ex (L"AutoreductIntervalEnable", is_checked, NULL);

					break;
				}

				case IDC_HOTKEY_CLEAN_CHK:
				{
					BOOLEAN is_checked = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN, is_checked);

					_r_config_setboolean_ex (L"HotkeyCleanEnable", is_checked, NULL);

					_app_hotkeyinit (_r_app_gethwnd ());

					break;
				}

				case IDC_HOTKEY_CLEAN:
				{
					if (!_r_ctrl_isbuttonchecked (hwnd, IDC_HOTKEY_CLEAN_CHK))
						break;

					if (notify_code == EN_CHANGE)
					{
						_r_config_setlong_ex (L"HotkeyClean", _r_hotkey_get (hwnd, ctrl_id), NULL);

						_app_hotkeyinit (_r_app_gethwnd ());
					}

					break;
				}

				case IDC_TRAYUSETRANSPARENCY_CHK:
				case IDC_TRAYSHOWBORDER_CHK:
				case IDC_TRAYROUNDCORNERS_CHK:
				case IDC_TRAYCHANGEBG_CHK:
				case IDC_TRAYUSEANTIALIASING_CHK:
				{
					BOOLEAN is_enabled = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					switch (ctrl_id)
					{
						case IDC_TRAYUSETRANSPARENCY_CHK:
						{
							_r_config_setboolean_ex (L"TrayUseTransparency", is_enabled, NULL);
							break;
						}

						case IDC_TRAYSHOWBORDER_CHK:
						{
							_r_config_setboolean_ex (L"TrayShowBorder", is_enabled, NULL);
							break;
						}

						case IDC_TRAYROUNDCORNERS_CHK:
						{
							_r_config_setboolean_ex (L"TrayRoundCorners", is_enabled, NULL);
							break;
						}

						case IDC_TRAYCHANGEBG_CHK:
						{
							_r_config_setboolean_ex (L"TrayChangeBg", is_enabled, NULL);
							break;
						}

						case IDC_TRAYUSEANTIALIASING_CHK:
						{
							_r_config_setboolean_ex (L"TrayUseAntialiasing", is_enabled, NULL);
							break;
						}
					}

					_app_iconinit (_r_dc_gettaskbardpi ());
					_app_iconredraw (_r_app_gethwnd ());

					break;
				}

				case IDC_TRAYACTIONSC:
				{
					if (notify_code == CBN_SELCHANGE)
						_r_config_setlong_ex (L"TrayActionDc", _r_combobox_getcurrentitem (hwnd, ctrl_id), NULL);

					break;
				}

				case IDC_TRAYACTIONMC:
				{
					if (notify_code == CBN_SELCHANGE)
						_r_config_setlong_ex (L"TrayActionMc", _r_combobox_getcurrentitem (hwnd, ctrl_id), NULL);

					break;
				}

				case IDC_SHOW_CLEAN_RESULT_CHK:
				{
					_r_config_setboolean_ex (L"BalloonCleanResults", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}

				case IDC_NOTIFICATIONSOUND_CHK:
				{
					_r_config_setboolean_ex (L"IsNotificationsSound", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}

				case IDC_FONT:
				{
					CHOOSEFONT cf = {0};
					LOGFONT logfont;
					LONG dpi_value;

					cf.lStructSize = sizeof (CHOOSEFONT);
					cf.hwndOwner = hwnd;
					cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST | CF_SCREENFONTS;

					dpi_value = _r_dc_gettaskbardpi ();

					_app_fontinit (&logfont, dpi_value);

					cf.lpLogFont = &logfont;

					if (ChooseFontW (&cf))
					{
						_r_config_setfont_ex (L"TrayFont", &logfont, dpi_value, NULL);

						_app_setfontcontrol (hwnd, IDC_FONT, &logfont, dpi_value);

						_app_iconinit (dpi_value);
						_app_iconredraw (_r_app_gethwnd ());
					}

					break;
				}

				case IDC_ALLOWSTANDBYLISTCLEANUP_CHK:
				{
					_r_config_setboolean_ex (L"IsAllowStandbyListCleanup", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}

				case IDC_LOGRESULTS_CHK:
				{
					_r_config_setboolean_ex (L"LogCleanResults", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

//
// Update checker - queries the release channel of this fork and installs the downloaded setup
//

static LONG update_isbusy = 0;

FORCEINLINE BOOLEAN _app_update_isvalidversion (
	_In_ PR_STRINGREF version
)
{
	BOOLEAN is_number_found = FALSE;

	if (!version->length || version->length > sizeof (WCHAR) * UPDATE_VERSION_MAX_LENGTH)
		return FALSE;

	for (SIZE_T i = 0; i < version->length / sizeof (WCHAR); i++)
	{
		if (version->buffer[i] == L'.')
			continue;

		if (!_r_str_isdigit (version->buffer[i]))
			return FALSE;

		is_number_found = TRUE;
	}

	return is_number_found;
}

BOOLEAN _app_update_isvalidhash (
	_In_ PR_STRINGREF hash
)
{
	WCHAR value;

	if (hash->length != (UPDATE_HASH_HEX_LENGTH * sizeof (WCHAR)))
		return FALSE;

	for (SIZE_T i = 0; i < (hash->length / sizeof (WCHAR)); i++)
	{
		value = hash->buffer[i];

		if (value >= L'0' && value <= L'9')
			continue;

		if ((value >= L'a' && value <= L'f') || (value >= L'A' && value <= L'F'))
			continue;

		return FALSE;
	}

	return TRUE;
}

// routine's downloader retries a failed certificate check with SECURITY_FLAG_IGNORE_UNKNOWN_CA, which would let
// anybody able to present a self-signed certificate serve both the manifest and the installer we then run as
// admin. The update channel is fetched here instead: chain validation stays on, https is required and the
// response size is bounded.
_Success_ (return == ERROR_SUCCESS)
ULONG _app_http_openurl (
	_In_ HINTERNET hsession,
	_In_ PR_STRINGREF url,
	_Out_ LPHINTERNET hconnect_ptr,
	_Out_ LPHINTERNET hrequest_ptr,
	_Out_opt_ PULONG total_length_ptr
)
{
	R_URLPARTS url_parts = {0};
	HINTERNET hrequest = NULL;
	HINTERNET hconnect = NULL;
	ULONG status;

	*hconnect_ptr = NULL;
	*hrequest_ptr = NULL;

	if (total_length_ptr)
		*total_length_ptr = 0;

	if (!_r_inet_queryurlparts (url, PR_URLPARTS_SCHEME | PR_URLPARTS_HOST | PR_URLPARTS_PORT | PR_URLPARTS_PATH, &url_parts))
	{
		status = NtLastError ();

		goto CleanupExit;
	}

	if (url_parts.scheme != INTERNET_SCHEME_HTTPS)
	{
		status = ERROR_WINHTTP_INVALID_URL;

		goto CleanupExit;
	}

	hconnect = WinHttpConnect (hsession, url_parts.host->buffer, url_parts.port, 0);

	if (!hconnect)
	{
		status = NtLastError ();

		goto CleanupExit;
	}

	hrequest = WinHttpOpenRequest (hconnect, NULL, url_parts.path->buffer, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);

	if (!hrequest)
	{
		status = NtLastError ();

		goto CleanupExit;
	}

	WinHttpSetOption (hrequest, WINHTTP_OPTION_DISABLE_FEATURE, &(ULONG){WINHTTP_DISABLE_KEEP_ALIVE}, sizeof (ULONG));

	if (!WinHttpSendRequest (hrequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH, 0))
	{
		status = NtLastError ();

		goto CleanupExit;
	}

	if (!WinHttpReceiveResponse (hrequest, NULL))
	{
		status = NtLastError ();

		goto CleanupExit;
	}

	if (_r_inet_querystatuscode (hrequest) != HTTP_STATUS_OK)
	{
		status = ERROR_WINHTTP_INVALID_SERVER_RESPONSE;

		goto CleanupExit;
	}

	*hconnect_ptr = hconnect;
	*hrequest_ptr = hrequest;

	if (total_length_ptr)
		*total_length_ptr = _r_inet_querycontentlength (hrequest);

	status = ERROR_SUCCESS;

CleanupExit:

	if (status != ERROR_SUCCESS)
	{
		if (hrequest)
			_r_inet_close (hrequest);

		if (hconnect)
			_r_inet_close (hconnect);
	}

	_r_inet_destroyurlparts (&url_parts);

	return status;
}

_Success_ (NT_SUCCESS (return))
NTSTATUS _app_http_gettext (
	_In_ HINTERNET hsession,
	_In_ PR_STRINGREF url,
	_In_ ULONG maximum_length,
	_Out_ PR_STRING_PTR out_string
)
{
	R_STRINGBUILDER sb;
	HINTERNET hconnect;
	HINTERNET hrequest;
	PR_BYTE content_bytes;
	PR_STRING string = NULL;
	ULONG total_readed = 0;
	ULONG allocated_length;
	ULONG readed_length = 0;
	ULONG win_status;
	NTSTATUS status;

	*out_string = NULL;

	win_status = _app_http_openurl (hsession, url, &hconnect, &hrequest, NULL);

	if (win_status != ERROR_SUCCESS)
	{
		_r_log (LOG_LEVEL_WARNING, NULL, L"_app_http_openurl", (NTSTATUS)win_status, url->buffer);

		return STATUS_UNSUCCESSFUL;
	}

	_r_obj_initializestringbuilder (&sb, PR_SIZE_BUFFER);

	allocated_length = PR_SIZE_BUFFER;
	content_bytes = _r_obj_createbyte_ex (NULL, allocated_length);

	status = STATUS_SUCCESS;

	while (_r_inet_readrequest (hrequest, content_bytes->buffer, allocated_length, &readed_length, &total_readed))
	{
		// the manifest is a couple of lines of text, so a bigger response is either broken or hostile
		if (total_readed > maximum_length)
		{
			status = STATUS_BUFFER_OVERFLOW;

			break;
		}

		_r_obj_setbytelength_ex (content_bytes, readed_length, allocated_length);

		status = _r_str_multibyte2unicode (&content_bytes->sr, &string);

		if (!NT_SUCCESS (status))
			break;

		_r_obj_appendstringbuilder2 (&sb, &string->sr);

		_r_obj_dereference (string);
		string = NULL;
	}

	if (!total_readed && NT_SUCCESS (status))
		status = STATUS_DATA_ERROR;

	if (NT_SUCCESS (status))
		*out_string = _r_obj_finalstringbuilder (&sb);
	else
		_r_obj_deletestringbuilder (&sb);

	_r_obj_dereference (content_bytes);

	_r_inet_close (hrequest);
	_r_inet_close (hconnect);

	return status;
}

_Success_ (NT_SUCCESS (return))
NTSTATUS _app_http_getfile (
	_In_ HINTERNET hsession,
	_In_ PR_STRINGREF url,
	_In_ ULONG maximum_length,
	_In_ HANDLE hfile
)
{
	HINTERNET hconnect;
	HINTERNET hrequest;
	PR_BYTE content_bytes;
	ULONG total_readed = 0;
	ULONG content_length;
	ULONG allocated_length;
	ULONG readed_length = 0;
	ULONG win_status;
	NTSTATUS status;

	win_status = _app_http_openurl (hsession, url, &hconnect, &hrequest, &content_length);

	if (win_status != ERROR_SUCCESS)
	{
		_r_log (LOG_LEVEL_WARNING, NULL, L"_app_http_openurl", (NTSTATUS)win_status, url->buffer);

		return STATUS_UNSUCCESSFUL;
	}

	// refuse a server that announces more than an installer before a single byte is written (content length is 0 when it is not sent)
	if (content_length && (content_length > maximum_length))
	{
		_r_inet_close (hrequest);
		_r_inet_close (hconnect);

		return STATUS_DATA_ERROR;
	}

	_r_sys_setthreadexecutionstate (ES_SYSTEM_REQUIRED | ES_CONTINUOUS);

	allocated_length = PR_SIZE_BUFFER;
	content_bytes = _r_obj_createbyte_ex (NULL, allocated_length);

	status = STATUS_SUCCESS;

	while (_r_inet_readrequest (hrequest, content_bytes->buffer, allocated_length, &readed_length, &total_readed))
	{
		// a chunked response has no content length, so the limit is checked while writing as well
		if (total_readed > maximum_length)
		{
			status = STATUS_DATA_ERROR;

			break;
		}

		status = _r_fs_writefile (hfile, content_bytes->buffer, readed_length);

		if (!NT_SUCCESS (status))
			break;
	}

	_r_sys_setthreadexecutionstate (ES_CONTINUOUS);

	_r_obj_dereference (content_bytes);

	_r_inet_close (hrequest);
	_r_inet_close (hconnect);

	return status;
}

_Success_ (NT_SUCCESS (return))
NTSTATUS _app_update_query (
	_In_ HINTERNET hsession,
	_Out_ PR_STRING_PTR out_version,
	_Out_ PR_STRING_PTR out_url,
	_Out_ PR_STRING_PTR out_hash
)
{
	PR_HASHTABLE string_table = NULL;
	PR_STRING value = NULL;
	PR_STRING manifest_url;
	PR_STRING manifest_text = NULL;
	PR_STRING version;
	PR_STRING url;
	PR_STRING hash;
	R_STRINGREF key_ref;
	R_STRINGREF version_ref;
	R_STRINGREF url_ref;
	R_STRINGREF hash_ref;
	R_STRINGREF remaining_ref;
	NTSTATUS status;

	*out_version = NULL;
	*out_url = NULL;
	*out_hash = NULL;

	manifest_url = _r_obj_createstring (UPDATE_MANIFEST_URL);

	status = _app_http_gettext (hsession, &manifest_url->sr, UPDATE_MAXIMUM_MANIFEST_SIZE, &manifest_text);

	if (!NT_SUCCESS (status))
		goto CleanupExit;

	// manifest format: "name=version|url|hash;name=version|url|hash;"
	string_table = _r_str_unserialize (&manifest_text->sr, L';', L'=');

	if (!string_table)
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	_r_obj_initializestringref (&key_ref, APP_NAME_SHORT);

	value = _r_obj_findhashtablepointer (string_table, _r_str_gethash2 (&key_ref, TRUE));

	if (!value || !_r_str_splitatchar (&value->sr, L'|', &version_ref, &remaining_ref))
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	if (!_r_str_splitatchar (&remaining_ref, L'|', &url_ref, &remaining_ref))
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	if (!_app_update_isvalidversion (&version_ref))
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	// the download is executed with admin rights - only accept assets of this repository
	if (!_r_str_isstartswith2 (&url_ref, UPDATE_RELEASE_URL_PREFIX, TRUE))
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	// the installer is started with admin rights, so a manifest without its sha256 is refused instead of trusted
	// the hash is the last field of the record: asking for another separator here would reject every valid manifest
	hash_ref = remaining_ref;

	if (!_app_update_isvalidhash (&hash_ref))
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	hash = _r_obj_createstring2 (&hash_ref);

	version = _r_obj_createstring2 (&version_ref);
	url = _r_obj_createstring2 (&url_ref);

	*out_version = version;
	*out_url = url;
	*out_hash = hash;

	status = STATUS_SUCCESS;

CleanupExit:

	if (value)
		_r_obj_dereference (value);

	if (string_table)
		_r_obj_dereference (string_table);

	if (manifest_text)
		_r_obj_dereference (manifest_text);

	_r_obj_dereference (manifest_url);

	return status;
}

_Success_ (NT_SUCCESS (return))
NTSTATUS _app_update_downloadinstaller (
	_In_ HINTERNET hsession,
	_In_ PR_STRING version,
	_In_ PR_STRING url,
	_In_ PR_STRING hash,
	_Out_ PR_STRING_PTR out_path
)
{
	WCHAR token[0x11];
	PR_STRING temp_directory;
	PR_STRING path;
	PR_STRING file_hash = NULL;
	CHAR signature[2];
	LARGE_INTEGER new_pos = {0};
	LONG64 file_size;
	HANDLE hfile = NULL;
	ULONG_PTR readed_length;
	NTSTATUS status;

	*out_path = NULL;

	temp_directory = _r_sys_gettempdirectory ();

	if (!temp_directory)
		return STATUS_NOT_FOUND;

	// temp is writable by every process of the user, so the name must not be guessable - an attacker could pre-create it and swap the payload before the hash is read
	_r_str_generaterandom (token, RTL_NUMBER_OF (token), FALSE);

	path = _r_format_string (L"%s\\%s-%s-%s-setup.exe", temp_directory->buffer, APP_NAME_SHORT, version->buffer, token);

	_r_obj_dereference (temp_directory);

	if (!path)
		return STATUS_NO_MEMORY;

	status = _r_fs_createfile (
		&path->sr,
		FILE_OVERWRITE_IF,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		FILE_ATTRIBUTE_NORMAL,
		FILE_RANDOM_ACCESS,
		FALSE,
		NULL,
		&hfile
	);

	if (!NT_SUCCESS (status))
		goto CleanupExit;

	status = _app_http_getfile (hsession, &url->sr, UPDATE_MAXIMUM_FILE_SIZE, hfile);

	if (!NT_SUCCESS (status))
		goto CleanupExit;

	// verify the payload really is an executable and not an error response
	file_size = 0;

	status = _r_fs_getsize2 (NULL, hfile, &file_size);

	if (status != STATUS_SUCCESS || file_size < UPDATE_MINIMUM_FILE_SIZE)
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	// checking the bytes we wrote through this handle - re-opening by path would leave a window for swapping the file
	new_pos.QuadPart = 0;

	status = _r_fs_setpos (hfile, &new_pos);

	if (status != STATUS_SUCCESS)
		goto CleanupExit;

	readed_length = 0;

	status = _r_fs_readfile (hfile, signature, sizeof (signature), &readed_length);

	if (status != STATUS_SUCCESS || readed_length != sizeof (signature) || signature[0] != 'M' || signature[1] != 'Z')
	{
		status = STATUS_DATA_ERROR;
		goto CleanupExit;
	}

	new_pos.QuadPart = 0;

	status = _r_fs_setpos (hfile, &new_pos);

	if (status != STATUS_SUCCESS)
		goto CleanupExit;

	// an MZ header is trivial to forge - refuse to run a payload not matching the published hash
	status = _r_crypt_getfilehash (UPDATE_HASH_ALGORITHM, NULL, hfile, &file_hash);

	if (NT_SUCCESS (status))
	{
		if (_r_str_compare (file_hash->buffer, hash->buffer, TRUE) != 0)
			status = STATUS_DATA_ERROR;

		_r_obj_dereference (file_hash);
		file_hash = NULL;
	}

CleanupExit:

	if (hfile)
		NtClose (hfile);

	if (status == STATUS_SUCCESS)
	{
		*out_path = path;
	}
	else
	{
		_r_fs_deletefile (&path->sr, NULL);

		_r_obj_dereference (path);
	}

	return status;
}

// the main window can be destroyed while this thread runs, and a recycled handle would parent the dialog onto an unrelated window
HWND _app_update_parent (
	_In_ HWND hwnd
)
{
	return IsWindow (hwnd) ? hwnd : NULL;
}

NTSTATUS NTAPI _app_update_thread (
	_In_ PVOID arglist
)
{
	APP_UPDATE_TASK task;
	PR_STRING useragent;
	PR_STRING proxy;
	PR_STRING version = NULL;
	PR_STRING url = NULL;
	PR_STRING hash = NULL;
	PR_STRING path = NULL;
	R_STRINGREF current_ref;
	HINTERNET hsession = NULL;
	BOOLEAN is_launched = FALSE;
	NTSTATUS status;

	task = *(PAPP_UPDATE_TASK)arglist;

	_r_mem_free (arglist);

	useragent = _r_app_getuseragent ();
	proxy = _r_app_getproxyconfiguration ();

	hsession = _r_inet_createsession (useragent, proxy);

	// useragent is a cached object owned by routine, must not be released here
	if (proxy)
		_r_obj_dereference (proxy);

	if (!hsession)
	{
		status = STATUS_UNSUCCESSFUL;

		if (!task.is_silent)
			_r_show_errormessage (_app_update_parent (task.hwnd), L"Could not create internet session!", NtLastError (), NULL, ET_WINHTTP);

		goto CleanupExit;
	}

	status = _app_update_query (hsession, &version, &url, &hash);

	if (!NT_SUCCESS (status))
	{
		// a rejected manifest means the answer did arrive but is not usable, so do not report it as a connection failure
		if (status == STATUS_DATA_ERROR)
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"_app_update_query", status, L"The update manifest is not valid for this build.");

			if (!task.is_silent)
				_r_show_errormessage (_app_update_parent (task.hwnd), L"The update manifest is not valid.", status, NULL, ET_NATIVE);
		}
		else
		{
			if (!task.is_silent)
				_r_show_errormessage (_app_update_parent (task.hwnd), _r_locale_getstring (IDS_UPDATE_ERROR), status, NULL, ET_WINHTTP);
		}

		goto CleanupExit;
	}

	_r_obj_initializestringref (&current_ref, APP_VERSION);

	if (_r_str_versioncompare (&current_ref, &version->sr) != -1)
	{
		if (!task.is_silent)
			_r_show_message (_app_update_parent (task.hwnd), MB_OK | MB_ICONINFORMATION, _r_app_getname (), _r_locale_getstring (IDS_UPDATE_NO));

		goto CleanupExit;
	}

	if (!_r_show_confirmmessage (_app_update_parent (task.hwnd), _r_app_getname (), _r_locale_getstring (IDS_UPDATE_YES), NULL, FALSE))
	{
		status = STATUS_CANCELLED;

		goto CleanupExit;
	}

	status = _app_update_downloadinstaller (hsession, version, url, hash, &path);

	if (!NT_SUCCESS (status))
	{
		// the payload was refused by its size or its sha256: it must never run and never look like a network error
		if (status == STATUS_DATA_ERROR)
		{
			_r_log (LOG_LEVEL_ERROR, NULL, L"_app_update_downloadinstaller", status, L"The downloaded installer did not match the published hash or size.");

			_r_show_errormessage (_app_update_parent (task.hwnd), L"The downloaded file failed verification.", status, url->buffer, ET_NATIVE);
		}
		else
		{
			_r_show_errormessage (_app_update_parent (task.hwnd), _r_locale_getstring (IDS_UPDATE_ERROR), status, url->buffer, ET_WINHTTP);
		}

		goto CleanupExit;
	}

	status = _r_sys_runasadmin (path->buffer, NULL, NULL);

	is_launched = (status == STATUS_SUCCESS);

	if (is_launched)
	{
		// leave the way free for the installer (the window is gone when the user already quit)
		if (IsWindow (task.hwnd))
			PostMessageW (task.hwnd, WM_COMMAND, IDM_EXIT, 0);
	}
	else
	{
		_r_show_errormessage (_app_update_parent (task.hwnd), L"Could not start installer!", status, path->buffer, ET_NATIVE);
	}

CleanupExit:

	if (hsession)
		_r_inet_close (hsession);

	_r_config_setlong64_ex (L"CheckUpdatesLast", _r_unixtime_now (), NULL);

	if (version)
		_r_obj_dereference (version);

	if (url)
		_r_obj_dereference (url);

	if (hash)
		_r_obj_dereference (hash);

	if (path)
	{
		if (!is_launched)
			_r_fs_deletefile (&path->sr, NULL);

		_r_obj_dereference (path);
	}

	_InterlockedExchange (&update_isbusy, 0);

	return STATUS_SUCCESS;
}

_Success_ (return)
BOOLEAN _app_update_check (
	_In_ HWND hwnd,
	_In_ BOOLEAN is_silent
)
{
	PAPP_UPDATE_TASK task;
	HANDLE hthread;
	NTSTATUS status;

	if (_InterlockedCompareExchange (&update_isbusy, 1, 0) != 0)
		return FALSE;

	task = _r_mem_allocate (sizeof (APP_UPDATE_TASK));

	task->hwnd = hwnd;
	task->is_silent = is_silent;

	status = _r_sys_createthread (&hthread, NtCurrentProcess (), &_app_update_thread, task, NULL, L"UpdateThread");

	if (!NT_SUCCESS (status))
	{
		_r_mem_free (task);

		_InterlockedExchange (&update_isbusy, 0);

		_r_show_errormessage (hwnd, L"Could not create thread!", status, NULL, ET_NATIVE);

		return FALSE;
	}

	NtResumeThread (hthread, NULL);

	NtClose (hthread);

	return TRUE;
}

VOID _app_initialize (
	_In_opt_ HWND hwnd
)
{
	ULONG privileges[] = {
		SE_PROF_SINGLE_PROCESS_PRIVILEGE,
		SE_INCREASE_QUOTA_PRIVILEGE,
	};
	NTSTATUS status;

	if (_r_sys_iselevated ())
	{
		status = _r_sys_setprocessprivilege (NtCurrentProcess (), privileges, RTL_NUMBER_OF (privileges), TRUE);

		// STATUS_NOT_ALL_ASSIGNED is success, but the cleaning that needs the missing privilege silently fails
		if (status != STATUS_SUCCESS)
			_r_log (LOG_LEVEL_WARNING, NULL, L"_r_sys_setprocessprivilege", status, L"SeProfileSingleProcessPrivilege");
	}

	if (!hwnd)
		return;

	_app_ui_initialize (hwnd);

	_app_ui_setsize (hwnd, _r_dc_getwindowdpi (hwnd));

	_app_ui_layoutcontrols (hwnd, _r_dc_getwindowdpi (hwnd));

	_app_ui_updatemetrics (NULL);

	// settings
	_r_settings_addpage (IDD_SETTINGS_GENERAL, IDS_SETTINGS_GENERAL);
	_r_settings_addpage (IDD_SETTINGS_MEMORY, IDS_SETTINGS_MEMORY);
	_r_settings_addpage (IDD_SETTINGS_APPEARANCE, IDS_SETTINGS_APPEARANCE);
	_r_settings_addpage (IDD_SETTINGS_TRAY, IDS_SETTINGS_TRAY);
	_r_settings_addpage (IDD_SETTINGS_ADVANCED, IDS_TITLE_ADVANCED);
}

VOID _app_showaboutmessage (
	_In_opt_ HWND hwnd
)
{
	static BOOLEAN is_opened = FALSE;

	TASKDIALOGCONFIG tdc = {0};
	WCHAR buffer[0x200];

	// routine's about dialog links the upstream repository, so this one is built from the fork urls
	if (is_opened)
		return;

	is_opened = TRUE;

	_r_str_printf (
		buffer,
		RTL_NUMBER_OF (buffer),
		L"Version %s %s, %d-bit (Unicode)\r\n%s\r\n\r\n<a href=\"%s\">%s</a>",
		_r_app_getversion (),
		_r_app_getversiontype (),
		_r_app_getarch (),
		_r_app_getcopyright (),
		APP_WEBSITE_URL,
		APP_WEBSITE_URL
	);

	tdc.cbSize = sizeof (tdc);
	tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
	tdc.hwndParent = hwnd;
	tdc.hInstance = _r_sys_getimagebase ();
	tdc.dwCommonButtons = TDCBF_OK_BUTTON;
	tdc.pszWindowTitle = _r_locale_getstring (IDS_ABOUT);
	tdc.pszMainInstruction = _r_app_getname ();
	tdc.pszMainIcon = MAKEINTRESOURCEW (IDI_MAIN);
	tdc.pszContent = buffer;
	tdc.pfCallback = &_r_msg_callback;
	tdc.pszFooterIcon = TD_INFORMATION_ICON;
	tdc.pszFooter = APP_ABOUT_LICENSE;

	_r_msg_taskdialog (&tdc, NULL, NULL, NULL);

	is_opened = FALSE;
}

INT_PTR CALLBACK DlgProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			_r_app_sethwnd (hwnd); // HACK!!!

			_app_initialize (hwnd);

			// the dialog template has no default button, so enter would do nothing without this
			SendMessageW (hwnd, DM_SETDEFID, IDC_CLEAN, 0);

			SetTimer (hwnd, UID, TIMER, &_app_timercallback);

			break;
		}

		case WM_CLOSE:
		{
			_r_wnd_toggle (hwnd, FALSE);

			return TRUE;
		}

		case WM_QUERYENDSESSION:
		{
			return TRUE;
		}

		case WM_ENDSESSION:
		{
			if (wparam)
				DestroyWindow (hwnd);

			break;
		}

		case WM_DESTROY:
		{
			KillTimer (hwnd, UID);

			_r_tray_destroy (hwnd, &GUID_TrayIcon);

			_app_ui_fontsfree ();

			// the tray does not own this icon, it is the one created by _app_iconcreate
			if (config.hicon)
			{
				DestroyIcon (config.hicon);

				config.hicon = NULL;
			}

			SAFE_DELETE_OBJECT (config.hbitmap_mask);
			SAFE_DELETE_OBJECT (config.hbitmap);
			SAFE_DELETE_OBJECT (config.hfont);

			SAFE_DELETE_DC (config.hdc_mask);
			SAFE_DELETE_DC (config.hdc);

			PostQuitMessage (0);

			break;
		}

		case RM_INITIALIZE:
		{
			// the window is created at the size of the dialog template, the pixel size is pinned again here
			_app_ui_setsize (hwnd, _r_dc_getwindowdpi (hwnd));

			_app_ui_layoutcontrols (hwnd, _r_dc_getwindowdpi (hwnd));

			_app_ui_updatemetrics (NULL);

			_app_trayiconrefresh (hwnd);

			break;
		}

		case RM_INITIALIZE_POST:
		{
			if (_r_sys_iselevated ())
				_app_hotkeyinit (hwnd);

			if (_app_update_isready ())
				_app_update_check (hwnd, TRUE);

			break;
		}

		case RM_TASKBARCREATED:
		{
			_app_trayiconrefresh (hwnd);

			break;
		}

		case RM_LOCALIZE:
		{
			_app_ui_localize (hwnd);

			// the cached strings are locale dependent, so drop them and format them again
			RtlZeroMemory (config.tooltip_prev, sizeof (config.tooltip_prev));

			config.ui_hasdata = FALSE;

			_app_ui_updatemetrics (NULL);

			_app_ui_invalidate (hwnd);

			break;
		}

		case WM_DPICHANGED:
		{
			RECT* rect = (RECT*)lparam;

			_app_iconinit (_r_dc_gettaskbardpi ());
			_app_iconredraw (hwnd);

			_app_ui_fontscreate (LOWORD (wparam));

			_app_ui_setsize (hwnd, LOWORD (wparam));

			_app_ui_layoutcontrols (hwnd, LOWORD (wparam));

			// routine's dpi handler is a stub, so the suggested rectangle is applied here
			if (rect)
				SetWindowPos (hwnd, NULL, rect->left, rect->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

			_app_ui_invalidate (hwnd);

			break;
		}

		case WM_SETTINGCHANGE:
		{
			HWND hsettings;
			R_STRINGREF sr;
			LPWSTR type;

			// routine refreshed the immersive colour policy for us, the windows are repainted to follow it
			type = (LPWSTR)lparam;

			if (!type)
				break;

			_r_obj_initializestringref (&sr, type);

			if (_r_str_isequal2 (&sr, L"ImmersiveColorSet", TRUE))
			{
				_r_theme_initialize (hwnd, _r_theme_isenabled ());

				hsettings = _r_settings_getwindow ();

				if (hsettings)
					_r_theme_initialize (hsettings, _r_theme_isenabled ());

				_app_ui_invalidate (hwnd);
			}

			break;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc;

			hdc = BeginPaint (hwnd, &ps);

			if (hdc)
				_app_ui_paint (hwnd, hdc);

			EndPaint (hwnd, &ps);

			break;
		}

		case WM_ERASEBKGND:
		{
			// the whole client area is painted by WM_PAINT, letting the default handler erase it would flicker
			return TRUE;
		}

		case WM_DRAWITEM:
		{
			LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lparam;

			if (dis->CtlType == ODT_BUTTON)
				_app_ui_drawbutton (dis);

			return TRUE;
		}

		case WM_CONTEXTMENU:
		{
			POINT point = {GET_X_LPARAM (lparam), GET_Y_LPARAM (lparam)};

			if (point.x == -1 && point.y == -1)
				_app_ui_showmainmenu (hwnd, NULL);
			else
				_app_ui_showmainmenu (hwnd, &point);

			return TRUE;
		}

		case WM_HOTKEY:
		{
			if (wparam == UID)
				_app_memoryclean (hwnd, SOURCE_HOTKEY, 0);

			break;
		}

		case WM_SHOWWINDOW:
		{
			if (wparam)
				_app_iconredraw (hwnd);

			break;
		}

		case RM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case NIN_KEYSELECT:
				{
					if (GetForegroundWindow () != hwnd)
						_r_wnd_toggle (hwnd, TRUE);

					break;
				}

				case WM_LBUTTONDOWN:
				case WM_MBUTTONDOWN:
				{
					LONG action;

					if (LOWORD (lparam) == WM_MBUTTONDOWN)
					{
						action = _r_config_getlong_ex (L"TrayActionMc", 1, NULL);
					}
					else
					{
						action = _r_config_getlong_ex (L"TrayActionDc", 0, NULL);
					}

					switch (action)
					{
						case 1:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL, 0);
							break;
						}

						case 2:
						{
							_r_sys_createprocess (L"taskmgr.exe", NULL, NULL, FALSE);
							break;
						}

						default:
						{
							_r_wnd_toggle (hwnd, FALSE);
							break;
						}
					}

					SetForegroundWindow (hwnd);

					break;
				}

				case WM_CONTEXTMENU:
				{
					HMENU hmenu, hsubmenu, hsubmenu_interval, hsubmenu_limit, hsubmenu_region;
					ULONG mask;

					SetForegroundWindow (hwnd); // don't touch

					hmenu = LoadMenuW (_r_sys_getimagebase (), MAKEINTRESOURCEW (IDM_TRAY));

					if (!hmenu)
						break;

					hsubmenu = GetSubMenu (hmenu, 0);

					if (!hsubmenu)
					{
						// the popup cannot be shown, but the menu we just loaded still belongs to us
						DestroyMenu (hmenu);

						break;
					}

					hsubmenu_region = GetSubMenu (hsubmenu, TRAY_SUBMENU_1);
					hsubmenu_limit = GetSubMenu (hsubmenu, TRAY_SUBMENU_2);
					hsubmenu_interval = GetSubMenu (hsubmenu, TRAY_SUBMENU_3);

					// localize
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_SHOW, FALSE, _r_locale_getstring (IDS_TRAY_SHOW));
					_r_menu_setitemtextformat (hsubmenu, IDM_TRAY_CLEAN, FALSE, L"%s...", _r_locale_getstring (IDS_CLEAN));
					_r_menu_setitemtext (hsubmenu, TRAY_SUBMENU_1, TRUE, _r_locale_getstring (IDS_TRAY_POPUP_1));
					_r_menu_setitemtext (hsubmenu, TRAY_SUBMENU_2, TRUE, _r_locale_getstring (IDS_TRAY_POPUP_2));
					_r_menu_setitemtext (hsubmenu, TRAY_SUBMENU_3, TRUE, _r_locale_getstring (IDS_TRAY_POPUP_3));
					_r_menu_setitemtextformat (hsubmenu, IDM_TRAY_SETTINGS, FALSE, L"%s...", _r_locale_getstring (IDS_SETTINGS));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_ABOUT, FALSE, _r_locale_getstring (IDS_ABOUT));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));

					// configure submenu #1
					if (hsubmenu_region)
					{
						mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

						_r_menu_setitemtext (hsubmenu_region, IDM_WORKINGSET_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_WORKINGSET));
						_r_menu_setitemtext (hsubmenu_region, IDM_SYSTEMFILECACHE_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE));
						_r_menu_setitemtext (hsubmenu_region, IDM_MODIFIEDFILECACHE_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE));
						_r_menu_setitemtextformat (hsubmenu_region, IDM_MODIFIEDLIST_CHK, FALSE, L"%s*", _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST));
						_r_menu_setitemtextformat (hsubmenu_region, IDM_STANDBYLIST_CHK, FALSE, L"%s*", _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));
						_r_menu_setitemtext (hsubmenu_region, IDM_STANDBYLISTPRIORITY0_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0));
						_r_menu_setitemtext (hsubmenu_region, IDM_REGISTRYCACHE_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE));
						_r_menu_setitemtext (hsubmenu_region, IDM_COMBINEMEMORYLISTS_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS));

						_r_menu_checkitem (hsubmenu_region, IDM_WORKINGSET_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET);
						_r_menu_checkitem (hsubmenu_region, IDM_SYSTEMFILECACHE_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE);
						_r_menu_checkitem (hsubmenu_region, IDM_MODIFIEDLIST_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST);
						_r_menu_checkitem (hsubmenu_region, IDM_STANDBYLIST_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST);
						_r_menu_checkitem (hsubmenu_region, IDM_STANDBYLISTPRIORITY0_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST);
						_r_menu_checkitem (hsubmenu_region, IDM_MODIFIEDFILECACHE_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE);
						_r_menu_checkitem (hsubmenu_region, IDM_REGISTRYCACHE_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE);
						_r_menu_checkitem (hsubmenu_region, IDM_COMBINEMEMORYLISTS_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS);

						if (!_r_sys_iselevated ())
						{
							_r_menu_enableitem (hsubmenu_region, IDM_WORKINGSET_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_SYSTEMFILECACHE_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_MODIFIEDFILECACHE_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_MODIFIEDLIST_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_STANDBYLIST_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_STANDBYLISTPRIORITY0_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_REGISTRYCACHE_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_COMBINEMEMORYLISTS_CHK, FALSE, FALSE);
						}

					}

					// configure submenu #2
					if (hsubmenu_limit)
					{
						_app_generate_menu (
							hsubmenu_limit,
							IDX_TRAY_POPUP_1,
							limits_arr,
							RTL_NUMBER_OF (limits_arr),
							NULL,
							_app_getlimitvalue (),
							100,
							_r_config_getboolean_ex (L"AutoreductEnable", FALSE, NULL)
						);
					}

					// configure submenu #3
					if (hsubmenu_interval)
					{
						_app_generate_menu (
							hsubmenu_interval,
							IDX_TRAY_POPUP_2,
							intervals_arr,
							RTL_NUMBER_OF (intervals_arr),
							_r_locale_getstring (IDS_MINUTES),
							_app_getintervalvalue (),
							1440,
							_r_config_getboolean_ex (L"AutoreductIntervalEnable", FALSE, NULL)
						);
					}

					_r_menu_popup (hsubmenu, hwnd, NULL, TRUE);

					DestroyMenu (hmenu);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);
			INT notify_code = HIWORD (wparam);

			if (notify_code == 0 && ctrl_id >= IDX_LANGUAGE && ctrl_id <= IDX_LANGUAGE + (INT)(INT_PTR)_r_locale_getcount () + 1)
			{
				_r_locale_apply (NULL, ctrl_id, IDX_LANGUAGE);

				return FALSE;
			}
			else if ((ctrl_id >= IDX_TRAY_POPUP_1 && ctrl_id <= IDX_TRAY_POPUP_1 + (INT)RTL_NUMBER_OF (limits_arr) - 1))
			{
				ULONG_PTR idx = (ULONG_PTR)(ctrl_id - IDX_TRAY_POPUP_1);

				_r_config_setboolean_ex (L"AutoreductEnable", TRUE, NULL);
				_r_config_setlong_ex (L"AutoreductValue", limits_arr[idx], NULL);

				return FALSE;
			}
			else if ((ctrl_id >= IDX_TRAY_POPUP_2 && ctrl_id <= IDX_TRAY_POPUP_2 + (INT)RTL_NUMBER_OF (intervals_arr) - 1))
			{
				ULONG_PTR idx = (ULONG_PTR)(ctrl_id - IDX_TRAY_POPUP_2);

				_r_config_setboolean_ex (L"AutoreductIntervalEnable", TRUE, NULL);
				_r_config_setlong_ex (L"AutoreductIntervalValue", intervals_arr[idx], NULL);

				return FALSE;
			}

			switch (ctrl_id)
			{
				case IDM_ALWAYSONTOP_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"AlwaysOnTop", FALSE);

					_r_wnd_top (hwnd, new_val);

					break;
				}

				case IDM_STARTMINIMIZED_CHK:
				{
					_app_config_invertboolean (L"IsStartMinimized", FALSE);

					break;
				}

				case IDM_REDUCTCONFIRMATION_CHK:
				{
					_app_config_invertboolean (L"IsShowReductConfirmation", TRUE);

					break;
				}

				case IDM_LOADONSTARTUP_CHK:
				{
					_r_autorun_enable (hwnd, !_r_autorun_isenabled ());

					break;
				}

				case IDM_USEDARKTHEME:
				{
					HWND hsettings;
					BOOLEAN is_enabled = !_r_theme_isenabled ();

					_r_theme_enable (hwnd, is_enabled);

					// the settings page is a separate window, routine does not repaint it for us
					hsettings = _r_settings_getwindow ();

					if (hsettings)
						_r_theme_initialize (hsettings, is_enabled);

					break;
				}

				case IDM_SKIPUACWARNING_CHK:
				{
					_r_skipuac_enable (hwnd, !_r_skipuac_isenabled ());

					break;
				}

				case IDM_CHECKUPDATES_CHK:
				{
					BOOLEAN new_val = !_app_update_isenabled ();

					_app_update_enable (new_val);

					break;
				}

				case IDM_WORKINGSET_CHK:
				case IDM_SYSTEMFILECACHE_CHK:
				case IDM_MODIFIEDFILECACHE_CHK:
				case IDM_MODIFIEDLIST_CHK:
				case IDM_STANDBYLIST_CHK:
				case IDM_STANDBYLISTPRIORITY0_CHK:
				case IDM_REGISTRYCACHE_CHK:
				case IDM_COMBINEMEMORYLISTS_CHK:
				{
					ULONG mask, new_mask = 0;

					mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

					switch (ctrl_id)
					{
						case IDM_WORKINGSET_CHK:
						{
							new_mask = REDUCT_WORKINGSET;
							break;
						}

						case IDM_SYSTEMFILECACHE_CHK:
						{
							new_mask = REDUCT_SYSTEMFILECACHE;
							break;
						}

						case IDM_MODIFIEDFILECACHE_CHK:
						{
							new_mask = REDUCT_MODIFIEDFILECACHE;
							break;
						}

						case IDM_MODIFIEDLIST_CHK:
						{
							new_mask = REDUCT_MODIFIEDLIST;
							break;
						}

						case IDM_STANDBYLIST_CHK:
						{
							new_mask = REDUCT_STANDBYLIST;
							break;
						}

						case IDM_STANDBYLISTPRIORITY0_CHK:
						{
							new_mask = REDUCT_STANDBYPRIORITY0LIST;
							break;
						}

						case IDM_REGISTRYCACHE_CHK:
						{
							new_mask = REDUCT_REGISTRYCACHE;
							break;
						}

						case IDM_COMBINEMEMORYLISTS_CHK:
						{
							new_mask = REDUCT_COMBINEMEMORYLISTS;
							break;
						}

						default:
						{
							return FALSE;
						}
					}

					if ((ctrl_id == IDM_STANDBYLIST_CHK && !(mask & REDUCT_STANDBYLIST)) || (ctrl_id == IDM_MODIFIEDLIST_CHK && !(mask & REDUCT_MODIFIEDLIST)))
					{
						if (!_r_show_confirmmessage (hwnd, _r_app_getname (), _r_locale_getstring (IDS_QUESTION_WARNING), L"IsShowWarningConfirmation", FALSE))
							return FALSE;
					}

					_r_config_setulong_ex (L"ReductMask2", (mask & new_mask) != 0 ? (mask & ~new_mask) : (mask | new_mask), NULL);

					break;
				}

				case IDM_CLEAN_WORKINGSET:
				case IDM_CLEAN_SYSTEMFILECACHE:
				case IDM_CLEAN_MODIFIEDFILECACHE:
				case IDM_CLEAN_MODIFIEDLIST:
				case IDM_CLEAN_STANDBYLIST:
				case IDM_CLEAN_STANDBYLISTPRIORITY0:
				case IDM_CLEAN_REGISTRYCACHE:
				case IDM_CLEAN_COMBINEMEMORYLISTS:
				{
					switch (ctrl_id)
					{
						case IDM_CLEAN_WORKINGSET:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_WORKINGSET);
							break;
						}

						case IDM_CLEAN_SYSTEMFILECACHE:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_SYSTEMFILECACHE);
							break;
						}

						case IDM_CLEAN_MODIFIEDFILECACHE:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_MODIFIEDFILECACHE);
							break;
						}

						case IDM_CLEAN_MODIFIEDLIST:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_MODIFIEDLIST);
							break;
						}

						case IDM_CLEAN_STANDBYLIST:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_STANDBYLIST);
							break;
						}

						case IDM_CLEAN_STANDBYLISTPRIORITY0:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_STANDBYPRIORITY0LIST);
							break;
						}

						case IDM_CLEAN_REGISTRYCACHE:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_REGISTRYCACHE);
							break;
						}

						case IDM_CLEAN_COMBINEMEMORYLISTS:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL_REGION, REDUCT_COMBINEMEMORYLISTS);
							break;
						}

						default:
						{
							return FALSE;
						}
					}

					break;
				}

				case IDM_TRAY_DISABLE_1:
				{
					_r_config_setboolean_ex (L"AutoreductEnable", FALSE, NULL);
					break;
				}

				case IDM_TRAY_DISABLE_2:
				{
					_r_config_setboolean_ex (L"AutoreductIntervalEnable", FALSE, NULL);
					break;
				}

				case IDM_SETTINGS:
				case IDM_TRAY_SETTINGS:
				case IDC_SETTINGS:
				{
					_r_settings_createwindow (hwnd, &SettingsProc, 0);
					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case IDC_CLEANMENU:
				{
					_app_ui_showcleanmenu (hwnd);

					break;
				}

				case IDC_MAINMENU:
				{
					_app_ui_showmainmenu (hwnd, NULL);

					break;
				}

				case IDOK: // process Enter key
				case IDC_CLEAN:
				case IDM_TRAY_CLEAN:
				{
					if (_r_sys_iselevated ())
					{
						_app_memoryclean (hwnd, SOURCE_MANUAL, 0);
					}
					else
					{
						_app_cleanpending_set (0);

						if (_r_app_runasadmin ())
						{
							DestroyWindow (hwnd);
						}
						else
						{
							_r_config_setulong_ex (L"CleanPendingMask", 0, NULL);

							_r_show_message (hwnd, MB_OK | MB_ICONSTOP, _r_app_getname (), _r_locale_getstring (IDS_STATUS_NOPRIVILEGES));
						}
					}

					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					_r_shell_opendefault (APP_WEBSITE_URL);
					break;
				}

				case IDM_CHECKUPDATES:
				{
					_app_update_check (hwnd, FALSE);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					_app_showaboutmessage (hwnd);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

BOOLEAN NTAPI _app_parseargs (
	_In_ R_CMDLINE_INFO_CLASS type
)
{
	PR_STRING clean_args;
	ULONG mask = 0;

	switch (type)
	{
		case CmdlineClean:
		{
			_r_sys_getopt (_r_sys_getcommandline (), L"clean", &clean_args);

			if (clean_args)
			{
				if (_r_str_isequal2 (&clean_args->sr, L"full", TRUE))
					mask = REDUCT_MASK_ALL;

				_r_obj_dereference (clean_args);
			}

			if (mask == 0)
				mask = REDUCT_MASK_DEFAULT;

			_app_initialize (NULL);

			_app_memoryclean (NULL, SOURCE_CMDLINE, mask);

			return TRUE;
		}

		case CmdlineHelp:
		{
			_r_show_message (NULL, MB_OK | MB_ICONINFORMATION, NULL, L"Available options for memreduct.exe:\r\n\r\n-clean - clear default memory regions\r\n" "-clean:full - clear all memory regions");

			cmdline_exit_code = ERROR_SUCCESS;

			return TRUE;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (
	_In_ HINSTANCE hinst,
	_In_opt_ HINSTANCE prev_hinst,
	_In_ LPWSTR cmdline,
	_In_ INT show_cmd
)
{
	HWND hwnd;

	if (!_r_app_initialize (&_app_parseargs))
		return cmdline_exit_code;

	hwnd = _r_app_createwindow (hinst, MAKEINTRESOURCEW (IDD_MAIN), MAKEINTRESOURCEW (IDI_MAIN), &DlgProc);

	if (!hwnd)
		return ERROR_APP_INIT_FAILURE;

	return _r_wnd_message_callback (hwnd, MAKEINTRESOURCEW (IDA_MAIN));
}
