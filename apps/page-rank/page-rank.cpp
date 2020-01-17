#include <cassert>
#include <cstring>

#include <algorithm>

#include <tlp.h>

#include "page-rank-kernel.h"

constexpr int kMaxNumPartitions = 2048;
constexpr int kMaxPartitionSize = 1024 * 32;

void Control(Pid num_partitions, tlp::mmap<const Vid> num_vertices,
             tlp::mmap<const Eid> num_edges,
             // to UpdateHandler
             tlp::ostream<Eid>& update_config_q0,
             tlp::ostream<Eid>& update_config_q1,
             tlp::ostream<bool>& update_phase_q0,
             tlp::ostream<bool>& update_phase_q1,
             // from UpdateHandler
             tlp::istream<NumUpdates>& num_updates_q0,
             tlp::istream<NumUpdates>& num_updates_q1,
             // to ProcElem
             tlp::ostream<TaskReq>& task_req_q0,
             tlp::ostream<TaskReq>& task_req_q1,
             // from ProcElem
             tlp::istream<TaskResp>& task_resp_q0,
             tlp::istream<TaskResp>& task_resp_q1) {
  // HLS crashes without this...
  update_config_q0.close();
  update_config_q1.close();
  update_phase_q0.close();
  update_phase_q1.close();
  task_req_q0.close();
  task_req_q1.close();

  // Keeps track of all partitions.
  // Vid of the 0-th vertex in each partition.
  Vid base_vids[kMaxNumPartitions];
  // Number of vertices in each partition.
  Vid num_vertices_local[kMaxNumPartitions];
  // Number of edges in each partition.
  Eid num_edges_local[kMaxNumPartitions];
  // Number of updates in each partition.
  Eid num_updates_local[kMaxNumPartitions];
  // Memory offset of the 0-th vertex in each partition.
  Vid vid_offsets[kMaxNumPartitions];
  // Memory offset of the 0-th edge in each partition.
  Eid eid_offsets[kMaxNumPartitions];

  Vid base_vid_acc = num_vertices[0];
  Vid vid_offset_acc = 0;
  Eid eid_offset_acc = 0;
load_config:
  for (Pid pid = 0; pid < num_partitions; ++pid) {
#pragma HLS pipeline II = 1
    Vid num_vertices_delta = num_vertices[pid + 1];
    Eid num_edges_delta = num_edges[pid];

    base_vids[pid] = base_vid_acc;
    num_vertices_local[pid] = num_vertices_delta;
    num_edges_local[pid] = num_edges_delta;
    vid_offsets[pid] = vid_offset_acc;
    eid_offsets[pid] = eid_offset_acc;

    base_vid_acc += num_vertices_delta;
    vid_offset_acc += num_vertices_delta;
    eid_offset_acc += num_edges_delta;
  }
  const auto parition_size = num_vertices_local[0];
  const auto overall_base_vid = base_vids[0];
  const Vid total_num_vertices = base_vid_acc - overall_base_vid;

  // Initialize UpdateMem, needed only once per execution.
config_update_offsets:
  for (Pid pid = 0; pid < num_partitions; ++pid) {
    VLOG_F(8, info) << "eid offset[" << pid
                    << "]: " << num_edges[num_partitions + pid];
    update_config_q0.write(num_edges[num_partitions + pid]);
    update_config_q1.write(num_edges[num_partitions + pid]);
  }
  // Tells UpdateHandler start to wait for phase requests.
  update_config_q0.close();
  update_config_q1.close();

  bool all_done = false;
bulk_steps:
  while (!all_done) {
    all_done = true;

    // Do the scatter phase for each partition, if active.
    // Wait until all PEs are done with the scatter phase.
    VLOG_F(5, info) << "Phase: " << TaskReq::kScatter;
    update_phase_q0.write(false);
    update_phase_q1.write(false);

  scatter:
    for (Pid pid_send = 0, pid_recv = 0; pid_recv < num_partitions;) {
      if (pid_send < num_partitions) {
        TaskReq req{TaskReq::kScatter,
                    pid_send,
                    overall_base_vid,
                    base_vids[pid_send],
                    parition_size,
                    num_vertices_local[pid_send],
                    num_edges_local[pid_send],
                    vid_offsets[pid_send],
                    eid_offsets[pid_send],
                    0.f,
                    false};
        switch (pid_send & 1) {
          case 0:
            if (task_req_q0.try_write(req) || task_req_q1.try_write(req)) {
              ++pid_send;
            }
            break;
          case 1:
            if (task_req_q1.try_write(req) || task_req_q0.try_write(req)) {
              ++pid_send;
            }
            break;
        }
      }
      TaskResp resp;
      if (task_resp_q0.try_read(resp)) {
        assert(resp.phase == TaskReq::kScatter);
        ++pid_recv;
      }
      if (task_resp_q1.try_read(resp)) {
        assert(resp.phase == TaskReq::kScatter);
        ++pid_recv;
      }
    }

    // Tell PEs to tell UpdateHandlers that the scatter phase is done.
    TaskReq req = {};
    req.scatter_done = true;
    task_req_q0.write(req);
    task_req_q1.write(req);

    // Get prepared for the gather phase.
    VLOG_F(5, info) << "Phase: " << TaskReq::kGather;
    update_phase_q0.write(true);
    update_phase_q1.write(true);

  reset_num_updates:
    for (Pid pid = 0; pid < num_partitions; ++pid) {
#pragma HLS pipeline II = 1
      num_updates_local[pid] = 0;
    }

  collect_num_updates:
    for (Pid pid_recv = 0; pid_recv < num_partitions * 2;) {
      // no two UpdateHandler shall return the same pid with num_updates > 0
#pragma HLS dependence false variable = num_updates_local
      NumUpdates num_updates;
      if (num_updates_q0.try_read(num_updates)) {
        VLOG_F(5, recv) << "num_updates: " << num_updates;
        if (num_updates.num_updates > 0) {
          num_updates_local[num_updates.pid] += num_updates.num_updates;
        }
        ++pid_recv;
      } else if (num_updates_q1.try_read(num_updates)) {
        VLOG_F(5, recv) << "num_updates: " << num_updates;
        if (num_updates.num_updates > 0) {
          num_updates_local[num_updates.pid] += num_updates.num_updates;
        }
        ++pid_recv;
      }
    }

    // updates.fence()
#ifdef __SYNTHESIS__
    ap_wait_n(80);
#endif

    // Do the gather phase for each partition.
    // Wait until all partitions are done with the gather phase.
  gather:
    for (Pid pid_send = 0, pid_recv = 0; pid_recv < num_partitions;) {
      if (pid_send < num_partitions) {
        TaskReq req{TaskReq::kGather,
                    pid_send,
                    overall_base_vid,
                    base_vids[pid_send],
                    num_vertices_local[0],
                    num_vertices_local[pid_send],
                    num_updates_local[pid_send],
                    vid_offsets[pid_send],
                    eid_offsets[pid_send],
                    (1.f - kDampingFactor) / total_num_vertices,
                    false};
        switch (pid_send & 1) {
          case 0:
            if (task_req_q0.try_write(req) || task_req_q1.try_write(req)) {
              ++pid_send;
            }
            break;
          case 1:
            if (task_req_q1.try_write(req) || task_req_q0.try_write(req)) {
              ++pid_send;
            }
            break;
        }
      }

      TaskResp resp;
      if (task_resp_q0.try_read(resp)) {
        assert(resp.phase == TaskReq::kGather);
        VLOG_F(3, recv) << resp;
        if (resp.active) all_done = false;
        ++pid_recv;
      }
      if (task_resp_q1.try_read(resp)) {
        assert(resp.phase == TaskReq::kGather);
        VLOG_F(3, recv) << resp;
        if (resp.active) all_done = false;
        ++pid_recv;
      }
    }
    VLOG_F(3, info) << (all_done ? "" : "not ") << "all done";
  }
  // Terminates UpdateHandler.
  update_phase_q0.close();
  update_phase_q1.close();
  task_req_q0.close();
  task_req_q1.close();
}

