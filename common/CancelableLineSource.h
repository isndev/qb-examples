#pragma once

// Console input for examples whose actor cores must never block in getline().
// One LineSource owns stdin exclusively. Lines are copied into a bounded queue;
// EOF is a separate, once-only event. stop() discards pending input, wakes the
// reader and joins it. Actors only call try_pop(), never capture a reader thread.

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <charconv>
#include <limits>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace qb_example::input {

inline constexpr std::size_t max_line_bytes = 64 * 1024;

struct Event {
    enum class Kind { line, eof, error };

    Kind        kind = Kind::error;
    std::string text;
};

#ifdef _WIN32
namespace detail {

inline constexpr char          helper_argument[] = "--qb-example-stdin-relay=";
inline constexpr std::uint32_t eof_frame         = (std::numeric_limits<std::uint32_t>::max)();
inline constexpr std::uint32_t error_frame       = eof_frame - 1;

inline bool
write_all(HANDLE handle, void const *data, std::size_t size) noexcept {
    auto const *bytes = static_cast<unsigned char const *>(data);
    while (size != 0) {
        DWORD      written = 0;
        auto const chunk   = static_cast<DWORD>(
            size < static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()) ? size : (std::numeric_limits<DWORD>::max)());
        if (!WriteFile(handle, bytes, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        bytes += written;
        size -= written;
    }
    return true;
}

inline bool
write_frame(HANDLE handle, std::uint32_t size, char const *data = nullptr) noexcept {
    std::array<unsigned char, 4> header{
        static_cast<unsigned char>(size), static_cast<unsigned char>(size >> 8), static_cast<unsigned char>(size >> 16),
        static_cast<unsigned char>(size >> 24)
    };
    return write_all(handle, header.data(), header.size()) && (data == nullptr || write_all(handle, data, size));
}

inline int
run_helper(HANDLE pipe_write) {
    std::string line;
    line.reserve(256);
    while (true) {
        auto const next = std::cin.get();
        if (next == std::char_traits<char>::eof()) {
            if (std::cin.bad()) {
                return write_frame(pipe_write, error_frame) ? 1 : 2;
            }
            if (!line.empty() && !write_frame(pipe_write, static_cast<std::uint32_t>(line.size()), line.data())) {
                return 2;
            }
            return write_frame(pipe_write, eof_frame) ? 0 : 2;
        }
        if (next == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!write_frame(pipe_write, static_cast<std::uint32_t>(line.size()), line.data())) {
                return 2;
            }
            line.clear();
            continue;
        }
        if (line.size() == max_line_bytes) {
            return write_frame(pipe_write, error_frame) ? 1 : 2;
        }
        line.push_back(static_cast<char>(next));
    }
}

inline std::wstring
executable_path() {
    std::vector<wchar_t> buffer(260);
    while (true) {
        DWORD const length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error("GetModuleFileNameW failed");
        }
        if (length < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }
        if (buffer.size() >= 32768) {
            throw std::runtime_error("executable path is too long");
        }
        buffer.resize(buffer.size() * 2);
    }
}

inline void
close_handle(HANDLE &handle) noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

