#include "../include/cli.h"
#include "../include/command_handler.h"
#include "../include/response_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <readline/readline.h>
#include <readline/history.h>

static volatile int line_ready = 0;
static char *latest_input = NULL; /* malloc'd by handle_line, freed by consumer */

/* readline callback invoked when a full line is ready */
static void handle_line(char *line) {
    if (!line) {
        line_ready = 1;
        latest_input = strdup("exit"); /* treat Ctrl+D as exit */
        return;
    }
    if (line[0] != '\0') {
        add_history(line);
    }
    latest_input = line; /* readline malloc'd this; ours to free now */
    line_ready = 1;
}

/* Trims leading/trailing whitespace, returning a newly-allocated string. */
static char *trim_dup(const char *s) {
    const char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start == '\0') return strdup("");

    const char *end = s + strlen(s) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;

    size_t len = (size_t)(end - start + 1);
    char *out = malloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static void print_help(void) {
    printf(
        "my_redis_cli 1.0.0\n"
        "Usage: \n"
        "      With arguments:            ./my_redis_cli -h <host> -p <port>\n"
        "      Default Host (127.0.0.1):  ./my_redis_cli -p <port>\n"
        "      Default Port (6379):       ./my_redis_cli -h <host>\n"
        "      One-shot execution:        ./my_redis_cli <command> [arguments]\n"
        "\n"
        "Interactive Mode (REPL):\n"
        "      ./my_redis_cli\n"
        "      Type Redis commands directly.\n"
        "\n"
        "To get help about Redis commands type:\n"
        "      \"help\" to display this help message\n"
        "      \"quit\" to exit\n"
        "\n"
        "Examples:\n"
        "      ./my_redis_cli PING\n"
        "      ./my_redis_cli SET mykey \"Hello World\"\n"
        "      ./my_redis_cli GET mykey\n"
        "\n"
        "To set my_redis_cli preferences:\n"
        "      \":set hints\" enable online hints\n"
        "      \":set nohints\" disable online hints\n"
        "Set your preferences in ~/.myredisclirc\n"
    );
}

void cli_init(CLI *cli, const char *host, int port) {
    cli->host = strdup(host);
    cli->port = port;
    redis_client_init(&cli->redisClient, host, port);
}

void cli_destroy(CLI *cli) {
    free(cli->host);
    redis_client_destroy(&cli->redisClient);
}

void cli_execute_command(CLI *cli, char **args, int count) {
    if (count == 0) return;

    size_t cmd_len;
    char *command = command_handler_build_resp(args, count, &cmd_len);
    if (!redis_client_send_command(&cli->redisClient, command, cmd_len)) {
        fprintf(stderr, "(Error) Failed to send command.\n");
        free(command);
        return;
    }
    free(command);

    char *response = response_parser_parse(redis_client_get_socket_fd(&cli->redisClient));
    printf("%s\n", response);
    free(response);
}

void cli_handle_subscription(CLI *cli, char **args, int count) {
    size_t cmd_len;
    char *command = command_handler_build_resp(args, count, &cmd_len);
    int ok = redis_client_send_command(&cli->redisClient, command, cmd_len);
    free(command);
    if (!ok) {
        fprintf(stderr, "(Error) Failed to send SUBSCRIBE command.\n");
        return;
    }

    printf("(Subscribed) Type 'exit'/'quit' to quit subscription mode.\n");

    int sockfd = redis_client_get_socket_fd(&cli->redisClient);

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    rl_callback_handler_install("", handle_line);

    int in_subscription = 1;
    while (in_subscription) {
        int ret = poll(fds, 2, 100); /* 100ms timeout */

        if (ret < 0) {
            perror("(Error) Poll failed");
            break;
        }

        if (fds[1].revents & (POLLHUP | POLLERR)) {
            printf("\nRedis connection lost. Exiting...\n");
            rl_callback_handler_remove();
            exit(1);
        }

        if (fds[1].revents & POLLIN) {
            char buffer[1];
            ssize_t bytes = recv(sockfd, buffer, sizeof(buffer), MSG_PEEK);
            if (bytes == 0) {
                printf("\nRedis server closed the connection. Exiting...\n");
                rl_callback_handler_remove();
                exit(1);
            }
            if (bytes > 0) {
                char *message = response_parser_parse(sockfd);
                printf("%s\n", message);
                free(message);
            }
        }

        if (fds[0].revents & POLLIN) {
            rl_callback_read_char();
        }

        if (line_ready) {
            char *input = trim_dup(latest_input);
            free(latest_input);
            latest_input = NULL;
            line_ready = 0;

            if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
                char *unsub_args[1] = { "UNSUBSCRIBE" };
                size_t unsub_len;
                char *unsub_cmd = command_handler_build_resp(unsub_args, 1, &unsub_len);
                redis_client_send_command(&cli->redisClient, unsub_cmd, unsub_len);
                free(unsub_cmd);
                in_subscription = 0;
            } else {
                printf("(Info) Type 'exit'/'quit' to leave subscription mode.\n");
            }
            free(input);
        }
    }

    rl_callback_handler_remove();
    printf("(Exited subscription mode)\n");
}