void VertexMem(tlp::istream<VertexReq>& vertex_req_q0,
               tlp::istream<VertexReq>& vertex_req_q1,
               tlp::istream<VertexAttr>& vertex_in_q0,
               tlp::istream<VertexAttr>& vertex_in_q1,
               tlp::ostream<VertexAttr>& vertex_out_q0,
               tlp::ostream<VertexAttr>& vertex_out_q1,
               tlp::mmap<VertexAttrAligned> vertices) {
  for (;;) {
    VertexReq req;
    if (vertex_req_q0.try_read(req)) {
      if (req.phase == TaskReq::kScatter) {
        // Read vertices from DRAM.
      read_vertices_0:
        for (Vid i = 0; i < req.length; ++i) {
          vertex_out_q0.write(vertices[req.offset + i]);
        }
      } else {
        // Write vertices to DRAM.
      write_vertices_0:
        for (Vid i = 0; i < req.length; ++i) {
          vertices[req.offset + i] = vertex_in_q0.read();
        }
      }
    } else if (vertex_req_q1.try_read(req)) {
      if (req.phase == TaskReq::kScatter) {
        // Read vertices from DRAM.
      read_vertices_1:
        for (Vid i = 0; i < req.length; ++i) {
          vertex_out_q1.write(vertices[req.offset + i]);
        }
      } else {
        // Write vertices to DRAM.
      write_vertices_1:
        for (Vid i = 0; i < req.length; ++i) {
          vertices[req.offset + i] = vertex_in_q1.read();
        }
      }
    }
  }
}

