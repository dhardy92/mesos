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

#ifndef __TESTS_CLUSTER_HPP__
#define __TESTS_CLUSTER_HPP__

#include <map>

#include <process/owned.hpp>
#include <process/pid.hpp>
#include <process/process.hpp>

#include <stout/error.hpp>
#include <stout/foreach.hpp>
#include <stout/none.hpp>
#include <stout/nothing.hpp>
#include <stout/option.hpp>
#include <stout/path.hpp>
#include <stout/try.hpp>

#include "files/files.hpp"

#include "master/allocator.hpp"
#include "master/contender.hpp"
#include "master/detector.hpp"
#include "master/hierarchical_allocator_process.hpp"
#include "master/flags.hpp"
#include "master/master.hpp"
#include "master/registrar.hpp"
#include "master/repairer.hpp"

#include "slave/flags.hpp"
#include "slave/containerizer/containerizer.hpp"
#include "slave/slave.hpp"

#include "state/in_memory.hpp"
#include "state/protobuf.hpp"
#include "state/storage.hpp"

#include "zookeeper/url.hpp"

namespace mesos {
namespace internal {
namespace tests {

class Cluster
{
public:
  Cluster(const Option<zookeeper::URL>& url = None())
    : masters(this, url),
      slaves(this, &masters) {}

  // Abstracts the masters of a cluster.
  class Masters
  {
  public:
    Masters(Cluster* _cluster, const Option<zookeeper::URL>& _url);
    ~Masters();

    void shutdown();

    // Start and manage a new master using the specified flags.
    // This overload is shorthand to specify that you want default
    // master objects and is equivalent to passing None to all of
    // the required arguments of the other overload.
    Try<process::PID<master::Master> > start(
        const master::Flags& flags = master::Flags());

    // Start and manage a new master using the specified flags.
    // An allocator process may be specified in which case it will outlive
    // the launched master.  If no allocator process is specified then
    // the default allocator will be instantiated.
    Try<process::PID<master::Master> > start(
        Option<master::allocator::AllocatorProcess*> allocatorProcess,
        const master::Flags& flags = master::Flags());


    // Stops and cleans up a master at the specified PID.
    Try<Nothing> stop(const process::PID<master::Master>& pid);

    // Returns a new master detector for this instance of masters.
    process::Owned<MasterDetector> detector();

  private:
    // Not copyable, not assignable.
    Masters(const Masters&);
    Masters& operator = (const Masters&);

    Cluster* cluster; // Enclosing class.
    Option<zookeeper::URL> url;

    // Encapsulates a single master's dependencies.
    struct MasterInfo
    {
      MasterInfo()
        : master(NULL),
          allocator(NULL),
          allocatorProcess(NULL),
          contender(NULL),
          detector(NULL) {}

      master::Master* master;
      master::allocator::Allocator* allocator;
      master::allocator::AllocatorProcess* allocatorProcess;
      state::Storage* storage;
      state::protobuf::State* state;
      master::Registrar* registrar;
      master::Repairer* repairer;
      MasterContender* contender;
      MasterDetector* detector;
    };

    std::map<process::PID<master::Master>, MasterInfo> masters;
  };

  // Abstracts the slaves of a cluster.
  class Slaves
  {
  public:
    Slaves(Cluster* _cluster, Masters* _masters);
    ~Slaves();

    // Stop and clean up all slaves.
    void shutdown();

    // Start and manage a new slave with a process isolator using the
    // specified flags.
    Try<process::PID<slave::Slave> > start(
        const slave::Flags& flags = slave::Flags());

    // Start and manage a new slave injecting the specified isolator.
    // The isolator is expected to outlive the launched slave (i.e.,
    // until it is stopped via Slaves::stop).
    Try<process::PID<slave::Slave> > start(
        slave::Containerizer* containerizer,
        const slave::Flags& flags = slave::Flags());

    // Start and manage a new slave injecting the specified Master
    // Detector. The detector is expected to outlive the launched
    // slave (i.e., until it is stopped via Slaves::stop).
    Try<process::PID<slave::Slave> > start(
        process::Owned<MasterDetector> detector,
        const slave::Flags& flags = slave::Flags());

    Try<process::PID<slave::Slave> > start(
        slave::Containerizer* containerizer,
        process::Owned<MasterDetector> detector,
        const slave::Flags& flags = slave::Flags());

