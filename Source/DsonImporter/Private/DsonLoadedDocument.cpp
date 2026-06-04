#include "DsonLoadedDocument.h"
#include "DsonImporter.h"
#include "Misc/FileHelper.h"

FDsonLoadedDocument::~FDsonLoadedDocument()
{
    Reset();
}

FDsonLoadedDocument::FDsonLoadedDocument(FDsonLoadedDocument&& Other) noexcept
    : Handle(Other.Handle)
{
    Other.Handle = nullptr;
}

FDsonLoadedDocument& FDsonLoadedDocument::operator=(FDsonLoadedDocument&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        Handle = Other.Handle;
        Other.Handle = nullptr;
    }
    return *this;
}

bool FDsonLoadedDocument::LoadFromFileAsError(const FString& Path, const TCHAR* LogPrefix)
{
    return LoadFromFile(Path, LogPrefix, false);
}

bool FDsonLoadedDocument::LoadFromFileAsWarning(const FString& Path, const TCHAR* LogPrefix)
{
    return LoadFromFile(Path, LogPrefix, true);
}

bool FDsonLoadedDocument::LoadFromFile(const FString& Path, const TCHAR* LogPrefix, bool bWarnOnly)
{
    Reset();

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *Path) || FileContent.IsEmpty())
    {
        LogReadFailure(LogPrefix, Path, bWarnOnly);
        return false;
    }

    FTCHARToUTF8 Utf8(*FileContent);

    Handle = GDsonParser.Create ? GDsonParser.Create() : nullptr;
    if (!Handle)
    {
        LogCreateFailure(LogPrefix, Path, bWarnOnly);
        return false;
    }

    const int32 Result = GDsonParser.LoadFromString
        ? GDsonParser.LoadFromString(Handle, Utf8.Get())
        : -1;
    if (Result != 0)
    {
        const char* ErrRaw = GDsonParser.GetLastError ? GDsonParser.GetLastError() : nullptr;
        const FString ErrorText = ErrRaw ? UTF8_TO_TCHAR(ErrRaw) : TEXT("unknown error");
        LogLoadFailure(LogPrefix, Path, ErrorText, bWarnOnly);
        Reset();
        return false;
    }

    return true;
}

void FDsonLoadedDocument::Reset()
{
    if (Handle && GDsonParser.Destroy)
    {
        GDsonParser.Destroy(Handle);
    }
    Handle = nullptr;
}

void FDsonLoadedDocument::LogReadFailure(const TCHAR* LogPrefix, const FString& Path, bool bWarnOnly) const
{
    if (bWarnOnly)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("%s: failed to read or empty file '%s'"), LogPrefix, *Path);
    }
    else
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("%s: failed to read file '%s'"), LogPrefix, *Path);
    }
}

void FDsonLoadedDocument::LogCreateFailure(const TCHAR* LogPrefix, const FString& Path, bool bWarnOnly) const
{
    if (bWarnOnly)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("%s: GDsonParser.Create() returned null for '%s'"), LogPrefix, *Path);
    }
    else
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("%s: GDsonParser.Create() returned null"), LogPrefix);
    }
}

void FDsonLoadedDocument::LogLoadFailure(
    const TCHAR* LogPrefix,
    const FString& Path,
    const FString& ErrorText,
    bool bWarnOnly) const
{
    if (bWarnOnly)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("%s: LoadFromString failed for '%s': %s"),
            LogPrefix, *Path, *ErrorText);
    }
    else
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("%s: LoadFromString failed for '%s': %s"),
            LogPrefix, *Path, *ErrorText);
    }
}