// Handles edge read requests.
void EdgeMem(tlp::istream<Eid>& edge_req_q0, tlp::istream<Eid>& edge_req_q1,
             tlp::ostream<Edge>& edge_resp_q0, tlp::ostream<Edge>& edge_resp_q1,
             tlp::async_mmap<tlp::vec_t<Edge, kEdgeVecLen>> edges) {
  constexpr int kMaxOutstanding = 64;
  uint8_t dst[kMaxOutstanding];
#pragma HLS array_partition complete variable = dst
  uint8_t dst_rd = 0;
  uint8_t dst_wr = 0;
  // dst is empty if dst_rd == dst_wr
  // dst is full if dst_rd == dst_wr + 1 (mod kMaxOutstanding)
  uint8_t leftover[2] = {};
#pragma HLS array_partition complete variable = leftover
  tlp::vec_t<Edge, kEdgeVecLen> edge_v[2];
#pragma HLS array_partition complete variable = edge_v
  uint8_t priority = 0;
  for (;;) {
    const auto dst_rd_copy = dst_rd;
    const uint8_t dst_wr_next = dst_wr == kMaxOutstanding - 1 ? 0 : dst_wr + 1;

    // Try to read from memory.
    const uint8_t dst_id = dst[dst_rd_copy];
    if (leftover[dst_id] == 0 && edges.read_data_try_read(edge_v[dst_id])) {
      dst_rd = dst_rd == kMaxOutstanding - 1 ? 0 : dst_rd + 1;
      leftover[dst_id] = kEdgeVecLen;
    }

    // If outstanding pool is not full, send new requests.
    if (dst_rd_copy != dst_wr_next) {
      bool valid[2] = {};
      const Eid eid[] = {
          edge_req_q0.peek(valid[0]),
          edge_req_q1.peek(valid[1]),
      };
      const auto idx = Arbitrate(valid, priority >> 2);
      if (Any(valid) && edges.read_addr_try_write(eid[idx])) {
        switch (idx) {
          case 0:
            edge_req_q0.read(nullptr);
            break;
          case 1:
            edge_req_q1.read(nullptr);
            break;
        }
        ++priority;
        dst[dst_wr] = idx;
        dst_wr = dst_wr_next;
      }
    }

    // Flush leftovers.
    if (leftover[0] != 0 &&
        edge_resp_q0.try_write(edge_v[0][kEdgeVecLen - leftover[0]])) {
      --leftover[0];
    }
    if (leftover[1] != 0 &&
        edge_resp_q1.try_write(edge_v[1][kEdgeVecLen - leftover[1]])) {
      --leftover[1];
    }
  }
}

void UpdateMem(tlp::istream<uint64_t>& read_addr_q0,
               tlp::istream<uint64_t>& read_addr_q1,
               tlp::ostream<tlp::vec_t<Update, kUpdateVecLen>>& read_data_q0,
               tlp::ostream<tlp::vec_t<Update, kUpdateVecLen>>& read_data_q1,
               tlp::istream<uint64_t>& write_addr_q0,
               tlp::istream<uint64_t>& write_addr_q1,
               tlp::istream<tlp::vec_t<Update, kUpdateVecLen>>& write_data_q0,
               tlp::istream<tlp::vec_t<Update, kUpdateVecLen>>& write_data_q1,
               tlp::async_mmap<tlp::vec_t<Update, kUpdateVecLen>> updates) {
  constexpr int kMaxOutstanding = 64;
  uint8_t dst[kMaxOutstanding];
#pragma HLS array_partition complete variable = dst
  uint8_t dst_rd = 0;
  uint8_t dst_wr = 0;
  // dst is empty if dst_rd == dst_wr
  // dst is full if dst_rd == dst_wr + 1 (mod kMaxOutstanding)
  bool update_valid = false;
  tlp::vec_t<Update, kUpdateVecLen> update_v;
  uint8_t priority_rd = 0;
  uint8_t priority_wr = 0;
  for (;;) {
    const auto dst_rd_copy = dst_rd;
    const uint8_t dst_wr_next = dst_wr == kMaxOutstanding - 1 ? 0 : dst_wr + 1;

    // Handle read data.
    if (!update_valid) update_valid = updates.read_data_try_read(update_v);
    if (update_valid) {
      bool read = false;
      switch (dst[dst_rd]) {
        case 0:
          read = read_data_q0.try_write(update_v);
          break;
        case 1:
          read = read_data_q1.try_write(update_v);
          break;
      }
      if (read) {
        dst_rd = dst_rd == kMaxOutstanding - 1 ? 0 : dst_rd + 1;
        update_valid = false;
      }
    }

    // Handle read addr.
    if (dst_rd_copy != dst_wr_next) {
      bool valid[2] = {};
      const uint64_t addr[] = {
          read_addr_q0.peek(valid[0]),
          read_addr_q1.peek(valid[1]),
      };
      const auto idx = Arbitrate(valid, priority_rd >> 2);
      if (Any(valid) && updates.read_addr_try_write(addr[idx])) {
        switch (idx) {
          case 0:
            read_addr_q0.read(nullptr);
            break;
          case 1:
            read_addr_q1.read(nullptr);
            break;
        }
        ++priority_rd;
        dst[dst_wr] = idx;
        dst_wr = dst_wr_next;
      }
    }

    // Handle write addr and data.
    bool addr_valid[2] = {};
    bool data_valid[2] = {};
    const uint64_t addr[] = {
        write_addr_q0.peek(addr_valid[0]),
        write_addr_q1.peek(addr_valid[1]),
    };
    const tlp::vec_t<Update, kUpdateVecLen> data[] = {
        write_data_q0.peek(data_valid[0]),
        write_data_q1.peek(data_valid[1]),
    };

    // Adding const confuses HLS.
    bool valid[] = {
        addr_valid[0] && data_valid[0],
        addr_valid[1] && data_valid[1],
    };
    const auto idx = Arbitrate(valid, priority_wr >> 2);
    if (Any(valid) && updates.write_addr_try_write(addr[idx])) {
      switch (idx) {
        case 0:
          write_addr_q0.read(nullptr);
          write_data_q0.read(nullptr);
          break;
        case 1:
          write_addr_q1.read(nullptr);
          write_data_q1.read(nullptr);
          break;
      }
      ++priority_wr;
      updates.write_data_write(data[idx]);
    }
  }
}

