#!/usr/bin/env bash
# PreToolUse guard for Claude Code's Bash tool. It enforces mechanically
# the agent rules that AGENTS.md and the constitution (Arts. XII, XVI)
# state in prose. Exit code 2 blocks the command and shows Claude the
# reason. It complements the deny rules in .claude/settings.json; neither
# replaces the other.
set -u

input=$(cat)
cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty')
[ -z "$cmd" ] && exit 0

block() {
  printf 'Blocked by .claude/hooks/guard.sh: %s\n' "$1" >&2
  exit 2
}
has() { printf '%s' "$cmd" | grep -Eq -- "$1"; }

# 1. Merging, approving, closing, releasing, and repository settings are
#    human decisions (Art. XVI).
if has '(^|[;&|({[:space:]])gh[[:space:]]+pr[[:space:]]+(merge|close)([[:space:]]|$)'; then
  block "agents never merge or close pull requests (AGENTS.md; constitution Art. XVI). Ask the human to do it."
fi
if has '(^|[;&|({[:space:]])gh[[:space:]]+pr[[:space:]]+review([[:space:]]|$)' && has '(--approve|-a)([[:space:]]|$)'; then
  block "agents never approve pull requests (Art. XVI)."
fi
if has '(^|[;&|({[:space:]])gh[[:space:]]+issue[[:space:]]+(close|delete|transfer)([[:space:]]|$)'; then
  block "closing, deleting, or transferring an issue is a human decision (Art. XVI)."
fi
if has '(^|[;&|({[:space:]])gh[[:space:]]+(release[[:space:]]+(create|delete|edit|upload)|repo[[:space:]]+(create|delete|edit|rename|archive|fork)|ruleset|secret|variable)([[:space:]]|$)'; then
  block "releases, repository settings, rulesets, secrets, and variables are outward-facing and human-only (Art. XII)."
fi
if has 'gh[[:space:]]+api' && has '/merge([^a-z_]|$)|/protection|/rulesets|/reviews|(-X|--method)[[:space:]]*["'\'']?(PUT|PATCH|DELETE)'; then
  block "merging, approving, or changing settings through the API is still a human decision (Art. XVI)."
fi

# 2. Tags are releases (Art. XII).
if has '(^|[;&|({[:space:]])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+push([[:space:]]|$)' && has '--tags|refs/tags/|[[:space:]]v[0-9]'; then
  block "pushing tags publishes a release. Ask the human."
fi

# 3. Pushes: no force, no direct pushes to main, and every unpushed commit
#    carries an Assisted-by: trailer (Art. XVI).
if has '(^|[;&|({[:space:]])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+push([[:space:]]|$)'; then
  if has '--force|--force-with-lease|--mirror|--delete|(^|[[:space:]])-[a-zA-Z]*[fd][a-zA-Z]*([[:space:]]|$)|[[:space:]]\+[^[:space:]]+|[[:space:]]:[^[:space:]]+'; then
    block "force-pushes and remote branch deletion are irreversible (Art. XII). Ask the human."
  fi
  if has 'push[^;&|]*([[:space:]]|:)main([[:space:]]|$)'; then
    block "main only changes through pull requests."
  fi
  upstream=$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null) || upstream=origin/main
  missing=$(git log "$upstream..HEAD" --format='%h%x09%(trailers:key=Assisted-by,valueonly,separator=%x2C)%x09%s' 2>/dev/null \
    | awk -F'\t' '$2 == "" { printf "%s %s; ", $1, $3 }')
  if [ -n "$missing" ]; then
    block "these unpushed commits have no Assisted-by: trailer: ${missing}They aren't pushed yet, so reword them to add the trailer before pushing. If a commit had no AI involvement, ask the human to push it themselves."
  fi
fi

# 4. Secrets never reach the transcript (Art. XIII).
if has '(^|[;&|({[:space:]])gh[[:space:]]+auth[[:space:]]+token([[:space:]]|$)|printenv[[:space:]]+[A-Z_]*(TOKEN|SECRET|KEY)'; then
  block "printing a secret exposes it in the transcript."
fi

exit 0
