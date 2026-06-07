# DsonToUnreal doc-economy guard (Docs/CodeReviewRules.md R10).
# Fires on Edit/Write/MultiEdit to an orientation doc (AGENTS.md, Docs/*.md,
# MaterialMastersV1.md) and self-filters out everything else.
#   PreToolUse  -> injects the R10 "tier + point-don't-duplicate" reminder.
#   PostToolUse -> warns if a HOT-PATH doc crossed its soft line budget.
# Cold/on-demand docs (DecisionLog, Reference, FormulaMorphsV2, MaterialMastersV1)
# are intentionally absent from the budget table -> exempt; they absorb history.
# Keep these budgets in sync with the R10 list in Docs/CodeReviewRules.md.
# Targets Windows PowerShell 5.1 (no pwsh dependency). Reads the payload from stdin.
$ErrorActionPreference = 'Stop'

$raw = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }

try { $payload = $raw | ConvertFrom-Json } catch { exit 0 }

$path = if ($payload.tool_input) { [string]$payload.tool_input.file_path } else { '' }
if ([string]::IsNullOrWhiteSpace($path)) { exit 0 }
$event = if ($payload.hook_event_name) { [string]$payload.hook_event_name } else { 'PreToolUse' }

# Only fire for THIS plugin's orientation docs. The /DsonToUnreal/ guard keeps it
# safe when wired in user/global settings (which load for every project) - without
# it, a generic /Docs/*.md or AGENTS.md edit in any other repo would trip it.
$norm = $path -replace '\\', '/'
if ($norm -notmatch '/DsonToUnreal/') { exit 0 }
$isDoc = ($norm -match '/Docs/[^/]+\.md$') -or ($norm -match '/AGENTS\.md$') -or ($norm -match '/MaterialMastersV1\.md$')
if (-not $isDoc) { exit 0 }

$leaf = Split-Path $path -Leaf

# Soft line budgets for hot-path docs only (mirror Docs/CodeReviewRules.md R10).
$budgets = @{
    'AGENTS.md'                = 140
    'Roadmap.md'               = 260
    'AuditGuide.md'            = 120
    'ImporterArchitecture.md'  = 185
    'AgentWorkflow.md'         = 120
    'CodeReviewRules.md'       = 240
    'Tooling.md'               = 80
}

function Write-Context([string]$eventName, [string]$text) {
    $out = @{ hookSpecificOutput = @{ hookEventName = $eventName; additionalContext = $text } }
    $out | ConvertTo-Json -Depth 5 -Compress
}

if ($event -eq 'PostToolUse') {
    # Budget tripwire: only for hot-path docs, only after the edit landed.
    if (-not $budgets.ContainsKey($leaf)) { exit 0 }
    $budget = $budgets[$leaf]
    $lines = 0
    try { $lines = @(Get-Content -LiteralPath $path -ErrorAction Stop).Count } catch { exit 0 }
    if ($lines -le $budget) { exit 0 }

    $msg = "DsonToUnreal doc budget (Docs/CodeReviewRules.md R10): $leaf is now $lines lines, over its soft budget of $budget. This is the re-bloat signal. Relocate cold content rather than letting a hot-path doc grow: dated rationale/postmortems/handoff history -> Docs/DecisionLog.md; durable facts/lessons/gotchas -> Docs/Reference.md; or split the doc. Status docs hold CURRENT state only. (Cold archives DecisionLog/Reference/FormulaMorphsV2/MaterialMastersV1 are exempt.)"
    Write-Context 'PostToolUse' $msg
    exit 0
}

# PreToolUse: economy reminder before any orientation-doc edit.
$reminder = @'
DsonToUnreal doc economy (Docs/CodeReviewRules.md R10) for this edit:
- One tier per doc - put content where it belongs: status -> Docs/Roadmap.md; dated rationale/postmortems -> Docs/DecisionLog.md; durable facts/lessons/gotchas -> Docs/Reference.md; rules -> Docs/CodeReviewRules.md; roles -> Docs/AgentWorkflow.md; code layout -> Docs/ImporterArchitecture.md; audit routing -> Docs/AuditGuide.md; tooling -> Docs/Tooling.md; entry/routing -> AGENTS.md.
- Point, don't duplicate: if another doc owns a fact, link it by name (the R1-R10 list and the Director/Implementer role model each have exactly one home).
- Status holds CURRENT state only: when work ships, move rationale to DecisionLog and durable facts to Reference - don't let Roadmap accumulate history.
- Prefer a table or pointer over repeated prose. Hot-path docs have soft line budgets; relocate or split rather than grow past them.
'@
Write-Context 'PreToolUse' $reminder
exit 0
