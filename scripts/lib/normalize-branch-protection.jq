# Normalize a GitHub branch-protection JSON document so a PUT payload and the
# response from GET /repos/:owner/:repo/branches/:branch/protection can be
# diffed for drift detection.
#
# Invoked as:  jq -S -f scripts/lib/normalize-branch-protection.jq <file>

# Drop URL fields GitHub adds on GET but that are absent on PUT.
del(.url)
| walk(if type == "object" and has("url") then del(.url) else . end)

# Unwrap {"enabled": bool} wrappers GitHub uses on GET for boolean toggles
# where PUT accepts a bare bool.
| (if .required_linear_history? | type == "object"
     then .required_linear_history = .required_linear_history.enabled
     else . end)
| (if .allow_force_pushes? | type == "object"
     then .allow_force_pushes = .allow_force_pushes.enabled
     else . end)
| (if .allow_deletions? | type == "object"
     then .allow_deletions = .allow_deletions.enabled
     else . end)
| (if .block_creations? | type == "object"
     then .block_creations = .block_creations.enabled
     else . end)
| (if .required_conversation_resolution? | type == "object"
     then .required_conversation_resolution = .required_conversation_resolution.enabled
     else . end)
| (if .lock_branch? | type == "object"
     then .lock_branch = .lock_branch.enabled
     else . end)
| (if .allow_fork_syncing? | type == "object"
     then .allow_fork_syncing = .allow_fork_syncing.enabled
     else . end)

# Sort context array so readback ordering does not flag drift.
| (if .required_status_checks.contexts?
     then .required_status_checks.contexts |= sort
     else . end)

# Unwrap enforce_admins: GET returns {"enabled": bool}, PUT takes a bare bool.
| (if .enforce_admins? | type == "object"
     then .enforce_admins = .enforce_admins.enabled
     else . end)

# required_signatures is GET-only ({"enabled": bool, "url": "..."}); absent from PUT.
| del(.required_signatures)

# required_status_checks.checks is GET-only (list of {app_id, context} objects).
| del(.required_status_checks?.checks)

# required_status_checks.contexts_url is a GET-only URL field not caught by the walk above.
| del(.required_status_checks?.contexts_url)

# restrictions: null in PUT payload; key absent in GET when no restrictions set.
# Normalise both to: key absent.
| if (.restrictions // null) == null then del(.restrictions) else . end
