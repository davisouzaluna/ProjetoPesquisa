import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
print(os.getcwd())
def calculate_mean(filename):
    data = pd.read_csv(filename)
    return data['Latência (ms)'].mean()

# Load means for each protocol and interval
quic_50 = calculate_mean('../comparacao_50ms_1k_pacotes/comparação estatistica/latencias_50msQuic.csv')
tcp_50 = calculate_mean('../comparacao_50ms_1k_pacotes/comparação estatistica/latencias_50msTCP.csv')
tls_50 = calculate_mean('../comparacao_50ms_1k_pacotes/comparação estatistica/latencias_50msTLS.csv')

quic_100 = calculate_mean('comparação estatistica/latencias_100msQuic.csv')
tcp_100 = calculate_mean('comparação estatistica/latencias_100msTCP.csv')
tls_100 = calculate_mean('comparação estatistica/latencias_100msTLS.csv')


# Convert means from milliseconds to microseconds
means_quic = [quic_50 * 1000, quic_100 * 1000]
means_tcp  = [tcp_50 * 1000, tcp_100 * 1000]
means_tls  = [tls_50 * 1000, tls_100 * 1000]

intervals = ['50', '100']
x = np.arange(len(intervals))
bar_width = 0.2

fig, ax = plt.subplots(figsize=(10, 6))
ax.bar(x - bar_width, means_quic, width=bar_width, label='QUIC', color='#D3D3D3')
ax.bar(x, means_tcp, width=bar_width, label='TCP', color='#A9A9A9') 
ax.bar(x + bar_width, means_tls, width=bar_width, label='TCP+TLS', color='#333333')  

ax.set_xticks(x)
ax.set_xticklabels(intervals, fontsize=13)
ax.set_xlabel('Publish Intervals (ms)', fontsize=13)
ax.set_ylabel('Publish Time (µs)', fontsize=13)
ax.legend(fontsize=13)
ax.grid(axis='y', linestyle='--', alpha=0.7)

plt.tight_layout()
plt.show()
