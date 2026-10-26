#!/usr/bin/env bash
# Offline tests for .claude/hooks/guard.sh.
#
#   .claude/hooks/test_guard.sh                test the guard beside this file
#   GUARD=<path> .claude/hooks/test_guard.sh   test another copy of the guard
#
# Plain bash: runs under macOS /bin/bash 3.2 and bash 5. The guard runs
# under the same bash ($BASH), with fake gh and git first on PATH and the
# system directories next, so a macOS run exercises the BSD tools. The
# fakes answer from fixture files and log every call; nothing touches the
# network. Exits non-zero if any case fails.
set -u

here=$(cd "$(dirname "$0")" && pwd) || exit 1
guard=${GUARD:-$here/guard.sh}
sh_bin=${BASH:-bash}
work=$(mktemp -d "${TMPDIR:-/tmp}/guard-test.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
sys_path=/usr/bin:/bin:/usr/sbin:/sbin:$PATH

H=1111111111111111111111111111111111111111 # PR head
O=2222222222222222222222222222222222222222 # older head / existing remote branch tip
M=3333333333333333333333333333333333333333 # origin/main
REPO=AEtherForge-Zero-Labs/overdrive

pass=0
fail=0
rc=0

# ---------------------------------------------------------------- fakes

mkdir -p "$work/bin" "$work/fx"

cat >"$work/bin/gh" <<'FAKE'
#!/usr/bin/env bash
# Fake gh: answers from fixture files in $FAKE_DIR, logs every call.
d=$FAKE_DIR
printf 'gh %s\n' "$*" >>"$d/calls.log"
jqexpr=
args=()
while [ $# -gt 0 ]; do
  case $1 in
    --jq | -q) jqexpr=$2; shift 2 ;;
    *) args[${#args[@]}]=$1; shift ;;
  esac
done
emit() {
  [ -f "$1" ] || { echo "fake gh: no fixture ${1##*/}" >&2; exit 1; }
  # Like gh --jq, print null as an empty line.
  if [ -n "$jqexpr" ]; then jq -r "$jqexpr" <"$1" | sed 's/^null$//'; else cat "$1"; fi
}
case "${args[0]:-} ${args[1]:-}" in
  "pr view") emit "$d/pr_view.json" ;;
  "issue view") ref=${args[2]:-}; emit "$d/issue_${ref##*/}.json" ;;
  "api "*)
    ep=
    for a in "${args[@]}"; do
      case $a in api | -*) ;; *) ep=$a; break ;; esac
    done
    case $ep in
      repos/*/pulls/*/files) emit "$d/pr_files.json" ;;
      repos/*/issues/*/comments) emit "$d/comments.json" ;;
      *) echo "fake gh: unexpected api call $ep" >&2; exit 1 ;;
    esac ;;
  *) echo "fake gh: unexpected call: $*" >&2; exit 1 ;;
esac
FAKE

