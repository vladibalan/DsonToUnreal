<#
.SYNOPSIS
    Pull the vendored DsonParser artifacts (C header, version header, CHANGELOG, DLL)
    from a local DsonParser repo working tree into this plugin's
    Source/ThirdParty/DsonParser drop.

.DESCRIPTION
    Hand-launched, plugin-side sync (the "Option A" pull). Knowledge of the seam
    lives here, on the consumer side, so the parser stays plugin-agnostic — nothing
    in the parser repo points back at this plugin.

    What it does, in order:
      1. Validates the parser repo path and that every source artifact exists
         (never a partial copy).
      2. Reads the incoming version (parser repo) vs. the currently-vendored version
         (this plugin) and applies a compatibility gate: a clean no-op when already
         current, a refusal on a downgrade, a loud warning on a MAJOR (breaking) bump.
      3. Copies the 4-file bundle (DLL first, so a locked DLL aborts before any header
         changes — effective atomicity for the one realistic failure: UE Editor open).
      4. Prints the CHANGELOG entries newer than what was vendored, so you see what to
         wire up.
      5. Shows `git status` for the drop. It never stages or commits — review the diff
         and commit branch-per-task (push stays with you). See Docs/Tooling.md.

    The import .lib is intentionally NOT in the bundle: the plugin binds the DLL at
    runtime via GetProcAddress (see Source/ThirdParty/DsonParser/DsonParser.Build.cs,
    "no .lib"), so the import library is never linked.

.PARAMETER ParserRepo
    DsonParser repo root. Defaults to $env:DSONPARSER_REPO so the committed script
    stays machine-independent (no hardcoded path).

.PARAMETER Configuration
    Which build output to take DsonParser.dll from (Release or Debug). Default Release.

.PARAMETER Force
    Override the compat gate: re-copy when already current, or allow a downgrade.

.EXAMPLE
    pwsh -File Tools/Sync-Parser.ps1 -ParserRepo E:\Work\Code\DsonTest2

.EXAMPLE
    # With $env:DSONPARSER_REPO set:
    pwsh -File Tools/Sync-Parser.ps1
#>
[CmdletBinding()]
param(
    [string]$ParserRepo = $env:DSONPARSER_REPO,
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Best-effort: emit UTF-8 so CHANGELOG glyphs (em dash, middle dot) render in the console.
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Get-DsonVersion([string]$HeaderPath) {
    if (-not (Test-Path -LiteralPath $HeaderPath)) { return $null }
    $hits = @(Select-String -LiteralPath $HeaderPath -Pattern 'DSONPARSER_VERSION_STRING\s+"([^"]+)"')
    if ($hits.Count -eq 0) { return $null }
    return [version]$hits[0].Matches[0].Groups[1].Value
}

# --- Destination: resolved from this script's location (no hardcoded plugin path) ---
$PluginRoot  = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$DestInclude = Join-Path $PluginRoot 'Source\ThirdParty\DsonParser\Include'
$DestLibs    = Join-Path $PluginRoot 'Source\ThirdParty\DsonParser\Libs\Win64'

# --- Validate the parser repo ---
if ([string]::IsNullOrWhiteSpace($ParserRepo)) {
    Fail "No parser repo given. Pass -ParserRepo <path> or set `$env:DSONPARSER_REPO to the DsonParser repo root."
}
if (-not (Test-Path -LiteralPath $ParserRepo -PathType Container)) {
    Fail "Parser repo not found: $ParserRepo"
}
$ParserRepo = (Resolve-Path -LiteralPath $ParserRepo).Path

$SrcVersionHeader = Join-Path $ParserRepo 'DsonParser\DsonParserVersion.h'
if (-not (Test-Path -LiteralPath $SrcVersionHeader)) {
    Fail "'$ParserRepo' does not look like the DsonParser repo (DsonParser\DsonParserVersion.h missing)."
}

# --- The 4-file bundle: source -> destination (no .lib; binding is runtime GetProcAddress) ---
$SrcDll = Join-Path $ParserRepo ('x64\{0}\DsonParser.dll' -f $Configuration)
$Bundle = @(
    @{ Src = (Join-Path $ParserRepo 'DsonParser\DsonParserAPI.h'); Dst = (Join-Path $DestInclude 'DsonParserAPI.h') }
    @{ Src = $SrcVersionHeader;                                    Dst = (Join-Path $DestInclude 'DsonParserVersion.h') }
    @{ Src = (Join-Path $ParserRepo 'CHANGELOG.md');               Dst = (Join-Path $DestInclude 'CHANGELOG.md') }
    @{ Src = $SrcDll;                                              Dst = (Join-Path $DestLibs 'DsonParser.dll') }
)

# --- Verify every source exists BEFORE touching the destination (no partial update) ---
$missing = @($Bundle | Where-Object { -not (Test-Path -LiteralPath $_.Src) } | ForEach-Object { $_.Src })
if ($missing.Count -gt 0) {
    Write-Host "Missing source artifact(s) in the parser repo:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  $_" }
    if ($missing -contains $SrcDll) {
        Write-Host "  -> Build the parser $Configuration|x64 first:" -ForegroundColor Yellow
        Write-Host "     msbuild DsonTest2.sln /p:Configuration=$Configuration /p:Platform=x64"
    }
    exit 1
}

