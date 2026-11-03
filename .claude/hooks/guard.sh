#!/usr/bin/env bash
# PreToolUse guard for Claude Code's Bash tool. It enforces mechanically
# the agent rules that AGENTS.md and the constitution (Arts. XIII, XVI)
# state in prose: agents merge only through the Art. XVI gate, never take
# the steps that need a human, never print secrets, and never change the
# guard or its settings. Exit 2 blocks the command and shows Claude the
# reason. It complements the deny rules in .claude/settings.json.
#
# It is a tripwire, not a security boundary. A command assembled at run
# time (a variable, a script file, a pipe into a program) can evade any
# text matcher. The boundary is the server-side ruleset plus a separate
# agent identity; see docs/process/swarm.md.
#
# Within that limit it fails closed:
# - a missing jq, unreadable hook input, or failed lookup blocks;
# - gh and git are parsed per command: an unknown subcommand, a flag
#   before the subcommand, or an expansion the guard cannot read blocks;
# - a command that mentions gh, git, an environment dump, .claude, or the
#   gh configuration may not also use a construct the guard does not
#   parse (line continuations, $'...', eval, sh -c, and similar);
# - merges, pushes, and mutating API calls are accepted in one exact
#   shape each.
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
PLAIN="Use a plain form the guard can read: literal arguments, text in a file (--body-file, -F, or the Write tool), and separate commands instead of substitutions or wrappers."
SECRET="printing a secret exposes it in the transcript (Art. XIII)."
HUMAN="a step that needs a human (Art. XVI). Label the issue needs-human."

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

# ------------------------------------------------------------ text views

# norm: quotes and backslashes removed, so `"gh" pr`, `g\h`, and
# `bash -c "env"` read as the words they run. lnorm: the same, lowercase.
norm=$(printf '%s' "$cmd" | tr -d "\\\\\"'") || block "could not normalize the command."
lnorm=$(printf '%s' "$norm" | tr 'A-Z' 'a-z') || block "could not normalize the command."
nslash=$(printf '%s' "$lnorm" | sed -e 's#//*#/#g' -e 's#/\./#/#g') || block "could not normalize the command."
flat=$(printf '%s\n' "$norm" | sed 's/[;&|()<>`=,{}$]/ /g') || block "could not split the command."
lflat=$(printf '%s\n' "$flat" | tr 'A-Z' 'a-z') || block "could not split the command."
set -f
W=($flat)  # words, original case
L=($lflat) # the same words lowercased
set +f

# sens: line continuations joined, then quotes, backslashes, and $
# removed, lowercase: what the words could become once the shell has
# expanded them. A command is sensitive if this view names gh, git, an
# environment dump, a merge or push, .claude, or the gh configuration.
bsnl=$'\\\n'
joined=${cmd//"$bsnl"/}
sens=$(printf '%s' "$joined" | tr -d "\\\\'\"\$" | tr 'A-Z' 'a-z') || block "could not normalize the command."
re_sens='(^|[^a-z0-9_.-])(gh|git|env|printenv|declare|typeset|export|set|readonly|compgen|eval|merge|push|credential)([^a-z0-9_-]|$)|\.claude|\.config|hosts\.y|token|secret|password'
is_sens=0
[[ $sens =~ $re_sens ]] && is_sens=1

trim() {
  local s=$1
  s=${s#"${s%%[![:space:]]*}"}
  s=${s%"${s##*[![:space:]]}"}
  printf '%s' "$s"
}

# --------------------------------------------------- always: secrets

re_env='(^|[[:space:];&|(){}`])([^[:space:];&|(){}`]*/)?(env|printenv)([[:space:]]+(-u[[:space:]]*[^-[:space:];&|(){}`][^[:space:];&|(){}`]*|-[^[:space:];&|(){}`]+))*[[:space:]]*($|[;&|(){}`])'
re_dump='(^|[;&|(){}`]|[[:space:]]-[a-z]*c[[:space:]]+)[[:space:]]*(export([[:space:]]+-p)?|set|declare([[:space:]]+-[a-z]+)*|typeset([[:space:]]+-[a-z]+)*|readonly([[:space:]]+-p)?)[[:space:]]*($|[;&|(){}`])'
re_tokvar='\$\{?[a-z0-9_]*(token|secret|password)'
re_ghcfg='hosts\.y|\.config/+gh($|[^a-z0-9_-])|\.config/+[^/[:space:]]*[]*?[]|\.config/*($|[[:space:];&|)<>])'
re_environ='(^|[^a-z_])environ([^a-z_]|$)|%env|process\.env|getenv|\$env([^a-z0-9_]|$)'
[[ $lnorm =~ $re_env ]] && block "env or printenv without a command prints every variable, tokens included; $SECRET"
[[ $lnorm =~ $re_dump ]] && block "dumping shell variables prints tokens; $SECRET"
[[ $lnorm =~ $re_tokvar ]] && block "expanding a token variable; $SECRET"
[[ $lnorm =~ $re_ghcfg ]] && block "the gh configuration holds credentials and aliases; $SECRET"
[[ $lnorm =~ $re_environ ]] && block "reading the process environment prints tokens; $SECRET"
case $cmd in *'${!'*) block "indirect expansion (\${!name}) can print any variable, tokens included; $SECRET" ;; esac
case $lnorm in *dotglob* | *globignore* | *glob_dots* | *globdots*) block "glob options that match hidden files can reach .claude; name paths literally." ;; esac

