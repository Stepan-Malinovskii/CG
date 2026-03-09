#include <cstdio>
#include <fcntl.h>
#include <io.h>

#include <Windows.h>

#include "D3DFramework.h" 

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nShowCmd)
{
	AllocConsole();
	FILE* dummy;
	freopen_s(&dummy, "CONOUT$", "w", stdout);
	freopen_s(&dummy, "CONOUT$", "w", stderr);

	D3DFramework framework(hInstance);
	if (!framework.Initialize())
	{
		return 0;
	}

	return framework.Run();
}