#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <stdbool.h>

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE prev, PWSTR cmdLine, int show) {
	RTL_OSVERSIONINFOW v = {sizeof(v)};
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (ntdll) {
		typedef LONG(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
		RtlGetVersionPtr fn = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
		if (!fn || fn(&v) != 0 || v.dwMajorVersion < 10) {
			MessageBoxW(NULL, L"FlashGBX requires Windows 10 or newer to run.", L"FlashGBX", MB_ICONERROR);
			return 1;
		}
	}

	int argc;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) {
		MessageBoxW(NULL, L"Error parsing command line arguments.", L"FlashGBX", MB_ICONERROR);
		return 1;
	}

	wchar_t cmd[8192];
	StringCchCopyW(cmd, 8192, L"Python\\python.exe -m FlashGBX");

	const wchar_t *exe = wcsrchr(argv[0], L'\\');
	exe = exe ? exe + 1 : argv[0];
	bool is_app = (_wcsicmp(exe, L"FlashGBX-app.exe") == 0);
	bool cli = false, cfg = false;

	for (int i = 1; i < argc; i++) {
        if (_wcsnicmp(argv[i], L"--cfgdir", 8) == 0) {
			cfg = true;
		} else if (_wcsnicmp(argv[i], L"--show-console", 14) == 0) {
			cli = true;
			continue;
		} else {
        	if (_wcsnicmp(argv[i], L"--", 2) == 0) cli = true;
		}
		StringCchCatW(cmd, 8192, L" \"");
		StringCchCatW(cmd, 8192, argv[i]);
		StringCchCatW(cmd, 8192, L"\"");
	}
	if (cli) StringCchCatW(cmd, 8192, L" --wait");
	if (!cfg) StringCchCatW(cmd, 8192, is_app ? L" --cfgdir appdata" : L" --cfgdir subdir");

    STARTUPINFOW startInfo = { sizeof(startInfo) };
    startInfo.lpTitle = L"FlashGBX";
	PROCESS_INFORMATION procInfo = {0};
	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, cli ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW, NULL, NULL, &startInfo, &procInfo)) {
        DWORD err = GetLastError();
        wchar_t buf[1024];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buf, 1024, NULL);
        wchar_t msg[2048];
        StringCchPrintfW(msg, 2048, L"Couldn't launch the FlashGBX application!\nError %lu: %s", err, buf);
        MessageBoxW(NULL, msg, L"FlashGBX Launcher", MB_ICONERROR);
		LocalFree(argv);
		return 1;
	}

	CloseHandle(procInfo.hThread);
	WaitForSingleObject(procInfo.hProcess, INFINITE);
	CloseHandle(procInfo.hProcess);
	LocalFree(argv);

	return 0;
}