# ------------------------------------------------------------- tokens

# Quote-aware tokens, one per line: KIND<TAB>FLAGS<TAB>lowercase<TAB>value.
# KIND is W (word, quotes removed), O (operator), R (redirection), or X
# (the quotes do not balance). FLAGS on a word: e if it expands ($ or a
# backtick outside single quotes), s if it substitutes ($( or a
# backtick), g if it has an unquoted glob character.
read -r -d '' TOKENIZE <<'JQ' || :
def unq:
  gsub("'(?<a>[^']*)'"; "\(.a)")
  | gsub("\"(?<a>(?:[^\"\\\\]|\\\\.)*)\""; "\(.a)")
  | gsub("\\\\(?<a>.)"; "\(.a)");
def flags:
  gsub("'[^']*'"; "") as $a
  | (if ($a | test("[$`]")) then "e" else "" end)
  + (if ($a | test("\\$\\(|`")) then "s" else "" end)
  + (if ($a | gsub("\"(?:[^\"\\\\]|\\\\.)*\""; "") | gsub("\\\\."; "") | test("[*?\\[]")) then "g" else "" end);
def re: "(?<o>\\|\\||&&|;;|\\$\\(|<\\(|>\\(|[;&|()`\\n])|(?<r>[0-9]*(?:&>>|&>|>>|>&|>\\||<<<|<<-|<<|<>|<&|>|<))|(?<w>(?:[^\\s'\"\\\\;&|()<>`]|\\\\.|'[^']*'|\"(?:[^\"\\\\]|\\\\.)*\")+)";
(.tool_input.command | sub("^\\s+"; "") | sub("\\s+$"; "")) as $c
| if ($c | gsub(re; "") | test("^\\s*$") | not) then "X\t\tunbalanced\tunbalanced"
  else $c | match(re; "g")
    | if .captures[0].string != null then "O\t\t" + .captures[0].string + "\t" + .captures[0].string
      elif .captures[1].string != null then "R\t\t" + .captures[1].string + "\t" + .captures[1].string
      else .captures[2].string as $r
        | ($r | unq | gsub("[\\t\\n\\r]"; " ")) as $v
        | "W\t" + ($r | flags) + "\t" + ($v | ascii_downcase) + "\t" + $v end
  end
JQ

