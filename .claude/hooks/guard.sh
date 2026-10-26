#!/usr/bin/env bash
# PreToolUse guard for Claude Code's Bash tool. It enforces mechanically
# the agent rules that AGENTS.md and the constitution (Arts. XIII, XVI)
# state in prose: agents merge only through the Art. XVI gate, never take
# the steps that need a human, never print secrets, and never change the
# guard or its settings. Exit 2 blocks the command and shows Claude the
# reason. It complements the deny rules in .claude/settings.json.
#
# It fails closed: a missing jq, unreadable hook input, or failed lookup
# blocks. Detection is broad (quotes and backslashes removed, wrappers
# such as `bash -c` and `eval` see through); acceptance of merges, pushes,
# and mutating API calls is one exact shape each. It is a tripwire, not a
# sandbox: a command assembled at run time can evade any text matcher.
#
# Portable to macOS /bin/bash 3.2 and BSD tools. Tests:
# .claude/hooks/test_guard.sh.
set -u
set -o pipefail
export LC_ALL=C

block() {
  printf 'Blocked by .claude/hooks/guard.sh: %s\n' "$1" >&2
  exit 2
}

TEXT_HINT="If the command only mentions this in text, put the text in a file (--body-file, -F, or the Write tool)."

# ------------------------------------------------------------ hook input

command -v jq >/dev/null 2>&1 ||
  block "jq is not installed, so the guard cannot read the command; it fails closed. Install jq."
input=$(cat) || block "could not read the hook input."
kind=$(printf '%s' "$input" | jq -r 'if type == "object" and (.tool_input | type) == "object"
  then (.tool_input.command | type) else "invalid" end' 2>/dev/null) ||
  block "the hook input is not valid JSON."
[ "$kind" = string ] || block "the hook input has no string tool_input.command."
cmd=$(printf '%s' "$input" | jq -j '.tool_input.command') ||
  block "could not read the command from the hook input."
[ -z "$cmd" ] && exit 0

# ------------------------------------------------------------ word views

# norm: quotes and backslashes removed, so `"gh" pr`, `g\h`, and
# `bash -c "gh pr merge"` read as the words they run.
norm=$(printf '%s' "$cmd" | tr -d "\\\\\"'") || block "could not normalize the command."
lnorm=$(printf '%s' "$norm" | tr 'A-Z' 'a-z') || block "could not normalize the command."
flat=$(printf '%s\n' "$norm" | sed 's/[;&|()<>`=,{}$]/ /g') || block "could not split the command."
lflat=$(printf '%s\n' "$flat" | tr 'A-Z' 'a-z') || block "could not split the command."
set -f
W=($flat)  # words, original case; subcommands match case-sensitively
L=($lflat) # the same words lowercased; program names match case-insensitively
set +f
nw=${#W[@]}

trim() {
  local s=$1
  s=${s#"${s%%[![:space:]]*}"}
  s=${s%"${s##*[![:space:]]}"}
  printf '%s' "$s"
}

# Quote-aware tokens, one per line: W<TAB>word (unquoted), O<TAB>operator,
# R<TAB>redirection, or X<TAB>unbalanced. Used where command boundaries
# matter: the read-only check and the gh api parser.
read -r -d '' TOKENIZE <<'JQ' || :
def unq:
  gsub("'(?<a>[^']*)'"; "\(.a)")
  | gsub("\"(?<a>(?:[^\"\\\\]|\\\\.)*)\""; "\(.a)")
  | gsub("\\\\(?<a>.)"; "\(.a)");
def re: "(?<o>\\|\\||&&|;;|\\$\\(|<\\(|>\\(|[;&|()`\\n])|(?<r>[0-9]*(?:&>>|&>|>>|>&|>\\||<<<|<<-|<<|<>|<&|>|<))|(?<w>(?:[^\\s'\"\\\\;&|()<>`]|\\\\.|'[^']*'|\"(?:[^\"\\\\]|\\\\.)*\")+)";
(.tool_input.command | sub("^\\s+"; "") | sub("\\s+$"; "")) as $c
| if ($c | gsub(re; "") | test("^\\s*$") | not) then "X\tunbalanced"
  else $c | match(re; "g")
    | if .captures[0].string != null then "O\t" + .captures[0].string
      elif .captures[1].string != null then "R\t" + .captures[1].string
      else "W\t" + (.captures[2].string | unq | gsub("[\\t\\n\\r]"; " ")) end
  end
JQ

tokenized=0
tokenize() {
  [ $tokenized = 1 ] && return 0
  local toks line
  toks=$(printf '%s' "$input" | jq -r "$TOKENIZE") || block "could not tokenize the command."
  TK=()
  TV=()
  nt=0
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    TK[$nt]=${line%%$'\t'*}
    TV[$nt]=${line#*$'\t'}
    nt=$((nt + 1))
  done <<<"$toks"
  tokenized=1
}

# ------------------------------------------------------ self-protection

# Commands that mention the guard or the Claude settings must be
# read-only (Art. XVI: weakening the guard hook needs a human).
ro_fail() { block "only read-only commands may touch the guard hook or the Claude settings (Art. XVI): $1. Read them with the Read tool, cat, grep, sed -n, git diff/log/show, or run .claude/hooks/test_guard.sh."; }

is_test_script() {
  case $1 in .claude/hooks/test_guard.sh | ./.claude/hooks/test_guard.sh | */.claude/hooks/test_guard.sh) return 0 ;; esac
  return 1
}

ro_git() {
  while [ $# -gt 0 ]; do
    case $1 in
      --no-pager) shift ;;
      -C) [ $# -ge 2 ] || return 1; shift 2 ;;
      *) break ;;
    esac
  done
  case ${1:-} in diff | log | show | status | blame) ;; *) return 1 ;; esac
  shift
  local a
  for a in "$@"; do
    case $a in --output* | --ext-diff | --textconv) return 1 ;; esac
  done
  return 0
}

