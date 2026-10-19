import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

os.makedirs('figures', exist_ok=True)

def rk4_step(A, B, a, b, dt):
    # Ecuatiile Legii Patratului a lui Lanchester: dA/dt = -b*B, dB/dt = -a*A
    dA1 = -b * B
    dB1 = -a * A
    
    dA2 = -b * max(0, B + 0.5 * dt * dB1)
    dB2 = -a * max(0, A + 0.5 * dt * dA1)
    
    dA3 = -b * max(0, B + 0.5 * dt * dB2)
    dB3 = -a * max(0, A + 0.5 * dt * dA2)
    
    dA4 = -b * max(0, B + dt * dB3)
    dB4 = -a * max(0, A + dt * dA3)
    
    A_new = max(0, A + (dt / 6.0) * (dA1 + 2*dA2 + 2*dA3 + dA4))
    B_new = max(0, B + (dt / 6.0) * (dB1 + 2*dB2 + 2*dB3 + dB4))
    return A_new, B_new

def analyze_lanchester():
    print("Analiza E8: Modelul Lanchester vs Simulare Stocastica")
    
    # Rata aproximativa de ucidere a unui agent (TTK mediu teoretic)
    a_rate = 0.5 
    b_rate = 0.5
    
    A, B = 5.0, 5.0
    dt = 0.1
    time_points = np.arange(0, 15, dt)
    A_hist, B_hist = [A], [B]
    
    for _ in time_points[1:]:
        A, B = rk4_step(A, B, a_rate, b_rate, dt)
        A_hist.append(A)
        B_hist.append(B)
        
    plt.plot(time_points, A_hist, 'b--', label='Blue Supravietuitori (ODE RK4)')
    plt.plot(time_points, B_hist, 'r--', label='Red Supravietuitori (ODE RK4)')
    
    plt.title("E8: Ecuatiile Lanchester (Predictie Matematica)")
    plt.xlabel("Timp (s)")
    plt.ylabel("Agenti in viata")
    plt.legend()
    plt.grid(True)
    plt.savefig('figures/e8_lanchester.png')
    plt.close()
    print("  -> Graficul matematic a fost generat in figures/e8_lanchester.png")

if __name__ == '__main__':
    analyze_lanchester()