toks=$(printf '%s' "$input" | jq -r "$TOKENIZE") || block "could not tokenize the command."
TK=()
TF=()
TL=()
TV=()
nt=0
has_ops=0
while IFS= read -r line; do
  [ -n "$line" ] || continue
  TK[$nt]=${line%%$'\t'*}
  rest=${line#*$'\t'}
  TF[$nt]=${rest%%$'\t'*}
  rest=${rest#*$'\t'}
  TL[$nt]=${rest%%$'\t'*}
  TV[$nt]=${rest#*$'\t'}
  [ "${TK[$nt]}" = O ] && has_ops=1
  nt=$((nt + 1))
done <<<"$toks"
unbalanced=0
[ "${TK[0]:-}" = X ] && unbalanced=1

# ------------------------------------------------------ self-protection

# Commands that touch the guard, the Claude settings, or the .claude
# directory as a whole must be read-only (Art. XVI: weakening the guard
# hook needs a human).
ro_fail() { block "only read-only commands may touch the guard hook, the Claude settings, or the .claude directory (Art. XVI): $1. Read them with the Read tool, cat, grep, sed -n, git diff/log/show, or run .claude/hooks/test_guard.sh."; }

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
  [ $unbalanced = 1 ] && ro_fail "unbalanced quotes"
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

re_p1='\.claude/(hooks|settings)'
re_p2='guard\.sh|settings(\.local)?\.json'
re_p3='\.claude/[^[:space:]]*[]*?[{]'
re_p4='\.claude($|[^a-z0-9_-])'
re_p5='(^|[^a-z0-9_.-])(hooks|settings)($|[^a-z0-9_.-])'
re_p6='\.claude/*(\.\.?/*)?($|[[:space:];&|)<>])' # the directory itself
if [[ $nslash =~ $re_p1 ]] || [[ $nslash =~ $re_p2 ]] || [[ $nslash =~ $re_p3 ]] ||
  [[ $nslash =~ $re_p6 ]] || { [[ $nslash =~ $re_p4 ]] && [[ $nslash =~ $re_p5 ]]; }; then
  ro_check
fi

# A glob in a hidden path component (.cl*, .[c]laude) can reach .claude.
HIDDEN_GLOB="a glob in a hidden path component can reach .claude or the gh configuration; name the path literally."
re_hidden='(^|/)(\.[^/]*[]*?[]|\[)'
i=0
while [ $i -lt $nt ]; do
  if [ "${TK[$i]}" = W ]; then
    case ${TF[$i]} in
      *g*) case ${TV[$i]} in '[' | '[[' | ']' | ']]') ;; *) [[ ${TV[$i]} =~ $re_hidden ]] && block "$HIDDEN_GLOB" ;; esac ;;
    esac
  fi
  i=$((i + 1))
done

# ------------------------------------- sensitive: fail-closed lexing

if [ $is_sens = 1 ]; then
  case $cmd in
    *"$bsnl"*) block "a backslash-newline line continuation joins words the guard reads apart. Write the command on one line. $PLAIN" ;;
    *"\$'"* | *"\$\""*) block "\$'...' and \$\"...\" quoting can spell any word. $PLAIN" ;;
  esac
  re_extglob='[]*?+@!][(]'
  [[ $cmd =~ $re_extglob ]] && block "extended globs and glob qualifiers hide which paths a command touches. $PLAIN"
  [ $unbalanced = 1 ] && block "the quotes in this command do not balance, so the guard cannot read it. $PLAIN"
fi
if [ $unbalanced = 1 ]; then
  re_hidden_text='(^|[/[:space:]=])\.[^/[:space:]]*[]*?[]'
  [[ $nslash =~ $re_hidden_text ]] && block "$HIDDEN_GLOB"
  exit 0 # nothing the guard parses is in it
fi

# ---------------------------------------------------- words anywhere

i=0
while [ $i -lt ${#L[@]} ]; do
  case ${L[$i]##*/} in
    printenv)
      for v in "${L[@]}"; do
        case $v in *token* | *secret* | *key* | *password*) block "$SECRET" ;; esac
      done ;;
    security)
      for v in "${L[@]}"; do
        case $v in find-*-password) block "reading a keychain password; $SECRET" ;; esac
      done ;;
  esac
  i=$((i + 1))
done

# ---------------------------------------------------------------- gh api

API_ALLOW="Agents may only comment, label, or react through gh api (Art. XVI)."
o='[^/]+'
re_comments="^repos/$o/$o/issues/[0-9]+/comments$"
re_comment="^repos/$o/$o/issues/comments/[0-9]+$"
re_labels="^repos/$o/$o/issues/[0-9]+/labels$"
re_label="^repos/$o/$o/issues/[0-9]+/labels/([^/]+)$"
re_reacted="^repos/$o/$o/(issues/[0-9]+|issues/comments/[0-9]+|pulls/comments/[0-9]+|comments/[0-9]+)/reactions"