void UpdateRouter(tlp::istream<UpdateWithPid>& update_in_q0,
                  tlp::istream<UpdateWithPid>& update_in_q1,
                  tlp::ostream<UpdateWithPid>& update_out_q0,
                  tlp::ostream<UpdateWithPid>& update_out_q1) {
  constexpr auto bit = 0;

  uint8_t priority = 0;
  for (bool eos_0, eos_1, valid_0, valid_1;;) {
    auto update_0 = update_in_q0.peek(valid_0, eos_0);
    auto update_1 = update_in_q1.peek(valid_1, eos_1);
    if (eos_0 && eos_1) {
      update_out_q0.close();
      update_out_q1.close();
      update_in_q0.try_open();
      update_in_q1.try_open();
    } else {
      bool fwd_0_0 = valid_0 && !eos_0 && (update_0.pid & 1 << bit) == 0;
      bool fwd_0_1 = valid_0 && !eos_0 && (update_0.pid & 1 << bit) != 0;
      bool fwd_1_0 = valid_1 && !eos_1 && (update_1.pid & 1 << bit) == 0;
      bool fwd_1_1 = valid_1 && !eos_1 && (update_1.pid & 1 << bit) != 0;

      bool conflict = (valid_0 && !eos_0) && (valid_1 && !eos_1) &&
                      fwd_0_0 == fwd_1_0 && fwd_0_1 == fwd_1_1;
      bool prioritize_1 = priority & 4;

      bool read_0 = !((!fwd_0_0 && !fwd_0_1) || (prioritize_1 && conflict));
      bool read_1 = !((!fwd_1_0 && !fwd_1_1) || (!prioritize_1 && conflict));
      bool write_0 = fwd_0_0 || fwd_1_0;
      bool write_1 = fwd_1_1 || fwd_0_1;
      bool write_0_0 = fwd_0_0 && (!fwd_1_0 || !prioritize_1);
      bool write_1_1 = fwd_1_1 && (!fwd_0_1 || prioritize_1);

      // if can forward through (0->0 or 1->1), do it
      // otherwise, check for conflict
      bool written_0 = false;
      bool written_1 = false;
      if (write_0) {
        written_0 = update_out_q0.try_write(write_0_0 ? update_0 : update_1);
      }
      if (write_1) {
        written_1 = update_out_q1.try_write(write_1_1 ? update_1 : update_0);
      }

      // if can forward through (0->0 or 1->1), do it
      // otherwise, round robin priority of both inputs every 4 cycles
      if (read_0 && (write_0_0 ? written_0 : written_1)) {
        update_0 = update_in_q0.read(nullptr);
      }
      if (read_1 && (write_1_1 ? written_1 : written_0)) {
        update_1 = update_in_q1.read(nullptr);
      }

      if (conflict) ++priority;
    }
  }
}

