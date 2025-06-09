#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Skrypt do analizy wyników benchmarku wydajności
Porównanie języków Rust i C++ w trybach channel i queue
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path
import warnings
warnings.filterwarnings('ignore')

# Konfiguracja wykresów
plt.rcParams['figure.figsize'] = (12, 8)
plt.rcParams['font.size'] = 10
sns.set_style("whitegrid")

class AnalizatorBenchmarku:
    def __init__(self, sciezka_pliku):
        """Inicjalizacja analizatora z wczytaniem danych"""
        self.dane = self.wczytaj_dane(sciezka_pliku)
        self.wyniki_analizy = {}
        
    def wczytaj_dane(self, sciezka_pliku):
        """Wczytanie i wstępna obróbka danych"""
        print(f"Wczytywanie danych z pliku: {sciezka_pliku}")
        
        try:
            dane = pd.read_csv(sciezka_pliku)
            print(f"Pomyślnie wczytano {len(dane)} wierszy danych")
            
            # Usunięcie wierszy z błędami
            dane_czyste = dane[dane['error'].isna() | (dane['error'] == '')]
            print(f"Po usunięciu błędów: {len(dane_czyste)} wierszy")
            
            return dane_czyste
            
        except Exception as e:
            print(f"Błąd podczas wczytywania pliku: {e}")
            return None
    
    def podstawowe_statystyki(self):
        """Podstawowe statystyki opisowe"""
        print("\n" + "="*60)
        print("PODSTAWOWE STATYSTYKI")
        print("="*60)
        
        print(f"Łączna liczba testów: {len(self.dane)}")
        print(f"Języki programowania: {', '.join(self.dane['language'].unique())}")
        print(f"Tryby komunikacji: {', '.join(self.dane['mode'].unique())}")
        print(f"Zakres liczby wątków: {self.dane['threads'].min()} - {self.dane['threads'].max()}")
        print(f"Zakres elementów: {self.dane['items'].min()} - {self.dane['items'].max()}")
        
        # Statystyki wydajności
        print("\nSTATYSTYKI WYDAJNOŚCI:")
        metryki = ['execution_time_sec', 'efficiency_percent', 'mutex_ops_per_sec', 
                  'messages_per_sec', 'peak_memory_mb']
        
        for metryka in metryki:
            if metryka in self.dane.columns:
                print(f"\n{metryka.replace('_', ' ').title()}:")
                print(f"  Średnia: {self.dane[metryka].mean():.4f}")
                print(f"  Mediana: {self.dane[metryka].median():.4f}")
                print(f"  Min: {self.dane[metryka].min():.4f}")
                print(f"  Max: {self.dane[metryka].max():.4f}")
    
    def porownanie_jezykow(self):
        """Porównanie wydajności między językami"""
        print("\n" + "="*60)
        print("PORÓWNANIE JĘZYKÓW PROGRAMOWANIA")
        print("="*60)
        
        porownanie = self.dane.groupby('language').agg({
            'execution_time_sec': ['mean', 'std', 'min', 'max'],
            'efficiency_percent': ['mean', 'std'],
            'mutex_ops_per_sec': ['mean', 'std'],
            'messages_per_sec': ['mean', 'std'],
            'peak_memory_mb': ['mean', 'std']
        }).round(4)
        
        print(porownanie)
        
        # Zapisanie wyników
        self.wyniki_analizy['porownanie_jezykow'] = porownanie
        
        return porownanie
    
    def porownanie_trybow(self):
        """Porównanie wydajności między trybami komunikacji"""
        print("\n" + "="*60)
        print("PORÓWNANIE TRYBÓW KOMUNIKACJI")
        print("="*60)
        
        porownanie = self.dane.groupby('mode').agg({
            'execution_time_sec': ['mean', 'std', 'min', 'max'],
            'efficiency_percent': ['mean', 'std'],
            'mutex_ops_per_sec': ['mean', 'std'],
            'messages_per_sec': ['mean', 'std'],
            'peak_memory_mb': ['mean', 'std']
        }).round(4)
        
        print(porownanie)
        
        # Zapisanie wyników
        self.wyniki_analizy['porownanie_trybow'] = porownanie
        
        return porownanie
    
    def analiza_skalowania(self):
        """Analiza skalowania względem liczby wątków"""
        print("\n" + "="*60)
        print("ANALIZA SKALOWANIA")
        print("="*60)
        
        skalowanie = self.dane.groupby(['language', 'mode', 'threads']).agg({
            'execution_time_sec': 'mean',
            'mutex_ops_per_sec': 'mean',
            'messages_per_sec': 'mean',
            'peak_memory_mb': 'mean'
        }).round(4)
        
        print(skalowanie)
        
        # Analiza współczynnika skalowania
        print("\nWSPÓŁCZYNNIKI SKALOWANIA:")
        for jezyk in self.dane['language'].unique():
            for tryb in self.dane['mode'].unique():
                dane_combo = self.dane[(self.dane['language'] == jezyk) & 
                                     (self.dane['mode'] == tryb)]
                if len(dane_combo) > 1:
                    watki_sorted = dane_combo.sort_values('threads')
                    if len(watki_sorted) >= 2:
                        wydajnosc_min = watki_sorted.iloc[0]['messages_per_sec']
                        wydajnosc_max = watki_sorted.iloc[-1]['messages_per_sec']
                        watki_min = watki_sorted.iloc[0]['threads']
                        watki_max = watki_sorted.iloc[-1]['threads']
                        
                        wspolczynnik = (wydajnosc_max / wydajnosc_min) / (watki_max / watki_min)
                        print(f"  {jezyk} - {tryb}: {wspolczynnik:.2f}")
        
        self.wyniki_analizy['skalowanie'] = skalowanie
        return skalowanie
    
    def najlepsze_konfiguracje(self, top_n=5):
        """Znajdowanie najlepszych konfiguracji"""
        print("\n" + "="*60)
        print("NAJLEPSZE KONFIGURACJE")
        print("="*60)
        
        # Najwyższa wydajność wiadomości na sekundę
        print(f"TOP {top_n} - NAJWYŻSZA WYDAJNOŚĆ (messages_per_sec):")
        top_wydajnosc = self.dane.nlargest(top_n, 'messages_per_sec')[
            ['language', 'mode', 'threads', 'items', 'messages_per_sec', 'execution_time_sec']
        ]
        print(top_wydajnosc.to_string(index=False))
        
        # Najlepsza efektywność
        print(f"\nTOP {top_n} - NAJWYŻSZA EFEKTYWNOŚĆ (efficiency_percent):")
        top_efektywnosc = self.dane.nlargest(top_n, 'efficiency_percent')[
            ['language', 'mode', 'threads', 'items', 'efficiency_percent', 'messages_per_sec']
        ]
        print(top_efektywnosc.to_string(index=False))
        
        # Najniższe zużycie pamięci
        print(f"\nTOP {top_n} - NAJNIŻSZE ZUŻYCIE PAMIĘCI (peak_memory_mb):")
        top_pamiec = self.dane.nsmallest(top_n, 'peak_memory_mb')[
            ['language', 'mode', 'threads', 'items', 'peak_memory_mb', 'messages_per_sec']
        ]
        print(top_pamiec.to_string(index=False))
        
        self.wyniki_analizy['najlepsze_konfiguracje'] = {
            'wydajnosc': top_wydajnosc,
            'efektywnosc': top_efektywnosc,
            'pamiec': top_pamiec
        }
    
    def generuj_wykresy(self, zapisz_pliki=False):
        """Generowanie wykresów analitycznych"""
        print("\n" + "="*60)
        print("GENEROWANIE WYKRESÓW")
        print("="*60)
        
        # Konfiguracja polskich etykiet dla matplotlib
        plt.rcParams['axes.unicode_minus'] = False
        
        # Mapowanie angielskich nazw na polskie
        dane_polskie = self.dane.copy()
        dane_polskie['tryb'] = dane_polskie['mode'].map({'channel': 'Kanał', 'queue': 'Kolejka'})
        dane_polskie['jezyk'] = dane_polskie['language'].map({'Rust': 'Rust', 'C++': 'C++'})
        
        # Wykres 1: Porównanie wydajności języków
        plt.figure(figsize=(14, 10))
        
        plt.subplot(2, 2, 1)
        sns.boxplot(data=dane_polskie, x='jezyk', y='messages_per_sec', hue='tryb')
        plt.title('Porównanie wydajności wiadomości/s')
        plt.ylabel('Wiadomości na sekundę')
        plt.xlabel('Język programowania')
        plt.yscale('log')
        plt.legend(title='Tryb komunikacji')
        
        plt.subplot(2, 2, 2)
        sns.boxplot(data=dane_polskie, x='jezyk', y='execution_time_sec', hue='tryb')
        plt.title('Porównanie czasu wykonania')
        plt.ylabel('Czas wykonania (s)')
        plt.xlabel('Język programowania')
        plt.yscale('log')
        plt.legend(title='Tryb komunikacji')
        
        plt.subplot(2, 2, 3)
        sns.boxplot(data=dane_polskie, x='jezyk', y='peak_memory_mb', hue='tryb')
        plt.title('Porównanie zużycia pamięci')
        plt.ylabel('Szczytowe zużycie pamięci (MB)')
        plt.xlabel('Język programowania')
        plt.legend(title='Tryb komunikacji')
        
        plt.subplot(2, 2, 4)
        sns.scatterplot(data=dane_polskie, x='threads', y='messages_per_sec', 
                       hue='jezyk', style='tryb', s=100)
        plt.title('Skalowanie względem liczby wątków')
        plt.ylabel('Wiadomości na sekundę')
        plt.xlabel('Liczba wątków')
        plt.yscale('log')
        plt.legend(title='Język / Tryb komunikacji')
        
        plt.tight_layout()
        
        if zapisz_pliki:
            plt.savefig('analiza_benchmarku.png', dpi=300, bbox_inches='tight')
            print("Wykres zapisany jako 'analiza_benchmarku.png'")
        
        plt.show()
        
        # Wykres 2: Macierz korelacji
        plt.figure(figsize=(12, 10))
        metryki_numeryczne = ['threads', 'items', 'execution_time_sec', 
                            'efficiency_percent', 'mutex_ops_per_sec', 
                            'messages_per_sec', 'peak_memory_mb']
        
        # Polskie nazwy kolumn dla macierzy korelacji
        nazwy_polskie = {
            'threads': 'Liczba wątków',
            'items': 'Liczba elementów', 
            'execution_time_sec': 'Czas wykonania [s]',
            'efficiency_percent': 'Efektywność [%]',
            'mutex_ops_per_sec': 'Operacje mutex/s',
            'messages_per_sec': 'Wiadomości/s',
            'peak_memory_mb': 'Pamięć szczytowa [MB]'
        }
        
        dane_korelacji = self.dane[metryki_numeryczne].copy()
        dane_korelacji.columns = [nazwy_polskie[col] for col in dane_korelacji.columns]
        
        korelacje = dane_korelacji.corr()
        sns.heatmap(korelacje, annot=True, cmap='RdYlBu_r', center=0,
                   square=True, fmt='.2f', cbar_kws={'label': 'Współczynnik korelacji'})
        plt.title('Macierz korelacji metryk wydajności')
        plt.xticks(rotation=45, ha='right')
        plt.yticks(rotation=0)
        plt.tight_layout()
        
        if zapisz_pliki:
            plt.savefig('korelacje_benchmarku.png', dpi=300, bbox_inches='tight')
            print("Macierz korelacji zapisana jako 'korelacje_benchmarku.png'")
        
        plt.show()
    
    def raport_szczegolowy(self):
        """Generowanie szczegółowego raportu tekstowego"""
        print("\n" + "="*60)
        print("SZCZEGÓŁOWY RAPORT ANALIZY")
        print("="*60)
        
        raport = []
        raport.append("RAPORT ANALIZY BENCHMARKU WYDAJNOŚCI")
        raport.append("="*50)
        raport.append(f"Data analizy: {pd.Timestamp.now().strftime('%Y-%m-%d %H:%M:%S')}")
        raport.append(f"Liczba testów: {len(self.dane)}")
        raport.append("")
        
        # Podsumowanie głównych wyników
        raport.append("GŁÓWNE WNIOSKI:")
        raport.append("-" * 20)
        
        # Najlepszy język
        wydajnosc_srednia = self.dane.groupby('language')['messages_per_sec'].mean()
        najlepszy_jezyk = wydajnosc_srednia.idxmax()
        raport.append(f"• Najwyższa średnia wydajność: {najlepszy_jezyk} "
                     f"({wydajnosc_srednia[najlepszy_jezyk]:.0f} msg/s)")
        
        # Najlepszy tryb
        wydajnosc_tryb = self.dane.groupby('mode')['messages_per_sec'].mean()
        najlepszy_tryb = wydajnosc_tryb.idxmax()
        raport.append(f"• Najwydajniejszy tryb: {najlepszy_tryb} "
                     f"({wydajnosc_tryb[najlepszy_tryb]:.0f} msg/s)")
        
        # Zużycie pamięci
        pamiec_srednia = self.dane.groupby('language')['peak_memory_mb'].mean()
        oszczedny_jezyk = pamiec_srednia.idxmin()
        raport.append(f"• Najniższe zużycie pamięci: {oszczedny_jezyk} "
                     f"({pamiec_srednia[oszczedny_jezyk]:.2f} MB)")
        
        raport_tekst = "\n".join(raport)
        print(raport_tekst)
        
        return raport_tekst
    
    def zapisz_wyniki(self):
        """Zapisanie wyników do plików"""
        print("\n" + "="*60)
        print("ZAPISYWANIE WYNIKÓW")
        print("="*60)
        
        # Zapisanie przetworzonego CSV
        self.dane.to_csv('dane_przetworzone.csv', index=False)
        print("Zapisano: dane_przetworzone.csv")
        
        # Zapisanie statystyk do Excel
        with pd.ExcelWriter('analiza_statystyki.xlsx') as writer:
            if 'porownanie_jezykow' in self.wyniki_analizy:
                self.wyniki_analizy['porownanie_jezykow'].to_excel(
                    writer, sheet_name='Porównanie języków')
            
            if 'porownanie_trybow' in self.wyniki_analizy:
                self.wyniki_analizy['porownanie_trybow'].to_excel(
                    writer, sheet_name='Porównanie trybów')
            
            if 'skalowanie' in self.wyniki_analizy:
                self.wyniki_analizy['skalowanie'].to_excel(
                    writer, sheet_name='Skalowanie')
        
        print("Zapisano: analiza_statystyki.xlsx")
    
    def uruchom_pelna_analize(self, zapisz_pliki=False):
        """Uruchomienie pełnej analizy"""
        print("ROZPOCZYNANIE PEŁNEJ ANALIZY BENCHMARKU")
        print("="*60)
        
        if self.dane is None:
            print("BŁĄD: Nie można przeprowadzić analizy - brak danych")
            return
        
        try:
            # Kolejne etapy analizy
            self.podstawowe_statystyki()
            self.porownanie_jezykow()
            self.porownanie_trybow()
            self.analiza_skalowania()
            self.najlepsze_konfiguracje()
            self.generuj_wykresy(zapisz_pliki)
            self.raport_szczegolowy()
            
            if zapisz_pliki:
                self.zapisz_wyniki()
            
            print("\n" + "="*60)
            print("ANALIZA ZAKOŃCZONA POMYŚLNIE")
            print("="*60)
            
        except Exception as e:
            print(f"BŁĄD podczas analizy: {e}")
            import traceback
            traceback.print_exc()


def main():
    """Funkcja główna"""
    # Ścieżka do pliku z danymi
    sciezka_pliku = 'benchmark_results_20250609_221720.csv'
    
    # Sprawdzenie czy plik istnieje
    if not Path(sciezka_pliku).exists():
        print(f"BŁĄD: Nie znaleziono pliku {sciezka_pliku}")
        print("Upewnij się, że plik znajduje się w tym samym katalogu co skrypt.")
        return
    
    # Utworzenie analizatora i uruchomienie analizy
    analizator = AnalizatorBenchmarku(sciezka_pliku)
    
    # Uruchomienie pełnej analizy
    # Ustaw zapisz_pliki=True aby zapisać wykresy i pliki wyników
    analizator.uruchom_pelna_analize(zapisz_pliki=True)


if __name__ == "__main__":
    main()