/**
 * @file main.c
 * @brief Main entry point and interactive workflow for the notecmd CLI.
 */

#include "notecmd.h"

struct termios orig_termios;
static int raw_mode_enabled = 0;

/**
 * @brief Restore the terminal before leaving on an external termination signal.
 *
 * @param sig POSIX signal number received by the process.
 */
static void handle_termination_signal(int sig) {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        write(STDOUT_FILENO, "\033[?25h", 6);
    }

    _exit(128 + sig);
}

/**
 * @brief In-memory representation of one saved command entry from history.
 */
typedef struct {
    char *command;
    char *note;
    char *timestamp;
} SavedCommand;

/**
 * @brief Return the absolute path to the history file, creating parent directories if needed.
 *
 * The returned string is heap-allocated and must be freed by the caller.
 *
 * @return char* Absolute path to `~/.local/share/notecmd/history.json`, or `NULL` on failure.
 */
char* get_history_file_path();

static int create_directories(const char *path) {
    char tmp[PATH_MAX];
    size_t len;

    if (path == NULL) {
        return -1;
    }

    len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return -1;
    }

    strcpy(tmp, path);

    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int is_test_mode_enabled() {
    const char *value = getenv("NOTECMD_TEST_MODE");

    return value != NULL && strcmp(value, "1") == 0;
}

void reset_terminal() {
    if (!raw_mode_enabled) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[?25h");
    fflush(stdout);
    raw_mode_enabled = 0;
}

void enable_raw_mode() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) {
        return;
    }

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return;
    }

    raw_mode_enabled = 1;
    signal(SIGINT, handle_termination_signal);
    signal(SIGTERM, handle_termination_signal);
    signal(SIGHUP, handle_termination_signal);
    atexit(reset_terminal);
    printf("\033[?25l");
    fflush(stdout);
}

int read_key() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    if (c == 27) { 
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        if (seq[0] == '[' || seq[0] == 'O') {
            if (seq[1] == 'A') return 1000;
            if (seq[1] == 'B') return 1001;
            if (seq[1] == 'C') return 1002;
            if (seq[1] == 'D') return 1003;
        }
    }
    return c;
}

static char *duplicate_substring(const char *start, size_t len) {
    char *copy = malloc(len + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static void trim_trailing_newlines(char *text) {
    size_t len;

    if (text == NULL) {
        return;
    }

    len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[--len] = '\0';
    }
}

static char *escape_json_string(const char *text) {
    size_t input_len;
    size_t max_len;
    char *escaped;
    size_t i;
    size_t j = 0;

    if (text == NULL) {
        return duplicate_substring("", 0);
    }

    input_len = strlen(text);
    max_len = (input_len * 2) + 1;
    escaped = malloc(max_len);
    if (escaped == NULL) {
        return NULL;
    }

    for (i = 0; i < input_len; i++) {
        switch (text[i]) {
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            default:
                escaped[j++] = text[i];
                break;
        }
    }

    escaped[j] = '\0';
    return escaped;
}

static char *unescape_json_substring(const char *start, size_t len) {
    char *value = malloc(len + 1);
    size_t i;
    size_t output_len = 0;

    if (value == NULL) {
        return NULL;
    }

    for (i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;

            switch (start[i]) {
                case 'n':
                    value[output_len++] = '\n';
                    break;
                case 'r':
                    value[output_len++] = '\r';
                    break;
                case 't':
                    value[output_len++] = '\t';
                    break;
                case '\\':
                    value[output_len++] = '\\';
                    break;
                case '"':
                    value[output_len++] = '"';
                    break;
                default:
                    value[output_len++] = start[i];
                    break;
            }

            continue;
        }

        value[output_len++] = start[i];
    }

    value[output_len] = '\0';
    return value;
}

static char *shell_escape_argument(const char *arg) {
    size_t len = strlen(arg);
    char *escaped = malloc((len * 4) + 3);
    size_t i;
    size_t j = 0;

    if (escaped == NULL) {
        return NULL;
    }

    escaped[j++] = '\'';
    for (i = 0; i < len; i++) {
        if (arg[i] == '\'') {
            memcpy(&escaped[j], "'\\''", 4);
            j += 4;
            continue;
        }

        escaped[j++] = arg[i];
    }
    escaped[j++] = '\'';
    escaped[j] = '\0';
    return escaped;
}

