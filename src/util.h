#pragma once

#include <string>
#include <Windows.h>

std::wstring FormatErrorMessage(DWORD errorCode = GetLastError());

bool GetPhysicalDriveFromDriveLetter(const std::wstring& driveLetter, std::wstring& physicalDrive);

bool IsElevated();

bool Reboot();