void UpdateHandler(
    Pid num_partitions,
    // from Control
    tlp::istream<Eid>& update_config_q, tlp::istream<bool>& update_phase_q,
    // to Control
    tlp::ostream<NumUpdates>& num_updates_out_q,
    // from ProcElem via UpdateRouter
    tlp::istream<UpdateReq>& update_req_q,
    tlp::istream<UpdateWithPid>& update_in_q,
    // to ProcElem via UpdateReorderer
    tlp::ostream<Update>& update_out_q,
    // to and from UpdateMem
    tlp::ostream<uint64_t>& updates_read_addr_q,
    tlp::istream<tlp::vec_t<Update, kUpdateVecLen>>& updates_read_data_q,
    tlp::ostream<uint64_t>& updates_write_addr_q,
    tlp::ostream<tlp::vec_t<Update, kUpdateVecLen>>& updates_write_data_q) {
  // HLS crashes without this...
  update_config_q.open();
  update_phase_q.open();
#ifdef __SYNTHESIS__
  ap_wait();
#endif  // __SYNTEHSIS__
  update_in_q.open();
#ifdef __SYNTHESIS__
  ap_wait();
#endif  // __SYNTEHSIS__
  update_out_q.close();

  // Memory offsets of each update partition.
  Eid update_offsets[kMaxNumPartitions];
  // Number of updates of each update partition in memory.
  Eid num_updates[kMaxNumPartitions];

num_updates_init:
  for (Pid pid = 0; pid < num_partitions; ++pid) {
#pragma HLS pipeline II = 1
    num_updates[pid] = 0;
  }

  // Initialization; needed only once per execution.
  int update_offset_idx = 0;
update_offset_init:
  TLP_WHILE_NOT_EOS(update_config_q) {
#pragma HLS pipeline II = 1
    update_offsets[update_offset_idx] = update_config_q.read(nullptr);
    ++update_offset_idx;
  }
  update_config_q.open();

update_phases:
  TLP_WHILE_NOT_EOS(update_phase_q) {
    const auto phase = update_phase_q.read();
    VLOG_F(5, recv) << "Phase: " << phase;
    if (phase == TaskReq::kScatter) {
      // kScatter lasts until update_phase_q is non-empty.
      Pid last_last_pid = 0xffffffff;
      Pid last_pid = 0xffffffff;
      Eid last_update_idx = 0xffffffff;
      tlp::vec_t<Update, kUpdateVecLen> update_v{0};
      uint8_t update_v_i = 0;
    update_writes:
      TLP_WHILE_NOT_EOS(update_in_q) {
#pragma HLS pipeline II = 1
#pragma HLS dependence variable = num_updates inter true distance = 2
        const auto peek_pid = update_in_q.peek(nullptr).pid;
        if (peek_pid != last_pid && peek_pid == last_last_pid) {
          // insert bubble
          last_last_pid = 0xffffffff;
        } else {
          const auto update_with_pid = update_in_q.read(nullptr);
          VLOG_F(5, recv) << "UpdateWithPid: " << update_with_pid;
          const Update update{update_with_pid.dst, update_with_pid.delta};
          const Pid pid = update_with_pid.pid;

          // number of updates already written to current partition, not
          // including the current update
          Eid update_idx;
          if (last_pid != pid) {
#pragma HLS latency min = 1 max = 1
            update_idx = num_updates[pid];
            if (last_pid != 0xffffffff) {
              num_updates[last_pid] = RoundUp<kUpdateVecLen>(last_update_idx);
            }
          } else {
            update_idx = last_update_idx;
          }

          // flush because we are switching partitions
          const bool flush = last_pid != pid && last_pid != 0xffffffff;
          // send old update_v with preivous update_idx and pid
          const Eid output_update_idx = flush ? last_update_idx : update_idx;
          const Pid output_pid = flush ? last_pid : pid;

          // set for next iteration
          last_last_pid = last_pid;
          last_pid = pid;
          last_update_idx = update_idx + 1;

          tlp::vec_t<Update, kUpdateVecLen> output_update_v = update_v;
          const bool write_enable = flush || update_v_i == kUpdateVecLen - 1;
          // update_v_i is the number of updates in update_v
          if (write_enable) {
            if (flush) {
              // send old update_v
              // new update_v contains current update
              update_v = {0};
              update_v.set(0, update);
              update_v_i = 1;
            } else {
              // send old update_v with current update written
              // new update_v is empty
              output_update_v.set(kUpdateVecLen - 1, update);
              update_v = {0};
              update_v_i = 0;
            }
          } else {
            // write update to update_v
            update_v.set(update_v_i, update);
            update_v_i += 1;
          }

          if (write_enable) {
            Eid update_offset = update_offsets[output_pid] + output_update_idx;
            updates_write_addr_q.write(update_offset / kUpdateVecLen);
            updates_write_data_q.write(output_update_v);
          }
        }
      }
      if (last_pid != 0xffffffff) {
        if (update_v_i > 0) {
          Eid update_offset = update_offsets[last_pid] + last_update_idx - 1;
          updates_write_addr_q.write(update_offset / kUpdateVecLen);
          updates_write_data_q.write(update_v);
        }
        num_updates[last_pid] = RoundUp<kUpdateVecLen>(last_update_idx);
      }
      update_in_q.open();
#ifdef __SYNTHESIS__
      ap_wait_n(1);
#endif  // __SYNTHESIS__
    send_num_updates:
      for (Pid pid = 0; pid < num_partitions; ++pid) {
        // TODO: store relevant partitions only
        VLOG_F(5, send) << "num_updates[" << pid << "]: " << num_updates[pid];
        num_updates_out_q.write({pid, num_updates[pid]});
        num_updates[pid] = 0;  // Reset for the next scatter phase.
      }
    } else {
      // Gather phase.
    recv_update_reqs:
      for (UpdateReq update_req; update_phase_q.empty();) {
        if (update_req_q.try_read(update_req)) {
          const auto pid = update_req.pid;
          const auto num_updates_pid = update_req.num_updates;
          VLOG_F(5, recv) << "UpdateReq: " << update_req;

          // update vec
          tlp::vec_t<Update, kUpdateVecLen> update_v{0};
          uint8_t update_v_i = kUpdateVecLen;

        update_reads:
          for (Eid i_rd = 0, i_wr = 0; i_rd < num_updates_pid;) {
            auto read_addr = update_offsets[pid] + i_wr;
            if (i_wr < num_updates_pid &&
                updates_read_addr_q.try_write(read_addr / kUpdateVecLen)) {
              i_wr += kUpdateVecLen;
            }

            // update_v_i has the number of updates already written
            if (update_v_i >= kUpdateVecLen &&
                updates_read_data_q.try_read(update_v)) {
              update_v_i = 0;
            }

            if (update_v_i < kUpdateVecLen &&
                (update_v[update_v_i].dst == 0 ||
                 update_out_q.try_write(update_v[update_v_i]))) {
              VLOG_F(5, send) << "Update: " << update_v[update_v_i];
              update_v_i += 1;
              ++i_rd;
            }
          }
          update_out_q.close();
        }
      }
    }
  }
  VLOG_F(3, info) << "done";
  update_phase_q.open();
}