static int parse_saved_command_record(const char *record_start, const char **next_record, SavedCommand *entry) {
    const char *command_end = strstr(record_start, "\", \"note\": \"");
    const char *note_start;
    const char *note_end;
    const char *timestamp_start;
    const char *timestamp_end;

    if (command_end == NULL) {
        return -1;
    }

    entry->command = unescape_json_substring(record_start, (size_t)(command_end - record_start));
    if (entry->command == NULL) {
        return -1;
    }

    note_start = command_end + strlen("\", \"note\": \"");
    note_end = strstr(note_start, "\", \"timestamp\": \"");
    if (note_end == NULL) {
        return -1;
    }

    entry->note = unescape_json_substring(note_start, (size_t)(note_end - note_start));
    if (entry->note == NULL) {
        return -1;
    }

    timestamp_start = note_end + strlen("\", \"timestamp\": \"");
    timestamp_end = strstr(timestamp_start, "\"}");
    if (timestamp_end == NULL) {
        return -1;
    }

    entry->timestamp = unescape_json_substring(timestamp_start, (size_t)(timestamp_end - timestamp_start));
    if (entry->timestamp == NULL) {
        return -1;
    }

    *next_record = timestamp_end + strlen("\"}");
    return 0;
}

static void free_saved_commands(SavedCommand *commands, size_t count) {
    size_t i;

    if (commands == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        free(commands[i].command);
        free(commands[i].note);
        free(commands[i].timestamp);
    }

    free(commands);
}

/**
 * @brief Load the saved command history from disk.
 *
 * This parser accepts both the current single-line JSON records and older malformed entries
 * where embedded newlines were written directly inside the note string.
 *
 * @param commands_out Receives a heap-allocated array of parsed entries.
 * @param count_out Receives the number of parsed entries.
 * @return int `0` on success, non-zero on failure.
 */
static int load_saved_commands(SavedCommand **commands_out, size_t *count_out) {
    char *history_path = get_history_file_path();
    FILE *file;
    SavedCommand *commands = NULL;
    size_t count = 0;
    size_t capacity = 0;
    char *contents;
    long file_size;
    const char *cursor;
    const char *record_start;

    if (history_path == NULL) {
        fprintf(stderr, "Failed creating history path\n");
        return 1;
    }

    file = fopen(history_path, "r");
    free(history_path);

    if (file == NULL) {
        if (errno == ENOENT) {
            *commands_out = NULL;
            *count_out = 0;
            return 0;
        }

        perror("Failed opening file");
        return 1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Failed reading file");
        fclose(file);
        return 1;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        perror("Failed reading file");
        fclose(file);
        return 1;
    }

    rewind(file);

    contents = malloc((size_t)file_size + 1);
    if (contents == NULL) {
        perror("Failed allocating memory");
        fclose(file);
        return 1;
    }

    if (fread(contents, 1, (size_t)file_size, file) != (size_t)file_size) {
        perror("Failed reading file");
        free(contents);
        fclose(file);
        return 1;
    }

    contents[file_size] = '\0';
    fclose(file);

    cursor = contents;
    while ((record_start = strstr(cursor, "{\"command\": \"")) != NULL) {
        SavedCommand entry = {0};
        const char *next_record = NULL;

        record_start += strlen("{\"command\": \"");
        if (parse_saved_command_record(record_start, &next_record, &entry) != 0) {
            free(entry.command);
            free(entry.note);
            free(entry.timestamp);
            cursor = record_start;
            continue;
        }

        trim_trailing_newlines(entry.note);

        if (count == capacity) {
            size_t new_capacity = (capacity == 0) ? 8 : capacity * 2;
            SavedCommand *resized = realloc(commands, new_capacity * sizeof(SavedCommand));

            if (resized == NULL) {
                perror("Failed allocating memory");
                free(entry.command);
                free(entry.note);
                free(entry.timestamp);
                free(contents);
                free_saved_commands(commands, count);
                return 1;
            }

            commands = resized;
            capacity = new_capacity;
        }

        commands[count++] = entry;
        cursor = next_record;
    }

    free(contents);
    *commands_out = commands;
    *count_out = count;
    return 0;
}

static void print_saved_commands(const SavedCommand *commands, size_t count) {
    size_t i;

    if (count == 0) {
        printf("No saved commands found.\n");
        return;
    }

    for (i = 0; i < count; i++) {
        printf("%zu. %s\n", i + 1, commands[i].command);
        if (commands[i].note[0] != '\0') {
            printf("   note: %s\n", commands[i].note);
        }
        printf("   saved: %s\n", commands[i].timestamp);
    }
}

/**
 * @brief Rewrite the history file with the provided command list.
 *
 * @param commands Array of command entries to persist.
 * @param count Number of entries in the array.
 * @return int `0` on success, non-zero on failure.
 */