cat >"$work/bin/git" <<'FAKE'
#!/usr/bin/env bash
# Fake git: answers from fixture files in $FAKE_DIR, logs every call.
d=$FAKE_DIR
printf 'git %s\n' "$*" >>"$d/calls.log"
[ "${1:-}" = -C ] && shift 2
sub=${1:-}
shift
case $sub in
  ls-remote)
    [ -f "$d/remote_refs" ] || { echo "fake git: cannot reach origin" >&2; exit 128; }
    exitcode=0 heads=0 remote= found=0
    pats=()
    for a in "$@"; do
      case $a in
        --exit-code) exitcode=1 ;;
        --heads | -h) heads=1 ;;
        -*) ;;
        *) if [ -z "$remote" ]; then remote=$a; else pats[${#pats[@]}]=$a; fi ;;
      esac
    done
    [ "$remote" = origin ] || exit 128
    while IFS=$'\t' read -r sha ref; do
      [ -n "$ref" ] || continue
      if [ $heads = 1 ]; then case $ref in refs/heads/*) ;; *) continue ;; esac; fi
      hit=0
      if [ ${#pats[@]} -eq 0 ]; then hit=1; else
        for p in "${pats[@]}"; do case $ref in "$p" | */"$p") hit=1 ;; esac; done
      fi
      [ $hit = 1 ] && { printf '%s\t%s\n' "$sha" "$ref"; found=1; }
    done <"$d/remote_refs"
    [ $exitcode = 1 ] && [ $found = 0 ] && exit 2
    exit 0 ;;
  log)
    range= nomerges=0
    for a in "$@"; do
      case $a in
        --no-merges) nomerges=1 ;;
        --) break ;;
        -*) ;;
        *) [ -z "$range" ] && range=$a ;;
      esac
    done
    printf 'range %s\n' "$range" >>"$d/calls.log"
    f=$d/log@$(printf '%s' "$range" | tr '/' '_')
    [ -f "$f" ] || f=$d/log
    [ -f "$f" ] || { echo "fatal: bad revision '$range'" >&2; exit 128; }
    while IFS= read -r line; do
      case $line in
        merge:*) [ $nomerges = 1 ] || printf '%s\n' "${line#merge:}" ;;
        *) printf '%s\n' "$line" ;;
      esac
    done <"$f"
    exit 0 ;;
  show-ref)
    ref=
    for a in "$@"; do case $a in -*) ;; *) ref=$a ;; esac; done
    case $ref in
      refs/heads/*) grep -qx -- "${ref#refs/heads/}" "$d/branches" 2>/dev/null && exit 0 ;;
      refs/tags/*) grep -qx -- "${ref#refs/tags/}" "$d/tags" 2>/dev/null && exit 0 ;;
    esac
    exit 1 ;;
  symbolic-ref)
    [ -f "$d/current_branch" ] || exit 1
    cat "$d/current_branch" ;;
  rev-parse)
    case "$*" in
      *"@{push}"*) [ -f "$d/push_ref" ] || { echo "fatal: no push destination" >&2; exit 128; }; cat "$d/push_ref" ;;
      *"@{u}"*) [ -f "$d/upstream" ] || exit 128; cat "$d/upstream" ;;
      *origin/main*) grep -q refs/heads/main "$d/remote_refs" 2>/dev/null || exit 1; echo 3333333333333333333333333333333333333333 ;;
      *) exit 128 ;;
    esac ;;
  config)
    key= regexp=0
    for a in "$@"; do
      case $a in --get-regexp) regexp=1 ;; -*) ;; *) [ -z "$key" ] && key=$a ;; esac
    done
    if [ $regexp = 1 ]; then f=$d/config_regexp; else f=$d/config_$key; fi
    [ -f "$f" ] || exit 1
    cat "$f" ;;
  *) echo "fake git: unexpected call: $sub $*" >&2; exit 1 ;;
esac
FAKE
chmod +x "$work/bin/gh" "$work/bin/git"

# ------------------------------------------------------------- fixtures

# review <association> <verdict> <head>: one page with one review comment.
review() {
  printf '[{"author_association":"%s","body":"Checked the diff.\\r\\n\\r\\nReview-Verdict: %s\\r\\nReviewed-Head: %s\\r\\n"}]\n' "$1" "$2" "$3"
}

# new_fx <name>: a fixture where the canonical merge of PR 5 and a
# canonical push of tx-5-x both pass every check.
new_fx() {
  fx=$work/fx/$1
  rm -rf "$fx"
  mkdir -p "$fx"
  cat >"$fx/pr_view.json" <<EOF
{"number":5,"state":"OPEN","baseRefName":"main","headRefOid":"$H",
 "isCrossRepository":false,
 "headRepository":{"name":"overdrive","nameWithOwner":"$REPO"},
 "url":"https://github.com/$REPO/pull/5",
 "changedFiles":1,
 "files":[{"path":"src/render.c"}],
 "labels":[{"name":"task"}],
 "statusCheckRollup":[
  {"__typename":"CheckRun","name":"guard-tests","status":"COMPLETED","conclusion":"SUCCESS"},
  {"__typename":"CheckRun","name":"docs-form-check","status":"COMPLETED","conclusion":"SKIPPED"},
  {"__typename":"StatusContext","context":"legacy","state":"SUCCESS"}],
 "closingIssuesReferences":[{"number":19,"url":"https://github.com/$REPO/issues/19"}]}
EOF
  printf '%s\n' '[{"filename":"src/render.c","status":"modified"}]' >"$fx/pr_files.json"
  review MEMBER PASS "$H" >"$fx/comments.json"
  printf '%s\n' '{"labels":[{"name":"task"}]}' >"$fx/issue_19.json"
  printf '%s\t%s\n' "$M" refs/heads/main "$O" refs/heads/feat/9-y >"$fx/remote_refs"
  printf '%s\n' tx-5-x main feature release claude/foo >"$fx/branches"
  printf '%s\n' v1.0 release >"$fx/tags"
  printf '%s\n' tx-5-x >"$fx/current_branch"
  printf '%s\n' refs/remotes/origin/feat/9-y >"$fx/push_ref"
  printf 'abc1234\tClaude:claude-opus-5-5\tfeat: add a thing\n' >"$fx/log"
  : >"$fx/calls.log"
}

# edit <file> <jq filter>: rewrite a JSON fixture of the current fixture.
edit() { jq "$2" "$fx/$1" >"$fx/$1.new" && mv "$fx/$1.new" "$fx/$1"; }

# --------------------------------------------------------------- runner

run_guard() {
  : >"$fx/calls.log"
  PATH="$work/bin:$sys_path" FAKE_DIR="$fx" "$sh_bin" "$guard" <"$1" >"$work/out" 2>"$work/err"
  rc=$?
}

failed() {
  fail=$((fail + 1))
  printf 'FAIL: %s\n' "$1"
  [ -s "$work/err" ] && sed -n '1,2s/^/        /p' "$work/err"
  return 0
}

check() { # check <A|B> <description>
  case "$1:$rc" in
    A:0 | B:2) pass=$((pass + 1)) ;;
    *) failed "[$1, exit $rc] $2" ;;
  esac
}

t() { # t <A|B> <description> <command>
  jq -n --arg c "$3" '{tool_name:"Bash",tool_input:{command:$c}}' >"$work/in.json" || exit 1
  run_guard "$work/in.json"
  check "$1" "$2"
}

raw() { # raw <A|B> <description> <hook input text>
  printf '%s' "$3" >"$work/in.json"
  run_guard "$work/in.json"
  check "$1" "$2"
}

called() { # called <description> <fixed string expected in the call log>
  if grep -qF -- "$2" "$fx/calls.log"; then pass=$((pass + 1)); else failed "$1: no call matching '$2'"; fi
}

no_calls() { # no_calls <description>
  if [ -s "$fx/calls.log" ]; then failed "$1: made lookups: $(head -1 "$fx/calls.log")"; else pass=$((pass + 1)); fi
}

says() { # says <description> <fixed string expected in the block message>
  if grep -qF -- "$2" "$work/err"; then pass=$((pass + 1)); else failed "$1: message lacks '$2'"; fi
}

# ================================================================ cases

new_fx base

# --- the guard itself parses ----------------------------------------
if "$sh_bin" -n "$guard" 2>"$work/err"; then pass=$((pass + 1)); else failed "$sh_bin -n guard.sh"; fi

# --- fails closed on its input (#19: fails open) --------------------
raw B "input that is not JSON" 'not json'
raw B "empty input" ''
raw B "input without tool_input" '{}'
raw B "command absent" '{"tool_name":"Bash","tool_input":{}}'
raw B "command is a number" '{"tool_name":"Bash","tool_input":{"command":42}}'
raw B "command is null" '{"tool_name":"Bash","tool_input":{"command":null}}'
raw B "command is an array" '{"tool_name":"Bash","tool_input":{"command":["gh","pr","merge","5"]}}'
raw B "input is an array" '[1,2]'
raw B "two JSON documents" '{"tool_input":{"command":"ls"}} {"tool_input":{"command":"ls"}}'
raw A "empty-string command passes" '{"tool_name":"Bash","tool_input":{"command":""}}'

mkdir -p "$work/nojq"
for tool in cat tr sed grep awk; do
  p=$(command -v "$tool") && ln -s "$p" "$work/nojq/$tool"
done
jq -n '{tool_name:"Bash",tool_input:{command:"git status"}}' >"$work/in.json"
PATH=$work/nojq FAKE_DIR=$fx "$sh_bin" "$guard" <"$work/in.json" >"$work/out" 2>"$work/err"
rc=$?
check B "jq missing from PATH"
says "jq missing from PATH" "jq"

# --- ordinary work passes, with no lookups ---------------------------
for c in \
  'git status' \
  'git add src/render.c tests/render_test.c' \
  'git commit -F /work/msg.txt' \
  "git log origin/main..HEAD --format='%h %s%n%(trailers:key=Assisted-by)%(trailers:key=Co-authored-by)'" \
  'gh pr create --base main --title "feat(render): add cell diff" --body-file /work/pr.md' \
  'gh issue comment 5 --body-file /work/comment.md' \
  'gh pr view 5' \
  'gh pr checks 5' \
  'gh issue list --label task --state open --json number,title,labels,assignees' \
  'gh issue edit 5 --add-label needs-human' \
  'gh api repos/AEtherForge-Zero-Labs/overdrive/pulls/5/comments' \
  'gh api -X GET search/issues -f q=repo:AEtherForge-Zero-Labs/overdrive' \
  "gh api graphql -f query='query { viewer { login } }'" \
  'gh auth status' \
  'gh ruleset list' \
  'gh ruleset view 25908557' \
  '"/Users/x/.claude/plugins/cache/claude-automata/tx/0.18.1/bin/tx-hook" seed' \
  '"/Users/x/.claude/plugins/cache/claude-automata/tx/0.18.1/bin/tx-hook" openspec validate i5-x --strict --no-interactive --json' \
  'cmake --preset debug' \
  'cmake --build --preset debug' \
  'ctest --preset debug' \
  'printenv HOME' \
  'env CC=clang cmake --preset debug' \
  'echo done'; do
  t A "ordinary: $c" "$c"
  no_calls "ordinary: $c"
done

# --- merges: canonical form only (#19: PR targeting, merge timing) ---
t A "canonical merge" 'gh pr merge 5 --squash --delete-branch'
called "canonical merge looks up exactly PR 5" "gh pr view 5"
called "canonical merge lists the files of PR 5" "repos/$REPO/pulls/5/files"
called "canonical merge reads the comments of PR 5" "repos/$REPO/issues/5/comments"
t A "canonical merge, flags reordered" 'gh pr merge 5 -d --merge'
t A "canonical merge, rebase" 'gh pr merge 5 --rebase --delete-branch'
t A "canonical merge, no flags" 'gh pr merge 5'
t A "canonical merge, surrounding whitespace" '  gh pr merge 5 --squash  '
t B "number after a flag" 'gh pr merge --squash 17'
t B "no number" 'gh pr merge --squash'
t B "URL instead of number" "gh pr merge https://github.com/$REPO/pull/5 --squash"
t B "global -R before pr" "gh -R $REPO pr merge 5 --squash"
t B "global --repo before pr" "gh --repo $REPO pr merge 5 --squash"
t B "--repo= after merge" "gh pr merge 5 --squash --repo=$REPO"
t B "env prefix" "GH_REPO=$REPO gh pr merge 5 --squash"
t B "bash -c wrapper" 'bash -c "gh pr merge 5 --squash"'
t B "eval wrapper" "eval 'gh pr merge 5 --squash'"
t B "absolute path to gh" '/opt/homebrew/bin/gh pr merge 5 --squash'
t B "uppercase program name" 'GH pr merge 5 --squash'
t B "quoted program name" '"gh" pr merge 5 --squash'
t B "chained after another command" 'git status && gh pr merge 5 --squash'
t B "chained before another command" 'gh pr merge 5 --squash; echo merged'
t B "--admin" 'gh pr merge 5 --squash --admin'
says "--admin" "admin"
t B "--auto" 'gh pr merge 5 --auto --squash'
says "--auto" "auto"
t B "--subject" 'gh pr merge 5 --squash --subject x'
t B "--body" "gh pr merge 5 --squash --body 'x'"
t B "--match-head-commit" "gh pr merge 5 --squash --match-head-commit $H"
t B "two methods" 'gh pr merge 5 --squash --rebase'
t B "repeated flag" 'gh pr merge 5 -d -d'
t B "leading zero number" 'gh pr merge 05 --squash'
t B "text that mentions a merge" 'gh pr comment 5 --body "ready for gh pr merge"'
says "text that mentions a merge" "file"

# --- the merge gate on PR 5 ------------------------------------------
gate() { # gate <A|B> <description>: run the canonical merge on the current fixture
  t "$1" "gate: $2" 'gh pr merge 5 --squash --delete-branch'
}
new_fx g; edit pr_view.json '.state = "CLOSED"'; gate B "PR not open"
new_fx g; edit pr_view.json '.baseRefName = "release"'; gate B "base is not main"
new_fx g; edit pr_view.json '.isCrossRepository = true'; gate B "cross-repository PR"
new_fx g; edit pr_view.json '.headRepository.nameWithOwner = "someone/overdrive"'; gate B "head repository differs"
new_fx g; edit pr_view.json '.number = 17'; gate B "gh answered for another PR"
new_fx g; edit pr_view.json '.headRefOid = "abc"'; gate B "unreadable head sha"
new_fx g; rm "$fx/pr_view.json"; gate B "PR lookup fails"
new_fx g; edit pr_view.json '.labels += [{"name":"needs-human"}]'; gate B "PR labeled needs-human"
new_fx g; printf '%s\n' '{"labels":[{"name":"needs-human"}]}' >"$fx/issue_19.json"; gate B "closing issue labeled needs-human"
new_fx g; rm "$fx/issue_19.json"; gate B "closing-issue lookup fails"
new_fx g; printf '%s\n' '{"labels":[]}' >"$fx/issue_19.json"; gate A "closing issue with no labels at all"
new_fx g; printf '%s\n' '{"labels":' >"$fx/issue_19.json"; gate B "closing-issue labels are not JSON"
new_fx g; edit pr_view.json '.statusCheckRollup = []'; gate B "no checks"
new_fx g; edit pr_view.json '.statusCheckRollup[0].conclusion = "FAILURE"'; gate B "failing check"
new_fx g; edit pr_view.json '.statusCheckRollup[0] |= (.status = "IN_PROGRESS" | .conclusion = "")'; gate B "pending check"
new_fx g; edit pr_view.json '.statusCheckRollup[2].state = "PENDING"'; gate B "pending status"
new_fx g; edit pr_view.json '.statusCheckRollup[0].conclusion = "NEUTRAL"'; gate A "neutral and skipped checks"
new_fx g
printf '%s\n' '[{"filename":".claude/hooks/guard.sh","status":"modified"}]' >"$fx/pr_files.json"
edit pr_view.json '.files = [{"path":".claude/hooks/guard.sh"}]'
gate B "reserved path changed"
new_fx g
printf '%s\n' '[{"filename":"x.sh","previous_filename":".claude/settings.json","status":"renamed"}]' >"$fx/pr_files.json"
edit pr_view.json '.files = [{"path":"x.sh"}]'
gate B "reserved path renamed away"
new_fx g
printf '%s\n' '[{"filename":"LICENSE","status":"removed"}]' >"$fx/pr_files.json"
gate B "reserved path deleted"
new_fx g
printf '%s\n' '[{"filename":"src/a.c"}][{"filename":".specify/memory/constitution.md"}]' >"$fx/pr_files.json"
edit pr_view.json '.changedFiles = 2'
gate B "reserved path on the second page"
new_fx g; edit pr_view.json '.changedFiles = 101'; gate B "file list shorter than changedFiles"
new_fx g; rm "$fx/pr_files.json"; gate B "file lookup fails"

# --- review bound to the head (#19: review binding) ------------------
new_fx g; printf '[]\n' >"$fx/comments.json"; gate B "no review comment"
new_fx g; review MEMBER PASS "$O" >"$fx/comments.json"; gate B "PASS names an older head"
new_fx g; review NONE PASS "$H" >"$fx/comments.json"; gate B "PASS from an outside commenter"
new_fx g
printf '[{"author_association":"MEMBER","body":"Review-Verdict: PASS"}]\n' >"$fx/comments.json"
gate B "PASS without Reviewed-Head"
new_fx g
printf '[{"author_association":"MEMBER","body":"Looks good, PASS for %s"}]\n' "$H" >"$fx/comments.json"
gate B "verdict not in protocol form"
new_fx g
{ review MEMBER PASS "$H"; review NONE FAIL "$H"; } >"$fx/comments.json"
gate B "FAIL for the same head on a later page"
new_fx g
{ review MEMBER FAIL "$O"; review OWNER PASS "$H"; } >"$fx/comments.json"
gate A "FAIL for an older head, PASS for this head"
new_fx g; review COLLABORATOR PASS "$H" >"$fx/comments.json"; gate A "PASS from a collaborator"
new_fx g
printf '[{"author_association":"MEMBER","body":"Review-Verdict: PASS\\nReviewed-Head: %s"}]\n' \
  "$(printf '%s' "$H" | tr '1' 'A')" >"$fx/comments.json"
gate B "Reviewed-Head names a different sha"
new_fx g; rm "$fx/comments.json"; gate B "comment lookup fails"

# --- pushes (#19: push to main, --all/--prune, trailers) -------------
new_fx p
t A "push src:dst to a new branch" 'git push -u origin tx-5-x:feat/5-x'
called "new branch: commits since origin/main" "range origin/main..tx-5-x"
t A "push to an existing branch" 'git push origin tx-5-x:feat/9-y'
called "existing branch: commits since the remote tip" "range $O..tx-5-x"
t A "push --set-upstream" 'git push --set-upstream origin tx-5-x:feat/5-x'
t A "push a branch by name" 'git push origin feature'
called "branch by name: its own commits" "range origin/main..feature"
t A "push HEAD" 'git push -u origin HEAD'
called "push HEAD: current branch" "range origin/main..HEAD"
t A "push with -C" 'git -C /work/repo push -u origin tx-5-x:feat/5-x'
t A "bare push to its upstream" 'git push'
called "bare push: commits since the upstream tip" "range $O..tx-5-x"
called "trailer check skips merge commits" "--no-merges"

new_fx p
printf 'merge:m111111\t\tMerge branch main into tx-5-x\nabc1234\tClaude:x\tfeat: x\n' >"$fx/log"
t A "an update-branch merge commit without trailers" 'git push -u origin tx-5-x:feat/5-x'

new_fx p
printf 'abc1234\tClaude:x\tfeat: x\ndef5678\t\tfix: y\n' >"$fx/log"
t B "a pushed commit without a trailer" 'git push -u origin tx-5-x:feat/5-x'
says "a pushed commit without a trailer" "def5678"

new_fx p
printf 'def5678\t\tfix: y\n' >"$fx/log@origin_main..feature"
t B "trailer check covers the pushed source, not HEAD" 'git push -u origin feature:feat/5-x'

new_fx p; rm "$fx/log"; t B "git log fails" 'git push -u origin tx-5-x:feat/5-x'
new_fx p; rm "$fx/remote_refs"; t B "origin unreachable" 'git push -u origin tx-5-x:feat/5-x'

new_fx p
t B "dst main" 'git push origin tx-5-x:main'
t B "dst refs/heads/main" 'git push origin tx-5-x:refs/heads/main'
t B "dst heads/main" 'git push origin tx-5-x:heads/main'
t B "dst Main" 'git push origin tx-5-x:Main'
t B "HEAD:main" 'git push origin HEAD:main'
t B "main by name" 'git push origin main'
t B "-C with refs/heads/main" 'git -C /work/repo push origin tx-5-x:refs/heads/main'
t B "dst HEAD" 'git push origin tx-5-x:HEAD'
t B "--all" 'git push --all origin'
t B "--prune" 'git push --prune origin tx-5-x:feat/5-x'
t B "--mirror" 'git push --mirror origin'
t B "--tags" 'git push origin --tags'
t B "--follow-tags" 'git push --follow-tags origin tx-5-x:feat/5-x'
t B "--force" 'git push --force origin tx-5-x:feat/5-x'
t B "-f" 'git push -f origin tx-5-x:feat/5-x'
t B "-uf" 'git push -uf origin tx-5-x:feat/5-x'
t B "--force-with-lease" 'git push --force-with-lease origin tx-5-x:feat/5-x'
t B "+refspec" 'git push origin +tx-5-x:feat/5-x'
t B "delete by :dst" 'git push origin :feat/9-y'
t B "--delete" 'git push --delete origin feat/9-y'
t B "-d" 'git push -d origin feat/9-y'
t B "tag destination" 'git push origin tx-5-x:refs/tags/v1.0'
t B "tag-like destination" 'git push origin tx-5-x:v2'
t B "tag by name" 'git push origin v1.0'
t B "source that is also a tag" 'git push origin release:feat/5-x'
t B "source that is not a local branch" 'git push origin nosuch:feat/5-x'
t B "claude/* destination" 'git push origin tx-5-x:claude/foo'
t B "claude/* branch" 'git push -u origin claude/foo'
t B "another remote" 'git push upstream tx-5-x:feat/5-x'
t B "-c before push" 'git -c push.default=matching push'
t B "env prefix" 'GIT_DIR=/x git push origin tx-5-x:feat/5-x'
t B "bash -c wrapper" 'bash -c "git push origin tx-5-x:main"'
t B "chained" 'git commit -F /work/m && git push origin tx-5-x:feat/5-x'
t B "extra flag" 'git push --no-verify origin tx-5-x:feat/5-x'
t B "uppercase program name" 'GIT push origin tx-5-x:main'

new_fx p; printf 'true\n' >"$fx/config_push.followTags"; t B "push.followTags set" 'git push -u origin tx-5-x:feat/5-x'
new_fx p; printf 'matching\n' >"$fx/config_push.default"; t B "bare push with push.default=matching" 'git push'
new_fx p; printf 'remote.origin.push +refs/heads/*:refs/heads/*\n' >"$fx/config_regexp"; t B "bare push with configured refspecs" 'git push'
new_fx p; printf 'refs/remotes/origin/main\n' >"$fx/push_ref"; t B "bare push whose upstream is main" 'git push'
new_fx p; printf 'refs/remotes/fork/x\n' >"$fx/push_ref"; t B "bare push to another remote" 'git push'
new_fx p; rm "$fx/push_ref"; t B "bare push without a destination" 'git push'
new_fx p; rm "$fx/current_branch"; t B "push HEAD while detached" 'git push origin HEAD:feat/5-x'

new_fx p
printf '%s\t%s\n' "$O" refs/heads/feat/9-y >"$fx/remote_refs"
t A "first push that creates main" 'git push -u origin main'
called "first push of main: whole history" "range main"

# --- gh api (#19: --method=DELETE, repo PATCH, statuses, GraphQL) ----
for c in \
  "gh api repos/$REPO/issues/5/comments -f body=hello" \
  "gh api -X POST repos/$REPO/issues/5/comments -F body=@/work/c.md" \
  "gh api -X PATCH repos/$REPO/issues/comments/123 -f body=edited" \
  "gh api 'repos/{owner}/{repo}/issues/5/labels' -f 'labels[]=agent:claimed'" \
  "gh api -X DELETE repos/$REPO/issues/5/labels/agent:claimed" \
  "gh api repos/$REPO/issues/5/reactions -f content=+1" \
  "gh api repos/$REPO/issues/comments/123/reactions -f content=heart" \
  "gh api -X DELETE repos/$REPO/issues/comments/123/reactions/9" \
  "gh api --method=POST repos/$REPO/rulesets --input /work/ruleset.json" \
  "gh api --method POST /repos/$REPO/rulesets --input /work/ruleset.json"; do
  t A "allowlisted api: $c" "$c"
  no_calls "allowlisted api: $c"
done
for c in \
  "gh api -X DELETE repos/$REPO/git/refs/heads/feature" \
  "gh api --method=DELETE repos/$REPO/git/refs/heads/feature" \
  "gh api --method=delete repos/$REPO/git/refs/heads/feature" \
  "gh api -XDELETE repos/$REPO/branches/main/protection" \
  "gh api -X delete repos/$REPO/git/refs/heads/feature" \
  "gh api -X PATCH repos/$REPO -f visibility=private" \
  "gh api --method PATCH repos/$REPO -F private=true" \
  "gh api repos/$REPO/statuses/$H -f state=success -f context=guard-tests" \
  "gh api repos/$REPO/check-runs -f name=guard-tests -f head_sha=$H" \
  "gh api -X POST repos/$REPO/check-suites/1/rerequest" \
  "gh api -X PUT repos/$REPO/pulls/5/merge" \
  "gh api repos/$REPO/merges -f base=main -f head=feature" \
  "gh api repos/$REPO/actions/workflows/guard.yml/dispatches -f ref=main" \
  "gh api -X PUT repos/$REPO/rulesets/1 --input /work/r.json" \
  "gh api -X PATCH repos/$REPO/rulesets/1 --input /work/r.json" \
  "gh api -X DELETE repos/$REPO/rulesets/1" \
  "gh api -X PUT repos/$REPO/branches/main/protection --input /work/p.json" \
  "gh api -X POST repos/$REPO/rulesets/1" \
  "gh api -X DELETE repos/$REPO/issues/5/labels/needs-human" \
  "gh api -X DELETE repos/$REPO/issues/5/labels" \
  "gh api -X PUT repos/$REPO/issues/5/labels -f 'labels[]=task'" \
  "gh api repos/$REPO/issues/5/comments -f body=x | cat" \
  "bash -c \"gh api -X PATCH repos/$REPO -f visibility=private\"" \
  "gh -R $REPO api -X PATCH repos/$REPO -f visibility=private" \
  "gh api graphql -f query='mutation { mergePullRequest(input: {pullRequestId: \"x\"}) { clientMutationId } }'" \
  "gh api graphql -F query=@/work/q.graphql" \
  "gh api -X GET graphql -f query='mutation { x }'"; do
  t B "api: $c" "$c"
done

# --- human-needed gh commands, any spelling --------------------------
for c in \
  'gh release create v1.0' \
  "gh -R $REPO release create v1.0" \
  "bash -c 'gh release delete v1.0 --yes'" \
  '/usr/local/bin/gh release upload v1.0 x.tgz' \
  'gh release edit v1.0 --draft=false' \
  "gh repo delete $REPO --yes" \
  'gh repo edit --visibility private' \
  'gh repo rename overdrive2' \
  'gh repo archive --yes' \
  'gh secret list' \
  'gh secret set X --body y' \
  'gh variable set X --body y' \
  'gh org list' \
  'gh issue delete 5 --yes' \
  'gh issue transfer 5 other/repo' \
  'gh label delete needs-human --yes' \
  'gh label edit needs-human --name later' \
  'gh workflow disable guard.yml' \
  'gh workflow run guard.yml' \
  'gh ruleset create' \
  'gh auth login' \
  'gh auth logout' \
  'gh auth refresh -s delete_repo' \
  'gh auth setup-git' \
  'gh auth switch' \
  'gh auth token' \
  'gh auth status -t' \
  'gh auth status --show-token' \
  'gh auth status -at' \
  'gh issue edit 19 --remove-label needs-human' \
  'gh pr edit 5 --remove-label "agent:claimed,needs-human"' \
  "gh alias set mm 'pr merge'"; do
  t B "human-needed: $c" "$c"
done

# --- secrets (#19: secrets) ------------------------------------------
for c in \
  'printenv OVD_FAKE_TOKEN' \
  'printenv GITHUB_TOKEN' \
  'printenv AWS_SECRET_ACCESS_KEY' \
  'printenv DB_PASSWORD' \
  'printenv API_KEY' \
  'env' \
  'printenv' \
  'ENV' \
  '/usr/bin/env' \
  'env | grep GH' \
  'env -u HOME' \
  'bash -c env' \
  'export -p' \
  'echo $GH_TOKEN' \
  'echo "${GITHUB_TOKEN}"' \
  'curl -H "Authorization: token ${GH_TOKEN}" https://api.github.com/user' \
  'echo ${MY_TOKEN:-unset}' \
  'cat ~/.config/gh/hosts.yml' \
  'grep oauth_token /Users/x/.config/gh/hosts.yml' \
  'git credential fill' \
  'security find-generic-password -w -s github'; do
  t B "secret: $c" "$c"
done

# --- self-protection -------------------------------------------------
for c in \
  'cat .claude/settings.json' \
  'cat .claude/settings.json 2>/dev/null' \
  'cat .claude/settings.json | jq .hooks' \
  'jq .permissions .claude/settings.json' \
  'grep -n block .claude/hooks/guard.sh' \
  "grep -nE 'a|b' .claude/hooks/guard.sh" \
  'rg -n block .claude/hooks/guard.sh' \
  "sed -n '1,40p' .claude/hooks/guard.sh" \
  'head -20 .claude/hooks/test_guard.sh' \
  'tail -5 .claude/hooks/guard.sh' \
  'wc -l .claude/hooks/guard.sh .claude/hooks/test_guard.sh' \
  'ls -la .claude/hooks' \
  'stat .claude/hooks/guard.sh' \
  'file .claude/hooks/guard.sh' \
  'diff .claude/hooks/guard.sh /work/old_guard.sh' \
  'git diff main -- .claude/hooks/guard.sh | head -50' \
  'git --no-pager diff -- .claude/settings.json' \
  'git log --oneline -- .claude/hooks/guard.sh' \
  'git show origin/main:.claude/hooks/guard.sh' \
  'git status --short .claude' \
  'git blame .claude/settings.json' \
  'bash -n .claude/hooks/guard.sh' \
  '/bin/bash -n .claude/hooks/guard.sh' \
  'shellcheck .claude/hooks/guard.sh' \
  '.claude/hooks/test_guard.sh' \
  'bash .claude/hooks/test_guard.sh' \
  '/bin/bash .claude/hooks/test_guard.sh'; do
  t A "read-only: $c" "$c"
done
for c in \
  'echo x > .claude/hooks/guard.sh' \
  'echo x >> .claude/settings.json' \
  "printf '{}' > .claude/settings.local.json" \
  "echo '{}' > ~/.claude/settings.json" \
  'echo x > .Claude/Hooks/guard.sh' \
  "sed -i '' 's/exit 2/exit 0/' .claude/hooks/guard.sh" \
  "sed -i.bak 's/2/0/' .claude/hooks/guard.sh" \
  "sed -n 'w /work/out' .claude/hooks/guard.sh" \
  "perl -pi -e 's/2/0/' .claude/hooks/guard.sh" \
  "python3 -c \"open('.claude/settings.json','w').write('{}')\"" \
  'cp /dev/null .claude/hooks/guard.sh' \
  'mv /work/x .claude/hooks/guard.sh' \
  'rm .claude/hooks/guard.sh' \
  'ln -sf /usr/bin/true .claude/hooks/guard.sh' \
  'chmod -x .claude/hooks/guard.sh' \
  'truncate -s 0 .claude/hooks/guard.sh' \
  'install -m 755 /work/x .claude/hooks/guard.sh' \
  'dd if=/dev/null of=.claude/hooks/guard.sh' \
  'tee .claude/settings.local.json < /work/x' \
  'cat /work/x | tee .claude/settings.json' \
  'cat .claude/settings.json > /work/copy.json' \
  'git checkout main -- .claude/hooks/guard.sh' \
  'git restore .claude/settings.json' \
  'git rm .claude/hooks/guard.sh' \
  'git mv .claude/hooks/guard.sh /work/x' \
  "jq '.hooks = {}' .claude/settings.json > /work/s && mv /work/s .claude/settings.json" \
  'grep -i exit .claude/hooks/guard.sh' \
  'cat "$(rm .claude/hooks/guard.sh)"' \
  'cat .claude/hooks/guard.sh; rm -rf .claude/hooks' \
  'cd .claude && rm -rf hooks' \
  'rm .claude/hook*/guard.sh' \
  'rm .claude//hooks/guard.sh' \
  'bash -c "echo x > .claude/hooks/guard.sh"' \
  'GUARD=/work/x .claude/hooks/test_guard.sh' \
  './bash -n .claude/hooks/guard.sh' \
  '.claude/hooks/guard.sh < /work/in.json'; do
  t B "modifies protected: $c" "$c"
done

# ============================================================== summary

printf '\n%d passed, %d failed (guard: %s, shell: %s)\n' "$pass" "$fail" "$guard" "$sh_bin"
[ "$fail" -eq 0 ]
