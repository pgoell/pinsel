#!/usr/bin/env bash
# Apply GitHub repo + branch-protection settings from .github/*.json, and turn
# on vulnerability alerts and Dependabot security updates.
#
# Reading branch protection (used by --check and the post-apply verify) requires
# a token with the "Administration" (read) permission. A local `gh auth login`
# with the `repo` scope has it; the Actions GITHUB_TOKEN does not, so the
# CI cannot run it without a separate admin token.
set -euo pipefail

cd "$(dirname "$0")/.."

DRY_RUN=0
SKIP_VERIFY=0
CHECK=0

usage() {
  cat <<'EOF'
Usage: scripts/apply-github-settings.sh [--check | --dry-run | --skip-verify] [--help]

Applies .github/repo-settings.json via PATCH /repos/:owner/:repo, turns on
vulnerability alerts and Dependabot security updates, then
.github/branch-protection.json via PUT /repos/:owner/:repo/branches/:default/protection,
then the Actions settings: .github/actions-permissions.json (allowlist on, every
action pinned to a commit SHA) and .github/selected-actions.json (the allowlist).
On a public repo it also holds every outside contributor's workflow runs for
approval.
Reads branch protection back and diffs the normalized result against the source
JSON, and reads back both security settings. Exits non-zero on drift.

Flags (mutually exclusive):
  --check         Read branch protection back and diff against branch-protection.json,
                  and check both security settings are on.
                  Do not apply. Exit 5 on drift, 0 on match.
  --dry-run       Print the gh api commands that would run, do not mutate.
  --skip-verify   Apply settings but skip the readback + drift diff.

  --help          Show this message and exit.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --check) CHECK=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --skip-verify) SKIP_VERIFY=1; shift ;;
    --help|-h) usage; exit 0 ;;
    --*) echo "unknown flag: $1" >&2; usage >&2; exit 2 ;;
    *)   echo "unexpected positional argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if (( DRY_RUN + SKIP_VERIFY + CHECK > 1 )); then
  echo "--check, --dry-run, and --skip-verify are mutually exclusive" >&2
  usage >&2
  exit 2
fi

# --- Preflight ----------------------------------------------------------------
command -v gh >/dev/null || { echo "gh is required; install https://cli.github.com/" >&2; exit 2; }
command -v jq >/dev/null || { echo "jq is required" >&2; exit 2; }
gh auth status >/dev/null 2>&1 || {
  echo "gh is not authenticated; run 'gh auth login'" >&2
  exit 3
}