static int write_saved_commands(const SavedCommand *commands, size_t count) {
    char *history_path = get_history_file_path();
    FILE *file;
    size_t i;

    if (history_path == NULL) {
        fprintf(stderr, "Failed creating history path\n");
        return 1;
    }

    file = fopen(history_path, "w");
    if (file == NULL) {
        perror("Failed opening file");
        free(history_path);
        return 1;
    }

    for (i = 0; i < count; i++) {
        char *escaped_command = escape_json_string(commands[i].command);
        char *escaped_note = escape_json_string(commands[i].note);

        if (escaped_command == NULL || escaped_note == NULL) {
            fprintf(stderr, "Error allocating memory\n");
            free(escaped_command);
            free(escaped_note);
            fclose(file);
            free(history_path);
            return 1;
        }

        fprintf(file, "{\"command\": \"%s\", \"note\": \"%s\", \"timestamp\": \"%s\"}\n",
                escaped_command,
                escaped_note,
                commands[i].timestamp);

        free(escaped_command);
        free(escaped_note);
    }

    fclose(file);
    free(history_path);
    return 0;
}

static void print_truncated_text(const char *text, size_t max_len) {
    size_t len = strlen(text);

    if (len <= max_len) {
        printf("%s", text);
        return;
    }

    if (max_len <= 3) {
        printf("%.*s", (int)max_len, text);
        return;
    }

    printf("%.*s...", (int)(max_len - 3), text);
}

static int contains_case_insensitive(const char *text, const char *query) {
    size_t text_len;
    size_t query_len;
    size_t i;
    size_t j;

    if (query == NULL || query[0] == '\0') {
        return 1;
    }

    if (text == NULL) {
        return 0;
    }

    text_len = strlen(text);
    query_len = strlen(query);

    if (query_len > text_len) {
        return 0;
    }

    for (i = 0; i + query_len <= text_len; i++) {
        for (j = 0; j < query_len; j++) {
            if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)query[j])) {
                break;
            }
        }

        if (j == query_len) {
            return 1;
        }
    }

    return 0;
}

static size_t rebuild_filtered_indices(const SavedCommand *commands, size_t count, const char *query, size_t *filtered_indices) {
    size_t i;
    size_t filtered_count = 0;

    for (i = 0; i < count; i++) {
        if (contains_case_insensitive(commands[i].command, query) ||
            contains_case_insensitive(commands[i].note, query) ||
            contains_case_insensitive(commands[i].timestamp, query)) {
            filtered_indices[filtered_count++] = i;
        }
    }

    return filtered_count;
}

static void render_saved_command_picker(
    const SavedCommand *commands,
    size_t total_count,
    const size_t *filtered_indices,
    size_t filtered_count,
    size_t selected_filtered,
    const char *query,
    int pending_action
) {
    size_t i;
    size_t window_start = 0;
    size_t window_end = filtered_count;
    size_t max_rows = 8;

    printf("\033[H\033[J");
    printf("Saved Commands\n");
    printf("============================================================\n");
    printf("Search : %s\n", query[0] != '\0' ? query : "(type to filter)");
    printf("Matches: %zu of %zu\n", filtered_count, total_count);
    printf("Controls: Up/Down move | Enter run | Tab send to shell\n");
    printf("          Backspace edit filter | Ctrl-U clear | Ctrl-D delete | Ctrl-X quit\n");
    printf("------------------------------------------------------------\n");

    if (filtered_count == 0) {
        printf("  No saved commands match the current filter.\n");
    } else {
        if (filtered_count > max_rows) {
            if (selected_filtered > max_rows / 2) {
                window_start = selected_filtered - (max_rows / 2);
            }

            if (window_start + max_rows > filtered_count) {
                window_start = filtered_count - max_rows;
            }

            window_end = window_start + max_rows;
        }

        for (i = window_start; i < window_end; i++) {
            size_t command_index = filtered_indices[i];

            printf("%c %2zu. ", (i == selected_filtered) ? '>' : ' ', command_index + 1);
            print_truncated_text(commands[command_index].command, 64);
            printf("\n");
        }

        if (window_end < filtered_count) {
            printf("  ...\n");
        }
    }

    printf("------------------------------------------------------------\n");

    if (filtered_count == 0) {
        printf("Selected: none\n");
        printf("Command : -\n");
        printf("Saved   : -\n");
        printf("Note    : -\n");
    } else {
        const SavedCommand *current = &commands[filtered_indices[selected_filtered]];

        printf("Selected: %zu/%zu\n", selected_filtered + 1, filtered_count);
        printf("Command : %s\n", current->command);
        printf("Saved   : %s\n", current->timestamp);
        printf("Note    : %s\n", current->note[0] != '\0' ? current->note : "(none)");
    }

    printf("------------------------------------------------------------\n");

    if (pending_action == 1 && filtered_count > 0) {
        printf("Delete armed. Press Ctrl-D again to delete this command.\n");
        printf("Any other key cancels the deletion.\n");
    } else if (pending_action == 2) {
        printf("Quit armed. Press Ctrl-X again to close the picker.\n");
        printf("Any other key cancels the quit request.\n");
    } else if (filtered_count == 0) {
        printf("Type to change the filter or press Ctrl-X twice to quit.\n");
    } else {
        printf("Enter runs the selected command.\n");
        printf("Tab sends it to the shell integration without running it.\n");
    }

    fflush(stdout);
}

