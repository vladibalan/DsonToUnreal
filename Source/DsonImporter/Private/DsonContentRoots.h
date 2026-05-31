#pragma once
#include "CoreMinimal.h"

/**
 * Reads DAZ Studio 4 content root folders from the Windows Registry.
 * Scans HKCU\Software\DAZ\Studio4 (and known variants) for
 * ContentDir0, ContentDir1, ... entries.
 * Returns an empty array if DAZ Studio is not installed.
 */
class FDsonContentRoots
{
public:
    /** Returns all DAZ Studio content root folders found on this machine. */
    static TArray<FString> Detect();

    /**
     * Resolves a DSON URL reference to an absolute file path.
     * DSON URLs are URL-encoded paths relative to a content root,
     * e.g. "/data/Daz%203D/Genesis%209/Base/Genesis9.dsf"
     * Searches all provided content roots and returns the first match.
     * Returns empty string if not found.
     */
    static FString ResolveUrl(
        const FString& DsonUrl,
        const TArray<FString>& ContentRoots);

    /** URL-decodes a DSON path (e.g. %20 -> space, %203D -> 3D). */
    static FString UrlDecode(const FString& Encoded);

private:
    /** Reads ContentDir0..N from a specific registry key path. */
    static void ReadRegistryKey(
        const FString& KeyPath,
        TArray<FString>& OutPaths);
};