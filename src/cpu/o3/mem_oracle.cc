/*
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "cpu/o3/mem_oracle.hh"
#include "cpu/o3/cpu.hh"
#include "cpu/o3/dyn_inst.hh"

namespace gem5 {
MemOracle::MemOracle(o3::CPU *_cpu, const BaseO3CPUParams &params)
      : cpu(_cpu)
 {

    //Slightly hacky option for passing trace output directory from an
  //environment variable. For example run as:
  // ORACLEMODE=Refine TRACEDIR=dir_of_trace bash -c './build/X86/gem5.debug
  // configs/example/se.py --cpu-type=X86O3CPU --caches
  // --cmd=tests/test-progs/hello/bin/x86/linux/hello'

  //Parse environment variable into oracle mode
  if (std::getenv("ORACLEMODE")){
    std::string mode_str = std::getenv("ORACLEMODE");

    if (mode_str == "Refine"){
        mode = OracleMode::Refine;
        printf("MemOracle: Refine mode from env\n");
    }else if (mode_str == "Trace"){
        mode = OracleMode::Trace;
        printf("MemOracle: Trace mode from env\n");
    }else if (mode_str == "Barrier"){
        mode = OracleMode::Barrier;
        printf("MemOracle: Barrier mode from env\n");
    }else{
        mode = OracleMode::Disabled;
        printf("MemOracle: Disabled mode from env\n");
    }
  } else {
    printf("MemOracle: Disabled mode by default\n");
  }

  if (std::getenv("TRACEDIR")){
    trace_dir = std::getenv("TRACEDIR");
    printf("MemOracle: Loaded path from env var %s \n",
    trace_dir.c_str());
  }

  if (mode == OracleMode::Disabled){
    return;
  }

  //Only wipe traces if starting from scratch
  if (mode == OracleMode::Trace){

    full_trace_f.push(boost::iostreams::zstd_compressor());
    full_trace_f.push(
      boost::iostreams::file_sink(trace_dir + "/full_trace.csv.zst"));

    mini_trace_f.push(boost::iostreams::zstd_compressor());
    mini_trace_f.push(
      boost::iostreams::file_sink(trace_dir + "/mini_trace_0.csv.zst"));

    mini_trace_f << "#Trace\n";

  }else if (mode == OracleMode::Refine || mode == OracleMode::Barrier){
    uint32_t next_trace_id = load_mini_trace( trace_dir );
    mini_trace_f.push(boost::iostreams::zstd_compressor());
    mini_trace_f.push(boost::iostreams::file_sink(
      trace_dir + "/mini_trace_" + std::to_string(next_trace_id) +".csv.zst"));

    //Append comment if refining trace
    mini_trace_f << "#Refinement\n";
  }

  // Register functions to be called before destruction
  registerExitCallback([this]() {
    flush_mini_buffer();
    flush_full_buffer();
    close_files();});
};

uint32_t
MemOracle::load_mini_trace(std::string path){

    //Iterate over up to N mini traces in order
    uint32_t trace_idx = 0;

    while (true){

      //Load actual file
      boost::iostreams::filtering_istream infile;
      infile.push(boost::iostreams::zstd_decompressor());
      infile.push(boost::iostreams::file_source(
        path + "/mini_trace_" + std::to_string(trace_idx) +".csv.zst"));
      printf("Trying to open mem trace path: %s\n",
        (path + "/mini_trace_" + std::to_string(trace_idx)
        + ".csv.zst").c_str());
      std::string line;

      //If fails to read pre-amble, file doesn't exist
      if (!std::getline(infile, line)){
        printf("Failed to open mem trace path %s\n",
          (path + "/mini_trace_" + std::to_string(trace_idx)
          + ".csv.zst").c_str());
        printf("Possibly using the above descriptor for next incremental"
          "trace\n");
        return trace_idx;
      }
      trace_idx++;


      while (std::getline(infile, line))
      {
        //Allow for comments to be ignored
        if (line[0] == '#'){
          continue;
        }

        bool barrier = false;
        if (line[0] == 'B'){
          barrier = true;
          line[0] = '0';
        }

        Addr pc_1, pc_2;
        uint64_t n_1, n_2;

        std::string temp;
        std::stringstream ss(line);

        std::getline(ss, temp, ':');
        pc_1 = std::stoull(temp);

        std::getline(ss, temp, ',');
        n_1 = std::stoull(temp);

        std::getline(ss, temp, ':');
        pc_2 = std::stoull(temp);

        std::getline(ss, temp, ',');
        n_2 = std::stoull(temp);

        TraceUID this_uid = TraceUID(pc_1, n_1);
        TraceUID dep_uid = TraceUID(pc_2, n_2);

        if (barrier){
          trace_barriers.insert(this_uid);
        }else{
          trace_dependencies.insert(
            std::pair<TraceUID, TraceUID>(this_uid, dep_uid));
        }
      }
    }
};

std::vector<InstSeqNum> MemOracle::checkInst(const o3::DynInstPtr &inst){
  assert (inst->isLoad() || inst->isStore() || inst->isAtomic());
  TraceUID tuid = TraceUID( inst->pcState().instAddr(), inst->n_visited);

  // if (tuid == TraceUID(4680765,2)){
  //   printf("Got here");
  // }

  std::vector<InstSeqNum> out;

  //Traverse through the multiple dependencies
  for (auto[itr, rangeEnd] = trace_dependencies.equal_range(tuid);
    itr != rangeEnd; ++itr){
    TraceUID depuid = itr->second;

    //Find dependent instruction in list of in-flight insts
    for (auto depinst : cpu->mem_dep_counter.in_flight){
      if (depinst->pcState().instAddr() == depuid.pc &&
        depinst->n_visited == depuid.n_visited &&
        depinst->isStore()){
          if (depinst->seqNum < inst->seqNum){
            out.push_back(depinst->seqNum);
          } else{
            DPRINTF(MemOracle,"Requesting dependence on newer inst\n");
          }
        }
    }
  }

  //Implement barriers
  if (trace_barriers.count(tuid)){
    DPRINTF(MemOracle,"Requesting barrier on newer inst\n");
    for (auto depinst : cpu->mem_dep_counter.in_flight){
      if (depinst->isStore()){
          if (depinst->seqNum < inst->seqNum){
            out.push_back(depinst->seqNum);
          } else{
            DPRINTF(MemOracle,"Requesting barrier on newer inst\n");
          }
        }
    }
  }
  return out;
}

//Tracer

void MemOracle::record_comitted(const o3::DynInstPtr &inst){

    if (mode == OracleMode::Disabled)
      return;

    //Record branch for branch distance graphs
    if (inst->isCondCtrl() && mode == OracleMode::Trace){

        //Generate full trace entry
        TraceUID tuid = TraceUID(inst->pcState().instAddr(), inst->n_visited);

        //False true unused elsewhere, use to label branch
        auto full_trace_elem = full_trace_T(false, true, tuid,
          TraceUID(inst->readPredTaken(), inst->mispredicted()),
          inst->seqNum, inst->effSeqNum, inst->effAddr, 0);
          full_mem_trace.push_back(full_trace_elem);
    }

    //Filter anything that aren't memory operations
    if (!(inst->isLoad() || inst->isStore() || inst->isAtomic())){
      return;
    }

    if (mode == OracleMode::Trace){
      //Push successfully committed instructions to buffers
      push_to_buffers(inst);
    } else if ((mode == OracleMode::Refine || mode == OracleMode::Barrier)
        && inst->sm_violator){
        //Generate mini trace entry if refining trace
        TraceUID tuid = TraceUID(inst->pcState().instAddr(),
          cpu->mem_dep_counter.sm_n_visited_at_prediction);

        //TraceUID tuid = TraceUID(inst->pcState().instAddr(),
        //cpu->mem_dep_counter.sm_n_visited_at_detection);

        TraceUID violator_uid = TraceUID(cpu->mem_dep_counter.sm_dep_pc,
          cpu->mem_dep_counter.sm_dep_n_visited);
        auto mini_trace_elem = mini_trace_T(tuid, violator_uid);
        mini_mem_trace.push_back(mini_trace_elem);

        DPRINTF(MemOracle,"MemOracle refine: %u:%u, seqnum %u, "
        "effadr %u , depends on: %u:%u\n", inst->pcState().instAddr(),
        inst->n_visited, inst->seqNum, inst->effAddr,
        cpu->mem_dep_counter.sm_dep_pc,
        cpu->mem_dep_counter.sm_dep_n_visited);

        //Generate full trace entry
        // auto full_trace_elem = full_trace_T(true, true, tuid, violator_uid,
        //   inst->seqNum, inst->effSeqNum, inst->effAddr);
        // full_mem_trace.push_back(full_trace_elem);

    }

    check_flush();
};

void MemOracle::push_to_buffers(const o3::DynInstPtr &inst){

    Addr effAddr = inst->effAddr;

    //Effsize in bytes
    uint64_t effSize = inst->effSize;
    Addr pc = inst->pcState().instAddr();
    uint64_t inst_n_visited = inst->n_visited;
    TraceUID tuid = TraceUID(pc, inst_n_visited);

   //Update last store to touch in fwd cache
    if (inst->isStore() || inst->isAtomic()){

      //Note of UID of touching inst with byte granularity
      for (uint64_t byteAddr = effAddr;
        byteAddr < effAddr + effSize; byteAddr++){
        fwd_cache[byteAddr] = tuid;
      }

      auto full_trace_elem = full_trace_T(false, false, tuid, TraceUID( 0, 0)
        , inst->seqNum, inst->effSeqNum, effAddr, effSize);
      full_mem_trace.push_back(full_trace_elem);

    }else if (inst->isLoad()){
      //Set to store all unique TraceUIDs across byte array
      std::set <TraceUID> dep_set;

      for (uint64_t byteAddr = effAddr;
        byteAddr < effAddr + effSize; byteAddr++){
        if (fwd_cache.count(byteAddr)){
          //Get last store that touched given memory
          TraceUID fwd_uid = fwd_cache[effAddr];
          dep_set.insert(fwd_uid);
        }
      }

      for (auto dep_uid : dep_set){
        //Generate mini trace entry
        auto mini_trace_elem = mini_trace_T(tuid, dep_uid);
        mini_mem_trace.push_back(mini_trace_elem);

        DPRINTF(MemOracle,"MemTracer commit dependent: %u:%u, seqnum %u,\
        effadr %u , depends on: %u:%u\n", pc, inst_n_visited, inst->seqNum,
          effAddr, dep_uid.pc, dep_uid.n_visited);
      }

      if (dep_set.size() > 0){
        //Generate full trace entry
        auto dep_uid = (*dep_set.begin());
        auto full_trace_elem = full_trace_T(true, true, tuid, dep_uid,
          inst->seqNum, inst->effSeqNum, effAddr, effSize);
        full_mem_trace.push_back(full_trace_elem);

      }else{
              //Generate full trace entry
        auto full_trace_elem = full_trace_T(true, false, tuid,
          TraceUID( 0, 0), inst->seqNum, inst->effSeqNum, effAddr, effSize);
        full_mem_trace.push_back(full_trace_elem);

        DPRINTF(MemOracle,"MemTracer commit new: %u:%u, seqnum %u,\
        effadr %u \n", pc, inst_n_visited, inst->seqNum, effAddr);
      }

    }
};

void MemOracle::flush_mini_buffer() {
  std::string out_buf;

  if (!mini_trace_f.good()) {
    printf("Failed to open mini_trace_file \n");
    return;
  }

  for (auto elem : mini_mem_trace) {

    //First character signifies barrier
    if (mode == OracleMode::Barrier){
      out_buf += 'B';
    }

    out_buf += std::to_string(std::get<0>(elem));
    out_buf += ',';
    out_buf += std::to_string(std::get<1>(elem));
    out_buf += '\n';
  }

  mini_trace_f << out_buf;
  mini_mem_trace.clear();
  out_buf.clear();
};

void MemOracle::flush_full_buffer() {

  std::string out_buf;

  if (mode != OracleMode::Trace){
    return;
  }

  if (!full_trace_f.good()) {
    printf("Failed to open full_trace_file \n");
    return;
  }

  for (auto elem : full_mem_trace) {

    out_buf += std::to_string(uint8_t(std::get<0>(elem)));
    out_buf += ',';
    out_buf += std::to_string(uint8_t(std::get<1>(elem)));
    out_buf += ',';
    out_buf += std::to_string(std::get<2>(elem));
    out_buf += ',';
    out_buf += std::to_string(std::get<3>(elem));
    out_buf += ',';
    out_buf += std::to_string(std::get<4>(elem));
    out_buf += ',';
    out_buf += std::to_string(std::get<5>(elem));
    out_buf += ',';
    out_buf += std::to_string(std::get<6>(elem));
    out_buf += '\n';
  }

  full_trace_f << out_buf;
  full_mem_trace.clear();
  out_buf.clear();
};

void MemOracle::check_flush(){
    if (full_mem_trace.size() > FLUSH_THRESHOLD) {
    flush_full_buffer();
    flush_mini_buffer();
    full_mem_trace.clear();
    mini_mem_trace.clear();
  }
}
void MemOracle::close_files(){

  if (mode == OracleMode::Trace){
    mini_trace_f.flush();
    full_trace_f.flush();
    mini_trace_f.reset();
    full_trace_f.reset();
  }else if (mode == OracleMode::Refine ||
    mode == OracleMode::Barrier){
      mini_trace_f.flush();
      mini_trace_f.reset();
    }
}

} // namespace gem5
