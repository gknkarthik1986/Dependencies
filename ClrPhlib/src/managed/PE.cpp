#include <ClrPhlib.h>
#include <UnmanagedPh.h>

using namespace System;
using namespace System::Text;
using namespace ClrPh;
using namespace Runtime::InteropServices;

PE::PE(
    _In_ String ^ Filepath
)
{
    m_Impl = new UnmanagedPE();
    wchar_t* PvFilepath = (wchar_t*)(Marshal::StringToHGlobalUni(Filepath)).ToPointer();

	this->LoadSuccessful = m_Impl->LoadPE(PvFilepath);
	this->Filepath = gcnew String(Filepath);


	if (LoadSuccessful)
		InitProperties();
	
	Marshal::FreeHGlobal(IntPtr((void*)PvFilepath));
}

void PE::InitProperties()
{
	LARGE_INTEGER time;
	SYSTEMTIME systemTime;

	PH_MAPPED_IMAGE PvMappedImage = m_Impl->m_PvMappedImage;
	
	Properties = gcnew PeProperties();
	Properties->Machine = PvMappedImage.NtHeaders->FileHeader.Machine;
	Properties->Magic = m_Impl->m_PvMappedImage.Magic;
	Properties->Checksum = PvMappedImage.NtHeaders->OptionalHeader.CheckSum;
	Properties->CorrectChecksum = (Properties->Checksum == PhCheckSumMappedImage(&PvMappedImage));

	RtlSecondsSince1970ToTime(PvMappedImage.NtHeaders->FileHeader.TimeDateStamp, &time);
	PhLargeIntegerToLocalSystemTime(&systemTime, &time);
	Properties->Time = gcnew DateTime (systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds, DateTimeKind::Local);

	if (PvMappedImage.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
	{
		PIMAGE_OPTIONAL_HEADER32 OptionalHeader = (PIMAGE_OPTIONAL_HEADER32) &PvMappedImage.NtHeaders->OptionalHeader;
		
		Properties->ImageBase = (IntPtr) (Int32) OptionalHeader->ImageBase;
		Properties->SizeOfImage = OptionalHeader->SizeOfImage;
		Properties->EntryPoint = (IntPtr) (Int32) OptionalHeader->AddressOfEntryPoint;
	}
	else
	{
		PIMAGE_OPTIONAL_HEADER64 OptionalHeader = (PIMAGE_OPTIONAL_HEADER64)&PvMappedImage.NtHeaders->OptionalHeader;

		Properties->ImageBase = (IntPtr)(Int64)OptionalHeader->ImageBase;
		Properties->SizeOfImage = OptionalHeader->SizeOfImage;
		Properties->EntryPoint = (IntPtr)(Int64)OptionalHeader->AddressOfEntryPoint;

	}

	Properties->Subsystem = PvMappedImage.NtHeaders->OptionalHeader.Subsystem;
	Properties->SubsystemVersion = gcnew Tuple<Int16, Int16>(
		PvMappedImage.NtHeaders->OptionalHeader.MajorSubsystemVersion,
		PvMappedImage.NtHeaders->OptionalHeader.MinorSubsystemVersion);
	Properties->Characteristics = PvMappedImage.NtHeaders->FileHeader.Characteristics;
	Properties->DllCharacteristics = PvMappedImage.NtHeaders->OptionalHeader.DllCharacteristics;

	Properties->FileSize = PvMappedImage.Size;
}

PE::~PE()
{
    delete m_Impl;
}

Collections::Generic::List<PeExport^> ^ PE::GetExports()
{
	Collections::Generic::List<PeExport^> ^Exports = gcnew Collections::Generic::List<PeExport^>();

	if (!LoadSuccessful)
		return Exports;

	if (NT_SUCCESS(PhGetMappedImageExports(&m_Impl->m_PvExports, &m_Impl->m_PvMappedImage)))
	{
		for (size_t Index = 0; Index < m_Impl->m_PvExports.NumberOfEntries; Index++)
		{
			Exports->Add(gcnew PeExport(*m_Impl, Index));
		}
	}

	return Exports;
}


Collections::Generic::List<PeImportDll^> ^ PE::GetImports()
{
	Collections::Generic::List<PeImportDll^> ^Imports = gcnew Collections::Generic::List<PeImportDll^>();

	if (!LoadSuccessful)
		return Imports;

	// Standard Imports
	if (NT_SUCCESS(PhGetMappedImageImports(&m_Impl->m_PvImports, &m_Impl->m_PvMappedImage)))
	{
		for (size_t IndexDll = 0; IndexDll< m_Impl->m_PvImports.NumberOfDlls; IndexDll++)
		{
			Imports->Add(gcnew PeImportDll(&m_Impl->m_PvImports, IndexDll));
		}
	}

	// Delayed Imports
	if (NT_SUCCESS(PhGetMappedImageDelayImports(&m_Impl->m_PvDelayImports, &m_Impl->m_PvMappedImage)))
	{
		for (size_t IndexDll = 0; IndexDll< m_Impl->m_PvDelayImports.NumberOfDlls; IndexDll++)
		{
			Imports->Add(gcnew PeImportDll(&m_Impl->m_PvDelayImports, IndexDll));
		}
	}

	return Imports;
}



String^ PE::GetManifest()
{
	if (!LoadSuccessful)
		return gcnew String("");

	// Extract embedded manifest
	INT  rawManifestLen;
	PSTR rawManifest;
	if (!m_Impl->GetPeManifest(&rawManifest, &rawManifestLen))
		return gcnew String("");


	// Converting to wchar* and passing it to a C#-recognized String object
	UTF8Encoding Utf8Decoder;

	array<unsigned char> ^buffer = gcnew array<unsigned char>(rawManifestLen + 1);
	for (int i = 0; i < rawManifestLen; i++)
	{
		buffer[i] = rawManifest[i];
	}
	buffer[rawManifestLen] = 0;

	return  Utf8Decoder.GetString(buffer, 0, rawManifestLen);
}

bool PE::IsWow64Dll() 
{
	return ((Properties->Machine & 0xffff ) == IMAGE_FILE_MACHINE_I386);
}