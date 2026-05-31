#include "DsonContentRoots.h"
#include "Misc/Paths.h"
#include "DsonImporter.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

TArray<FString> FDsonContentRoots::Detect()
{
    TArray<FString> Result;
    ReadRegistryKey(TEXT("Software\\DAZ\\Studio4"), Result);
    ReadRegistryKey(TEXT("Software\\DAZ\\Studio4_64"), Result);
    ReadRegistryKey(TEXT("Software\\DAZ\\Studio4 Beta"), Result);

    if (Result.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonContentRoots: no DAZ Studio content roots found"));
    }
    else
    {
        UE_LOG(LogDsonImporter, Log,
            TEXT("DsonContentRoots: detected %d content root(s)"), Result.Num());
    }

    return Result;
}

void FDsonContentRoots::ReadRegistryKey(const FString& KeyPath, TArray<FString>& OutPaths)
{
    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonContentRoots: scanning registry key %s"), *KeyPath);

    HKEY Key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, *KeyPath, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return;

    for (int32 i = 0; i < 256; ++i)
    {
        FString ValueName = FString::Printf(TEXT("ContentDir%d"), i);

        wchar_t ValueData[512] = {};
        DWORD DataSize = static_cast<DWORD>(sizeof(ValueData));
        DWORD Type = 0;

        if (RegQueryValueExW(Key, *ValueName, nullptr, &Type,
                             reinterpret_cast<LPBYTE>(ValueData), &DataSize) != ERROR_SUCCESS)
            break;

        if (Type == REG_SZ || Type == REG_EXPAND_SZ)
        {
            FString Path(ValueData);
            if (FPaths::DirectoryExists(Path))
            {
                UE_LOG(LogDsonImporter, Verbose,
                    TEXT("DsonContentRoots: found content root: %s"), *Path);

                bool bDuplicate = false;
                for (const FString& Existing : OutPaths)
                {
                    if (Existing.Equals(Path, ESearchCase::IgnoreCase))
                    {
                        bDuplicate = true;
                        break;
                    }
                }
                if (!bDuplicate)
                    OutPaths.Add(Path);
            }
            else
            {
                UE_LOG(LogDsonImporter, Verbose,
                    TEXT("DsonContentRoots: registry path not found on disk: %s"), *Path);
            }
        }
    }

    RegCloseKey(Key);
}

#include "Windows/HideWindowsPlatformTypes.h"

FString FDsonContentRoots::ResolveUrl(const FString& DsonUrl, const TArray<FString>& ContentRoots)
{
    FString Url = DsonUrl;

    // Strip fragment (everything after #)
    int32 HashIndex = INDEX_NONE;
    if (Url.FindChar(TEXT('#'), HashIndex))
        Url = Url.Left(HashIndex);

    FString Decoded = UrlDecode(Url);

    // Strip leading slash
    if (Decoded.StartsWith(TEXT("/")))
        Decoded = Decoded.RightChop(1);

    for (const FString& Root : ContentRoots)
    {
        FString FullPath = FPaths::Combine(Root, Decoded);
        if (FPaths::FileExists(FullPath))
            return FullPath;
    }

    return FString();
}

FString FDsonContentRoots::UrlDecode(const FString& Encoded)
{
    FString Result;
    Result.Reserve(Encoded.Len());

    for (int32 i = 0; i < Encoded.Len(); ++i)
    {
        const TCHAR C = Encoded[i];
        if (C == TEXT('%') && i + 2 < Encoded.Len())
        {
            const FString HexStr = Encoded.Mid(i + 1, 2);
            const int32 HexVal = FCString::Strtoi(*HexStr, nullptr, 16);
            Result.AppendChar(static_cast<TCHAR>(HexVal));
            i += 2;
        }
        else
        {
            Result.AppendChar(C);
        }
    }

    return Result;
}