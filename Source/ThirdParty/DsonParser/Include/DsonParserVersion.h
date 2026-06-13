// DsonParser version orientation:
// Canonical single-source-of-truth version macros for the DsonParser library.
// These macros are the authoritative library version; DsonParserAPI.h includes
// this header, DsonParser_GetVersion() returns DSONPARSER_VERSION_STRING, and the
// CHANGELOG.md top heading mirrors it. On every release bump the macros here and
// the CHANGELOG heading together so the two stay equal (see docs/versioning.md).
//
// This is a PUBLISHED header that ships beside DsonParserAPI.h, so it must compile
// as plain C and stay UE-agnostic (code-review-rules R4): macros only — no types,
// no includes, no C++-only constructs.
//
// NOTE: this is the LIBRARY's own version, distinct from
// DsonDocument_GetFileVersion(), which returns a parsed DSON asset's file_version.

#pragma once

#define DSONPARSER_VERSION_MAJOR  2
#define DSONPARSER_VERSION_MINOR  0
#define DSONPARSER_VERSION_PATCH  0
#define DSONPARSER_VERSION_STRING "2.0.0"