ro_segment() {
  [ ${#SEG[@]} -eq 0 ] && return 0
  set -- "${SEG[@]}"
  local c0=$1 a
  shift
  for a in "$@"; do
    case $a in -i* | --in-place*) ro_fail "'$a' edits in place" ;; esac
  done
  case $c0 in
    cat | head | tail | wc | ls | stat | diff | grep | egrep | fgrep | jq | shellcheck) return 0 ;;
    file)
      for a in "$@"; do case $a in -C | --compile) ro_fail "file -C writes" ;; esac; done
      return 0 ;;
    less)
      for a in "$@"; do case $a in -o* | -O* | --log-file* | --LOG-FILE*) ro_fail "less -o writes" ;; esac; done
      return 0 ;;
    rg)
      for a in "$@"; do case $a in --pre*) ro_fail "rg --pre runs a program" ;; esac; done
      return 0 ;;
    sed)
      if [ "${1:-}" = -n ] && [ $# -ge 2 ] &&
        [[ $2 =~ ^(([0-9]+|\$)(,([0-9]+|\$))?|/[^/]*/(,/[^/]*/)?)p$ ]]; then
        shift 2
        for a in "$@"; do case $a in -*) ro_fail "sed takes only -n '<lines>p'" ;; esac; done
        return 0
      fi
      ro_fail "sed is allowed only as sed -n '<lines>p' <file>" ;;
    bash | /bin/bash)
      [ $# -eq 2 ] && [ "$1" = -n ] && return 0
      [ $# -eq 1 ] && is_test_script "$1" && return 0
      ro_fail "bash may only check syntax (-n) or run the guard tests" ;;
    git)
      ro_git "$@" && return 0
      ro_fail "git may only diff, log, show, status, or blame here" ;;
  esac
  [ $# -eq 0 ] && is_test_script "$c0" && return 0
  ro_fail "'$c0' is not a read-only command"
}

ro_check() {
  case $cmd in *'$('* | *'`'* | *'<('* | *'>('*) ro_fail "command substitution" ;; esac
  tokenize
  [ "${TK[0]:-}" = X ] && ro_fail "unbalanced quotes"
  SEG=()
  local i=0 r target
  while [ $i -lt $nt ]; do
    case ${TK[$i]} in
      W) SEG[${#SEG[@]}]=${TV[$i]} ;;
      O) ro_segment; SEG=() ;;
      R)
        r=${TV[$i]}
        target=
        if [ $((i + 1)) -lt $nt ] && [ "${TK[$((i + 1))]}" = W ]; then
          target=${TV[$((i + 1))]}
          i=$((i + 1))
        fi
        case $r in
          *'<' | *'<<' | *'<<-' | *'<<<' | *'<&') ;;
          *'>&') case $target in [0-9] | -) ;; *) ro_fail "output redirection" ;; esac ;;
          *) [ "$target" = /dev/null ] || ro_fail "output redirection" ;;
        esac ;;
    esac
    i=$((i + 1))
  done
  ro_segment
}