# --- Compat gate ---
$incoming = Get-DsonVersion $SrcVersionHeader
if ($null -eq $incoming) { Fail "Could not read DSONPARSER_VERSION_STRING from $SrcVersionHeader" }
$current = Get-DsonVersion (Join-Path $DestInclude 'DsonParserVersion.h')   # $null on a fresh drop

if ($null -ne $current) {
    if ($incoming -eq $current -and -not $Force) {
        Write-Host "Already at $current; nothing to do. Use -Force to re-copy." -ForegroundColor Green
        exit 0
    }
    if ($incoming -lt $current -and -not $Force) {
        Fail "Refusing downgrade $current -> $incoming. Use -Force to override."
    }
    if ($incoming.Major -gt $current.Major) {
        Write-Host "WARNING: MAJOR bump $current -> $incoming (breaking ABI)." -ForegroundColor Yellow
        Write-Host "         Expect binding work in DsonParserFunctions.h; the build's ABI check may go red until wired." -ForegroundColor Yellow
    }
}

# --- Copy: DLL first. If it's locked (UE Editor open), abort before any header changes. ---
$dll = $Bundle | Where-Object { $_.Dst -like '*DsonParser.dll' }
try {
    New-Item -ItemType Directory -Force -Path $DestLibs | Out-Null
    Copy-Item -LiteralPath $dll.Src -Destination $dll.Dst -Force
}
catch {
    Fail "Could not copy DsonParser.dll (is the UE Editor open and holding it?). Headers left untouched. $($_.Exception.Message)"
}

New-Item -ItemType Directory -Force -Path $DestInclude | Out-Null
foreach ($f in @($Bundle | Where-Object { $_.Dst -notlike '*DsonParser.dll' })) {
    Copy-Item -LiteralPath $f.Src -Destination $f.Dst -Force
}

# --- Report the transition ---
if ($null -ne $current) {
    $from = "$current"
    if     ($incoming.Major -ne $current.Major) { $class = 'MAJOR' }
    elseif ($incoming.Minor -ne $current.Minor) { $class = 'MINOR' }
    elseif ($incoming.Build -ne $current.Build) { $class = 'PATCH' }
    else                                        { $class = 're-copy' }
}
else {
    $from = '(fresh)'
    $class = 'fresh'
}
Write-Host ""
Write-Host "Synced DsonParser $from -> $incoming  [$class]" -ForegroundColor Cyan

# --- CHANGELOG slice: the sections newer than what was vendored ---
$lines = @(Get-Content -LiteralPath (Join-Path $DestInclude 'CHANGELOG.md') -Encoding UTF8)
$headings = @()
for ($i = 0; $i -lt $lines.Count; $i++) {
    $h = [regex]::Match($lines[$i], '^##\s+(\d+\.\d+\.\d+)\b')
    if ($h.Success) {
        $headings += [pscustomobject]@{ Idx = $i; Ver = [version]$h.Groups[1].Value }
    }
}
$startObj = $headings | Where-Object { $_.Ver -eq $incoming } | Select-Object -First 1

Write-Host ""
Write-Host "What changed (CHANGELOG):" -ForegroundColor Cyan
if ($null -eq $startObj) {
    Write-Host "  (could not locate the $incoming heading; see Include\CHANGELOG.md)"
}
elseif ($class -eq 're-copy') {
    Write-Host "  (re-copy of $incoming; no version delta)"
}
else {
    if ($null -ne $current) {
        $stopObj = $headings | Where-Object { $_.Ver -eq $current } | Select-Object -First 1
    }
    else {
        $stopObj = $headings | Where-Object { $_.Idx -gt $startObj.Idx } | Select-Object -First 1
    }
    $stopIdx = if ($null -ne $stopObj) { $stopObj.Idx } else { $lines.Count }
    $lines[$startObj.Idx..($stopIdx - 1)] | ForEach-Object { Write-Host "  $_" }
}

# --- Next steps + diff surface (no auto-commit) ---
Write-Host ""
Write-Host "Next:" -ForegroundColor Cyan
Write-Host "  - Any new exports above need an X-macro row in"
Write-Host "    Source\DsonImporter\Private\DsonParserFunctions.h (Implementer); the build's ABI check validates them."
Write-Host "  - Review the diff and commit branch-per-task (push stays with you)."
Write-Host ""
Write-Host "git status (Source\ThirdParty\DsonParser):" -ForegroundColor Cyan
Push-Location $PluginRoot
try {
    git status --short -- 'Source/ThirdParty/DsonParser'
}
catch {
    Write-Host "  (git unavailable here; review the drop manually)"
}
finally {
    Pop-Location
}
