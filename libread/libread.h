#pragma once
#include <Windows.h>
#include <vector>

#define LIBREAD_API __declspec(dllexport)

#define MAX_CHANNELS 0xff

enum ChannelState {
	FREE = 1,
	BUSY,
	ACK,
	READY,
	ABANDON
};

enum ArgType {
	INVALID_TYPE = 0,
	WCHAR_TYPE,
	ULONG_TYPE,
	UNISTR_TYPE,
	VOIDPTR_TYPE,
	INPTR_TYPE,
	INOUTPTR_TYPE,
	ASCII_TYPE,
	MEM_TYPE,
	LAST_TYPE
};

enum ResultCode {
	SBOX_ALL_OK = 0,
	SBOX_ERROR_GENERIC,
	SBOX_ERROR_BAD_PARAMS,
	SBOX_ERROR_UNSUPPORTED,
	SBOX_ERROR_NO_SPACE,
	SBOX_ERROR_INVALID_IPC,
	SBOX_ERROR_FAILED_IPC,
	SBOX_ERROR_NO_HANDLE,
	SBOX_ERROR_UNEXPECTED_CALL,
	SBOX_ERROR_WAIT_ALREADY_CALLED,
	SBOX_ERROR_CHANNEL_ERROR,
	SBOX_ERROR_CANNOT_CREATE_DESKTOP,
	SBOX_ERROR_CANNOT_CREATE_WINSTATION,
	SBOX_ERROR_FAILED_TO_SWITCH_BACK_WINSTATION,
	SBOX_ERROR_LAST
};

typedef struct _Parameter {
	ArgType type;
	DWORD size;
	DWORD offset;
	LPVOID buffer;
} Parameter;

class LIBREAD_API AdobeObject
{
public:
	virtual void Unpack() = 0;
	virtual void PrettyPrint() = 0;

	DWORD ReadUint(DWORD address);
	bool WriteUint(DWORD address, DWORD value);
	LPVOID Read(DWORD address, SIZE_T length);
	bool Write(DWORD address, LPVOID data, SIZE_T dataLength);

public:
	HANDLE hProcess = INVALID_HANDLE_VALUE;
};

class ChannelControl;
class CrossCallParams;
class ClientInfo;
class CrossCallReturn;

class LIBREAD_API ServerControl : public AdobeObject
{
public:

	UINT address = 0;
	HANDLE ping_event = INVALID_HANDLE_VALUE;
	HANDLE pong_event = INVALID_HANDLE_VALUE;
	HANDLE _ping_event = INVALID_HANDLE_VALUE;
	HANDLE _pong_event = INVALID_HANDLE_VALUE;
	UINT channel_size = 0;
	UINT channel_buffer = 0;
	UINT shared_base = 0;
	ChannelControl *channel = NULL;
	LPVOID dispatcher = NULL;
	ClientInfo* target_info = NULL;

	ServerControl(HANDLE hProcess, DWORD address);
	~ServerControl();
	CrossCallReturn* DoRequest();
	bool DuplicateHandles();
	void Unpack();
	void PrettyPrint();
};

class LIBREAD_API ChannelControl : public AdobeObject
{
public:
	UINT address = 0;
	UINT shared_base = 0;
	UINT channel_base = 0;
	UINT channel_buffer = 0;
	ChannelState state = ChannelState::FREE;
	HANDLE ping_event = INVALID_HANDLE_VALUE;
	HANDLE pong_event = INVALID_HANDLE_VALUE;
	UINT ipc_tag = 0;
	CrossCallParams *crosscall;

	ChannelControl(HANDLE hProcess, DWORD address, DWORD shared_base);
	~ChannelControl();
	void Unpack();
	void PrettyPrint();
	void SetState(ChannelState state);
	ChannelState GetState();
	CrossCallParams* ReadRequest();
};

class LIBREAD_API ClientInfo : public AdobeObject
{
public:

	DWORD dwAddress = 0;
	HANDLE client_process = INVALID_HANDLE_VALUE;
	HANDLE job_object = INVALID_HANDLE_VALUE;
	UINT pid = 0;
	UINT unknown1 = 0;
	UINT unknown2 = 0;

	ClientInfo(HANDLE hProcess, DWORD address);
	~ClientInfo();
	void Unpack();
	void PrettyPrint();
};

class LIBREAD_API IPCControl : public AdobeObject
{
public:
	DWORD dwSharedMem = 0;
	DWORD dwChannelCount = 0;
	HANDLE hServerAlive = INVALID_HANDLE_VALUE;
	ChannelControl *channels[MAX_CHANNELS]; // max channel count

	IPCControl(HANDLE hHandle, DWORD dwAddress);
	void Unpack();
	void PrettyPrint();
	ChannelControl* GetFreeChannel();
};

class LIBREAD_API CrossCallParams : public AdobeObject
{
private:
	UINT Align(UINT value)
	{
		size_t alignment = sizeof(ULONG_PTR) * 2;
		return ((value + alignment - 1)/alignment) * alignment;
	}

public:

	UINT tag = 0;
	bool is_in_out = 0;
	CrossCallReturn *crosscall_return = NULL;
	UINT params_count = 0;
	ChannelControl *channel_control = NULL;
	Parameter parameters[17];

	CrossCallParams(ChannelControl *control);
	~CrossCallParams();
	void Pack();
	void Unpack();
	void PrettyPrint();
};

class LIBREAD_API CrossCallReturn : public AdobeObject
{
public:

	UINT tag = 0;
	UINT call_outcome = 0;
	union {
		DWORD nt_status;
		DWORD win32_result;
	};

	HANDLE handle = INVALID_HANDLE_VALUE;
	UINT extended_count = 0;
	UINT extended[8] = {};
	ChannelControl *channel_control;

	// ancillary values to aid with debugging
	DWORD signal_return = 0;
	DWORD signal_gle = 0;

	CrossCallReturn(ChannelControl *control);
	~CrossCallReturn();
	void Pack();
	void Unpack();
	void ClearAndPack();
	void PrettyPrint();
};

LIBREAD_API DWORD __cdecl find_memory_map(HANDLE hProcess);
LIBREAD_API DWORD __cdecl scan_memory(HANDLE hProcess, LPVOID address_low, DWORD nbytes, std::vector<BYTE>& bytes_to_find);
LIBREAD_API HANDLE __cdecl duplicate_handle(HANDLE hProcess, HANDLE hOriginal);
LIBREAD_API ServerControl* __cdecl find_free_servercontrol(HANDLE hProcess);
LIBREAD_API inline const char* ArgTypeToStringA(ArgType type);
LIBREAD_API inline const wchar_t* ArgTypeToStringW(ArgType type);