# api_check <gh word index> <api word index>: the words of this segment
# after `api` are in SW[<api index>+1 ..]. The method is read from
# wherever -X/--method appears.
api_check() {
  local gi=$1 ai=$2 method= fields=0 fromfile=0 endpoint= npos=0 a v q expands=0
  set --
  q=$((ai + 1))
  while [ $q -lt $nsw ]; do
    set -- "$@" "${TV[${SW[$q]}]}"
    case ${TF[${SW[$q]}]} in *e*) expands=1 ;; esac
    q=$((q + 1))
  done
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
    [ $expands = 1 ] && block "a GraphQL query built from \$-expansion or a substitution hides what it does; pass it inline in single quotes."
    return 0
  fi
  case $method in GET | HEAD) return 0 ;; esac
  [ $expands = 1 ] && block "a mutating gh api call must use literal arguments, not \$-expansion or substitutions. $PLAIN"
  [ $gi -eq 0 ] && [ "${TV[0]}" = gh ] && [ $ai -eq 1 ] && [ $has_ops = 0 ] ||
    block "a mutating gh api call must be a plain 'gh api ...' command run alone (no env prefix, path, global flag, wrapper, chain, or pipe). $API_ALLOW"
  case $method in
    POST)
      [[ $endpoint =~ $re_comments ]] && return 0
      [[ $endpoint =~ $re_labels ]] && return 0
      [[ $endpoint =~ ${re_reacted}$ ]] && return 0 ;;
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
  block "gh api $method $endpoint is not on the guard's allowlist. $API_ALLOW Merges go through 'gh pr merge'; statuses, check runs, dispatches, rulesets, repository settings, and protection are the owner's."
}

# ----------------------------------------------------------------- merge

# Paths whose changes need the owner (Art. XVI): governance, license,
# ownership, and the guard itself.
reserved='^(\.specify/memory/constitution\.md|LICENSE|\.github/CODEOWNERS|\.claude/hooks/guard\.sh|\.claude/settings\.json)$'

CANON_MERGE="a merge must be exactly 'gh pr merge <N> [--squash|--merge|--rebase] [--delete-branch|-d] --match-head-commit <reviewed sha>', run alone, so the Art. XVI gate checks that PR at that head. $TEXT_HINT"

