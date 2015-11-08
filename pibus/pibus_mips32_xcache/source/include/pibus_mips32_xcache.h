//////////////////////////////////////////////////////////////////////////////////
// File: pibus_mips32_xcache.h  
// Author: Alain Greiner
// Date : 11/01/2010  
// Copyright UPMC/LIP6
// This program is released under the GNU public license
//////////////////////////////////////////////////////////////////////////////////        
// This component is a MIPS32 processor with two instruction & data caches, 
// connected to the system bus by a PIBUS interface.
// The processor is actually implemented as a cycle-accurate ISS.
// The cache controller contains two separated instruction and data caches, 
// sharing the same PIBUS interface, with the following parameters:
//     => The number of sets must be a power of 2 and no larger than 1024.
//     => The number of words per line must be a power of 2 and no larger than 32.
//     => The number of associative ways per set must be a power of 2 no larger than 8.
// It contains a write buffer implemented a simple FIFO. The FIFO depth is a parameter.
// All Pibus write transactions are single word.
// The data cache supports a snoop-invalidate mechanism.
//     
// INSTRUCTION CACHE
// The ICACHE is read only.
// In case of MISS, the processor is stalled, the missing address is
// stored in the ICACHE_MISS_ADDR register by the ICACHE controler,
// and a request is posted to the PIBUS controler.
// The missing cache line is written in the ICACHE_MISS_BUF[ICACHE_WORDS]
// buffer by the PIBUS controller, and the cache is updated by the ICACHE_FSM.
// In case of set_associative cache, the choice of the victim is pseudo-LRU.
// There is two types of transactions generated by ICACHE:
// - IMISS	=> generate a read burst on the bus
// - IUNC	=> generate aRn atomic read on the bus
//
// DATA CACHE 
// The write policy is WRITE-THROUGH: the data is always written 
// in the memory, and the cache is updated only in case of HIT.
// The DCACHE accepts non cachable segments : It decodes the MSB
// bits of the adress using a CACHED_TABLE ROM constructed from 
// the informations stored in the segment table.
// The DCACHE accepts a "line invalidate" command. The line defined by
// the Y field of the address is invalidated in case of HIT.
// In case of read MISS, or read UNCACHED, the processor is stalled. 
// The missing cache line is written in the DCACHE_MISS_BUF[DCACHE_WORDS]
// buffer by the PIBUS controller, and the cache is updated by the DCACHE_FSM.
// In case of set_associative cache, the choice of the victim is pseudo-LRU.
// The DCACHE controller communicates with the PIBUS controler through
// through various request flip-flops and a FIFO acting as a write buffer.
// There is four types of transactions generated by DCACHE:
// - DMISS      => generate a read burst on the bus
// - DUNC 	=> generate an atomic read on the bus
// - WRITE   	=> generate an atomic write on the bus
// - SC      	=> generate an atomic write on the bus
// 
// A processor request is refused (i.e. DCACHE.MISS = true)
// if there is a READ MISS, a READ UNCACHED, or a WRITE with FIFO full.
//
// BUS ERRORS
// For the read transactions (both instruction and data), the processor is
// stalled, and a bus error can be precisely signaled, using the ICACHE.BERR
// or DCACHE.BERR signals.
// In case of write transactions, bus errors cannot be signaled accurately because i
// the write  requests are handled "asynchronously" by the PIBUS_FSM
// (Write Buffer mechanism). A bus error on a write request will be signaled 
// at the next data read transaction, using the DCACHE.BERR signal, 
// and a dedicated BUS_ERROR flip-flop to keep the error information.
//
// SNOOP
// The Data cache supports an optionnal SNOOP mechanism : The SNOOP_FSM snoops
// the bus to detect external write requests. In case of "external hit",
// the corresponding cache line is invalidated. If there is too many
// successive external hits, the cache is flushed.
// 
// LL/LC
// The Data cache supports cachable LL/SC requests, using the
// general snoop cache coherence mechanism.
// - for a LL request, the r_llsc_pending flip-flop is set, and
// the corresponding address is stored in the r_llsc_address register.
// It is then handled as a normal LW request, including a DMISS transaction
// on the bus in case of miss.
// - A SC request is a success if : r_llsc_pending && (r_llsc_address == ad)
// The r_llsc_pending flip-flop is reset the SC is handled as a SW transaction
// on the bus in case of success.
// - All write requests on the bus are monitored, and the r_llsc_pending
// flip-flop is reset in case of external hit.
//
// This component contains 4 FSMs :
// - DCACHE_FSM controls the DCACHE interface.
// - ICACHE_FSM controls the ICACHE interface.
// - PIBUS_FSM controls the PIBUS interface. 
// - SNOOP_FSM controls the snoop-invalidate mechanism.
//
// INSTRUMENTATION
// Six counters can be used for instrumentation :
// 1) IMISS_COUNTER : Number of Instruction MISS transactions
// 2) DMISS_COUNTER : Number of Cached Read MISS transactions
// 3) UNC_COUNTER   : Number of	Uncached Read transactions 
// 4) WRITE_COUNTER : Number of Write transactions
// 5) IREQ_COUNTER  : Total number of Instruction Read requests
// 6) DREQ_COUNTER  : Total number of Cached Read requests	
// The Dcache Miss Rate can be computed as DMISS_COUNTER / DREQ_COUNTER
// The Icache Miss Rate can be computed as IMISS_COUNTER / IREQ_COUNTER
//
/////////////////////////////////////////////////////////////////////////////// 
// This component has 11 "constructor" parameters
// - sc_module_name 	name		: instance name
// - pibusSegmentTable 	segtab 		: segment table
// - uint32_t		proc_id		: processor identifier
// - uint32_t		icache_sets	: number of sets (icache)
// - uint32_t		icache_ways 	: number of associative ways (icache)
// - uint32_t		icache_words 	: number of words per line (icache)
// - uint32_t		dcache_ways 	: number of sets (dcache)
// - uint32_t		dcache_sets  	: number of associative ways (dcache)
// - uint32_t		dcache_words 	: number of words per line (dcache)
// - uint32_t		wbuf_depth   	: write buffer depth 
// - bool		snoop_active    : default value is true
//////////////////////////////////////////////////////////////////////////////

