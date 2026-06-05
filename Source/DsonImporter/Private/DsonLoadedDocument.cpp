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

void FDsonLoadedDocument::LogFailure(
    const TCHAR* LogPrefix,
    const FString& Message,
    bool bWarnOnly) const
{
    if (bWarnOnly)
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("%s: %s"), LogPrefix, *Message);
    }
    else
    {
        UE_LOG(LogDsonImporter, Error, TEXT("%s: %s"), LogPrefix, *Message);
    }
}

void FDsonLoadedDocument::LogReadFailure(const TCHAR* LogPrefix, const FString& Path, bool bWarnOnly) const
{
    if (bWarnOnly)
    {
        LogFailure(LogPrefix, FString::Printf(
            TEXT("failed to read or empty file '%s'"), *Path), bWarnOnly);
    }
    else
    {
        LogFailure(LogPrefix, FString::Printf(
            TEXT("failed to read file '%s'"), *Path), bWarnOnly);
    }
}

void FDsonLoadedDocument::LogCreateFailure(const TCHAR* LogPrefix, const FString& Path, bool bWarnOnly) const
{
    if (bWarnOnly)
    {
        LogFailure(LogPrefix, FString::Printf(
            TEXT("GDsonParser.Create() returned null for '%s'"), *Path), bWarnOnly);
    }
    else
    {
        LogFailure(LogPrefix, TEXT("GDsonParser.Create() returned null"), bWarnOnly);
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
        LogFailure(LogPrefix, FString::Printf(
            TEXT("LoadFromString failed for '%s': %s"), *Path, *ErrorText), bWarnOnly);
    }
    else
    {
        LogFailure(LogPrefix, FString::Printf(
            TEXT("LoadFromString failed for '%s': %s"), *Path, *ErrorText), bWarnOnly);
    }
}
