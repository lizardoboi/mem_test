import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("results.csv", sep=';')

# Convert numeric columns
num_cols = [
    "BlockSize", "BufferSize", "LaunchNum",
    "WriteBandwidth", "ReadBandwidth",
    "AbsError(write)", "RelError(write)",
    "AbsError(read)", "RelError(read)",
    "AverageWriteTime", "AverageReadTime"
]

for c in num_cols:
    if c in df.columns:
        df[c] = pd.to_numeric(df[c], errors='coerce')

plt.figure(figsize=(12, 7))
for mem in df['MemoryType'].unique():
    subset = df[df['MemoryType'] == mem]
    numeric_subset = subset[num_cols]  # только числовые колонки
    subset_grouped = subset.groupby("BlockSize")[["WriteBandwidth", "ReadBandwidth"]].mean()

    plt.plot(subset_grouped.index, subset_grouped["WriteBandwidth"],
             marker="o", label=f"{mem} Write")
    plt.plot(subset_grouped.index, subset_grouped["ReadBandwidth"],
             marker="s", label=f"{mem} Read")

plt.xscale("log")
plt.xlabel("BlockSize (bytes)")
plt.ylabel("Bandwidth (MB/s)")
plt.title("Пропускная способность чтения/записи vs BlockSize")
plt.grid(True)
plt.legend()
plt.savefig("bandwidth_vs_blocksize.png", dpi=300)
plt.close()

plt.figure(figsize=(12, 7))
for mem in df['MemoryType'].unique():
    subset = df[df['MemoryType'] == mem]
    grouped = subset.groupby("BlockSize")[["AbsError(write)", "AbsError(read)"]].mean()

    plt.plot(grouped.index, grouped["AbsError(write)"],
             marker="o", label=f"{mem} AbsErr Write")
    plt.plot(grouped.index, grouped["AbsError(read)"],
             marker="s", label=f"{mem} AbsErr Read")

plt.xscale("log")
plt.yscale("log")
plt.xlabel("BlockSize (bytes)")
plt.ylabel("Absolute Error (sec)")
plt.title("Абсолютная погрешность vs BlockSize")
plt.grid(True)
plt.legend()
plt.savefig("error_abs_vs_blocksize.png", dpi=300)
plt.close()

# Relative error plot
plt.figure(figsize=(12, 7))
for mem in df['MemoryType'].unique():
    subset = df[df['MemoryType'] == mem]
    grouped = subset.groupby("BlockSize")[["RelError(write)", "RelError(read)"]].mean()

    plt.plot(grouped.index, grouped["RelError(write)"],
             marker="o", label=f"{mem} RelErr Write")
    plt.plot(grouped.index, grouped["RelError(read)"],
             marker="s", label=f"{mem} RelErr Read")

plt.xscale("log")
plt.xlabel("BlockSize (bytes)")
plt.ylabel("Relative Error (%)")
plt.title("Относительная погрешность vs BlockSize")
plt.grid(True)
plt.legend()
plt.savefig("error_rel_vs_blocksize.png", dpi=300)
plt.close()

plt.figure(figsize=(12, 7))
grouped = df.groupby("LaunchNum")[["RelError(write)", "RelError(read)"]].mean()

plt.plot(grouped.index, grouped["RelError(write)"], marker="o", label="Write")
plt.plot(grouped.index, grouped["RelError(read)"], marker="s", label="Read")

plt.xlabel("LaunchNum")
plt.ylabel("Relative Error (%)")
plt.title("Погрешность измерений vs число испытаний")
plt.grid(True)
plt.legend()
plt.savefig("error_vs_launchnum.png", dpi=300)
plt.close()

storage_types = ["SSD", "HDD", "flash"]

for mem in storage_types:
    if mem not in df['MemoryType'].unique():
        continue

    subset = df[df['MemoryType'] == mem]
    plt.figure(figsize=(12, 7))

    for bsize in sorted(subset["BlockSize"].unique()):
        sub = subset[subset["BlockSize"] == bsize]
        grouped = sub.groupby("BufferSize")[["WriteBandwidth"]].mean()

        plt.plot(grouped.index, grouped["WriteBandwidth"],
                 marker="o", label=f"BlockSize={bsize/1024/1024:.0f} MB")

    plt.xscale("log")
    plt.xlabel("BufferSize (bytes)")
    plt.ylabel("Write Bandwidth (MB/s)")
    plt.title(f"Влияние BufferSize на пропускную способность ({mem})")
    plt.grid(True)
    plt.legend()
    plt.savefig(f"buffer_effect_{mem}.png", dpi=300)
    plt.close()

print("Графики успешно сохранены!")