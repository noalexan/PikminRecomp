// --record-input: port-0 pad state per retrace, written in --input-script format so a run (and a
// crash) can be replayed. A line is written only when the state changes, and flushed at once.
#pragma once
#include <cstdint>
#include <string>

namespace rt {

// Writes the "# pikmin <build> clock=<fast|real-time> ..." header line. False if it can't open.
bool input_record_open(const std::string& path, const std::string& header);
void input_record(uint32_t retrace, uint16_t buttons, int8_t sx, int8_t sy, int8_t cx, int8_t cy, bool swarm);
const std::string& input_record_path();  // empty when not recording

}  // namespace rt
