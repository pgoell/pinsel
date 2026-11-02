#!/usr/bin/env bash
# Which round of review this push is, and which commits it covers. Each kind of
# review (review, security) keeps one comment on the pull request with a marker
# that records the last commit it covered and the round. Round 1 reads the whole
# pull request, later rounds only the commits since, and after three rounds the
# review stops and asks the author to rethink, so an agent author and an agent
# reviewer cannot cycle all night.
#
#   round.sh plan    writes round, from, comment_id, stop, rerun and max to $GITHUB_OUTPUT
#   round.sh record  writes the marker comment for this round
#
# Reads KIND, PR, BASE_SHA, HEAD_SHA and GH_TOKEN from the environment, and
# ROUND, FROM and COMMENT_ID for record.
# shellcheck disable=SC2153 # ROUND and FROM come from the environment
set -euo pipefail

MAX_ROUNDS=3
marker="<!-- ci-review:$KIND"

case $1 in
  plan)
    # The comment this workflow wrote, not one a person pasted the marker into.
    found=$(gh api --paginate "repos/$GITHUB_REPOSITORY/issues/$PR/comments" \
      --jq ".[] | select(.user.login == \"github-actions[bot]\") | select(.body | startswith(\"$marker\")) | [.id, .body] | @tsv" | tail -1)
    round=1
    from=$BASE_SHA
    comment_id=
    rerun=false
    if [ -n "$found" ]; then
      comment_id=${found%%$'\t'*}
      last_sha=$(sed -nE 's/.*sha=([0-9a-f]{40}).*/\1/p' <<<"$found")
      last_round=$(sed -nE 's/.*round=([0-9]+).*/\1/p' <<<"$found")
      round=$((last_round + 1))
      # A rerun on a commit a finished round already covered has nothing new
      # to read, and must not use up a round.
      if [ "$last_sha" = "$HEAD_SHA" ]; then rerun=true; fi
      # A force push can drop the commit the last round covered: then this
      # round reads the whole pull request again, and still counts.
      if git merge-base --is-ancestor "$last_sha" "$HEAD_SHA" 2>/dev/null; then
        from=$last_sha
      fi
    fi
    stop=false
    if [ "$round" -gt "$MAX_ROUNDS" ] || [ "$rerun" = true ]; then stop=true; fi
    {
      echo "round=$round"
      echo "from=$from"
      echo "comment_id=$comment_id"
      echo "stop=$stop"
      echo "rerun=$rerun"
      echo "max=$MAX_ROUNDS"
    } >>"$GITHUB_OUTPUT"
    ;;
  record)
    if [ "$ROUND" -gt "$MAX_ROUNDS" ]; then
      text="**${KIND^}**: $MAX_ROUNDS rounds are done and this push brought round $ROUND. No more rounds run on this pull request. Stop fixing line by line: rethink the approach, or ask a code owner."
    else
      text="**${KIND^}** round $ROUND of $MAX_ROUNDS covered \`${FROM:0:7}..${HEAD_SHA:0:7}\`."
    fi
    body="$marker sha=$HEAD_SHA round=$ROUND -->
$text"
    if [ -n "$COMMENT_ID" ]; then
      gh api -X PATCH "repos/$GITHUB_REPOSITORY/issues/comments/$COMMENT_ID" -f body="$body" >/dev/null
    else
      gh api "repos/$GITHUB_REPOSITORY/issues/$PR/comments" -f body="$body" >/dev/null
    fi
    ;;
esac
