#include "libread.h"
#include <stdio.h>

//
// Generic AdobeObject functions
//
DWORD AdobeObject::ReadUint(DWORD address)
{
	if (hProcess != INVALID_HANDLE_VALUE && (address >= 0x00 && address <= 0x7fffffff))
	{
		DWORD dwVal = 0;
		SIZE_T dwRead = 0;
		ReadProcessMemory(hProcess, (LPVOID)address, &dwVal, sizeof(DWORD), &dwRead);

		if (dwRead == sizeof(DWORD))
			return dwVal;
	}

	return 0;
}

bool AdobeObject::WriteUint(DWORD address, DWORD value)
{
	if (hProcess != INVALID_HANDLE_VALUE && (address >= 0x00 && address <= 0x7fffffff))
	{
		SIZE_T dwWrote = 0;
		WriteProcessMemory(hProcess, (LPVOID)address, &value, sizeof(DWORD), &dwWrote);

		if (dwWrote == sizeof(DWORD))
			return true;
	}

	return false;
}

LPVOID AdobeObject::Read(DWORD address, SIZE_T length)
{
	if (hProcess != INVALID_HANDLE_VALUE && (address >= 0x00 && address <= 0x7fffffff))
	{
		LPVOID lpBuffer = VirtualAlloc(NULL, length, MEM_COMMIT, PAGE_READWRITE);
		SIZE_T dwRead = 0;
		if (ReadProcessMemory(hProcess, (LPVOID)address, lpBuffer, length, &dwRead) == 0)
			printf("[dbg] Failed to ReadProcessMemory: %d\n", GetLastError());

		if (dwRead == length)
			return lpBuffer;
	}

	return NULL;
}

bool AdobeObject::Write(DWORD address, LPVOID data, SIZE_T dataLength)
{
	if (hProcess != INVALID_HANDLE_VALUE && (address >= 0x00 && address <= 0x7fffffff))
	{
		SIZE_T dwWrote = 0;
		if (WriteProcessMemory(hProcess, (LPVOID)address, data, dataLength, &dwWrote) == 0)
			printf("[dbg] Failed to WriteProcessMemory: %d\n", GetLastError());

		if (dwWrote == dataLength)
			return true;
	}

	return false;
}

// 
// ServerControl
//
ServerControl::ServerControl(HANDLE hProcess, DWORD address)
{
	this->address = address;
	this->hProcess = hProcess;
	if (this->address)
		this->Unpack();

	this->DuplicateHandles();
}

ServerControl::~ServerControl()
{
	if (this->_ping_event != INVALID_HANDLE_VALUE)
		CloseHandle(this->_ping_event);

	if (this->_pong_event != INVALID_HANDLE_VALUE)
		CloseHandle(this->_pong_event);
}

void ServerControl::Unpack()
{
	this->ping_event = (HANDLE)this->ReadUint(this->address);
	if (this->ping_event == 0x0)
		// bail early
		return;

	this->pong_event = (HANDLE)this->ReadUint(this->address + 0x4);
	this->channel_size = this->ReadUint(this->address + 0x8);
	this->channel_buffer = this->ReadUint(this->address + 0xc);
	this->shared_base = this->ReadUint(this->address + 0x10);
	this->channel = new ChannelControl(this->hProcess, this->ReadUint(this->address + 0x14), this->shared_base);
	this->dispatcher = (LPVOID)this->ReadUint(this->address + 0x18);
	this->target_info = new ClientInfo(this->hProcess, (this->address + 0x1c));
}

void ServerControl::PrettyPrint()
{
	printf("===> ServerControl\n");
	printf("  Ping Event: %08x\n", this->ping_event);
	printf("  Pong Event: %08x\n", this->pong_event);
	printf("  Channel Size: %08x\n", this->channel_size);
	printf("  Channel Buffer: %08x\n", this->channel_buffer);
	printf("  Shared Base: %08x\n", this->shared_base);
}

