#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$REPO_ROOT/build/notecmd"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/notecmd-tests.XXXXXX")"

trap 'rm -rf "$TMP_DIR"' EXIT

fail() {
  echo "FAIL: $1" >&2
  exit 1
}

assert_contains() {
  local haystack="$1"
  local needle="$2"

  [[ "$haystack" == *"$needle"* ]] || fail "expected to find '$needle'"
}

assert_not_contains() {
  local haystack="$1"
  local needle="$2"

  [[ "$haystack" != *"$needle"* ]] || fail "did not expect to find '$needle'"
}

assert_equals() {
  local left="$1"
  local right="$2"

  [[ "$left" == "$right" ]] || fail "expected '$right' but got '$left'"
}

test_legacy_history_loading() {
  local home="$TMP_DIR/legacy-home"
  local output

  mkdir -p "$home/.local/share/notecmd"
  cat <<'EOF' > "$home/.local/share/notecmd/history.json"
{"command": "ls", "note": "legacy note
", "timestamp": "2026-04-12 10:00:00"}
EOF

  output="$(env HOME="$home" "$BIN" --list)"
  assert_contains "$output" "1. ls"
  assert_contains "$output" "note: legacy note"
  assert_contains "$output" "saved: 2026-04-12 10:00:00"
}

test_save_and_list() {
  local home="$TMP_DIR/save-home"
  local run_output
  local list_output

  mkdir -p "$home"
  run_output="$(env HOME="$home" "$BIN" -n "quote test" printf '%s\n' 'a b')"
  list_output="$(env HOME="$home" "$BIN" --list)"

  assert_contains "$run_output" "a b"
  assert_contains "$list_output" "'printf' '%s\n' 'a b'"
  assert_contains "$list_output" "note: quote test"
}

test_shell_init_output() {
  local zsh_init
  local bash_init

  zsh_init="$("$BIN" init zsh)"
  bash_init="$("$BIN" init bash)"

  assert_contains "$zsh_init" "print -z -- \"\$cmd\""
  assert_contains "$zsh_init" "bindkey '^X^N' notecmd-widget"
  assert_contains "$bash_init" "READLINE_LINE=\"\$cmd\""
  assert_contains "$bash_init" "bind -x '\"\\C-x\\C-n\":__notecmd_widget'"
}

test_picker_filter_and_tab_output() {
  local home="$TMP_DIR/picker-home"
  local selection_file="$TMP_DIR/selection.txt"
  local picker_output
  local selected_command

  mkdir -p "$home/.local/share/notecmd"
  cat <<'EOF' > "$home/.local/share/notecmd/history.json"
{"command": "ls", "note": "simple list", "timestamp": "2026-04-12 20:00:00"}
{"command": "git status --short", "note": "repo check", "timestamp": "2026-04-12 20:01:00"}
{"command": "printf '%s\\n' 'hello world'", "note": "quoted args", "timestamp": "2026-04-12 20:02:00"}
EOF

  picker_output="$(printf 'git\t' | env HOME="$home" NOTECMD_TEST_MODE=1 NOTECMD_SELECTION_FILE="$selection_file" "$BIN")"
  selected_command="$(cat "$selection_file")"

  assert_contains "$picker_output" "Search : git"
  assert_equals "$selected_command" "git status --short"
}

test_plain_letters_still_filter() {
  local home="$TMP_DIR/picker-home"
  local picker_output

  picker_output="$(printf 'q\030\030' | env HOME="$home" NOTECMD_TEST_MODE=1 "$BIN")"

  assert_contains "$picker_output" "Search : q"
  assert_contains "$picker_output" "Quit armed. Press Ctrl-X again to close the picker."
}

test_picker_delete_flow() {
  local home="$TMP_DIR/picker-home"
  local delete_output
  local list_output

  delete_output="$(printf '\004\004\030\030' | env HOME="$home" NOTECMD_TEST_MODE=1 "$BIN")"
  list_output="$(env HOME="$home" "$BIN" --list)"

  assert_contains "$delete_output" "Delete armed. Press Ctrl-D again to delete this command."
  assert_not_contains "$list_output" "1. ls"
  assert_contains "$list_output" "1. git status --short"
  assert_contains "$delete_output" "Quit armed. Press Ctrl-X again to close the picker."
}

test_legacy_history_loading
test_save_and_list
test_shell_init_output
test_picker_filter_and_tab_output
test_plain_letters_still_filter
test_picker_delete_flow

echo "All tests passed."
