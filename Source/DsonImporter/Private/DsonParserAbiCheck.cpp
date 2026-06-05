// DsonParserAbiCheck.cpp
//
// SOLE PURPOSE: build-time ABI drift tripwire. Nothing in this translation unit
// runs, links, or emits code — it contains only `static_assert`s over type
// traits. There are no function definitions, no objects, and no ODR-use of any
// parser export (every reference to an export is inside `decltype`, which is an
// unevaluated context, so no symbol/import is requested from DsonParser.dll).
//
// WHY THIS EXISTS
// The plugin binds the parser DLL through the hand-maintained X-macro list
// `DSON_PARSER_API_LIST` in DsonParserFunctions.h, by reinterpret_cast from
// GetProcAddress. The compiler therefore never checks those signatures against
// the DLL author's authoritative prototypes in the vendored C header
// (Source/ThirdParty/DsonParser/Include/DsonParserAPI.h). A wrong return type,
// parameter type, parameter order, or parameter count compiles cleanly and only
// corrupts the stack/registers at runtime. This file closes that gap WITHOUT
// changing the binding: for every entry in DSON_PARSER_API_LIST it asserts that
// the bound member's function-pointer type is ABI-compatible with
// `decltype(&<ExportName>)` taken from the vendored header.
//
// See Docs/CodeReviewRules.md, R2 (single-source parser ABI in
// DsonParserFunctions.h): this is the mechanical backstop for that rule. It is a
// "lighter alternative" to deriving the binding types from the vendored header
// via decltype — it leaves the existing binding untouched and merely trips a
// build error if the two sources ever disagree on a function the plugin binds.
//
// TOLERATED SPELLING DIFFERENCE (deliberate, ABI-identical on Win64/MSVC, see the
// comment in DsonParserFunctions.h): the binding spells the document handle
// `uint64_t` while the vendored header spells it `DsonDocumentHandle` (== void*).
// On Win64 LLP64 both are 8 bytes and pass identically in a register, so the
// check treats `uint64_t` and `void*` as equivalent. `int32_t` and `int` are the
// same type under MSVC, so they need no special case. Every other type — double,
// bool, const char*, const char**, double*, etc. — is compared strictly, and any
// difference in parameter COUNT or ORDER trips the assert. This plugin and DLL
// ship Win64-only (DsonParser.Build.cs guards on Win64), so the Win64/MSVC
// assumption is safe.
//
// FAILURE MODES (what a red build here means):
//   * static_assert fires  -> SIGNATURE drift: the X-macro entry for that export
//     no longer matches the vendored prototype (return/param type, order, or
//     count). Fix the X-macro row in DsonParserFunctions.h (or the vendored
//     header, whichever is wrong) so they agree — and rebuild.
//   * `decltype(&X)` fails to compile (unknown identifier) -> NAME drift: the
//     X-macro lists an export the vendored header does not declare at all.
//
// SAFETY: if the vendored header is somehow unavailable, the whole file compiles
// to nothing (guarded by __has_include) so it can never break a build for the
// wrong reason; a #pragma message records that the check was skipped.

#if defined(__has_include)
#  if __has_include("DsonParserAPI.h")
#    define DSON_HAVE_VENDORED_API 1
#  endif
#endif

#if defined(DSON_HAVE_VENDORED_API)

#include <cstdint>
#include <type_traits>

#include "DsonParserAPI.h"      // vendored authoritative C ABI prototypes
#include "DsonParserFunctions.h" // DSON_PARSER_API_LIST — the binding we verify

// All helpers live in a uniquely named namespace so the common trait names below
// cannot collide with other translation units in a UE unity build.
namespace DsonParserAbiCheckDetail
{
    // Decompose a free-function pointer type into return type + parameter pack.
    template <class T> struct FnPtr;            // undefined for non-fn-pointers
    template <class R, class... Args>
    struct FnPtr<R (*)(Args...)>
    {
        using Ret = R;
        template <template <class...> class Tmpl> using ApplyArgs = Tmpl<Args...>;
    };

    template <class...> struct Pack {};

    // Per-type ABI equivalence: identical by default, plus the one deliberate,
    // ABI-identical handle spelling (uint64_t <-> DsonDocumentHandle == void*).
    template <class A, class B> struct ParamAbiEquiv : std::is_same<A, B> {};
    template <> struct ParamAbiEquiv<std::uint64_t, void*> : std::true_type {};
    template <> struct ParamAbiEquiv<void*, std::uint64_t> : std::true_type {};

    // Position-by-position pack comparison; differing arity falls through to the
    // primary template (false), so a parameter-count mismatch is caught.
    template <class PA, class PB> struct PackAbiEquiv : std::false_type {};
    template <> struct PackAbiEquiv<Pack<>, Pack<>> : std::true_type {};
    template <class A, class... As, class B, class... Bs>
    struct PackAbiEquiv<Pack<A, As...>, Pack<B, Bs...>>
        : std::integral_constant<bool,
              ParamAbiEquiv<A, B>::value
                  && PackAbiEquiv<Pack<As...>, Pack<Bs...>>::value> {};

    // True iff binding pointer type Tb and vendored pointer type Tv have
    // ABI-equivalent return types and ABI-equivalent, equal-length parameters.
    template <class Tb, class Tv>
    struct AbiCompatible
        : std::integral_constant<bool,
              ParamAbiEquiv<typename FnPtr<Tb>::Ret,
                            typename FnPtr<Tv>::Ret>::value
                  && PackAbiEquiv<
                         typename FnPtr<Tb>::template ApplyArgs<Pack>,
                         typename FnPtr<Tv>::template ApplyArgs<Pack> >::value> {};

    // For each X-macro row: reconstruct the bound member's function-pointer type
    // exactly as DsonParserFunctions.h declares it ("Ret (*Member) Params"), and
    // assert it is ABI-compatible with the vendored prototype decltype(&Export).
    // Ret/Params come straight from the same list, so the assertions stay
    // single-sourced — there are no hand-typed signatures here.
#define DSON_ABI_ASSERT(Required, Ret, Member, ExportName, Params)             \
    static_assert(                                                             \
        ::DsonParserAbiCheckDetail::AbiCompatible<Ret(*) Params,               \
                                                  decltype(&ExportName)>::value,\
        "DSON ABI drift: binding '" #Member "' -> '" #ExportName               \
        "' is not ABI-compatible with the vendored DsonParserAPI.h prototype.");

    DSON_PARSER_API_LIST(DSON_ABI_ASSERT)

#undef DSON_ABI_ASSERT
} // namespace DsonParserAbiCheckDetail

#else // !DSON_HAVE_VENDORED_API

#pragma message("DsonParserAbiCheck: vendored DsonParserAPI.h not found; ABI cross-check skipped (no error).")

#endif // DSON_HAVE_VENDORED_API
