import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile

try:
    # Read audio files
    sr1, y1 = wavfile.read('maju-di-dataset.wav')
    sr2, y2 = wavfile.read('maju-baudrate-2jt.wav')

    # Normalize audio for better visualization (optional but good practice)
    y1 = y1 / np.max(np.abs(y1))
    y2 = y2 / np.max(np.abs(y2))

    # Create figure
    fig, ax = plt.subplots(nrows=2, ncols=2, figsize=(14, 8))

    # Time axes
    t1 = np.arange(len(y1)) / float(sr1)
    t2 = np.arange(len(y2)) / float(sr2)

    # 1. Waveform - Dataset Lama
    ax[0, 0].plot(t1, y1, color='blue', alpha=0.7)
    ax[0, 0].set_title('Waveform: Baud Rate 115.200 (Dataset Lama)')
    ax[0, 0].set_ylabel('Amplitude (Normalized)')

    # 2. Waveform - Dataset Baru
    ax[0, 1].plot(t2, y2, color='green', alpha=0.7)
    ax[0, 1].set_title('Waveform: Baud Rate 2.000.000 (Baru)')
    ax[0, 1].set_ylabel('Amplitude (Normalized)')

    # 3. Spectrogram - Dataset Lama
    Pxx1, freqs1, bins1, im1 = ax[1, 0].specgram(y1, NFFT=512, Fs=sr1, noverlap=256, cmap='magma')
    ax[1, 0].set_title('Spectrogram: Baud Rate 115.200')
    ax[1, 0].set_xlabel('Time (s)')
    ax[1, 0].set_ylabel('Frequency (Hz)')
    fig.colorbar(im1, ax=ax[1, 0])

    # 4. Spectrogram - Dataset Baru
    Pxx2, freqs2, bins2, im2 = ax[1, 1].specgram(y2, NFFT=512, Fs=sr2, noverlap=256, cmap='magma')
    ax[1, 1].set_title('Spectrogram: Baud Rate 2.000.000')
    ax[1, 1].set_xlabel('Time (s)')
    ax[1, 1].set_ylabel('Frequency (Hz)')
    fig.colorbar(im2, ax=ax[1, 1])

    plt.tight_layout()
    plt.savefig('audio_comparison.png', dpi=150)
    plt.close()
    print("Grafik berhasil dibuat menggunakan scipy dan matplotlib.")
except Exception as e:
    print(f"Error: {e}")