/**
 * @brief Write the selected command to the shell integration handoff file.
 *
 * @param command Command string to send back to the shell wrapper.
 * @return int `0` if a selection file was written, `1` if no selection file is configured, `-1` on error.
 */
static int write_selected_command_to_file(const char *command) {
    const char *selection_path = getenv("NOTECMD_SELECTION_FILE");
    FILE *file;

    if (selection_path == NULL || selection_path[0] == '\0') {
        return 1;
    }

    file = fopen(selection_path, "w");
    if (file == NULL) {
        perror("Failed opening selection file");
        return -1;
    }

    fprintf(file, "%s", command);
    fclose(file);
    return 0;
}

static int inject_command_into_tty(int tty_fd, const char *command) {
#ifdef TIOCSTI
    size_t i;

    for (i = 0; command[i] != '\0'; i++) {
        char ch = command[i];

        if (ioctl(tty_fd, TIOCSTI, &ch) != 0) {
            return -1;
        }
    }

    return 0;
#else
    (void)tty_fd;
    printf("Copying directly to the shell prompt is not supported on this system.\n");
    printf("Selected command:\n%s\n", command);
    return 0;
#endif
}

static int copy_command_to_shell_prompt(const char *command) {
#ifdef TIOCSTI
    pid_t pid = fork();

    if (pid < 0) {
        perror("Failed fork");
        return 1;
    }

    if (pid == 0) {
        int tty_fd;
        pid_t current_pgrp;
        int attempts = 0;

        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGHUP, SIG_IGN);

        tty_fd = open("/dev/tty", O_RDWR);
        if (tty_fd < 0) {
            _exit(1);
        }

        current_pgrp = getpgrp();
        while (attempts < 100) {
            pid_t foreground_pgrp = tcgetpgrp(tty_fd);

            if (foreground_pgrp >= 0 && foreground_pgrp != current_pgrp) {
                break;
            }

            usleep(10000);
            attempts++;
        }

        usleep(50000);

        if (inject_command_into_tty(tty_fd, command) != 0) {
            dprintf(tty_fd, "\nSelected command:\n%s\n", command);
            close(tty_fd);
            _exit(1);
        }

        close(tty_fd);
        _exit(0);
    }

    return 0;
#else
    return inject_command_into_tty(STDIN_FILENO, command);
#endif
}

static int delete_saved_command(SavedCommand *commands, size_t *count, size_t index) {
    size_t i;

    free(commands[index].command);
    free(commands[index].note);
    free(commands[index].timestamp);

    for (i = index; i + 1 < *count; i++) {
        commands[i] = commands[i + 1];
    }

    (*count)--;
    return write_saved_commands(commands, *count);
}

/**
 * @brief Execute a saved command through `/bin/sh -c`.
 *
 * @param command Saved command entry to execute.
 * @return int Child exit status, or non-zero on launcher failure.
 */
static int run_saved_command(const SavedCommand *command) {
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        perror("Failed fork");
        return 1;
    }

    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command->command, (char *)NULL);
        perror("Failed exec");
        exit(127);
    }

    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return 1;
}

/**
 * @brief Open the interactive picker and act on the selected history entry.
 *
 * Supported actions include execute, send-to-shell, delete with confirmation, and quit with confirmation.
 *
 * @param commands Mutable history array.
 * @param count Number of entries in the history array; updated after deletions.
 * @return int Process exit code for the selected action.
 */
