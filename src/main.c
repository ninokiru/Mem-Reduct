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

BOOLEAN _app_listviewsettext (
	_In_ HWND hwnd,
	_In_ INT item_id,
	_In_ INT subitem_id,
	_In_ LPWSTR text
)
{
	// pushing an unchanged string still repaints the row, so remember what the list already shows
	if (_r_str_compare (text, config.item_prev[item_id], FALSE) == 0)
		return FALSE;

	_r_str_copy (config.item_prev[item_id], RTL_NUMBER_OF (config.item_prev[item_id]), text);

	_r_listview_setitem (hwnd, IDC_LISTVIEW, item_id, subitem_id, text, I_DEFAULT, I_DEFAULT, I_DEFAULT);

	return TRUE;
}

VOID CALLBACK _app_timercallback (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ UINT_PTR id_event,
	_In_ ULONG time
)
{
	R_MEMORY_INFO mem_info;
	WCHAR buffer[0x80];
	WCHAR tooltip[0x80];
	HICON hicon = NULL;
	HICON hicon_prev = NULL;
	LONG64 timestamp;
	ULONG limit_value;
	ULONG percent;
	BOOLEAN is_clean = FALSE;
	BOOLEAN is_redraw = FALSE;

	if (id_event != UID)
		return;

	_app_getmemoryinfo (&mem_info);

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

	// set item lparam information, the colour indication reads it back on every draw
	for (INT i = 0; i < _r_listview_getitemcount (hwnd, IDC_LISTVIEW); i++)
	{
		if (i < 3)
		{
			percent = mem_info.physical_memory.percent;
		}
		else if (i < 6)
		{
			percent = mem_info.page_file.percent;
		}
		else if (i < 9)
		{
			percent = mem_info.system_cache.percent;
		}
		else
		{
			continue;
		}

		if ((ULONG)_r_listview_getitemlparam (hwnd, IDC_LISTVIEW, i) == percent)
			continue;

		_r_listview_setitem (hwnd, IDC_LISTVIEW, i, 0, NULL, I_DEFAULT, I_DEFAULT, (LPARAM)percent);

		is_redraw = TRUE;
	}

	// physical memory
	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%" TEXT (PR_DOUBLE) L"%%", mem_info.physical_memory.percent_f);
	is_redraw |= _app_listviewsettext (hwnd, 0, 1, buffer);

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info.physical_memory.free_bytes);
	is_redraw |= _app_listviewsettext (hwnd, 1, 1, buffer);

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info.physical_memory.total_bytes);
	is_redraw |= _app_listviewsettext (hwnd, 2, 1, buffer);

	// pagefile
	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%" TEXT (PR_DOUBLE) L"%%", mem_info.page_file.percent_f);
	is_redraw |= _app_listviewsettext (hwnd, 3, 1, buffer);

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info.page_file.free_bytes);
	is_redraw |= _app_listviewsettext (hwnd, 4, 1, buffer);

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info.page_file.total_bytes);
	is_redraw |= _app_listviewsettext (hwnd, 5, 1, buffer);

	// system cache
	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%" TEXT (PR_DOUBLE) L"%%", mem_info.system_cache.percent_f);
	is_redraw |= _app_listviewsettext (hwnd, 6, 1, buffer);

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info.system_cache.free_bytes);
	is_redraw |= _app_listviewsettext (hwnd, 7, 1, buffer);

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), mem_info.system_cache.total_bytes);
	is_redraw |= _app_listviewsettext (hwnd, 8, 1, buffer);

	// an idle machine stops producing new values, so the list is not repainted at all then
	if (is_redraw)
		_r_listview_redraw (hwnd, IDC_LISTVIEW);
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
			{
				_app_iconredraw (_r_app_gethwnd ());

				_r_listview_redraw (_r_app_gethwnd (), IDC_LISTVIEW);
			}

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

						_r_listview_redraw (_r_app_gethwnd (), IDC_LISTVIEW);
					}

					break;
				}

				case IDC_ALWAYSONTOP_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"AlwaysOnTop", is_enable, NULL);

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, is_enable);

					break;
				}

				case IDC_LOADONSTARTUP_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_autorun_enable (hwnd, is_enable);

					is_enable = _r_autorun_isenabled ();

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_LOADONSTARTUP_CHK, 0, MF_BYCOMMAND, is_enable);

					_r_ctrl_checkbutton (hwnd, ctrl_id, is_enable);

					break;
				}

				case IDC_STARTMINIMIZED_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"IsStartMinimized", is_enable, NULL);

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_STARTMINIMIZED_CHK, 0, MF_BYCOMMAND, is_enable);

					break;
				}

				case IDC_REDUCTCONFIRMATION_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"IsShowReductConfirmation", is_enable, NULL);

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_REDUCTCONFIRMATION_CHK, 0, MF_BYCOMMAND, is_enable);

					break;
				}

				case IDC_SKIPUACWARNING_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_skipuac_enable (hwnd, is_enable);

					is_enable = _r_skipuac_isenabled ();

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_SKIPUACWARNING_CHK, 0, MF_BYCOMMAND, is_enable);

					_r_ctrl_checkbutton (hwnd, ctrl_id, is_enable);

					break;
				}

				case IDC_CHECKUPDATES_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_app_update_enable (is_enable);

					is_enable = _app_update_isenabled ();

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_CHECKUPDATES_CHK, 0, MF_BYCOMMAND, is_enable);

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
					BOOLEAN is_enabled = _r_ctrl_isenabled (hwnd, ctrl_id);

					hbuddy = _r_updown_getbuddy (hwnd, IDC_AUTOREDUCTVALUE);

					if (hbuddy)
						_r_ctrl_enable (hbuddy, 0, is_checked && is_enabled);

					if (is_enabled)
						_r_config_setboolean_ex (L"AutoreductEnable", is_checked, NULL);

					break;
				}

				case IDC_AUTOREDUCTINTERVALENABLE_CHK:
				{
					HWND hbuddy;
					BOOLEAN is_checked = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);
					BOOLEAN is_enabled = _r_ctrl_isenabled (hwnd, ctrl_id);

					hbuddy = _r_updown_getbuddy (hwnd, IDC_AUTOREDUCTINTERVALVALUE);

					if (hbuddy)
						_r_ctrl_enable (hbuddy, 0, is_checked && is_enabled);

					if (is_enabled)
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
		_r_log (LOG_LEVEL_WARNING, NULL, L"_app_http_openurl", (NTSTATUS)win_status, NULL);

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
		_r_log (LOG_LEVEL_WARNING, NULL, L"_app_http_openurl", (NTSTATUS)win_status, NULL);

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

