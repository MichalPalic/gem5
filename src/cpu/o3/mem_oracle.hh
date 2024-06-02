
#ifndef __CPU_MEM_DEP_ORACLE_HH__
#define __CPU_MEM_DEP_ORACLE_HH__

#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/iostreams/filtering_stream.hpp>

#include "base/trace.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/o3/dyn_inst_ptr.hh"
#include "cpu/o3/mem_dep_counter.hh"
#include "params/BaseO3CPU.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

  //Syntactic sugar
  using valid = bool;

  //Tuple holds bool:  Valid (bool), Load(true)/store(false),
  // this pc/access_number, pc/access_number dependent, sequence_number,
  //eff_sequence_number mem_addr

  using full_trace_T = std::tuple<valid, bool, TraceUID, TraceUID, InstSeqNum,
      InstSeqNum, Addr, uint32_t>;

  //Tuple of sequence number (this) - sequence number (depending on),
  //valid (bool), if entry is actually a barrier.
  using mini_trace_T = std::tuple <TraceUID, TraceUID>;

  // bool compare_mini_trace(const mini_trace_T &a, const mini_trace_T &b);
  enum class OracleMode
  {
      Disabled,
      Trace,
      Refine,
      Run,
      Barrier
  };

  class MemOracle
  {
    public:

    OracleMode mode = OracleMode::Disabled;

    o3::CPU* cpu;

    //File handles
    boost::iostreams::filtering_ostream mini_trace_f;
    boost::iostreams::filtering_ostream full_trace_f;

    //Used to store loaded mini-trace information
    std::multimap<TraceUID, TraceUID> trace_dependencies;
    std::set<TraceUID> trace_barriers;

    //File dump vars
    std::string trace_dir = "m5out";

    MemOracle(o3::CPU * _cpu, const BaseO3CPUParams &params);

    std::vector<InstSeqNum> checkInst(const o3::DynInstPtr &inst);
    uint32_t load_mini_trace(std::string path);


    //Tracer remnants
    const uint32_t FLUSH_THRESHOLD = 1000000;
    std::vector<full_trace_T > full_mem_trace;
    std::vector<mini_trace_T> mini_mem_trace;
    //Fwd address cache, for mapping sequence number - sequence number
    // dependency pairs

    std::unordered_map<Addr, TraceUID> fwd_cache;
    void record_comitted(const o3::DynInstPtr &inst);
    void push_to_buffers(const o3::DynInstPtr &inst);

    void check_flush();
    void flush_mini_buffer();
    void flush_full_buffer();
    void close_files();

  };

}

#endif // __CPU_MEM_DEP_ORACLE_HH__