inline std::runtime_error
win32_error(char const *operation) {
    return std::runtime_error(std::string(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ")");
}

inline void
cancel_and_resolve(HANDLE pipe, OVERLAPPED &request) noexcept {
    (void) CancelIoEx(pipe, &request);
    DWORD transferred = 0;
    if (!GetOverlappedResultEx(pipe, &request, &transferred, 2000, FALSE)) {
        auto const error = GetLastError();
        if (error == WAIT_TIMEOUT || error == ERROR_IO_INCOMPLETE) {
            // Returning would free an OVERLAPPED still owned by the kernel.
            std::terminate();
        }
    }
}

struct PipePair {
    HANDLE server = nullptr;
    HANDLE writer = nullptr;
};

// The endpoint name is random and local; only this process's user SID gets
// access. The child inherits the already-open write handle, never the name.
inline PipePair
create_private_pipe() {
    std::array<UCHAR, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("BCryptGenRandom stdin pipe name failed");
    }
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring      name     = L"\\\\.\\pipe\\qb-example-stdin-" + std::to_wstring(GetCurrentProcessId()) + L"-";
    for (auto const byte : random) {
        name.push_back(digits[byte >> 4]);
        name.push_back(digits[byte & 0xf]);
    }

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        throw win32_error("OpenProcessToken stdin pipe");
    }
    PipePair pair{};
    try {
        DWORD token_bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &token_bytes);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || token_bytes == 0) {
            throw win32_error("GetTokenInformation stdin pipe sizing");
        }
        std::vector<std::max_align_t> token_storage((token_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        if (!GetTokenInformation(token, TokenUser, token_storage.data(), token_bytes, &token_bytes)) {
            throw win32_error("GetTokenInformation stdin pipe");
        }
        auto const                   *user      = reinterpret_cast<TOKEN_USER const *>(token_storage.data());
        DWORD const                   acl_bytes = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(user->User.Sid);
        std::vector<std::max_align_t> acl_storage((acl_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        auto                         *acl = reinterpret_cast<ACL *>(acl_storage.data());
        if (!InitializeAcl(acl, acl_bytes, ACL_REVISION) || !AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, user->User.Sid)) {
            throw win32_error("stdin pipe user ACL");
        }
        SECURITY_DESCRIPTOR descriptor{};
        if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION)
            || !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE)) {
            throw win32_error("stdin pipe security descriptor");
        }
        SECURITY_ATTRIBUTES secure{sizeof(secure), &descriptor, FALSE};
        pair.server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0,
                                       static_cast<DWORD>(max_line_bytes + 8), 0, &secure);
        if (pair.server == INVALID_HANDLE_VALUE) {
            pair.server = nullptr;
            throw win32_error("CreateNamedPipeW stdin relay");
        }

        HANDLE connect_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (connect_event == nullptr) {
            throw win32_error("stdin pipe connect event");
        }
        OVERLAPPED connect{};
        connect.hEvent = connect_event;
        bool pending   = false;
        if (ConnectNamedPipe(pair.server, &connect)) {
            close_handle(connect_event);
            throw std::runtime_error("stdin pipe connected before private writer");
        } else {
            auto const error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                pending = true;
            } else {
                close_handle(connect_event);
                throw std::runtime_error("stdin pipe connected before private writer (Win32 " + std::to_string(error) + ")");
            }
        }
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        pair.writer = CreateFileW(name.c_str(), GENERIC_WRITE, 0, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pair.writer == INVALID_HANDLE_VALUE) {
            pair.writer      = nullptr;
            auto const error = GetLastError();
            if (pending) {
                cancel_and_resolve(pair.server, connect);
            }
            close_handle(connect_event);
            throw std::runtime_error("CreateFileW stdin relay writer failed (Win32 " + std::to_string(error) + ")");
        }
        if (pending) {
            auto const waited = WaitForSingleObject(connect_event, 2000);
            if (waited != WAIT_OBJECT_0) {
                cancel_and_resolve(pair.server, connect);
                close_handle(connect_event);
                throw std::runtime_error("stdin pipe connection timed out");
            }
            DWORD ignored = 0;
            if (!GetOverlappedResult(pair.server, &connect, &ignored, FALSE)) {
                auto const error = GetLastError();
                close_handle(connect_event);
                throw std::runtime_error("stdin pipe connection failed (Win32 " + std::to_string(error) + ")");
            }
        }
        close_handle(connect_event);
        close_handle(token);
        return pair;
    } catch (...) {
        close_handle(pair.writer);
        close_handle(pair.server);
        close_handle(token);
        throw;
    }
}

} // namespace detail
#endif

// Call before normal argument validation in every executable using LineSource.
// On Windows the parent relaunches this executable with a private pipe handle;
// only that child is allowed to block on console or redirected stdin. The return
// value is present only inside the helper process and must become main's exit code.
inline std::optional<int>
maybe_run_helper(int argc, char *argv[]) {
#ifdef _WIN32
    if (argc != 2 || argv == nullptr || argv[1] == nullptr) {
        return std::nullopt;
    }
    std::string_view const argument(argv[1]);
    std::string_view const prefix(detail::helper_argument);
    if (!argument.starts_with(prefix)) {
        return std::nullopt;
    }
    auto const     hex    = argument.substr(prefix.size());
    std::uintptr_t value  = 0;
    auto const     parsed = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
    if (hex.empty() || parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size() || value == 0) {
        return 2;
    }
    return detail::run_helper(reinterpret_cast<HANDLE>(value));
#else
    (void) argc;
    (void) argv;
    return std::nullopt;
#endif
}

class LineSource final {
public:
    explicit LineSource(std::size_t max_pending_lines = 64)
        : _capacity(max_pending_lines) {
        if (_capacity == 0) {
            throw std::invalid_argument("LineSource queue capacity must be positive");
        }
#ifdef _WIN32
        start_windows_reader();
#else
        start_posix_reader();
#endif
    }

    LineSource(LineSource const &)            = delete;
    LineSource &operator=(LineSource const &) = delete;

    ~LineSource() {
        stop();
    }