read -r -d '' VIEW_CHECK <<'JQ' || :
def st: (.conclusion // "") as $c | (if $c == "" then (.state // "PENDING") else $c end) | ascii_upcase;
def green: st as $s | ["SUCCESS", "SKIPPED", "NEUTRAL"] | index($s) != null;
def base_repo: .url | capture("^https://[^/]+/(?<r>[^/]+/[^/]+)/pull/[0-9]+$").r;
if (.number // null) != $n then "gh returned PR #\(.number // "?") instead of #\($n)."
elif .state != "OPEN" then "PR #\($n) is \(.state), not open."
elif .baseRefName != "main" then "PR #\($n) targets \(.baseRefName); agents merge only into main."
elif ((.headRefOid // "") | test("^[0-9a-f]{40}$") | not) then "PR #\($n) has no readable head commit."
elif .headRefOid != $want then "PR #\($n)'s head is \(.headRefOid), not the --match-head-commit \($want); the branch moved. Review the new head."
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
  local n=$1 want=$2 view verdict repo head changed pages names count hit issues url labels found
  view=$(gh pr view "$n" --json number,state,baseRefName,headRefOid,isCrossRepository,headRepository,url,changedFiles,labels,statusCheckRollup,closingIssuesReferences) ||
    block "could not read PR #$n to check the merge gate (Art. XVI)."
  verdict=$(printf '%s' "$view" | jq -r --argjson n "$n" --arg want "$want" "$VIEW_CHECK") ||
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
  local t a m= d= n sha=
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
  while [ $# -gt 0 ]; do
    a=$1
    shift
    case $a in
      --squash | --merge | --rebase) [ -z "$m" ] || block "$CANON_MERGE"; m=$a ;;
      --delete-branch | -d) [ -z "$d" ] || block "$CANON_MERGE"; d=$a ;;
      --match-head-commit)
        [ -z "$sha" ] && [ $# -ge 1 ] || block "$CANON_MERGE"
        sha=$1
        shift
        [[ $sha =~ ^[0-9a-f]{40}$ ]] || block "--match-head-commit needs the full 40-character lowercase sha of the reviewed head. $CANON_MERGE" ;;
      *) block "$CANON_MERGE" ;;
    esac
  done
  [ -n "$sha" ] || block "the merge must name the reviewed head with --match-head-commit <sha>, so GitHub refuses it if the branch moves. $CANON_MERGE"
  merge_gate "$n" "$sha"
}

# ------------------------------------------------------------------ push

CANON_PUSH="a push must be exactly 'git [-C <dir>] push [-u|--set-upstream] origin <src>[:<dst>]' or a bare 'git push', run alone. $TEXT_HINT"

gitc() {
  if [ -n "${cdir:-}" ]; then git -C "$cdir" "$@"; else git "$@"; fi
}

push_check() {
  local t src= dst= srcref d ld remote rsha msha main=0 range log missing rc out v
  cdir=
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

  # The range starts at what origin has now, as ls-remote saw it, not at
  # a possibly stale origin/main.
  if [ -n "$rsha" ]; then
    range=$rsha..$srcref
  elif [ -n "$msha" ]; then
    range=$msha..$srcref
  else
    range=$srcref # the first push, which creates main
  fi
  log=$(gitc log --no-merges --format='%h%x09%(trailers:key=Assisted-by,valueonly,separator=%x2C)%(trailers:key=Co-authored-by,valueonly,separator=%x2C)%x09%s' "$range" --) ||
    block "could not list the commits this push sends ($range); fetch origin and retry."
  missing=$(printf '%s\n' "$log" | awk -F'\t' 'NF && $2 == "" { printf "%s %s; ", $1, $3 }')
  [ -z "$missing" ] ||
    block "these commits to push have neither an Assisted-by: nor a Co-Authored-By: trailer: ${missing}Reword them before pushing. If a commit had no AI involvement, ask the human to push it."
}

# ------------------------------------------------- gh and git, parsed

GH_CMDS=" auth browse codespace discussion gist issue org pr project release repo skill cache run workflow agent-task alias api attestation completion config copilot extension ext extensions gpg-key label licenses preview ruleset search secret ssh-key status variable accessibility actions environment exit-codes formatting mintty reference telemetry "
GH_FLAT=" api browse status completion licenses accessibility actions environment exit-codes formatting mintty reference telemetry copilot "
GIT_CMDS=" add am annotate apply archive bisect blame branch bundle cat-file check-attr check-ignore check-mailmap check-ref-format checkout cherry cherry-pick clean clone commit commit-graph commit-tree config count-objects describe diff diff-files diff-index diff-tree difftool fetch for-each-ref format-patch fsck gc grep hash-object help init interpret-trailers log ls-files ls-remote ls-tree maintenance merge merge-base merge-file merge-tree mergetool mktree mv name-rev notes pull push range-diff read-tree rebase reflog remote repack replace rerere reset restore rev-list rev-parse revert rm shortlog show show-branch show-ref sparse-checkout stash status stripspace submodule switch symbolic-ref tag update-index update-ref var verify-commit verify-pack verify-tag version whatchanged worktree write-tree "
UNREAD="the guard cannot read what a \$-expansion or substitution in a gh or git command expands to."

# SW: the word indices of the current segment; nsw: their count.
# gh_parse <position in SW> <strict 0|1>
gh_parse() {
  local q=$1 strict=$2 j s1= s2= j1 a lv
  j=$((q + 1))
  while [ $j -lt $nsw ]; do
    case ${TV[${SW[$j]}]} in
      -R | --repo) j=$((j + 2)) ;;
      -R?* | --repo=*) j=$((j + 1)) ;;
      *) break ;;
    esac
  done
  # Expansions anywhere in the command head are unreadable.
  a=$((q + 1))
  while [ $a -le $((j + 1)) ] && [ $a -lt $nsw ]; do
    case ${TF[${SW[$a]}]} in *e*) block "$UNREAD $PLAIN" ;; esac
    a=$((a + 1))
  done
  [ $j -lt $nsw ] || return 0
  s1=${TV[${SW[$j]}]}
  case $s1 in
    --version | --help | -h) [ $j -eq $((nsw - 1)) ] || block "gh $s1 takes no subcommand." ; return 0 ;;
    -*) block "a flag before the gh subcommand ('$s1') hides the command from the guard; put flags after it (only -R/--repo may come first). $PLAIN" ;;
  esac
  if [ $strict = 1 ]; then
    case $GH_CMDS in *" $s1 "*) ;; *) block "'gh $s1' is not a gh command the guard knows; aliases and extensions hide what runs. $PLAIN" ;; esac
  fi
  j1=$j
  j=$((j + 1))
  while [ $j -lt $nsw ]; do
    case ${TV[${SW[$j]}]} in
      -R | --repo) j=$((j + 2)) ;;
      -R?* | --repo=*) j=$((j + 1)) ;;
      *) break ;;
    esac
  done
  if [ $j -lt $nsw ]; then
    s2=${TV[${SW[$j]}]}
    case ${TF[${SW[$j]}]} in *e*) block "$UNREAD $PLAIN" ;; esac
    case $GH_FLAT in
      *" $s1 "*) ;;
      *) case $s2 in
           -h | --help) ;;
           -*) block "a flag between 'gh $s1' and its subcommand ('$s2') hides the command from the guard; put flags after the subcommand. $PLAIN" ;;
         esac ;;
    esac
  fi
  case "$s1 $s2" in
    "release create" | "release delete" | "release delete-asset" | "release edit" | "release upload")
      block "publishing or changing a release is $HUMAN" ;;
    "repo delete" | "repo edit" | "repo rename" | "repo archive" | "repo unarchive" | "repo transfer" | "repo deploy-key" | "repo create")
      block "creating or deleting a repository or changing its settings, keys, visibility, or ownership is $HUMAN" ;;
    "issue delete" | "issue transfer") block "deleting or transferring issues is $HUMAN" ;;
    "label delete" | "label edit") block "deleting or renaming labels is $HUMAN" ;;
    "workflow disable" | "workflow run" | "workflow enable") block "disabling, enabling, or dispatching workflows is $HUMAN" ;;
    "alias set" | "alias import") block "gh aliases hide commands from the guard." ;;
    "extension install" | "extension upgrade" | "extension exec" | "extension create" | "ext install" | "ext upgrade" | "ext exec" | "ext create" | "extensions install" | "extensions upgrade" | "extensions exec" | "extensions create")
      block "gh extensions run code the guard cannot see; installing or running one is $HUMAN" ;;
    "cache delete" | "cache remove") block "deleting Actions caches is $HUMAN" ;;
    "config set" | "config clear-cache") block "gh config can set a pager, editor, or browser that runs commands; changing it is $HUMAN" ;;
    "ssh-key add" | "ssh-key delete" | "gpg-key add" | "gpg-key delete") block "changing account keys is $HUMAN" ;;
    "repo sync")
      a=$((j + 1))
      while [ $a -lt $nsw ]; do
        case ${TV[${SW[$a]}]} in --force* | -[a-zA-Z]*f*) block "gh repo sync --force overwrites a branch, which is $HUMAN" ;; esac
        a=$((a + 1))
      done ;;
  esac
  case $s1 in
    secret | variable | org) block "secrets, variables, and org settings are $HUMAN" ;;
    ruleset) case $s2 in list | view | check | "" | -*) ;; *) block "changing rulesets is $HUMAN" ;; esac ;;
    auth)
      [ "$s2" = status ] || block "gh auth other than 'gh auth status' handles credentials; $SECRET"
      a=$((q + 1))
      while [ $a -lt $nsw ]; do
        case ${TV[${SW[$a]}]} in --show-token* | -t* | -[a-zA-Z]*t*) block "gh auth status with a token flag; $SECRET" ;; esac
        a=$((a + 1))
      done ;;
  esac
  # Removing needs-human is the owner's decision.
  a=$((q + 1))
  while [ $a -lt $nsw ]; do
    case ${TV[${SW[$a]}]} in
      --remove-label*)
        lv=
        local b=$((q + 1))
        while [ $b -lt $nsw ]; do
          case ${TF[${SW[$b]}]} in *e*) block "$UNREAD $PLAIN" ;; esac
          lv="$lv ${TL[${SW[$b]}]}"
          b=$((b + 1))
        done
        case $lv in *needs-human*) block "removing needs-human is the owner's decision (Art. XVI)." ;; esac ;;
    esac
    a=$((a + 1))
  done
  case "$s1 $s2" in
    "pr merge") merge_check; exit 0 ;;
  esac
  [ "$s1" = api ] && api_check "$q" "$j1"
  return 0
}

