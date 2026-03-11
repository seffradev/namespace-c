#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>

#define MAX_EVENTS 10

/*!
 * Concatenate `lhs` and `rhs` in the order of the parameters.
 * This allocates a new string that is owned by the caller.
 *
 * @param lhs The left-hand side of concatenation
 * @param rhs The right-hand side of concatenation
 * @returns The concatenated string on success, NULL on failure
 */
char*
concatenate(const char* lhs, const char* rhs) {
    size_t lhs_length = strlen(lhs);
    size_t rhs_length = strlen(rhs);

    char* result = (char*)malloc(lhs_length + rhs_length + 1);

    if (result == NULL) {
        fprintf(stderr, "Failed to allocate memory. Reason: %s\n", strerror(errno));
        return NULL;
    }

    strcpy(result, lhs);
    strcat(result, rhs);

    return result;
}

/*!
 *
 *
 * @param namespace_name
 * @returns
 */
char*
namespace_get_path(const char* namespace_name) {
    const char* base = "/var/run/netns/";
    char* namespace_path = concatenate(base, namespace_name);

    if (namespace_path == NULL) {
        fprintf(stderr, "Failed to get full namespace path. Reason: %s\n", strerror(errno));
        return NULL;
    }

    return namespace_path;
}

/*!
 *
 *
 * @param namespace_path
 * @returns
 */
int
namespace_open(const char* namespace_path) {
    int file_descriptor = open(namespace_path, O_RDONLY);
    if (file_descriptor == -1) {
        return -1;
    }

    return file_descriptor;
}

/*!
 * Move the current thread into the namespace referred to by
 * `namespace_file_descriptor`.
 *
 * @param namespace_file_descriptor
 * @returns
 */
bool
namespace_enter(int namespace_file_descriptor) {
    int result = setns(namespace_file_descriptor, CLONE_NEWNET);
    if (result == -1) {
        fprintf(stderr, "Failed to enter namespace. Reason: %s\n", strerror(errno));
        return false;
    }

    return true;
}

/*!
 *
 *
 * @param namespace_name
 * @returns
 */
bool
thread_move_to_namespace(const char* namespace_name) {
    char* namespace_path = namespace_get_path(namespace_name);
    if (namespace_path == NULL) {
        fprintf(stderr, "Failed to get namespace path\n");
        return false;
    }

    int new_namespace = namespace_open(namespace_path);
    if (new_namespace == -1) {
        fprintf(stderr, "Failed to open namespace at path %s\n", namespace_path);
        free(namespace_path);
        return false;
    }

    if (!namespace_enter(new_namespace)) {
        fprintf(stderr, "Failed to enter namespace at path %s\n", namespace_path);

        if (close(new_namespace) == -1) {
            fprintf(stderr, "Failed to close new namespace. Reason: %s\n", strerror(errno));
        }

        free(namespace_path);

        return false;
    }

    if (close(new_namespace) == -1) {
        fprintf(stderr, "Failed to close new namespace. Reason: %s\n", strerror(errno));
        free(namespace_path);
        return false;
    }

    free(namespace_path);
    return true;
}

/*!
 *
 *
 * @param destination
 * @param file_descriptor
 * @returns
 */
bool
socket_send_to(int destination, int file_descriptor) {
    char iobuffer[] = {1};
    struct iovec io = {.iov_base = &iobuffer, .iov_len = sizeof(iobuffer)};

    union {
        char buffer[CMSG_SPACE(sizeof(file_descriptor))];
        struct cmsghdr align;
    } data;

    struct msghdr message = {.msg_name = NULL,
                             .msg_namelen = 0,
                             .msg_iov = &io,
                             .msg_iovlen = 1,
                             .msg_control = data.buffer,
                             .msg_controllen = sizeof(data.buffer),
                             .msg_flags = 0};

    struct cmsghdr* control_message = CMSG_FIRSTHDR(&message);
    control_message->cmsg_level = SOL_SOCKET;
    control_message->cmsg_type = SCM_RIGHTS;
    control_message->cmsg_len = CMSG_LEN(sizeof(file_descriptor));
    memcpy(CMSG_DATA(control_message), &file_descriptor, sizeof(file_descriptor));

    if (sendmsg(destination, &message, 0) == -1) {
        fprintf(stderr, "Failed to send message. Reason: %s\n", strerror(errno));
        return false;
    }

    return true;
}

