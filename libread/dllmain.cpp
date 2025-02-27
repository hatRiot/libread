#include "libread.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <iterator>

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

DWORD find_memory_map(HANDLE hProcess)
{
	UINT current_address = 0x00000000;
	while (true)
	{
		MEMORY_BASIC_INFORMATION mbi = { 0 };
		if (VirtualQueryEx(hProcess, (LPVOID)current_address, &mbi, sizeof(mbi)) == 0)
			return 0;

		if (mbi.RegionSize == 0x200000 && mbi.Type == MEM_MAPPED)
			return (UINT)mbi.AllocationBase;

		if (current_address+mbi.RegionSize >= 0x7fff0000)
			break;

		current_address += mbi.RegionSize;
	}

	return 0;
}

DWORD scan_memory(HANDLE hProcess, LPVOID address_low, DWORD nbytes, std::vector<BYTE>& bytes_to_find)
{
	std::vector<const void*> addresses_found;

	const DWORD pmask = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE |
		PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

	MEMORY_BASIC_INFORMATION mbi{};

	BYTE* address = static_cast<BYTE*>(address_low);
	BYTE* address_high = address + nbytes;
	SIZE_T dwReadBytes = 0;


	while (address < address_high && ::VirtualQueryEx(hProcess, address, std::addressof(mbi), sizeof(mbi)))
	{
		if ((mbi.State == MEM_COMMIT) && (mbi.Protect & pmask) && !(mbi.Protect & PAGE_GUARD))
		{
			LPVOID lpBuf = VirtualAlloc(NULL, mbi.RegionSize, MEM_COMMIT, PAGE_READWRITE);
			ReadProcessMemory(hProcess, address, lpBuf, mbi.RegionSize, &dwReadBytes);

			const BYTE* begin = static_cast<const BYTE*>(lpBuf);
			const BYTE* end = begin + mbi.RegionSize;

			const BYTE* found = std::search(begin, end, bytes_to_find.begin(), bytes_to_find.end());
			while (found != end)
			{
				//printf("%08x (%08x) remotely @ %08x\n", found, (found - lpBuf), (address + (found - lpBuf)));
				// we're searching on shared_base, so to capture the full ServerControl lets backup 0x10
				return (DWORD)((address + (found - lpBuf)) - 0x10);
			}

			VirtualFree(lpBuf, mbi.RegionSize, MEM_FREE);
		}

		address += mbi.RegionSize;
		mbi = {};
	}

	return -1;
}

HANDLE duplicate_handle(HANDLE hProcess, HANDLE hOriginal)
{
	HANDLE hDup = INVALID_HANDLE_VALUE;
	HANDLE hCurrentProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());

	if (!DuplicateHandle(hProcess, hOriginal, hCurrentProc, &hDup, 0, false, DUPLICATE_SAME_ACCESS)) {
		CloseHandle(hCurrentProc);
		return INVALID_HANDLE_VALUE;
	}
	
	CloseHandle(hCurrentProc);

	return hDup;
}

ServerControl* find_free_servercontrol(HANDLE hProcess)
{
	DWORD mmap = find_memory_map(hProcess);
	if (mmap <= 0)
		return NULL;
	
	std::vector<BYTE> find;
	for (int i = 0; i < 4; ++i)
		find.push_back((mmap >> (8 * i)) & 0xff);

	DWORD start_addr = 0x100;
	ServerControl *control = NULL;
	while (true) {
		DWORD found = scan_memory(hProcess, (LPVOID)start_addr, (0x7fffffff - start_addr), find);

		// sanity check channel_size
		DWORD dwChanSize = 0, dwRet = 0;
		ReadProcessMemory(hProcess, (LPVOID)(found + 8), &dwChanSize, sizeof(DWORD), &dwRet);

		if (dwChanSize == 0x20000) {
			control = new ServerControl(hProcess, found);
			if (control && control->channel_size == 0x20000 && control->channel->state == 1) 
				break;

			delete control;
		}

		start_addr = (DWORD)found + sizeof(ServerControl);
		if (start_addr >= 0x7fffffff)
			break;
	}

	return control;
}

// yeah idk; someone with crazy c++20 template skills improve this
inline const char* ArgTypeToStringA(ArgType type)
{
	switch (type)
	{
	case INVALID_TYPE:  return "INVALID_TYPE";
	case WCHAR_TYPE:    return "WCHAR_TYPE";
	case ULONG_TYPE:    return "ULONG_TYPE";
	case UNISTR_TYPE:   return "UNISTR_TYPE";
	case VOIDPTR_TYPE:  return "VOIDPTR_TYPE";
	case INPTR_TYPE:    return "INPTR_TYPE";
	case INOUTPTR_TYPE: return "INOUTPTR_TYPE";
	case ASCII_TYPE:    return "ASCII_TYPE";
	case MEM_TYPE:		return "MEM_TYPE";
	case LAST_TYPE:		return "LAST_TYPE";
	default:			return "UNKNOWN_TYPE";
	}
}

inline const wchar_t* ArgTypeToStringW(ArgType type)
{
	switch (type)
	{
	case INVALID_TYPE:  return L"INVALID_TYPE";
	case WCHAR_TYPE:    return L"WCHAR_TYPE";
	case ULONG_TYPE:    return L"ULONG_TYPE";
	case UNISTR_TYPE:   return L"UNISTR_TYPE";
	case VOIDPTR_TYPE:  return L"VOIDPTR_TYPE";
	case INPTR_TYPE:    return L"INPTR_TYPE";
	case INOUTPTR_TYPE: return L"INOUTPTR_TYPE";
	case ASCII_TYPE:    return L"ASCII_TYPE";
	case MEM_TYPE:		return L"MEM_TYPE";
	case LAST_TYPE:		return L"LAST_TYPE";
	default:			return L"UNKNOWN_TYPE";
	}
}