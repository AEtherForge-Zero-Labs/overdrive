#!/usr/bin/env bash
# PreToolUse guard for Claude Code's Bash tool. It enforces mechanically
# the agent rules that AGENTS.md and the constitution (Arts. XII, XVI)
# state in prose: agents may merge through the Art. XVI gate, and never
# take the steps that need a human. Exit code 2 blocks the command and
# shows Claude the reason. It complements the deny rules in
# .claude/settings.json; neither replaces the other.
set -u

input=$(cat)
cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty')
[ -z "$cmd" ] && exit 0

block() {
  printf 'Blocked by .claude/hooks/guard.sh: %s\n' "$1" >&2
  exit 2
}
has() { printf '%s' "$cmd" | grep -Eq -- "$1"; }

# Paths whose changes need the owner (Art. XVI): governance, license,
# ownership, and the guard itself.
reserved='^(\.specify/memory/constitution\.md|LICENSE|\.github/CODEOWNERS|\.claude/hooks/guard\.sh|\.claude/settings\.json)$'

# 1. Merging goes through the Art. XVI gate.
if has '(^|[;&|({[:space:]])gh[[:space:]]+pr[[:space:]]+merge([[:space:]]|$)'; then
  if has '--admin'; then
    block "--admin bypasses branch protection; that is a human decision (Art. XVI)."
  fi
  pr=$(printf '%s' "$cmd" | sed -E 's/.*gh[[:space:]]+pr[[:space:]]+merge[[:space:]]+([0-9]+|https?:[^[:space:]]+).*/\1/;t;s/.*//')
  view=$(gh pr view ${pr:+"$pr"} --json number,files,labels,statusCheckRollup,closingIssuesReferences 2>/dev/null) \
    || block "could not read the pull request to check the merge gate. Name the PR number explicitly."
  files=$(printf '%s' "$view" | jq -r '.files[].path' | grep -E "$reserved" || true)
  [ -n "$files" ] && block "the PR changes owner-reserved paths (${files//$'\n'/, }). Label it needs-human for the owner (Art. XVI)."
  printf '%s' "$view" | jq -e '[.labels[].name] | index("needs-human")' >/dev/null \
    && block "the PR is labeled needs-human (Art. XVI)."
  checks=$(printf '%s' "$view" | jq -r '.statusCheckRollup | length')
  [ "$checks" = "0" ] && block "the PR has no checks on its head commit; nothing proves it green (Art. XVI)."
  bad=$(printf '%s' "$view" | jq -r '.statusCheckRollup[] | select(((.conclusion // .state) | ascii_upcase) as $s | ["SUCCESS","SKIPPED","NEUTRAL"] | index($s) | not) | (.name // .context) + "=" + ((.conclusion // .state // "PENDING") | tostring)')
  [ -n "$bad" ] && block "checks are not all green on the PR head: ${bad//$'\n'/, }. Wait for them or fix the failure (Art. XVI)."
  issues=$(printf '%s' "$view" | jq -r '.closingIssuesReferences[].number')
  for n in $issues; do
    gh issue view "$n" --json labels --jq '[.labels[].name] | index("needs-human")' 2>/dev/null | grep -qv null \
      && block "issue #$n is labeled needs-human (Art. XVI)."
  done
fi

# 2. Steps that need a human (Art. XVI): publishing, destruction,
#    visibility/ownership, secrets, protection removal.
if has '(^|[;&|({[:space:]])gh[[:space:]]+(release[[:space:]]+(create|delete|edit|upload)|repo[[:space:]]+(delete|edit|rename|archive|unarchive|transfer)|secret|variable|org)([[:space:]]|$)'; then
  block "releases, repository visibility/ownership, secrets, variables, and org settings need a human (Art. XVI). Label the issue needs-human."
fi
if has '(^|[;&|({[:space:]])gh[[:space:]]+issue[[:space:]]+(delete|transfer)([[:space:]]|$)'; then
  block "deleting or transferring issues needs a human (Art. XVI)."
fi
if has 'gh[[:space:]]+api'; then
  if has '/merge([^a-z_]|$)'; then
    block "merge through 'gh pr merge' so the Art. XVI gate is checked, not through the API."
  fi
  if has '(-X|--method)[[:space:]]*["'\'']?DELETE' || { has '/protection|/rulesets' && has '(-X|--method)[[:space:]]*["'\'']?(PUT|PATCH)' && has 'enforcement["'\'']?[[:space:]]*[:=][[:space:]]*["'\'']?disabled'; }; then
    block "deleting resources or disabling protection through the API needs a human (Art. XVI)."
  fi
fi

# 3. Tags are releases (Art. XVI).
if has '(^|[;&|({[:space:]])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+push([[:space:]]|$)' && has '--tags|refs/tags/|[[:space:]]v[0-9]'; then
  block "pushing tags publishes a release, which needs a human (Art. XVI)."
fi

# 4. Pushes: no force or remote deletion; main only through pull requests
#    (except the first push that creates it); every unpushed commit
#    discloses AI assistance (Art. XVI).
if has '(^|[;&|({[:space:]])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+push([[:space:]]|$)'; then
  if has '--force|--force-with-lease|--mirror|--delete|(^|[[:space:]])-[a-zA-Z]*[fd][a-zA-Z]*([[:space:]]|$)|[[:space:]]\+[^[:space:]]+|[[:space:]]:[^[:space:]]+'; then
    block "force-pushes and remote branch deletion are destructive and need a human (Art. XVI)."
  fi
  if has 'push[^;&|]*([[:space:]]|:)main([[:space:]]|$)'; then
    if git ls-remote --exit-code --heads origin main >/dev/null 2>&1; then
      block "main only changes through pull requests (Art. XVI)."
    fi
  fi
  upstream=$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null) \
    || { git rev-parse -q --verify origin/main >/dev/null && upstream=origin/main; } || upstream=""
  range=${upstream:+$upstream..}HEAD
  missing=$(git log "$range" --format='%h%x09%(trailers:key=Assisted-by,valueonly,separator=%x2C)%(trailers:key=Co-authored-by,valueonly,separator=%x2C)%x09%s' 2>/dev/null \
    | awk -F'\t' '$2 == "" { printf "%s %s; ", $1, $3 }')
  if [ -n "$missing" ]; then
    block "these unpushed commits have neither an Assisted-by: nor a Co-Authored-By: trailer: ${missing}Reword them before pushing. If a commit had no AI involvement, ask the human to push it."
  fi
fi

# 5. Secrets never reach the transcript (Art. XIII).
if has '(^|[;&|({[:space:]])gh[[:space:]]+auth[[:space:]]+token([[:space:]]|$)|printenv[[:space:]]+[A-Z_]*(TOKEN|SECRET|KEY)'; then
  block "printing a secret exposes it in the transcript."
fi

exit 0
