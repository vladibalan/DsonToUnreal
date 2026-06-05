#pragma once

#include "CoreMinimal.h"
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
}