static int select_saved_command_and_run(SavedCommand *commands, size_t *count) {
    size_t selected_filtered = 0;
    size_t filtered_count;
    size_t *filtered_indices;
    char query[256] = "";
    int key;
    int pending_action = 0;

    if (*count == 0) {
        printf("No saved commands found.\n");
        return EXIT_FAILURE;
    }

    if (!is_test_mode_enabled() && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
        fprintf(stderr, "Interactive selection requires a TTY.\n");
        return EXIT_FAILURE;
    }

    filtered_indices = malloc(sizeof(size_t) * (*count));
    if (filtered_indices == NULL) {
        fprintf(stderr, "Error allocating memory\n");
        return EXIT_FAILURE;
    }

    filtered_count = rebuild_filtered_indices(commands, *count, query, filtered_indices);
    enable_raw_mode();

    while (1) {
        render_saved_command_picker(commands, *count, filtered_indices, filtered_count, selected_filtered, query, pending_action);

        key = read_key();

        if (pending_action == 1) {
            if (key == 4 && filtered_count > 0) {
                if (delete_saved_command(commands, count, filtered_indices[selected_filtered]) != 0) {
                    reset_terminal();
                    free(filtered_indices);
                    return EXIT_FAILURE;
                }

                pending_action = 0;

                if (*count == 0) {
                    reset_terminal();
                    free(filtered_indices);
                    printf("Deleted the last saved command.\n");
                    return EXIT_SUCCESS;
                }

                filtered_count = rebuild_filtered_indices(commands, *count, query, filtered_indices);
                if (filtered_count == 0) {
                    selected_filtered = 0;
                } else if (selected_filtered >= filtered_count) {
                    selected_filtered = filtered_count - 1;
                }

                continue;
            }

            pending_action = 0;
        } else if (pending_action == 2) {
            if (key == 24) {
                reset_terminal();
                free(filtered_indices);
                printf("Selection cancelled.\n");
                return EXIT_SUCCESS;
            }

            pending_action = 0;
        }

        if ((key == 10 || key == 13) && filtered_count > 0) {
            break;
        }

        if (key == '\t') {
            int write_result;

            if (filtered_count == 0) {
                continue;
            }

            printf("\033[H\033[J");
            fflush(stdout);
            reset_terminal();

            write_result = write_selected_command_to_file(commands[filtered_indices[selected_filtered]].command);
            if (write_result == 0) {
                free(filtered_indices);
                return EXIT_SUCCESS;
            }

            if (write_result < 0) {
                free(filtered_indices);
                return EXIT_FAILURE;
            }

            key = copy_command_to_shell_prompt(commands[filtered_indices[selected_filtered]].command);
            free(filtered_indices);
            return key;
        }

        if (key == 4 && filtered_count > 0) {
            pending_action = 1;
            continue;
        }

        if (key == 24) {
            pending_action = 2;
            continue;
        }

        if ((key == 127 || key == 8) && query[0] != '\0') {
            query[strlen(query) - 1] = '\0';
            filtered_count = rebuild_filtered_indices(commands, *count, query, filtered_indices);
            selected_filtered = 0;
            continue;
        }

        if (key == 21) {
            query[0] = '\0';
            filtered_count = rebuild_filtered_indices(commands, *count, query, filtered_indices);
            selected_filtered = 0;
            continue;
        }

        if (key >= 32 && key <= 126 && strlen(query) + 1 < sizeof(query)) {
            size_t query_len = strlen(query);

            query[query_len] = (char)key;
            query[query_len + 1] = '\0';
            filtered_count = rebuild_filtered_indices(commands, *count, query, filtered_indices);
            selected_filtered = 0;
            continue;
        }

        if (key == 1000 && selected_filtered > 0) {
            selected_filtered--;
        } else if (key == 1001 && selected_filtered + 1 < filtered_count) {
            selected_filtered++;
        }
    }

    reset_terminal();
    printf("Running: %s\n", commands[filtered_indices[selected_filtered]].command);
    key = run_saved_command(&commands[filtered_indices[selected_filtered]]);
    free(filtered_indices);
    return key;
}

/**
 * @brief Ask the user whether they want to attach a note after running a command.
 *
 * @return int `1` if the user selected "Yes", `0` otherwise.
 */
int get_yes_no_selection() {
    int selection = 0;
    int key;

   
    printf("Do you want to add a note?\n");
    printf(">> Yes\n   No\n");
    fflush(stdout);

    while (1) {
        key = read_key();

        if (key == 10 || key == 13) break;
        
        if (key == 1002 || key == 1001) selection = 1;
        if (key == 1003 || key == 1000) selection = 0;

        printf("\033[2A\033[K"); 
        if (selection == 0) {
            printf(">> Yes\n   No\n");
        } else {
            printf("   Yes\n>> No\n");

        }
        fflush(stdout);
    }

    return (selection == 0) ? 1 : 0;
}

/**
 * @brief Build a shell-safe command string from a slice of `argv`.
 *
 * Every argument is single-quoted so the saved history can be replayed faithfully later.
 *
 * @param argv Original process argument vector.
 * @param start_index Index of the first command token.
 * @return char* Heap-allocated command string, or `NULL` on allocation failure.
 */
