#include <stdio.h>
#include <string.h>
#include <inotifytools/inotifytools.h>
#include <inotifytools/inotify.h>
#include <sys/select.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include "monitor.h"
#include <sys/stat.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>

/*void abort_handler(int signum)
{

}

void signal_handler()
{
    struct sigaction new_action;
    new_action.sa_handler = abort_handler;
//    sigemptyset(&new_action.sa_mask);

//    sigaction(SIGABRT, &new_action, NULL);
    signal(SIGABRT, SIG_IGN);
}*/

FILE *log;
char command_buf[4096];
FILE *rc_file;
char command[4096];

char sentry_buf[4096];


int socket_INIT()
{
    int sockfd, n;
    struct sockaddr_in servaddr;
    int yes = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

void init()
{
    inotify_INIT();
    log_INIT();
}


void inotify_INIT()
{
    if ( !inotifytools_initialize()
         || !inotifytools_watch_recursively(WEB_PATH, IN_ALL_EVENTS ) ) {
        fprintf(stderr, "%s\n", strerror( inotifytools_error() ) );
        exit(EXIT_FAILURE);
    }
}


int remove_directory(const char *path)
{
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = -1;

    if (d)
    {
        struct dirent *p;

        r = 0;

        while (!r && (p=readdir(d)))
        {
            int r2 = -1;
            char *buf;
            size_t len;

            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
            {
                continue;
            }

            len = path_len + strlen(p->d_name) + 2;
            buf = malloc(len);

            if (buf)
            {
                struct stat statbuf;

                snprintf(buf, len, "%s/%s", path, p->d_name);

                if (!stat(buf, &statbuf))
                {
                    if (S_ISDIR(statbuf.st_mode))
                    {
                        r2 = remove_directory(buf);
                    }
                    else
                    {
                        r2 = unlink(buf);
                    }
                }

                free(buf);
            }

            r = r2;
        }

        closedir(d);
    }

    if (!r)
    {
        r = rmdir(path);
    }

    return r;
}

int dir_exists(char *path) {
    DIR *dir = opendir(path);
    int rc;
    if (dir)
    {
//        rc = remove_directory(path);
//        if (rc < 0) {
//            perror("rmdir");
//            exit(EXIT_FAILURE);
//        }
        return 1;
    }
    else if (errno != ENOENT)
    {
        perror("opendir");
        exit(EXIT_FAILURE);

    }

    return 0;

}

int handle_creat(struct inotify_event * event)
{
    char *pathname = NULL;
    char *eventname = NULL;
    char filename[4096];
    char git_filename[4096];
    char buf[1024];
    int rc;

    if (event->len > 0)
        eventname = event->name;


    pathname = inotifytools_filename_from_wd(event->wd);

    if (eventname != NULL)
        snprintf(filename, sizeof(filename), "%s%s", pathname, eventname);
    else
        snprintf(filename, sizeof(filename), "%s", pathname);

    snprintf(git_filename, sizeof(git_filename), "%s%s", GIT_PATH, filename + strlen(WEB_PATH));


    if (event->mask & IN_ISDIR)
    {
        rc = dir_exists(git_filename);
    } else {
        rc = access( git_filename, F_OK ) != -1? 1: 0;
    }

    if (rc == 0) {
        snprintf(command, sizeof command, "rm -rf %s 2>&1", filename);

        rc_file = popen(command, "r");
        rc = fread(command_buf, sizeof command_buf, 1, rc_file);
        rc = fclose(rc_file);

        snprintf(buf, sizeof buf, "%s was created", filename);
        send_sentry(buf, command_buf);
        if (rc != 0) {
            sys_error("git clone error", rc);
        }
    }
}

int handle_del(struct inotify_event * event) {
    char *pathname = NULL;
    char *eventname = NULL;
    char filename[4096];
    char git_filename[4096];
    char buf[1024];
    int rc;

    if (event->len > 0)
        eventname = event->name;

    pathname = inotifytools_filename_from_wd(event->wd);

    if (eventname != NULL)
        snprintf(filename, sizeof(filename), "%s%s", pathname, eventname);
    else
        snprintf(filename, sizeof(filename), "%s", pathname);

    snprintf(git_filename, sizeof(git_filename), "%s%s", GIT_PATH, filename + strlen(WEB_PATH));


    if (event->mask & IN_ISDIR)
    {
        rc = dir_exists(git_filename);
    } else {
        rc = access( git_filename, F_OK ) != -1? 1: 0;
    }

    if (rc == 1) {

        snprintf(command, sizeof command, "cp -rf %s %s 2>&1", git_filename, filename);

        rc_file = popen(command, "r");
        rc = fread(command_buf, sizeof command_buf, 1, rc_file);
        rc = fclose(rc_file);

        snprintf(buf, sizeof buf, "%s was deleted", filename);
        send_sentry(buf, command_buf);
        /*if (rc != 0) {
            sys_error("git clone error", rc);
        }*/
    }
}

int handle_diff(struct inotify_event * event)
{
    char * pathname = NULL;
    char * eventname = NULL;
    char filename[4096];
    char git_filename[4096];

    if (event->len > 0)
        eventname = event->name;
    else
        return 0;

    pathname = inotifytools_filename_from_wd( event->wd );

    snprintf(filename, sizeof(filename), "%s%s", pathname, eventname);

    snprintf(git_filename, sizeof(git_filename), "%s%s", GIT_PATH, filename+strlen(WEB_PATH));


    printf("diff filename: %s\n", filename);
    printf("git filename: %s\n", git_filename);


    snprintf(command, sizeof(command), "/home/zhang/CLionProjects/monitor/diff_command.sh %s %s", git_filename, filename);
    system(command);


}

int get_line(int sock,char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

void log_INIT()
{
    log = fopen(LOG_PATH, "a");
    if (log == NULL) {
        perror("fopen error");
        sys_error("fopen error", errno);
    }
}



int sys_error(char *message, int errnum)
{
    static time_t ticks;
    ticks = time(NULL);

    snprintf(sentry_buf, sizeof sentry_buf, "[%.24s] %s: %s: %s\n", ctime(&ticks), message ,strerror(errnum), command_buf);

//    snprintf(command, sizeof command, "%s/sentry_report.py %s", WORK_PATH, message);
//    rc_file = popen(command, "w");
//    fwrite(buf, sizeof buf, 1, rc_file);
//    fclose(rc_file);

    send_sentry(message, sentry_buf);


    fwrite(sentry_buf, strlen(sentry_buf), 1, log);
    exit(EXIT_FAILURE);
}

int send_sentry(char *message, char *content)
{
    snprintf(command, sizeof command, "%s/sentry_report.py %s", WORK_PATH, message);
    rc_file = popen(command, "w");
    fwrite(content, strlen(content), 1, rc_file);
    fclose(rc_file);
}

void git_fetch(char *tag)
{
    int rc;

    snprintf(command, sizeof command, "cd %s && git checkout master && git pull && git checkout %s 2>&1",
             GIT_PATH, tag);
    rc_file = popen(command, "r");
    rc = fread(command_buf, sizeof command_buf, 1, rc_file);
    rc = fclose(rc_file);

    if (rc != 0) {
        sys_error("git clone error", rc);
    }
}

int find_space(char *message, int size)
{
    int i;
    for (i = 0; i < size; i++)
    {
        if (isspace(*(message+i)))
        {
            return i;
        }
    }

    return i;
}

int main() {
    // initialize and watch the entire directory tree from the current working
    // directory downwards for all events

    int listenfd, clifd, maxfd;
    static fd_set read_fds;
    struct pollfd fds[2];
    char buf[4096];
    int rc;
    int n;
    nfds_t fdsize = 2;

    init();
    rc = dir_exists(GIT_PATH);
    if (rc == 1)
    {
        rc = remove_directory(GIT_PATH);
        if (rc < 0) {
            sys_error("git clone error", rc);
        }
    }

    snprintf(command, sizeof command, "git clone %s %s 2>&1", GIT_REMOTE, GIT_PATH);
    rc_file = popen(command, "r");
    rc = fread(command_buf, sizeof command_buf, 1, rc_file);
    rc = fclose(rc_file);

    if (rc != 0) {
        sys_error("git clone error", rc);
    }


    listenfd = socket_INIT();

    fds[0].fd = listenfd;
    fds[0].events = POLLRDNORM | POLLIN;

    fds[1].fd = inotify_fd;
    fds[1].events = POLLRDNORM | POLLIN;


    for (;;) {
        rc = poll(fds, fdsize, -1);

        if (rc < 0) {
            perror("select");
            exit(EXIT_FAILURE);
        } else if (rc == 0) {
            continue;
        }

        if (fds[1].revents & POLLIN)
        {
            // Output all events as "<timestamp> <path> <events>"
            struct inotify_event *event = inotifytools_next_event(-1);
            while (event) {
                inotifytools_printf(event, "%T %w%f %e\n");

                if (event->mask & IN_ACCESS
                    || event->mask & IN_OPEN
                    || event->mask & IN_CLOSE_NOWRITE
                    || event->mask & IN_CLOSE
                    || event->mask & IN_ATTRIB)
                {
                } else if (event->mask & IN_CREATE) {
                    handle_creat(event);
                } else if (event->mask & IN_DELETE || event->mask & IN_DELETE_SELF) {
                    handle_del(event);
                } else if (event->mask & IN_MODIFY) {
                    handle_diff(event);
                }
                event = inotifytools_next_event(-1);
            }
        } else if (fds[0].revents & POLLIN){
            clifd = accept(listenfd, NULL, NULL);
            n = get_line(clifd, buf, sizeof(buf));
            if (n < 0)
                continue;

            buf[n-1] = '\0';
            if (strcmp(buf, "GIT") != 0) {
                continue;
            }

            n = get_line(clifd, buf, sizeof(buf));
            if (n < 0)
                continue;
            buf[n-1] = '\0';


            if (strcmp(buf, "flush") == 0) {

                n = get_line(clifd, buf, sizeof(buf));
                buf[n-1] = '\0';

                rc = find_space(buf, n); // rc 为　index

                if (rc == n) {
                    memcpy(command_buf, buf, strlen(buf));
                    command_buf[strlen(buf)] = '\0';
                    sys_error("socket message format error", 234234);
                }

                buf[rc] = '\0';

                git_fetch(buf+rc+1);

                //暂停　ｍｏｎｉｔｏｒ　ｗｅｂl
                fdsize = 1;
            } else if (strcmp(buf, "monitor") == 0){
                fdsize = 2;
            }
            close(clifd);
        }
    }

    return 0;
}