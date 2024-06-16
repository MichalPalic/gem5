/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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

#ifndef __CPU_O3_STORE_SET_HH__
#define __CPU_O3_STORE_SET_HH__

#include <list>
#include <map>
#include <utility>
#include <vector>

#include "base/types.hh"
#include "cpu/inst_seq.hh"

namespace gem5
{

namespace o3
{

struct ltseqnum
{
    bool
    operator()(const InstSeqNum &lhs, const InstSeqNum &rhs) const
    {
        return lhs > rhs;
    }
};

/**
 * Implements a store set predictor for determining if memory
 * instructions are dependent upon each other.  See paper "Memory
 * Dependence Prediction using Store Sets" by Chrysos and Emer.  SSID
 * stands for Store Set ID, SSIT stands for Store Set ID Table, and
 * LFST is Last Fetched Store Table.
 */
class StoreSet
{
  public:
    typedef unsigned SSID;

  public:
    /** Default constructor.  init() must be called prior to use. */
    StoreSet() { };

    /** Creates store set predictor with given table sizes. */
    StoreSet(uint64_t clear_period, int SSIT_size, int LFST_size,
      int _branch_hist_length);

    /** Default destructor. */
    ~StoreSet();

    /** Initializes the store set predictor with the given table sizes. */
    void init(uint64_t clear_period, int SSIT_size, int LFST_size,
      int _branch_hist_length);

    /** Records a memory ordering violation between the younger load
     * and the older store. */
    void violation(Addr store_PC, InstSeqNum store_seq_num, Addr load_PC,
      InstSeqNum load_seq_num);

    /** Clears the store set predictor every so often so that all the
     * entries aren't used and stores are constantly predicted as
     * conflicting.
     */
    void checkClear();

    /** Inserts a load into the store set predictor.  This does nothing but
     * is included in case other predictors require a similar function.
     */
    void insertLoad(Addr load_PC, InstSeqNum load_seq_num);

    /** Inserts a store into the store set predictor.  Updates the
     * LFST if the store has a valid SSID. */
    void insertStore(Addr store_PC, InstSeqNum store_seq_num, ThreadID tid);

    /** Checks if the instruction with the given PC is dependent upon
     * any store.  @return Returns the sequence number of the store
     * instruction this PC is dependent upon.  Returns 0 if none.
     */
    InstSeqNum checkInst(Addr PC, InstSeqNum seq_num);

    /** Records this PC/sequence number as issued. */
    void issued(Addr issued_PC, InstSeqNum issued_seq_num, bool is_store);

    /** Squashes for a specific thread until the given sequence number. */
    void squash(InstSeqNum squashed_num, ThreadID tid);

    /** Resets all tables. */
    void clear();

    /** Debug function to dump the contents of the store list. */
    void dump();

    /*Branch tracking circular buffers*/
    uint32_t branch_hist_length = 0;
    std::map<uint64_t, bool> global_branches;

    private:
    /** Calculates the index into the SSIT based on the PC. */
    inline int calcIndex(Addr PC)
    { return (PC >> offsetBits) & indexMask; }

    inline int calcIndexWBranch(Addr PC, InstSeqNum seq_num)
    {
    //Iterate over branch history and find the n last branches that came just
    //before sequence number of inst. Otherwise the signature changes at
    //runtime

    //Limited to 64bit size rn
    assert(branch_hist_length <= 64);

    uint64_t branch_state = 0;

    std::reverse_iterator<std::map<uint64_t,bool>::const_iterator> target_itr;
    //Start with the newest branch and iterate until the branch just before
    //our instruction is found
    for (target_itr = global_branches.crbegin();
      target_itr != global_branches.crend(); target_itr++){
        if ((*target_itr).first < seq_num){
          break;
        }
    }

    //Now fill a uint with branch outcomes
    //I want the most recent and thus strongest correlated branch to
    //spread entries as far as possible

    for (int i = 0; i < branch_hist_length; i++){
      if (target_itr != global_branches.crend()){
        branch_state |= (uint64_t((*target_itr).second) << i);
      }
    }

    //Shift so that relevant part of branch vetor is alligned with the upper
    //end of the relevevant bits for indexing

    uint32_t upper_shift = 32U - __builtin_clz(indexMask);
    branch_state = branch_state << upper_shift;

    uint32_t calculated_index = ((PC >> offsetBits)^ branch_state) & indexMask;
    return calculated_index;
    }

    /** Calculates a Store Set ID based on the PC. */
    inline SSID calcSSID(Addr PC)
    { return ((PC ^ (PC >> 10)) % LFSTSize); }

    /** The Store Set ID Table. */
    std::vector<SSID> SSIT;

    /** Bit vector to tell if the SSIT has a valid entry. */
    std::vector<bool> validSSIT;

    /** Last Fetched Store Table. */
    std::vector<InstSeqNum> LFST;

    /** Bit vector to tell if the LFST has a valid entry. */
    std::vector<bool> validLFST;

    /** Map of stores that have been inserted into the store set, but
     * not yet issued or squashed.
     */
    std::map<InstSeqNum, int, ltseqnum> storeList;

    typedef std::map<InstSeqNum, int, ltseqnum>::iterator SeqNumMapIt;

    /** Number of loads/stores to process before wiping predictor so all
     * entries don't get saturated
     */
    uint64_t clearPeriod;

    /** Store Set ID Table size, in entries. */
    int SSITSize;

    /** Last Fetched Store Table size, in entries. */
    int LFSTSize;

    /** Mask to obtain the index. */
    uint32_t indexMask;

    // HACK: Hardcoded for now.
    int offsetBits;

    /** Number of memory operations predicted since last clear of predictor */
    int memOpsPred;
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_STORE_SET_HH__