/*!
 *
 *
 * @param source
 * @returns
 */
int
socket_receive_from(int source) {
    char iobuffer[] = {1};
    struct iovec io = {.iov_base = &iobuffer, .iov_len = sizeof(iobuffer)};

    union {
        char buffer[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } data;

    struct msghdr message = {.msg_name = NULL,
                             .msg_namelen = 0,
                             .msg_iov = &io,
                             .msg_iovlen = 1,
                             .msg_control = data.buffer,
                             .msg_controllen = sizeof(data.buffer),
                             .msg_flags = 0};

    int result = recvmsg(source, &message, MSG_WAITALL);
    if (result == -1) {
        fprintf(stderr, "Failed to receive message. Reason: %s\n", strerror(errno));
        return -1;
    }

    if (result == 0) {
        fprintf(stderr, "Peer was unexpectedly closed before receiving a file descriptor\n");
        return -1;
    }

    struct cmsghdr* control_message = CMSG_FIRSTHDR(&message);
    if (control_message == NULL) {
        fprintf(stderr, "Received a message that lacks a control part\n");
        return -1;
    }

    if (control_message->cmsg_level != SOL_SOCKET) {
        fprintf(stderr, "Received a message of incorrect level\n");
        return -1;
    }

    if (control_message->cmsg_type != SCM_RIGHTS) {
        fprintf(stderr, "Received a message of incorrect type\n");
        return -1;
    }

    int received_file_descriptor = -1;
    memcpy(&received_file_descriptor, CMSG_DATA(control_message), sizeof(received_file_descriptor));

    return received_file_descriptor;
}

/*!
 *
 *
 * @param address
 * @returns
 */
int
socket_create_udp(struct sockaddr_in* address) {
    int file_descriptor = socket(AF_INET, SOCK_DGRAM, 0);
    if (file_descriptor == -1) {
        fprintf(stderr, "Failed to create socket. Reason: %s\n", strerror(errno));
        return -1;
    }

    if (bind(file_descriptor, (struct sockaddr*)address, sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Failed to bind address. Reason: %s\n", strerror(errno));
        close(file_descriptor);
        return -1;
    }

    return file_descriptor;
}

/*!
 *
 *
 * @param namespace_name
 * @param parent
 * @param address
 * @returns
 */
void
socket_create_udp_in_namespaced_fork(const char* namespace_name,
                                     int parent,
                                     struct sockaddr_in* address) {
    if (!thread_move_to_namespace(namespace_name)) {
        fprintf(stderr, "Failed to move fork into namespace %s\n", namespace_name);
        _exit(1);
    }

    int file_descriptor = socket_create_udp(address);
    if (file_descriptor == -1) {
        fprintf(stderr, "Failed to create socket\n");
        _exit(1);
    }

    if (!socket_send_to(parent, file_descriptor)) {
        fprintf(stderr, "Failed to send socket to parent\n");
        _exit(1);
    }
}

/*!
 * Create a UDP socket in a namespace by spawning a child process
 * and letting the child enter a namespace and create the socket.
 * The socket is then received via a control message sent over a
 * Unix Domain Socket.
 *
 * @param namespace_name The namespace to create the socket in.
 * @param address The address to bind the socket to.
 * @returns The created socket on success, and -1 on failure.
 */
int
socket_create_udp_in_namespace(const char* namespace_name, struct sockaddr_in* address) {
    int sockets[2];
    int result = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);

    if (result == -1) {
        fprintf(stderr, "Failed to create a pair of sockets. Reason: %s\n", strerror(errno));
        return -1;
    }

    int child = sockets[0];
    int parent = sockets[1];

    int pid = fork();
    switch (pid) {
    case -1:
        fprintf(stderr, "Failed to create fork. Reason: %s\n", strerror(errno));
        return -1;
    case 0:
        close(child);
        socket_create_udp_in_namespaced_fork(namespace_name, parent, address);
        close(parent);
        _exit(0);
    default:
        close(parent);
        int file_descriptor = socket_receive_from(child);
        close(child);

        int status = 0;
        waitpid(pid, &status, 0);

        int code = WEXITSTATUS(status);
        if (code != 0) {
            fprintf(stderr, "Child process failed when trying to create namespaced socket\n");
            return -1;
        }

        return file_descriptor;
    }
}

/*!
 * Send a message over UDP to `address`.
 *
 * @param udp_file_descriptor The socket to send via.
 * @param address The address of the peer.
 * @param message A pointer to the message to send.
 * @param size The length of the message.
 */
void
udp_handle_outgoing(int udp_file_descriptor,
                    struct sockaddr_in* address,
                    char* message,
                    size_t size) {
    ssize_t sent_length = sendto(udp_file_descriptor,
                                 message,
                                 size,
                                 0,
                                 (struct sockaddr*)address,
                                 sizeof(struct sockaddr_in));

    if (sent_length == -1) {
        fprintf(stderr, "Failed to send message. Reason: %s\n", strerror(errno));
        return;
    }

    printf("Sent %ld bytes of data\n", sent_length);
}

/*!
 * Receive a message from the provided file descriptor.
 *
 * @param udp_file_descriptor An open UDP file descriptor that is ready to be read.
 */
void
udp_handle_incoming(int udp_file_descriptor, struct sockaddr_in* address) {
    char message[0xFFFF];

    struct sockaddr_in originating_address;
    socklen_t originating_address_length = sizeof(originating_address);

    ssize_t received_length = recvfrom(udp_file_descriptor,
                                       message,
                                       sizeof(message),
                                       0,
                                       (struct sockaddr*)&originating_address,
                                       &originating_address_length);

    if (received_length == -1) {
        fprintf(stderr, "Failed receiving data over UDP. Reason: %s\n", strerror(errno));
        return;
    }

    printf("Received %ld bytes of data\n", received_length);

    udp_handle_outgoing(udp_file_descriptor, address, message, received_length);
}

/*!
 * Wait for events on the given `epoll` file descriptor and branch
 * these out to be either receivers or senders.
 *
 * @param epoll_file_descriptor The target `epoll` file descriptor to wait on.
 * @param udp The UDP socket to send and receive on.
 * @param tun The TUN socket to write and read from.
 * @param events The container of possible events.
 * @param address The target address to send packets to.
 * @returns If the function fails, it returns `false` to denote an exit and `true` to denote that it may remain active.
 */
bool
event_wait_for(int epoll_file_descriptor,
               int udp,
               struct epoll_event events[MAX_EVENTS],
               struct sockaddr_in* address) {
    int number_of_file_descriptors = epoll_wait(epoll_file_descriptor, events, MAX_EVENTS, -1);
    if (number_of_file_descriptors == -1) {
        fprintf(stderr, "Failed to wait for events. Reason: %s\n", strerror(errno));
        return false;
    }

    for (int i = 0; i < number_of_file_descriptors; ++i) {
        if ((events[i].events & EPOLLIN) != 0 && events[i].data.fd == udp) {
            udp_handle_incoming(udp, address);
        }
    }

    return true;
}

/*!
 * Constructs a `struct sockaddr_in*` targeting peer `ip:port`.
 * The caller owns the result of the invocation.
 *
 * @param ip The peer IP.
 * @param port The peer port.
 * @returns The address to the peer on success, NULL on failure.
 */
struct sockaddr_in*
address_get_from(const char* ip, unsigned int port) {
    struct in_addr ip_address;

    int result = inet_aton(ip, &ip_address);

    if (result == 0) {
        fprintf(stderr, "Failed converting IP address %s to binary form", ip);
        return NULL;
    }

    struct sockaddr_in* address = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));

    if (address == NULL) {
        fprintf(stderr,
                "Failed to allocate memory for internet address. Reason: %s\n",
                strerror(errno));

        return NULL;
    }

    address->sin_family = AF_INET;
    address->sin_addr = ip_address;
    address->sin_port = htons(port);

    // TODO: Explore if setting sin_zero may be the cause of
    // failing with `sendto` and `recvfrom`.
    // [Source](https://silviocesare.wordpress.com/2007/10/22/setting-sin_zero-to-0-in-struct-sockaddr_in/)
    memset(&address->sin_zero, 0, sizeof(address->sin_zero));

    return address;
}

