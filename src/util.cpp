#include "util.h"

#include <iostream>

std::wstring FormatErrorMessage(const DWORD errorCode)
{
	LPWSTR buffer = nullptr;
	const DWORD result = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&buffer),
		0,
		nullptr
	);

	std::wstring errorMessage;
	if (result != 0)
	{
		errorMessage = buffer;
		LocalFree(buffer);
	}
	else
	{
		errorMessage = L"Unknown error";
	}

	return errorMessage;
}

bool GetPhysicalDriveFromDriveLetter(const std::wstring& driveLetter, std::wstring& physicalDrive)
{
	DWORD bytesReturned;
	STORAGE_DEVICE_NUMBER storageDeviceNumber;

	const HANDLE hVolume = CreateFileW(driveLetter.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
	if (hVolume == INVALID_HANDLE_VALUE)
	{
		std::wcout << L"Failed to open volume " << driveLetter << L". Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	if (!DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &storageDeviceNumber,
		sizeof storageDeviceNumber, &bytesReturned, nullptr))
	{
		std::wcout << L"Failed to retrieve device number for volume " << driveLetter << L". Error: " <<
			FormatErrorMessage() << std::endl;
		CloseHandle(hVolume);
		return false;
	}

	CloseHandle(hVolume);

	physicalDrive = L"\\\\.\\PhysicalDrive" + std::to_wstring(storageDeviceNumber.DeviceNumber);

	return true;
}

bool IsElevated()
{
	BOOL bRet = false;
	HANDLE hToken = nullptr;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		TOKEN_ELEVATION elevation;
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof elevation, &cbSize))
		{
			bRet = elevation.TokenIsElevated > 0;
		}
	}

	if (hToken)
	{
		CloseHandle(hToken);
	}

	return bRet;
}

bool Reboot()
{
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;

	// Get a token for this process. 

	if (!OpenProcessToken(GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		std::wcout << L"Failed to open process token. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	// Get the LUID for the shutdown privilege. 
	LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME,
		&tkp.Privileges[0].Luid);

	tkp.PrivilegeCount = 1;  // one privilege to set    
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	// Get the shutdown privilege for this process. 
	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0,
		nullptr, nullptr))
	{
		std::wcout << L"Failed to adjust token privileges. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	// Shut down the system and force all applications to close. 
	if (!ExitWindowsEx(EWX_REBOOT | EWX_FORCE,
		SHTDN_REASON_MAJOR_SYSTEM | SHTDN_REASON_MINOR_OTHER))
	{
		std::wcout << L"Failed to reboot. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	//shutdown was successful
	return true;
}
