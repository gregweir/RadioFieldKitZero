// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "gnss.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdexcept>
#include <string>

using namespace rfk;

void check(bool passed, const char* why) {
    if (!passed) throw std::runtime_error(why);
}

// Appends the two-hex NMEA checksum (XOR of the body) and the CRLF terminator.
std::string nmea(const std::string& body) {
    std::uint8_t x = 0;
    for (char c : body) x = static_cast<std::uint8_t>(x ^ c);
    char cs[4];
    std::snprintf(cs, sizeof cs, "*%02X", x);
    return "$" + body + cs + "\r\n";
}

// A read function over a queue of whole byte strings, POSIX-read semantics.
struct Feed {
    mutable std::deque<std::string> chunks;
    ssize_t operator()(char* out, std::size_t size) const {
        if (chunks.empty()) return 0;
        auto& chunk = chunks.front();
        const ssize_t n = static_cast<ssize_t>(std::min(size, chunk.size()));
        std::memcpy(out, chunk.data(), static_cast<std::size_t>(n));
        if (static_cast<std::size_t>(n) == chunk.size()) chunks.pop_front();
        else chunk.erase(0, static_cast<std::size_t>(n));
        return n;
    }
};

const char* valid_gga = "GPGGA,123519,4807.038,N,01131.324,E,1,08,0.9,545.4,M,46.9,M,,";
const double lat_munich = 48.0 + 7.038 / 60, lon_munich = 11.0 + 31.324 / 60;

bool near(double a, double b) { return std::fabs(a - b) < 5e-7; }