void UpdateReorderer(tlp::istream<Update>& update_in_q,
                     tlp::ostream<Update>& update_out_q) {
  constexpr int kWindowSize = 5;
  constexpr int kBufferSize = 2;
  Update window[kWindowSize];
  Update buffer[kBufferSize];
#pragma HLS array_partition variable = window complete
#pragma HLS array_partition variable = buffer complete
#pragma HLS data_pack variable = window
#pragma HLS data_pack variable = buffer
  for (int i = 0; i < kWindowSize; ++i) {
#pragma HLS unroll
    window[i] = {};
  }
  for (int i = 0; i < kBufferSize; ++i) {
#pragma HLS unroll
    buffer[i] = {};
  }

  for (;;) {
  update_reads:
    TLP_WHILE_NOT_EOS(update_in_q) {
#pragma HLS pipeline II = 1
      int idx_without_conflict = FindFirstWithoutConflict(buffer, window);
      int idx_empty = FindFirstEmpty(buffer);
      bool input_has_conflict = true;
      bool input_empty = update_in_q.empty();
      Update input_update{};
      if (idx_without_conflict == -1 && idx_empty != -1 && !input_empty) {
        input_update = update_in_q.read(nullptr);
        input_has_conflict = HasConflict(window, input_update);
      }

      // possible write actions:
      // 1. write one of the updates in the reorder buffer
      // 2. write the input
      // 3. write a bubble

      auto output_update =
          idx_without_conflict != -1
              ? buffer[idx_without_conflict]
              : !input_has_conflict ? input_update : decltype(input_update)({});

      // possible buffer actions:
      // 1. remove an update
      // 2. do nothing
      // 3. put input in buffer (if not full)

      if (idx_without_conflict != -1) {
        buffer[idx_without_conflict] = {0, 0.f};
      } else if (idx_empty != -1 && !input_empty && input_has_conflict) {
        buffer[idx_empty] = input_update;
      }

      // update the window
      Shift(window, output_update);

      // write the output
      update_out_q.write(output_update);
      VLOG_F(5, send) << "Update: " << output_update;
    }

    // after the loop, flush the reorder buffer
  update_read_cleanup:
    for (int i = 0; i < kBufferSize;) {
      if (buffer[i].dst == 0) {
        ++i;
      } else {
        auto update = buffer[i];
        if (!HasConflict(window, update)) {
          buffer[i] = {};
          ++i;
        } else {
          update = {};
          LOG(WARNING) << "bubble inserted";
        }

        Shift(window, update);

        update_out_q.write(update);
        VLOG_F(5, send) << "Update: " << update;
      }
    }
    update_out_q.close();
    update_in_q.open();
  }
}