CrossCallReturn* ServerControl::DoRequest()
{
	this->channel->SetState(ChannelState::BUSY);
	this->channel->crosscall->Pack();

	if (!this->_ping_event)
		return NULL;

	DWORD dwRet = SignalObjectAndWait(this->_ping_event, this->_pong_event, 2000, false);
	this->channel->crosscall->Unpack();
	
	this->channel->crosscall->crosscall_return->signal_return = dwRet;
	this->channel->crosscall->crosscall_return->signal_gle = GetLastError();

	return this->channel->crosscall->crosscall_return;
}

bool ServerControl::DuplicateHandles()
{
	this->_ping_event = duplicate_handle(this->hProcess, this->ping_event);
	this->_pong_event = duplicate_handle(this->hProcess, this->pong_event);
	if (this->_ping_event != INVALID_HANDLE_VALUE && this->_pong_event != INVALID_HANDLE_VALUE)
		return true;

	return false;
}

// 
// ChannelControl
//
ChannelControl::ChannelControl(HANDLE hProcess, DWORD address, DWORD shared_base)
{
	this->address = address;
	this->hProcess = hProcess;
	this->shared_base = shared_base;
	if (this->address)
		this->Unpack();
}

ChannelControl::~ChannelControl(){}

void ChannelControl::Unpack()
{
	this->channel_base = this->ReadUint(this->address);
	this->state        = static_cast<ChannelState>(this->ReadUint(this->address + 0x4));
	this->ping_event   = (HANDLE)this->ReadUint(this->address + 0x8);
	this->pong_event   = (HANDLE)this->ReadUint(this->address + 0xc);
	this->ipc_tag      = this->ReadUint(this->address + 0x10);
	this->channel_buffer = (this->shared_base + this->channel_base);
	this->crosscall = new CrossCallParams(this);
}

void ChannelControl::PrettyPrint()
{
	printf("===> ChannelControl\n");
	printf("  Channel Base: %08x\n", this->channel_base);
	printf("  Channel Buffer: %08x\n", this->channel_buffer);
	printf("  State: %d\n", this->state);
	printf("  Ping Event: %08x\n", this->ping_event);
	printf("  Pong Event: %08x\n", this->pong_event);
	printf("  IPC tag: %d\n", this->ipc_tag);

	if (this->crosscall)
		this->crosscall->PrettyPrint();
}

void ChannelControl::SetState(ChannelState state)
{
	this->WriteUint(this->address + 0x4, state);
}

ChannelState ChannelControl::GetState()
{
	this->state = static_cast<ChannelState>(this->ReadUint(this->address + 0x4));
	return this->state;
}

CrossCallParams* ChannelControl::ReadRequest()
{
	this->crosscall = new CrossCallParams(this);
	return this->crosscall;
}

// 
// ClientInfo
//
ClientInfo::ClientInfo(HANDLE hProcess, DWORD dwAddress)
{
	this->hProcess = hProcess;
	this->dwAddress = dwAddress;
	if (this->dwAddress)
		this->Unpack();
}

ClientInfo::~ClientInfo() 
{
}

void ClientInfo::Unpack()
{
	this->client_process = (HANDLE)this->ReadUint(this->dwAddress);
	this->job_object     = (HANDLE)this->ReadUint(this->dwAddress + 0x4);
	this->pid            = this->ReadUint(this->dwAddress + 0x8);
	this->unknown1		 = this->ReadUint(this->dwAddress + 0xc);
	this->unknown2		 = this->ReadUint(this->dwAddress + 0x10);
}

void ClientInfo::PrettyPrint()
{
	printf("===> ClientInfo\n");
	printf("  Client Process: %08x\n", this->client_process);
	printf("  Job Object: %08x\n", this->job_object);
	printf("  PID: %d\n", this->pid);
	printf("  Unknown 1: %08x\n", this->unknown1);
	printf("  Unknown 2: %08x\n", this->unknown2);
}

//
// CrossCallParams
//
CrossCallParams::CrossCallParams(ChannelControl *control)
{
	this->channel_control = control;
	this->hProcess = control->hProcess;
	if (this->channel_control != NULL)
		this->Unpack();
}

