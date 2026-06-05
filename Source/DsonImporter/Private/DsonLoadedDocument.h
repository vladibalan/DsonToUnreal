#pragma once

#include "CoreMinimal.h"
#include "DsonParserFunctions.h"

// RAII wrapper for parser document handles created by GDsonParser.
// Owns exactly one DsonDocumentHandle and destroys it automatically.
class FDsonLoadedDocument
{
public:
    FDsonLoadedDocument() = default;
    ~FDsonLoadedDocument();

    FDsonLoadedDocument(const FDsonLoadedDocument&) = delete;
    FDsonLoadedDocument& operator=(const FDsonLoadedDocument&) = delete;

    FDsonLoadedDocument(FDsonLoadedDocument&& Other) noexcept;
    FDsonLoadedDocument& operator=(FDsonLoadedDocument&& Other) noexcept;

    // OutError (optional) receives the same human-readable failure text that is logged,
    // for callers that surface it in UI (e.g. the validator dialog).
    bool LoadFromFileAsError(const FString& Path, const TCHAR* LogPrefix, FString* OutError = nullptr);
    bool LoadFromFileAsWarning(const FString& Path, const TCHAR* LogPrefix, FString* OutError = nullptr);

    DsonDocumentHandle GetHandle() const { return Handle; }
    uint64_t GetHandle64() const { return reinterpret_cast<uint64_t>(Handle); }
    bool IsValid() const { return Handle != nullptr; }

    void Reset();

private:
    bool LoadFromFile(const FString& Path, const TCHAR* LogPrefix, bool bWarnOnly, FString* OutError);
    void LogFailure(const TCHAR* LogPrefix, const FString& Message, bool bWarnOnly) const;

    DsonDocumentHandle Handle = nullptr;
};
