import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

os.makedirs('figures', exist_ok=True)

def analyze_e1_hit_prob():
    print("Analiza E1: Probabilitatea de lovire (Rayleigh)")
    try:
        df = pd.read_csv('results/e1/events.csv')
        shots = df[df['type'] == 4] # EventType::SHOT = 4
        
        if len(shots) == 0:
            print("  [!] Niciun foc inregistrat.")
            return

        miss_dists = shots['v2'].values
        r_t = 0.3 # raza tintei din parametri
        hit_prob_sim = np.mean(miss_dists <= r_t)
        
        sigma_eff = 0.010 # valoarea de baza
        d = 20.0 # 25 (Red) - 5 (Blue)
        hit_prob_theory = 1.0 - np.exp(-(r_t**2) / (2.0 * sigma_eff**2 * d**2))
        
        print(f"  P_hit Simulat:  {hit_prob_sim:.4f}")
        print(f"  P_hit Teoretic: {hit_prob_theory:.4f}")
        print("  -> Rezultat valid!" if abs(hit_prob_sim - hit_prob_theory) < 0.05 else "  -> Necesita investigatie.")
        
        plt.hist(miss_dists, bins=50, density=True, alpha=0.6, color='r', label='Simulat')
        plt.title("E1: Distributia distantei de ratare")
        plt.legend()
        plt.savefig('figures/e1_hit_prob.png')
        plt.close()
    except Exception as e:
        print(f"Eroare E1: {e}")

def analyze_e3_detect():
    print("Analiza E3: Timpul de detectie (Exponential)")
    try:
        df = pd.read_csv('results/e3/events.csv')
        detects = df[df['type'] == 1] # EventType::DETECT = 1
        
        if len(detects) == 0:
            print("  [!] Nicio detectie inregistrata.")
            return
            
        times = detects['t'].values
        mean_time = np.mean(times)
        
        # Teoretic E[T] = 1/lambda
        dist = 30.0 # 35 - 5
        lambda_val = 3.0 * np.exp(-dist / 8.0)
        theory_time = 1.0 / lambda_val
        
        print(f"  E[T] Simulat:  {mean_time:.2f} s")
        print(f"  E[T] Teoretic: {theory_time:.2f} s")
        
        plt.hist(times, bins=20, color='b', alpha=0.7)
        plt.title("E3: Timpi de detectie")
        plt.savefig('figures/e3_detect_time.png')
        plt.close()
    except Exception as e:
        print(f"Eroare E3: {e}")

if __name__ == '__main__':
    analyze_e1_hit_prob()
    analyze_e3_detect()
