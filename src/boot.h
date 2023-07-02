#pragma once

enum class PartitionStyle
{
	Mbr,
	Gpt
};

bool OpenBootDrive();
bool CloseBootDrive();

bool GetBootDriveType(PartitionStyle& style);

bool DestroyMBR();
bool DestroyGPT();
