#include "05-services/01-tcp-chat/shared/Protocol.h"
#include "05-services/02-pubsub-broker/shared/Protocol.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace protocol_framing_test {

class Buffer {
    std::vector<char> _bytes;

public:
    [[nodiscard]] std::size_t
    size() const noexcept {
        return _bytes.size();
    }

    [[nodiscard]] const char *
    cbegin() const noexcept {
        return _bytes.data();
    }

    [[nodiscard]] const std::vector<char> &
    bytes() const noexcept {
        return _bytes;
    }

    void
    append(const void *data, std::size_t size) {
        const auto *first = static_cast<const char *>(data);
        _bytes.insert(_bytes.end(), first, first + size);
    }

    void
    consume(std::size_t size) {
        _bytes.erase(_bytes.begin(), _bytes.begin() + static_cast<std::ptrdiff_t>(size));
    }
};

class FakeIO {
public:
    using base_io_t = FakeIO;

    struct Received {
        std::uint8_t type;
        std::string  payload;

        bool operator==(const Received &) const = default;
    };

private:
    Buffer                _input;
    std::size_t           _max_message_size = std::numeric_limits<std::size_t>::max();
    std::vector<Received> _received;

public:
    Buffer &
    in() noexcept {
        return _input;
    }

    [[nodiscard]] std::size_t
    max_message_size() const noexcept {
        return _max_message_size;
    }

    void
    set_max_message_size(std::size_t size) noexcept {
        _max_message_size = size;
    }

    void
    on(const chat::Message &message) {
        _received.push_back({static_cast<std::uint8_t>(message.type), message.payload});
    }

    void
    on(broker::Message &&message) {
        _received.push_back({static_cast<std::uint8_t>(message.type), std::move(message.payload)});
    }

    [[nodiscard]] const std::vector<Received> &
    received() const noexcept {
        return _received;
    }
};

