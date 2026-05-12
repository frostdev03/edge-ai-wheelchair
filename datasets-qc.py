import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile

# Ganti dengan nama file dataset lama milikmu
nama_file = 'kiri-add1.wav' 

# Baca file WAV
samplerate, data = wavfile.read(nama_file)

# Tampilkan informasi matematis
print("=== ANALISIS FILE WAV ===")
print(f"Sample Rate : {samplerate} Hz")
print(f"Total Sampel: {len(data)}")
print(f"Durasi      : {len(data)/samplerate:.2f} detik")
print(f"Amplitudo Maks: {np.max(data)}")
print(f"Amplitudo Min : {np.min(data)}")

# Cek apakah suara menabrak batas maksimal (Clipping/Pecah)
if np.max(data) >= 32767 or np.min(data) <= -32768:
    print("⚠️ PERINGATAN: Terdeteksi CLIPPING (Suara Pecah/Terdistorsi)!")

# Visualisasikan Gelombang Suara
plt.figure(figsize=(12, 4))
plt.plot(data, color='blue', alpha=0.7)
plt.title(f"Bentuk Gelombang Suara: {nama_file}")
plt.xlabel("Titik Sampel")
plt.ylabel("Amplitudo (Nilai 16-bit)")
plt.grid(True)
plt.show()