    // Stops and cleans up a slave at the specified PID. If 'shutdown'
    // is true than the slave is sent a shutdown message instead of
    // being terminated.
    Try<Nothing> stop(
        const process::PID<slave::Slave>& pid,
        bool shutdown = false);

  private:
    // Not copyable, not assignable.
    Slaves(const Slaves&);
    Slaves& operator = (const Slaves&);

    Cluster* cluster; // Enclosing class.
    Masters* masters; // Used to create MasterDetector instances.

    // Encapsulates a single slave's dependencies.
    struct Slave
    {
      Slave()
        : containerizer(NULL),
          slave(NULL),
          detector(NULL) {}

      // Only register the containerizer here if it is created within the
      // Cluster.
      slave::Containerizer* containerizer;
      slave::Slave* slave;
      process::Owned<MasterDetector> detector;
    };

    std::map<process::PID<slave::Slave>, Slave> slaves;
  };

  // Shuts down all masters and slaves.
  void shutdown()
  {
    masters.shutdown();
    slaves.shutdown();
  }

  // Cluster wide shared abstractions.
  Files files;

  Masters masters;
  Slaves slaves;

private:
  // Not copyable, not assignable.
  Cluster(const Cluster&);
  Cluster& operator = (const Cluster&);
};


inline Cluster::Masters::Masters(
    Cluster* _cluster,
    const Option<zookeeper::URL>& _url)
  : cluster(_cluster),
    url(_url) {}


inline Cluster::Masters::~Masters()
{
  shutdown();
}


inline void Cluster::Masters::shutdown()
{
  // TODO(benh): Use utils::copy from stout once namespaced.
  std::map<process::PID<master::Master>, MasterInfo> copy(masters);
  foreachkey (const process::PID<master::Master>& pid, copy) {
    stop(pid);
  }
  masters.clear();
}


inline Try<process::PID<master::Master> > Cluster::Masters::start(
    const master::Flags& flags)
{
  return start(None(), flags);
}


inline Try<process::PID<master::Master> > Cluster::Masters::start(
    Option<master::allocator::AllocatorProcess*> allocatorProcess,
    const master::Flags& flags)
{
  // Disallow multiple masters when not using ZooKeeper.
  if (!masters.empty() && url.isNone()) {
    return Error("Can not start multiple masters when not using ZooKeeper");
  }

  MasterInfo masterInfo;

  if (allocatorProcess.isNone()) {
    masterInfo.allocatorProcess =
        new master::allocator::HierarchicalDRFAllocatorProcess();
    masterInfo.allocator =
        new master::allocator::Allocator(masterInfo.allocatorProcess);
  } else {
    masterInfo.allocatorProcess = NULL;
    masterInfo.allocator =
        new master::allocator::Allocator(allocatorProcess.get());
  }

  if (flags.registry_strict) {
    EXIT(1) << "Cannot run with --registry_strict; currently not supported";
  }

  if (flags.registry == "in_memory") {
    masterInfo.storage = new state::InMemoryStorage();
  } else {
    return Error("'" + flags.registry + "' is not a supported"
                 " option for registry persistence");
  }

  CHECK_NOTNULL(masterInfo.storage);

  masterInfo.state = new state::protobuf::State(masterInfo.storage);
  masterInfo.registrar = new master::Registrar(flags, masterInfo.state);
  masterInfo.repairer = new master::Repairer();

  if (url.isSome()) {
    masterInfo.contender = new ZooKeeperMasterContender(url.get());
    masterInfo.detector = new ZooKeeperMasterDetector(url.get());
  } else {
    masterInfo.contender = new StandaloneMasterContender();
    masterInfo.detector = new StandaloneMasterDetector();
  }

  masterInfo.master = new master::Master(
      masterInfo.allocator,
      masterInfo.registrar,
      masterInfo.repairer,
      &cluster->files,
      masterInfo.contender,
      masterInfo.detector,
      flags);

  if (url.isNone()) {
    // This means we are using the StandaloneMasterDetector.
    CHECK_NOTNULL(dynamic_cast<StandaloneMasterDetector*>(masterInfo.detector))
        ->appoint(masterInfo.master->info());
  }

  process::PID<master::Master> pid = process::spawn(masterInfo.master);

  masters[pid] = masterInfo;

  return pid;
}


inline Try<Nothing> Cluster::Masters::stop(
    const process::PID<master::Master>& pid)
{
  if (masters.count(pid) == 0) {
    return Error("No master found to stop");
  }

  MasterInfo masterInfo = masters[pid];

  process::terminate(masterInfo.master);
  process::wait(masterInfo.master);
  delete masterInfo.master;

  delete masterInfo.allocator; // Terminates and waits for allocator process.
  delete masterInfo.allocatorProcess; // May be NULL.

  delete masterInfo.registrar;
  delete masterInfo.repairer;
  delete masterInfo.state;
  delete masterInfo.storage;

  delete masterInfo.contender;
  delete masterInfo.detector;

  masters.erase(pid);

  return Nothing();
}


inline process::Owned<MasterDetector> Cluster::Masters::detector()
{
  if (url.isSome()) {
    return process::Owned<MasterDetector>(
        new ZooKeeperMasterDetector(url.get()));
  }

  CHECK(masters.size() == 1);

  return process::Owned<MasterDetector>(
      new StandaloneMasterDetector(masters.begin()->first));
}


inline Cluster::Slaves::Slaves(Cluster* _cluster, Masters* _masters)
  : cluster(_cluster), masters(_masters) {}


inline Cluster::Slaves::~Slaves()
{
  shutdown();
}


inline void Cluster::Slaves::shutdown()
{
  // TODO(benh): Use utils::copy from stout once namespaced.
  std::map<process::PID<slave::Slave>, Slave> copy(slaves);
  foreachkey (const process::PID<slave::Slave>& pid, copy) {
    stop(pid);
  }
  slaves.clear();
}


inline Try<process::PID<slave::Slave> > Cluster::Slaves::start(
    const slave::Flags& flags)
{
  // TODO(benh): Create a work directory if using the default.

  Slave slave;

  // Create a new containerizer for this slave.
  Try<slave::Containerizer*> containerizer =
    slave::Containerizer::create(flags, true);
  CHECK_SOME(containerizer);

  slave.containerizer = containerizer.get();

  // Get a detector for the master(s).
  slave.detector = masters->detector();

  slave.slave = new slave::Slave(
      flags, slave.detector.get(), slave.containerizer, &cluster->files);
  process::PID<slave::Slave> pid = process::spawn(slave.slave);

  slaves[pid] = slave;

  return pid;
}


inline Try<process::PID<slave::Slave> > Cluster::Slaves::start(
    slave::Containerizer* containerizer,
    const slave::Flags& flags)
{
  return start(containerizer, masters->detector(), flags);
}


inline Try<process::PID<slave::Slave> > Cluster::Slaves::start(
    process::Owned<MasterDetector> detector,
    const slave::Flags& flags)
{
  // TODO(benh): Create a work directory if using the default.

  Slave slave;

  // Create a new containerizer for this slave.
  Try<slave::Containerizer*> containerizer =
    slave::Containerizer::create(flags, true);
  CHECK_SOME(containerizer);

  slave.containerizer = containerizer.get();

  // Get a detector for the master(s).
  slave.detector = detector;

  slave.slave = new slave::Slave(
      flags, slave.detector.get(), slave.containerizer, &cluster->files);
  process::PID<slave::Slave> pid = process::spawn(slave.slave);

  slaves[pid] = slave;

  return pid;
}


inline Try<process::PID<slave::Slave> > Cluster::Slaves::start(
    slave::Containerizer* containerizer,
    process::Owned<MasterDetector> detector,
    const slave::Flags& flags)
{
  // TODO(benh): Create a work directory if using the default.

  Slave slave;

  // Get a detector for the master(s).
  slave.detector = detector;

  slave.slave = new slave::Slave(
      flags, slave.detector.get(), containerizer, &cluster->files);
  process::PID<slave::Slave> pid = process::spawn(slave.slave);

  slaves[pid] = slave;

  return pid;
}


inline Try<Nothing> Cluster::Slaves::stop(
    const process::PID<slave::Slave>& pid,
    bool shutdown)
{
  if (slaves.count(pid) == 0) {
    return Error("No slave found to stop");
  }

  Slave slave = slaves[pid];

  if (shutdown) {
    process::dispatch(slave.slave, &slave::Slave::shutdown, process::UPID());
  } else {
    process::terminate(slave.slave);
  }
  process::wait(slave.slave);
  delete slave.slave;

  delete slave.containerizer; // May be NULL.

  slaves.erase(pid);

  return Nothing();
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {

#endif // __TESTS_CLUSTER_HPP__