    // Returns immediately. A final fragment without '\n' is delivered as a line
    // before EOF; an empty file delivers EOF directly. EOF/error is emitted once.
    bool
    try_pop(Event &event) {
        std::lock_guard lock(_mutex);
        return pop_locked(event);
    }

    // For non-actor main loops. Actor callbacks must use try_pop().
    bool
    wait_pop(Event &event, std::chrono::milliseconds timeout) {
        std::unique_lock lock(_mutex);
        if (!_condition.wait_for(lock, timeout,
                                 [&] { return _stopped || !_lines.empty() || (_terminal != Terminal::none && !_terminal_delivered); })) {
            return false;
        }
        return pop_locked(event);
    }

    void
    stop() noexcept {
        // Serialize concurrent stop calls: every caller returns after the join.
        std::lock_guard stop_lock(_stop_mutex);
        {
            std::lock_guard lock(_mutex);
            if (_stopped) {
                return;
            }
            _stopped = true;
            _lines.clear();
            _terminal = Terminal::none;
        }
        _condition.notify_all();
#ifdef _WIN32
        // The reader's overlapped wait observes this before relying on child
        // termination. Job closure also kills the child if ordinary termination
        // is delayed; no other process owns the job or pipe writer handles.
        if (_stop_event != nullptr) {
            SetEvent(_stop_event);
        }
        if (_child_process != nullptr) {
            if (WaitForSingleObject(_child_process, 0) == WAIT_TIMEOUT) {
                TerminateProcess(_child_process, 2);
            }
        }
        detail::close_handle(_job);
        if (_child_process != nullptr && WaitForSingleObject(_child_process, 2000) != WAIT_OBJECT_0) {
            // A failed kernel reap cannot be reconciled with the no-orphan
            // contract. Fail closed instead of returning with a live helper.
            std::terminate();
        }
#else
        if (_wake_write >= 0) {
            char const signal = 1;
            (void) ::write(_wake_write, &signal, 1);
        }
#endif
        if (_reader.joinable()) {
            _reader.join();
        }
#ifdef _WIN32
        detail::close_handle(_read_event);
        detail::close_handle(_stop_event);
        detail::close_handle(_pipe_read);
        detail::close_handle(_child_process);
#else
        if (_wake_read >= 0) {
            ::close(_wake_read);
            _wake_read = -1;
        }
        if (_wake_write >= 0) {
            ::close(_wake_write);
            _wake_write = -1;
        }
#endif
    }

private:
    enum class Terminal { none, eof, error };

    bool
    pop_locked(Event &event) {
        if (_stopped) {
            return false;
        }
        if (!_lines.empty()) {
            event.kind = Event::Kind::line;
            event.text = std::move(_lines.front());
            _lines.pop_front();
            _condition.notify_all();
            return true;
        }
        if (_terminal == Terminal::none || _terminal_delivered) {
            return false;
        }
        event.kind = _terminal == Terminal::eof ? Event::Kind::eof : Event::Kind::error;
        event.text.clear();
        _terminal_delivered = true;
        return true;
    }

    bool
    publish_line(std::string &line) {
        std::unique_lock lock(_mutex);
        _condition.wait(lock, [&] { return _stopped || _lines.size() < _capacity; });
        if (_stopped) {
            return false;
        }
        _lines.emplace_back(std::move(line));
        _condition.notify_all();
        return true;
    }

