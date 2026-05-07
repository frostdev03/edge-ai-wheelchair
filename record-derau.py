import serial
import wave
import numpy as np
import os
import time
import signal
import sys

# ==== KONFIGURASI ====
PORT = 'COM4'
BAUD = 115200
DURASI_REKAM = 30  # detik per segmen

# ==== KONFIGURASI DATASET (HARDCODE) ====
NAMA_KELAS = "derau"

# ==== PARAMETER AUDIO ====
sample_rate = 16000
num_channels = 1
sample_width = 2  # bytes (int16)

# ==== FLAG UNTUK CTRL+C ====
stop_requested = False

def handle_sigint(sig, frame):
    global stop_requested
    print("\n\n[INFO] Ctrl+C diterima, menyelesaikan segmen saat ini lalu berhenti...")
    stop_requested = True

signal.signal(signal.SIGINT, handle_sigint)

# ==== FUNGSI AUTO-INCREMENT NAMA FILE ====
def get_next_filename():
    index = 1
    while os.path.exists(f"{NAMA_KELAS}{index}.wav"):
        index += 1
    return f"{NAMA_KELAS}{index}.wav"

# ==== FUNGSI SIMPAN WAV ====
def save_wav(raw_data, filename):
    if len(raw_data) == 0:
        print(f"⚠️  Segmen kosong, dilewati.")
        return

    # ✅ FIX: Pastikan panjang buffer kelipatan 2 (int16 = 2 bytes)
    if len(raw_data) % 2 != 0:
        raw_data = raw_data[:-1]

    audio = np.frombuffer(raw_data, dtype=np.int16)
    with wave.open(filename, "wb") as wav_file:
        wav_file.setnchannels(num_channels)
        wav_file.setsampwidth(sample_width)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(audio.tobytes())

    durasi_aktual = len(audio) / sample_rate
    print(f"✅ Tersimpan: {filename} ({durasi_aktual:.1f} detik, {len(raw_data)} bytes)")

# ==== MAIN ====
print("=========================================")
print(f"  Perekam Dataset Otomatis ESP32-S3 Zero ")
print(f"  Kelas     : {NAMA_KELAS}")
print(f"  Durasi    : {DURASI_REKAM} detik per segmen")
print(f"  Stop      : Ctrl+C (selesaikan segmen dulu)")
print("=========================================\n")

try:
    ser = serial.Serial(PORT, BAUD)
    print(f"[INFO] Terhubung ke port {PORT}.\n")
except Exception as e:
    print(f"❌ Gagal membuka port {PORT}: {e}")
    sys.exit(1)

segmen_berhasil = 0

try:
    while not stop_requested:
        output_wav = get_next_filename()
        raw_data = bytearray()

        # Kirim perintah mulai rekam
        ser.write(b'r')
        waktu_mulai = time.time()

        print(f"🔴 [SEGMEN {segmen_berhasil + 1}] Merekam → {output_wav} ...")

        # Loop rekam selama DURASI_REKAM detik
        while True:
            elapsed = time.time() - waktu_mulai
            sisa = DURASI_REKAM - elapsed
            print(f"\r   ⏱  {max(sisa, 0):.1f} detik tersisa...   ", end="", flush=True)

            if elapsed >= DURASI_REKAM:
                break

            if ser.in_waiting > 0:
                raw_data.extend(ser.read(ser.in_waiting))

            time.sleep(0.01)

        # Baca sisa data di buffer sebelum stop
        time.sleep(0.05)
        if ser.in_waiting > 0:
            raw_data.extend(ser.read(ser.in_waiting))

        # Kirim perintah stop ke ESP32
        ser.write(b's')
        print()  # newline setelah countdown

        # Simpan file
        save_wav(raw_data, output_wav)
        segmen_berhasil += 1

        if not stop_requested:
            print(f"   ▶ Mulai segmen berikutnya...\n")
            time.sleep(0.2)

finally:
    # ✅ Port ditutup SETELAH semua proses selesai
    if ser.is_open:
        ser.write(b's')
        ser.close()
    print(f"\n[INFO] Port serial ditutup.")
    print(f"✅ Selesai. Total segmen terekam: {segmen_berhasil}")