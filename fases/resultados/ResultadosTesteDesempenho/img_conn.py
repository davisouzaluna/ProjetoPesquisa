import matplotlib.pyplot as plt
import numpy as np


quic_times = [0.003014908 * 1000, 0.003137277 * 1000]   # [3.014908, 3.137277] ms
tcp_times  = [0.000327549 * 1000, 0.000051444 * 1000]    # [0.327549, 0.051444] ms
tls_times  = [0.011757890 * 1000, 0.012989165 * 1000]    # [11.75789, 12.989165] ms

intervals = ['50', '100']
x = np.arange(len(intervals))
bar_width = 0.2

fig, ax = plt.subplots(figsize=(10, 6))

ax.bar(x - bar_width, quic_times, width=bar_width, label='QUIC', color='#D3D3D3')
ax.bar(x, tcp_times, width=bar_width, label='TCP', color='#A9A9A9')
ax.bar(x + bar_width, tls_times, width=bar_width, label='TCP+TLS', color='#333333')

ax.set_xticks(x)
ax.set_xticklabels(intervals,fontsize=13)
ax.set_xlabel('Connection Intervals (ms)',fontsize=13)
ax.set_ylabel('Connection Estabilishment Time (ms)',fontsize=13)
ax.legend(fontsize=13)
ax.grid(axis='y', linestyle='--', alpha=0.7)

plt.tight_layout()
plt.show()
