#include "net/buffer.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using cppws::net::Buffer;

class BufferTest : public ::testing::Test {
protected:
  Buffer buf{256};
};

TEST_F(BufferTest, InitiallyEmpty) {
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.readable_bytes(), 0u);
  EXPECT_EQ(buf.writable_bytes(), 256u);
  EXPECT_EQ(buf.capacity(), 256u);
}

TEST_F(BufferTest, WriteAndRead) {
  const std::string_view data = "Hello, World!";
  std::memcpy(buf.write_ptr(), data.data(), data.size());
  buf.commit_write(data.size());

  EXPECT_EQ(buf.readable_bytes(), data.size());
  EXPECT_FALSE(buf.empty());

  const auto view = buf.readable();
  EXPECT_EQ(std::string_view(view.data(), view.size()), data);
}

TEST_F(BufferTest, ConsumeAdvancesCursor) {
  const std::string_view data = "ABCDE";
  std::memcpy(buf.write_ptr(), data.data(), data.size());
  buf.commit_write(data.size());

  buf.consume(2);
  EXPECT_EQ(buf.readable_bytes(), 3u);

  const auto view = buf.readable();
  EXPECT_EQ(std::string_view(view.data(), view.size()), "CDE");
}

TEST_F(BufferTest, ConsumeAllResetsCorrectly) {
  std::memcpy(buf.write_ptr(), "test", 4);
  buf.commit_write(4);
  buf.consume_all();

  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.readable_bytes(), 0u);
  EXPECT_EQ(buf.writable_bytes(), 256u);
}

TEST_F(BufferTest, CompactRecoversDeadZone) {
  const std::string_view data = "Hello";
  std::memcpy(buf.write_ptr(), data.data(), data.size());
  buf.commit_write(data.size());

  buf.consume(3); // Dead zone: [0..3)

  const std::size_t recovered = buf.compact();
  EXPECT_EQ(recovered, 3u);
  EXPECT_EQ(buf.readable_bytes(), 2u);

  const auto view = buf.readable();
  EXPECT_EQ(std::string_view(view.data(), view.size()), "lo");
}

TEST_F(BufferTest, CompactOnEmptyIsNoop) {
  const std::size_t recovered = buf.compact();
  EXPECT_EQ(recovered, 0u);
  EXPECT_EQ(buf.writable_bytes(), 256u);
}

TEST_F(BufferTest, ReserveGrowsCapacity) {
  const std::string_view data = "data";
  std::memcpy(buf.write_ptr(), data.data(), data.size());
  buf.commit_write(data.size());

  buf.reserve(1024);
  EXPECT_GE(buf.capacity(), 1024u);
  EXPECT_EQ(buf.readable_bytes(), data.size()); // Data preserved
}

TEST_F(BufferTest, ReserveSmallerThanCapacityIsNoop) {
  buf.reserve(16); // Less than 256
  EXPECT_EQ(buf.capacity(), 256u);
}

TEST_F(BufferTest, ResetClearsCursors) {
  std::memcpy(buf.write_ptr(), "data", 4);
  buf.commit_write(4);
  buf.consume(2);
  buf.reset();

  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.writable_bytes(), 256u);
}

TEST_F(BufferTest, WritableSpaceDecrementsCorrectly) {
  const std::size_t initial_writable = buf.writable_bytes();
  std::memcpy(buf.write_ptr(), "XYZ", 3);
  buf.commit_write(3);
  EXPECT_EQ(buf.writable_bytes(), initial_writable - 3);
}

TEST_F(BufferTest, MultipleWriteAndConsumeRoundtrips) {
  for (int i = 0; i < 10; ++i) {
    const std::string msg = "msg" + std::to_string(i);
    std::memcpy(buf.write_ptr(), msg.data(), msg.size());
    buf.commit_write(msg.size());
    buf.consume(msg.size());
    buf.compact();
  }
  EXPECT_TRUE(buf.empty());
}
