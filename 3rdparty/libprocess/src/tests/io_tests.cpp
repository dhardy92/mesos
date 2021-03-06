#include <gmock/gmock.h>

#include <string>

#include <process/future.hpp>
#include <process/gtest.hpp>
#include <process/io.hpp>

#include <stout/gtest.hpp>
#include <stout/os.hpp>

#include "encoder.hpp"

using namespace process;

using std::string;


TEST(IO, Poll)
{
  ASSERT_TRUE(GTEST_IS_THREADSAFE);

  int pipes[2];
  ASSERT_NE(-1, pipe(pipes));

  // Test discard when polling.
  Future<short> future = io::poll(pipes[0], io::READ);
  EXPECT_TRUE(future.isPending());
  future.discard();
  AWAIT_DISCARDED(future);

  // Test successful polling.
  future = io::poll(pipes[0], io::READ);
  EXPECT_TRUE(future.isPending());
  ASSERT_EQ(3, write(pipes[1], "hi", 3));
  AWAIT_EXPECT_EQ(io::READ, future);

  close(pipes[0]);
  close(pipes[1]);
}


TEST(IO, Read)
{
  ASSERT_TRUE(GTEST_IS_THREADSAFE);

  int pipes[2];
  char data[3];

  // Create a blocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));

  // Test on a blocking file descriptor.
  AWAIT_EXPECT_FAILED(io::read(pipes[0], data, 3));

  close(pipes[0]);
  close(pipes[1]);

  // Test on a closed file descriptor.
  AWAIT_EXPECT_FAILED(io::read(pipes[0], data, 3));

  // Create a nonblocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));
  ASSERT_SOME(os::nonblock(pipes[0]));
  ASSERT_SOME(os::nonblock(pipes[1]));

  // Test reading nothing.
  AWAIT_EXPECT_EQ(0, io::read(pipes[0], data, 0));

  // Test discarded read.
  Future<size_t> future = io::read(pipes[0], data, 3);
  EXPECT_TRUE(future.isPending());
  future.discard();
  AWAIT_DISCARDED(future);

  // Test successful read.
  future = io::read(pipes[0], data, 3);
  ASSERT_FALSE(future.isReady());

  ASSERT_EQ(2, write(pipes[1], "hi", 2));

  AWAIT_ASSERT_EQ(2u, future);
  EXPECT_EQ('h', data[0]);
  EXPECT_EQ('i', data[1]);

  // Test cancellation.
  future = io::read(pipes[0], data, 1);
  ASSERT_FALSE(future.isReady());

  future.discard();

  ASSERT_EQ(3, write(pipes[1], "omg", 3));

  AWAIT_ASSERT_EQ(3u, io::read(pipes[0], data, 3)) << string(data, 2);
  EXPECT_EQ('o', data[0]);
  EXPECT_EQ('m', data[1]);
  EXPECT_EQ('g', data[2]);

  // Test read EOF.
  future = io::read(pipes[0], data, 3);
  ASSERT_FALSE(future.isReady());

  close(pipes[1]);

  AWAIT_ASSERT_EQ(0u, future);

  close(pipes[0]);
}


TEST(IO, BufferedRead)
{
  // 128 Bytes.
  string data =
      "This data is much larger than BUFFERED_READ_SIZE, which means it will "
      "trigger multiple buffered async reads as a result.........";
  ASSERT_EQ(128u, data.size());

  // Keep doubling the data size until we're guaranteed to trigger at least
  // 3 buffered async reads.
  while (data.length() < 3 * io::BUFFERED_READ_SIZE) {
    data.append(data);
  }

  // First read from a file.
  ASSERT_SOME(os::write("file", data));

  Try<int> fd = os::open("file", O_RDONLY);
  ASSERT_SOME(fd);

  // Read from blocking fd.
  AWAIT_EXPECT_FAILED(io::read(fd.get()));

  // Read from non-blocking fd.
  ASSERT_TRUE(os::nonblock(fd.get()).isSome());
  AWAIT_EXPECT_EQ(data, io::read(fd.get()));

  os::close(fd.get());

  // Now read from pipes.
  int pipes[2];

  // Create a blocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));

  // Test on a blocking pipe.
  AWAIT_EXPECT_FAILED(io::read(pipes[0]));

  close(pipes[0]);
  close(pipes[1]);

  // Test on a closed pipe.
  AWAIT_EXPECT_FAILED(io::read(pipes[0]));

  // Create a nonblocking pipe for reading.
  ASSERT_NE(-1, ::pipe(pipes));
  ASSERT_SOME(os::nonblock(pipes[0]));

  // Test a successful read from the pipe.
  Future<string> future = io::read(pipes[0]);

  // At first, the future will not be ready until we write to and
  // close the pipe.
  ASSERT_FALSE(future.isReady());

  ASSERT_SOME(os::write(pipes[1], data));
  close(pipes[1]);

  AWAIT_EXPECT_EQ(data, future);

  close(pipes[0]);

  ASSERT_SOME(os::rm("file"));
}


