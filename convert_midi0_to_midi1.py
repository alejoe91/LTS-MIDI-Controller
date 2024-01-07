import mido
from pathlib import Path

def prepare_midi_for_synth(midi_type0_file, output_folder=None):
    type_0 = mido.MidiFile(midi_type0_file)
    if type_0.type != 0:
        return
    type_1 = mido.MidiFile(
        type=1,
        ticks_per_beat=type_0.ticks_per_beat)
    time = 0
    channels = {}
    for item in type_0.tracks[0]:
        msg = item.copy()

        if hasattr(msg, 'time'):
            time += msg.time

        channel = msg.channel if hasattr(msg, 'channel') else -1
        if channel not in channels:
            channels[channel] = {
                'time': 0,
                'msg': []
            }
        chl = channels[channel]

        if hasattr(msg, 'time'):
            msg.time = time - chl['time']
            chl['time'] = time
        chl['msg'].append(msg)

    for chan in channels:
        track = type_1.add_track()
        track.extend(channels[chan]['msg'])

    midi_filename = Path(midi_type0_file).stem
    if output_folder is None:
        output_folder = Path(".")
    else:
        output_folder = Path(output_folder)
    output_file = output_folder / f"{midi_filename}_1.mid"
    type_1.save(str(output_file))
    print("Converted: ", midi_filename, " -> ", output_file.name)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description='Convert MIDI type 0 to type 1')
    parser.add_argument('input', type=str, help='Input MIDI file')
    parser.add_argument('-o', '--output', type=str, help='Output folder')
    args = parser.parse_args()
    prepare_midi_for_synth(args.input, args.output)
