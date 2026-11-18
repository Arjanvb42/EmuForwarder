#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>

#ifdef TEGELMAKER_PS4
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

class UsbFtp {
public:
    UsbFtp() = default;
    UsbFtp(const UsbFtp &) = delete;
    UsbFtp &operator=(const UsbFtp &) = delete;

    ~UsbFtp()
    {
        close_socket(data);
        close_socket(control);
    }

    static std::string console_address()
    {
#ifdef TEGELMAKER_PS4
        static bool initialized;
        if (!initialized) {
            sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
            sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
            sceNetInit();
            sceNetCtlInit();
            initialized = true;
        }
        OrbisNetCtlInfo info{};
        if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == 0)
            return info.ip_address;
#endif
        return {};
    }

    bool login(const std::string &lan)
    {
        address = "127.0.0.1";
        control = socket_at(address.c_str(), 2121);
        if (control < 0 && !lan.empty()) {
            address = lan;
            control = socket_at(address.c_str(), 2121);
        }
        if (control < 0 || response() != 220)
            return false;
        const int user = command("USER anonymous");
        if (user != 230 && (user != 331 || command("PASS tegelmaker@localhost") != 230))
            return false;
        return command("TYPE I") == 200;
    }

    const std::string &host() const { return address; }

    bool start(const char *verb, const char *path, const std::string &lan)
    {
        if (data >= 0 || !usb_path(path) || command("PASV") != 227)
            return false;
        const char *tuple = strchr(reply, '(');
        unsigned a, b, c, d, hi, lo;
        if (!tuple || sscanf(tuple, "(%u,%u,%u,%u,%u,%u)", &a, &b, &c, &d, &hi, &lo) != 6 ||
            a > 255 || b > 255 || c > 255 || d > 255 || hi > 255 || lo > 255)
            return false;
        const unsigned port = hi * 256 + lo;
        if (!port)
            return false;
        char reported[16];
        snprintf(reported, sizeof(reported), "%u.%u.%u.%u", a, b, c, d);
        const std::string peer = lan == reported ? lan : address;
        data = socket_at(peer.c_str(), port);
        if (data < 0 && peer != address)
            data = socket_at(address.c_str(), port);
        if (data < 0)
            return false;
        if (const int result = command(std::string(verb) + " " + path); result == 125 || result == 150)
            return true;
        close_socket(data);
        data = -1;
        return false;
    }

    int reading(void *buffer, size_t size)
    {
        return receive(data, buffer, size);
    }

    bool finish()
    {
        close_socket(data);
        data = -1;
        const int result = response();
        return result == 226 || result == 250;
    }

    bool remove_usb_file(const char *path)
    {
        return usb_path(path) && command(std::string("DELE ") + path) == 250;
    }

private:
    int control = -1, data = -1;
    char reply[1024]{};
    char incoming[1024]{};
    size_t cursor = 0, buffered = 0;
    std::string address;

    static bool usb_path(const char *path)
    {
        return strncmp(path, "/mnt/usb", 8) == 0 && path[8] >= '0' && path[8] <= '7' &&
               (path[9] == '/' || path[9] == 0) && !strpbrk(path, "\r\n") &&
               !strstr(path, "/../") && strcmp(path + strlen(path) - 3, "/..");
    }

    static int socket_at(const char *host, unsigned port)
    {
        const timeval timeout{4, 0};
#ifdef TEGELMAKER_PS4
        const int fd = sceNetSocket("Tegelmaker USB", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        sockaddr_in target{};
        target.sin_len = sizeof(target);
        target.sin_family = ORBIS_NET_AF_INET;
        target.sin_port = sceNetHtons(static_cast<uint16_t>(port));
        sceNetSetsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sceNetSetsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        const bool ok = sceNetInetPton(ORBIS_NET_AF_INET, host, &target.sin_addr) == 1 &&
                        sceNetConnect(fd, reinterpret_cast<const OrbisNetSockaddr *>(&target),
                                      sizeof(target)) == 0;
#else
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(static_cast<uint16_t>(port));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        const bool ok = inet_pton(AF_INET, host, &target.sin_addr) == 1 &&
                        connect(fd, reinterpret_cast<const sockaddr *>(&target), sizeof(target)) == 0;
#endif
        if (ok)
            return fd;
        close_socket(fd);
        return -1;
    }

    static int receive(int socket, void *buffer, size_t size)
    {
#ifdef TEGELMAKER_PS4
        return sceNetRecv(socket, buffer, size, 0);
#else
        return recv(socket, buffer, size, 0);
#endif
    }

    static void close_socket(int socket)
    {
        if (socket < 0)
            return;
#ifdef TEGELMAKER_PS4
        sceNetSocketClose(socket);
#else
        close(socket);
#endif
    }

    int response()
    {
        size_t size = 0;
        while (size + 1 < sizeof(reply)) {
            if (cursor == buffered) {
                const int n = receive(control, incoming, sizeof(incoming));
                if (n <= 0)
                    return -1;
                cursor = 0;
                buffered = static_cast<size_t>(n);
            }
            const char value = incoming[cursor++];
            if (value == '\n') {
                reply[size] = 0;
                if (size >= 4 && reply[3] == ' ' &&
                    isdigit(static_cast<unsigned char>(reply[0])) &&
                    isdigit(static_cast<unsigned char>(reply[1])) &&
                    isdigit(static_cast<unsigned char>(reply[2])))
                    return (reply[0] - '0') * 100 + (reply[1] - '0') * 10 + reply[2] - '0';
                size = 0;
            } else if (value != '\r') {
                reply[size++] = value;
            }
        }
        return -1;
    }

    int command(const std::string &command_text)
    {
        const std::string line = command_text + "\r\n";
        size_t offset = 0;
        while (offset < line.size()) {
#ifdef TEGELMAKER_PS4
            const int sent = sceNetSend(control, line.data() + offset, line.size() - offset, 0);
#else
            const int sent = send(control, line.data() + offset, line.size() - offset, MSG_NOSIGNAL);
#endif
            if (sent <= 0)
                return -1;
            offset += sent;
        }
        return response();
    }
};
