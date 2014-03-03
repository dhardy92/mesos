/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __LOG_HPP__
#define __LOG_HPP__

#include <stdint.h>

#include <list>
#include <set>
#include <string>

#include <process/owned.hpp>
#include <process/process.hpp>
#include <process/shared.hpp>
#include <process/timeout.hpp>

#include <stout/duration.hpp>
#include <stout/none.hpp>
#include <stout/option.hpp>
#include <stout/result.hpp>

#include "zookeeper/group.hpp"

namespace mesos {
namespace internal {
namespace log {

// Forward declarations.
class LogProcess;
class LogReaderProcess;
class LogWriterProcess;


class Log
{
public:
  // Forward declarations.
  class Reader;
  class Writer;

  class Position
  {
  public:
    bool operator == (const Position& that) const
    {
      return value == that.value;
    }

    bool operator < (const Position& that) const
    {
      return value < that.value;
    }

    bool operator <= (const Position& that) const
    {
      return value <= that.value;
    }

    bool operator > (const Position& that) const
    {
      return value > that.value;
    }

    bool operator >= (const Position& that) const
    {
      return value >= that.value;
    }

    // Returns an "identity" off this position, useful for serializing
    // to logs or across communication mediums.
    std::string identity() const
    {
      CHECK(sizeof(value) == 8);
      char bytes[8];
      bytes[0] =(0xff & (value >> 56));
      bytes[1] = (0xff & (value >> 48));
      bytes[2] = (0xff & (value >> 40));
      bytes[3] = (0xff & (value >> 32));
      bytes[4] = (0xff & (value >> 24));
      bytes[5] = (0xff & (value >> 16));
      bytes[6] = (0xff & (value >> 8));
      bytes[7] = (0xff & value);
      return std::string(bytes, sizeof(bytes));
    }

  private:
    friend class Log;
    friend class Writer;
    friend class LogReaderProcess;
    friend class LogWriterProcess;

    Position(uint64_t _value) : value(_value) {}

    uint64_t value;
  };

  class Entry
  {
  public:
    Position position;
    std::string data;

  private:
    friend class LogReaderProcess;

    Entry(const Position& _position, const std::string& _data)
      : position(_position), data(_data) {}
  };

  class Reader
  {
  public:
    Reader(Log* log);
    ~Reader();

    // Returns all entries between the specified positions, unless
    // those positions are invalid, in which case returns an error.
    Result<std::list<Entry> > read(
        const Position& from,
        const Position& to,
        const process::Timeout& timeout);

    // Returns the beginning position of the log from the perspective
    // of the local replica (which may be out of date if the log has
    // been opened and truncated while this replica was partitioned).
    Position beginning();

    // Returns the ending (i.e., last) position of the log from the
    // perspective of the local replica (which may be out of date if
    // the log has been opened and appended to while this replica was
    // partitioned).
    Position ending();

  private:
    LogReaderProcess* process;
  };

  class Writer
  {
  public:
    // Creates a new writer associated with the specified log. Only
    // one writer (local and remote) is valid at a time. A writer
    // becomes invalid if any operation returns an error, and a new
    // writer must be created in order perform subsequent operations.
    Writer(Log* log, const Duration& timeout, int retries = 3);
    ~Writer();

    // Attempts to append the specified data to the log. A none result
    // means the operation timed out, otherwise the new ending
    // position of the log is returned or an error. Upon error a new
    // Writer must be created.
    Result<Position> append(
        const std::string& data,
        const process::Timeout& timeout);

    // Attempts to truncate the log up to but not including the
    // specificed position. A none result means the operation timed
    // out, otherwise the new ending position of the log is returned
    // or an error. Upon error a new Writer must be created.
    Result<Position> truncate(
        const Position& to,
        const process::Timeout& timeout);

  private:
    LogWriterProcess* process;
  };

  // Creates a new replicated log that assumes the specified quorum
  // size, is backed by a file at the specified path, and coordinates
  // with other replicas via the set of process PIDs.
  Log(int quorum,
      const std::string& path,
      const std::set<process::UPID>& pids);

  // Creates a new replicated log that assumes the specified quorum
  // size, is backed by a file at the specified path, and coordinates
  // with other replicas associated with the specified ZooKeeper
  // servers, timeout, and znode.
  Log(int quorum,
      const std::string& path,
      const std::string& servers,
      const Duration& timeout,
      const std::string& znode,
      const Option<zookeeper::Authentication>& auth = None());

  ~Log();

  // Returns a position based off of the bytes recovered from
  // Position.identity().
  Position position(const std::string& identity) const
  {
    CHECK(identity.size() == 8);
    const char* bytes = identity.c_str();
    uint64_t value =
      ((uint64_t) (bytes[0] & 0xff) << 56) |
      ((uint64_t) (bytes[1] & 0xff) << 48) |
      ((uint64_t) (bytes[2] & 0xff) << 40) |
      ((uint64_t) (bytes[3] & 0xff) << 32) |
      ((uint64_t) (bytes[4] & 0xff) << 24) |
      ((uint64_t) (bytes[5] & 0xff) << 16) |
      ((uint64_t) (bytes[6] & 0xff) << 8) |
      ((uint64_t) (bytes[7] & 0xff));
    return Position(value);
  }

private:
  friend class LogReaderProcess;
  friend class LogWriterProcess;

  LogProcess* process;
};

} // namespace log {
} // namespace internal {
} // namespace mesos {

#endif // __LOG_HPP__
