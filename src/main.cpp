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

void PrintBytes(const BYTE* buffer, DWORD bufferSize)
{
	for (DWORD i = 0; i < bufferSize; i++)
	{
		std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]) << " ";
		if ((i + 1) % 16 == 0)
			std::cout << std::endl;
	}
	std::cout << std::dec << std::endl;
}

std::wstring FormatErrorMessage(DWORD errorCode = GetLastError())
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

bool GetGptPartitions(std::vector<PartitionEntry>& partitions)
{
	DWORD dwBytesReturned;
	DRIVE_LAYOUT_INFORMATION_EX* driveLayoutInfo = nullptr;
	DWORD bufferSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + (sizeof(PARTITION_INFORMATION_EX) * 128);
	// Allocate a larger buffer

	while (true)
	{
		driveLayoutInfo = static_cast<DRIVE_LAYOUT_INFORMATION_EX*>(realloc(driveLayoutInfo, bufferSize));
		if (!driveLayoutInfo)
		{
			std::wcout << L"Failed to allocate memory for drive layout information." << std::endl;
			return false;
		}

		if (DeviceIoControl(hBootDrive, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, driveLayoutInfo, bufferSize,
		                    &dwBytesReturned, nullptr))
		{
			break; // Successfully retrieved drive layout information
		}

		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			std::wcout << L"Failed to get drive layout. Error: " << FormatErrorMessage() << std::endl;
			free(driveLayoutInfo);
			return false;
		}

		// Retry with a larger buffer size
		bufferSize += sizeof(PARTITION_INFORMATION_EX) * 128;
	}

	const PARTITION_INFORMATION_EX* partitionInfo = driveLayoutInfo->PartitionEntry;
	for (DWORD i = 0; i < driveLayoutInfo->PartitionCount; ++i)
	{
		PartitionEntry entry{};
		entry.partitionNumber = partitionInfo->PartitionNumber;
		entry.startingOffset = partitionInfo->StartingOffset.QuadPart;
		entry.size = partitionInfo->PartitionLength.QuadPart;
		entry.partitionType = partitionInfo->Gpt.PartitionType;
		entry.partitionId = partitionInfo->Gpt.PartitionId;

		partitions.push_back(entry);

		++partitionInfo;
	}

	free(driveLayoutInfo);

	return true;
}

bool IsESP(const GUID& partitionType)
{
	constexpr GUID espGuid = {0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
	return IsEqualGUID(partitionType, espGuid);
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
	                     sizeof(storageDeviceNumber), &bytesReturned, nullptr))
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

std::wstring GUIDToString(const GUID& guid)
{
	wchar_t guidStr[39]; // GUID string representation has a fixed length of 39 characters (including null-terminator)
	if (StringFromGUID2(guid, guidStr, std::size(guidStr)) == 0)
	{
		std::wcout << L"Failed to convert GUID to string." << std::endl;
		return L"";
	}

	return guidStr;
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
	                         nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
	if (hBootDrive == INVALID_HANDLE_VALUE)
	{
		std::wcout << L"Failed to open the boot drive. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	return true;
}

bool GetESPVolume(std::wstring& espVolume)
{
	std::vector<PartitionEntry> partitions;
	if (!GetGptPartitions(partitions))
	{
		return false;
	}

	PartitionEntry esp{};
	bool found = false;
	for (const auto& partition : partitions)
	{
		if (IsESP(partition.partitionType))
		{
			esp = partition;
			found = true;
			break;
		}
	}

	if (!found)
	{
		std::wcout << L"Failed to locate EFI System Partition" << std::endl;
		return false;
	}

	espVolume = L"\\\\?\\Volume" + GUIDToString(esp.partitionId);

	return true;
}

bool GetBootDriveType(PartitionStyle& style)
{
	// Get the partition style of the boot drive
	DWORD bytesReturned = 0;
	PARTITION_INFORMATION_EX partitionInfo;
	if (!DeviceIoControl(hBootDrive, IOCTL_DISK_GET_PARTITION_INFO_EX, nullptr, 0, &partitionInfo,
	                     sizeof(partitionInfo), &bytesReturned, nullptr))
	{
		std::wcout << L"Failed to retrieve the partition style. Error: " << FormatErrorMessage() << std::endl;
		CloseHandle(hBootDrive);
		return false;
	}

	// Check if the partition style indicates GPT
	const bool isGPT = (partitionInfo.PartitionStyle == PARTITION_STYLE_GPT);

	style = isGPT ? PartitionStyle::Gpt : PartitionStyle::Mbr;
	return true;
}

bool ReadMBR(BYTE buffer[512])
{
	DWORD bytesRead = 0;
	if (!(ReadFile(hBootDrive, buffer, 512, &bytesRead, nullptr) && bytesRead == 512))
	{
		std::wcout << L"Failed to read MBR from boot drive. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

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
		if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize))
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

bool WriteBootSignature(const unsigned char sig[2])
{
	unsigned char buffer[512];
	ReadMBR(buffer);

	buffer[510] = sig[0];
	buffer[511] = sig[1];

	DWORD bytesWritten = 0;
	PrintBytes(buffer, 512);
	if (!(WriteFile(hBootDrive, buffer, 512, &bytesWritten, nullptr) && bytesWritten == 512))
	{
		std::wcout << L"Failed to write to boot sector. Error: " << FormatErrorMessage() << std::endl;
		return false;
	}

	return true;
}

int wmain(int argc, const wchar_t* argv[])
{
	const std::vector<std::wstring> args(argv + 1, argv + argc);
	if (!IsElevated())
	{
		std::wcout << L"Run as administrator" << std::endl;
		system("pause");
		return 1;
	}

	if (!OpenBootDrive())
	{
		return 1;
	}

	PartitionStyle style;
	if (!GetBootDriveType(style))
	{
		goto exit_error;
	}

	if (style == PartitionStyle::Gpt)
	{
		std::wcout << L"Detected GPT" << std::endl;

		std::wstring espVolume;
		if (!GetESPVolume(espVolume))
		{
			goto exit_error;
		}

		std::wcout << L"Found ESP at " << espVolume << std::endl;
	}
	else
	{
		std::wcout << L"Detected MBR" << std::endl;

		unsigned char sig[2];
		unsigned char buffer[512];
		if (argc > 1 && args[0] == L"--rollback-mbr-signature")
		{
			std::wcout << L"Rolling back 0xAA55" << std::endl;
			sig[0] = 0x55;
			sig[1] = 0xAA;
		}
		else
		{
			std::wcout << L"Destroying MBR signature" << std::endl;
			sig[0] = 0;
			sig[1] = 0;
		}

		if (!WriteBootSignature(sig))
		{
			goto exit_error;
		}

		ReadMBR(buffer);
		PrintBytes(buffer, 512);
	}

	std::wcout << L"Done" << std::endl;
	CloseHandle(hBootDrive);

	system("pause");
	return 0;

exit_error:
	CloseHandle(hBootDrive);
	system("pause");
	return 1;
}
