#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Skrypt do analizy wyników benchmarku wydajności
Autor: System analizy danych
Data: 2025-06-09
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from scipy import stats
import warnings
warnings.filterwarnings('ignore')

# Konfiguracja polskich fontów i stylu wykresów
plt.rcParams['font.size'] = 10
plt.rcParams['axes.titlesize'] = 12
plt.rcParams['axes.labelsize'] = 10
plt.rcParams['xtick.labelsize'] = 9
plt.rcParams['ytick.labelsize'] = 9
plt.rcParams['legend.fontsize'] = 9
plt.rcParams['figure.titlesize'] = 14

# Ustawienie stylu seaborn
sns.set_style("whitegrid")
sns.set_palette("husl")

def wczytaj_dane(sciezka_pliku):
    """Wczytuje dane z pliku CSV i wykonuje podstawowe czyszczenie."""
    print("Wczytywanie danych...")
    dane = pd.read_csv(sciezka_pliku)
    
    print(f"Wczytano {len(dane)} wierszy")
    print(f"Kolumny: {list(dane.columns)}")
    
    # Debug: sprawdź kolumnę success
    if 'success' in dane.columns:
        print(f"Unikalne wartości success: {dane['success'].unique()}")
        print(f"Typ kolumny success: {dane['success'].dtype}")
        
        # Czyszczenie kolumny success - usunięcie białych znaków
        dane['success_clean'] = dane['success'].astype(str).str.strip()
        
        # Filtrowanie tylko udanych testów
        dane_udane = dane[dane['success_clean'] == 'True'].copy()
        
        print(f"Udane testy po czyszczeniu: {len(dane_udane)}")
        
    else:
        print("BŁĄD: Brak kolumny 'success'!")
        return pd.DataFrame()
    
    if len(dane_udane) > 0:
        print(f"Dostępne języki: {sorted(dane_udane['config_language'].unique())}")
        print(f"Dostępne implementacje: {sorted(dane_udane['config_implementation'].unique())}")
    
    return dane_udane

def podstawowe_statystyki(dane):
    """Generuje podstawowe statystyki opisowe."""
    print("\n" + "="*60)
    print("PODSTAWOWE STATYSTYKI WYDAJNOŚCI")
    print("="*60)
    
    metryki = [
        'messages_per_second', 'throughput_mbps', 'cpu_usage_percent', 
        'memory_usage_mb', 'avg_operation_time_us', 'connection_rate'
    ]
    
    polskie_nazwy = {
        'messages_per_second': 'Wiadomości/s',
        'throughput_mbps': 'Przepustowość (Mbps)',
        'cpu_usage_percent': 'Użycie CPU (%)',
        'memory_usage_mb': 'Użycie pamięci (MB)',
        'avg_operation_time_us': 'Śr. czas operacji (μs)',
        'connection_rate': 'Współczynnik połączeń'
    }
    
    statystyki = dane[metryki].describe()
    statystyki.index = ['Liczba', 'Średnia', 'Odch. std.', 'Min', '25%', '50%', '75%', 'Max']
    statystyki.columns = [polskie_nazwy[col] for col in statystyki.columns]
    
    print(statystyki.round(2))
    return statystyki

def analiza_wedlug_jezyka_implementacji(dane):
    """Analiza wydajności według języka i implementacji."""
    print("\n" + "="*60)
    print("ANALIZA WEDŁUG JĘZYKA I IMPLEMENTACJI")
    print("="*60)
    
    # Grupowanie danych
    grupy = dane.groupby(['config_language', 'config_implementation'])
    
    metryki = ['messages_per_second', 'throughput_mbps', 'cpu_usage_percent', 'memory_usage_mb']
    polskie_nazwy = {
        'messages_per_second': 'Wiadomości/s',
        'throughput_mbps': 'Przepustowość (Mbps)',
        'cpu_usage_percent': 'Użycie CPU (%)',
        'memory_usage_mb': 'Użycie pamięci (MB)'
    }
    
    wyniki = []
    for (jezyk, implementacja), grupa in grupy:
        wiersz = {'Język': jezyk, 'Implementacja': implementacja}
        for metryka in metryki:
            wiersz[polskie_nazwy[metryka]] = grupa[metryka].mean()
        wyniki.append(wiersz)
    
    df_wyniki = pd.DataFrame(wyniki)
    df_wyniki = df_wyniki.round(2)
    print(df_wyniki.to_string(index=False))
    
    return df_wyniki

