/* playback_clock.h — the test clock from motion_streaming_tdd.md §0.1.
 *
 * Integer rules follow whitepaper §4.9.  No operating-system clock and no
 * sleeping: the test advances master time explicitly.  This type lives in
 * the test support code, not in the shared core.
 */

#ifndef PLAYBACK_CLOCK_H
#define PLAYBACK_CLOCK_H

#include <stdint.h>

struct PlaybackClock {
    void set_crystal_ppm(int32_t ppm) {
        crystal_ppm_ = ppm;
    }

    void advance_us(int64_t dt_us) {
        if (dt_us <= 0)
            return;
        master_us_ += dt_us;

        crystal_acc_ += dt_us * crystal_ppm_;
        int64_t extra = crystal_acc_ / 1000000;
        crystal_acc_ -= extra * 1000000;
        local_us_ += dt_us + extra;
    }

    int64_t master_us() const { return master_us_; }
    int64_t local_us() const { return local_us_; }

    void on_sync(int64_t master_us) {
        if (!synced_) {
            synced_ = true;
            correction_ppm_ = 0;
            eval_anchor_us_ = master_us;
            eval_anchor_local_us_ = local_us_;
        } else {
            int64_t eval_now = eval_us();
            int64_t d_master = master_us - last_sync_master_us_;
            int64_t d_local = local_us_ - last_sync_local_us_;
            if (d_master > 0)
                correction_ppm_ = (int32_t)((d_local - d_master) * 1000000 /
                                            d_master);
            eval_anchor_us_ = eval_now;
            eval_anchor_local_us_ = local_us_;
        }
        last_sync_master_us_ = master_us;
        last_sync_local_us_ = local_us_;
    }

    int64_t eval_us() const {
        if (!synced_)
            return master_us_;
        int64_t elapsed = local_us_ - eval_anchor_local_us_;
        return eval_anchor_us_ +
               elapsed * (1000000 - correction_ppm_) / 1000000;
    }

    int32_t correction_ppm() const { return correction_ppm_; }

    bool sync_lost(int64_t timeout_us) const {
        if (!synced_)
            return true;
        return (local_us_ - last_sync_local_us_) > timeout_us;
    }

private:
    int32_t crystal_ppm_ = 0;
    int64_t master_us_ = 0;
    int64_t local_us_ = 0;
    int64_t crystal_acc_ = 0;

    bool    synced_ = false;
    int64_t last_sync_master_us_ = 0;
    int64_t last_sync_local_us_ = 0;

    int32_t correction_ppm_ = 0;
    int64_t eval_anchor_us_ = 0;
    int64_t eval_anchor_local_us_ = 0;
};

#endif /* PLAYBACK_CLOCK_H */