int
tun_open(const char* name) {
    int file_descriptor = open("/dev/net/tun", O_RDWR);

    if (file_descriptor == -1) {
        fprintf(stderr, "Failed to open TUN device. Reason %s\n", strerror(errno));
        return -1;
    }

    struct ifreq interface_request;
    memset(&interface_request, 0, sizeof(interface_request));
    strncpy(interface_request.ifr_name, name, IFNAMSIZ);
    interface_request.ifr_flags = IFF_TUN | IFF_NO_PI;

    int result = ioctl(file_descriptor, TUNSETIFF, &interface_request);
    if (result < 0) {
        fprintf(stderr,
                "Failed to modify TUN interface with name %s. Reason: %s\n",
                name,
                strerror(errno));

        close(file_descriptor);
        return -1;
    }

    return file_descriptor;
}

volatile bool running = true;

/*!
 * A signal handler, for shutting of the application.
 */
void
interrupt(int) {
    running = false;
}

int
main(int argc, char* argv[]) {
    struct sigaction action;
    memset(&action, '\0', sizeof(action));
    action.sa_handler = &interrupt;
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1) {
        fprintf(stderr, "Failed to register signal action. Exiting");
        return 1;
    }

    if (argc != 7) {
        printf("Usage: namespace <namespace name> <bind address> <bind port> <target "
               "address> <target port>\n");
        return 1;
    }

    const char* namespace_name = argv[1];
    const char* bind_ip_address = argv[2];
    unsigned int bind_port = atoi(argv[3]);
    const char* target_ip_address = argv[4];
    unsigned int target_port = atoi(argv[5]);

    if (bind_port > 65535) {
        fprintf(stderr, "Bind port must be in the range [0, 65535]\n");
        return 1;
    }

    if (target_port > 65535) {
        fprintf(stderr, "Target port must be in the range [0, 65535]\n");
        return 1;
    }

    struct sockaddr_in* bind_address = address_get_from(bind_ip_address, bind_port);
    if (bind_address == NULL) {
        fprintf(stderr, "Failed to get internet bind address\n");
        return 1;
    }

    struct sockaddr_in* target_address = address_get_from(target_ip_address, target_port);
    if (target_address == NULL) {
        fprintf(stderr, "Failed to get internet target address\n");
        free(bind_address);
        return 1;
    }

    int udp = socket_create_udp_in_namespace(namespace_name, bind_address);
    free(bind_address);

    if (udp == -1) {
        fprintf(stderr, "Failed to create UDP socket in namespace %s\n", namespace_name);

        free(target_address);
        return -1;
    }

    int epoll_file_descriptor = epoll_create1(0);
    if (epoll_file_descriptor == -1) {
        fprintf(stderr, "Could not set up epoll. Reason: %s\n", strerror(errno));
        close(udp);

        free(target_address);
        return 1;
    }

    struct epoll_event registered_event;
    struct epoll_event events[MAX_EVENTS];

    registered_event.events = EPOLLIN;
    registered_event.data.fd = udp;
    int result = epoll_ctl(epoll_file_descriptor, EPOLL_CTL_ADD, udp, &registered_event);

    if (result == -1) {
        fprintf(stderr, "Failed to add UDP socket to epoll. Reason: %s\n", strerror(errno));
        close(epoll_file_descriptor);
        free(target_address);
        return 1;
    }

    while (running) {
        if (!event_wait_for(epoll_file_descriptor, udp, events, target_address)) {
            break;
        }
    }

    free(target_address);
}
