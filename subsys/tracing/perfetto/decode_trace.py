#!/usr/bin/env python3
"""Decode a Perfetto trace file and print each packet."""

import sys
import os

# Add the build directory to find the generated protobuf module
build_dir = os.path.join(os.path.dirname(__file__), '../../../../build/zephyr/subsys/tracing/perfetto/proto')
sys.path.insert(0, build_dir)

# Also try the nanopb generator path for nanopb_pb2
nanopb_gen = os.path.join(os.path.dirname(__file__), '../../../../build/zephyr/subsys/tracing/perfetto/nanopb/generator/proto')
sys.path.insert(0, nanopb_gen)

try:
    import perfetto_trace_pb2 as perfetto
except ImportError:
    print("Could not import perfetto_trace_pb2. Trying to generate it...")
    import subprocess
    proto_file = os.path.join(os.path.dirname(__file__), 'proto/perfetto_trace.proto')
    out_dir = os.path.dirname(__file__)
    subprocess.run(['protoc', f'--python_out={out_dir}', f'-I{os.path.dirname(proto_file)}', proto_file], check=True)
    import perfetto_trace_pb2 as perfetto


def decode_varint(data, pos):
    """Decode a varint from data starting at pos. Returns (value, new_pos)."""
    result = 0
    shift = 0
    while pos < len(data):
        byte = data[pos]
        result |= (byte & 0x7f) << shift
        pos += 1
        if not (byte & 0x80):
            break
        shift += 7
    return result, pos


def skip_field(data, pos, wire_type):
    """Skip a field based on wire type. Returns new position."""
    if wire_type == 0:  # Varint
        while pos < len(data) and data[pos] & 0x80:
            pos += 1
        return pos + 1
    elif wire_type == 1:  # 64-bit
        return pos + 8
    elif wire_type == 2:  # Length-delimited
        length, pos = decode_varint(data, pos)
        return pos + length
    elif wire_type == 5:  # 32-bit
        return pos + 4
    else:
        raise ValueError(f"Unknown wire type: {wire_type}")


def print_packet(i, packet):
    """Print a TracePacket in human-readable form."""
    print(f"\n=== Packet {i} ===")

    # Show raw serialized size and fields present
    raw_data = packet.SerializeToString()
    print(f"  [raw size: {len(raw_data)} bytes]")

    if packet.HasField('timestamp'):
        print(f"  timestamp: {packet.timestamp} ns ({packet.timestamp / 1e9:.6f} s)")

    if packet.HasField('trusted_packet_sequence_id'):
        print(f"  sequence_id: {packet.trusted_packet_sequence_id}")

    if packet.HasField('sequence_flags'):
        flags = []
        if packet.sequence_flags & 1:
            flags.append("INCREMENTAL_STATE_CLEARED")
        if packet.sequence_flags & 2:
            flags.append("NEEDS_INCREMENTAL_STATE")
        print(f"  sequence_flags: {packet.sequence_flags} ({', '.join(flags)})")

    if packet.HasField('track_descriptor'):
        td = packet.track_descriptor
        print(f"  track_descriptor:")
        print(f"    uuid: {td.uuid}")
        if td.HasField('parent_uuid'):
            print(f"    parent_uuid: {td.parent_uuid}")
        if td.name:
            print(f"    name: '{td.name}'")
        if td.HasField('process'):
            print(f"    process: pid={td.process.pid}, name='{td.process.process_name}'")
        if td.HasField('thread'):
            print(f"    thread: pid={td.thread.pid}, tid={td.thread.tid}, name='{td.thread.thread_name}'")

    if packet.HasField('track_event'):
        te = packet.track_event
        print(f"  track_event:")
        type_names = {0: "UNSPECIFIED", 1: "SLICE_BEGIN", 2: "SLICE_END", 3: "INSTANT", 4: "COUNTER"}
        print(f"    type: {type_names.get(te.type, te.type)}")
        if te.HasField('track_uuid'):
            print(f"    track_uuid: {te.track_uuid}")
        if te.HasField('name_iid'):
            print(f"    name_iid: {te.name_iid}")
        if te.category_iids:
            print(f"    category_iids: {list(te.category_iids)}")

    if packet.HasField('interned_data'):
        id = packet.interned_data
        print(f"  interned_data:")
        for en in id.event_names:
            print(f"    event_name: iid={en.iid}, name='{en.name}'")
        for ec in id.event_categories:
            print(f"    event_category: iid={ec.iid}, name='{ec.name}'")

    # Show all fields using ListFields() for debugging
    all_fields = packet.ListFields()
    known_field_nums = {8, 10, 13, 11, 60, 12}  # timestamp, seq_id, flags, track_event, track_descriptor, interned_data
    for field_desc, value in all_fields:
        if field_desc.number not in known_field_nums:
            print(f"  [unhandled field {field_desc.number} '{field_desc.name}': {repr(value)[:100]}]")

    # Decode raw field numbers from the packet data
    pos = 0
    field_numbers = []
    while pos < len(raw_data):
        tag, new_pos = decode_varint(raw_data, pos)
        field_num = tag >> 3
        wire_type = tag & 0x7
        field_numbers.append(field_num)
        try:
            pos = skip_field(raw_data, new_pos, wire_type)
        except:
            break

    unknown_fields = [f for f in field_numbers if f not in known_field_nums]
    if unknown_fields:
        print(f"  [contains fields not in our proto: {sorted(set(unknown_fields))}]")

def main():
    if len(sys.argv) < 2:
        trace_file = "/home/tannewt/repos/circuitpython/ports/zephyr-cp/zephyr/build/channel0_0"
    else:
        trace_file = sys.argv[1]

    print(f"Reading trace file: {trace_file}")

    with open(trace_file, 'rb') as f:
        data = f.read()

    print(f"File size: {len(data)} bytes")
    print(f"Raw hex (first 200 bytes): {data[:200].hex()}")

    # Parse as a Trace message (standard Perfetto format)
    trace = perfetto.Trace()
    trace.ParseFromString(data)
    packets = list(trace.packet)
    print(f"\nParsed {len(packets)} packets")

    print(f"\n{'='*60}")
    print(f"Total packets: {len(packets)}")
    print(f"{'='*60}")

    for i, packet in enumerate(packets):
        print_packet(i, packet)


if __name__ == '__main__':
    main()
