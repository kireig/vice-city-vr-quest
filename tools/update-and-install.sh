#!/usr/bin/env bash
# Update from official master without discarding local changes or app data.
set -euo pipefail

OFFICIAL_URL="https://github.com/dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git"
KIT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${HOME}/VCVRBuild"
LOG_PATH="${TMPDIR:-/tmp}/ViceCityVR-Update.log"
DRY_RUN=0
FORWARD=()

die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
need_value() { [ "$#" -gt 1 ] && [ -n "$2" ] || die "Missing value for $1"; }
while [ "$#" -gt 0 ]; do
  case "$1" in
    --work-dir|-WorkDir) need_value "$@"; WORK_DIR="$2"; shift 2 ;;
    --log-path|-LogPath) need_value "$@"; LOG_PATH="$2"; shift 2 ;;
    --android-sdk|-AndroidSdk|--java-home|-JavaHome|--serial|-Serial)
      need_value "$@"; FORWARD+=("$1" "$2"); shift 2 ;;
    --build-only|-BuildOnly|--non-interactive|-NonInteractive|--release|-Release)
      FORWARD+=("$1"); shift ;;
    --dry-run|-DryRun) DRY_RUN=1; shift ;;
    --help|-h)
      printf '%s\n' 'UPDATE: fetch official master, build, and update the installed APK.' \
        'Options: --work-dir DIR, --android-sdk DIR, --java-home DIR, --serial ID,' \
        '         --log-path FILE, --build-only, --release, --non-interactive, --dry-run.' \
        'Dry run checks local state; it does not fetch, clone, build or install.'
      exit 0 ;;
    *) die "Unknown update option: $1 (see --help)." ;;
  esac
done

command -v git >/dev/null 2>&1 || die 'Git is required. Install Git and rerun UPDATE.'
[ -d "$(dirname "$LOG_PATH")" ] || mkdir -p "$(dirname "$LOG_PATH")"
exec > >(tee "$LOG_PATH") 2>&1

version_text() {
  local value
  value="$(sed -nE 's/^[[:space:]]*versionCode[[:space:]]*=[[:space:]]*([0-9]+)[[:space:]]*$/\1/p')"
  [[ "$value" =~ ^[0-9]+$ ]] || die 'Cannot read a unique numeric versionCode from this source kit.'
  printf '%s\n' "$value"
}
version_at() {
  git -C "$1" show "$2:overlay/android/app/build.gradle.kts" | version_text
}
require_clean() {
  local dirty
  dirty="$(git -C "$1" status --porcelain --untracked-files=no)" || die 'Cannot inspect local Git changes.'
  [ -z "$dirty" ] || die "Tracked local changes in $1. Commit or preserve them before UPDATE; nothing was reset or stashed."
}
require_repo_root() {
  local actual expected
  actual="$(git -C "$1" rev-parse --show-toplevel)" || die "Not a Git source checkout: $1"
  expected="$(cd "$1" && pwd)"
  [ "$(cd "$actual" && pwd)" = "$expected" ] || die 'The source kit must be the root of its own Git checkout.'
  local origin branch
  origin="$(git -C "$1" remote get-url origin)"
  case "$origin" in
    "$OFFICIAL_URL"|"${OFFICIAL_URL%.git}"|git@github.com:dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git) ;;
    *) die 'Origin is not the official source kit. No remote was changed.' ;;
  esac
  branch="$(git -C "$1" symbolic-ref --short HEAD)" || die 'UPDATE requires the master branch.'
  [ "$branch" = master ] || die 'UPDATE requires the master branch; your current branch was left untouched.'
}

[ -f "$KIT_ROOT/overlay/android/app/build.gradle.kts" ] || die 'Run UPDATE from the complete source kit.'
MIN_VERSION="$(version_text < "$KIT_ROOT/overlay/android/app/build.gradle.kts")"
[ "$MIN_VERSION" -ge 520 ] || MIN_VERSION=520
if [ -e "$KIT_ROOT/.git" ]; then
  SOURCE="$KIT_ROOT"
  require_repo_root "$SOURCE"
else
  # ZIP copies stay untouched; the managed checkout shares the existing caches.
  SOURCE="$WORK_DIR/source-kit"
  if [ -e "$SOURCE" ]; then
    require_repo_root "$SOURCE"
  elif [ "$DRY_RUN" -eq 0 ]; then
    [ -d "$WORK_DIR" ] || mkdir -p "$WORK_DIR"
    git clone --single-branch --branch master "$OFFICIAL_URL" "$SOURCE" || die 'Official source-kit clone failed.'
  fi
fi

if [ -e "$SOURCE" ]; then
  require_repo_root "$SOURCE"
  require_clean "$SOURCE"
  HEAD_BEFORE="$(git -C "$SOURCE" rev-parse --verify HEAD)"
  CACHE_VERSION="$(version_at "$SOURCE" "$HEAD_BEFORE")"
  [ "$CACHE_VERSION" -le "$MIN_VERSION" ] || MIN_VERSION="$CACHE_VERSION"
fi
if [ "$DRY_RUN" -eq 1 ]; then
  printf 'Dry run: source=%s; version floor=%s; official master; no network/build/install.\n' "$SOURCE" "$MIN_VERSION"
  exit 0
fi

git -C "$SOURCE" fetch --no-tags "$OFFICIAL_URL" refs/heads/master || die 'Could not fetch official master.'
FETCHED="$(git -C "$SOURCE" rev-parse --verify FETCH_HEAD)"
REMOTE_VERSION="$(version_at "$SOURCE" "$FETCHED")"
[ "$REMOTE_VERSION" -ge "$MIN_VERSION" ] ||
  die "Official master is versionCode $REMOTE_VERSION, below local minimum $MIN_VERSION. Wait for the release to be published; no older APK was built."
if git -C "$SOURCE" cat-file -e "$FETCHED:tools/update-required-assets.txt" 2>/dev/null; then
  die 'This version needs updated bundled assets. Use the normal BUILD_AND_INSTALL for that version instead of UPDATE.'
fi
git -C "$SOURCE" merge-base --is-ancestor "$HEAD_BEFORE" "$FETCHED" ||
  die 'Local commits and official master cannot be fast-forwarded. Preserve your work and resolve the divergence before UPDATE.'
require_clean "$SOURCE"
[ "$(git -C "$SOURCE" rev-parse --verify HEAD)" = "$HEAD_BEFORE" ] || die 'Local HEAD changed during fetch; rerun UPDATE.'
git -C "$SOURCE" merge --ff-only --no-autostash --no-overwrite-ignore "$FETCHED" ||
  die 'Fast-forward refused, possibly because an untracked file would be overwritten. Local files were preserved.'
[ "$(git -C "$SOURCE" rev-parse --verify HEAD)" = "$FETCHED" ] || die 'The updated checkout does not match the verified commit.'
require_clean "$SOURCE"
[ -f "$SOURCE/tools/build-and-install.sh" ] || die 'Updated kit has no build helper.'
printf 'Updating APK from verified versionCode %s. Game files and saves will not be copied.\n' "$REMOTE_VERSION"
exec bash "$SOURCE/tools/build-and-install.sh" --update-only --work-dir "$WORK_DIR" \
  --log-path "$LOG_PATH" "${FORWARD[@]}"
