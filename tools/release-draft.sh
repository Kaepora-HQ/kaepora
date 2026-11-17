#!/usr/bin/env bash
# Prints the release to finish, as commit=<the tag's commit> and version=<version>: the release of the version in the
# branch's .release-please-manifest.json, while it is a draft. Prints nothing once it is published, or before it exists.
#
# Usage: tools/release-draft.sh <branch>
# Needs GITHUB_REPOSITORY, and a GH_TOKEN that can write to it, as only writers see drafts.
set -euo pipefail

branch=$1

version=$(gh api -H 'Accept: application/vnd.github.raw' \
    "repos/$GITHUB_REPOSITORY/contents/.release-please-manifest.json?ref=$branch" | jq -r '."."')
release=$(gh api graphql -F owner="${GITHUB_REPOSITORY%/*}" -F name="${GITHUB_REPOSITORY#*/}" -F tag="v$version" \
    -f query='query($owner: String!, $name: String!, $tag: String!) {
        repository(owner: $owner, name: $name) { release(tagName: $tag) { isDraft tagCommit { oid } } }
    }' --jq '.data.repository.release')
echo "The release of $version in $branch's manifest: ${release:-none}" >&2

if [ "$(jq -r .isDraft <<< "$release")" = true ]; then
    echo "commit=$(jq -r .tagCommit.oid <<< "$release")"
    echo "version=$version"
fi