template <typename Header, typename Protocol>
void
verify_protocol(std::uint16_t magic, std::uint8_t type) {
    static_assert(sizeof(Header) == 8);
    constexpr std::size_t header_size      = sizeof(Header);
    constexpr std::size_t max_payload_size = 64 * 1024;

    {
        FakeIO      io;
        Protocol    protocol{io};
        Header      header{magic, 1, type, 4};
        const auto *raw_header = reinterpret_cast<const char *>(&header);

        for (std::size_t i = 0; i < header_size; ++i) {
            io.in().append(raw_header + i, 1);
            EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
            EXPECT_TRUE(protocol.ok());
        }
        constexpr char payload[] = "DATA";
        for (std::size_t i = 0; i < 4; ++i) {
            io.in().append(payload + i, 1);
            const auto expected = i == 3 ? header_size + 4 : qb::io::async::IProtocol::kNoMessage;
            EXPECT_EQ(protocol.getMessageSize(), expected);
            EXPECT_TRUE(protocol.ok());
        }

        protocol.onMessage(header_size + 4);
        ASSERT_EQ(io.received().size(), 1u);
        EXPECT_EQ(io.received().front(), (FakeIO::Received{type, "DATA"}));
    }

    {
        FakeIO   io;
        Protocol protocol{io};
        Header   empty{magic, 1, type, 0};
        Header   next{magic, 1, type, 3};
        io.in().append(&empty, sizeof(empty));
        io.in().append(&next, sizeof(next));
        io.in().append("XYZ", 3);

        ASSERT_EQ(protocol.getMessageSize(), header_size);
        protocol.onMessage(header_size);
        io.in().consume(header_size);
        ASSERT_EQ(protocol.getMessageSize(), header_size + 3);
        protocol.onMessage(header_size + 3);

        ASSERT_EQ(io.received().size(), 2u);
        EXPECT_EQ(io.received()[0], (FakeIO::Received{type, ""}));
        EXPECT_EQ(io.received()[1], (FakeIO::Received{type, "XYZ"}));
    }

    for (const auto bad_header : {
             Header{static_cast<std::uint16_t>(magic ^ 1u), 1, type, 4},
             Header{magic, 2, type, 4},
         }) {
        FakeIO   io;
        Protocol protocol{io};
        io.in().append(&bad_header, sizeof(bad_header));
        const auto original = io.in().bytes();

        for (int repeat = 0; repeat < 3; ++repeat) {
            EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
            EXPECT_FALSE(protocol.ok());
            EXPECT_EQ(io.in().bytes(), original);
        }
    }

    {
        FakeIO   io;
        Protocol protocol{io};
        io.set_max_message_size(header_size - 1);
        Header header{magic, 1, type, 0};
        io.in().append(&header, sizeof(header));
        const auto original = io.in().bytes();

        EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
        EXPECT_FALSE(protocol.ok());
        EXPECT_EQ(io.in().bytes(), original);
    }

    {
        FakeIO   io;
        Protocol protocol{io};
        io.set_max_message_size(header_size + 2);
        Header header{magic, 1, type, 3};
        io.in().append(&header, sizeof(header));
        const auto original = io.in().bytes();

        EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
        EXPECT_FALSE(protocol.ok());
        EXPECT_EQ(io.in().bytes(), original);
    }

    {
        FakeIO   io;
        Protocol protocol{io};
        io.set_max_message_size(header_size + 3);
        Header header{magic, 1, type, 3};
        io.in().append(&header, sizeof(header));

        EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
        EXPECT_TRUE(protocol.ok());

        constexpr char payload[] = {'A', '\0', 'B'};
        io.in().append(payload, 2);
        EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
        EXPECT_TRUE(protocol.ok());
        io.in().append(payload + 2, 1);
        ASSERT_EQ(protocol.getMessageSize(), header_size + 3);
        protocol.onMessage(header_size + 3);

        ASSERT_EQ(io.received().size(), 1u);
        EXPECT_EQ(io.received().front(), (FakeIO::Received{type, std::string(payload, sizeof(payload))}));
    }

    {
        FakeIO   io;
        Protocol protocol{io};
        Header   header{magic, 1, type, static_cast<std::uint32_t>(max_payload_size + 1)};
        io.in().append(&header, sizeof(header));
        const auto original = io.in().bytes();

        EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
        EXPECT_FALSE(protocol.ok());
        EXPECT_EQ(io.in().bytes(), original);
    }

    {
        FakeIO   io;
        Protocol protocol{io};
        io.set_max_message_size(header_size + max_payload_size);
        Header header{magic, 1, type, static_cast<std::uint32_t>(max_payload_size)};
        io.in().append(&header, sizeof(header));

        EXPECT_EQ(protocol.getMessageSize(), qb::io::async::IProtocol::kNoMessage);
        EXPECT_TRUE(protocol.ok());

        const std::string payload(max_payload_size, 'x');
        io.in().append(payload.data(), payload.size());
        ASSERT_EQ(protocol.getMessageSize(), header_size + max_payload_size);
        protocol.onMessage(header_size + max_payload_size);

        ASSERT_EQ(io.received().size(), 1u);
        EXPECT_EQ(io.received().front().type, type);
        EXPECT_EQ(io.received().front().payload.size(), max_payload_size);
        EXPECT_EQ(io.received().front().payload, payload);
    }
}

TEST(ChatProtocolFraming, RejectsInvalidAndFramesFragmentedCoalescedAndBoundaryMessages) {
    verify_protocol<chat::MessageHeader, chat::ChatProtocol<FakeIO>>(chat::PROTOCOL_MAGIC,
                                                                     static_cast<std::uint8_t>(chat::MessageType::CHAT_MESSAGE));
}

TEST(BrokerProtocolFraming, RejectsInvalidAndFramesFragmentedCoalescedAndBoundaryMessages) {
    verify_protocol<broker::MessageHeader, broker::BrokerProtocol<FakeIO>>(broker::PROTOCOL_MAGIC,
                                                                           static_cast<std::uint8_t>(broker::MessageType::MESSAGE));
}

} // namespace protocol_framing_test
