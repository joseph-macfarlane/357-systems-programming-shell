#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "mush.h"

#define prompt "8-P "
#define newline "\n"
#define READ 0
#define WRITE 1

int interactive_mode;

void usage(char *message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

void handler(int sig) {
    if (sig == SIGINT && interactive_mode) {
        if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
            write(STDIN_FILENO, newline, strlen(newline));
            write(STDIN_FILENO, prompt, strlen(prompt));
        }
    }
}



int main(int argc, char *argv[]) {

    FILE *infile;
    char *line;
    int len;
    struct pipeline *pipeline;
    /* input: /mush2, so enter interactive mode */
    if (argc == 1) {
        infile = stdin;
        interactive_mode = 1;
    }
    /* too many arguments entered */
    else if (argc > 2) {
        usage("too many arguments");
    }
    /* input: ./mush2 [filename], so enter batch mode */
    else {
        infile = fopen(argv[1], "r");
        interactive_mode = 0;
    }

    char *path;
    /* handles SIGINTs so that they don't end the process */
    signal(SIGINT, handler);
    if (interactive_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        write(STDIN_FILENO, prompt, strlen(prompt));
    }
    /* reads through each line of stdin */
    while ((line = readLongString(infile)) != NULL && !feof(infile)) {

    /* check if nothing was entered */
        if (line == NULL || !*line || *line == "\n") {
            if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
                write(STDIN_FILENO, prompt, strlen(prompt));

            }
            continue;
        }
        if ((pipeline = crack_pipeline(line)) == NULL) {
            if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
                write(STDIN_FILENO, prompt, strlen(prompt));
            }

            continue;
        }

        len = pipeline->length;
        /* if no pipes, then */
        /* 1) check for cd */
        /* 2) else exec the command */
        if (len == 1) {
            pid_t pid;
            int ip;
            path = pipeline->stage[0].argv[0];
            if (!strncmp(path, "cd", 2)) {
                if (pipeline->stage[0].argc == 1) {
                    if (chdir("~") == -1) {
                        struct passwd *pw = getpwuid(getuid());
                        const char *homedir = pw->pw_dir;
                        if (chdir(homedir) == -1) {
                            write(STDIN_FILENO,
                              "unable to determine home directory\n",
                              strlen("unable to determine home directory\n"));
                        }
                    }
                }
                else if (pipeline->stage[0].argc == 2) {
                    if (chdir(pipeline->stage[0].argv[1]) == -1) {
                        perror(pipeline->stage[0].argv[1]);
                        /*printf("%s: Not a directory\n",
                            pipeline->stage[0].argv[1]);*/
                    }
                }
                else {
                    usage("cd: too many arguments");
                }
                if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
                    write(STDIN_FILENO, prompt, strlen(prompt));
                }
                /* since we can assume cd is the first and only */
                /* command on the line, simply continue onto next line */
                continue;
            }
            /* fork error */
            if ((pid = fork()) < 0) {
                usage("fork failed");
            }
            int in_fd, out_fd;
            /* child process */
            if (pid == 0) {
                char *inname;
                inname = pipeline->stage[0].inname;
                if (inname) {
                        if ((in_fd = open(pipeline->stage[0].inname, O_RDONLY)) == -1) {
                            perror(pipeline->stage[0].inname);
                        }
                        if (dup2(in_fd, STDIN_FILENO) == -1) {
                        perror("dup 2 infile descriptor");
                    }
                }
                char *outname;
                outname = pipeline->stage[0].outname;
                if (outname) {
                    if ((out_fd = open(outname, O_CREAT | O_RDWR | O_TRUNC, 0666)) == -1) {
                        perror(pipeline->stage[0].outname);
                    }
                    if (dup2(out_fd, STDOUT_FILENO) == -1) {
                        perror("dup 2 outfile descriptor");
                    }
                }

                execvp(pipeline->stage[0].argv[0],
                pipeline->stage[0].argv);
                /* if the execvp failed then perhaps case: foo */
                perror(pipeline->stage[0].argv[0]);
                exit(EXIT_FAILURE);

            }
            if (pid > 0) {
            /* parent waits for child to complete */
                if (wait(&ip) == -1) {
                    perror("wait failed");
                }
            }
        }
        /* begin piping */
        else {
            int i;
            int one[2];
            int two[2];
            int num_pipes;
            pid_t child;
            int num_args;
            char *inname;
            char *outname;
            inname = NULL;
            outname = NULL;

            num_args = pipeline->length;
            num_pipes = pipeline->length - 1;
            int in_fd;
            int out_fd;
            if (pipe(one)) {
                usage("pipe one");
            }

            for (i = 0; i < num_args; i++) {
                if (i < num_pipes) {
                    if (pipe(two)) {
                        usage("pipe two");
                    }
                }
                if (!(child = fork())) {
                    /* child */
                    if (i > 0) {

                        if (dup2(one[READ], STDIN_FILENO) == -1) {
                            usage("dup2 one");
                        }
                    }
                    else if (i == 0) {
                        if (pipeline->stage[i].inname) {
                            inname = pipeline->stage[i].inname;
                            if ((in_fd = open(pipeline->stage[i].inname, O_RDONLY)) == -1) {
                                usage("open inname");
                            }
                            if (dup2(in_fd, STDIN_FILENO) == -1) {
                                usage("dup2 two");
                            }
                        }
                    }
                    if (i < num_pipes) {
                        if (dup2(two[WRITE], STDOUT_FILENO) == -1) {
                            usage("dup2 two");
                        }
                    }
                    else if (i == num_pipes) {
                        if (pipeline->stage[i].outname) {
                            outname = pipeline->stage[i].outname;
                            if ((out_fd = open(outname, O_CREAT | O_RDWR | O_TRUNC, 0666)) == -1) {
                                usage("open outname");
                            }
                            if (dup2(out_fd, STDOUT_FILENO) == -1) {
                                usage("dup2 two");
                            }
                        }
                    }
                    close(one[READ]);
                    close(one[WRITE]);
                    close(two[READ]);
                    close(two[WRITE]);
                    execvp(pipeline->stage[i].argv[0],
                        pipeline->stage[i].argv);
                    perror(pipeline->stage[i].argv[0]);
                }


                /*parent */
                close(one[READ]);
                close(one[WRITE]);
                one[READ] = two[READ];
                one[WRITE] = two[WRITE];
                /* if in fd was used then close it */
                if (inname)
                        close(in_fd);
                /* if out fd was used then close it */
                else if (outname)
                        close(out_fd);
            }
            close(one[READ]);
            close(one[WRITE]);
            close(two[READ]);
            close(two[WRITE]);
            while (num_args--) {
                if (wait(NULL) == -1) {
                    perror("wait");
                }
            }


        }




       if (argc == 1 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
            write(STDIN_FILENO, prompt, strlen(prompt));
       }

    }

    return 0;
}