def analiza_skalowania(dane):
    """Analiza skalowalności względem liczby klientów i rozmiaru wiadomości."""
    print("\n" + "="*60)
    print("ANALIZA SKALOWALNOŚCI")
    print("="*60)
    
    # Analiza względem liczby klientów
    print("\nWydajność względem liczby klientów:")
    skalowanie_klienci = dane.groupby(['config_num_clients', 'config_language', 'config_implementation']).agg({
        'messages_per_second': 'mean',
        'throughput_mbps': 'mean',
        'cpu_usage_percent': 'mean'
    }).round(2)
    
    print(skalowanie_klienci)
    
    # Analiza względem rozmiaru wiadomości
    print("\nWydajność względem rozmiaru wiadomości (KB):")
    skalowanie_rozmiar = dane.groupby(['config_message_size_kb', 'config_language', 'config_implementation']).agg({
        'messages_per_second': 'mean',
        'throughput_mbps': 'mean',
        'cpu_usage_percent': 'mean'
    }).round(2)
    
    print(skalowanie_rozmiar)
    
    return skalowanie_klienci, skalowanie_rozmiar

def utworz_wykresy(dane):
    """Tworzy wykresy analizy wydajności."""
    print("\nTworzenie wykresów...")
    
    if len(dane) == 0:
        print("❌ Brak danych do utworzenia wykresów!")
        return
    
    # Utworzenie figury z wieloma subplotami
    fig, axes = plt.subplots(2, 3, figsize=(18, 12))
    fig.suptitle('Analiza wydajności benchmarku według języka i implementacji', fontsize=16, fontweight='bold')
    
    try:
        # 1. Porównanie wydajności (wiadomości/s)
        ax1 = axes[0, 0]
        dane_pivot = dane.pivot_table(values='messages_per_second', 
                                      index='config_num_clients', 
                                      columns=['config_language', 'config_implementation'], 
                                      aggfunc='mean')
        
        if not dane_pivot.empty:
            for col in dane_pivot.columns:
                jezyk, impl = col
                etykieta = f"{jezyk.upper()} ({impl})"
                ax1.plot(dane_pivot.index, dane_pivot[col], marker='o', linewidth=2, label=etykieta)
        
        ax1.set_xlabel('Liczba klientów')
        ax1.set_ylabel('Wiadomości na sekundę')
        ax1.set_title('Wydajność względem liczby klientów')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # 2. Przepustowość względem rozmiaru wiadomości
        ax2 = axes[0, 1]
        dane_rozmiar = dane[dane['config_message_size_kb'] > 0]  # Wykluczenie rozmiaru 0
        if not dane_rozmiar.empty:
            dane_pivot2 = dane_rozmiar.pivot_table(values='throughput_mbps', 
                                                   index='config_message_size_kb', 
                                                   columns=['config_language', 'config_implementation'], 
                                                   aggfunc='mean')
            
            if not dane_pivot2.empty:
                for col in dane_pivot2.columns:
                    jezyk, impl = col
                    etykieta = f"{jezyk.upper()} ({impl})"
                    ax2.plot(dane_pivot2.index, dane_pivot2[col], marker='s', linewidth=2, label=etykieta)
        
        ax2.set_xlabel('Rozmiar wiadomości (KB)')
        ax2.set_ylabel('Przepustowość (Mbps)')
        ax2.set_title('Przepustowość względem rozmiaru wiadomości')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        if not dane_rozmiar.empty and dane_rozmiar['config_message_size_kb'].max() > dane_rozmiar['config_message_size_kb'].min():
            ax2.set_xscale('log')
        
        # 3. Użycie CPU
        ax3 = axes[0, 2]
        if len(dane['config_language'].unique()) > 0:
            sns.boxplot(data=dane, x='config_language', y='cpu_usage_percent', 
                        hue='config_implementation', ax=ax3)
        ax3.set_xlabel('Język programowania')
        ax3.set_ylabel('Użycie CPU (%)')
        ax3.set_title('Rozkład użycia CPU')
        
        # 4. Użycie pamięci
        ax4 = axes[1, 0]
        if len(dane['config_language'].unique()) > 0:
            sns.boxplot(data=dane, x='config_language', y='memory_usage_mb', 
                        hue='config_implementation', ax=ax4)
        ax4.set_xlabel('Język programowania')
        ax4.set_ylabel('Użycie pamięci (MB)')
        ax4.set_title('Rozkład użycia pamięci')
        
        # 5. Czas operacji względem liczby wątków
        ax5 = axes[1, 1]
        dane_pivot3 = dane.pivot_table(values='avg_operation_time_us', 
                                       index='config_num_threads', 
                                       columns=['config_language', 'config_implementation'], 
                                       aggfunc='mean')
        
        if not dane_pivot3.empty:
            for col in dane_pivot3.columns:
                jezyk, impl = col
                etykieta = f"{jezyk.upper()} ({impl})"
                ax5.plot(dane_pivot3.index, dane_pivot3[col], marker='^', linewidth=2, label=etykieta)
        
        ax5.set_xlabel('Liczba wątków')
        ax5.set_ylabel('Śr. czas operacji (μs)')
        ax5.set_title('Czas operacji względem liczby wątków')
        ax5.legend()
        ax5.grid(True, alpha=0.3)
        
        # 6. Mapa ciepła korelacji
        ax6 = axes[1, 2]
        metryki_korelacja = ['messages_per_second', 'throughput_mbps', 'cpu_usage_percent', 
                            'memory_usage_mb', 'avg_operation_time_us', 'connection_rate']
        
        polskie_etykiety = ['Wiad./s', 'Przepust.', 'CPU %', 'Pamięć', 'Czas op.', 'Wsp. poł.']
        
        # Sprawdź czy wszystkie kolumny istnieją
        dostepne_metryki = [m for m in metryki_korelacja if m in dane.columns]
        if len(dostepne_metryki) > 1:
            korelacja = dane[dostepne_metryki].corr()
            # Dostosuj etykiety do dostępnych metryk
            etykiety_dostepne = [polskie_etykiety[metryki_korelacja.index(m)] for m in dostepne_metryki]
            korelacja.index = etykiety_dostepne
            korelacja.columns = etykiety_dostepne
            
            sns.heatmap(korelacja, annot=True, cmap='RdYlBu_r', center=0, 
                        square=True, ax=ax6, cbar_kws={'shrink': 0.8})
        ax6.set_title('Mapa korelacji między metrykami')
        
        plt.tight_layout()
        plt.savefig('analiza_benchmarku.png', dpi=300, bbox_inches='tight')
        plt.show()
        print("✅ Wykresy utworzone pomyślnie!")
        
    except Exception as e:
        print(f"❌ Błąd przy tworzeniu wykresów: {str(e)}")
        print("Sprawdź czy dane zawierają wymagane kolumny.")

