# DsonToUnreal release-tag guard (Docs/Versioning.md R12 step 4).
# Stop hook: when the plugin's VersionName has a CHANGELOG entry but no matching
# git tag vX.Y.Z, surface a non-blocking reminder to create the tag. Silent when the
# tag already exists (the normal state) and outside DsonToUnreal sessions.
# Why a Stop hook: tagging is the last step of a release and was skipped across the
# v1.1.0-v1.5.0 burst because nothing surfaced the omission. Re-checking at every
# turn-end means a missing release tag cannot quietly persist.
# Scope: this hook is wired globally (loads for every session), so it self-scopes by
# the session cwd - a Stop payload carries no edited-file path, unlike the edit guards.
# Targets Windows PowerShell 5.1 (no pwsh). ASCII only. Reads the payload from stdin.
# Never blocks or errors out: every path exits 0; parse/git failures are swallowed.
$ErrorActionPreference = 'Stop'

$raw = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }
try { $payload = $raw | ConvertFrom-Json } catch { exit 0 }

# Self-scope: act only when the session is working in the DsonToUnreal tree. When cwd
# is reported and does not match, stay silent (avoids cross-repo noise); when cwd is
# absent, fall through to surfacing rather than failing silently.
$cwd = if ($payload.cwd) { ([string]$payload.cwd) -replace '\\', '/' } else { '' }
if ($cwd -and ($cwd -notmatch '(?i)DsonToUnreal')) { exit 0 }

# Locate the repo from THIS script (always the real plugin root, regardless of cwd):
#   <root>/.claude/hooks/<this>.ps1  ->  <root>
$repoRoot  = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$uplugin   = Join-Path $repoRoot 'DsonToUnreal.uplugin'
$changelog = Join-Path $repoRoot 'CHANGELOG.md'
if (-not (Test-Path -LiteralPath $uplugin)) { exit 0 }

# VersionName in the .uplugin is the single source of truth (Docs/Versioning.md).
try { $version = (Get-Content -LiteralPath $uplugin -Raw | ConvertFrom-Json).VersionName }
catch { exit 0 }
if ([string]::IsNullOrWhiteSpace($version)) { exit 0 }

# Only nag once the release is substantially done - VersionName bumped AND changelogged
# - so an in-progress bump (before the CHANGELOG entry is written) does not trip it.
if (-not (Test-Path -LiteralPath $changelog)) { exit 0 }
$headingPattern = '^##\s+' + [regex]::Escape($version) + '(\s|$)'
if (-not (Select-String -LiteralPath $changelog -Pattern $headingPattern -Quiet)) { exit 0 }

# In sync if the tag already exists. A git error (not installed / not a repo) -> silent.
$tag = 'v' + $version
$existing = $null
try { $existing = & git -C $repoRoot tag --list $tag } catch { exit 0 }
if ($LASTEXITCODE -ne 0) { exit 0 }
if (-not [string]::IsNullOrWhiteSpace(($existing -join ''))) { exit 0 }

# Drift: a changelogged release with no tag. Surface a non-blocking reminder.
$cmd = "git tag -a $tag -m 'DsonToUnreal $version' <release-commit>"
$msg = "DsonToUnreal release not tagged (Docs/Versioning.md R12 step 4): VersionName is " +
       "$version and CHANGELOG.md has its entry, but git tag $tag does not exist yet. " +
       "Create the annotated tag on the release commit (the commit that bumped VersionName " +
       "to $version, usually HEAD right after it), run from the repo root ($repoRoot): " +
       "$cmd -- then the user pushes it: git push origin $tag."

$out = @{ systemMessage = $msg }
$out | ConvertTo-Json -Depth 5 -Compress
exit 0
