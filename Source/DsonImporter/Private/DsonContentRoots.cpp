#include "DsonContentRoots.h"
#include "DsonImportUtils.h"
#include "Misc/Paths.h"
#include "DsonImporter.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

/*
 * Intent:
 * - Discover DAZ Studio content library roots from the Windows Registry.
 * - Resolve DSON URL-style references into absolute files under those roots.
 * - Decode URL escapes used by DAZ paths.
 *
 * Read this file for path resolution failures, registry probing, and URL decoding.
 */

static bool ContainsPathIgnoreCase(const TArray<FString>& Paths, const FString& Candidate)
{
    for (const FString& Existing : Paths)
    {
        if (Existing.Equals(Candidate, ESearchCase::IgnoreCase))
            return true;
    }

    return false;
}

static void AddExistingContentRootIfUnique(const FString& Path, TArray<FString>& OutPaths)
{
    if (!FPaths::DirectoryExists(Path))
    {
        UE_LOG(LogDsonImporter, Verbose,
            TEXT("DsonContentRoots: registry path not found on disk: %s"), *Path);
        return;
    }

    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonContentRoots: found content root: %s"), *Path);

    if (!ContainsPathIgnoreCase(OutPaths, Path))
        OutPaths.Add(Path);
}

static FString MakeContentRelativePath(const FString& DsonUrl)
{
    FString Decoded = FDsonContentRoots::UrlDecode(DsonImportUtils::StripUrlFragment(DsonUrl));
    if (Decoded.StartsWith(TEXT("/")))
        Decoded = Decoded.RightChop(1);

    return Decoded;
}

TArray<FString> FDsonContentRoots::Detect()
{
    // Probe every DAZ Studio registry key variant this plugin knows about.
    // The returned list is intentionally de-duplicated in ReadRegistryKey.
    TArray<FString> Result;
    static const TCHAR* RegistryKeys[] = {
        TEXT("Software\\DAZ\\Studio4"),
        TEXT("Software\\DAZ\\Studio4_64"),
        TEXT("Software\\DAZ\\Studio4 Beta"),
    };

    for (const TCHAR* RegistryKey : RegistryKeys)
    {
        ReadRegistryKey(RegistryKey, Result);
    }

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
    // Reads ContentDir0..ContentDir255 until the first missing value.
    // Registry values that point to missing directories are logged and skipped.
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
            AddExistingContentRootIfUnique(Path, OutPaths);
        }
    }

    RegCloseKey(Key);
}

#include "Windows/HideWindowsPlatformTypes.h"

FString FDsonContentRoots::ResolveUrl(const FString& DsonUrl, const TArray<FString>& ContentRoots)
{
    // Normalizes a DAZ reference to "content-root-relative path", then tries each root.
    // Fragments identify sub-assets inside a file and are not part of the disk path.
    const FString RelativePath = MakeContentRelativePath(DsonUrl);

    for (const FString& Root : ContentRoots)
    {
        FString FullPath = FPaths::Combine(Root, RelativePath);
        if (FPaths::FileExists(FullPath))
            return FullPath;
    }

    return FString();
}

FString FDsonContentRoots::UrlDecode(const FString& Encoded)
{
    // Minimal percent-decoder for DAZ paths. This intentionally handles the common
    // "%NN" byte escapes used in DSON URLs without pulling in a broader URL parser.
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