# git_parse <position in SW> <strict 0|1>
git_parse() {
  local q=$1 strict=$2 j a v lv
  j=$((q + 1))
  while [ $j -lt $nsw ]; do
    a=${TV[${SW[$j]}]}
    case ${TF[${SW[$j]}]} in *e*) block "$UNREAD $PLAIN" ;; esac
    case $a in
      -C | --git-dir | --work-tree | --namespace)
        [ $((j + 1)) -lt $nsw ] || return 0
        case ${TF[${SW[$((j + 1))]}]} in *e*) block "$UNREAD $PLAIN" ;; esac
        j=$((j + 2)) ;;
      -c | --config-env)
        [ $((j + 1)) -lt $nsw ] || return 0
        case ${TF[${SW[$((j + 1))]}]} in *e*) block "$UNREAD $PLAIN" ;; esac
        v=${TL[${SW[$((j + 1))]}]}
        case $v in alias.* | include.* | includeif.*) block "git aliases and config includes hide commands from the guard; setting them is $HUMAN" ;; esac
        j=$((j + 2)) ;;
      -c?* | --config-env=*)
        v=${TL[${SW[$j]}]}
        v=${v#-c}
        v=${v#--config-env=}
        case $v in alias.* | include.* | includeif.*) block "git aliases and config includes hide commands from the guard; setting them is $HUMAN" ;; esac
        j=$((j + 1)) ;;
      --git-dir=* | --work-tree=* | --namespace=* | --exec-path=* | --list-cmds=*) j=$((j + 1)) ;;
      -p | --paginate | -P | --no-pager | --bare | --no-replace-objects | --literal-pathspecs | --glob-pathspecs | --noglob-pathspecs | --icase-pathspecs | --no-optional-locks | --no-lazy-fetch | --no-advice | --exec-path | --html-path | --man-path | --info-path) j=$((j + 1)) ;;
      --version | --help | -h | -v) return 0 ;;
      -*)
        [ $strict = 1 ] && block "the guard does not know git option '$a'; it fails closed. $PLAIN"
        return 0 ;;
      *) break ;;
    esac
  done
  [ $j -lt $nsw ] || return 0
  a=${TV[${SW[$j]}]}
  case $a in
    push) push_check; exit 0 ;;
    send-pack | http-push | receive-pack) block "'git $a' bypasses the push checks; use the canonical git push." ;;
    credential | credential-*) block "git credential prints stored credentials; $SECRET" ;;
    config)
      v=$((j + 1))
      while [ $v -lt $nsw ]; do
        case ${TF[${SW[$v]}]} in *e*) block "$UNREAD $PLAIN" ;; esac
        lv=${TL[${SW[$v]}]}
        case $lv in alias.* | include.* | includeif.* | *.alias.* | --file=*alias* | --edit | -e)
          block "git aliases and config includes hide commands from the guard; setting them is $HUMAN" ;;
        esac
        v=$((v + 1))
      done ;;
  esac
  if [ $strict = 1 ]; then
    case $GIT_CMDS in *" $a "*) ;; *) block "'git $a' is not a git command the guard knows; aliases hide what runs. $PLAIN" ;; esac
  fi
  return 0
}