char* build_command_string(char *argv[], int start_index) {
    size_t total_len = 0;
    int i;

    for (i = start_index; argv[i] != NULL; i++) {
        total_len += (strlen(argv[i]) * 4) + 2;
        total_len += 1;
    }

    char *command = malloc(total_len + 1);
    if (command == NULL) {
        return NULL;
    }

    command[0] = '\0';
    for (i = start_index; argv[i] != NULL; i++) {
        char *escaped_arg = shell_escape_argument(argv[i]);

        if (escaped_arg == NULL) {
            free(command);
            return NULL;
        }

        strcat(command, escaped_arg);
        free(escaped_arg);

        if (argv[i + 1] != NULL) {
            strcat(command, " ");
        }
    }

    return command;
}

/**
 * @brief Return the absolute path to the history file under the user's home directory.
 *
 * @return char* Heap-allocated path string, or `NULL` on failure.
 */
char* get_history_file_path() {
    const char *home = getenv("HOME");
    if (home == NULL) return NULL;
    
    char *dir_path = malloc(strlen(home) + 32);
    char *file_path = malloc(strlen(home) + 50);
    
    if (!dir_path || !file_path) {
        free(dir_path);
        free(file_path);
        return NULL;
    }

    sprintf(dir_path, "%s/.local/share/notecmd", home);
    sprintf(file_path, "%s/.local/share/notecmd/history.json", home);

    if (create_directories(dir_path) != 0) {
        free(dir_path);
        free(file_path);
        return NULL;
    }

    free(dir_path);
    return file_path;
}

/**
 * @brief Append one executed command to the history file.
 *
 * @param command Command string to persist.
 * @param note Optional note, may be `NULL`.
 * @return int `0` on success, non-zero on failure.
 */
int save_command_json(const char *command, const char *note) {
    char *history_path = get_history_file_path();
    FILE *f;
    char *escaped_command;
    char *escaped_note;
    char *note_copy = NULL;

    if (history_path == NULL) {
        fprintf(stderr, "Failed creating history path\n");
        return 1;
    }

    f = fopen(history_path, "a");
    if (f == NULL) {
        perror("Failed opening file");
        free(history_path);
        return 1;
    }

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    if (note != NULL) {
        note_copy = duplicate_substring(note, strlen(note));
        if (note_copy == NULL) {
            fprintf(stderr, "Error allocating memory\n");
            fclose(f);
            free(history_path);
            return 1;
        }

        trim_trailing_newlines(note_copy);
    }

    escaped_command = escape_json_string(command);
    escaped_note = escape_json_string(note_copy != NULL ? note_copy : "");
    free(note_copy);

    if (escaped_command == NULL || escaped_note == NULL) {
        fprintf(stderr, "Error allocating memory\n");
        free(escaped_command);
        free(escaped_note);
        fclose(f);
        free(history_path);
        return 1;
    }

    fprintf(f, "{\"command\": \"%s\", \"note\": \"%s\", \"timestamp\": \"%s\"}\n", 
            escaped_command, 
            escaped_note, 
            timestamp);
    
    free(escaped_command);
    free(escaped_note);
    fclose(f);
    free(history_path);
    return 0;
}

/**
 * @brief Execute a command from `argv`, optionally prompt for a note, then save it to history.
 *
 * @param argv Original process argument vector.
 * @param command_index Index of the executable token in `argv`.
 * @param note Optional note supplied from the CLI.
 * @return int Exit status of the executed child command, or non-zero on failure.
 */
int process_input(char *argv[], int command_index, char* note) {
    char *command_str = build_command_string(argv, command_index);
    if (command_str == NULL) {
        fprintf(stderr, "Error allocating memory\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Failed fork");
        free(command_str);
        return 1;
    }

    if (pid == 0) {
        execvp(argv[command_index], &argv[command_index]);
        perror("Failed execvp");
        exit(127);
    } else {
        int status;
        int exit_status;
        waitpid(pid, &status, 0);

        printf("----------------------------------------------------------\n");

        printf("You're trying to save the command:\n%s\n", command_str);

        if (note == NULL) {
            enable_raw_mode();
            int wants_note = get_yes_no_selection();
            reset_terminal();

            if (wants_note) {
                printf("Add a note: ");
                char buffer[256];
                if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
                    trim_trailing_newlines(buffer);
                    note = buffer;
                }
            } else {
                printf("No note added.\n");
            }
        } else {
            printf("Existing note found: %s\n", note);
        }

        printf("----------------------------------------------------------\n");

        printf("[Recap]\n");
        printf("Command: %s\n", command_str);
        printf("Note: %s\n", note ? note : "");

        if (save_command_json(command_str, note) != 0) {
            free(command_str);
            return 1;
        }

        exit_status = WEXITSTATUS(status);
        free(command_str);

        return exit_status;
    }
}

