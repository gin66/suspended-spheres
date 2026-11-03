/* node_sim.h — host double for one Fly-Micro4 node (§8 of the TDD).
 *
 * Four axes, a two-segment queue (active + next), a receive ring, the
 * playback clock, and the fault/hold paths.  It uses the shared core's
 * pure segment evaluation and step conversion; it does not touch PIO,
 * CAN hardware, or the operating-system clock.
 */

#ifndef NODE_SIM_H
#define NODE_SIM_H

#include "motion_core.h"
#include "playback_clock.h"

#include <string.h>

struct NodeSim {
    static const int QUEUE_MAX = 2;
    static const int RX_MAX = 16;

    struct Queued {
        mc_segment_t seg;
        int64_t start_us;
        int64_t tseg_us;
    };

    struct Axis {
        Queued q[QUEUE_MAX];
        int qcount;
        bool holding;
        bool decel_active;
        Queued decel;
        int fault;
        int32_t held_um;
        int32_t step_pos;
        int32_t following_error_steps;
    };

    struct Rx {
        uint8_t data[8];
        int id;
        int64_t t_us;
        int64_t start_us;
        int64_t tseg_us;
    };

    Axis ax[MC_AXIS_COUNT];
    int board_id;
    int32_t accel_limit_um_s2;
    int64_t sync_timeout_us;
    bool can_fault;
    bool estop;
    PlaybackClock clock;

    Rx rx[RX_MAX];
    int rx_head;
    int rx_tail;
    int parse_count;

    NodeSim() { reset(); }

    void reset() {
        for (int i = 0; i < MC_AXIS_COUNT; ++i) {
            ax[i].qcount = 0;
            ax[i].holding = false;
            ax[i].decel_active = false;
            ax[i].fault = 0;
            ax[i].held_um = 0;
            ax[i].step_pos = 0;
            ax[i].following_error_steps = 0;
        }
        board_id = -1;
        accel_limit_um_s2 = 1500000;   /* 1.5 m/s² nominal */
        sync_timeout_us = 100000;      /* 100 ms */
        can_fault = false;
        estop = false;
        clock = PlaybackClock();
        rx_head = rx_tail = 0;
        parse_count = 0;
    }

    void set_board(int id) { board_id = id; }
    void sync(int64_t master_us) { clock.on_sync(master_us); }

    static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

    static bool segment_in_range(const mc_segment_t *seg, int64_t tseg_us,
                                 int32_t *p_end_out) {
        int32_t p0 = mc_code_to_um(seg->p0);
        int32_t p_end = mc_segment_eval_um(seg, tseg_us);
        if (p0 < 0 || p0 > 2000000 || p_end < 0 || p_end > 2000000)
            return false;
        int32_t a0 = mc_segment_accel_um_s2(seg, 0);
        int32_t a1 = mc_segment_accel_um_s2(seg, tseg_us);
        if (a0 < -9810000 || a1 < -9810000)
            return false;
        if (p_end_out)
            *p_end_out = p_end;
        return true;
    }

    void make_decel(int i, int64_t start_us, int32_t p_um, int32_t v_um_s,
                    int fault_flag) {
        Axis &a = ax[i];
        if (v_um_s == 0) {
            a.decel_active = false;
            a.holding = true;
            a.held_um = p_um;
            a.fault |= fault_flag;
            return;
        }
        int32_t acc = (v_um_s > 0) ? -accel_limit_um_s2 : accel_limit_um_s2;
        int64_t tdec = (int64_t)abs_i32(v_um_s) * 1000000 /
                       accel_limit_um_s2;
        if (tdec < 1)
            tdec = 1;

        a.decel.seg.p0 = mc_um_to_code(p_um);
        a.decel.seg.v0 = mc_speed_encode(v_um_s);
        a.decel.seg.a0 = (int16_t)(acc / 1000);
        a.decel.seg.j = 0;
        a.decel.start_us = start_us;
        a.decel.tseg_us = tdec;
        a.decel_active = true;
        a.holding = false;
        a.fault |= fault_flag;
    }

    int32_t segment_speed_at(const Queued &q, int64_t t_us) const {
        int64_t tau = t_us - q.start_us;
        if (tau < 0)
            tau = 0;
        if (tau > q.tseg_us)
            tau = q.tseg_us;
        return mc_segment_speed_um_s(&q.seg, tau);
    }

    void advance_axis(int i, int64_t t_us) {
        Axis &a = ax[i];

        if (a.qcount == 2 && t_us >= a.q[1].start_us) {
            a.q[0] = a.q[1];
            a.qcount = 1;
        }

        if (a.qcount >= 1 && !a.decel_active && !a.holding) {
            int64_t end = a.q[0].start_us + a.q[0].tseg_us;
            if (t_us > end) {
                int32_t p_end = mc_segment_eval_um(&a.q[0].seg,
                                                   a.q[0].tseg_us);
                int32_t v_end = mc_segment_speed_um_s(&a.q[0].seg,
                                                      a.q[0].tseg_us);
                make_decel(i, end, p_end, v_end,
                           MC_STATUS_FAULT_SAMPLE_TIMEOUT);
            }
        }

        if (a.decel_active) {
            int64_t dec_end = a.decel.start_us + a.decel.tseg_us;
            if (t_us >= dec_end) {
                a.held_um = mc_segment_eval_um(&a.decel.seg, a.decel.tseg_us);
                a.decel_active = false;
                a.holding = true;
            }
        }
    }

