# CPU configuration: Luke XL
#--l1d_size=256KiB --l1i_size=256KiB --l2_size=4MB

from m5.defines import buildEnv
from m5.params import *
from m5.proxy import *

from m5.objects.BaseCPU import BaseCPU
from m5.objects.FUPool import *
#from m5.objects.O3Checker import O3Checker
from m5.objects.BranchPredictor import *

class SMTFetchPolicy(ScopedEnum):
    vals = [ 'RoundRobin', 'Branch', 'IQCount', 'LSQCount' ]

class SMTQueuePolicy(ScopedEnum):
    vals = [ 'Dynamic', 'Partitioned', 'Threshold' ]

class CommitPolicy(ScopedEnum):
    vals = [ 'RoundRobin', 'OldestReady' ]

class BaseO3CPU(BaseCPU):
    type = 'BaseO3CPU'
    cxx_class = 'gem5::o3::CPU'
    cxx_header = 'cpu/o3/dyn_inst.hh'

    @classmethod
    def memory_mode(cls):
        return 'timing'

    @classmethod
    def require_caches(cls):
        return True

    @classmethod
    def support_take_over(cls):
        return True

    activity = Param.Unsigned(0, "Initial count")

    cacheStorePorts = Param.Unsigned(200, "Cache Ports. "
          "Constrains stores only.")
    cacheLoadPorts = Param.Unsigned(200, "Cache Ports. "
          "Constrains loads only.")

    decodeToFetchDelay = Param.Cycles(1, "Decode to fetch delay")
    renameToFetchDelay = Param.Cycles(1 ,"Rename to fetch delay")
    iewToFetchDelay = Param.Cycles(1, "Issue/Execute/Writeback to fetch "
                                   "delay")
    commitToFetchDelay = Param.Cycles(1, "Commit to fetch delay")
    fetchWidth = Param.Unsigned(12, "Fetch width")
    fetchBufferSize = Param.Unsigned(64, "Fetch buffer size in bytes")
    fetchQueueSize = Param.Unsigned(32, "Fetch queue size in micro-ops "
                                    "per-thread")

    renameToDecodeDelay = Param.Cycles(1, "Rename to decode delay")
    iewToDecodeDelay = Param.Cycles(1, "Issue/Execute/Writeback to decode "
                                    "delay")
    commitToDecodeDelay = Param.Cycles(1, "Commit to decode delay")
    fetchToDecodeDelay = Param.Cycles(1, "Fetch to decode delay")
    decodeWidth = Param.Unsigned(12, "Decode width")

    iewToRenameDelay = Param.Cycles(1, "Issue/Execute/Writeback to rename "
                                    "delay")
    commitToRenameDelay = Param.Cycles(1, "Commit to rename delay")
    decodeToRenameDelay = Param.Cycles(1, "Decode to rename delay")
    renameWidth = Param.Unsigned(12, "Rename width")

    commitToIEWDelay = Param.Cycles(1, "Commit to "
               "Issue/Execute/Writeback delay")
    renameToIEWDelay = Param.Cycles(2, "Rename to "
               "Issue/Execute/Writeback delay")
    issueToExecuteDelay = Param.Cycles(1, "Issue to execute delay (internal "
              "to the IEW stage)")
    dispatchWidth = Param.Unsigned(12, "Dispatch width")
    issueWidth = Param.Unsigned(12, "Issue width")
    wbWidth = Param.Unsigned(12, "Writeback width")
    fuPool = Param.FUPool(DefaultFUPool(), "Functional Unit pool")

    iewToCommitDelay = Param.Cycles(1, "Issue/Execute/Writeback to commit "
               "delay")
    renameToROBDelay = Param.Cycles(1, "Rename to reorder buffer delay")
    commitWidth = Param.Unsigned(12, "Commit width")
    squashWidth = Param.Unsigned(12, "Squash width")
    trapLatency = Param.Cycles(13, "Trap latency")
    fetchTrapLatency = Param.Cycles(1, "Fetch trap latency")

    backComSize = Param.Unsigned(5,
            "Time buffer size for backwards communication")
    forwardComSize = Param.Unsigned(5,
            "Time buffer size for forward communication")

    LQEntries = Param.Unsigned(48, "Number of load queue entries")
    SQEntries = Param.Unsigned(48, "Number of store queue entries")
    LSQDepCheckShift = Param.Unsigned(4,
            "Number of places to shift addr before check")
    LSQCheckLoads = Param.Bool(True,
        "Should dependency violations be checked for "
        "loads & stores or just stores")
    store_set_clear_period = Param.Unsigned(62464,
            "Number of load/store insts before the dep predictor "
            "should be invalidated")
    LFSTSize = Param.Unsigned(192, "Last fetched store table size")
    SSITSize = Param.Unsigned(192, "Store set ID table size")

    numRobs = Param.Unsigned(1, "Number of Reorder Buffers");

    numPhysIntRegs = Param.Unsigned(256,
            "Number of physical integer registers")
    numPhysFloatRegs = Param.Unsigned(256, "Number of physical floating point "
                                      "registers")
    numPhysVecRegs = Param.Unsigned(256, "Number of physical vector "
                                      "registers")
    numPhysVecPredRegs = Param.Unsigned(32, "Number of physical predicate "
                                      "registers")
    # most ISAs don't use condition-code regs, so default is 0
    numPhysCCRegs = Param.Unsigned(0, "Number of physical cc registers")
    numIQEntries = Param.Unsigned(384, "Number of instruction queue entries")
    numROBEntries = Param.Unsigned(1024, "Number of reorder buffer entries")

    smtNumFetchingThreads = Param.Unsigned(1, "SMT Number of Fetching Threads")
    smtFetchPolicy = Param.SMTFetchPolicy('RoundRobin', "SMT Fetch policy")
    smtLSQPolicy    = Param.SMTQueuePolicy('Partitioned',
                                           "SMT LSQ Sharing Policy")
    smtLSQThreshold = Param.Int(100, "SMT LSQ Threshold Sharing Parameter")
    smtIQPolicy    = Param.SMTQueuePolicy('Partitioned',
                                          "SMT IQ Sharing Policy")
    smtIQThreshold = Param.Int(100, "SMT IQ Threshold Sharing Parameter")
    smtROBPolicy   = Param.SMTQueuePolicy('Partitioned',
                                          "SMT ROB Sharing Policy")
    smtROBThreshold = Param.Int(100, "SMT ROB Threshold Sharing Parameter")
    smtCommitPolicy = Param.CommitPolicy('RoundRobin', "SMT Commit Policy")

    branchPred = Param.BranchPredictor(TournamentBP(numThreads =
                                                       Parent.numThreads),
                                       "Branch Predictor")
    needsTSO = Param.Bool(False, "Enable TSO Memory model")
