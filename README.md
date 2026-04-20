# notecmd

`notecmd` is a small C CLI that helps you build a personal command history with notes.
It can:

- run a command and save it with an optional note
- list saved commands from local history
- reopen saved commands in an interactive picker
- filter commands live inside the picker
- send a selected command back to your shell instead of running it immediately

The history is stored locally as JSON lines under `~/.local/share/notecmd/history.json`.

## Features

- command execution plus automatic history save
- optional per-command notes
- interactive TUI picker
- live filtering by command, note, or timestamp
- shell integration for `zsh` and `bash`
- automated end-to-end tests

## Build

```sh
make
```

The binary is generated at:

```sh
./build/notecmd
```

## Homebrew

You can install `notecmd` with Homebrew directly from this repository's formula:

```sh
brew install https://raw.githubusercontent.com/ilaario/notecmd/main/Formula/notecmd.rb
```

This formula currently builds from the published `v1.1` source tarball on GitHub.

## Basic Usage

Run a command and save it:

```sh
./build/notecmd ls -la
```

Run a command and attach a note immediately:

```sh
./build/notecmd -n "inspect repo state" git status --short
```

List saved commands:

```sh
./build/notecmd --list
```

Open the interactive picker:

```sh
./build/notecmd
```

Show help:

```sh
./build/notecmd --help
```

## Picker Controls

Inside the picker:

- `Up` / `Down` move the selection
- typing filters by command text, note, or timestamp
- `Backspace` removes one character from the filter
- `Ctrl-U` clears the filter
- `Enter` runs the selected command
- `Tab` sends the selected command back to the shell integration
- `Ctrl-D`, then `Ctrl-D` again deletes the selected command
- `Ctrl-X`, then `Ctrl-X` again quits

The bottom panel always shows details for the currently selected entry.

## Shell Integration

`notecmd` can print integration code for `zsh` and `bash`.

For `zsh`:

```sh
eval "$(/absolute/path/to/notecmd init zsh)"
```

For `bash`:

```sh
eval "$(/absolute/path/to/notecmd init bash)"
```

What this enables:

- calling `notecmd` with arguments still runs the binary normally
- calling `notecmd` with no arguments opens the picker
- pressing `Tab` inside the picker hands the selected command back to the shell
- a widget is bound on `Ctrl-X Ctrl-N`

Shell-specific behavior:

- `zsh`: the selected command is inserted directly into the editing buffer
- `bash`: the widget fills `READLINE_LINE`; the plain `notecmd` shell function also stores the selected command in history so it can be recalled with `Up`

## History File

Saved commands are written to:

```sh
~/.local/share/notecmd/history.json
```

Each entry stores:

- `command`
- `note`
- `timestamp`

The loader is backward-compatible with older malformed entries where notes were written with embedded newlines.

## Tests

Run the automated test suite with:

```sh
make test
```

The current test suite covers:

- loading legacy history entries
- saving and listing commands
- shell init output for `zsh` and `bash`
- picker filtering plus `Tab` handoff
- picker delete and quit confirmation flows

## Notes For Development

- source: [src/main.c](src/main.c)
- shared includes/constants: [include/notecmd.h](include/notecmd.h)
- tests: [tests/test.sh](tests/test.sh)

If you want to install the binary globally, you can use:

```sh
make install
```

You can also override the install prefix:

```sh
make install PREFIX=/opt/homebrew
```
