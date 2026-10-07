set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

command -v git >/dev/null || { echo "git is required" >&2; exit 1; }

export GIT_TERMINAL_PROMPT=0
export GCM_INTERACTIVE=never

destination="Trinity-Forge/Tests/Models"
failed=()

# Each entry is a repository of its own under the destination, holding only the listed paths, and only their files are downloaded
while read -r name url ref paths || [ -n "${name:-}" ]; do
    case "$name" in ''|\#*) continue ;; esac
    path="$destination/$name"

    echo "==> $name @ $ref"
    if [ ! -e "$path/.git" ]; then
        mkdir -p "$path"
        git init --quiet "$path"
        git -C "$path" remote add origin "$url"
    else
        git -C "$path" remote set-url origin "$url"
    fi

    patterns=()
    for it_path in $paths; do
        patterns+=("/$it_path")
    done

    if git -C "$path" sparse-checkout set --no-cone "${patterns[@]}" \
        && git -C "$path" fetch --quiet --depth 1 --filter=blob:none origin "$ref" \
        && git -C "$path" -c advice.detachedHead=false checkout --quiet --force FETCH_HEAD; then
        :
    else
        failed+=("$name  $url")
    fi
done < Scripts/Samples.lock

echo
if [ ${#failed[@]} -gt 0 ]; then
    echo "Could not fetch:"
    printf '    %s\n' "${failed[@]}"
    exit 1
fi

echo "Samples are in $destination. Next: Trinity-Forge --import-test"