    void
    publish_terminal(Terminal terminal) noexcept {
        {
            std::lock_guard lock(_mutex);
            if (_stopped || _terminal != Terminal::none) {
                return;
            }
            _terminal = terminal;
        }
        _condition.notify_all();
    }

#ifdef _WIN32
    bool
    read_exact(HANDLE handle, void *data, std::size_t size) noexcept {
        auto *bytes = static_cast<unsigned char *>(data);
        while (size != 0) {
            {
                std::lock_guard lock(_mutex);
                if (_stopped) {
                    return false;
                }
            }
            DWORD      count = 0;
            auto const chunk = static_cast<DWORD>(
                size < static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()) ? size : (std::numeric_limits<DWORD>::max)());
            OVERLAPPED request{};
            request.hEvent = _read_event;
            ResetEvent(_read_event);
            if (!ReadFile(handle, bytes, chunk, nullptr, &request)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    return false;
                }
                HANDLE const waits[2]{_read_event, _stop_event};
                auto const   result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (result != WAIT_OBJECT_0) {
                    detail::cancel_and_resolve(handle, request);
                    return false;
                }
            }
            if (!GetOverlappedResult(handle, &request, &count, FALSE)) {
                return false;
            }
            if (count == 0) {
                return false;
            }
            bytes += count;
            size -= count;
        }
        return true;
    }

    void
    read_windows_frames() noexcept {
        try {
            while (true) {
                std::array<unsigned char, 4> header{};
                if (!read_exact(_pipe_read, header.data(), header.size())) {
                    publish_terminal(Terminal::error);
                    return;
                }
                std::uint32_t const size = static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8)
                                           | (static_cast<std::uint32_t>(header[2]) << 16) | (static_cast<std::uint32_t>(header[3]) << 24);
                if (size == detail::eof_frame) {
                    publish_terminal(Terminal::eof);
                    return;
                }
                if (size == detail::error_frame || size > max_line_bytes) {
                    publish_terminal(Terminal::error);
                    return;
                }
                std::string line(size, '\0');
                if (!read_exact(_pipe_read, line.data(), size)) {
                    publish_terminal(Terminal::error);
                    return;
                }
                if (!publish_line(line)) {
                    return;
                }
            }
        } catch (...) {
            publish_terminal(Terminal::error);
        }
    }

    void
    start_windows_reader() {
        // A process started without stdin has no interactive control. Report
        // EOF; its owner decides whether a network service stays running.
        auto const stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
        if (stdin_handle == nullptr || stdin_handle == INVALID_HANDLE_VALUE) {
            publish_terminal(Terminal::eof);
            return;
        }

        SECURITY_ATTRIBUTES          inherit{sizeof(inherit), nullptr, TRUE};
        HANDLE                       pipe_write        = nullptr;
        HANDLE                       child_stdin       = nullptr;
        HANDLE                       null_output       = nullptr;
        HANDLE                       attribute_storage = nullptr;
        LPPROC_THREAD_ATTRIBUTE_LIST attributes        = nullptr;
        PROCESS_INFORMATION          process{};
        try {
            auto const pipe = detail::create_private_pipe();
            _pipe_read      = pipe.server;
            pipe_write      = pipe.writer;
            _stop_event     = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            _read_event     = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (_stop_event == nullptr || _read_event == nullptr) {
                throw detail::win32_error("stdin relay events");
            }
            _job = CreateJobObjectW(nullptr, nullptr);
            if (_job == nullptr) {
                throw detail::win32_error("stdin relay job");
            }
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                throw detail::win32_error("stdin relay kill-on-close job");
            }
            if (!DuplicateHandle(GetCurrentProcess(), stdin_handle, GetCurrentProcess(), &child_stdin, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
                throw detail::win32_error("stdin handle inheritance");
            }
            null_output =
                CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (null_output == INVALID_HANDLE_VALUE) {
                throw detail::win32_error("stdin relay NUL output");
            }

            SIZE_T bytes = 0;
            if (InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
                throw detail::win32_error("InitializeProcThreadAttributeList sizing");
            }
            attribute_storage = HeapAlloc(GetProcessHeap(), 0, bytes);
            if (attribute_storage == nullptr) {
                throw std::bad_alloc();
            }
            attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage);
            if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
                attributes = nullptr;
                throw detail::win32_error("InitializeProcThreadAttributeList");
            }
            std::array<HANDLE, 3> handles{child_stdin, pipe_write, null_output};
            if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(), sizeof(handles), nullptr,
                                           nullptr)) {
                throw detail::win32_error("stdin relay handle allowlist");
            }

            auto const        path         = detail::executable_path();
            auto const        handle_value = reinterpret_cast<std::uintptr_t>(pipe_write);
            wchar_t           hex[sizeof(void *) * 2 + 1]{};
            constexpr wchar_t digits[] = L"0123456789abcdef";
            auto              value    = handle_value;
            std::size_t       length   = 0;
            do {
                hex[length++] = digits[value & 0xf];
                value >>= 4;
            } while (value != 0);
            std::wstring handle_hex;
            handle_hex.reserve(length);
            while (length != 0) {
                handle_hex.push_back(hex[--length]);
            }
            std::wstring   command = L"\"" + path + L"\" --qb-example-stdin-relay=" + handle_hex;
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb         = sizeof(startup);
            startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput  = child_stdin;
            startup.StartupInfo.hStdOutput = null_output;
            startup.StartupInfo.hStdError  = null_output;
            startup.lpAttributeList        = attributes;
            if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr,
                                nullptr, &startup.StartupInfo, &process)) {
                throw detail::win32_error("CreateProcessW stdin helper");
            }
            _child_process   = process.hProcess;
            process.hProcess = nullptr;
            if (!AssignProcessToJobObject(_job, _child_process)) {
                throw detail::win32_error("AssignProcessToJobObject stdin helper");
            }
            if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
                throw detail::win32_error("ResumeThread stdin helper");
            }
            detail::close_handle(process.hThread);
            detail::close_handle(pipe_write);
            detail::close_handle(child_stdin);
            detail::close_handle(null_output);
            DeleteProcThreadAttributeList(attributes);
            attributes = nullptr;
            HeapFree(GetProcessHeap(), 0, attribute_storage);
            attribute_storage = nullptr;
            _reader           = std::thread([this] { read_windows_frames(); });
        } catch (...) {
            if (attributes != nullptr) {
                DeleteProcThreadAttributeList(attributes);
            }
            if (attribute_storage != nullptr) {
                HeapFree(GetProcessHeap(), 0, attribute_storage);
            }
            detail::close_handle(process.hThread);
            detail::close_handle(process.hProcess);
            detail::close_handle(pipe_write);
            detail::close_handle(child_stdin);
            detail::close_handle(null_output);
            if (_child_process != nullptr) {
                TerminateProcess(_child_process, 2);
            }
            detail::close_handle(_job);
            if (_child_process != nullptr && WaitForSingleObject(_child_process, 2000) != WAIT_OBJECT_0) {
                std::terminate();
            }
            detail::close_handle(_child_process);
            detail::close_handle(_read_event);
            detail::close_handle(_stop_event);
            detail::close_handle(_pipe_read);
            throw;
        }
    }