def analiza_efektywnosci(dane):
    """Analiza efektywności - stosunek wydajności do zużycia zasobów."""
    print("\n" + "="*60)
    print("ANALIZA EFEKTYWNOŚCI")
    print("="*60)
    
    # Obliczenie wskaźników efektywności
    dane['efektywnosc_cpu'] = dane['messages_per_second'] / dane['cpu_usage_percent']
    dane['efektywnosc_pamiec'] = dane['messages_per_second'] / (dane['memory_usage_mb'] / 1000)  # Per GB
    
    efektywnosc = dane.groupby(['config_language', 'config_implementation']).agg({
        'efektywnosc_cpu': 'mean',
        'efektywnosc_pamiec': 'mean',
        'messages_per_second': 'mean',
        'cpu_usage_percent': 'mean',
        'memory_usage_mb': 'mean'
    }).round(2)
    
    efektywnosc.columns = ['Efekt. CPU', 'Efekt. pamięć', 'Wiad./s', 'CPU %', 'Pamięć MB']
    print("Wskaźniki efektywności (wyższa wartość = lepsza efektywność):")
    print(efektywnosc)
    
    return efektywnosc

def porownanie_statystyczne(dane):
    """Wykonuje testy statystyczne porównujące różne konfiguracje."""
    print("\n" + "="*60)
    print("PORÓWNANIE STATYSTYCZNE")
    print("="*60)
    
    # Test t-studenta dla różnic między językami
    rust_dane = dane[dane['config_language'] == 'rust']['messages_per_second'].dropna()
    cpp_dane = dane[dane['config_language'] == 'cpp']['messages_per_second'].dropna()
    
    if len(rust_dane) > 1 and len(cpp_dane) > 1:
        t_stat, p_value = stats.ttest_ind(rust_dane, cpp_dane)
        print(f"Test t-studenta - Rust vs C++:")
        print(f"  Średnia Rust: {rust_dane.mean():.2f} wiad./s")
        print(f"  Średnia C++: {cpp_dane.mean():.2f} wiad./s")
        print(f"  Statystyka t: {t_stat:.4f}")
        print(f"  Wartość p: {p_value:.4f}")
        print(f"  Różnica jest {'statystycznie istotna' if p_value < 0.05 else 'nieistotna'} (α = 0.05)")
    
    # Test dla różnic między implementacjami
    async_dane = dane[dane['config_implementation'] == 'async']['messages_per_second'].dropna()
    threaded_dane = dane[dane['config_implementation'] == 'threaded']['messages_per_second'].dropna()
    
    if len(async_dane) > 1 and len(threaded_dane) > 1:
        t_stat2, p_value2 = stats.ttest_ind(async_dane, threaded_dane)
        print(f"\nTest t-studenta - Async vs Threaded:")
        print(f"  Średnia Async: {async_dane.mean():.2f} wiad./s")
        print(f"  Średnia Threaded: {threaded_dane.mean():.2f} wiad./s")
        print(f"  Statystyka t: {t_stat2:.4f}")
        print(f"  Wartość p: {p_value2:.4f}")
        print(f"  Różnica jest {'statystycznie istotna' if p_value2 < 0.05 else 'nieistotna'} (α = 0.05)")