#ifndef PIBUS_MIPS32_XCACHE_H
#define PIBUS_MIPS32_XCACHE_H

#include <systemc>
#include "pibus_segment_table.h"
#include "pibus_mnemonics.h"
#include "generic_fifo.h"
#include "generic_cache.h"
#include "mips32.h"
#include "iss2.h"
#include "gdbserver.h"

namespace soclib { namespace caba {

using namespace sc_core;
using namespace soclib::common;
using namespace soclib::caba;

/////////////////////////////////////
class PibusMips32Xcache : sc_module {

    // structural parameters 
    const char*			m_name;
    const bool*			m_cached_table;
    const uint32_t		m_icache_sets;
    const uint32_t		m_icache_words;
    const uint32_t		m_icache_ways;
    const uint32_t		m_dcache_sets;
    const uint32_t		m_dcache_words;
    const uint32_t		m_dcache_ways;
    const uint32_t		m_msb_shift;
    const uint32_t		m_msb_mask;
    const bool			m_snoop_active;
    uint32_t			m_line_data_mask;
    uint32_t			m_line_inst_mask;

    char			m_dcache_fsm_str[12][20];
    char			m_icache_fsm_str[8][20];
    char			m_pibus_fsm_str[8][20];

    Iss2::InstructionRequest 	m_ireq;
    Iss2::InstructionResponse 	m_irsp;
    Iss2::DataRequest 		m_dreq;
    Iss2::DataResponse 		m_drsp;

    // processor
    GdbServer<Mips32ElIss>	r_proc;

    // cache registers
    sc_register<int>		r_dcache_fsm;		  // DCACHE FSM state
    sc_register<uint32_t>	r_dcache_save_addr;
    sc_register<uint32_t>	r_dcache_save_way;
    sc_register<uint32_t>	r_dcache_save_set;
    sc_register<uint32_t>	r_dcache_save_word;
    sc_register<uint32_t>	r_dcache_save_wdata;
    sc_register<uint32_t>	r_dcache_save_type;  
    sc_register<uint32_t>	r_dcache_save_be;  
    sc_register<bool>		r_dcache_save_cached;  
    sc_register<uint32_t>	r_dcache_save_rdata;
    sc_register<bool>		r_dcache_miss_req;  	  // request to Pibus FSM
    sc_register<bool>		r_dcache_unc_req;  	  // request to Pibus FSM
    sc_register<bool>		r_dcache_sc_req;  	  // request to Pibus FSM
    sc_register<bool>		r_llsc_pending;		  // LL reservation
    sc_register<uint32_t>	r_llsc_addr;		  // LL/SC address
  
    sc_register<int>		r_icache_fsm;		  // ICACHE FSM state
    sc_register<uint32_t>	r_icache_save_addr;  
    sc_register<uint32_t>	r_icache_save_way;
    sc_register<uint32_t>	r_icache_save_set;
    sc_register<bool>		r_icache_miss_req;  	  // request to Pibus FSM
    sc_register<bool>		r_icache_unc_req;  	  // request to Pibus FSM

    sc_register<int>		r_pibus_fsm;		  // PIBUS FSM state
    sc_register<uint32_t>	r_pibus_wcount;		  // word counter 
    sc_register<bool>		r_pibus_ins;		  // instruction request when true
    sc_register<uint32_t>	r_pibus_addr; 		  // base address
    sc_register<uint32_t>	r_pibus_wdata;		  // written data
    sc_register<uint32_t>	r_pibus_opc;		  // transaction opc
    sc_register<bool>		r_pibus_rsp_ok;		  // transaction completed : success
    sc_register<bool>		r_pibus_rsp_error;	  // transaction completed : error  
    uint32_t			r_pibus_buf[32];	  // data buffer 