nslash=$(printf '%s' "$lnorm" | sed -e 's#//*#/#g' -e 's#/\./#/#g') || block "could not normalize the command."
re_p1='\.claude/(hooks|settings)'
re_p2='guard\.sh|settings(\.local)?\.json'
re_p3='\.claude/[^[:space:]]*[]*?[{]'
re_p4='\.claude($|[^a-z0-9_-])'
re_p5='(^|[^a-z0-9_.-])(hooks|settings)($|[^a-z0-9_.-])'
if [[ $nslash =~ $re_p1 ]] || [[ $nslash =~ $re_p2 ]] || [[ $nslash =~ $re_p3 ]] ||
  { [[ $nslash =~ $re_p4 ]] && [[ $nslash =~ $re_p5 ]]; }; then
  ro_check
fi

# --------------------------------------------------------------- secrets

SECRET="printing a secret exposes it in the transcript (Art. XIII)."
re_env='(^|[[:space:];&|(){}`])([^[:space:];&|(){}`]*/)?(env|printenv)([[:space:]]+(-u[[:space:]]*[^-[:space:];&|(){}`][^[:space:];&|(){}`]*|-[^[:space:];&|(){}`]+))*[[:space:]]*($|[;&|(){}`])'
re_dump='(^|[;&|(){}`])[[:space:]]*(export([[:space:]]+-p)?|set|declare[[:space:]]+-[px]+|typeset[[:space:]]+-[px]+)[[:space:]]*($|[;&|(){}`])'
re_tokvar='\$\{?[a-z0-9_]*(token|secret|password)'
re_ghcfg='hosts\.yml|\.config/gh($|/|[[:space:]])'
[[ $lnorm =~ $re_env ]] && block "env or printenv without a command prints every variable, tokens included; $SECRET"
[[ $lnorm =~ $re_dump ]] && block "dumping shell variables prints tokens; $SECRET"
[[ $lnorm =~ $re_tokvar ]] && block "expanding a token variable; $SECRET"
[[ $lnorm =~ $re_ghcfg ]] && block "the gh configuration holds credentials and aliases; $SECRET"

# ------------------------------------------------- gh and git, any form

# gh_next <index>: set j to the next word at or after <index> that is not
# a global -R/--repo flag or its value. Fails at the end of the words.
gh_next() {
  j=$1
  while [ $j -lt $nw ]; do
    case ${W[$j]} in
      -R | --repo) j=$((j + 2)) ;;
      -R?*) j=$((j + 1)) ;;
      *) return 0 ;;
    esac
  done
  return 1
}