static int resolve_binary_path(const char *argv0, char *resolved_path, size_t resolved_size) {
    char candidate[PATH_MAX];

    if (argv0 == NULL || resolved_path == NULL || resolved_size == 0) {
        return -1;
    }

    if (strchr(argv0, '/') != NULL) {
        if (realpath(argv0, resolved_path) != NULL) {
            return 0;
        }

        snprintf(resolved_path, resolved_size, "%s", argv0);
        return -1;
    }

    {
        const char *path_env = getenv("PATH");

        if (path_env != NULL) {
            char *path_copy = duplicate_substring(path_env, strlen(path_env));
            char *saveptr = NULL;
            char *token;

            if (path_copy == NULL) {
                return -1;
            }

            for (token = strtok_r(path_copy, ":", &saveptr); token != NULL; token = strtok_r(NULL, ":", &saveptr)) {
                snprintf(candidate, sizeof(candidate), "%s/%s", token, argv0);
                if (access(candidate, X_OK) == 0) {
                    if (realpath(candidate, resolved_path) != NULL) {
                        free(path_copy);
                        return 0;
                    }

                    snprintf(resolved_path, resolved_size, "%s", candidate);
                    free(path_copy);
                    return 0;
                }
            }

            free(path_copy);
        }
    }

    snprintf(resolved_path, resolved_size, "%s", argv0);
    return -1;
}

static const char *detect_shell_name() {
    const char *shell_path = getenv("SHELL");
    const char *shell_name;

    if (shell_path == NULL || shell_path[0] == '\0') {
        return NULL;
    }

    shell_name = strrchr(shell_path, '/');
    return shell_name != NULL ? shell_name + 1 : shell_path;
}

/**
 * @brief Emit shell integration code for `zsh` or `bash`.
 *
 * @param argv0 Program name used to resolve the binary path.
 * @param shell_name Target shell name.
 * @return int `0` on success, non-zero on failure or unsupported shell.
 */
static int print_shell_init_script(const char *argv0, const char *shell_name) {
    char resolved_path[PATH_MAX];
    char *escaped_path;

    resolve_binary_path(argv0, resolved_path, sizeof(resolved_path));
    escaped_path = shell_escape_argument(resolved_path);
    if (escaped_path == NULL) {
        fprintf(stderr, "Error allocating memory\n");
        return 1;
    }

    printf(": ${NOTECMD_BIN:=%s}\n", escaped_path);
    printf("__notecmd_pick() {\n");
    printf("  local tmp picker_status\n");
    printf("  tmp=\"$(mktemp \"${TMPDIR:-/tmp}/notecmd.XXXXXX\")\" || return 1\n");
    printf("  NOTECMD_SELECTION_FILE=\"$tmp\" \"$NOTECMD_BIN\" </dev/tty >/dev/tty 2>/dev/tty\n");
    printf("  picker_status=$?\n");
    printf("  if [ -s \"$tmp\" ]; then\n");
    printf("    cat \"$tmp\"\n");
    printf("  fi\n");
    printf("  rm -f \"$tmp\"\n");
    printf("  return $picker_status\n");
    printf("}\n");

    if (strcmp(shell_name, "zsh") == 0) {
        printf("notecmd() {\n");
        printf("  local cmd picker_status\n");
        printf("  if [ \"$#\" -gt 0 ]; then\n");
        printf("    \"$NOTECMD_BIN\" \"$@\"\n");
        printf("    return $?\n");
        printf("  fi\n");
        printf("  cmd=\"$(__notecmd_pick)\"\n");
        printf("  picker_status=$?\n");
        printf("  [ $picker_status -ne 0 ] && return $picker_status\n");
        printf("  if [ -n \"$cmd\" ]; then\n");
        printf("    print -z -- \"$cmd\"\n");
        printf("  fi\n");
        printf("}\n");
        printf("__notecmd_widget() {\n");
        printf("  local cmd picker_status\n");
        printf("  cmd=\"$(__notecmd_pick)\"\n");
        printf("  picker_status=$?\n");
        printf("  [ $picker_status -ne 0 ] && zle redisplay && return $picker_status\n");
        printf("  if [ -n \"$cmd\" ]; then\n");
        printf("    BUFFER=\"$cmd\"\n");
        printf("    CURSOR=${#BUFFER}\n");
        printf("  fi\n");
        printf("  zle redisplay\n");
        printf("}\n");
        printf("zle -N notecmd-widget __notecmd_widget\n");
        printf("bindkey '^X^N' notecmd-widget\n");
    } else if (strcmp(shell_name, "bash") == 0) {
        printf("notecmd() {\n");
        printf("  local cmd picker_status\n");
        printf("  if [ \"$#\" -gt 0 ]; then\n");
        printf("    \"$NOTECMD_BIN\" \"$@\"\n");
        printf("    return $?\n");
        printf("  fi\n");
        printf("  cmd=\"$(__notecmd_pick)\"\n");
        printf("  picker_status=$?\n");
        printf("  [ $picker_status -ne 0 ] && return $picker_status\n");
        printf("  if [ -n \"$cmd\" ]; then\n");
        printf("    history -s -- \"$cmd\"\n");
        printf("    printf 'Selected command added to history. Press Up to edit it.\\n'\n");
        printf("  fi\n");
        printf("}\n");
        printf("__notecmd_widget() {\n");
        printf("  local cmd picker_status\n");
        printf("  cmd=\"$(__notecmd_pick)\"\n");
        printf("  picker_status=$?\n");
        printf("  [ $picker_status -ne 0 ] && return $picker_status\n");
        printf("  if [ -n \"$cmd\" ]; then\n");
        printf("    READLINE_LINE=\"$cmd\"\n");
        printf("    READLINE_POINT=${#READLINE_LINE}\n");
        printf("  fi\n");
        printf("}\n");
        printf("bind -x '\"\\C-x\\C-n\":__notecmd_widget'\n");
    } else {
        fprintf(stderr, "Unsupported shell '%s'. Use 'zsh' or 'bash'.\n", shell_name);
        free(escaped_path);
        return 1;
    }

    free(escaped_path);
    return 0;
}

