import audiomentations as A
import soundfile as sf
import librosa
import numpy as np
import os

# Define augmentasi
augment = A.Compose([
    A.AddGaussianSNR(min_snr_db=5, max_snr_db=20, p=0.5),
    A.PitchShift(min_semitones=-2, max_semitones=2, p=0.5),
    A.TimeStretch(min_rate=0.9, max_rate=1.1, p=0.4),
    A.Shift(min_shift=-0.1, max_shift=0.1, p=0.3),
    A.AddBackgroundNoise(
        sounds_path="./noise_files/",  # folder noise
        min_snr_db=5,
        max_snr_db=15,
        p=0.6
    ),
])

def augment_dataset(input_dir, output_dir, n_augment=4):
    """
    input_dir  : folder berisi file .wav asli per kelas
    n_augment  : berapa versi augmentasi per file
    """
    for class_name in os.listdir(input_dir):
        class_path = os.path.join(input_dir, class_name)
        out_path   = os.path.join(output_dir, class_name)
        os.makedirs(out_path, exist_ok=True)

        for fname in os.listdir(class_path):
            audio, sr = librosa.load(os.path.join(class_path, fname), sr=16000)

            # Simpan file asli
            sf.write(os.path.join(out_path, fname), audio, sr)

            # Generate versi augmentasi
            for i in range(n_augment):
                aug_audio = augment(audio, sample_rate=sr)
                new_name  = f"{fname[:-4]}_aug{i}.wav"
                sf.write(os.path.join(out_path, new_name), aug_audio, sr)

        print(f"✅ {class_name} selesai")

# Jalankan
augment_dataset("./dataset/", "./dataset_augmented/", n_augment=1)
# 400 sampel × 5 (1 asli + 4 augmentasi) = 2000 sampel/kelas