def rekomendacje(dane):
    """Generuje rekomendacje na podstawie analizy."""
    print("\n" + "="*60)
    print("REKOMENDACJE I WNIOSKI")
    print("="*60)
    
    if len(dane) == 0:
        print("❌ Brak danych do analizy!")
        return
    
    # Znajdź najlepsze konfiguracje (z obsługą pustych danych)
    try:
        najlepsza_wydajnosc = dane.loc[dane['messages_per_second'].idxmax()]
        najlepsza_przepustowosc = dane.loc[dane['throughput_mbps'].idxmax()]
        najnizsza_cpu = dane.loc[dane['cpu_usage_percent'].idxmin()]
        
        print("🏆 NAJLEPSZE KONFIGURACJE:")
        print(f"  Najwyższa wydajność: {najlepsza_wydajnosc['config_language'].upper()} "
              f"({najlepsza_wydajnosc['config_implementation']}) - "
              f"{najlepsza_wydajnosc['messages_per_second']:.0f} wiad./s")
        
        print(f"  Najwyższa przepustowość: {najlepsza_przepustowosc['config_language'].upper()} "
              f"({najlepsza_przepustowosc['config_implementation']}) - "
              f"{najlepsza_przepustowosc['throughput_mbps']:.2f} Mbps")
        
        print(f"  Najniższe użycie CPU: {najnizsza_cpu['config_language'].upper()} "
              f"({najnizsza_cpu['config_implementation']}) - "
              f"{najnizsza_cpu['cpu_usage_percent']:.1f}%")
        
        # Analiza średnich
        srednie = dane.groupby(['config_language', 'config_implementation']).agg({
            'messages_per_second': 'mean',
            'cpu_usage_percent': 'mean',
            'memory_usage_mb': 'mean'
        }).round(2)
        
        print("\n📊 PODSUMOWANIE ŚREDNICH WARTOŚCI:")
        for (jezyk, impl), row in srednie.iterrows():
            print(f"  {jezyk.upper()} ({impl}): {row['messages_per_second']} wiad./s, "
                  f"{row['cpu_usage_percent']}% CPU, {row['memory_usage_mb']} MB RAM")
    
    except Exception as e:
        print(f"❌ Błąd przy generowaniu rekomendacji: {str(e)}")
        print("Sprawdź czy dane zawierają wymagane kolumny i wartości.")

