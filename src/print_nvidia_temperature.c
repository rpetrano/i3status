// vim:ts=8:expandtab
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include "i3status.h"

// TODO: path in config
char *default_command[] =
        { "/usr/bin/nvidia-smi", "-l", "", "-q", "-d", "TEMPERATURE", NULL };

int parse_output(
        const char * const output,
        int * const temp
) {
        fprintf(stderr, "%s\n", output);
        char temp_str[16];
        char *j = temp_str;

        bool good_line = false;
        for (const char *i = output; *i != 0; i++) {
                if (BEGINS_WITH(i, "GPU Current Temp")) {
                    good_line = true;
                    j = temp_str;
                }

                if (*i == '\r' || *i == '\n') {
                    good_line = false;
                }

                if (good_line && *i >= '0' && *i <= '9') {
                    *(j++) = *i;
                }
        }

        *j = 0;
        return sscanf(temp_str, "%d", temp);
}

void print_nvidia_temperature_info(
        yajl_gen json_gen,
        char *buffer,
        int interval,
        const char *format,
        int max_threshold
) {
        char *outwalk = buffer;

        static int pipefd[2];
        static int forked = false;
        pid_t cpid = 1;
        char temp_buff[4096];

        // if the nvidia-smi process hasn't been spawned
        if (!forked) {
            if (pipe(pipefd) == -1) {
                    perror("pipe");
                    return;
            }

            cpid = fork();
            forked = true;
        }

        switch (cpid) {
                case -1:
                        perror("fork");
                        return;

                case 0:
                        close(pipefd[0]);
                        dup2(pipefd[1], 1);

                        char buff[16];
                        sprintf(buff, "%d", interval);
                        default_command[2] = buff;

                        execve(*default_command, default_command, default_command + 6);

                        perror("execve");
                        exit(0);
        }

        struct pollfd poll_fd = { pipefd[0], POLLIN };
        int poll_st = poll(&poll_fd, 1, 0);

        if (poll_st < 0) {
                perror("poll");
                return;
        }

        if (poll_st == 0) {
                return;
        }



        for (const char * walk = format; *walk != 0; walk++) {
                if (*walk != '%') {
                        *(outwalk++) = *walk;
                        continue;
                }

                if (BEGINS_WITH(walk + 1, "degrees")) {
                        bool colorful_output = false;

                        int read_st = read(pipefd[0], temp_buff, sizeof(temp_buff));
                        if (read_st < 0) {
                                perror("read");
                                return;
                        }

                        // TODO: check if failed
                        int temp;
                        parse_output(temp_buff, &temp);

                        if (temp >= max_threshold) {
                            START_COLOR("color_bad");
                            colorful_output = true;
                        }

                        outwalk += sprintf(outwalk, "%d", temp);

                        if (colorful_output) {
                                END_COLOR;
                        }

                        walk += strlen("degrees");
                }
        }

        OUTPUT_FULL_TEXT(buffer);
}