void cli_run(CLI *cli, char **commandArgs, int commandArgCount) {
    int readline_active = 0;

    if (!redis_client_connect(&cli->redisClient)) {
        return;
    }

    if (commandArgCount > 0) {
        cli_execute_command(cli, commandArgs, commandArgCount);
    }

    printf("Connected to Redis at %d\n", redis_client_get_socket_fd(&cli->redisClient));

    int sockfd = redis_client_get_socket_fd(&cli->redisClient);

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    char prompt[300];
    snprintf(prompt, sizeof(prompt), "%s:%d> ", cli->host, cli->port);

    for (;;) {
        if (!readline_active) {
            rl_callback_handler_install(prompt, handle_line);
            readline_active = 1;
        }

        int ret = poll(fds, 2, -1); /* block indefinitely */
        if (ret < 0) {
            perror("(Error) Poll failed");
            if (readline_active) {
                rl_callback_handler_remove();
                readline_active = 0;
            }
            break;
        }

        if (fds[1].revents & (POLLHUP | POLLERR)) {
            printf("\nRedis connection lost. Exiting...\n");
            if (readline_active) {
                rl_callback_handler_remove();
                readline_active = 0;
            }
            break;
        }

        if (fds[1].revents & POLLIN) {
            char buffer[1];
            ssize_t bytes = recv(sockfd, buffer, sizeof(buffer), MSG_PEEK);
            if (bytes == 0) {
                printf("\nRedis server closed the connection. Exiting...\n");
                if (readline_active) {
                    rl_callback_handler_remove();
                    readline_active = 0;
                }
                break;
            }
        }

        if (fds[0].revents & POLLIN) {
            rl_callback_read_char();
        }

        if (line_ready) {
            char *line = trim_dup(latest_input);
            free(latest_input);
            latest_input = NULL;
            line_ready = 0;

            if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
                printf("Goodbye.\n");
                free(line);
                if (readline_active) {
                    rl_callback_handler_remove();
                    readline_active = 0;
                }
                break;
            }

            if (strcmp(line, "help") == 0) {
                print_help();
                free(line);
                continue;
            }

            int arg_count = 0;
            char **args = command_handler_split_args(line, &arg_count);
            free(line);
            if (arg_count == 0) {
                continue;
            }

            char first_cmd[64];
            size_t n = strlen(args[0]);
            if (n >= sizeof(first_cmd)) n = sizeof(first_cmd) - 1;
            for (size_t i = 0; i < n; ++i) {
                first_cmd[i] = (char)toupper((unsigned char)args[0][i]);
            }
            first_cmd[n] = '\0';

            if (strcmp(first_cmd, "SUBSCRIBE") == 0) {
                if (readline_active) {
                    rl_callback_handler_remove();
                    readline_active = 0;
                }
                cli_handle_subscription(cli, args, arg_count);
                command_handler_free_args(args, arg_count);
                continue; /* skip rest of loop; prompt reinstalled at top */
            }

            cli_execute_command(cli, args, arg_count);
            command_handler_free_args(args, arg_count);
        }
    }

    if (readline_active) {
        rl_callback_handler_remove();
    }
    redis_client_disconnect(&cli->redisClient);
}