HUMAN="that needs a human (Art. XVI). Label the issue needs-human."
has_gh=0 has_git=0 has_pr=0 has_merge=0 has_push=0 has_api=0
i=0
while [ $i -lt $nw ]; do
  w=${W[$i]}
  case $w in
    pr) has_pr=1 ;;
    merge) has_merge=1 ;;
    push) has_push=1 ;;
  esac
  case ${L[$i]##*/} in
    git) has_git=1 ;;
    printenv)
      for v in "${L[@]}"; do
        case $v in *token* | *secret* | *key* | *password*) block "$SECRET" ;; esac
      done ;;
    security)
      for v in "${L[@]}"; do
        case $v in find-*-password) block "reading a keychain password; $SECRET" ;; esac
      done ;;
    gh)
      has_gh=1
      s1= s2=
      if gh_next $((i + 1)); then
        s1=${W[$j]}
        gh_next $((j + 1)) && s2=${W[$j]}
      fi
      case "$s1 $s2" in
        "release create" | "release delete" | "release delete-asset" | "release edit" | "release upload")
          block "publishing or changing a release is $HUMAN" ;;
        "repo delete" | "repo edit" | "repo rename" | "repo archive" | "repo unarchive" | "repo transfer")
          block "deleting the repository or changing its settings, visibility, or ownership is $HUMAN" ;;
        "issue delete" | "issue transfer") block "deleting or transferring issues is $HUMAN" ;;
        "label delete" | "label edit") block "deleting or renaming labels is $HUMAN" ;;
        "workflow disable" | "workflow run") block "disabling or dispatching workflows is $HUMAN" ;;
        "alias set" | "alias import") block "gh aliases hide commands from the guard." ;;
      esac
      case $s1 in
        secret | variable | org) block "secrets, variables, and org settings are $HUMAN" ;;
        ruleset) case $s2 in list | view | check | "" | -*) ;; *) block "changing rulesets is $HUMAN" ;; esac ;;
        auth)
          [ "$s2" = status ] || block "gh auth other than 'gh auth status' handles credentials; $SECRET"
          for v in "${W[@]}"; do
            case $v in --show-token* | -t* | -[a-zA-Z]*t*) block "gh auth status with a token flag; $SECRET" ;; esac
          done ;;
        api) has_api=1 ;;
      esac ;;
  esac
  i=$((i + 1))
done

if [ $has_git = 1 ]; then
  for v in "${W[@]}"; do
    case $v in credential | credential-*) block "git credential prints stored credentials; $SECRET" ;; esac
  done
fi

if [ $has_gh = 1 ]; then
  case $lnorm in *needs-human*)
    for v in "${W[@]}"; do
      case $v in --remove-label*) block "removing needs-human is the owner's decision (Art. XVI)." ;; esac
    done ;;
  esac
fi

# ---------------------------------------------------------------- gh api

API_ALLOW="Agents may only comment, label, react, or create a ruleset through gh api (Art. XVI)."
o='[^/]+'
re_comments="^repos/$o/$o/issues/[0-9]+/comments$"
re_comment="^repos/$o/$o/issues/comments/[0-9]+$"
re_labels="^repos/$o/$o/issues/[0-9]+/labels$"
re_label="^repos/$o/$o/issues/[0-9]+/labels/([^/]+)$"
re_reacted="^repos/$o/$o/(issues/[0-9]+|issues/comments/[0-9]+|pulls/comments/[0-9]+|comments/[0-9]+)/reactions"
re_rulesets="^repos/$o/$o/rulesets$"

