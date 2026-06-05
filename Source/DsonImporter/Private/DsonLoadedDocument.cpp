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

bool FDsonLoadedDocument::LoadFromFileAsError(const FString& Path, const TCHAR* LogPrefix, FString* OutError)
{
    return LoadFromFile(Path, LogPrefix, false, OutError);
}

bool FDsonLoadedDocument::LoadFromFileAsWarning(const FString& Path, const TCHAR* LogPrefix, FString* OutError)
{
    return LoadFromFile(Path, LogPrefix, true, OutError);
}

bool FDsonLoadedDocument::LoadFromFile(const FString& Path, const TCHAR* LogPrefix, bool bWarnOnly, FString* OutError)
{
    Reset();

    // Log the failure and (optionally) hand the same text back to the caller for UI.
    auto Fail = [&](const FString& Message) -> bool
    {
        if (OutError)
            *OutError = Message;
        LogFailure(LogPrefix, Message, bWarnOnly);
        return false;
    };

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *Path) || FileContent.IsEmpty())
        return Fail(FString::Printf(TEXT("failed to read or empty file '%s'"), *Path));

    FTCHARToUTF8 Utf8(*FileContent);

    Handle = GDsonParser.Create ? GDsonParser.Create() : nullptr;
    if (!Handle)
        return Fail(FString::Printf(TEXT("GDsonParser.Create() returned null for '%s'"), *Path));

    const int32 Result = GDsonParser.LoadFromString
        ? GDsonParser.LoadFromString(Handle, Utf8.Get())
        : -1;
    if (Result != 0)
    {
        const char* ErrRaw = GDsonParser.GetLastError ? GDsonParser.GetLastError() : nullptr;
        const FString ErrorText = ErrRaw ? UTF8_TO_TCHAR(ErrRaw) : TEXT("unknown error");
        Reset();
        return Fail(FString::Printf(TEXT("LoadFromString failed for '%s': %s"), *Path, *ErrorText));
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