    int32_t eval(int i, int64_t t_us) {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return -1;
        advance_axis(i, t_us);
        Axis &a = ax[i];

        if (a.decel_active) {
            int64_t tau = t_us - a.decel.start_us;
            if (tau < 0)
                tau = 0;
            if (tau > a.decel.tseg_us)
                tau = a.decel.tseg_us;
            return mc_segment_eval_um(&a.decel.seg, tau);
        }
        if (a.qcount >= 1) {
            Queued &q = a.q[0];
            if (t_us < q.start_us)
                return a.held_um;
            if (t_us <= q.start_us + q.tseg_us) {
                int64_t tau = t_us - q.start_us;
                return mc_segment_eval_um(&q.seg, tau);
            }
        }
        if (a.holding)
            return a.held_um;
        return -1;
    }

    int32_t speed(int i, int64_t t_us) {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return 0;
        advance_axis(i, t_us);
        Axis &a = ax[i];
        if (a.decel_active) {
            int64_t tau = t_us - a.decel.start_us;
            if (tau < 0)
                tau = 0;
            if (tau > a.decel.tseg_us)
                tau = a.decel.tseg_us;
            return mc_segment_speed_um_s(&a.decel.seg, tau);
        }
        if (a.qcount >= 1) {
            Queued &q = a.q[0];
            if (t_us >= q.start_us && t_us <= q.start_us + q.tseg_us)
                return segment_speed_at(q, t_us);
        }
        return 0;
    }

    int push(int axis, int type, int64_t start_us, int64_t tseg_us,
             const mc_segment_t *seg) {
        if (axis < 0 || axis >= MC_AXIS_COUNT || seg == 0 || tseg_us <= 0)
            return -1;
        if (type != 0)
            return -1;

        advance_axis(axis, start_us);
        Axis &a = ax[axis];
        int32_t p_end = 0;
        if (!segment_in_range(seg, tseg_us, &p_end)) {
            a.fault |= (mc_segment_accel_um_s2(seg, 0) < -9810000 ||
                        mc_segment_accel_um_s2(seg, tseg_us) < -9810000)
                           ? MC_STATUS_FAULT_ACCEL
                           : MC_STATUS_FAULT_OUT_OF_RANGE;
            a.holding = true;
            a.decel_active = false;
            return -1;
        }

        bool reanchor = a.holding || a.decel_active;
        if (reanchor) {
            if ((a.fault & MC_STATUS_FAULT_SAMPLE_TIMEOUT) == 0) {
                int32_t gap = (int32_t)seg->p0 - mc_um_to_code(a.held_um);
                if (abs_i32(gap) > 1) {
                    a.fault |= MC_STATUS_FAULT_FOLLOWING;
                    a.holding = true;
                    a.decel_active = false;
                    return -1;
                }
            }
            a.q[0].seg = *seg;
            a.q[0].start_us = start_us;
            a.q[0].tseg_us = tseg_us;
            a.qcount = 1;
            a.holding = false;
            a.decel_active = false;
            a.fault = 0;
            a.held_um = p_end;
            a.step_pos = mc_um_to_steps(mc_code_to_um(seg->p0));
            return 0;
        }

        if (a.qcount == 0) {
            a.q[0].seg = *seg;
            a.q[0].start_us = start_us;
            a.q[0].tseg_us = tseg_us;
            a.qcount = 1;
            a.held_um = p_end;
            a.step_pos = mc_um_to_steps(mc_code_to_um(seg->p0));
            a.fault = 0;
            return 0;
        }

        if (a.qcount == 1) {
            int32_t prev_end = mc_segment_eval_um(&a.q[0].seg,
                                                  a.q[0].tseg_us);
            int32_t prev_v = mc_segment_speed_um_s(&a.q[0].seg,
                                                   a.q[0].tseg_us);
            int32_t p_gap = (int32_t)seg->p0 - mc_um_to_code(prev_end);
            int32_t v_gap = (int32_t)seg->v0 - mc_speed_encode(prev_v);
            if (abs_i32(p_gap) > 1 || abs_i32(v_gap) > 1) {
                a.fault |= MC_STATUS_FAULT_FOLLOWING;
                a.holding = true;
                return -1;
            }
            a.q[1].seg = *seg;
            a.q[1].start_us = start_us;
            a.q[1].tseg_us = tseg_us;
            a.qcount = 2;
            a.held_um = p_end;
            return 0;
        }

        return -1;   /* queue full */
    }

    int state(int i) const {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return MC_STATUS_IDLE;
        const Axis &a = ax[i];
        if (a.fault)
            return MC_STATUS_FAULT;
        if (a.holding || a.decel_active)
            return MC_STATUS_HOLDING;
        if (a.qcount)
            return MC_STATUS_PLAYING;
        return MC_STATUS_IDLE;
    }