CrossCallParams::~CrossCallParams() {}

void CrossCallParams::Unpack()
{
	this->tag				= this->ReadUint(this->channel_control->channel_buffer);
	this->is_in_out			= this->ReadUint(this->channel_control->channel_buffer + 0x4);
	this->crosscall_return  = new CrossCallReturn(this->channel_control);
	this->params_count		= this->ReadUint(this->channel_control->channel_buffer + 0x3c);

	for (int i = 0; i < this->params_count; ++i) {
		Parameter parameter;
		SIZE_T dwReadBytes = 0;
		parameter.type = static_cast<ArgType>(this->ReadUint(this->channel_control->channel_buffer + (0x4 * (3 * i + 0x10))));
		parameter.size = this->ReadUint(this->channel_control->channel_buffer + (0x4 * (3 * i + 0x12)));
		parameter.offset = this->ReadUint(this->channel_control->channel_buffer + (0x4 * (3 * i + 0x11)));

		parameter.buffer = VirtualAlloc(NULL, parameter.size, MEM_COMMIT, PAGE_READWRITE);
		ReadProcessMemory(this->hProcess, (LPVOID)(this->channel_control->channel_buffer + parameter.offset), parameter.buffer, parameter.size, &dwReadBytes);
		if (dwReadBytes != parameter.size)
			printf("[dbg] Failed to read parameter!\n");

		this->parameters[i] = parameter;
	}
}

void CrossCallParams::Pack()
{
	this->WriteUint(this->channel_control->channel_buffer, this->tag);
	this->WriteUint(this->channel_control->channel_buffer + 0x4, this->is_in_out);
	this->crosscall_return->ClearAndPack();
	this->WriteUint(this->channel_control->channel_buffer + 0x3c, this->params_count);

	DWORD delta_base = ((this->params_count + 1) * 12) + 12 + 52;

	// write offset of parameter 0
	this->WriteUint(this->channel_control->channel_buffer + (0x4 * (3 * 0 + 0x11)), delta_base);

	for (int i = 0; i < this->params_count; ++i)
	{
		this->WriteUint(this->channel_control->channel_buffer + (0x4 * (3 * i + 0x10)),
						this->parameters[i].type);
		this->WriteUint(this->channel_control->channel_buffer + (0x4 * (3 * i + 0x12)), 
						this->parameters[i].size);

		// write parameter
		this->Write(this->channel_control->channel_buffer + delta_base, this->parameters[i].buffer, this->parameters[i].size);
		
		// update delta base and write it in arg+1
		delta_base = this->Align(delta_base + this->parameters[i].size);
		this->WriteUint(this->channel_control->channel_buffer + (0x4 * (3 * (i + 1) + 0x11)), delta_base);
	}

	// write tag to channel control structure
	this->WriteUint(this->channel_control->address + 0x10, this->tag);
}

void CrossCallParams::PrettyPrint()
{
	printf("===> CrossCallParams\n");
	printf("  Tag: %d\n", this->tag);
	printf("  IsInOut: %d\n", this->is_in_out);
	printf("  Params count: %d\n", this->params_count);
	for (int i = 0; i < this->params_count; ++i)
	{
		printf("   Argument %d\n", i);
		printf("    Type: %d\n", this->parameters[i].type);
		printf("    Size: %d\n", this->parameters[i].size);
		printf("    Offset: %08x\n", this->parameters[i].offset);
		if (this->parameters[i].type == WCHAR_TYPE ||
			this->parameters[i].type == UNISTR_TYPE) {
			if (this->parameters[i].size > 0) {
				wchar_t *str = (wchar_t*)this->parameters[i].buffer;
				wprintf(L"    Data: %s\n", str);
			}
		} 
		else if (this->parameters[i].type == ASCII_TYPE) {
			if (this->parameters[i].size > 0) {
				char *str = (char*)this->parameters[i].buffer;
				printf("    Data: %s\n", str);
			}
		}
		else {
			printf("    Data: %08x\n", *reinterpret_cast<DWORD*>(this->parameters[i].buffer));
		}

	}

	if (this->crosscall_return != NULL)
		this->crosscall_return->PrettyPrint();
}

