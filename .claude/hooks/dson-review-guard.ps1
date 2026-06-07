# DsonToUnreal PreToolUse guard.
# Injects the R1-R7 code-review checklist (Docs/CodeReviewRules.md) as additionalContext
# before any Edit/Write/MultiEdit to plugin C++ source, so the agent self-checks each diff.
# Targets Windows PowerShell 5.1 (no pwsh dependency). Reads the hook payload from stdin.
$ErrorActionPreference = 'Stop'

$raw = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }

try { $payload = $raw | ConvertFrom-Json } catch { exit 0 }

$path = if ($payload.tool_input) { [string]$payload.tool_input.file_path } else { '' }
if ([string]::IsNullOrWhiteSpace($path)) { exit 0 }

# Only fire for THIS plugin's C++ source (.cpp/.h under Source/DsonImporter). The
# /DsonToUnreal/ guard keeps it safe when wired in user/global settings (which load
# for every project) - without it the checklist would fire on any repo that happens
# to have a Source/DsonImporter/ path.
$norm = $path -replace '\\', '/'
if ($norm -notmatch '/DsonToUnreal/') { exit 0 }
if ($norm -notmatch '/Source/DsonImporter/') { exit 0 }
if ($norm -notmatch '\.(cpp|h)$') { exit 0 }

$checklist = @'
DsonToUnreal code-review self-check (Docs/CodeReviewRules.md) for this edit:
R1 UE 5.4.4 APIs only - no 5.5+ overloads (e.g. FString/TArray RemoveAt takes a bool, not EAllowShrinking); prefer forms already used in-repo; stay within Build.cs modules.
R2 Parser exports = one DSON_PARSER_API_LIST row in DsonParserFunctions.h; never re-add parallel typedef/struct/load lists; the Required flag must match IsValid(); signature must match the DLL C ABI.
R3 Acquire document handles via FDsonLoadedDocument (no hand-rolled Create/LoadFromString/Destroy); convert parser const char* with DsonImportUtils::FromUtf8 before the next parser call.
R4 No duplicated helper - reuse DsonImportUtils / FDsonContentRoots / FDsonAssetUtils / GenerationToString; never re-inline the DAZ->UE coordinate flip (DsonImportUtils::DazPointToUe).
R5 Compact without losing behavior: no dead params, no UENUM/UPROPERTY/USTRUCT without a reflection consumer, no debug dumps at Log; but do not delete load-bearing scaffolding.
R6 static_cast over C-style casts; if constexpr for compile-time branches.
R7 Keep permissive parsing and the failure-return/log-context contracts; flag public-signature (breaking) changes explicitly.
After editing, state which of R1-R7 you checked - do not just say "looks fine".
'@

$out = @{
    hookSpecificOutput = @{
        hookEventName     = 'PreToolUse'
        additionalContext = $checklist
    }
}

$out | ConvertTo-Json -Depth 5 -Compress
exit 0
