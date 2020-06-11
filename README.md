# libread

`libread` is a simple library for interfacing with the Adobe Reader sandbox. It provides basic access to write and read requests to the broker process. It is by no means feature complete, but please feel free to submit pull requests ;)

 See the public blogpost here (TODO) for details.

## Building

This project is built using Visual Studio 2017 and *should* build out of the box. A prebuilt x86 DLL is provided already. Note that there is no x64 DLL because there is [no x64 version of Adobe Reader](https://community.adobe.com/t5/acrobat-reader/adobe-acrobat-reader-dc-64-bit-edition/td-p/9034166?page=1).

## Using

Check out the [sander](https://github.com/hatRiot/sander) project for additional details on how to use this library. 

In short, however, the following code demonstrates how to use the library to obtain a `ServerControl` handle and send an IPC request:

```
void TriggerIPC(DWORD dwPid)
{
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, dwPid);
	if (hProcess == INVALID_HANDLE_VALUE) {
		printf("[-] Could not open PID %d (err: %d)!\n", dwPid, GetLastError());
		return;
	}

	ServerControl *sc = find_free_servercontrol(hProcess);
	sc->channel->SetState(ChannelState::BUSY);
	CrossCallParams *ccp = new CrossCallParams(sc->channel);
	
	ccp->tag = 62;
	ccp->is_in_out = 0;
	ccp->params_count = 1;

	wchar_t *path = (wchar_t*)L"C:\\testpath\0";
	ccp->parameters[0].buffer = path;
	ccp->parameters[0].size = wcslen(path) * sizeof(wchar_t);
	ccp->parameters[0].type = ArgType::WCHAR_TYPE;
	sc->channel->crosscall = ccp;

	CrossCallReturn *ccr = sc->DoRequest();
	if (ccr->signal_return != 0)
		printf("Signal failed (%08x = %d)\n", ccr->signal_return, ccr->signal_gle);
	else
		ccr->PrettyPrint();

	sc->channel->SetState(ChannelState::FREE);

	delete ccp;
	return;
}
```