api_check() {
  tokenize
  [ "${TK[0]:-}" = X ] && block "a mutating gh api call must be a plain command; its quotes do not balance."
  local args=() i=0
  while [ $i -lt $nt ]; do
    case ${TK[$i]} in
      W) args[${#args[@]}]=${TV[$i]} ;;
      R) i=$((i + 1)) ;;
      O) block "a mutating gh api call must run alone, not chained, piped, or wrapped. $API_ALLOW" ;;
    esac
    i=$((i + 1))
  done
  [ ${#args[@]} -ge 2 ] && [ "${args[0]}" = gh ] && [ "${args[1]}" = api ] ||
    block "a mutating gh api call must be a plain 'gh api ...' command (no env prefix, path, global flag, or wrapper). $API_ALLOW"
  set -- "${args[@]}"
  shift 2
  local method= fields=0 fromfile=0 endpoint= npos=0 a v
  while [ $# -gt 0 ]; do
    a=$1
    shift
    case $a in
      -X | --method) [ $# -ge 1 ] || block "gh api $a needs a value."; method=$1; shift ;;
      --method=*) method=${a#--method=} ;;
      -X*) method=${a#-X}; method=${method#=} ;;
      -f | -F | --field | --raw-field)
        [ $# -ge 1 ] || block "gh api $a needs a value."
        fields=1; v=$1; shift
        case ${v#*=} in @*) fromfile=1 ;; esac ;;
      --field=* | --raw-field=*) fields=1; case ${a#*=} in *=@*) fromfile=1 ;; esac ;;
      -f* | -F*) fields=1; case ${a#-?} in *=@*) fromfile=1 ;; esac ;;
      --input) [ $# -ge 1 ] || block "gh api --input needs a value."; fields=1; fromfile=1; shift ;;
      --input=*) fields=1; fromfile=1 ;;
      -H | --header | -q | --jq | -t | --template | --hostname | --cache | -p | --preview)
        [ $# -ge 1 ] || block "gh api $a needs a value."; shift ;;
      --header=* | --jq=* | --template=* | --hostname=* | --cache=* | --preview=* | -H?* | -q?* | -t?* | -p?*) ;;
      -i | --include | --paginate | --silent | --slurp | --verbose) ;;
      -*) block "the guard does not recognize gh api flag '$a'; it fails closed." ;;
      *) endpoint=$a; npos=$((npos + 1)) ;;
    esac
  done
  [ $npos -eq 1 ] || block "could not find the gh api endpoint."
  method=$(printf '%s' "$method" | tr 'a-z' 'A-Z')
  [ -n "$method" ] || { if [ $fields = 1 ]; then method=POST; else method=GET; fi; }
  endpoint=${endpoint#/}
  if [ "$endpoint" = graphql ]; then
    case $lnorm in *mutation*) block "GraphQL mutations change repository state outside the guard's allowlist. $API_ALLOW" ;; esac
    [ $fromfile = 1 ] && block "the guard cannot read a GraphQL query from a file; pass it inline with -f query='...'."
    return 0
  fi
  case $method in GET | HEAD) return 0 ;; esac
  case $method in
    POST)
      [[ $endpoint =~ $re_comments ]] && return 0
      [[ $endpoint =~ $re_labels ]] && return 0
      [[ $endpoint =~ ${re_reacted}$ ]] && return 0
      [[ $endpoint =~ $re_rulesets ]] && return 0 ;;
    PATCH)
      [[ $endpoint =~ $re_comment ]] && return 0 ;;
    DELETE)
      if [[ $endpoint =~ $re_label ]]; then
        v=$(printf '%s' "${BASH_REMATCH[1]}" | tr 'A-Z' 'a-z')
        case $v in *needs-human* | *%*) block "removing needs-human is the owner's decision (Art. XVI)." ;; esac
        return 0
      fi
      [[ $endpoint =~ ${re_reacted}/[0-9]+$ ]] && return 0 ;;
  esac
  block "gh api $method $endpoint is not on the guard's allowlist. $API_ALLOW Merges go through 'gh pr merge'; statuses, check runs, dispatches, repository settings, and protection are the owner's."
}

if [ $has_api = 1 ]; then
  for v in "${W[@]}"; do
    case $v in
      -X* | -[a-zA-Z]*X* | --method* | -f* | -F* | -[a-zA-Z]*[fF]* | --field* | --raw-field* | --input*)
        api_check
        break ;;
    esac
  done
fi

# ----------------------------------------------------------------- merge

# Paths whose changes need the owner (Art. XVI): governance, license,
# ownership, and the guard itself.
reserved='^(\.specify/memory/constitution\.md|LICENSE|\.github/CODEOWNERS|\.claude/hooks/guard\.sh|\.claude/settings\.json)$'

CANON_MERGE="a merge must be exactly 'gh pr merge <N> [--squash|--merge|--rebase] [--delete-branch|-d]', run alone, so the Art. XVI gate checks that PR. $TEXT_HINT"