INTERP_RE='^(python[0-9.]*|pypy[0-9.]*|perl[0-9.]*|ruby[0-9.]*|irb|node|nodejs|deno|bun|php[0-9.]*|lua[0-9.]*|luajit|osascript|pwsh|powershell|tclsh|wish|rscript|julia|groovy|jshell|swift|awk|gawk|nawk|mawk)$'
SHELL_RE='^(sh|bash|zsh|dash|ksh|mksh|yash|posh|fish|csh|tcsh)$'
re_ienv='(^|[^a-z0-9_])env([^a-z0-9_]|$)'

# process_segment <first token> <end token, exclusive> <op before> <op after>
process_segment() {
  local s=$1 e=$2 before=$3 after=$4 k p pw pf prog a inline=0 q b mode
  SW=()
  nsw=0
  k=$s
  while [ $k -lt $e ]; do
    case ${TK[$k]} in
      W) SW[$nsw]=$k; nsw=$((nsw + 1)) ;;
      R) [ $((k + 1)) -lt $e ] && [ "${TK[$((k + 1))]}" = W ] && k=$((k + 1)) ;;
    esac
    k=$((k + 1))
  done
  [ $nsw -gt 0 ] || return 0
  p=0
  while [ $p -lt $nsw ] && [[ ${TV[${SW[$p]}]} =~ ^[A-Za-z_][A-Za-z0-9_]*\+?= ]]; do p=$((p + 1)); done
  [ $p -lt $nsw ] || return 0
  pw=${TV[${SW[$p]}]}
  pf=${TF[${SW[$p]}]}
  case $pf in *e*) block "a command name built from \$-expansion or a substitution hides what runs. $PLAIN" ;; esac
  case $pf in *g*) case $pw in '[' | '[[') ;; *) block "a command name with a glob hides what runs. $PLAIN" ;; esac ;; esac
  prog=${TL[${SW[$p]}]}
  prog=${prog##*/}

  case $prog in
    git-*)
      case ${prog#git-} in
        push | send-pack | http-push | receive-pack) block "'$prog' bypasses the push checks; use the canonical git push." ;;
        credential*) block "$prog prints stored credentials; $SECRET" ;;
      esac ;;
    eval | source | .)
      [ $is_sens = 1 ] && block "$prog runs text the guard cannot parse. $PLAIN"
      q=$((p + 1))
      while [ $q -lt $nsw ]; do
        case ${TF[${SW[$q]}]} in *e*) block "$prog of an expansion runs text the guard cannot see. $PLAIN" ;; esac
        q=$((q + 1))
      done ;;
    ps)
      q=$((p + 1))
      while [ $q -lt $nsw ]; do
        a=${TV[${SW[$q]}]}
        case $a in -*E*) block "ps -E prints process environments; $SECRET" ;; esac
        if [ $q -eq $((p + 1)) ]; then
          case $a in -*) ;; *e*) block "BSD-style ps e prints process environments; $SECRET" ;; esac
        fi
        q=$((q + 1))
      done
      case $lnorm in *command_mode*) block "legacy ps mode prints process environments with -e; $SECRET" ;; esac ;;
  esac
  if [[ $prog =~ $SHELL_RE ]]; then
    q=$((p + 1))
    b=0
    while [ $q -lt $nsw ]; do
      a=${TV[${SW[$q]}]}
      case $a in
        -*c*) [[ $a =~ ^-[a-zA-Z]*c[a-zA-Z]*$ ]] && inline=1 ;;
        -* | +*) ;;
        *) b=1; break ;;
      esac
      q=$((q + 1))
    done
    if [ $inline = 1 ]; then
      [ $is_sens = 1 ] && block "$prog -c runs a string the guard cannot parse. Run the command directly. $PLAIN"
      q=$((p + 1))
      while [ $q -lt $nsw ]; do
        case ${TF[${SW[$q]}]} in *e*) block "$prog -c of an expansion runs text the guard cannot see. $PLAIN" ;; esac
        q=$((q + 1))
      done
    elif [ $b = 0 ] && [ "$before" = '|' ] && [ $is_sens = 1 ]; then
      block "piping text into $prog runs commands the guard cannot parse. $PLAIN"
    fi
  elif [[ $prog =~ $INTERP_RE ]]; then
    [[ $lnorm =~ $re_ienv ]] && block "an interpreter reading ENV prints tokens; $SECRET"
    if [ $is_sens = 1 ]; then
      q=$((p + 1))
      while [ $q -lt $nsw ]; do
        a=${TV[${SW[$q]}]}
        case $prog in
          awk | gawk | nawk | mawk)
            case ${TL[${SW[$q]}]} in *system* | *getline* | *'|'*) block "awk running commands next to gh, git, or protected paths. $PLAIN" ;; esac ;;
          *)
            case $a in
              --eval* | --print* | --exec* | eval) inline=1 ;;
              -*) [[ $a =~ ^-[a-zA-Z]*[ceEprx][a-zA-Z]*$ ]] && inline=1 ;;
            esac ;;
        esac
        q=$((q + 1))
      done
      [ $inline = 1 ] && block "$prog with inline code runs text the guard cannot parse, next to gh, git, or protected paths. $PLAIN"
    fi
  fi

  # Every word that names gh or git starts an invocation the guard parses.
  q=$p
  while [ $q -lt $nsw ]; do
    b=${TL[${SW[$q]}]}
    b=${b##*/}
    case $b in
      gh | git)
        if [ $q -eq $p ]; then
          mode=1
        else
          case $prog in
            grep | egrep | fgrep | rg | ag | ack | echo | printf | cat | head | tail | less | more | wc | sed | tr | sort | uniq | cut | jq | which | type | whereis | man | file | stat | ls | diff | cmp | gh | git)
              mode=0 ;;
            xargs | parallel | find | fd | watch | sudo | doas | su | ssh | script | unbuffer | flock | chronic | setsid | entr)
              block "run $b directly, not through $prog: the guard cannot see the arguments $prog adds. $PLAIN" ;;
            *) mode=1 ;;
          esac
        fi
        case $after in '$(' | '`' | '<(' | '>(') block "$UNREAD $PLAIN" ;; esac
        if [ $b = gh ]; then gh_parse "$q" "$mode"; else git_parse "$q" "$mode"; fi ;;
    esac
    q=$((q + 1))
  done
  return 0
}

i=0
start=0
prev_op=
while [ $i -le $nt ]; do
  if [ $i -eq $nt ] || [ "${TK[$i]}" = O ]; then
    op=
    [ $i -lt $nt ] && op=${TV[$i]}
    process_segment "$start" "$i" "$prev_op" "$op"
    prev_op=$op
    start=$((i + 1))
  fi
  i=$((i + 1))
done

exit 0