int main() try {
    {
        GnssSource src(Feed{{nmea(valid_gga)}}, [] { return 1000; });
        src.poll();
        check(src.fix().valid, "valid GGA fix");
        check(near(src.fix().pos.latitude, lat_munich), "GGA latitude");
        check(near(src.fix().pos.longitude, lon_munich), "GGA longitude");
        check(src.fix().fix_mono_ms == 1000, "fix stamp is the injected clock");
        src.poll();
        check(src.fix().fix_mono_ms == 1000, "repeat poll keeps the fix");
    }
    {
        GnssFix first;
        Feed feed{{nmea("GPRMC,123519,A,4807.038,N,01131.324,E,022.4,084.4,230394,003.1,W")}};
        GnssSource src(std::ref(feed), [] { return 2000; });
        src.poll();
        first = src.fix();
        check(first.valid && near(first.pos.latitude, lat_munich) &&
              near(first.pos.longitude, lon_munich), "valid RMC fix");
    }
    {
        Feed feed{{nmea("GPGGA,123519,4807.038,N,01131.324,E,1,08,0.9,545.4,M,46.9,M,,"),
                   nmea("GPGGA,123520,4807.038,N,01131.324,E,0,,,,,M,,"),
                   nmea("GPRMC,123521,V,4807.038,N,01131.324,E,,,230394,,")}};
        GnssSource src(std::move(feed), [] { return 3000; });
        src.poll();
        check(!src.fix().valid, "void sentences unset validity");
        check(near(src.fix().pos.latitude, lat_munich), "void keeps last-known latitude");
    }
    {
        Feed feed{{"$GPGGA,123519,4807.038,N,01131.324,E,1,08,0.9,545.4,M,46.9,M,,*01\r\n",
                   "no dollar sign junk, not even a newline",
                   nmea("GPGLL,4807.038,N,01131.324,E,123519,A"),
                   nmea(valid_gga)}};
        GnssSource src(std::move(feed), [] { return 4000; });
        src.poll();
        check(src.fix().valid, "junk is skipped and a later good GGA parses");
    }
    {
        const std::string line = nmea(valid_gga);
        GnssSource src([&line](char* out, std::size_t size) {
            static std::size_t sent = 0;
            if (sent >= line.size()) return ssize_t(0);
            const std::size_t n = std::min(size, (line.size() - sent + 2) / 3);
            std::memcpy(out, line.data() + sent, n);
            sent += n;
            return static_cast<ssize_t>(n);
        }, [] { return 5000; });
        src.poll();
        check(src.fix().valid, "sentence split across reads reassembles");
    }
    {
        std::string noise(4096, 'k');
        noise += "\n";
        Feed feed{{noise, nmea(valid_gga)}};
        GnssSource src(std::move(feed), [] { return 6000; });
        src.poll();
        check(src.fix().valid, "no-newline noise is bounded and later lines parse");
    }

    // --- GpsFetch: one open per attempt, classified outcomes, port released.
    {
        bool read_attempted = false;
        struct Clock { std::int64_t value = 1000; } clock;

        // Hooks over a one-shot scripted port. `body` is NMEA text (empty =
        // dead air); `destroyed` flips when the port's read closure is gone.
        auto hooks_scripted = [&](std::shared_ptr<bool> destroyed, const std::string& body)
            -> GpsHooks {
            GpsHooks h;
            h.open = [destroyed, body]() -> PortOpen {
                struct Guard { bool* flag; ~Guard() { *flag = true; } };
                auto guard = std::make_shared<Guard>(Guard{destroyed.get()});
                auto feed = std::make_shared<Feed>(Feed{});
                if (!body.empty()) feed->chunks.push_back(nmea(body));
                return PortOpen{std::function<ssize_t(char*, std::size_t)>(
                                    [guard, feed](char* out, std::size_t size) {
                                        return (*feed)(out, size);
                                    }),
                                false};
            };
            return h;
        };

        // 1. valid fix fills position (accepted one poll after parse).
        auto flag1 = std::make_shared<bool>(false);
        GpsHooks hooks = hooks_scripted(flag1, valid_gga);
        hooks.mono = [&clock] { return clock.value; };
        GpsFetch fixer(hooks);
        fixer.start();
        check(fixer.state() == GpsOutcome::running, "start opens the port");
        fixer.poll();
        check(fixer.state() == GpsOutcome::running, "fresh parser output is not yet accepted");
        fixer.poll();
        check(fixer.state() == GpsOutcome::fix, "valid fix fills position");
        check(fixer.position() && near(fixer.position()->latitude, lat_munich) &&
              near(fixer.position()->longitude, lon_munich), "fix carries degrees north/east");
        check(*flag1, "fix released the port");

        // 2. stale fix rejected: parsed at 1000 ms, clock jumps past the window.
        auto flag2 = std::make_shared<bool>(false);
        GpsHooks stale_hooks = hooks_scripted(flag2, valid_gga);
        stale_hooks.mono = [&clock] { return clock.value; };
        GpsFetch stale(stale_hooks);
        stale.start();
        stale.poll();
        clock.value = 1000 + kGpsFreshMs + 1;
        stale.poll();
        check(stale.state() == GpsOutcome::running, "stale fix is not accepted");
        clock.value = 1000 + kGpsTimeoutMs + 1;
        stale.poll();
        check(stale.state() == GpsOutcome::timeout && !stale.position(), "stale stream ends in timeout");
        check(*flag2, "timeout released the port");

        // 3. void-only (indoor dead air) times out.
        clock.value = 1000;
        auto flag3 = std::make_shared<bool>(false);
        GpsHooks silent_hooks = hooks_scripted(flag3, "");
        silent_hooks.mono = [&clock] { return clock.value; };
        GpsFetch silent(silent_hooks);
        silent.start();
        clock.value = 1000 + kGpsTimeoutMs - 1;
        silent.poll();
        check(silent.state() == GpsOutcome::running, "just inside budget still waiting");
        clock.value = 1000 + kGpsTimeoutMs + 1;
        silent.poll();
        check(silent.state() == GpsOutcome::timeout && !silent.position(), "dead air times out");
        check(*flag3, "timeout released the silent port");

        // 4. cancel releases the port synchronously and stays cancelled.
        auto flag4 = std::make_shared<bool>(false);
        GpsHooks cancel_hooks = hooks_scripted(flag4, valid_gga);
        cancel_hooks.mono = [&clock] { return clock.value; };
        GpsFetch cancelling(cancel_hooks);
        cancelling.start();
        cancelling.cancel();
        check(*flag4, "cancel released the port synchronously");
        check(cancelling.state() == GpsOutcome::cancelled, "cancel classifies");
        cancelling.poll();
        check(cancelling.state() == GpsOutcome::cancelled, "poll after cancel stays cancelled");

        // 5. busy port classifies immediately without a read.
        GpsHooks busy_hooks;
        busy_hooks.open = [&read_attempted] {
            return PortOpen{std::function<ssize_t(char*, std::size_t)>(
                                [&read_attempted](char*, std::size_t) {
                                    read_attempted = true; return ssize_t(-1);
                                }),
                            true};
        };
        busy_hooks.mono = [&clock] { return clock.value; };
        GpsFetch busyf(busy_hooks);
        busyf.start();
        busyf.poll();
        check(busyf.state() == GpsOutcome::busy && !read_attempted, "busy port classifies immediately");

        // 6. missing port classifies immediately.
        GpsHooks miss_hooks;
        miss_hooks.open = [] { return PortOpen{ReadFn{}, false}; };
        miss_hooks.mono = [&clock] { return clock.value; };
        GpsFetch missing(miss_hooks);
        missing.start();
        missing.poll();
        check(missing.state() == GpsOutcome::missing, "missing port classifies");
    }
    check(!rfk::serial_open("/rfk/no-such-port").busy && !rfk::serial_open("/rfk/no-such-port").read,
          "a missing port is quiet");

    // --- serial lifecycle: the read closure is the fd's sole owner, so
    // closing the closure releases the port (flock and all).
    {
        int master = ::posix_openpt(O_RDWR | O_NOCTTY);
        check(master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0, "pty master");
        const char* pts = ::ptsname(master);
        check(pts && *pts, "pty slave name");
        PortOpen first = rfk::serial_open(pts);
        check(first.read && !first.busy, "pty port opens");
        PortOpen second = rfk::serial_open(pts);
        check(second.busy, "second open while the first holds it is busy");
        {  // fetch end: the closure scope ends, taking the fd with it
            PortOpen keeper = std::move(first);
            check(static_cast<bool>(keeper.read), "closure still lives before release");
        }
        PortOpen third = rfk::serial_open(pts);
        check(third.read && !third.busy, "destroying the read closure released the port");
        ::close(master);
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