VOID _app_resizecolumns (
	_In_ HWND hwnd
)
{
	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 0, NULL, -50);
	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 1, NULL, -50);
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
	else
	{
		if (hwnd)
			_r_ctrl_setbuttonshield (hwnd, IDC_CLEAN, TRUE);
	}

	if (!hwnd)
		return;

	_r_ctrl_setbuttonmargins (hwnd, IDC_CLEAN, _r_dc_getwindowdpi (hwnd));

	// configure listview
	_r_listview_setstyle (hwnd, IDC_LISTVIEW, LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER, TRUE);

	_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 1, NULL, 10, LVCFMT_RIGHT);
	_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 2, NULL, 10, LVCFMT_LEFT);

	_app_resizecolumns (hwnd);

	// configure listview
	for (INT i = 0, k = 0; i < 3; i++)
	{
		_r_listview_addgroup (hwnd, IDC_LISTVIEW, i, _r_locale_getstring (IDS_GROUP_1 + i), 0, LVGS_COLLAPSIBLE, LVGS_COLLAPSIBLE);

		for (INT j = 0; j < 3; j++)
		{
			_r_listview_additem (hwnd, IDC_LISTVIEW, k++, _r_locale_getstring (IDS_ITEM_1 + j), I_DEFAULT, i, 0);
		}
	}

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
	tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_EXPAND_FOOTER_AREA | TDF_SIZE_TO_CONTENT;
	tdc.hwndParent = hwnd;
	tdc.hInstance = _r_sys_getimagebase ();
	tdc.dwCommonButtons = TDCBF_OK_BUTTON;
	tdc.pszWindowTitle = _r_locale_getstring (IDS_ABOUT);
	tdc.pszMainInstruction = _r_app_getname ();
	tdc.pszMainIcon = MAKEINTRESOURCEW (IDI_MAIN);
	tdc.pszContent = buffer;
	tdc.pfCallback = &_r_msg_callback;
	tdc.pszCollapsedControlText = _r_locale_getstring (IDS_DONATE);
	tdc.pszExpandedInformation = L"<a href=\"" APP_DONATE_URL L"\">github.com/henrypp/memreduct#donate</a>";
	tdc.pszFooterIcon = TD_INFORMATION_ICON;
	tdc.pszFooter = APP_ABOUT_FOOTER;

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
			HMENU hmenu;

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				_r_menu_checkitem (hmenu, IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"AlwaysOnTop", FALSE, NULL));
				_r_menu_checkitem (hmenu, IDM_USEDARKTHEME, 0, MF_BYCOMMAND, _r_theme_isenabled ());
				_r_menu_checkitem (hmenu, IDM_LOADONSTARTUP_CHK, 0, MF_BYCOMMAND, _r_autorun_isenabled ());
				_r_menu_checkitem (hmenu, IDM_STARTMINIMIZED_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"IsStartMinimized", FALSE, NULL));
				_r_menu_checkitem (hmenu, IDM_REDUCTCONFIRMATION_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"IsShowReductConfirmation", TRUE, NULL));
				_r_menu_checkitem (hmenu, IDM_SKIPUACWARNING_CHK, 0, MF_BYCOMMAND, _r_skipuac_isenabled ());
				_r_menu_checkitem (hmenu, IDM_CHECKUPDATES_CHK, 0, MF_BYCOMMAND, _app_update_isenabled ());

				if (!_r_sys_iselevated ())
					_r_menu_enableitem (hmenu, IDM_SKIPUACWARNING_CHK, FALSE, FALSE);
			}

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
			// localize menu
			HMENU hmenu;

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				_r_menu_setitemtext (hmenu, 0, TRUE, _r_locale_getstring (IDS_FILE));
				_r_menu_setitemtext (hmenu, 1, TRUE, _r_locale_getstring (IDS_VIEW));
				_r_menu_setitemtext (hmenu, 2, TRUE, _r_locale_getstring (IDS_SETTINGS));
				_r_menu_setitemtext (hmenu, 3, TRUE, _r_locale_getstring (IDS_HELP));

				_r_menu_setitemtextformat (hmenu, IDM_SETTINGS, FALSE, L"%s...\tF2", _r_locale_getstring (IDS_SETTINGS));
				_r_menu_setitemtext (hmenu, IDM_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));
				_r_menu_setitemtext (hmenu, IDM_ALWAYSONTOP_CHK, FALSE, _r_locale_getstring (IDS_ALWAYSONTOP_CHK));
				_r_menu_setitemtext (hmenu, IDM_LOADONSTARTUP_CHK, FALSE, _r_locale_getstring (IDS_LOADONSTARTUP_CHK));
				_r_menu_setitemtext (hmenu, IDM_USEDARKTHEME, FALSE, _r_locale_getstring (IDS_USEDARKTHEME));
				_r_menu_setitemtext (hmenu, IDM_STARTMINIMIZED_CHK, FALSE, _r_locale_getstring (IDS_STARTMINIMIZED_CHK));
				_r_menu_setitemtext (hmenu, IDM_REDUCTCONFIRMATION_CHK, FALSE, _r_locale_getstring (IDS_REDUCTCONFIRMATION_CHK));
				_r_menu_setitemtext (hmenu, IDM_SKIPUACWARNING_CHK, FALSE, _r_locale_getstring (IDS_SKIPUACWARNING_CHK));
				_r_menu_setitemtext (hmenu, IDM_CHECKUPDATES_CHK, FALSE, _r_locale_getstring (IDS_CHECKUPDATES_CHK));
				_r_menu_setitemtext (GetSubMenu (hmenu, LANG_SUBMENU), LANG_MENU, TRUE, _r_locale_getstring (IDS_LANGUAGE));
				_r_menu_setitemtext (hmenu, IDM_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
				_r_menu_setitemtext (hmenu, IDM_CHECKUPDATES, FALSE, _r_locale_getstring (IDS_CHECKUPDATES));
				_r_menu_setitemtextformat (hmenu, IDM_ABOUT, FALSE, L"%s\tF1", _r_locale_getstring (IDS_ABOUT));
			}

			// configure listview
			for (INT i = 0, k = 0; i < 3; i++)
			{
				_r_listview_setgroup (hwnd, IDC_LISTVIEW, i, _r_locale_getstring (IDS_GROUP_1 + i), 0, 0);

				for (INT j = 0; j < 3; j++)
				{
					_r_listview_setitem (hwnd, IDC_LISTVIEW, k++, 0, _r_locale_getstring (IDS_ITEM_1 + j), I_DEFAULT, I_DEFAULT, I_DEFAULT);
				}
			}

			// the numbers and the group names are locale dependent, so drop the strings cached for the update skip
			RtlZeroMemory (config.item_prev, sizeof (config.item_prev));
			RtlZeroMemory (config.tooltip_prev, sizeof (config.tooltip_prev));

			// configure button
			_r_ctrl_setstring (hwnd, IDC_CLEAN, _r_locale_getstring (IDS_CLEAN));

			// enum localizations
			if (hmenu)
				_r_locale_enum (GetSubMenu (hmenu, LANG_SUBMENU), LANG_MENU, IDX_LANGUAGE);

			break;
		}

		case WM_DPICHANGED:
		{
			_app_iconinit (_r_dc_gettaskbardpi ());
			_app_iconredraw (hwnd);

			_app_resizecolumns (hwnd);

			if (!_r_sys_iselevated ())
				_r_ctrl_setbuttonmargins (hwnd, IDC_CLEAN, LOWORD (wparam));

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
			}

			break;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc;

			hdc = BeginPaint (hwnd, &ps);

			if (!hdc)
				break;

			_r_dc_drawwindow (hdc, hwnd, TRUE);

			EndPaint (hwnd, &ps);

			break;
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

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case BCN_DROPDOWN:
				{
					R_RECTANGLE rectangle;
					WCHAR buffer[0x100];
					RECT rect;
					HMENU hmenu;

					hmenu = CreatePopupMenu ();

					if (!hmenu)
						break;

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

					if (GetClientRect (nmlp->hwndFrom, &rect))
					{
						ClientToScreen (nmlp->hwndFrom, (PPOINT)&rect);

						_r_wnd_recttorectangle (&rectangle, &rect);
						_r_wnd_adjustrectangletoworkingarea (&rectangle, nmlp->hwndFrom);
						_r_wnd_rectangletorect (&rect, &rectangle);

						_r_menu_popup (hmenu, hwnd, (PPOINT)&rect, TRUE);
					}

					DestroyMenu (hmenu);

					break;
				}

				case NM_CUSTOMDRAW:
				{
					LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;
					LONG_PTR result = CDRF_DODEFAULT;
					ULONG value;

					if (nmlp->idFrom != IDC_LISTVIEW)
						break;

					switch (lpnmlv->nmcd.dwDrawStage)
					{
						case CDDS_PREPAINT:
						{
							result = (CDRF_NOTIFYPOSTPAINT | CDRF_NOTIFYITEMDRAW);
							break;
						}

						case CDDS_ITEMPREPAINT:
						{
							value = (ULONG)lpnmlv->nmcd.lItemlParam;

							if (value >= _app_getdangervalue ())
							{
								lpnmlv->clrText = _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL);

								result = (CDRF_NOTIFYPOSTPAINT | CDRF_NEWFONT);
							}
							else if (value >= _app_getwarningvalue ())
							{
								lpnmlv->clrText = _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL);

								result = (CDRF_NOTIFYPOSTPAINT | CDRF_NEWFONT);
							}

							break;
						}
					}

					SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, result);

					return result;
				}
			}

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
						break;

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
				HMENU hmenu, hsubmenu;

				hmenu = GetMenu (hwnd);

				if (hmenu)
				{
					hsubmenu = GetSubMenu (GetSubMenu (hmenu, LANG_SUBMENU), LANG_MENU);

					if (hsubmenu)
						_r_locale_apply (hsubmenu, ctrl_id, IDX_LANGUAGE);
				}

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

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					_r_wnd_top (hwnd, new_val);

					break;
				}

				case IDM_STARTMINIMIZED_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"IsStartMinimized", FALSE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					break;
				}

				case IDM_REDUCTCONFIRMATION_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"IsShowReductConfirmation", TRUE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					break;
				}

				case IDM_LOADONSTARTUP_CHK:
				{
					_r_autorun_enable (hwnd, !_r_autorun_isenabled ());
					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, _r_autorun_isenabled ());

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

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, is_enabled);

					break;
				}

				case IDM_SKIPUACWARNING_CHK:
				{
					_r_skipuac_enable (hwnd, !_r_skipuac_isenabled ());
					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, _r_skipuac_isenabled ());

					break;
				}

				case IDM_CHECKUPDATES_CHK:
				{
					BOOLEAN new_val = !_app_update_isenabled ();

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
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
						if (_r_app_runasadmin ())
						{
							DestroyWindow (hwnd);
						}
						else
						{
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