TEST(IO, Write)
{
  ASSERT_TRUE(GTEST_IS_THREADSAFE);

  int pipes[2];

  // Create a blocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));

  // Test on a blocking file descriptor.
  AWAIT_EXPECT_FAILED(io::write(pipes[1], (void*) "hi", 2));

  close(pipes[0]);
  close(pipes[1]);

  // Test on a closed file descriptor.
  AWAIT_EXPECT_FAILED(io::write(pipes[1], (void*) "hi", 2));

  // Create a nonblocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));
  ASSERT_SOME(os::nonblock(pipes[0]));
  ASSERT_SOME(os::nonblock(pipes[1]));

  // Test writing nothing.
  AWAIT_EXPECT_EQ(0, io::write(pipes[1], (void*) "hi", 0));

  // Test successful write.
  AWAIT_EXPECT_EQ(2, io::write(pipes[1], (void*) "hi", 2));

  char data[2];
  AWAIT_EXPECT_EQ(2, io::read(pipes[0], data, 2));
  EXPECT_EQ("hi", string(data, 2));

  // Test write to broken pipe.
  close(pipes[0]);
  AWAIT_EXPECT_FAILED(io::write(pipes[1], (void*) "hi", 2));

  close(pipes[1]);
}


TEST(IO, DISABLED_BlockingWrite)
{
  ASSERT_TRUE(GTEST_IS_THREADSAFE);

  int pipes[2];

  // Create a nonblocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));
  ASSERT_SOME(os::nonblock(pipes[0]));
  ASSERT_SOME(os::nonblock(pipes[1]));

  // Determine the pipe buffer size by writing until we block.
  size_t size = 0;
  ssize_t length = 0;
  while ((length = ::write(pipes[1], "data", 4)) >= 0) {
    size += length;
  }

  ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);

  close(pipes[0]);
  close(pipes[1]);

  // Recreate a nonblocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));
  ASSERT_SOME(os::nonblock(pipes[0]));
  ASSERT_SOME(os::nonblock(pipes[1]));

  // Create 8 pipe buffers worth of data. Try and write all the data
  // at once. Check that the future is pending after doing the
  // write. Then read 128 bytes and make sure the write remains
  // pending.

  string data = "data"; // 4 Bytes.
  ASSERT_EQ(4u, data.size());

  while (data.size() < (8 * size)) {
    data.append(data);
  }

  Future<Nothing> future1 = io::write(pipes[1], data);

  EXPECT_TRUE(future1.isPending());

  // Check that a subsequent write remains pending and can be
  // discarded.
  Future<Nothing> future2 = io::write(pipes[1], "hello world");
  EXPECT_TRUE(future2.isPending());
  future2.discard();
  AWAIT_DISCARDED(future2);

  // Check after reading some data the first write remains pending.
  ASSERT_LT(128, size);
  char temp[128];
  AWAIT_EXPECT_EQ(128, io::read(pipes[0], temp, 128));

  EXPECT_TRUE(future1.isPending());

  // Now read all the data we wrote the first time and expect the
  // first future to succeed since the second future should have been
  // completely discarded.
  length = 128; // To account for io::read above.
  while (length < data.size()) {
    Future<size_t> read = io::read(pipes[0], temp, 128);
    AWAIT_READY(read);
    length += read.get();
  }

  AWAIT_EXPECT_READY(future1);

  close(pipes[0]);
  close(pipes[1]);
}


TEST(IO, splice)
{
  ASSERT_TRUE(GTEST_IS_THREADSAFE);

  // Create a temporary file for splicing into.
  Try<string> path = os::mktemp();
  ASSERT_SOME(path);

  Try<int> fd = os::open(
      path.get(),
      O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IRWXO);
  ASSERT_SOME(fd);

  ASSERT_SOME(os::nonblock(fd.get()));

  // Use a pipe for doing the splicing.
  int pipes[2];

  // Start with a blocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));

  // Test splicing on a blocking file descriptor.
  AWAIT_EXPECT_FAILED(io::splice(pipes[1], fd.get()));

  close(pipes[0]);
  close(pipes[1]);

  // Test on a closed file descriptor.
  AWAIT_EXPECT_FAILED(io::splice(pipes[1], fd.get()));

  // Now create a nonblocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));
  ASSERT_SOME(os::nonblock(pipes[0]));
  ASSERT_SOME(os::nonblock(pipes[1]));

  // Test write to broken pipe.
  close(pipes[0]);
  AWAIT_EXPECT_FAILED(io::splice(pipes[1], fd.get()));

  close(pipes[1]);

  // Recreate a nonblocking pipe.
  ASSERT_NE(-1, ::pipe(pipes));
  ASSERT_SOME(os::nonblock(pipes[0]));
  ASSERT_SOME(os::nonblock(pipes[1]));

  // Test discard.
  Future<Nothing> splice = io::splice(pipes[0], fd.get());
  EXPECT_TRUE(splice.isPending());
  splice.discard();
  AWAIT_DISCARDED(splice);

  // Now write data to the pipe and splice to the file.
  string data =
    "Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed do "
    "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim "
    "ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut "
    "aliquip ex ea commodo consequat. Duis aute irure dolor in "
    "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
    "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
    "culpa qui officia deserunt mollit anim id est laborum.";

  // Create more data!
  while (Bytes(data.size()) < Megabytes(1)) {
    data.append(data);
  }

  splice = io::splice(pipes[0], fd.get());

  AWAIT_READY(io::write(pipes[1], data));

  // Closing the write pipe should cause an EOF on the read end, thus
  // completing 'splice'.
  close(pipes[1]);

  AWAIT_READY(splice);

  close(pipes[0]);

  os::close(fd.get());

  // Now make sure all the data is there!
  Try<string> read = os::read(path.get());
  ASSERT_SOME(read);
  EXPECT_EQ(data, read.get());
}