#else
    static void
    set_fd_flags(int fd, bool nonblocking) {
        auto const flags = ::fcntl(fd, F_GETFD);
        if (flags < 0 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
            throw std::runtime_error("fcntl FD_CLOEXEC failed");
        }
        if (nonblocking) {
            auto const status = ::fcntl(fd, F_GETFL);
            if (status < 0 || ::fcntl(fd, F_SETFL, status | O_NONBLOCK) < 0) {
                throw std::runtime_error("fcntl O_NONBLOCK failed");
            }
        }
    }

    void
    read_posix_lines() noexcept {
        try {
            std::string line;
            line.reserve(256);
            std::array<char, 4096> buffer{};
            while (true) {
                pollfd    fds[2]{{STDIN_FILENO, POLLIN, 0}, {_wake_read, POLLIN, 0}};
                int const ready = ::poll(fds, 2, -1);
                if (ready < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    publish_terminal(Terminal::error);
                    return;
                }
                if (fds[1].revents != 0) {
                    return;
                }
                if ((fds[0].revents & POLLNVAL) != 0) {
                    publish_terminal(Terminal::error);
                    return;
                }
                if ((fds[0].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                    continue;
                }
                auto const count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
                if (count < 0) {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    publish_terminal(Terminal::error);
                    return;
                }
                if (count == 0) {
                    if (!line.empty() && !publish_line(line)) {
                        return;
                    }
                    publish_terminal(Terminal::eof);
                    return;
                }
                for (std::ptrdiff_t index = 0; index < count; ++index) {
                    char const ch = buffer[static_cast<std::size_t>(index)];
                    if (ch == '\n') {
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }
                        if (!publish_line(line)) {
                            return;
                        }
                        line.clear();
                    } else {
                        if (line.size() == max_line_bytes) {
                            publish_terminal(Terminal::error);
                            return;
                        }
                        line.push_back(ch);
                    }
                }
            }
        } catch (...) {
            publish_terminal(Terminal::error);
        }
    }

    void
    start_posix_reader() {
        int wake[2]{-1, -1};
        if (::pipe(wake) < 0) {
            throw std::runtime_error("stdin wake pipe failed");
        }
        try {
            set_fd_flags(wake[0], false);
            set_fd_flags(wake[1], true);
            _wake_read  = wake[0];
            _wake_write = wake[1];
            _reader     = std::thread([this] { read_posix_lines(); });
        } catch (...) {
            ::close(wake[0]);
            ::close(wake[1]);
            _wake_read  = -1;
            _wake_write = -1;
            throw;
        }
    }
#endif

    std::size_t             _capacity;
    std::mutex              _mutex;
    std::mutex              _stop_mutex;
    std::condition_variable _condition;
    std::deque<std::string> _lines;
    Terminal                _terminal           = Terminal::none;
    bool                    _terminal_delivered = false;
    bool                    _stopped            = false;
    std::thread             _reader;
#ifdef _WIN32
    HANDLE _pipe_read     = nullptr;
    HANDLE _read_event    = nullptr;
    HANDLE _stop_event    = nullptr;
    HANDLE _job           = nullptr;
    HANDLE _child_process = nullptr;
#else
    int _wake_read  = -1;
    int _wake_write = -1;
#endif
};

} // namespace qb_example::input
