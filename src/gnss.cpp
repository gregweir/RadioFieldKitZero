// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "gnss.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>

#include <memory>
#include <fcntl.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

namespace rfk {
namespace {

// 1 knot = 1852 m / 3600 s (the NMEA RMC speed field is knots) — RFK ignores
// speed and course, so only the pending-buffer constant lives here.
constexpr std::size_t kPendingLimit = 2048; // bytes kept awaiting a newline

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// XOR of the characters between '$' and the '* checksum separator, compared
// against the two hex digits that follow it.
bool checksum_ok(const std::string& line) {
    if (line.size() < 4 || line[0] != '$') return false;
    const std::size_t star = line.rfind('*');
    if (star == std::string::npos || star + 3 > line.size()) return false;
    std::uint8_t sum = 0;
    for (std::size_t index = 1; index < star; ++index)
        sum = static_cast<std::uint8_t>(sum ^ static_cast<std::uint8_t>(line[index]));
    const int high = hex_nibble(line[star + 1]), low = hex_nibble(line[star + 2]);
    if (high < 0 || low < 0) return false;
    return sum == static_cast<std::uint8_t>((high << 4) | low);
}

// NMEA ddmm.mmmm (lat) / dddmm.mmmm (lon) prefix to decimal degrees; the
// hemisphere sign is applied by the caller. Empty or too-short fields fail.
bool parse_deg_minutes(const std::string& text, std::size_t degree_digits, double* out) {
    if (text.size() < degree_digits + 1) return false;
    try {
        const double degrees = std::stod(text.substr(0, degree_digits));
        const double minutes = std::stod(text.substr(degree_digits));
        if (!std::isfinite(degrees) || !std::isfinite(minutes)) return false;
        *out = degrees + minutes / 60.0;
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// The lat/lon pair from $GxGGA fields 2..5 / $GxRMC fields 3..6; the
// hemisphere fields must be N/S and E/W.
bool parse_position(const std::vector<std::string>& fields, std::size_t lat, std::size_t ns,
                    std::size_t lon, std::size_t ew, double* latitude, double* longitude) {
    if (fields.size() <= ew || fields[ns].empty() || fields[ew].empty()) return false;
    double north = 0.0, east = 0.0;
    if (!parse_deg_minutes(fields[lat], 2, &north) || !parse_deg_minutes(fields[lon], 3, &east))
        return false;
    if ((fields[ns] != "N" && fields[ns] != "S") || (fields[ew] != "E" && fields[ew] != "W"))
        return false;
    if (fields[ns] == "S") north = -north;
    if (fields[ew] == "W") east = -east;
    if (std::fabs(north) > 90.0 || std::fabs(east) > 180.0) return false;
    *latitude = north;
    *longitude = east;
    return true;
}

std::vector<std::string> split_fields(const std::string& body, std::vector<std::string>& out) {
    out.clear();
    std::size_t begin = 0;
    while (true) {
        const std::size_t comma = body.find(',', begin);
        out.push_back(body.substr(begin, comma == std::string::npos ? comma : comma - begin));
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return std::move(out);
}

bool sentence_kind(const std::string& line, const char* kind) {
    // $GxGGA / $GxRMC with any talker id (GP, GN, GL, GA, GB, ...).
    return line.size() >= 6 && line[3] == kind[0] && line[4] == kind[1] && line[5] == kind[2];
}

std::int64_t steady_ms() {
    return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
}

} // namespace

GnssSource::GnssSource(ReadFn read_fn, MonoFn mono_ms) : read_fn_(std::move(read_fn)), mono_ms_(std::move(mono_ms)) {}

void GnssSource::poll() {
    if (!read_fn_) return;
    char buffer[512];
    ssize_t count;
    while ((count = read_fn_(buffer, sizeof(buffer))) > 0) {
        pending_.append(buffer, static_cast<std::size_t>(count));
        std::size_t newline;
        while ((newline = pending_.find('\n')) != std::string::npos) {
            std::string line = pending_.substr(0, newline);
            pending_.erase(0, newline + 1);
            handle_line(std::move(line));
        }
        // A stream without newlines must not grow without bound; keep only
        // the tail, which is where the next complete line will end.
        if (pending_.size() > kPendingLimit) pending_.erase(0, pending_.size() - kPendingLimit);
    }
}

void GnssSource::handle_line(std::string&& line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!checksum_ok(line)) return;
    // Fields are split without the '$', talker/type token first, and with
    // the checksum suffix removed.
    line.resize(line.rfind('*'));
    if (line.size() < 6) return;
    const std::vector<std::string> fields = split_fields(line.substr(1), scratch_);
    if (sentence_kind(line, "GGA")) apply_gga(fields);
    else if (sentence_kind(line, "RMC")) apply_rmc(fields);
    // Other checksummed sentence types are ignored.
}

// $GxGGA,time,lat,N/S,lon,E/W,quality,... : quality 0 (or empty) is the
// void-fix marker, quality > 0 comes with the position.
void GnssSource::apply_gga(const std::vector<std::string>& fields) {
    if (fields.size() < 7) return;
    const std::string& quality = fields[6];
    if (quality.empty() || quality == "0") {
        mark_void();
        return;
    }
    double latitude = 0.0, longitude = 0.0;
    if (parse_position(fields, 2, 3, 4, 5, &latitude, &longitude)) {
        fix_.pos = {latitude, longitude};
        apply_valid_fix();
    }
    // A quality>0 sentence with an unparseable position is malformed: no
    // state change.
}

// $GxRMC,time,status,lat,N/S,lon,E/W,... : status A = valid fix, V = void.
void GnssSource::apply_rmc(const std::vector<std::string>& fields) {
    if (fields.size() < 3) return;
    const std::string& status = fields[2];
    if (status == "V" || status.empty()) {
        mark_void();
        return;
    }
    if (status != "A") return;
    double latitude = 0.0, longitude = 0.0;
    if (!parse_position(fields, 3, 4, 5, 6, &latitude, &longitude)) return;
    fix_.pos = {latitude, longitude};
    apply_valid_fix();
}

void GnssSource::apply_valid_fix() {
    fix_.valid = true;
    fix_.fix_mono_ms = mono_ms_ ? mono_ms_() : steady_ms();
}

void GnssSource::mark_void() {
    // Void never clears the last-known position, only validity.
    fix_.valid = false;
}

GnssFix GnssSource::fix() const { return fix_; }

GpsFetch::GpsFetch(GpsHooks hooks) : hooks_(std::move(hooks)) {}

void GpsFetch::start() {
    if (state_ == GpsOutcome::running) return;
    const PortOpen port = hooks_.open ? hooks_.open() : serial_open();
    start_mono_ms_ = hooks_.mono ? hooks_.mono() : steady_ms();
    position_.reset();
    if (port.busy) {
        state_ = GpsOutcome::busy;
    } else if (!port.read) {
        state_ = GpsOutcome::missing;
    } else {
        source_.emplace(port.read, hooks_.mono);
        state_ = GpsOutcome::running;
    }
}

GpsOutcome GpsFetch::poll() {
    if (state_ != GpsOutcome::running) return state_;
    const auto mono = [&] { return hooks_.mono ? hooks_.mono() : steady_ms(); };
    // Accept the fix observed on the previous tick, fresh for this attempt:
    // parsed after start() and within the window at evaluation time.
    const GnssFix observed = source_->fix();
    if (observed.valid && observed.fix_mono_ms >= start_mono_ms_ &&
        mono() - observed.fix_mono_ms <= kGpsFreshMs) {
        position_ = observed.pos;
        source_.reset();
        state_ = GpsOutcome::fix;
        return state_;
    }
    source_->poll();
    if (mono() - start_mono_ms_ >= kGpsTimeoutMs) {
        source_.reset();
        state_ = GpsOutcome::timeout;
    }
    return state_;
}

GpsOutcome GpsFetch::state() const { return state_; }
std::optional<Position> GpsFetch::position() const { return position_; }
bool GpsFetch::running() const { return state_ == GpsOutcome::running; }

void GpsFetch::cancel() {
    if (state_ != GpsOutcome::running) return;
    source_.reset();
    state_ = GpsOutcome::cancelled;
}

// The ATGM336H sits on /dev/ttyS0 at 115200 8N1 (the Cap's GNSS receiver);
// the port is opened raw and non-blocking so read_fn never stalls the UI
// loop. Compiled for every Linux build so tests can exercise the lifecycle
// through a pty. The flock keeps two copies of this app (or any locking
// reader) off the same stream; a reader that does not lock is not detected
// here and garbles both. Busy is only the expected conflict, EWOULDBLOCK /
// EAGAIN — any other flock failure leaves the port usable unlocked rather
// than blocking the feature.
namespace {
struct FdCloser {
    int fd;
    explicit FdCloser(int value) : fd(value) {}
    FdCloser(const FdCloser&) = delete;  // one guard per descriptor
    FdCloser& operator=(const FdCloser&) = delete;
    ~FdCloser() { ::close(fd); }
};
} // namespace

PortOpen serial_open(const char* port) {
    errno = 0;
    const int fd = ::open(port, O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return {ReadFn{}, errno == EACCES || errno == EPERM || errno == EBUSY};
    auto owned = std::make_shared<const FdCloser>(fd);
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0 &&
        (errno == EWOULDBLOCK || errno == EAGAIN))
        return {ReadFn{}, true};  // owned drops: fd closed here
    termios tio{};
    if (::tcgetattr(fd, &tio) == 0) {
        ::cfmakeraw(&tio);
        ::cfsetispeed(&tio, B115200);
        ::cfsetospeed(&tio, B115200);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        if (::tcsetattr(fd, TCSANOW, &tio) == 0)
            return {[owned](char* out, std::size_t size) -> ssize_t {
                ssize_t count;
                do {
                    count = ::read(owned->fd, out, size);
                } while (count < 0 && errno == EINTR);
                return count;
            }, false};
    }
    return {ReadFn{}, false};  // owned drops: fd closed on a failed config
}

} // namespace rfk