//
// CrossCallReturn
//
CrossCallReturn::CrossCallReturn(ChannelControl *control)
{
	this->channel_control = control;
	this->hProcess = control->hProcess;
	if (this->channel_control != NULL)
		this->Unpack();
}

CrossCallReturn::~CrossCallReturn(){}

void CrossCallReturn::Unpack()
{
	DWORD offset = this->channel_control->channel_buffer + 0x8;
	this->tag = this->ReadUint(offset);
	this->call_outcome = this->ReadUint(offset + 0x4);

	// union
	this->nt_status = this->ReadUint(offset + 0x8);
	this->win32_result = this->ReadUint(offset + 0x8);

	this->extended_count = this->ReadUint(offset + 0xc);
	this->handle = (HANDLE)this->ReadUint(offset + 0x10);

	if (this->extended_count > 8) {
		printf("[dbg] Invalid extended count! (%d)\n", this->extended_count);
		return;
	}

	for (int i = 0; i < this->extended_count; ++i)
		this->extended[i] = this->ReadUint(offset + 0x14 + (4 * i));
}

void CrossCallReturn::Pack()
{
	DWORD offset = this->channel_control->channel_buffer + 0x8;
	this->WriteUint(offset, this->tag);
	this->WriteUint(offset + 0x4, this->call_outcome);
	this->WriteUint(offset + 0x8, this->nt_status | this->win32_result);
	this->WriteUint(offset + 0xc, this->extended_count);
	this->WriteUint(offset + 0x10, (DWORD)this->handle);

	for (int i = 0; i < this->extended_count; ++i)
		this->WriteUint(offset + 0x14 + (4 * i), this->extended[i]);
}

void CrossCallReturn::PrettyPrint()
{
	printf("===> CrossCallReturn\n");
	printf("  Call Outcome: %08x\n", this->call_outcome);
	printf("  NT Status|win32: %08x\n", this->nt_status);
	printf("  Handle: %08x\n", this->handle);
	printf("  Extended count: %d\n", this->extended_count);
	for (int i = 0; i < this->extended_count; ++i)
		printf("    %08x\n", this->extended[i]);
}

void CrossCallReturn::ClearAndPack()
{
	this->tag = 0x0;
	this->call_outcome = 0x0;
	this->nt_status = 0x0;
	this->win32_result = 0x0;
	this->handle = 0x0;
	this->extended_count = 0;
	for (int i = 0; i < 8; ++i)
		this->extended[i] = 0x0;

	this->Pack();
}

//
// IPCControl
//
IPCControl::IPCControl(HANDLE hProcess, DWORD dwAddress)
{
	this->hProcess = hProcess;
	this->dwSharedMem = dwAddress;
	if (this->dwSharedMem != NULL)
		this->Unpack();
}
void IPCControl::Unpack()
{
	this->dwChannelCount = this->ReadUint(this->dwSharedMem);
	this->hServerAlive = (HANDLE)this->ReadUint(this->dwSharedMem + 0x4);

	DWORD dwAddr = this->dwSharedMem + 0x8;
	for (int i = 0; i < this->dwChannelCount; ++i) {
		ChannelControl *channel = new ChannelControl(this->hProcess, dwAddr, this->dwSharedMem);
		this->channels[i] = channel;
		dwAddr += 0x14;
	}
}

void IPCControl::PrettyPrint()
{
	printf("===> IPCControl\n");
	printf("  Channel count: %d\n", this->dwChannelCount);
	printf("  Server alive: %08x\n", this->hServerAlive);
}

ChannelControl* IPCControl::GetFreeChannel()
{
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		if (this->channels[i] == NULL)
			break;

		if (this->channels[i]->state == ChannelState::FREE)
			return this->channels[i];
	}

	return NULL;
}