# --- Resolve repo -------------------------------------------------------------
REPO_INFO=$(gh repo view --json nameWithOwner,defaultBranchRef \
  --jq '.nameWithOwner + " " + .defaultBranchRef.name' 2>/dev/null) || {
  echo "could not resolve current repo via 'gh repo view'; is this a GitHub repo clone?" >&2
  exit 4
}
OWNER_REPO=${REPO_INFO% *}
DEFAULT_BRANCH=${REPO_INFO##* }

REPO_SETTINGS=.github/repo-settings.json
BRANCH_PROTECTION=.github/branch-protection.json
ACTIONS_PERMISSIONS=.github/actions-permissions.json
SELECTED_ACTIONS=.github/selected-actions.json

echo "target: $OWNER_REPO (branch: $DEFAULT_BRANCH)"

# --- Verifier ----------------------------------------------------------------
verify_branch_protection() {
  echo "→ verifying branch protection"
  local actual_raw actual_norm expected_norm
  actual_raw=$(mktemp)
  actual_norm=$(mktemp)
  expected_norm=$(mktemp)
  # A RETURN trap stays set after the function; clear it so it does not fire on
  # the next function's return.
  trap 'rm -f "$actual_raw" "$actual_norm" "$expected_norm"; trap - RETURN' RETURN

  gh api "/repos/$OWNER_REPO/branches/$DEFAULT_BRANCH/protection" > "$actual_raw"

  local filter=scripts/lib/normalize-branch-protection.jq
  jq -S -f "$filter" "$actual_raw"        > "$actual_norm"
  jq -S -f "$filter" "$BRANCH_PROTECTION" > "$expected_norm"

  if diff -u "$expected_norm" "$actual_norm" >&2; then
    echo "✔ branch protection matches branch-protection.json"
    return 0
  else
    echo "✖ drift detected between branch-protection.json and GitHub" >&2
    return 5
  fi
}

verify_security_updates() {
  echo "→ verifying security settings"
  local rc=0
  if ! gh api "/repos/$OWNER_REPO/vulnerability-alerts" --silent 2>/dev/null; then
    echo "✖ vulnerability alerts are off" >&2
    rc=5
  fi
  if [[ "$(gh api "/repos/$OWNER_REPO/automated-security-fixes" --jq .enabled 2>/dev/null)" != "true" ]]; then
    echo "✖ Dependabot security updates are off" >&2
    rc=5
  fi
  (( rc == 0 )) && echo "✔ vulnerability alerts and Dependabot security updates are on"
  return $rc
}

verify() {
  local rc=0
  verify_branch_protection || rc=$?
  verify_security_updates || rc=$?
  return $rc
}

# --- Check-only path ----------------------------------------------------------
if [[ "$CHECK" == "1" ]]; then
  verify
  exit $?
fi

# --- Apply or describe --------------------------------------------------------
if [[ "$DRY_RUN" == "1" ]]; then
  echo "→ would run: gh api --method PATCH /repos/$OWNER_REPO --input $REPO_SETTINGS"
  echo "→ would run: gh api --method PUT  /repos/$OWNER_REPO/vulnerability-alerts"
  echo "→ would run: gh api --method PUT  /repos/$OWNER_REPO/automated-security-fixes"
  echo "→ would run: gh api --method PUT  /repos/$OWNER_REPO/branches/$DEFAULT_BRANCH/protection --input $BRANCH_PROTECTION"
  echo "→ would run: gh api --method PUT  /repos/$OWNER_REPO/actions/permissions --input $ACTIONS_PERMISSIONS"
  echo "→ would run: gh api --method PUT  /repos/$OWNER_REPO/actions/permissions/selected-actions --input $SELECTED_ACTIONS"
  echo "→ would run, on a public repo: gh api --method PUT /repos/$OWNER_REPO/actions/permissions/fork-pr-contributor-approval -f approval_policy=all_external_contributors"
  echo "✔ dry run complete, no changes made"
  exit 0
fi

echo "→ applying repo-level settings"
gh api --method PATCH "/repos/$OWNER_REPO" --input "$REPO_SETTINGS" >/dev/null

echo "→ turning on vulnerability alerts and Dependabot security updates"
# Security updates need the alerts on first. GitHub turns the alerts on a moment
# after the PUT returns: wait until they read back on.
gh api --method PUT "/repos/$OWNER_REPO/vulnerability-alerts" >/dev/null
for _ in {1..15}; do
  gh api "/repos/$OWNER_REPO/vulnerability-alerts" --silent 2>/dev/null && break
  sleep 2
done
gh api --method PUT "/repos/$OWNER_REPO/automated-security-fixes" >/dev/null

echo "→ applying branch protection"
gh api --method PUT "/repos/$OWNER_REPO/branches/$DEFAULT_BRANCH/protection" \
  --input "$BRANCH_PROTECTION" >/dev/null

echo "→ applying Actions settings"
gh api --method PUT "/repos/$OWNER_REPO/actions/permissions" --input "$ACTIONS_PERMISSIONS" >/dev/null
gh api --method PUT "/repos/$OWNER_REPO/actions/permissions/selected-actions" \
  --input "$SELECTED_ACTIONS" >/dev/null
# GitHub refuses this setting on a private repo, where no fork can open a PR.
if [[ "$(gh repo view --json visibility --jq .visibility)" == "PUBLIC" ]]; then
  gh api --method PUT "/repos/$OWNER_REPO/actions/permissions/fork-pr-contributor-approval" \
    -f approval_policy=all_external_contributors >/dev/null
fi

if [[ "$SKIP_VERIFY" == "1" ]]; then
  echo "✔ applied (verify skipped)"
  exit 0
fi

verify
exit $?
