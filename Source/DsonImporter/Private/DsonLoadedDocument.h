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

    bool LoadFromFileAsError(const FString& Path, const TCHAR* LogPrefix);
    bool LoadFromFileAsWarning(const FString& Path, const TCHAR* LogPrefix);

    DsonDocumentHandle GetHandle() const { return Handle; }
    uint64_t GetHandle64() const { return reinterpret_cast<uint64_t>(Handle); }
    bool IsValid() const { return Handle != nullptr; }

    void Reset();

private:
    bool LoadFromFile(const FString& Path, const TCHAR* LogPrefix, bool bWarnOnly);
    void LogReadFailure(const TCHAR* LogPrefix, const FString& Path, bool bWarnOnly) const;
    void LogCreateFailure(const TCHAR* LogPrefix, const FString& Path, bool bWarnOnly) const;
    void LogLoadFailure(const TCHAR* LogPrefix, const FString& Path, const FString& ErrorText, bool bWarnOnly) const;

    DsonDocumentHandle Handle = nullptr;
};