void ProcElem(
    // from Control
    tlp::istream<TaskReq>& task_req_q,
    // to Control
    tlp::ostream<TaskResp>& task_resp_q,
    // to and from VertexMem
    tlp::ostream<VertexReq>& vertex_req_q,
    tlp::istream<VertexAttr>& vertex_in_q,
    tlp::ostream<VertexAttr>& vertex_out_q,
    // to and from EdgeMem
    tlp::ostream<Eid>& edge_req_q, tlp::istream<Edge>& edge_resp_q,
    // to UpdateHandler
    tlp::ostream<UpdateReq>& update_req_q,
    // from UpdateHandler via UpdateReorderer
    tlp::istream<Update>& update_in_q,
    // to UpdateHandler via UpdateRouter
    tlp::ostream<UpdateWithPid>& update_out_q) {
  // HLS crashes without this...
  task_req_q.open();
#ifdef __SYNTHESIS__
  ap_wait();
#endif  // __SYNTEHSIS__
  update_out_q.close();
#ifdef __SYNTHESIS__
  ap_wait();
#endif  // __SYNTEHSIS__
  update_in_q.open();

  VertexAttr vertices_local[kMaxPartitionSize];

task_requests:
  TLP_WHILE_NOT_EOS(task_req_q) {
    const auto req = task_req_q.read();
    VLOG_F(5, recv) << "TaskReq: " << req;
    if (req.scatter_done) {
      update_out_q.close();
    } else {
      vertex_req_q.write({TaskReq::kScatter, req.vid_offset, req.num_vertices});
      bool active = false;
      if (req.phase == TaskReq::kScatter) {
      vertex_reads:
        for (Vid i = 0; i < req.num_vertices; ++i) {
          vertices_local[i] = vertex_in_q.read();
        }

      edge_reads:
        for (Eid eid_rd = 0, vec_eid_wr = 0; eid_rd < req.num_edges;) {
          if (vec_eid_wr < req.num_edges / kEdgeVecLen) {
            if (edge_req_q.try_write(req.eid_offset / kEdgeVecLen +
                                     vec_eid_wr)) {
              ++vec_eid_wr;
            }
          }
          Edge edge;
          if (edge_resp_q.try_read(edge)) {
            if (edge.src != 0) {
              auto src = vertices_local[edge.src - req.base_vid];
              // pre-compute pid for routing
              auto pid = (edge.dst - req.overall_base_vid) / req.partition_size;
              // use pre-computed src.tmp = src.ranking / src.out_degree
              UpdateWithPid update{edge.dst, src.tmp, pid};
              VLOG_F(5, send) << "Update: " << update;
              update_out_q.write(update);
            }
            ++eid_rd;
          }
        }
      } else {
        update_req_q.write({req.phase, req.pid, req.num_edges});
      vertex_resets:
        for (Vid i = 0; i < req.num_vertices; ++i) {
          vertices_local[i] = vertex_in_q.read();
          vertices_local[i].tmp = 0.f;
        }

      update_reads:
        TLP_WHILE_NOT_EOS(update_in_q) {
#pragma HLS pipeline II = 1
#pragma HLS dependence variable = vertices_local inter true distance = 5
          auto update = update_in_q.read(nullptr);
          if (update.dst != 0) {
            VLOG_F(5, recv) << "Update: " << update;
#pragma HLS latency max = 4
            vertices_local[update.dst - req.base_vid].tmp += update.delta;
          }
        }
        update_in_q.open();

        vertex_req_q.write(
            {TaskReq::kGather, req.vid_offset, req.num_vertices});

        float max_delta = 0.f;

      vertex_writes:
        for (Vid i = 0; i < req.num_vertices; ++i) {
          auto vertex = vertices_local[i];
          const float new_ranking = req.init + vertex.tmp * kDampingFactor;
          const float abs_delta = std::abs(new_ranking - vertex.ranking);
          max_delta = std::max(max_delta, abs_delta);
          vertex.ranking = new_ranking;
          // pre-compute vertex.tmp = vertex.ranking / vertex.out_degree
          vertex.tmp = vertex.ranking / vertex.out_degree;
          vertex_out_q.write(vertex);
        }
        active = max_delta > kConvergenceThreshold;
      }
      TaskResp resp{req.phase, req.pid, active};
      task_resp_q.write(resp);
    }
  }
  task_req_q.open();
}

