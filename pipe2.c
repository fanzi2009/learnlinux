#include <sys/epoll.h>
#include <iostream>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

const int size = 10;

int create_reactor () {
    int epollfd = epoll_create (size);
    if (epollfd == -1) {
        std::cerr << strerror (errno) << "\n";
        return -1;
    }
    return epollfd;
}

int add_pipe_to_reactor (int epollfd, int fd) {
    epoll_event event;

    event.events = EPOLLIN;
    event.data.fd = fd;
    int r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    if (r == -1) {
        std::cerr << strerror (errno) << "\n";
        close (epollfd);
        close (fd);
    }
    return r;
}

// child_loop
void child_loop (int epollfd, int fd) {

    bool readyToWrite = true;
    while (true) {
        const int infinity = -1;
        // and wait for events
        epoll_event events[size];
        int r = epoll_wait (epollfd, events, size, infinity);
        if (r == -1) {
            close (epollfd);
            close (fd);
            exit (1);
        }
        // demultiplex events
        int i = 0;
        while (i < r) {
            if (events[i].data.fd == fd) {
                if (events[i].events & EPOLLOUT) {
                    if (readyToWrite) {
                        // (3) Notifies parent
                        char buffer = 0;
                        int i = write (fd, &buffer, 1);
                        if (i != -1) {
                            std::cout << "Signaled parent\n";
                        }
                        else {
                            std::cerr << "write: " << strerror (errno) << "\n";
                            close (epollfd);
                            close (fd);
                            exit (1);
                        }
                        // unregisters from reactor
                        readyToWrite = false;
                        epoll_event event;
                        event.events = EPOLLIN;
                        event.data.fd = fd;
                        int r = epoll_ctl (epollfd, EPOLL_CTL_MOD, fd, &event);
                        if (r == -1) {
                            std::cerr << "epoll: " << strerror (errno) << "\n";
                            close (epollfd);
                            close (fd);
                            exit (1);
                        }
                    }
                    else {
                        std::cerr << "Asked for write but nothing to write!\n";
                    }
                }
            }
            ++i;
        }
    }
}
// child_loop

// parent_loop
void parent_loop (int epollfd, int fd) {

    while (true) {
        const int infinity = -1;
        // and wait for events
        epoll_event events[size];
        int r = epoll_wait (epollfd, events, size, infinity);
        if (r == -1) {
            close (epollfd);
            close (fd);
            exit (1);
        }

        // demultiplex events
        int i = 0;
        while (i < r) {
            if (events[i].data.fd == fd) {
                // (4) Process pending notification
                const size_t s = 32;
                char buffer[s];
                int i = read (fd, buffer, s);
                if (i != -1) {
                    std::cout << "Received msg from child\n";
                }
            }
            ++i;
        }
    }
    close (epollfd);
    close (fd);
}
// parent_loop

int main() {

    // init
    // (1) Init
    int fd[2];
    int r = pipe2 (fd, O_NONBLOCK);
    if (r == -1) {
        std::cerr << strerror (errno) << "\n";
        return 1;
    }
    // init

    pid_t pid = fork ();
    if (pid == -1) {
        close (fd[0]);
        close (fd[1]);
        std::cerr << strerror (errno) << "\n";
        return 1;
    }

    // fchild
    if (pid == 0) { // child

        // (2) Close useless pipe end
        close (fd[0]);

        int epollfd = create_reactor ();
        if (epollfd == -1) {
            close (fd[1]);
            return 1;
        }
        if (add_pipe_to_reactor (epollfd, fd[1]) == -1) {
            return 1;
        }

        // child writes msg to parent to signal it
        epoll_event event;
        event.events = EPOLLIN | EPOLLOUT;
        event.data.fd = fd[1];
        int r = epoll_ctl (epollfd, EPOLL_CTL_MOD, fd[1], &event);
        if (r == -1) {
            std::cerr << strerror (errno) << "\n";
            close (epollfd);
            close (fd[1]);
            return 1;
        }

        child_loop (epollfd, fd[1]);

    }
    // fchild
    else { // fparent

        // (2) Close useless pipe end
        close (fd[1]);

        int epollfd = create_reactor ();
        if (epollfd == -1) {
            close (fd[0]);
            return 1;
        }
        if (add_pipe_to_reactor (epollfd, fd[0]) == -1) {
            return 1;
        }

        // watch possibly other fds
        // ...

        parent_loop (epollfd, fd[0]);
        // fparent
    }
    return 0;
}

