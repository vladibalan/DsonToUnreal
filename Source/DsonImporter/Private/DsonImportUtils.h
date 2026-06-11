#pragma once

#include "CoreMinimal.h"
#include "DsonContentRoots.h"     // FDsonContentRoots::UrlDecode
#include "DsonParserFunctions.h"  // GDsonParser, uint64_t/DsonDocumentHandle

/*
 * Intent:
 * - Small, dependency-light helpers shared across the importer's builders/validator.
 * - Header-only by design: these are tiny leaf functions used in several translation
 *   units, so inlining here avoids a new .cpp without duplicating logic.
 *
 * Read this file before adding another copy of a DSON URL/id/utf8/coordinate helper.
 */
namespace DsonImportUtils
{
    inline bool IsUrlSchemeStart(TCHAR C)
    {
        return (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z'));
    }

    inline bool IsUrlSchemeChar(TCHAR C)
    {
        return IsUrlSchemeStart(C) || (C >= TEXT('0') && C <= TEXT('9'))
            || C == TEXT('+') || C == TEXT('.') || C == TEXT('-');
    }

    // Strip a leading RFC-style "scheme:" only when ':' appears before the first '/'.
    // DAZ formula outputs use "<AssetId>:/data/...#Modifier?value"; disk resolution
    // needs the content-root-relative "/data/..." portion.
    inline FString StripUrlScheme(const FString& Url)
    {
        int32 ColonIndex = INDEX_NONE;
        if (!Url.FindChar(TEXT(':'), ColonIndex) || ColonIndex <= 0)
            return Url;

        int32 SlashIndex = INDEX_NONE;
        if (Url.FindChar(TEXT('/'), SlashIndex) && SlashIndex < ColonIndex)
            return Url;

        if (!IsUrlSchemeStart(Url[0]))
            return Url;

        for (int32 i = 1; i < ColonIndex; ++i)
        {
            if (!IsUrlSchemeChar(Url[i]))
                return Url;
        }

        return Url.Mid(ColonIndex + 1);
    }

    // Strip everything from the first '#' onward. DSON URLs use a trailing '#fragment'
    // to identify a sub-asset inside a file; the fragment is never part of the disk path.
    inline FString StripUrlFragment(const FString& Url)
    {
        int32 HashIndex = INDEX_NONE;
        return Url.FindChar(TEXT('#'), HashIndex) ? Url.Left(HashIndex) : Url;
    }

    // Nullable parser UTF-8 const char* -> FString (empty when null). Parser string
    // pointers are transient, so convert immediately before the next parser call.
    inline FString FromUtf8(const char* Raw)
    {
        return Raw ? FString(UTF8_TO_TCHAR(Raw)) : FString();
    }

    // DAZ node/parent references are URL fragment ids like "#hip"; strip the single
    // leading '#'. Returns empty string for null input.
    inline FString NormalizeDazId(const char* Raw)
    {
        FString Id = FromUtf8(Raw);
        if (Id.StartsWith(TEXT("#")))
            Id.RemoveAt(0, 1, /*bAllowShrinking=*/false);
        return Id;
    }

    // DAZ unit_scale with the DAZ default fallback (1 DAZ unit = 1 cm). Zero/missing
    // both map to the default so callers never scale geometry to nothing.
    inline double ReadDazUnitScale(uint64_t Handle)
    {
        double UnitScale = GDsonParser.GetUnitScale
            ? GDsonParser.GetUnitScale(Handle) : 1.0 / 100.0;
        if (UnitScale == 0.0)
            UnitScale = 1.0 / 100.0;
        return UnitScale;
    }

    // DAZ (Y-up, right-handed) -> UE5 (Z-up, left-handed) for a position:
    //     UE_X =  DAZ_Z,  UE_Y = -DAZ_X,  UE_Z =  DAZ_Y   (all * Scale)
    // The -DAZ_X negation is the reflection that converts handedness; without it the
    // figure silently mirrors (anatomical left/right swap). The skeleton bone transform
    // and the mesh vertex conversion MUST use this exact mapping or they tear apart, so
    // both go through this single function.
    inline FVector DazPointToUe(double X, double Y, double Z, double Scale)
    {
        return FVector(Z * Scale, -X * Scale, Y * Scale);
    }

    // Resolves a "#fragment" LIE reference to the image_library index that holds the
    // layer stack for this LIE entry. Returns INDEX_NONE when FragmentRef does not start
    // with '#' or the decoded id is not found. Callers log the missing-entry warning with
    // their own context; the function only does the lookup (R7 permissive contract intact).
    inline int32 FindImageLibraryIndex(DsonDocumentHandle Doc, const FString& FragmentRef)
    {
        if (!FragmentRef.StartsWith(TEXT("#")))
            return INDEX_NONE;
        const FString ImageId = FDsonContentRoots::UrlDecode(FragmentRef.Mid(1));
        const int32 Count = GDsonParser.GetImageCount ? GDsonParser.GetImageCount(Doc) : 0;
        for (int32 i = 0; i < Count; ++i)
        {
            // R3: copy id before the next parser call
            const FString EntryId = FromUtf8(GDsonParser.GetImageId ? GDsonParser.GetImageId(Doc, i) : nullptr);
            if (EntryId == ImageId)
                return i;
        }
        return INDEX_NONE;
    }
}