read -r -d '' VIEW_CHECK <<'JQ' || :
def st: (.conclusion // "") as $c | (if $c == "" then (.state // "PENDING") else $c end) | ascii_upcase;
def green: st as $s | ["SUCCESS", "SKIPPED", "NEUTRAL"] | index($s) != null;
def base_repo: .url | capture("^https://[^/]+/(?<r>[^/]+/[^/]+)/pull/[0-9]+$").r;
if (.number // null) != $n then "gh returned PR #\(.number // "?") instead of #\($n)."
elif .state != "OPEN" then "PR #\($n) is \(.state), not open."
elif .baseRefName != "main" then "PR #\($n) targets \(.baseRefName); agents merge only into main."
elif ((.headRefOid // "") | test("^[0-9a-f]{40}$") | not) then "PR #\($n) has no readable head commit."
elif ((.url // "") | test("^https://[^/]+/[^/]+/[^/]+/pull/[0-9]+$") | not) then "PR #\($n) has no readable URL."
elif .isCrossRepository != false or ((.headRepository.nameWithOwner // "") | ascii_downcase) != (base_repo | ascii_downcase)
  then "PR #\($n) comes from another repository; only this repository's branches merge through the agent gate."
elif (.changedFiles | type) != "number" then "PR #\($n) has no readable file count."
elif ([.labels[]?.name] | index("needs-human")) != null then "PR #\($n) is labeled needs-human (Art. XVI)."
elif ((.statusCheckRollup // []) | length) == 0 then "PR #\($n) has no checks on its head commit; nothing proves it green (Art. XVI)."
elif ([.statusCheckRollup[] | select(green | not)] | length) > 0
  then "checks are not all green on PR #\($n)'s head: " + ([.statusCheckRollup[] | select(green | not) | "\(.name // .context // "?")=\(st)"] | join(", ")) + ". Wait for them or fix the failure (Art. XVI)."
else "OK\t\(base_repo)\t\(.headRefOid)\t\(.changedFiles)" end
JQ

read -r -d '' REVIEW_CHECK <<'JQ' || :
def lines: (.body // "") | split("\n") | map(sub("\r$"; "") | sub("^[ \t]+"; "") | sub("[ \t]+$"; ""));
def names_head: any(lines[]; startswith("Reviewed-Head: ") and (.[15:] | ascii_downcase) == $head);
def says($v): any(lines[]; . == "Review-Verdict: " + $v);
def trusted: (.author_association // "") as $a | ["OWNER", "MEMBER", "COLLABORATOR"] | index($a) != null;
if all(.[]; type == "array") | not then error("unexpected comments payload") else . end
| [add // [] | .[] | select(names_head)] as $mine
| if any($mine[]; says("FAIL")) then "FAIL"
  elif any($mine[]; says("PASS") and trusted) then "PASS"
  else "NONE" end
JQ

merge_gate() {
  local n=$1 view verdict repo head changed pages names count hit issues url labels found
  view=$(gh pr view "$n" --json number,state,baseRefName,headRefOid,isCrossRepository,headRepository,url,changedFiles,labels,statusCheckRollup,closingIssuesReferences) ||
    block "could not read PR #$n to check the merge gate (Art. XVI)."
  verdict=$(printf '%s' "$view" | jq -r --argjson n "$n" "$VIEW_CHECK") ||
    block "could not parse PR #$n to check the merge gate (Art. XVI)."
  case $verdict in
    OK$'\t'*) ;;
    *) block "$verdict" ;;
  esac
  IFS=$'\t' read -r _ repo head changed <<<"$verdict"

  pages=$(gh api --paginate "repos/$repo/pulls/$n/files") ||
    block "could not list PR #$n's files to check for reserved paths (Art. XVI)."
  names=$(printf '%s' "$pages" | jq -rs 'if all(.[]; type == "array") then (add // []) | .[] | .filename, (.previous_filename // empty) else error("unexpected payload") end') ||
    block "could not parse PR #$n's file list."
  count=$(printf '%s' "$pages" | jq -s 'add // [] | length') || block "could not count PR #$n's files."
  [ "$count" -ge "$changed" ] ||
    block "the guard sees $count of PR #$n's $changed changed files, so it cannot rule out reserved paths."
  hit=$(printf '%s\n' "$names" | grep -E "$reserved")
  [ $? -le 1 ] || block "could not check PR #$n's files for reserved paths."
  hit=$(printf '%s' "$hit" | tr '\n' ' ')
  [ -z "$hit" ] ||
    block "PR #$n changes owner-reserved paths ($hit). Label it needs-human for the owner (Art. XVI)."

  pages=$(gh api --paginate "repos/$repo/issues/$n/comments") ||
    block "could not read PR #$n's comments to find its review (Art. XVI)."
  verdict=$(printf '%s' "$pages" | jq -rs --arg head "$head" "$REVIEW_CHECK") ||
    block "could not parse PR #$n's comments."
  case $verdict in
    PASS) ;;
    FAIL) block "a review comment found a defect in PR #$n's head $head (Review-Verdict: FAIL). Push a fix and get a new review." ;;
    *) block "PR #$n has no review of its head: it needs a comment from a member with the lines 'Review-Verdict: PASS' and 'Reviewed-Head: $head' (Art. XVI)." ;;
  esac

  issues=$(printf '%s' "$view" | jq -r '.closingIssuesReferences[]?.url') ||
    block "could not read PR #$n's closing issues."
  for url in $issues; do
    labels=$(gh issue view "$url" --json labels) ||
      block "could not read issue $url to check for needs-human (Art. XVI)."
    found=$(printf '%s' "$labels" | jq -r 'if (.labels | type) == "array" then ([.labels[].name] | index("needs-human") != null) else error("no labels") end') ||
      block "could not parse issue $url's labels."
    case $found in
      false) ;;
      true) block "issue $url is labeled needs-human (Art. XVI)." ;;
      *) block "could not parse issue $url's labels." ;;
    esac
  done
}

merge_check() {
  local t a m= d= n
  t=$(trim "$cmd")
  case " $lnorm " in *" --admin "*) block "--admin bypasses branch protection; that is a human decision (Art. XVI)." ;; esac
  case " $lnorm " in *" --auto "*) block "--auto merges later, after the gate has looked; $CANON_MERGE" ;; esac
  [[ $t =~ ^gh([[:blank:]]+[A-Za-z0-9-]+)+$ ]] || block "$CANON_MERGE"
  set -f
  set -- $t
  set +f
  [ "$1 ${2:-} ${3:-}" = "gh pr merge" ] || block "$CANON_MERGE"
  n=${4:-}
  [[ $n =~ ^[1-9][0-9]*$ ]] || block "$CANON_MERGE"
  shift 4
  for a in "$@"; do
    case $a in
      --squash | --merge | --rebase) [ -z "$m" ] || block "$CANON_MERGE"; m=$a ;;
      --delete-branch | -d) [ -z "$d" ] || block "$CANON_MERGE"; d=$a ;;
      *) block "$CANON_MERGE" ;;
    esac
  done
  merge_gate "$n"
}

