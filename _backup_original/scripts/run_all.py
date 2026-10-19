import os, subprocess, sys

def run_cmd(cmd):
    print(f"\n>>> Execut: {cmd}")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"Eroare la executia: {cmd}")
        sys.exit(1)

def main():
    print("=== BODYCAM-MINI: CAMPANIA FINALA ===")
    
    print("\n--- 1. COMPILARE ---")
    run_cmd("cmake --build build --config Release")
    
    exe = ".\\build\\Release\\bodycam_exp.exe" if os.name == 'nt' else "./build/bodycam_exp"
    
    print("\n--- 2. SIMULARI ANALITICE (E1, E3, E8) ---")
    run_cmd(f"{exe} --scenario assets/scenarios/e1_range.ini --reps 200 --seed 111 --out results/e1 --events")
    run_cmd(f"{exe} --scenario assets/scenarios/e3_detect.ini --reps 200 --seed 222 --out results/e3 --events")
    run_cmd(f"{exe} --scenario assets/scenarios/e8_arena.ini --reps 100 --seed 333 --out results/e8")
    
    print("\n--- 3. SIMULARE MISIUNE MONTE CARLO (E7) ---")
    # Generam 100 de universuri alternative pentru aceeasi misiune
    run_cmd(f"{exe} --scenario assets/scenarios/e7_mission.ini --reps 100 --seed 777 --out results/e7")
    
    print("\n--- 4. GENERARE GRAFICE SI ANALIZE ---")
    run_cmd("python analysis/f5_validation.py")
    run_cmd("python analysis/f7_lanchester.py")
    run_cmd("python analysis/f6_mission.py")
    
    print("\n--- 5. GENERARE RAPORT FINAL ---")
    run_cmd("python scripts/make_report.py")
    
    print("\nFelicitari! Proiectul este GATA. Toate graficele sunt in figures/, iar raportul este in docs/raport.md")

if __name__ == "__main__":
    main()
