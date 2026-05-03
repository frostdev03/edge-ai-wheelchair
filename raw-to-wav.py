import wave
import numpy as np

# ==== PARAMETER AUDIO ====
sample_rate = 16000      
num_channels = 1        
sample_width = 2        

input_raw = "output.raw"
output_wav = "kanan2.wav"

print("Membaca RAW...")
with open(input_raw, "rb") as f:
    raw_data = f.read()

audio = np.frombuffer(raw_data, dtype=np.int16)

print("Menulis WAV...")
with wave.open(output_wav, "wb") as wav_file:
    wav_file.setnchannels(num_channels)
    wav_file.setsampwidth(sample_width)
    wav_file.setframerate(sample_rate)
    wav_file.writeframes(audio.tobytes())

print("Selesai! File WAV jadi:", output_wav)