[ $has_gh = 1 ] && [ $has_pr = 1 ] && [ $has_merge = 1 ] && { merge_check; exit 0; }

# ------------------------------------------------------------------ push

CANON_PUSH="a push must be exactly 'git [-C <dir>] push [-u|--set-upstream] origin <src>[:<dst>]' or a bare 'git push', run alone. $TEXT_HINT"

push_check() {
  local t cdir= src= dst= srcref d ld remote rsha msha main=0 range log missing rc out
  t=$(trim "$cmd")
  for v in "${W[@]}"; do
    case $v in
      --force* | -[a-zA-Z]*f* | -f* | +*) block "force-pushes rewrite history and need a human (Art. XVI)." ;;
      --delete | -[a-zA-Z]*d* | -d* | :*) block "deleting remote branches needs a human (Art. XVI)." ;;
      --mirror | --all | --prune | --tags | --follow-tags) block "$v pushes or deletes many refs at once, tags included; that needs a human (Art. XVI)." ;;
    esac
  done
  local ref='[A-Za-z0-9_][A-Za-z0-9_./-]*'
  local re_full="^git([[:blank:]]+-C[[:blank:]]+([A-Za-z0-9_./~][A-Za-z0-9_./~-]*))?[[:blank:]]+push([[:blank:]]+(-u|--set-upstream))?[[:blank:]]+origin[[:blank:]]+($ref)(:($ref))?$"
  if [[ $t =~ ^git[[:blank:]]+push$ ]]; then
    src=$(git symbolic-ref -q --short HEAD) || block "HEAD is detached; push with an explicit '<src>:<dst>'."
    out=$(git rev-parse --symbolic-full-name '@{push}' 2>/dev/null) ||
      block "this branch has no push destination; use 'git push -u origin $src:<dst>'."
    case $out in
      refs/remotes/origin/?*) dst=${out#refs/remotes/origin/} ;;
      *) block "a bare git push here would push to $out, not a branch on origin; use the explicit form." ;;
    esac
    out=$(git config --get push.default)
    rc=$?
    case $rc in
      0) case $out in simple | current | upstream) ;; *) block "push.default=$out pushes more than this branch; use the explicit form." ;; esac ;;
      1) ;;
      *) block "could not read push.default." ;;
    esac
    git config --get-regexp '^remote\.origin\.(push|mirror)$' >/dev/null
    case $? in
      0) block "origin has configured push refspecs or mirroring; use the explicit form." ;;
      1) ;;
      *) block "could not read origin's push configuration." ;;
    esac
    srcref=$src
  elif [[ $t =~ $re_full ]]; then
    cdir=${BASH_REMATCH[2]}
    src=${BASH_REMATCH[5]}
    dst=${BASH_REMATCH[7]}
    case $cdir in "~" | "~/"*) cdir=$HOME${cdir#"~"} ;; esac
    if [ "$src" = HEAD ]; then
      out=$(gitc symbolic-ref -q --short HEAD) || block "HEAD is detached; push a branch by name."
      [ -n "$dst" ] || dst=$out
    else
      gitc show-ref --verify --quiet "refs/heads/$src" ||
        block "'$src' is not a local branch; push HEAD or a branch."
      gitc show-ref --verify --quiet "refs/tags/$src"
      case $? in
        1) ;;
        0) block "'$src' is also a tag, and git would push the tag (Art. XVI: tags are releases)." ;;
        *) block "could not check whether '$src' is a tag." ;;
      esac
      [ -n "$dst" ] || dst=$src
    fi
    srcref=$src
  else
    block "$CANON_PUSH"
  fi

  out=$(gitc config --type=bool --get push.followTags)
  case $? in
    0) [ "$out" = true ] && block "push.followTags is set, so this push would publish tags (Art. XVI)." ;;
    1) ;;
    *) block "could not read push.followTags." ;;
  esac

  d=$dst
  case $d in
    refs/heads/*) d=${d#refs/heads/} ;;
    refs/tags/*) block "pushing tags publishes a release, which needs a human (Art. XVI)." ;;
    refs/*) block "push only to branches (refs/heads/...)." ;;
  esac
  ld=$(printf '%s' "$d" | tr 'A-Z' 'a-z')
  case $ld in
    main) main=1 ;;
    head) block "HEAD on origin is main; main only changes through pull requests (Art. XVI)." ;;
    heads/* | tags/* | remotes/* | refs/*) block "'$dst' is ambiguous on origin; name the branch plainly." ;;
    claude/*) block "never push a claude/* branch; /tx:close pushes the transaction branch." ;;
    v[0-9]*) block "'$dst' looks like a release tag, which needs a human (Art. XVI)." ;;
  esac

  remote=$(gitc ls-remote origin "refs/heads/$d" refs/heads/main) ||
    block "could not read origin's branches; the guard needs them to check this push."
  rsha=$(printf '%s\n' "$remote" | awk -F'\t' -v r="refs/heads/$d" '$2 == r { print $1 }')
  msha=$(printf '%s\n' "$remote" | awk -F'\t' '$2 == "refs/heads/main" { print $1 }')
  if [ $main = 1 ] && [ -n "$msha" ]; then
    block "main only changes through pull requests (Art. XVI)."
  fi

  if [ -n "$rsha" ]; then
    range=$rsha..$srcref
  elif [ -n "$msha" ]; then
    range=origin/main..$srcref
  else
    range=$srcref # the first push, which creates main
  fi
  log=$(gitc log --no-merges --format='%h%x09%(trailers:key=Assisted-by,valueonly,separator=%x2C)%(trailers:key=Co-authored-by,valueonly,separator=%x2C)%x09%s' "$range" --) ||
    block "could not list the commits this push sends ($range); fetch origin and retry."
  missing=$(printf '%s\n' "$log" | awk -F'\t' 'NF && $2 == "" { printf "%s %s; ", $1, $3 }')
  [ -z "$missing" ] ||
    block "these commits to push have neither an Assisted-by: nor a Co-Authored-By: trailer: ${missing}Reword them before pushing. If a commit had no AI involvement, ask the human to push it."
}

gitc() {
  if [ -n "${cdir:-}" ]; then git -C "$cdir" "$@"; else git "$@"; fi
}

[ $has_git = 1 ] && [ $has_push = 1 ] && { push_check; exit 0; }

exit 0
