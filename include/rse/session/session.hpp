// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "types.hpp"
#include <stdexcept>
#include <vector>

namespace rse::session {
class UnsupportedOperation : public std::logic_error {
public:
    using std::logic_error::logic_error;
};
// Internal source-level application contract. See docs/SESSIONS.md. Callers
// serialize access; this interface neither owns host threads nor promises an ABI.
class Session {
public:
    virtual ~Session()=default;
    // Metadata (including its referenced strings/spans) lasts until destruction.
    [[nodiscard]] virtual const SystemInfo& info() const noexcept=0;
    [[nodiscard]] virtual Status status() const noexcept=0;
    virtual void reset()=0;
    // Inclusive system-defined master ticks; equal target is a no-op, rewind
    // is rejected. Execution failures require reset; invalid requests do not.
    virtual void advance_to(Tick target)=0;
    // Optional bounded-by-the-adapter operation: reach its next published frame.
    virtual void run_frame() {throw UnsupportedOperation("Frame stepping is unavailable");}
    // A complete pressed-action set for one port at a strictly future tick.
    // Implementations copy/translate the request before returning.
    virtual void schedule_input(Tick,std::string_view,std::span<const std::string_view>) {
        throw UnsupportedOperation("Input is unavailable");
    }
    // Null before the first publication or when video is absent. The latest
    // frame view lasts until the next non-const operation or destruction.
    [[nodiscard]] virtual std::optional<VideoFrame> video() const noexcept {return std::nullopt;}
    // Decode the current frame into exactly width*height caller-owned pixels.
    // Palette interpretation and emphasis are system/adapter responsibilities.
    virtual void convert_video(std::span<Rgba>) const {
        throw UnsupportedOperation("Video conversion is unavailable");
    }
    // Owning interleaved S16 samples. Draining does not advance simulated time.
    // Empty means no pending samples; absent audio is an unsupported operation.
    [[nodiscard]] virtual std::vector<std::int16_t> take_audio() {
        throw UnsupportedOperation("Audio is unavailable");
    }
};
}