void PageRank(Pid num_partitions, tlp::mmap<const Vid> num_vertices,
              tlp::mmap<const Eid> num_edges,
              tlp::mmap<VertexAttrAligned> vertices,
              tlp::async_mmap<tlp::vec_t<Edge, kEdgeVecLen>> edges,
              tlp::async_mmap<tlp::vec_t<Update, kUpdateVecLen>> updates) {
  // between Control and ProcElem
  tlp::stream<TaskReq, 2> task_req_0("task_req_0");
  tlp::stream<TaskReq, 2> task_req_1("task_req_1");
  tlp::stream<TaskResp, 2> task_resp_0("task_resp_0");
  tlp::stream<TaskResp, 2> task_resp_1("task_resp_1");

  // between ProcElem and VertexMem
  tlp::stream<VertexReq, 2> vertex_req_0("vertex_req_0");
  tlp::stream<VertexReq, 2> vertex_req_1("vertex_req_1");
  tlp::stream<VertexAttr, 2> vertex_pe2mm_0("vertex_pe2mm_0");
  tlp::stream<VertexAttr, 2> vertex_pe2mm_1("vertex_pe2mm_1");
  tlp::stream<VertexAttr, 2> vertex_mm2pe_0("vertex_mm2pe_0");
  tlp::stream<VertexAttr, 2> vertex_mm2pe_1("vertex_mm2pe_1");

  // between ProcElem and EdgeMem
  tlp::stream<Eid, 2> edge_req_0("edge_req_0");
  tlp::stream<Eid, 2> edge_req_1("edge_req_1");
  tlp::stream<Edge, 2> edge_resp_0("edge_resp_0");
  tlp::stream<Edge, 2> edge_resp_1("edge_resp_1");

  // between Control and UpdateHandler
  tlp::stream<Eid, 2> update_config_0("update_config_0");
  tlp::stream<Eid, 2> update_config_1("update_config_1");
  tlp::stream<bool, 2> update_phase_0("update_phase_0");
  tlp::stream<bool, 2> update_phase_1("update_phase_1");

  // between UpdateHandler and ProcElem
  tlp::stream<UpdateReq, 2> update_req_0("update_req_0");
  tlp::stream<UpdateReq, 2> update_req_1("update_req_1");

  // between UpdateHandler and UpdateMem
  tlp::stream<uint64_t, 2> update_read_addr_0("update_read_addr_0");
  tlp::stream<uint64_t, 2> update_read_addr_1("update_read_addr_1");
  tlp::stream<tlp::vec_t<Update, kUpdateVecLen>, 2> update_read_data_0(
      "update_read_data_0");
  tlp::stream<tlp::vec_t<Update, kUpdateVecLen>, 2> update_read_data_1(
      "update_read_data_1");
  tlp::stream<uint64_t, 2> update_write_addr_0("update_write_addr_0");
  tlp::stream<uint64_t, 2> update_write_addr_1("update_write_addr_1");
  tlp::stream<tlp::vec_t<Update, kUpdateVecLen>, 2> update_write_data_0(
      "update_write_data_0");
  tlp::stream<tlp::vec_t<Update, kUpdateVecLen>, 2> update_write_data_1(
      "update_write_data_1");

  tlp::stream<UpdateWithPid, 2> update_pe2ro_0("update_pe2router_0");
  tlp::stream<UpdateWithPid, 2> update_pe2ro_1("update_pe2router_1");
  tlp::stream<UpdateWithPid, 2> update_ro2mm_0("update_router2handler_0");
  tlp::stream<UpdateWithPid, 2> update_ro2mm_1("update_router2handler_1");
  tlp::stream<Update, 2> update_ro2pe_0("update_reorderer2pe_0");
  tlp::stream<Update, 2> update_ro2pe_1("update_reorderer2pe_1");
  tlp::stream<Update, 2> update_mm2ro_0("update_mm2ro_0");
  tlp::stream<Update, 2> update_mm2ro_1("update_mm2ro_1");

  tlp::stream<NumUpdates, 2> num_updates_0("num_updates_0");
  tlp::stream<NumUpdates, 2> num_updates_1("num_updates_1");

  tlp::task()
      .invoke<-1>(UpdateRouter, "UpdateRouter", update_pe2ro_0, update_pe2ro_1,
                  update_ro2mm_0, update_ro2mm_1)
      .invoke<-1>(VertexMem, "VertexMem", vertex_req_0, vertex_req_1,
                  vertex_pe2mm_0, vertex_pe2mm_1, vertex_mm2pe_0,
                  vertex_mm2pe_1, vertices)
      .invoke<-1>(EdgeMem, "EdgeMem", edge_req_0, edge_req_1, edge_resp_0,
                  edge_resp_1, edges)
      .invoke<-1>(UpdateReorderer, "UpdateReorder_0", update_mm2ro_0,
                  update_ro2pe_0)
      .invoke<-1>(UpdateReorderer, "UpdateReorder_1", update_mm2ro_1,
                  update_ro2pe_1)
      .invoke<-1>(UpdateMem, "UpdateMem", update_read_addr_0,
                  update_read_addr_1, update_read_data_0, update_read_data_1,
                  update_write_addr_0, update_write_addr_1, update_write_data_0,
                  update_write_data_1, updates)
      .invoke<0>(ProcElem, "ProcElem_0", task_req_0, task_resp_0, vertex_req_0,
                 vertex_mm2pe_0, vertex_pe2mm_0, edge_req_0, edge_resp_0,
                 update_req_0, update_ro2pe_0, update_pe2ro_0)
      .invoke<0>(ProcElem, "ProcElem_1", task_req_1, task_resp_1, vertex_req_1,
                 vertex_mm2pe_1, vertex_pe2mm_1, edge_req_1, edge_resp_1,
                 update_req_1, update_ro2pe_1, update_pe2ro_1)
      .invoke<0>(Control, "Control", num_partitions, num_vertices, num_edges,
                 update_config_0, update_config_1, update_phase_0,
                 update_phase_1, num_updates_0, num_updates_1, task_req_0,
                 task_req_1, task_resp_0, task_resp_1)
      .invoke<0>(UpdateHandler, "UpdateHandler_0", num_partitions,
                 update_config_0, update_phase_0, num_updates_0, update_req_0,
                 update_ro2mm_0, update_mm2ro_0, update_read_addr_0,
                 update_read_data_0, update_write_addr_0, update_write_data_0)
      .invoke<0>(UpdateHandler, "UpdateHandler_1", num_partitions,
                 update_config_1, update_phase_1, num_updates_1, update_req_1,
                 update_ro2mm_1, update_mm2ro_1, update_read_addr_1,
                 update_read_data_1, update_write_addr_1, update_write_data_1);
}
