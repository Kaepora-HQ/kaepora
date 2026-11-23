#!/usr/bin/env bash
# Finds the Neon database the site's workflow deploys with, or retires a preview's.
#
#   tools/site-database.sh production        production's database
#   tools/site-database.sh preview <branch>  the database branch preview/<branch>, made from production's on first use,
#                                            set to expire 7 days after this call
#   tools/site-database.sh expire <branch>   moves preview/<branch> aside and has Neon delete it a day later
#
# For production and a preview, the job's later steps get the database's pooled address as DATABASE_URL and its direct
# address as DATABASE_URL_UNPOOLED, through $GITHUB_ENV. Both are masked in the log first.
# Reads NEON_API_KEY and NEON_PROJECT_ID, and the runner's RUNNER_TEMP and GITHUB_ENV.
set -euo pipefail

project=https://console.neon.tech/api/v2/projects/$NEON_PROJECT_ID

# neon <method> <path> <program> [JSON body] sends a call on the site's project and prints what <program>, a jq program,
# reads from Neon's answer. In the program, need(f; "what") is f, or stops the script when the answer lacks it.
# A 423 means an operation already under way holds the project, and Neon's API reference says the call is safe to send
# again. Its guide to operations says to wait longer before each new try and to give up after a set number: here five
# tries, 2, 4, 8 and 16 s apart.
neon() {
    local body=() answer=$RUNNER_TEMP/neon-answer.json status pause=2
    [ $# -lt 4 ] || body=(--json "$4")
    while :; do
        status=$(curl --silent --show-error --request "$1" --header "Authorization: Bearer $NEON_API_KEY" "${body[@]}" \
            --output "$answer" --write-out '%{http_code}' "$project$2")
        if [ "$status" != 423 ] || [ "$pause" -gt 16 ]; then break; fi
        echo "Another operation holds the Neon project. Sending $1 $2 again in $pause s." >&2
        sleep "$pause"
        pause=$((pause * 2))
    done
    if [[ $status != 2* ]]; then
        echo "::error::$1 $2 got $status from Neon: $(jq -r .message "$answer")" >&2
        exit 1
    fi
    jq -r --arg call "$1 $2" \
        'def need(f; what): f // ("::error::Neon answered \($call) without \(what).\n" | halt_error(1)); '"$3" "$answer"
}

# The program for an answer that changed a branch: the ids of the operations it started, on one line.
started='need(.operations; "the operations it started") | map(need(.id; "an operation id")) | join(" ")'

# settle <operation id>... returns once each operation has ended, and stops the script if one did not succeed.
# Neon's guide to operations suggests looking at an operation every 5 s. Waiting for the script's own operations to end
# keeps them from refusing its next call.
settle() {
    local id answer status action reason
    for id in "$@"; do
        while :; do
            answer=$(neon GET "/operations/$id" \
                '.operation | [need(.status; "a status"), need(.action; "an action"), .error // ""] | join(" ")')
            read -r status action reason <<<"$answer"
            case $status in
            finished | skipped) break ;;
            failed | error | cancelled)
                echo "::error::Neon could not $action: ${reason:-$status}" >&2
                exit 1
                ;;
            esac
            sleep 5
        done
    done
}

# expiry <days> prints the time <days> days from now, in the form Neon takes for a branch's expiry.
expiry() {
    jq -nr --argjson days "$1" 'now + $days * 24 * 60 * 60 | floor | todate'
}

# branch_named <name> prints the id of the branch called exactly <name>, or nothing. Neon's search also matches part of
# a name: preview/fix finds preview/fix-maps.
branch_named() {
    wanted=$1 neon GET "/branches?search=$(jq -rn --arg name "$1" '$name | @uri')" \
        'need(.branches; "a list of branches")[] | select(.name == env.wanted) | need(.id; "the id of \(.name)")'
}

use_branch() {
    local answer database role pooled direct
    answer=$(neon GET "/branches/$1/databases" 'need(.databases; "a list of databases") |
        need(if length == 1 then .[0] else null end; "exactly one database") |
        [need(.name; "its name"), need(.owner_name; "its owner")] | map(@uri) | join(" ")')
    read -r database role <<<"$answer"
    pooled=$(neon GET "/connection_uri?branch_id=$1&database_name=$database&role_name=$role&pooled=true" \
        'need(.uri; "an address")')
    direct=$(neon GET "/connection_uri?branch_id=$1&database_name=$database&role_name=$role&pooled=false" \
        'need(.uri; "an address")')
    echo "::add-mask::$pooled"
    echo "::add-mask::$direct"
    printf 'DATABASE_URL=%s\nDATABASE_URL_UNPOOLED=%s\n' "$pooled" "$direct" >>"$GITHUB_ENV"
}

case $1 in
production)
    # Neon splits this list into pages without saying how long one is. 10000, the longest page its reference allows,
    # holds every branch.
    id=$(neon GET '/branches?limit=10000' '[need(.branches; "a list of branches")[] | select(.default)] |
        need(if length == 1 then .[0].id else null end; "the id of one default branch")')
    use_branch "$id"
    ;;
preview)
    # An expiry from the first deploy, renewed by each later one, retires a preview that no closing or deletion reaches.
    expires_at=$(expiry 7)
    id=$(branch_named "preview/$2")
    if [ -z "$id" ]; then
        # With no parent given, Neon branches from the project's default branch, which holds production's data.
        request=$(jq -n --arg name "preview/$2" --arg expires_at "$expires_at" \
            '{branch: {$name, $expires_at}, endpoints: [{type: "read_write"}]}')
        answer=$(neon POST /branches '[need(.branch.id; "the id of the new branch"),
            need(.branch.expires_at; "its expiry"), ('"$started"')] | join(" ")' "$request")
        read -r id expires_at ids <<<"$answer"
        echo "Made preview/$2 ($id) from production's database."
    else
        request=$(jq -n --arg expires_at "$expires_at" '{branch: {$expires_at}}')
        answer=$(neon PATCH "/branches/$id" '[need(.branch.expires_at; "its expiry"), ('"$started"')] | join(" ")' \
            "$request")
        read -r expires_at ids <<<"$answer"
        echo "preview/$2 is $id."
    fi
    read -r -a operations <<<"$ids"
    settle "${operations[@]}"
    echo "Neon deletes it after $expires_at, unless a later deploy sets a later time."
    use_branch "$id"
    ;;
expire)
    id=$(branch_named "preview/$2")
    if [ -z "$id" ]; then
        echo "Neon holds no preview/$2. The pull request deployed no preview, or its database has been retired already."
        exit 0
    fi
    # A pull request opened again from the same git branch must make a new preview/<branch>, not find this one. The id
    # in the new name keeps it apart from a database retired earlier from the same git branch.
    request=$(jq -n --arg name "closed/$2/$id" --arg expires_at "$(expiry 1)" '{branch: {$name, $expires_at}}')
    answer=$(neon PATCH "/branches/$id" \
        '[need(.branch.name; "its name"), need(.branch.expires_at; "its expiry"), ('"$started"')] | join(" ")' \
        "$request")
    read -r name expires_at ids <<<"$answer"
    read -r -a operations <<<"$ids"
    settle "${operations[@]}"
    echo "preview/$2 is now $name. Neon deletes it after $expires_at."
    ;;
*)
    echo "Usage: $0 production | preview <branch> | expire <branch>" >&2
    exit 2
    ;;
esac