    int fault(int i) const {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return 0;
        return ax[i].fault;
    }

    bool holding(int i) const {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return false;
        return ax[i].holding || ax[i].decel_active;
    }

    int32_t held_um(int i) const {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return -1;
        return ax[i].held_um;
    }

    int32_t step_pos(int i) const {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return 0;
        return ax[i].step_pos;
    }

    void request_stop(int fault_flag) {
        int64_t now = clock.eval_us();
        for (int i = 0; i < MC_AXIS_COUNT; ++i) {
            Axis &a = ax[i];
            if (a.qcount == 0 && !a.decel_active)
                continue;
            int32_t p = eval(i, now);
            int32_t v = speed(i, now);
            make_decel(i, now, p, v, fault_flag);
        }
    }

    void inject_following_error(int i, int32_t steps) {
        if (i < 0 || i >= MC_AXIS_COUNT)
            return;
        ax[i].following_error_steps = steps;
        ax[i].fault |= MC_STATUS_FAULT_FOLLOWING;
    }

    int step(int32_t chunk_us, int16_t steps[MC_AXIS_COUNT],
             uint16_t *ticks) {
        if (steps == 0 || ticks == 0 || chunk_us <= 0)
            return -1;
        int64_t tick64 = (int64_t)chunk_us * 16000000 / 1000000;
        if (tick64 < 3200 || tick64 > 65535)
            return -1;

        if (estop) {
            for (int i = 0; i < MC_AXIS_COUNT; ++i)
                steps[i] = 0;
            *ticks = (uint16_t)tick64;
            return -1;
        }

        if (clock.sync_lost(sync_timeout_us)) {
            for (int i = 0; i < MC_AXIS_COUNT; ++i) {
                if (ax[i].qcount || ax[i].decel_active)
                    ax[i].fault |= MC_STATUS_FAULT_SYNC_LOST;
                ax[i].holding = true;
                ax[i].decel_active = false;
                steps[i] = 0;
            }
            *ticks = (uint16_t)tick64;
            return -1;
        }

        if (can_fault) {
            request_stop(MC_STATUS_FAULT_SAMPLE_TIMEOUT);
            can_fault = false;
        }

        int64_t t = clock.eval_us();
        int16_t out[MC_AXIS_COUNT];
        int32_t next[MC_AXIS_COUNT];
        for (int i = 0; i < MC_AXIS_COUNT; ++i) {
            int32_t um = eval(i, t);
            Axis &a = ax[i];
            if (um < 0) {
                out[i] = 0;
                next[i] = a.step_pos;
                continue;
            }
            int32_t target = mc_um_to_steps(um);
            int32_t delta = target - a.step_pos;
            if (delta > 128 || delta < -128) {
                a.fault |= MC_STATUS_FAULT_UNDERRUN;
                a.holding = true;
                a.decel_active = false;
                a.held_um = um;
                return -1;
            }
            out[i] = (int16_t)delta;
            next[i] = target;
        }

        for (int i = 0; i < MC_AXIS_COUNT; ++i) {
            ax[i].step_pos = next[i];
            steps[i] = out[i];
        }
        *ticks = (uint16_t)tick64;
        return 0;
    }

    /* --- receive ring: store bytes, parse only in drain() ------------- */

    bool rx_push(int id, const uint8_t data[8], int64_t t_us,
                 int64_t start_us, int64_t tseg_us) {
        int next = (rx_head + 1) % RX_MAX;
        if (next == rx_tail)
            return false;
        Rx &r = rx[rx_head];
        r.id = id;
        for (int i = 0; i < 8; ++i)
            r.data[i] = data[i];
        r.t_us = t_us;
        r.start_us = start_us;
        r.tseg_us = tseg_us;
        rx_head = next;
        return true;
    }

    int drain() {
        int parsed = 0;
        while (rx_tail != rx_head) {
            Rx &r = rx[rx_tail];
            int type = mc_id_type(r.id);
            if (mc_board_accepts_id(board_id, r.id) && type == 0) {
                int motor = mc_id_motor(r.id);
                int axis = motor - board_id * 4;
                mc_segment_t seg;
                if (mc_unpack_frame(r.data, 8, 0, &seg) == 0)
                    (void)push(axis, 0, r.start_us, r.tseg_us, &seg);
                ++parse_count;
                ++parsed;
            }
            rx_tail = (rx_tail + 1) % RX_MAX;
        }
        return parsed;
    }

    int apply_frame(int id, const uint8_t data[8], int64_t start_us,
                    int64_t tseg_us) {
        if (!mc_board_accepts_id(board_id, id) || mc_id_type(id) != 0)
            return -1;
        int motor = mc_id_motor(id);
        int axis = motor - board_id * 4;
        mc_segment_t seg;
        if (mc_unpack_frame(data, 8, 0, &seg) != 0)
            return -1;
        return push(axis, 0, start_us, tseg_us, &seg);
    }

    int32_t first_p0_code(int i) const {
        if (i < 0 || i >= MC_AXIS_COUNT || ax[i].qcount == 0)
            return -1;
        return ax[i].q[0].seg.p0;
    }
};

#endif /* NODE_SIM_H */