/**
 * @brief Print CLI usage information.
 *
 * @param program_name Name or path used to invoke the binary.
 */
void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] [ARGUMENTS]\n", program_name);
    printf("       %s init [zsh|bash]\n", program_name);
    printf("\nOptions:\n");
    printf("  -h, --help      Show help\n");
    printf("  -v, --version   Show version\n");
    printf("  -n, --note      Include a note for the command\n");
    printf("  -l, --list      List all the saved commands\n");
    printf("\nWithout arguments, opens an interactive picker for saved commands.\n");
    printf("Use 'init zsh' or 'init bash' to print shell integration.\n");

}

/**
 * @brief Print the current program version.
 */
void print_version() {
    printf("%s version %s\n", "notecmd", VERSION);
}

/**
 * @brief Program entry point.
 *
 * With no positional command, notecmd opens the interactive picker. Otherwise it executes the
 * requested command, optionally stores a note, and appends the result to history.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return int Process exit code.
 */
int main(int argc, char *argv[]) {
    int opt;
    int command_index = 1;
    int parse_argc = argc;
    char *note = NULL;
    SavedCommand *saved_commands = NULL;
    size_t saved_count = 0;

    if (argc < 2) {
        if (load_saved_commands(&saved_commands, &saved_count) != 0) {
            return EXIT_FAILURE;
        }

        opt = select_saved_command_and_run(saved_commands, &saved_count);
        free_saved_commands(saved_commands, saved_count);
        return opt;
    }

    if (strcmp(argv[1], "init") == 0) {
        const char *shell_name = argc >= 3 ? argv[2] : detect_shell_name();

        if (shell_name == NULL) {
            fprintf(stderr, "Unable to detect the shell. Use '%s init zsh' or '%s init bash'.\n", argv[0], argv[0]);
            return EXIT_FAILURE;
        }

        return print_shell_init_script(argv[0], shell_name) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    static struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {"note",    required_argument, 0, 'n'},
        {"list",    no_argument,       0, 'l'},
        {0,         0,                 0, 0}
    };

    while (command_index < argc) {
        if (strcmp(argv[command_index], "--") == 0) {
            command_index++;
            break;
        }

        if (argv[command_index][0] != '-') {
            break;
        }

        if (strcmp(argv[command_index], "-n") == 0 ||
            strcmp(argv[command_index], "--note") == 0) {
            if (command_index + 1 >= argc) {
                fprintf(stderr, "Option '%s' requires an argument.\n", argv[command_index]);
                return EXIT_FAILURE;
            }
            command_index += 2;
            continue;
        }

        command_index++;
    }

    parse_argc = command_index;
    optind = 1;

    while ((opt = getopt_long(parse_argc, argv, "hvn:l", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            case 'v':
                print_version();
                return EXIT_SUCCESS;
            case 'n':
                note = optarg;
                break;
            case 'l':
                if (load_saved_commands(&saved_commands, &saved_count) != 0) {
                    return EXIT_FAILURE;
                }

                print_saved_commands(saved_commands, saved_count);
                free_saved_commands(saved_commands, saved_count);
                return EXIT_SUCCESS;
            case '?':
                fprintf(stderr, "Use '%s --help' for more info.\n", argv[0]);
                return EXIT_FAILURE;
            default:
                abort();
        }
    }

    if (command_index >= argc) {
        fprintf(stderr, "No command provided.\n");
        return EXIT_FAILURE;
    }

    return process_input(argv, command_index, note);
}
