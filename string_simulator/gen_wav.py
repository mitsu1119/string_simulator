from math import *
import struct
import wave

def save(wav, filename):
    max = 0.0
    for i in range(len(wav)):
        if max < abs(wav[i]):
            max = abs(wav[i])
    print("max: ", max)
    amp = 32000.0 / max
    for i in range(len(wav)):
        # wav[i] = struct.pack('h', int(wav[i] / max * 32767.0 / 2))
        wav[i] = struct.pack('h', int(wav[i]))
    wavefile = wave.open(filename, 'w')
    wavefile.setnchannels(1)
    wavefile.setsampwidth(2)
    wavefile.setframerate(48000)
    wavefile.writeframes(b''.join(wav))
    wavefile.close()

with open('amps.txt', 'r') as f:
    wav = eval(f.read())

save(wav, 'output.wav')