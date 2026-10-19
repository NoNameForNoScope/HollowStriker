import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

os.makedirs('figures', exist_ok=True)

def analyze_mission():
    print("Analiza E7: Misiune Monte Carlo")
    try:
        df = pd.read_csv('results/e7/replications.csv')
        # Calculam rata de succes
        success_rate = np.mean(df['outcome'] == 'blue_win')
        print(f"  Rata de succes (Blue Win): {success_rate * 100:.1f}%")
        
        plt.hist(df['t_end'], bins=15, color='green', alpha=0.7, edgecolor='black')
        plt.title(f"E7: Timpul de misiune\n(Rata Succes: {success_rate * 100:.1f}%)")
        plt.xlabel("Timp (s)")
        plt.ylabel("Numar de simulari")
        plt.grid(axis='y', alpha=0.5)
        plt.savefig('figures/e7_mission_time.png')
        plt.close()
    except Exception as e:
        print(f"Eroare E7: {e}")

if __name__ == '__main__':
    analyze_mission()