    sc_register<bool>           r_snoop_dcache_inval_req; // dcache slot must be invalidated
    sc_register<uint32_t>	r_snoop_dcache_inval_way; // way to be invalidated
    sc_register<uint32_t>	r_snoop_dcache_inval_set; // set to be invalidated
    sc_register<bool>    	r_snoop_llsc_inval_req;	  // llsc reservation must be invalidated
    sc_register<bool>    	r_snoop_flush_req;        // panic: both dcache and llsc flush
    sc_register<uint32_t>	r_snoop_address_save;     // previous external hit address
   

    // Fifos implementing the write buffer
    GenericFifo<uint32_t>      	r_wbuf_data;
    GenericFifo<uint32_t>      	r_wbuf_addr;
    GenericFifo<uint32_t>      	r_wbuf_type;
   
    // caches
    soclib::GenericCache<uint32_t>	r_icache;
    soclib::GenericCache<uint32_t>	r_dcache;

    // Intrumentation counters
    uint32_t			c_total_cycles;
    uint32_t			c_frz_cycles;
    uint32_t			c_imiss_count;
    uint32_t			c_imiss_frz;
    uint32_t			c_iunc_count;
    uint32_t			c_iunc_frz;
    uint32_t			c_dread_count;
    uint32_t			c_dmiss_count;
    uint32_t			c_dmiss_frz;
    uint32_t			c_dunc_count;
    uint32_t			c_dunc_frz;
    uint32_t			c_write_count;
    uint32_t			c_write_frz;
    uint32_t			c_sc_ok_count;
    uint32_t			c_sc_ko_count;

    // DCACHE_FSM STATES
    enum{
	DCACHE_IDLE,
	DCACHE_WRITE_UPDT,
	DCACHE_WRITE_REQ,
        DCACHE_MISS_SELECT,
        DCACHE_MISS_INVAL,
	DCACHE_MISS_WAIT,
	DCACHE_MISS_UPDT,
	DCACHE_UNC_WAIT,
	DCACHE_UNC_GO,
	DCACHE_ERROR,
        DCACHE_INVAL,
        DCACHE_SC_WAIT,
    };

    // ICACHE_FSM STATES
    enum{
	ICACHE_IDLE,
        ICACHE_MISS_SELECT,
        ICACHE_MISS_INVAL,
	ICACHE_MISS_WAIT,
	ICACHE_MISS_UPDT,
        ICACHE_UNC_WAIT,
        ICACHE_UNC_GO,
	ICACHE_ERROR,
    };

    // PIBUS_FSM STATES
    enum{
	PIBUS_IDLE,
	PIBUS_READ_REQ,
	PIBUS_READ_AD,
	PIBUS_READ_DTAD,
	PIBUS_READ_DT,
	PIBUS_WRITE_REQ,
	PIBUS_WRITE_AD,
	PIBUS_WRITE_DT,
    };
	
    // SNOOP_FSM STATES
    enum{
	SNOOP_IDLE,
	SNOOP_INVAL,
	SNOOP_FLUSH,
    };

protected:

    SC_HAS_PROCESS(PibusMips32Xcache);

public:

    // PORTS
    sc_in<bool>			p_ck;
    sc_in<bool>			p_resetn;
    sc_in<bool>			p_irq;
    sc_out<bool>		p_req;
    sc_in<bool>			p_gnt;
    sc_out<bool>		p_lock;
    sc_out<bool>		p_read;
    sc_out<uint32_t>		p_opc;
    sc_inout<uint32_t>		p_a;
    sc_inout<uint32_t>		p_d;
    sc_in<uint32_t>		p_ack;
    sc_in<bool>			p_tout;
    sc_in<bool>			p_avalid;

    //  constructor
    PibusMips32Xcache (sc_module_name 		name, 		// instance name
			PibusSegmentTable 	&segtab,	// segment table for cacheability
                	uint32_t		proc_id,	// processor identifier
			uint32_t		icache_ways,	// number of associative ways per set
			uint32_t		icache_sets,	// number of dcache sets
			uint32_t		icache_words,	// number of words per line
			uint32_t		dcache_ways, 	// number of associative ways per set
			uint32_t		dcache_sets,	// number of icache sets
			uint32_t		dcache_words,	// number of words per line
                	uint32_t		fifo_depth,	// write buffer depth
			bool		snoop_active = true);	// snoop activation 

    ~PibusMips32Xcache ();

    // methods
    void transition();
    void genMoore();
    void printStatistics();
    void printTrace();

}; // end structure PibusMips32Xcache
 
}} // end namespaces

#endif 

