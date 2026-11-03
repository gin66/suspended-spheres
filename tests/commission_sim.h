/* commission_sim.h — host table and board-side discovery double (§9).
 *
 * The flash ID is the only persistent identity.  The board ID is RAM and
 * is assigned by the host.  The host keeps (flash_id, axis) -> position;
 * the board never learns a grid position.
 */

#ifndef COMMISSION_SIM_H
#define COMMISSION_SIM_H

#include "motion_core.h"

#include <string.h>

struct FlashId {
    uint8_t b[8];
};

inline bool flash_eq(const uint8_t a[8], const uint8_t b[8]) {
    for (int i = 0; i < 8; ++i)
        if (a[i] != b[i])
            return false;
    return true;
}

struct HostTable {
    struct Entry {
        bool used;
        uint8_t flash[8];
        int board_id;
        int position[MC_AXES_PER_NODE];
    } e[MC_BOARD_COUNT];
    int count;

    HostTable() { clear(); }

    void clear() {
        count = 0;
        for (int i = 0; i < MC_BOARD_COUNT; ++i)
            e[i].used = false;
    }

    int find(const uint8_t flash[8]) const {
        for (int i = 0; i < MC_BOARD_COUNT; ++i)
            if (e[i].used && flash_eq(e[i].flash, flash))
                return i;
        return -1;
    }

    int board_for(const uint8_t flash[8]) const {
        int i = find(flash);
        return i < 0 ? -1 : e[i].board_id;
    }

    int position_for(const uint8_t flash[8], int axis) const {
        int i = find(flash);
        if (i < 0 || axis < 0 || axis >= MC_AXES_PER_NODE)
            return -1;
        return e[i].position[axis];
    }

    /* Known flash: same board ID.  New flash: lowest free board ID. */
    int add(const uint8_t flash[8], int first_position) {
        int i = find(flash);
        if (i >= 0)
            return e[i].board_id;

        bool taken[MC_BOARD_COUNT] = {false};
        for (int k = 0; k < MC_BOARD_COUNT; ++k)
            if (e[k].used)
                taken[e[k].board_id] = true;

        int board = -1;
        for (int b = 0; b < MC_BOARD_COUNT; ++b)
            if (!taken[b]) {
                board = b;
                break;
            }
        if (board < 0)
            return -1;

        i = count;
        e[i].used = true;
        for (int k = 0; k < 8; ++k)
            e[i].flash[k] = flash[k];
        e[i].board_id = board;
        for (int a = 0; a < MC_AXES_PER_NODE; ++a)
            e[i].position[a] = first_position + a;
        ++count;
        return board;
    }
};

struct BoardSim {
    uint8_t flash[8];
    int board_id;          /* -1 while unidentified */
    bool pending_disc;
    int64_t disc_due_us;
    int disc_emitted;

    BoardSim() : board_id(-1), pending_disc(false), disc_due_us(0),
                 disc_emitted(0) {
        for (int i = 0; i < 8; ++i)
            flash[i] = 0;
    }

    void set_flash(const uint8_t id[8]) {
        for (int i = 0; i < 8; ++i)
            flash[i] = id[i];
    }

    int advertise_delay_us() const {
        return (flash[0] % 10) * 1000;
    }

    /* A board with an ID stays silent. */
    void on_ask(int64_t now_us) {
        if (board_id >= 0)
            return;
        pending_disc = true;
        disc_due_us = now_us + advertise_delay_us();
    }

    bool disc_due(int64_t now_us) const {
        return pending_disc && now_us >= disc_due_us;
    }

    int emit_disc(int64_t now_us) {
        if (!disc_due(now_us))
            return -1;
        pending_disc = false;
        ++disc_emitted;
        return mc_id_make(0, 8);
    }

    int on_assign(int field, const uint8_t payload[8]) {
        if (field < 0 || field >= MC_BOARD_COUNT)
            return -1;
        if (mc_id_make(field, 10) < 0)
            return -1;
        if (!flash_eq(payload, flash))
            return -1;
        board_id = field;
        pending_disc = false;
        return 0;
    }

    void clear_ram_id() { board_id = -1; }
};

/* Run one 5 s ASK round.  Returns the number of DISC frames emitted. */
inline int ask_round(BoardSim *boards, int n, int64_t now_us,
                     int64_t collect_us) {
    for (int i = 0; i < n; ++i)
        boards[i].on_ask(now_us);
    int emitted = 0;
    for (int i = 0; i < n; ++i)
        if (boards[i].emit_disc(now_us + collect_us) >= 0)
            ++emitted;
    return emitted;
}

#endif /* COMMISSION_SIM_H */
