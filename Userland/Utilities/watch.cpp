/*
 * Copyright (c) 2020, Sahan Fernando <sahan.h.fernando@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Assertions.h>
#include <AK/DeprecatedString.h>
#include <AK/StringBuilder.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/FileWatcher.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <LibMain/Main.h>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int opt_interval = 2;
static bool flag_noheader = false;
static bool flag_beep_on_fail = false;
static int volatile exit_code = 0;
static volatile pid_t child_pid = -1;

static DeprecatedString build_header_string(Vector<DeprecatedString> const& command, Duration const& interval)
{
    StringBuilder builder;
    auto interval_seconds = interval.to_truncated_seconds();
    auto interval_fractional_seconds = (interval.to_truncated_milliseconds() % 1000) / 100;
    builder.appendff("Every {}.{}s: \x1b[1m", interval_seconds, interval_fractional_seconds);
    builder.join(' ', command);
    builder.append("\x1b[0m"sv);
    return builder.to_deprecated_string();
}

static DeprecatedString build_header_string(Vector<DeprecatedString> const& command, Vector<DeprecatedString> const& filenames)
{
    StringBuilder builder;
    builder.appendff("Every time any of {} changes: \x1b[1m", filenames);
    builder.join(' ', command);
    builder.append("\x1b[0m"sv);
    return builder.to_deprecated_string();
}

static void handle_signal(int signal)
{
    if (child_pid > 0) {
        if (kill(child_pid, signal) < 0) {
            perror("kill");
        }
        int status;
        if (waitpid(child_pid, &status, 0) < 0) {
            perror("waitpid");
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            exit_code = 1;
        }
    }
    exit(exit_code);
}

static int run_command(Vector<DeprecatedString> const& command)
{
    Vector<char const*> argv;
    argv.ensure_capacity(command.size() + 1);
    for (auto& arg : command)
        argv.unchecked_append(arg.characters());
    argv.unchecked_append(nullptr);

    if ((errno = posix_spawnp(const_cast<pid_t*>(&child_pid), argv[0], nullptr, nullptr, const_cast<char**>(argv.data()), environ))) {
        exit_code = 1;
        perror("posix_spawn");
        return errno;
    }

    // Wait for the child to terminate, then return its exit code.
    int status;
    pid_t exited_pid;
    do {
        exited_pid = waitpid(child_pid, &status, 0);
    } while (exited_pid < 0 && errno == EINTR);
    VERIFY(exited_pid == child_pid);
    child_pid = -1;
    if (exited_pid < 0) {
        perror("waitpid");
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return 1;
    }
}

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::signal(SIGINT, handle_signal));
    TRY(Core::System::pledge("stdio proc exec rpath"));

    Vector<DeprecatedString> files_to_watch;
    Vector<DeprecatedString> command;
    Core::ArgsParser args_parser;
    args_parser.set_stop_on_first_non_option(true);
    args_parser.set_general_help("Execute a command repeatedly, and watch its output over time.");
    args_parser.add_option(opt_interval, "Amount of time between updates", "interval", 'n', "seconds");
    args_parser.add_option(flag_noheader, "Turn off the header describing the command and interval", "no-title", 't');
    args_parser.add_option(flag_beep_on_fail, "Beep if the command has a non-zero exit code", "beep", 'b');
    Core::ArgsParser::Option file_arg {
        .argument_mode = Core::ArgsParser::OptionArgumentMode::Required,
        .help_string = "Run command whenever this file changes. Can be used multiple times.",
        .long_name = "file",
        .short_name = 'f',
        .value_name = "file",
        .accept_value = [&files_to_watch](auto filename) {
            files_to_watch.append(filename);
            return true;
        }
    };
    args_parser.add_option(move(file_arg));
    args_parser.add_positional_argument(command, "Command to run", "command");
    args_parser.parse(arguments);

    DeprecatedString header;

    auto watch_callback = [&] {
        // Clear the screen, then reset the cursor position to the top left.
        warn("\033[H\033[2J");
        // Print the header.
        if (!flag_noheader) {
            warnln("{}", header);
            warnln();
        } else {
            fflush(stderr);
        }
        if (run_command(command) != 0) {
            exit_code = 1;
            if (flag_beep_on_fail) {
                warnln("\a");
                fflush(stderr);
            }
        }
    };

    if (!files_to_watch.is_empty()) {
        header = build_header_string(command, files_to_watch);

        auto file_watcher = Core::BlockingFileWatcher();
        for (auto const& file : files_to_watch) {
            if (!FileSystem::exists(file)) {
                warnln("Cannot watch '{}', it does not exist.", file);
                return 1;
            }
            if (!file_watcher.is_watching(file)) {
                auto could_add_to_watch = TRY(file_watcher.add_watch(file, Core::FileWatcherEvent::Type::MetadataModified));
                if (!could_add_to_watch) {
                    warnln("Could not add '{}' to watch list.", file);
                    return 1;
                }
            }
        }

        watch_callback();
        while (true) {
            auto maybe_event = file_watcher.wait_for_event();
            if (maybe_event.has_value()) {
                watch_callback();
            }
        }
    } else {
        TRY(Core::System::pledge("stdio proc exec"));

        Duration interval;
        if (opt_interval <= 0) {
            interval = Duration::from_milliseconds(100);
        } else {
            interval = Duration::from_seconds(opt_interval);
        }

        auto now = MonotonicTime::now();
        auto next_run_time = now;
        header = build_header_string(command, interval);
        while (true) {
            auto duration_to_sleep = (next_run_time - now).to_timespec();
            timespec remaining_sleep {};
            do {
                clock_nanosleep(CLOCK_MONOTONIC, 0, &duration_to_sleep, &remaining_sleep);
            } while (remaining_sleep.tv_sec || remaining_sleep.tv_nsec);

            watch_callback();

            now = MonotonicTime::now();
            next_run_time = next_run_time + interval;
            if (next_run_time < now) {
                // The next execution is overdue, so we set next_run_time to now to prevent drift.
                next_run_time = now;
            }
        }
    }
}
