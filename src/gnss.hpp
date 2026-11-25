// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#pragma once
// GNSS fix source for the cap's ATGM336H receiver, following the stock
// reader's conventions: an injectable read function so tests run without the
// serial port, and a single-owner, non-blocking poll contract.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <sys/types.h> // ::ssize_t
#include <vector>

#include "field.hpp"

namespace rfk {

// POSIX read() semantics (bytes copied, 0 = nothing available, -1 = error),
// expected non-blocking so a poll() never stalls the UI.
using ReadFn = std::function<ssize_t(char*, std::size_t)>;
// Steady-clock milliseconds; the empty function means the real steady clock.
using MonoFn = std::function<std::int64_t()>;

// One parsed fix state. `pos` is degrees north/east. `fix_mono_ms` is the
// monotonic stamp taken at parse time of the most recent accepted
// valid-fix sentence; a void-fix sentence never refreshes it. Before any
// valid fix (and after a void-fix sentence) `valid` is false and `pos` holds
// the last-known value.
struct GnssFix {
    bool valid = false;
    Position pos{0.0, 0.0};
    std::int64_t fix_mono_ms = 0;
};

class GnssSource {
public:
    explicit GnssSource(ReadFn read_fn, MonoFn mono_ms = {});

    // Read whatever arrived and apply it to the current fix state.
    void poll();
    GnssFix fix() const;

private:
    void handle_line(std::string&& line);
    void apply_gga(const std::vector<std::string>& fields);
    void apply_rmc(const std::vector<std::string>& fields);
    void apply_valid_fix();
    void mark_void();

    ReadFn read_fn_;
    MonoFn mono_ms_;
    std::string pending_;
    std::vector<std::string> scratch_;
    GnssFix fix_;
};

// Device source: opens `port` (default /dev/ttyS0) at 115200 8N1 (ATGM336H),
// non-blocking, flock-locked exclusively. Busy is true when the port is
// already claimed by a locking reader; an empty read is a missing port.
struct PortOpen {
    ReadFn read;
    bool busy = false;
};
// The returned read closure is the descriptor's SOLE owner: destroying it
// closes the fd and drops the flock, so every fetch end — fix, timeout,
// cancel — releases the port synchronously.
PortOpen serial_open(const char* port = "/dev/ttyS0");

// One Use GPS attempt. start() opens the port once (busy/missing classify
// immediately); every poll() is a bounded non-blocking UI-tick drain that
// accepts a fix observed on the previous tick if it is still fresh, and ends
// the attempt on timeout. Every exit path — fix, timeout, cancel — releases
// the port synchronously.
enum class GpsOutcome { idle, running, fix, timeout, cancelled, busy, missing };
constexpr std::int64_t kGpsTimeoutMs = 30000; // indoor no-fix budget
constexpr std::int64_t kGpsFreshMs = 1500;    // accept window on the fix stamp

struct GpsHooks {
    std::function<PortOpen()> open; // empty: the real serial port
    MonoFn mono;                    // empty: the real steady clock
};

class GpsFetch {
public:
    explicit GpsFetch(GpsHooks hooks = {});

    void start();
    GpsOutcome poll();
    GpsOutcome state() const;
    std::optional<Position> position() const; // set when state() == fix
    bool running() const;
    void cancel(); // releases the port synchronously, state -> cancelled

private:
    GpsHooks hooks_;
    std::optional<GnssSource> source_;
    std::int64_t start_mono_ms_ = 0;
    GpsOutcome state_ = GpsOutcome::idle;
    std::optional<Position> position_;
};

} // namespace rfk