def main():
    """Główna funkcja wykonująca pełną analizę."""
    sciezka_pliku = 'benchmark_results_20250609_234211.csv'
    
    print("ANALIZA WYNIKÓW BENCHMARKU WYDAJNOŚCI")
    print("="*60)
    
    try:
        # Wczytanie danych
        dane = wczytaj_dane(sciezka_pliku)
        
        if len(dane) == 0:
            print("❌ Brak prawidłowych danych do analizy! Sprawdź plik CSV.")
            print("Upewnij się, że kolumna 'success' zawiera wartość 'True' dla udanych testów.")
            return
        
        # Wykonanie analiz
        podstawowe_statystyki(dane)
        analiza_wedlug_jezyka_implementacji(dane)
        analiza_skalowania(dane)
        analiza_efektywnosci(dane)
        porownanie_statystyczne(dane)
        
        # Utworzenie wykresów
        utworz_wykresy(dane)
        
        # Rekomendacje
        rekomendacje(dane)
        
        print(f"\n✅ Analiza zakończona pomyślnie!")
        print(f"📈 Wykresy zapisane jako 'analiza_benchmarku.png'")
        
    except FileNotFoundError:
        print(f"❌ Błąd: Nie znaleziono pliku {sciezka_pliku}")
        print("Upewnij się, że plik znajduje się w tym samym katalogu co skrypt.")
    except pd.errors.EmptyDataError:
        print(f"❌ Błąd: Plik {sciezka_pliku} jest pusty lub nieprawidłowy.")
    except Exception as e:
        print(f"❌ Błąd podczas analizy: {str(e)}")
        print("Sprawdź format pliku CSV i upewnij się, że zawiera wymagane kolumny.")
        porownanie_statystyczne(dane)
        
        # Utworzenie wykresów
        utworz_wykresy(dane)
        
        # Rekomendacje
        rekomendacje(dane)
        
        print(f"\n✅ Analiza zakończona pomyślnie!")
        print(f"📈 Wykresy zapisane jako 'analiza_benchmarku.png'")
        
    except FileNotFoundError:
        print(f"❌ Błąd: Nie znaleziono pliku {sciezka_pliku}")
        print("Upewnij się, że plik znajduje się w tym samym katalogu co skrypt.")
    except pd.errors.EmptyDataError:
        print(f"❌ Błąd: Plik {sciezka_pliku} jest pusty lub nieprawidłowy.")
    except Exception as e:
        print(f"❌ Błąd podczas analizy: {str(e)}")
        print("Sprawdź format pliku CSV i upewnij się, że zawiera wymagane kolumny.")

if __name__ == "__main__":
    main()