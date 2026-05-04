import serial
import wave
import numpy as np
import os

# ==== KONFIGURASI ====
PORT = 'COM4'
BAUD = 115200

# ==== KONFIGURASI DATASET (HARDCODE) ====
# Ubah teks di bawah ini sesuai perintah yang sedang kamu rekam massal!
# (Contoh: "maju", "mundur", "kiri", "kanan", "stop", "derau")
NAMA_KELAS = "derau"

# ==== PARAMETER AUDIO ====
sample_rate = 16000      
num_channels = 1        
sample_width = 2        

# ==== LOGIKA AUTO-INCREMENT NAMA FILE ====
index = 1
while os.path.exists(f"{NAMA_KELAS}{index}.wav"):
    index += 1

output_wav = f"{NAMA_KELAS}{index}.wav"

print("=========================================")
print(f"  Perekam Dataset Otomatis ESP32-S3 Zero ")
print(f"  Target File : {output_wav}")
print("=========================================")

# Buffer untuk menyimpan data raw di memori (RAM)
raw_data = bytearray()

try:
    # Membuka koneksi Serial
    ser = serial.Serial(PORT, BAUD)
    print(f"\n[INFO] Terhubung ke port {PORT}.")
    
    # Mengirim perintah rekam
    print("[INFO] Mengirim perintah mulai rekam (r)...")
    ser.write(b'r')
    
    print(f"\n🔴 [MEREKAM] Ucapkan '{NAMA_KELAS}' sekarang!")
    print("⏹️  Tekan Ctrl+C untuk BERHENTI dan AUTO-SAVE.\n")
    
    # Loop Perekaman
    while True:
        if ser.in_waiting > 0:
            # Membaca data yang masuk dan memasukkannya ke buffer
            data = ser.read(ser.in_waiting)
            raw_data.extend(data)
            
except KeyboardInterrupt:
    print("\n\n[INFO] Proses perekaman dihentikan.")
    # Mengirim perintah stop ke ESP32
    if 'ser' in locals() and ser.is_open:
        ser.write(b's')
        ser.close()
        print("[INFO] Port serial ditutup.")

# ==== PROSES KONVERSI & AUTO-SAVE ====
print(f"[INFO] Total data RAW terkumpul: {len(raw_data)} bytes")

if len(raw_data) > 0:
    print(f"[INFO] Mengonversi data ke format WAV...")
    
    # Mengubah array byte menjadi array numpy 16-bit
    audio = np.frombuffer(raw_data, dtype=np.int16)

    # Menyimpan menjadi file WAV
    with wave.open(output_wav, "wb") as wav_file:
        wav_file.setnchannels(num_channels)
        wav_file.setsampwidth(sample_width)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(audio.tobytes())

    print(f"✅ SUKSES! File auto-save sebagai: {output_wav}")
else:
    print("❌ GAGAL: Tidak ada data audio yang terekam. Pastikan ESP32 mengirim data.")