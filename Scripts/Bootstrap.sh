set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

command -v git >/dev/null || { echo "git is required" >&2; exit 1; }
[ -d .git ] || git init -q

export GIT_TERMINAL_PROMPT=0
export GCM_INTERACTIVE=never

failed=()
unreachable=()

while read -r name url ref _; do
    case "$name" in ''|\#*) continue ;; esac
    path="Vendor/$name"

    if [ ! -e "$path/.git" ]; then
        echo "==> adding $name"
        if ! git submodule add --quiet --force --depth 1 "$url" "$path"; then
            failed+=("$name  $url")
            continue
        fi
        git config -f .gitmodules "submodule.$path.shallow" true
    elif [ "$(git config -f .gitmodules --get "submodule.$path.url")" != "$url" ]; then
        echo "==> $name: remote is now $url"
        git config -f .gitmodules "submodule.$path.url" "$url"
        git submodule --quiet sync -- "$path"
    fi

    echo "==> $name @ $ref"
    if git -C "$path" fetch --quiet --depth 1 origin "$ref"; then
        git -C "$path" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
    elif [ "$(git -C "$path" rev-parse HEAD)" = "$ref" ]; then
        unreachable+=("$name  $url")
    else
        failed+=("$name  $url")
        continue
    fi
    git add "$path" .gitmodules
done < Scripts/Vendor.lock

echo
if [ ${#unreachable[@]} -gt 0 ]; then
    echo "Already at the pinned commit, but the remote could not be reached or does not have it:"
    printf '    %s\n' "${unreachable[@]}"
    echo "Create or update these forks; until then a fresh clone of Trinity cannot fetch them."
    echo
fi
if [ ${#failed[@]} -gt 0 ]; then
    echo "Could not fetch:"
    printf '    %s\n' "${failed[@]}"
    echo "Check that each fork exists and contains the pinned commit (see README, Dependencies)."
    exit 1
fi

echo "Dependencies are in place. Next: cmake --preset <name>   (cmake --list-presets)"